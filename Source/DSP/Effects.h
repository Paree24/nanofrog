#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// ---- Mod FX: chorus / flanger / phaser
// Classic modulated-delay design (cf. STK Chorus): slow LFOs, slightly
// detuned per channel so the sweep never pumps mono, shallow depth sweeps,
// a darkened wet tap, and a dry signal that always anchors pitch. The old
// implementation swept +-10 ms at up to 8 Hz (a vibrato, not a chorus)
// and its "phaser" was two bare allpasses with no feedback.
class ModFx
{
public:
    void setSampleRate (double sr)
    {
        fs = sr;
        bufL.assign ((size_t) (sr * 0.1 + 16), 0.0f);
        bufR.assign ((size_t) (sr * 0.1 + 16), 0.0f);
        pos = 0; lfoPh = lfoPhR = 0.0;
        std::fill (std::begin (apL), std::end (apL), 0.0f);
        std::fill (std::begin (apR), std::end (apR), 0.0f);
        lpL = lpR = fbL = fbR = 0.0f;
    }
    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        lfoPh = lfoPhR = 0.0;
        std::fill (std::begin (apL), std::end (apL), 0.0f);
        std::fill (std::begin (apR), std::end (apR), 0.0f);
        lpL = lpR = fbL = fbR = 0.0f;
    }

    // type 0 off 1 chorus 2 flanger 3 phaser; rate 0..1, depth 0..1, mix 0..1
    void process (float* L, float* R, int n, int type, float rate01, float depth01, float mix01)
    {
        if (type == 0 || mix01 <= 0.001f)
        {
            // Off or muted: the delay line keeps recording (as it does under
            // the Phaser), so switching back to Chorus / Flanger never replays
            // audio from whenever it was last used
            for (int i = 0; i < n; ++i)
            {
                bufL[pos] = L[i]; bufR[pos] = R[i];
                pos = (pos + 1) % bufL.size();
            }
            return;
        }
        // Slow sweep (0.05..2.55 Hz); right channel runs ~11% faster.
        double rateHz = 0.05 + (double) rate01 * (double) rate01 * 2.5;
        double rateR = rateHz * 1.11;
        float lpK = 1.0f - std::exp (-2.0f * 3.14159265f * 3500.0f / (float) fs);
        for (int i = 0; i < n; ++i)
        {
            lfoPh += rateHz / fs;
            if (lfoPh >= 1.0) lfoPh -= 1.0;
            lfoPhR += rateR / fs;
            if (lfoPhR >= 1.0) lfoPhR -= 1.0;
            float sL = std::sin (float (6.2831853 * lfoPh));
            float sR = std::sin (float (6.2831853 * lfoPhR));
            if (type == 1) // chorus: ~12ms base, shallow darkened sweep
            {
                float sw = 1.0f + depth01 * 4.0f; // 1..5 ms
                float wetL = readDelay (bufL, pos, 12.0f + sL * sw);
                float wetR = readDelay (bufR, pos, 12.0f + sR * sw);
                lpL += lpK * (wetL - lpL); // BBD-style dark tap
                lpR += lpK * (wetR - lpR);
                bufL[pos] = L[i]; bufR[pos] = R[i];
                L[i] = L[i] * (1.0f - mix01 * 0.5f) + lpL * mix01 * 0.5f;
                R[i] = R[i] * (1.0f - mix01 * 0.5f) + lpR * mix01 * 0.5f;
            }
            else if (type == 2) // flanger: short base, antiphase sweep, feedback
            {
                // base always exceeds the sweep, so the tap never reaches
                // zero/negative delay (that read ~100 ms stale buffer data)
                float sw = 0.3f + depth01 * 1.7f; // 0.3..2.0 ms
                float wetL = readDelay (bufL, pos, 2.2f + sL * sw);
                float wetR = readDelay (bufR, pos, 2.2f - sR * sw);
                float fb = 0.3f + depth01 * 0.4f;
                bufL[pos] = L[i] + wetL * fb * 0.7f;
                bufR[pos] = R[i] + wetR * fb * 0.7f;
                L[i] = L[i] * (1.0f - mix01 * 0.6f) + wetL * mix01 * 0.6f;
                R[i] = R[i] * (1.0f - mix01 * 0.6f) + wetR * mix01 * 0.6f;
            }
            else // phaser: 4 modulated allpasses per side + feedback
            {
                float center = 0.45f;
                float width = 0.08f + depth01 * 0.35f;
                float fbAmt = depth01 * 0.55f;
                float cL = center + sL * width, cR = center + sR * width;
                bufL[pos] = L[i]; bufR[pos] = R[i]; // keep the chorus / flanger line current
                float yL = L[i] + fbL * fbAmt, yR = R[i] + fbR * fbAmt;
                for (int s = 0; s < 4; ++s)
                {
                    yL = allpass (yL, apL[s], cL);
                    yR = allpass (yR, apR[s], cR);
                }
                fbL = yL; fbR = yR;
                L[i] = L[i] * (1.0f - mix01 * 0.5f) + yL * mix01 * 0.5f;
                R[i] = R[i] * (1.0f - mix01 * 0.5f) + yR * mix01 * 0.5f;
            }
            pos = (pos + 1) % bufL.size();
        }
    }

