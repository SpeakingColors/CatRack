# CatRack roadmap

Version 0.1.0 is a test build. This list says plainly what the approved mockup (`docs/mockup/CatRack.html`) shows that this version does not do, and what is approximate or unverified.

## Not built yet (shown greyed out as "Planned" in the unit browser)
- Meowximizer, Cat Crusher, Fat Cat, Cat Spacer, Meowbass, Shimmer Reverb, Shimmer Delay, Hairball, Scratch Cat FX, Stretch Cat, Copycat. Their pure C++ cores are not yet in a form that can be hosted here (some need a sidechain or analyser, some are large). 20 of the 31 units are hosted.
- The 9 factory presets that need those units (the "Mega" chains and Full Rack Demo) are left out; 16 factory chains ship.
- The mockup's preview rig (built-in test sounds and audio file preview) is not in the plugin: the real audio from the host is used.
- The mockup's bypass desaturation effect on the window is not drawn.
- When twelve single units overflow the signal path strip, the chips shrink instead of the mockup's exact wrap.
- A new instance starts as an empty rack ("Empty rack"); the mockup opened with the Start Here chain loaded.

## Approximate or our own choices (not measured against hardware)
- Soft Clip, the Furchild and LapCat2A warmth stage and the parameter mappings of units that wrap a sibling plugin's core are our own choices. Apurrr2500 and Purrrstressor mappings are approximate.
- PunchPaw: the Lookahead knob has no effect yet.
- The EQ display curves and the "level" read-out in the unit displays follow the mockup's drawing model; they are not a measured frequency response of the audio.
- Colour stage aliasing at the most extreme setting is about -33 to -43 dB with 2x oversampling; 4x would be better and may come later.
- The brand name in the header uses the ExtraBold Italic cut of Barlow Condensed (the mockup asks for 700 italic).
- The tail length reported to the host (2.5 s) is a guess, not measured.
- A parallel pair keeps its Dry/Wet per row position.

## Not verified
- Nobody has listened to this build by ear on real material.
- Not tested in real DAWs (only the pluginval validator in CI) and the installers are not tested on real machines.
- Preset navigation with `[` and `]` goes to the first preset when the loaded name is not found.
