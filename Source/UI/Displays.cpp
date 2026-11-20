#include "Displays.h"
#include "../PluginProcessor.h"

namespace ui
{
static juce::String noteName (float hz)
{
    if (hz <= 0.0f) return "--";
    const int n = juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
}

//== Scope =================================================================================
void Scope::tick (bool reduceMotion)
{
    auto& t = proc.telemetry;
    const int N = NeonRiotProcessor::Telemetry::kScope;
    const int w = t.scopeWrite.load (std::memory_order_acquire);
    for (int k = 0; k < N; ++k) buf[(size_t) k] = t.scope[(size_t) ((w + k) & (N - 1))].load (std::memory_order_relaxed);
    hz = t.noteHz.load();
    const int v = t.voices.load();
    persistence = ! reduceMotion;
    float recent = 0.0f;
    for (int k = N - 2048; k < N; ++k) recent = std::max (recent, std::abs (buf[(size_t) k]));
    if (recent < 1.0e-4f && voices == v && quiet >= (int) traces.size()) return;     // silent: nothing to redraw
    quiet = recent < 1.0e-4f ? quiet + 1 : 0;
    voices = v;

    // Trigger on a rising zero crossing so the played note's waveform stands still.
    const double sr = t.sampleRate.load();
    const float period = juce::jlimit (24.0f, 1800.0f, (float) (sr / std::max (20.0f, hz)));
    const int window = juce::jlimit (256, 2400, (int) (period * 2.5f));
    int start = N - window - 1;
    for (int k = start; k > std::max (2, start - (int) period - 2); --k)
        if (buf[(size_t) k - 1] < 0.0f && buf[(size_t) k] >= 0.0f) { start = k; break; }

    for (int k = (int) traces.size() - 1; k > 0; --k) traces[(size_t) k].swapWithPath (traces[(size_t) k - 1]);
    auto& p = traces[0];
    p.clear();
    const auto r = getLocalBounds().toFloat().reduced (4.0f, 14.0f).withTrimmedTop (4.0f);
    float peak = 0.05f;
    for (int k = 0; k < window; ++k) peak = std::max (peak, std::abs (buf[(size_t) (start + k)]));
    const float gain = juce::jlimit (1.0f, 6.0f, 0.9f / peak);       // gentle auto-range for quiet patches
    const int step = std::max (1, window / 400);
    for (int k = 0; k < window; k += step)
    {
        const float x = r.getX() + r.getWidth() * (float) k / (float) window;
        const float y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, buf[(size_t) (start + k)] * gain) * r.getHeight() * 0.47f;
        if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    repaint();
}

void Scope::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto r = b.reduced (4.0f, 14.0f).withTrimmedTop (4.0f);
    const float sc = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (bg.getWidth() != juce::roundToInt (b.getWidth() * sc))
    {
        bg = juce::Image (juce::Image::RGB, juce::roundToInt (b.getWidth() * sc), juce::roundToInt (b.getHeight() * sc), true);
        juce::Graphics bgG (bg);
        bgG.addTransform (juce::AffineTransform::scale (sc));
        bgG.fillAll (juce::Colours::black);
        drawDisplayWell (bgG, b.reduced (1.0f));
        bgG.setColour (C::green.withAlpha (0.06f));
        for (int i = 1; i < 10; ++i) bgG.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 10.0f), r.getY(), r.getBottom());
        for (int i = 1; i < 4; ++i) bgG.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());
        bgG.setColour (C::green.withAlpha (0.14f));
        bgG.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        bgG.setFont (Fonts::get().mono (11.0f));
        bgG.setColour (C::green.withAlpha (0.75f));
        bgG.drawText ("SCOPE", b.reduced (8.0f, 3.0f).removeFromTop (12.0f), juce::Justification::left, false);
    }
    g.drawImage (bg, b);

    if (persistence)
        for (int k = (int) traces.size() - 1; k >= 1; --k)
        {
            g.setColour (C::green.withAlpha (0.10f / (float) k));
            g.strokePath (traces[(size_t) k], juce::PathStrokeType (1.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        }
    g.setColour (C::green.withAlpha (0.22f));
    g.strokePath (traces[0], juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (C::green);
    g.strokePath (traces[0], juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setFont (Fonts::get().mono (11.0f));
    g.setColour (C::amber.withAlpha (0.9f));
    g.drawText (voices > 0 ? noteName (hz) + "  " + juce::String (hz, 1) + " Hz" : juce::String ("IDLE"),
                b.reduced (8.0f, 3.0f).removeFromTop (12.0f), juce::Justification::right, false);
    g.setColour (C::boneDim);
    g.drawText ("VOICES " + juce::String (voices), b.reduced (8.0f, 3.0f).removeFromBottom (12.0f), juce::Justification::right, false);
}

//== Meters ================================================================================
void Meters::tick()
{
    auto& t = proc.telemetry;
    const float before[4] { level[0], level[1], hold[0], hold[1] };
    for (int c = 0; c < 2; ++c)
    {
        const float pk = t.peak[c].exchange (0.0f, std::memory_order_relaxed);
        const float db = juce::Decibels::gainToDecibels (pk, -60.0f);
        level[c] = std::max (db, level[c] - 1.6f);                // ~48 dB/s fall at 30 fps
        if (db >= hold[c]) { hold[c] = db; holdAge[c] = 0.0f; }
        else if ((holdAge[c] += 1.0f) > 45.0f) hold[c] = std::max (-60.0f, hold[c] - 0.8f);
    }
    for (int k = 0; k < 2; ++k) level[k] = std::max (level[k], -60.0f);
    bool moved = false;
    for (int k = 0; k < 2; ++k) moved |= ! juce::exactlyEqual (before[k], level[k]) || ! juce::exactlyEqual (before[k + 2], hold[k]);
    if (moved) repaint();
}

void Meters::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.fillAll (juce::Colours::black);
    drawDisplayWell (g, b.reduced (1.0f));
    auto inner = b.reduced (6.0f, 16.0f);
    const float w = (inner.getWidth() - 4.0f) * 0.5f;
    auto yOf = [&] (float db) { return inner.getBottom() - inner.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    for (int c = 0; c < 2; ++c)
    {
        const auto col = juce::Rectangle<float> (inner.getX() + (float) c * (w + 4.0f), inner.getY(), w, inner.getHeight());
        for (float db = -60.0f; db < 0.0f; db += 3.0f)          // segmented LED ladder
        {
            const auto seg = juce::Rectangle<float> (col.getX(), yOf (db + 3.0f) + 1.0f, w, yOf (db) - yOf (db + 3.0f) - 1.5f);
            const auto colr = db >= -3.0f ? C::magenta : (db >= -12.0f ? C::amber : C::green);
            g.setColour (level[c] > db ? colr : colr.withAlpha (0.08f));
            g.fillRect (seg);
        }
        g.setColour (hold[c] > -3.0f ? C::magenta : C::bone);
        g.fillRect (juce::Rectangle<float> (col.getX(), yOf (hold[c]) - 1.0f, w, 2.0f));
    }
    g.setFont (Fonts::get().mono (10.5f));
    g.setColour (C::green.withAlpha (0.8f));
    g.drawText ("L R", b.removeFromTop (14.0f), juce::Justification::centred, false);
    const float mx = std::max (hold[0], hold[1]);
    g.setColour (mx > -3.0f ? C::magenta : C::bone);
    g.drawText (mx <= -59.0f ? juce::String ("-inf") : juce::String (mx, 1), getLocalBounds().toFloat().removeFromBottom (14.0f),
                juce::Justification::centred, false);
}

//== Flow strip ============================================================================
const juce::StringArray& FlowStrip::stageNames()
{
    static const juce::StringArray n { "OSC", "BITE", "PRE", "FILTER", "POST", "CHAOS", "FX", "ANCHOR", "OUT" };
    return n;
}

FlowStrip::FlowStrip (NeonRiotProcessor& p) : proc (p)
{
    const char* ids[] { PID::biteAmt, PID::preDrive, PID::fltReso, PID::fltEnv, PID::postDrive, PID::driveMix,
                        PID::chaosSelect, PID::chaosAmount, PID::chaosMix, PID::delayMix, PID::reverbMix,
                        PID::chorusMix, PID::shiftMix, PID::anchorAmt, PID::chaosMode, PID::chaosMorph };
    for (size_t k = 0; k < 16; ++k) idx[k] = p.parameterIndex (ids[k]);
    setTooltip ("Signal flow. Each stage lights by how hard it is working. Click a stage to go to its controls.");
}

juce::Rectangle<float> FlowStrip::box (int i) const
{
    const float w = (float) getWidth() / 9.0f;
    return juce::Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).reduced (4.0f, 5.0f);
}

void FlowStrip::tick()
{
    auto e = [this] (int k) { return proc.effectiveValue (idx[(size_t) k]); };
    std::array<float, 9> a {};
    a[0] = proc.telemetry.voices.load() > 0 ? 1.0f : 0.15f;
    a[1] = e (0);
    a[2] = e (1);
    a[3] = juce::jlimit (0.0f, 1.0f, std::max (e (2) / 1.2f, std::abs (e (3))));
    a[4] = e (4) * e (5);
    const bool chaosOn = e (14) > 0.5f ? e (15) > 0.05f : e (6) > 0.5f;
    a[5] = chaosOn ? e (7) * e (8) : 0.0f;
    a[6] = std::max ({ e (9), e (10), e (11), e (12) });
    a[7] = e (13);
    const float rms = std::max (proc.telemetry.rms[0].load(), proc.telemetry.rms[1].load());
    a[8] = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (rms, -60.0f) + 48.0f) / 42.0f);
    bool changed = false;
    for (size_t k = 0; k < 9; ++k) { const float v = activity[k] + (a[k] - activity[k]) * 0.35f;
                                     changed |= std::abs (v - activity[k]) > 0.004f; activity[k] = v; }
    if (changed) repaint();
}

