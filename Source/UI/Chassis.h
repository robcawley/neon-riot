#pragma once
#include "Theme.h"

namespace ui
{
// Procedural materials. Everything here is generated from noise, never loaded from files.
struct Noise
{
    uint32_t seed = 1;
    float lattice (int x, int y) const
    {
        uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float) ((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
    }
    float value (float x, float y) const
    {
        const int xi = (int) std::floor (x), yi = (int) std::floor (y);
        float fx = x - (float) xi, fy = y - (float) yi;
        fx = fx * fx * (3.0f - 2.0f * fx); fy = fy * fy * (3.0f - 2.0f * fy);
        const float a = lattice (xi, yi), b = lattice (xi + 1, yi), c = lattice (xi, yi + 1), d = lattice (xi + 1, yi + 1);
        return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
    }
    float fbm (float x, float y, int octaves) const
    {
        float sum = 0, amp = 0.5f, norm = 0;
        for (int o = 0; o < octaves; ++o) { sum += amp * value (x, y); norm += amp; x *= 2.03f; y *= 2.03f; amp *= 0.5f; }
        return sum / norm;
    }
};

enum class Material { Chassis, Plate, Display };

// Material texture at logical size w x h (generated once, then scaled into the cached panel).
juce::Image makeMaterial (Material m, int w, int h, uint32_t seed);

struct PlateStyle
{
    juce::String title;                // stencil label; empty = none
    juce::Colour titleColour = C::amber;
    bool screws = true;
    float rustEdge = 1.0f;             // 0..1 how eaten the edges are
};

// Draws a bolted steel plate onto the static panel layer.
void drawPlate (juce::Graphics& g, juce::Rectangle<float> r, const PlateStyle& s,
                const juce::Image& plateTex, const juce::Image& rustTex, uint32_t seed);
// A recessed display window (CRT-style), for values and scopes.
void drawDisplayWell (juce::Graphics& g, juce::Rectangle<float> r);
void drawScrew (juce::Graphics& g, juce::Point<float> c, float radius, float angle);
} // namespace ui
