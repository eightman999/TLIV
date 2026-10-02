; TLIV installer - Inno Setup 6 (per-user, no admin)
; Build:  "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\TLIV.iss
; Needs build\TLIV.exe (run build.bat first, or just release.bat). Output: dist\TLIVSetup-<version>.exe
; The version is read from the built exe (set in src\version.h).

#define MyAppName "TLIV"
#define MyAppVersion GetStringFileInfo("..\build\TLIV.exe", PRODUCT_VERSION)
#define MyAppPublisher "Micromochi_Teno"
#define MyAppId "MicromochiTeno.TLIV"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
ShowLanguageDialog=yes
InfoAfterFile=Readme.txt
OutputDir=..\dist
OutputBaseFilename=TLIVSetup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
; Tells the shell that file associations changed when install/uninstall finishes.
ChangesAssociations=yes
VersionInfoVersion={#MyAppVersion}
UninstallDisplayIcon={app}\TLIV.exe

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
; 中国語は Inno Setup 公式リポジトリ（jrsoftware/issrc の Files/Languages）から取得して同梱
Name: "zhcn"; MessagesFile: "ChineseSimplified.isl"
Name: "zhtw"; MessagesFile: "ChineseTraditional.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build\TLIV.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "Readme.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\TLIV.exe"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\TLIV.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\TLIV.exe"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Settings written next to the exe by the app.
Type: files; Name: "{app}\TLIV.ini"

[Registry]
; "Open with" candidate only. The default app of any extension is not touched.
Root: HKCU; Subkey: "Software\Classes\Applications\TLIV.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "TLIV"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\TLIV.exe\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\TLIV.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\Applications\TLIV.exe\SupportedTypes"; ValueType: string; ValueName: ".png"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLIV.exe\SupportedTypes"; ValueType: string; ValueName: ".apng"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLIV.exe\SupportedTypes"; ValueType: string; ValueName: ".jpg"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLIV.exe\SupportedTypes"; ValueType: string; ValueName: ".jpeg"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLIV.exe\SupportedTypes"; ValueType: string; ValueName: ".jfif"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLIV.exe\SupportedTypes"; ValueType: string; ValueName: ".gif"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLIV.exe\SupportedTypes"; ValueType: string; ValueName: ".bmp"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLIV.exe\SupportedTypes"; ValueType: string; ValueName: ".webp"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLIV.exe\SupportedTypes"; ValueType: string; ValueName: ".ico"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLIV.exe\SupportedTypes"; ValueType: string; ValueName: ".svg"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\TLIV.Image"; ValueType: string; ValueName: ""; ValueData: "TLIV Image"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\TLIV.Image\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\TLIV.exe,0"
Root: HKCU; Subkey: "Software\Classes\TLIV.Image\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\TLIV.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\.png"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.png\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.png\OpenWithProgids"; ValueType: string; ValueName: "TLIV.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.apng"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.apng\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.apng\OpenWithProgids"; ValueType: string; ValueName: "TLIV.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.jpg"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.jpg\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.jpg\OpenWithProgids"; ValueType: string; ValueName: "TLIV.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.jpeg"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.jpeg\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.jpeg\OpenWithProgids"; ValueType: string; ValueName: "TLIV.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.jfif"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.jfif\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.jfif\OpenWithProgids"; ValueType: string; ValueName: "TLIV.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.gif"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.gif\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.gif\OpenWithProgids"; ValueType: string; ValueName: "TLIV.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.bmp"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.bmp\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.bmp\OpenWithProgids"; ValueType: string; ValueName: "TLIV.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.webp"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.webp\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.webp\OpenWithProgids"; ValueType: string; ValueName: "TLIV.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.ico"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.ico\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.ico\OpenWithProgids"; ValueType: string; ValueName: "TLIV.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.svg"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.svg\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.svg\OpenWithProgids"; ValueType: string; ValueName: "TLIV.Image"; ValueData: ""; Flags: uninsdeletevalue
