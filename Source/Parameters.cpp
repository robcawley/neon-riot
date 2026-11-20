#include "Parameters.h"
#include <limits>
#include <sstream>
#include <locale>
#include <iterator>

using namespace juce;
namespace
{
    float invalidText() { return std::numeric_limits<float>::quiet_NaN(); }

    // Strict, locale-independent number + optional unit. Parsing is a host/UI operation.
    float numberWithUnit (const String& text, const String& unit = {}, double bareScale = 1.0,
                          const String& alternate = {}, double alternateScale = 1.0)
    {
        std::istringstream input (text.trim().toStdString());
        input.imbue (std::locale::classic());
        double value;
        if (! (input >> value) || ! std::isfinite (value)) return invalidText();
        const std::string remainder ((std::istreambuf_iterator<char> (input)), std::istreambuf_iterator<char>());
        const auto suffix = String (remainder).trim();
        double scale = bareScale;
        if (suffix.isNotEmpty() && ! suffix.equalsIgnoreCase (unit))
        {
            if (alternate.isEmpty() || ! suffix.equalsIgnoreCase (alternate)) return invalidText();
            scale = alternateScale;
        }
        const auto limit = static_cast<double> (std::numeric_limits<float>::max());
        return static_cast<float> (jlimit (-limit, limit, value * scale));
    }

    // JUCE's inverse callback has no current-value argument. Keep invalid entry unchanged
    // here, before normalisation, for both host fields and the panel's typed editor.
    class TextParameterFloat final : public AudioParameterFloat
    {
    public:
        TextParameterFloat (const ParameterID& pid, const String& parameterName, NormalisableRange<float> parameterRange,
                            float initial, const AudioParameterFloatAttributes& attrs = {})
            : AudioParameterFloat (pid, parameterName, parameterRange, initial, attrs),
              parse (attrs.getValueFromStringFunction()) {}

        float getValueForText (const String& text) const override
        {
            const auto value = parse ? parse (text) : numberWithUnit (text);
            if (! std::isfinite (value))
                return static_cast<const AudioProcessorParameter&> (*this).getValue();
            return convertTo0to1 (jlimit (range.start, range.end, value));
        }
    private:
        std::function<float (const String&)> parse;
    };
}
using APF = TextParameterFloat;
using APC = AudioParameterChoice;
using APB = AudioParameterBool;
using API = AudioParameterInt;

namespace
{
    constexpr int kVersion = 1;
    ParameterID id (const char* s) { return { s, kVersion }; }

    NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    AudioParameterFloatAttributes pct()
    {
        return AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })
            .withValueFromStringFunction ([] (const String& s) { return numberWithUnit (s, "%", 0.01); });
    }
    AudioParameterFloatAttributes bipct()
    {
        return AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { auto n = roundToInt (v * 100.0f); return (n > 0 ? "+" : "") + String (n) + " %"; })
            .withValueFromStringFunction ([] (const String& s) { return numberWithUnit (s, "%", 0.01); });
    }
    // Display text only (ranges unchanged): "+9.0 ct", "-3.5 st".
    AudioParameterFloatAttributes signedUnit (const char* unit)
    {
        return AudioParameterFloatAttributes().withStringFromValueFunction ([unit] (float v, int)
            { const float r = std::round (v * 10.0f) / 10.0f; return (r > 0.0f ? "+" : "") + String (r, 1) + " " + unit; })
            .withValueFromStringFunction ([unit] (const String& s) { return numberWithUnit (s, unit); });
    }
    AudioParameterFloatAttributes ms()
    {
        return AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return v < 1.0f ? String (v * 1000.0f, 1) + " ms" : String (v, 2) + " s"; })
            .withValueFromStringFunction ([] (const String& s) { return numberWithUnit (s, "ms", 0.001, "s", 1.0); });
    }
    AudioParameterFloatAttributes hz()
    {
        return AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return v < 1000.0f ? String (v, v < 10.0f ? 2 : 0) + " Hz" : String (v / 1000.0f, 2) + " kHz"; })
            .withValueFromStringFunction ([] (const String& s)
            {
                const auto t = s.trim();
                return t.endsWithIgnoreCase ("k") ? numberWithUnit (t, "k", 1000.0)
                                                   : numberWithUnit (t, "Hz", 1.0, "kHz", 1000.0);
            });
    }
    AudioParameterFloatAttributes db()
    {
        return AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (v, 1) + " dB"; })
            .withValueFromStringFunction ([] (const String& s) { return numberWithUnit (s, "dB"); });
    }
    AudioParameterFloatAttributes waveName()
    {
        return AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            if (v < 0.05f) return String ("Triangle");
            if (std::abs (v - 1.0f) < 0.05f) return String ("Saw");
            if (v > 1.95f) return String ("Pulse");
            return v < 1.0f ? "Tri>Saw " + String (roundToInt (v * 100)) + "%"
                            : "Saw>Pulse " + String (roundToInt ((v - 1.0f) * 100)) + "%";
        }).withValueFromStringFunction ([] (const String& s)
        {
            const auto t = s.trim();
            if (t.equalsIgnoreCase ("Triangle")) return 0.0f;
            if (t.equalsIgnoreCase ("Saw")) return 1.0f;
            if (t.equalsIgnoreCase ("Pulse")) return 2.0f;
            if (t.startsWithIgnoreCase ("Tri>Saw ")) return numberWithUnit (t.substring (8), "%", 0.01);
            if (t.startsWithIgnoreCase ("Saw>Pulse ")) return 1.0f + numberWithUnit (t.substring (10), "%", 0.01);
            return numberWithUnit (t);
        });
    }

    void oscGroup (AudioProcessorValueTreeState::ParameterLayout& l, int n,
                   const char* wave, const char* shape, const char* oct, const char* semi,
                   const char* fine, const char* level, float defWave, float defLevel)
    {
        const String p = "Osc " + String (n) + " ";
        auto g = std::make_unique<AudioProcessorParameterGroup> ("osc" + String (n), "Oscillator " + String (n), " | ");
        g->addChild (std::make_unique<APF> (id (wave),  p + "Wave",  NormalisableRange<float> (0.0f, 2.0f), defWave, waveName()));
        g->addChild (std::make_unique<APF> (id (shape), p + "Shape", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()));
        g->addChild (std::make_unique<API> (id (oct),   p + "Octave", -3, 3, 0));
        g->addChild (std::make_unique<API> (id (semi),  p + "Semitone", -12, 12, 0));
        g->addChild (std::make_unique<APF> (id (fine),  p + "Fine", NormalisableRange<float> (-100.0f, 100.0f), 0.0f,
                         signedUnit ("ct")));
        g->addChild (std::make_unique<APF> (id (level), p + "Level", NormalisableRange<float> (0.0f, 1.0f), defLevel, pct()));
        l.add (std::move (g));
    }
}

AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;

    oscGroup (l, 1, PID::osc1Wave, PID::osc1Shape, PID::osc1Oct, PID::osc1Semi, PID::osc1Fine, PID::osc1Level, 1.0f, 0.8f);
    oscGroup (l, 2, PID::osc2Wave, PID::osc2Shape, PID::osc2Oct, PID::osc2Semi, PID::osc2Fine, PID::osc2Level, 2.0f, 0.0f);
    l.add (std::make_unique<APB> (id (PID::osc2Sync), "Osc 2 Sync", false));

    l.add (std::make_unique<AudioProcessorParameterGroup> ("subnoise", "Sub and Noise", " | ",
        std::make_unique<APF> (id (PID::subLevel), "Sub Level", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct()),
        std::make_unique<APC> (id (PID::subOct), "Sub Octave", Choices::subOct, 0),
        std::make_unique<APC> (id (PID::subWave), "Sub Wave", Choices::subWave, 2),
        std::make_unique<APF> (id (PID::noiseLevel), "Noise Level", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()),
        std::make_unique<APF> (id (PID::noiseColour), "Noise Colour", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bipct())));

    l.add (std::make_unique<AudioProcessorParameterGroup> ("voice", "Voice", " | ",
        std::make_unique<APC> (id (PID::voiceMode), "Voice Mode", Choices::voiceMode, 0),
        std::make_unique<APF> (id (PID::glide), "Glide", skewed (0.0f, 2.0f, 0.15f), 0.0f, ms()),
        std::make_unique<API> (id (PID::pbRange), "Pitch Bend Range", 1, 24, 2),
        std::make_unique<APF> (id (PID::drift), "Drift", NormalisableRange<float> (0.0f, 1.0f), 0.15f, pct()),
        std::make_unique<APF> (id (PID::instability), "Instability", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct())));

    l.add (std::make_unique<AudioProcessorParameterGroup> ("filter", "Filter", " | ",
        std::make_unique<APC> (id (PID::fltMode), "Filter Mode", Choices::fltMode, 0),
        std::make_unique<APF> (id (PID::fltCutoff), "Cutoff", skewed (20.0f, 20000.0f, 800.0f), 1200.0f, hz()),
        std::make_unique<APF> (id (PID::fltReso), "Resonance", NormalisableRange<float> (0.0f, 1.2f), 0.25f,
             AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                 { return v > 1.0f ? "FEEDBACK +" + String (roundToInt ((v - 1.0f) * 500)) + " %"
                                   : String (roundToInt (v * 100)) + " %"; })
                 .withValueFromStringFunction ([] (const String& s)
                 {
                     const auto t = s.trim();
                     return t.startsWithIgnoreCase ("FEEDBACK ")
                         ? 1.0f + numberWithUnit (t.substring (9), "%", 0.002)
                         : numberWithUnit (t, "%", 0.01);
                 })),
        std::make_unique<APF> (id (PID::fltEnv), "Filter Env Amount", NormalisableRange<float> (-1.0f, 1.0f), 0.4f, bipct()),
        std::make_unique<APF> (id (PID::fltKey), "Filter Key Track", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct()),
        std::make_unique<APF> (id (PID::fltVel), "Filter Velocity", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct()),
        std::make_unique<APF> (id (PID::fltDrive), "Filter Drive", NormalisableRange<float> (0.0f, 1.0f), 0.2f, pct())));

    auto envGroup = [&] (const char* gid, const char* name, const char* a, const char* d, const char* s, const char* r,
                         float da, float dd, float ds, float dr)
    {
        auto g = std::make_unique<AudioProcessorParameterGroup> (gid, name, " | ");
        const String p = String (name) + " ";
        g->addChild (std::make_unique<APF> (id (a), p + "Attack",  skewed (0.0003f, 10.0f, 0.2f), da, ms()));
        g->addChild (std::make_unique<APF> (id (d), p + "Decay",   skewed (0.001f, 10.0f, 0.4f), dd, ms()));
        g->addChild (std::make_unique<APF> (id (s), p + "Sustain", NormalisableRange<float> (0.0f, 1.0f), ds, pct()));
        g->addChild (std::make_unique<APF> (id (r), p + "Release", skewed (0.001f, 10.0f, 0.4f), dr, ms()));
        return g;
    };
    l.add (envGroup ("fenv", "Filter Env", PID::fA, PID::fD, PID::fS, PID::fR, 0.0005f, 0.25f, 0.2f, 0.15f));
    {
        auto g = envGroup ("aenv", "Amp Env", PID::aA, PID::aD, PID::aS, PID::aR, 0.0005f, 0.4f, 0.9f, 0.08f);
        g->addChild (std::make_unique<APF> (id (PID::ampVel), "Amp Velocity", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct()));
        l.add (std::move (g));
    }

    auto lfoGroup = [&] (int n, const char* wave, const char* rate, const char* sync, const char* div,
                         const char* dest, const char* amt, int defDest)
    {
        const String p = "LFO " + String (n) + " ";
        auto g = std::make_unique<AudioProcessorParameterGroup> ("lfo" + String (n), "LFO " + String (n), " | ");
        g->addChild (std::make_unique<APC> (id (wave), p + "Wave", Choices::lfoWave, 0));
        g->addChild (std::make_unique<APF> (id (rate), p + "Rate", skewed (0.02f, 40.0f, 3.0f), 4.0f, hz()));
        g->addChild (std::make_unique<APB> (id (sync), p + "Tempo Sync", false));
        g->addChild (std::make_unique<APC> (id (div),  p + "Division", Choices::lfoDiv, 8));
        g->addChild (std::make_unique<APC> (id (dest), p + "Destination", Choices::lfoDest, defDest));
        g->addChild (std::make_unique<APF> (id (amt),  p + "Amount", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bipct()));
        return g;
    };
    l.add (lfoGroup (1, PID::l1Wave, PID::l1Rate, PID::l1Sync, PID::l1Div, PID::l1Dest, PID::l1Amt, 1));
    l.add (lfoGroup (2, PID::l2Wave, PID::l2Rate, PID::l2Sync, PID::l2Div, PID::l2Dest, PID::l2Amt, 2));

    l.add (std::make_unique<AudioProcessorParameterGroup> ("perf", "Performance", " | ",
        std::make_unique<APF> (id (PID::mwLfo), "Mod Wheel to LFO 1", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()),
        std::make_unique<APF> (id (PID::mwCutoff), "Mod Wheel to Cutoff", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bipct())));

    l.add (std::make_unique<AudioProcessorParameterGroup> ("bite", "String Bite", " | ",
        std::make_unique<APF> (id (PID::biteAmt), "Bite Amount", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()),
        std::make_unique<APF> (id (PID::bitePick), "Bite Pick", NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct()),
        std::make_unique<APF> (id (PID::biteComb), "Bite Comb", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct()),
        std::make_unique<APF> (id (PID::biteTone), "Bite Tone", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct())));

    l.add (std::make_unique<AudioProcessorParameterGroup> ("drive", "Saturation", " | ",
        std::make_unique<APF> (id (PID::preDrive), "Pre Drive", NormalisableRange<float> (0.0f, 1.0f), 0.1f, pct()),
        std::make_unique<APC> (id (PID::preType), "Pre Drive Type", Choices::driveType, 0),
        std::make_unique<APF> (id (PID::postDrive), "Post Drive", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()),
        std::make_unique<APC> (id (PID::postType), "Post Drive Type", Choices::driveType, 1),
        std::make_unique<APF> (id (PID::driveTone), "Drive Tone", NormalisableRange<float> (0.0f, 1.0f), 0.7f, pct()),
        std::make_unique<APF> (id (PID::driveMix), "Drive Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct()),
        std::make_unique<APB> (id (PID::driveComp), "Drive Compensation", true),
        std::make_unique<APC> (id (PID::driveEngine), "Drive Engine", StringArray { "M1 Original", "M2 Saturation" }, 0),
        std::make_unique<APF> (id (PID::preBias), "Pre Bias", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bipct()),
        std::make_unique<APF> (id (PID::postBias), "Post Bias", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bipct()),
        std::make_unique<APF> (id (PID::driveTilt), "Drive Tilt", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bipct())));

    l.add (std::make_unique<AudioProcessorParameterGroup> ("master", "Sub Anchor and Output", " | ",
        std::make_unique<APF> (id (PID::anchorAmt), "Sub Anchor", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()),
        std::make_unique<APF> (id (PID::anchorFreq), "Sub Anchor Freq", skewed (40.0f, 250.0f, 110.0f), 110.0f, hz()),
        std::make_unique<APF> (id (PID::output), "Output", NormalisableRange<float> (-36.0f, 12.0f), -1.0f, db())));

    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("chaos", "CHAOS", " | ");
        group->addChild (std::make_unique<APC> (id (PID::chaosSelect), "CHAOS Select", Choices::chaos, 0));
        group->addChild (std::make_unique<APC> (id (PID::chaosMode), "CHAOS Mode", Choices::chaosMode, 0));
        group->addChild (std::make_unique<APF> (id (PID::chaosMorph), "CHAOS Morph", NormalisableRange<float> (0.0f, 7.0f), 0.0f, AudioParameterFloatAttributes()));
        group->addChild (std::make_unique<APF> (id (PID::chaosAmount), "CHAOS Intensity", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()));
        group->addChild (std::make_unique<APC> (id (PID::chaosDiv), "CHAOS Division", Choices::lfoDiv, 8));
        group->addChild (std::make_unique<APB> (id (PID::chaosPunch), "CHAOS PUNCH", false));
        group->addChild (std::make_unique<APB> (id (PID::chaosHold), "CHAOS HOLD", false));
        group->addChild (std::make_unique<APF> (id (PID::chaosHoldPitch), "HOLD Pitch", NormalisableRange<float> (-24.0f, 24.0f), 0.0f, signedUnit ("st")));
        group->addChild (std::make_unique<APF> (id (PID::chaosHoldSize), "HOLD Size", NormalisableRange<float> (0.05f, 1.0f), 1.0f, pct()));
        group->addChild (std::make_unique<APF> (id (PID::chaosMix), "CHAOS Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct()));
        l.add (std::move (group));
    }
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("delay", "Stereo Delay", " | ");
        group->addChild (std::make_unique<APF> (id (PID::delayMix), "Delay Mix", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()));
        group->addChild (std::make_unique<APF> (id (PID::delayTime), "Delay Time", NormalisableRange<float> (0.005f, 2.0f), 0.25f, ms()));
        group->addChild (std::make_unique<APB> (id (PID::delaySync), "Delay Tempo Sync", true));
        group->addChild (std::make_unique<APC> (id (PID::delayDiv), "Delay Division", Choices::lfoDiv, 8));
        group->addChild (std::make_unique<APF> (id (PID::delayFeedback), "Delay Feedback", NormalisableRange<float> (0.0f, 0.95f), 0.4f, pct()));
        group->addChild (std::make_unique<APB> (id (PID::delayPing), "Delay Ping Pong", true));
        group->addChild (std::make_unique<APF> (id (PID::delayTone), "Delay Tone", NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct()));
        l.add (std::move (group));
    }
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("reverb", "Dark Reverb", " | ");
        group->addChild (std::make_unique<APF> (id (PID::reverbMix), "Reverb Mix", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()));
        group->addChild (std::make_unique<APF> (id (PID::reverbPre), "Reverb Pre Delay", NormalisableRange<float> (0.0f, 0.2f), 0.02f, ms()));
        group->addChild (std::make_unique<APF> (id (PID::reverbSize), "Reverb Size", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct()));
        group->addChild (std::make_unique<APF> (id (PID::reverbDamp), "Reverb Damping", NormalisableRange<float> (0.0f, 1.0f), 0.6f, pct()));
        l.add (std::move (group));
    }
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("chorus", "Stereo Chorus", " | ");
        group->addChild (std::make_unique<APF> (id (PID::chorusMix), "Chorus Mix", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()));
        group->addChild (std::make_unique<APF> (id (PID::chorusRate), "Chorus Rate", NormalisableRange<float> (0.03f, 2.0f), 0.25f, hz()));
        group->addChild (std::make_unique<APF> (id (PID::chorusDepth), "Chorus Depth", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct()));
        l.add (std::move (group));
    }
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("shifter", "Frequency Shifter", " | ");
        group->addChild (std::make_unique<APF> (id (PID::shiftMix), "Shift Mix", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()));
        group->addChild (std::make_unique<APF> (id (PID::shiftHz), "Shift Frequency", NormalisableRange<float> (-1000.0f, 1000.0f), 0.0f, hz()));
        group->addChild (std::make_unique<APF> (id (PID::shiftFeedback), "Shift Feedback", NormalisableRange<float> (0.0f, 0.3f), 0.0f, pct()));
        l.add (std::move (group));
    }

    {
        // Centre-neutral macro offsets: 0 leaves the patch exactly as designed.
        auto group = std::make_unique<AudioProcessorParameterGroup> ("macros", "Macros", " | ");
        const char* names[] { "WEIGHT", "BITE", "ACID", "DRIVE", "CHAOS", "MOTION", "SPACE", "OUTPUT" };
        for (int k = 0; k < 8; ++k)
            group->addChild (std::make_unique<APF> (id (kMacroIds[k]), String ("Macro ") + names[k],
                                                    NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bipct()));
        l.add (std::move (group));
    }
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("lfoart", "LFO Articulation", " | ");
        group->addChild (std::make_unique<APB> (id (PID::l1Retrig), "LFO 1 Retrigger", false));
        group->addChild (std::make_unique<APF> (id (PID::l1Fade), "LFO 1 Fade In", skewed (0.0f, 5.0f, 0.6f), 0.0f, ms()));
        group->addChild (std::make_unique<APB> (id (PID::l2Retrig), "LFO 2 Retrigger", false));
        group->addChild (std::make_unique<APF> (id (PID::l2Fade), "LFO 2 Fade In", skewed (0.0f, 5.0f, 0.6f), 0.0f, ms()));
        l.add (std::move (group));
    }

    return l;
}
