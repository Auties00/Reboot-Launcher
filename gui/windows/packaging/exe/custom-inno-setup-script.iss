[Setup]
AppId={{APP_ID}}
AppVersion={{APP_VERSION}}
AppName={{DISPLAY_NAME}}
AppPublisher={{PUBLISHER_NAME}}
AppPublisherURL={{PUBLISHER_URL}}
AppSupportURL={{PUBLISHER_URL}}
AppUpdatesURL={{PUBLISHER_URL}}
DefaultDirName={autopf}\{{DISPLAY_NAME}}
DisableProgramGroupPage=yes
OutputBaseFilename={{OUTPUT_BASE_FILENAME}}
Compression=zip
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
ChangesEnvironment=yes
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
InstallingVC2017redist=Installing Visual C++ Redistributable

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce
Name: "launchAtStartup"; Description: "{cm:AutoStartProgram,{{DISPLAY_NAME}}}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Dirs]
Name: "{app}"; Permissions: everyone-full

[Files]
Source: "{{SOURCE_DIR}}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Permissions: everyone-full
Source: "..\..\dependencies\redist\VC_redist.x64.exe"; DestDir: {tmp}; Flags: dontcopy

[Run]
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -Command ""Add-MpPreference -ExclusionPath '{app}'"""; Flags: runhidden
Filename: "{app}\{{EXECUTABLE_NAME}}"; Description: "{cm:LaunchProgram,{{DISPLAY_NAME}}}"; Flags: runascurrentuser nowait postinstall skipifsilent
Filename: "{tmp}\VC_redist.x64.exe"; StatusMsg: "{cm:InstallingVC2017redist}"; Parameters: "/quiet"; Check: VC2017RedistNeedsInstall; Flags: waituntilterminated

[Icons]
Name: "{autoprograms}\{{DISPLAY_NAME}}"; Filename: "{app}\{{EXECUTABLE_NAME}}"
Name: "{autodesktop}\{{DISPLAY_NAME}}"; Filename: "{app}\{{EXECUTABLE_NAME}}"; Tasks: desktopicon
Name: "{userstartup}\{{DISPLAY_NAME}}"; Filename: "{app}\{{EXECUTABLE_NAME}}"; WorkingDir: "{app}"; Tasks: launchAtStartup

[Code]
function CompareVersion(version1, version2: String): Integer;
var
    packVersion1, packVersion2: Int64;
begin
    if not StrToVersion(version1, packVersion1) then packVersion1 := 0;
    if not StrToVersion(version2, packVersion2) then packVersion2 := 0;
    Result := ComparePackedVersion(packVersion1, packVersion2);
end;

function BoolToStr(Value: Boolean): String;
begin
  if Value then
    Result := 'Yes'
  else
    Result := 'No';
end;

function VC2017RedistNeedsInstall: Boolean;
var
  Version: String;
begin
  if RegQueryStringValue(HKEY_LOCAL_MACHINE, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', Version) then
  begin
    Result := (CompareVersion(Copy(Version, 2, Length(Version)), '14.40.33810.00') < 0);
  end
  else
  begin
    Result := True;
  end;
  Log('Visual C++ Redistributable version: ' + Version);
  Log('Needs installation? ' + BoolToStr(Result));
  if (Result) then
  begin
    ExtractTemporaryFile('VC_redist.x64.exe');
  end;
end;

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType:string; ValueName: "OPENSSL_ia32cap"; ValueData: "~0x20000000"; Flags: preservestringtype

