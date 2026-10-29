// NeonRiotLoops: renders MIDI loops through NEON RIOT presets for Logic Live Loops.
// Usage: NeonRiotLoops <jobs.json>
// jobs.json: { "bpm": 85, "outdir": "...", "groups": [ { "name": "Bass", "peakDb": -3,
//   "jobs": [ { "preset": "Low Orbit", "midi": "a.mid", "out": "Bass/x.wav", "beats": 16, "seamless": true,
//               "overrides": { "glide": 0.05 }, "ramps": [ { "id": "flt_cutoff", "from": 300, "to": 6000, "curve": 2 } ],
//               "bend": [ 0, 1 ], "rootNote": 62 } ] } ] }
// Seamless loops are the middle of three passes with a 10 ms seam blend, so tails wrap and the
// loop point never clicks. One gain per group keeps the parts' relative levels.
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"

namespace
{
constexpr double SR = 48000.0;
constexpr int BS = 256;

struct Head final : juce::AudioPlayHead
{
    PositionInfo info;
    juce::Optional<PositionInfo> getPosition() const override { return info; }
};

bool setParam (NeonRiotProcessor& p, const juce::String& id, float v)
{
    if (auto* prm = p.apvts.getParameter (id)) { prm->setValueNotifyingHost (prm->convertTo0to1 (v)); return true; }
    std::printf ("  unknown parameter %s\n", id.toRawUTF8());
    return false;
}

int findProgram (NeonRiotProcessor& p, const juce::String& name)
{
    for (int i = 0; i < p.getNumPrograms(); ++i)
        if (p.getProgramName (i) == name) return i;
    return -1;
}

juce::AudioBuffer<float> renderJob (const juce::var& job, double bpm, const juce::File& base)
{
    NeonRiotProcessor proc;
    Head head;
    proc.setPlayHead (&head);
    proc.setPlayConfigDetails (0, 2, SR, BS);
    const int prog = findProgram (proc, job["preset"].toString());
    if (prog < 0) std::printf ("  unknown preset %s\n", job["preset"].toString().toRawUTF8());
    proc.setCurrentProgram (juce::jmax (0, prog));
    if (auto* ov = job["overrides"].getDynamicObject())
        for (auto& pr : ov->getProperties()) setParam (proc, pr.name.toString(), (float) (double) pr.value);
    proc.prepareToPlay (SR, BS);

    const double spq = SR * 60.0 / bpm;
    const int beats = (int) job["beats"];
    const int loopSamples = (int) std::llround (spq * beats);
    const bool seamless = (bool) job["seamless"];
    const int passes = seamless ? 3 : 1;

    juce::MidiFile mf;
    {
        juce::FileInputStream in (base.getChildFile (job["midi"].toString()));
        if (! in.openedOk() || ! mf.readFrom (in)) { std::printf ("  cannot read %s\n", job["midi"].toString().toRawUTF8()); return {}; }
    }
    const double tpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 480;
    struct E { int at; juce::MidiMessage m; };
    std::vector<E> ev;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (auto* e : *mf.getTrack (t))
            if (e->message.isNoteOnOrOff())
                for (int pass = 0; pass < passes; ++pass)
                    ev.push_back ({ (int) std::llround ((e->message.getTimeStamp() / tpq + (double) beats * pass) * spq), e->message });
    std::stable_sort (ev.begin(), ev.end(), [] (const E& a, const E& b) { return a.at < b.at; });

    struct Ramp { juce::String id; float from, to, curve; };
    std::vector<Ramp> ramps;
    if (auto* arr = job["ramps"].getArray())
        for (auto& r : *arr) ramps.push_back ({ r["id"].toString(), (float) (double) r["from"], (float) (double) r["to"],
                                                r.hasProperty ("curve") ? (float) (double) r["curve"] : 1.0f });
    const bool bend = job["bend"].isArray();
    const float bendFrom = bend ? (float) (double) job["bend"][0] : 0.0f, bendTo = bend ? (float) (double) job["bend"][1] : 0.0f;

    const int total = loopSamples * passes + (seamless ? (int) SR * 4 : 0);
    juce::AudioBuffer<float> all (2, total), blk (2, BS);
    size_t next = 0;
    for (int pos = 0; pos < total; pos += BS)
    {
        const int n = juce::jmin (BS, total - pos);
        const double ppq = pos / spq;
        head.info.setIsPlaying (true);
        head.info.setBpm (bpm);
        head.info.setPpqPosition (ppq);
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });

        // automation runs across one loop length (risers are single pass)
        const float t = juce::jlimit (0.0f, 1.0f, (float) std::fmod ((double) pos, (double) loopSamples) / (float) loopSamples);
        for (auto& r : ramps) setParam (proc, r.id, r.from + (r.to - r.from) * std::pow (t, r.curve));

