#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <random>
#include <vector>

NanoFrogProcessor::NanoFrogProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    PresetBank::ensureLoaded();
    // Audio-thread containers never grow mid-stream: reserve worst cases.
    heldNotes.reserve (128);
    arp.held.reserve (128);
    arp.sorted.reserve (128);
    arp.pattern.reserve (128 * 4 * 2);
    if (PresetBank::isFallback())
        PresetBank::installFallback (PresetBank::capture (apvts)); // Init from live defaults
    undo = std::make_unique<SnapshotUndo> (*this,
        [this]
        {
            juce::ValueTree t ("NF_UNDO");
            t.setProperty ("preset", currentPreset, nullptr);
            t.setProperty ("user", currentUser, nullptr);
            auto v = PresetBank::capture (apvts);
            juce::MemoryBlock mb (v.data(), v.size() * sizeof (float));
            t.setProperty ("values", mb, nullptr);
            return t;
        },
        [this] (const juce::ValueTree& t)
        {
            if (auto* mb = t.getProperty ("values").getBinaryData())
            {
                std::vector<float> v (mb->getSize() / sizeof (float));
                std::memcpy (v.data(), mb->getData(), v.size() * sizeof (float));
                PresetBank::apply (apvts, v);
            }
            currentPreset = t.getProperty ("preset", currentPreset);
            currentUser = t.getProperty ("user", "").toString();
        });
}

NanoFrogProcessor::~NanoFrogProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout NanoFrogProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto F = [&] (const juce::String& pre, const char* id, const char* name, float lo, float hi, float def)
    {
        // NOTE (LESSONS.md #12 family): the (id,name,min,max,def) overload
        // hardcodes an absolute 0.01 snap interval. Always pass an explicit
        // continuous range so preset recall and sweeps stay exact.
        juce::NormalisableRange<float> range (lo, hi, 0.0f);
        juce::String nm = pre.isEmpty() ? juce::String (name) : (juce::String (name) + " T2");
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (pre + id, 1), nm, range, def));
    };
    auto C = [&] (const juce::String& pre, const char* id, const char* name, std::initializer_list<const char*> ch, int def)
    {
        juce::StringArray a; for (auto c : ch) a.add (c);
        juce::String nm = pre.isEmpty() ? juce::String (name) : (juce::String (name) + " T2");
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (pre + id, 1), nm, a, def));
    };
    auto B = [&] (const juce::String& pre, const char* id, const char* name, bool def)
    {
        juce::String nm = pre.isEmpty() ? juce::String (name) : (juce::String (name) + " T2");
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (pre + id, 1), nm, def));
    };

    auto addTimbre = [&] (const juce::String& pre)
    {
    C (pre, "osc1_wave","Osc1 Wave", {"Saw","Square","Triangle","Sine","Digital","Noise","User"}, 0);
    {
        juce::StringArray digi;
        for (int i = 1; i <= 128; ++i) digi.add ("DIGITAL " + juce::String (i).paddedLeft ('0', 2));
        for (int i = 1; i <= 64; ++i) digi.add ("USER " + juce::String (i).paddedLeft ('0', 2));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (pre + "osc1_digital", 1), (pre.isEmpty() ? juce::String ("Osc1 Digital") : juce::String ("Osc1 Digital T2")), digi, 0));
    }
    F (pre, "osc1_octave","Osc1 Octave", -2, 2, 0);
    F (pre, "osc1_pitch","Osc1 Pitch", -12, 12, 0);
    F (pre, "osc1_fine","Osc1 Fine", -50, 50, 0);
    F (pre, "osc1_level","Osc1 Level", 0, 1, 1.0f);
    F (pre, "osc1_shape","Osc1 Shape", 0, 1, 0);
    F (pre, "osc1_pwm","Osc1 PWM", 0, 1, 0.5f);
    F (pre, "osc1_xmod","Osc1 XMod", 0, 1, 0);
    C (pre, "osc_mod","Osc Mod", {"Off","Ring","Sync","RingSync"}, 0);
    C (pre, "osc2_wave","Osc2 Wave", {"Saw","Square","Triangle","Sine","Digital","Noise","User"}, 0);
    F (pre, "osc2_octave","Osc2 Octave", -2, 2, 0);
    F (pre, "osc2_pitch","Osc2 Pitch", -12, 12, 0);
    F (pre, "osc2_fine","Osc2 Fine", -50, 50, 0);
    F (pre, "osc2_level","Osc2 Level", 0, 1, 0.0f);
    F (pre, "osc2_shape","Osc2 Shape", 0, 1, 0.0f);
    F (pre, "osc2_pwm","Osc2 PWM", 0, 1, 0.5f);
    {
        juce::StringArray digi2;
        for (int i = 1; i <= 128; ++i) digi2.add ("DIGITAL " + juce::String (i).paddedLeft ('0', 2));
        for (int i = 1; i <= 64; ++i) digi2.add ("USER " + juce::String (i).paddedLeft ('0', 2));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (pre + "osc2_digital", 1), (pre.isEmpty() ? juce::String ("Osc2 Digital") : juce::String ("Osc2 Digital T2")), digi2, 0));
    }
    F (pre, "osc2_xmod","Osc2 XMod", 0, 1, 0.0f);
    F (pre, "mix_o1","Mix Osc1", 0, 1, 1.0f);
    F (pre, "mix_o2","Mix Osc2", 0, 1, 0.0f);
    F (pre, "mix_noise","Mix Noise", 0, 1, 0);
    F (pre, "fm_amt","FM Amount", 0, 1, 0);
    C (pre, "filter_type","Filter Type", {"LP24","LP12","BP12","HP12"}, 0);
    F (pre, "filter_cutoff","Filter Cutoff", 0, 1, 1.0f);
    F (pre, "filter_reso","Filter Resonance", 0, 1, 0.0f);
    F (pre, "filter_keytrack","Filter KeyTrack", -1, 1, 0);
    F (pre, "filter_envamt","Filter Env Amount", -1, 1, 0.0f);
    F (pre, "amp_level","Amp Level", 0, 1, 1.0f);
    F (pre, "amp_pan","Amp Pan", -1, 1, 0);
    F (pre, "amp_velocity","Amp Velocity", 0, 1, 0.0f);
    B (pre, "amp_dist","Amp Distortion", false);
    F (pre, "amp_drive","Amp Drive", 0, 1, 0.0f);
    F (pre, "env1_a","Env1 Attack", 0, 1, 0.0f);
    F (pre, "env1_d","Env1 Decay", 0, 1, 0.0f);
    F (pre, "env1_s","Env1 Sustain", 0, 1, 1.0f);
    F (pre, "env1_r","Env1 Release", 0, 1, 0.1f);
    F (pre, "env2_a","Env2 Attack", 0, 1, 0.0f);
    F (pre, "env2_d","Env2 Decay", 0, 1, 0.0f);
    F (pre, "env2_s","Env2 Sustain", 0, 1, 1.0f);
    F (pre, "env2_r","Env2 Release", 0, 1, 0.1f);
    C (pre, "lfo1_wave","LFO1 Wave", {"Sine","Triangle","Square","Saw","Random"}, 0);
    F (pre, "lfo1_rate","LFO1 Rate", 0, 1, 0.5f);
    F (pre, "lfo1_depth","LFO1 Depth", 0, 1, 0.0f);
    C (pre, "lfo2_wave","LFO2 Wave", {"Sine","Triangle","Square","Saw","Random"}, 0);
    F (pre, "lfo2_rate","LFO2 Rate", 0, 1, 0.5f);
    F (pre, "lfo2_depth","LFO2 Depth", 0, 1, 0.0f);
    {
        // Tempo-sync divisions (Off + 14 note values); index 0 = free rate.
        juce::StringArray syncNotes { "Off", "1/1", "3/4", "2/3", "1/2", "3/8", "1/3",
                                      "1/4", "3/16", "1/6", "1/8", "3/32", "1/12",
                                      "1/16", "1/24", "1/32" };
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (pre + "lfo1_sync", 1),
            pre.isEmpty() ? juce::String ("LFO1 Sync") : juce::String ("LFO1 Sync T2"),
            syncNotes, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (pre + "lfo2_sync", 1),
            pre.isEmpty() ? juce::String ("LFO2 Sync") : juce::String ("LFO2 Sync T2"),
            syncNotes, 0));
        // LFO KEY SYNC (hardware): VOICE restarts each note's LFO, TIMBRE
        // restarts on the first key of a phrase and later notes follow it,
        // OFF free-runs
        for (auto k : { "lfo1_keysync", "lfo2_keysync" })
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID (pre + k, 1),
                juce::String (k[3] == '1' ? "LFO1 Key Sync" : "LFO2 Key Sync") + (pre.isEmpty() ? "" : " T2"),
                juce::StringArray { "Off", "Timbre", "Voice" }, 2));
    }
    juce::StringArray srcs { "ENV1","ENV2","LFO1","LFO2","Velocity","KeyTrack","PitchBend","ModWheel","Random" };
    juce::StringArray dsts { "Pitch","OSC2 Pitch","OSC1 Shape","Noise Level","Filter Cutoff","Filter Reso","Amp Level","Pan","LFO1 Rate","LFO2 Rate" };
    juce::String sids[4] = { pre + "mod1_src", pre + "mod2_src", pre + "mod3_src", pre + "mod4_src" };
    juce::String dids[4] = { pre + "mod1_dst", pre + "mod2_dst", pre + "mod3_dst", pre + "mod4_dst" };
    juce::String aids[4] = { pre + "mod1_amt", pre + "mod2_amt", pre + "mod3_amt", pre + "mod4_amt" };
    int dsrc[4] = { 2, 0, 3, 4 }, ddst[4] = { 4, 0, 1, 6 };
    float damt[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 4; ++i)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (sids[i], 1), sids[i], srcs, dsrc[i]));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (dids[i], 1), dids[i], dsts, ddst[i]));
        juce::NormalisableRange<float> amtRange (-1.0f, 1.0f, 0.0f); // continuous: see note on F() above
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (aids[i], 1), aids[i], amtRange, damt[i]));
    }
    };
    addTimbre ("");
    addTimbre ("t2_");
    // ---- shared: FX, arp, master ----
    C ("", "modfx_type","Mod FX", {"Off","Chorus","Flanger","Phaser"}, 0);
    F ("", "modfx_rate","Mod Rate", 0, 1, 0.25f);
    F ("", "modfx_depth","Mod Depth", 0, 1, 0.25f);
    F ("", "modfx_mix","Mod Mix", 0, 1, 0.25f);
    C ("", "delay_type","Delay Type", {"Stereo","Cross","PingPong"}, 0);
    F ("", "delay_time","Delay Time", 0, 1, 0.35f);
    F ("", "delay_feedback","Delay Feedback", 0, 1, 0.25f);
    F ("", "delay_mix","Delay Mix", 0, 1, 0.0f);
    F ("", "eq_low","EQ Low", -12, 12, 0);
    F ("", "eq_high","EQ High", -12, 12, 0);
    B ("", "arp_on","Arp On", false);
    C ("", "arp_mode","Arp Mode", {"Up","Down","Alt1","Alt2","Random","Trigger"}, 0);
    C ("", "arp_rate","Arp Rate", {"1/4","1/8","1/16","1/8T","1/16T","1/32","1/6"}, 2);
    F ("", "arp_gate","Arp Gate", 0, 1, 0.8f);
    F ("", "arp_octaves","Arp Octaves", 1, 4, 1);
    F ("", "arp_swing","Arp Swing", -1, 1, 0);
    B ("", "arp_latch","Arp Latch", false);
    // LAST STEP 1..8 + per-step on/off (a switched-off step is a rest)
    F ("", "arp_last_step","Arp Last Step", 1, 8, 8);
    for (int k = 1; k <= 8; ++k)
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID ("arp_step" + juce::String (k), 1), "Arp Step " + juce::String (k), true));
    auto addVoice = [&] (const juce::String& pre)
    {
        F (pre, "voice_poly","Polyphony", 1, 8, 1);
        F (pre, "voice_detune","Detune", 0, 1, 0.0f);
        F (pre, "voice_portamento","Portamento", 0, 1, 0);
        F (pre, "voice_vib","Vibrato", 0, 1, 0.0f);
        // VOICE ASSIGN (hardware): Poly, Mono (one voice, note priority,
        // Single = legato / Multi = retrigger), Unison (4 stacked voices)
        C (pre, "voice_assign","Voice Assign", {"Poly","Mono","Unison"}, 0);
        C (pre, "voice_trigger","Trigger Mode", {"Single","Multi"}, 0);
        C (pre, "voice_priority","Note Priority", {"Last","Low","High"}, 0);
        F (pre, "voice_unidet","Unison Detune", 0, 99, 15);
    };
    addVoice ("");
    addVoice ("t2_");
    F ("", "output_level","Output Level", 0, 1.25f, 1.0f);
    F ("", "master_tune","Master Tune", -50, 50, 0);
    F ("", "song_tempo","Song Tempo", 20, 300, 120);
    F ("", "bend_range","Bend Range", 0, 12, 2);
    // delay tempo sync: Off (TIME knob) or a note value at the song / host tempo
    C ("", "delay_sync","Delay Sync", {"Off","1/1","3/4","2/3","1/2","3/8","1/3","1/4","3/16","1/6","1/8","3/32","1/12","1/16","1/24","1/32"}, 0);
    // ---- shared: timbre crossfade (0 = Timbre 1, 1 = Timbre 2) ----
    F ("", "tmix","Timbre Mix", 0, 1, 0.0f);
    // ---- output safety: brickwall-ish ceiling post output gain ----
    B ("", "limiter","Limiter", true);
    return layout;
}

