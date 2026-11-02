; Windows installer + uninstaller (Inno Setup). Called as: ISCC /DName=CatRack "/DProduct=CatRack" /DVersion=0.1.0 packaging/windows.iss
; Files are staged in ..\stage by the release workflow. Works offline.
; Installs the VST3 plugin and the manual. The uninstaller removes everything it installed. Your saved presets
; (%APPDATA%\Studio 87\CatRack) are kept unless you answer Yes when it asks. A silent uninstall (/SILENT, /VERYSILENT) always keeps them.
[Setup]
AppName={#Product}
AppVersion={#Version}
AppPublisher=Studio 87
AppPublisherURL=https://github.com/SpeakingColors/catrack
DefaultDirName={autopf}\Studio 87\{#Product}
DefaultGroupName=Studio 87
OutputDir=..\Output
OutputBaseFilename={#Name}-Setup
LicenseFile=..\LICENSE
SetupIconFile=..\assets\icon.ico
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
UninstallDisplayName={#Product}
UninstallDisplayIcon={app}\{#Product}.ico
CloseApplications=yes

[Messages]
SelectDirLabel3=Setup will install the manual and the uninstaller into the following folder. The VST3 plugin itself is installed separately into C:\Program Files\Common Files\VST3, where DAWs look for it.
SelectDirBrowseLabel=To continue, click Next. If you would like to select a different folder for the manual, click Browse.

[Dirs]
; read-only on the folder only marks it for the custom icon (Windows ignores it for files inside)
Name: "{commoncf64}\VST3\{#Product}.vst3"; Attribs: readonly

[Files]
Source: "..\stage\vst3\{#Product}.vst3\*"; DestDir: "{commoncf64}\VST3\{#Product}.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\assets\icon.ico"; DestDir: "{commoncf64}\VST3\{#Product}.vst3"; DestName: "{#Product}.ico"; Flags: ignoreversion
Source: "..\packaging\desktop.ini"; DestDir: "{commoncf64}\VST3\{#Product}.vst3"; Attribs: hidden system; Flags: ignoreversion
Source: "..\assets\icon.ico"; DestDir: "{app}"; DestName: "{#Product}.ico"; Flags: ignoreversion
Source: "..\stage\{#Name}-manual.pdf"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#Product} manual"; Filename: "{app}\{#Name}-manual.pdf"; IconFilename: "{app}\{#Product}.ico"
Name: "{group}\Uninstall {#Product}"; Filename: "{uninstallexe}"; IconFilename: "{app}\{#Product}.ico"

[UninstallDelete]
; the plugin folder is removed by the uninstall log; these catch anything the plugin wrote next to its files
Type: filesandordirs; Name: "{commoncf64}\VST3\{#Product}.vst3"
Type: filesandordirs; Name: "{app}"

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  presets, vendor: String;
begin
  if CurUninstallStep <> usPostUninstall then exit;
  presets := ExpandConstant('{userappdata}\Studio 87\{#Product}');
  vendor := ExpandConstant('{userappdata}\Studio 87');
  if DirExists(presets) and (not UninstallSilent) then
    if MsgBox('Also delete your saved {#Product} presets?' + #13#10 + #13#10 + presets + #13#10 + #13#10 +
              'Choose No to keep them (recommended if you plan to reinstall).', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(presets, True, True, True);
  RemoveDir(vendor);   { only succeeds when empty }
end;