private:
    float readDelay (const std::vector<float>& b, size_t p, float dms)
    {
        double ds = dms * 0.001 * fs;
        double rp = (double) p - ds;
        while (rp < 0) rp += b.size();
        size_t i0 = (size_t) rp % b.size();
        size_t i1 = (i0 + 1) % b.size();
        float fr = float (rp - std::floor (rp));
        return b[i0] * (1 - fr) + b[i1] * fr;
    }
    float allpass (float x, float& s, float c)
    {
        float y = -c * x + s;
        s = x + c * y;
        return y;
    }
    double fs = 44100; std::vector<float> bufL, bufR; size_t pos = 0;
    double lfoPh = 0.0, lfoPhR = 0.0;
    float apL[4] = {}, apR[4] = {};
    float lpL = 0.0f, lpR = 0.0f, fbL = 0.0f, fbR = 0.0f;
};

// ---- Stereo delay: stereo / cross / pingpong
class TempoDelay
{
public:
    void setSampleRate (double sr)
    {
        fs = sr;
        bufL.assign ((size_t) (sr * 2.5 + 16), 0.0f);
        bufR.assign ((size_t) (sr * 2.5 + 16), 0.0f);
        pos = 0; dsSm = -1.0;
    }
    void reset() { std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f); }

    // time01 0..1 -> 20..900ms (or syncMs > 0: a tempo-synced time, up to 2.4 s),
    // fb 0..0.85, mix 0..1
    void process (float* L, float* R, int n, int type, float time01, float fb, float mix, float syncMs = -1.0f)
    {
        // Always runs (mix 0 just outputs dry) so the line never holds stale
        // echoes that would replay when the mix comes back up.
        float ms = syncMs > 0.0f ? std::min (syncMs, 2400.0f) : 20.0f + time01 * time01 * 880.0f;
        double ds = ms * 0.001 * fs;
        if (dsSm < 0.0) dsSm = ds;
        // glide the read tap (tape-style) instead of jumping: no zipper clicks
        const double tk = 1.0 - std::exp (-1.0 / (0.05 * fs));
        float f = std::min (0.85f, fb);
        for (int i = 0; i < n; ++i)
        {
            dsSm += (ds - dsSm) * tk;
            float dL = readAt (bufL, pos, dsSm);
            float inL = L[i], inR = R[i];
            float wL, wR, nL, nR;
            if (type == 0)      // Stereo: each side echoes itself
            {
                float dR = readAt (bufR, pos, dsSm);
                wL = dL; wR = dR; nL = inL + dL * f; nR = inR + dR * f;
            }
            else if (type == 1) // Cross: the right line runs 3/4 of the time and
            {                   // the feedback crosses over, so even a centred
                                // source fans out into an off-beat, widening trail
                float dR = readAt (bufR, pos, dsSm * 0.75);
                wL = dL; wR = dR; nL = inL + dR * f; nR = inR + dL * f;
            }
            else                // Ping-pong: the input enters the left line only and
            {                   // each echo jumps sides (L, R, L ...), hard-panned
                float dR = readAt (bufR, pos, dsSm);
                wL = dL; wR = dR; nL = 0.5f * (inL + inR) + dR * f; nR = dL * f;
            }
            bufL[pos] = nL; bufR[pos] = nR;
            L[i] = inL * (1.0f - mix * 0.5f) + wL * mix * 0.5f;
            R[i] = inR * (1.0f - mix * 0.5f) + wR * mix * 0.5f;
            pos = (pos + 1) % bufL.size();
        }
    }

