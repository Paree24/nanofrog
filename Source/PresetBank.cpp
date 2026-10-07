#include "PresetBank.h"
#include <cstdlib>
#include <map>

namespace PresetBank
{

const char* const kParamIds[kParamCount] = {
    "osc1_wave",
    "osc1_digital",
    "osc1_octave",
    "osc1_pitch",
    "osc1_fine",
    "osc1_level",
    "osc1_shape",
    "osc1_pwm",
    "osc1_xmod",
    "osc_mod",
    "osc2_wave",
    "osc2_octave",
    "osc2_pitch",
    "osc2_fine",
    "osc2_level",
    "mix_o1",
    "mix_o2",
    "mix_noise",
    "fm_amt",
    "filter_type",
    "filter_cutoff",
    "filter_reso",
    "filter_keytrack",
    "filter_envamt",
    "amp_level",
    "amp_pan",
    "amp_velocity",
    "amp_dist",
    "amp_drive",
    "env1_a",
    "env1_d",
    "env1_s",
    "env1_r",
    "env2_a",
    "env2_d",
    "env2_s",
    "env2_r",
    "lfo1_wave",
    "lfo1_rate",
    "lfo1_depth",
    "lfo1_sync",
    "lfo2_wave",
    "lfo2_rate",
    "lfo2_depth",
    "lfo2_sync",
    "mod1_src",
    "mod1_dst",
    "mod1_amt",
    "mod2_src",
    "mod2_dst",
    "mod2_amt",
    "mod3_src",
    "mod3_dst",
    "mod3_amt",
    "mod4_src",
    "mod4_dst",
    "mod4_amt",
    "modfx_type",
    "modfx_rate",
    "modfx_depth",
    "modfx_mix",
    "delay_type",
    "delay_time",
    "delay_feedback",
    "delay_mix",
    "eq_low",
    "eq_high",
    "arp_on",
    "arp_mode",
    "arp_rate",
    "arp_gate",
    "arp_octaves",
    "arp_swing",
    "arp_latch",
    "voice_poly",
    "voice_detune",
    "voice_portamento",
    "voice_vib",
    "output_level",
    "master_tune",
    "song_tempo",
    "osc2_shape",
    "osc2_pwm",
    "osc2_digital",
    "osc2_xmod",
    "t2_osc1_wave",
    "t2_osc1_digital",
    "t2_osc1_octave",
    "t2_osc1_pitch",
    "t2_osc1_fine",
    "t2_osc1_level",
    "t2_osc1_shape",
    "t2_osc1_pwm",
    "t2_osc1_xmod",
    "t2_osc_mod",
    "t2_osc2_wave",
    "t2_osc2_octave",
    "t2_osc2_pitch",
    "t2_osc2_fine",
    "t2_osc2_level",
    "t2_mix_o1",
    "t2_mix_o2",
    "t2_mix_noise",
    "t2_fm_amt",
    "t2_filter_type",
    "t2_filter_cutoff",
    "t2_filter_reso",
    "t2_filter_keytrack",
    "t2_filter_envamt",
    "t2_amp_level",
    "t2_amp_pan",
    "t2_amp_velocity",
    "t2_amp_dist",
    "t2_amp_drive",
    "t2_env1_a",
    "t2_env1_d",
    "t2_env1_s",
    "t2_env1_r",
    "t2_env2_a",
    "t2_env2_d",
    "t2_env2_s",
    "t2_env2_r",
    "t2_lfo1_wave",
    "t2_lfo1_rate",
    "t2_lfo1_depth",
    "t2_lfo1_sync",
    "t2_lfo2_wave",
    "t2_lfo2_rate",
    "t2_lfo2_depth",
    "t2_lfo2_sync",
    "t2_mod1_src",
    "t2_mod1_dst",
    "t2_mod1_amt",
    "t2_mod2_src",
    "t2_mod2_dst",
    "t2_mod2_amt",
    "t2_mod3_src",
    "t2_mod3_dst",
    "t2_mod3_amt",
    "t2_mod4_src",
    "t2_mod4_dst",
    "t2_mod4_amt",
    "t2_voice_poly",
    "t2_voice_detune",
    "t2_voice_portamento",
    "t2_voice_vib",
    "t2_osc2_shape",
    "t2_osc2_pwm",
    "t2_osc2_digital",
    "t2_osc2_xmod",
    "tmix",
    "limiter"
};

// ------------------------------------------------------------------ bank
static std::vector<NanoPreset> factory;
static bool loaded = false;
static bool fallback = false;

static juce::File moduleDir()
{
    return juce::File::getSpecialLocation (
        juce::File::currentExecutableFile).getParentDirectory();
}

static void searchPaths (std::vector<juce::File>& out)
{
    if (const char* e = std::getenv ("NANOFROG_FACTORY_JSON"))
        if (*e) out.push_back (juce::File (juce::String::fromUTF8 (e)));
    if (const char* e = std::getenv ("NANOBERG_FACTORY_JSON")) // pre-rename override
        if (*e) out.push_back (juce::File (juce::String::fromUTF8 (e)));
    juce::File mod = moduleDir();
    out.push_back (mod.getChildFile ("NanoFrogFactory.json"));
    out.push_back (mod.getChildFile ("NanoBergFactory.json")); // pre-rename bundle
    // VST3 bundle layout: <name>.vst3/Contents/<arch>/binary
    out.push_back (mod.getParentDirectory().getChildFile ("Resources")
                      .getChildFile ("NanoFrogFactory.json"));
    out.push_back (mod.getParentDirectory().getChildFile ("Resources")
                      .getChildFile ("NanoBergFactory.json")); // pre-rename bundle
    out.push_back (juce::File::getSpecialLocation (
                       juce::File::userApplicationDataDirectory)
                       .getChildFile ("NanoFrog")
                       .getChildFile ("NanoFrogFactory.json"));
    out.push_back (juce::File::getSpecialLocation (
                       juce::File::userDocumentsDirectory)
                       .getChildFile ("NanoFrog")
                       .getChildFile ("NanoFrogFactory.json"));
    out.push_back (juce::File::getCurrentWorkingDirectory()
                       .getChildFile ("NanoFrogFactory.json"));
    out.push_back (juce::File::getCurrentWorkingDirectory()
                       .getChildFile ("FactoryBank")
                       .getChildFile ("NanoFrogFactory.json"));
}

static std::map<juce::String, int> idIndex()
{
    std::map<juce::String, int> m;
    for (int i = 0; i < kParamCount; ++i) m[kParamIds[i]] = i;
    return m;
}

static bool loadBankFile (const juce::File& f)
{
    auto text = f.loadFileAsString();
    if (text.isEmpty()) return false;
    auto parsed = juce::JSON::parse (text);
    if (! parsed.isObject()) return false;
    auto* obj = parsed.getDynamicObject();
    if (! obj || obj->getProperty ("type").toString() != "NanoFrogBank") return false;
    auto ids = obj->getProperty ("param_ids");
    auto pres = obj->getProperty ("presets");
    if (! pres.isArray()) return false;
    auto idx = idIndex();
    std::vector<NanoPreset> bank;
    for (auto& pv : *pres.getArray())
    {
        auto* po = pv.getDynamicObject();
        if (po == nullptr) continue;
        NanoPreset np;
        np.name = po->getProperty ("name").toString();
        if (np.name.isEmpty()) continue;
        auto tags = po->getProperty ("tags");
        if (tags.isArray())
            for (auto& t : *tags.getArray()) np.tags.add (t.toString());
        np.values.assign (kParamCount, 0.0f);
        auto vals = po->getProperty ("values");
        if (! vals.isArray()) continue;
        int n = juce::jmin (vals.getArray()->size(),
                            ids.isArray() ? ids.getArray()->size()
                                          : vals.getArray()->size());
        for (int k = 0; k < n; ++k)
        {
            juce::String id = ids.isArray() ? ids.getArray()->getReference (k).toString()
                                            : juce::String (kParamIds[k]);
            auto it = idx.find (id);
            if (it == idx.end()) continue; // unknown id: ignore
            np.values[(size_t) it->second] = (float) vals.getArray ()->getReference (k);
        }
        bank.push_back (std::move (np));
    }
    if (bank.empty()) return false;
    factory = std::move (bank);
    return true;
}

void ensureLoaded()
{
    if (loaded) return;
    loaded = true;
    std::vector<juce::File> paths;
    searchPaths (paths);
    for (auto& f : paths)
        if (f.existsAsFile() && loadBankFile (f)) { fallback = false; return; }
    fallback = true; // processor installs an Init voice from live defaults
}

bool isFallback() { ensureLoaded(); return fallback; }

void installFallback (const std::vector<float>& defaultValues)
{
    NanoPreset np;
    np.name = "Init Voice";
    np.values = defaultValues;
    if ((int) np.values.size() != kParamCount)
        np.values.assign (kParamCount, 0.0f);
    np.tags.add ("Init");
    np.tags.add ("Single");
    factory = { std::move (np) };
    fallback = true;
    loaded = true;
}

int count() { ensureLoaded(); return (int) factory.size(); }

const NanoPreset& get (int i)
{
    ensureLoaded();
    return factory[(size_t) juce::jlimit (0, (int) factory.size() - 1, i)];
}

int findByName (const juce::String& name)
{
    ensureLoaded();
    for (int i = 0; i < (int) factory.size(); ++i)
        if (factory[(size_t) i].name == name) return i;
    return -1;
}

juce::StringArray categoryList()
{
    return { "Bass", "Lead", "Pad", "Arp", "Keys", "Bell",
             "Brass", "Choir", "Hit", "SFX", "Sequence" };
}

std::vector<int> filter (const juce::String& query, const juce::String& category)
{
    ensureLoaded();
    juce::StringArray words;
    {
        juce::String q = query.toLowerCase();
        while (q.isNotEmpty())
        {
            juce::String w = q.upToFirstOccurrenceOf (" ", false, false).trim();
            if (w.isNotEmpty()) words.add (w);
            q = q.fromFirstOccurrenceOf (" ", false, false).trim();
        }
    }
    bool wantUser = category == "User";
    bool anyCat = category.isEmpty() || category == "All" || wantUser;
    auto users = wantUser || category == "All" ? scanUser() : std::vector<UserPreset>();
    std::vector<int> rows;
    auto matches = [&] (const juce::String& name, const juce::StringArray& tags)
    {
        for (auto& w : words)
        {
            bool hit = name.toLowerCase().contains (w);
            if (! hit)
                for (auto& t : tags)
                    if (t.toLowerCase().contains (w)) { hit = true; break; }
            if (! hit) return false;
        }
        return true;
    };
    if (! wantUser)
        for (int i = 0; i < (int) factory.size(); ++i)
        {
            auto& pr = factory[(size_t) i];
            if (! anyCat && ! pr.tags.contains (category)) continue;
            if (matches (pr.name, pr.tags)) rows.push_back (i);
        }
    if (wantUser || category == "All")
        for (int u = 0; u < (int) users.size(); ++u)
            if (matches (users[(size_t) u].name, users[(size_t) u].tags))
                rows.push_back (userRow (u));
    return rows;
}

// ------------------------------------------------------------------ user dir
juce::File userDir()
{
    if (const char* e = std::getenv ("NANOFROG_USER_DIR"))
        if (*e) return juce::File (juce::String::fromUTF8 (e));
    if (const char* e = std::getenv ("NANOBERG_USER_DIR")) // pre-rename override
        if (*e) return juce::File (juce::String::fromUTF8 (e));
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("NanoFrog").getChildFile ("User Presets");
}

// Pre-rename user location: scanned (not written) so saved presets survive.
static juce::File legacyUserDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("NanoBerg").getChildFile ("User Presets");
}

