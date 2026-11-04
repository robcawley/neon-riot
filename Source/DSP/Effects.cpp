#include "Effects.h"
#include "../Parameters.h"

namespace nr
{
void Ring::prepare (int n) { data.assign ((size_t) std::max (n, 4), 0); w = valid = 0; }
void Ring::reset() { w = valid = 0; }
float Ring::read (float delay) const
{
    return at ((double) w - clampf (delay, 1, (float) data.size() - 2));
}
float Ring::at (double pos) const
{
    const double n = (double) data.size();
    pos -= std::floor (pos / n) * n;
    const int i = (int) pos, j = i + 1 == (int) data.size() ? 0 : i + 1;
    const int size = (int) data.size();
    const float a = (w + size - 1 - i) % size < valid ? data[(size_t) i] : 0;
    const float b = (w + size - 1 - j) % size < valid ? data[(size_t) j] : 0;
    return lerp (a, b, (float) (pos - i));
}
void Ring::push (float x) { data[(size_t) w] = x; valid = std::min (valid + 1, (int) data.size()); if (++w == (int) data.size()) w = 0; }
void WetFilter::prepare (float sr, float cutoff)
{
    low.setFreq (120, sr); dark.setFreq (cutoff, sr); reset();
}
void WetFilter::reset() { low.reset(); dark.reset(); }
float WetFilter::process (float x) { return dark.process (x - low.process (x)); }

void FrequencyShifter::Analytic::process (float x, const std::array<float, 63>& kernel, float& real, float& imag)
{
    h[(size_t) w] = x;
    real = h[(size_t) ((w + 64 - 31) & 63)]; imag = 0;
    for (int k = 0; k < 63; k += 2) imag += kernel[(size_t) k] * h[(size_t) ((w + 64 - k) & 63)];
    w = (w + 1) & 63;
}
void FrequencyShifter::prepare (float sr)
{
    sampleRate = sr;
    for (int n = 0; n < 63; ++n)
    {
        const int k = n - 31;
        taps[(size_t) n] = k != 0 && k % 2 != 0
            ? 2.0f / (kPi * (float) k) * (0.54f - 0.46f * std::cos (kTwoPi * (float) n / 62)) : 0;
    }
    for (auto& f : feedbackFilters) f.prepare (sr, 12000);
    reset();
}
void FrequencyShifter::reset()
{
    phase = 0; previous = {};
    for (auto& f : filters) f.reset();
    for (auto& f : feedbackFilters) f.reset();
}
Stereo FrequencyShifter::process (Stereo x, float hz, float fb)
{
    phase += (double) clampf (hz, -1000, 1000) / sampleRate; phase -= std::floor (phase);
    const float c = std::cos (kTwoPi * (float) phase), s = std::sin (kTwoPi * (float) phase);
    float outputs[2]; const float inputs[] { x.l, x.r }, old[] { previous.l, previous.r };
    for (size_t ch = 0; ch < 2; ++ch)
    {
        float real, imag;
        const float feedback = feedbackFilters[ch].process (old[ch]) * clampf (fb, 0, 0.3f);
        filters[ch].process (clampf (inputs[ch] + feedback, -8, 8), taps, real, imag);
        outputs[ch] = clampf (real * c - imag * s, -8, 8);
    }
    previous = { outputs[0], outputs[1] }; return previous;
}

void StereoDelay::prepare (float sr, float maxSeconds)
{
    sampleRate = sr;
    maximumDelay = clampf (maxSeconds, 0.25f, 48);
    for (auto& l : lines) l.prepare ((int) (sr * maximumDelay) + 4);
    for (auto& f : filters) f.prepare (sr, 4000);
    timeFadeStep = 1.0f / (0.020f * sr);
    feedbackSmooth.setTime (0.020f, sr); feedbackSmooth.reset (0.4f);
    toneSmooth.setTime (0.020f, sr); toneSmooth.reset (0.4f);
    ping.setTime (0.020f, sr); ping.reset (1);
    reset();
}
void StereoDelay::reset()
{
    for (auto& l : lines) l.reset();
    for (auto& f : filters) f.reset();
    lastTone = -1; timeSeconds = oldTime = 0.25f; timeFade = 1;
    feedbackSmooth.reset (0.4f); toneSmooth.reset (0.4f); ping.reset (1);
}
Stereo StereoDelay::process (Stereo x, float seconds, float feedback, bool pingPong, float tone)
{
    const float requested = clampf (seconds, 0.005f, maximumDelay);
    if (timeFade >= 1 && std::abs (requested - timeSeconds) > 1.0e-6f)
    { oldTime = timeSeconds; timeSeconds = requested; timeFade = 0; }
    timeFade = std::min (1.0f, timeFade + timeFadeStep);
    ping.target (pingPong ? 1.0f : 0.0f); feedbackSmooth.target (feedback); toneSmooth.target (tone);
    const float pp = ping.next(), fb = feedbackSmooth.next(), colour = toneSmooth.next();
    if (std::abs (colour - lastTone) > 1.0e-5f)
    {
        for (auto& f : filters) f.dark.setFreq (1000 * std::exp2 (colour * 3), sampleRate);
        lastTone = colour;
    }
    const Stereo fresh { lines[0].read (timeSeconds * sampleRate), lines[1].read (timeSeconds * sampleRate) };
    const Stereo old { lines[0].read (oldTime * sampleRate), lines[1].read (oldTime * sampleRate) };
    const Stereo tap = blend (old, fresh, timeFade);
    const Stereo wet { filters[0].process (tap.l), filters[1].process (tap.r) };
    // Feedback cannot extend beyond a 6-second RT60, even at a 48-second synced division.
    const float gain = std::min (0.72f * clampf (fb / 0.95f, 0, 1), std::exp (-6.907756f * (timeFade < 1 ? std::max (oldTime, timeSeconds) : timeSeconds) / 6));
    lines[0].push (clampf (x.l + tanhA (lerp (wet.l, wet.r, pp) * gain), -8, 8));
    lines[1].push (clampf (x.r * (1 - pp) + tanhA (lerp (wet.r, wet.l, pp) * gain), -8, 8));
    return wet;
}

void DarkReverb::prepare (float sr)
{
    sampleRate = sr;
    for (auto& l : lines) l.prepare ((int) (sr * 0.16f) + 4);
    for (auto& l : pre) l.prepare ((int) (sr * 0.25f) + 4);
    for (auto& f : inputFilter) f.prepare (sr, 5000);
    sizeSmooth.setTime (0.1f, sr); sizeSmooth.reset (0.5f);
    predelaySmooth.setTime (0.05f, sr); predelaySmooth.reset (0.02f);
    set (0.5f, 0.6f, 0.02f); reset();
}
void DarkReverb::reset()
{
    for (auto& l : lines) l.reset();
    for (auto& l : pre) l.reset();
    for (auto& d : damping) d.reset();
    for (auto& f : inputFilter) f.reset();
}
void DarkReverb::set (float size, float damp, float predelay)
{
    constexpr float times[] { 0.0311f, 0.0337f, 0.0367f, 0.0391f, 0.0419f, 0.0443f, 0.0473f, 0.0503f };
    sizeSmooth.target (size); predelaySmooth.target (predelay);
    for (size_t i = 0; i < 8; ++i)
    {
        lengths[i] = times[i] * sampleRate;
        const float seconds = times[i] * (0.5f + 2 * size);
        gains[i] = std::exp (-6.907756f * seconds / (0.6f + size * 5.4f));
        damping[i].setFreq (1000 + 6000 * (1 - damp), sampleRate);
    }
}
Stereo DarkReverb::process (Stereo x)
{
    const float predelay = predelaySmooth.next() * sampleRate;
    pre[0].push (inputFilter[0].process (x.l)); pre[1].push (inputFilter[1].process (x.r));
    const Stereo input { pre[0].read (predelay + 1), pre[1].read (predelay + 1) };
    const float scale = 0.5f + 2 * sizeSmooth.next();
    std::array<float, 8> v {}; float sum = 0;
    for (size_t i = 0; i < 8; ++i) { v[i] = damping[i].process (lines[i].read (lengths[i] * scale)); sum += v[i]; }
    // Householder feedback matrix is orthogonal; damping/gains are strictly contractive.
    for (size_t i = 0; i < 8; ++i)
    {
        const float excitation = ((i & 1u) == 0 ? input.l : input.r) * ((i & 2u) == 0 ? 0.25f : -0.25f);
        lines[i].push (clampf (excitation + gains[i] * (v[i] - 0.25f * sum), -8, 8));
    }
    return { (v[0] + v[1] - v[2] - v[3] + v[4] - v[5] + v[6] - v[7]) * 0.35f,
             (v[0] - v[1] + v[2] - v[3] - v[4] + v[5] + v[6] - v[7]) * 0.35f };
}

void Chorus::prepare (float sr)
{
    sampleRate = sr;
    for (auto& l : lines) l.prepare ((int) (sr * 0.06f) + 4);
    for (auto& f : filters) f.prepare (sr, 12000);
    reset();
}
void Chorus::reset() { phase = 0; for (auto& l : lines) l.reset(); for (auto& f : filters) f.reset(); }
Stereo Chorus::process (Stereo x, float rate, float depth)
{
    phase += rate / sampleRate; phase -= std::floor (phase);
    const float a = kTwoPi * (float) phase;
    const float dl = (0.015f + depth * 0.008f * std::sin (a)) * sampleRate;
    const float dr = (0.019f + depth * 0.008f * std::sin (a + kPi * 0.5f)) * sampleRate;
    lines[0].push (x.l); lines[1].push (x.r);
    return { filters[0].process (lines[0].read (dl)), filters[1].process (lines[1].read (dr)) };
}

void Capture::prepare (int n) { live.assign ((size_t) n, 0); snapshot.assign ((size_t) n, 0); reset(); }
void Capture::reset() { clock = start = 0; capturedLength = 0; /* clock validity hides all old storage */ }
void Capture::push (float x)
{
    const uint64_t n = live.size(), i = clock % n;
    if (capturedLength > 0 && clock >= start + n && clock < start + n + (uint64_t) capturedLength)
        snapshot[(size_t) (clock - n - start)] = live[(size_t) i];
    live[(size_t) i] = x; ++clock;
}
void Capture::capture (int length)
{
    capturedLength = (int) std::min (clock, (uint64_t) std::clamp (length, 0, (int) live.size()));
    start = clock - (uint64_t) capturedLength;
}
float Capture::sample (int index) const
{
    const uint64_t absolute = start + (uint64_t) index;
    return clock > absolute + live.size() ? snapshot[(size_t) index] : live[(size_t) (absolute % live.size())];
}
float Capture::frozen (double position) const
{
    if (capturedLength < 2) return 0;
    position -= std::floor (position / capturedLength) * capturedLength;
    const int i = (int) position, j = i + 1 == capturedLength ? 0 : i + 1;
    return lerp (sample (i), sample (j), (float) (position - i));
}

void Chaos::prepare (float sr)
{
    sampleRate = sr; history.prepare ((int) (sr * 48.01f) + 4); shortHistory.prepare ((int) (sr * 10) + 4);
    foldShift.prepare (sr); voidDelay.prepare (sr, 1.25f); voidReverb.prepare (sr);
    for (auto& f : foldTone) f.setFreq (8000, sr);
    for (auto& f : foldDc) f.prepare (sr);
    tearDc.prepare (sr); holdDc.prepare (sr);
    tearTone.setFreq (3500, sr);
    tearBand.prepare (sr, 3500); tearBand.low.setFreq (650, sr); crushSlew.setFreq (6000, sr);
    for (size_t i = 0; i < diffusers.size(); ++i) diffusers[i].prepare ((int) (sr * (0.0043f + (float) i * 0.0041f)) + 1);
    for (auto* s : { &amount, &mix, &holdBlend, &pitch, &size, &morph }) s->setTime (0.020f, sr);
    punch.setTime (0.001f, sr); // reaches >99% in about 5 ms
    reset();
}
void Chaos::reset()
{
    history.reset(); shortHistory.reset(); for (auto& g : grains) g = {};
    foldShift.reset(); voidDelay.reset(); voidReverb.reset();
    for (auto& d : diffusers) d.reset();
    tearBand.reset(); crushSlew.reset(); tearTone.reset(); tearDc.reset(); holdDc.reset();
    for (auto& f : foldTone) f.reset();
    for (auto& f : foldDc) f.reset();
    random.seed (0xA73C19u);
    weights.fill (0); weights[0] = 1;
    heldPhase = fracturePhase = 0; holdDecay = slowEnvelope = fastEnvelope = gate = crushValue = ratePhase = 0;
    grainCountdown = fractureCountdown = holdTransition = fractureTransition = 0;
    heldOverlap = heldPrevious = lastHeld = fractureStart = oldFractureStart = 0;
    fractureLength = oldFractureLength = 100; oldFracturePhase = 0;
    held = repeating = false; first = true;
}
void Chaos::set (const EffectParameters& p, double bpm)
{
    parameters = p;
    divisionSamples = (float) (sampleRate * 60.0 / std::clamp (bpm, 20.0, 999.0) * kDivQuarters[p.division]);
    amount.target (p.amount); mix.target (p.mix); punch.target (p.punch ? 1.0f : 0.0f);
    pitch.target (std::exp2 (p.holdPitch / 12)); size.target (p.holdSize); morph.target (p.mode == 0 ? (float) p.select : p.morph);
    if (p.hold && ! held)
    {
        heldPrevious = lastHeld; holdTransition = (int) (sampleRate * 0.005f);
        const float context = std::min (sampleRate * 0.003f, divisionSamples * 0.25f);
        history.capture ((int) std::ceil (divisionSamples + context));
        const float available = (float) history.length();
        const float availableContext = std::min (context, available * 0.2f);
        heldOverlap = available - std::min (divisionSamples, std::max (1.0f, available - availableContext));
        heldPhase = 0; holdDecay = 1;
    }
    // A restored HOLD has no captured audio; keep dry audible until a real capture exists.
    holdBlend.target (p.hold && history.length() >= 2 ? 1.0f : 0.0f);
    held = p.hold;
    voidReverb.set (0.6f + 0.35f * p.amount, 0.8f, 0.025f);
    if (first)
    {
        for (auto* s : { &amount, &mix, &punch, &pitch, &size, &morph }) s->reset (s->t);
        holdBlend.reset (0); first = false;
    }
}
float Chaos::readLoop (double phase, float length) const
{
    const float fade = std::min (sampleRate * 0.003f, length * 0.2f), period = length - fade;
    const float pos = (float) phase * period;
    if (pos < fade) return lerp (history.frozen (pos + period), history.frozen (pos), pos / fade);
    return history.frozen (pos);
}
Stereo Chaos::fracture (float x, float a)
{
    if (--fractureCountdown <= 0)
    {
        oldFractureStart = fractureStart; oldFractureLength = fractureLength; oldFracturePhase = fracturePhase;
        fractureTransition = (int) (sampleRate * 0.003f);
        float musicalPeriod = divisionSamples / std::exp2 (1 + std::floor (a * 4));
        // Keep very short/long choices on power-of-two tempo subdivisions, not a free-time clamp.
        while (musicalPeriod < sampleRate * 0.008f) musicalPeriod *= 2;
        while (musicalPeriod > sampleRate) musicalPeriod *= 0.5f;
        fractureLength = musicalPeriod + sampleRate * 0.002f;
        fractureStart = (float) shortHistory.w - fractureLength;
        fracturePhase = 0; fractureCountdown = (int) (musicalPeriod * (2 + (int) (a * 5)));
        repeating = random.unipolar() < a * 0.9f;
    }
    const float fade = std::min (sampleRate * 0.002f, fractureLength * 0.25f);
    const float period = fractureLength - fade, pos = (float) fracturePhase;
    float y = shortHistory.at (fractureStart + pos);
    if (pos < fade) y = lerp (shortHistory.at (fractureStart + period + pos), y, pos / fade);
    fracturePhase += 1;
    if (fracturePhase >= period) fracturePhase -= period;
    if (fractureTransition > 0)
    {
        const float oldFade = std::min (sampleRate * 0.002f, oldFractureLength * 0.25f);
        const float oldPeriod = std::max (1.0f, oldFractureLength - oldFade);
        float old = shortHistory.at (oldFractureStart + oldFracturePhase);
        if (oldFracturePhase < oldFade)
            old = lerp (shortHistory.at (oldFractureStart + oldPeriod + oldFracturePhase), old, (float) oldFracturePhase / oldFade);
        y = lerp (old, y, 1 - (float) fractureTransition / (sampleRate * 0.003f));
        oldFracturePhase += 1; if (oldFracturePhase >= oldPeriod) oldFracturePhase -= oldPeriod;
        --fractureTransition;
    }
    // Smooth entry/exit using a short low-pass on the repeat gate.
    const float target = repeating ? a : 0;
    gate += (target - gate) * (1.0f / (0.003f * sampleRate));
    y = lerp (x, y, gate);
    return { y, y };
}
Stereo Chaos::swarm (float, float a)
{
    if (--grainCountdown <= 0)
    {
        grainCountdown = (int) (sampleRate / (8 + 55 * a));
        for (auto& g : grains) if (! g.active)
        {
            g.active = true; g.age = 0; g.length = (int) (sampleRate * (0.035f + 0.08f * random.unipolar()));
            g.speed = std::exp2 (random.bipolar() * a * 1.25f);
            g.position = shortHistory.w - sampleRate * (0.16 + 0.24 * random.unipolar());
            g.pan = random.bipolar() * a; break;
        }
    }
    Stereo wet;
    for (auto& g : grains) if (g.active)
    {
        const float window = 0.5f - 0.5f * std::cos (kTwoPi * (float) g.age / (float) g.length);
        const float x = shortHistory.at (g.position) * window * 0.24f;
        wet.l += x * std::sqrt (0.5f * (1 - g.pan)); wet.r += x * std::sqrt (0.5f * (1 + g.pan));
        g.position += g.speed; if (++g.age >= g.length) g.active = false;
    }
    return wet;
}
Stereo Chaos::voidEffect (Stereo x, float a)
{
    Stereo echoes = voidDelay.process (x, clampf (divisionSamples / sampleRate, 0.04f, 1.2f), 0.5f + a * 0.4f, true, 0.1f);
    float d = (echoes.l + echoes.r) * 0.5f;
    for (auto& line : diffusers)
    {
        const float tap = line.read ((float) line.data.size() - 2);
        const float y = tap - 0.55f * d;
        line.push (d + 0.55f * y); d = y;
    }
    const Stereo space = voidReverb.process (x * 0.25f + Stereo { d, -d } * 0.35f);
    return x * (1 - 0.5f * a) + echoes * (0.25f + 0.5f * a) + space * (0.25f + a * 0.5f);
}
Stereo Chaos::process (float x)
{
    const float a = lerp (amount.next(), 1, punch.next()), wetMix = mix.next();
    const float loopPeriod = std::max (4.0f, ((float) history.length() - heldOverlap) * size.next());
    const float loopLength = loopPeriod + std::min (sampleRate * 0.003f, loopPeriod * 0.25f);
    const float rate = pitch.next();
    float looping = readLoop (heldPhase, loopLength) * holdDecay;
    if (holdTransition > 0)
    {
        looping = lerp (heldPrevious, looping, 1 - (float) holdTransition / (sampleRate * 0.005f));
        --holdTransition;
    }
    lastHeld = looping;
    looping = holdDc.process (looping);
    heldPhase += rate / loopPeriod;
    heldPhase -= std::floor (heldPhase);
    holdDecay *= std::exp (-13.81551f / (12 * sampleRate)); // -120 dB in 12 s, even with HOLD latched
    history.push (x);
    const float holdAmount = holdBlend.next();
    const float input = lerp (x, looping, holdAmount);
    shortHistory.push (input);
    const Stereo dry { input, input };
    const Stereo broken = fracture (input, a);
    ratePhase += (0.003f + (1 - a) * (1 - a)) * 0.75f;
    if (ratePhase >= 1) { ratePhase -= 1; const float q = std::exp2 (16 - 13 * a); crushValue = std::trunc (input * q) / q; }
    const float crushed = crushSlew.process (crushValue);
    fastEnvelope += (std::abs (input) - fastEnvelope) * onePoleCoef (0.001f, sampleRate);
    slowEnvelope += (std::abs (input) - slowEnvelope) * onePoleCoef (0.03f, sampleRate);
    const float transient = clampf ((fastEnvelope - slowEnvelope) * 18, 0, 1);
    const float cone = tearBand.process (input * (1 + random.bipolar() * a));
    const float dropout = lerp (1, transient, a * 0.8f);
    const float torn = tearDc.process (tearTone.process ((Shaper::fuzz (input * (1 + a * 20)) * dropout + cone * a * transient) * 0.7f));
    const Stereo shifted = foldShift.process (dry, a * a * 260 - a * 50, 0.05f * a);
    const Stereo folded { foldDc[0].process (foldTone[0].process (0.25f * std::sin (shifted.l * (1 + a * 15)))),
                          foldDc[1].process (foldTone[1].process (0.25f * std::sin (shifted.r * (1 + a * 15)))) };
    const Stereo grainsOut = swarm (input, a);
    const Stereo voidOut = voidEffect (dry, a);
    // A curated blend, then a gentle bounded fold: different balances come forward as amount rises.
    const Stereo melt = blend (broken, grainsOut, a * 0.45f) * (1 - a * 0.25f)
        + blend (voidOut, folded, a * 0.65f) * (0.15f + a * 0.35f);
    const std::array<Stereo, 8> outputs { dry, broken, blend (dry, { crushed, crushed }, a),
        blend (dry, { torn, torn }, a), blend (dry, folded, a), blend (dry, grainsOut, a),
        blend (dry, voidOut, a), blend (dry, { tanhA (melt.l * 1.4f), tanhA (melt.r * 1.4f) }, a) };
    const float selection = clampf (morph.next(), 0, 7);
    std::array<float, 8> target {};
    if (parameters.mode == 0) target[(size_t) parameters.select] = 1;
    else
    {
        const int lo = (int) selection, hi = std::min (7, lo + 1);
        const float fraction = selection - (float) lo;
        target[(size_t) lo] = std::cos (fraction * kPi * 0.5f);
        target[(size_t) hi] += std::sin (fraction * kPi * 0.5f);
    }
    Stereo effected; float weightSum = 0;
    for (size_t i = 0; i < 8; ++i)
    {
        weights[i] += (target[i] - weights[i]) * (1.0f / (0.004f * sampleRate)); // settles in ~20 ms
        effected = effected + outputs[i] * weights[i]; weightSum += weights[i];
    }
    // Equal-power weights apply to wet contributions; count their shared dry part once.
    // Otherwise zero intensity at a halfway morph would boost the dry signal by 3 dB.
    effected = effected + dry * (-(1 - a) * (weightSum - 1));
    if (a <= 0) effected = dry;
    return blend ({ x, x }, effected, wetMix);
}

void Effects::prepare (float sr)
{
    chaos.prepare (sr); chorus.prepare (sr); shift.prepare (sr); delay.prepare (sr); reverb.prepare (sr);
    for (auto* bank : { &chorusReturn, &shiftReturn, &delayReturn, &reverbReturn }) for (auto& f : *bank) f.prepare (sr, 16000);
    for (auto* s : { &chorusMix, &chorusRate, &chorusDepth, &shiftMix, &shiftHz, &shiftFeedback, &delayMix, &reverbMix }) s->setTime (0.02f, sr);
    reset();
}
void Effects::reset()
{
    chaos.reset(); chorus.reset(); shift.reset(); delay.reset(); reverb.reset();
    for (auto* bank : { &chorusReturn, &shiftReturn, &delayReturn, &reverbReturn }) for (auto& f : *bank) f.reset();
    first = true;
}
void Effects::set (const EffectParameters& p, double bpm)
{
    parameters = p; chaos.set (p, bpm);
    delaySeconds = p.delaySync ? (float) (60 / std::clamp (bpm, 20.0, 999.0) * kDivQuarters[p.delayDiv]) : p.delayTime;
    chorusMix.target (p.chorusMix); chorusRate.target (p.chorusRate); chorusDepth.target (p.chorusDepth);
    shiftMix.target (p.shiftMix); shiftHz.target (p.shiftHz); shiftFeedback.target (p.shiftFeedback);
    delayMix.target (p.delayMix); reverbMix.target (p.reverbMix);
    reverb.set (p.reverbSize, p.reverbDamp, p.reverbPredelay);
    if (first)
    {
        for (auto* s : { &chorusMix, &chorusRate, &chorusDepth, &shiftMix, &shiftHz, &shiftFeedback, &delayMix, &reverbMix }) s->reset (s->t);
        first = false;
    }
}
Stereo Effects::process (float x)
{
    Stereo signal = chaos.process (x);
    const Stereo c = chorus.process (signal, chorusRate.next(), chorusDepth.next());
    signal = signal + Stereo { chorusReturn[0].process (c.l), chorusReturn[1].process (c.r) } * chorusMix.next();
    const Stereo s = shift.process (signal, shiftHz.next(), shiftFeedback.next());
    signal = blend (signal, { shiftReturn[0].process (s.l), shiftReturn[1].process (s.r) }, shiftMix.next());
    const Stereo d = delay.process (signal, delaySeconds, parameters.delayFeedback, parameters.pingPong, parameters.delayTone);
    signal = signal + Stereo { delayReturn[0].process (d.l), delayReturn[1].process (d.r) } * delayMix.next();
    const Stereo r = reverb.process (signal);
    signal = signal + Stereo { reverbReturn[0].process (r.l), reverbReturn[1].process (r.r) } * reverbMix.next();
    return signal;
}
}