private:
    float readAt (const std::vector<float>& b, size_t p, double ds)
    {
        double rp = (double) p - ds;
        while (rp < 0) rp += b.size();
        size_t i0 = (size_t) rp % b.size();
        size_t i1 = (i0 + 1) % b.size();
        float fr = float (rp - std::floor (rp));
        return b[i0] * (1 - fr) + b[i1] * fr;
    }
    double fs = 44100; std::vector<float> bufL, bufR; size_t pos = 0;
    double dsSm = -1.0; // smoothed delay in samples (-1 = snap on first block)
};

// ---- 2-band EQ: lowshelf + highshelf (RBJ cookbook)
class TwoBandEq
{
public:
    void setSampleRate (double sr) { fs = sr; reset(); }
    void reset()
    {
        for (auto& s : st)
            for (auto& v : s) v = 0.0;
    }

    // Always filters: 0 dB shelves are identity, and skipping them left the
    // biquad states stale (a click on re-engage).
    void process (float* L, float* R, int n, float lowDb, float highDb)
    {
        computeShelf (200.0, lowDb, true, bL, aL);
        computeShelf (6000.0, highDb, false, bH, aH);
        for (int i = 0; i < n; ++i)
        {
            L[i] = biq (L[i], bL, aL, st[0]);
            L[i] = biq (L[i], bH, aH, st[1]);
            R[i] = biq (R[i], bL, aL, st[2]);
            R[i] = biq (R[i], bH, aH, st[3]);
        }
    }

private:
    double fs = 44100;
    double bL[3] = {1,0,0}, aL[3] = {1,0,0}, bH[3] = {1,0,0}, aH[3] = {1,0,0};
    // biquad state {x1, x2, y1, y2}: L low, L high, R low, R high
    double st[4][4] = {};

    void computeShelf (double fc, double db, bool low, double* b, double* a)
    {
        double A = std::pow (10.0, db / 40.0);
        double w0 = 2 * 3.14159265 * fc / fs;
        double alpha = std::sin (w0) / 2 * std::sqrt ((A + 1/A) * (1/0.9 - 1) + 2);
        double cw = std::cos (w0);
        double b0,b1,b2,a0,a1,a2;
        if (low) { b0=A*((A+1)-(A-1)*cw+2*std::sqrt(A)*alpha); b1=2*A*((A-1)-(A+1)*cw); b2=A*((A+1)-(A-1)*cw-2*std::sqrt(A)*alpha); a0=(A+1)+(A-1)*cw+2*std::sqrt(A)*alpha; a1=-2*((A-1)+(A+1)*cw); a2=(A+1)+(A-1)*cw-2*std::sqrt(A)*alpha; }
        else { b0=A*((A+1)+(A-1)*cw+2*std::sqrt(A)*alpha); b1=-2*A*((A-1)+(A+1)*cw); b2=A*((A+1)+(A-1)*cw-2*std::sqrt(A)*alpha); a0=(A+1)-(A-1)*cw+2*std::sqrt(A)*alpha; a1=2*((A-1)-(A+1)*cw); a2=(A+1)-(A-1)*cw-2*std::sqrt(A)*alpha; }
        b[0]=b0/a0; b[1]=b1/a0; b[2]=b2/a0; a[0]=1; a[1]=a1/a0; a[2]=a2/a0;
    }
    static float biq (float x, double* b, double* a, double* s)
    {
        double& x1 = s[0]; double& x2 = s[1]; double& y1 = s[2]; double& y2 = s[3];
        double y = b[0]*x + b[1]*x1 + b[2]*x2 - a[1]*y1 - a[2]*y2;
        x2=x1; x1=x; y2=y1; y1=y;
        return float (y);
    }
};

// ======================================================================
// Extensions beyond the classic VA effect chain (only mod FX, delay
// and EQ): drive, saturation and reverb, ported from Pastel Sampler. All
// default to off, so factory and user presets sound exactly as before.
// ======================================================================

