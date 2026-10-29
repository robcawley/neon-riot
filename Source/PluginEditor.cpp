#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "UI/Chassis.h"

namespace ui
{
namespace
{
    // Materials are generated once per session and shared by every editor instance.
    const juce::Image& texture (int which)
    {
        static const juce::Image chassis = makeMaterial (Material::Chassis, 1280, 800, 7u);
        static const juce::Image plate   = makeMaterial (Material::Plate, 820, 600, 11u);
        static const juce::Image rust    = makeMaterial (Material::Chassis, 700, 520, 23u);
        return which == 0 ? chassis : (which == 1 ? plate : rust);
    }

    void drawWordmark (juce::Graphics& g)
    {
        // NEON as bent glass tubes, RIOT as a rust-eaten stencil plate
        juce::GlyphArrangement neon, riot;
        const auto f = Fonts::get().heavy (40.0f).withExtraKerningFactor (0.04f);
        neon.addLineOfText (f, "NEON", 20.0f, 41.0f);
        juce::Path pn; neon.createPath (pn);
        neonStroke (g, pn, C::green, 1.3f, 1.2f);
        const float x = 20.0f + juce::GlyphArrangement::getStringWidth (f, "NEON") + 10.0f;
        riot.addLineOfText (f, "RIOT", x, 41.0f);
        juce::Path pr; riot.createPath (pr);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (pr);
            g.setGradientFill (juce::ColourGradient (C::bone, 0, 12, C::boneDim, 0, 44, false));
            g.fillRect (pr.getBounds().expanded (2));
            g.setOpacity (0.45f);
            g.drawImage (texture (2), pr.getBounds().expanded (30), juce::RectanglePlacement::fillDestination);
            g.setOpacity (1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.9f));             // stencil bridges
            for (float bx = pr.getBounds().getX() + 9.0f; bx < pr.getBounds().getRight(); bx += 17.0f)
                g.fillRect (juce::Rectangle<float> (2.0f, 9.0f).withPosition (bx, 22.0f));
        }
        g.setFont (Fonts::get().mono (10.0f));
        g.setColour (C::amber.withAlpha (0.9f));
        const float cx = x + juce::GlyphArrangement::getStringWidth (f, "RIOT") + 14.0f;
        g.drawText ("HEAVY BASS", juce::Rectangle<float> (cx, 16.0f, 100.0f, 12.0f), juce::Justification::left, false);
        g.drawText ("SYNTHESIZER", juce::Rectangle<float> (cx, 28.0f, 100.0f, 12.0f), juce::Justification::left, false);
        g.setColour (C::boneDim);
        g.drawText ("RC-01", juce::Rectangle<float> (cx, 40.0f, 100.0f, 12.0f), juce::Justification::left, false);
    }
}

//==============================================================================
void LitButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRoundedRectangle (r, 3.0f);
    if (lit) { g.setColour (colour.withAlpha (0.14f)); g.fillRoundedRectangle (r, 3.0f); }
    g.setColour (colour.withAlpha (lit ? 0.9f : (hover ? 0.55f : 0.18f)));
    g.drawRoundedRectangle (r, 3.0f, lit ? 1.4f : 1.0f);
    const auto f = Fonts::get().heavy (14.5f).withExtraKerningFactor (0.06f);
    if (lit) neonText (g, text, r, f, colour, juce::Justification::centred, 0.7f);
    else { g.setFont (f); g.setColour (hover ? C::bone : C::boneDim); g.drawFittedText (text, r.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.7f); }
}

