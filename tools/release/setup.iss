; SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
; SPDX-License-Identifier: GPL-2.0-or-later
;
; The release's setup .exe (Inno Setup 6; make-release.ps1 compiles it with ISCC): installs the
; staged release folder for the current user (no administrator), offers a desktop shortcut to
; BLauncher.exe, and on uninstall lets the launcher remove the game view it made (links to the
; player's game, removed as links) before the files go (Kaniel 2026-10-13: one setup file, no bat
; files). The saves, settings and logs in user\ stay after an uninstall.
;
;   ISCC /DAppVersion=0.1.3 /DSourceDir=<staged release> /DOutputDir=<dir> /DIconFile=<ico> setup.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define AppName "Bloodborne Seamless Co-op"

[Setup]
AppId={{6E0B4C2A-3F7D-4B8E-9A51-2C8D7E4F1B63}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppName}
AppPublisherURL=https://github.com/lKaniel/bloodborne-server
AppSupportURL=https://github.com/lKaniel/bloodborne-server/issues
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=BloodborneSeamlessCoop-v{#AppVersion}-setup
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\BLauncher.exe
UninstallDisplayName={#AppName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; The game, its shader cache and the saves need room beside the install.
ExtraDiskSpaceRequired=1073741824
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\BLauncher.exe"; WorkingDir: "{app}"
Name: "{autoprograms}\{#AppName} - read me"; Filename: "{app}\READ ME FIRST.txt"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\BLauncher.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\BLauncher.exe"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\BLauncher.exe"; Parameters: "--coop-uninstall"; WorkingDir: "{app}"; Flags: runhidden waituntilterminated; RunOnceId: "CoopUninstall"
