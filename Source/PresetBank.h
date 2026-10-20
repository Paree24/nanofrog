#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Preset storage is data, never compiled code: the factory bank ships as
// FactoryBank/NanoFrogFactory.json (bundle Resources, or user/config dirs)
// and user presets are single JSON files on disk. A compiled-in Init voice
// exists only as an emergency fallback so the plugin is never dead.
//
// Values are raw (denormalised) parameter values in kParamIds order; files
// carry their own param_ids array and are mapped by id (unknown ids ignored,
// missing ids keep current/default values).
struct NanoPreset
{
    juce::String name;
    std::vector<float> values; // kParamIds order
    juce::StringArray tags;
    bool shadowed = false; // factory slot overwritten by the user (see overwriteFactory)
};

namespace PresetBank
{
    constexpr int kParamCount = 186;
    extern const char* const kParamIds[kParamCount];

    void ensureLoaded(); // idempotent: JSON bank, else Init fallback
    bool isFallback();
    void installFallback (const std::vector<float>& defaultValues);
    int count();
    const NanoPreset& get (int i);
    int findByName (const juce::String& name);

    // category "" or "All" = any factory category; "User" = user presets only.
    // query matches all words against name + tags (case-insensitive).
    // Returned ids: >=0 factory index, <0 user index encoded as -1 - u.
    static inline int userRow (int u) { return -1 - u; }
    static inline bool isUserRow (int row) { return row < 0; }
    static inline int userRowIndex (int row) { return -1 - row; }
    std::vector<int> filter (const juce::String& query, const juce::String& category);
    juce::StringArray categoryList(); // for browser buttons (excludes User)

    struct UserPreset
    {
        juce::String name;
        juce::File file;
        juce::StringArray tags;
    };
    juce::File userDir();
    std::vector<UserPreset> scanUser();
    juce::String sanitise (const juce::String& name);
    // Returns the written file (null File on failure). Dedupes the name.
    juce::File saveUser (const juce::String& name, const juce::StringArray& tags,
                         const std::vector<float>& values);
    bool overwriteUser (const juce::File& file, const std::vector<float>& values,
                        const juce::StringArray& tags);
    bool deleteUser (const juce::File& file);
    bool readUser (const juce::File& file, NanoPreset& out);
    // The file SAVE would write for this name (it may already exist).
    juce::File userFileFor (const juce::String& name);

    // Factory overwrites (drumsynth model): the shipped bank is never
    // modified. OVER on a factory slot writes a shadow copy to
    // <userDir>/Factory/NNN Name.nbpreset that replaces the slot's values
    // from then on (row marked '*'); restoreFactory deletes the shadow and
    // brings back the shipped values.
    juce::File factoryShadowDir();
    bool overwriteFactory (int index, const std::vector<float>& values);
    bool restoreFactory (int index); // true if a shadow existed
    void rescanShadows();

    // APVTS glue (canonical order = kParamIds)
    std::vector<float> capture (juce::AudioProcessorValueTreeState& apvts);
    void apply (juce::AudioProcessorValueTreeState& apvts, const std::vector<float>& values);
    void resetToDefaults (juce::AudioProcessorValueTreeState& apvts);
}