static void collectUserFiles (std::vector<UserPreset>& out, const juce::File& dir)
{
    if (! dir.isDirectory()) return;
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.nbpreset");
    files.sort();
    for (auto& f : files)
    {
        bool known = false;
        for (auto& u : out)
            if (u.file.getFileName() == f.getFileName()) { known = true; break; }
        if (known) continue; // new location wins on filename clash
        NanoPreset np;
        if (! readUser (f, np)) continue;
        out.push_back ({ np.name.isNotEmpty() ? np.name
                                              : f.getFileNameWithoutExtension(),
                         f, np.tags });
    }
}

std::vector<UserPreset> scanUser()
{
    std::vector<UserPreset> out;
    collectUserFiles (out, userDir());
    collectUserFiles (out, legacyUserDir());
    return out;
}

juce::String sanitise (const juce::String& name)
{
    juce::String s = name.trim();
    if (s.isEmpty()) return "Untitled";
    juce::String ok;
    for (auto c : s)
        ok += (juce::CharacterFunctions::isLetterOrDigit (c) || c == ' ' || c == '-'
               || c == '_' || c == '\'' || c == '.') ? juce::String::charToString (c)
                                                     : juce::String ("_");
    return ok.substring (0, 48);
}

static bool writePresetFile (const juce::File& f, const juce::String& name,
                             const juce::StringArray& tags,
                             const std::vector<float>& values)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("type", "NanoFrogPreset");
    obj->setProperty ("version", 1);
    obj->setProperty ("name", name);
    juce::Array<juce::var> jt;
    for (auto& t : tags) jt.add (t);
    obj->setProperty ("tags", jt);
    juce::Array<juce::var> ji;
    for (int i = 0; i < kParamCount; ++i) ji.add (kParamIds[i]);
    obj->setProperty ("param_ids", ji);
    juce::Array<juce::var> jv;
    for (int i = 0; i < kParamCount; ++i)
        jv.add ((double) (i < (int) values.size() ? values[(size_t) i] : 0.0));
    obj->setProperty ("values", jv);
    return f.replaceWithText (juce::JSON::toString (juce::var (obj), true));
}