// StringRef: looking a parameter up never builds a juce::String (no heap
// allocation on the audio thread).
static inline float pv (juce::AudioProcessorValueTreeState& s, juce::StringRef id)
{
    if (auto* p = s.getRawParameterValue (id)) return p->load();
    return 0.0f;
}

float NanoFrogProcessor::egTimeFromKnob (float v01, float maxSec)
{
    float v = juce::jlimit (0.0f, 1.0f, v01);
    return 0.002f + v * v * maxSec;
}

VoiceParams NanoFrogProcessor::collectVoiceParams (int timbre)
{
    VoiceParams p;
    auto P = [&] (const char* id) -> float
    {
        if (timbre == 0) return pv (apvts, id);
        char buf[64];
        std::snprintf (buf, sizeof (buf), "t2_%s", id);
        return pv (apvts, buf);
    };
    p.osc1Wave = (int) P ("osc1_wave");
    p.osc1Digital = (int) P ("osc1_digital");
    p.osc1Oct = (int) std::round (P ("osc1_octave"));
    p.osc1Pitch = (int) std::round (P ("osc1_pitch"));
    p.osc1Fine = P ("osc1_fine") + pv (apvts, "master_tune");
    p.osc1Level = P ("osc1_level");
    p.osc1Shape = P ("osc1_shape");
    p.osc1Pwm = P ("osc1_pwm");
    p.osc1Xmod = P ("osc1_xmod");
    p.oscMod = (int) P ("osc_mod");
    p.osc2Wave = (int) P ("osc2_wave");
    p.osc2Oct = (int) std::round (P ("osc2_octave"));
    p.osc2Pitch = (int) std::round (P ("osc2_pitch"));
    p.osc2Fine = P ("osc2_fine") + pv (apvts, "master_tune");
    p.osc2Level = P ("osc2_level");
    p.mixO1 = P ("mix_o1"); p.mixO2 = P ("mix_o2"); p.mixN = P ("mix_noise");
    p.fmAmt = P ("fm_amt");
    p.filtType = (int) P ("filter_type");
    p.filtCut = P ("filter_cutoff");
    p.filtRes = P ("filter_reso");
    p.filtKey = P ("filter_keytrack");
    p.filtEnv = P ("filter_envamt");
    p.ampLevel = P ("amp_level");
    p.ampPan = P ("amp_pan");
    p.ampVel = P ("amp_velocity");
    p.ampDist = P ("amp_dist") > 0.5f;
    p.ampDrive = P ("amp_drive");
    p.e1a = egTimeFromKnob (P ("env1_a"), 4.0f);
    p.e1d = egTimeFromKnob (P ("env1_d"), 8.0f);
    p.e1s = P ("env1_s");
    p.e1r = egTimeFromKnob (P ("env1_r"), 8.0f);
    p.e2a = egTimeFromKnob (P ("env2_a"), 4.0f);
    p.e2d = egTimeFromKnob (P ("env2_d"), 8.0f);
    p.e2s = P ("env2_s");
    p.e2r = egTimeFromKnob (P ("env2_r"), 8.0f);
    auto rateHz = [] (float v) { v = juce::jlimit (0.0f, 1.0f, v); return 0.05 * std::pow (600.0, v); };
    p.lfo1Wave = (int) P ("lfo1_wave");
    p.lfo1Rate = rateHz (P ("lfo1_rate"));
    p.lfo1Depth = P ("lfo1_depth");
    p.lfo2Wave = (int) P ("lfo2_wave");
    p.lfo2Rate = rateHz (P ("lfo2_rate"));
    p.lfo2Depth = P ("lfo2_depth");
    const char* sids[4] = { "mod1_src","mod2_src","mod3_src","mod4_src" };
    const char* dids[4] = { "mod1_dst","mod2_dst","mod3_dst","mod4_dst" };
    const char* aids[4] = { "mod1_amt","mod2_amt","mod3_amt","mod4_amt" };
    for (int i = 0; i < 4; ++i) { p.mSrc[i] = (int) P (sids[i]); p.mDst[i] = (int) P (dids[i]); p.mAmt[i] = P (aids[i]); }
    p.portamento = P ("voice_portamento");
    p.vibAmt = P ("voice_vib");
    p.detune = P ("voice_detune");
    p.osc2Shape = P ("osc2_shape");
    p.osc2Pwm = P ("osc2_pwm");
    p.osc2Digital = (int) P ("osc2_digital");
    p.osc2Xmod = P ("osc2_xmod");
    p.lfo1Note = (int) P ("lfo1_sync") - 1;
    p.lfo2Note = (int) P ("lfo2_sync") - 1;
    p.songTempo = effBpm;
    // the 2-tap DETUNE stack belongs to a mono voice (Unison assign stacks 4 voices instead)
    p.unison = ((int) P ("voice_assign") == 1 && p.detune > 0.01f);
    p.lfo1KeySync = (int) P ("lfo1_keysync");
    p.lfo2KeySync = (int) P ("lfo2_keysync");
    p.userTable = nullptr;
    p.userTable2 = nullptr;
    {
        // Digital selector 0..127 = factory DIGITAL, 128..191 = USER 01..64.
        int bc = bankCount.load();
        if (bc > 0)
        {
            int b1 = p.osc1Digital >= 128 ? p.osc1Digital - 128 : p.osc1Digital;
            int b2 = p.osc2Digital >= 128 ? p.osc2Digital - 128 : p.osc2Digital;
            if (p.osc1Wave == 6 || p.osc1Wave == 4)
                p.userTable = bankMip[bankActive.load()][juce::jlimit (0, bc - 1, b1)];
            if (p.osc2Wave == 6 || p.osc2Wave == 4)
                p.userTable2 = bankMip[bankActive.load()][juce::jlimit (0, bc - 1, b2)];
        }
    }
    return p;
}

void NanoFrogProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    for (int t = 0; t < 2; ++t)
        for (auto& v : voices[t]) v.setSampleRate (sampleRate);
    modfx.setSampleRate (sampleRate);
    delay.setSampleRate (sampleRate);
    eq.setSampleRate (sampleRate);
    monoScratch.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    limiter.prepare (sampleRate);
    setLatencySamples (limiter.latency()); // lookahead, held while the limiter is off
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    gateG = 1.0f; idleSamples = 0;
}

int NanoFrogProcessor::allocateVoice (int timbre, int note, int poly)
{
    poly = juce::jlimit (1, MaxVoices, poly);
    // reuse voice already on this note
    for (int i = 0; i < MaxVoices; ++i)
        if (voices[timbre][i].isActive() && voices[timbre][i].getNote() == note) return i;
    // find free voice within poly limit
    for (int i = 0; i < poly; ++i)
        if (! voices[timbre][i].isActive()) return i;
    // steal quietest
    int idx = 0; float best = 1e9f;
    for (int i = 0; i < poly; ++i)
    {
        float score = voices[timbre][i].audibleLevel() + voices[timbre][i].getAge() * -1e-7f;
        if (score < best) { best = score; idx = i; }
    }
    return idx;
}

// Poly voice start, with LFO KEY SYNC = TIMBRE: the first voice of a phrase
// restarts the LFOs, later voices take their phase from one already sounding.
void NanoFrogProcessor::startPolyVoice (int t, int note, float vel)
{
    const auto& vp = blockVp[t];
    int vi = allocateVoice (t, note, blockPoly[t]);
    const NanoVoice* ref = nullptr;
    for (int k = 0; k < MaxVoices; ++k)
        if (k != vi && voices[t][k].isActive()) { ref = &voices[t][k]; break; }
    auto& v = voices[t][vi];
    v.setUnison (0.0f, 1.0f);
    v.noteOn (note, vel, vp, false);
    if (vp.lfo1KeySync == 1 || vp.lfo2KeySync == 1)
    {
        double a, b, ra = 0.0, rb = 0.0;
        v.getLfoPhases (a, b);
        if (ref != nullptr) ref->getLfoPhases (ra, rb);
        v.setLfoPhases (vp.lfo1KeySync == 1 ? ra : a, vp.lfo2KeySync == 1 ? rb : b);
    }
}

// the key a Mono / Unison timbre sounds: last pressed, lowest or highest held
int NanoFrogProcessor::monoPick (int t) const
{
    if (heldNotes.empty()) return -1;
    if (blockPriority[t] == 1) return *std::min_element (heldNotes.begin(), heldNotes.end());
    if (blockPriority[t] == 2) return *std::max_element (heldNotes.begin(), heldNotes.end());
    return heldNotes.back();
}

// Mono (one voice) / Unison (voices 0..3, detuned): a new key, or an arp step.
// Single trigger: overlapping keys slide (legato, envelopes keep going);
// Multi: every key restarts the envelopes. Detached notes always restart.
void NanoFrogProcessor::monoNoteOn (int t, int note, float vel, bool fromArp)
{
    const int stack = blockAssign[t] == 2 ? kUnison : 1;
    const auto& vp = blockVp[t];
    int target = fromArp ? note : monoPick (t);
    if (target < 0) return;
    bool sounding = false, gated = false;
    for (int k = 0; k < stack; ++k)
        if (voices[t][k].isActive()) { sounding = true; break; }
    gated = sounding && ! fromArp && (int) heldNotes.size() > 1; // another key was already down
    for (int k = 0; k < stack; ++k)
    {
        auto& v = voices[t][k];
        // unison spread: four voices across the DETUNE width (cents), 1/2 level each
        const float off = stack == 1 ? 0.0f : blockUniDet[t] * ((float) k / (float) (stack - 1) - 0.5f);
        v.setUnison (off, stack == 1 ? 1.0f : 0.5f);
        if (v.isActive() && v.getNote() == target && gated) continue; // already there (priority kept it)
        if (sounding && v.isActive())
            v.legato (target, vel, vp, ! gated || blockTrigger[t] == 1);
        else
            v.noteOn (target, vel, vp, false);
    }
    for (int k = stack; k < MaxVoices; ++k)
        if (voices[t][k].isActive()) voices[t][k].noteOff(); // leftovers from a poly / unison switch
}

// a key is up (and the pedal isn't holding it): fall back to another held key
// or release
void NanoFrogProcessor::monoRelease (int t, int note)
{
    const int stack = blockAssign[t] == 2 ? kUnison : 1;
    bool ours = false;
    for (int k = 0; k < stack; ++k)
        if (voices[t][k].isActive() && voices[t][k].getNote() == note) ours = true;
    if (! ours) return;
    int next = monoPick (t);
    for (int k = 0; k < stack; ++k)
    {
        auto& v = voices[t][k];
        if (! v.isActive()) continue;
        if (next >= 0) v.legato (next, lastVel, blockVp[t], blockTrigger[t] == 1);
        else v.noteOff();
    }
}

void NanoFrogProcessor::releaseKey (int n)
{
    for (int t = 0; t < 2; ++t)
    {
        if (t == 1 && blockT2Off) continue;
        if (blockAssign[t] != 0) monoRelease (t, n);
        else
            for (auto& v : voices[t])
                if (v.isActive() && v.getNote() == n) v.noteOff();
    }
}

void NanoFrogProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        int n = m.getNoteNumber();
        float vel = m.getVelocity() / 127.0f;
        lastVel = vel;
        sustainHeld[n] = 0;
        // Latch: the first key of a fresh gesture replaces the latched set.
        if (blockArpOn && blockLatch)
        {
            if (heldNotes.empty()) arp.held.clear();
            if (std::find (arp.held.begin(), arp.held.end(), n) == arp.held.end())
                arp.held.push_back (n);
        }
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), n), heldNotes.end());
        heldNotes.push_back (n); // press order (Last priority)
        if (! blockArpOn)
        {
            for (int t = 0; t < 2; ++t)
            {
                if (t == 1 && blockT2Off) continue; // Timbre Mix at 0: T2 is silent anyway
                if (blockAssign[t] != 0) monoNoteOn (t, n, vel, false);
                else startPolyVoice (t, n, vel);
            }
        }
    }
    else if (m.isNoteOff())
    {
        int n = m.getNoteNumber();
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), n), heldNotes.end());
        // Arp notes are released by the arp clock itself (arpTick).
        if (! blockArpOn)
        {
            if (sustainPedal > 0.5f) sustainHeld[n] = 1; // pedal: defer release
            else
            {
                sustainHeld[n] = 0;
                releaseKey (n);
            }
        }
    }
    else if (m.isPitchWheel())
    {
        // LESSONS.md #13: center is 8192, with a deadband so worn wheels pin exact zero.
        int raw = m.getPitchWheelValue();
        const float range = pv (apvts, "bend_range"); // semitones each way
        bendSemi = (std::abs (raw - 8192) <= 48) ? 0.0f : (raw - 8192) / 8192.0f * range;
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        // (checked before isController: these are CC 120/123 messages)
        heldNotes.clear();
        arp.held.clear();
        std::memset (sustainHeld, 0, sizeof (sustainHeld));
        for (int t = 0; t < 2; ++t)
            for (auto& v : voices[t]) v.noteOff();
        arp.currentNote = -1;
    }
    else if (m.isController())
    {
        if (m.getControllerNumber() == 1) modWheel = m.getControllerValue() / 127.0f;
        else if (m.getControllerNumber() == 64)
        {
            bool down = m.getControllerValue() > 63;
            sustainPedal = down ? 1.0f : 0.0f;
            if (! down)
            {
                for (int i = 0; i < 128; ++i)
                {
                    if (sustainHeld[i] != 0
                        && std::find (heldNotes.begin(), heldNotes.end(), i) == heldNotes.end())
                    {
                        releaseKey (i);
                        sustainHeld[i] = 0;
                    }
                }
            }
        }
    }
}

void NanoFrogProcessor::releaseArpNote()
{
    if (arp.currentNote == -2) // trigger chord
    {
        for (int t = 0; t < 2; ++t)
            for (auto& v : voices[t]) v.noteOff();
    }
    else if (arp.currentNote >= 0)
    {
        for (int t = 0; t < 2; ++t)
            for (auto& v : voices[t])
                if (v.isActive() && v.getNote() == arp.currentNote) v.noteOff();
    }
    arp.currentNote = -1;
}

void NanoFrogProcessor::arpFireStep()
{
    const auto& src = blockLatch ? arp.held : heldNotes;
    static const double beats[7] = { 1.0, 0.5, 0.25, 1.0/3.0, 1.0/6.0, 0.125, 2.0/3.0 }; // last = 1/6
    int ri = juce::jlimit (0, 6, (int) pv (apvts, "arp_rate"));
    double stepSec = 60.0 / juce::jmax (20.0, effBpm) * beats[ri];
    double stepSamples = juce::jmax (32.0, stepSec * fs);
    // Swing lengthens even steps and shortens odd ones (pairs keep the grid).
    float swing = juce::jlimit (-1.0f, 1.0f, pv (apvts, "arp_swing"));
    double len = stepSamples * (1.0 + ((arp.tick & 1) == 0 ? 0.5 : -0.5) * swing);
    int mode = (int) pv (apvts, "arp_mode");
    int octs = juce::jlimit (1, 4, (int) std::round (pv (apvts, "arp_octaves")));

    auto& notes = arp.sorted;
    notes.assign (src.begin(), src.end());
    std::sort (notes.begin(), notes.end());
    auto& pattern = arp.pattern;
    pattern.clear();
    if (mode == 1) { for (int o = octs - 1; o >= 0; --o) for (int i = (int) notes.size() - 1; i >= 0; --i) pattern.push_back (notes[(size_t) i] + o * 12); }
    else if (mode == 5) { pattern.assign (notes.begin(), notes.end()); }
    else
    {
        // Up / Random use the full octave run; Alt modes bounce over it.
        for (int o = 0; o < octs; ++o) for (int x : notes) pattern.push_back (x + o * 12);
        // ALT1 turns at the ends without repeating them (C E G E), ALT2
        // plays both end notes twice (C E G G E C)
        int upN = (int) pattern.size();
        if (mode == 2) for (int i = upN - 2; i > 0; --i) pattern.push_back (pattern[(size_t) i]);
        else if (mode == 3) for (int i = upN - 1; i >= 0; --i) pattern.push_back (pattern[(size_t) i]);
    }
    if (pattern.empty()) return;
    if (arp.step >= (int) pattern.size()) arp.step = 0;

    releaseArpNote();
    // LAST STEP cycle: a switched-off step is a rest (the pattern waits for
    // the next step that plays)
    const int lastStep = juce::jlimit (1, 8, (int) std::round (pv (apvts, "arp_last_step")));
    if (arp.stepPos >= lastStep) arp.stepPos = 0;
    static const char* stepIds[8] = { "arp_step1", "arp_step2", "arp_step3", "arp_step4",
                                      "arp_step5", "arp_step6", "arp_step7", "arp_step8" };
    const bool stepOn = pv (apvts, stepIds[arp.stepPos]) > 0.5f;
    arp.stepPos = (arp.stepPos + 1) % lastStep;
    ++arp.tick;
    if (! stepOn)
    {
        arp.samplesToNext += len;
        arpOffCountdown = 0.0;
        return;
    }
    float vel = lastVel;
    if (mode == 5) // trigger: whole chord each step
    {
        for (int x : notes)
            for (int t = 0; t < 2; ++t)
            {
                if (t == 1 && blockT2Off) continue;
                if (blockAssign[t] != 0) monoNoteOn (t, x, vel, true); // a mono timbre plays the top of the chord
                else startPolyVoice (t, x, vel);
            }
        arp.currentNote = -2;
    }
    else
    {
        int idx = (mode == 4) ? (int) (rand() % pattern.size()) : arp.step;
        int nn = juce::jlimit (0, 127, pattern[(size_t) idx]);
        for (int t = 0; t < 2; ++t)
        {
            if (t == 1 && blockT2Off) continue;
            if (blockAssign[t] != 0) monoNoteOn (t, nn, vel, true);
            else startPolyVoice (t, nn, vel);
        }
        arp.currentNote = nn;
    }
    arp.step++;
    arp.samplesToNext += len;
    arpOffCountdown = len * (double) juce::jlimit (0.05f, 1.0f, pv (apvts, "arp_gate"));
}

void NanoFrogProcessor::arpTick()
{
    const auto& src = blockLatch ? arp.held : heldNotes;
    if (! blockArpOn || src.empty())
    {
        if (arp.running)
        {
            releaseArpNote();
            arp.running = false;
            arpOffCountdown = 0.0;
        }
        return;
    }
    if (! arp.running)
    {
        // fresh gesture: first step lands on the key press, not a stale phase
        arp.running = true;
        arp.step = 0; arp.stepPos = 0; arp.tick = 0;
        arp.samplesToNext = 0.0;
    }
    if (arpOffCountdown > 0.0)
    {
        arpOffCountdown -= 1.0;
        if (arpOffCountdown <= 0.0) releaseArpNote();
    }
    arp.samplesToNext -= 1.0;
    if (arp.samplesToNext <= 0.0) arpFireStep();
}

void NanoFrogProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    bool haveHostBpm = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
            if (pos->getBpm()) { hostBpm = *pos->getBpm(); haveHostBpm = true; }
    }
    effBpm = haveHostBpm ? hostBpm : (double) pv (apvts, "song_tempo");

    blockVp[0] = collectVoiceParams (0);
    blockVp[1] = collectVoiceParams (1);
    blockPoly[0] = (int) std::round (pv (apvts, "voice_poly"));
    blockPoly[1] = (int) std::round (pv (apvts, "t2_voice_poly"));
    for (int t = 0; t < 2; ++t)
    {
        const char* pre = t == 0 ? "" : "t2_";
        char buf[64];
        auto get = [&] (const char* id) { std::snprintf (buf, sizeof (buf), "%s%s", pre, id); return pv (apvts, buf); };
        const int assign = (int) get ("voice_assign");
        if (assign != blockAssign[t])
            for (auto& v : voices[t]) v.noteOff(); // assign mode changed: let everything ring out
        blockAssign[t] = assign;
        blockTrigger[t] = (int) get ("voice_trigger");
        blockPriority[t] = (int) get ("voice_priority");
        blockUniDet[t] = get ("voice_unidet");
        // envelope knob moves reach notes that are already sounding
        for (auto& v : voices[t]) if (v.isActive()) v.updateEnvelopes (blockVp[t]);
    }
    blockArpOn = pv (apvts, "arp_on") > 0.5f;
    blockLatch = pv (apvts, "arp_latch") > 0.5f;
    if (blockLatch && ! prevLatch) arp.held.assign (heldNotes.begin(), heldNotes.end());
    if (! blockLatch && prevLatch) arp.held.clear();
    prevLatch = blockLatch;

    int n = buffer.getNumSamples();
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (R == nullptr && (int) monoScratch.size() < n) monoScratch.resize ((size_t) n); // host broke its block-size promise
    float* Rp = R ? R : monoScratch.data();

    if (prevArpOn && ! blockArpOn)
    {
        // Arp switched off mid-pattern: ringing arp voices would otherwise
        // sustain forever (their EGs never see noteOff). Release them all.
        for (int t = 0; t < 2; ++t)
            for (auto& v : voices[t]) v.noteOff();
        arp.currentNote = -1; arp.step = 0; arp.running = false; arpOffCountdown = 0.0;
    }
    prevArpOn = blockArpOn;

    // Per-block constants (hoisted out of the sample loop).
    float mix = juce::jlimit (0.0f, 1.0f, pv (apvts, "tmix"));
    const float t1 = std::cos (mix * 1.5707963f), t2 = std::sin (mix * 1.5707963f);
    // Timbre Mix at 0: once the crossfade has settled, Timbre 2 is not rendered
    // (and starts no notes) - half the voice work for a single-timbre sound
    blockT2Off = mix <= 0.0f && tmixSm[1] < 1.0e-4f;
    if (blockT2Off)
        for (auto& v : voices[1]) if (v.isActive()) v.fastKill();
    const float mc = (float) std::exp (-1.0 / (0.005 * fs));
    const float wk = 1.0f - (float) std::exp (-1.0 / (0.008 * fs)); // wheel smoothing

    // Sample-accurate MIDI + arp clock: events land on their own sample.
    auto midiIt = midi.cbegin();
    const auto midiEnd = midi.cend();
    for (int i = 0; i < n; ++i)
    {
        for (; midiIt != midiEnd && (*midiIt).samplePosition <= i; ++midiIt)
            handleMidi ((*midiIt).getMessage());
        arpTick();
        bendSm += (bendSemi - bendSm) * wk;
        modSm += (modWheel - modSm) * wk;

        float s1l = 0, s1r = 0, s2l = 0, s2r = 0;
        for (int v = 0; v < MaxVoices; ++v)
        {
            if (voices[0][v].isActive())
            {
                float vl, vr;
                voices[0][v].renderSample (blockVp[0], bendSm, modSm, vl, vr);
                s1l += vl; s1r += vr;
            }
            if (! blockT2Off && voices[1][v].isActive())
            {
                float vl, vr;
                voices[1][v].renderSample (blockVp[1], bendSm, modSm, vl, vr);
                s2l += vl; s2r += vr;
            }
        }
        // timbre crossfade (smoothed): equal-power, 0 = T1, 1 = T2
        tmixSm[0] = tmixSm[0] * mc + t1 * (1.0f - mc);
        tmixSm[1] = tmixSm[1] * mc + t2 * (1.0f - mc);
        float sl = s1l * tmixSm[0] + s2l * tmixSm[1];
        float sr = s1r * tmixSm[0] + s2r * tmixSm[1];
        // voice-bus normalize (linear: the mixer warmth stage in each voice and
        // the master lookahead limiter carry the saturation / peak control)
        L[i] = sl * 0.35f;
        Rp[i] = sr * 0.35f;
    }
    for (; midiIt != midiEnd; ++midiIt) // stray events past the block end
        handleMidi ((*midiIt).getMessage());

    // FX chain: modfx -> delay -> eq -> output level
    int mtype = (int) pv (apvts, "modfx_type");
    modfx.process (L, Rp, n, mtype, pv (apvts, "modfx_rate"), pv (apvts, "modfx_depth"), pv (apvts, "modfx_mix"));
    int dtype = (int) pv (apvts, "delay_type");
    // DELAY SYNC: a note value at the song / host tempo replaces the TIME knob
    const int dsync = (int) pv (apvts, "delay_sync");
    const float syncMs = dsync > 0 ? (float) (kSyncBeats[juce::jlimit (0, 14, dsync - 1)] * 60000.0 / juce::jmax (20.0, effBpm)) : -1.0f;
    delay.process (L, Rp, n, dtype, pv (apvts, "delay_time"), pv (apvts, "delay_feedback"), pv (apvts, "delay_mix"), syncMs);
    eq.process (L, Rp, n, pv (apvts, "eq_low"), pv (apvts, "eq_high"));
    float out = pv (apvts, "output_level");
    bool limOn = pv (apvts, "limiter") > 0.5f;
    // master: output level -> DC / subsonic cleanup (12 Hz one-pole) -> idle
    // gate (6 s after the last voice, ramp to digital zero) -> lookahead
    // limiter (-0.3 dBFS ceiling, same lookahead when off)
    bool anyActive = false;
    for (int t = 0; t < 2 && ! anyActive; ++t)
        for (auto& v : voices[t]) if (v.isActive()) { anyActive = true; break; }
    if (anyActive) idleSamples = 0; else idleSamples += n;
    const bool shouldClose = idleSamples > (int) (6.0 * fs);
    const float gateStep = 1.0f / (float) (0.05 * fs);
    const float dcR = 1.0f - (float) (2.0 * 3.14159265358979 * 12.0 / fs);
    for (int i = 0; i < n; ++i)
    {
        if (shouldClose) gateG = juce::jmax (0.0f, gateG - gateStep); else gateG = 1.0f;
        float x[2] = { L[i] * out, Rp[i] * out };
        for (int c = 0; c < 2; ++c)
        {
            const float y = x[c] - dcX[c] + dcR * dcY[c];
            dcX[c] = x[c]; dcY[c] = y;
            x[c] = y * gateG;
        }
        L[i] = x[0]; Rp[i] = x[1];
    }
    limiter.process (L, Rp, n, 0.9661f, limOn);
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        peakL = juce::jmax (peakL, std::abs (L[i]));
        peakR = juce::jmax (peakR, std::abs (Rp[i]));
    }
    outSmoothL = outSmoothL * 0.9f + peakL * 0.1f;
    outSmoothR = outSmoothR * 0.9f + peakR * 0.1f;
    meterL.store (outSmoothL);
    meterR.store (outSmoothR);
    pushScopeData (L, Rp, n);
}

void NanoFrogProcessor::setCurrentProgram (int index)
{
    // Never touch host automation state here: some hosts call this on the
    // audio thread, where setValueNotifyingHost would deadlock the DAW.
    // The actual parameter update is deferred to the message thread.
    if (index >= 0 && index < PresetBank::count())
    {
        currentPreset = index;
        pendingPreset.store (index);
        triggerAsyncUpdate();
    }
}

void NanoFrogProcessor::handleAsyncUpdate()
{
    int index = pendingPreset.exchange (-1);
    if (index >= 0 && index < PresetBank::count())
        applyPreset (index);
    juce::String rescan, userRestore;
    {
        juce::ScopedLock sl (pathLock);
        rescan.swapWith (pendingFolderRescan);
        userRestore.swapWith (pendingUserRestore);
    }
    if (rescan.isNotEmpty())
        importUserFolder (rescan);
    if (userRestore.isNotEmpty())
        loadUserPreset (PresetBank::userDir().getChildFile (userRestore));
}

const juce::String NanoFrogProcessor::getProgramName (int index)
{
    if (index >= 0 && index < PresetBank::count()) return PresetBank::get (index).name;
    return "Init";
}

juce::StringArray NanoFrogProcessor::getFactoryNames() const
{
    juce::StringArray a;
    for (int i = 0; i < PresetBank::count(); ++i) a.add (PresetBank::get (i).name);
    return a;
}

int NanoFrogProcessor::mutateGroup (const juce::String& b)
{
    if (b.startsWith ("osc") || b == "fm_amt" || b == "voice_detune") return MG_OSC;
    if (b.startsWith ("mix_")) return MG_MIXER;
    if (b.startsWith ("filter_")) return MG_FILTER;
    if (b.startsWith ("amp_") || b.startsWith ("env")) return MG_AMP_EG;
    if (b.startsWith ("lfo")) return MG_LFO;
    if (b.startsWith ("mod") && ! b.startsWith ("modfx")) return MG_PATCH;
    if (b.startsWith ("modfx") || b.startsWith ("delay") || b.startsWith ("eq_")) return MG_FX;
    if (b.startsWith ("arp")) return MG_ARP;
    return -1;
}

void NanoFrogProcessor::setMutateLocked (int g, bool locked)
{
    int m = (int) apvts.state.getProperty ("mutateLocks", 0);
    apvts.state.setProperty ("mutateLocks", locked ? (m | (1 << g)) : (m & ~(1 << g)), nullptr);
}

