#include "Theme.h"
#include "BinaryData.h"

namespace ui
{
Fonts& Fonts::get() { static Fonts f; return f; }

Fonts::Fonts()
{
    semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::RajdhaniSemiBold_ttf, BinaryData::RajdhaniSemiBold_ttfSize);
    bold     = juce::Typeface::createSystemTypefaceFor (BinaryData::RajdhaniBold_ttf, BinaryData::RajdhaniBold_ttfSize);
    med      = juce::Typeface::createSystemTypefaceFor (BinaryData::RajdhaniMedium_ttf, BinaryData::RajdhaniMedium_ttfSize);
    monoFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ShareTechMonoRegular_ttf, BinaryData::ShareTechMonoRegular_ttfSize);
}

void neonStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float glow)
{
    if (glow > 0.0f)
    {
        g.setColour (c.withAlpha (0.10f * glow));
        g.strokePath (p, juce::PathStrokeType (width * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.22f * glow));
        g.strokePath (p, juce::PathStrokeType (width * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c.interpolatedWith (juce::Colours::white, 0.45f).withAlpha (0.8f));
    g.strokePath (p, juce::PathStrokeType (std::max (0.6f, width * 0.35f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void neonText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Font f,
               juce::Colour c, juce::Justification j, float glow)
{
    g.setFont (f);
    if (glow > 0.0f)
    {
        g.setColour (c.withAlpha (0.18f * glow));
        for (auto [dx, dy] : { std::pair { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
            g.drawText (t, r.translated (dx, dy), j, false);
    }
    g.setColour (c);
    g.drawText (t, r, j, false);
}
} // namespace ui
