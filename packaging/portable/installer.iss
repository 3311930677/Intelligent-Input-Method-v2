; OwO Input Method - Inno Setup script.
;
; Design goals (match the portable ZIP's behaviour):
;   * No administrator required: install per-user, register the TSF DLL under
;     HKEY_CURRENT_USER (the DLL's DllRegisterServer already writes HKCU), and
;     put the background service in the current user's Startup folder.
;   * Provide a proper uninstall entry in "Apps & features".
;
; ASCII-only on purpose: keeps the .iss free of encoding pitfalls. The bundled
; readme.txt still contains the Chinese instructions.

#define AppVersion "0.9.0"

[Setup]
AppId={{6D31C9B1-8978-4F49-89B4-66EB1E741591}
AppName=OwO Input Method
AppVersion={#AppVersion}
AppPublisher=OwO Team
AppPublisherURL=https://github.com/3311930677/Intelligent-Input-Method-v2
DefaultDirName={autopf}\OwO
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\web\releases
OutputBaseFilename=OwO-InputMethod-Setup-{#AppVersion}-win-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=OwO Input Method {#AppVersion}
UninstallDisplayIcon={app}\bin\owo_core_service.exe

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\..\build\release-portable\OwO.TSF.dll";DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\..\build\release-portable\owo_core_service.exe";  DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\..\build\release-portable\owo_config_shell.exe";  DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\..\build\release-portable\owo_plugin_shell.exe";  DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\..\build\windows-release\rime-ice-cn-2026.06.30.owolx"; DestDir: "{app}\lexicon"; Flags: ignoreversion
Source: "..\..\build\release-portable\readme.txt";            DestDir: "{app}"; DestName: "README.txt"; Flags: isreadme ignoreversion
Source: "..\..\LICENSE";                                      DestDir: "{app}"; Flags: ignoreversion

[Icons]
; Background service auto-start for the current user (no admin needed).
Name: "{userstartup}\OwO Core Service"; Filename: "{app}\bin\owo_core_service.exe"; Parameters: "--lexicon ""{app}\lexicon\rime-ice-cn-2026.06.30.owolx"""; WorkingDir: "{app}\bin"

[Run]
; Register the TSF text service (DllRegisterServer writes HKCU -> no admin).
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\bin\OwO.TSF.dll"""; Flags: runhidden waituntilterminated; StatusMsg: "Registering input method..."
; Start the background service right away.
Filename: "{app}\bin\owo_core_service.exe"; Parameters: "--lexicon ""{app}\lexicon\rime-ice-cn-2026.06.30.owolx"""; Flags: nowait runhidden; StatusMsg: "Starting background service..."

[UninstallRun]
; Stop the service so its files are not locked, then unregister the DLL.
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im owo_core_service.exe"; Flags: runhidden; RunOnceId: "KillCore"
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\bin\OwO.TSF.dll"""; Flags: runhidden waituntilterminated; RunOnceId: "UnregTSF"

[Messages]
BeveledLabel=OwO Input Method - unsigned build
