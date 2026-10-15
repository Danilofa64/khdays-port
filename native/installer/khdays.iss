; The port's installer (README.md beside this file). Built by build_installer.py, which defines
; Version (the date and the commit) and Stage (the folder of the files to install).
;
; Installed for the player alone, without administrator rights, in a folder the player can write:
; the port keeps its saves, settings and mods beside itself. The install has the repository's
; layout at its root (tools/, native/tools/, native/installer/ beside data/, mods/, saves/), so the
; tools run there unchanged; the helper native/installer/setup_steps.py runs each step with the
; Python in setup/python/.

#ifndef Version
  #define Version "0-dev"
#endif
#ifndef Stage
  #error Stage is not defined: build with native/installer/build_installer.py
#endif

#define AppName "KH 358/2 Days Port"
#define AppExe "khdays-native.exe"

[Setup]
AppId={{68af474f-87b5-4746-a4e2-f339c94450df}
AppName={#AppName}
AppVersion={#Version}
AppVerName={#AppName} {#Version}
DefaultDirName={localappdata}\Programs\KH 358-2 Days Port
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=KH358-2-Days-Port-Setup-{#Version}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=yes
LanguageDetectionMethod=uilanguage
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
; the game's data made from the ROM after the files (measured: 262 MB for the European ROM)
ExtraDiskSpaceRequired=262293463

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"

#include "messages.iss"

[Files]
; first (solid compression): the helper and the bare Python, for the checks before installing,
; unpacked to {tmp}\helper in the install's layout
Source: "{#Stage}\setup\python\*"; DestDir: "{tmp}\helper\setup\python"; Excludes: "Lib\site-packages\*"; Flags: dontcopy recursesubdirs
Source: "{#Stage}\native\installer\*"; DestDir: "{tmp}\helper\native\installer"; Flags: dontcopy recursesubdirs
Source: "{#Stage}\tools\verify_rom\*"; DestDir: "{tmp}\helper\tools\verify_rom"; Flags: dontcopy recursesubdirs
; the port, its tools and its Python
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"

[Run]
Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Description: "{cm:StartPort}"; Flags: nowait postinstall skipifsilent; Check: DataReady

[UninstallDelete]
; what the installer made on this computer (the player's saves and own mods are kept)
Type: filesandordirs; Name: "{app}\data\extracted"
Type: filesandordirs; Name: "{app}\data\cutscenes"
Type: filesandordirs; Name: "{app}\mods\HD PACK"
Type: filesandordirs; Name: "{app}\setup"
Type: filesandordirs; Name: "{app}\native"
Type: filesandordirs; Name: "{app}\tools"
Type: dirifempty; Name: "{app}\data"
Type: dirifempty; Name: "{app}\mods"

[Code]
var
  RomPage: TInputFileWizardPage;
  RomStatus: TNewStaticText;
  RomChecked: String;     { the file last found supported }
  HelperReady: Boolean;
  DataOk: Boolean;

{ ---- The helper's results: JSON written in ASCII (other characters as \u escapes) ---- }

function HexValue(const S: String): Integer;
var
  I, D: Integer;
begin
  Result := 0;
  for I := 1 to Length(S) do begin
    D := Pos(Lowercase(S[I]), '0123456789abcdef') - 1;
    if D < 0 then begin Result := -1; Exit; end;
    Result := Result * 16 + D;
  end;
end;

{ The value of "Key" in a flat JSON object: a string unescaped, or true/false/null/a number as written. }
function JsonValue(const Json, Key: String): String;
var
  P, N: Integer;
  C: Char;
begin
  Result := '';
  P := Pos('"' + Key + '":', Json);
  if P = 0 then Exit;
  P := P + Length(Key) + 3;
  while (P <= Length(Json)) and (Json[P] = ' ') do P := P + 1;
  if (P <= Length(Json)) and (Json[P] = '"') then begin
    P := P + 1;
    while P <= Length(Json) do begin
      C := Json[P];
      if C = '"' then Exit;
      if C = '\' then begin
        P := P + 1;
        C := Json[P];
        if C = 'n' then Result := Result + #13#10
        else if C = 't' then Result := Result + #9
        else if C = 'u' then begin
          N := HexValue(Copy(Json, P + 1, 4));
          if N >= 0 then Result := Result + Chr(N);
          P := P + 4;
        end else Result := Result + C;
      end else
        Result := Result + C;
      P := P + 1;
    end;
  end else begin
    while (P <= Length(Json)) and (Pos(Json[P], ',}' + #13#10) = 0) do begin
      Result := Result + Json[P];
      P := P + 1;
    end;
  end;
end;

function HelperLanguage: String;
begin
  Result := ActiveLanguage;
end;

{ Runs the helper's Command with Python from Root (the install's layout); its exit code, its JSON in Json. }
function RunHelperIn(const Root, Command: String; var Json: String): Integer;
var
  ResultFile: String;
  Raw: AnsiString;
begin
  ResultFile := AddBackslash(GetTempDir) + 'khdays-setup-result.json';
  DeleteFile(ResultFile);
  if not Exec(Root + '\setup\python\python.exe',
              '-I "' + Root + '\native\installer\setup_steps.py" ' + Command +
              ' --result "' + ResultFile + '" --language ' + HelperLanguage,
              Root, SW_HIDE, ewWaitUntilTerminated, Result) then
    Result := 2;
  Json := '';
  if LoadStringFromFile(ResultFile, Raw) then
    Json := String(Raw);
end;

// Before installing: the helper unpacked once into the setup's temporary folder.
function RunHelper(const Command: String; var Json: String): Integer;
begin
  if not HelperReady then begin
    ExtractTemporaryFiles('{tmp}\helper\*');
    HelperReady := True;
  end;
  Result := RunHelperIn(ExpandConstant('{tmp}\helper'), Command, Json);
end;

{ ---- The ROM page ---- }

function CheckRom(const Path: String): Boolean;
var
  Json: String;
begin
  Result := False;
  if Path = '' then begin
    RomStatus.Caption := CustomMessage('RomNone');
    Exit;
  end;
  RomStatus.Caption := CustomMessage('RomChecking');
  WizardForm.Refresh;
  if RunHelper('verify-rom "' + Path + '"', Json) = 0 then begin
    RomChecked := Path;
    Result := True;
  end;
  RomStatus.Caption := JsonValue(Json, 'message');
  if RomStatus.Caption = '' then
    RomStatus.Caption := CustomMessage('RomCheckFailed');
end;

procedure InitializeWizard;
var
  Json, Found: String;
begin
  RomPage := CreateInputFilePage(wpWelcome, CustomMessage('RomPageCaption'),
    CustomMessage('RomPageDescription'), CustomMessage('RomPageText'));
  RomPage.Add(CustomMessage('RomFileLabel'), CustomMessage('RomFilter'), '.nds');
  RomStatus := TNewStaticText.Create(RomPage);
  RomStatus.Parent := RomPage.Surface;
  RomStatus.Top := RomPage.Edits[0].Top + RomPage.Edits[0].Height + ScaleY(16);
  RomStatus.Width := RomPage.SurfaceWidth;
  RomStatus.AutoSize := False;
  RomStatus.WordWrap := True;
  RomStatus.Height := ScaleY(60);
  RomStatus.Caption := '';
  { the ROM given last time (a repair or an update); else one already where KH Melon Mix keeps a
    player's own dump, offered and checked like any other }
  RomPage.Values[0] := ExpandConstant('{param:ROM|}');
  if RomPage.Values[0] = '' then
    RomPage.Values[0] := GetPreviousData('RomPath', '');
  if (RomPage.Values[0] = '') and (RunHelper('find-rom', Json) = 0) then begin
    Found := JsonValue(Json, 'found');
    if (Found <> '') and (Found <> 'null') then
      RomPage.Values[0] := Found;
  end;
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'RomPath', RomChecked);
end;

{ ---- The folder: the player must be able to write there ---- }

function FolderWritable(const Folder: String): Boolean;
var
  Probe, Existing: String;
begin
  Existing := Folder;
  while (Existing <> '') and not DirExists(Existing) do
    Existing := ExtractFileDir(Existing);
  Probe := AddBackslash(Existing) + 'khdays-write-test.tmp';
  Result := SaveStringToFile(Probe, 'x', False);
  if Result then
    DeleteFile(Probe);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = RomPage.ID then
    Result := CheckRom(RomPage.Values[0])
  else if CurPageID = wpSelectDir then begin
    Result := FolderWritable(WizardDirValue);
    if not Result then
      SuppressibleMsgBox(FmtMessage(CustomMessage('FolderNotWritable'), [WizardDirValue]), mbError, MB_OK, IDOK);
  end;
end;

{ Before installing, also when silent (/ROM=<file>, no pages): the ROM checked, or nothing installed. }
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if RomChecked = '' then
    if not CheckRom(RomPage.Values[0]) then
      Result := RomStatus.Caption;
end;

{ ---- After the files: the game's data from the ROM ---- }

function DataReady: Boolean;
begin
  Result := DataOk;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Json: String;
begin
  if CurStep = ssPostInstall then begin
    WizardForm.StatusLabel.Caption := CustomMessage('DataPreparing');
    WizardForm.Refresh;
    DataOk := RunHelperIn(ExpandConstant('{app}'), 'prepare-data "' + RomChecked + '" "' +
                          ExpandConstant('{app}') + '"', Json) = 0;
    if not DataOk then
      SuppressibleMsgBox(CustomMessage('DataFailed') + #13#10#13#10 + JsonValue(Json, 'message'), mbError, MB_OK, IDOK);
  end;
end;

{ ---- Uninstalling: the player's saves and settings kept unless they ask ---- }

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Json: String;
begin
  if CurUninstallStep = usUninstall then begin
    { silent: the default, keep them }
    if SuppressibleMsgBox(CustomMessage('RemoveSaves'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES then
      if RunHelperIn(ExpandConstant('{app}'), 'remove-user-files "' + ExpandConstant('{app}') + '"', Json) <> 0 then
        SuppressibleMsgBox(JsonValue(Json, 'message'), mbError, MB_OK, IDOK);
  end;
end;
