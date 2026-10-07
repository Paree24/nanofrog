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
        if (type == 0 || mix01 <= 0.001f) return;
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
                float sw = 0.3f + depth01 * 2.0f; // 0.3..2.3 ms
                float wetL = readDelay (bufL, pos, 2.0f + sL * sw);
                float wetR = readDelay (bufR, pos, 2.0f - sR * sw);
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
        pos = 0;
    }
    void reset() { std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f); }

    // time01 0..1 -> 20..900ms, fb 0..0.85, mix 0..1
    void process (float* L, float* R, int n, int type, float time01, float fb, float mix)
    {
        if (mix <= 0.001f) { return; }
        float ms = 20.0f + time01 * time01 * 880.0f;
        double ds = ms * 0.001 * fs;
        float f = std::min (0.85f, fb);
        for (int i = 0; i < n; ++i)
        {
            float dL = readAt (bufL, pos, ds);
            float dR = readAt (bufR, pos, ds);
            float inL = L[i], inR = R[i];
            float wL, wR, nL, nR;
            if (type == 0)      { wL = dL; wR = dR; nL = inL + dL * f; nR = inR + dR * f; }
            else if (type == 1) { wL = dR; wR = dL; nL = inL + dR * f; nR = inR + dL * f; }
            else                { wL = dR; wR = dL; nL = inR + dR * f; nR = inL + dL * f; }
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
};

// ---- 2-band EQ: lowshelf + highshelf (RBJ cookbook)
class TwoBandEq
{
public:
    void setSampleRate (double sr) { fs = sr; reset(); }
    void reset() { x1L = x2L = y1L = y2L = x1H = x2H = y1H = y2H = 0; in1L = in2L = 0; in1H = in2H = 0; }

    void process (float* L, float* R, int n, float lowDb, float highDb)
    {
        if (lowDb == 0.0f && highDb == 0.0f) return;
        computeShelf (200.0, lowDb, true, bL, aL);
        computeShelf (6000.0, highDb, false, bH, aH);
        for (int i = 0; i < n; ++i)
        {
            L[i] = biq (L[i], bL, aL, in1L, in2L, y1L, y2L);
            L[i] = biq (L[i], bH, aH, in1H, in2H, y1H2L, y2H2L);
            R[i] = biq (R[i], bL, aL, in1LR, in2LR, y1LR, y2LR);
            R[i] = biq (R[i], bH, aH, in1HR, in2HR, y1HR, y2HR);
        }
    }

private:
    double fs = 44100;
    double bL[3] = {1,0,0}, aL[3] = {1,0,0}, bH[3] = {1,0,0}, aH[3] = {1,0,0};
    // per-channel state (L low, L high, R low, R high)
    double in1L=0,in2L=0,y1L=0,y2L=0, in1H=0,in2H=0,y1H2L=0,y2H2L=0;
    double in1LR=0,in2LR=0,y1LR=0,y2LR=0, in1HR=0,in2HR=0,y1HR=0,y2HR=0;
    double x1L=0,x2L=0,y1Lc=0,y2Lc=0,x1H=0,x2H=0,y1H=0,y2H=0;

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
    static float biq (float x, double* b, double* a, double& x1, double& x2, double& y1, double& y2)
    {
        double y = b[0]*x + b[1]*x1 + b[2]*x2 - a[1]*y1 - a[2]*y2;
        x2=x1; x1=x; y2=y1; y1=y;
        return float (y);
    }
};
