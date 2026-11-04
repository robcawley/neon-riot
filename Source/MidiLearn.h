#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

// MIDI learn. The audio thread binds and applies CCs with atomics only; the host is told
// about learned moves on the message thread, through a lock-free FIFO, so automation records.
class MidiLearn : private juce::Timer
{
public:
    static constexpr int kNoParam = -1;

    MidiLearn() { for (auto& a : ccToParam) a.store (kNoParam); }
    ~MidiLearn() override { stopTimer(); }

    void attach (std::vector<juce::RangedAudioParameter*> paramsIn, std::vector<std::atomic<float>*> rawIn)
    {
        params = std::move (paramsIn);
        raw = std::move (rawIn);
        startTimerHz (60);
    }

    //== message thread =====================================================================
    void startLearning (int paramIndex) { learnTarget.store (paramIndex); }
    void cancelLearning()               { learnTarget.store (kNoParam); }
    int  learningIndex() const          { return learnTarget.load(); }
    void forget (int paramIndex)
    {
        for (auto& a : ccToParam) { int p = paramIndex; a.compare_exchange_strong (p, kNoParam); }
        changeCount.fetch_add (1);
    }
    void forgetAll() { for (auto& a : ccToParam) a.store (kNoParam); changeCount.fetch_add (1); }
    int ccFor (int paramIndex) const
    {
        for (int cc = 0; cc < 128; ++cc) if (ccToParam[(size_t) cc].load() == paramIndex) return cc;
        return -1;
    }
    int  paramForCC (int cc) const { return ccToParam[(size_t) cc].load(); }
    uint32_t changes() const       { return changeCount.load(); }

    // Move queued learned values to the host. Called by the timer; tests call it directly.
    void dispatch()
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);
        auto send = [this] (int start, int size)
        {
            for (int k = start; k < start + size; ++k)
                if (auto* p = juce::isPositiveAndBelow (queueParam[(size_t) k], (int) params.size())
                                  ? params[(size_t) queueParam[(size_t) k]] : nullptr)
                    if (std::abs (p->getValue() - queueValue[(size_t) k]) > 1.0e-6f)
                        p->setValueNotifyingHost (queueValue[(size_t) k]);
        };
        send (start1, size1);
        send (start2, size2);
        fifo.finishedRead (size1 + size2);
    }

    void save (juce::ValueTree& state) const
    {
        auto map = state.getOrCreateChildWithName ("MIDI_LEARN", nullptr);
        map.removeAllChildren (nullptr);
        for (int cc = 0; cc < 128; ++cc)
            if (const int p = ccToParam[(size_t) cc].load(); juce::isPositiveAndBelow (p, (int) params.size()))
            {
                juce::ValueTree e ("MAP");
                e.setProperty ("cc", cc, nullptr);
                e.setProperty ("param", params[(size_t) p]->getParameterID(), nullptr);
                map.appendChild (e, nullptr);
            }
    }

    void load (const juce::ValueTree& state)
    {
        for (auto& a : ccToParam) a.store (kNoParam);
        const auto map = state.getChildWithName ("MIDI_LEARN");
        for (const auto& e : map)
        {
            const int cc = (int) e.getProperty ("cc", -1);
            const auto id = e.getProperty ("param").toString();
            for (int p = 0; p < (int) params.size(); ++p)
                if (juce::isPositiveAndBelow (cc, 120) && params[(size_t) p]->getParameterID() == id)
                    ccToParam[(size_t) cc].store (p);
        }
        changeCount.fetch_add (1);
    }

    //== audio thread =======================================================================
    // Returns true if the CC was bound or consumed by a mapping (it still reaches the engine).
    void handleController (int cc, int value) noexcept
    {
        if (! juce::isPositiveAndBelow (cc, 120)) return;      // 120+ are channel-mode messages
        if (const int target = learnTarget.load(); target != kNoParam)
        {
            for (auto& a : ccToParam) { int p = target; a.compare_exchange_strong (p, kNoParam); }
            ccToParam[(size_t) cc].store (target);
            learnTarget.store (kNoParam);
            changeCount.fetch_add (1);
        }
        const int p = ccToParam[(size_t) cc].load();
        if (! juce::isPositiveAndBelow (p, (int) params.size())) return;

        const float norm = (float) value / 127.0f;
        auto* param = params[(size_t) p];
        // the engine hears it from the next block on; the host hears it via the FIFO
        raw[(size_t) p]->store (param->getNormalisableRange().snapToLegalValue (param->convertFrom0to1 (norm)));
        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);
        if (size1 + size2 > 0)
        {
            const int k = size1 > 0 ? start1 : start2;
            queueParam[(size_t) k] = p;
            queueValue[(size_t) k] = norm;
            fifo.finishedWrite (1);
        }
    }

private:
    void timerCallback() override { dispatch(); }

    std::vector<juce::RangedAudioParameter*> params;
    std::vector<std::atomic<float>*> raw;
    std::array<std::atomic<int>, 128> ccToParam;
    std::atomic<int> learnTarget { kNoParam };
    std::atomic<uint32_t> changeCount { 0 };

    static constexpr int kQueue = 1024;
    juce::AbstractFifo fifo { kQueue };
    std::array<int, kQueue> queueParam {};
    std::array<float, kQueue> queueValue {};
};
