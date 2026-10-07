#pragma once
#include <cmath>
#include <random>
#include <array>

// Band-limited osc core: PolyBLEP saw/square, naive tri/sine,
// 128 original digital single-cycle waves, white noise.
// All original content; no sampled or copied wavetables.
class OscCore
{
public:
    enum Wave { Saw = 0, Square = 1, Triangle = 2, Sine = 3, Digital = 4, Noise = 5, User = 6 };
    static constexpr int kUserLen = 2048;

    void setSampleRate (double sr) { sampleRate = sr; phase = 0.0; noiseGen.seed (12345u); }
    void setUserTable (const float* t) { userTable = t; }

    // freq in Hz, shape 0..1 (morph/brightness), pwm 0..1 (pulse width),
    // xmod 0..1 (extra harmonic drive for digital/saw), digitalIndex 0..127
    float render (int wave, double freq, float shape, float pwm, float xmod, int digitalIndex)
    {
        if (freq < 0.1) freq = 0.1;
        if (freq > sampleRate * 0.45) freq = sampleRate * 0.45;
        const double inc = freq / sampleRate;
        float out = 0.0f;
        switch (wave)
        {
            case Saw:      out = polyBlepSaw (inc, shape, xmod); break;
            case Square:   out = polyBlepSquare (inc, pwm); break;
            case Triangle: out = naiveTri (inc, shape); break;
            case Sine:     out = naiveSine (inc, shape); break;
            case Digital:  out = digitalWave (inc, digitalIndex, shape, pwm, xmod); break;
            case Noise:    out = whiteNoise(); advancePhase (inc); break;
            case User:     out = renderUser (inc, userTable); break;
            default:       out = polyBlepSaw (inc, shape, xmod); break;
        }
        return out;
    }

    void resetPhase() { phase = 0.0; }
    void setPhase (double p) { phase = p - std::floor (p); }
    double getPhase() const { return phase; }
    void advancePhase (double inc) { phase += inc; if (phase >= 1.0) phase -= 1.0; }

    // Exposed for sync handling
    double phase = 0.0;

    // Square with explicit duty cycle 0..0.5 (hardware: C1 0 -> 50%, 127 -> 0%).
    float squarePw (double freq, double duty);

    // Saw morphing toward an octave-up saw (hardware SAW Control 1:
    // 0 = conventional saw, max = one octave higher).
    float sawMorph (double freq, float morph);

private:
    double sampleRate = 44100.0;
    std::mt19937 noiseGen { 42 };
    std::uniform_real_distribution<float> uni { -1.0f, 1.0f };
    float holdRandom = 0.0f;
    const float* userTable = nullptr;

