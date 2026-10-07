#pragma once
#include "Oscillator.h"
#include "FilterEnv.h"
#include <cmath>

// Snapshot of all voice-relevant params for one block.
struct VoiceParams
{
    int osc1Wave = 0, osc1Digital = 0, osc1Oct = 0, osc1Pitch = 0;
    float osc1Fine = 0, osc1Level = 0.7f, osc1Shape = 0, osc1Pwm = 0, osc1Xmod = 0;
    int oscMod = 0; // 0 off 1 ring 2 sync 3 ringsync
    int osc2Wave = 1, osc2Oct = 0, osc2Pitch = 0;
    float osc2Fine = 0, osc2Level = 0.35f;
    float mixO1 = 0.8f, mixO2 = 0.4f, mixN = 0.0f;
    float fmAmt = 0.0f; // osc2 -> osc1 exponential FM amount (repurposed dead mix_audio)
    int filtType = 0;
    float filtCut = 0.6f, filtRes = 0.2f, filtKey = 0.0f, filtEnv = 0.25f;
    float ampLevel = 0.8f, ampPan = 0.0f, ampVel = 0.5f;
    bool ampDist = false; float ampDrive = 0.25f;
    float e1a = 0.005f, e1d = 0.3f, e1s = 0.0f, e1r = 0.3f;
    float e2a = 0.005f, e2d = 0.3f, e2s = 0.7f, e2r = 0.3f;
    int lfo1Wave = 1, lfo2Wave = 1;
    double lfo1Rate = 1.0, lfo2Rate = 1.0;
    float lfo1Depth = 0.0f, lfo2Depth = 0.0f;
    int mSrc[4] = { 2, 0, 3, 4 };
    int mDst[4] = { 4, 0, 1, 6 };
    float mAmt[4] = { 0, 0, 0, 0 };
    float portamento = 0.0f; // 0..1 -> seconds
    float vibAmt = 0.0f;
    float detune = 0.0f; // 0..1 osc spread (Lesson: never leave a knob unwired)
    float osc2Shape = 0.0f, osc2Pwm = 0.5f, osc2Xmod = 0.0f;
    int osc2Digital = 0;
    const float* userTable = nullptr; // osc1 bank entry (null = none)
    const float* userTable2 = nullptr; // osc2 bank entry (null = none)
    double songTempo = 120.0; // beats for tempo-synced LFOs / arpeggiator
    int lfo1Note = -1, lfo2Note = -1; // T-5 sync division, -1 = free rate
    bool unison = false; // 2-tap detuned stack (mono unison mode)
};

// Tempo-sync divisions (MIDI table T-5), in beats. Index = sync-note value.
static constexpr double kSyncBeats[15] = {
    4.0, 3.0, 8.0 / 3.0, 2.0, 1.5, 4.0 / 3.0, 1.0, 0.75,
    2.0 / 3.0, 0.5, 0.375, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125
};

static inline double noteToFreq (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }

class NanoVoice
{
public:
    void setSampleRate (double sr)
    {
        fs = sr;
        osc1.setSampleRate (sr); osc1b.setSampleRate (sr);
        osc2.setSampleRate (sr); osc2b.setSampleRate (sr);
        noiseOsc.setSampleRate (sr);
        filt.setSampleRate (sr);
        eg1.setSampleRate (sr); eg2.setSampleRate (sr);
        lfo1.setSampleRate (sr); lfo2.setSampleRate (sr);
        reset();
    }

    void reset()
    {
        osc1.resetPhase(); osc1b.resetPhase(); osc2.resetPhase(); osc2b.resetPhase();
        filt.reset(); eg1.reset(); eg2.reset(); lfo1.reset(); lfo2.reset();
        active = false; note = -1; velocity = 0; fade = 1.0f;
        stealing = false; pendingNote = -1; stealGain = 0.0f;
        lfo1MulSm = lfo2MulSm = 0.0f; fmPhase = 0.0f;
        curFreq1 = curFreq2 = 440.0; cutSm = 8000; ampSm = 1.0; panSmL = panSmR = 0.7f;
        age = 0; randConst = (float) rand() / RAND_MAX * 2.0f - 1.0f;
    }

