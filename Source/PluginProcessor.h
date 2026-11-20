#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "DSP/Engine.h"
#include "Macros.h"
#include "MidiLearn.h"
#include <string_view>

class NeonRiotProcessor : public juce::AudioProcessor
{
public:
    NeonRiotProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NEON RIOT"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 110.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // For the editor: parameter order, live effective (macro-modulated) values, MIDI learn.
    const std::vector<juce::RangedAudioParameter*>& parameterList() const { return plist; }
    int parameterIndex (const juce::String& id) const;
    float effectiveValue (int index) const { return effective[(size_t) index].load (std::memory_order_relaxed); }
    MidiLearn& midiLearn() { return learn; }
    juce::UndoManager& undoManager() { return undo; }
    std::array<juce::MemoryBlock, 2> abSlot;   // A/B compare (editor-driven, message thread only)
    int abActive = 0;

    // Audio -> UI telemetry: atomics only, written once per block.
    struct Telemetry
    {
        static constexpr int kScope = 4096;                 // power of two
        std::array<std::atomic<float>, kScope> scope {};
        std::atomic<int> scopeWrite { 0 };
        std::atomic<float> peak[2] {}, rms[2] {};
        std::atomic<float> noteHz { 65.4f };
        std::atomic<int> voices { 0 };
        std::atomic<bool> clipped { false };
        std::atomic<double> sampleRate { 48000.0 };
    } telemetry;

private:
    void snapshot (int numSamples);
    nr::Block readBlock() const;
    float get (const char* id) const;
    struct ParameterHash
    {
        using is_transparent = void;
        size_t operator() (std::string_view s) const noexcept { return std::hash<std::string_view> {} (s); }
    };
    std::unordered_map<std::string, int, ParameterHash, std::equal_to<>> idIndex;
    std::vector<juce::RangedAudioParameter*> plist;
    std::vector<std::atomic<float>*> rawList;
    std::vector<float> vals, lo, hi;
    std::unique_ptr<std::atomic<float>[]> effective;
    std::array<int, 8> macroIndex {};
    std::array<float, 8> macroSmooth {};
    bool macrosPrimed = false;
    double currentSampleRate = 48000.0;
    MacroMap macros;
    MidiLearn learn;
    juce::UndoManager undo { 30000, 30 };
    void publish (const juce::AudioBuffer<float>& buffer);

    nr::Engine engine;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeonRiotProcessor)
};
