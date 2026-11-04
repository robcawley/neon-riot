#include "Engine.h"
#include "../Parameters.h"

namespace nr
{
static inline float noteToHz (float n) { return 440.0f * std::exp2 ((n - 69.0f) / 12.0f); }

//==============================================================================
void Voice::prepare (float sampleRate, int maxCombSamples, uint32_t seed)
{
    sr = sampleRate;
    filter.prepare (sr);
    pre.prepare (sr);
    amp.prepare (sr);
    filtEnv.prepare (sr);
    dc.prepare (sr);
    noiseLp.setFreq (1500.0f, sr);
    pickLp.setFreq (2500.0f, sr);
    comb.assign ((size_t) maxCombSamples, 0.0f);
    rngSeed = seed;
    reset();
}

void Voice::reset()
{
    rng.seed (rngSeed);
    vlfo[0].reset(); vlfo[1].reset(); lfoAge = 0;
    vlfo[0].rng.seed (rngSeed * 2654435761u + 101u);
    vlfo[1].rng.seed (rngSeed * 2246822519u + 211u);
    driftV = driftT = instV = instT = cutInstV = cutInstT = osc2Detune = 0;
    driftCount = instCount = 0;
    o1.reset(); o2.reset(); sub.reset(); subSinePhase = 0.0;
    pre.reset(); filter.reset(); amp.reset(); filtEnv.reset(); dc.reset();
    noiseLp.reset(); pickLp.reset(); combDamp.reset();
    std::fill (comb.begin(), comb.end(), 0.0f);
    combW = 0; biteEnv = pickEnv = 0.0f; note = -1; gliding = false;
}

void Voice::setBlock (const Block& b)
{
    blk = &b;
    amp.set (b.aA, b.aD, b.aS, b.aR);
    filtEnv.set (b.fA, b.fD, b.fS, b.fR);
    filter.mode = b.fltMode;
    vlfo[0].wave = b.l1Wave; vlfo[1].wave = b.l2Wave;
    glideCoef = b.glide > 0.0005f ? onePoleCoef (b.glide / 3.0f, sr) : 1.0f;
    biteDecay = std::exp (-1.0f / (0.012f * sr));
    pickDecay = std::exp (-1.0f / ((0.0015f + 0.004f * (1.0f - b.biteTone)) * sr));
    combDamp.setFreq (1000.0f * std::exp2 (b.biteTone * 4.0f), sr);
    if (b.subWave == 1) sub.setShape (0.0f, 0.0f);
    else                sub.setShape (2.0f, 0.0f);
}

void Voice::start (int n, float vel, bool retrigger, bool glideFrom, float fromNote, const Block& b)
{
    const bool fresh = ! amp.active();
    note = n;
    targetNote = (float) n;
    currentNote = glideFrom ? fromNote : (float) n;
    velocity = vel;

    if (fresh)
    {
        o1.reset(); o2.reset (b.drift * 0.25f * rng.unipolar()); sub.reset(); subSinePhase = 0.0;
        pre.reset(); filter.reset(); dc.reset(); combDamp.reset();
        std::fill (comb.begin(), comb.end(), 0.0f);
    }
    osc2Detune = rng.bipolar() * 0.04f * b.drift;   // up to +-4 cents
    if (retrigger)
    {
        amp.noteOn(); filtEnv.noteOn();
        biteEnv = 1.0f; pickEnv = 1.0f;
        vlfo[0].reset(); vlfo[1].reset(); lfoAge = 0;
    }
}

void Voice::retarget (int n, bool glide)
{
    note = n;
    targetNote = (float) n;
    if (! glide) currentNote = targetNote;
}

void Voice::release() { amp.noteOff(); filtEnv.noteOff(); }

float Voice::render (const Frame& f, float& clean)
{
    if (! amp.active()) return 0.0f;
    const Block& b = *blk;

    currentNote += (targetNote - currentNote) * glideCoef;

    // analogue drift (slow) and instability (fast) random walks
    if (--driftCount <= 0) { driftT = rng.bipolar(); driftCount = (int) (sr * (0.4f + 0.8f * rng.unipolar())); }
    if (--instCount <= 0)  { instT = rng.bipolar(); cutInstT = rng.bipolar(); instCount = (int) (sr * (0.03f + 0.05f * rng.unipolar())); }
    driftV   += (driftT - driftV) * (1.0f / (0.3f * sr));
    instV    += (instT - instV) * (1.0f / (0.012f * sr));
    cutInstV += (cutInstT - cutInstV) * (1.0f / (0.02f * sr));

    const float driftSemi = driftV * 0.15f * b.drift;
    const float instSemi  = instV * 0.3f * b.instability;

    const float n1 = currentNote + f.pitch + b.osc1Pitch + driftSemi + instSemi;
    const float n2 = currentNote + f.pitch + f.osc2Pitch + b.osc2Pitch + osc2Detune + 0.7f * driftSemi - instSemi;
    const float hz1 = noteToHz (n1);
    const double dt1 = std::min (0.45, (double) hz1 / sr);
    const double dt2 = std::min (0.45, (double) noteToHz (n2) / sr);

    o1.setShape (f.osc1Wave, f.osc1Shape);
    o2.setShape (f.osc2Wave, f.osc2Shape);
    bool wrapped; float frac = 0.0f;
    const float s1 = o1.process (dt1, wrapped, frac);
    const float s2 = b.osc2Sync ? o2.processSynced (dt2, wrapped, frac) : o2.process (dt2);

    float subS;
    if (b.subWave == 0)
    {
        subSinePhase += dt1 * b.subDiv;
        subSinePhase -= std::floor (subSinePhase);
        subS = std::sin (kTwoPi * (float) subSinePhase);
    }
    else subS = sub.process (dt1 * b.subDiv);

    const float wn = rng.bipolar();
    const float lp = noiseLp.process (wn);
    const float c = f.noiseLevel > 0.0f ? b.noiseColour : 0.0f;
    const float nz = c < 0.0f ? lerp (wn, lp * 2.0f, -c) : lerp (wn, (wn - lp) * 1.3f, c);

    const float oscs = 0.5f * (s1 * f.osc1Level + s2 * f.osc2Level);
    const float subPart = subS * f.subLevel * 0.6f;
    const float cleanLayer = oscs + subPart;
    float x = cleanLayer + nz * f.noiseLevel * 0.4f;

    // STRING BITE: plucked accent, pick noise, comb resonance tuned to the note
    if (b.biteAmt > 0.0005f)
    {
        biteEnv *= biteDecay;
        pickEnv *= pickDecay;
        const float wn2 = rng.bipolar();
        const float pick = (wn2 - pickLp.process (wn2)) * pickEnv * b.bitePick * b.biteAmt * 1.2f;
        const float ex = x * (1.0f + 1.5f * b.biteAmt * biteEnv) + pick;

        const float combAmt = clampf (f.comb, 0.0f, 1.0f);
        const float fb = 0.985f * combAmt;
        const int size = (int) comb.size();
        const float d = clampf (sr / hz1, 2.0f, (float) size - 2.0f);
        float rp = (float) combW - d;
        if (rp < 0.0f) rp += (float) size;
        const int i0 = (int) rp;
        const int i1 = (i0 + 1) % size;
        const float fr = rp - (float) i0;
        const float delayed = comb[(size_t) i0] + (comb[(size_t) i1] - comb[(size_t) i0]) * fr;
        const float y = ex + fb * combDamp.process (delayed);
        comb[(size_t) combW] = clampf (y, -4.0f, 4.0f);
        combW = (combW + 1) % size;
        const float combOut = y * (1.0f - 0.8f * fb);
        x = lerp (ex, combOut, combAmt * (0.4f + 0.6f * b.biteAmt));
    }

    // pre-filter drive
    if (b.enhancedDrive)
        x = pre.process (b.preType, x, f.preDrive, f.preBias, f.driveTilt, b.driveComp);
    else
    {
        const float g = Shaper::gainFor (f.preDrive);
        x = Shaper::process (b.preType, x, g) * std::pow (g, -0.35f);
    }
    x = dc.process (x);

    // filter
    const float fe = filtEnv.next();
    const float envOct = b.fltEnv * fe * 7.0f * (1.0f - b.fltVel + b.fltVel * velocity);
    const float keyOct = b.fltKey * (currentNote - 60.0f) / 12.0f;
    const float fc = std::exp2 (f.cutoffLog2 + envOct + keyOct + cutInstV * 0.5f * b.instability);
    const float y = filter.process (x + rng.bipolar() * 1.0e-5f, fc, f.reso * 4.0f, 1.0f + 5.0f * b.fltDrive);

    const float a = amp.next();
    const float gain = a * (1.0f - b.ampVel + b.ampVel * velocity) * f.ampMul;
    clean += cleanLayer * gain;
    return y * gain;
}

//==============================================================================
void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = (float) sampleRate;
    const int maxComb = (int) (sr / 15.0f) + 4;
    for (size_t i = 0; i < voices.size(); ++i)
        voices[i].prepare (sr, maxComb, 0x13579u + (uint32_t) i * 7919u);