    void noteOn (int midiNote, float vel, const VoiceParams& p, bool /*legato*/)
    {
        // Click-free stealing (LESSONS.md #11): an audible voice fades out
        // over ~2.5 ms before the new note takes it over.
        if (active && eg2.getLevel() > 0.0005f)
        {
            pendingNote = midiNote; pendingVel = vel;
            stealing = true; stealGain = 1.0f;
        }
        else startNote (midiNote, vel, p);
    }

    void startNote (int midiNote, float vel, const VoiceParams& p)
    {
        note = midiNote; velocity = vel; active = true; age = 0; fade = 0.0f;
        stealing = false; pendingNote = -1; fmPhase = 0.0f;
        randConst = (float) rand() / RAND_MAX * 2.0f - 1.0f;
        eg1.setParams (p.e1a, p.e1d, p.e1s, p.e1r);
        eg2.setParams (p.e2a, p.e2d, p.e2s, p.e2r);
        eg1.noteOn(); eg2.noteOn();
        lfo1.keySync (true); lfo2.keySync (true);
        // freq targets set lazily per-sample; init glide start at current or target
        double t = baseFreq (midiNote, p, true);
        if (p.portamento <= 0.001f) { curFreq1 = t; curFreq2 = baseFreq (midiNote, p, false); }
        // sync osc2 to osc1 phase when sync modes enabled
        if (p.oscMod == 2 || p.oscMod == 3)
        {
            osc2.setPhase (osc1.getPhase());
            osc2b.setPhase (osc1.getPhase());
        }
    }

    void noteOff() { eg1.noteOff(); eg2.noteOff(); pendingNote = -1; }
    void fastKill()
    {
        eg1.reset(); eg2.reset(); active = false;
        stealing = false; pendingNote = -1; stealGain = 0.0f;
    }
    bool isActive() const { return active && !eg2.isIdle(); }
    bool isAudible() const { return active && eg2.getLevel() > 0.0005f; }
    int getNote() const { return note; }
    float audibleLevel() const { return eg2.getLevel(); }
    void touchAge() { ++age; }
    long getAge() const { return age; }

    // pitch bend in semitones, modwheel 0..1
    inline void renderSample (const VoiceParams& p, float bendSemi, float modWheel,
                              float& outL, float& outR)
    {
        if (! active) { outL = outR = 0.0f; return; }
        if (stealing)
        {
            renderActive (p, bendSemi, modWheel, outL, outR);
            outL *= stealGain; outR *= stealGain;
            stealGain -= 1.0f / (0.0025f * (float) fs);
            if (stealGain > 0.0f) return;
            stealing = false;
            if (pendingNote < 0) { active = false; outL = outR = 0.0f; return; }
            int nn = pendingNote; float vv = pendingVel; pendingNote = -1;
            startNote (nn, vv, p);
        }
        renderActive (p, bendSemi, modWheel, outL, outR);
    }

