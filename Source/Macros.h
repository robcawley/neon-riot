#pragma once
#include <cmath>
#include <algorithm>
#include <functional>
#include <tuple>
#include "Parameters.h"

// The eight performance macros. Each is centre-neutral (-1..+1, 0 = the patch as designed)
// and applies curated offsets to a per-block snapshot of real parameter values. The
// parameters themselves are never written. Values are clamped to each target's own range
// afterwards, so macros, automation and LFOs stack safely.
class MacroMap
{
public:
    // 'indexOf' maps a parameter ID to its index in the snapshot.
    void bind (const std::function<int (const char*)>& indexOf)
    {
        using namespace PID;
        auto i = [&] (const char* id) { return indexOf (id); };
        subLevelI = i (subLevel); anchorI = i (anchorAmt); anchorFI = i (anchorFreq); noiseI = i (noiseLevel);
        driftI = i (drift); instI = i (instability);
        biteI = i (biteAmt); pickI = i (bitePick); combI = i (biteComb); toneI = i (biteTone);
        envI = i (fltEnv); fVelI = i (fltVel); fDI = i (fD); fSI = i (fS); aAI = i (aA); aDI = i (aD);
        aSI = i (aS); aRI = i (aR); ampVelI = i (ampVel); cutI = i (fltCutoff); resoI = i (fltReso);
        fDriveI = i (fltDrive); preI = i (preDrive); postI = i (postDrive); postTypeI = i (postType);
        chaosAmtI = i (chaosAmount); chaosMixI = i (chaosMix); selectI = i (chaosSelect); modeI = i (chaosMode);
        l1FadeI = i (l1Fade); l2FadeI = i (l2Fade);
        l1AmtI = i (l1Amt); l2AmtI = i (l2Amt); l1RateI = i (l1Rate); l2RateI = i (l2Rate);
        chorusMixI = i (chorusMix); chorusRateI = i (chorusRate); chorusDepthI = i (chorusDepth);
        revMixI = i (reverbMix); revSizeI = i (reverbSize); delayMixI = i (delayMix); outI = i (output);
    }