void NanoFrogProcessor::mutateCurrentPatch()
{
    // Message thread only (driven by the header MUTATE button). One undo step.
    auto step = undoStep();
    juce::StringArray tags;
    if (currentPreset >= 0 && currentPreset < PresetBank::count())
        tags = PresetBank::get (currentPreset).tags;
    else if (currentUser.isNotEmpty())
    {
        NanoPreset np;
        if (PresetBank::readUser (PresetBank::userDir().getChildFile (currentUser), np))
            tags = np.tags;
    }
    juce::String cat = tags.isEmpty() ? juce::String() : tags[0];
    if (cat == "User" && tags.size() > 1) cat = tags[1]; // user files tag [User, category]
    // Bass/keys/leads/percussive (Hit = stabs, Bell = bells/plucks) voices
    // keep their envelopes: transients carry these sounds.
    bool freezeAdsr = (cat == "Bass" || cat == "Lead" || cat == "Keys"
                       || cat == "Hit" || cat == "Bell");

    auto rawOf = [&] (const juce::String& pid) -> float
    {
        if (auto* pv = apvts.getRawParameterValue (pid)) return pv->load();
        return 0.0f;
    };
    // Audibility gates: mutate flavour, never wake a bypassed module.
    // Shared modules first (no timbre prefix).
    bool modfxLive = ((int) std::round (rawOf ("modfx_type")) != 0
                      && rawOf ("modfx_mix") > 0.0f);
    bool delayLive = rawOf ("delay_mix") > 0.0f;
    struct TimbreGate
    {
        bool lfoLive[2] = {};
        bool modLive[4] = {};
        bool fmLive = false;
        bool oscLive[2] = {};
        bool noiseLive = false;
    };
    auto gateFor = [&] (const juce::String& pre)
    {
        TimbreGate g;
        for (int l = 0; l < 2; ++l)
            g.lfoLive[l] = rawOf (pre + (l == 0 ? "lfo1_depth" : "lfo2_depth")) > 0.0f;
        for (int m = 0; m < 4; ++m)
            g.modLive[m] = rawOf (pre + "mod" + juce::String (m + 1) + "_amt") != 0.0f;
        g.fmLive = rawOf (pre + "fm_amt") > 0.0f;
        for (int o = 0; o < 2; ++o)
            g.oscLive[o] = rawOf (pre + (o == 0 ? "osc1_level" : "osc2_level")) > 0.0f
                        && rawOf (pre + (o == 0 ? "mix_o1" : "mix_o2")) > 0.0f;
        g.noiseLive = rawOf (pre + "mix_noise") > 0.0f;
        if (! g.noiseLive)
            for (int m = 0; m < 4; ++m)
                if ((int) std::round (rawOf (pre + "mod" + juce::String (m + 1) + "_dst")) == 3
                    && rawOf (pre + "mod" + juce::String (m + 1) + "_amt") != 0.0f)
                { g.noiseLive = true; break; }
        return g;
    };
    TimbreGate gate[2] = { gateFor (""), gateFor ("t2_") };

    std::mt19937 rng { std::random_device{}() };
    auto chance = [&] (double p)
    {
        return std::uniform_real_distribution<double> (0.0, 1.0) (rng) < p;
    };
    auto warranted = [&] (const juce::String& base) // structural/master: never touched
    {
        return base == "osc_mod" || base == "filter_type"
            || base.endsWith ("_octave") || base.endsWith ("_pitch") // pitch never moves
            || base == "voice_poly" || base == "voice_portamento"
            || base == "voice_vib" || base == "output_level"
            || base == "master_tune" || base == "song_tempo" || base == "tmix"
            || base == "modfx_type" || base == "delay_type"
            || base.startsWith ("voice_assign") || base == "voice_trigger" || base == "voice_priority"
            || base.endsWith ("_keysync") || base.startsWith ("arp_step") || base == "arp_last_step"
            || base == "bend_range" || base == "delay_sync"
            // pitch never moves: fine tune, detune spread, unison width, vibrato
            || base.endsWith ("_fine") || base == "voice_detune" || base == "voice_unidet"
            || base == "voice_vib";
    };
    auto isAdsr = [&] (const juce::String& base)
    {
        return base == "env1_a" || base == "env1_d" || base == "env1_s"
            || base == "env1_r" || base == "env2_a" || base == "env2_d"
            || base == "env2_s" || base == "env2_r";
    };
    auto spanFor = [&] (const juce::String& base) // normalized-range fraction
    {
        if (base.endsWith ("_fine")) return 0.08; // ~+-4 cents
        if (base == "voice_detune") return 0.15;
        if (base.endsWith ("_shape") || base.endsWith ("_pwm")
            || base.endsWith ("_xmod")) return 0.12;
        if (base.endsWith ("_level") || base.startsWith ("mix_")
            || base == "fm_amt") return 0.10;
        if (base == "filter_cutoff" || base == "filter_reso") return 0.08;
        if (base == "filter_keytrack" || base == "filter_envamt") return 0.12;
        if (base == "amp_pan" || base == "amp_velocity") return 0.10;
        if (base == "amp_drive") return 0.12;
        if (base == "amp_level") return 0.10;
        if (base.endsWith ("_rate") || base.endsWith ("_depth")) return 0.12;
        if (base.endsWith ("_amt")) return 0.12;
        if (base == "delay_time" || base == "delay_feedback"
            || base == "delay_mix" || base == "modfx_rate"
            || base == "modfx_depth" || base == "modfx_mix") return 0.10;
        if (base == "eq_low" || base == "eq_high") return 0.06;
        if (base == "env1_s" || base == "env2_s") return 0.08;
        if (base.startsWith ("env1_") || base.startsWith ("env2_")) return 0.12;
        return 0.06;
    };

    auto ti_of = [] (const juce::String& pid) { return pid.startsWith ("t2_"); };
    for (int i = 0; i < PresetBank::kParamCount; ++i)
    {
        juce::String id = PresetBank::kParamIds[i];
        juce::String base = id.startsWith ("t2_") ? id.substring (3) : id;
        if (warranted (base)) continue;
        if (freezeAdsr && isAdsr (base)) continue;
        if (const int g = mutateGroup (base); g >= 0 && isMutateLocked (g)) continue; // locked section
        if (base.startsWith ("mod") && base.endsWith ("_amt") && base[3] >= '1' && base[3] <= '4')
        {
            // a patch slot aimed at PITCH / OSC2 PITCH keeps its depth
            const int dst = (int) std::round (rawOf ((ti_of (id) ? "t2_" : "") + base.substring (0, 4) + "_dst"));
            if (dst == 0 || dst == 1) continue;
        }
        // Never wake a bypassed module: same modules, different flavour.
        int ti = id.startsWith ("t2_") ? 1 : 0;
        bool gated = false;
        if (base == "modfx_rate" || base == "modfx_depth" || base == "modfx_mix")
            gated = ! modfxLive;
        else if (base == "delay_time" || base == "delay_feedback" || base == "delay_mix")
            gated = ! delayLive;
        else if (base == "lfo1_rate" || base == "lfo1_depth")
            gated = ! gate[ti].lfoLive[0];
        else if (base == "lfo2_rate" || base == "lfo2_depth")
            gated = ! gate[ti].lfoLive[1];
        else if (base.startsWith ("mod") && base.endsWith ("_amt")
                 && base[3] >= '1' && base[3] <= '4')
            gated = ! gate[ti].modLive[base[3] - '1'];
        else if (base == "fm_amt")
            gated = ! gate[ti].fmLive;
        else if (base == "osc1_level" || base == "mix_o1")
            gated = ! gate[ti].oscLive[0];
        else if (base == "osc2_level" || base == "mix_o2")
            gated = ! gate[ti].oscLive[1];
        else if (base == "mix_noise")
            gated = ! gate[ti].noiseLive;
        if (gated) continue;
        auto* p = apvts.getParameter (id);
        if (p == nullptr) continue;
        p->beginChangeGesture();
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
        {
            juce::ignoreUnused (f);
            float v = p->getValue();
            float d = (float) (std::uniform_real_distribution<double> (-1.0, 1.0) (rng)
                               * spanFor (base));
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v + d));
        }
        else if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
        {
            juce::ignoreUnused (c);
            int n = 0;
            if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (
                    apvts.getParameter (id)))
                n = ch->choices.size();
            int cur = (int) std::round (p->convertFrom0to1 (p->getValue()));
            int next = cur;
            // Osc selectors may step musically (never pitch); everything else
            // stays put.
            if ((base == "osc1_wave" || base == "osc2_wave") && chance (0.12))
                next = (int) (std::uniform_int_distribution<int> (0, 6) (rng));
            else if ((base == "osc1_digital" || base == "osc2_digital") && chance (0.25))
                next = juce::jlimit (0, n - 1, cur
                                     + (chance (0.5) ? 1 : -1)
                                     * (int) (std::uniform_int_distribution<int> (1, 8) (rng)));
            if (next != cur)
                p->setValueNotifyingHost (p->convertTo0to1 ((float) next));
        }
        p->endChangeGesture();
    }
}