    inline void renderActive (const VoiceParams& p, float bendSemi, float modWheel,
                              float& outL, float& outR)
    {
        touchAge();
        osc1.setUserTable (p.userTable);
        osc1b.setUserTable (p.userTable);
        osc2.setUserTable (p.userTable2);
        osc2b.setUserTable (p.userTable2);
        float e1 = eg1.tick(), e2 = eg2.tick();
        if (eg2.isIdle()) { active = false; outL = outR = 0.0f; return; }

        // Base LFO rates: tempo-synced divisions override the free rate.
        // Division period = beats * seconds-per-beat, so rate = bpm/60/beats.
        // Ticks use the stored (previous-sample) rate modulation; the matrix
        // below refreshes the multipliers for the next sample.
        double bpm = p.songTempo > 20.0 ? p.songTempo : 120.0;
        double r1base = (p.lfo1Note >= 0 && p.lfo1Note < 15)
                        ? (bpm / 60.0) / kSyncBeats[p.lfo1Note] : p.lfo1Rate;
        double r2base = (p.lfo2Note >= 0 && p.lfo2Note < 15)
                        ? (bpm / 60.0) / kSyncBeats[p.lfo2Note] : p.lfo2Rate;
        float l1 = lfo1.tick (p.lfo1Wave,
                              r1base * std::pow (2.0, (double) lfo1MulSm * 4.0), false)
                     * p.lfo1Depth;
        float l2 = lfo2.tick (p.lfo2Wave,
                              r2base * std::pow (2.0, (double) lfo2MulSm * 4.0), false)
                     * p.lfo2Depth;

        float keyTrack = (note - 60) / 48.0f; // -1..1ish
        auto srcVal = [&] (int s) -> float
        {
            switch (s)
            {
                case 0: return e1 * 2.0f - 1.0f;      // ENV1 bipolar-ish
                case 1: return e2 * 2.0f - 1.0f;      // ENV2
                case 2: return l1; case 3: return l2;
                case 4: return velocity * 2.0f - 1.0f;
                case 5: return keyTrack;
                case 6: return bendSemi / 12.0f * 2.0f;
                case 7: return modWheel * 2.0f - 1.0f;
                case 8: return randConst;
                default: return 0.0f;
            }
        };

        // modulation accumulators (virtual-patch scaling ≈ destination units:
        // full intensity moves pitch ±1 octave, others ≈ ±half range)
        float o1pitch = 0, o2pitch = 0, o1shape = 0, noiseAdd = 0;
        float cutAdd = 0, resAdd = 0, ampAdd = 0, panAdd = 0;
        float lfo1RateMul = 0, lfo2RateMul = 0;
        for (int i = 0; i < 4; ++i)
        {
            float amt = p.mAmt[i];
            if (amt == 0.0f) continue;
            float v = srcVal (p.mSrc[i]) * amt;
            switch (p.mDst[i])
            {
                case 0: o1pitch += v * 12.0f; break;   // semitones
                case 1: o2pitch += v * 12.0f; break;
                case 2: o1shape += v * 0.5f; break;
                case 3: noiseAdd += v * 0.5f; break;
                case 4: cutAdd += v * 0.5f; break;
                case 5: resAdd += v * 0.5f; break;
                case 6: ampAdd += v * 0.5f; break;
                case 7: panAdd += v * 0.5f; break;
                case 8: lfo1RateMul += v; break;
                case 9: lfo2RateMul += v; break;
                default: break;
            }
        }
        lfo1MulSm = lfo1RateMul;
        lfo2MulSm = lfo2RateMul;

        double tgt1 = baseFreq (note, p, true) * std::pow (2.0, (bendSemi + o1pitch) / 12.0);
        double tgt2 = baseFreq (note, p, false) * std::pow (2.0, (bendSemi + o2pitch) / 12.0);
        // vibrato (mod wheel -> pitch) small
        double vib = 1.0 + modWheel * p.vibAmt * 0.02 * l2;
        tgt1 *= vib; tgt2 *= vib;
        // portamento glide
        float portaSec = p.portamento * p.portamento * 2.0f;
        if (portaSec > 0.002f)
        {
            double c = std::exp (-1.0 / (portaSec * fs));
            curFreq1 = curFreq1 * c + tgt1 * (1.0 - c);
            curFreq2 = curFreq2 * c + tgt2 * (1.0 - c);
        }
        else { curFreq1 = tgt1; curFreq2 = tgt2; }

        // OSC1 CONTROL 1/2 per-wave behavior (hardware-faithful):
        // SAW: C1 morphs toward octave-up saw; SQUARE: C1 = pulse width
        // (50% -> 0%); TRIANGLE: C1 retunes up to +19 semitones;
        // C2 = LFO1 depth onto C1 (DWGS uses its own selector instead).
        float c1e = clamp01 (p.osc1Shape + o1shape + l1 * p.osc1Pwm);
        int w1 = p.osc1Wave <= 6 ? p.osc1Wave : 0;
        auto renderO1 = [&] (OscCore& o, double f) -> float
        {
            if (w1 == 0) return o.sawMorph (f, c1e);
            if (w1 == 1) return o.squarePw (f, 0.5 * (1.0 - c1e));
            if (w1 == 2) return o.render (2, f * std::pow (2.0, c1e * 19.0 / 12.0),
                                          0.0f, 0.5f, 0.0f, 0);
            if (w1 == 3) return o.render (3, f, c1e, 0.0f, p.osc1Xmod, 0);
            // Digital/User share one selector: factory wave (<128) or bank
            // entry (>=128). The Digital combo always sounds, whichever of
            // the two wave modes is active (userTable preset by the host).
            if (w1 == 4 || w1 == 6)
            {
                int di = p.osc1Digital;
                if (di < 128) return o.render (4, f, 0.0f, p.osc1Pwm, p.osc1Xmod, di);
                return o.render (6, f, 0.0f, 0.0f, 0.0f, 0);
            }
            if (w1 == 5) return o.render (5, f, 0.0f, 0.0f, 0.0f, 0);
            return o.render (4, f, 0.0f, p.osc1Pwm, p.osc1Xmod, 0);
        };
        int w2base = p.osc2Wave <= 6 ? p.osc2Wave : 0;
        float c1e2 = clamp01 (p.osc2Shape + l1 * p.osc2Pwm);
        auto renderO2 = [&] (OscCore& o, double f) -> float
        {
            int w2 = w2base;
            if (w2 == 0) return o.sawMorph (f, c1e2);
            if (w2 == 1) return o.squarePw (f, 0.5 * (1.0 - c1e2));
            if (w2 == 2) return o.render (2, f * std::pow (2.0, c1e2 * 19.0 / 12.0),
                                          0.0f, 0.5f, 0.0f, 0);
            if (w2 == 3) return o.render (3, f, c1e2, 0.0f, p.osc2Xmod, 0);
            // Shared Digital/User selector (see osc1): combo always sounds.
            if (w2 == 4 || w2 == 6)
            {
                int di = p.osc2Digital;
                if (di < 128) return o.render (4, f, 0.0f, p.osc2Pwm, p.osc2Xmod, di);
                return o.render (6, f, 0.0f, 0.0f, 0.0f, 0);
            }
            if (w2 == 5) return o.render (5, f, 0.0f, 0.0f, 0.0f, 0);
            return o.render (4, f, 0.0f, p.osc2Pwm, p.osc2Xmod, 0);
        };
        float s1, s2;
        bool syncOn = (p.oscMod == 2 || p.oscMod == 3);
        bool ringOn = (p.oscMod == 1 || p.oscMod == 3);
        // FM (osc2 -> osc1): exponential, +/-2 octaves at full amount.
        // The modulator is a pure sine at osc2's current pitch, so ratios
        // from octave/semitone offsets stay harmonic (DX-style); fine cents
        // add only slow beating. Amount 0 gives fmMul == 1 (no FM).
        fmPhase += curFreq2 / fs;
        if (fmPhase >= 1.0) fmPhase -= 1.0;
        double fmMul = std::pow (2.0, std::sin (6.283185307179586 * fmPhase)
                                        * (double) p.fmAmt * 2.0);
        if (p.unison)
        {
            // two taps, symmetric detune around center (independent phases)
            double uA = std::pow (2.0, -p.detune * 25.0 / 1200.0);
            double uB = std::pow (2.0, p.detune * 25.0 / 1200.0);
            double prevPh = osc1.getPhase();
            float a1 = renderO1 (osc1, curFreq1 * uA * fmMul);
            float b1 = renderO1 (osc1b, curFreq1 * uB * fmMul);
            s1 = 0.5f * (a1 + b1);
            float a2 = renderO2 (osc2, curFreq2 * uA);
            float b2 = renderO2 (osc2b, curFreq2 * uB);
            s2 = 0.5f * (a2 + b2);
            if (syncOn && osc1.getPhase() < prevPh) { osc2.setPhase (0.0); osc2b.setPhase (0.0); }
        }
        else
        {
            double prevPh = osc1.getPhase();
            s1 = renderO1 (osc1, curFreq1 * fmMul);
            s2 = renderO2 (osc2, curFreq2);
            if (syncOn && osc1.getPhase() < prevPh) osc2.setPhase (0.0); // hard sync
        }
        if (ringOn) s2 = s1 * s2 * 1.5f; // ring
        float nz = noiseOsc.render (5, 440.0, 0, 0, 0, 0);

        float gO1 = p.mixO1 * p.osc1Level;
        float gO2 = p.mixO2 * p.osc2Level;
        float gN = clamp01 (p.mixN + noiseAdd);
        float mixed = s1 * gO1 + s2 * gO2 + nz * gN * 0.7f;
        mixed = std::tanh (mixed * 1.3f); // mixer warmth: gentle glue, tames harsh peaks

        // filter
        float cutNorm = clamp01 (p.filtCut + e1 * p.filtEnv + cutAdd + keyTrack * p.filtKey * 0.5f);
        float reso = clamp01 (p.filtRes + resAdd);
        float cutHz = 40.0f * std::pow (450.0f, cutNorm); // 40..18000
        // smooth cutoff to avoid zipper
        double c2 = std::exp (-1.0 / (0.008 * fs));
        cutSm = cutSm * c2 + cutHz * (1.0 - c2);
        float f = filt.process (p.filtType, float (cutSm), reso, mixed);

        // amp: env2 * level * velocity * mod
        float velGain = 1.0f - p.ampVel * (1.0f - velocity);
        float ag = clamp01 (p.ampLevel * velGain + ampAdd) * e2;
        double ac = std::exp (-1.0 / (0.005 * fs));
        ampSm = ampSm * ac + ag * (1.0 - ac);
        float a = f * float (ampSm);
        if (p.ampDist) a = std::tanh (a * (1.0f + p.ampDrive * 6.0f)) * 0.8f;

        float pan = clamp11 (p.ampPan + panAdd);
        float l = 0.5f, r = 0.5f;
        float ang = (pan * 0.5f + 0.5f) * 1.5707963f;
        l = std::cos (ang); r = std::sin (ang);
        // smooth pan gains
        panSmL = panSmL * 0.999f + l * 0.001f;
        panSmR = panSmR * 0.999f + r * 0.001f;
        outL = a * panSmL * 1.4f * fade;
        outR = a * panSmR * 1.4f * fade;
        // LESSONS.md #11: short attack fade-in suppresses attack/steal clicks.
        if (fade < 1.0f) fade = std::min (1.0f, fade + 1.0f / (0.003f * (float) fs));
    }

private:
    static float clamp01 (float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
    static float clamp11 (float x) { return x < -1 ? -1 : (x > 1 ? 1 : x); }

    double baseFreq (int midiNote, const VoiceParams& p, bool first) const
    {
        int oct = first ? p.osc1Oct : p.osc2Oct;
        int pit = first ? p.osc1Pitch : p.osc2Pitch;
        float fin = first ? p.osc1Fine : p.osc2Fine;
        // Detune knob: opposing spread, osc1 down / osc2 up, max +/-15 cents.
        double spread = p.detune * 15.0 * (first ? -1.0 : 1.0);
        double f = noteToFreq (midiNote) * std::pow (2.0, oct + pit / 12.0 + (fin + spread) / 1200.0);
        return f;
    }

    double fs = 44100.0;
    OscCore osc1, osc1b, osc2, osc2b, noiseOsc;
    SvFilter filt;
    Adsr eg1, eg2;
    Lfo lfo1, lfo2;
    float lfo1MulSm = 0.0f, lfo2MulSm = 0.0f;
    float fmPhase = 0.0f; // FM modulator phase: pure sine at osc2's pitch
    bool active = false;
    int note = -1;
    float velocity = 0, randConst = 0, fade = 1.0f;
    // click-free steal state (LESSONS.md #11)
    bool stealing = false;
    int pendingNote = -1;
    float pendingVel = 0.0f, stealGain = 0.0f;
    double curFreq1 = 440, curFreq2 = 440, cutSm = 8000, ampSm = 0;
    float panSmL = 0.7f, panSmR = 0.7f;
    long age = 0;
};