void FlowStrip::paint (juce::Graphics& g)
{
    const auto& names = stageNames();
    for (int i = 0; i < 9; ++i)
    {
        const auto r = box (i);
        const auto col = i == 5 ? C::magenta : (i == 8 ? C::amber : C::green);
        const float a = activity[(size_t) i];
        if (i < 8)
        {
            const float y = r.getCentreY();
            g.setColour (col.withAlpha (0.2f + 0.5f * a));
            g.drawLine (r.getRight(), y, box (i + 1).getX(), y, 1.5f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 3.0f);
        if (a > 0.02f)
        {
            g.setColour (col.withAlpha (0.08f + 0.22f * a));
            g.fillRoundedRectangle (r, 3.0f);
        }
        g.setColour (col.withAlpha (0.25f + 0.65f * a));
        g.drawRoundedRectangle (r, 3.0f, 1.0f + a);
        const auto f = Fonts::get().label (12.5f);
        if (a > 0.3f) neonText (g, names[i], r, f, col, juce::Justification::centred, a);
        else { g.setFont (f); g.setColour (C::boneDim); g.drawText (names[i], r, juce::Justification::centred, false); }
    }
}

void FlowStrip::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < 9; ++i)
        if (box (i).contains (e.position) && onStage) onStage (i);
}

