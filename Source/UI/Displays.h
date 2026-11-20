#pragma once
#include "Controls.h"
#include "Chassis.h"

namespace ui
{
// Every display is refreshed by the editor's timer through tick(); nothing reads audio directly.

class Scope : public juce::Component
{
public:
    explicit Scope (NeonRiotProcessor& p) : proc (p) { setOpaque (true); }
    void tick (bool reduceMotion);
    void paint (juce::Graphics&) override;
    void resized() override { bg = {}; }
private:
    juce::Image bg;
    NeonRiotProcessor& proc;
    std::array<float, 4096> buf {};
    std::array<juce::Path, 3> traces;      // newest first: phosphor persistence
    float hz = 0.0f;
    int voices = 0, quiet = 0;
    bool persistence = true;
};

class Meters : public juce::Component
{
public:
    explicit Meters (NeonRiotProcessor& p) : proc (p) { setOpaque (true); }
    void tick();
    void paint (juce::Graphics&) override;
private:
    NeonRiotProcessor& proc;
    float level[2] {}, hold[2] { -60.0f, -60.0f }, holdAge[2] {};
};

class FlowStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit FlowStrip (NeonRiotProcessor& p);
    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    std::function<void (int stage)> onStage;
    static const juce::StringArray& stageNames();
private:
    juce::Rectangle<float> box (int i) const;
    NeonRiotProcessor& proc;
    std::array<float, 9> activity {};
    std::array<int, 16> idx {};
};

class EnvelopeView : public juce::Component
{
public:
    EnvelopeView (NeonRiotProcessor& p, const char* a, const char* d, const char* s, const char* r, juce::Colour c);
    void tick();
    void paint (juce::Graphics&) override;
private:
    NeonRiotProcessor& proc;
    int ia, id, is, ir;
    float A = 0, D = 0, S = 0, R = 0;
    juce::Colour colour;
};

// The CHAOS dial: a heavy stepped selector inside an independent magenta intensity ring.
// Rendered into a cached image; redrawn only when its state (or the quantised glow) changes.
class ChaosDial : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ChaosDial (NeonRiotProcessor& p);
    ~ChaosDial() override;
    void tick (bool reduceMotion);
    void paint (juce::Graphics&) override;
    void resized() override { cache = {}; cap = {}; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    enum class Zone { None, Knob, Ring };
    struct Geo { juce::Point<float> c; float Ri, Rk, Rlamp, Rlab; };
    Geo geo() const;
    static float angleOf (float pos) { return -2.35619f + 4.71239f * juce::jlimit (0.0f, 7.0f, pos) / 7.0f; }
    juce::Rectangle<float> labelBox (int i) const;
    float position() const { return mode > 0.5f ? morph : select; }
    void render (juce::Graphics&);
    void showMenu();

    NeonRiotProcessor& proc;
    juce::RangedAudioParameter *pSelect, *pMode, *pMorph, *pAmount;
    std::unique_ptr<juce::ParameterAttachment> aSelect, aMode, aMorph, aAmount;
    float select = 0, mode = 0, morph = 0, amount = 0;
    float effSelect = 0, effAmount = 0, effMorph = 0;
    int glow = 0;                                  // 0..8, follows the output level while active
    int iSelect = 0, iAmount = 0, iMorph = 0;
    Zone zone = Zone::None;
    float dragAccum = 0, dragValue = 0, lastY = 0, gestureFrom = 0;
    juce::Image cap, cache;
    std::array<float, 9> cacheKey {};
};
} // namespace ui
