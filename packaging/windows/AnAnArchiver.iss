#ifndef PackageRoot
  #error PackageRoot must identify the complete deployed portable directory
#endif
#ifndef OutputRoot
  #error OutputRoot is required
#endif
#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef OutputName
  #define OutputName "AnAnArchiver-" + AppVersion + "-windows-x64-setup"
#endif

[Setup]
AppId={{B5695574-44A8-4DAB-AD67-31776F46CC31}
AppName=AnAnArchiver
AppVersion={#AppVersion}
AppPublisher=ananwanan
AppPublisherURL=https://github.com/ananwanan/AnAnArchiver
AppSupportURL=https://github.com/ananwanan/AnAnArchiver/issues
DefaultDirName={localappdata}\Programs\AnAnArchiver
CreateAppDir=no
UsePreviousAppDir=no
DefaultGroupName=AnAnArchiver
PrivilegesRequired=lowest
SetupArchitecture=x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
DisableProgramGroupPage=yes
DisableDirPage=yes
AllowNoIcons=yes
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
OutputDir={#OutputRoot}
OutputBaseFilename={#OutputName}
UninstallDisplayIcon={code:ExecutablePath}
CreateUninstallRegKey=not IsIntegrationTest
ChangesAssociations=not IsIntegrationTest
CloseApplications=yes
RestartApplications=no
UsePreviousTasks=yes
UninstallFilesDir={code:UninstallDataRoot}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[CustomMessages]
english.Associations=Register AnAnArchiver in Open with for supported archives
english.ContextMenu=Add Open / Extract / Compress commands to Explorer's context menu
english.DesktopIcon=Create a desktop shortcut
english.IntegrationGroup=Optional system integration:
english.MoveInstallDir=AnAnArchiver is already installed in another directory. Uninstall that installation before choosing a different directory.
english.InstallFolderTitle=Installation folder
english.InstallFolderDescription=Choose where AnAnArchiver will be installed.
english.InstallFolderPrompt=Enter an absolute folder path. Extended-length paths are supported.
english.InstallFolderLabel=Folder:
english.InvalidInstallFolder=Enter a valid absolute folder path with valid Windows names. Root folders, relative components, trailing dots or spaces and device names are not supported.
chinesesimplified.Associations=为支持的归档格式注册 AnAnArchiver“打开方式”
chinesesimplified.ContextMenu=在资源管理器右键菜单中添加“打开 / 解压 / 压缩”
chinesesimplified.DesktopIcon=创建桌面快捷方式
chinesesimplified.IntegrationGroup=可选的系统集成：
chinesesimplified.MoveInstallDir=AnAnArchiver 已安装在其他目录。请先卸载原安装，再选择不同的安装目录。
chinesesimplified.InstallFolderTitle=安装目录
chinesesimplified.InstallFolderDescription=选择 AnAnArchiver 的安装位置。
chinesesimplified.InstallFolderPrompt=请输入绝对目录路径。支持超长路径。
chinesesimplified.InstallFolderLabel=目录：
chinesesimplified.InvalidInstallFolder=请输入有效的绝对目录路径和 Windows 文件夹名称。不支持根目录、相对路径段、末尾的点或空格及设备名称。
english.ArchiveDescription=AnAnArchiver archive
chinesesimplified.ArchiveDescription=AnAnArchiver 归档
english.OpenMenu=Open with AnAnArchiver
chinesesimplified.OpenMenu=用 AnAnArchiver 打开
english.ExtractHereMenu=Extract here with AnAnArchiver
chinesesimplified.ExtractHereMenu=用 AnAnArchiver 解压到当前位置
english.ExtractFolderMenu=Extract to folder with AnAnArchiver
chinesesimplified.ExtractFolderMenu=用 AnAnArchiver 解压到同名文件夹
english.CompressMenu=Compress with AnAnArchiver
chinesesimplified.CompressMenu=用 AnAnArchiver 压缩

[Tasks]
Name: "associations"; Description: "{cm:Associations}"; GroupDescription: "{cm:IntegrationGroup}"; Flags: unchecked
Name: "contextmenu"; Description: "{cm:ContextMenu}"; GroupDescription: "{cm:IntegrationGroup}"; Flags: unchecked
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; GroupDescription: "{cm:IntegrationGroup}"; Flags: unchecked

[Files]
Source: "{#PackageRoot}\*"; DestDir: "{code:InstallRoot}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PackageRoot}\package-owned-files.txt"; DestName: "ananarchiver-new-owned-files.txt"; Flags: dontcopy

[Icons]
Name: "{group}\AnAnArchiver"; Filename: "{code:ExecutablePath}"; Check: not IsIntegrationTest
Name: "{group}\Uninstall AnAnArchiver"; Filename: "{uninstallexe}"; Check: not IsIntegrationTest
Name: "{autodesktop}\AnAnArchiver"; Filename: "{code:ExecutablePath}"; Tasks: desktopicon; Check: not IsIntegrationTest

[Run]
Filename: "{code:ExecutablePath}"; Description: "{cm:LaunchProgram,AnAnArchiver}"; Flags: shellexec nowait postinstall skipifsilent; Check: not IsIntegrationTest

[UninstallDelete]
Type: files; Name: "{code:InstallRoot}\.integration-test-namespace"
Type: files; Name: "{code:UninstallDataRoot}\.integration-test-namespace"
Type: files; Name: "{code:UninstallDataRoot}\install-root.txt"
Type: dirifempty; Name: "{code:UninstallDataRoot}"
Type: dirifempty; Name: "{localappdata}\AnAnArchiver\Installer"; Check: not IsIntegrationTest
Type: dirifempty; Name: "{localappdata}\AnAnArchiver"; Check: not IsIntegrationTest

[Code]
const
  ArchiveProgID = 'AnAnArchiver.Archive';
  ArchiveExtensions = '.zip;.zipx;.7z;.rar;.tar;.gz;.tgz;.bz2;.tbz;.tbz2;.xz;.txz;.zst;.tzst;.lz4;.cab;.cpio;.ar;.iso;.lha;.lzh;.xar';
  TestPrefix = 'Software\AnAnArchiver\PackageValidation\';
var
  CachedRegistryPrefix: String;
  RegistryPrefixLoaded: Boolean;
  InstallFolderPage: TInputQueryWizardPage;
  InstallFolderBrowseButton: TNewButton;
  SelectedInstallFolder: String;

function GetFileAttributesW(Name: String): LongWord;
  external 'GetFileAttributesW@kernel32.dll stdcall';
function NativeLastError: LongWord;
  external 'GetLastError@kernel32.dll stdcall';

function ValidTestPrefix(Value: String): Boolean; forward;
function AppRoot: String; forward;
function ValidInstallFolder(Value: String): Boolean; forward;

function InstallRoot(Param: String): String;
var
  Stored: TArrayOfString;
  Owner: String;
begin
  if IsUninstaller and (SelectedInstallFolder = '') then begin
    if not LoadStringsFromFile(ExtractFileDir(ExpandConstant('{uninstallexe}')) + '\install-root.txt', Stored) or
       (GetArrayLength(Stored) <> 1) then RaiseException('The installation directory record could not be read.');
    SelectedInstallFolder := Stored[0];
    if not ValidInstallFolder(SelectedInstallFolder) or
       not RegQueryStringValue(HKCU, AppRoot + '\Integration', 'InstallPath', Owner) or
       (CompareText(Owner, SelectedInstallFolder) <> 0) then
      RaiseException('The installation directory record does not match its owner registration.');
  end;
  begin
    Result := SelectedInstallFolder;
    if Result = '' then Result := ExpandConstant('{localappdata}\Programs\AnAnArchiver');
  end;
end;

function UninstallDataRoot(Param: String): String;
var
  Prefix: String;
begin
  if IsUninstaller then Result := ExtractFileDir(ExpandConstant('{uninstallexe}'))
  else begin
    Prefix := ExpandConstant('{param:REGISTRYPREFIX|}');
    if Prefix <> '' then begin
      if not ValidTestPrefix(Prefix) then RaiseException('Invalid installer test registry namespace.');
      Result := AddBackslash(GetEnv('TEMP')) + 'AnAnArchiver-InstallerData-' + Copy(Prefix, Length(Prefix) - 35, 36);
    end else Result := ExpandConstant('{localappdata}\AnAnArchiver\Installer\B5695574-44A8-4DAB-AD67-31776F46CC31');
  end;
end;

function ExecutablePath(Param: String): String;
begin
  Result := InstallRoot('') + '\AnAnArchiver.exe';
  if Length(Result) >= 248 then Result := PathConvertNormalToSuper(Result);
end;

function CharacterPosition(Needle: Char; Value: String): Integer;
var
  I: Integer;
begin
  Result := 0;
  { Pos(AnsiString literal, UnicodeString) may return a byte offset. Keep path
    positions in Unicode code units, matching Copy/Delete and Windows paths. }
  for I := 1 to Length(Value) do begin
    if Value[I] = Needle then begin Result := I; Exit; end;
  end;
end;

function ValidTestPrefix(Value: String): Boolean;
var
  Guid: String;
  I: Integer;
begin
  Result := False;
  if Copy(Value, 1, Length(TestPrefix)) <> TestPrefix then Exit;
  Guid := Copy(Value, Length(TestPrefix) + 1, MaxInt);
  if Length(Guid) <> 36 then Exit;
  for I := 1 to 36 do begin
    if (I = 9) or (I = 14) or (I = 19) or (I = 24) then begin
      if Guid[I] <> '-' then Exit;
    end else if Pos(Lowercase(Guid[I]), '0123456789abcdef') = 0 then Exit;
  end;
  Result := True;
end;

function RegistryPrefix: String;
var
  Stored: AnsiString;
begin
  if not RegistryPrefixLoaded then begin
    CachedRegistryPrefix := ExpandConstant('{param:REGISTRYPREFIX|}');
    if IsUninstaller and (CachedRegistryPrefix = '') then begin
      if LoadStringFromFile(UninstallDataRoot('') + '\.integration-test-namespace', Stored) then
        CachedRegistryPrefix := Trim(String(Stored))
      else if LoadStringFromFile(ExpandConstant('{code:InstallRoot}\.integration-test-namespace'), Stored) then
        CachedRegistryPrefix := Trim(String(Stored));
    end;
    if CachedRegistryPrefix <> '' then begin
      if not ValidTestPrefix(CachedRegistryPrefix) then
        RaiseException('REGISTRYPREFIX must be Software\AnAnArchiver\PackageValidation\ followed by a GUID.');
    end;
    RegistryPrefixLoaded := True;
  end;
  Result := CachedRegistryPrefix;
end;

function IsIntegrationTest: Boolean;
begin
  Result := RegistryPrefix <> '';
end;

function ClassesRoot: String;
begin
  if IsIntegrationTest then Result := RegistryPrefix + '\Classes'
  else Result := 'Software\Classes';
end;

function AppRoot: String;
begin
  if IsIntegrationTest then Result := RegistryPrefix + '\AnAnArchiver'
  else Result := 'Software\AnAnArchiver';
end;

function RegisteredApplicationsRoot: String;
begin
  if IsIntegrationTest then Result := RegistryPrefix + '\RegisteredApplications'
  else Result := 'Software\RegisteredApplications';
end;

function Executable: String;
begin
  Result := '"' + ExecutablePath('') + '"';
end;

function OpenCommand: String;
begin
  Result := Executable + ' "%1"';
end;

function CommandForAction(Action: String): String;
begin
  if Action = 'open' then Result := OpenCommand
  else Result := Executable + ' --' + Action + ' "%1"';
end;

function CommandOwned(Key, Expected: String): Boolean;
var
  Value: String;
begin
  Result := False;
  { Existing incomplete or COM-delegated verbs are not owned just because their
    default command is missing. Never overwrite or uninstall their private data. }
  if RegValueExists(HKCU, Key + '\command', 'DelegateExecute') or
     RegValueExists(HKCU, Key, 'ExplorerCommandHandler') then Exit;
  if not RegQueryStringValue(HKCU, Key + '\command', '', Value) then Exit;
  Result := Value = Expected;
end;

function CommandAvailable(Key, Expected, Previous: String): Boolean;
begin
  Result := False;
  if not RegKeyExists(HKCU, Key) then begin Result := True; Exit; end;
  Result := CommandOwned(Key, Expected) or
    ((Previous <> '') and (Previous[1] = '"') and CommandOwned(Key, Previous));
end;

function CheckRegistrationOwnership: Boolean;
var
  Owner, PreviousExe, PreviousOpen, Remaining, Extension, Key: String;
  Separator: Integer;
begin
  Result := True;
  PreviousExe := '';
  PreviousOpen := '';
  if RegQueryStringValue(HKCU, AppRoot + '\Integration', 'InstallPath', Owner) then begin
    PreviousExe := '"' + Owner + '\AnAnArchiver.exe"';
    if Length(Owner + '\AnAnArchiver.exe') >= 248 then
      PreviousExe := '"' + PathConvertNormalToSuper(Owner + '\AnAnArchiver.exe') + '"';
    PreviousOpen := PreviousExe + ' "%1"';
  end;
  if IsTaskSelected('associations') then begin
    if not CommandAvailable(ClassesRoot + '\' + ArchiveProgID + '\shell\open', OpenCommand, PreviousOpen) or
       not CommandAvailable(ClassesRoot + '\Applications\AnAnArchiver.exe\shell\open', OpenCommand, PreviousOpen) then begin
      Result := False; Exit;
    end;
    if RegKeyExists(HKCU, AppRoot + '\Capabilities') and (PreviousExe = '') then begin
      Result := False; Exit;
    end;
  end;
  if not IsTaskSelected('contextmenu') then Exit;
  Remaining := ArchiveExtensions;
  repeat
    Separator := CharacterPosition(';', Remaining);
    if Separator = 0 then begin Extension := Remaining; Remaining := ''; end
    else begin Extension := Copy(Remaining, 1, Separator - 1); Delete(Remaining, 1, Separator); end;
    Key := ClassesRoot + '\SystemFileAssociations\' + Extension + '\shell\AnAnArchiver.';
    if not CommandAvailable(Key + 'Open', OpenCommand, PreviousOpen) or
       not CommandAvailable(Key + 'ExtractHere', CommandForAction('extract-here'), PreviousExe + ' --extract-here "%1"') or
       not CommandAvailable(Key + 'ExtractToFolder', CommandForAction('extract-to-folder'), PreviousExe + ' --extract-to-folder "%1"') then begin
      Result := False; Exit;
    end;
  until Remaining = '';
  Result := CommandAvailable(ClassesRoot + '\*\shell\AnAnArchiver.Compress',
    CommandForAction('compress'), PreviousExe + ' --compress "%1"') and
    CommandAvailable(ClassesRoot + '\Directory\shell\AnAnArchiver.Compress',
    CommandForAction('compress'), PreviousExe + ' --compress "%1"');
end;

function SafeOwnedPath(Value: String): Boolean;
var
  Segments, Segment: String;
  Separator, I: Integer;
begin
  Result := False;
  if (Value = '') or (Value[1] = '/') or (Value[1] = '\') or
     (Pos(':', Value) > 0) or (Pos('\', Value) > 0) then Exit;
  Segments := Value;
  repeat
    Separator := CharacterPosition('/', Segments);
    if Separator = 0 then begin Segment := Segments; Segments := ''; end
    else begin Segment := Copy(Segments, 1, Separator - 1); Delete(Segments, 1, Separator); end;
    if (Segment = '') or (Segment = '.') or (Segment = '..') then Exit;
    if (Segment[Length(Segment)] = '.') or (Segment[Length(Segment)] = ' ') then Exit;
    for I := 1 to Length(Segment) do
      if (Ord(Segment[I]) < 32) or (CharacterPosition(Segment[I], '<>:"|?*') > 0) then Exit;
  until Segments = '';
  Result := True;
end;

function WithoutReparsePoints(Relative: String): Boolean;
var
  Current, Remaining, Segment: String;
  Separator: Integer;
  Attributes, ErrorCode: LongWord;
begin
  Result := False;
  Current := ExpandConstant('{code:InstallRoot}');
  Attributes := GetFileAttributesW(PathConvertNormalToSuper(Current));
  if Attributes = $FFFFFFFF then begin
    ErrorCode := NativeLastError;
    RaiseException('The package directory could not be checked safely (Windows error ' + IntToStr(ErrorCode) + ').');
  end;
  if (Attributes and $400) <> 0 then Exit;
  Remaining := Relative;
  repeat
    Separator := CharacterPosition('/', Remaining);
    if Separator = 0 then begin Segment := Remaining; Remaining := ''; end
    else begin Segment := Copy(Remaining, 1, Separator - 1); Delete(Remaining, 1, Separator); end;
    Current := Current + '\' + Segment;
    Attributes := GetFileAttributesW(PathConvertNormalToSuper(Current));
    if Attributes = $FFFFFFFF then begin
      ErrorCode := NativeLastError;
      if (ErrorCode <> 2) and (ErrorCode <> 3) then
        RaiseException('An obsolete package path could not be checked safely: ' + Relative + ' (Windows error ' + IntToStr(ErrorCode) + ').');
      Exit;
    end;
    if (Attributes and $400) <> 0 then Exit;
  until Remaining = '';
  Result := True;
end;

procedure RemoveObsoleteOwnedFiles;
var
  Previous, Current: TArrayOfString;
  I, J, Separator: Integer;
  Relative, ExpectedHash, Candidate: String;
  Retained: Boolean;
begin
  if not LoadStringsFromFile(PathConvertNormalToSuper(ExpandConstant('{code:InstallRoot}\package-owned-files.txt')), Previous) then begin
    Log('No previous package ownership manifest was readable.');
    Exit;
  end;
  if not LoadStringsFromFile(ExpandConstant('{tmp}\ananarchiver-new-owned-files.txt'), Current) then
    RaiseException('The new package ownership manifest could not be read.');
  Log('Package ownership reconciliation: ' + IntToStr(GetArrayLength(Previous)) + ' previous entries, ' +
    IntToStr(GetArrayLength(Current)) + ' new entries.');
  for I := 0 to GetArrayLength(Previous) - 1 do begin
    Separator := CharacterPosition('|', Previous[I]);
    if Separator > 1 then begin
      Relative := Copy(Previous[I], 1, Separator - 1);
      ExpectedHash := Copy(Previous[I], Separator + 1, MaxInt);
      if SafeOwnedPath(Relative) and (Length(ExpectedHash) = 64) and WithoutReparsePoints(Relative) then begin
        Retained := False;
        for J := 0 to GetArrayLength(Current) - 1 do
          if Copy(Current[J], 1, Length(Relative) + 1) = Relative + '|' then Retained := True;
        if not Retained then begin
          Candidate := Relative;
          StringChangeEx(Candidate, '/', '\', True);
          Candidate := PathConvertNormalToSuper(ExpandConstant('{code:InstallRoot}\') + Candidate);
          if FileExists(Candidate) then begin
            { Preserve files the user replaced or edited after the older installation. }
            if CompareText(GetSHA256OfFile(Candidate), ExpectedHash) = 0 then begin
              if not DeleteFile(Candidate) then
                RaiseException('An obsolete package file could not be removed: ' + Relative);
              Log('Removed unchanged obsolete package file: ' + Relative);
            end else begin
              Log('Preserved modified obsolete package file: ' + Relative);
            end;
          end;
        end;
      end;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Owner: String;
begin
  Result := '';
  if not ValidInstallFolder(ExpandConstant('{code:InstallRoot}')) then begin
    Result := CustomMessage('InvalidInstallFolder');
    Exit;
  end;
  if RegQueryStringValue(HKCU, AppRoot + '\Integration', 'InstallPath', Owner) and
     (CompareText(Owner, ExpandConstant('{code:InstallRoot}')) <> 0) then begin
    { This release supports in-place upgrades. A second directory would leave the
      original uninstaller and integration ownership behind; refuse before writes. }
    Result := CustomMessage('MoveInstallDir') + #13#10 + Owner;
    Exit;
  end;
  if not CheckRegistrationOwnership then begin
    Result := 'An AnAnArchiver integration key belongs to another command. Disable the optional integration or remove that conflicting registration first.';
    Exit;
  end;
  ExtractTemporaryFile('ananarchiver-new-owned-files.txt');
end;

function ValidInstallFolder(Value: String): Boolean;
var
  Remaining, Component, BaseName, UpperName, Root: String;
  ServerName, ShareName: String;
  Separator, I, ComponentCount: Integer;
  UNC: Boolean;
begin
  Result := False;
  if (Length(Value) < 4) or (Length(Value) > 32000) then Exit;
  UNC := Copy(Value, 1, 2) = '\\';
  if UNC then begin
    Remaining := Copy(Value, 3, MaxInt);
    Separator := CharacterPosition('\', Remaining);
    if Separator < 2 then Exit;
    ServerName := Copy(Remaining, 1, Separator - 1);
    if (ServerName = '.') or (ServerName = '?') or
       (ServerName[Length(ServerName)] = '.') or (ServerName[Length(ServerName)] = ' ') then Exit;
    for I := 1 to Length(ServerName) do
      if (Ord(ServerName[I]) < 32) or (CharacterPosition(ServerName[I], '<>:"/|?*') > 0) then Exit;
    Root := '\\' + ServerName;
    Delete(Remaining, 1, Separator);
    Separator := CharacterPosition('\', Remaining);
    if Separator < 2 then Exit;
    ShareName := Copy(Remaining, 1, Separator - 1);
    if (ShareName[Length(ShareName)] = '.') or (ShareName[Length(ShareName)] = ' ') then Exit;
    for I := 1 to Length(ShareName) do
      if (Ord(ShareName[I]) < 32) or (CharacterPosition(ShareName[I], '<>:"/|?*') > 0) then Exit;
    Root := Root + '\' + ShareName;
    Delete(Remaining, 1, Separator);
  end else begin
    if (Pos(Uppercase(Value[1]), 'ABCDEFGHIJKLMNOPQRSTUVWXYZ') = 0) or
       (Copy(Value, 2, 2) <> ':\') then Exit;
    Root := Copy(Value, 1, 3);
    Remaining := Copy(Value, 4, MaxInt);
  end;
  if (Remaining = '') or not DirExists(Root) then begin
    Log('Invalid install folder root: ' + Root);
    Exit;
  end;
  ComponentCount := 0;
  repeat
    Separator := CharacterPosition('\', Remaining);
    if Separator = 0 then begin Component := Remaining; Remaining := ''; end
    else begin Component := Copy(Remaining, 1, Separator - 1); Delete(Remaining, 1, Separator); end;
    if (Component = '') or (Length(Component) > 255) then begin Log('Invalid folder component length'); Exit; end;
    if (Component[Length(Component)] = '.') or (Component[Length(Component)] = ' ') then begin
      Log('Invalid folder component ending: ' + Component + ', code ' + IntToStr(Ord(Component[Length(Component)]))); Exit;
    end;
    for I := 1 to Length(Component) do begin
      if (Ord(Component[I]) < 32) or (CharacterPosition(Component[I], '<>:"/|?*') > 0) then begin
        Log('Invalid folder component character: ' + IntToStr(Ord(Component[I]))); Exit;
      end;
      if (Ord(Component[I]) >= $D800) and (Ord(Component[I]) <= $DBFF) then begin
        if (I = Length(Component)) or (Ord(Component[I + 1]) < $DC00) or
           (Ord(Component[I + 1]) > $DFFF) then Exit;
      end else if (Ord(Component[I]) >= $DC00) and (Ord(Component[I]) <= $DFFF) then begin
        if (I = 1) or (Ord(Component[I - 1]) < $D800) or (Ord(Component[I - 1]) > $DBFF) then Exit;
      end;
    end;
    BaseName := Component;
    Separator := CharacterPosition('.', BaseName);
    if Separator > 0 then BaseName := Copy(BaseName, 1, Separator - 1);
    UpperName := Uppercase(BaseName);
    if (UpperName = 'CON') or (UpperName = 'PRN') or (UpperName = 'AUX') or (UpperName = 'NUL') or
       (UpperName = 'CONIN$') or (UpperName = 'CONOUT$') then Exit;
    if (Length(UpperName) = 4) and ((Copy(UpperName, 1, 3) = 'COM') or (Copy(UpperName, 1, 3) = 'LPT')) and
       (CharacterPosition(UpperName[4], '123456789¹²³') > 0) then Exit;
    ComponentCount := ComponentCount + 1;
    if ComponentCount > 40 then Exit;
  until Remaining = '';
  Result := True;
end;

procedure BrowseInstallFolder(Sender: TObject);
var
  Selected: String;
begin
  Selected := InstallFolderPage.Values[0];
  if BrowseForFolder(CustomMessage('InstallFolderDescription'), Selected, False) then
    InstallFolderPage.Values[0] := Selected;
end;

procedure InitializeWizard;
var
  Previous: String;
begin
  { Inno 7 supports extended-length file operations, but its stock directory page
    still rejects paths above 240 characters in ValidateCustomDirEdit:
    https://github.com/jrsoftware/issrc/blob/is-7_1_0/Projects/Src/Setup.WizardForm.pas
    MainForm.Install repeats this limit even with the stock page hidden. Use
    CreateAppDir=no and explicitly validated code:InstallRoot destinations. The
    unused stock app directory is never created or used for payload/metadata. }
  InstallFolderPage := CreateInputQueryPage(wpWelcome, CustomMessage('InstallFolderTitle'),
    CustomMessage('InstallFolderDescription'), CustomMessage('InstallFolderPrompt'));
  InstallFolderPage.Add(CustomMessage('InstallFolderLabel'), False);
  InstallFolderPage.Edits[0].MaxLength := 32000;
  SelectedInstallFolder := ExpandConstant('{param:DIR|}');
  if SelectedInstallFolder = '' then begin
    if RegQueryStringValue(HKCU, AppRoot + '\Integration', 'InstallPath', Previous) then
      SelectedInstallFolder := Previous
    else SelectedInstallFolder := ExpandConstant('{localappdata}\Programs\AnAnArchiver');
  end;
  InstallFolderPage.Values[0] := SelectedInstallFolder;
  InstallFolderBrowseButton := TNewButton.Create(WizardForm);
  InstallFolderBrowseButton.Parent := InstallFolderPage.Surface;
  InstallFolderBrowseButton.Caption := SetupMessage(msgButtonBrowse);
  InstallFolderBrowseButton.Width := ScaleX(85);
  InstallFolderBrowseButton.Height := WizardForm.NextButton.Height;
  InstallFolderBrowseButton.Left := InstallFolderPage.SurfaceWidth - InstallFolderBrowseButton.Width;
  InstallFolderBrowseButton.Top := InstallFolderPage.Edits[0].Top + InstallFolderPage.Edits[0].Height + ScaleY(8);
  InstallFolderBrowseButton.OnClick := @BrowseInstallFolder;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Selected: String;
begin
  Result := True;
  if CurPageID = InstallFolderPage.ID then begin
    Selected := RemoveBackslashUnlessRoot(PathConvertSuperToNormal(InstallFolderPage.Values[0]));
    Log('Selected installation folder (' + IntToStr(Length(Selected)) + ' characters): ' + Selected);
    if not ValidInstallFolder(Selected) then begin
      SuppressibleMsgBox(CustomMessage('InvalidInstallFolder'), mbError, MB_OK, IDOK);
      Result := False;
      Exit;
    end;
    InstallFolderPage.Values[0] := Selected;
    SelectedInstallFolder := Selected;
  end;
end;

procedure DeleteOwnedCommand(Key, Expected: String);
begin
  if CommandOwned(Key, Expected) then
    RegDeleteKeyIncludingSubkeys(HKCU, Key);
end;

procedure DeleteOwnedIntegration;
var
  Remaining, Extension, Value, Key, Owner: String;
  Separator: Integer;
  Owned: Boolean;
begin
  Owned := RegQueryStringValue(HKCU, AppRoot + '\Integration', 'InstallPath', Owner) and
    (CompareText(Owner, ExpandConstant('{code:InstallRoot}')) = 0);
  if not Owned then Exit;
  Remaining := ArchiveExtensions;
  repeat
    Separator := CharacterPosition(';', Remaining);
    if Separator = 0 then begin Extension := Remaining; Remaining := ''; end
    else begin Extension := Copy(Remaining, 1, Separator - 1); Delete(Remaining, 1, Separator); end;
    Key := ClassesRoot + '\' + Extension + '\OpenWithProgids';
    if RegQueryStringValue(HKCU, Key, ArchiveProgID, Value) and (Value = '') then begin
      RegDeleteValue(HKCU, Key, ArchiveProgID);
      RegDeleteKeyIfEmpty(HKCU, Key);
      RegDeleteKeyIfEmpty(HKCU, ClassesRoot + '\' + Extension);
    end;
    Key := ClassesRoot + '\SystemFileAssociations\' + Extension + '\shell\AnAnArchiver.';
    DeleteOwnedCommand(Key + 'Open', OpenCommand);
    DeleteOwnedCommand(Key + 'ExtractHere', CommandForAction('extract-here'));
    DeleteOwnedCommand(Key + 'ExtractToFolder', CommandForAction('extract-to-folder'));
    RegDeleteKeyIfEmpty(HKCU, ClassesRoot + '\SystemFileAssociations\' + Extension + '\shell');
    RegDeleteKeyIfEmpty(HKCU, ClassesRoot + '\SystemFileAssociations\' + Extension);
  until Remaining = '';
  DeleteOwnedCommand(ClassesRoot + '\*\shell\AnAnArchiver.Compress', CommandForAction('compress'));
  DeleteOwnedCommand(ClassesRoot + '\Directory\shell\AnAnArchiver.Compress', CommandForAction('compress'));
  RegDeleteKeyIfEmpty(HKCU, ClassesRoot + '\*\shell');
  RegDeleteKeyIfEmpty(HKCU, ClassesRoot + '\*');
  RegDeleteKeyIfEmpty(HKCU, ClassesRoot + '\Directory\shell');
  RegDeleteKeyIfEmpty(HKCU, ClassesRoot + '\Directory');
  RegDeleteKeyIfEmpty(HKCU, ClassesRoot + '\SystemFileAssociations');
  if CommandOwned(ClassesRoot + '\' + ArchiveProgID + '\shell\open', OpenCommand) then
    RegDeleteKeyIncludingSubkeys(HKCU, ClassesRoot + '\' + ArchiveProgID);
  if CommandOwned(ClassesRoot + '\Applications\AnAnArchiver.exe\shell\open', OpenCommand) then
    RegDeleteKeyIncludingSubkeys(HKCU, ClassesRoot + '\Applications\AnAnArchiver.exe');
  RegDeleteKeyIfEmpty(HKCU, ClassesRoot + '\Applications');
  if RegQueryStringValue(HKCU, RegisteredApplicationsRoot, 'AnAnArchiver', Value)
      and (Value = AppRoot + '\Capabilities') then RegDeleteValue(HKCU, RegisteredApplicationsRoot, 'AnAnArchiver');
  RegDeleteKeyIfEmpty(HKCU, RegisteredApplicationsRoot);
  RegDeleteKeyIncludingSubkeys(HKCU, AppRoot + '\Capabilities');
  RegDeleteKeyIncludingSubkeys(HKCU, AppRoot + '\Integration');
  RegDeleteKeyIfEmpty(HKCU, AppRoot);
  if IsIntegrationTest then begin
    Key := RegistryPrefix + '\Uninstall\AnAnArchiver';
    if RegQueryStringValue(HKCU, Key, 'InstallLocation', Value) and
      (CompareText(Value, ExpandConstant('{code:InstallRoot}')) = 0) then RegDeleteKeyIncludingSubkeys(HKCU, Key);
    RegDeleteKeyIfEmpty(HKCU, RegistryPrefix + '\Uninstall');
    RegDeleteKeyIfEmpty(HKCU, ClassesRoot);
  end;
end;

procedure WriteString(Key, Name, Value: String);
begin
  if not RegWriteStringValue(HKCU, Key, Name, Value) then
    RaiseException('Could not register AnAnArchiver system integration.');
end;

procedure RegisterMenu(Key, MenuText, Action: String);
begin
  WriteString(Key, '', MenuText);
  WriteString(Key, 'Icon', Executable + ',0');
  WriteString(Key, 'MultiSelectModel', 'Single');
  WriteString(Key + '\command', '', CommandForAction(Action));
end;

procedure RegisterIntegration;
var
  Remaining, Extension, Key: String;
  Separator: Integer;
  RootRecord: TArrayOfString;
begin
  SetArrayLength(RootRecord, 1);
  RootRecord[0] := InstallRoot('');
  if not SaveStringsToUTF8File(UninstallDataRoot('') + '\install-root.txt', RootRecord, False) then
    RaiseException('The installation directory record could not be saved.');
  WriteString(AppRoot + '\Integration', 'InstallPath', ExpandConstant('{code:InstallRoot}'));
  RegWriteDWordValue(HKCU, AppRoot + '\Integration', 'Associations', Ord(IsTaskSelected('associations')));
  RegWriteDWordValue(HKCU, AppRoot + '\Integration', 'ContextMenu', Ord(IsTaskSelected('contextmenu')));
  if IsTaskSelected('associations') then begin
    WriteString(ClassesRoot + '\' + ArchiveProgID, '', CustomMessage('ArchiveDescription'));
    WriteString(ClassesRoot + '\' + ArchiveProgID + '\DefaultIcon', '', Executable + ',0');
    WriteString(ClassesRoot + '\' + ArchiveProgID + '\shell\open\command', '', OpenCommand);
    WriteString(ClassesRoot + '\Applications\AnAnArchiver.exe', 'FriendlyAppName', 'AnAnArchiver');
    WriteString(ClassesRoot + '\Applications\AnAnArchiver.exe\shell\open\command', '', OpenCommand);
    WriteString(AppRoot + '\Capabilities', 'ApplicationName', 'AnAnArchiver');
    WriteString(AppRoot + '\Capabilities', 'ApplicationDescription', 'Archive browser, compression and extraction');
    WriteString(RegisteredApplicationsRoot, 'AnAnArchiver', AppRoot + '\Capabilities');
  end;
  Remaining := ArchiveExtensions;
  repeat
    Separator := CharacterPosition(';', Remaining);
    if Separator = 0 then begin Extension := Remaining; Remaining := ''; end
    else begin Extension := Copy(Remaining, 1, Separator - 1); Delete(Remaining, 1, Separator); end;
    if IsTaskSelected('associations') then begin
      { Do not change .ext defaults or Windows UserChoice. Users choose the default in Windows. }
      WriteString(ClassesRoot + '\' + Extension + '\OpenWithProgids', ArchiveProgID, '');
      WriteString(ClassesRoot + '\Applications\AnAnArchiver.exe\SupportedTypes', Extension, '');
      WriteString(AppRoot + '\Capabilities\FileAssociations', Extension, ArchiveProgID);
    end;
    if IsTaskSelected('contextmenu') then begin
      Key := ClassesRoot + '\SystemFileAssociations\' + Extension + '\shell\AnAnArchiver.';
      RegisterMenu(Key + 'Open', CustomMessage('OpenMenu'), 'open');
      RegisterMenu(Key + 'ExtractHere', CustomMessage('ExtractHereMenu'), 'extract-here');
      RegisterMenu(Key + 'ExtractToFolder', CustomMessage('ExtractFolderMenu'), 'extract-to-folder');
    end;
  until Remaining = '';
  if IsTaskSelected('contextmenu') then begin
    RegisterMenu(ClassesRoot + '\*\shell\AnAnArchiver.Compress', CustomMessage('CompressMenu'), 'compress');
    RegisterMenu(ClassesRoot + '\Directory\shell\AnAnArchiver.Compress', CustomMessage('CompressMenu'), 'compress');
  end;
  if IsIntegrationTest then begin
    if not SaveStringToFile(UninstallDataRoot('') + '\.integration-test-namespace', RegistryPrefix, False) then
      RaiseException('The installer test namespace record could not be saved.');
    SaveStringToFile(ExpandConstant('{code:InstallRoot}\.integration-test-namespace'), RegistryPrefix, False);
    Key := RegistryPrefix + '\Uninstall\AnAnArchiver';
    WriteString(Key, 'DisplayName', 'AnAnArchiver');
    WriteString(Key, 'DisplayVersion', '{#AppVersion}');
    WriteString(Key, 'InstallLocation', ExpandConstant('{code:InstallRoot}'));
    WriteString(Key, 'UninstallString', '"' + ExpandConstant('{uninstallexe}') + '"');
  end else begin
    Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{B5695574-44A8-4DAB-AD67-31776F46CC31}_is1';
    WriteString(Key, 'InstallLocation', InstallRoot(''));
    WriteString(Key, 'Inno Setup: App Path', InstallRoot(''));
  end;
end;

function InitializeSetup: Boolean;
begin
  RegistryPrefix;
  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then RemoveObsoleteOwnedFiles;
  if CurStep = ssPostInstall then begin
    { Reconcile previous selections on upgrades, including options explicitly deselected. }
    DeleteOwnedIntegration;
    RegisterIntegration;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then DeleteOwnedIntegration;
end;