void PlateLayer::paint (juce::Graphics& g)
{
    const float sc = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int w = juce::roundToInt ((float) getWidth() * sc), h = juce::roundToInt ((float) getHeight() * sc);
    if (cache.getWidth() != w || cache.getHeight() != h)
    {
        cache = juce::Image (juce::Image::RGB, std::max (1, w), std::max (1, h), true);
        juce::Graphics cg (cache);
        cg.addTransform (juce::AffineTransform::scale (sc));
        if (chassis)
        {
            cg.drawImage (texture (0), getLocalBounds().toFloat(), juce::RectanglePlacement::fillDestination);
            // a darker top deck behind the wordmark and bar
            cg.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0, 0, juce::Colours::black.withAlpha (0.25f), 0, 58, false));
            cg.fillRect (0, 0, getWidth(), 58);
            cg.setColour (juce::Colours::black.withAlpha (0.8f));
            cg.drawHorizontalLine (57, 0.0f, (float) getWidth());
            drawWordmark (cg);
        }
        else
        {
            // drawers sit on a dark backing so the left bank never shows through
            cg.fillAll (C::steelDeep);
        }
        cg.addTransform (juce::AffineTransform::translation (-(float) getX(), -(float) getY()));
        for (const auto& p : plates)
            drawPlate (cg, p.r, p.style, texture (1), texture (2), p.seed);
    }
    g.drawImage (cache, getLocalBounds().toFloat());
}

void MidiMapList::tick()
{
    auto& learn = proc.midiLearn();
    if (learn.changes() == seen) return;
    seen = learn.changes();
    lines.clear();
    for (int cc = 0; cc < 120; ++cc)
        if (const int p = learn.paramForCC (cc); p >= 0)
            lines.add ("CC " + juce::String (cc).paddedLeft (' ', 3) + "  >  " + proc.parameterList()[(size_t) p]->getName (40));
    repaint();
}

void MidiMapList::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    drawDisplayWell (g, b);
    g.setFont (Fonts::get().mono (12.0f));
    if (lines.isEmpty())
    {
        g.setColour (C::boneDim);
        g.drawFittedText ("No controllers mapped.\nRight-click any control and choose MIDI Learn.", getLocalBounds().reduced (10),
                          juce::Justification::centredLeft, 3);
        return;
    }
    g.setColour (C::green);
    for (int i = 0; i < std::min (lines.size(), 7); ++i)
        g.drawText (lines[i], juce::Rectangle<float> (10.0f, 6.0f + 15.0f * (float) i, b.getWidth() - 20.0f, 15.0f), juce::Justification::left, false);
    if (lines.size() > 7)
    {
        g.setColour (C::boneDim);
        g.drawText ("+ " + juce::String (lines.size() - 7) + " more", b.reduced (10.0f, 4.0f), juce::Justification::bottomRight, false);
    }
}

//==============================================================================
template <class T, class... Args>
T* Panel::add (int pg, juce::Rectangle<int> r, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    auto* raw = c.get();
    raw->setBounds (r);
    addAndMakeVisible (raw);
    if (pg >= 0) pages[(size_t) pg].push_back (raw);
    if (auto* pc = dynamic_cast<ParamControl*> (raw)) controls.push_back (pc);
    owned.push_back (std::move (c));
    return raw;
}

Knob* Panel::knob (int pg, const char* id, const juce::String& label, Knob::Size s, int x, int y, juce::Colour c, int width)
{
    return add<Knob> (pg, { x, y, width > 0 ? width : Knob::preferredWidth (s), Knob::preferredHeight (s) }, proc, id, label, s, c);
}

void Panel::plate (int pg, juce::Rectangle<float> r, juce::String title, juce::Colour c, float rust)
{
    PlateStyle st; st.title = std::move (title); st.titleColour = c; st.rustEdge = rust;
    const uint32_t seed = (uint32_t) (r.getX() * 31.0f + r.getY() * 7.0f + 1.0f);
    (pg <= 0 ? base : drawers[(size_t) pg]).plates.push_back ({ r, st, seed });
}

Panel::Panel (NeonRiotProcessor& p) : proc (p)
{
    setSize (W, H);
    base.chassis = true;
    base.setBounds (getLocalBounds());
    addAndMakeVisible (base);
    buildMain();
    for (int k = 1; k <= 4; ++k)
    {
        drawers[(size_t) k].setBounds (10, 64, 392, 556);
        addChildComponent (drawers[(size_t) k]);
    }
    buildPages();
    buildTopBar();
    reduceMotion = (bool) p.apvts.state.getProperty ("reduceMotion", false);
    openPage (0);
}

