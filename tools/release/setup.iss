; SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
; SPDX-License-Identifier: GPL-2.0-or-later
;
; The release's setup .exe (Inno Setup 6; make-release.ps1 compiles it with ISCC): installs the
; staged release folder for the current user (no administrator), offers a desktop shortcut to
; BLauncher.exe, and on uninstall lets the launcher remove the game view it made (links to the
; player's game, removed as links) before the files go (Kaniel 2026-10-13: one setup file, no bat
; files). The saves, settings and logs in user\ stay after an uninstall.
;
;   ISCC /DAppVersion=0.1.3 /DSourceDir=<staged release> /DOutputDir=<dir> /DIconFile=<ico> [/DStyleFile=<vsf>] setup.iss

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
; The launcher's look (Kaniel 2026-10-17): Inno's dark style recolored to the launcher's slate,
; parchment and gold (setup_style.py; make-release.ps1 passes it as StyleFile) with its own title bar
; and frame, the hunter logo and night street (setup_art.py draws tools\release\art), gold titles from
; [Code], and no box, disc or folder pictures.
WizardStyle=modern dark hidebevels includetitlebar
#ifdef StyleFile
WizardStyleFile={#StyleFile}
#endif
WizardBackColor=#0d0f13
WizardImageFile=art\wizard-202.png,art\wizard-269.png,art\wizard-336.png,art\wizard-430.png,art\wizard-534.png
WizardImageBackColor=#12151a
WizardSmallImageFile=art\small-58.png,art\small-77.png,art\small-97.png,art\small-124.png,art\small-159.png
WizardSmallImageBackColor=#0d0f13
DisableWelcomePage=no
; The language is Windows' own (any of [Languages]); the pick list (its focused row stays Windows
; blue) only when no translation matches it.
ShowLanguageDialog=auto
; The game, its shader cache and the saves need room beside the install.
ExtraDiskSpaceRequired=1073741824
CloseApplications=yes

[Languages]
; Every translation Inno Setup has: its own in the compiler, the rest (official ones this Inno lacks
; and the community ones) in tools\release\languages from jrsoftware/issrc Files\Languages.
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "arabic"; MessagesFile: "compiler:Languages\Arabic.isl"
Name: "armenian"; MessagesFile: "compiler:Languages\Armenian.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "bulgarian"; MessagesFile: "compiler:Languages\Bulgarian.isl"
Name: "catalan"; MessagesFile: "compiler:Languages\Catalan.isl"
Name: "corsican"; MessagesFile: "compiler:Languages\Corsican.isl"
Name: "czech"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "danish"; MessagesFile: "compiler:Languages\Danish.isl"
Name: "dutch"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "finnish"; MessagesFile: "compiler:Languages\Finnish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "hebrew"; MessagesFile: "compiler:Languages\Hebrew.isl"
Name: "hungarian"; MessagesFile: "compiler:Languages\Hungarian.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "norwegian"; MessagesFile: "compiler:Languages\Norwegian.isl"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "portuguese"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "slovak"; MessagesFile: "compiler:Languages\Slovak.isl"
Name: "slovenian"; MessagesFile: "compiler:Languages\Slovenian.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "swedish"; MessagesFile: "compiler:Languages\Swedish.isl"
Name: "tamil"; MessagesFile: "compiler:Languages\Tamil.isl"
Name: "thai"; MessagesFile: "compiler:Languages\Thai.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"
Name: "abkhazian"; MessagesFile: "languages\Abkhazian.isl"
Name: "afrikaans"; MessagesFile: "languages\Afrikaans.isl"
Name: "albanian"; MessagesFile: "languages\Albanian.isl"
Name: "azerbaijan"; MessagesFile: "languages\Azerbaijan.isl"
Name: "basque"; MessagesFile: "languages\Basque.isl"
Name: "belarusian"; MessagesFile: "languages\Belarusian.isl"
Name: "bengali"; MessagesFile: "languages\Bengali.islu"
Name: "bosnian"; MessagesFile: "languages\Bosnian.isl"
Name: "chinesesimplified"; MessagesFile: "languages\ChineseSimplified.isl"
Name: "chinesetraditional"; MessagesFile: "languages\ChineseTraditional.isl"
Name: "croatian"; MessagesFile: "languages\Croatian.isl"
Name: "englishbritish"; MessagesFile: "languages\EnglishBritish.isl"
Name: "esperanto"; MessagesFile: "languages\Esperanto.isl"
Name: "estonian"; MessagesFile: "languages\Estonian.isl"
Name: "ewe"; MessagesFile: "languages\Ewe.isl"
Name: "farsi"; MessagesFile: "languages\Farsi.isl"
Name: "galician"; MessagesFile: "languages\Galician.isl"
Name: "georgian"; MessagesFile: "languages\Georgian.isl"
Name: "greek"; MessagesFile: "languages\Greek.isl"
Name: "hindi"; MessagesFile: "languages\Hindi.islu"
Name: "icelandic"; MessagesFile: "languages\Icelandic.isl"
Name: "indonesian"; MessagesFile: "languages\Indonesian.isl"
Name: "kazakh"; MessagesFile: "languages\Kazakh.islu"
Name: "kurdish"; MessagesFile: "languages\Kurdish.isl"
Name: "latvian"; MessagesFile: "languages\Latvian.isl"
Name: "ligurian"; MessagesFile: "languages\Ligurian.isl"
Name: "lithuanian"; MessagesFile: "languages\Lithuanian.isl"
Name: "luxemburgish"; MessagesFile: "languages\Luxemburgish.isl"
Name: "macedonian"; MessagesFile: "languages\Macedonian.isl"
Name: "malaysian"; MessagesFile: "languages\Malaysian.isl"
Name: "mongolian"; MessagesFile: "languages\Mongolian.isl"
Name: "nepali"; MessagesFile: "languages\Nepali.islu"
Name: "occitan"; MessagesFile: "languages\Occitan.isl"
Name: "romanian"; MessagesFile: "languages\Romanian.isl"
Name: "scottishgaelic"; MessagesFile: "languages\ScottishGaelic.isl"
Name: "serbiancyrillic"; MessagesFile: "languages\SerbianCyrillic.isl"
Name: "serbianlatin"; MessagesFile: "languages\SerbianLatin.isl"
Name: "sinhala"; MessagesFile: "languages\Sinhala.islu"
Name: "tatar"; MessagesFile: "languages\Tatar.isl"
Name: "urdu"; MessagesFile: "languages\Urdu.isl"
Name: "uyghur"; MessagesFile: "languages\Uyghur.islu"
Name: "uzbek"; MessagesFile: "languages\Uzbek.isl"
Name: "valencian"; MessagesFile: "languages\Valencian.isl"
Name: "vietnamese"; MessagesFile: "languages\Vietnamese.isl"
Name: "asturian"; MessagesFile: "languages\Asturian.isl"
Name: "marathi"; MessagesFile: "languages\Marathi.islu"
Name: "montenegrin"; MessagesFile: "languages\Montenegrin.isl"
Name: "norwegiannynorsk"; MessagesFile: "languages\NorwegianNynorsk.isl"

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
; The launcher's update runs this setup silently (bbport_coop.setup_command): the launcher starts again.
Filename: "{app}\BLauncher.exe"; WorkingDir: "{app}"; Flags: nowait skipifnotsilent

