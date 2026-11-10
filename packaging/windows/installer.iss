; SPDX-License-Identifier: GPL-3.0-or-later
;
; Inno Setup script for the Windows installer. Built by the release workflow:
;   iscc /DAppVersion=0.1.0 /DStageDir=<cmake --install prefix> /DOutputDir=<dir> installer.iss
; StageDir holds bin\twisted-harness-designer.exe with the Qt DLLs and plugins
; that windeployqt placed next to it.

#ifndef AppVersion
  #error AppVersion must be defined (/DAppVersion=x.y.z)
#endif
#ifndef StageDir
  #error StageDir must be defined (/DStageDir=path)
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define AppName "Twisted Builds Harness Designer"
#define AppExe "twisted-harness-designer.exe"
#define ProgId "TwistedBuilds.HarnessDesigner.tbh"

[Setup]
; Never change AppId: Windows uses it to find the installed copy for upgrades and uninstall.
AppId={{6E0B7C1A-3F5D-4B8E-9A21-7C4D2E8F9B30}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Twisted Builds LLC
AppPublisherURL=https://github.com/twisted-builds-llc/Twisted_Builds_Harness_Designer
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Asks "install for me only" (no admin) or "for all users" (admin).
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\..\LICENSE
SetupIconFile=..\..\resources\icons\twisted-harness-designer.ico
UninstallDisplayIcon={app}\bin\{#AppExe}
OutputDir={#OutputDir}
OutputBaseFilename=TwistedHarnessDesigner-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate"; Description: "Open .tbh harness files with {#AppName}"; GroupDescription: "File types:"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\bin\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\bin\{#AppExe}"; Tasks: desktopicon

[Registry]
; HKA writes to HKCU for a per-user install and HKLM for an all-users install.
Root: HKA; Subkey: "Software\Classes\.tbh"; ValueType: string; ValueName: ""; ValueData: "{#ProgId}"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.tbh\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "Twisted Builds Harness"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\{#AppExe},0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\{#AppExe}"" ""%1"""; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".tbh"; ValueData: ""; Flags: uninsdeletekey; Tasks: associate

[Run]
Filename: "{app}\bin\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
