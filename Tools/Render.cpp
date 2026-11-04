#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Evolve.h"
#include <chrono>
#include <numeric>
#include <random>
#include <vector>
#include <cstdlib>
#include <new>
#include <set>

// Test-only C++ allocation probe. Enabled only around the audio callback.
thread_local bool probeAllocations = false;
thread_local uint64_t callbackAllocations = 0;
void* operator new (std::size_t size)
{
    if (probeAllocations) ++callbackAllocations;
    if (void* p = std::malloc (std::max (size, std::size_t (1)))) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return ::operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void* operator new (std::size_t size, std::align_val_t alignment)
{
    if (probeAllocations) ++callbackAllocations;
    void* p = nullptr;
    if (posix_memalign (&p, (std::size_t) alignment, std::max (size, std::size_t (1))) == 0) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size, std::align_val_t alignment) { return ::operator new (size, alignment); }
void operator delete (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { std::free (p); }

int renderPresets (const juce::File& outDir)
{
    outDir.createDirectory();

    const double sr = 48000.0; const int bs = 256;
    const double bpm = 128.0, beat = 60.0 / bpm;
    const int total = (int) (sr * 8.0);

    NeonRiotProcessor proc;
    proc.setPlayConfigDetails (0, 2, sr, bs);
    for (int prog = 0; prog < proc.getNumPrograms(); ++prog)
    {
        // Fresh deterministic engine per preset, independent of preceding renders.
        proc.setCurrentProgram (prog);
        proc.prepareToPlay (sr, bs);

        // riff: 16 eighths of a bass line, overlapping slides on 3 and 7, then a held 3-note chord
        juce::MidiBuffer all;
        const int notes[] { 36, 36, 48, 39, 36, 43, 46, 34, 36, 36, 48, 39, 41, 43, 46, 31 };
        for (int k = 0; k < 16; ++k)
        {
            const int on  = (int) (k * beat * 0.5 * sr);
            const bool slide = (k % 4 == 2);
            const int off = on + (int) (beat * 0.5 * sr * (slide ? 1.2 : 0.6));
            all.addEvent (juce::MidiMessage::noteOn (1, notes[k], (juce::uint8) (k % 4 == 0 ? 120 : 80)), on);
            all.addEvent (juce::MidiMessage::noteOff (1, notes[k]), off);
        }
        const int chordOn = (int) (sr * 4.5);
        for (int n : { 36, 43, 48 }) all.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), chordOn);
        for (int n : { 36, 43, 48 }) all.addEvent (juce::MidiMessage::noteOff (1, n), (int) (sr * 6.5));

        juce::AudioBuffer<float> out (2, total), block (2, bs);
        double peak = 0, sum = 0, dc = 0, maxStep = 0; int bad = 0; float prev = 0, prevRight = 0;
        for (int pos = 0; pos < total; pos += bs)
        {
            const int n = std::min (bs, total - pos);
            juce::MidiBuffer mb;
            for (const auto m : all)
                if (m.samplePosition >= pos && m.samplePosition < pos + n)
                    mb.addEvent (m.getMessage(), m.samplePosition - pos);
            block.setSize (2, n, false, false, true);
            proc.processBlock (block, mb);
            for (int i = 0; i < n; ++i)
            {
                const float s = block.getSample (0, i);
                const float right = block.getSample (1, i);
                if (! std::isfinite (s) || ! std::isfinite (right)) ++bad;
                peak = std::max ({ peak, (double) std::abs (s), (double) std::abs (right) });
                sum += 0.5 * (s * s + right * right); dc += 0.5 * (s + right);
                maxStep = std::max ({ maxStep, (double) std::abs (s - prev), (double) std::abs (right - prevRight) });
                prev = s; prevRight = right;
                out.setSample (0, pos + i, s); out.setSample (1, pos + i, block.getSample (1, i));
            }
        }
        const auto name = proc.getProgramName (prog);
        auto f = outDir.getChildFile (juce::String (prog).paddedLeft ('0', 2) + " " + name + ".wav");
        f.deleteFile();
        juce::WavAudioFormat wav;
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0)))
            w->writeFromAudioSampleBuffer (out, 0, total);

        std::printf ("%-22s peak %6.1f dBFS  rms %6.1f dBFS  dc %+.4f  maxstep %.3f  nonfinite %d\n",
                     name.toRawUTF8(), juce::Decibels::gainToDecibels (peak, -120.0),
                     juce::Decibels::gainToDecibels (std::sqrt (sum / total), -120.0), dc / total, maxStep, bad);
        proc.releaseResources();
        if (bad != 0 || peak > 1.0) return 1;
    }
    return 0;
}

