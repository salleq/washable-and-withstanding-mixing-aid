; Inno Setup script for the WWMA Windows installer.
; Built by CI: ISCC /DArch=x64|arm64 /DVersion=0.1.0 /DSourceDir=<folder with the .vst3> wwma.iss

#ifndef Arch
  #define Arch "x64"
#endif
#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist"
#endif

[Setup]
AppId={{3E9A1F64-7C2B-4B9D-A6E1-57A0C0DE0A11}
AppName=WWMA Mixing Aid
AppVersion={#Version}
AppVerName=WWMA Mixing Aid {#Version}
AppPublisher=salleq
AppPublisherURL=https://github.com/salleq/washable-and-withstanding-mixing-aid
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallDisplayName=WWMA Mixing Aid (VST3, CLAP)
CreateUninstallRegKey=yes
PrivilegesRequired=admin
LicenseFile=..\..\LICENSE
OutputDir=..\..\installers
#if Arch == "arm64"
OutputBaseFilename=WWMA-{#Version}-Windows-ARM64-Setup
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
OutputBaseFilename=WWMA-{#Version}-Windows-x64-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Messages]
FinishedLabel=WWMA is installed in %n{commoncf64}\VST3 and {commoncf64}\CLAP%n%nRescan plugins in your DAW and put WWMA last on your master bus. It only measures: your sound passes through untouched.

[Files]
Source: "{#SourceDir}\WWMA Mixing Aid.vst3\*"; DestDir: "{commoncf64}\VST3\WWMA Mixing Aid.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\WWMA Mixing Aid.clap"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\WWMA Mixing Aid.vst3"
Type: files; Name: "{commoncf64}\CLAP\WWMA Mixing Aid.clap"
