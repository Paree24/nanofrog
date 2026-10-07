#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/Voice.h"
#include "DSP/Effects.h"

#include "PresetBank.h"

// Factory presets live in FactoryBank/NanoFrogFactory.json (data, not code);
// user presets are single JSON files in the user dir. See PresetBank.h.

class NanoFrogProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    NanoFrogProcessor();
    ~NanoFrogProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NanoFrog"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }
    int getNumPrograms() override { return PresetBank::count(); }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}
    bool hasPreset() const { return true; }
    // User preset loaded via the browser (not part of the host program list).
    bool loadUserPreset (const juce::File& file);
    juce::String getCurrentUserPreset() const { return currentUser; }
    // Drops the user name tag (e.g. its file was deleted); sound untouched.
    void forgetUserPreset() { currentUser.clear(); }
    // One-click flavour re-roll (message thread only): jitters continuous
    // params a few percent, plus small musical steps on osc selectors.
    // Bass/keys/leads/percussive voices keep their envelopes (category
    // comes from the current preset's tags).
    void mutateCurrentPatch();
    // Immediate factory load for the browser (message thread only; hosts must
    // use setCurrentProgram, which defers to the message thread).
    void loadFactoryPreset (int index)
    {
        if (index >= 0 && index < PresetBank::count())
        {
            pendingPreset.store (-1);
            applyPreset (index);
        }
    }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // for UI meters
    float getOutputLevel() const { return std::max (meterL.load(), meterR.load()); }
    float getOutputLevelL() const { return meterL.load(); }
    float getOutputLevelR() const { return meterR.load(); }
    juce::StringArray getFactoryNames() const;

    // ---- imported user waves: single file or folder bank (message thread
    // writes, audio reads). Single import = bank of 1. Folder paths are
    // remembered and rescanned on state load. ----
    static constexpr int kUserLen = 2048;
    static constexpr int kBankMax = 64;
    bool importUserWave (const float* monoSrc, int numFrames, const juce::String& name);
    int importUserFolder (const juce::String& folderPath); // waves loaded, -1 on error
    static bool makeSingleCycle (const float* monoSrc, int numFrames, float* dst2048);
    bool hasUserWave() const { return bankCount.load() > 0; }
    int getUserBankCount() const { return bankCount.load(); }
    juce::String getUserWaveName() const { return userDisplayName; }
    juce::String getUserBankEntryName (int i) const;
    juce::String getUserWavePath() const { return userWavePath; }
    juce::String getUserFolderPath() const { return userFolderPath; }

    // ---- oscilloscope tap (lock-free, audio pushes / UI pulls) ----
    void pullScopeData (float* dst, int& numAvailable);
    int getScopeCapacity() const { return 8192; }

private:
    void applyPreset (int index);
    void handleAsyncUpdate() override;
    std::atomic<int> pendingPreset { -1 };
    juce::String pendingUserRestore; // guarded by pathLock (may be set off-thread)
    juce::String currentUser; // user preset file name when in user mode (message thread)
    VoiceParams collectVoiceParams (int timbre);
    void handleMidi (const juce::MidiMessage& m);
    int allocateVoice (int timbre, int note, int poly);
    float egTimeFromKnob (float v01, float maxSec);

    static constexpr int MaxVoices = 8;
    NanoVoice voices[2][MaxVoices];
    float tmixSm[2] = { 1.0f, 0.0f }; // equal-power T1/T2 crossfade gains
    // arp state
    struct ArpState {
        bool active = false; int step = 0; double nextStepSample = 0;
        std::vector<int> held; int currentNote = -1;
    } arp;
    std::vector<int> heldNotes; // raw held (before arp)
    int currentPreset = 0;
    double fs = 44100.0;
    float bendSemi = 0.0f, modWheel = 0.0f, sustainPedal = 0.0f;
    int sustainHeld[128] = { 0 };
    ModFx modfx; TempoDelay delay; TwoBandEq eq;
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f };
    float outSmoothL = 0.0f, outSmoothR = 0.0f;
    double hostBpm = 120.0;
    double effBpm = 120.0; // per-block tempo: host when available, else song_tempo
    bool prevArpOn = false; // edge detector: arp-off must release arp voices
    double arpOffCountdown = 0.0; // arp gate note-off timer (samples)

    float bankBuf[2][kBankMax][kUserLen] = {};
    juce::String bankNames[kBankMax];
    std::atomic<int> bankActive { 0 };
    std::atomic<int> bankCount { 0 };
    juce::String userDisplayName;   // folder name or file name (message thread)
    juce::String userWavePath;      // remembered single-file path (message thread)
    juce::String userFolderPath;    // remembered folder path (message thread)
    juce::CriticalSection pathLock; // guards pendingFolderRescan (setStateInformation thread)
    juce::String pendingFolderRescan;

    juce::AbstractFifo scopeFifo { 8192 };
    float scopeBuf[8192] = {};
    void pushScopeData (const float* L, const float* R, int n);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NanoFrogProcessor)
};
