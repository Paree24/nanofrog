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
    if (PresetBank::isFallback())
        PresetBank::installFallback (PresetBank::capture (apvts)); // Init from live defaults
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
    }
    juce::StringArray srcs { "ENV1","ENV2","LFO1","LFO2","Velocity","KeyTrack","PitchBend","ModWheel","Random" };
    juce::StringArray dsts { "OSC1 Pitch","OSC2 Pitch","OSC1 Shape","Noise Level","Filter Cutoff","Filter Reso","Amp Level","Pan","LFO1 Rate","LFO2 Rate" };
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
    C ("", "arp_mode","Arp Mode", {"Up","Down","AltUp","AltDown","Random","Trigger"}, 0);
    C ("", "arp_rate","Arp Rate", {"1/4","1/8","1/16","1/8T","1/16T","1/32"}, 2);
    F ("", "arp_gate","Arp Gate", 0, 1, 0.8f);
    F ("", "arp_octaves","Arp Octaves", 1, 4, 1);
    F ("", "arp_swing","Arp Swing", -1, 1, 0);
    B ("", "arp_latch","Arp Latch", false);
    auto addVoice = [&] (const juce::String& pre)
    {
        F (pre, "voice_poly","Polyphony", 1, 8, 1);
        F (pre, "voice_detune","Detune", 0, 1, 0.0f);
        F (pre, "voice_portamento","Portamento", 0, 1, 0);
        F (pre, "voice_vib","Vibrato", 0, 1, 0.0f);
    };
    addVoice ("");
    addVoice ("t2_");
    F ("", "output_level","Output Level", 0, 1.25f, 1.0f);
    F ("", "master_tune","Master Tune", -50, 50, 0);
    F ("", "song_tempo","Song Tempo", 20, 300, 120);
    // ---- shared: timbre crossfade (0 = Timbre 1, 1 = Timbre 2) ----
    F ("", "tmix","Timbre Mix", 0, 1, 0.0f);
    // ---- output safety: brickwall-ish ceiling post output gain ----
    B ("", "limiter","Limiter", true);
    return layout;
}

static inline float pv (juce::AudioProcessorValueTreeState& s, const juce::String& id)
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
    juce::String pre = timbre == 0 ? "" : "t2_";
    auto P = [&] (const char* id) -> float { return pv (apvts, pre + id); };
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
    p.unison = ((int) std::round (P ("voice_poly")) == 1 && p.detune > 0.01f);
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
                p.userTable = bankBuf[bankActive.load()][juce::jlimit (0, bc - 1, b1)];
            if (p.osc2Wave == 6 || p.osc2Wave == 4)
                p.userTable2 = bankBuf[bankActive.load()][juce::jlimit (0, bc - 1, b2)];
        }
    }
    return p;
}

void NanoFrogProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (int t = 0; t < 2; ++t)
        for (auto& v : voices[t]) v.setSampleRate (sampleRate);
    modfx.setSampleRate (sampleRate);
    delay.setSampleRate (sampleRate);
    eq.setSampleRate (sampleRate);
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

void NanoFrogProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        int n = m.getNoteNumber();
        sustainHeld[n] = 0;
        if (std::find (heldNotes.begin(), heldNotes.end(), n) == heldNotes.end())
            heldNotes.push_back (n);
        bool arpOn = pv (apvts, "arp_on") > 0.5f;
        if (! arpOn)
        {
            float vel = m.getVelocity() / 127.0f;
            for (int t = 0; t < 2; ++t)
            {
                VoiceParams p = collectVoiceParams (t);
                int poly = (int) std::round (
                    pv (apvts, t == 0 ? "voice_poly" : "t2_voice_poly"));
                int vi = allocateVoice (t, n, poly);
                voices[t][vi].noteOn (n, vel, p, false);
            }
        }
    }
    else if (m.isNoteOff())
    {
        int n = m.getNoteNumber();
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), n), heldNotes.end());
        bool arpOn = pv (apvts, "arp_on") > 0.5f;
        bool latch = pv (apvts, "arp_latch") > 0.5f;
        if (! arpOn || (! latch && heldNotes.empty()))
        {
            if (! arpOn)
            {
                if (sustainPedal > 0.5f) sustainHeld[n] = 1; // pedal: defer release
                else
                {
                    sustainHeld[n] = 0;
                    for (int t = 0; t < 2; ++t)
                        for (auto& v : voices[t])
                            if (v.isActive() && v.getNote() == n) v.noteOff();
                }
            }
        }
        if (heldNotes.empty() && !(arpOn && latch))
        {
            // let arp stop; silence stuck arp note
            if (arp.currentNote >= 0)
            {
                for (int t = 0; t < 2; ++t)
                    for (auto& v : voices[t])
                        if (v.isActive() && v.getNote() == arp.currentNote) v.noteOff();
                arp.currentNote = -1;
            }
        }
    }
    else if (m.isPitchWheel())
    {
        // LESSONS.md #13: center is 8192, with a deadband so worn wheels pin exact zero.
        int raw = m.getPitchWheelValue();
        bendSemi = (std::abs (raw - 8192) <= 48) ? 0.0f : (raw - 8192) / 8192.0f * 2.0f; // +/-2 semitones
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
                        for (int t = 0; t < 2; ++t)
                            for (auto& v : voices[t])
                                if (v.isActive() && v.getNote() == i) v.noteOff();
                        sustainHeld[i] = 0;
                    }
                }
            }
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        heldNotes.clear();
        std::memset (sustainHeld, 0, sizeof (sustainHeld));
        for (int t = 0; t < 2; ++t)
            for (auto& v : voices[t]) v.noteOff();
        arp.currentNote = -1;
    }
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
    for (const auto meta : midi)
        handleMidi (meta.getMessage());

    VoiceParams vp[2] = { collectVoiceParams (0), collectVoiceParams (1) };
    int polyBank[2] = {
        (int) std::round (pv (apvts, "voice_poly")),
        (int) std::round (pv (apvts, "t2_voice_poly"))
    };
    int n = buffer.getNumSamples();
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    std::vector<float> monoR (n, 0.0f);
    float* Rp = R ? R : monoR.data();

    // --- arpeggiator stepper (block-quantized) ---
    bool arpOn = pv (apvts, "arp_on") > 0.5f;
    if (prevArpOn && ! arpOn)
    {
        // Arp switched off mid-pattern: ringing arp voices would otherwise
        // sustain forever (their EGs never see noteOff). Release them all.
        for (int t = 0; t < 2; ++t)
            for (auto& v : voices[t]) v.noteOff();
        arp.currentNote = -1; arp.step = 0; arpOffCountdown = 0.0;
    }
    prevArpOn = arpOn;
    if (! arpOn || heldNotes.empty()) arpOffCountdown = 0.0; // never cut manual notes
    if (arpOn && ! heldNotes.empty())
    {
        static const double beats[6] = { 1.0, 0.5, 0.25, 1.0/3.0, 1.0/6.0, 0.125 };
        int ri = juce::jlimit (0, 5, (int) pv (apvts, "arp_rate"));
        double stepBeats = beats[ri];
        double stepSec = 60.0 / juce::jmax (20.0, effBpm) * stepBeats;
        int stepSamples = juce::jmax (32, (int) (stepSec * fs));
        int mode = (int) pv (apvts, "arp_mode");
        int octs = juce::jlimit (1, 4, (int) std::round (pv (apvts, "arp_octaves")));
        // build pattern
        std::vector<int> notes = heldNotes;
        std::sort (notes.begin(), notes.end());
        std::vector<int> pattern;
        if (mode == 0) { for (int o = 0; o < octs; ++o) for (int x : notes) pattern.push_back (x + o * 12); }
        else if (mode == 1) { for (int o = octs - 1; o >= 0; --o) for (int i = (int) notes.size() - 1; i >= 0; --i) pattern.push_back (notes[i] + o * 12); }
        else if (mode == 2 || mode == 3)
        {
            std::vector<int> up; for (int o = 0; o < octs; ++o) for (int x : notes) up.push_back (x + o * 12);
            pattern = up;
            for (int i = (int) up.size() - 2; i > 0; --i) pattern.push_back (up[i]);
            if (mode == 3) std::reverse (pattern.begin(), pattern.end());
        }
        else if (mode == 4) { pattern = notes; }
        else { pattern = notes; } // trigger: chord
        if (arp.step >= (int) pattern.size()) arp.step = 0;
        // simple: advance one step per block if enough samples elapsed (approx via counter)
        arp.nextStepSample -= n;
        if (arp.nextStepSample <= 0)
        {
            arp.nextStepSample = stepSamples;
            int idx = (mode == 4) ? (rand() % pattern.size()) : (arp.step % pattern.size());
            int nn = juce::jlimit (0, 127, pattern[idx]);
            if (mode == 5) // trigger chord
            {
                for (int t = 0; t < 2; ++t)
                    for (auto& v : voices[t]) v.noteOff();
                for (int x : notes)
                    for (int t = 0; t < 2; ++t)
                    {
                        int vi = allocateVoice (t, x, polyBank[t]);
                        voices[t][vi].noteOn (x, 0.9f, vp[t], false);
                    }
                arp.currentNote = -2;
            }
            else
            {
                if (arp.currentNote >= 0)
                    for (int t = 0; t < 2; ++t)
                        for (auto& v : voices[t])
                            if (v.isActive() && v.getNote() == arp.currentNote) v.noteOff();
                for (int t = 0; t < 2; ++t)
                {
                    int vi = allocateVoice (t, nn, polyBank[t]);
                    voices[t][vi].noteOn (nn, 0.9f, vp[t], false);
                }
                arp.currentNote = nn;
            }
            arp.step++;
            arpOffCountdown = stepSamples
                * (double) juce::jlimit (0.05f, 1.0f, pv (apvts, "arp_gate"));
        }
    }

    for (int i = 0; i < n; ++i)
    {
        if (arpOffCountdown > 0.0)
        {
            arpOffCountdown -= 1.0;
            if (arpOffCountdown <= 0.0 && arp.currentNote != -1)
            {
                if (arp.currentNote == -2)
                {
                    for (int t = 0; t < 2; ++t)
                        for (auto& v : voices[t]) v.noteOff();
                }
                else
                    for (int t = 0; t < 2; ++t)
                        for (auto& v : voices[t])
                            if (v.isActive() && v.getNote() == arp.currentNote) v.noteOff();
                arp.currentNote = -1;
            }
        }
        float s1l = 0, s1r = 0, s2l = 0, s2r = 0;
        for (int v = 0; v < MaxVoices; ++v)
        {
            if (voices[0][v].isActive())
            {
                float vl, vr;
                voices[0][v].renderSample (vp[0], bendSemi, modWheel, vl, vr);
                s1l += vl; s1r += vr;
            }
            if (voices[1][v].isActive())
            {
                float vl, vr;
                voices[1][v].renderSample (vp[1], bendSemi, modWheel, vl, vr);
                s2l += vl; s2r += vr;
            }
        }
        // timbre crossfade (smoothed): equal-power, 0 = T1, 1 = T2
        float mix = juce::jlimit (0.0f, 1.0f, pv (apvts, "tmix"));
        float t1 = std::cos (mix * 1.5707963f), t2 = std::sin (mix * 1.5707963f);
        double mc = std::exp (-1.0 / (0.005 * fs));
        tmixSm[0] = tmixSm[0] * (float) mc + t1 * (float) (1.0 - mc);
        tmixSm[1] = tmixSm[1] * (float) mc + t2 * (float) (1.0 - mc);
        float sl = s1l * tmixSm[0] + s2l * tmixSm[1];
        float sr = s1r * tmixSm[0] + s2r * tmixSm[1];
        // gentle voice-bus normalize + soft saturation (tames high-reso peaks)
        float ml = std::tanh (sl * 0.35f);
        float mr = std::tanh (sr * 0.35f);
        L[i] = ml;
        Rp[i] = mr;
    }

    // FX chain: modfx -> delay -> eq -> output level
    int mtype = (int) pv (apvts, "modfx_type");
    modfx.process (L, Rp, n, mtype, pv (apvts, "modfx_rate"), pv (apvts, "modfx_depth"), pv (apvts, "modfx_mix"));
    int dtype = (int) pv (apvts, "delay_type");
    delay.process (L, Rp, n, dtype, pv (apvts, "delay_time"), pv (apvts, "delay_feedback"), pv (apvts, "delay_mix"));
    eq.process (L, Rp, n, pv (apvts, "eq_low"), pv (apvts, "eq_high"));
    float out = pv (apvts, "output_level");
    bool limOn = pv (apvts, "limiter") > 0.5f;
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float sl = L[i] * out, sr = Rp[i] * out;
        if (limOn) { sl = std::tanh (sl); sr = std::tanh (sr); }
        L[i] = sl; Rp[i] = sr;
        peakL = juce::jmax (peakL, std::abs (sl));
        peakR = juce::jmax (peakR, std::abs (sr));
    }
    outSmoothL = outSmoothL * 0.9f + peakL * 0.1f;
    outSmoothR = outSmoothR * 0.9f + peakR * 0.1f;
    meterL.store (outSmoothL);
    meterR.store (outSmoothR);
    pushScopeData (L, Rp, n);
    if (R == nullptr) buffer.copyFrom (1, 0, buffer, 0, 0, n);
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