void NanoFrogProcessor::applyPreset (int index)
{
    auto step = undoStep(); // a preset load is one undo step
    PresetBank::apply (apvts, PresetBank::get (index).values);
    currentPreset = index;
    currentUser.clear(); // back on a factory voice
}

bool NanoFrogProcessor::loadUserPreset (const juce::File& file)
{
    NanoPreset np;
    if (! PresetBank::readUser (file, np)) return false;
    auto step = undoStep();
    PresetBank::apply (apvts, np.values);
    currentPreset = -1;
    currentUser = file.getFileName();
    return true;
}

void NanoFrogProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetVersion", 1, nullptr);
    state.setProperty ("currentPreset", currentPreset, nullptr);
    state.setProperty ("userPreset", currentUser, nullptr);
    state.setProperty ("userWavePath", userWavePath, nullptr);
    state.setProperty ("userFolderPath", userFolderPath, nullptr);
    if (bankCount.load() == 1 && userWavePath.isNotEmpty())
    {
        // Single waves are embedded (8 KB); folder banks are rescanned by path.
        juce::MemoryOutputStream mos;
        juce::Base64::convertToBase64 (mos, bankBuf[bankActive.load()][0], sizeof (bankBuf[0][0]));
        state.setProperty ("userWave", mos.toString(), nullptr);
        state.setProperty ("userWaveName", bankNames[0], nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void NanoFrogProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            // projects from before VOICE ASSIGN: a polyphony of 1 was the mono voice
            for (auto pre : { "", "t2_" })
            {
                const juce::String aid = juce::String (pre) + "voice_assign", pid = juce::String (pre) + "voice_poly";
                bool hasAssign = false; float poly = 2.0f;
                for (auto* c : xml->getChildIterator())
                {
                    if (c->getStringAttribute ("id") == aid) hasAssign = true;
                    if (c->getStringAttribute ("id") == pid) poly = (float) c->getDoubleAttribute ("value", 2.0);
                }
                if (! hasAssign && poly < 1.5f)
                    if (auto* p = apvts.getParameter (aid)) p->setValueNotifyingHost (p->convertTo0to1 (1.0f));
            }
            currentPreset = (int) xml->getIntAttribute ("currentPreset", 0);
            currentUser.clear();
            juce::String userFile = xml->getStringAttribute ("userPreset");
            if (userFile.isNotEmpty())
            {
                juce::File uf = PresetBank::userDir().getChildFile (userFile);
                if (uf.existsAsFile())
                {
                    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
                        loadUserPreset (uf); // restores user mode + name
                    else
                    {
                        // File I/O off the audio thread: defer to the message thread.
                        juce::ScopedLock sl (pathLock);
                        pendingUserRestore = userFile;
                        triggerAsyncUpdate();
                    }
                }
            }
            juce::String folderPath = xml->getStringAttribute ("userFolderPath");
            if (folderPath.isNotEmpty() && juce::File (folderPath).isDirectory())
            {
                // Folder rescan does file I/O: defer to the message thread.
                juce::ScopedLock sl (pathLock);
                pendingFolderRescan = folderPath;
                triggerAsyncUpdate();
                return;
            }
            juce::String b64 = xml->getStringAttribute ("userWave");
            if (b64.isNotEmpty())
            {
                juce::MemoryOutputStream mos;
                if (juce::Base64::convertFromBase64 (mos, b64)
                    && mos.getDataSize() == sizeof (bankBuf[0][0]))
                {
                    int inactive = 1 - bankActive.load();
                    std::memcpy (bankBuf[inactive][0], mos.getData(), sizeof (bankBuf[0][0]));
                    buildUserMips (bankBuf[inactive][0], bankMip[inactive][0]);
                    bankNames[0] = xml->getStringAttribute ("userWaveName", "Restored wave");
                    bankActive.store (inactive);
                    bankCount.store (1);
                    userDisplayName = bankNames[0];
                    userWavePath = xml->getStringAttribute ("userWavePath");
                    userFolderPath = {};
                }
            }
        }
}

// Extract one loopable cycle from arbitrary mono audio (message thread only).
// Length rule: files at/below single-cycle size are taken whole; larger
// files are period-detected and sliced to exactly one cycle, so the table
// never holds more than one period. Detection clusters zero-crossing
// intervals (plus pair sums for octave-doubled crossings), scores candidates
// by harmonic summation of autocorrelation (robust to dominant harmonics
// and beating), floors implausible sub-12-frame periods, and falls back to
// the crossing median. Either way the result is resampled to 2048 with a
// seam-closing crossfade and normalised.
static constexpr int kSingleMax = 4096;

bool NanoFrogProcessor::makeSingleCycle (const float* monoSrc, int numFrames, float* dst2048)
{
    if (monoSrc == nullptr || dst2048 == nullptr || numFrames < 64) return false;
    const int N = 2048;
    // DC-remove + normalise a scratch copy for robust detection
    std::vector<float> norm ((size_t) numFrames);
    double sum = 0.0;
    for (int i = 0; i < numFrames; ++i) sum += monoSrc[i];
    float mean = float (sum / numFrames);
    float peak = 1e-6f;
    for (int i = 0; i < numFrames; ++i)
    {
        norm[(size_t) i] = monoSrc[i] - mean;
        peak = std::max (peak, std::abs (norm[(size_t) i]));
    }
    for (int i = 0; i < numFrames; ++i) norm[(size_t) i] /= peak;
    auto at = [&] (int i)
    {
        if (i < 0) i = 0;
        if (i >= numFrames) i = numFrames - 1;
        return norm[(size_t) i];
    };
    auto sampleSpan = [&] (double start, double len)
    {
        for (int i = 0; i < N; ++i)
        {
            double pos = start + i * len / N;
            int i0 = (int) pos;
            if (i0 < 0) i0 = 0;
            if (i0 >= numFrames) i0 = numFrames - 1;
            int i1 = i0 + 1 < numFrames - 1 ? i0 + 1 : numFrames - 1;
            float fr = float (pos - std::floor (pos));
            dst2048[i] = at (i0) * (1.0f - fr) + at (i1) * fr;
        }
    };
    if (numFrames <= kSingleMax)
    {
        sampleSpan (0.0, (double) numFrames);
    }
    else
    {
        // Period detection: rising zero crossings -> candidate intervals
        // (plus adjacent-pair sums, which catch octave-doubled crossings
        // from strong even harmonics), verified by normalized autocorrelation.
        // The smallest well-correlated candidate wins; median is the fallback.
        // This keeps harmonically rich waves at concert pitch instead of
        // locking onto beating/subharmonics (which sound detuned, or loop
        // more than one cycle).
        const int scan = std::min (numFrames, 1 << 18);
        double cross[256]; int nCross = 0;
        bool armed = at (0) < -0.02f;
        for (int i = 1; i < scan && nCross < 256; ++i)
        {
            float b = at (i);
            if (armed && b > 0.02f)
            {
                float a = at (i - 1);
                cross[nCross++] = (i - 1) + (-a / (b - a));
                armed = false;
            }
            else if (b < -0.02f) armed = true;
        }
        double cand[16]; int nCand = 0;
        auto addCand = [&] (double v)
        {
            // Musical-plausibility floor: wave-ROM fundamentals below ~12
            // frames (>1.1 kHz at these rates) are essentially always
            // upper-harmonic artifacts, never the true period.
            if (v < 12.0 || v > scan / 2) return;
            for (int i = 0; i < nCand; ++i)
                if (std::abs (cand[i] - v) < v * 0.1) return;
            if (nCand < 16) cand[nCand++] = v;
        };
        if (nCross >= 4)
        {
            double iv[255]; int nIv = 0;
            for (int i = 0; i + 1 < nCross; ++i) iv[nIv++] = cross[i + 1] - cross[i];
            std::sort (iv, iv + nIv);
            for (int i = 0; i < nIv;)
            {
                int j = i;
                while (j < nIv && iv[j] < iv[i] * 1.1) ++j;
                if (j - i >= 3) addCand (iv[(i + j) / 2]);
                i = j;
            }
            double ps[255]; int nPs = 0;
            for (int i = 0; i + 1 < nIv && nPs < 255; ++i) ps[nPs++] = iv[i] + iv[i + 1];
            std::sort (ps, ps + nPs);
            for (int i = 0; i < nPs;)
            {
                int j = i;
                while (j < nPs && ps[j] < ps[i] * 1.1) ++j;
                if (j - i >= 2) addCand (ps[(i + j) / 2]);
                i = j;
            }
        }
        double energy = 1e-9;
        for (int i = 0; i < scan; ++i) energy += (double) at (i) * at (i);
        auto acorr = [&] (int lag) -> double
        {
            if (lag < 4 || lag >= scan) return 0.0;
            double num = 0.0;
            for (int i = 0; i + lag < scan; ++i) num += (double) at (i) * at (i + lag);
            return num / energy;
        };
        // Harmonic summation: the true fundamental's multiples all correlate,
        // so it outscores both a dominant upper harmonic and slow beating.
        double best = 0.0, bestScore = 0.0;
        std::sort (cand, cand + nCand);
        for (int ci = 0; ci < nCand; ++ci)
        {
            int base = (int) std::round (cand[ci]);
            double s = acorr (base) + acorr (base * 2) * 0.5 + acorr (base * 3) / 3.0;
            if (s > bestScore) { bestScore = s; best = cand[ci]; }
        }
        double cyc = 0.0;
        // slice start: first crossing a tenth into the file skips leading
        // clicks/transients; fall back to the very first crossing.
        double start = nCross > 0 ? cross[0] : 0.0;
        for (int i = 0; i < nCross; ++i)
            if (cross[i] >= scan / 10) { start = cross[i]; break; }
        if (bestScore > 1.0) cyc = best;
        if (cyc == 0.0 && nCross >= 3)
        {
            double periods[16]; int nP = 0;
            for (int i = 0; i + 1 < nCross && nP < 16; ++i)
                periods[nP++] = cross[i + 1] - cross[i];
            std::sort (periods, periods + nP);
            cyc = periods[nP / 2];
            start = cross[0];
        }
        if (cyc >= 4.0 && start + cyc < numFrames)
            sampleSpan (start, cyc);
        else
            sampleSpan (0.0, (double) std::min (numFrames, kSingleMax));
    }
    // loop crossfade: bend the tail exactly onto the loop start
    constexpr int XF = 128;
    float c0 = dst2048[0];
    for (int i = 0; i < XF; ++i)
    {
        float w = float (i + 1) / XF;
        dst2048[N - XF + i] = dst2048[N - XF + i] * (1.0f - w) + c0 * w;
    }
    double s2 = 0.0;
    for (int i = 0; i < N; ++i) s2 += dst2048[i];
    float m2 = float (s2 / N);
    float pk = 1e-6f;
    for (int i = 0; i < N; ++i) { dst2048[i] -= m2; pk = std::max (pk, std::abs (dst2048[i])); }
    float g = 0.9f / pk;
    for (int i = 0; i < N; ++i) dst2048[i] *= g;
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NanoFrogProcessor(); }