void FlowStrip::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (getLocalBounds().contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

//== Envelope view =========================================================================
EnvelopeView::EnvelopeView (NeonRiotProcessor& p, const char* a, const char* d, const char* s, const char* r, juce::Colour c)
    : proc (p), ia (p.parameterIndex (a)), id (p.parameterIndex (d)), is (p.parameterIndex (s)), ir (p.parameterIndex (r)), colour (c)
{
    setInterceptsMouseClicks (false, false);
}

void EnvelopeView::tick()
{
    const float a = proc.effectiveValue (ia), d = proc.effectiveValue (id), s = proc.effectiveValue (is), r = proc.effectiveValue (ir);
    if (! juce::exactlyEqual (a, A) || ! juce::exactlyEqual (d, D) || ! juce::exactlyEqual (s, S) || ! juce::exactlyEqual (r, R)) { A = a; D = d; S = s; R = r; repaint(); }
}

void EnvelopeView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    drawDisplayWell (g, b);
    const auto r = b.reduced (5.0f, 6.0f);
    // segment widths on a log-ish time axis so fast and slow envelopes both read
    auto w = [] (float t) { return std::log1p (t * 40.0f); };
    const float wa = w (A), wd = w (D), wr = w (R), hold = 0.9f;
    const float total = wa + wd + hold + wr;
    const float sx = r.getWidth() / std::max (total, 0.01f);
    auto y = [&] (float lvl) { return r.getBottom() - lvl * r.getHeight(); };
    juce::Path p;
    float x = r.getX();
    p.startNewSubPath (x, y (0));
    x += wa * sx; p.lineTo (x, y (1));
    const float x0 = x;
    x += wd * sx;
    p.quadraticTo (x0 + (x - x0) * 0.25f, y (S), x, y (S));
    x += hold * sx; p.lineTo (x, y (S));
    const float x1 = x;
    x += wr * sx;
    p.quadraticTo (x1 + (x - x1) * 0.25f, y (0), x, y (0));
    juce::Path fill (p);
    fill.lineTo (r.getX(), y (0)); fill.closeSubPath();
    g.setColour (colour.withAlpha (0.08f));
    g.fillPath (fill);
    neonStroke (g, p, colour, 1.5f, 0.7f);
}

