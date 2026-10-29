#include "Controls.h"
#include "../PluginProcessor.h"

namespace ui
{
namespace
{
    constexpr float kStart = -2.35619f, kEnd = 2.35619f;      // -135 deg .. +135 deg
    float angleFor (float n) { return kStart + (kEnd - kStart) * n; }
    float clipboard = -1.0f;

    struct ParamAction : juce::UndoableAction
    {
        ParamAction (juce::RangedAudioParameter& p, float f, float t) : param (p), from (f), to (t) {}
        bool perform() override { set (to); return true; }
        bool undo() override    { set (from); return true; }
        int getSizeInUnits() override { return 8; }
        void set (float n) { param.beginChangeGesture(); param.setValueNotifyingHost (n); param.endChangeGesture(); }
        juce::RangedAudioParameter& param; float from, to;
    };

    juce::StringArray choicesOf (juce::RangedAudioParameter& p)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&p)) return c->choices;
        if (dynamic_cast<juce::AudioParameterBool*> (&p) != nullptr) return { "OFF", "ON" };
        juce::StringArray s;
        const auto r = p.getNormalisableRange();
        for (float v = r.start; v <= r.end + 0.001f; v += 1.0f) s.add (p.getText (p.convertTo0to1 (v), 0));
        return s;
    }
}

void pushUndo (NeonRiotProcessor& p, juce::RangedAudioParameter& param, float from, float to)
{
    if (std::abs (to - from) < 1.0e-6f) return;
    p.undoManager().beginNewTransaction (param.getName (40));
    p.undoManager().perform (new ParamAction (param, from, to));
}

//==============================================================================
ParamControl::ParamControl (NeonRiotProcessor& p, const char* paramId, juce::String lbl)
    : label (std::move (lbl)), proc (p), param (*p.apvts.getParameter (paramId)),
      index (p.parameterIndex (paramId)),
      attachment (param, [this] (float v) { value = v; valueChanged(); }, nullptr)
{
    attachment.sendInitialUpdate();
    effective = value;
    setTooltip (describe (paramId));
    setTitle (param.getName (64));
    setDescription (describe (paramId));
    setRepaintsOnMouseActivity (false);
}

ParamControl::~ParamControl() { if (editing) attachment.endGesture(); }

void ParamControl::refreshEffective()
{
    const float e = proc.effectiveValue (index);
    if (std::abs (e - effective) > 1.0e-5f * (std::abs (e) + 1.0f)) { effective = e; repaint(); }
    const bool learningNow = isLearning();
    if (learningNow) repaint();
}

bool ParamControl::isLearning() const { return proc.midiLearn().learningIndex() == index; }
int ParamControl::learnedCC() const   { return proc.midiLearn().ccFor (index); }

void ParamControl::beginEdit() { gestureStart = norm(); editing = true; attachment.beginGesture(); }
void ParamControl::setNorm (float n) { attachment.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n))); }

void ParamControl::endEdit()
{
    if (! editing) return;
    attachment.endGesture();
    editing = false;
    const float now = norm();
    if (std::abs (now - gestureStart) > 1.0e-6f)
    {
        proc.undoManager().beginNewTransaction (param.getName (40));
        proc.undoManager().perform (new ParamAction (param, gestureStart, now));
    }
}

void ParamControl::setNormComplete (float n)
{
    const float from = norm();
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n)));
    if (std::abs (norm() - from) > 1.0e-6f)
    {
        proc.undoManager().beginNewTransaction (param.getName (40));
        proc.undoManager().perform (new ParamAction (param, from, norm()));
    }
}