juce::File saveUser (const juce::String& name, const juce::StringArray& tags,
                     const std::vector<float>& values)
{
    auto dir = userDir();
    if (! dir.isDirectory() && ! dir.createDirectory()) return {};
    juce::String base = sanitise (name);
    juce::File f = dir.getChildFile (base + ".nbpreset");
    for (int n = 2; f.existsAsFile() && n < 100; ++n)
        f = dir.getChildFile (base + " " + juce::String (n) + ".nbpreset");
    return writePresetFile (f, f.getFileNameWithoutExtension(), tags, values) ? f : juce::File();
}

bool overwriteUser (const juce::File& file, const std::vector<float>& values,
                    const juce::StringArray& tags)
{
    // Own dirs only (current + pre-rename location).
    if (file.getParentDirectory() != userDir()
        && file.getParentDirectory() != legacyUserDir()) return false;
    NanoPreset cur;
    juce::String name = file.getFileNameWithoutExtension();
    if (readUser (file, cur) && cur.name.isNotEmpty()) name = cur.name;
    return writePresetFile (file, name, tags, values);
}

bool deleteUser (const juce::File& file)
{
    if (file.getParentDirectory() != userDir()
        && file.getParentDirectory() != legacyUserDir()) return false;
    return file.deleteFile();
}

