#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

// Small, allocation-free DSP building blocks used inside the audio callback.
namespace nr
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clampf (float x, float lo, float hi) { return std::min (hi, std::max (lo, x)); }
inline float lerp (float a, float b, float t)     { return a + (b - a) * t; }

// Pade tanh, accurate enough for saturation and cheap; exactly +-1 beyond +-3.
inline float tanhA (float x)
{
    if (x > 3.0f)  return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float onePoleCoef (float timeSeconds, float sr)
{
    return timeSeconds <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (timeSeconds * sr));
}

struct Rng
{
    uint32_t s = 0x9E3779B9u;
    void seed (uint32_t v) { s = v ? v : 1u; }
    float bipolar()  { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
    float unipolar() { return 0.5f + 0.5f * bipolar(); }
};

struct Smooth
{
    float v = 0.0f, t = 0.0f, k = 1.0f;
    void setTime (float seconds, float sr) { k = onePoleCoef (seconds, sr); }
    void reset (float x)  { v = t = x; }
    void target (float x) { t = x; }
    float next()          { v += (t - v) * k; return v; }
};

struct DcBlocker
{
    float x1 = 0.0f, y1 = 0.0f, r = 0.995f;
    void prepare (float sr) { r = 1.0f - (kTwoPi * 12.0f / sr); }
    void reset() { x1 = y1 = 0.0f; }
    float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
};

struct OnePoleLP
{
    float z = 0.0f, a = 1.0f;
    void setFreq (float hz, float sr) { a = 1.0f - std::exp (-kTwoPi * std::min (hz, 0.49f * sr) / sr); }
    void reset() { z = 0.0f; }
    float process (float x) { z += a * (x - z); return z; }
};

//==============================================================================
// Morphing oscillator: triangle -> saw -> pulse, band-limited with polyBLEP
// corrections applied across a one-sample delay (handles wraps, pulse edges and
// hard-sync resets with the same mechanism).
struct BlepOsc
{
    double phase = 0.0;
    float pending = 0.0f;
    float wTri = 0.0f, wSaw = 1.0f, wSq = 0.0f, pw = 0.5f, skew = 0.5f;

    void reset (double startPhase = 0.0) { phase = startPhase; pending = 0.0f; }

    void setShape (float wave, float shape)
    {
        wave = clampf (wave, 0.0f, 2.0f);
        shape = clampf (shape, 0.0f, 1.0f);
        if (wave < 1.0f) { wTri = 1.0f - wave; wSaw = wave; wSq = 0.0f; }
        else             { wTri = 0.0f; wSaw = 2.0f - wave; wSq = wave - 1.0f; }
        pw   = 0.5f - 0.45f * shape;     // pulse narrows
        skew = 0.5f + 0.49f * shape;     // triangle leans toward a ramp
    }

    float naive (double p) const
    {
        const float pf = (float) p;
        const float tri = pf < skew ? -1.0f + 2.0f * pf / skew : 1.0f - 2.0f * (pf - skew) / (1.0f - skew);
        const float saw = 2.0f * pf - 1.0f;
        const float sq  = pf < pw ? 1.0f : -1.0f;
        return wTri * tri + wSaw * saw + wSq * sq;
    }

    // Step of height h that happened d samples ago (0 <= d < 1).
    inline void blep (float& cur, float h, float d)
    {
        pending += 0.5f * h * d * d;
        const float e = 1.0f - d;
        cur -= 0.5f * h * e * e;
    }

    float process (double dt, bool& wrapped, float& frac)
    {
        const double p0 = phase;
        phase += dt;
        wrapped = false;
        if (phase >= 1.0) { phase -= 1.0; wrapped = true; frac = (float) (phase / dt); }

        float cur = naive (phase);
        if (wrapped)
        {
            blep (cur, -2.0f * wSaw + 2.0f * wSq, frac);
            if (wSq > 0.0f)
            {
                if (p0 < pw)          blep (cur, -2.0f * wSq, (float) ((phase + 1.0 - pw) / dt));
                else if (phase >= pw) blep (cur, -2.0f * wSq, (float) ((phase - pw) / dt));
            }
        }
        else if (wSq > 0.0f && p0 < pw && phase >= pw)
            blep (cur, -2.0f * wSq, (float) ((phase - pw) / dt));

        const float out = pending;
        pending = cur;
        return out;
    }

    float process (double dt) { bool w; float f; return process (dt, w, f); }

    // Hard-synced slave: reset when the master wrapped 'masterFrac' samples ago.
    float processSynced (double dt, bool masterWrapped, float masterFrac)
    {
        if (! masterWrapped) return process (dt);
        double pr = phase + dt * (1.0 - masterFrac);
        pr -= std::floor (pr);
        const float h = naive (0.0) - naive (pr);
        phase = dt * masterFrac;
        float cur = naive (phase);
        blep (cur, h, masterFrac);
        const float out = pending;
        pending = cur;
        return out;
    }
};

//==============================================================================
// 4-pole zero-delay-feedback ladder with tanh at the loop input, giving
// multimode outputs by mixing the pole taps.
struct Ladder
{
    float s[4] {};
    float sr = 48000.0f;
    int mode = 0;
    // u, y1, y2, y3, y4 mixing coefficients per mode
    static constexpr float kMix[6][5] {
        { 0, 0, 0, 0, 1 },     // LP24
        { 0, 0, 1, 0, 0 },     // LP12
        { 0, 2, -2, 0, 0 },    // BP12
        { 0, 0, 4, -8, 4 },    // BP24
        { 1, -2, 1, 0, 0 },    // HP12
        { 1, -4, 6, -4, 1 } }; // HP24

