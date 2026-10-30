#pragma once
#include "Primitives.h"
#include <array>
#include <vector>

namespace nr
{
struct Stereo
{
    float l = 0, r = 0;
    Stereo operator+ (Stereo b) const { return { l + b.l, r + b.r }; }
    Stereo operator* (float g) const { return { l * g, r * g }; }
};
inline Stereo blend (Stereo a, Stereo b, float t) { return a * (1 - t) + b * t; }

struct EffectParameters
{
    int select = 0, mode = 0, division = 8;
    float morph = 0, amount = 0, mix = 1, holdPitch = 0, holdSize = 1;
    bool punch = false, hold = false;
    float delayMix = 0, delayTime = 0.25f, delayFeedback = 0.4f, delayTone = 0.4f;
    bool delaySync = true, pingPong = true; int delayDiv = 8;
    float reverbMix = 0, reverbPredelay = 0.02f, reverbSize = 0.5f, reverbDamp = 0.6f;
    float chorusMix = 0, chorusRate = 0.25f, chorusDepth = 0.5f;
    float shiftMix = 0, shiftHz = 0, shiftFeedback = 0;
};

// All vectors are sized only in prepare. Index arithmetic and interpolation allocate nothing.
struct Ring
{
    std::vector<float> data;
    int w = 0, valid = 0;
    void prepare (int capacity);
    void reset();
    float read (float delay) const;
    float at (double absolute) const;
    void push (float x);
};
struct WetFilter
{
    OnePoleLP low, dark;
    void prepare (float sr, float highCut = 16000);
    void reset();
    float process (float x);
};

// Causal 63-tap windowed Hilbert transformer plus its matched 31-sample real delay.
// This delay is part of the wet effect, like a chorus/delay tap; dry throughput stays immediate.
class FrequencyShifter
{
public:
    void prepare (float sr);
    void reset();
    Stereo process (Stereo x, float hz, float feedback);
private:
    struct Analytic
    {
        std::array<float, 64> h {};
        int w = 0;
        void reset() { h.fill (0); w = 0; }
        void process (float x, const std::array<float, 63>& taps, float& real, float& imag);
    };
    std::array<float, 63> taps {};
    std::array<Analytic, 2> filters;
    std::array<WetFilter, 2> feedbackFilters;
    Stereo previous;
    double phase = 0;
    float sampleRate = 48000;
};

class StereoDelay
{
public:
    void prepare (float sr, float maxSeconds = 48);
    void reset();
    Stereo process (Stereo x, float seconds, float feedback, bool pingPong, float tone);
private:
    std::array<Ring, 2> lines;
    std::array<WetFilter, 2> filters;
    Smooth ping, feedbackSmooth, toneSmooth;
    float timeSeconds = 0.25f, oldTime = 0.25f, timeFade = 1, timeFadeStep = 1;
    float sampleRate = 48000, lastTone = -1, maximumDelay = 48;
};

class DarkReverb
{
public:
    void prepare (float sr);
    void reset();
    void set (float size, float damp, float predelay);
    Stereo process (Stereo x);
private:
    std::array<Ring, 8> lines;
    std::array<OnePoleLP, 8> damping;
    std::array<float, 8> lengths {}, gains {};
    std::array<Ring, 2> pre;
    std::array<WetFilter, 2> inputFilter;
    Smooth sizeSmooth, predelaySmooth;
    float sampleRate = 48000;
};

class Chorus
{
public:
    void prepare (float sr);
    void reset();
    Stereo process (Stereo x, float rate, float depth);
private:
    std::array<Ring, 2> lines;
    std::array<WetFilter, 2> filters;
    float sampleRate = 48000;
    double phase = 0;
};

// Live history with a lazy copy-on-write snapshot: capturing any division costs O(1).
// Only a sample about to be overwritten is copied, so a 48-second HOLD never spikes the callback.
class Capture
{
public:
    void prepare (int capacity);
    void reset();
    void push (float x);
    void capture (int length);
    float frozen (double position) const;
    int length() const { return capturedLength; }
private:
    float sample (int index) const;
    std::vector<float> live, snapshot;
    uint64_t clock = 0, start = 0;
    int capturedLength = 0;
};

class Chaos
{
public:
    void prepare (float sr);
    void reset();
    void set (const EffectParameters& p, double bpm);
    Stereo process (float x);
private:
    struct Grain { bool active = false; double position = 0; float speed = 1; int age = 0, length = 1; float pan = 0; };
    float readLoop (double phase, float length) const;
    Stereo fracture (float x, float amount);
    Stereo swarm (float x, float amount);
    Stereo voidEffect (Stereo x, float amount);
    float sampleRate = 48000, divisionSamples = 24000;
    EffectParameters parameters;
    Capture history;
    Ring shortHistory;
    std::array<Grain, 12> grains;
    std::array<float, 8> weights { 1, 0, 0, 0, 0, 0, 0, 0 };
    std::array<Ring, 4> diffusers;
    FrequencyShifter foldShift;
    StereoDelay voidDelay;
    DarkReverb voidReverb;
    WetFilter tearBand;
    OnePoleLP crushSlew, tearTone;
    std::array<OnePoleLP, 2> foldTone;
    std::array<DcBlocker, 2> foldDc;
    DcBlocker tearDc, holdDc;
    Rng random;
    Smooth amount, mix, punch, holdBlend, pitch, size, morph;
    double heldPhase = 0, fracturePhase = 0;
    float heldOverlap = 0, holdDecay = 0, heldPrevious = 0, lastHeld = 0, slowEnvelope = 0, fastEnvelope = 0, gate = 0, crushValue = 0;
    float ratePhase = 0, fractureStart = 0, fractureLength = 100;
    int grainCountdown = 0, fractureCountdown = 0, holdTransition = 0, fractureTransition = 0;
    float oldFractureStart = 0, oldFractureLength = 100; double oldFracturePhase = 0;
    bool held = false, repeating = false, first = true;
};

class Effects
{
public:
    void prepare (float sr);
    void reset();
    void set (const EffectParameters&, double bpm);
    Stereo process (float x);
private:
    Chaos chaos;
    Chorus chorus;
    FrequencyShifter shift;
    StereoDelay delay;
    DarkReverb reverb;
    std::array<WetFilter, 2> chorusReturn, shiftReturn, delayReturn, reverbReturn;
    Smooth chorusMix, chorusRate, chorusDepth, shiftMix, shiftHz, shiftFeedback, delayMix, reverbMix;
    EffectParameters parameters;
    float delaySeconds = 0.25f;
    bool first = true;
};
}