    mono.assign ((size_t) maxBlock, 0.0f);
    clean.assign ((size_t) maxBlock, 0.0f);

    os.reset();
    os.initProcessing ((size_t) maxBlock);
    postLatency = juce::roundToInt (os.getLatencyInSamples());
    latency = postLatency + PreDrive2x::latency;
    post.prepare (sr * 4.0f);
    effects.prepare (sr);
    cleanDelay.assign (128, 0.0f);
    cleanW = 0;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, 1 };
    juce::dsp::ProcessSpec stereoSpec { sampleRate, (juce::uint32) maxBlock, 2 };
    anchorHp.prepare (stereoSpec); anchorHp.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    anchorLp.prepare (spec); anchorLp.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    for (auto& f : sideHp) { f.prepare (spec); f.setType (juce::dsp::LinkwitzRileyFilterType::highpass); f.setCutoffFrequency (120); }
    postDc.prepare (sr); cleanDc.prepare (sr);

    for (auto* s : { &sWave1, &sShape1, &sWave2, &sShape2, &sL1, &sL2, &sSub, &sNoise, &sReso, &sPre, &sComb, &sAnchor, &sOut, &sPreBias, &sTilt })
        s->setTime (0.02f, sr);
    sCut.setTime (0.008f, sr);
    for (auto* s : { &sPost, &sTone, &sMix, &sPostBias, &sPostTilt })
        s->setTime (0.02f, sr * 4.0f);

    lfo1.rng.seed (12345u); lfo2.rng.seed (67890u);
    firstBlock = true;
    reset();
}