namespace
{
constexpr double sampleRate = 48000.0;
void set (NeonRiotProcessor& p, const char* id, float value)
{
    auto* parameter = p.apvts.getParameter (id);
    if (parameter == nullptr) { std::fprintf (stderr, "Missing parameter %s\n", id); std::exit (2); }
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}
struct Stats
{
    double peak = 0, power = 0, dc = 0, maxJump = 0, jumpPower = 0;
    uint64_t count = 0, bad = 0;
    float previous = 0;
    void add (float x)
    {
        if (! std::isfinite (x)) { ++bad; return; }
        const double jump = x - previous; previous = x;
        peak = std::max (peak, (double) std::abs (x));
        maxJump = std::max (maxJump, std::abs (jump)); jumpPower += jump * jump;
        power += (double) x * x; dc += x; ++count;
    }
    double rms() const { return std::sqrt (power / std::max (uint64_t (1), count)); }
    bool safe() const { return bad == 0 && peak <= 1.0; }
    void print (const char* label) const
    {
        std::printf ("%s peak %.2f dBFS rms %.2f dBFS dc %+.6f maxjump %.6f nonfinite %llu\n",
            label, juce::Decibels::gainToDecibels (peak, -160.0), juce::Decibels::gainToDecibels (rms(), -160.0),
            dc / std::max (uint64_t (1), count), maxJump, (unsigned long long) bad);
    }
};
void configure (NeonRiotProcessor& p, int type, double sr = sampleRate, int bs = 64)
{
    p.setCurrentProgram (0);
    set (p, PID::driveEngine, 1); set (p, PID::voiceMode, 2);
    set (p, PID::preType, (float) type); set (p, PID::postType, (float) type);
    set (p, PID::preDrive, 1); set (p, PID::postDrive, 1);
    set (p, PID::biteAmt, 1); set (p, PID::osc2Level, 0.7f); set (p, PID::noiseLevel, 0.1f);
    set (p, PID::fltReso, 1.2f); set (p, PID::fltCutoff, 6000);
    set (p, PID::aR, 10); set (p, PID::aS, 1); set (p, PID::anchorAmt, 0.5f);
    p.setPlayConfigDetails (0, 2, sr, bs); p.prepareToPlay (sr, bs);
}
void chord (juce::MidiBuffer& midi, int root = 36)
{
    for (int i = 0; i < 8; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (1, root + i * 3, (juce::uint8) 110), 0);
}
void heavyEffects (NeonRiotProcessor& p, int effect = 7)
{
    set (p, PID::chaosSelect, (float) effect); set (p, PID::chaosAmount, 1);
    set (p, PID::delayMix, 0.75f); set (p, PID::delaySync, 0); set (p, PID::delayTime, 0.24f);
    set (p, PID::delayFeedback, 0.95f); set (p, PID::delayTone, 0.7f);
    set (p, PID::reverbMix, 0.75f); set (p, PID::reverbSize, 1); set (p, PID::reverbDamp, 0);
    set (p, PID::chorusMix, 0.65f); set (p, PID::chorusDepth, 1);
    set (p, PID::shiftMix, 0.4f); set (p, PID::shiftHz, -120); set (p, PID::shiftFeedback, 0.3f);
}

int stress()
{
    int failures = 0;
    // Exercise every CHAOS effect and all five drive types, with all FX active.
    for (int effect = 0; effect < 8; ++effect)
    {
        const int type = effect % 5;
        NeonRiotProcessor p; configure (p, type); heavyEffects (p, effect);
        juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi;
        Stats stats, tail; double lastAudible = 0;
        const int activeBlocks = (int) (sampleRate * 30 / 64);
        const int tailBlocks = (int) std::ceil (sampleRate * p.getTailLengthSeconds() / 64);
        for (int b = 0; b < activeBlocks + tailBlocks; ++b)
        {
            midi.clear();
            if (b < activeBlocks && b % 375 == 0)
            {
                midi.addEvent (juce::MidiMessage::allSoundOff (1), 0);
                chord (midi, 12 + (b / 375 * 7) % 84);
                set (p, PID::preBias, b % 750 == 0 ? -1.0f : 1.0f);
                set (p, PID::postBias, b % 750 == 0 ? 1.0f : -1.0f);
                set (p, PID::driveTilt, b % 750 == 0 ? -1.0f : 1.0f);
                set (p, PID::fltMode, (float) ((b / 375) % 6));
                set (p, PID::output, 12);
                set (p, PID::chaosPunch, b % 750 == 0 ? 1.0f : 0.0f);
                set (p, PID::chaosHold, b % 750 == 0 ? 0.0f : 1.0f);
                set (p, PID::chaosHoldPitch, b % 1500 == 0 ? -24.0f : 24.0f);
                set (p, PID::chaosHoldSize, b % 1500 == 0 ? 0.05f : 1.0f);
            }
            if (b == activeBlocks) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            p.processBlock (audio, midi);
            for (int i = 0; i < 64; ++i)
                for (int c = 0; c < 2; ++c)
                {
                    stats.add (audio.getSample (c, i));
                    if (b >= activeBlocks && std::abs (audio.getSample (c, i)) >= 0.0000316228f)
                        lastAudible = (double) ((b - activeBlocks) * 64 + i) / sampleRate;
                    if (b >= activeBlocks + tailBlocks - 750) tail.add (audio.getSample (c, i));
                }
        }
        stats.print ((Choices::chaos[effect] + " / " + Choices::driveType[type]).toRawUTF8());
        std::printf ("  last >= -90 dBFS: %.3fs after note-off\n", lastAudible);
        std::printf ("  final-second tail %.2f dBFS (reported %.1fs): %s\n",
            juce::Decibels::gainToDecibels (tail.peak, -160.0), p.getTailLengthSeconds(),
            stats.safe() && tail.peak < 0.0000316228 ? "PASS" : "FAIL");
        if (! stats.safe() || tail.peak >= 0.0000316228) ++failures;
    }
    return failures ? 1 : 0;
}
int cpu()
{
    int failures = 0;
    for (int type = 0; type < 5; ++type)
    {
        NeonRiotProcessor p; configure (p, type); heavyEffects (p);
        juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi;
        std::vector<double> durations; durations.reserve (7500);
        chord (midi); p.processBlock (audio, midi); midi.clear();
        for (int b = 0; b < 8000; ++b)
        {
            const auto begin = std::chrono::steady_clock::now();
            p.processBlock (audio, midi);
            const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - begin).count();
            if (b >= 500) durations.push_back (us);
        }
        const double budget = 64.0 / sampleRate * 1.0e6;
        const double average = std::accumulate (durations.begin(), durations.end(), 0.0) / durations.size();
        std::sort (durations.begin(), durations.end());
        std::printf ("%-12s 8 voices 48k/64: mean %.2f%% p99 %.2f%% worst %.2f%% of real time\n",
            Choices::driveType[type].toRawUTF8(), average / budget * 100,
            durations[durations.size() * 99 / 100] / budget * 100, durations.back() / budget * 100);
        if (average > budget * 0.5 || durations[durations.size() * 99 / 100] > budget) ++failures;
    }
    return failures ? 1 : 0;
}
int state()
{
    int failures = 0;
    std::minstd_rand random (20261025);
    for (int trial = 0; trial < 6; ++trial)
    {
        NeonRiotProcessor a, b;
        for (auto* param : a.getParameters())
        {
            const int steps = param->getNumSteps();
            const float value = steps > 1 && steps < 10000
                ? (float) (random() % (unsigned) steps) / (float) (steps - 1)
                : (float) (random() % 10001) / 10000.0f;
            param->setValueNotifyingHost (value);
        }
        set (a, PID::driveEngine, (float) (trial % 2));
        set (a, PID::chaosHold, 0);
        juce::MemoryBlock saved; a.getStateInformation (saved);
        b.setStateInformation (saved.getData(), (int) saved.getSize());
        for (int i = 0; i < a.getParameters().size(); ++i)
            if (std::abs (a.getParameters()[i]->getValue() - b.getParameters()[i]->getValue()) > 1.0e-6f)
            { std::printf ("parameter mismatch %s %.9g %.9g\n", a.getParameters()[i]->getName (100).toRawUTF8(), a.getParameters()[i]->getValue(), b.getParameters()[i]->getValue()); ++failures; }
        for (auto* parameter : a.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                if (a.apvts.getRawParameterValue (rp->getParameterID())->load()
                    != b.apvts.getRawParameterValue (rp->getParameterID())->load())
                { std::printf ("raw mismatch %s\n", rp->getParameterID().toRawUTF8()); ++failures; }
        const double sr = trial < 2 ? 44100.0 : trial < 4 ? 48000.0 : 96000.0;
        a.setPlayConfigDetails (0, 2, sr, 64); b.setPlayConfigDetails (0, 2, sr, 64);
        a.prepareToPlay (sr, 64); b.prepareToPlay (sr, 64);
        juce::AudioBuffer<float> x (2, 64), y (2, 64); juce::MidiBuffer midi;
        double largest = 0;
        for (int block = 0; block < 1500; ++block)
        {
            midi.clear(); if (block == 0) chord (midi);
            if (block == 700) midi.addEvent (juce::MidiMessage::allNotesOff (1), 17);
            if (block == 200 || block == 300 || block == 450 || block == 600)
            {
                const float on = block == 200 || block == 450 ? 1.0f : 0.0f;
                set (a, PID::chaosHold, on); set (b, PID::chaosHold, on);
            }
            a.processBlock (x, midi); b.processBlock (y, midi);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < 64; ++n)
            {
                if (! std::isfinite (x.getSample (c, n)) || ! std::isfinite (y.getSample (c, n))) ++failures;
                largest = std::max (largest, (double) std::abs (x.getSample (c, n) - y.getSample (c, n)));
            }
        }
        if (largest != 0) ++failures;
        std::printf ("state trial %d %.0fHz %s max difference %.9g\n", trial, sr, trial % 2 ? "M2" : "M1", largest);
    }
    // Restore an actual M1-shaped tree into an instance that has already used M2.
    NeonRiotProcessor legacy, upgraded;
    auto tree = legacy.apvts.copyState();
    for (const char* id : { PID::driveEngine, PID::preBias, PID::postBias, PID::driveTilt })
        tree.removeChild (tree.getChildWithProperty ("id", id), nullptr);
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), data);
    for (int i = tree.getNumChildren() - 1; i >= 0; --i)
    {
        const auto id = tree.getChild (i).getProperty ("id").toString();
        if (id.startsWith ("chaos_") || id.startsWith ("delay_") || id.startsWith ("reverb_")
            || id.startsWith ("chorus_") || id.startsWith ("shift_")) tree.removeChild (i, nullptr);
    }
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), data);
    set (upgraded, PID::driveEngine, 1); heavyEffects (upgraded);
    upgraded.setStateInformation (data.getData(), (int) data.getSize());
    if (upgraded.apvts.getRawParameterValue (PID::driveEngine)->load() != 0) ++failures;
    for (const char* id : { PID::chaosSelect, PID::chaosAmount, PID::chaosHold, PID::chaosPunch,
                           PID::delayMix, PID::reverbMix, PID::chorusMix, PID::shiftMix })
        if (upgraded.apvts.getRawParameterValue (id)->load() != 0) ++failures;
    std::printf ("state + M1/M2 migration: %s (MIDI map checked by --midilearn and hosted --state)\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
int clicks()
{
    NeonRiotProcessor p; configure (p, 0);
    set (p, PID::biteAmt, 0); set (p, PID::noiseLevel, 0); set (p, PID::fltReso, 0.25f);
    set (p, PID::chaosAmount, 0.8f); set (p, PID::chaosMix, 0.7f);
    set (p, PID::delayMix, 0.3f); set (p, PID::reverbMix, 0.3f); set (p, PID::chorusMix, 0.3f);
    set (p, PID::fltCutoff, 20000); set (p, PID::anchorAmt, 0); set (p, PID::voiceMode, 0);
    juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi;
    Stats stats; double boundary = 0; int jumpBlock = 0, jumpSample = 0;
    for (int b = 0; b < 7500; ++b)
    {
        midi.clear(); if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 110), 0);
        const bool change = b > 375 && b % 375 == 0;
        if (change)
        {
            set (p, PID::chaosSelect, (float) ((b / 375) % 8));
            set (p, PID::chaosPunch, b % 750 == 0 ? 1.0f : 0.0f);
            set (p, PID::chaosHold, b % 1500 == 0 ? 1.0f : 0.0f);
            set (p, PID::chaosMode, b >= 4500 ? 1.0f : 0.0f);
            set (p, PID::chaosMorph, (float) ((b / 375) % 7) + 0.5f);
            set (p, PID::preType, (float) ((b / 375) % 5));
            set (p, PID::postType, (float) ((b / 375 + 2) % 5));
        }
        const float previous = stats.previous;
        p.processBlock (audio, midi);
        if (change) boundary = std::max (boundary, (double) std::abs (audio.getSample (0, 0) - previous));
        for (int i = 0; i < 64; ++i) if (b > 375)
        {
            if (std::abs (audio.getSample (0, i) - stats.previous) > stats.maxJump) { jumpBlock = b; jumpSample = i; }
            stats.add (audio.getSample (0, i));
        }
    }
    stats.print ("drive/CHAOS switching + morph/PUNCH/HOLD, every beat");
    std::printf ("largest jump at %.6fs (block %d + %d)\n", (jumpBlock * 64 + jumpSample) / sampleRate, jumpBlock, jumpSample);
    const double typical = std::sqrt (stats.jumpPower / (double) stats.count);
    const double ratio = boundary / std::max (1e-9, typical), global = stats.maxJump / std::max (1e-9, typical);
    std::printf ("boundary jump %.6f / typical RMS jump %.6f = %.2fx; global %.2fx; %s (flag >12x)\n",
        boundary, typical, ratio, global, ratio > 12 || global > 12 ? "FLAG" : "PASS");
    return stats.safe() && ratio <= 12 && global <= 12 ? 0 : 1;
}
int saturation (const juce::File& outDir)
{
    outDir.createDirectory();
    int failures = 0;
    for (bool post : { false, true }) for (int type = 0; type < 5; ++type)
    {
        double reference = 0, minDb = 100, maxDb = -100;
        for (float amount : { 0.0f, 0.3f, 0.6f, 1.0f })
        {
            NeonRiotProcessor p; p.setCurrentProgram (0);
            set (p, PID::driveEngine, 1); set (p, PID::drift, 0);
            set (p, PID::preType, (float) type); set (p, PID::postType, (float) type);
            set (p, PID::preDrive, post ? 0 : amount); set (p, PID::postDrive, post ? amount : 0);
            set (p, PID::fltCutoff, 18000); set (p, PID::fltReso, 0); set (p, PID::fltEnv, 0);
            set (p, PID::fltDrive, 0); set (p, PID::subLevel, 0); set (p, PID::aS, 1);
            set (p, PID::driveTone, 1);
            p.setPlayConfigDetails (0, 2, sampleRate, 256); p.prepareToPlay (sampleRate, 256);
            juce::AudioBuffer<float> audio (2, 256), rendered (2, 144000); juce::MidiBuffer midi;
            Stats stats;
            for (int pos = 0; pos < 144000; pos += 256)
            {
                const int n = std::min (256, 144000 - pos);
                audio.setSize (2, n, false, false, true);
                midi.clear(); if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 110), 0);
                p.processBlock (audio, midi);
                for (int c = 0; c < 2; ++c) rendered.copyFrom (c, pos, audio, c, 0, n);
                if (pos >= 48000) for (int i = 0; i < n; ++i) stats.add (audio.getSample (0, i));
            }
            const double db = juce::Decibels::gainToDecibels (stats.rms(), -160.0);
            if (amount == 0) reference = db;
            minDb = std::min (minDb, db); maxDb = std::max (maxDb, db);
            if (! stats.safe() || std::abs (db - reference) > 3.0) ++failures;
            const auto label = juce::String (post ? "Post " : "Pre ") + Choices::driveType[type] + " " + juce::String (amount * 100, 0);
            auto file = outDir.getChildFile (label + ".wav"); file.deleteFile();
            juce::WavAudioFormat wav;
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (file), sampleRate, 2, 24, {}, 0)))
                writer->writeFromAudioSampleBuffer (rendered, 0, rendered.getNumSamples());
            std::printf ("%-20s RMS %.2f dBFS delta %+.2f dB\n", label.toRawUTF8(), db, db - reference);
        }
        std::printf ("  drive RMS span %.2f dB\n", maxDb - minDb);
    }
    return failures ? 1 : 0;
}
int realtime()
{
    std::array<juce::uint8, 512> sysex;
    sysex.fill (0x40); sysex.front() = 0xf0; sysex[1] = 0x7d; sysex.back() = 0xf7;
    callbackAllocations = 0;
    Stats stats;
    for (double sr : { 44100.0, 48000.0, 96000.0 }) for (int bs : { 1, 64, 256 })
    {
        NeonRiotProcessor p; configure (p, 3, sr, bs); heavyEffects (p);
        juce::AudioBuffer<float> audio (2, bs); juce::MidiBuffer midi;
        midi.ensureSize (2048);
        for (int b = 0; b < 1000; ++b)
        {
            midi.clear();
            if (b % 50 == 0)
            {
                midi.addEvent (juce::MidiMessage::allSoundOff (1), 0); chord (midi);
                set (p, PID::preType, (float) (b / 50 % 5));
                set (p, PID::postType, (float) ((b / 50 + 2) % 5));
                set (p, PID::chaosSelect, (float) (b / 50 % 8));
                set (p, PID::chaosHold, b % 100 == 0 ? 1.0f : 0.0f);
                set (p, PID::chaosPunch, b % 100 == 0 ? 0.0f : 1.0f);
                set (p, PID::chaosHoldPitch, b % 100 == 0 ? -24.0f : 24.0f);
                set (p, PID::preBias, (float) (b % 100) / 50 - 0.5f);
                set (p, PID::driveTilt, b % 100 == 0 ? -1.0f : 1.0f);
            }
            if (b % 13 == 0) midi.addEvent (sysex.data(), (int) sysex.size(), bs / 2);
            probeAllocations = true;
            p.processBlock (audio, midi);
            probeAllocations = false;
            for (int c = 0; c < 2; ++c) for (int n = 0; n < bs; ++n) stats.add (audio.getSample (c, n));
        }
        std::printf ("RT %.0fHz/%d latency %d samples\n", sr, bs, p.getLatencySamples());
    }
    stats.print ("RT sweep");
    std::printf ("C++ allocations inside processBlock: %llu %s\n", (unsigned long long) callbackAllocations,
        callbackAllocations == 0 && stats.safe() ? "PASS" : "FAIL");
    return callbackAllocations == 0 && stats.safe() ? 0 : 1;
}

