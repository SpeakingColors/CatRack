**Test build (pre-release).** CatRack is a free, open source tube-style program equalizer (VST3) by Studio 87: low boost and atten on a shared frequency, high boost with width, high atten, a tube stage and an Output level. It collects nothing and never uses the network.

This zip holds the installers for Windows (.exe) and macOS (.pkg), Linux Flatpak extension and plain .vst3 tarball, the PDF manual, INSTALL.txt, LICENSE and SHA256SUMS. Everything works offline.

**What is checked:** the DSP core has automatic tests (flat response, exact gains, constant latency, aliasing, level match, NaN safety), the plugin builds on Windows, macOS and Linux and passes the pluginval validator on Linux, and the window is rendered by the real compiled editor for the manual.

**What is not checked yet:** the installers on real Windows and macOS machines, the Flatpak extension in a real Flatpak DAW, the plugin in real DAWs, and the sound by ear on real material. The filter shapes are our own model, not measured from hardware.

**Why Windows or macOS may warn you:** the installers are not signed with a paid publisher certificate (it costs money every year and this project is free), so Windows SmartScreen and macOS Gatekeeper warn about any unsigned installer. On Windows choose "More info", then "Run anyway". On macOS open System Settings, Privacy and Security, and choose "Open Anyway". You can compare the files with SHA256SUMS in the zip, and the source is on GitHub.
