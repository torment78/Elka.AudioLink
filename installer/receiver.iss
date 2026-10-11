#ifndef PayloadDir
  #error PayloadDir required
#endif
#ifndef OutputPath
  #error OutputPath required
#endif
[Setup]
AppId={{7AB7D0A5-596F-4B34-980C-76F5F263839B}
AppName=Elka AudioLink Receiver
AppVersion=0.1.0
AppPublisher=ElkaSoft
DefaultDirName={localappdata}\Programs\ElkaSoft\AudioLink
DefaultGroupName=Elka AudioLink
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern dark
LicenseFile=..\LICENSE
OutputDir={#OutputPath}
OutputBaseFilename=elka-audiolink-receiver-0.1.0-windows-x64-setup
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
CreateUninstallRegKey=not IsIsolated
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\ElkaAudioLink.exe
[Files]
Source: "{#PayloadDir}\receiver\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked
[Icons]
Name: "{group}\Elka AudioLink Receiver"; Filename: "{app}\ElkaAudioLink.exe"; Check: not IsIsolated
Name: "{autodesktop}\Elka AudioLink Receiver"; Filename: "{app}\ElkaAudioLink.exe"; Tasks: desktopicon; Check: not IsIsolated
[Code]
function IsIsolated: Boolean;
begin
  Result := ExpandConstant('{param:ISOLATED|0}') = '1';
end;
