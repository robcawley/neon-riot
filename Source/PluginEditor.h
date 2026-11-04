#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/Displays.h"
#include "UI/Chassis.h"

class NeonRiotProcessor;

namespace ui
{
// A small lit text button for the top bar and drawers.
class LitButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    LitButton (juce::String t, juce::Colour c = C::green) : text (std::move (t)), colour (c) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
    void setLit (bool b) { if (b != lit) { lit = b; repaint(); } }
    juce::String text;
    juce::Colour colour;
    std::function<void()> onClick;
    bool lit = false, hover = false;
};

// Plates painted once per scale into an image, so dynamic repaints are a cheap blit.
struct PlateDef { juce::Rectangle<float> r; PlateStyle style; uint32_t seed; };

class PlateLayer : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    std::vector<PlateDef> plates;
    bool chassis = false;                       // also paint the full chassis, wordmark and rails
    juce::Image cache;
};

class MidiMapList : public juce::Component
{
public:
    explicit MidiMapList (NeonRiotProcessor& p) : proc (p) {}
    void tick();
    void paint (juce::Graphics&) override;
private:
    NeonRiotProcessor& proc;
    uint32_t seen = 0xffffffffu;
    juce::StringArray lines;
};

class Panel : public juce::Component
{
public:
    static constexpr int W = 1280, H = 800;
    Panel (NeonRiotProcessor& p);
    void tick();
    void resized() override {}
    void openPage (int page);       // 0 = none, 1 MOD, 2 FX, 3 VOICE, 4 XY
    std::function<void (float)> onScale;
    bool reduceMotion = false;

private:
    template <class T, class... Args> T* add (int page, juce::Rectangle<int> r, Args&&... args);
    Knob* knob (int page, const char* id, const juce::String& label, Knob::Size s, int x, int y, juce::Colour c = C::green, int width = 0);
    void buildTopBar();
    void buildMain();
    void buildPages();
    void showSettings();
    void showPresets();
    void switchAB (int slot);
    void showAllParameters (bool show);
    void plate (int page, juce::Rectangle<float> r, juce::String title, juce::Colour c = C::amber, float rust = 1.0f);

    NeonRiotProcessor& proc;
    PlateLayer base;
    std::array<PlateLayer, 5> drawers;            // [1..4] used
    std::array<std::vector<juce::Component*>, 5> pages;   // [0] = left bank
    std::vector<ParamControl*> controls;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<LitButton*> tabs;
    int page = 0;

    Scope scope { proc };
    Meters meters { proc };
    FlowStrip flow { proc };
    ChaosDial dial { proc };
    std::unique_ptr<EnvelopeView> fenv, aenv;
    std::unique_ptr<XYPad> xy;
    std::unique_ptr<MidiMapList> midiList;
    LitButton *presetName = nullptr, *abA = nullptr, *abB = nullptr, *clip = nullptr, *undoB = nullptr, *redoB = nullptr;
    std::unique_ptr<juce::GenericAudioProcessorEditor> generic;
    std::unique_ptr<juce::Viewport> genericView;
    std::unique_ptr<LitButton> genericClose;
    int lastProgram = -1;
};
} // namespace ui

class NeonRiotEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NeonRiotEditor (NeonRiotProcessor&);
    ~NeonRiotEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    ui::Panel& getPanel() { return panel; }          // for the offscreen renderer
    void tickForTest() { panel.tick(); }

private:
    void timerCallback() override;
    NeonRiotProcessor& proc;
    ui::Panel panel;
    juce::TooltipWindow tips { this, 650 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeonRiotEditor)
};