int dsp()
{
    nr::PreDrive2x pre; pre.prepare (48000);
    int peakSample = 0; float peak = 0;
    for (int n = 0; n < 256; ++n)
    {
        const float y = pre.process (0, n == 0 ? 0.001f : 0, 0, 0, 0, false);
        if (std::abs (y) > peak) { peak = std::abs (y); peakSample = n; }
    }
    std::printf ("2x pre impulse peak %d samples, expected %d: %s\n", peakSample,
        nr::PreDrive2x::latency, peakSample == nr::PreDrive2x::latency ? "PASS" : "FAIL");
    pre.prepare (48000);
    nr::DriveVoicing base; base.prepare (48000);
    double br = 0, bi = 0, ar = 0, ai = 0, bfR = 0, bfI = 0, afR = 0, afI = 0;
    for (int n = 0; n < 96000; ++n)
    {
        const float x = 0.5f * std::sin (nr::kTwoPi * 9000.0f * (float) (n % 48000) / 48000.0f);
        const float b = base.process (0, x, 0.3f, 0, 0, false);
        const float a = pre.process (0, x, 0.3f, 0, 0, false);
        if (n < 48000) continue;
        const double phase = 2.0 * juce::MathConstants<double>::pi * 21000 * n / 48000;
        const double fundamental = 2.0 * juce::MathConstants<double>::pi * 9000 * n / 48000;
        br += b * std::cos (phase); bi += b * std::sin (phase);
        ar += a * std::cos (phase); ai += a * std::sin (phase);
        bfR += b * std::cos (fundamental); bfI += b * std::sin (fundamental);
        afR += a * std::cos (fundamental); afI += a * std::sin (fundamental);
    }
    const double baseAlias = std::hypot (br, bi) / std::hypot (bfR, bfI);
    const double aaAlias = std::hypot (ar, ai) / std::hypot (afR, afI);
    const double improvement = 20 * std::log10 (baseAlias / aaAlias);
    std::printf ("9kHz Warm/30%%: 21kHz alias relative to fundamental: base %.2f dB, 2x %.2f dB, reduction %.2f dB: %s\n",
        20 * std::log10 (baseAlias), 20 * std::log10 (aaAlias), improvement, improvement > 10 ? "PASS" : "FAIL");
    return peakSample == nr::PreDrive2x::latency && improvement > 10 ? 0 : 1;
}

