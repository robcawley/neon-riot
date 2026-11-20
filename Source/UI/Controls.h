#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

class NeonRiotProcessor;

namespace ui
{
// Common behaviour for every control bound to one parameter.
class ParamControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    ParamControl (NeonRiotProcessor& p, const char* paramId, juce::String label);
    ~ParamControl() override;

    // Called by the editor's timer: picks up macro-modulated (effective) values.
    void refreshEffective();
    float norm() const { return param.convertTo0to1 (value); }
    float effectiveNorm() const { return param.convertTo0to1 (effective); }
    bool modulated() const { return std::abs (norm() - effectiveNorm()) > 0.004f; }
    juce::String valueText() const { return param.getCurrentValueAsText(); }
    juce::String effectiveText() const { return param.getText (effectiveNorm(), 0); }
    juce::RangedAudioParameter& parameter() { return param; }
    bool isLearning() const;
    int learnedCC() const;

    juce::String label;
    juce::Colour colour = C::green;

protected:
    void beginEdit();
    void setNorm (float n);              // during a gesture
    void endEdit();
    void setNormComplete (float n);      // one-shot change, with undo
    void showMenu();
    void typeValue();
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    virtual void valueChanged() { repaint(); }

    NeonRiotProcessor& proc;
    juce::RangedAudioParameter& param;
    int index = 0;
    float value = 0.0f, effective = 0.0f;
    bool editing = false, hovering = false;

    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hovering = false; repaint(); }

private:
    juce::ParameterAttachment attachment;
    float gestureStart = 0.0f;
    std::unique_ptr<juce::TextEditor> editor;
};

// Rotary knob: machined cap, neon value arc, outer ring for the effective (macro) value.
class Knob : public ParamControl
{
public:
    enum class Size { Small, Medium, Large, Macro };
    Knob (NeonRiotProcessor& p, const char* id, juce::String label, Size s = Size::Medium, juce::Colour c = C::green);
    void paint (juce::Graphics&) override;
    void resized() override { cap = {}; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool bipolar = false;
    Size size;
    static int preferredHeight (Size s);
    static int preferredWidth (Size s);
private:
    juce::Rectangle<float> dialArea() const;
    juce::Image cap;            // cached machined cap, redrawn only on resize
    float dragNorm = 0.0f, lastY = 0.0f;
    bool dragging = false;
};

// A row (or grid) of lit cells, one per choice: readable at a glance.
class Segments : public ParamControl
{
public:
    Segments (NeonRiotProcessor& p, const char* id, juce::String label, juce::StringArray cellNames = {},
              int columns = 0, juce::Colour c = C::green);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::StringArray names;
    int cols;
    juce::Rectangle<float> cell (int i) const;
};

// A display window showing the current choice; click for the full list, drag or scroll to step.
class ListSelector : public ParamControl
{
public:
    ListSelector (NeonRiotProcessor& p, const char* id, juce::String label, juce::Colour c = C::amber);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    void step (int delta);
};

// LED toggle for on/off parameters.
class Switch : public ParamControl
{
public:
    Switch (NeonRiotProcessor& p, const char* id, juce::String label, juce::Colour c = C::green);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
};

// Momentary PUNCH under a hazard-striped guard, and latching HOLD.
class PerformButton : public ParamControl
{
public:
    PerformButton (NeonRiotProcessor& p, const char* id, juce::String label, bool momentary);
    ~PerformButton() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    bool momentary, pressed = false;
};

// XY performance pad driving two macros through ordinary parameter attachments.
class XYPad : public juce::Component, public juce::SettableTooltipClient
{
public:
    XYPad (NeonRiotProcessor& p);
    ~XYPad() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void setAxes (int xMacro, int yMacro);
    int xAxis = 1, yAxis = 6;      // BITE x SPACE by default
private:
    void bind();
    void apply (juce::Point<float> pos);
    juce::Rectangle<float> pad() const { return getLocalBounds().toFloat().reduced (10.0f).withTrimmedBottom (4.0f); }
    NeonRiotProcessor& proc;
    juce::RangedAudioParameter *px = nullptr, *py = nullptr;
    std::unique_ptr<juce::ParameterAttachment> ax, ay;
    float vx = 0.5f, vy = 0.5f;
    float gestureX = 0.5f, gestureY = 0.5f;
    std::array<juce::Point<float>, 24> trail {};
    int trailHead = 0;
    bool active = false;
};

// Records a finished parameter change in the processor's undo history.
void pushUndo (NeonRiotProcessor& p, juce::RangedAudioParameter& param, float fromNorm, float toNorm);

// Shared tooltip text per parameter ID: one plain sentence each.
juce::String describe (const juce::String& paramId);
} // namespace ui