// Band-limited copies of a 2048-sample user wave (UserMip layout): level k
// keeps harmonics 1 .. 1024 >> k, resynthesised by an inverse FFT at the
// level's own table length (message thread).
void NanoFrogProcessor::buildUserMips (const float* src, float* dst)
{
    juce::dsp::FFT fwd (11); // 2048
    std::vector<float> spec (4096, 0.0f);
    std::copy (src, src + 2048, spec.begin());
    fwd.performRealOnlyForwardTransform (spec.data());
    for (int k = 0; k < OscCore::kMipLevels; ++k)
    {
        const int len = OscCore::mipLen (k);
        const int order = (int) std::lround (std::log2 ((double) len));
        const int maxH = juce::jmin (1024 >> k, len / 2 - 1);
        juce::dsp::FFT inv (order);
        std::vector<float> b ((size_t) len * 2, 0.0f);
        for (int h = 1; h <= maxH; ++h)
        {
            b[(size_t) (2 * h)] = spec[(size_t) (2 * h)];
            b[(size_t) (2 * h + 1)] = spec[(size_t) (2 * h + 1)];
            b[(size_t) (2 * (len - h))] = spec[(size_t) (2 * h)];          // conjugate half, for
            b[(size_t) (2 * (len - h) + 1)] = -spec[(size_t) (2 * h + 1)]; // backends that read it
        }
        inv.performRealOnlyInverseTransform (b.data());
        // the inverse's scaling differs between FFT backends: measure it
        std::vector<float> imp ((size_t) len * 2, 0.0f);
        imp[0] = 1.0f;
        inv.performRealOnlyForwardTransform (imp.data());
        inv.performRealOnlyInverseTransform (imp.data());
        const float toLevel = (std::abs (imp[0]) > 1.0e-12f ? 1.0f / imp[0] : 1.0f) * (float) len / 2048.0f;
        float* lv = dst + OscCore::mipOffset (k);
        for (int n = 0; n < len; ++n) lv[n] = b[(size_t) n] * toLevel;
    }
}

bool NanoFrogProcessor::importUserWave (const float* monoSrc, int numFrames, const juce::String& name)
{
    int inactive = 1 - bankActive.load();
    if (! makeSingleCycle (monoSrc, numFrames, bankBuf[inactive][0])) return false;
    buildUserMips (bankBuf[inactive][0], bankMip[inactive][0]);
    bankNames[0] = name;
    bankActive.store (inactive);
    bankCount.store (1);
    userDisplayName = name;
    userWavePath = name;
    userFolderPath = {};
    return true;
}

struct FileNameCompare
{
    static int compareElements (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareIgnoreCase (b.getFileName());
    }
};

int NanoFrogProcessor::importUserFolder (const juce::String& folderPath)
{
    juce::File dir (folderPath);
    if (! dir.isDirectory()) return -1;
    juce::Array<juce::File> files;
    dir.findChildFiles (files, juce::File::findFiles, true, "*.wav;*.aif;*.aiff");
    FileNameCompare cmp;
    files.sort (cmp);
    int inactive = 1 - bankActive.load();
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    juce::String names[kBankMax];
    int loaded = 0;
    for (auto& f : files)
    {
        if (loaded >= kBankMax) break;
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
        if (reader == nullptr || reader->numChannels < 1 || reader->lengthInSamples < 64)
            continue;
        int n = (int) juce::jmin<int64_t> (reader->lengthInSamples, 1 << 20);
        juce::AudioBuffer<float> buf (1, n);
        reader->read (&buf, 0, n, 0, true, false);
        if (! makeSingleCycle (buf.getReadPointer (0), n, bankBuf[inactive][loaded]))
            continue;
        buildUserMips (bankBuf[inactive][loaded], bankMip[inactive][loaded]);
        names[loaded] = f.getFileNameWithoutExtension();
        ++loaded;
    }
    if (loaded == 0) return -1;
    for (int i = 0; i < loaded; ++i) bankNames[i] = names[i];
    bankActive.store (inactive);
    bankCount.store (loaded);
    userDisplayName = dir.getFileName() + " (" + juce::String (loaded) + ")";
    userFolderPath = dir.getFullPathName();
    userWavePath = {};
    return loaded;
}

juce::String NanoFrogProcessor::getUserBankEntryName (int i) const
{
    if (i >= 0 && i < kBankMax) return bankNames[i];
    return {};
}

void NanoFrogProcessor::pushScopeData (const float* L, const float* R, int n)
{
    // Writer never resets the FIFO (that races the UI reader): when the UI
    // isn't draining, newer samples are simply dropped.
    int m = std::min (n, scopeFifo.getFreeSpace());
    if (m <= 0) return;
    int s1, size1, s2, size2;
    scopeFifo.prepareToWrite (m, s1, size1, s2, size2);
    for (int i = 0; i < size1; ++i) scopeBuf[s1 + i] = 0.5f * (L[i] + R[i]);
    for (int i = 0; i < size2; ++i) scopeBuf[s2 + i] = 0.5f * (L[size1 + i] + R[size1 + i]);
    scopeFifo.finishedWrite (m);
}

void NanoFrogProcessor::pullScopeData (float* dst, int& numAvailable)
{
    int ready = scopeFifo.getNumReady();
    int m = std::min (ready, numAvailable);
    int s1, size1, s2, size2;
    scopeFifo.prepareToRead (m, s1, size1, s2, size2);
    for (int i = 0; i < size1; ++i) dst[i] = scopeBuf[s1 + i];
    for (int i = 0; i < size2; ++i) dst[size1 + i] = scopeBuf[s2 + i];
    scopeFifo.finishedRead (m);
    numAvailable = m;
}

juce::AudioProcessorEditor* NanoFrogProcessor::createEditor() { return new NanoFrogEditor (*this); }