//== CHAOS dial ============================================================================
static const char* kChaosNames[] { "BYPASS", "FRACTURE", "CRUSH", "TEAR", "FOLD", "SWARM", "VOID", "MELTDOWN" };

ChaosDial::ChaosDial (NeonRiotProcessor& p) : proc (p)
{
    pSelect = p.apvts.getParameter (PID::chaosSelect);
    pMode   = p.apvts.getParameter (PID::chaosMode);
    pMorph  = p.apvts.getParameter (PID::chaosMorph);
    pAmount = p.apvts.getParameter (PID::chaosAmount);
    aSelect = std::make_unique<juce::ParameterAttachment> (*pSelect, [this] (float v) { select = v; repaint(); }, nullptr);
    aMode   = std::make_unique<juce::ParameterAttachment> (*pMode,   [this] (float v) { mode = v; repaint(); }, nullptr);
    aMorph  = std::make_unique<juce::ParameterAttachment> (*pMorph,  [this] (float v) { morph = v; repaint(); }, nullptr);
    aAmount = std::make_unique<juce::ParameterAttachment> (*pAmount, [this] (float v) { amount = v; repaint(); }, nullptr);
    for (auto* a : { aSelect.get(), aMode.get(), aMorph.get(), aAmount.get() }) a->sendInitialUpdate();
    iSelect = p.parameterIndex (PID::chaosSelect); iAmount = p.parameterIndex (PID::chaosAmount); iMorph = p.parameterIndex (PID::chaosMorph);
    setTooltip ("CHAOS. Drag the knob or click a name to choose the effect; drag the outer magenta ring for intensity. "
                "Right-click for MIDI learn.");
    setTitle ("CHAOS");
}

ChaosDial::~ChaosDial()
{
    if (zone == Zone::Knob) (mode > 0.5f ? aMorph : aSelect)->endGesture();
    if (zone == Zone::Ring) aAmount->endGesture();
}

