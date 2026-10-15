#include "PresetBrowser.h"
#include "PluginProcessor.h"
#include <set>

PresetBrowser::PresetBrowser (NanoFrogProcessor& p)
    : proc (p)
{
    setLookAndFeel (&look);

    search.setTextToShowWhenEmpty ("Search name or tag (reese, 808, wobble, acid, pad...)",
                                   NanoColors::dim.withAlpha (0.7f));
    search.setColour (juce::TextEditor::backgroundColourId, NanoColors::comboBg);
    search.setColour (juce::TextEditor::textColourId, NanoColors::text);
    search.setColour (juce::TextEditor::outlineColourId, NanoColors::border);
    search.setColour (juce::TextEditor::focusedOutlineColourId, NanoColors::accent);
    search.setFont (look.uiFont (14.0f));
    search.addListener (this);
    addAndMakeVisible (search);

    juce::StringArray cats { "All" };
    cats.addArray (PresetBank::categoryList());
    cats.add ("User");
    for (auto& c : cats)
    {
        // "Seq" keeps the 13-button row from clipping at panel width
        juce::String label = (c == "Sequence") ? juce::String ("Seq") : c;
        auto* btn = new juce::TextButton (label);
        btn->getProperties().set ("cat", c);
        catBtns.emplace_back (btn);
        addAndMakeVisible (btn);
        btn->onClick = [this, c] { activeCat = c; refreshCatButtons(); rebuildRows(); };
    }
    refreshCatButtons();

    list.setRowHeight (26);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);
    auto& sb = list.getVerticalScrollBar();
    sb.setColour (juce::ScrollBar::thumbColourId, NanoColors::accent.withAlpha (0.7f));
    sb.setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    saveCat.addItemList (PresetBank::categoryList(), 1);
    saveCat.setSelectedId (1, juce::dontSendNotification);
    saveCat.setTooltip ("Tag for the saved preset");
    addAndMakeVisible (saveCat);

    nameCap.setText ("NAME", juce::dontSendNotification);
    nameCap.setJustificationType (juce::Justification::centredLeft);
    nameCap.setColour (juce::Label::textColourId, NanoColors::dim);
    setOwnFont (nameCap, look.uiFont (13.0f));
    addAndMakeVisible (nameCap);
    nameBox.setColour (juce::TextEditor::backgroundColourId, NanoColors::comboBg);
    nameBox.setColour (juce::TextEditor::textColourId, NanoColors::text);
    nameBox.setColour (juce::TextEditor::outlineColourId, NanoColors::border);
    nameBox.setColour (juce::TextEditor::focusedOutlineColourId, NanoColors::accent);
    nameBox.setFont (look.uiFont (14.0f));
    nameBox.setTextToShowWhenEmpty ("Preset name...", NanoColors::dim.withAlpha (0.7f));
    nameBox.onTextChange = [this] { setOverwriteArmed (false); }; // edited name: disarm
    nameBox.onReturnKey = [this] { clickSave(); };
    addAndMakeVisible (nameBox);

    closeBtn.onClick = [&] { if (onClose) onClose(); };
    saveBtn.onClick = [&] { clickSave(); };
    overBtn.onClick = [&] { overwriteSelectedRow(); };
    delBtn.onClick = [&] { deleteSelectedRow(); };
    for (auto* b : { &saveBtn, &overBtn, &delBtn }) addAndMakeVisible (b);
    addAndMakeVisible (closeBtn);
    addAndMakeVisible (status);
    status.setColour (juce::Label::textColourId, NanoColors::dim);
    setOwnFont (status, look.uiFont (12.0f));
    status.setJustificationType (juce::Justification::centredLeft);
    rebuildRows();
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        PresetBank::rescanShadows(); // pick up shadows written elsewhere
        currentFactory = proc.getCurrentProgram();
        currentUserFile = proc.getCurrentUserPreset();
        nameBox.setText (currentPresetName(), false); // prefill: current sound's name
        setOverwriteArmed (false);
        rebuildRows();
        search.grabKeyboardFocus();
    }
}