void Panel::buildMain()
{
    using S = Knob::Size;
    // ---- left bank: sound sources and String Bite
    plate (0, { 10, 64, 392, 118 }, "OSC 1");
    knob (0, PID::osc1Wave, "WAVE", S::Medium, 22, 92);
    knob (0, PID::osc1Shape, "SHAPE", S::Medium, 88, 92);
    knob (0, PID::osc1Level, "LEVEL", S::Medium, 154, 92);
    knob (0, PID::osc1Oct, "OCT", S::Small, 228, 98, C::amber);
    knob (0, PID::osc1Semi, "SEMI", S::Small, 284, 98, C::amber);
    knob (0, PID::osc1Fine, "FINE", S::Small, 340, 98, C::amber);

    plate (0, { 10, 188, 392, 118 }, "OSC 2");
    knob (0, PID::osc2Wave, "WAVE", S::Medium, 22, 216);
    knob (0, PID::osc2Shape, "SHAPE", S::Medium, 88, 216);
    knob (0, PID::osc2Level, "LEVEL", S::Medium, 154, 216);
    knob (0, PID::osc2Oct, "OCT", S::Small, 228, 222, C::amber);
    knob (0, PID::osc2Semi, "SEMI", S::Small, 284, 222, C::amber);
    knob (0, PID::osc2Fine, "FINE", S::Small, 340, 222, C::amber);
    add<Switch> (0, { 306, 194, 88, 20 }, proc, PID::osc2Sync, "HARD SYNC");

    plate (0, { 10, 312, 392, 110 }, "SUB / NOISE");
    knob (0, PID::subLevel, "SUB", S::Medium, 22, 338);
    add<Segments> (0, { 92, 336, 108, 38 }, proc, PID::subOct, "SUB OCT", juce::StringArray { "-1", "-2" });
    add<Segments> (0, { 92, 377, 108, 38 }, proc, PID::subWave, "SUB WAVE", juce::StringArray { "SIN", "TRI", "SQR" });
    knob (0, PID::noiseLevel, "NOISE", S::Medium, 214, 338);
    knob (0, PID::noiseColour, "COLOUR", S::Medium, 284, 338);

    plate (0, { 10, 428, 392, 114 }, "STRING BITE");
    knob (0, PID::biteAmt, "BITE", S::Medium, 26, 454);
    knob (0, PID::bitePick, "PICK", S::Medium, 116, 454);
    knob (0, PID::biteComb, "STRING", S::Medium, 206, 454);
    knob (0, PID::biteTone, "TONE", S::Medium, 296, 454);

    // the articulation controls live on the front panel too (also in the VOICE drawer)
    plate (0, { 10, 548, 392, 72 }, "PLAY");
    add<Segments> (0, { 96, 556, 196, 26 }, proc, PID::voiceMode, "", juce::StringArray { "MONO", "LEGATO", "POLY" });
    add<ListSelector> (0, { 96, 588, 196, 24 }, proc, PID::pbRange, "BEND", C::amber);
    knob (0, PID::glide, "GLIDE", S::Small, 298, 549, C::amber, 50);
    knob (0, PID::drift, "DRIFT", S::Small, 346, 549, C::green, 50);

    // ---- centre: display and CHAOS
    plate (-1, { 410, 64, 460, 176 }, "SIGNAL");
    scope.setBounds (424, 92, 352, 106);  addAndMakeVisible (scope);
    meters.setBounds (784, 92, 72, 106);  addAndMakeVisible (meters);
    flow.setBounds (418, 204, 444, 30);   addAndMakeVisible (flow);
    flow.onStage = [this] (int stage) { openPage (stage == 6 ? 2 : 0); };

    plate (-1, { 410, 248, 460, 372 }, "CHAOS", C::magenta, 1.2f);
    dial.setBounds (418, 272, 444, 258);  addAndMakeVisible (dial);
    add<PerformButton> (-1, { 420, 534, 106, 78 }, proc, PID::chaosPunch, "PUNCH", true);
    add<PerformButton> (-1, { 530, 534, 94, 78 }, proc, PID::chaosHold, "HOLD", false);
    add<Segments> (-1, { 630, 536, 98, 30 }, proc, PID::chaosMode, "", juce::StringArray { "STEP", "MORPH" }, 0, C::magenta);
    add<ListSelector> (-1, { 630, 572, 98, 40 }, proc, PID::chaosDiv, "DIVISION", C::amber);
    knob (-1, PID::chaosMix, "MIX", S::Small, 732, 538, C::magenta, 44);
    knob (-1, PID::chaosHoldPitch, "PITCH", S::Small, 775, 538, C::magenta, 44);
    knob (-1, PID::chaosHoldSize, "SIZE", S::Small, 818, 538, C::magenta, 44);

    // ---- right bank: filter, envelopes, saturation
    plate (-1, { 878, 64, 392, 176 }, "FILTER");
    add<Segments> (-1, { 892, 92, 364, 24 }, proc, PID::fltMode, "", juce::StringArray { "LP24", "LP12", "BP12", "BP24", "HP12", "HP24" });
    knob (-1, PID::fltCutoff, "CUTOFF", S::Large, 892, 124);
    knob (-1, PID::fltReso, "RESONANCE", S::Large, 980, 124);
    knob (-1, PID::fltDrive, "DRIVE", S::Medium, 1070, 134);
    knob (-1, PID::fltKey, "KEY", S::Small, 1140, 140);
    knob (-1, PID::fltVel, "VEL", S::Small, 1198, 140);

    plate (-1, { 878, 244, 392, 96 }, "FILTER ENV", C::amber);
    fenv = std::make_unique<EnvelopeView> (proc, PID::fA, PID::fD, PID::fS, PID::fR, C::amber);
    fenv->setBounds (892, 272, 96, 56); addAndMakeVisible (*fenv);
    knob (-1, PID::fA, "A", S::Small, 994, 266, C::amber);
    knob (-1, PID::fD, "D", S::Small, 1046, 266, C::amber);
    knob (-1, PID::fS, "S", S::Small, 1098, 266, C::amber);
    knob (-1, PID::fR, "R", S::Small, 1150, 266, C::amber);
    knob (-1, PID::fltEnv, "AMOUNT", S::Small, 1206, 266, C::amber);

    plate (-1, { 878, 348, 392, 96 }, "AMP ENV", C::amber);
    aenv = std::make_unique<EnvelopeView> (proc, PID::aA, PID::aD, PID::aS, PID::aR, C::amber);
    aenv->setBounds (892, 376, 96, 56); addAndMakeVisible (*aenv);
    knob (-1, PID::aA, "A", S::Small, 994, 370, C::amber);
    knob (-1, PID::aD, "D", S::Small, 1046, 370, C::amber);
    knob (-1, PID::aS, "S", S::Small, 1098, 370, C::amber);
    knob (-1, PID::aR, "R", S::Small, 1150, 370, C::amber);
    knob (-1, PID::ampVel, "VELOCITY", S::Small, 1206, 370, C::amber);

    plate (-1, { 878, 452, 392, 168 }, "SATURATION");
    add<Segments> (-1, { 1100, 457, 96, 21 }, proc, PID::driveEngine, "", juce::StringArray { "M1", "M2" });
    add<Switch> (-1, { 1202, 457, 64, 21 }, proc, PID::driveComp, "COMP");
    knob (-1, PID::preDrive, "PRE", S::Small, 892, 482);
    add<Segments> (-1, { 950, 494, 250, 30 }, proc, PID::preType, "", juce::StringArray { "WARM", "ASYM", "FUZZ", "FOLD", "DIGI" });
    knob (-1, PID::preBias, "BIAS", S::Small, 1206, 482);
    knob (-1, PID::postDrive, "POST", S::Small, 892, 548);
    add<Segments> (-1, { 950, 560, 250, 30 }, proc, PID::postType, "", juce::StringArray { "WARM", "ASYM", "FUZZ", "FOLD", "DIGI" });
    knob (-1, PID::driveTone, "TONE", S::Small, 1206, 548);

    // ---- macro rail with Sub Anchor and output
    plate (-1, { 10, 630, 1260, 162 }, "MACROS", C::green, 0.8f);
    const char* names[] { "WEIGHT", "BITE", "ACID", "DRIVE", "CHAOS", "MOTION", "SPACE", "OUTPUT" };
    for (int k = 0; k < 8; ++k)
        knob (-1, kMacroIds[k], names[k], S::Macro, 24 + k * 119, 656, k == 4 ? C::magenta : (k == 7 ? C::amber : C::green));
    plate (-1, { 992, 642, 268, 140 }, "SUB ANCHOR / OUT", C::amber, 0.5f);
    knob (-1, PID::anchorAmt, "ANCHOR", S::Medium, 1004, 680);
    knob (-1, PID::anchorFreq, "X-OVER", S::Medium, 1072, 680);
    knob (-1, PID::output, "OUTPUT", S::Medium, 1176, 680, C::amber);
}

