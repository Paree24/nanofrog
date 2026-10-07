#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "NanoLook.h"
#include "PresetBank.h"

class NanoFrogProcessor;

// Full preset browser overlay: search (name + tags), category filter,
// click-to-audition list, user preset save/overwrite/delete.
// Factory bank is data (JSON); user presets are single JSON files on disk.
class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::TextEditor::Listener
{
public:
    explicit PresetBrowser (NanoFrogProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void visibilityChanged() override;

    std::function<void()> onClose;
    std::function<void()> onPresetChanged; // fired after any load
    int getRowCount() const { return (int) rows.size(); } // test hook
    void selectRowSync (int r) // test hook: deterministic row load
    {
        if (r < 0 || r >= (int) rows.size()) return;
        list.selectRow (r, false, true); // fires selectedRowsChanged inline
        loadRow (r); // plus direct load in case selection was already current
    }
    void clickDel() { delBtn.triggerClick(); } // test hook (async dispatch)

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int lastRow) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void textEditorTextChanged (juce::TextEditor&) override;

    void rebuildRows();
    void refreshCatButtons();
    void loadRow (int r);
    void showStatus (const juce::String& t);
    juce::String rowName (int r) const;
    juce::String rowTags (int r) const;

    NanoFrogProcessor& proc;
    NanoLook look;
    juce::TextEditor search;
    juce::ListBox list { "browser", this };
    juce::TextButton closeBtn { "X" };
    juce::TextButton saveBtn { "SAVE" };
    juce::TextButton overBtn { "OVER" };
    juce::TextButton delBtn { "DEL" };
    juce::ComboBox saveCat;
    juce::Label nameCap;
    juce::TextEditor nameBox;
    juce::String nextDefaultName();
    juce::Label status;
    std::vector<std::unique_ptr<juce::TextButton>> catBtns;
    std::vector<int> rows; // factory index, or PresetBank::userRow(u)
    std::vector<PresetBank::UserPreset> users; // row-id resolution cache
    juce::String activeCat = "All";
    int currentFactory = -1;
    juce::String currentUserFile;
    bool suppressSelect = false;
    juce::Rectangle<int> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};