ChaosDial::Geo ChaosDial::geo() const
{
    Geo g;
    g.Ri = 0.40f * (float) getHeight();
    g.c = { (float) getWidth() * 0.5f, g.Ri + 30.0f };
    g.Rk = g.Ri - 26.0f;
    g.Rlamp = g.Ri - 13.0f;
    g.Rlab = g.Ri + 20.0f;
    return g;
}

juce::Rectangle<float> ChaosDial::labelBox (int i) const
{
    const auto gm = geo();
    const float ang = angleOf ((float) i);
    const float s = std::sin (ang), co = -std::cos (ang);
    const float w = 76.0f, h = 16.0f;
    if (std::abs (s) < 0.45f)                       // near the top: centred above the ring
        return juce::Rectangle<float> (w, h).withCentre (gm.c + juce::Point<float> (s, co) * (gm.Rlab + 5.0f));
    const auto a = gm.c + juce::Point<float> (s, co) * gm.Rlab;
    return s < 0.0f ? juce::Rectangle<float> (a.x - w, a.y - h * 0.5f, w, h)      // left side: ends at the ring
                    : juce::Rectangle<float> (a.x, a.y - h * 0.5f, w, h);         // right side: starts at the ring
}

void ChaosDial::tick (bool reduceMotion)
{
    effSelect = proc.effectiveValue (iSelect); effAmount = proc.effectiveValue (iAmount); effMorph = proc.effectiveValue (iMorph);
    const bool active = amount > 0.001f && (mode > 0.5f ? morph > 0.01f : juce::roundToInt (select) != 0);
    int gl = 4;
    if (active && ! reduceMotion)
    {
        const float rms = std::max (proc.telemetry.rms[0].load(), proc.telemetry.rms[1].load());
        gl = juce::jlimit (0, 8, (int) ((juce::Decibels::gainToDecibels (rms, -60.0f) + 42.0f) / 4.5f));
    }
    glow = gl;
    const std::array<float, 9> key { select, mode, morph, amount, effSelect, effAmount, effMorph, (float) glow, 0.0f };
    if (key != cacheKey) repaint();
}

void ChaosDial::paint (juce::Graphics& g)
{
    const float sc = g.getInternalContext().getPhysicalPixelScaleFactor();
    const std::array<float, 9> key { select, mode, morph, amount, effSelect, effAmount, effMorph, (float) glow, sc };
    const int w = juce::roundToInt ((float) getWidth() * sc), h = juce::roundToInt ((float) getHeight() * sc);
    if (key != cacheKey || cache.getWidth() != w || cache.getHeight() != h)
    {
        cache = juce::Image (juce::Image::ARGB, std::max (1, w), std::max (1, h), true);
        juce::Graphics cg (cache);
        cg.addTransform (juce::AffineTransform::scale (sc));
        render (cg);
        cacheKey = key;
    }
    g.drawImage (cache, getLocalBounds().toFloat());
}