void Panel::buildPages()
{
    using S = Knob::Size;
    // ---- 1 MOD
    for (int n = 0; n < 2; ++n)
    {
        const int oy = 64 + n * 208;
        plate (1, { 10, (float) oy, 392, 200 }, n == 0 ? "LFO 1" : "LFO 2");
        add<ListSelector> (1, { 24, oy + 32, 170, 40 }, proc, n ? PID::l2Wave : PID::l1Wave, "WAVE", C::green);
        add<ListSelector> (1, { 206, oy + 32, 182, 40 }, proc, n ? PID::l2Dest : PID::l1Dest, "DESTINATION", C::green);
        knob (1, n ? PID::l2Rate : PID::l1Rate, "RATE", S::Medium, 20, oy + 82, C::amber);
        knob (1, n ? PID::l2Amt : PID::l1Amt, "AMOUNT", S::Medium, 86, oy + 82);
        knob (1, n ? PID::l2Fade : PID::l1Fade, "FADE IN", S::Medium, 152, oy + 82, C::amber);
        add<ListSelector> (1, { 226, oy + 86, 162, 40 }, proc, n ? PID::l2Div : PID::l1Div, "DIVISION", C::amber);
        add<Switch> (1, { 226, oy + 138, 80, 20 }, proc, n ? PID::l2Sync : PID::l1Sync, "SYNC", C::amber);
        add<Switch> (1, { 310, oy + 138, 86, 20 }, proc, n ? PID::l2Retrig : PID::l1Retrig, "RETRIG", C::amber);
    }
    plate (1, { 10, 480, 392, 140 }, "WHEEL / BEND");
    knob (1, PID::mwLfo, "WHL>LFO1", S::Medium, 22, 512);
    knob (1, PID::mwCutoff, "WHL>CUT", S::Medium, 92, 512);
    knob (1, PID::pbRange, "BEND", S::Medium, 162, 512, C::amber);

    // ---- 2 FX
    plate (2, { 10, 64, 392, 130 }, "CHORUS / SHIFTER");
    knob (2, PID::chorusMix, "CHORUS", S::Small, 22, 96);
    knob (2, PID::chorusRate, "RATE", S::Small, 76, 96, C::amber);
    knob (2, PID::chorusDepth, "DEPTH", S::Small, 130, 96);
    knob (2, PID::shiftMix, "SHIFT", S::Small, 214, 96);
    knob (2, PID::shiftHz, "FREQ", S::Small, 268, 96);
    knob (2, PID::shiftFeedback, "FEEDBK", S::Small, 322, 96);
    plate (2, { 10, 202, 392, 200 }, "DELAY");
    knob (2, PID::delayMix, "MIX", S::Medium, 20, 234);
    knob (2, PID::delayTime, "TIME", S::Medium, 86, 234, C::amber);
    knob (2, PID::delayFeedback, "REPEATS", S::Medium, 152, 234);
    knob (2, PID::delayTone, "TONE", S::Medium, 218, 234);
    add<ListSelector> (2, { 288, 236, 104, 40 }, proc, PID::delayDiv, "DIVISION", C::amber);
    add<Switch> (2, { 288, 290, 100, 20 }, proc, PID::delaySync, "SYNC", C::amber);
    add<Switch> (2, { 288, 316, 104, 20 }, proc, PID::delayPing, "PING PONG");
    plate (2, { 10, 410, 392, 210 }, "DARK REVERB");
    knob (2, PID::reverbMix, "MIX", S::Medium, 20, 444);
    knob (2, PID::reverbPre, "PRE-DLY", S::Medium, 86, 444, C::amber);
    knob (2, PID::reverbSize, "SIZE", S::Medium, 152, 444);
    knob (2, PID::reverbDamp, "DAMP", S::Medium, 218, 444);

    // ---- 3 VOICE
    plate (3, { 10, 64, 392, 200 }, "VOICE");
    add<Segments> (3, { 24, 94, 364, 40 }, proc, PID::voiceMode, "MODE", juce::StringArray { "MONO", "LEGATO", "POLY" });
    knob (3, PID::glide, "GLIDE", S::Medium, 20, 146, C::amber);
    knob (3, PID::drift, "DRIFT", S::Medium, 86, 146);
    knob (3, PID::instability, "UNSTABLE", S::Medium, 152, 146);
    plate (3, { 10, 272, 392, 150 }, "DRIVE DETAIL");
    add<Segments> (3, { 24, 302, 230, 40 }, proc, PID::driveEngine, "ENGINE", juce::StringArray { "M1 ORIGINAL", "M2 SATURATION" });
    knob (3, PID::driveMix, "MIX", S::Small, 22, 348);
    knob (3, PID::driveTilt, "TILT", S::Small, 78, 348);
    knob (3, PID::postBias, "POST BIAS", S::Small, 134, 348);
    plate (3, { 10, 430, 392, 190 }, "MIDI LEARN");
    midiList = std::make_unique<MidiMapList> (proc);
    midiList->setBounds (24, 460, 364, 116); addChildComponent (*midiList); pages[3].push_back (midiList.get());
    auto* forgetAll = add<LitButton> (3, { 24, 584, 130, 26 }, "FORGET ALL", C::magenta);
    forgetAll->onClick = [this] { proc.midiLearn().forgetAll(); };

    // ---- 4 PERFORM (XY)
    plate (4, { 10, 64, 392, 556 }, "PERFORM");
    xy = std::make_unique<XYPad> (proc);
    xy->setBounds (16, 90, 380, 380); addChildComponent (*xy); pages[4].push_back (xy.get());
    struct Pair { const char* name; int x, y; };
    const Pair pairs[] { { "BITE / SPACE", 1, 6 }, { "DRIVE / MOTION", 3, 5 }, { "ACID / CHAOS", 2, 4 }, { "WEIGHT / BITE", 0, 1 } };
    for (int k = 0; k < 4; ++k)
    {
        auto* b = add<LitButton> (4, { 24 + (k % 2) * 186, 482 + (k / 2) * 36, 180, 30 }, pairs[k].name, C::amber);
        const int x = pairs[k].x, y = pairs[k].y;
        b->onClick = [this, x, y] { xy->setAxes (x, y); };
    }
}