juce::String PresetBrowser::currentPresetName() const
{
    juce::String u = proc.getCurrentUserPreset();
    if (u.isNotEmpty()) return u.upToLastOccurrenceOf (".nbpreset", false, true);
    int f = proc.getCurrentProgram();
    if (f > 0 && f < PresetBank::count()) return PresetBank::get (f).name; // never prefill "Init"
    return {};
}

// SAVE (inline, no pop-up): saves under the name field. An existing user
// preset needs a second click ("OVERWRITE?"); editing the name disarms it.
void PresetBrowser::clickSave()
{
    juce::String name = nameBox.getText().trim();
    if (name.isEmpty()) { nameBox.grabKeyboardFocus(); showStatus ("Type a name first"); return; }
    juce::File target = PresetBank::userFileFor (name);
    bool exists = target.existsAsFile();
    if (exists && ! overwriteArmed)
    {
        setOverwriteArmed (true);
        showStatus ("'" + name + "' exists: click again to overwrite");
        return;
    }
    setOverwriteArmed (false);
    juce::StringArray tags { "User", saveCat.getText() };
    auto values = PresetBank::capture (proc.apvts);
    juce::File saved = exists ? (PresetBank::overwriteUser (target, values, tags) ? target : juce::File())
                              : PresetBank::saveUser (name, tags, values);
    if (! saved.existsAsFile()) { showStatus ("Save failed"); return; }
    proc.markUserPreset (saved.getFileName()); // the saved file is now the current sound
    currentFactory = -1;
    currentUserFile = saved.getFileName();
    activeCat = "User";
    search.setText ({}, false);
    refreshCatButtons();
    rebuildRows();
    showStatus ((exists ? "Overwrote '" : "Saved '") + name + "'");
    if (onPresetChanged) onPresetChanged();
}

void PresetBrowser::setOverwriteArmed (bool on)
{
    overwriteArmed = on;
    saveBtn.setButtonText (on ? "OVERWRITE?" : "SAVE");
    saveBtn.setToggleState (on, juce::dontSendNotification); // lit while armed
}

void PresetBrowser::refreshCatButtons()
{
    for (auto& b : catBtns)
    {
        bool on = (b->getProperties()["cat"].toString() == activeCat);
        b->setToggleState (on, juce::dontSendNotification); // lit like other active buttons
        b->setColour (juce::TextButton::textColourOffId, NanoColors::dim);
    }
}

void PresetBrowser::rebuildRows()
{
    users = PresetBank::scanUser();
    rows = PresetBank::filter (search.getText().trim(), activeCat);
    list.updateContent();
    // highlight current without triggering a load
    suppressSelect = true;
    list.deselectAllRows();
    for (int r = 0; r < (int) rows.size(); ++r)
    {
        int id = rows[(size_t) r];
        bool cur = (! PresetBank::isUserRow (id) && id == currentFactory)
            || (PresetBank::isUserRow (id)
                && users[(size_t) PresetBank::userRowIndex (id)].file.getFileName()
                       == currentUserFile);
        if (cur) { list.selectRow (r, false, true); list.scrollToEnsureRowIsOnscreen (r); break; }
    }
    suppressSelect = false;
    list.repaint();
}

int PresetBrowser::getNumRows() { return (int) rows.size(); }

juce::String PresetBrowser::rowName (int r) const
{
    int id = rows[(size_t) r];
    if (PresetBank::isUserRow (id))
        return users[(size_t) PresetBank::userRowIndex (id)].name;
    const auto& pr = PresetBank::get (id);
    return pr.shadowed ? pr.name + " *" : pr.name; // '*' = user-overwritten slot
}