void Engine::reset()
{
    for (auto& v : voices) v.reset();
    heldOrder.fill (0); sustained.fill (false); sustain = false;
    counter = pressCounter = 0; lastMode = -1; lastNote = 60; bend = modWheel = 0;
    lfo1.reset(); lfo2.reset();
    effects.reset(); for (auto& f : sideHp) f.reset();
    legacyDelay.fill (0); legacyW = 0; post.reset();
    os.reset(); anchorHp.reset(); anchorLp.reset(); toneLp.reset(); postDc.reset(); cleanDc.reset();
    std::fill (cleanDelay.begin(), cleanDelay.end(), 0.0f);
}

void Engine::setBlock (const Block& nb)
{
    b = nb;
    // A mode change retires held voices through their release instead of cutting a waveform.
    if (lastMode != -1 && lastMode != b.voiceMode) allNotesOff (false);
    lastMode = b.voiceMode;

    for (auto& v : voices) v.setBlock (b);
    lfo1.wave = b.l1Wave; lfo2.wave = b.l2Wave;

    sWave1.target (b.osc1Wave); sShape1.target (b.osc1Shape);
    sWave2.target (b.osc2Wave); sShape2.target (b.osc2Shape);
    sL1.target (b.osc1Level); sL2.target (b.osc2Level); sSub.target (b.subLevel); sNoise.target (b.noiseLevel);
    sCut.target (std::log2 (b.cutoff)); sReso.target (b.reso); sPre.target (b.preDrive); sComb.target (b.biteComb);
    sPost.target (b.postDrive); sTone.target (b.driveTone); sMix.target (b.driveMix);
    sPreBias.target (b.preBias); sPostBias.target (b.postBias);
    sTilt.target (b.driveTilt); sPostTilt.target (b.driveTilt);
    sAnchor.target (b.anchorAmt); sOut.target (b.outputDb);

    if (firstBlock)
    {
        for (auto* s : { &sWave1, &sShape1, &sWave2, &sShape2, &sL1, &sL2, &sSub, &sNoise, &sCut, &sReso, &sPre,
                         &sComb, &sPost, &sTone, &sMix, &sAnchor, &sOut, &sPreBias, &sPostBias, &sTilt, &sPostTilt })
            s->reset (s->t);
        firstBlock = false;
    }
    anchorHp.setCutoffFrequency (b.anchorFreq);
    anchorLp.setCutoffFrequency (b.anchorFreq);
}