        juce::MidiBuffer mb;
        if (bend)
        {
            const float b = bendFrom + (bendTo - bendFrom) * t * t;
            mb.addEvent (juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, (int) std::lround (8192.0 + b * 8191.0))), 0);
        }
        while (next < ev.size() && ev[next].at < pos + n)
        {
            mb.addEvent (ev[next].m, juce::jlimit (0, n - 1, ev[next].at - pos));
            ++next;
        }
        blk.setSize (2, n, false, false, true);
        blk.clear();
        proc.processBlock (blk, mb);
        for (int c = 0; c < 2; ++c) all.copyFrom (c, pos, blk, c, 0, n);
    }

    juce::AudioBuffer<float> loop (2, loopSamples);
    if (! seamless)
    {
        for (int c = 0; c < 2; ++c) loop.copyFrom (c, 0, all, c, 0, loopSamples);
        const int fade = (int) (SR * 0.004);                         // no click at the cut
        loop.applyGainRamp (loopSamples - fade, fade, 1.0f, 0.0f);
        return loop;
    }
    const int start = loopSamples, x = (int) (SR * 0.010);
    for (int c = 0; c < 2; ++c)
    {
        loop.copyFrom (c, 0, all, c, start, loopSamples);
        // seam: blend the start toward what actually follows the loop's own end (pass 3)
        for (int i = 0; i < x; ++i)
        {
            const float w = (float) i / (float) x;
            loop.setSample (c, i, all.getSample (c, start + i) * w + all.getSample (c, start + loopSamples + i) * (1.0f - w));
        }
    }
    return loop;
}

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double bpm, int beats, int root, bool oneShot)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::StringPairArray meta;
    meta.set (juce::WavAudioFormat::acidOneShot, oneShot ? "1" : "0");
    meta.set (juce::WavAudioFormat::acidRootSet, root > 0 ? "1" : "0");
    meta.set (juce::WavAudioFormat::acidStretch, "1");
    meta.set (juce::WavAudioFormat::acidDiskBased, "0");
    meta.set (juce::WavAudioFormat::acidizerFlag, "1");
    meta.set (juce::WavAudioFormat::acidRootNote, juce::String (root > 0 ? root : 62));
    meta.set (juce::WavAudioFormat::acidBeats, juce::String (beats));
    meta.set (juce::WavAudioFormat::acidDenominator, "4");
    meta.set (juce::WavAudioFormat::acidNumerator, "4");
    meta.set (juce::WavAudioFormat::acidTempo, juce::String (bpm));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), SR, 2, 24, meta, 0));
    return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: NeonRiotLoops <jobs.json>\n"); return 1; }
    const juce::File jf { juce::String (argv[1]) };
    const auto root = juce::JSON::parse (jf);
    const double bpm = root["bpm"];
    const juce::File outDir (root["outdir"].toString());
    const auto base = jf.getParentDirectory();

    int failures = 0;
    for (auto& group : *root["groups"].getArray())
    {
        struct Out { juce::String path; juce::AudioBuffer<float> buf; int beats, rootNote; bool oneShot; };
        std::vector<Out> outs;
        for (auto& job : *group["jobs"].getArray())
        {
            std::printf ("%s ...\n", job["out"].toString().toRawUTF8());
            auto b = renderJob (job, bpm, base);
            if (b.getNumSamples() == 0) { ++failures; continue; }
            bool finite = true;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) finite &= std::isfinite (b.getSample (c, i));
            if (! finite) { std::printf ("  NON-FINITE\n"); ++failures; continue; }
            outs.push_back ({ job["out"].toString(), std::move (b), (int) job["beats"],
                              job.hasProperty ("rootNote") ? (int) job["rootNote"] : 62, ! (bool) job["seamless"] });
        }
        // Per file: bring each loop to the group's loudness (RMS) target, never above its peak ceiling.
        const float ceil = juce::Decibels::decibelsToGain ((float) (double) group["peakDb"]);
        const bool hasRms = group.hasProperty ("rmsDb");
        const float rmsTarget = hasRms ? juce::Decibels::decibelsToGain ((float) (double) group["rmsDb"]) : 0.0f;
        for (auto& o : outs)
        {
            const float peak = juce::jmax (o.buf.getMagnitude (0, o.buf.getNumSamples()), 1.0e-6f);
            const float rms = juce::jmax (0.5f * (o.buf.getRMSLevel (0, 0, o.buf.getNumSamples()) + o.buf.getRMSLevel (1, 0, o.buf.getNumSamples())), 1.0e-6f);
            const float gain = hasRms ? juce::jmin (rmsTarget / rms, ceil / peak) : ceil / peak;
            o.buf.applyGain (gain);
            writeWav (outDir.getChildFile (o.path), o.buf, bpm, o.beats, o.rootNote, false);
            std::printf ("%-48s peak %6.2f dB  rms %6.2f dB\n", o.path.toRawUTF8(),
                         juce::Decibels::gainToDecibels (o.buf.getMagnitude (0, o.buf.getNumSamples())),
                         juce::Decibels::gainToDecibels (o.buf.getRMSLevel (0, 0, o.buf.getNumSamples())));
        }
        std::printf ("group %s done\n", group["name"].toString().toRawUTF8());
    }
    std::printf ("%s\n", failures == 0 ? "DONE" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