void ChaosDial::render (juce::Graphics& g)
{
    const auto gm = geo();
    const auto c = gm.c;
    const float Ri = gm.Ri, Rk = gm.Rk;
    const float pos = position();
    const bool active = amount > 0.001f && (mode > 0.5f ? morph > 0.01f : juce::roundToInt (select) != 0);
    const float effPos = mode > 0.5f ? effMorph : effSelect;
    const float glowF = 0.75f + 0.08f * (float) glow;

    // intensity ring: groove, ticks, value, effective
    juce::Path groove;
    groove.addCentredArc (c.x, c.y, Ri, Ri, 0.0f, -2.35619f, 2.35619f, true);
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.strokePath (groove, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (int i = 0; i <= 20; ++i)
    {
        const float ang = -2.35619f + 4.71239f * (float) i / 20.0f;
        const auto d = juce::Point<float> (std::sin (ang), -std::cos (ang));
        g.setColour (C::magenta.withAlpha (i % 5 == 0 ? 0.5f : 0.2f));
        g.drawLine ({ c + d * (Ri + 7.0f), c + d * (Ri + (i % 5 == 0 ? 13.0f : 10.0f)) }, 1.0f);
    }
    if (amount > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, Ri, Ri, 0.0f, -2.35619f, -2.35619f + 4.71239f * amount, true);
        neonStroke (g, arc, C::magenta.withMultipliedAlpha (active ? std::min (1.0f, glowF) : 0.45f), 4.0f, active ? glowF + 0.3f : 0.5f);
    }
    if (std::abs (effAmount - amount) > 0.004f)
    {
        juce::Path arc;
        const float a0 = -2.35619f + 4.71239f * std::min (amount, effAmount), a1 = -2.35619f + 4.71239f * std::max (amount, effAmount);
        arc.addCentredArc (c.x, c.y, Ri + 7.5f, Ri + 7.5f, 0.0f, a0, a1, true);
        g.setColour (C::magenta.withAlpha (0.65f));
        g.strokePath (arc, juce::PathStrokeType (1.6f));
    }

    // position lamps and names
    for (int i = 0; i < 8; ++i)
    {
        const float ang = angleOf ((float) i);
        const auto d = juce::Point<float> (std::sin (ang), -std::cos (ang));
        const float lit = mode > 0.5f ? juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (pos - (float) i)) : (juce::roundToInt (pos) == i ? 1.0f : 0.0f);
        const bool effHere = std::abs (effPos - (float) i) < 0.5f && std::abs (effPos - pos) > 0.01f;
        const auto lamp = juce::Rectangle<float> (8.0f, 8.0f).withCentre (c + d * gm.Rlamp);
        g.setColour (juce::Colours::black); g.fillEllipse (lamp.expanded (2.0f));
        g.setColour (i == 0 ? C::bone.withAlpha (0.15f + 0.75f * lit) : C::magenta.withAlpha (0.12f + 0.88f * lit));
        g.fillEllipse (lamp);
        if (lit > 0.3f && i != 0) { g.setColour (C::magenta.withAlpha (0.25f * lit)); g.fillEllipse (lamp.expanded (5.0f)); }
        if (effHere) { g.setColour (C::magenta); g.drawEllipse (lamp.expanded (4.0f), 1.3f); }

        const auto box = labelBox (i);
        const float s = std::sin (ang);
        const auto just = std::abs (s) < 0.45f ? juce::Justification::centred
                        : (s < 0.0f ? juce::Justification::centredRight : juce::Justification::centredLeft);
        const auto f = Fonts::get().heavy (14.0f).withExtraKerningFactor (0.05f);
        if (lit > 0.3f) neonText (g, kChaosNames[i], box, f, i == 0 ? C::bone : C::magenta.withMultipliedAlpha (0.5f + 0.5f * lit), just, lit);
        else { g.setFont (f); g.setColour (effHere ? C::magenta.withAlpha (0.85f) : C::boneDim); g.drawText (kChaosNames[i], box, just, false); }
    }

    // the knob: heavy machined cap with a knurled skirt
    const float sc = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int px = juce::roundToInt (Rk * 2.0f * sc);
    if (cap.isNull() || cap.getWidth() != px)
    {
        cap = juce::Image (juce::Image::ARGB, px, px, true);
        juce::Graphics cg (cap);
        cg.addTransform (juce::AffineTransform::scale (sc));
        const juce::Rectangle<float> b (0, 0, Rk * 2.0f, Rk * 2.0f);
        const auto cc = b.getCentre();
        cg.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2d31), b.getTopLeft(), juce::Colour (0xff070708), b.getBottomRight(), false));
        cg.fillEllipse (b);
        for (int i = 0; i < 72; ++i)
        {
            const float ang = juce::MathConstants<float>::twoPi * (float) i / 72.0f;
            const auto d = juce::Point<float> (std::sin (ang), -std::cos (ang));
            cg.setColour (i % 2 ? juce::Colours::white.withAlpha (0.13f) : juce::Colours::black.withAlpha (0.6f));
            cg.drawLine ({ cc + d * Rk * 0.84f, cc + d * (Rk - 1.0f) }, 1.6f);
        }
        const auto face = b.reduced (Rk * 0.17f);
        cg.setColour (juce::Colours::black.withAlpha (0.7f));
        cg.fillEllipse (face.expanded (2.0f));
        cg.setGradientFill (juce::ColourGradient (juce::Colour (0xff45484f), face.getTopLeft(), juce::Colour (0xff111214), face.getBottomRight(), false));
        cg.fillEllipse (face);
        for (float rr = 0.1f; rr < 0.98f; rr += 0.06f)
        {
            cg.setColour (juce::Colours::white.withAlpha (0.022f));
            cg.drawEllipse (face.withSizeKeepingCentre (face.getWidth() * rr, face.getHeight() * rr), 0.8f);
        }
        juce::Path glint;
        glint.addCentredArc (cc.x, cc.y, face.getWidth() * 0.42f, face.getWidth() * 0.42f, 0.0f, -1.2f, -0.3f, true);
        cg.setColour (juce::Colours::white.withAlpha (0.18f));
        cg.strokePath (glint, juce::PathStrokeType (2.0f));
        cg.setColour (juce::Colours::black.withAlpha (0.8f));
        cg.drawEllipse (b.reduced (0.5f), 1.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (juce::Rectangle<float> (Rk * 2.0f, Rk * 2.0f).withCentre (c.translated (3.0f, 5.0f)));
    g.drawImage (cap, juce::Rectangle<float> (Rk * 2.0f, Rk * 2.0f).withCentre (c));

    // pointer slot
    const float ang = angleOf (pos);
    const auto d = juce::Point<float> (std::sin (ang), -std::cos (ang));
    juce::Path ptr;
    ptr.startNewSubPath (c + d * Rk * 0.25f);
    ptr.lineTo (c + d * Rk * 0.78f);
    g.setColour (juce::Colours::black);
    g.strokePath (ptr, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    neonStroke (g, ptr, active ? C::magenta : C::bone, 3.0f, active ? glowF + 0.3f : 0.3f);

    // readout in the gap below the dial
    const auto rd = juce::Rectangle<float> (170.0f, 22.0f).withCentre ({ c.x, c.y + Ri + 12.0f });
    drawDisplayWell (g, rd);
    juce::String name;
    if (mode > 0.5f)
    {
        const int lo = (int) std::floor (pos), hi = std::min (7, lo + 1);
        name = (pos - (float) lo < 0.02f || lo == hi) ? juce::String (kChaosNames[lo])
             : juce::String (kChaosNames[lo]) + ">" + kChaosNames[hi];
    }
    else name = kChaosNames[juce::jlimit (0, 7, juce::roundToInt (pos))];
    g.setFont (Fonts::get().mono (13.0f));
    g.setColour (active ? C::magenta : C::bone);
    g.drawFittedText (name, rd.reduced (8.0f, 0).toNearestInt(), juce::Justification::left, 1, 0.75f);
    g.drawText (juce::String (juce::roundToInt (amount * 100.0f)) + "%", rd.reduced (8.0f, 0), juce::Justification::right, false);
}

void ChaosDial::showMenu()
{
    juce::PopupMenu m;
    auto& learn = proc.midiLearn();
    const juce::RangedAudioParameter* params[] { pSelect, pAmount, pMorph };
    const int ids[] { iSelect, iAmount, iMorph };
    for (int k = 0; k < 3; ++k)
    {
        const int cc = learn.ccFor (ids[k]);
        m.addItem (1 + k, "MIDI Learn: " + params[k]->getName (30) + (cc >= 0 ? " (CC " + juce::String (cc) + ")" : juce::String()));
        if (cc >= 0) m.addItem (11 + k, "Forget CC " + juce::String (cc) + " (" + params[k]->getName (30) + ")");
    }
    juce::Component::SafePointer<ChaosDial> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, ids] (int r)
    {
        if (safe == nullptr) return;
        if (r >= 1 && r <= 3) safe->proc.midiLearn().startLearning (ids[r - 1]);
        if (r >= 11 && r <= 13) safe->proc.midiLearn().forget (ids[r - 11]);
    });
}