void Engine::setTransport (double newBpm, double newPpq, bool isPlaying)
{
    bpm = juce::jlimit (20.0, 999.0, newBpm);
    ppq = newPpq;
    playing = isPlaying;
    if (playing)
    {
        if (b.l1Sync) { const double q = kDivQuarters[b.l1Div]; lfo1.phase = ppq / q - std::floor (ppq / q); }
        if (b.l2Sync) { const double q = kDivQuarters[b.l2Div]; lfo2.phase = ppq / q - std::floor (ppq / q); }
    }
}

//==============================================================================
void Engine::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())                 noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())           noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())        bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
    else if (m.isAllNotesOff())       allNotesOff (false);
    else if (m.isAllSoundOff())       allNotesOff (true);
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        if (cc == 1) modWheel = (float) m.getControllerValue() / 127.0f;
        else if (cc == 64)
        {
            const bool down = m.getControllerValue() >= 64;
            if (sustain && ! down)
            {
                sustain = false;
                if (b.voiceMode == 2)
                {
                    for (int n = 0; n < 128; ++n)
                        if (sustained[(size_t) n] && heldOrder[(size_t) n] == 0)
                            for (auto& v : voices)
                                if (v.note == n && v.isActive() && ! v.isReleasing()) v.release();
                }
                else
                {
                    auto& v = voices[0];
                    const int t = topHeld();
                    if (t < 0) v.release();
                    else if (t != v.note) v.retarget (t, true);
                }
                sustained.fill (false);
            }
            sustain = down;
        }
    }
}

int Engine::topHeld() const
{
    int best = -1; int bestOrder = 0;
    for (int n = 0; n < 128; ++n)
        if (heldOrder[(size_t) n] > bestOrder) { bestOrder = heldOrder[(size_t) n]; best = n; }
    return best;
}

Voice* Engine::allocate()
{
    Voice* best = nullptr;
    for (auto& v : voices) if (! v.isActive()) return &v;
    for (auto& v : voices) if (v.isReleasing() && (! best || v.age < best->age)) best = &v;
    if (best) return best;
    for (auto& v : voices) if (! best || v.age < best->age) best = &v;
    return best;
}

void Engine::noteOn (int n, float vel)
{
    bool othersHeld = false;
    for (int i = 0; i < 128; ++i) if (i != n && heldOrder[(size_t) i] > 0) { othersHeld = true; break; }
    heldOrder[(size_t) n] = (int) ++pressCounter;
    sustained[(size_t) n] = false;

    const bool glide = b.glide > 0.0005f;
    if (b.voiceMode == 2)
    {
        auto* v = allocate();
        v->age = ++counter;
        v->start (n, vel, true, glide, lastNote, b);
    }
    else
    {
        auto& v = voices[0];
        v.age = ++counter;
        const float from = v.isActive() ? v.currentNote : lastNote;
        if (b.voiceMode == 0)
            v.start (n, vel, true, glide, from, b);
        else if (othersHeld && v.isActive() && ! v.isReleasing())
            v.retarget (n, true);                       // legato: slide, no retrigger
        else
            v.start (n, vel, true, false, from, b);
    }
    lastNote = (float) n;
}

