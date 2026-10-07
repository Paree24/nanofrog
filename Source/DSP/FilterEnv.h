#pragma once
#include <cmath>

// TPT state-variable filter with LP12/HP12/BP12 taps.
// LP24 = two LP12 stages in series. Stable, no self-oscillation blowup.
class SvFilter
{
public:
    void setSampleRate (double sr) { fs = sr; reset(); }
    void reset() { s1 = s2 = s3 = s4 = 0.0; }

    // type: 0=LP24, 1=LP12, 2=BP12, 3=HP12
    inline float process (int type, float cutoffHz, float reso01, float in)
    {
        if (cutoffHz < 20.0f) cutoffHz = 20.0f;
        float nyq = float (fs * 0.45);
        if (cutoffHz > nyq) cutoffHz = nyq;
        // resonance 0..1 -> Q 0.5..12, damping factor for SVF
        float q = 0.5f + reso01 * reso01 * 11.5f;
        float k = 1.0f / q;
        float g = std::tan (float (3.14159265 * cutoffHz / fs));
        if (g > 4.0f) g = 4.0f;
        float a1 = 1.0f / (1.0f + g * (g + k));
        // stage helper
        auto stage = [&] (double& ic1, double& ic2, float x, float& lp, float& bp, float& hp)
        {
            float v3 = x - ic2;
            float v1 = a1 * ic1 + a1 * g * v3;
            float v2 = ic2 + g * v1;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        };
        float lp = 0, bp = 0, hp = 0;
        if (type == 0) // LP24: two LP12 in series (48dB? we use 2 stages = 24dB)
        {
            float l1, b1, h1, l2, b2, h2;
            stage (s1, s2, in, l1, b1, h1);
            // slight interstage saturation for analog feel
            float driven = std::tanh (l1 * 1.1f);
            stage (s3, s4, driven, l2, b2, h2);
            return l2;
        }
        stage (s1, s2, in, lp, bp, hp);
        s3 = s4 = 0.0;
        if (type == 1) return lp;
        if (type == 2) return bp * 1.4f; // compensate BP gain
        return hp;
    }

private:
    double fs = 44100.0;
    double s1 = 0, s2 = 0, s3 = 0, s4 = 0;
};

// Simple ADSR, sample-rate aware, with curved segments.
class Adsr
{
public:
    enum Stage { Idle, Attack, Decay, Sustain, Release, Done };

    void setSampleRate (double sr) { fs = sr; }
    void setParams (float aSec, float dSec, float s01, float rSec)
    {
        a = std::max (0.001f, aSec); d = std::max (0.005f, dSec);
        s = s01; r = std::max (0.005f, rSec);
    }
    void noteOn() { stage = Attack; decPos = 0.0f; if (a <= 0.002f) level = 1.0f; }
    void noteOff()
    {
        if (stage != Idle && stage != Done) { stage = Release; }
    }
    void reset() { stage = Idle; level = 0.0f; }
    bool isActive() const { return stage != Idle && stage != Done; }
    bool isIdle() const { return stage == Idle || stage == Done; }
    Stage getStage() const { return stage; }
    float getLevel() const { return level; }

    inline float tick()
    {
        switch (stage)
        {
            case Idle: level = 0.0f; break;
            case Attack: {
                level += float (1.0 / (a * fs));
                if (level >= 1.0f) { level = 1.0f; stage = Decay; decPos = 0.0f; }
                break;
            }
            case Decay: {
                // exponential approach calibrated so the stage lands on
                // sustain at t ~= d (no fudge constants that compress timing)
                float c = 1.0f - std::exp (-7.0f / (d * (float) fs));
                level += (s - level) * c;
                decPos += 1.0f / (d * (float) fs);
                if (decPos >= 1.0f) { level = s; stage = Sustain; }
                break;
            }
            case Sustain: level = s; break;
            case Release: {
                // exponential to silence: hits -62 dB at t ~= r
                float c = 1.0f - std::exp (-7.13f / (r * (float) fs));
                level *= (1.0f - c);
                if (level < 0.0008f) { level = 0.0f; stage = Done; }
                break;
            }
            case Done: level = 0.0f; stage = Idle; break;
        }
        return level;
    }

private:
    double fs = 44100.0;
    float a = 0.01f, d = 0.1f, s = 0.8f, r = 0.2f;
    float level = 0.0f;
    float decPos = 0.0f;
    Stage stage = Idle;
};

// LFO: sine/tri/square/saw/random(S&H), 0..1 bipolar output -1..1
class Lfo
{
public:
    enum Wave { Sine = 0, Tri = 1, Square = 2, Saw = 3, Random = 4 };

    void setSampleRate (double sr) { fs = sr; }
    void reset() { phase = 0.0; current = 0.0f; target = 1.0f; }
    void keySync (bool toZero) { if (toZero) { phase = 0.0; pickRandom(); } }

    inline float tick (int wave, double freqHz, bool /*tempoSyncIgnored*/)
    {
        if (freqHz < 0.01) freqHz = 0.01;
        if (freqHz > 60.0) freqHz = 60.0;
        double inc = freqHz / fs;
        float out = 0.0f;
        switch (wave)
        {
            case Sine:   out = std::sin (float (6.2831853 * phase)); break;
            case Tri:    out = float (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase); break;
            case Square: out = (phase < 0.5 ? 1.0f : -1.0f); break;
            case Saw:    out = float (2.0 * phase - 1.0); break;
            case Random:
                out = current;
                break;
        }
        phase += inc;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            if (wave == Random) { current = target; pickRandom(); }
        }
        return out;
    }

private:
    void pickRandom() { target = (float) rand() / RAND_MAX * 2.0f - 1.0f; if (phase == 0.0) current = target; }
    double fs = 44100.0, phase = 0.0;
    float current = 0.0f, target = 1.0f;
};