    void prepare (float sampleRate) { sr = sampleRate; reset(); }
    void reset() { for (auto& x : s) x = 0.0f; }

    float process (float x, float cutoffHz, float k, float drive)
    {
        const float fc = clampf (cutoffHz, 10.0f, 0.45f * sr);
        const float g  = std::tan (kPi * fc / sr);
        const float G  = g / (1.0f + g);
        const float b  = 1.0f - G;

        const float G2 = G * G, G3 = G2 * G, G4 = G3 * G;
        const float S  = G3 * s[0] * b + G2 * s[1] * b + G * s[2] * b + s[3] * b;

        const bool lowpass = mode < 2;
        const float in = x * (lowpass ? 1.0f + 0.45f * std::min (k, 4.0f) : 1.0f);

        float u = (in - k * S) / (1.0f + k * G4);
        u = tanhA (u * drive) / drive;

        float y[4];
        float xi = u;
        for (int i = 0; i < 4; ++i)
        {
            const float v = (xi - s[i]) * G;
            y[i] = v + s[i];
            s[i] = y[i] + v;
            xi = y[i];
        }
        // Keep runaway feedback bounded.
        for (auto& st : s) st = clampf (st, -8.0f, 8.0f);

        const auto& m = kMix[mode];
        return m[0] * u + m[1] * y[0] + m[2] * y[1] + m[3] * y[2] + m[4] * y[3];
    }
};

//==============================================================================
// Analogue-style ADSR with exponential segments; retriggers from the current
// level so voice steals and fast re-attacks don't click.
struct Envelope
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float v = 0.0f, sr = 48000.0f;
    float ca = 1, cd = 1, cr = 1, sus = 1;

    void prepare (float sampleRate) { sr = sampleRate; reset(); }
    void reset() { stage = Idle; v = 0.0f; }

    void set (float a, float d, float s, float r)
    {
        ca = onePoleCoef (a / 1.466f, sr);
        cd = onePoleCoef (d / 4.6f, sr);
        cr = onePoleCoef (r / 4.6f, sr);
        sus = s;
    }
    void noteOn()  { stage = Attack; }
    void noteOff() { if (stage != Idle) stage = Release; }
    bool active() const { return stage != Idle; }

    float next()
    {
        switch (stage)
        {
            case Attack:  v += (1.3f - v) * ca; if (v >= 1.0f) { v = 1.0f; stage = Decay; } break;
            case Decay:   v += (sus - v) * cd; if (std::abs (v - sus) < 1e-4f) stage = Sustain; break;
            case Sustain: v += (sus - v) * 0.002f; break;
            case Release: v += (0.0f - v) * cr; if (v < 1e-5f) { v = 0.0f; stage = Idle; } break;
            case Idle:    break;
        }
        return v;
    }
};

//==============================================================================
struct Lfo
{
    double phase = 0.0;
    float held = 0.0f, from = 0.0f, to = 0.0f;
    Rng rng;
    int wave = 0;

    void reset() { phase = 0.0; held = from = to = 0.0f; }

    float tick (double inc)
    {
        phase += inc;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            held = rng.bipolar();
            from = to; to = rng.bipolar();
        }
        return valueAt (phase);
    }

    float valueAt (double p) const
    {
        const float x = (float) p;
        switch (wave)
        {
            case 0: return std::sin (kTwoPi * x);
            case 1: return x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x;
            case 2: return 1.0f - 2.0f * x;
            case 3: return 2.0f * x - 1.0f;
            case 4: return x < 0.5f ? 1.0f : -1.0f;
            case 5: return held;
            default: { const float t = 0.5f - 0.5f * std::cos (kPi * x); return lerp (from, to, t); }
        }
    }
};

//==============================================================================
// The five saturation voicings. Input is already gained; output stays in +-1.2.
namespace Shaper
{
    inline float warm (float x) { return tanhA (x); }

    inline float asym (float x)
    {
        const float y = x >= 0.0f ? tanhA (x + 0.35f) - 0.3364f : tanhA (0.6f * x + 0.35f) - 0.3364f;
        return y * 1.15f;
    }

    inline float fuzz (float x)
    {
        float a = x > 0.0f ? 1.0f - std::exp (-x) : -(1.0f - std::exp (1.6f * x)) * 0.85f;
        // crossover gap: the broken, sputtering edge of a starved transistor
        const float gap = 0.04f;
        a = a > 0.0f ? std::max (0.0f, a - gap) : std::min (0.0f, a + gap);
        return a / (1.0f - gap);
    }

    inline float fold (float x) { return std::sin (1.3f * x); }

    inline float digital (float x, float wrapAmt)
    {
        const float clipped = clampf (x, -1.0f, 1.0f);
        if (wrapAmt <= 0.0f || std::abs (x) <= 1.0f) return clipped;
        float w = std::fmod (x + 1.0f, 2.0f);
        if (w < 0.0f) w += 2.0f;
        return lerp (clipped, w - 1.0f, wrapAmt);   // integer-overflow wraparound
    }

    inline float process (int type, float x, float gain)
    {
        const float v = x * gain;
        switch (type)
        {
            case 0: return warm (v);
            case 1: return asym (v);
            case 2: return fuzz (v);
            case 3: return fold (v);
            default: return digital (v, clampf ((gain - 6.0f) / 30.0f, 0.0f, 1.0f));
        }
    }

    inline float gainFor (float amount) { return 1.0f + 47.0f * amount * amount; }
}
} // namespace nr
