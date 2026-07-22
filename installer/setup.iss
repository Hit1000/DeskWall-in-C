[Setup]
AppId={{B1F7A3D2-5E4C-4A8B-9F6D-2C3E8A1B7D5F}
AppName=DeskWall
AppVersion=1.0.0
AppPublisher=DeskWall
DefaultDirName={autopf}\DeskWall
DefaultGroupName=DeskWall
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=Output
OutputBaseFilename=DeskWallSetup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=DeskWall
UninstallDisplayIcon={app}\deskwall.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "..\build\Release\deskwall.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\DeskWall"; Filename: "{app}\deskwall.exe"
Name: "{group}\Uninstall DeskWall"; Filename: "{uninstallexe}"
Name: "{autodesktop}\DeskWall"; Filename: "{app}\deskwall.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\deskwall.exe"; Description: "Launch DeskWall now"; Flags: nowait postinstall skipifsilent

[Code]
// Auto-launch on Windows startup is handled by the app itself via HKCU Run key.
// The installer just copies the files — the app registers itself on first launch.
