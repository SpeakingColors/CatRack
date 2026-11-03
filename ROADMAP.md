# CatRack roadmap

Version 0.1.1 is a test build. This list says plainly what the approved mockup (`docs/mockup/CatRack.html`) shows that this version does not do, and what is approximate or unverified.

## Not built yet (shown greyed out as "Planned" in the unit browser)
28 of the 31 units are hosted. The three that are not:
- Cat Spacer: its Latency control (256 to 2048 samples) changes the delay while playing, and the rack only re-aligns latency when the arrangement changes. It needs a rack change first.
- Stretch Cat: it is a sampler that stretches a loaded file; a live stream cannot be stretched in time, so a rack version (pitch only) has to be designed first.
- Copycat: it only listens and shows what it heard (note, key, chord, tempo, synth settings); the unit panel has no read-out for that yet.
- Two factory chains need Cat Spacer (Mega Trance Lead, Mega Freeze FX) and are left out; 23 factory chains ship.
- The mockup's preview rig (built-in test sounds and audio file preview) is not in the plugin: the real audio from the host is used.
- The mockup's bypass desaturation effect on the window is not drawn.
- When twelve single units overflow the signal path strip, the chips shrink instead of the mockup's exact wrap.

## Approximate or our own choices (not measured against hardware)
- Soft Clip, the Furchild and LapCat2A warmth stage and the parameter mappings of units that wrap a sibling plugin's core are our own choices. Apurrr2500 and Purrrstressor mappings are approximate.
- PunchPaw: the Lookahead knob has no effect: its core has a fixed 32 sample lookahead and no setting for it.
- Units added in 0.1.1 (Hairball, Shimmer Reverb and Delay, Scratch Cat FX, Meowximizer, Cat Crusher, Fat Cat, Meowbass) use the DSP of the standalone plugins. Cat Crusher, Fat Cat and Meowbass were rewritten without JUCE, so their oversampling is the zero latency half-band filters of Hairball instead of JUCE's, they report no latency, and they are not a bit for bit match of the plugins. Hairball runs in FX mode only. Scratch Cat FX keeps a 20 second ring of the input (the plugin keeps 60). Shimmer Reverb has the five types of the mockup (the plugin has more). Meowximizer's Nasty range is off.
- Controls: a unit with more than nine controls shows them on two pages, as the mockup does.
- The EQ display curves and the "level" read-out in the unit displays follow the mockup's drawing model; they are not a measured frequency response of the audio.
- Colour stage aliasing at the most extreme setting is about -33 to -43 dB with 2x oversampling; 4x would be better and may come later.
- The brand name in the header uses the ExtraBold Italic cut of Barlow Condensed (the mockup asks for 700 italic).
- The tail length reported to the host (2.5 s) is a guess, not measured.
- A parallel pair keeps its Dry/Wet per row position.

## Not verified
- Nobody has listened to this build by ear on real material.
- Not tested in real DAWs (only the pluginval validator in CI) and the installers are not tested on real machines.