void ParamControl::showMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (param.getName (48));
    const int cc = learnedCC();
    if (isLearning()) m.addItem (1, "Cancel MIDI Learn");
    else              m.addItem (1, cc >= 0 ? "MIDI Learn (now CC " + juce::String (cc) + ")" : "MIDI Learn");
    if (cc >= 0) m.addItem (2, "Forget CC " + juce::String (cc));
    m.addSeparator();
    m.addItem (3, "Reset to default");
    m.addItem (4, "Type a value...");
    m.addItem (5, "Copy value");
    m.addItem (6, "Paste value", clipboard >= 0.0f);
    juce::Component::SafePointer<ParamControl> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr) return;
        auto& c = *safe;
        auto& learn = c.proc.midiLearn();
        switch (r)
        {
            case 1: if (c.isLearning()) learn.cancelLearning(); else learn.startLearning (c.index); break;
            case 2: learn.forget (c.index); break;
            case 3: c.setNormComplete (c.param.getDefaultValue()); break;
            case 4: c.typeValue(); break;
            case 5: clipboard = c.norm(); break;
            case 6: if (clipboard >= 0.0f) c.setNormComplete (clipboard); break;
            default: break;
        }
        c.repaint();
    });
}

void ParamControl::typeValue()
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (Fonts::get().mono (14.0f));
    editor->setColour (juce::TextEditor::backgroundColourId, C::display);
    editor->setColour (juce::TextEditor::textColourId, colour);
    editor->setColour (juce::TextEditor::outlineColourId, colour.withAlpha (0.6f));
    editor->setColour (juce::TextEditor::focusedOutlineColourId, colour);
    editor->setJustification (juce::Justification::centred);
    editor->setText (param.getCurrentValueAsText(), false);
    editor->selectAll();
    addAndMakeVisible (*editor);
    editor->setBounds (getLocalBounds().withSizeKeepingCentre (std::max (getWidth(), 70), 24).constrainedWithin (getLocalBounds().expanded (20)));
    auto finish = [this] (bool commit)
    {
        if (editor == nullptr) return;
        if (commit) setNormComplete (param.getValueForText (editor->getText()));
        juce::Component::SafePointer<ParamControl> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->editor.reset(); });
    };
    editor->onReturnKey = [finish] { finish (true); };
    editor->onEscapeKey = [finish] { finish (false); };
    editor->onFocusLost = [finish] { finish (true); };
    editor->grabKeyboardFocus();
}