void Panel::buildTopBar()
{
    auto* prev = add<LitButton> (-1, { 380, 14, 26, 28 }, juce::String::fromUTF8 ("\xe2\x97\x82"));
    presetName = add<LitButton> (-1, { 410, 14, 250, 28 }, "", C::green);
    auto* next = add<LitButton> (-1, { 664, 14, 26, 28 }, juce::String::fromUTF8 ("\xe2\x96\xb8"));
    presetName->lit = true;
    presetName->setTooltip ("Factory presets. Use Logic's own preset menu to save your sounds.");
    prev->onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n);
                             proc.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true)); };
    next->onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % n);
                             proc.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true)); };
    presetName->onClick = [this] { showPresets(); };

    abA = add<LitButton> (-1, { 700, 14, 30, 28 }, "A", C::amber);
    abB = add<LitButton> (-1, { 734, 14, 30, 28 }, "B", C::amber);
    abA->setTooltip ("A/B compare: switch between two versions of the sound. B starts as a copy of A.");
    abB->setTooltip (abA->getTooltip());
    abA->onClick = [this] { switchAB (0); };
    abB->onClick = [this] { switchAB (1); };
    abA->setLit (proc.abActive == 0); abB->setLit (proc.abActive == 1);

    undoB = add<LitButton> (-1, { 780, 14, 54, 28 }, "UNDO", C::amber);
    redoB = add<LitButton> (-1, { 838, 14, 54, 28 }, "REDO", C::amber);
    undoB->onClick = [this] { proc.undoManager().undo(); };
    redoB->onClick = [this] { proc.undoManager().redo(); };
    undoB->setTooltip ("Undo the last change made in this window (Cmd-Z).");
    redoB->setTooltip ("Redo (Shift-Cmd-Z).");

    const char* tabNames[] { "MOD", "FX", "VOICE", "XY" };
    const int tx[] { 904, 966, 1014, 1084 }, tw[] { 58, 44, 66, 44 };
    for (int k = 0; k < 4; ++k)
    {
        auto* t = add<LitButton> (-1, { tx[k], 14, tw[k], 28 }, tabNames[k], C::green);
        t->onClick = [this, k] { openPage (page == k + 1 ? 0 : k + 1); };
        tabs.push_back (t);
    }
    tabs[0]->setTooltip ("LFOs, fade-in, retrigger, mod wheel and bend.");
    tabs[1]->setTooltip ("Chorus, frequency shifter, delay and dark reverb.");
    tabs[2]->setTooltip ("Voice mode, glide, drift, drive detail and MIDI learn mappings.");
    tabs[3]->setTooltip ("XY performance pad: play two macros at once.");

    auto* setup = add<LitButton> (-1, { 1140, 14, 70, 28 }, "SETUP", C::amber);
    setup->onClick = [this] { showSettings(); };
    clip = add<LitButton> (-1, { 1216, 14, 50, 28 }, "CLIP", C::magenta);
    clip->setTooltip ("Lights if the output reached 0 dBFS. Click to reset.");
    clip->onClick = [this] { proc.telemetry.clipped.store (false); clip->setLit (false); };
}

