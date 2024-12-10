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
Filename: "{app}\{{EXECUTABLE_NAME}}"; Description: "{cm:LaunchProgram,{{DISPLAY_NAME}}}"; Flags: runascurrentuser nowait postinstall skipifsilent
Filename: "{tmp}\VC_redist.x64.exe"; StatusMsg: "{cm:InstallingVC2017redist}"; Parameters: "/quiet"; Check: VC2017RedistNeedsInstall; Flags: waituntilterminated

[Icons]
Name: "{autoprograms}\{{DISPLAY_NAME}}"; Filename: "{app}\{{EXECUTABLE_NAME}}"
Name: "{autodesktop}\{{DISPLAY_NAME}}"; Filename: "{app}\{{EXECUTABLE_NAME}}"; Tasks: desktopicon
Name: "{userstartup}\{{DISPLAY_NAME}}"; Filename: "{app}\{{EXECUTABLE_NAME}}"; WorkingDir: "{app}"; Tasks: launchAtStartup

[Code]
var
  Page: TInputOptionWizardPage;

procedure InitializeWizard();
begin
  Page := CreateInputOptionPage(
         wpWelcome,
        '   Allow DLL injection',
        ' The Reboot Launcher needs to inject DLLs into Fortnite to create the game server',
        'Selecting the option below will add the Reboot Launcher to the Windows Exclusions list. ' +
        'This is necessary because DLL injection is often detected as a virus, but is necessary to modify Fortnite. ' +
        'This option was designed for advanced users who want to manually manage the exclusions list on their machine. ' +
        'If you do not trust the Reboot Launcher, you can audit the source code at https://github.com/Auties00/reboot_launcher and build it from source.',
        False,
        False
  );
  Page.Add('&Add the launcher to the Windows Exclusions list');
  Page.Values[0] := True;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  InstallationDir: String;
begin
  if (CurStep = ssPostInstall) and Page.Values[0] then
  begin
    InstallationDir := ExpandConstant('{app}');
    Exec('powershell.exe', '-ExecutionPolicy Bypass -Command ""Add-MpPreference -ExclusionPath ''' + InstallationDir + '''""' , '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Log('Powershell exit code: ' + IntToStr(ResultCode));
  end;
end;

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

