#include "Chassis.h"

namespace ui
{
static inline float smooth (float e0, float e1, float x)
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

juce::Image makeMaterial (Material m, int w, int h, uint32_t seed)
{
    juce::Image img (juce::Image::RGB, w, h, false);
    juce::Image::BitmapData data (img, juce::Image::BitmapData::writeOnly);
    Noise n { seed }, n2 { seed * 7u + 3u }, n3 { seed * 13u + 5u };

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float fx = (float) x, fy = (float) y;
            float r, g, b;
            if (m == Material::Display)
            {
                const float grain = n.value (fx * 0.9f, fy * 0.9f) * 0.012f;
                const float scan = (y % 3 == 0) ? -0.006f : 0.0f;
                r = 0.012f + grain; g = 0.022f + grain * 1.4f + scan; b = 0.016f + grain;
            }
            else
            {
                // blackened steel: low variation, horizontal brushing, fine pitting
                const float mottled = n.fbm (fx / 140.0f, fy / 140.0f, 4);
                const float brushed = n2.value (fx / 260.0f, fy * 0.7f) * 0.5f + n2.value (fx / 37.0f, fy * 1.3f) * 0.5f;
                const float pits = smooth (0.82f, 0.95f, n3.value (fx / 2.2f, fy / 2.2f));
                const float base = (m == Material::Plate ? 0.060f : 0.085f) + 0.030f * mottled + 0.018f * brushed - 0.030f * pits;
                r = base; g = base * 1.01f; b = base * 1.08f;

                // rust: low-frequency blooms, with streaks running down
                const float bloom = n.fbm (fx / 170.0f + 11.0f, fy / 170.0f, 5) + 0.22f * n3.fbm (fx / 35.0f, fy / 35.0f, 3);
                const float streak = n2.value (fx / 5.0f, fy / 140.0f);
                float rustAmt = m == Material::Plate
                    ? smooth (0.74f, 0.86f, bloom) * 0.55f + smooth (0.9f, 0.97f, n3.value (fx / 9.0f, fy / 9.0f)) * 0.35f
                    : smooth (0.60f, 0.78f, bloom) * 0.9f + smooth (0.62f, 0.9f, streak) * smooth (0.45f, 0.6f, bloom) * 0.5f;
                rustAmt = juce::jlimit (0.0f, 1.0f, rustAmt);
                const float rv = n3.fbm (fx / 11.0f, fy / 11.0f, 3);
                const float rr = 0.22f + 0.42f * rv, rg = 0.085f + 0.17f * rv * rv, rb = 0.03f + 0.04f * rv;
                const float pitDark = 1.0f - 0.45f * smooth (0.7f, 0.92f, n.value (fx / 3.0f, fy / 3.0f));
                r = r + (rr * pitDark - r) * rustAmt;
                g = g + (rg * pitDark - g) * rustAmt;
                b = b + (rb * pitDark - b) * rustAmt;
            }
            data.setPixelColour (x, y, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, r),
                                                                    juce::jlimit (0.0f, 1.0f, g),
                                                                    juce::jlimit (0.0f, 1.0f, b), 1.0f));
        }
    return img;
}

void drawScrew (juce::Graphics& g, juce::Point<float> c, float radius, float angle)
{
    // rust halo
    g.setGradientFill (juce::ColourGradient (C::rust.withAlpha (0.55f), c, C::rust.withAlpha (0.0f), c.translated (radius * 2.4f, 0), true));
    g.fillEllipse (juce::Rectangle<float> (radius * 4.8f, radius * 4.8f).withCentre (c));
    // countersink
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.5f, radius * 2.5f).withCentre (c));
    g.setGradientFill (juce::ColourGradient (C::steelLight.brighter (0.35f), c.translated (-radius, -radius),
                                             C::steelDeep, c.translated (radius, radius), false));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c));
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    const auto d = juce::Point<float> (std::cos (angle), std::sin (angle)) * radius * 0.8f;
    g.drawLine ({ c - d, c + d }, radius * 0.36f);
}

void drawPlate (juce::Graphics& g, juce::Rectangle<float> r, const PlateStyle& s,
                const juce::Image& plateTex, const juce::Image& rustTex, uint32_t seed)
{
    Noise n { seed };
    juce::Path outline;
    outline.addRoundedRectangle (r, 5.0f);

    // drop shadow onto the chassis
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (r.translated (2.0f, 3.0f).expanded (1.0f), 6.0f);

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (outline);
        g.drawImage (plateTex, r, juce::RectanglePlacement::fillDestination);

        // rust eats in from the edges: a ring of the chassis rust, masked by noise
        if (s.rustEdge > 0.0f)
        {
            juce::Path ring;
            ring.addRoundedRectangle (r, 5.0f);
            ring.addRoundedRectangle (r.reduced (9.0f), 3.0f);
            ring.setUsingNonZeroWinding (false);
            g.reduceClipRegion (ring);
            g.setOpacity (0.55f * s.rustEdge);
            g.drawImage (rustTex, r.translated ((float) (n.lattice (1, 2) * 300), (float) (n.lattice (3, 4) * 200)),
                         juce::RectanglePlacement::fillDestination);
            g.setOpacity (1.0f);
        }
    }
    // bevel
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawLine (r.getX() + 5, r.getY() + 0.5f, r.getRight() - 5, r.getY() + 0.5f, 1.0f);
    g.drawLine (r.getX() + 0.5f, r.getY() + 5, r.getX() + 0.5f, r.getBottom() - 5, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 5.0f, 1.2f);

    if (s.screws)
    {
        const float in = 9.0f;
        int k = 0;
        for (auto p : { r.getTopLeft().translated (in, in), r.getTopRight().translated (-in, in),
                        r.getBottomLeft().translated (in, -in), r.getBottomRight().translated (-in, -in) })
        {
            // oxide streak running down from each lower-side fixing
            if (k++ < 2)
            {
                g.setGradientFill (juce::ColourGradient (C::rust.withAlpha (0.28f), p, C::rust.withAlpha (0.0f),
                                                         p.translated (0, 38.0f + 30.0f * n.lattice (k, 9)), false));
                g.fillRect (juce::Rectangle<float> (2.2f, 70.0f).withPosition (p.x - 1.1f + (n.lattice (k, 5) - 0.5f) * 2.0f, p.y));
            }
            drawScrew (g, p, 3.4f, n.lattice (k, 7) * 3.1f);
        }
    }

    if (s.title.isNotEmpty())
    {
        // stencil-cut title: dark slot with lit text
        const auto f = Fonts::get().heavy (13.0f).withExtraKerningFactor (0.12f);
        const float tw = juce::GlyphArrangement::getStringWidth (f, s.title) + 20.0f;
        auto slot = juce::Rectangle<float> (tw, 18.0f).withPosition (r.getX() + 20.0f, r.getY() + 7.0f);
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.fillRoundedRectangle (slot, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (slot, 2.0f, 1.0f);
        neonText (g, s.title, slot, f, s.titleColour, juce::Justification::centred, 0.6f);
    }
}

void drawDisplayWell (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (r.expanded (2.0f), 4.0f);
    g.setColour (C::display);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawLine (r.getX() + 3, r.getBottom() + 1.5f, r.getRight() - 3, r.getBottom() + 1.5f, 1.0f);
}
} // namespace ui