// ---- Drive: tanh distortion with a darkened tone blend
class DriveFx
{
public:
    void reset() { toneL = toneR = 0.0f; }
    // drive01 0..1 (gain 1..50), mix01 0..1
    void process (float* L, float* R, int n, float drive01, float mix01)
    {
        if (drive01 <= 0.0005f || mix01 <= 0.001f) return;
        const float g = 1.0f + drive01 * 49.0f;
        const float tone = 0.12f;
        for (int i = 0; i < n; ++i)
        {
            float dL = std::tanh (L[i] * g), dR = std::tanh (R[i] * g);
            toneL += tone * (dL - toneL);
            toneR += tone * (dR - toneR);
            float tL = dL * 0.65f + toneL * 0.35f;
            float tR = dR * 0.65f + toneR * 0.35f;
            L[i] = L[i] * (1.0f - mix01) + tL * 0.7f * mix01;
            R[i] = R[i] * (1.0f - mix01) + tR * 0.7f * mix01;
        }
    }
private:
    float toneL = 0.0f, toneR = 0.0f;
};

// ---- Saturation: tape (tanh) / tube (asymmetric soft clip) + DC block + tone
class SatFx
{
public:
    void setSampleRate (double sr) { fs = sr; reset(); }
    void reset() { dcL = dcR = lpL = lpR = 0.0f; }
    // amt 0..1, tone 0..1 (2..18 kHz), mode 0 tape 1 tube
    void process (float* L, float* R, int n, float amt, float tone, int mode)
    {
        if (amt <= 0.001f) return;
        amt = std::clamp (amt, 0.0f, 1.0f);
        const float d = 1.0f + amt * 9.0f;
        const float fc = 2000.0f * std::pow (9.0f, std::clamp (tone, 0.0f, 1.0f));
        const float g = 1.0f - std::exp (-2.0f * 3.14159265f * fc / (float) fs);
        const float dcC = 1.0f - std::exp (-2.0f * 3.14159265f * 5.0f / (float) fs);
        const float post = 1.0f / (1.0f + amt * 1.5f);
        const float even = mode == 1 ? 0.18f : 0.06f;
        for (int i = 0; i < n; ++i)
        {
            float wL = shape (L[i] * d, mode, even), wR = shape (R[i] * d, mode, even);
            dcL += dcC * (wL - dcL); wL -= dcL;
            dcR += dcC * (wR - dcR); wR -= dcR;
            lpL += g * (wL - lpL); lpR += g * (wR - lpR);
            L[i] = lpL * post;
            R[i] = lpR * post;
        }
    }
private:
    static float shape (float x, int mode, float even)
    {
        float s = mode == 1 ? x / (1.0f + 0.5f * std::fabs (x)) : std::tanh (x);
        return s + even * s * s;
    }
    double fs = 44100;
    float dcL = 0.0f, dcR = 0.0f, lpL = 0.0f, lpR = 0.0f;
};