std::unique_ptr<juce::AccessibilityHandler> ParamControl::createAccessibilityHandler()
{
    struct Value : juce::AccessibilityRangedNumericValueInterface
    {
        explicit Value (ParamControl& c) : control (c) {}
        bool isReadOnly() const override { return false; }
        double getCurrentValue() const override { return control.norm(); }
        void setValue (double v) override { control.setNormComplete ((float) v); }
        AccessibleValueRange getRange() const override { return { { 0.0, 1.0 }, 0.01 }; }
        ParamControl& control;
    };
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

//==============================================================================
static float diameter (Knob::Size s)
{
    switch (s) { case Knob::Size::Small: return 34.0f; case Knob::Size::Medium: return 44.0f;
                 case Knob::Size::Large: return 62.0f; case Knob::Size::Macro: return 78.0f; }
    return 44.0f;
}
int Knob::preferredWidth (Size s)  { return (int) diameter (s) + (s == Size::Macro ? 40 : 26); }
int Knob::preferredHeight (Size s) { return (int) diameter (s) + (s == Size::Macro ? 44 : 36); }

Knob::Knob (NeonRiotProcessor& p, const char* id, juce::String lbl, Size s, juce::Colour c)
    : ParamControl (p, id, std::move (lbl)), size (s)
{
    colour = c;
    const auto r = param.getNormalisableRange();
    bipolar = r.start < 0.0f && r.end > 0.0f && std::abs (r.start + r.end) < 1.0e-3f;
}

juce::Rectangle<float> Knob::dialArea() const
{
    const float d = diameter (size);
    return juce::Rectangle<float> (d, d).withCentre ({ getWidth() * 0.5f, 4.0f + d * 0.5f });
}

void Knob::paint (juce::Graphics& g)
{
    const auto a = dialArea();
    const auto c = a.getCentre();
    const float R = a.getWidth() * 0.5f;
    const bool big = size == Size::Large || size == Size::Macro;

    // scale ticks
    if (big)
        for (int i = 0; i <= 10; ++i)
        {
            const float ang = angleFor ((float) i / 10.0f);
            const auto dir = juce::Point<float> (std::sin (ang), -std::cos (ang));
            g.setColour (C::boneDim.withAlpha (i == 0 || i == 10 || (bipolar && i == 5) ? 0.8f : 0.35f));
            g.drawLine ({ c + dir * (R + 3.0f), c + dir * (R + (i % 5 == 0 ? 7.5f : 5.5f)) }, 1.0f);
        }

    // groove
    juce::Path groove;
    groove.addCentredArc (c.x, c.y, R - 2.0f, R - 2.0f, 0.0f, kStart, kEnd, true);
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.strokePath (groove, juce::PathStrokeType (big ? 5.0f : 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    const float vAng = angleFor (norm());
    const float from = bipolar ? 0.0f : kStart;
    if (std::abs (vAng - from) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, R - 2.0f, R - 2.0f, 0.0f, std::min (from, vAng), std::max (from, vAng), true);
        neonStroke (g, arc, colour, big ? 2.8f : 2.2f, hovering || dragging ? 1.3f : 0.9f);
    }

    // effective (macro-modulated) value: thin outer ring plus a marker
    if (modulated())
    {
        const float eAng = angleFor (effectiveNorm());
        juce::Path ring;
        ring.addCentredArc (c.x, c.y, R + 1.6f, R + 1.6f, 0.0f, std::min (vAng, eAng), std::max (vAng, eAng), true);
        g.setColour (colour.withAlpha (0.55f));
        g.strokePath (ring, juce::PathStrokeType (1.4f));
        const auto e = c + juce::Point<float> (std::sin (eAng), -std::cos (eAng)) * (R + 1.6f);
        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (4.2f, 4.2f).withCentre (e));
    }

    // machined cap, cached at the physical resolution
    const float capR = R * 0.70f;
    const float sc = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int px = juce::roundToInt (capR * 2.0f * sc);
    if (cap.isNull() || cap.getWidth() != px)
    {
        cap = juce::Image (juce::Image::ARGB, px, px, true);
        juce::Graphics cg (cap);
        cg.addTransform (juce::AffineTransform::scale (sc));
        const juce::Rectangle<float> b (0, 0, capR * 2.0f, capR * 2.0f);
        const auto cc = b.getCentre();
        cg.setColour (juce::Colours::black.withAlpha (0.6f));
        cg.fillEllipse (b);
        cg.setColour (juce::Colour (0xff1d1f22));
        cg.fillEllipse (b.reduced (0.6f));
        for (int i = 0; i < 40; ++i)                                         // knurled rim
        {
            const float ang = juce::MathConstants<float>::twoPi * (float) i / 40.0f;
            const auto dir = juce::Point<float> (std::sin (ang), -std::cos (ang));
            cg.setColour (i % 2 ? juce::Colours::white.withAlpha (0.10f) : juce::Colours::black.withAlpha (0.5f));
            cg.drawLine ({ cc + dir * capR * 0.80f, cc + dir * (capR - 0.8f) }, 1.0f);
        }
        const auto face = b.reduced (capR * 0.2f);
        cg.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d43), face.getTopLeft(), juce::Colour (0xff0e0f11), face.getBottomRight(), false));
        cg.fillEllipse (face);
        for (float rr = 0.15f; rr < 0.95f; rr += 0.12f)                     // spun-metal rings
        {
            cg.setColour (juce::Colours::white.withAlpha (0.025f));
            cg.drawEllipse (face.withSizeKeepingCentre (face.getWidth() * rr, face.getHeight() * rr), 0.7f);
        }
        cg.setColour (juce::Colours::white.withAlpha (0.12f));
        cg.drawEllipse (face.reduced (0.5f), 0.8f);
    }
    g.drawImage (cap, juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c));

    // pointer
    const auto dir = juce::Point<float> (std::sin (vAng), -std::cos (vAng));
    juce::Path ptr;
    ptr.startNewSubPath (c + dir * capR * 0.22f);
    ptr.lineTo (c + dir * capR * 0.78f);
    neonStroke (g, ptr, colour, big ? 2.4f : 1.9f, 0.7f);

    // label and value
    auto text = getLocalBounds().toFloat();
    text.removeFromTop (a.getBottom() + (big ? 6.0f : 3.0f));
    const float lh = size == Size::Macro ? 17.0f : 14.0f;
    auto lbl = text.removeFromTop (lh);
    if (size == Size::Macro)
        neonText (g, label, lbl, Fonts::get().heavy (17.0f).withExtraKerningFactor (0.08f), C::bone, juce::Justification::centred, 0.3f);
    else
    {
        g.setColour (C::bone);
        g.setFont (Fonts::get().label (13.5f));
        g.drawText (label, lbl, juce::Justification::centred, false);
    }
    auto well = text.removeFromTop (15.0f).withSizeKeepingCentre (std::min (text.getWidth() - 2.0f, size == Size::Macro ? 86.0f : 66.0f), 15.0f);
    g.setColour (C::display);
    g.fillRoundedRectangle (well, 2.0f);
    juce::String vt = isLearning() ? juce::String ("MOVE A KNOB") : valueText();
    const auto vc = isLearning() ? C::magenta : (hovering || dragging ? colour : colour.withAlpha (0.82f));
    g.setColour (vc);
    g.setFont (Fonts::get().mono (12.0f));
    g.drawFittedText (vt, well.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.8f);

    if (const int cc = learnedCC(); cc >= 0)
    {
        g.setColour (C::amber);
        g.setFont (Fonts::get().mono (9.5f));
        g.drawText ("CC" + juce::String (cc), juce::Rectangle<float> (30, 11).withPosition (a.getRight() - 8.0f, a.getY() - 2.0f),
                    juce::Justification::centredLeft, false);
    }
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    if (e.mods.isAltDown()) { typeValue(); return; }
    dragging = true;
    dragNorm = norm();
    lastY = e.position.y;
    beginEdit();
    repaint();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const bool fine = e.mods.isShiftDown() || e.mods.isCommandDown();
    const float pixelsForFull = (fine ? 5.0f : 1.0f) * (size == Size::Macro ? 260.0f : 200.0f);
    dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + (lastY - e.position.y) / pixelsForFull);
    lastY = e.position.y;
    setNorm (dragNorm);
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    if (dragging) endEdit();
    dragging = false;
    repaint();
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) setNormComplete (param.getDefaultValue());
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float step = (e.mods.isShiftDown() || e.mods.isCommandDown()) ? 0.005f : 0.03f;
    const float d = (w.deltaY != 0.0f ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
    setNormComplete (norm() + (d > 0 ? step : -step) * std::min (4.0f, std::abs (d) * 8.0f));
}

