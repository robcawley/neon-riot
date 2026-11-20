#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;   // real-world values; unlisted = default
};

const std::vector<FactoryPreset>& factoryPresets();
void applyPreset (juce::AudioProcessorValueTreeState& state, const FactoryPreset& p);