void NanoFrogProcessor::mutateCurrentPatch()
{
    // Message thread only (driven by the header MUTATE button).
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
            || base == "voice_poly" || base == "voice_portamento"
            || base == "voice_vib" || base == "output_level"
            || base == "master_tune" || base == "song_tempo" || base == "tmix"
            || base == "modfx_type" || base == "delay_type";
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

    for (int i = 0; i < PresetBank::kParamCount; ++i)
    {
        juce::String id = PresetBank::kParamIds[i];
        juce::String base = id.startsWith ("t2_") ? id.substring (3) : id;
        if (warranted (base)) continue;
        if (freezeAdsr && isAdsr (base)) continue;
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
            // Osc selectors may step musically; everything else stays put.
            if ((base == "osc1_wave" || base == "osc2_wave") && chance (0.12))
                next = (int) (std::uniform_int_distribution<int> (0, 6) (rng));
            else if ((base == "osc1_octave" || base == "osc2_octave") && chance (0.08))
                next = juce::jlimit (-2, 2, cur + (chance (0.5) ? 1 : -1));
            else if ((base == "osc1_pitch" || base == "osc2_pitch") && chance (0.12))
                next = juce::jlimit (-12, 12, cur + (chance (0.5) ? 1 : -1)
                                     * (chance (0.25) ? 2 : 1));
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
    PresetBank::apply (apvts, PresetBank::get (index).values);
    currentPreset = index;
    currentUser.clear(); // back on a factory voice
}

bool NanoFrogProcessor::loadUserPreset (const juce::File& file)
{
    NanoPreset np;
    if (! PresetBank::readUser (file, np)) return false;
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

bool NanoFrogProcessor::importUserWave (const float* monoSrc, int numFrames, const juce::String& name)
{
    int inactive = 1 - bankActive.load();
    if (! makeSingleCycle (monoSrc, numFrames, bankBuf[inactive][0])) return false;
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
    int free = scopeFifo.getFreeSpace();
    if (free < n) { scopeFifo.reset(); free = scopeFifo.getFreeSpace(); }
    int m = std::min (n, free);
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