//==============================================================================
Segments::Segments (NeonRiotProcessor& p, const char* id, juce::String lbl, juce::StringArray cellNames, int columns, juce::Colour c)
    : ParamControl (p, id, std::move (lbl)), names (cellNames.isEmpty() ? choicesOf (param) : cellNames), cols (columns)
{
    colour = c;
}

juce::Rectangle<float> Segments::cell (int i) const
{
    auto r = getLocalBounds().toFloat();
    if (label.isNotEmpty()) r.removeFromTop (15.0f);
    const int n = names.size();
    const int c = cols > 0 ? cols : n;
    const int rows = (n + c - 1) / c;
    const float w = r.getWidth() / (float) c, h = r.getHeight() / (float) rows;
    return juce::Rectangle<float> (r.getX() + w * (float) (i % c), r.getY() + h * (float) (i / c), w, h).reduced (1.5f);
}

void Segments::paint (juce::Graphics& g)
{
    if (label.isNotEmpty())
    {
        g.setColour (C::boneDim);
        g.setFont (Fonts::get().label (12.5f));
        g.drawText (label, getLocalBounds().removeFromTop (14), juce::Justification::centredLeft, false);
    }
    const int sel = juce::roundToInt (norm() * (float) (names.size() - 1));
    const int eff = juce::roundToInt (effectiveNorm() * (float) (names.size() - 1));
    for (int i = 0; i < names.size(); ++i)
    {
        const auto r = cell (i);
        const bool on = i == sel;
        g.setColour (on ? colour.withAlpha (0.16f) : juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (on ? colour.withAlpha (0.9f) : (i == eff ? colour.withAlpha (0.45f) : juce::Colours::white.withAlpha (0.08f)));
        g.drawRoundedRectangle (r, 2.5f, on ? 1.4f : 1.0f);
        const auto f = Fonts::get().label (std::min (13.5f, r.getHeight() * 0.62f));
        if (on) neonText (g, names[i], r, f, colour, juce::Justification::centred, 0.6f);
        else { g.setFont (f); g.setColour (C::boneDim); g.drawFittedText (names[i], r.toNearestInt(), juce::Justification::centred, 1, 0.7f); }
    }
}

void Segments::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    for (int i = 0; i < names.size(); ++i)
        if (cell (i).contains (e.position))
            setNormComplete (names.size() > 1 ? (float) i / (float) (names.size() - 1) : 0.0f);
}