juce::String PresetBrowser::rowTags (int r) const
{
    int id = rows[(size_t) r];
    if (PresetBank::isUserRow (id))
        return users[(size_t) PresetBank::userRowIndex (id)].tags.joinIntoString (", ");
    return PresetBank::get (id).tags.joinIntoString (", ");
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) rows.size()) return;
    int id = rows[(size_t) row];
    bool isUser = PresetBank::isUserRow (id);
    bool isCurrent = (! isUser && id == currentFactory)
        || (isUser && users[(size_t) PresetBank::userRowIndex (id)].file.getFileName()
                        == currentUserFile);
    if (selected)
    {
        g.setColour (NanoColors::accent.withAlpha (0.22f));
        g.fillRoundedRectangle (2, 1, w - 4, h - 2, 3.0f);
    }
    g.setColour (isCurrent ? NanoColors::accentHi : NanoColors::dim.withAlpha (0.85f));
    g.setFont (look.uiFont (12.0f));
    g.drawText (isUser ? juce::String ("USR") : juce::String (id).paddedLeft ('0', 3),
                10, 0, 44, h, juce::Justification::centredLeft, false);
    g.setColour (isCurrent ? NanoColors::accentHi : NanoColors::text);
    g.setFont (look.uiFont (13.5f));
    g.drawText (rowName (row), 58, 0, w - 66, h, juce::Justification::centredLeft, true);
    g.setColour (NanoColors::dim.withAlpha (0.7f));
    g.setFont (look.uiFont (11.5f));
    g.drawText (rowTags (row), 58, 0, w - 74, h, juce::Justification::centredRight, true);
}

bool PresetBrowser::overwriteSelectedRow()
{
    int r = list.getSelectedRow();
    if (r < 0 || r >= (int) rows.size()) { showStatus ("Select a user preset"); return false; }
    int id = rows[(size_t) r];
    if (! PresetBank::isUserRow (id))
    {
        // Factory slot: write a shadow copy (shipped bank untouched, DEL restores).
        juce::String fname = PresetBank::get (id).name;
        if (PresetBank::overwriteFactory (id, PresetBank::capture (proc.apvts)))
        {
            rebuildRows();
            showStatus ("Overwrote factory '" + fname + "' (DEL restores)");
            if (onPresetChanged) onPresetChanged();
            return true;
        }
        showStatus ("Overwrite failed");
        return false;
    }
    // Copy out first: rebuildRows() below replaces the users vector.
    juce::File ufile = users[(size_t) PresetBank::userRowIndex (id)].file;
    juce::String uname = users[(size_t) PresetBank::userRowIndex (id)].name;
    juce::StringArray utags = users[(size_t) PresetBank::userRowIndex (id)].tags;
    if (PresetBank::overwriteUser (ufile, PresetBank::capture (proc.apvts), utags))
    {
        rebuildRows();
        showStatus ("Overwrote '" + uname + "'");
        if (onPresetChanged) onPresetChanged();
        return true;
    }
    showStatus ("Overwrite failed");
    return false;
}

bool PresetBrowser::deleteSelectedRow()
{
    int r = list.getSelectedRow();
    if (r < 0 || r >= (int) rows.size()) return false;
    int id = rows[(size_t) r];
    if (! PresetBank::isUserRow (id))
    {
        // Factory slot: DEL only removes a user overwrite (restores shipped).
        juce::String fname = PresetBank::get (id).name;
        if (PresetBank::restoreFactory (id))
        {
            rebuildRows();
            showStatus ("Restored factory '" + fname + "'");
            if (onPresetChanged) onPresetChanged();
            return true;
        }
        showStatus ("Factory presets can't be deleted");
        return false;
    }
    // Copy out first: rebuildRows() below replaces the users vector.
    juce::File ufile = users[(size_t) PresetBank::userRowIndex (id)].file;
    juce::String uname = users[(size_t) PresetBank::userRowIndex (id)].name;
    bool wasCurrent = (currentUserFile == ufile.getFileName());
    if (PresetBank::deleteUser (ufile))
    {
        if (wasCurrent)
        {
            currentUserFile.clear();
            proc.forgetUserPreset(); // sound stays, name no longer ghosts
        }
        rebuildRows();
        showStatus ("Deleted '" + uname + "'");
        if (onPresetChanged) onPresetChanged();
        return true;
    }
    return false;
}