int effectsTests (const juce::File& outDir)
{
    int failures = 0;
    // Exercise the lazy snapshot through repeated wraps and rapid recapture without any copy.
    nr::Capture capture; capture.prepare (127);
    for (int n = 0; n < 400; ++n) capture.push ((float) n);
    capture.capture (100);
    for (int n = 400; n < 1000; ++n)
    {
        capture.push ((float) n);
        for (int j = 0; j < 100; ++j) if (capture.frozen (j) != (float) (300 + j)) ++failures;
    }
    capture.capture (127);
    for (int n = 1000; n < 1500; ++n)
    {
        capture.push ((float) n);
        for (int j = 0; j < 127; ++j) if (capture.frozen (j) != (float) (873 + j)) ++failures;
    }
    capture.reset(); if (capture.frozen (0) != 0) ++failures;
    std::printf ("HOLD copy-on-write snapshot, wraps/full-capacity/recapture/reset: %s\n", failures ? "FAIL" : "PASS");
    // Verify both shift directions and unwanted sideband rejection at a well-resolved carrier.
    for (float hz : { -350.0f, 350.0f })
    {
        nr::FrequencyShifter shifter; shifter.prepare (48000);
        double wantedR = 0, wantedI = 0, unwantedR = 0, unwantedI = 0;
        for (int n = 0; n < 96000; ++n)
        {
            const float x = 0.1f * (float) std::cos (2 * juce::MathConstants<double>::pi * 5000 * n / 48000);
            const auto y = shifter.process ({ x, x }, hz, 0);
            if (n < 48000) continue;
            const double w = 2 * juce::MathConstants<double>::pi * (5000 + hz) * n / 48000;
            const double u = 2 * juce::MathConstants<double>::pi * (5000 - hz) * n / 48000;
            wantedR += y.l * std::cos (w); wantedI += y.l * std::sin (w);
            unwantedR += y.l * std::cos (u); unwantedI += y.l * std::sin (u);
        }
        const double reject = 20 * std::log10 (std::hypot (wantedR, wantedI) / std::hypot (unwantedR, unwantedI));
        std::printf ("Hilbert shift %+.0fHz: unwanted sideband %.2f dB below wanted: %s\n", hz, reject, reject >= 30 ? "PASS" : "FAIL");
        if (reject < 30 || ! std::isfinite (reject)) ++failures;
    }
    // Direct FX tails include the slowest division at 20 BPM, bypass/return switching and memory.
    for (int effect = 0; effect < 8; ++effect)
    {
        nr::Effects rack; rack.prepare (48000);
        nr::EffectParameters p;
        p.select = effect; p.amount = 1; p.chorusMix = 1; p.chorusDepth = 1;
        p.shiftMix = 0.5f; p.shiftHz = -500; p.shiftFeedback = 0.3f;
        p.delayMix = 1; p.delayFeedback = 0.95f; p.delayDiv = 0;
        p.reverbMix = 1; p.reverbSize = 1; p.reverbDamp = 0; p.division = 0;
        rack.set (p, 20);
        Stats left, right, tail; double lastAudible = 0;
        for (int n = 0; n < 48000 * 110; ++n)
        {
            if (n == 4800) { p.hold = true; rack.set (p, 20); }
            if (n == 9600) { p.hold = false; rack.set (p, 20); }
            const auto y = rack.process (n == 0 ? 0.5f : 0);
            left.add (y.l); right.add (y.r);
            if (std::abs (y.l) >= 0.0000316228f || std::abs (y.r) >= 0.0000316228f) lastAudible = n / 48000.0;
            if (n >= 109 * 48000) { tail.add (y.l); tail.add (y.r); }
        }
        const bool pass = left.bad == 0 && right.bad == 0 && tail.peak < 0.0000316228;
        std::printf ("%-10s direct impulse + 48s delay: last >= -90dB %.3fs, final tail %.2fdBFS, nonfinite %llu: %s\n",
            Choices::chaos[effect].toRawUTF8(), lastAudible, juce::Decibels::gainToDecibels (tail.peak, -160.0),
            (unsigned long long) (left.bad + right.bad), pass ? "PASS" : "FAIL");
        if (! pass) ++failures;
    }
    // Sub Anchor must preserve a clean fundamental while the upper signal is being destroyed.
    NeonRiotProcessor dry, wet;
    for (auto* p : { &dry, &wet })
    {
        p->setCurrentProgram (0); set (*p, PID::osc1Level, 0); set (*p, PID::subWave, 0);
        set (*p, PID::subLevel, 1); set (*p, PID::anchorAmt, 1); set (*p, PID::anchorFreq, 250);
        set (*p, PID::drift, 0); set (*p, PID::fltEnv, 0); set (*p, PID::aS, 1); set (*p, PID::output, -12);
        p->setPlayConfigDetails (0, 2, 48000, 64); p->prepareToPlay (48000, 64);
    }
    heavyEffects (wet, 7);
    juce::AudioBuffer<float> x (2, 64), y (2, 64); juce::MidiBuffer midi;
    double referenceR = 0, referenceI = 0, wetR = 0, wetI = 0, sideR = 0, sideI = 0;
    for (int block = 0; block < 3000; ++block)
    {
        midi.clear(); if (block == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110), 0);
        dry.processBlock (x, midi); wet.processBlock (y, midi);
        if (block < 1500) continue;
        for (int i = 0; i < 64; ++i)
        {
            const double phase = 2 * juce::MathConstants<double>::pi * 32.7031956626 * (block * 64 + i) / 48000;
            const double c = std::cos (phase), s = std::sin (phase);
            const double mid = 0.5 * (y.getSample (0, i) + y.getSample (1, i));
            const double side = 0.5 * (y.getSample (0, i) - y.getSample (1, i));
            referenceR += x.getSample (0, i) * c; referenceI += x.getSample (0, i) * s;
            wetR += mid * c; wetI += mid * s; sideR += side * c; sideI += side * s;
        }
    }
    const double delta = 20 * std::log10 (std::hypot (wetR, wetI) / std::hypot (referenceR, referenceI));
    const double sideDb = 20 * std::log10 (std::max (1e-15, std::hypot (sideR, sideI)) / std::hypot (wetR, wetI));
    const bool anchored = std::abs (delta) < 0.5 && sideDb < -40;
    std::printf ("Sub Anchor 32.7Hz with all FX: fundamental delta %+.5fdB; side/mid %.2fdB: %s\n", delta, sideDb, anchored ? "PASS" : "FAIL");
    if (! anchored) ++failures;
    // Listening samples: all CHAOS positions at 20% and 100%, including full stereo.
    outDir.createDirectory();
    for (int effect = 1; effect < 8; ++effect) for (float intensity : { 0.2f, 1.0f })
    {
        NeonRiotProcessor p; p.setCurrentProgram (0); set (p, PID::driveEngine, 1);
        set (p, PID::chaosSelect, (float) effect); set (p, PID::chaosAmount, intensity);
        set (p, PID::fltCutoff, 3200); set (p, PID::anchorAmt, 0.6f); set (p, PID::output, -6);
        p.setPlayConfigDetails (0, 2, 48000, 256); p.prepareToPlay (48000, 256);
        juce::AudioBuffer<float> audio (2, 256), out (2, 48000 * 6); Stats stats;
        for (int pos = 0; pos < out.getNumSamples(); pos += 256)
        {
            const int count = std::min (256, out.getNumSamples() - pos); audio.setSize (2, count, false, false, true);
            midi.clear();
            if (pos % 24064 == 0 && pos < 48000 * 4)
            {
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, 36 + (pos / 24064 % 4) * 3, (juce::uint8) 110), 0);
            }
            if (pos >= 48000 * 4 && pos < 48000 * 4 + 256) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            p.processBlock (audio, midi);
            for (int c = 0; c < 2; ++c)
            {
                out.copyFrom (c, pos, audio, c, 0, count);
                for (int i = 0; i < count; ++i) stats.add (audio.getSample (c, i));
            }
        }
        const auto label = Choices::chaos[effect] + " " + juce::String (intensity * 100, 0);
        stats.print (label.toRawUTF8()); if (! stats.safe()) ++failures;
        auto file = outDir.getChildFile (label + ".wav"); file.deleteFile(); juce::WavAudioFormat wav;
        if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (file), 48000, 2, 24, {}, 0)))
            writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    }
    return failures ? 1 : 0;
}