//==============================================================================
ListSelector::ListSelector (NeonRiotProcessor& p, const char* id, juce::String lbl, juce::Colour c)
    : ParamControl (p, id, std::move (lbl)) { colour = c; }

void ListSelector::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const bool inlineLabel = label.isNotEmpty() && getHeight() < 30;
    if (label.isNotEmpty() && ! inlineLabel)
    {
        g.setColour (C::boneDim);
        g.setFont (Fonts::get().label (12.5f));
        g.drawText (label, r.removeFromTop (15.0f), juce::Justification::centredLeft, false);
    }
    r = r.withSizeKeepingCentre (r.getWidth(), std::min (r.getHeight(), 22.0f));
    if (inlineLabel)
    {
        g.setColour (C::display);
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (C::boneDim);
        g.setFont (Fonts::get().label (12.5f));
        g.drawText (label, r.withTrimmedLeft (18.0f), juce::Justification::centredLeft, false);
    }
    if (! inlineLabel) { g.setColour (C::display); g.fillRoundedRectangle (r, 2.5f); }
    g.setColour (hovering ? colour.withAlpha (0.5f) : juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (r, 2.5f, 1.0f);
    for (int side : { -1, 1 })
    {
        juce::Path t;
        const float x = side < 0 ? r.getX() + 8.0f : r.getRight() - 8.0f, y = r.getCentreY();
        t.addTriangle (x - 3.0f * side, y - 3.5f, x - 3.0f * side, y + 3.5f, x + 3.0f * side, y);
        g.setColour (colour.withAlpha (0.6f));
        g.fillPath (t);
    }
    g.setColour (isLearning() ? C::magenta : colour);
    g.setFont (Fonts::get().mono (12.5f));
    g.drawFittedText (isLearning() ? juce::String ("MOVE A KNOB") : valueText() + (inlineLabel && param.getParameterID() == PID::pbRange ? " st" : ""),
                      r.reduced (16.0f, 0).toNearestInt(), inlineLabel ? juce::Justification::centredRight : juce::Justification::centred, 1, 0.75f);
}

void ListSelector::step (int delta)
{
    const auto choices = choicesOf (param);
    const int n = std::max (2, choices.size());
    const int i = juce::jlimit (0, n - 1, juce::roundToInt (norm() * (float) (n - 1)) + delta);
    setNormComplete ((float) i / (float) (n - 1));
}

