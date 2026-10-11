#ifndef PayloadDir
  #error PayloadDir required
#endif
#ifndef OutputPath
  #error OutputPath required
#endif
[Setup]
AppId={{14BC234E-3400-4EB5-974A-EB958C16D5EF}
AppName=Elka AudioLink OBS Plugin
AppVersion=0.1.1
AppPublisher=ElkaSoft
DefaultDirName={autopf}\obs-studio
UsePreviousAppDir=no
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern dark
LicenseFile=..\LICENSE
OutputDir={#OutputPath}
OutputBaseFilename=elka-audiolink-obs-0.1.1-windows-x64-setup
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
DisableProgramGroupPage=yes
DisableDirPage=no
CreateUninstallRegKey=not IsIsolated
UninstallFilesDir={app}\data\obs-plugins\elka-audiolink
UninstallDisplayName=Elka AudioLink OBS Plugin
DirExistsWarning=no
[Files]
Source: "{#PayloadDir}\plugin\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Code]
function IsIsolated: Boolean;
begin
  Result := ExpandConstant('{param:ISOLATED|0}') = '1';
end;
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then begin
    Result := FileExists(ExpandConstant('{app}\bin\64bit\obs64.exe'));
    if not Result then MsgBox('Choose the OBS root folder containing bin\64bit\obs64.exe. Close OBS before installing.', mbError, MB_OK);
  end;
end;
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not FileExists(ExpandConstant('{app}\bin\64bit\obs64.exe')) then Result := 'OBS was not found in the selected folder.';
  if not FileExists(ExpandConstant('{app}\bin\64bit\Qt6Network.dll')) then Result := 'This OBS installation is missing Qt6Network.dll. Repair OBS before installing AudioLink.';
end;