void Engine::noteOff (int n)
{
    heldOrder[(size_t) n] = 0;
    if (sustain) { sustained[(size_t) n] = true; return; }

    if (b.voiceMode == 2)
    {
        for (auto& v : voices)
            if (v.note == n && v.isActive() && ! v.isReleasing()) v.release();
    }
    else
    {
        auto& v = voices[0];
        if (v.note == n)
        {
            const int t = topHeld();
            if (t >= 0) v.retarget (t, true);
            else v.release();
        }
    }
}

void Engine::allNotesOff (bool hard)
{
    heldOrder.fill (0); sustained.fill (false);
    for (auto& v : voices) { if (hard) v.kill(); else v.release(); }
}

//==============================================================================
static inline void applyLfo (Frame& f, int dest, float lfo, float amt)
{
    const float v = lfo * amt;
    switch (dest)
    {
        case 1: f.pitch     += lfo * amt * std::abs (amt) * 12.0f; break;
        case 2: f.cutoffLog2 += v * 5.0f; break;
        case 3: f.osc1Wave  += v; f.osc2Wave += v; break;
        case 4: f.osc1Shape += v * 0.5f; f.osc2Shape += v * 0.5f; break;
        case 5: f.ampMul    *= 1.0f - std::abs (amt) * (0.5f + 0.5f * lfo * (amt >= 0.0f ? 1.0f : -1.0f)); break;
        case 6: f.preDrive  += v; break;
        case 7: f.osc2Pitch += lfo * amt * std::abs (amt) * 12.0f; break;
        case 8: f.comb      += v; break;
        case 9: f.reso      += v * 0.6f; break;
        default: break;
    }
}

void Engine::render (int start, int num)
{
    const double inc1 = b.l1Sync ? (bpm / 60.0) / kDivQuarters[b.l1Div] / sr : b.l1Rate / sr;
    const double inc2 = b.l2Sync ? (bpm / 60.0) / kDivQuarters[b.l2Div] / sr : b.l2Rate / sr;
    const float mwScale = (1.0f - b.mwLfo) + b.mwLfo * modWheel;

    for (int i = 0; i < num; ++i)
    {
        Frame f;
        f.osc1Wave = sWave1.next(); f.osc1Shape = sShape1.next();
        f.osc2Wave = sWave2.next(); f.osc2Shape = sShape2.next();
        f.osc1Level = sL1.next(); f.osc2Level = sL2.next(); f.subLevel = sSub.next(); f.noiseLevel = sNoise.next();
        f.cutoffLog2 = sCut.next() + modWheel * b.mwCutoff * 5.0f;
        f.reso = sReso.next(); f.preDrive = sPre.next(); f.comb = sComb.next();
        f.preBias = sPreBias.next(); f.driveTilt = sTilt.next();
        f.pitch = bend * b.pbRange; f.osc2Pitch = 0.0f; f.ampMul = 1.0f;

        const float v1 = lfo1.tick (inc1), v2 = lfo2.tick (inc2);
        // An LFO that retriggers or fades in is applied per voice; otherwise it stays global,
        // on exactly the original path.
        const bool pv1 = b.l1Retrig || b.l1Fade > 0.0005f, pv2 = b.l2Retrig || b.l2Fade > 0.0005f;
        if (! pv1) applyLfo (f, b.l1Dest, v1, b.l1Amt * mwScale);
        if (! pv2) applyLfo (f, b.l2Dest, v2, b.l2Amt);
        f.preDrive = clampf (f.preDrive, 0.0f, 1.0f);
        f.comb = clampf (f.comb, 0.0f, 1.0f);
        f.reso = clampf (f.reso, 0.0f, 1.25f);

        float m = 0.0f, c = 0.0f;
        if (! pv1 && ! pv2)
        {
            for (auto& v : voices) m += v.render (f, c);
        }
        else
        {
            for (auto& v : voices)
            {
                if (! v.isActive()) continue;
                Frame fv = f;
                if (pv1)
                {
                    const float val = b.l1Retrig ? v.vlfo[0].tick (inc1) : v1;
                    applyLfo (fv, b.l1Dest, val, b.l1Amt * mwScale * v.lfoFade (b.l1Fade));
                }
                if (pv2)
                {
                    const float val = b.l2Retrig ? v.vlfo[1].tick (inc2) : v2;
                    applyLfo (fv, b.l2Dest, val, b.l2Amt * v.lfoFade (b.l2Fade));
                }
                ++v.lfoAge;
                fv.preDrive = clampf (fv.preDrive, 0.0f, 1.0f);
                fv.comb = clampf (fv.comb, 0.0f, 1.0f);
                fv.reso = clampf (fv.reso, 0.0f, 1.25f);
                m += v.render (fv, c);
            }
        }
        // The M1 path receives the same fixed delay as the new pre oversampler.
        // Delay the whole voice sum so its original filter/envelope interaction survives.
        const float delayed = legacyDelay[(size_t) legacyW];
        legacyDelay[(size_t) legacyW] = m;
        legacyW = (legacyW + 1) % PreDrive2x::latency;
        mono[(size_t) (start + i)] = b.enhancedDrive ? m : delayed;
        clean[(size_t) (start + i)] = c;
    }
}