int routingTests()
{
    int failures = 0;
    nr::Chaos lower, upper, morph;
    lower.prepare (48000); upper.prepare (48000); morph.prepare (48000);
    nr::EffectParameters p; p.amount = 0.65f; p.select = 2;
    lower.set (p, 120); p.select = 3; upper.set (p, 120);
    p.mode = 1; p.morph = 2.5f; morph.set (p, 120);
    double maximum = 0;
    for (int n = 0; n < 48000; ++n)
    {
        const float input = 0.2f * std::sin (nr::kTwoPi * 220 * (float) n / 48000);
        const auto l = lower.process (input), u = upper.process (input), m = morph.process (input);
        if (n > 12000)
        {
            const float excessDry = (1 - p.amount) * (std::sqrt (2.0f) - 1);
            const auto expected = (l + u) * std::sqrt (0.5f) + nr::Stereo { input, input } * (-excessDry);
            maximum = std::max ({ maximum, (double) std::abs (m.l - expected.l), (double) std::abs (m.r - expected.r) });
        }
    }
    const bool equalPower = maximum < 0.0001;
    std::printf ("Morph halfway CRUSH/TEAR equal-power error %.9g: %s\n", maximum, equalPower ? "PASS" : "FAIL");
    if (! equalPower) ++failures;
    p.amount = 0; morph.prepare (48000); morph.set (p, 120);
    bool neutral = true;
    for (int n = 0; n < 2000; ++n)
    {
        const float x = 0.1f * std::sin (nr::kTwoPi * 110 * (float) n / 48000);
        const auto y = morph.process (x);
        if (y.l != x || y.r != x) neutral = false;
    }
    std::printf ("Morph zero intensity exact dry identity: %s\n", neutral ? "PASS" : "FAIL");
    if (! neutral) ++failures;
    // A synced HOLD must retain its division period after accounting for seam overlap.
    nr::Chaos heldLoop; heldLoop.prepare (48000);
    nr::EffectParameters heldParameters; heldParameters.division = 8; heldLoop.set (heldParameters, 120);
    for (int n = 0; n < 48000; ++n)
        heldLoop.process (0.1f * std::sin (nr::kTwoPi * 120 * (float) n / 48000));
    heldParameters.hold = true; heldLoop.set (heldParameters, 120);
    std::vector<float> heldAudio (144000);
    for (auto& sample : heldAudio) sample = heldLoop.process (0).l;
    double aa = 0, bb = 0, ab = 0;
    for (int n = 48000; n < 96000; ++n)
    {
        const double a = heldAudio[(size_t) n], b = heldAudio[(size_t) (n + 12000)];
        aa += a * a; bb += b * b; ab += a * b;
    }
    const double correlation = ab / std::sqrt (aa * bb);
    std::printf ("HOLD 1/8 at 120BPM (12000-sample period): correlation %.9f: %s\n", correlation,
        correlation > 0.9999 ? "PASS" : "FAIL");
    if (! std::isfinite (correlation) || correlation <= 0.9999) ++failures;
    NeonRiotProcessor mono; mono.setCurrentProgram (0); heavyEffects (mono);
    mono.setPlayConfigDetails (0, 1, 48000, 64); mono.prepareToPlay (48000, 64);
    juce::AudioBuffer<float> audio (1, 64); juce::MidiBuffer midi; Stats stats;
    for (int n = 0; n < 2000; ++n)
    {
        midi.clear(); if (n == 0) chord (midi);
        if (n == 100) { set (mono, PID::chaosHold, 1); set (mono, PID::chaosHoldPitch, 24); }
        if (n == 400) set (mono, PID::chaosHold, 0);
        mono.processBlock (audio, midi);
        for (int i = 0; i < 64; ++i) stats.add (audio.getSample (0, i));
    }
    stats.print ("mono host layout + full FX"); if (! stats.safe()) ++failures;
    // Changes in a delay's read position crossfade instead of dragging across old samples.
    nr::StereoDelay delay; delay.prepare (48000);
    float previous = 0; double maxJump = 0, power = 0;
    for (int n = 0; n < 48000 * 3; ++n)
    {
        const float seconds = n < 48000 ? 0.023f : n < 96000 ? 0.391f : 0.137f;
        const float x = 0.1f * std::sin (nr::kTwoPi * 220 * (float) n / 48000);
        const auto y = delay.process ({ x, x }, seconds, 0.95f, true, 0.5f);
        const double jump = y.l - previous; previous = y.l;
        power += jump * jump; maxJump = std::max (maxJump, std::abs (jump));
    }
    const double ratio = maxJump / std::sqrt (power / (48000 * 3));
    std::printf ("delay read-head retiming: jump/typical %.2fx: %s\n", ratio, ratio < 12 ? "PASS" : "FAIL");
    if (ratio >= 12 || ! std::isfinite (ratio)) ++failures;
    return failures ? 1 : 0;
}

}

// Original positional output-directory usage remains supported.
//== Phase C tests ==========================================================================
namespace
{
std::vector<float> renderRiff (NeonRiotProcessor& p, int seconds = 4, int bs = 256, double sr = sampleRate)
{
    p.setPlayConfigDetails (0, 2, sr, bs); p.prepareToPlay (sr, bs);
    const int total = (int) (sr * seconds);
    const int notes[] { 36, 36, 48, 39, 36, 43, 46, 34 };
    juce::MidiBuffer all;
    for (int k = 0; k < 8; ++k)
    {
        const int on = (int) (k * 0.25 * sr);
        all.addEvent (juce::MidiMessage::noteOn (1, notes[k], (juce::uint8) (k % 4 == 0 ? 120 : 70)), on);
        all.addEvent (juce::MidiMessage::noteOff (1, notes[k]), on + (int) (0.18 * sr));
    }
    all.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), (int) (2.0 * sr));
    all.addEvent (juce::MidiMessage::noteOff (1, 36), (int) (3.2 * sr));
    std::vector<float> out; out.reserve ((size_t) total * 2);
    juce::AudioBuffer<float> block (2, bs);
    for (int pos = 0; pos < total; pos += bs)
    {
        juce::MidiBuffer mb;
        for (const auto m : all)
            if (m.samplePosition >= pos && m.samplePosition < pos + bs) mb.addEvent (m.getMessage(), m.samplePosition - pos);
        p.processBlock (block, mb);
        for (int n = 0; n < bs; ++n) { out.push_back (block.getSample (0, n)); out.push_back (block.getSample (1, n)); }
    }
    return out;
}
Stats statsOf (const std::vector<float>& v) { Stats s; for (float x : v) s.add (x); return s; }

int macrosTest()
{
    const char* names[] { "WEIGHT", "BITE", "ACID", "DRIVE", "CHAOS", "MOTION", "SPACE", "OUTPUT" };
    bool ok = true;
    // 1. Neutral macros reproduce every preset exactly; the macro path must not perturb them.
    const int numPresets = NeonRiotProcessor().getNumPrograms();
    for (int prog = 0; prog < numPresets; ++prog)
    {
        NeonRiotProcessor a, b;
        a.setCurrentProgram (prog); b.setCurrentProgram (prog);
        for (int k = 0; k < 8; ++k) set (b, kMacroIds[k], 0.0f);
        if (renderRiff (a, 2) != renderRiff (b, 2)) { std::printf ("neutral mismatch preset %d FAIL\n", prog); ok = false; }
    }
    std::printf ("Neutral macros, %d presets bit-identical: %s\n", numPresets, ok ? "PASS" : "FAIL");

    // 2. Each macro's range on four contrasting presets: RMS per position, safety.
    const int progs[] { 0, 2, 3, 12 };
    for (int k = 0; k < 8; ++k)
    {
        std::printf ("%-7s", names[k]);
        for (int prog : progs)
        {
            std::printf ("  p%02d:", prog);
            for (float m : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
            {
                NeonRiotProcessor p; p.setCurrentProgram (prog); set (p, kMacroIds[k], m);
                const auto st = statsOf (renderRiff (p, 4));
                ok = ok && st.safe();
                std::printf (" %6.1f", juce::Decibels::gainToDecibels (st.rms(), -160.0));
            }
        }
        std::printf ("\n");
    }

    // 3. Parameters are never written: raw values stay put while effective values move.
    {
        NeonRiotProcessor p; p.setCurrentProgram (2);
        const int cut = p.parameterIndex (PID::fltCutoff), reso = p.parameterIndex (PID::fltReso);
        const float rawCut = p.apvts.getRawParameterValue (PID::fltCutoff)->load();
        set (p, PID::macroAcid, 1.0f);
        renderRiff (p, 1);
        const bool untouched = p.apvts.getRawParameterValue (PID::fltCutoff)->load() == rawCut;
        const bool moved = p.effectiveValue (cut) < rawCut && p.effectiveValue (reso) > p.apvts.getRawParameterValue (PID::fltReso)->load();
        std::printf ("ACID +100%%: cutoff raw %.0f Hz, effective %.0f Hz; reso raw %.2f effective %.2f: %s\n",
            rawCut, p.effectiveValue (cut), p.apvts.getRawParameterValue (PID::fltReso)->load(), p.effectiveValue (reso),
            untouched && moved ? "PASS" : "FAIL");
        ok = ok && untouched && moved;
    }

    // 4. All macros sweeping hard, every block, under the allocation probe.
    {
        callbackAllocations = 0; Stats st;
        NeonRiotProcessor p; configure (p, 3); heavyEffects (p);
        juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi; midi.ensureSize (2048);
        for (int b = 0; b < 3000; ++b)
        {
            midi.clear();
            if (b % 100 == 0) { midi.addEvent (juce::MidiMessage::allSoundOff (1), 0); chord (midi); }
            for (int k = 0; k < 8; ++k)
                set (p, kMacroIds[k], (float) std::sin (b * 0.013 * (k + 1)));
            probeAllocations = true; p.processBlock (audio, midi); probeAllocations = false;
            for (int c = 0; c < 2; ++c) for (int n = 0; n < 64; ++n) st.add (audio.getSample (c, n));
        }
        st.print ("Macro sweep stress");
        const bool pass = callbackAllocations == 0 && st.safe();
        std::printf ("Macro sweep: allocations %llu, %s\n", (unsigned long long) callbackAllocations, pass ? "PASS" : "FAIL");
        ok = ok && pass;
    }
    std::printf ("MACROS %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int midiLearnTest()
{
    bool ok = true;
    auto check = [&] (bool c, const char* what) { std::printf ("%-58s %s\n", what, c ? "PASS" : "FAIL"); ok = ok && c; };
    NeonRiotProcessor p; p.setCurrentProgram (0);
    p.setPlayConfigDetails (0, 2, sampleRate, 64); p.prepareToPlay (sampleRate, 64);
    juce::AudioBuffer<float> audio (2, 64);
    auto send = [&] (int cc, int value)
    {
        juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, cc, value), 10);
        callbackAllocations = 0; probeAllocations = true; p.processBlock (audio, m); probeAllocations = false;
        return callbackAllocations;
    };
    const int bite = p.parameterIndex (PID::macroBite), cutoff = p.parameterIndex (PID::fltCutoff);
    p.midiLearn().startLearning (bite);
    const auto allocs = send (21, 127);
    check (p.midiLearn().ccFor (bite) == 21, "Learn binds the next CC (21) to Macro BITE");
    check (allocs == 0, "No allocation while binding and applying a CC");
    check (std::abs (p.apvts.getRawParameterValue (PID::macroBite)->load() - 1.0f) < 1e-6f, "Bound CC drives the engine-side value immediately");
    p.midiLearn().dispatch();
    check (std::abs (p.apvts.getParameter (PID::macroBite)->getValue() - 1.0f) < 1e-6f, "Message-thread dispatch notifies the host parameter");
    send (21, 0); p.midiLearn().dispatch();
    check (std::abs (p.apvts.getRawParameterValue (PID::macroBite)->load() + 1.0f) < 1e-6f, "CC 0 reaches the bottom of a bipolar range");
    p.midiLearn().startLearning (cutoff); send (21, 64);
    check (p.midiLearn().ccFor (cutoff) == 21 && p.midiLearn().ccFor (bite) == -1, "Re-learning a CC moves it to the new parameter");
    p.midiLearn().startLearning (bite); send (22, 10);
    check (p.midiLearn().ccFor (bite) == 22, "Second mapping on another CC");
    p.midiLearn().startLearning (bite); send (123, 0);
    check (p.midiLearn().ccFor (bite) == 22 && p.midiLearn().learningIndex() == bite, "Channel-mode CCs (120+) are never learned");
    p.midiLearn().cancelLearning();

    juce::MemoryBlock blob; p.getStateInformation (blob);
    NeonRiotProcessor q; q.setStateInformation (blob.getData(), (int) blob.getSize());
    check (q.midiLearn().ccFor (cutoff) == 21 && q.midiLearn().ccFor (bite) == 22, "Mappings survive save and project reload");
    juce::MemoryBlock blob2; q.getStateInformation (blob2);
    check (blob == blob2, "Saved state is stable across a save/load/save cycle");
    q.midiLearn().forget (cutoff);
    check (q.midiLearn().ccFor (cutoff) == -1 && q.midiLearn().ccFor (bite) == 22, "Forget removes only that mapping");

    // Old (Phase B) state with no MIDI_LEARN child: no mappings, neutral macros.
    auto tree = juce::ValueTree::fromXml (*juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize()));
    tree.removeChild (tree.getChildWithName ("MIDI_LEARN"), nullptr);
    for (const char* id : kMacroIds) tree.removeChild (tree.getChildWithProperty ("id", id), nullptr);
    juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
    NeonRiotProcessor r; r.setStateInformation (old.getData(), (int) old.getSize());
    bool neutral = true;
    for (const char* id : kMacroIds) neutral = neutral && r.apvts.getRawParameterValue (id)->load() == 0.0f;
    check (r.midiLearn().ccFor (bite) == -1 && neutral, "Pre-macro projects load with no mappings and neutral macros");
    std::printf ("MIDI LEARN %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

std::vector<float> renderNoteAt (NeonRiotProcessor& p, int startSample, int length, int note = 36)
{
    p.setPlayConfigDetails (0, 2, sampleRate, 64); p.prepareToPlay (sampleRate, 64);
    juce::AudioBuffer<float> audio (2, 64);
    std::vector<float> out;
    for (int pos = 0; pos < startSample + length; pos += 64)
    {
        juce::MidiBuffer m;
        if (startSample >= pos && startSample < pos + 64) m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), startSample - pos);
        p.processBlock (audio, m);
        for (int n = 0; n < 64; ++n) if (pos + n >= startSample) out.push_back (audio.getSample (0, n));
    }
    out.resize ((size_t) length);
    return out;
}