void ChaosDial::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    const auto gm = geo();
    const float dist = e.position.getDistanceFrom (gm.c);
    lastY = e.position.y;
    dragAccum = 0.0f;
    zone = Zone::None;
    for (int i = 0; i < 8; ++i)
        if (labelBox (i).expanded (4.0f).contains (e.position))
        {
            auto* param = mode > 0.5f ? pMorph : pSelect;
            const float from = param->getValue();
            (mode > 0.5f ? aMorph : aSelect)->setValueAsCompleteGesture ((float) i);
            pushUndo (proc, *param, from, param->getValue());
            return;
        }
    if (dist <= gm.Rk + 6.0f)
    {
        zone = Zone::Knob;
        if (mode > 0.5f) { gestureFrom = pMorph->getValue(); dragValue = morph; aMorph->beginGesture(); }
        else             { gestureFrom = pSelect->getValue(); dragValue = select; aSelect->beginGesture(); }
    }
    else if (dist >= gm.Ri - 10.0f && dist <= gm.Ri + 18.0f)
    {
        zone = Zone::Ring;
        gestureFrom = pAmount->getValue(); dragValue = amount;
        aAmount->beginGesture();
    }
}

void ChaosDial::mouseDrag (const juce::MouseEvent& e)
{
    const float dy = lastY - e.position.y;
    lastY = e.position.y;
    const bool fine = e.mods.isShiftDown() || e.mods.isCommandDown();
    if (zone == Zone::Knob)
    {
        if (mode > 0.5f)
        {
            dragValue = juce::jlimit (0.0f, 7.0f, dragValue + dy * 7.0f / (fine ? 1200.0f : 260.0f));
            aMorph->setValueAsPartOfGesture (dragValue);
        }
        else
        {
            dragAccum += dy;                        // detents: one position per 26 px
            const float stepPx = fine ? 60.0f : 26.0f;
            while (std::abs (dragAccum) >= stepPx)
            {
                const int dir = dragAccum > 0 ? 1 : -1;
                dragAccum -= (float) dir * stepPx;
                dragValue = juce::jlimit (0.0f, 7.0f, (float) juce::roundToInt (dragValue) + (float) dir);
                aSelect->setValueAsPartOfGesture (dragValue);
            }
        }
    }
    else if (zone == Zone::Ring)
    {
        dragValue = juce::jlimit (0.0f, 1.0f, dragValue + dy / (fine ? 1100.0f : 220.0f));
        aAmount->setValueAsPartOfGesture (dragValue);
    }
}

void ChaosDial::mouseUp (const juce::MouseEvent&)
{
    if (zone == Zone::Knob)
    {
        auto* param = mode > 0.5f ? pMorph : pSelect;
        (mode > 0.5f ? aMorph : aSelect)->endGesture();
        pushUndo (proc, *param, gestureFrom, param->getValue());
    }
    else if (zone == Zone::Ring)
    {
        aAmount->endGesture();
        pushUndo (proc, *pAmount, gestureFrom, pAmount->getValue());
    }
    zone = Zone::None;
}

void ChaosDial::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto gm = geo();
    const float dist = e.position.getDistanceFrom (gm.c);
    if (dist >= gm.Ri - 10.0f && dist <= gm.Ri + 18.0f)
    {
        const float from = pAmount->getValue();
        aAmount->setValueAsCompleteGesture (0.0f);
        pushUndo (proc, *pAmount, from, 0.0f);
    }
}
} // namespace ui