    // v: real values, lo/hi: each parameter's range. m: smoothed macro positions in panel order.
    void apply (float* v, const float* lo, const float* hi, const float* m) const
    {
        auto up = [] (float x) { return std::max (0.0f, x); };
        auto dn = [] (float x) { return std::max (0.0f, -x); };
        auto add = [&] (int k, float d) { v[k] = std::clamp (v[k] + d, lo[k], hi[k]); };
        auto mul = [&] (int k, float f) { v[k] = std::clamp (v[k] * f, lo[k], hi[k]); };
        // adds away from zero, so a negative envelope amount deepens rather than flips
        auto addSigned = [&] (int k, float d) { add (k, v[k] < 0.0f ? -d : d); };

        if (const float w = m[0]; w != 0.0f)            // WEIGHT: fundamental, clean low end, steadier pitch
        {
            add (subLevelI, 0.3f * w);
            add (outI, -2.5f * up (w));          // heavier, not just louder
            add (anchorI, 0.6f * w);
            mul (anchorFI, std::exp2 (0.6f * w));
            mul (noiseI, 1.0f - 0.5f * up (w));
            mul (driftI, 1.0f - 0.4f * up (w));
        }
        if (const float b = m[1]; b != 0.0f)            // BITE: picked and snappy <-> muted and damped
        {
            const float u = up (b), d = dn (b);
            add (biteI, 0.7f * u);  mul (biteI, 1.0f - 0.5f * d);
            add (pickI, 0.45f * u); mul (pickI, 1.0f - d);
            add (combI, 0.25f * u);
            add (toneI, 0.25f * u - 0.45f * d);
            addSigned (envI, 0.3f * u); mul (envI, 1.0f - 0.55f * d);
            add (fVelI, 0.35f * u);
            add (ampVelI, 0.25f * u);
            mul (fDI, std::exp2 (-0.9f * u));
            mul (aAI, std::exp2 (-2.0f * u));
            mul (aDI, std::exp2 (-1.6f * d));
            mul (aSI, 1.0f - 0.6f * d);
            mul (aRI, std::exp2 (-2.5f * d));
            mul (cutI, std::exp2 (-1.0f * d));
        }
        if (const float a = m[2]; a != 0.0f)            // ACID: resonance, envelope sweep, accent
        {
            const float u = up (a), d = dn (a);
            add (resoI, 0.45f * u);  mul (resoI, 1.0f - 0.8f * d);
            addSigned (envI, 0.45f * u); mul (envI, 1.0f - 0.8f * d);
            add (fDriveI, 0.45f * u); mul (fDriveI, 1.0f - 0.8f * d);
            mul (fDI, std::exp2 (-0.8f * u));
            mul (fSI, 1.0f - 0.75f * u);
            add (fVelI, 0.35f * u);
            mul (cutI, std::exp2 (-1.2f * u + 1.3f * d));
        }
        if (const float dr = m[3]; dr != 0.0f)          // DRIVE: pre and post together, bass held
        {
            const float u = up (dr), d = dn (dr);
            const int postType = (int) std::lround (v[postTypeI]);
            const float harsh = (postType >= 3) ? 0.6f : 1.0f;   // Fold and Digital get there sooner
            add (preI, 0.5f * u);          mul (preI, 1.0f - d);
            add (postI, 0.5f * harsh * u); mul (postI, 1.0f - d);
            add (fDriveI, 0.2f * u);       mul (fDriveI, 1.0f - 0.6f * d);
            add (anchorI, 0.25f * u);
        }
        if (const float c = m[4]; c != 0.0f)            // CHAOS: intensity and wet, plus instability
        {
            const float u = up (c), d = dn (c);
            // On a patch with the dial at BYPASS, turning CHAOS up engages MELTDOWN from zero
            // intensity, so it fades in. The dial's outer ring shows the effective position.
            if (u > 0.0f && std::lround (v[selectI]) == 0 && std::lround (v[modeI]) == 0)
                v[selectI] = 7.0f;
            add (chaosAmtI, 0.8f * u); mul (chaosAmtI, 1.0f - d);
            add (chaosMixI, 1.0f * u); mul (chaosMixI, 1.0f - d);
            add (instI, 0.3f * u);     mul (instI, 1.0f - d);
            add (driftI, 0.15f * u);
        }
        if (const float mo = m[5]; mo != 0.0f)          // MOTION: LFO depth and rate, drift, chorus
        {
            const float u = up (mo), d = dn (mo);
            const float depth = (1.0f + 1.5f * u) * (1.0f - d);
            // patches with still LFOs get their own gentle movement that blooms after the attack
            for (auto [amt, fade, extra] : { std::tuple { l1AmtI, l1FadeI, 0.12f }, std::tuple { l2AmtI, l2FadeI, 0.18f } })
            {
                if (v[amt] == 0.0f && u > 0.0f) { add (amt, extra * u); add (fade, 0.6f * u); }
                else mul (amt, depth);
            }
            mul (l1RateI, std::exp2 (mo)); mul (l2RateI, std::exp2 (mo));
            add (driftI, 0.35f * u); mul (driftI, 1.0f - d);
            mul (instI, 1.0f - d);
            add (chorusDepthI, 0.3f * u);
            mul (chorusRateI, std::exp2 (0.8f * mo));
        }
        if (const float s = m[6]; s != 0.0f)            // SPACE: sends; low end stays centred downstream
        {
            const float u = up (s), d = dn (s);
            add (revMixI, 0.4f * u);    mul (revMixI, 1.0f - d);
            add (delayMixI, 0.28f * u); mul (delayMixI, 1.0f - d);
            add (chorusMixI, 0.35f * u); mul (chorusMixI, 1.0f - d);
            add (revSizeI, 0.3f * u);
        }
        if (const float o = m[7]; o != 0.0f)            // OUTPUT: -24 dB .. +6 dB
            add (outI, o > 0.0f ? 6.0f * o : 24.0f * o);
    }

private:
    int subLevelI = 0, anchorI = 0, anchorFI = 0, noiseI = 0, driftI = 0, instI = 0;
    int biteI = 0, pickI = 0, combI = 0, toneI = 0, envI = 0, fVelI = 0, fDI = 0, fSI = 0;
    int aAI = 0, aDI = 0, aSI = 0, aRI = 0, ampVelI = 0, cutI = 0, resoI = 0, fDriveI = 0;
    int preI = 0, postI = 0, postTypeI = 0, chaosAmtI = 0, chaosMixI = 0, selectI = 0, modeI = 0, l1FadeI = 0, l2FadeI = 0;
    int l1AmtI = 0, l2AmtI = 0, l1RateI = 0, l2RateI = 0;
    int chorusMixI = 0, chorusRateI = 0, chorusDepthI = 0, revMixI = 0, revSizeI = 0, delayMixI = 0, outI = 0;
};