int lfoTest()
{
    bool ok = true;
    // Retrigger: a note's modulation is the same whenever it starts; free-running it isn't.
    auto startDiff = [] (float amt, bool retrig, int offset)
    {
        std::vector<float> a, b;
        for (int start : { 0, offset })
        {
            NeonRiotProcessor p; p.setCurrentProgram (0);
            set (p, PID::drift, 0); set (p, PID::l1Wave, 4); set (p, PID::l1Rate, 3.0f);
            set (p, PID::l1Dest, 5); set (p, PID::l1Amt, amt); set (p, PID::l1Retrig, retrig ? 1.0f : 0.0f);
            (start == 0 ? a : b) = renderNoteAt (p, start, 48000);
        }
        double diff = 0; for (size_t k = 0; k < a.size(); ++k) diff = std::max (diff, (double) std::abs (a[k] - b[k]));
        return diff;
    };
    // Control: no LFO at all. Whatever differs here is not the LFO's doing.
    const double control = startDiff (0.0f, false, 3333), aligned = startDiff (0.0f, false, 3328);
    std::printf ("Control (LFO off): note at 0 vs 3333 differs by %.2e; vs 3328 (block-aligned) by %.2e\n", control, aligned);
    for (bool retrig : { false, true })
    {
        const double diff = startDiff (0.8f, retrig, 3333);
        const bool pass = retrig ? diff <= control + 1e-6 : diff > 1e-2;
        std::printf ("LFO retrigger %s: max difference between note starts %.2e %s\n", retrig ? "on " : "off", diff, pass ? "PASS" : "FAIL");
        ok = ok && pass;
    }
    // Fade in: tremolo absent just after the attack, full later.
    {
        NeonRiotProcessor p; p.setCurrentProgram (0);
        set (p, PID::drift, 0); set (p, PID::aS, 1); set (p, PID::aD, 0.05f); set (p, PID::fltEnv, 0);
        set (p, PID::l1Wave, 0); set (p, PID::l1Rate, 6.0f); set (p, PID::l1Dest, 5); set (p, PID::l1Amt, 1.0f);
        set (p, PID::l1Fade, 1.0f);
        const auto v = renderNoteAt (p, 0, 48000 * 3, 60);     // C4: 15 ms windows hold ~4 cycles
        auto spread = [&] (double t0, double t1)
        {
            double lo = 1e9, hi = 0;
            for (int w = (int) (t0 * 48000); w + 734 <= (int) (t1 * 48000); w += 367)
            {
                double e = 0; for (int k = w; k < w + 734; ++k) e += v[(size_t) k] * v[(size_t) k];
                e = std::sqrt (e / 734); lo = std::min (lo, e); hi = std::max (hi, e);
            }
            return juce::Decibels::gainToDecibels (hi / std::max (lo, 1e-9));
        };
        const double early = spread (0.12, 0.25), late = spread (1.5, 2.5);
        const bool pass = early < 1.0 && late > 4.0;
        std::printf ("LFO fade 1 s: level swing %.2f dB just after attack, %.2f dB after 1.5 s %s\n", early, late, pass ? "PASS" : "FAIL");
        ok = ok && pass;
    }
    std::printf ("LFO ARTICULATION %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
} // namespace

//== Interface: offscreen snapshots and repaint cost =========================================
namespace
{
int uiTest (const juce::File& outDir)
{
    outDir.createDirectory();
    NeonRiotProcessor p;
    p.setCurrentProgram (1);
    set (p, PID::macroBite, 0.35f); set (p, PID::macroSpace, -0.3f);
    set (p, PID::chaosSelect, 3.0f); set (p, PID::chaosAmount, 0.45f);
    p.setPlayConfigDetails (0, 2, sampleRate, 256); p.prepareToPlay (sampleRate, 256);
    juce::AudioBuffer<float> audio (2, 256);
    auto play = [&] (int blocks, bool noteOn)
    {
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer m;
            if (b == 0 && noteOn) m.addEvent (juce::MidiMessage::noteOn (1, 33, (juce::uint8) 110), 0);
            p.processBlock (audio, m);
        }
    };
    play (40, true);

    std::unique_ptr<NeonRiotEditor> ed (dynamic_cast<NeonRiotEditor*> (p.createEditor()));
    ed->setSize (1280, 800);
    for (int k = 0; k < 5; ++k) { play (3, false); ed->tickForTest(); }   // meters, phosphor, effective rings

    auto save = [&] (const juce::String& name, float scale)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
        auto f = outDir.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    };
    save ("ui-main.png", 1.0f);
    save ("ui-main@2x.png", 2.0f);
    const char* pages[] { "", "ui-mod.png", "ui-fx.png", "ui-voice.png", "ui-xy.png" };
    for (int k = 1; k <= 4; ++k) { ed->getPanel().openPage (k); ed->tickForTest(); save (pages[k], 1.0f); save (juce::String (pages[k]).replace (".png", "@2x.png"), 2.0f); }
    ed->getPanel().openPage (0);

    // Repaint cost (software renderer, so an upper bound for CoreGraphics on this Mac).
    auto timeArea = [&] (juce::Rectangle<int> area, int runs)
    {
        ed->createComponentSnapshot (area, true, 2.0f);                    // warm caches
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < runs; ++k) { play (1, false); ed->tickForTest(); ed->createComponentSnapshot (area, true, 2.0f); }
        return (juce::Time::getMillisecondCounterHiRes() - t0) / runs;
    };
    const double full = timeArea (ed->getLocalBounds(), 20);
    std::printf ("UI full-window repaint at 2x (resize, preset change): %.2f ms\n", full);
    // Per frame while playing, only these repaint: scope and meters always, the CHAOS dial
    // when its glow step or state changes, the flow strip when a stage's activity moves.
    struct Area { const char* name; juce::Rectangle<int> r; };
    double perFrame = 0;
    for (const Area& a : { Area { "scope", { 424, 92, 352, 106 } }, Area { "meters", { 784, 92, 72, 106 } },
                           Area { "flow strip", { 418, 204, 444, 30 } }, Area { "CHAOS dial (cached blit)", { 418, 272, 444, 258 } },
                           Area { "empty 8x8 (snapshot overhead)", { 600, 30, 8, 8 } } })
    {
        const double ms = timeArea (a.r, 60);
        std::printf ("  %-30s %.3f ms\n", a.name, ms);
        if (a.name[0] == 's' || a.name[0] == 'm') perFrame += ms;
    }
    std::printf ("UI steady playing cost (scope + meters at 30 fps, 2x): %.1f%% of one core\n", perFrame * 30.0 / 10.0);
    const double tickOnly = [&] { const auto t0 = juce::Time::getMillisecondCounterHiRes();
                                   for (int k = 0; k < 200; ++k) ed->tickForTest();
                                   return (juce::Time::getMillisecondCounterHiRes() - t0) / 200; }();
    std::printf ("UI timer tick (no painting): %.3f ms\n", tickOnly);
    return 0;
}

