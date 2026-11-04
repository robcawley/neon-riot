#include "Evolve.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "Presets.h"
#include <chrono>
#include <numeric>
#include <set>

namespace nrtest
{
namespace
{
constexpr int sr = 48000, bs = 256, holdSeconds = 20;
constexpr double silence = 0.0000316227766; // -90 dBFS amplitude
void set (NeonRiotProcessor& p, const char* id, float value)
{
    auto* v = p.apvts.getParameter (id);
    v->setValueNotifyingHost (v->convertTo0to1 (value));
}
void noteOn (juce::MidiBuffer& midi, bool low = false)
{
    if (low) midi.addEvent (juce::MidiMessage::noteOn (1, 24, (juce::uint8) 100), 0);
    else for (int key : { 36, 43, 48 }) midi.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 100), 0);
}
double db (double gain) { return juce::Decibels::gainToDecibels (gain, -160.0); }
struct Result
{
    int index = 0, bad = 0;
    juce::String name;
    double peak = 0, rms = 0, tail = 0, finalTail = 0, mono = 0, body = 0;
    double centroidSpan = 0, centroidRelative = 0, rmsSpan = 0, lateCentroidRelative = 0, lateRmsSpan = 0;
    std::array<double, holdSeconds> centroids {}, levels {};
    bool evolves() const
    {
        return (centroidRelative >= 0.05 || rmsSpan >= 1.5)
            && (lateCentroidRelative >= 0.03 || lateRmsSpan >= 0.75);
    }
};
std::pair<double, double> span (const std::array<double, holdSeconds>& values, int first)
{
    const auto begin = values.begin() + first;
    const auto extrema = std::minmax_element (begin, values.end());
    const double mean = std::accumulate (begin, values.end(), 0.0) / (holdSeconds - first);
    return { *extrema.second - *extrema.first, mean };
}
Result measure (int index, const juce::File& directory)
{
    NeonRiotProcessor p; p.setCurrentProgram (index);
    p.setPlayConfigDetails (0, 2, sr, bs); p.prepareToPlay (sr, bs);
    Result r; r.index = index; r.name = p.getProgramName (index);
    const int hold = holdSeconds * sr, total = hold + (int) (p.getTailLengthSeconds() * sr);
    juce::AudioBuffer<float> output (2, total), block (2, bs);
    juce::MidiBuffer midi;
    double energy = 0, monoEnergy = 0, lowEnergy = 0, monoLowEnergy = 0;
    double lowL = 0, lowR = 0;
    const double lowCoef = 1 - std::exp (-2 * juce::MathConstants<double>::pi * 180 / sr);
    for (int position = 0; position < total; position += bs)
    {
        const int count = std::min (bs, total - position);
        block.setSize (2, count, false, false, true); midi.clear();
        if (position == 0) noteOn (midi);
        if (position == hold) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        p.processBlock (block, midi);
        for (int channel = 0; channel < 2; ++channel) output.copyFrom (channel, position, block, channel, 0, count);
        for (int n = 0; n < count; ++n)
        {
            const double l = block.getSample (0, n), right = block.getSample (1, n);
            if (! std::isfinite (l) || ! std::isfinite (right)) ++r.bad;
            const double peak = std::max (std::abs (l), std::abs (right));
            r.peak = std::max (r.peak, peak);
            if (position + n < hold)
            {
                const double power = (l*l + right*right) * 0.5;
                energy += power; r.levels[(size_t) ((position + n) / sr)] += power;
                const double mid = (l + right) * 0.5; monoEnergy += mid*mid;
                lowL += lowCoef * (l-lowL); lowR += lowCoef * (right-lowR);
                lowEnergy += (lowL*lowL + lowR*lowR) * 0.5;
                const double lowMid = (lowL + lowR) * 0.5; monoLowEnergy += lowMid*lowMid;
            }
            else if (peak >= silence) r.tail = (double) (position + n - hold + 1) / sr;
            if (position + n >= total - sr) r.finalTail = std::max (r.finalTail, peak);
        }
    }
    r.rms = db (std::sqrt (energy / hold));
    r.mono = db (std::sqrt (monoEnergy / std::max (energy, 1e-30)));
    r.body = db (std::sqrt (monoLowEnergy / std::max (lowEnergy, 1e-30)));
    for (auto& value : r.levels) value = db (std::sqrt (value / sr));

    // Amplitude-weighted spectral centroid: Hann-windowed 4096-point FFTs of mono audio,
    // 2048-sample hops wholly inside each one-second window, bins 20 Hz–12 kHz.
    constexpr int order = 12, fftSize = 1 << order;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> window (fftSize, juce::dsp::WindowingFunction<float>::hann);
    std::array<float, fftSize * 2> frame {};
    for (int second = 0; second < holdSeconds; ++second)
    {
        double numerator = 0, denominator = 0;
        for (int offset = 0; offset + fftSize <= sr; offset += fftSize / 2)
        {
            frame.fill (0);
            for (int n = 0; n < fftSize; ++n)
                frame[(size_t) n] = 0.5f * (output.getSample (0, second*sr+offset+n) + output.getSample (1, second*sr+offset+n));
            window.multiplyWithWindowingTable (frame.data(), fftSize);
            fft.performFrequencyOnlyForwardTransform (frame.data());
            for (int bin = 2; bin <= fftSize / 4; ++bin)
            {
                const double frequency = (double) bin * sr / fftSize;
                numerator += frame[(size_t) bin] * frequency; denominator += frame[(size_t) bin];
            }
        }
        r.centroids[(size_t) second] = numerator / std::max (denominator, 1e-30);
    }
    const auto [centroidSpan, centroidMean] = span (r.centroids, 5);
    r.centroidSpan = centroidSpan; r.centroidRelative = centroidSpan / std::max (centroidMean, 1.0);
    r.rmsSpan = span (r.levels, 5).first;
    const auto [lateSpan, lateMean] = span (r.centroids, 10);
    r.lateCentroidRelative = lateSpan / std::max (lateMean, 1.0); r.lateRmsSpan = span (r.levels, 10).first;

    // Every sample of the reported 110-second tail is checked. The audition WAV omits only
    // the verified sub--90 dB remainder, retaining at least two seconds beyond its last crossing.
    const int written = std::min (total, hold + (int) std::ceil ((r.tail + 2) * sr));
    const auto file = directory.getChildFile (juce::String (index).paddedLeft ('0', 2) + " " + r.name + ".wav");
    file.deleteFile(); juce::WavAudioFormat wav;
    auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (file), sr, 2, 24, {}, 0));
    if (! writer || ! writer->writeFromAudioSampleBuffer (output, 0, written)) throw std::runtime_error ("could not write evolution WAV");
    return r;
}
juce::var asJson (const Result& r)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("index", r.index); o->setProperty ("name", r.name); o->setProperty ("peakDb", db(r.peak));
    o->setProperty ("holdRmsDb", r.rms); o->setProperty ("nonfinite", r.bad); o->setProperty ("tailSeconds", r.tail);
    o->setProperty ("tailVerifiedSeconds", 110); o->setProperty ("finalTailDb", db(r.finalTail));
    o->setProperty ("monoDb", r.mono); o->setProperty ("bodyMonoDb", r.body);
    o->setProperty ("centroidSpanHz", r.centroidSpan); o->setProperty ("centroidRelative", r.centroidRelative);
    o->setProperty ("lateCentroidRelative", r.lateCentroidRelative); o->setProperty ("rmsSpanDb", r.rmsSpan);
    o->setProperty ("lateRmsSpanDb", r.lateRmsSpan); o->setProperty ("evolves", r.evolves());
    juce::Array<juce::var> centroids, levels;
    for (auto x : r.centroids) centroids.add (x); for (auto x : r.levels) levels.add (x);
    o->setProperty ("centroidHzBySecond", centroids); o->setProperty ("rmsDbBySecond", levels);
    return juce::var (o);
}
struct Hold
{
    std::vector<float> samples;
    double peak = 0, rms = 0;
    int bad = 0;
};
Hold shortHold (int index, const char* macro = nullptr, float value = 0, bool low = false)
{
    NeonRiotProcessor p; p.setCurrentProgram (index);
    if (macro != nullptr) set (p, macro, value);
    p.setPlayConfigDetails (0, 2, sr, bs); p.prepareToPlay (sr, bs);
    juce::AudioBuffer<float> block (2, bs); juce::MidiBuffer midi;
    Hold result; const int total = sr*12; result.samples.reserve ((size_t) total*2);
    double power = 0;
    for (int position = 0; position < total; position += bs)
    {
        midi.clear(); if (position == 0) noteOn (midi, low);
        p.processBlock (block, midi);
        for (int n = 0; n < bs; ++n) for (int c = 0; c < 2; ++c)
        {
            const float x = block.getSample (c,n); if (! std::isfinite(x)) ++result.bad;
            result.peak = std::max (result.peak, (double) std::abs(x)); power += x*x;
            result.samples.push_back (x);
        }
    }
    result.rms = db (std::sqrt (power / result.samples.size())); return result;
}
}
int evolve (const juce::File& directory)
{
    if (directory.createDirectory().failed()) return 2;
    NeonRiotProcessor inventory;
    if (inventory.getNumPrograms() != 56) { std::fprintf(stderr,"Expected 56 programs\n"); return 2; }
    int failures = 0; juce::Array<juce::var> results;
    std::vector<double> referenceLevels;
    std::set<juce::String> names;
    for (int i=0;i<inventory.getNumPrograms();++i) if (! names.insert(inventory.getProgramName(i)).second) ++failures;
    for (const char* name : { "Low Orbit", "Black Reservoir", "Sodium Horizon" })
        for (int i=0;i<36;++i) if (inventory.getProgramName(i)==name)
        {
            auto r=measure(i,directory); referenceLevels.push_back(r.rms);results.add(asJson(r));
            std::printf("REFERENCE %-23s peak %.2f RMS %.2f tail %.2fs mono %+.2fdB\n",name,db(r.peak),r.rms,r.tail,r.mono);
        }
    if (referenceLevels.size()!=3) return 2;
    const double referenceMean=std::accumulate(referenceLevels.begin(),referenceLevels.end(),0.0)/3;
    const double loudLimit=*std::max_element(referenceLevels.begin(),referenceLevels.end())+3;
    std::printf("Reference held RMS mean %.2f; new-preset target %.2f..%.2f dBFS; absolute loud limit %.2f\n",referenceMean,referenceMean-3,referenceMean+3,loudLimit);
    std::printf("| Index | Preset | Peak dBFS | Hold RMS | Tail s | Mono dB | Body dB | Centroid span Hz | RMS span dB | Late centroid %% | Late RMS span | Status |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (int i=36;i<56;++i)
    {
        inventory.setCurrentProgram(i);
        for(const char* id:kMacroIds) if(inventory.apvts.getRawParameterValue(id)->load()!=0)++failures;
        const auto& patch=factoryPresets()[(size_t)i];std::set<juce::String> ids;
        for(const auto& [id,value]:patch.values){auto* v=inventory.apvts.getParameter(id);
            if(v==nullptr||!ids.insert(id).second||value<v->getNormalisableRange().start||value>v->getNormalisableRange().end)++failures;}
        auto r=measure(i,directory); results.add(asJson(r));
        const bool pass=r.bad==0&&db(r.peak)<=-3&&r.finalTail<silence&&r.tail<110&&r.mono>=-3&&r.body>=-1
            && r.evolves() && std::abs(r.rms-referenceMean)<=3 && r.rms<=loudLimit;
        if(!pass)++failures;
        std::printf("| %d | %s | %.2f | %.2f | %.2f | %+.2f | %+.2f | %.1f | %.2f | %.1f | %.2f | %s |\n",i,r.name.toRawUTF8(),db(r.peak),r.rms,r.tail,r.mono,r.body,r.centroidSpan,r.rmsSpan,r.lateCentroidRelative*100,r.lateRmsSpan,pass?"PASS":"REWORK");
    }
    if(!directory.getChildFile("evolution.json").replaceWithText(juce::JSON::toString(juce::var(results))))++failures;
    std::printf("EVOLVE: %d failures\n",failures);return failures?1:0;
}
int evolveChecks (const juce::File& directory)
{
    directory.createDirectory();int failures=0;juce::Array<juce::var> rows;
    for(int i=36;i<56;++i)
    {
        NeonRiotProcessor p;p.setCurrentProgram(i);const auto name=p.getProgramName(i);
        const auto original=shortHold(i);const auto low=shortHold(i,nullptr,0,true);
        bool pass=low.bad==0&&db(low.peak)<=-3&&low.rms>-45;
        auto* row=new juce::DynamicObject();row->setProperty("index",i);row->setProperty("name",name);
        row->setProperty("lowNoteRmsDb",low.rms);row->setProperty("lowNotePeakDb",db(low.peak));
        juce::Array<juce::var> variations;
        for(const char* macro:{PID::macroMotion,PID::macroSpace})for(float value:{-1.0f,1.0f})
        {
            const auto changed=shortHold(i,macro,value);double difference=0;
            for(size_t n=sr*2*5;n<original.samples.size();++n){const double d=changed.samples[n]-original.samples[n];difference+=d*d;}
            difference=std::sqrt(difference/(original.samples.size()-sr*2*5));
            const bool ok=changed.bad==0&&changed.peak<=1&&difference>1e-5;pass=pass&&ok;
            auto* v=new juce::DynamicObject();v->setProperty("macro",macro);v->setProperty("value",value);
            v->setProperty("differenceRmsDb",db(difference));v->setProperty("peakDb",db(changed.peak));v->setProperty("pass",ok);variations.add(juce::var(v));
        }
        row->setProperty("variations",variations);row->setProperty("pass",pass);rows.add(juce::var(row));
        std::printf("%d %-24s low MIDI24 RMS %.2f peak %.2f; MOTION/SPACE +/-: %s\n",i,name.toRawUTF8(),low.rms,db(low.peak),pass?"PASS":"FAIL");if(!pass)++failures;
    }
    if(!directory.getChildFile("playability.json").replaceWithText(juce::JSON::toString(juce::var(rows))))++failures;
    std::printf("EVOLVE PLAYABILITY: %d failures\n",failures);return failures?1:0;
}
int evolveCpu()
{
    double heaviest=0;int heaviestIndex=-1,failures=0;
    std::printf("| Index | Preset | Mean %% | p99 %% | Worst %% |\n|---|---|---|---|---|\n");
    for(int i=36;i<56;++i)
    {
        NeonRiotProcessor p;p.setCurrentProgram(i);p.setPlayConfigDetails(0,2,sr,64);p.prepareToPlay(sr,64);
        juce::AudioBuffer<float> block(2,64);juce::MidiBuffer midi;
        for(int key:{36,43,48,51,55,60,63,67})midi.addEvent(juce::MidiMessage::noteOn(1,key,(juce::uint8)100),0);
        p.processBlock(block,midi);midi.clear();
        for(int n=0;n<1500;++n)p.processBlock(block,midi);
        std::vector<double> times;times.reserve(7500);
        for(int n=0;n<7500;++n){const auto start=std::chrono::steady_clock::now();p.processBlock(block,midi);
            times.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());}
        const double budget=64.0/sr*1e6;
        const double mean=std::accumulate(times.begin(),times.end(),0.0)/times.size()/budget*100;
        std::sort(times.begin(),times.end());const double p99=times[times.size()*99/100]/budget*100,worst=times.back()/budget*100;
        std::printf("| %d | %s | %.2f | %.2f | %.2f |\n",i,p.getProgramName(i).toRawUTF8(),mean,p99,worst);
        if(mean>heaviest){heaviest=mean;heaviestIndex=i;}if(mean>=50||p99>=100)++failures;
    }
    std::printf("Heaviest mean: index %d, %.2f%%; EVOLVE CPU: %d failures\n",heaviestIndex,heaviest,failures);return failures?1:0;
}
}
