**Test build (pre-release).** CatRack is a free, open source rack plugin (VST3) by Studio 87: up to 12 effect units in one window, in any order, with parallel pairs, bypass, solo and mute per unit, in and out meters in dB, and factory plus your own chain presets. It collects nothing and never uses the network.

This zip holds the installers for Windows (.exe) and macOS (.pkg), the Linux Flatpak extension and a plain .vst3 tarball, the PDF manual, INSTALL.txt, LICENSE and SHA256SUMS. Everything works offline.

**0.1.3:** Purr Drive, Purr Trans and Purr Fuzz now run at 4x oversampling with zero latency; aliasing at full drive is down from -33 to -43 dB to -64 to -83 dB.

**What is in this version:** 28 of the 31 units shown in the design are built in (Cat Spacer, Stretch Cat and Copycat are standalone plugins, not rack units). 23 factory chains. New since 0.1.0: Meowximizer, Cat Crusher, Fat Cat, Meowbass, Shimmer Reverb, Shimmer Delay, Hairball and Scratch Cat FX, units with more than nine controls get a page switch, a new instance opens with the Start Here chain. The units run as built-in code; CatRack never loads other plugin files.

**What is checked:** the DSP core has automatic tests (latency equals the sum of the rows, stability on every unit, state round trip), the plugin builds on Windows, macOS and Linux, passes the pluginval validator on Linux, and the window pictures are rendered by the real compiled editor.

**What is not checked yet:** the installers on real Windows and macOS machines, the Flatpak extension in a real Flatpak DAW, the plugin in real DAWs, and the sound by ear on real material. Some unit mappings are our own approximations; see ROADMAP.md.

**Why Windows or macOS may warn you:** the installers are not signed with a paid publisher certificate (it costs money every year and this project is free), so Windows SmartScreen and macOS Gatekeeper warn about any unsigned installer. On Windows choose "More info", then "Run anyway". On macOS open System Settings, Privacy and Security, and choose "Open Anyway". You can compare the files with SHA256SUMS in the zip, and the source is on GitHub.