    static float polyBlep (double t, double dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    float polyBlepSaw (double inc, float shape, float xmod)
    {
        double t = phase;
        float v = float (2.0 * t - 1.0) - float (polyBlep (t, inc));
        // shape adds a mild second-harmonic fold for brightness control (original waveshaping)
        float shaped = v + xmod * 0.35f * std::sin (float (6.2831853 * t * 2.0));
        float bright = shape * 0.25f * std::sin (float (6.2831853 * t * 3.0));
        advancePhase (inc);
        return v * 0.85f + (shaped - v) * 0.5f + bright * 0.3f;
    }

    float polyBlepSquare (double inc, float pwm)
    {
        double pw = 0.05 + 0.90 * pwm; // 5%..95%
        return squarePwDuty (inc, pw);
    }

    // Square with explicit duty cycle 0..0.5 (hardware: C1 0 -> 50%, 127 -> 0%).
    // Duty below ~0.4% renders silence (matches "no sound" at extreme).
    float squarePwDuty (double inc, double duty)
    {
        if (duty < 0.004) { advancePhase (inc); return 0.0f; }
        if (duty > 0.5) duty = 0.5;
        double t = phase;
        float v = (t < duty ? 1.0f : -1.0f);
        v += float (polyBlep (t, inc));
        double t2 = t - duty; if (t2 < 0.0) t2 += 1.0;
        v -= float (polyBlep (t2, inc));
        advancePhase (inc);
        return v * 0.7f;
    }

    float naiveTri (double inc, float shape)
    {
        double t = phase;
        // morph triangle -> sine-ish -> ramp-ish with shape
        float tri = float (t < 0.5 ? 4.0 * t - 1.0 : 3.0 - 4.0 * t);
        float sine = std::sin (float (6.2831853 * t));
        advancePhase (inc);
        // crude antialias: attenuate harmonics at high freq
        return tri * (1.0f - 0.5f * shape) + sine * (0.5f * shape);
    }

    float naiveSine (double inc, float shape)
    {
        double t = phase;
        float s = std::sin (float (6.2831853 * t));
        float s3 = std::sin (float (6.2831853 * t * 3.0));
        advancePhase (inc);
        return s * 0.9f + s3 * 0.25f * shape; // shape adds 3rd harmonic (cross-like timbre)
    }

    // 128 original digital waves: two banks of 64 (8 families x 8
    // variations). Bank 0 (0..63) is the classic set; bank 1 (64..127)
    // re-voices each family (sub weight, shifted formants, wider detune).
    // Recipes are procedural harmonic/inharmonic stacks (inspired by the
    // *character* range of classic DWGS-style waves — hollow, bell,
    // metallic, vocal, bright — but fully original synthesis, no sampled
    // content). Variation morphs brightness/formants within each family.
    float digitalWave (double inc, int index, float shape, float pwm, float xmod)
    {
        if (index < 0) index = 0; if (index > 127) index = 127;
        int bank = (index >= 64) ? 1 : 0; // second bank re-voices the families
        float bs = (float) bank;
        int g = index % 8;         // family
        int v = (index / 8) % 8;   // variation
        float vf = v / 7.0f;
        double t = phase;
        const double TAU = 6.283185307179586;

        float ratio[16]; float level[16]; float ph[16];
        int nP = 0;
        auto add = [&] (double r, float lv, float pha)
        {
            if (nP < 16 && lv > 0.001f)
            {
                ratio[nP] = (float) r; level[nP] = lv; ph[nP] = pha; ++nP;
            }
        };
        switch (g)
        {
            case 0: // analog stack: saw -> square morph (even harmonics fade with v)
                for (int k = 1; k <= 12; ++k)
                {
                    bool odd = (k & 1) != 0;
                    float lv = odd ? 1.0f / k : (1.0f / k) * (1.0f - vf * 0.85f);
                    add ((double) k, lv, 0.0f);
                }
                add (0.5, 0.40f * bs, 0.0f); // bank 1: sub-octave weight
                break;
            case 1: // hollow reed: odd harmonics + breath shimmer grows with v
                add (1.0, 1.0f, 0.0f);
                add (3.0, 0.55f, 0.4f);
                add (5.0, 0.35f, 1.1f);
                add (7.0, 0.20f, 2.0f);
                add (9.0, 0.05f + vf * 0.25f, 2.6f);
                add (11.0, 0.03f + vf * 0.20f, 0.7f);
                add (13.0, vf * 0.15f, 1.9f);
                add (15.0, bs * 0.18f, 0.3f); // bank 1: air partial
                break;
            case 2: // glass bell: harmonic -> inharmonic with v
            {
                float bend = vf * 0.35f;
                add (1.0, 1.0f, 0.0f);
                add (2.0 + bend * 0.4, 0.55f, 0.9f);
                add (2.76 + bend, 0.40f, 2.2f);
                add (3.9 + bend * 1.4, 0.28f, 1.4f);
                add (5.1 + bend * 1.8, 0.18f, 0.3f);
                add (6.8 + bend * 2.2, 0.10f, 2.8f);
                add (8.9 + bend * 2.6, bs * 0.12f, 1.1f); // bank 1: extra clang
                break;
            }
            case 3: // metallic FM-ish: fixed odd sidebands, level = mod index
            {
                float m = 0.15f + vf * 0.65f;
                add (1.0, 1.0f, 0.0f);
                add (1.49, 0.55f * m + 0.05f, 1.2f);
                add (2.0, 0.40f, 2.4f);
                add (2.51, 0.45f * m + 0.03f, 0.5f);
                add (3.0, 0.25f, 1.7f);
                add (3.49, 0.30f * m, 2.9f);
                add (4.51, bs * 0.25f * m, 0.9f); // bank 1: upper sideband
                break;
            }
            case 4: // vocal formant: two bumps sliding with v (vowel morph)
            {
                float c1 = 2.5f + vf * 2.0f + bs * 1.5f, c2 = 6.0f + vf * 3.0f + bs * 2.0f;
                for (int k = 1; k <= 10; ++k)
                {
                    float d1 = (k - c1) * (k - c1) / 2.5f;
                    float d2 = (k - c2) * (k - c2) / 4.0f;
                    add ((double) k, 0.12f + 0.75f * std::exp (-d1) + 0.55f * std::exp (-d2),
                         k * 0.35f);
                }
                break;
            }
            case 5: // bright edge: saw core + top-octave bite grows with v
                for (int k = 1; k <= 7; ++k) add ((double) k, 1.0f / k, 0.0f);
                for (int k = 8; k <= 12; ++k) add ((double) k, (0.10f + vf * 0.55f) / (1 + (k - 8) * 0.3f), k * 0.5f);
                add (13.0, bs * 0.20f, 0.4f); // bank 1: extra bite
                add (14.0, bs * 0.15f, 1.2f);
                break;
            case 6: // mellow wood: fundamental + soft low partials, warmth with v
                add (1.0, 1.0f, 0.0f);
                add (2.0, 0.25f + vf * 0.45f, 0.6f);
                add (3.0, 0.12f + vf * 0.18f, 1.8f);
                add (4.0, 0.05f, 0.9f);
                add (5.0, bs * 0.10f, 1.2f); // bank 1: airy top
                add (6.0, bs * 0.06f, 2.2f);
                break;
            default: // detuned texture: close pairs spread with v + shimmer
            {
                float sp = 1.0f + vf * 0.012f + bs * 0.008f; // bank 1: wider spread
                add (1.0, 0.60f, 0.0f);
                add (sp, 0.60f, 1.0f);
                add (2.0, 0.35f, 0.4f);
                add (2.0 * sp, 0.35f, 2.1f);
                add (3.0, 0.20f, 1.3f);
                add (4.02, 0.12f + vf * 0.15f, 2.7f);
                add (6.0, 0.05f + vf * 0.12f, 0.8f);
                add (8.0, bs * 0.08f, 1.5f);
                break;
            }
        }
        float s = 0.0f;
        for (int k = 0; k < nP; ++k)
            s += level[k] * std::sin (float (TAU * t * ratio[k] + ph[k]));
        float pulse = (std::fmod (t + pwm * 0.5, 1.0) < 0.5 ? 1.0f : -1.0f);
        float folded = std::tanh (s * (1.0f + xmod * 2.0f));
        float out = folded * (1.0f - shape * 0.45f) + pulse * (shape * 0.25f);
        advancePhase (inc);
        return out * 0.55f;
    }

    float whiteNoise()
    {
        return uni (noiseGen) * 0.8f;
    }

    // Imported single-cycle wave (2048 samples, zero-mean, peak-normalised by
    // the importer). Null table falls back to sine so "User" is never silent.
    float renderUser (double inc, const float* table)
    {
        double t = phase;
        float out;
        if (table != nullptr)
        {
            double pos = t * kUserLen;
            int i0 = (int) pos % kUserLen;
            int i1 = (i0 + 1) % kUserLen;
            float fr = float (pos - std::floor (pos));
            out = table[i0] * (1.0f - fr) + table[i1] * fr;
        }
        else
        {
            out = std::sin (float (6.2831853 * t));
        }
        advancePhase (inc);
        return out;
    }
};

inline float OscCore::squarePw (double freq, double duty)
{
    if (freq < 0.1) freq = 0.1;
    if (freq > sampleRate * 0.45) freq = sampleRate * 0.45;
    return squarePwDuty (freq / sampleRate, duty);
}

inline float OscCore::sawMorph (double freq, float morph)
{
    if (freq < 0.1) freq = 0.1;
    if (freq > sampleRate * 0.45) freq = sampleRate * 0.45;
    if (morph < 0.0f) morph = 0.0f; if (morph > 1.0f) morph = 1.0f;
    double inc = freq / sampleRate;
    double t = phase;
    float s1 = float (2.0 * t - 1.0) - float (polyBlep (t, inc));
    float out = s1;
    if (morph > 0.001f)
    {
        double t2 = t + t; if (t2 >= 1.0) t2 -= 1.0;
        double inc2 = inc * 2.0 > 0.4 ? 0.4 : inc * 2.0;
        float s2 = float (2.0 * t2 - 1.0)
                   - float (polyBlep (t, inc2))
                   - float (polyBlep (std::fmod (t + 0.5, 1.0), inc2));
        out = s1 * (1.0f - morph) + s2 * morph * 0.9f;
    }
    advancePhase (inc);
    return out;
}