// ---- Reverb: 4 modulated damped combs per side + 4 allpasses + predelay
class ModVerb
{
public:
    void setSampleRate (double sr)
    {
        fs = sr;
        const float combMs[4] = { 47.0f, 53.0f, 44.0f, 58.0f };
        for (int i = 0; i < 4; ++i)
        {
            int maxLen = (int) (sr * 0.13) + 64;
            for (auto* c : { &cl[i], &cr[i] })
            {
                c->buf.assign ((size_t) maxLen, 0.0f);
                c->len = maxLen; c->pos = 0; c->lp = 0.0f;
            }
            cl[i].base = combMs[i] * (float) sr / 1000.0f;
            cr[i].base = (combMs[i] + 1.1f) * (float) sr / 1000.0f;
        }
        const float apMs[4] = { 5.0f, 1.7f, 3.4f, 2.6f };
        const float apG[4] = { 0.7f, 0.6f, 0.65f, 0.6f };
        for (int i = 0; i < 4; ++i)
        {
            int maxLen = (int) (sr * 0.01) + 16;
            for (auto* a : { &al[i], &ar[i] })
            {
                a->buf.assign ((size_t) maxLen, 0.0f);
                a->len = maxLen; a->pos = 0; a->g = apG[i];
            }
            al[i].d = apMs[i] * (float) sr / 1000.0f;
            ar[i].d = (apMs[i] + 0.4f) * (float) sr / 1000.0f;
        }
        pd.buf.assign ((size_t) ((int) (sr * 0.06) + 16), 0.0f);
        pd.len = (int) pd.buf.size(); pd.pos = 0;
        curLenScale = -1.0f;
    }
    void reset()
    {
        for (int i = 0; i < 4; ++i)
        {
            for (auto* c : { &cl[i], &cr[i] }) { std::fill (c->buf.begin(), c->buf.end(), 0.0f); c->lp = 0.0f; }
            for (auto* a : { &al[i], &ar[i] }) std::fill (a->buf.begin(), a->buf.end(), 0.0f);
        }
        std::fill (pd.buf.begin(), pd.buf.end(), 0.0f);
        curLenScale = -1.0f;
    }
    // size / damp / mix 0..1
    void process (float* L, float* R, int n, float size, float damp, float mix)
    {
        if (mix <= 0.001f) return;
        size = std::clamp (size, 0.0f, 1.0f);
        const float fb = 0.70f + size * 0.25f;
        const float lpC = 0.55f * (1.0f - std::clamp (damp, 0.0f, 1.0f)) + 0.05f;
        const float lenScale = 0.75f + size * 0.6f;
        if (curLenScale < 0.0f) curLenScale = lenScale;
        // size glides over ~50 ms: the comb lengths never jump (no zipper)
        const float kLen = 1.0f - std::exp (-1.0f / (0.050f * (float) fs));
        const float modAmp = (float) ((0.0006 + size * 0.002) * fs);
        const float rates[4] = { 0.11f, 0.19f, 0.27f, 0.23f };
        const float wetG = 0.5f + size * 0.7f;
        const float preD = size * 0.025f * (float) fs;
        for (int i = 0; i < n; ++i)
        {
            curLenScale += (lenScale - curLenScale) * kLen;
            pd.buf[(size_t) pd.pos] = (L[i] + R[i]) * 0.5f;
            const float in = readInterp (pd.buf, pd.len, preD, pd.pos);
            pd.pos = (pd.pos + 1) % pd.len;
            float accL = 0.0f, accR = 0.0f;
            for (int c = 0; c < 4; ++c)
            {
                mlfo[c] += rates[c] / fs;
                if (mlfo[c] >= 1.0) mlfo[c] -= 1.0;
                const float mod = std::sin (6.2831853f * (float) mlfo[c]) * modAmp;
                accL += runComb (cl[c], in, fb, lpC, mod);
                accR += runComb (cr[c], in, fb, lpC, -mod);
            }
            float wL = accL * 0.25f, wR = accR * 0.25f;
            for (int a = 0; a < 4; ++a) { wL = runAP (wL, al[a]); wR = runAP (wR, ar[a]); }
            L[i] = L[i] * (1.0f - mix) + wL * wetG * mix;
            R[i] = R[i] * (1.0f - mix) + wR * wetG * mix;
        }
    }
private:
    struct Comb { std::vector<float> buf; int len = 1, pos = 0; float base = 0.0f, lp = 0.0f; };
    struct AP { std::vector<float> buf; int len = 1, pos = 0; float d = 0.0f, g = 0.65f; };
    struct PD { std::vector<float> buf; int len = 1, pos = 0; };
    static float readInterp (const std::vector<float>& b, int len, float ds, int wp)
    {
        float rp = (float) wp - ds;
        while (rp < 0.0f) rp += (float) len;
        int i0 = (int) rp % len, i1 = (i0 + 1) % len;
        float fr = rp - std::floor (rp);
        return b[(size_t) i0] * (1.0f - fr) + b[(size_t) i1] * fr;
    }
    float runComb (Comb& c, float in, float fb, float lpC, float mod)
    {
        float d = std::clamp (c.base * curLenScale + mod, 4.0f, (float) c.len - 2.0f);
        float y = readInterp (c.buf, c.len, d, c.pos);
        c.lp += lpC * (y - c.lp);
        c.buf[(size_t) c.pos] = in + c.lp * fb;
        c.pos = (c.pos + 1) % c.len;
        return y;
    }
    static float runAP (float x, AP& a)
    {
        int rp = a.pos - (int) a.d;
        while (rp < 0) rp += a.len;
        float y = -a.g * x + a.buf[(size_t) (rp % a.len)];
        a.buf[(size_t) a.pos] = x + a.g * y;
        a.pos = (a.pos + 1) % a.len;
        return y;
    }
    Comb cl[4], cr[4];
    AP al[4], ar[4];
    PD pd;
    float curLenScale = -1.0f;
    double fs = 44100, mlfo[4] = { 0.0, 0.25, 0.5, 0.75 };
};
