[Setup]
AppId={{F4F6D7B0-0D53-4E6A-B402-8DF816A2762A}
AppName=subConverter
AppVersion=1.0.0
AppPublisher=DSCOM
DefaultDirName={autopf}\subConverter
DefaultGroupName=subConverter
OutputDir=Output
OutputBaseFilename=subConverter_setup
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
SetupIconFile=icon.ico
UninstallDisplayIcon={app}\icon.ico

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"

[Files]
Source: "build\subConverter.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\subConverterEngine\*"; DestDir: "{app}\subConverterEngine"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "icon.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "install_context_menu.cmd"; DestDir: "{app}"; Flags: ignoreversion
Source: "uninstall_context_menu.cmd"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.smi\shell\subConverter.Rename"; Flags: deletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\subConverter.Rename"; Flags: deletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.ass\shell\subConverter.Rename"; Flags: deletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.smi\shell\subConverter.ToSmi"; ValueType: string; ValueName: ""; ValueData: "subConverter - ToSmi"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.smi\shell\subConverter.ToSmi"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\icon.ico"""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.smi\shell\subConverter.ToSmi"; ValueType: string; ValueName: "MultiSelectModel"; ValueData: "Player"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.smi\shell\subConverter.ToSmi\command"; ValueType: string; ValueName: ""; ValueData: """{app}\subConverter.exe"" /to:smi ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.smi\shell\subConverter.ToSrt"; ValueType: string; ValueName: ""; ValueData: "subConverter - ToSrt"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.smi\shell\subConverter.ToSrt"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\icon.ico"""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.smi\shell\subConverter.ToSrt"; ValueType: string; ValueName: "MultiSelectModel"; ValueData: "Player"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.smi\shell\subConverter.ToSrt\command"; ValueType: string; ValueName: ""; ValueData: """{app}\subConverter.exe"" /to:srt ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.smi\shell\subConverter.ToAss"; ValueType: string; ValueName: ""; ValueData: "subConverter - ToAss"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.smi\shell\subConverter.ToAss"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\icon.ico"""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.smi\shell\subConverter.ToAss"; ValueType: string; ValueName: "MultiSelectModel"; ValueData: "Player"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.smi\shell\subConverter.ToAss\command"; ValueType: string; ValueName: ""; ValueData: """{app}\subConverter.exe"" /to:ass ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\subConverter.ToSmi"; ValueType: string; ValueName: ""; ValueData: "subConverter - ToSmi"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\subConverter.ToSmi"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\icon.ico"""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\subConverter.ToSmi"; ValueType: string; ValueName: "MultiSelectModel"; ValueData: "Player"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\subConverter.ToSmi\command"; ValueType: string; ValueName: ""; ValueData: """{app}\subConverter.exe"" /to:smi ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\subConverter.ToSrt"; ValueType: string; ValueName: ""; ValueData: "subConverter - ToSrt"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\subConverter.ToSrt"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\icon.ico"""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\subConverter.ToSrt"; ValueType: string; ValueName: "MultiSelectModel"; ValueData: "Player"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\subConverter.ToSrt\command"; ValueType: string; ValueName: ""; ValueData: """{app}\subConverter.exe"" /to:srt ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\subConverter.ToAss"; ValueType: string; ValueName: ""; ValueData: "subConverter - ToAss"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\subConverter.ToAss"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\icon.ico"""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\subConverter.ToAss"; ValueType: string; ValueName: "MultiSelectModel"; ValueData: "Player"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\subConverter.ToAss\command"; ValueType: string; ValueName: ""; ValueData: """{app}\subConverter.exe"" /to:ass ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.ass\shell\subConverter.ToSmi"; ValueType: string; ValueName: ""; ValueData: "subConverter - ToSmi"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.ass\shell\subConverter.ToSmi"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\icon.ico"""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.ass\shell\subConverter.ToSmi"; ValueType: string; ValueName: "MultiSelectModel"; ValueData: "Player"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.ass\shell\subConverter.ToSmi\command"; ValueType: string; ValueName: ""; ValueData: """{app}\subConverter.exe"" /to:smi ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.ass\shell\subConverter.ToSrt"; ValueType: string; ValueName: ""; ValueData: "subConverter - ToSrt"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.ass\shell\subConverter.ToSrt"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\icon.ico"""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.ass\shell\subConverter.ToSrt"; ValueType: string; ValueName: "MultiSelectModel"; ValueData: "Player"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.ass\shell\subConverter.ToSrt\command"; ValueType: string; ValueName: ""; ValueData: """{app}\subConverter.exe"" /to:srt ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.ass\shell\subConverter.ToAss"; ValueType: string; ValueName: ""; ValueData: "subConverter - ToAss"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.ass\shell\subConverter.ToAss"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\icon.ico"""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.ass\shell\subConverter.ToAss"; ValueType: string; ValueName: "MultiSelectModel"; ValueData: "Player"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.ass\shell\subConverter.ToAss\command"; ValueType: string; ValueName: ""; ValueData: """{app}\subConverter.exe"" /to:ass ""%1"""; Flags: uninsdeletekey
