#pragma once
// Geometry of the 1200 x 640 faceplate, worked out from the mockup's CSS (docs/mockup/CatRack.html):
// wood cheeks 52 wide, header 46, body 568 (rack column 716, unit panel 288, meters 92), footer 26.
namespace layout
{
struct Rc { float x, y, w, h; };
constexpr int W = 1200, H = 640;
constexpr float cheek = 52.0f, mainX = 52.0f, mainW = 1096.0f;
constexpr float hdrH = 46.0f, footY = 614.0f, footH = 26.0f, bodyY = 46.0f, bodyH = 568.0f;
constexpr float rackX = 52.0f, rackW = 716.0f, detX = 768.0f, detW = 288.0f, metX = 1056.0f, metW = 92.0f;

// rack column (padding 6, gap 4)
constexpr Rc strip = { 58.0f, 52.0f, 704.0f, 84.0f };
constexpr Rc toolbar = { 58.0f, 140.0f, 704.0f, 28.0f };
constexpr Rc rows = { 58.0f, 172.0f, 704.0f, 436.0f };
constexpr int maxUnits = 12;

// unit panel (padding 8, gap 7)
constexpr float detPad = 8.0f, detGap = 7.0f;

// meters
constexpr Rc meterCv = { 1060.0f, 71.0f, 84.0f, 500.0f };
constexpr Rc meterCap = { 1060.0f, 54.0f, 84.0f, 14.0f };
constexpr Rc meterPk = { 1060.0f, 574.0f, 84.0f, 13.0f };

constexpr const char* tipCheek = "Double-click the wood to reset the window to 100%.";
constexpr const char* tipBrand = "CatRack by Studio 87. A free, open source rack of effects. Open About & Support for the project links.";
constexpr const char* tipPrev = "Previous chain preset.";
constexpr const char* tipNext = "Next chain preset.";
constexpr const char* tipSave = "Save this whole chain as your own preset. Asks before overwriting.";
constexpr const char* tipBypass = "Bypass: hear the dry signal to compare with the processed sound (key B). The meters keep moving.";
constexpr const char* tipGuide = "Short guide and keyboard shortcuts.";
constexpr const char* tipSettings = "Info bubbles, power saving, colour look.";
constexpr const char* tipAbout = "About CatRack, support and where your files live.";
constexpr const char* tipClear = "Remove every unit from the rack.";
constexpr const char* tipMeter = "Input meter (left pair) and output meter (right pair) in dB, left and right channel. Blue is safe, purple is loud, pink is at or over 0 dB. Click to clear the peaks.";
constexpr const char* tipGrip = "Drag to move this unit. Drop on the top or bottom edge of a row to put it before or after. Drop on the middle of another unit to run the two side by side.";
constexpr const char* tipOn = "Switch this unit on or off. Off means the signal passes through untouched.";
constexpr const char* tipSolo = "Solo: hear only the soloed units. All other units step out of the way, so you can listen to one on its own.";
constexpr const char* tipMute = "Mute: silence this unit. In a parallel pair only this lane goes quiet. In a single row it silences everything after it.";
} // namespace layout