int textTest()
{
    NeonRiotProcessor processor;
    int failures = 0, floats = 0, roundTrips = 0, invalidChecks = 0, clamps = 0;
    auto check = [&] (bool ok, const juce::String& message)
    {
        if (! ok) { ++failures; std::printf ("FAIL %s\n", message.toRawUTF8()); }
    };
    for (auto* parameter : processor.getParameters())
    {
        auto* f = dynamic_cast<juce::AudioParameterFloat*> (parameter);
        if (f == nullptr) continue;
        ++floats;
        for (float norm : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const auto text = parameter->getText (norm, 256);
            const float raw = f->convertFrom0to1 (norm);
            const float parsed = f->convertFrom0to1 (parameter->getValueForText (text));
            // One last displayed digit, expressed in stored units. Names near the
            // wave endpoints deliberately cover a 0.05-wide neighbourhood.
            float step = 0.0000001f;
            if (text.contains (">")) step = 0.01f;
            else if (text == "Triangle" || text == "Saw" || text == "Pulse") step = 0.05f;
            else if (text.startsWith ("FEEDBACK")) step = 0.002f;
            else if (text.endsWith ("%")) step = 0.01f;
            else if (text.endsWith ("ms")) step = 0.0001f;
            else if (text.endsWith (" s")) step = 0.01f;
            else if (text.endsWith ("kHz")) step = 10.0f;
            else if (text.endsWith ("Hz")) step = raw < 10.0f ? 0.01f : 1.0f;
            else if (text.endsWith ("dB") || text.endsWith ("ct") || text.endsWith ("st")) step = 0.1f;
            const float epsilon = 8.0f * std::numeric_limits<float>::epsilon() * std::max (1.0f, std::abs (raw));
            check (std::abs (parsed - raw) <= step + epsilon,
                   f->paramID + " round trip " + text + " -> " + juce::String (parsed, 7));
            ++roundTrips;
        }
        for (float norm : { 0.0f, 0.371f, 1.0f })
        {
            parameter->setValueNotifyingHost (norm);
            const float before = parameter->getValue();
            for (const char* bad : { "", "garbage", "45oops", "1..2", "nan", "inf", "--3", "3 ms trailing", "45 %\ntrailing" })
            {
                check (parameter->getValueForText (bad) == before, f->paramID + " invalid entry: " + bad);
                ++invalidChecks;
            }
        }
        for (const auto& entry : { std::pair<const char*, float> { "-1000000000", 0.0f }, { "1000000000", 1.0f } })
        {
            check (parameter->getValueForText (entry.first) == entry.second, f->paramID + " range clamp");
            ++clamps;
        }
    }
    struct Example { const char* id; const char* text; float expected; };
    const Example examples[] {
        { PID::osc1Shape, "45", .45f }, { PID::osc1Shape, "45%", .45f }, { PID::osc1Shape, "45 %", .45f },
        { PID::noiseColour, "-30", -.30f }, { PID::noiseColour, "+30 %", .30f },
        { PID::glide, "300", .3f }, { PID::glide, "300 ms", .3f }, { PID::glide, "300ms", .3f },
        { PID::glide, "1.5s", 1.5f }, { PID::glide, "1.5 s", 1.5f },
        { PID::fltCutoff, "330", 330.0f }, { PID::fltCutoff, "330 Hz", 330.0f },
        { PID::fltCutoff, "2.5k", 2500.0f }, { PID::fltCutoff, "2.5 kHz", 2500.0f },
        { PID::fltCutoff, " 2.5 KHZ ", 2500.0f }, { PID::shiftHz, "-330 Hz", -330.0f },
        { PID::output, "-6", -6.0f }, { PID::output, "-6 dB", -6.0f },
        { PID::osc1Fine, "+9", 9.0f }, { PID::osc1Fine, "9 ct", 9.0f },
        { PID::chaosHoldPitch, "-3.5 st", -3.5f },
        { PID::osc1Wave, "Triangle", 0.0f }, { PID::osc1Wave, "Saw", 1.0f },
        { PID::osc1Wave, "Pulse", 2.0f }, { PID::osc1Wave, "0.5", .5f },
        { PID::osc1Wave, "Tri>Saw 35%", .35f }, { PID::osc1Wave, "Saw>Pulse 65%", 1.65f },
        { PID::fltReso, "FEEDBACK +50 %", 1.1f }, { PID::fltReso, "110", 1.1f },
        { PID::chaosMorph, "3.5", 3.5f }, { PID::fltCutoff, "2.5e3 Hz", 2500.0f }
    };
    std::printf ("Typed examples (stored units):\n");
    for (const auto& e : examples)
    {
        auto* p = processor.apvts.getParameter (e.id);
        const float actual = p->convertFrom0to1 (p->getValueForText (e.text));
        check (std::abs (actual - e.expected) <= 0.00001f * std::max (1.0f, std::abs (e.expected)),
               juce::String (e.id) + " typed " + e.text);
        std::printf ("  %-18s %-20s -> %.7g (expected %.7g)\n", e.id, e.text, actual, e.expected);
    }
    std::printf ("TEXT: %d floats, %d round trips, %d invalid checks, %d clamps, %zu examples; %d failures\n",
                 floats, roundTrips, invalidChecks, clamps, std::size (examples), failures);
    return failures == 0 ? 0 : 1;
}

