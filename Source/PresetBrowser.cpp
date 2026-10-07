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
    nameCap.setFont (look.uiFont (13.0f));
    addAndMakeVisible (nameCap);
    nameBox.setColour (juce::TextEditor::backgroundColourId, NanoColors::comboBg);
    nameBox.setColour (juce::TextEditor::textColourId, NanoColors::text);
    nameBox.setColour (juce::TextEditor::outlineColourId, NanoColors::border);
    nameBox.setColour (juce::TextEditor::focusedOutlineColourId, NanoColors::accent);
    nameBox.setFont (look.uiFont (14.0f));
    nameBox.setText (nextDefaultName(), false);
    nameBox.selectAll();
    addAndMakeVisible (nameBox);

    closeBtn.onClick = [&] { if (onClose) onClose(); };
    saveBtn.onClick = [&]
    {
        juce::String name = nameBox.getText().trim();
        if (name.isEmpty()) name = nextDefaultName();
        juce::StringArray tags { "User", saveCat.getText() };
        juce::File saved = PresetBank::saveUser (name, tags,
                                                PresetBank::capture (proc.apvts));
        if (saved.existsAsFile())
        {
            activeCat = "User";
            refreshCatButtons();
            rebuildRows();
            nameBox.setText (nextDefaultName(), false);
            nameBox.selectAll();
            currentUserFile = saved.getFileName();
            showStatus ("Saved '" + name + "'");
            if (onPresetChanged) onPresetChanged();
        }
        else showStatus ("Save failed");
    };
    overBtn.onClick = [&]
    {
        int r = list.getSelectedRow();
        if (r < 0 || r >= (int) rows.size()) { showStatus ("Select a user preset"); return; }
        int id = rows[(size_t) r];
        if (! PresetBank::isUserRow (id)) { showStatus ("Factory presets are read-only"); return; }
        // Copy out first: rebuildRows() below replaces the users vector.
        juce::File ufile = users[(size_t) PresetBank::userRowIndex (id)].file;
        juce::String uname = users[(size_t) PresetBank::userRowIndex (id)].name;
        juce::StringArray utags = users[(size_t) PresetBank::userRowIndex (id)].tags;
        if (PresetBank::overwriteUser (ufile, PresetBank::capture (proc.apvts), utags))
        {
            rebuildRows();
            showStatus ("Overwrote '" + uname + "'");
            if (onPresetChanged) onPresetChanged();
        }
        else showStatus ("Overwrite failed");
    };
    delBtn.onClick = [&]
    {
        int r = list.getSelectedRow();
        if (r < 0 || r >= (int) rows.size()) return;
        int id = rows[(size_t) r];
        if (! PresetBank::isUserRow (id)) { showStatus ("Factory presets are read-only"); return; }
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
        }
    };
    for (auto* b : { &saveBtn, &overBtn, &delBtn }) addAndMakeVisible (b);
    addAndMakeVisible (closeBtn);
    addAndMakeVisible (status);
    status.setColour (juce::Label::textColourId, NanoColors::dim);
    status.setFont (look.uiFont (12.0f));
    status.setJustificationType (juce::Justification::centredLeft);
    rebuildRows();
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        currentFactory = proc.getCurrentProgram();
        currentUserFile = proc.getCurrentUserPreset();
        nameBox.setText (nextDefaultName(), false);
        rebuildRows();
        search.grabKeyboardFocus();
    }
}

juce::String PresetBrowser::nextDefaultName()
{
    std::set<juce::String> taken;
    for (auto& u : PresetBank::scanUser()) taken.insert (u.name);
    int n = 1;
    while (taken.count ("My Preset " + juce::String (n)) > 0) ++n;
    return "My Preset " + juce::String (n);
}

void PresetBrowser::refreshCatButtons()
{
    for (auto& b : catBtns)
    {
        bool on = (b->getProperties()["cat"].toString() == activeCat);
        b->setColour (juce::TextButton::textColourOffId,
                      on ? NanoColors::accentHi : NanoColors::dim);
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
    return PresetBank::get (id).name;
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
    list.setBounds (panel.getX() + 16, panel.getY() + 114,
                    panel.getWidth() - 32, panel.getHeight() - 114 - 86);
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