bool readUser (const juce::File& file, NanoPreset& out)
{
    auto text = file.loadFileAsString();
    if (text.isEmpty()) return false;
    auto parsed = juce::JSON::parse (text);
    if (! parsed.isObject()) return false;
    auto* obj = parsed.getDynamicObject();
    if (! obj) return false;
    // Accept pre-rename files too (type was "NanoBergPreset").
    juce::String type = obj->getProperty ("type").toString();
    if (type != "NanoFrogPreset" && type != "NanoBergPreset") return false;
    out.name = obj->getProperty ("name").toString();
    out.tags.clear();
    auto tags = obj->getProperty ("tags");
    if (tags.isArray())
        for (auto& t : *tags.getArray()) out.tags.add (t.toString());
    out.values.assign (kParamCount, 0.0f);
    auto idx = idIndex();
    auto ids = obj->getProperty ("param_ids");
    auto vals = obj->getProperty ("values");
    if (! vals.isArray()) return false;
    int n = juce::jmin (vals.getArray()->size(),
                        ids.isArray() ? ids.getArray()->size()
                                      : vals.getArray()->size());
    for (int k = 0; k < n; ++k)
    {
        juce::String id = ids.isArray() ? ids.getArray()->getReference (k).toString()
                                        : juce::String (kParamIds[k]);
        auto it = idx.find (id);
        if (it == idx.end()) continue;
        out.values[(size_t) it->second] = (float) vals.getArray ()->getReference (k);
    }
    return true;
}

// ------------------------------------------------------------------ APVTS
std::vector<float> capture (juce::AudioProcessorValueTreeState& apvts)
{
    std::vector<float> v;
    v.reserve (kParamCount);
    for (int i = 0; i < kParamCount; ++i)
    {
        float f = 0.0f;
        if (auto* pv = apvts.getRawParameterValue (kParamIds[i])) f = pv->load();
        v.push_back (f);
    }
    return v;
}

void resetToDefaults (juce::AudioProcessorValueTreeState& apvts)
{
    for (auto* param : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
}

void apply (juce::AudioProcessorValueTreeState& apvts, const std::vector<float>& values)
{
    resetToDefaults (apvts);
    for (int i = 0; i < kParamCount && i < (int) values.size(); ++i)
        if (auto* p = apvts.getParameter (kParamIds[i]))
            p->setValueNotifyingHost (p->convertTo0to1 (values[(size_t) i]));
}

} // namespace PresetBank
