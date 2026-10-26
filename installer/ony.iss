; Ony 설치 프로그램 (Inno Setup 6). 코드 서명 없음.
; 사용: iscc installer\ony.iss   (사전에 dist\ 에 ony.exe 와 Qt DLL 이 있어야 함)
#define AppName "Ony"
#define AppVersion "0.1.0"
#define AppExe "ony.exe"

[Setup]
AppId={{B7E4C1D2-5A3F-4E8B-9C21-6D0F7A8E1B34}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Ony
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir=..\release
OutputBaseFilename=Ony-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExe}
; 관리자 권한 없이 현재 사용자에게만 설치할 수도 있다
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"

[Tasks]
Name: "desktopicon"; Description: "바탕화면에 바로가기 만들기"; GroupDescription: "추가 작업:"
Name: "assoc"; Description: ".ony 파일을 Ony 로 열기"; GroupDescription: "추가 작업:"

[Files]
Source: "..\dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.ony"; ValueType: string; ValueName: ""; ValueData: "Ony.Project"; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Ony.Project"; ValueType: string; ValueName: ""; ValueData: "Ony 프로젝트"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Ony.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Ony.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc

[Run]
Filename: "{app}\{#AppExe}"; Description: "Ony 실행"; Flags: nowait postinstall skipifsilent
