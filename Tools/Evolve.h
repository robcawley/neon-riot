#pragma once
#include <juce_core/juce_core.h>
namespace nrtest
{
int evolve (const juce::File& directory);
int evolveChecks (const juce::File& directory);
int evolveCpu();
}
