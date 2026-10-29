#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
// Palette. Acid green = primary activity, amber = envelopes/timing/secondary,
// magenta = CHAOS, PUNCH/HOLD and warnings only.
namespace C
{
    inline const juce::Colour steelDeep   { 0xff0c0d0f };
    inline const juce::Colour steel       { 0xff17181b };
    inline const juce::Colour steelLight  { 0xff2a2c31 };
    inline const juce::Colour plate       { 0xff131417 };
    inline const juce::Colour display     { 0xff040705 };
    inline const juce::Colour rustDark    { 0xff3e1c0d };
    inline const juce::Colour rust        { 0xff8a3b14 };
    inline const juce::Colour rustBright  { 0xffb5561c };
    inline const juce::Colour green       { 0xff9dff3a };
    inline const juce::Colour greenDim    { 0xff3f6a1c };
    inline const juce::Colour amber       { 0xffffb000 };
    inline const juce::Colour amberDim    { 0xff6b4a08 };
    inline const juce::Colour magenta     { 0xffff2e88 };
    inline const juce::Colour magentaDim  { 0xff5c1232 };
    inline const juce::Colour bone        { 0xffe4ddcd };   // labels: >= 7:1 on plate
    inline const juce::Colour boneDim     { 0xff9a9488 };   // secondary labels: >= 4.5:1 on plate
}

class Fonts
{
public:
    static Fonts& get();
    juce::Font label (float h) const   { return juce::Font (juce::FontOptions (semibold).withHeight (h)); }
    juce::Font heavy (float h) const   { return juce::Font (juce::FontOptions (bold).withHeight (h)); }
    juce::Font medium (float h) const  { return juce::Font (juce::FontOptions (med).withHeight (h)); }
    juce::Font mono (float h) const    { return juce::Font (juce::FontOptions (monoFace).withHeight (h)); }
private:
    Fonts();
    juce::Typeface::Ptr semibold, bold, med, monoFace;
};

// Draws a neon stroke: a wide faint bloom, then the bright core.
void neonStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float glow = 1.0f);
void neonText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Font f,
               juce::Colour c, juce::Justification j, float glow = 1.0f);
} // namespace ui
