#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Primitives.h"
#include "Saturation.h"
#include "Effects.h"

namespace nr
{
// Values read from the parameter tree once per render chunk.
struct Block
{
    float osc1Wave, osc1Shape, osc1Level, osc1Pitch;   // pitch: oct*12 + semi + fine/100
    float osc2Wave, osc2Shape, osc2Level, osc2Pitch;
    bool  osc2Sync;
    float subLevel, subDiv; int subWave;
    float noiseLevel, noiseColour;
    int   voiceMode; float glide, pbRange, drift, instability;
    int   fltMode; float cutoff, reso, fltEnv, fltKey, fltVel, fltDrive;
    float fA, fD, fS, fR, aA, aD, aS, aR, ampVel;
    int   l1Wave, l1Dest, l1Div, l2Wave, l2Dest, l2Div; bool l1Sync, l2Sync; float l1Rate, l1Amt, l2Rate, l2Amt;
    float mwLfo, mwCutoff;
    bool l1Retrig, l2Retrig; float l1Fade, l2Fade;   // per-voice LFO articulation
    float biteAmt, bitePick, biteComb, biteTone;
    float preDrive; int preType;
    bool enhancedDrive; float preBias, postBias, driveTilt;
    float postDrive; int postType; float driveTone, driveMix; bool driveComp;
    float anchorAmt, anchorFreq, outputDb;
    EffectParameters effects;
};

// Per-sample modulation shared by all voices.
struct Frame
{
    float osc1Wave, osc1Shape, osc2Wave, osc2Shape, osc1Level, osc2Level, subLevel, noiseLevel;
    float pitch, osc2Pitch;        // semitones
    float cutoffLog2, reso;        // log2(Hz)
    float preDrive, comb, preBias, driveTilt;
    float ampMul;
};

class Voice
{
public:
    void prepare (float sampleRate, int maxCombSamples, uint32_t seed);
    void reset();
    void start (int note, float velocity, bool retrigger, bool glideFrom, float fromNote, const Block& b);
    void retarget (int note, bool glide);
    void release();
    void kill() { amp.reset(); filtEnv.reset(); }
    bool isActive() const { return amp.active(); }
    bool isReleasing() const { return amp.stage == Envelope::Release; }
    void setBlock (const Block& b);
    // returns processed voice output; adds the clean low-end layer into 'clean'
    float render (const Frame& f, float& clean);

    int note = -1;
    float currentNote = 60.0f;
    uint32_t age = 0;

    // Per-voice LFO copies: used only when an LFO retriggers or fades in.
    Lfo vlfo[2];
    uint32_t lfoAge = 0;
    float lfoFade (float seconds) const
    {
        if (seconds <= 0.0005f) return 1.0f;
        // a short hold then an S-curve rise: vibrato blooms after the attack
        const float x = clampf (((float) lfoAge / sr - 0.25f * seconds) / (0.75f * seconds), 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

private:
    float sr = 48000.0f;
    BlepOsc o1, o2, sub;
    double subSinePhase = 0.0;
    Ladder filter;
    Envelope amp, filtEnv;
    DcBlocker dc;
    PreDrive2x pre;
    OnePoleLP noiseLp, pickLp, combDamp;
    Rng rng;
    uint32_t rngSeed = 1;

    float targetNote = 60.0f, glideCoef = 1.0f, velocity = 1.0f;
    bool gliding = false;

    // analogue drift / instability random walks
    float driftV = 0, driftT = 0, instV = 0, instT = 0, cutInstV = 0, cutInstT = 0, osc2Detune = 0;
    int driftCount = 0, instCount = 0;

    // String Bite
    std::vector<float> comb;
    int combW = 0;
    float biteEnv = 0, pickEnv = 0, biteDecay = 0, pickDecay = 0;

    const Block* blk = nullptr;
};

class Engine
{
public:
    static constexpr int kNumVoices = 8;

    void prepare (double sampleRate, int maxBlock);
    void reset();
    int latencySamples() const { return latency; }
    float lastPlayedNote() const { return lastNote; }
    int activeVoiceCount() const { int n = 0; for (const auto& v : voices) n += v.isActive() ? 1 : 0; return n; }

    void setBlock (const Block& b);
    void setTransport (double bpm, double ppq, bool playing);
    void handleMidi (const juce::MidiMessage& m);
    void render (int start, int num);                 // voices into mono/clean scratch
    void finish (juce::AudioBuffer<float>& out, int num); // post chain to output

private:
    void noteOn (int note, float vel);
    void noteOff (int note);
    void allNotesOff (bool hard);
    Voice* allocate();
    int topHeld() const;

    float sr = 48000.0f;
    Block b {};
    std::array<Voice, kNumVoices> voices;
    uint32_t counter = 0;

    // note memory for mono/legato and sustain
    std::array<int, 128> heldOrder {};   // 0 = not held, else press counter
    std::array<bool, 128> sustained {};
    uint32_t pressCounter = 0;
    bool sustain = false;
    int lastMode = -1;
    float lastNote = 60.0f;

    float bend = 0.0f, modWheel = 0.0f;

    Lfo lfo1, lfo2;
    double bpm = 120.0, ppq = 0.0; bool playing = false;

    Smooth sWave1, sShape1, sWave2, sShape2, sL1, sL2, sSub, sNoise, sCut, sReso, sPre, sComb;
    Smooth sPost, sTone, sMix, sAnchor, sOut, sPreBias, sPostBias, sTilt, sPostTilt;

    std::vector<float> mono, clean;
    juce::dsp::Oversampling<float> os { 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::dsp::LinkwitzRileyFilter<float> anchorHp, anchorLp;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> sideHp;
    Effects effects;
    OnePoleLP toneLp;
    DriveVoicing post;
    std::array<float, PreDrive2x::latency> legacyDelay {};
    int legacyW = 0, postLatency = 0;
    DcBlocker postDc, cleanDc;
    std::vector<float> cleanDelay; int cleanW = 0;
    int latency = 0;
    bool firstBlock = true;
};
} // namespace nr
