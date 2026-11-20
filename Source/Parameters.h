#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Stable parameter IDs. NEVER rename or reuse an ID once shipped: Logic stores
// automation and presets against these. New parameters get new IDs.
namespace PID
{
    // Oscillators
    inline constexpr auto osc1Wave = "osc1_wave", osc1Shape = "osc1_shape", osc1Oct = "osc1_oct",
                          osc1Semi = "osc1_semi", osc1Fine = "osc1_fine", osc1Level = "osc1_level";
    inline constexpr auto osc2Wave = "osc2_wave", osc2Shape = "osc2_shape", osc2Oct = "osc2_oct",
                          osc2Semi = "osc2_semi", osc2Fine = "osc2_fine", osc2Level = "osc2_level",
                          osc2Sync = "osc2_sync";
    inline constexpr auto subLevel = "sub_level", subOct = "sub_oct", subWave = "sub_wave";
    inline constexpr auto noiseLevel = "noise_level", noiseColour = "noise_colour";

    // Voicing / performance
    inline constexpr auto voiceMode = "voice_mode", glide = "glide", pbRange = "pb_range",
                          drift = "drift", instability = "instability";

    // Filter
    inline constexpr auto fltMode = "flt_mode", fltCutoff = "flt_cutoff", fltReso = "flt_reso",
                          fltEnv = "flt_env", fltKey = "flt_key", fltVel = "flt_vel", fltDrive = "flt_drive";

    // Envelopes
    inline constexpr auto fA = "fenv_a", fD = "fenv_d", fS = "fenv_s", fR = "fenv_r";
    inline constexpr auto aA = "aenv_a", aD = "aenv_d", aS = "aenv_s", aR = "aenv_r", ampVel = "amp_vel";

    // LFOs
    inline constexpr auto l1Wave = "lfo1_wave", l1Rate = "lfo1_rate", l1Sync = "lfo1_sync", l1Div = "lfo1_div",
                          l1Dest = "lfo1_dest", l1Amt = "lfo1_amt";
    inline constexpr auto l2Wave = "lfo2_wave", l2Rate = "lfo2_rate", l2Sync = "lfo2_sync", l2Div = "lfo2_div",
                          l2Dest = "lfo2_dest", l2Amt = "lfo2_amt";
    inline constexpr auto mwLfo = "mw_lfo", mwCutoff = "mw_cutoff";

    // String Bite
    inline constexpr auto biteAmt = "bite_amt", bitePick = "bite_pick", biteComb = "bite_comb", biteTone = "bite_tone";

    // Saturation
    inline constexpr auto preDrive = "pre_drive", preType = "pre_type",
                          postDrive = "post_drive", postType = "post_type",
                          driveTone = "drive_tone", driveMix = "drive_mix", driveComp = "drive_comp";

    inline constexpr auto driveEngine = "drive_engine", preBias = "pre_bias", postBias = "post_bias", driveTilt = "drive_tilt";

    // CHAOS and effects (M3)
    inline constexpr auto chaosSelect = "chaos_select";
    inline constexpr auto chaosMode = "chaos_mode";
    inline constexpr auto chaosMorph = "chaos_morph";
    inline constexpr auto chaosAmount = "chaos_amount";
    inline constexpr auto chaosDiv = "chaos_div";
    inline constexpr auto chaosPunch = "chaos_punch";
    inline constexpr auto chaosHold = "chaos_hold";
    inline constexpr auto chaosHoldPitch = "chaos_hold_pitch";
    inline constexpr auto chaosHoldSize = "chaos_hold_size";
    inline constexpr auto chaosMix = "chaos_mix";
    inline constexpr auto delayMix = "delay_mix";
    inline constexpr auto delayTime = "delay_time";
    inline constexpr auto delaySync = "delay_sync";
    inline constexpr auto delayDiv = "delay_div";
    inline constexpr auto delayFeedback = "delay_feedback";
    inline constexpr auto delayPing = "delay_ping";
    inline constexpr auto delayTone = "delay_tone";
    inline constexpr auto reverbMix = "reverb_mix";
    inline constexpr auto reverbPre = "reverb_predelay";
    inline constexpr auto reverbSize = "reverb_size";
    inline constexpr auto reverbDamp = "reverb_damp";
    inline constexpr auto chorusMix = "chorus_mix";
    inline constexpr auto chorusRate = "chorus_rate";
    inline constexpr auto chorusDepth = "chorus_depth";
    inline constexpr auto shiftMix = "shift_mix";
    inline constexpr auto shiftHz = "shift_hz";
    inline constexpr auto shiftFeedback = "shift_feedback";

    // Macros and LFO articulation (M4)
    inline constexpr auto macroWeight = "macro_weight", macroBite = "macro_bite", macroAcid = "macro_acid",
                          macroDrive = "macro_drive", macroChaos = "macro_chaos", macroMotion = "macro_motion",
                          macroSpace = "macro_space", macroOutput = "macro_output";
    inline constexpr auto l1Retrig = "lfo1_retrig", l1Fade = "lfo1_fade", l2Retrig = "lfo2_retrig", l2Fade = "lfo2_fade";

    // Sub Anchor / master
    inline constexpr auto anchorAmt = "anchor_amt", anchorFreq = "anchor_freq", output = "output";
}

namespace Choices
{
    inline const juce::StringArray voiceMode { "Mono", "Legato", "Poly" };
    inline const juce::StringArray subOct    { "-1 Oct", "-2 Oct" };
    inline const juce::StringArray subWave   { "Sine", "Triangle", "Square" };
    inline const juce::StringArray fltMode   { "LP 24", "LP 12", "BP 12", "BP 24", "HP 12", "HP 24" };
    inline const juce::StringArray lfoWave   { "Sine", "Triangle", "Saw Down", "Saw Up", "Square", "Sample & Hold", "Drift" };
    inline const juce::StringArray lfoDest   { "Off", "Pitch", "Cutoff", "Osc Wave", "Pulse Width", "Amp",
                                               "Pre Drive", "Osc 2 Pitch", "Comb", "Resonance" };
    inline const juce::StringArray lfoDiv    { "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4 D", "1/4", "1/4 T",
                                               "1/8 D", "1/8", "1/8 T", "1/16 D", "1/16", "1/16 T", "1/32" };
    inline const juce::StringArray chaos { "BYPASS", "FRACTURE", "CRUSH", "TEAR", "FOLD", "SWARM", "VOID", "MELTDOWN" };
    inline const juce::StringArray chaosMode { "Stepped", "Morph" };
    inline const juce::StringArray driveType { "Warm", "Asymmetric", "Fuzz", "Fold", "Digital" };
}

// Length of each LFO division in quarter notes, index-matched to Choices::lfoDiv.
inline constexpr double kDivQuarters[] { 16.0, 8.0, 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0,
                                         0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125 };

// Macro IDs in panel order: WEIGHT, BITE, ACID, DRIVE, CHAOS, MOTION, SPACE, OUTPUT.
inline constexpr const char* kMacroIds[8] { PID::macroWeight, PID::macroBite, PID::macroAcid, PID::macroDrive,
                                            PID::macroChaos, PID::macroMotion, PID::macroSpace, PID::macroOutput };

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
