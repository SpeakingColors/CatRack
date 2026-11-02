**Test build (pre-release).** CatRack is a free, open source rack plugin (VST3) by Studio 87: up to 12 effect units in one window, in any order, with parallel pairs, bypass, solo and mute per unit, in and out meters in dB, and factory plus your own chain presets. It collects nothing and never uses the network.

This zip holds the installers for Windows (.exe) and macOS (.pkg), the Linux Flatpak extension and a plain .vst3 tarball, the PDF manual, INSTALL.txt, LICENSE and SHA256SUMS. Everything works offline.

**What is in this version:** 20 of the 31 units shown in the design are built in (the others are listed in ROADMAP.md and appear greyed out as Planned). 16 factory chains. The units run as built-in code; CatRack never loads other plugin files.

**What is checked:** the DSP core has automatic tests (latency equals the sum of the rows, stability on every unit, state round trip), the plugin builds on Windows, macOS and Linux, passes the pluginval validator on Linux, and the window pictures are rendered by the real compiled editor.

**What is not checked yet:** the installers on real Windows and macOS machines, the Flatpak extension in a real Flatpak DAW, the plugin in real DAWs, and the sound by ear on real material. Some unit mappings are our own approximations; see ROADMAP.md.

**Why Windows or macOS may warn you:** the installers are not signed with a paid publisher certificate (it costs money every year and this project is free), so Windows SmartScreen and macOS Gatekeeper warn about any unsigned installer. On Windows choose "More info", then "Run anyway". On macOS open System Settings, Privacy and Security, and choose "Open Anyway". You can compare the files with SHA256SUMS in the zip, and the source is on GitHub.