void Panel::openPage (int pg)
{
    page = pg;
    for (int k = 1; k <= 4; ++k)
    {
        drawers[(size_t) k].setVisible (k == pg);
        for (auto* c : pages[(size_t) k]) c->setVisible (k == pg);
    }
    for (auto* c : pages[0]) c->setVisible (pg == 0);
    for (int k = 0; k < (int) tabs.size(); ++k) tabs[(size_t) k]->setLit (k + 1 == pg);
}

void Panel::showPresets()
{
    juce::PopupMenu m;
    const int cur = proc.getCurrentProgram();
    for (int i = 0; i < proc.getNumPrograms(); ++i) m.addItem (i + 1, proc.getProgramName (i), true, i == cur);
    juce::Component::SafePointer<Panel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetName).withMaximumNumColumns (2), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        safe->proc.setCurrentProgram (r - 1);
        safe->proc.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true));
    });
}

void Panel::switchAB (int slot)
{
    if (slot == proc.abActive) return;
    proc.getStateInformation (proc.abSlot[(size_t) proc.abActive]);
    auto& target = proc.abSlot[(size_t) slot];
    if (target.isEmpty()) target = proc.abSlot[(size_t) proc.abActive];
    else proc.setStateInformation (target.getData(), (int) target.getSize());
    proc.abActive = slot;
    abA->setLit (slot == 0); abB->setLit (slot == 1);
}

