#define VersionString "0.1.2"
#define AppId "1f7e9ebf-ed92-4d88-8eac-89e3fe53282c"
#define VC_Redist_URL "https://aka.ms/vs/17/release/vc_redist.x64.exe"
#define AppName "codepointer"

[Setup]
AppName={#AppName}
AppVersion={#VersionString}
AppId={#AppId}
DefaultDirName={localappdata}\Programs\{#AppName}

DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppName}.ico
OutputDir=dist
;OutputBaseFilename={#AppName}-win64-v{#VersionString}
OutputBaseFilename={#AppName}-win64
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
ShowComponentSizes=yes
SetupIconFile={#AppName}.ico
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Files]
Source: "dist\windows-msvc\usr\bin\{#AppName}.exe"; DestDir: "{app}";
Source: "dist\windows-msvc\usr\bin\*.dll"; DestDir: "{app}";
Source: "dist\windows-msvc\usr\bin\generic\*.dll"; DestDir: "{app}\generic";
Source: "dist\windows-msvc\usr\bin\iconengines\*.dll"; DestDir: "{app}\iconengines";
Source: "dist\windows-msvc\usr\bin\imageformats\*.dll"; DestDir: "{app}\imageformats";
Source: "dist\windows-msvc\usr\bin\networkinformation\*.dll"; DestDir: "{app}\networkinformation";
Source: "dist\windows-msvc\usr\bin\platforms\*.dll"; DestDir: "{app}\platforms";
Source: "dist\windows-msvc\usr\bin\styles\*.dll"; DestDir: "{app}\styles";
Source: "dist\windows-msvc\usr\bin\tls\*.dll"; DestDir: "{app}\tls";

; Icons
;Source: "dist\windows-msvc\usr\share\icons\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion noregerror allowunsafefiles
Source: "dist\windows-msvc\usr\share\icons\breeze\index.theme"; DestDir: "{app}\icons\breeze"; Flags: ignoreversion
Source: "dist\windows-msvc\usr\share\icons\breeze\actions\16\*.svg"; DestDir: "{app}\icons\breeze\actions\16\"; Flags: ignoreversion
Source: "dist\windows-msvc\usr\share\icons\breeze\actions\22\*.svg"; DestDir: "{app}\icons\breeze\actions\22\"; Flags: ignoreversion
Source: "dist\windows-msvc\usr\share\icons\breeze\actions\32\*.svg"; DestDir: "{app}\icons\breeze\actions\32\"; Flags: ignoreversion
Source: "dist\windows-msvc\usr\share\icons\breeze\devices\16\*.svg"; DestDir: "{app}\icons\breeze\devices\16\"; Flags: ignoreversion
Source: "dist\windows-msvc\usr\share\icons\breeze\devices\22\*.svg"; DestDir: "{app}\icons\breeze\devices\22\"; Flags: ignoreversion
Source: "dist\windows-msvc\usr\{#AppName}.ico"; DestDir: "{app}\"; Flags: ignoreversion

[Registry]
; Right-click menu for .txt (safe, does not steal default app)
Root: HKCU; Subkey: "Software\Classes\txtfile\shell\Edit with {#AppName}"; ValueType: string; ValueData: "Edit with {#AppName}"
Root: HKCU; Subkey: "Software\Classes\txtfile\shell\Edit with {#AppName}\command"; ValueType: string; ValueData: """{app}\{#AppName}.exe"" ""%1"""

; Optional: register custom file type
Root: HKCU; Subkey: "Software\Classes\{#AppName}.File"; ValueType: string; ValueData: "{#AppName} Document"
Root: HKCU; Subkey: "Software\Classes\{#AppName}.File\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppName}.exe,0"
Root: HKCU; Subkey: "Software\Classes\{#AppName}.File\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppName}.exe"" ""%1"""

[UninstallDelete]
Type: filesandordirs; Name: "{app}\*"

[Icons]
Name: "{group}\{#AppName} v{#VersionString}"; Filename: "{app}\{#AppName}.exe"; Comment: "{#AppName} editor - version {#VersionString}"; Flags: uninsneveruninstall

[Code]

var
  VCPage: TWizardPage;
  BtnOpen: TNewButton;
  BtnRecheck: TNewButton;
  MsgLbl: TNewStaticText;

function IsVC2015_2022x64Installed: Boolean;
var
  Val: Cardinal;
begin
  Result := False;
  if RegQueryDWordValue(HKLM, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
     'Installed', Val) and (Val = 1) then
    Result := True
  else if RegQueryDWordValue(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
     'Installed', Val) and (Val = 1) then
    Result := True;
end;

procedure OpenVCRedistPage;
var
  ErrorCode: Integer;
begin
  ShellExec('', ExpandConstant('{#VC_Redist_URL}'), '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
end;

procedure BtnOpenClick(Sender: TObject);
begin
  OpenVCRedistPage;
end;

procedure BtnRecheckClick(Sender: TObject);
begin
  if IsVC2015_2022x64Installed then
    MsgBox('Great! The Microsoft Visual C++ runtime is installed. Click Next to continue.', mbInformation, MB_OK)
  else
    MsgBox('Still not detected. Please finish installing the runtime, then click Re-check.', mbError, MB_OK);
end;

procedure InitializeWizard;
begin
  if not IsVC2015_2022x64Installed then
  begin
    VCPage := CreateCustomPage(wpWelcome,
      'Dependency Required',
      'Microsoft Visual C++ 2015–2022 x64 Runtime not found');

    MsgLbl := TNewStaticText.Create(VCPage);
    MsgLbl.Parent := VCPage.Surface;
    MsgLbl.Width := VCPage.SurfaceWidth;
    MsgLbl.Top := ScaleY(8);
    MsgLbl.WordWrap := True;
    MsgLbl.Caption :=
      '{#AppName} needs the Microsoft Visual C++ 2015–2022 x64 runtime.' + #13#10#13#10 +
      '1) Click "Download" to get vc_redist.x64.exe from Microsoft.' + #13#10 +
      '2) Install it, then return here and click "Re-check".';

    BtnOpen := TNewButton.Create(VCPage);
    BtnOpen.Parent := VCPage.Surface;
    BtnOpen.Caption := 'Download';
    BtnOpen.Left := 0;
    BtnOpen.Top := MsgLbl.Top + MsgLbl.Height + ScaleY(12);
    BtnOpen.OnClick := @BtnOpenClick;

    BtnRecheck := TNewButton.Create(VCPage);
    BtnRecheck.Parent := VCPage.Surface;
    BtnRecheck.Caption := 'Re-check';
    BtnRecheck.Left := BtnOpen.Left + BtnOpen.Width + ScaleX(8);
    BtnRecheck.Top := BtnOpen.Top;
    BtnRecheck.OnClick := @BtnRecheckClick;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if Assigned(VCPage) and (CurPageID = VCPage.ID) then
  begin
    if not IsVC2015_2022x64Installed then
    begin
      MsgBox('The Microsoft Visual C++ runtime is required. Please install it before continuing.',
        mbError, MB_OK);
      Result := False;
    end;
  end;
end;

procedure RemoveOldShortcuts;
var
  OldShortcutPath: string;
  FindRec: TFindRec;
  CurrentShortcutName: string;
begin
  OldShortcutPath := ExpandConstant('{group}\*');
  CurrentShortcutName := '{#AppName} v{#VersionString}.lnk';

  if FindFirst(OldShortcutPath, FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY = 0) and
           (ExtractFileExt(FindRec.Name) = '.lnk') and
           (CompareText(FindRec.Name, CurrentShortcutName) <> 0) then
        begin
          DeleteFile(ExpandConstant('{group}\' + FindRec.Name));
          Log('Deleted old shortcut: ' + FindRec.Name);
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    RemoveOldShortcuts;
end;
