#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

NeonRiotProcessor::NeonRiotProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NEON_RIOT", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            idIndex[rp->getParameterID().toStdString()] = (int) plist.size();
            plist.push_back (rp);
            rawList.push_back (apvts.getRawParameterValue (rp->getParameterID()));
            lo.push_back (rp->getNormalisableRange().start);
            hi.push_back (rp->getNormalisableRange().end);
        }
    vals.assign (plist.size(), 0.0f);
    effective = std::make_unique<std::atomic<float>[]> (plist.size());
    for (size_t k = 0; k < plist.size(); ++k)
        effective[k].store (rawList[k]->load());
    for (int k = 0; k < 8; ++k) macroIndex[(size_t) k] = parameterIndex (kMacroIds[k]);
    macros.bind ([this] (const char* id) { return parameterIndex (id); });
    learn.attach (plist, rawList);
}

int NeonRiotProcessor::parameterIndex (const juce::String& id) const
{
    const auto it = idIndex.find (id.toStdString());
    jassert (it != idIndex.end());
    return it == idIndex.end() ? 0 : it->second;
}

// Snapshot every parameter, then lay the smoothed macro offsets over the copy.
void NeonRiotProcessor::snapshot (int numSamples)
{
    const size_t n = plist.size();
    for (size_t k = 0; k < n; ++k) vals[k] = rawList[k]->load (std::memory_order_relaxed);

    const float coef = 1.0f - std::exp (-(float) numSamples / (0.025f * (float) currentSampleRate));
    std::array<float, 8> m {};
    bool any = false;
    for (size_t k = 0; k < 8; ++k)
    {
        const float target = vals[(size_t) macroIndex[k]];
        float& sm = macroSmooth[k];
        sm = macrosPrimed ? sm + (target - sm) * coef : target;
        if (std::abs (target - sm) < 1.0e-4f) sm = target;    // lands exactly, so neutral is exact
        m[k] = sm;
        any = any || sm != 0.0f;
    }
    macrosPrimed = true;
    if (any) macros.apply (vals.data(), lo.data(), hi.data(), m.data());

    for (size_t k = 0; k < n; ++k) effective[k].store (vals[k], std::memory_order_relaxed);
}

bool NeonRiotProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void NeonRiotProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    telemetry.sampleRate.store (sampleRate);
    macrosPrimed = false;
    engine.prepare (sampleRate, std::max (samplesPerBlock, 32));
    setLatencySamples (engine.latencySamples());
}

float NeonRiotProcessor::get (const char* id) const
{
    const auto it = idIndex.find (id);
    jassert (it != idIndex.end());
    return vals[(size_t) it->second];
}

