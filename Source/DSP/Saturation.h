#pragma once
#include "Primitives.h"
#include <array>

namespace nr
{
// Fixed linear-phase half-band FIR. Upsampling + decimation delay = 16 base samples.
// Blackman-windowed sinc, generated from the expression documented in ARCHITECTURE.md.
struct HalfBand
{
    static constexpr std::array<float, 33> taps { 0.0f, -7.464370712e-05f, 0.0f, 0.0008539931377f, 0.0f, -0.003229202035f, 0.0f, 0.008789911481f, 0.0f, -0.02017206968f, 0.0f, 0.0424707263f, 0.0f, -0.09191677109f, 0.0f, 0.3132937312f, 0.4999686487f, 0.3132937312f, 0.0f, -0.09191677109f, 0.0f, 0.0424707263f, 0.0f, -0.02017206968f, 0.0f, 0.008789911481f, 0.0f, -0.003229202035f, 0.0f, 0.0008539931377f, 0.0f, -7.464370712e-05f, 0.0f };
    std::array<float, 33> history {};
    int w = 0;
    void reset() { history.fill (0.0f); w = 0; }
    float process (float x)
    {
        history[(size_t) w] = x;
        float y = 0.0f;
        // Every second tap except the centre is exactly zero.
        for (int j = 1; j < 33; j += 2)
            y += taps[(size_t) j] * history[(size_t) ((w + 33 - j) % 33)];
        y += taps[16] * history[(size_t) ((w + 17) % 33)];
        w = (w + 1) % 33;
        return y;
    }
};

// Stateful voicing, shared by 2x pre and 4x post stages. Type changes fade for 20 ms.
// Compensation tracks dry/wet power slowly, bounded to prevent gain on silence.
struct DriveVoicing
{
    float sr = 96000.0f, env = 0.0f, dryPower = 0.01f, wetPower = 0.01f;
    float envelopeK = 0.0f, powerK = 0.0f, fadeStep = 0.0f;
    std::array<float, 5> weights { 1, 0, 0, 0, 0 };
    bool first = true;
    OnePoleLP emphasis, deemphasis;
    void prepare (float rate)
    {
        sr = rate; envelopeK = onePoleCoef (0.004f, sr);
        powerK = onePoleCoef (0.15f, sr); fadeStep = 1.0f / (0.020f * sr);
        emphasis.setFreq (700.0f, sr); deemphasis.setFreq (700.0f, sr);
        reset();
    }
    void reset()
    {
        env = 0; dryPower = wetPower = 0.01f; first = true;
        emphasis.reset(); deemphasis.reset();
    }
    float process (int type, float x, float drive, float bias, float tilt, bool compensate)
    {
        type = std::clamp (type, 0, 4);
        if (first) { weights.fill (0); weights[(size_t) type] = 1; first = false; }
        // A convex crossfade preserves unity weight even when switches are interrupted.
        const float remaining = 1.0f - weights[(size_t) type];
        if (remaining > 0.0f)
        {
            const float step = std::min (fadeStep, remaining);
            const float scale = (remaining - step) / remaining;
            for (int i = 0; i < 5; ++i) if (i != type) weights[(size_t) i] *= scale;
            weights[(size_t) type] += step;
        }
        env += (std::abs (x) - env) * envelopeK;
        const float low = emphasis.process (x);
        const float warmWeight = weights[0];
        const float emphasisGain = 0.18f * warmWeight * drive - 0.65f * tilt;
        const float input = x + emphasisGain * low;
        const float gain = Shaper::gainFor (drive);
        const float v = input * gain;
        const float offset = bias * (0.3f + 1.7f * drive);
        float wet = 0.0f;
        for (int i = 0; i < 5; ++i)
        {
            if (weights[(size_t) i] < 1.0e-7f) continue;
            float y = 0.0f;
            switch (i)
            {
                case 0: y = tanhA (v + offset) - tanhA (offset); break;
                case 1:
                {
                    const float asymBias = offset + 0.2f + 0.2f * drive;
                    const float slope = v >= 0 ? 1.0f : 0.85f - 0.4f * drive;
                    y = tanhA (slope * v + asymBias) - tanhA (asymBias);
                    break;
                }
                case 2:
                {
                    // Envelope-dependent starvation: attacks open the gate, quiet decays sputter.
                    const float starvation = drive * drive;
                    const float gap = 0.04f + 0.55f * starvation / (1.0f + env * 16.0f);
                    const float sag = 1.0f / (1.0f + env * starvation * 3.0f);
                    const float shifted = v * sag + offset + starvation * env * 0.7f;
                    const float a = Shaper::fuzz (shifted);
                    y = std::copysign (std::max (0.0f, std::abs (a) - gap), a) / (1.0f - gap);
                    break;
                }
                case 3: y = std::sin (1.3f * (v + offset)) - std::sin (1.3f * offset); break;
                default:
                {
                    const float wrapped = Shaper::digital (v + offset, clampf ((gain - 6.0f) / 30.0f, 0, 1));
                    const float levels = std::exp2 (16.0f - 12.0f * drive);
                    y = std::trunc (wrapped * levels) / levels;
                    break;
                }
            }
            wet += weights[(size_t) i] * y;
        }
        // Inverse low shelf, stable for the complete tilt range.
        const float lowWet = deemphasis.process (wet);
        wet -= emphasisGain / (1.0f + emphasisGain) * lowWet;
        dryPower += (x * x - dryPower) * powerK;
        wetPower += (wet * wet - wetPower) * powerK;
        const float correction = compensate ? clampf (std::sqrt ((dryPower + 1.0e-9f) / (wetPower + 1.0e-9f)), 0.02f, 4.0f) : 1.0f;
        return wet * correction;
    }
};

struct PreDrive2x
{
    static constexpr int latency = 16;
    HalfBand up, down;
    DriveVoicing voice;
    void prepare (float sr) { voice.prepare (sr * 2.0f); reset(); }
    void reset() { up.reset(); down.reset(); voice.reset(); }
    float process (int type, float x, float drive, float bias, float tilt, bool comp)
    {
        const float a = voice.process (type, up.process (2.0f * x), drive, bias, tilt, comp);
        const float y = down.process (a);
        down.process (voice.process (type, up.process (0.0f), drive, bias, tilt, comp));
        return y;
    }
};
} // namespace nr
