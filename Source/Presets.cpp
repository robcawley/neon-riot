#include "Presets.h"
#include "Parameters.h"

using namespace PID;

namespace
{
// Shared foundation for the appended cinematic bank only. Expanded into ordinary preset
// values at construction; no new synthesis path, parameter, or audio-thread work.
FactoryPreset cinematic (const char* name, std::initializer_list<std::pair<const char*, float>> overrides)
{
    FactoryPreset patch { name, {
        { driveEngine, 1 }, { voiceMode, 2 }, { osc1Wave, 0.7f }, { osc2Wave, 1.2f },
        { osc1Level, 0.65f }, { osc2Level, 0.35f }, { osc2Fine, 7 }, { subLevel, 0.28f }, { subWave, 0 },
        { noiseLevel, 0.015f }, { noiseColour, -0.6f }, { drift, 0.28f }, { instability, 0.015f },
        { fltCutoff, 850 }, { fltReso, 0.25f }, { fltEnv, 0.2f }, { fltKey, 0.3f }, { fltVel, 0.2f }, { fltDrive, 0.18f },
        { fA, 7 }, { fD, 8 }, { fS, 0.3f }, { fR, 3 },
        { aA, 2.5f }, { aD, 3 }, { aS, 0.9f }, { aR, 3 }, { ampVel, 0.35f },
        { biteAmt, 0.3f }, { bitePick, 0.05f }, { biteComb, 0.65f }, { biteTone, 0.4f },
        { preDrive, 0.2f }, { preType, 0 }, { postDrive, 0.1f }, { postType, 0 }, { driveTone, 0.4f }, { driveTilt, -0.15f },
        { l1Wave, 0 }, { l1Rate, 0.045f }, { l1Dest, 2 }, { l1Amt, 0.3f }, { l1Fade, 3 }, { l1Retrig, 1 },
        { l2Wave, 6 }, { l2Rate, 0.075f }, { l2Dest, 3 }, { l2Amt, 0.22f }, { l2Fade, 4 }, { l2Retrig, 1 },
        { chorusMix, 0.2f }, { chorusRate, 0.11f }, { chorusDepth, 0.6f },
        { delayMix, 0.12f }, { delaySync, 1 }, { delayDiv, 4 }, { delayFeedback, 0.36f }, { delayTone, 0.25f },
        { reverbMix, 0.32f }, { reverbSize, 0.76f }, { reverbDamp, 0.78f }, { reverbPre, 0.035f },
        { anchorAmt, 0.6f }, { anchorFreq, 105 }, { output, -10 }
    } };
    for (const auto& value : overrides)
    {
        auto it = std::find_if (patch.values.begin(), patch.values.end(), [&] (const auto& v)
            { return juce::StringRef (v.first) == juce::StringRef (value.first); });
        if (it == patch.values.end()) patch.values.push_back (value); else it->second = value.second;
    }
    return patch;
}
}

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list {
        { "Riot Init", {} },

        { "Snarl Bass", {
            { osc1Wave, 1.0f }, { osc2Wave, 1.0f }, { osc2Level, 0.6f }, { osc2Fine, 9.0f },
            { subLevel, 0.6f }, { subWave, 2 }, { glide, 0.035f },
            { fltCutoff, 330.0f }, { fltReso, 0.45f }, { fltEnv, 0.55f }, { fltDrive, 0.4f },
            { fD, 0.3f }, { fS, 0.25f }, { aR, 0.12f },
            { preDrive, 0.45f }, { preType, 1 }, { postDrive, 0.35f }, { postType, 2 }, { driveTone, 0.6f },
            { anchorAmt, 0.6f }, { anchorFreq, 100.0f }, { output, -3.0f } } },

        { "Squelch Line", {
            { osc1Wave, 1.0f }, { subLevel, 0.0f }, { voiceMode, 1 }, { glide, 0.06f },
            { fltCutoff, 380.0f }, { fltReso, 0.92f }, { fltEnv, 0.75f }, { fltVel, 0.8f }, { fltDrive, 0.6f },
            { fA, 0.0005f }, { fD, 0.18f }, { fS, 0.0f }, { fR, 0.1f },
            { aD, 0.3f }, { aS, 0.8f }, { aR, 0.03f }, { ampVel, 0.3f },
            { preDrive, 0.2f }, { postDrive, 0.3f }, { postType, 0 }, { output, -4.0f } } },

        { "Fuzz Pluck Bass", {
            { osc1Wave, 1.0f }, { osc2Wave, 2.0f }, { osc2Shape, 0.3f }, { osc2Oct, -1 }, { osc2Level, 0.5f },
            { subLevel, 0.4f }, { biteAmt, 0.8f }, { bitePick, 0.6f }, { biteComb, 0.6f }, { biteTone, 0.45f },
            { fltCutoff, 900.0f }, { fltReso, 0.2f }, { fltEnv, 0.35f }, { fD, 0.25f }, { fS, 0.3f },
            { aD, 0.6f }, { aS, 0.35f }, { aR, 0.2f },
            { postDrive, 0.55f }, { postType, 2 }, { driveTone, 0.55f },
            { anchorAmt, 0.7f }, { output, -4.0f } } },

        { "Rust Drone", {
            { voiceMode, 2 }, { osc1Wave, 2.0f }, { osc1Shape, 0.6f },
            { osc2Wave, 1.0f }, { osc2Sync, 1 }, { osc2Oct, 1 }, { osc2Semi, 7 }, { osc2Level, 0.7f },
            { subLevel, 0.3f }, { subWave, 0 }, { drift, 0.4f },
            { l1Wave, 1 }, { l1Rate, 0.2f }, { l1Dest, 7 }, { l1Amt, 0.35f },
            { l2Wave, 6 }, { l2Rate, 0.1f }, { l2Dest, 2 }, { l2Amt, 0.3f },
            { fltMode, 2 }, { fltCutoff, 2500.0f }, { fltReso, 0.5f }, { fltEnv, 0.0f },
            { aA, 1.5f }, { aS, 1.0f }, { aR, 3.0f },
            { preDrive, 0.4f }, { preType, 3 }, { postDrive, 0.2f }, { postType, 4 }, { output, -7.0f } } },

        { "Broken Digital Bass", {
            { osc1Wave, 2.0f }, { osc1Shape, 0.5f }, { subLevel, 0.7f }, { subWave, 0 },
            { fltCutoff, 700.0f }, { fltReso, 0.3f }, { fltEnv, 0.5f }, { fD, 0.2f }, { fS, 0.1f },
            { l1Wave, 5 }, { l1Sync, 1 }, { l1Div, 11 }, { l1Dest, 2 }, { l1Amt, 0.4f },
            { preDrive, 0.3f }, { postDrive, 0.75f }, { postType, 4 }, { driveTone, 0.5f },
            { anchorAmt, 0.8f }, { output, -5.0f } } },

        { "Low Orbit", {
            { voiceMode, 2 }, { osc1Wave, 0.6f }, { osc2Wave, 1.0f }, { osc2Fine, 12.0f }, { osc2Level, 0.6f },
            { subLevel, 0.3f }, { subWave, 0 }, { noiseLevel, 0.1f }, { noiseColour, -0.5f },
            { drift, 0.5f }, { instability, 0.15f },
            { fltCutoff, 600.0f }, { fltReso, 0.35f }, { fltEnv, 0.1f },
            { l1Rate, 0.07f }, { l1Dest, 2 }, { l1Amt, 0.4f },
            { aA, 2.5f }, { aS, 1.0f }, { aR, 4.0f }, { preDrive, 0.15f }, { output, -5.0f } } },
        { "Copper Weight", {
            { driveEngine, 1 }, { preType, 0 }, { postType, 0 }, { preDrive, 0.55f }, { postDrive, 0.3f },
            { fltCutoff, 850 }, { anchorAmt, 0.5f }, { driveTilt, -0.2f }, { output, -3 } } },
        { "Split Rail", {
            { driveEngine, 1 }, { preType, 1 }, { postType, 1 }, { preDrive, 0.65f }, { postDrive, 0.4f },
            { preBias, 0.3f }, { fltCutoff, 1500 }, { fltEnv, 0.55f }, { output, -3 } } },
        { "Dead Battery", {
            { driveEngine, 1 }, { preType, 2 }, { postType, 2 }, { preDrive, 0.9f }, { postDrive, 0.75f },
            { aD, 0.8f }, { aS, 0.05f }, { fltCutoff, 3200 }, { anchorAmt, 0.4f }, { output, -3 } } },
        { "Bent Alloy", {
            { driveEngine, 1 }, { preType, 3 }, { postType, 3 }, { preDrive, 0.8f }, { postDrive, 0.4f },
            { preBias, 0.45f }, { postBias, -0.2f }, { osc2Level, 0.3f }, { osc2Semi, 7 },
            { fltCutoff, 4500 }, { anchorAmt, 0.65f }, { output, -4 } } },
        { "Overflow Teeth", {
            { driveEngine, 1 }, { preType, 4 }, { postType, 4 }, { preDrive, 0.7f }, { postDrive, 0.5f },
            { fltCutoff, 3200 }, { driveTone, 0.42f }, { anchorAmt, 0.8f }, { output, -6 } } },
        { "Split Concrete", {
            { driveEngine, 1 }, { chaosSelect, 1 }, { chaosAmount, 0.55f }, { chaosDiv, 8 },
            { fltCutoff, 1600 }, { anchorAmt, 0.65f }, { output, -5 } } },
        { "Clock Dust", {
            { driveEngine, 1 }, { chaosSelect, 2 }, { chaosAmount, 0.6f }, { chaosMix, 0.7f },
            { fltCutoff, 2600 }, { anchorAmt, 0.7f }, { output, -5 } } },
        { "Cone Wound", {
            { driveEngine, 1 }, { chaosSelect, 3 }, { chaosAmount, 0.55f }, { aD, 0.4f }, { aS, 0.2f },
            { fltCutoff, 1900 }, { biteAmt, 0.4f }, { anchorAmt, 0.65f }, { output, -7 } } },
        { "Bent Voltage", {
            { driveEngine, 1 }, { chaosSelect, 4 }, { chaosAmount, 0.4f }, { chaosMix, 0.55f },
            { fltCutoff, 2400 }, { anchorAmt, 0.75f }, { output, -6 } } },
        { "Glass Moths", {
            { driveEngine, 1 }, { chaosSelect, 5 }, { chaosAmount, 0.55f }, { chorusMix, 0.2f },
            { fltCutoff, 3600 }, { aA, 0.08f }, { aR, 1.2f }, { anchorAmt, 0.7f }, { output, -5 } } },
        { "Abandoned Chamber", {
            { driveEngine, 1 }, { chaosSelect, 6 }, { chaosAmount, 0.55f }, { chaosDiv, 8 },
            { fltCutoff, 1400 }, { aR, 0.5f }, { anchorAmt, 0.7f }, { output, -6 } } },
        { "Controlled Collapse", {
            { driveEngine, 1 }, { chaosSelect, 7 }, { chaosAmount, 0.6f }, { chaosMix, 0.65f },
            { fltCutoff, 2100 }, { anchorAmt, 0.8f }, { output, -7 } } },
        { "Wide Current", {
            { driveEngine, 1 }, { chorusMix, 0.4f }, { chorusRate, 0.2f }, { chorusDepth, 0.75f },
            { osc2Level, 0.4f }, { osc2Fine, 7 }, { fltCutoff, 1100 }, { anchorAmt, 0.75f }, { output, -5 } } },
        { "Downshift Static", {
            { driveEngine, 1 }, { shiftMix, 0.3f }, { shiftHz, -65 }, { shiftFeedback, 0.1f },
            { fltCutoff, 3100 }, { anchorAmt, 0.8f }, { output, -5 } } },
        { "Echo Yard", {
            { driveEngine, 1 }, { delayMix, 0.35f }, { delayDiv, 7 }, { delayFeedback, 0.65f },
            { fltCutoff, 1500 }, { aD, 0.25f }, { aS, 0.25f }, { anchorAmt, 0.7f }, { output, -5 } } },
        { "Black Reservoir", {
            { driveEngine, 1 }, { reverbMix, 0.55f }, { reverbSize, 0.8f }, { reverbDamp, 0.8f },
            { reverbPre, 0.04f }, { aA, 0.15f }, { aR, 1.5f }, { fltCutoff, 1200 },
            { voiceMode, 2 }, { anchorAmt, 0.7f }, { output, -6 } } },

        // ---- Phase D: the string-instrument character (all M2; macros start at noon)
        { "Iron Fundamental", {                                         // rounded, weighty bass
            { driveEngine, 1 }, { osc1Wave, 0.55f }, { osc2Wave, 0.0f }, { osc2Oct, -1 }, { osc2Level, 0.35f },
            { subLevel, 0.55f }, { subWave, 0 }, { fltCutoff, 420 }, { fltReso, 0.15f }, { fltEnv, 0.25f },
            { fltKey, 0.4f }, { fltVel, 0.55f }, { fD, 0.35f }, { fS, 0.4f },
            { aD, 0.8f }, { aS, 0.75f }, { aR, 0.12f }, { biteAmt, 0.25f }, { bitePick, 0.15f }, { biteComb, 0.35f }, { biteTone, 0.35f },
            { preDrive, 0.3f }, { preType, 0 }, { postDrive, 0.2f }, { postType, 0 }, { driveTilt, -0.3f },
            { anchorAmt, 0.7f }, { anchorFreq, 90 }, { output, -2.0f } } },
        { "Pickhand Roundwound", {                                      // picked bass
            { driveEngine, 1 }, { voiceMode, 1 }, { glide, 0.03f },
            { osc2Wave, 2.0f }, { osc2Shape, 0.25f }, { osc2Level, 0.3f }, { osc2Fine, 6 }, { subLevel, 0.4f },
            { fltCutoff, 700 }, { fltReso, 0.2f }, { fltEnv, 0.45f }, { fltVel, 0.7f }, { fD, 0.22f }, { fS, 0.3f },
            { aD, 0.9f }, { aS, 0.45f }, { aR, 0.15f }, { ampVel, 0.65f },
            { biteAmt, 0.85f }, { bitePick, 0.7f }, { biteComb, 0.65f }, { biteTone, 0.55f },
            { preDrive, 0.35f }, { preType, 1 }, { postDrive, 0.3f }, { postType, 0 }, { anchorAmt, 0.6f }, { output, -0.5f } } },
        { "Palm Muted Eighths", {                                       // muted bass
            { driveEngine, 1 }, { subLevel, 0.45f }, { fltCutoff, 520 }, { fltReso, 0.2f }, { fltEnv, 0.5f },
            { fltVel, 0.6f }, { fD, 0.12f }, { fS, 0.05f }, { aD, 0.22f }, { aS, 0.0f }, { aR, 0.05f }, { ampVel, 0.6f },
            { biteAmt, 0.6f }, { bitePick, 0.35f }, { biteComb, 0.3f }, { biteTone, 0.25f },
            { preDrive, 0.4f }, { preType, 1 }, { postDrive, 0.25f }, { postType, 1 }, { anchorAmt, 0.65f }, { output, -1.0f } } },
        { "Hammer-On Lead", {                                           // singing legato lead
            { driveEngine, 1 }, { voiceMode, 1 }, { glide, 0.08f }, { osc2Oct, 1 }, { osc2Level, 0.3f }, { osc2Fine, -5 },
            { subLevel, 0.15f }, { fltCutoff, 1800 }, { fltReso, 0.35f }, { fltEnv, 0.3f }, { fltKey, 0.6f }, { fltVel, 0.5f },
            { aA, 0.005f }, { aD, 1.2f }, { aS, 0.8f }, { aR, 0.35f },
            { biteAmt, 0.45f }, { bitePick, 0.3f }, { biteComb, 0.55f }, { biteTone, 0.6f },
            { l1Dest, 1 }, { l1Amt, 0.18f }, { l1Rate, 5.2f }, { l1Fade, 0.9f }, { l1Retrig, 1 },
            { preDrive, 0.45f }, { postDrive, 0.35f }, { postType, 1 }, { driveTone, 0.55f },
            { delayMix, 0.18f }, { reverbMix, 0.15f }, { anchorAmt, 0.3f }, { output, -2.5f } } },
        { "Feedback Sustainer", {                                       // singing lead into resonance
            { driveEngine, 1 }, { voiceMode, 1 }, { glide, 0.12f }, { osc2Wave, 2.0f }, { osc2Level, 0.4f }, { osc2Fine, 4 },
            { fltCutoff, 1300 }, { fltReso, 0.75f }, { fltDrive, 0.5f }, { fltEnv, 0.2f }, { aA, 0.01f }, { aS, 1.0f }, { aR, 0.5f },
            { biteAmt, 0.3f }, { biteComb, 0.8f }, { biteTone, 0.7f },
            { l1Dest, 1 }, { l1Amt, 0.15f }, { l1Rate, 5.6f }, { l1Fade, 1.4f }, { l1Retrig, 1 },
            { l2Dest, 2 }, { l2Amt, 0.15f }, { l2Rate, 0.3f }, { l2Fade, 2.0f },
            { postDrive, 0.5f }, { postType, 2 }, { driveTone, 0.45f }, { reverbMix, 0.2f }, { output, -4.5f } } },
        { "Resonant Pluck", {
            { driveEngine, 1 }, { voiceMode, 2 }, { fltCutoff, 500 }, { fltReso, 0.7f }, { fltEnv, 0.65f }, { fltVel, 0.7f },
            { fD, 0.18f }, { fS, 0.0f }, { aD, 0.6f }, { aS, 0.0f }, { aR, 0.4f },
            { biteAmt, 0.7f }, { bitePick, 0.5f }, { biteComb, 0.75f }, { biteTone, 0.65f }, { preDrive, 0.2f },
            { chorusMix, 0.2f }, { delayMix, 0.2f }, { delayDiv, 7 }, { reverbMix, 0.15f }, { anchorAmt, 0.5f }, { output, -3.0f } } },
        { "Breathing Steel", {                                          // restrained analogue movement
            { driveEngine, 1 }, { osc1Wave, 0.9f }, { osc2Level, 0.5f }, { osc2Fine, 9 }, { drift, 0.35f }, { instability, 0.05f },
            { fltCutoff, 900 }, { fltReso, 0.3f }, { fltEnv, 0.2f }, { aA, 0.02f }, { aS, 0.9f }, { aR, 0.25f },
            { l1Wave, 6 }, { l1Dest, 2 }, { l1Amt, 0.2f }, { l1Rate, 0.25f }, { l1Fade, 1.0f },
            { l2Dest, 1 }, { l2Amt, 0.06f }, { l2Rate, 4.5f }, { l2Fade, 1.5f }, { l2Retrig, 1 },
            { preDrive, 0.25f }, { anchorAmt, 0.5f }, { output, -4 } } },
        { "Bowed Cable Drone", {                                        // metallic string drone
            { driveEngine, 1 }, { voiceMode, 2 }, { osc1Wave, 2.0f }, { osc1Shape, 0.3f },
            { osc2Sync, 1 }, { osc2Oct, 1 }, { osc2Semi, 5 }, { osc2Level, 0.5f },
            { l1Wave, 1 }, { l1Dest, 7 }, { l1Amt, 0.25f }, { l1Rate, 0.12f },
            { noiseLevel, 0.1f }, { noiseColour, 0.4f }, { biteAmt, 0.5f }, { bitePick, 0.0f }, { biteComb, 0.9f }, { biteTone, 0.75f },
            { fltMode, 2 }, { fltCutoff, 2200 }, { fltReso, 0.4f }, { fltEnv, 0.0f },
            { aA, 1.2f }, { aS, 1.0f }, { aR, 3.0f }, { preDrive, 0.3f }, { preType, 3 },
            { shiftMix, 0.2f }, { shiftHz, 3.5f }, { reverbMix, 0.35f }, { reverbSize, 0.8f }, { output, -5.0f } } },
        { "Tension Wire", {
            { driveEngine, 1 }, { voiceMode, 2 }, { osc2Semi, 7 }, { osc2Level, 0.4f },
            { biteAmt, 0.4f }, { biteComb, 0.85f }, { biteTone, 0.8f }, { fltCutoff, 3000 }, { fltReso, 0.25f }, { fltEnv, 0.0f },
            { aA, 0.6f }, { aS, 1.0f }, { aR, 2.5f }, { preDrive, 0.55f }, { preType, 3 }, { preBias, 0.3f },
            { chorusMix, 0.3f }, { reverbMix, 0.3f }, { output, -8 } } },
        { "Sodium Horizon", {                                           // wide cinematic texture
            { driveEngine, 1 }, { voiceMode, 2 }, { osc1Wave, 0.6f }, { osc2Level, 0.5f }, { osc2Fine, 11 }, { drift, 0.45f },
            { fltCutoff, 750 }, { fltReso, 0.3f }, { fltEnv, 0.15f }, { aA, 1.8f }, { aS, 1.0f }, { aR, 4.0f },
            { l1Dest, 2 }, { l1Amt, 0.3f }, { l1Rate, 0.08f },
            { chorusMix, 0.45f }, { chorusDepth, 0.7f }, { delayMix, 0.2f }, { delayDiv, 4 }, { delayFeedback, 0.55f },
            { reverbMix, 0.5f }, { reverbSize, 0.9f }, { reverbDamp, 0.7f }, { anchorAmt, 0.7f }, { output, -8 } } },
        { "Riot Gear", {                                                // CHAOS performance bass: play PUNCH
            { driveEngine, 1 }, { fltCutoff, 900 }, { fltEnv, 0.5f }, { fltReso, 0.5f }, { biteAmt, 0.5f },
            { preDrive, 0.45f }, { preType, 1 }, { postDrive, 0.45f }, { postType, 2 },
            { chaosSelect, 1 }, { chaosAmount, 0.35f }, { chaosDiv, 11 }, { chaosMix, 0.8f }, { anchorAmt, 0.8f }, { output, -5 } } },
        { "Swarm Choir", {
            { driveEngine, 1 }, { voiceMode, 2 }, { osc2Level, 0.4f }, { osc2Fine, 8 }, { fltCutoff, 1600 },
            { aA, 0.3f }, { aS, 1.0f }, { aR, 2.0f },
            { chaosMode, 1 }, { chaosMorph, 5.5f }, { chaosAmount, 0.5f }, { reverbMix, 0.3f }, { anchorAmt, 0.6f }, { output, -8 } } },
        { "Meltdown Rail", {                                            // low MELTDOWN: open it with the CHAOS macro
            { driveEngine, 1 }, { fltCutoff, 1100 }, { fltEnv, 0.4f }, { postDrive, 0.4f }, { postType, 1 },
            { chaosSelect, 7 }, { chaosAmount, 0.25f }, { anchorAmt, 0.85f }, { output, -5 } } },

        // ---- QA Part 3: cinematic bank, indices 36–55. Every macro starts at noon.
        // Slow-bloom low pads: the foundation stays centred while the upper voice opens.
        cinematic ("Silt Lantern", {
            { osc1Wave, 0.35f }, { osc2Wave, 0.8f }, { osc2Fine, -9 }, { subLevel, 0.4f },
            { fltCutoff, 420 }, { fltEnv, 0.5f }, { fA, 10 }, { fD, 10 }, { fS, 0.7f },
            { aA, 4.5f }, { aR, 4 }, { l1Rate, 0.035f }, { l1Amt, 0.4f },
            { l2Dest, 4 }, { l2Amt, 0.32f }, { l2Rate, 0.055f }, { output, -2.83f } }),
        cinematic ("Mantle Bloom", {
            { osc1Wave, 0.65f }, { osc2Wave, 0.1f }, { osc2Oct, 1 }, { osc2Level, 0.42f },
            { fltCutoff, 680 }, { fltEnv, 0.4f }, { fA, 9 }, { fD, 10 }, { fS, 0.8f },
            { aA, 3.8f }, { aR, 4.5f }, { l1Wave, 1 }, { l1Rate, 0.038f }, { l1Amt, 0.42f },
            { l2Dest, 7 }, { l2Amt, 0.08f }, { l2Rate, 0.06f }, { chorusMix, 0.27f }, { output, -1.71f } }),
        cinematic ("Submerged Filament", {
            { osc1Wave, 1.65f }, { osc1Shape, 0.32f }, { osc2Wave, 0.4f }, { osc2Fine, 12 },
            { fltCutoff, 380 }, { fltEnv, 0.5f }, { fA, 10 }, { fD, 9 }, { fS, 0.6f },
            { aA, 5 }, { aR, 3.5f }, { l1Rate, 0.052f }, { l1Amt, 0.38f },
            { l2Dest, 4 }, { l2Amt, 0.48f }, { l2Rate, 0.032f }, { reverbMix, 0.38f }, { output, 0.17f } }),
        cinematic ("Horizon Embers", {
            { osc1Wave, 0.25f }, { osc2Wave, 1.0f }, { osc2Semi, 7 }, { osc2Level, 0.24f },
            { fltCutoff, 720 }, { fltReso, 0.32f }, { fltEnv, 0.35f }, { fA, 8.5f }, { fS, 0.7f },
            { aA, 3 }, { aR, 5 }, { l1Wave, 6 }, { l1Rate, 0.07f }, { l1Amt, 0.5f },
            { l2Wave, 1 }, { l2Dest, 3 }, { l2Rate, 0.041f }, { l2Amt, 0.38f },
            { shiftMix, 0.12f }, { shiftHz, 1.2f }, { output, -0.59f } }),

        // Metallic string drones: comb body, bowed attacks and slow inharmonic beating.
        cinematic ("Rosin on Iron", {
            { osc1Wave, 1.3f }, { osc1Shape, 0.2f }, { osc2Wave, 2 }, { osc2Fine, -6 },
            { biteAmt, 0.72f }, { bitePick, 0 }, { biteComb, 0.9f }, { biteTone, 0.62f },
            { fltCutoff, 1700 }, { fltReso, 0.3f }, { fltEnv, 0.1f }, { aA, 2.8f }, { aR, 4 },
            { l1Dest, 8 }, { l1Rate, 0.055f }, { l1Amt, 0.35f }, { l2Wave, 0 }, { l2Dest, 2 }, { l2Amt, 0.3f },
            { shiftMix, 0.22f }, { shiftHz, 2.3f }, { shiftFeedback, 0.06f }, { reverbMix, 0.3f }, { output, -3.24f } }),
        cinematic ("Suspended Rivets", {
            { osc1Wave, 1.8f }, { osc1Shape, 0.25f }, { osc2Sync, 1 }, { osc2Oct, 1 }, { osc2Semi, 7 },
            { biteAmt, 0.65f }, { bitePick, 0.18f }, { biteComb, 0.86f }, { biteTone, 0.7f },
            { fltCutoff, 2200 }, { fltReso, 0.22f }, { fltEnv, -0.16f }, { fA, 6 },
            { aA, 0.025f }, { aD, 8 }, { aS, 0.65f }, { aR, 3.5f },
            { l1Dest, 7 }, { l1Wave, 1 }, { l1Rate, 0.04f }, { l1Amt, 0.22f },
            { l2Dest, 8 }, { l2Amt, 0.28f }, { l2Rate, 0.065f },
            { shiftMix, 0.2f }, { shiftHz, -3.1f }, { delayMix, 0.2f }, { delayDiv, 2 }, { output, 0.3f } }),
        cinematic ("Tidal Armature", {
            { osc1Wave, 0.9f }, { osc2Wave, 1.9f }, { osc2Shape, 0.6f }, { osc2Semi, 12 }, { osc2Level, 0.28f },
            { biteAmt, 0.8f }, { bitePick, 0.02f }, { biteComb, 0.94f }, { biteTone, 0.5f },
            { fltCutoff, 1400 }, { fltEnv, 0.3f }, { aA, 4 }, { aR, 4.5f },
            { l1Dest, 8 }, { l1Wave, 1 }, { l1Rate, 0.028f }, { l1Amt, 0.5f },
            { l2Dest, 4 }, { l2Rate, 0.09f }, { l2Amt, 0.4f },
            { shiftMix, 0.25f }, { shiftHz, 0.8f }, { reverbSize, 0.88f }, { output, -3.69f } }),
        cinematic ("Glass Wound Coil", {
            { osc1Wave, 0.45f }, { osc2Wave, 1 }, { osc2Oct, 1 }, { osc2Semi, 7 }, { osc2Level, 0.3f },
            { biteAmt, 0.7f }, { bitePick, 0.12f }, { biteComb, 0.88f }, { biteTone, 0.8f },
            { fltCutoff, 2400 }, { fltEnv, -0.18f }, { aA, 0.12f }, { aD, 7 }, { aS, 0.72f }, { aR, 3 },
            { l1Dest, 9 }, { l1Rate, 0.045f }, { l1Amt, 0.22f }, { l2Dest, 7 }, { l2Amt, 0.13f },
            { shiftMix, 0.18f }, { shiftHz, 4.7f }, { shiftFeedback, 0.08f }, { reverbMix, 0.4f }, { output, -1.1f } }),

        // Pulsing beds: a song-locked pulse underneath a much slower colour change.
        cinematic ("Relay Cathedral", {
            { osc1Wave, 1.65f }, { osc2Wave, 0.8f }, { fltCutoff, 1100 }, { fltEnv, 0.15f },
            { aA, 2 }, { aR, 3.5f }, { l1Dest, 5 }, { l1Wave, 0 }, { l1Sync, 1 }, { l1Div, 5 }, { l1Amt, 0.65f },
            { l2Dest, 2 }, { l2Wave, 1 }, { l2Rate, 0.035f }, { l2Amt, 0.48f },
            { delayMix, 0.24f }, { delayDiv, 7 }, { delayFeedback, 0.46f }, { reverbMix, 0.4f }, { output, 1.71f } }),
        cinematic ("Braided Signal", {
            { osc1Wave, 1 }, { osc2Wave, 1.6f }, { osc2Semi, 7 }, { osc2Level, 0.27f },
            { fltCutoff, 650 }, { fltReso, 0.35f }, { fltEnv, 0.15f }, { aA, 1.5f }, { aR, 3 },
            { l1Dest, 2 }, { l1Wave, 1 }, { l1Sync, 1 }, { l1Div, 4 }, { l1Amt, 0.5f },
            { l2Dest, 3 }, { l2Wave, 0 }, { l2Rate, 0.026f }, { l2Amt, 0.5f },
            { delayMix, 0.26f }, { delayDiv, 6 }, { delayFeedback, 0.4f }, { chorusMix, 0.12f }, { output, -0.4f } }),
        cinematic ("Clockwork Moss", {
            { osc1Wave, 0.4f }, { osc2Wave, 2 }, { osc2Shape, 0.4f }, { noiseLevel, 0.035f },
            { biteAmt, 0.5f }, { biteComb, 0.78f }, { fltCutoff, 900 }, { fltEnv, 0.3f }, { fA, 9 },
            { aA, 2.5f }, { aR, 4 }, { l1Dest, 5 }, { l1Wave, 1 }, { l1Sync, 1 }, { l1Div, 7 }, { l1Amt, 0.55f },
            { l2Dest, 8 }, { l2Wave, 0 }, { l2Rate, 0.043f }, { l2Amt, 0.4f },
            { delayMix, 0.2f }, { delayDiv, 8 }, { reverbMix, 0.3f }, { output, -1.58f } }),

        // Dark atmospheres: restrained VOID, gentle drive, breath and slow filter wander.
        cinematic ("Ashfall Aperture", {
            { osc1Wave, 0.2f }, { osc2Wave, 1.1f }, { osc2Level, 0.28f }, { subLevel, 0.4f },
            { noiseLevel, 0.055f }, { noiseColour, -0.85f }, { fltCutoff, 430 }, { fltEnv, 0.15f },
            { aA, 3.5f }, { aR, 5 }, { l1Wave, 6 }, { l1Rate, 0.04f }, { l1Amt, 0.6f },
            { l2Dest, 3 }, { l2Rate, 0.03f }, { l2Amt, 0.25f },
            { chaosSelect, 6 }, { chaosAmount, 0.18f }, { chaosMix, 0.35f },
            { reverbMix, 0.48f }, { reverbSize, 0.9f }, { reverbDamp, 0.9f }, { output, -3.36f } }),
        cinematic ("Nightwell Vapour", {
            { osc1Wave, 1.6f }, { osc1Shape, 0.5f }, { osc2Wave, 0.2f }, { osc2Fine, -13 },
            { fltCutoff, 580 }, { fltReso, 0.32f }, { fltEnv, -0.12f }, { fA, 9 }, { fD, 10 },
            { aA, 4 }, { aR, 5 }, { l1Dest, 4 }, { l1Rate, 0.027f }, { l1Amt, 0.55f },
            { l2Dest, 2 }, { l2Rate, 0.065f }, { l2Amt, 0.38f },
            { chaosSelect, 6 }, { chaosAmount, 0.12f }, { chaosMix, 0.3f },
            { shiftMix, 0.16f }, { shiftHz, -1.7f }, { reverbMix, 0.5f }, { output, 1.05f } }),
        cinematic ("Unlit Atrium", {
            { osc1Wave, 0.75f }, { osc2Wave, 0.45f }, { osc2Oct, -1 }, { osc2Level, 0.22f }, { subLevel, 0.22f },
            { noiseLevel, 0.04f }, { noiseColour, -0.7f }, { fltCutoff, 780 }, { fltReso, 0.4f }, { fltEnv, -0.2f },
            { aA, 3 }, { aR, 4.5f }, { l1Wave, 1 }, { l1Rate, 0.023f }, { l1Amt, 0.5f },
            { l2Dest, 9 }, { l2Amt, 0.18f }, { l2Rate, 0.047f },
            { chaosSelect, 6 }, { chaosAmount, 0.16f }, { chaosMix, 0.25f },
            { delayMix, 0.18f }, { delayDiv, 2 }, { reverbMix, 0.42f }, { reverbDamp, 0.88f }, { output, -0.15f } }),

        // Tension/rises: ten-second envelope lift plus a slower, repeatable saw or triangle.
        cinematic ("Winding Pressure", {
            { osc1Wave, 1.7f }, { osc2Wave, 1 }, { osc2Semi, 1 }, { osc2Level, 0.2f },
            { fltCutoff, 500 }, { fltReso, 0.42f }, { fltEnv, 0.65f }, { fA, 10 }, { fD, 10 }, { fS, 0.9f },
            { aA, 3.5f }, { aR, 4 }, { l1Wave, 3 }, { l1Rate, 0.025f }, { l1Amt, 0.65f },
            { l2Dest, 7 }, { l2Wave, 3 }, { l2Rate, 0.022f }, { l2Amt, 0.18f },
            { biteComb, 0.82f }, { shiftMix, 0.12f }, { shiftHz, 2 }, { output, -0.49f } }),
        cinematic ("Ascending Cable", {
            { osc1Wave, 0.6f }, { osc2Sync, 1 }, { osc2Wave, 1.8f }, { osc2Oct, 1 }, { osc2Level, 0.3f },
            { biteAmt, 0.65f }, { biteComb, 0.88f }, { biteTone, 0.6f },
            { fltCutoff, 1300 }, { fltEnv, 0.35f }, { fA, 10 }, { fS, 0.9f }, { aA, 2.5f }, { aR, 4 },
            { l1Dest, 7 }, { l1Wave, 3 }, { l1Rate, 0.025f }, { l1Amt, 0.38f },
            { l2Dest, 2 }, { l2Wave, 1 }, { l2Rate, 0.032f }, { l2Amt, 0.4f },
            { preDrive, 0.27f }, { shiftMix, 0.2f }, { shiftHz, 1.5f }, { output, -1.31f } }),
        cinematic ("Faultline Halo", {
            { osc1Wave, 1.2f }, { osc2Wave, 1.6f }, { osc2Semi, 7 }, { osc2Fine, -11 },
            { fltCutoff, 900 }, { fltReso, 0.38f }, { fltEnv, 0.5f }, { fA, 10 }, { fD, 10 }, { fS, 0.85f },
            { aA, 4 }, { aR, 5 }, { l1Dest, 6 }, { l1Wave, 3 }, { l1Rate, 0.024f }, { l1Amt, 0.3f },
            { l2Dest, 2 }, { l2Wave, 3 }, { l2Rate, 0.027f }, { l2Amt, 0.55f },
            { preDrive, 0.3f }, { preType, 1 }, { postDrive, 0.15f }, { reverbMix, 0.42f }, { output, 0.4f } }),

        // Chaos-textured beds: a stable string/sub voice under a quiet, slowly moving fracture.
        cinematic ("Moth Current", {
            { osc1Wave, 0.55f }, { osc2Wave, 1.4f }, { osc2Fine, 10 }, { drift, 0.4f },
            { fltCutoff, 1100 }, { fltEnv, 0.25f }, { aA, 3 }, { aR, 4 },
            { l1Wave, 1 }, { l1Rate, 0.036f }, { l1Amt, 0.35f }, { l2Dest, 3 }, { l2Rate, 0.06f }, { l2Amt, 0.45f },
            { chaosMode, 1 }, { chaosMorph, 5.15f }, { chaosAmount, 0.22f }, { chaosMix, 0.45f },
            { shiftMix, 0.1f }, { shiftHz, 2.6f }, { anchorAmt, 0.72f }, { output, -1.69f } }),
        cinematic ("Velvet Debris", {
            { osc1Wave, 1.7f }, { osc1Shape, 0.35f }, { osc2Wave, 0.25f }, { osc2Level, 0.4f },
            { fltCutoff, 700 }, { fltReso, 0.3f }, { fltEnv, 0.15f }, { aA, 4.5f }, { aR, 5 },
            { l1Wave, 6 }, { l1Rate, 0.055f }, { l1Amt, 0.48f }, { l2Dest, 4 }, { l2Rate, 0.031f }, { l2Amt, 0.5f },
            { chaosMode, 1 }, { chaosMorph, 5.65f }, { chaosAmount, 0.18f }, { chaosMix, 0.4f },
            { chorusMix, 0.28f }, { reverbMix, 0.4f }, { anchorAmt, 0.7f }, { output, -1.95f } }),
        cinematic ("Ghosts in the Busbar", {
            { osc1Wave, 0.8f }, { osc2Wave, 1.6f }, { osc2Semi, 7 }, { osc2Level, 0.28f },
            { biteAmt, 0.55f }, { biteComb, 0.85f }, { fltCutoff, 1500 }, { fltEnv, 0.1f }, { aA, 2.5f }, { aR, 4 },
            { l1Dest, 8 }, { l1Wave, 1 }, { l1Rate, 0.03f }, { l1Amt, 0.42f },
            { l2Dest, 2 }, { l2Wave, 0 }, { l2Rate, 0.065f }, { l2Amt, 0.4f },
            { chaosMode, 1 }, { chaosMorph, 5.4f }, { chaosAmount, 0.26f }, { chaosMix, 0.38f },
            { delayMix, 0.22f }, { delayDiv, 4 }, { delayFeedback, 0.48f }, { reverbMix, 0.38f }, { output, -3.35f } }),
    };
    return list;
}

void applyPreset (juce::AudioProcessorValueTreeState& state, const FactoryPreset& p)
{
    for (auto* param : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            rp->setValueNotifyingHost (rp->getDefaultValue());

    for (const auto& [id, value] : p.values)
        if (auto* rp = state.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
}