nr::Block NeonRiotProcessor::readBlock() const
{
    using namespace PID;
    nr::Block b {};
    auto i = [this] (const char* id) { return juce::roundToInt (get (id)); };
    b.osc1Wave = get (osc1Wave); b.osc1Shape = get (osc1Shape); b.osc1Level = get (osc1Level);
    b.osc1Pitch = (float) (i (osc1Oct) * 12 + i (osc1Semi)) + get (osc1Fine) / 100.0f;
    b.osc2Wave = get (osc2Wave); b.osc2Shape = get (osc2Shape); b.osc2Level = get (osc2Level);
    b.osc2Pitch = (float) (i (osc2Oct) * 12 + i (osc2Semi)) + get (osc2Fine) / 100.0f;
    b.osc2Sync = get (osc2Sync) > 0.5f;
    b.subLevel = get (subLevel); b.subDiv = i (subOct) == 0 ? 0.5f : 0.25f; b.subWave = i (subWave);
    b.noiseLevel = get (noiseLevel); b.noiseColour = get (noiseColour);
    b.voiceMode = i (voiceMode); b.glide = get (glide); b.pbRange = get (pbRange);
    b.drift = get (drift); b.instability = get (instability);
    b.fltMode = i (fltMode); b.cutoff = get (fltCutoff); b.reso = get (fltReso); b.fltEnv = get (fltEnv);
    b.fltKey = get (fltKey); b.fltVel = get (fltVel); b.fltDrive = get (fltDrive);
    b.fA = get (fA); b.fD = get (fD); b.fS = get (fS); b.fR = get (fR);
    b.aA = get (aA); b.aD = get (aD); b.aS = get (aS); b.aR = get (aR); b.ampVel = get (ampVel);
    b.l1Wave = i (l1Wave); b.l1Rate = get (l1Rate); b.l1Sync = get (l1Sync) > 0.5f; b.l1Div = i (l1Div);
    b.l1Dest = i (l1Dest); b.l1Amt = get (l1Amt);
    b.l2Wave = i (l2Wave); b.l2Rate = get (l2Rate); b.l2Sync = get (l2Sync) > 0.5f; b.l2Div = i (l2Div);
    b.l2Dest = i (l2Dest); b.l2Amt = get (l2Amt);
    b.mwLfo = get (mwLfo); b.mwCutoff = get (mwCutoff);
    b.l1Retrig = get (l1Retrig) > 0.5f; b.l1Fade = get (l1Fade);
    b.l2Retrig = get (l2Retrig) > 0.5f; b.l2Fade = get (l2Fade);
    b.biteAmt = get (biteAmt); b.bitePick = get (bitePick); b.biteComb = get (biteComb); b.biteTone = get (biteTone);
    b.enhancedDrive = get (driveEngine) > 0.5f;
    b.preBias = get (preBias); b.postBias = get (postBias); b.driveTilt = get (driveTilt);
    b.preDrive = get (preDrive); b.preType = i (preType);
    b.postDrive = get (postDrive); b.postType = i (postType);
    b.driveTone = get (driveTone); b.driveMix = get (driveMix); b.driveComp = get (driveComp) > 0.5f;
    b.anchorAmt = get (anchorAmt); b.anchorFreq = get (anchorFreq); b.outputDb = get (output);
    b.effects.select = i (chaosSelect);
    b.effects.mode = i (chaosMode);
    b.effects.morph = get (chaosMorph);
    b.effects.amount = get (chaosAmount);
    b.effects.division = i (chaosDiv);
    b.effects.punch = get (chaosPunch) > 0.5f;
    b.effects.hold = get (chaosHold) > 0.5f;
    b.effects.holdPitch = get (chaosHoldPitch);
    b.effects.holdSize = get (chaosHoldSize);
    b.effects.mix = get (chaosMix);
    b.effects.delayMix = get (delayMix);
    b.effects.delayTime = get (delayTime);
    b.effects.delaySync = get (delaySync) > 0.5f;
    b.effects.delayDiv = i (delayDiv);
    b.effects.delayFeedback = get (delayFeedback);
    b.effects.pingPong = get (delayPing) > 0.5f;
    b.effects.delayTone = get (delayTone);
    b.effects.reverbMix = get (reverbMix);
    b.effects.reverbPredelay = get (reverbPre);
    b.effects.reverbSize = get (reverbSize);
    b.effects.reverbDamp = get (reverbDamp);
    b.effects.chorusMix = get (chorusMix);
    b.effects.chorusRate = get (chorusRate);
    b.effects.chorusDepth = get (chorusDepth);
    b.effects.shiftMix = get (shiftMix);
    b.effects.shiftHz = get (shiftHz);
    b.effects.shiftFeedback = get (shiftFeedback);
    return b;
}

void NeonRiotProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int num = buffer.getNumSamples();
    buffer.clear();

    snapshot (num);
    engine.setBlock (readBlock());

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            engine.setTransport (pos->getBpm().orFallback (120.0),
                                 pos->getPpqPosition().orFallback (0.0),
                                 pos->getIsPlaying());

    // sample-accurate MIDI: render up to each event, then apply it
    int cursor = 0;
    for (const auto meta : midi)
    {
        // Only short channel messages are used. Copying long SysEx into MidiMessage
        // allocates with malloc; ignore it before constructing an owning message.
        if (meta.numBytes > 3) continue;
        const int pos = juce::jlimit (0, num, meta.samplePosition);
        if (pos > cursor) { engine.render (cursor, pos - cursor); cursor = pos; }
        const auto message = meta.getMessage();
        if (message.isController())
            learn.handleController (message.getControllerNumber(), message.getControllerValue());
        engine.handleMidi (message);
    }
    if (cursor < num) engine.render (cursor, num - cursor);

    engine.finish (buffer, num);
    publish (buffer);
}