int gesturesTest()
{
    struct Recorder : juce::AudioProcessorParameter::Listener {
        std::vector<std::pair<int,char>> events;
        void parameterValueChanged(int i,float) override {events.emplace_back(i,'V');}
        void parameterGestureChanged(int i,bool start) override {events.emplace_back(i,start?'B':'E');}
    } record;
    NeonRiotProcessor p;int failures=0;
    for(auto* v:p.getParameters())v->addListener(&record);
    auto event=[](juce::Component& c,juce::Point<float> pos){return juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(),pos,juce::ModifierKeys::leftButtonModifier,
        1,0,0,0,0,&c,&c,juce::Time::getCurrentTime(),pos,juce::Time::getCurrentTime(),1,false);};
    auto verify=[&](const char* name,std::initializer_list<const char*> ids,auto action,bool gestures=true){
        record.events.clear();action();bool ok=true;int values=0,begins=0,ends=0;
        for(const auto* id:ids){int depth=0,count=0,b=0,e=0;const int index=p.parameterIndex(id);
            for(auto [i,type]:record.events)if(i==index){
                if(type=='B'){ok=ok&&depth==0;++depth;++b;}if(type=='E'){ok=ok&&depth==1;--depth;++e;}
                if(type=='V'){ok=ok&&(!gestures||depth==1);++count;}}
            ok=ok&&depth==0&&count>0&&(!gestures||(b>0&&b==e));values+=count;begins+=b;ends+=e;}
        std::printf("%-32s begin %d value %d end %d %s\n",name,begins,values,ends,ok?"PASS":"FAIL");if(!ok)++failures;
    };
    ui::Knob knob(p,PID::fltCutoff,"CUTOFF");knob.setSize(80,100);
    verify("Knob drag",{PID::fltCutoff},[&]{knob.mouseDown(event(knob,{40,60}));knob.mouseDrag(event(knob,{40,20}));knob.mouseUp(event(knob,{40,20}));});
    ui::Segments segments(p,PID::voiceMode,"MODE");segments.setSize(180,40);
    verify("Segments click",{PID::voiceMode},[&]{segments.mouseDown(event(segments,{150,30}));});
    ui::ListSelector list(p,PID::pbRange,"BEND");list.setSize(120,40);
    verify("ListSelector step",{PID::pbRange},[&]{list.mouseDown(event(list,{115,25}));});
    ui::Switch toggle(p,PID::osc2Sync,"SYNC");toggle.setSize(80,24);
    verify("Switch",{PID::osc2Sync},[&]{toggle.mouseDown(event(toggle,{20,12}));});
    ui::PerformButton punch(p,PID::chaosPunch,"PUNCH",true);punch.setSize(100,80);
    verify("PerformButton PUNCH",{PID::chaosPunch},[&]{punch.mouseDown(event(punch,{40,40}));punch.mouseUp(event(punch,{40,40}));});
    ui::XYPad xy(p);xy.setSize(400,400);
    verify("XYPad default axes",{PID::macroBite,PID::macroSpace},[&]{xy.mouseDown(event(xy,{180,180}));xy.mouseDrag(event(xy,{300,100}));xy.mouseUp(event(xy,{300,100}));});
    ui::ChaosDial dial(p);dial.setSize(444,258);
    verify("ChaosDial stepped knob",{PID::chaosSelect},[&]{dial.mouseDown(event(dial,{222,133}));dial.mouseDrag(event(dial,{222,73}));dial.mouseUp(event(dial,{222,73}));});
    verify("ChaosDial ring",{PID::chaosAmount},[&]{dial.mouseDown(event(dial,{325,133}));dial.mouseDrag(event(dial,{325,83}));dial.mouseUp(event(dial,{325,83}));});
    verify("ChaosDial name click",{PID::chaosSelect},[&]{dial.mouseDown(event(dial,{180,12}));});
    set(p,PID::chaosMode,1);
    verify("ChaosDial morph knob",{PID::chaosMorph},[&]{dial.mouseDown(event(dial,{222,133}));dial.mouseDrag(event(dial,{222,93}));dial.mouseUp(event(dial,{222,93}));});
    // Same-axis mapping is legal via setAxes; it must never double-begin a parameter gesture.
    xy.setAxes(1,1);
    verify("XYPad same axis",{PID::macroBite},[&]{xy.mouseDown(event(xy,{180,180}));xy.mouseDrag(event(xy,{300,100}));xy.mouseUp(event(xy,{300,100}));});

    xy.setAxes(1,6);
    verify("XY rebind during drag",{PID::macroBite,PID::macroSpace},[&]{
        xy.mouseDown(event(xy,{180,180}));xy.mouseDrag(event(xy,{250,130}));xy.setAxes(2,4);xy.mouseUp(event(xy,{250,130}));});
    verify("PUNCH close while held",{PID::chaosPunch},[&]{
        auto held=std::make_unique<ui::PerformButton>(p,PID::chaosPunch,"PUNCH",true);held->setSize(100,80);
        held->mouseDown(event(*held,{40,40}));held.reset();});
    p.setPlayConfigDetails(0,2,48000,64);p.prepareToPlay(48000,64);
    const char* learned[]={PID::fltCutoff,PID::chaosSelect,PID::chaosPunch};
    for(int k=0;k<3;++k){auto* v=p.apvts.getParameter(learned[k]);v->setValueNotifyingHost(0);
        p.midiLearn().startLearning(p.parameterIndex(learned[k]));
        verify(("MIDI host notification "+juce::String(k)).toRawUTF8(),{learned[k]},[&]{
            juce::AudioBuffer<float>b(2,64);juce::MidiBuffer m;m.addEvent(juce::MidiMessage::controllerEvent(1,21+k,127),0);
            p.processBlock(b,m);p.midiLearn().dispatch();},false);
    }

    xy.setAxes(1,6);set(p,PID::macroBite,0);set(p,PID::macroSpace,0);p.undoManager().clearUndoHistory();
    xy.mouseDown(event(xy,{180,180}));xy.mouseDrag(event(xy,{300,100}));xy.mouseUp(event(xy,{300,100}));
    const float xyX=p.apvts.getParameter(PID::macroBite)->getValue(),xyY=p.apvts.getParameter(PID::macroSpace)->getValue();
    bool xyUndo=p.undoManager().undo()&&p.apvts.getParameter(PID::macroBite)->getValue()==.5f&&p.apvts.getParameter(PID::macroSpace)->getValue()==.5f;
    xyUndo=xyUndo&&p.undoManager().redo()&&p.apvts.getParameter(PID::macroBite)->getValue()==xyX&&p.apvts.getParameter(PID::macroSpace)->getValue()==xyY;
    std::printf("XY two-axis undo/redo transaction: %s\n",xyUndo?"PASS":"FAIL");if(!xyUndo)++failures;
    // Panel coverage includes drawer children even when hidden; CHAOS uses its own attachments.
    std::unique_ptr<NeonRiotEditor> editor(static_cast<NeonRiotEditor*>(p.createEditor()));
    std::set<juce::String> covered{PID::chaosSelect,PID::chaosMorph,PID::chaosAmount};
    std::function<void(juce::Component&)> visit=[&](juce::Component& c){
        if(auto* control=dynamic_cast<ui::ParamControl*>(&c))covered.insert(control->parameter().getParameterID());
        for(auto* child:c.getChildren())visit(*child);
    };visit(*editor);

    auto button=[&](const juce::String& label){ui::LitButton* found=nullptr;
        std::function<void(juce::Component&)> find=[&](juce::Component& c){
            if(auto* b=dynamic_cast<ui::LitButton*>(&c);b&&b->text==label)found=b;
            for(auto* child:c.getChildren())find(*child);};find(*editor);
        if(found&&found->onClick)found->onClick();else {++failures;std::printf("MISSING BUTTON %s\n",label.toRawUTF8());}};
    set(p,PID::fltCutoff,1000);const float before=p.apvts.getParameter(PID::fltCutoff)->getValue();
    knob.mouseDown(event(knob,{40,60}));knob.mouseDrag(event(knob,{40,20}));knob.mouseUp(event(knob,{40,20}));
    const float changed=p.apvts.getParameter(PID::fltCutoff)->getValue();button("UNDO");
    bool history=p.apvts.getParameter(PID::fltCutoff)->getValue()==before;button("REDO");
    history=history&&p.apvts.getParameter(PID::fltCutoff)->getValue()==changed;
    button("B");set(p,PID::fltCutoff,330);const float bValue=p.apvts.getParameter(PID::fltCutoff)->getValue();
    button("A");history=history&&p.apvts.getParameter(PID::fltCutoff)->getValue()==changed;
    button("B");history=history&&p.apvts.getParameter(PID::fltCutoff)->getValue()==bValue;
    std::printf("Undo/redo and A/B button callbacks: %s\n",history?"PASS":"FAIL");if(!history)++failures;
    int missingTips=0,debugOnly=0;
    for(auto* v:p.parameterList()){
        if(ui::describe(v->getParameterID()).isEmpty()){++missingTips;std::printf("MISSING TOOLTIP %s\n",v->getParameterID().toRawUTF8());}
        if(covered.count(v->getParameterID())==0){++debugOnly;std::printf("DEBUG ONLY %s\n",v->getParameterID().toRawUTF8());}
    }
    std::printf("COVERAGE: %zu panel parameters; %d debug-only; %d missing tooltips\n",covered.size(),debugOnly,missingTips);
    if(missingTips)++failures;
    for(auto* v:p.getParameters())v->removeListener(&record);
    std::printf("GESTURES: %d failures\n",failures);return failures?1:0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    std::setvbuf (stdout, nullptr, _IOLBF, 0);
    const juce::String mode (argc > 1 ? argv[1] : "--presets");
    const juce::File outDir (argc > 2 ? argv[2] : "renders");
    if (mode == "--evolve") return nrtest::evolve (outDir);
    if (mode == "--evolve-checks") return nrtest::evolveChecks (outDir);
    if (mode == "--evolve-cpu") return nrtest::evolveCpu();
    if (mode == "--gestures") return gesturesTest();
    if (mode == "--text") return textTest();
    if (mode == "--routing") return routingTests();
    if (mode == "--fx") return effectsTests (outDir);
    if (mode == "--rt") return realtime();
    if (mode == "--dsp") return dsp();
    if (mode == "--stress") return stress();
    if (mode == "--cpu") return cpu();
    if (mode == "--state") return state();
    if (mode == "--clicks") return clicks();
    if (mode == "--saturation") return saturation (outDir);
    if (mode == "--presets") return renderPresets (outDir);
    if (mode == "--macros") return macrosTest();
    if (mode == "--midilearn") return midiLearnTest();
    if (mode == "--lfo") return lfoTest();
    if (mode == "--ui") return uiTest (outDir);
    if (! mode.startsWith ("--")) return renderPresets (juce::File (mode));
    std::fprintf (stderr, "Usage: NeonRiotRender [--evolve|--evolve-checks|--evolve-cpu|--gestures|--text|--presets|--stress|--clicks|--cpu|--state|--saturation|--dsp|--rt|--fx|--routing|--macros|--midilearn|--lfo] [outdir]\n");
    return 2;
}