void ListSelector::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    if (e.position.x < 20.0f) { step (-1); return; }
    if (e.position.x > (float) getWidth() - 20.0f) { step (1); return; }
    const auto choices = choicesOf (param);
    const int cur = juce::roundToInt (norm() * (float) (choices.size() - 1));
    juce::PopupMenu m;
    for (int i = 0; i < choices.size(); ++i) m.addItem (i + 1, choices[i], true, i == cur);
    juce::Component::SafePointer<ListSelector> safe (this);
    const int n = choices.size();
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, n] (int r)
    {
        if (safe != nullptr && r > 0) safe->setNormComplete ((float) (r - 1) / (float) std::max (1, n - 1));
    });
}

void ListSelector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    step ((w.deltaY > 0) != w.isReversed ? -1 : 1);
}

//==============================================================================
Switch::Switch (NeonRiotProcessor& p, const char* id, juce::String lbl, juce::Colour c)
    : ParamControl (p, id, std::move (lbl)) { colour = c; }

void Switch::paint (juce::Graphics& g)
{
    const bool on = norm() > 0.5f;
    auto r = getLocalBounds().toFloat();
    const auto led = juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ r.getX() + 8.0f, r.getCentreY() });
    g.setColour (juce::Colours::black);
    g.fillEllipse (led.expanded (2.0f));
    if (on)
    {
        g.setColour (colour.withAlpha (0.25f));
        g.fillEllipse (led.expanded (5.0f));
        g.setGradientFill (juce::ColourGradient (colour.brighter (0.6f), led.getCentre(), colour.darker (0.3f), led.getBottomRight(), true));
    }
    else g.setColour (colour.withAlpha (0.12f));
    g.fillEllipse (led);
    r.removeFromLeft (20.0f);
    g.setColour (isLearning() ? C::magenta : (on ? C::bone : C::boneDim));
    g.setFont (Fonts::get().label (13.0f));
    g.drawFittedText (isLearning() ? juce::String ("MOVE A KNOB") : label, r.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
}

void Switch::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    setNormComplete (norm() > 0.5f ? 0.0f : 1.0f);
}

//==============================================================================
PerformButton::PerformButton (NeonRiotProcessor& p, const char* id, juce::String lbl, bool isMomentary)
    : ParamControl (p, id, std::move (lbl)), momentary (isMomentary) { colour = C::magenta; }

PerformButton::~PerformButton()
{
    if (pressed) { setNorm (0.0f); endEdit(); }
}

void PerformButton::paint (juce::Graphics& g)
{
    const bool on = norm() > 0.5f;
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    if (momentary)
    {
        // hazard-striped guard
        juce::Path frame; frame.addRoundedRectangle (r, 6.0f);
        juce::Path hole; hole.addRoundedRectangle (r.reduced (6.0f), 4.0f);
        {
            juce::Graphics::ScopedSaveState ss (g);
            frame.addPath (hole); frame.setUsingNonZeroWinding (false);
            g.reduceClipRegion (frame);
            g.fillAll (juce::Colour (0xff141414));
            g.setColour (C::magenta.withAlpha (0.75f));
            for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 12.0f)
            {
                juce::Path s;
                s.addQuadrilateral (x, r.getBottom(), x + 6.0f, r.getBottom(), x + 6.0f + r.getHeight(), r.getY(), x + r.getHeight(), r.getY());
                g.fillPath (s);
            }
        }
        const auto b = r.reduced (9.0f);
        if (on) { g.setColour (C::magenta.withAlpha (0.35f)); g.fillRoundedRectangle (b.expanded (4.0f), 6.0f); }
        g.setGradientFill (juce::ColourGradient (on ? C::magenta.brighter (0.3f) : juce::Colour (0xff2c1620), b.getTopLeft(),
                                                 on ? C::magenta.darker (0.5f) : juce::Colour (0xff120a0e), b.getBottomRight(), false));
        g.fillRoundedRectangle (b, 4.0f);
        neonText (g, label, b, Fonts::get().heavy (18.0f).withExtraKerningFactor (0.1f), on ? juce::Colours::white : C::magenta,
                  juce::Justification::centred, on ? 1.0f : 0.5f);
    }
    else
    {
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillRoundedRectangle (r, 5.0f);
        const auto b = r.reduced (4.0f);
        if (on) { g.setColour (C::magenta.withAlpha (0.3f)); g.fillRoundedRectangle (b.expanded (3.0f), 5.0f); }
        g.setColour (on ? C::magenta.withAlpha (0.85f) : C::magentaDim.withAlpha (0.5f));
        g.fillRoundedRectangle (b, 4.0f);
        neonText (g, label, b, Fonts::get().heavy (17.0f).withExtraKerningFactor (0.1f), on ? juce::Colours::white : C::magenta.withAlpha (0.9f),
                  juce::Justification::centred, on ? 1.0f : 0.3f);
    }
}

void PerformButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    if (momentary) { pressed = true; beginEdit(); setNorm (1.0f); }
    else setNormComplete (norm() > 0.5f ? 0.0f : 1.0f);
}

void PerformButton::mouseUp (const juce::MouseEvent&)
{
    if (momentary && pressed) { setNorm (0.0f); endEdit(); pressed = false; }
}

//==============================================================================
XYPad::XYPad (NeonRiotProcessor& p) : proc (p)
{
    xAxis = (int) p.apvts.state.getProperty ("xyX", 1);
    yAxis = (int) p.apvts.state.getProperty ("xyY", 6);
    setTooltip ("Drag to play two macros at once. Click an axis name to choose its macro; double-click to centre.");
    bind();
}

XYPad::~XYPad() { if (active) { ax->endGesture(); if (px != py) ay->endGesture(); } }

void XYPad::setAxes (int x, int y)
{
    xAxis = juce::jlimit (0, 7, x); yAxis = juce::jlimit (0, 7, y);
    proc.apvts.state.setProperty ("xyX", xAxis, nullptr);
    proc.apvts.state.setProperty ("xyY", yAxis, nullptr);
    if (active) { ax->endGesture(); if (px != py) ay->endGesture(); active = false; }
    bind(); repaint();
}

void XYPad::bind()
{
    px = proc.apvts.getParameter (kMacroIds[xAxis]);
    py = proc.apvts.getParameter (kMacroIds[yAxis]);
    ax = std::make_unique<juce::ParameterAttachment> (*px, [this] (float v) { vx = px->convertTo0to1 (v); repaint(); }, nullptr);
    ay = std::make_unique<juce::ParameterAttachment> (*py, [this] (float v) { vy = py->convertTo0to1 (v); repaint(); }, nullptr);
    ax->sendInitialUpdate(); ay->sendInitialUpdate();
}

static const char* macroName (int i)
{
    static const char* n[] { "WEIGHT", "BITE", "ACID", "DRIVE", "CHAOS", "MOTION", "SPACE", "OUTPUT" };
    return n[juce::jlimit (0, 7, i)];
}