void NeonRiotProcessor::publish (const juce::AudioBuffer<float>& buffer)
{
    auto& t = telemetry;
    const int num = buffer.getNumSamples(), nch = buffer.getNumChannels();
    int w = t.scopeWrite.load (std::memory_order_relaxed);
    for (int c = 0; c < 2; ++c)
    {
        const float* d = buffer.getReadPointer (std::min (c, nch - 1));
        float pk = 0.0f, sq = 0.0f;
        for (int n = 0; n < num; ++n) { pk = std::max (pk, std::abs (d[n])); sq += d[n] * d[n]; }
        // peaks accumulate until the UI takes them; RMS is the latest block
        float prev = t.peak[c].load (std::memory_order_relaxed);
        while (pk > prev && ! t.peak[c].compare_exchange_weak (prev, pk, std::memory_order_relaxed)) {}
        t.rms[c].store (std::sqrt (sq / (float) std::max (1, num)), std::memory_order_relaxed);
        if (pk >= 0.999f) t.clipped.store (true, std::memory_order_relaxed);
    }
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getReadPointer (std::min (1, nch - 1));
    for (int n = 0; n < num; ++n)
    {
        t.scope[(size_t) w].store (0.5f * (l[n] + r[n]), std::memory_order_relaxed);
        w = (w + 1) & (Telemetry::kScope - 1);
    }
    t.scopeWrite.store (w, std::memory_order_release);
    t.noteHz.store (440.0f * std::exp2 ((engine.lastPlayedNote() - 69.0f) / 12.0f), std::memory_order_relaxed);
    t.voices.store (engine.activeVoiceCount(), std::memory_order_relaxed);
}

int NeonRiotProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

const juce::String NeonRiotProcessor::getProgramName (int index)
{
    const auto& p = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) p.size()) ? juce::String (p[(size_t) index].name) : juce::String();
}

void NeonRiotProcessor::setCurrentProgram (int index)
{
    const auto& p = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) p.size())) return;
    currentProgram = index;
    applyPreset (apvts, p[(size_t) index]);
}

void NeonRiotProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // APVTS normally tolerates small float differences while flushing. Save the exact RT
    // values: granular/feedback paths can amplify even a one-ulp state discrepancy.
    for (auto* parameter : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            state.getChildWithProperty ("id", rp->getParameterID()).setProperty (
                "value", apvts.getRawParameterValue (rp->getParameterID())->load (std::memory_order_relaxed), nullptr);
    learn.save (state);
    state.setProperty ("stateVersion", 3, nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NeonRiotProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) tree.getProperty ("program", 0);
            learn.load (tree);
            // States from M1 have no engine selector: always restore the original voicing.
            for (const char* id : { PID::driveEngine, PID::preBias, PID::postBias, PID::driveTilt })
            if (! tree.getChildWithProperty ("id", id).isValid())
            {
                juce::ValueTree legacy ("PARAM");
                legacy.setProperty ("id", id, nullptr);
                legacy.setProperty ("value", 0.0f, nullptr);
                tree.appendChild (legacy, nullptr);
            }
            for (auto* param : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
                    if (! tree.getChildWithProperty ("id", rp->getParameterID()).isValid())
                    {
                        juce::ValueTree child ("PARAM");
                        child.setProperty ("id", rp->getParameterID(), nullptr);
                        child.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
                        tree.appendChild (child, nullptr);
                    }
            const auto exact = tree.createCopy();
            apvts.replaceState (tree);
            // The public normalized host restoration above can round-trip real values by a
            // few ulps. Restore the serialized real-valued atomic view exactly after it.
            // Future automation still updates these same APVTS atoms through its listeners.
            for (auto* parameter : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                {
                    const auto child = exact.getChildWithProperty ("id", rp->getParameterID());
                    const float value = (float) child.getProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()));
                    if (std::isfinite (value))
                        apvts.getRawParameterValue (rp->getParameterID())->store (
                            rp->getNormalisableRange().snapToLegalValue (value), std::memory_order_relaxed);
                }
        }
}

juce::AudioProcessorEditor* NeonRiotProcessor::createEditor() { return new NeonRiotEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NeonRiotProcessor(); }