void Engine::finish (juce::AudioBuffer<float>& out, int num)
{
    // Post-filter drive, 4x oversampled
    float* chans[] = { mono.data() };
    juce::dsp::AudioBlock<float> block (chans, 1, (size_t) num);
    auto up = os.processSamplesUp (block);
    float* u = up.getChannelPointer (0);
    const float upSr = sr * 4.0f;
    for (size_t i = 0; i < up.getNumSamples(); ++i)
    {
        const float drive = sPost.next(), tone = sTone.next(), mix = sMix.next();
        const float g = Shaper::gainFor (drive);
        const float comp = b.driveComp ? std::pow (g, -0.5f) * (b.postType == 3 ? 1.0f : 1.15f) : 1.0f;
        toneLp.setFreq (600.0f * std::exp2 (tone * 5.0f), upSr);
        const float bias = sPostBias.next(), tilt = sPostTilt.next();
        const float wet = b.enhancedDrive
            ? toneLp.process (post.process (b.postType, u[i], drive, bias, tilt, b.driveComp))
            : toneLp.process (Shaper::process (b.postType, u[i], g)) * comp;
        u[i] = u[i] + (wet - u[i]) * mix;
    }
    os.processSamplesDown (block);

    effects.set (b.effects, bpm);
    const int nCh = out.getNumChannels();
    const int mask = (int) cleanDelay.size() - 1;
    bool bad = false;
    for (int i = 0; i < num; ++i)
    {
        float p = postDc.process (mono[(size_t) i]);

        cleanDelay[(size_t) cleanW] = cleanDc.process (clean[(size_t) i]);
        const float cd = cleanDelay[(size_t) ((cleanW - latency) & mask)];
        cleanW = (cleanW + 1) & mask;

        const Stereo processed = effects.process (p);
        const float mid = (processed.l + processed.r) * 0.5f;
        float side = (processed.l - processed.r) * 0.5f;
        for (auto& f : sideHp) side = f.processSample (0, side);
        const float lp = anchorLp.processSample (0, cd), anchor = sAnchor.next();
        const float level = juce::Decibels::decibelsToGain (sOut.next(), -100.0f);
        float stereo[2] { mid + side, mid - side };
        for (int ch = 0; ch < 2; ++ch)
        {
            const float hp = anchorHp.processSample (ch, stereo[ch]);
            stereo[ch] = lerp (stereo[ch], hp + lp, anchor) * level;
        }
        if (nCh == 1) stereo[0] = 0.5f * (stereo[0] + stereo[1]);
        for (int ch = 0; ch < nCh; ++ch)
        {
            float y = stereo[ch];
            if (! std::isfinite (y)) { y = 0; bad = true; }
            // Safety remains the last operation in both stereo and mono layouts.
            const float a = std::abs (y);
            if (a > 0.95f) y = std::copysign (0.95f + 0.05f * tanhA ((a - 0.95f) / 0.05f), y);
            out.setSample (ch, i, y);
        }
    }
    if (bad) reset();
}
} // namespace nr