void PresetBrowser::loadRow (int r)
{
    // Copy out first: nothing here rebuilds, but never hold vector refs
    // across processor calls as a rule.
    int id = rows[(size_t) r];
    bool isUser = PresetBank::isUserRow (id);
    juce::File ufile;
    if (isUser) ufile = users[(size_t) PresetBank::userRowIndex (id)].file;
    if (isUser)
    {
        if (proc.loadUserPreset (ufile))
        {
            currentFactory = -1;
            currentUserFile = ufile.getFileName();
        }
    }
    else
    {
        proc.loadFactoryPreset (id); // message thread: applies immediately
        currentFactory = id;
        currentUserFile.clear();
    }
    if (onPresetChanged) onPresetChanged();
    list.repaint();
}

void PresetBrowser::selectedRowsChanged (int)
{
    if (suppressSelect) return;
    int r = list.getSelectedRow();
    if (r < 0 || r >= (int) rows.size()) return;
    loadRow (r);
}

void PresetBrowser::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
{
    if (onClose) onClose();
}

void PresetBrowser::textEditorTextChanged (juce::TextEditor&) { rebuildRows(); }

void PresetBrowser::showStatus (const juce::String& t) { status.setText (t, juce::dontSendNotification); }

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f)); // scrim
    g.setColour (NanoColors::panel);
    g.fillRoundedRectangle (panel.toFloat(), 6.0f);
    g.setColour (NanoColors::border);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 6.0f, 1.0f);
    g.setColour (NanoColors::header);
    g.fillRoundedRectangle ((float) panel.getX(), (float) panel.getY(),
                            (float) panel.getWidth(), 34, 6.0f);
    g.fillRect (panel.getX(), panel.getY() + 28, panel.getWidth(), 6);
    g.setColour (NanoColors::accent);
    g.setFont (look.titleFont (17.0f));
    g.drawText ("PRESET BROWSER", panel.getX() + 16, panel.getY(), 300, 34,
                juce::Justification::centredLeft, false);
    g.setColour (NanoColors::dim);
    g.setFont (look.uiFont (12.0f));
    g.drawText (juce::String (rows.size()) + " presets", panel.getRight() - 216,
                panel.getY(), 160, 34, juce::Justification::centredRight, false);
}

void PresetBrowser::resized()
{
    setBounds (0, 0, 1280, 900);
    panel = juce::Rectangle<int> (280, 150, 720, 600);
    closeBtn.setBounds (panel.getRight() - 44, panel.getY() + 5, 36, 24);
    search.setBounds (panel.getX() + 16, panel.getY() + 46, panel.getWidth() - 32, 28);
    int nb = (int) catBtns.size();
    int bw = (panel.getWidth() - 32 - (nb - 1) * 6) / nb;
    for (int b = 0; b < nb; ++b)
        catBtns[(size_t) b]->setBounds (panel.getX() + 16 + b * (bw + 6),
                                        panel.getY() + 82, bw, 24);
    int listH = (panel.getHeight() - 114 - 86) / 26 * 26; // whole rows only
    list.setBounds (panel.getX() + 16, panel.getY() + 114, panel.getWidth() - 32, listH);
    nameCap.setBounds (panel.getX() + 16, panel.getBottom() - 72, 52, 26);
    nameBox.setBounds (panel.getX() + 74, panel.getBottom() - 72,
                       panel.getWidth() - 74 - 16, 26);
    int fy = panel.getBottom() - 40;
    saveBtn.setBounds (panel.getX() + 16, fy, 84, 26);
    overBtn.setBounds (panel.getX() + 106, fy, 84, 26);
    delBtn.setBounds (panel.getX() + 196, fy, 70, 26);
    saveCat.setBounds (panel.getX() + 272, fy, 130, 26);
    status.setBounds (panel.getX() + 410, fy, panel.getRight() - 12 - (panel.getX() + 410), 26);
}

void PresetBrowser::mouseDown (const juce::MouseEvent& e)
{
    if (! panel.contains (e.getPosition()) && onClose) onClose(); // scrim closes
}