void Panel::showSettings()
{
    juce::PopupMenu m, sizes;
    const float cur = (float) proc.apvts.state.getProperty ("uiScale", 1.0f);
    const float scales[] { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    for (int i = 0; i < 5; ++i)
        sizes.addItem (10 + i, juce::String (juce::roundToInt (scales[i] * 100)) + " %", true, std::abs (cur - scales[i]) < 0.01f);
    m.addSubMenu ("Interface size", sizes);
    m.addItem (2, "Reduce motion", true, reduceMotion);
    m.addItem (3, "Show all parameters (debug)");
    m.addSeparator();
    m.addItem (4, "Fonts: Rajdhani, Share Tech Mono (SIL Open Font License)", false);
    juce::Component::SafePointer<Panel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, scales] (int r)
    {
        if (safe == nullptr) return;
        if (r >= 10 && r < 15 && safe->onScale) safe->onScale (scales[r - 10]);
        if (r == 2) { safe->reduceMotion = ! safe->reduceMotion; safe->proc.apvts.state.setProperty ("reduceMotion", safe->reduceMotion, nullptr); }
        if (r == 3) safe->showAllParameters (true);
    });
}

void Panel::showAllParameters (bool show)
{
    if (! show) { genericClose.reset(); genericView.reset(); generic.reset(); return; }
    generic = std::make_unique<juce::GenericAudioProcessorEditor> (proc);
    genericView = std::make_unique<juce::Viewport>();
    genericView->setViewedComponent (generic.get(), false);
    genericView->setBounds (10, 64, 1260, 728);
    generic->setSize (1240, generic->getHeight());
    addAndMakeVisible (*genericView);
    genericClose = std::make_unique<LitButton> ("CLOSE", C::magenta);
    genericClose->setBounds (1180, 70, 80, 26);
    genericClose->onClick = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Panel> (this)]
                                     { if (safe != nullptr) safe->showAllParameters (false); }); };
    addAndMakeVisible (*genericClose);
}