void XYPad::paint (juce::Graphics& g)
{
    const auto r = pad();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (r.expanded (3.0f), 5.0f);
    g.setColour (C::display);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (C::green.withAlpha (0.07f));
    for (int i = 1; i < 8; ++i)
    {
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 8.0f), r.getY(), r.getBottom());
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 8.0f), r.getX(), r.getRight());
    }
    g.setColour (C::green.withAlpha (0.18f));
    g.drawVerticalLine ((int) r.getCentreX(), r.getY(), r.getBottom());
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    const auto pos = juce::Point<float> (r.getX() + vx * r.getWidth(), r.getBottom() - vy * r.getHeight());
    for (int k = 0; k < (int) trail.size(); ++k)
    {
        const auto& t = trail[(size_t) ((trailHead + k) % (int) trail.size())];
        if (t.isOrigin()) continue;
        g.setColour (C::green.withAlpha (0.25f * (float) k / (float) trail.size()));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (t));
    }
    g.setColour (C::green.withAlpha (active ? 0.3f : 0.15f));
    g.fillEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (pos));
    juce::Path ring; ring.addEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (pos));
    neonStroke (g, ring, C::green, 2.0f, active ? 1.3f : 0.8f);

    g.setFont (Fonts::get().label (13.0f));
    g.setColour (C::amber);
    g.drawText (juce::String ("X  ") + macroName (xAxis) + juce::String::fromUTF8 (" \xe2\x96\xbe"), r.reduced (8.0f).removeFromBottom (16.0f), juce::Justification::bottomRight, false);
    g.drawText (juce::String ("Y  ") + macroName (yAxis) + juce::String::fromUTF8 (" \xe2\x96\xbe"), r.reduced (8.0f).removeFromTop (16.0f), juce::Justification::topLeft, false);
    g.setColour (C::green.withAlpha (0.9f));
    g.setFont (Fonts::get().mono (11.5f));
    g.drawText (px->getCurrentValueAsText() + "  /  " + py->getCurrentValueAsText(), r.reduced (8.0f).removeFromBottom (16.0f),
                juce::Justification::bottomLeft, false);
}

void XYPad::apply (juce::Point<float> p)
{
    const auto r = pad();
    vx = juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / r.getWidth());
    vy = juce::jlimit (0.0f, 1.0f, (r.getBottom() - p.y) / r.getHeight());
    ax->setValueAsPartOfGesture (px->convertFrom0to1 (vx));
    if (px != py) ay->setValueAsPartOfGesture (py->convertFrom0to1 (vy));
    else vy = vx;
    trail[(size_t) trailHead] = { r.getX() + vx * r.getWidth(), r.getBottom() - vy * r.getHeight() };
    trailHead = (trailHead + 1) % (int) trail.size();
    repaint();
}

void XYPad::mouseDown (const juce::MouseEvent& e)
{
    const auto r = pad().reduced (8.0f);
    const bool onX = r.withTrimmedTop (r.getHeight() - 18.0f).withTrimmedLeft (r.getWidth() * 0.5f).contains (e.position);
    const bool onY = r.withHeight (18.0f).withWidth (r.getWidth() * 0.5f).contains (e.position);
    if (onX || onY)
    {
        juce::PopupMenu m;
        for (int i = 0; i < 7; ++i) m.addItem (i + 1, macroName (i), true, i == (onX ? xAxis : yAxis));
        juce::Component::SafePointer<XYPad> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, onX] (int res)
        {
            if (safe == nullptr || res <= 0) return;
            if (onX) safe->setAxes (res - 1, safe->yAxis); else safe->setAxes (safe->xAxis, res - 1);
        });
        return;
    }
    active = true;
    gestureX = px->getValue(); gestureY = py->getValue();
    ax->beginGesture(); if (px != py) ay->beginGesture();
    apply (e.position);
}

void XYPad::mouseDrag (const juce::MouseEvent& e) { if (active) apply (e.position); }

void XYPad::mouseUp (const juce::MouseEvent&)
{
    if (active)
    {
        ax->endGesture(); if (px != py) ay->endGesture();
        const float x = px->getValue(), y = py->getValue();
        if (x != gestureX || (px != py && y != gestureY))
        {
            proc.undoManager().beginNewTransaction ("XY macros");
            if (x != gestureX) proc.undoManager().perform (new ParamAction (*px, gestureX, x));
            if (px != py && y != gestureY) proc.undoManager().perform (new ParamAction (*py, gestureY, y));
        }
    }
    active = false;
    trail.fill ({});
    repaint();
}

void XYPad::mouseDoubleClick (const juce::MouseEvent&)
{
    ax->setValueAsCompleteGesture (px->convertFrom0to1 (px->getDefaultValue()));
    if (px != py) ay->setValueAsCompleteGesture (py->convertFrom0to1 (py->getDefaultValue()));
}
} // namespace ui
