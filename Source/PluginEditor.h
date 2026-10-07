#pragma once
#include <utility>
#include <juce_audio_formats/juce_audio_formats.h>
#include "NanoLook.h"
#include "PresetBrowser.h"

class NanoFrogProcessor;

// Radio-style segmented strip bound to a choice parameter (filter type,
// LFO waves). Clicking pushes the parameter; sync() pulls (host automation).
struct SegStrip : juce::Component
{
    SegStrip (NanoFrogProcessor& proc, const juce::String& pid,
              const juce::StringArray& labels, bool redActive, int cols);
    void sync();

    NanoFrogProcessor& proc;
    juce::String pid;
    std::vector<juce::TextButton*> btns;
};

class FrogContent : public juce::Component, public juce::Timer
{
public:
    static constexpr int baseW = 1280, baseH = 900;

    explicit FrogContent (NanoFrogProcessor&);
    ~FrogContent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    // Test hooks (headless layout test, LESSONS.md #17).
    int getSectionCount() const;
    juce::ComboBox& getPresetBox() { return presetBox; }
    void selectPage (int i);
    bool isPageVisible (int i) const;
    void openBrowser();
    void closeBrowser();
    PresetBrowser* getBrowser() { return browser.get(); } // test hook
    bool selfTestControls();
    void driveAsyncControls();
    bool verifyAsyncControls();
    std::vector<std::pair<juce::String, float>> asyncOrig;

private:
    struct KnobCell { juce::Slider* slider; juce::String pid; juce::Label* value; juce::String last; };
    struct CtrlCell { juce::Component* comp; juce::String pid; };

    NanoFrogProcessor& proc;
    NanoLook look;
    juce::Label presetLabel;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" };
    juce::ComboBox presetBox;
    juce::TextButton browseBtn { "BROWSE" };
    std::unique_ptr<PresetBrowser> browser;
    juce::TextButton tabT1 { "TIMBRE 1" }, tabT2 { "TIMBRE 2" };
    int curPage = 0;
    juce::Component page0, page1;
    juce::Component* buildParent = nullptr;
    int buildTimbre = 0;
    juce::Label waveNameLabel;
    juce::Label waveNameLabel2;
    juce::TextButton importBtn { "IMPORT WAV" };
    juce::TextButton folderBtn { "FOLDER" };
    juce::TextButton importBtn2 { "IMPORT WAV" };
    juce::TextButton folderBtn2 { "FOLDER" };
    juce::TextButton impBtnO2[2];
    juce::TextButton fldBtnO2[2];
    juce::String statusText;
    juce::uint32 statusHoldMs = 0;
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton* syncBtns[2][2] = {};
    juce::TextButton* ringBtns[2][2] = {};

    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::pair<juce::GroupComponent*, juce::Rectangle<int>>> secLayout;
    std::vector<KnobCell> knobCells;
    std::vector<CtrlCell> choiceCells;
    std::vector<CtrlCell> toggleCells;
    juce::TextButton* arpOffBtn = nullptr;
    juce::Slider* mixSlider = nullptr; // header T1/T2 crossfade knob (no readout cell)
    std::vector<SegStrip*> strips;
    std::vector<juce::Component*> scopeViews;
    std::vector<juce::Component*> meterViews;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> cAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> bAtt;

    juce::Slider* addKnob (juce::Component* parent, const juce::String& pid,
                           const juce::String& label, int x, int y, int size = 44,
                           int labelW = 56);
    juce::ComboBox* addChoice (juce::Component* parent, const juce::String& pid,
                               const juce::String& label, int x, int y, int w = 104);
    juce::ToggleButton* addToggle (juce::Component* parent, const juce::String& pid,
                                   const juce::String& label, int x, int y, int w, int h = 26);
    juce::GroupComponent* addSection (const juce::String& title, int x, int y, int w, int h);
    SegStrip* addSeg (juce::Component* parent, const juce::String& pid,
                      const juce::StringArray& labels, bool red, int x, int y,
                      int bw, int bh, int cols);
    juce::TextButton* addModButton (juce::Component* parent, const juce::String& text,
                                    bool isSync, int slot, int x, int y, int w, int h);
    void setOscModBit (int timbre, bool isSync);
    void syncModButtons (int timbre);
    void refreshPresetBox();
    void refreshValues();
    void refreshWaveLabel (juce::Label& lb, const juce::String& digiPid);
    void refreshStrips();
    void importWaveFile (int timbre, int osc);
    void importWaveFolder (int timbre, int osc);
    void selectUserWave (int timbre, int osc);
    void showStatus (const juce::String& t);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrogContent)
};

// Resizable host editor: fixed-layout content scaled to fit (aspect kept,
// letterboxed), with a corner resizer. Content stays at baseW x baseH.
class NanoFrogEditor : public juce::AudioProcessorEditor
{
public:
    explicit NanoFrogEditor (NanoFrogProcessor&);
    ~NanoFrogEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    FrogContent& getContent() { return *content; }

private:
    NanoFrogProcessor& proc;
    std::unique_ptr<FrogContent> content;
    juce::ResizableCornerComponent corner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NanoFrogEditor)
};