void Panel::tick()
{
    for (auto* c : controls) if (c->isShowing()) c->refreshEffective();
    scope.tick (reduceMotion);
    meters.tick();
    flow.tick();
    dial.tick (reduceMotion);
    fenv->tick(); aenv->tick();
    if (midiList->isShowing()) midiList->tick();
    clip->setLit (proc.telemetry.clipped.load());
    undoB->setLit (proc.undoManager().canUndo());
    redoB->setLit (proc.undoManager().canRedo());
    if (proc.getCurrentProgram() != lastProgram || presetName->text.isEmpty())
    {
        lastProgram = proc.getCurrentProgram();
        presetName->text = juce::String (lastProgram + 1).paddedLeft ('0', 2) + "  " + proc.getProgramName (lastProgram).toUpperCase();
        presetName->repaint();
    }
}
} // namespace ui

//==============================================================================
NeonRiotEditor::NeonRiotEditor (NeonRiotProcessor& p)
    : AudioProcessorEditor (p), proc (p), panel (p)
{
    addAndMakeVisible (panel);
    panel.onScale = [this] (float s)
    {
        proc.apvts.state.setProperty ("uiScale", s, nullptr);
        setSize (juce::roundToInt (ui::Panel::W * s), juce::roundToInt (ui::Panel::H * s));
    };
    const float s = juce::jlimit (0.75f, 2.0f, (float) p.apvts.state.getProperty ("uiScale", 1.0f));
    setResizable (true, true);
    setResizeLimits (960, 600, 2560, 1600);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) ui::Panel::W / ui::Panel::H);
    setSize (juce::roundToInt (ui::Panel::W * s), juce::roundToInt (ui::Panel::H * s));
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

NeonRiotEditor::~NeonRiotEditor() { stopTimer(); }

void NeonRiotEditor::resized()
{
    const float s = (float) getWidth() / (float) ui::Panel::W;
    panel.setTransform (juce::AffineTransform::scale (s));
    panel.setBounds (0, 0, ui::Panel::W, ui::Panel::H);
}

bool NeonRiotEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'Z')
    {
        if (k.getModifiers().isShiftDown()) proc.undoManager().redo(); else proc.undoManager().undo();
        return true;
    }
    return false;
}

void NeonRiotEditor::timerCallback() { panel.tick(); }
