// Hairball: parameter table. One source of truth for the engine, the plugin (APVTS), the mockup and the manual.
// Pure C++17, no JUCE. Every entry is a host parameter, so everything is automatable.
#pragma once
#include <cstddef>

namespace hb
{
struct ParamInfo
{
    const char* id;      // stable id (never rename: saved projects use it)
    const char* name;    // label on the panel
    const char* group;   // panel section
    float lo, hi, def;   // range and default, in the unit below
    const char* unit;    // "", "ms", "s", "st", "%", "dB", "Hz", "x"
    const char* tip;     // info bubble text
    bool choice;         // true: integer choice, labels in choices
    const char* choices; // '|' separated labels for choices
};

// Order matters for the Params struct below only through ids; the engine reads by index constants.
enum P
{
    // Source
    pMode, pLiveLen, pFreeze, pHold, pCaptureOffset,
    // Grains
    pAttack, pGrainLen, pSpacing, pWaveSpacing, pPan,
    // Effects
    pLfoDepth, pLfoSpeed, pRand,
    // Transients
    pTransMode, pTransHold,
    // Time
    pLoop, pKeyTo, pStart, pRoot,
    // Pitch
    pPitch, pFine, pPitchEnvAmt, pPitchEnvTime, pPitchEnvCurve, pGlide,
    // Riser
    pRiseOn, pRiseTime, pRiseCurve, pRisePitch, pRiseGrain, pRiseTone, pRiseHz, pRiseFilter, pRiseKey,
    // Amp (instrument)
    pAmpA, pAmpD, pAmpS, pAmpR, pVoices,
    // Thicken
    pWidth, pBassMono, pLowG, pMidG, pMidF, pMidQ, pHighG,
    // Filter
    pFiltType, pCutoff, pReso,
    // Colour (PurrOne stages)
    pDrive, pTransistor, pFuzz,
    // Dance
    pDance,
    // Output
    pMix, pOut, pSafeLim, pNasty,
    pCount
};

static const ParamInfo kParams[pCount] = {
    // id, name, group, lo, hi, def, unit, tip, choice, choices
    { "mode", "Mode", "Source", 0, 1, 0, "", "Instrument plays the loaded sample with MIDI. FX works on the audio passing through the track.", true, "Instrument|FX" },
    { "liveLen", "Capture", "Source", 0.5f, 8, 4, "s", "How many seconds of the incoming audio Hairball keeps ready to freeze (FX mode).", false, "" },
    { "freeze", "Freeze", "Source", 0, 1, 0, "", "FX mode: stop recording and keep looping what is in the buffer. Grains then play only the frozen audio.", true, "Off|On" },
    { "hold", "Hold", "Time", 0, 1, 0, "", "Holds the playing position still, like Wave Spacing at 0 for a moment. Automate it for stutters and stalls.", true, "Off|On" },
    { "capOff", "Lag", "Source", 0, 2000, 120, "ms", "FX mode, not frozen: how far behind the live audio the grains read.", false, "" },

    { "attack", "Attack", "Grains", 0, 250, 20, "ms", "Fade in and fade out of each grain. Added to the grain length on both ends.", false, "" },
    { "grainLen", "Hold len", "Grains", 2, 800, 80, "ms", "Length each grain plays before the next one takes over.", false, "" },
    { "spacing", "Gr. spacing", "Grains", 25, 400, 100, "%", "Gap between grains. 100 percent places them end to end with a clean crossfade. Lower means more overlap and a thicker sound, higher means gaps.", false, "" },
    { "waveSpacing", "Wave spacing", "Grains", -300, 300, 100, "%", "How fast the playing position moves through the sound. 100 is normal, 0 freezes, negative plays the grains backwards in order, 300 is three times faster.", false, "" },
    { "pan", "Pan", "Grains", 0, 100, 0, "%", "Odd grains go left, even grains go right. 0 is off.", false, "" },

    { "lfoDepth", "Depth", "Effects", 0, 300, 0, "%", "Size of the LFO that moves Wave spacing. 0 is off.", false, "" },
    { "lfoSpeed", "Speed", "Effects", 0.05f, 30, 2, "Hz", "Speed of the LFO on Wave spacing.", false, "" },
    { "rand", "Rand", "Effects", 0, 100, 0, "%", "Randomises where each grain starts, so the order is shuffled. 0 is off.", false, "" },

    { "transMode", "Transients", "Transients", 0, 2, 0, "", "Grains that cross a hit snap to it and take the Transient length. Off, Regions (slices you provide) or Detect (automatic).", true, "Off|Regions|Detect" },
    { "transHold", "Trans. len", "Transients", 2, 400, 40, "ms", "Length of grains that start on a transient.", false, "" },

    { "loop", "Loop", "Time", 0, 1, 1, "", "Loop the sound when the position reaches the end.", true, "Off|On" },
    { "keyTo", "Key to", "Time", 0, 3, 0, "", "What the MIDI keys do: Pitch, Percent (C5 to C7 pick the start position), Step (C5 and up offset the start by steps), Transient (C5 and up pick a slice).", true, "Pitch|Percent|Step|Transient" },
    { "start", "Start", "Time", 0, 100, 0, "%", "Where in the sound playing starts.", false, "" },
    { "root", "Root key", "Time", 24, 96, 60, "", "The MIDI note that plays the sound at its original pitch.", false, "" },

    { "pitch", "Pitch", "Pitch", -48, 48, 0, "st", "Pitch in semitones. Grains are read faster or slower; the position still moves at Wave spacing.", false, "" },
    { "fine", "Fine", "Pitch", -100, 100, 0, "ct", "Fine tune in cents.", false, "" },
    { "pitchEnv", "Env amt", "Pitch", -48, 48, 0, "st", "Pitch envelope. Starts this many semitones away from the pitch and glides back to it.", false, "" },
    { "pitchEnvT", "Env time", "Pitch", 5, 8000, 300, "ms", "How long the pitch envelope takes to return to the set pitch.", false, "" },
    { "pitchEnvC", "Env curve", "Pitch", 0.2f, 5, 1, "x", "Shape of the pitch envelope. Below 1 snaps back fast, above 1 lingers away from the pitch.", false, "" },
    { "glide", "Glide", "Pitch", 0, 2000, 0, "ms", "Instrument: time to slide between notes.", false, "" },

    { "riseOn", "Riser", "Riser", 0, 1, 0, "", "Starts the riser. In Instrument mode every note starts it. Turn it off and it eases back.", true, "Off|On" },
    { "riseTime", "Time", "Riser", 0.1f, 32, 4, "s", "How long the riser takes to reach its end.", false, "" },
    { "riseCurve", "Curve", "Riser", 0.2f, 5, 1.6f, "x", "Below 1 jumps up early, above 1 builds slowly and shoots up at the end.", false, "" },
    { "risePitch", "Pitch up", "Riser", 0, 60, 24, "st", "How many semitones the riser lifts the pitch.", false, "" },
    { "riseGrain", "Grains", "Riser", 0, 100, 70, "%", "How far the grains shrink and speed up as the riser builds.", false, "" },
    { "riseTone", "Tone", "Riser", 0, 100, 60, "%", "Grains get so close they turn into a pitched tone at the Tone Hz (or the key you play). 0 stays granular.", false, "" },
    { "riseHz", "Tone Hz", "Riser", 40, 2000, 440, "Hz", "Pitch the grains settle on at the top of the riser (FX mode).", false, "" },
    { "riseFilter", "Filter up", "Riser", 0, 6, 2, "oct", "Opens the filter by this many octaves while the riser builds.", false, "" },
    { "riseKey", "Follow key", "Riser", 0, 1, 1, "", "Instrument: the riser tone follows the key you play.", true, "Off|On" },

    { "ampA", "Attack", "Amp", 0, 4000, 5, "ms", "Instrument: note fade in.", false, "" },
    { "ampD", "Decay", "Amp", 0, 4000, 200, "ms", "Instrument: fall to the sustain level.", false, "" },
    { "ampS", "Sustain", "Amp", 0, 100, 100, "%", "Instrument: level while the key is held.", false, "" },
    { "ampR", "Release", "Amp", 0, 8000, 250, "ms", "Instrument: note fade out.", false, "" },
    { "voices", "Voices", "Amp", 1, 8, 4, "", "Instrument: how many notes can play at once. Fewer voices use less CPU.", false, "" },

    { "width", "Width", "Thicken", 0, 200, 100, "%", "Mid/side width. 100 is unchanged, 0 is mono, 200 is double the side signal.", false, "" },
    { "bassMono", "Bass mono", "Thicken", 0, 400, 0, "Hz", "Keeps everything below this frequency in the middle. 0 is off.", false, "" },
    { "lowG", "Low", "Thicken", -18, 18, 0, "dB", "Low shelf at 120 Hz.", false, "" },
    { "midG", "Mid", "Thicken", -18, 18, 0, "dB", "Bell filter gain.", false, "" },
    { "midF", "Mid Hz", "Thicken", 100, 8000, 1000, "Hz", "Bell filter frequency.", false, "" },
    { "midQ", "Mid Q", "Thicken", 0.3f, 8, 1, "", "Bell filter width. Higher is narrower.", false, "" },
    { "highG", "High", "Thicken", -18, 18, 0, "dB", "High shelf at 8 kHz.", false, "" },

    { "filtType", "Filter", "Filter", 0, 3, 0, "", "Off, Low pass, High pass or Band pass.", true, "Off|LP|HP|BP" },
    { "cutoff", "Cutoff", "Filter", 20, 20000, 12000, "Hz", "Filter cutoff. The riser can open it by itself.", false, "" },
    { "reso", "Reso", "Filter", 0.5f, 12, 0.7f, "", "Filter resonance.", false, "" },

    { "drive", "Drive", "Colour", 0, 200, 0, "%", "Symmetric analog clip. Odd harmonics, loud and bright. Above 100 percent needs Nasty range.", false, "" },
    { "transistor", "Transistor", "Colour", 0, 200, 0, "%", "Neve style asymmetric stage. Even harmonics, warm and thick. Above 100 percent needs Nasty range.", false, "" },
    { "fuzz", "Fuzz", "Colour", 0, 200, 0, "%", "Lopsided hard fuzz. Above 80 percent it turns nasty on purpose. Above 100 percent needs Nasty range.", false, "" },

    { "dance", "Dance", "Dance", 0, 100, 0, "%", "Glue compressor on everything above 150 Hz, bass left alone. Pumps and holds the sound together for dance music.", false, "" },

    { "mix", "Dry/wet", "Output", 0, 100, 100, "%", "Blend between the untouched input and Hairball. At 0 the input passes bit for bit.", false, "" },
    { "out", "Output", "Output", -24, 12, 0, "dB", "Output level.", false, "" },
    { "safeLim", "Limiter", "Output", 0, 1, 1, "", "Safety limiter at the output, never above 0 dBFS.", true, "Off|On" },
    { "nasty", "Nasty range", "Output", 0, 1, 0, "", "Lets Drive, Transistor and Fuzz go beyond 100 percent. Off by default.", true, "Off|On" },
};

inline int indexOf(const char* id)
{
    for (int i = 0; i < pCount; ++i)
    {
        const char* a = kParams[i].id; const char* b = id;
        while (*a && *a == *b) { ++a; ++b; }
        if (!*a && !*b) return i;
    }
    return -1;
}
} // namespace hb
