; UTF-8 with BOM. AppId is permanent: never regenerate it for a new release.
#ifndef AppVersion
  #define AppVersion "0.2.0-beta"
#endif
#ifndef AppVersionNumeric
  #define AppVersionNumeric "0.2.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\SubRec"
#endif
#ifndef OutputName
  #define OutputName "SubRec-dev-win64-setup"
#endif
#ifndef RepoURL
  #error RepoURL must be supplied (the actual project repository URL)
#endif
#if !FileExists(SourcePath + "ChineseSimplified.isl")
  #error ChineseSimplified.isl is missing; obtain the upstream translation and review its license (README.md)
#endif

[Setup]
AppId={{04DE0717-B7D3-492D-84C6-73C1E49B54F8}
AppName=字录 SubRec
AppVersion={#AppVersion}
AppVerName=字录 SubRec {#AppVersion}
AppPublisher=SubRec
AppPublisherURL={#RepoURL}
AppSupportURL={#RepoURL}/issues
AppUpdatesURL={#RepoURL}/releases
VersionInfoVersion={#AppVersionNumeric}
DefaultDirName={autopf}\SubRec
DisableProgramGroupPage=yes
DisableDirPage=auto
UsePreviousAppDir=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
WizardStyle=modern
SetupIconFile=SubRec.ico
UninstallDisplayIcon={app}\appSubRec.exe
UninstallDisplayName=字录 SubRec
InfoBeforeFile=NOTICE.txt
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir=..\..\out
OutputBaseFilename={#OutputName}
; Match the application's explicit session namespace, also during uninstall.
AppMutex=Local\SubRec.SingleInstance
SetupMutex=Local\SubRec.Setup
CloseApplications=yes
RestartApplications=no
LanguageDetectionMethod=uilanguage
ShowLanguageDialog=auto

[Languages]
Name: "zh_CN"; MessagesFile: "ChineseSimplified.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Messages]
zh_CN.InfoBeforeLabel=许可与源码说明。安装或升级前，请先停止录制并从托盘退出字录。
en.InfoBeforeLabel=License and source notice. Before installing or upgrading, stop recording and exit SubRec from the tray.

[CustomMessages]
zh_CN.ViewNotice=查看许可说明
en.ViewNotice=View license notice

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[InstallDelete]
; Generated from the payload and the retained list of earlier deployment dirs.
#include "installdelete.generated.iss"
Type: files; Name: "{app}\*.dll"

[Files]
; sources/ and licenses/ are mandatory, exactly as in the portable payload.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "NOTICE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "SubRec.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\字录 SubRec"; Filename: "{app}\appSubRec.exe"; WorkingDir: "{app}"; IconFilename: "{app}\SubRec.ico"
Name: "{autodesktop}\字录 SubRec"; Filename: "{app}\appSubRec.exe"; WorkingDir: "{app}"; IconFilename: "{app}\SubRec.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\appSubRec.exe"; Description: "{cm:LaunchProgram,字录 SubRec}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\NOTICE.txt"; Description: "{cm:ViewNotice}"; Flags: postinstall shellexec skipifsilent unchecked

[UninstallDelete]
Type: files; Name: "{app}\SubRec.log"
Type: dirifempty; Name: "{app}"