[UninstallRun]
Filename: "{app}\BLauncher.exe"; Parameters: "--coop-uninstall"; WorkingDir: "{app}"; Flags: runhidden waituntilterminated; RunOnceId: "CoopUninstall"

[Code]
// The launcher's GOLD and MUTED (bbport_launcher_win.py) as $bbggrr; the rest comes from the style.
const
  Gold = $78b8d9;
  Muted = $90a5ae;

// A label takes our color only once the style stops drawing its font.
procedure Ink(L: TNewStaticText; Color: Integer);
begin
  L.StyleElements := L.StyleElements - [seFont];
  L.Font.Color := Color;
end;

// Palatino has Latin, Greek and Cyrillic only: other scripts keep Windows' own font for titles.
function SerifScript: Boolean;
begin
  Result := Pos(',' + ActiveLanguage + ',', ',arabic,armenian,bengali,chinesesimplified,chinesetraditional,' +
    'farsi,georgian,hebrew,hindi,japanese,korean,marathi,nepali,sinhala,tamil,thai,urdu,uyghur,') = 0;
end;

// Titles as the launcher's: gold serif, not bold.
procedure Title(L: TNewStaticText; Size: Integer);
begin
  Ink(L, Gold);
  if SerifScript then
    L.Font.Name := 'Palatino Linotype';
  L.Font.Size := Size;
  L.Font.Style := [];
end;

// The page title in its serif with the page's hint under it.
procedure Header(Name, Description: TNewStaticText; Grow: Boolean);
begin
  Title(Name, 12);
  Name.Top := ScaleY(8);
  Name.Height := ScaleY(26);
  if Grow then
    WizardForm.AdjustLabelHeight(Name);
  Description.Top := Name.Top + Name.Height + ScaleY(2);
  Ink(Description, Muted);
end;

// No folder or warning pictures: the text moves into their place (on its right in right-to-left
// languages, where the picture sits right of the text).
procedure DropPicture(Image: TBitmapImage; L: TNewStaticText);
begin
  Image.Visible := False;
  if Image.Left > L.Left then
    L.Width := Image.Left + Image.Width - L.Left
  else begin
    L.Width := L.Width + (L.Left - Image.Left);
    L.Left := Image.Left;
  end;
end;

// The big title of the first and last page, as tall as its lines (three in Russian), text under it.
procedure BigTitle(L, Below: TNewStaticText);
var
  Grown: Integer;
begin
  Title(L, 17);
  Grown := WizardForm.AdjustLabelHeight(L);
  Below.Top := Below.Top + Grown;
end;

procedure InitializeWizard;
begin
  Header(WizardForm.PageNameLabel, WizardForm.PageDescriptionLabel, True);
  BigTitle(WizardForm.WelcomeLabel1, WizardForm.WelcomeLabel2);
  BigTitle(WizardForm.FinishedHeadingLabel, WizardForm.FinishedLabel);
  Ink(WizardForm.DiskSpaceLabel, Muted);
  Ink(WizardForm.FilenameLabel, Muted);
  DropPicture(WizardForm.SelectDirBitmapImage, WizardForm.SelectDirLabel);
  DropPicture(WizardForm.SelectGroupBitmapImage, WizardForm.SelectStartMenuFolderLabel);
  DropPicture(WizardForm.PreparingErrorBitmapImage, WizardForm.PreparingLabel);
end;

// Each page's title is fitted again, as its text changes from page to page, and the folder is not
// shown selected (an edit's selection is Windows blue, whatever the style).
procedure CurPageChanged(CurPageID: Integer);
begin
  Header(WizardForm.PageNameLabel, WizardForm.PageDescriptionLabel, True);
  if CurPageID = wpSelectDir then
    WizardForm.DirEdit.SelStart := Length(WizardForm.DirEdit.Text);
end;

procedure InitializeUninstallProgressForm;
begin
  Header(UninstallProgressForm.PageNameLabel, UninstallProgressForm.PageDescriptionLabel, False);
end;
