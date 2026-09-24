; 文明6 虚拟组网联机代理 —— Inno Setup 安装脚本
;
; 生成安装包（需先安装 Inno Setup 6）：
;   ISCC /DAppSourceDir=<Release 构建输出目录> /DWinDivertDir=<驱动目录> /DAppVersion=<版本> installer\civ6proxy.iss
; 或经由 CMake 一键生成：
;   cmake --build build --config Release --target installer
;
; 注意：本文件为 UTF-8（含 BOM），以保证中文提示在 Inno Setup 中正确显示。

#ifndef AppVersion
  #define AppVersion "1.2"
#endif
; 主程序与运行时 DLL（civ6proxy.exe / slint_cpp.dll）所在目录，默认取仓库内 Release 构建输出。
#ifndef AppSourceDir
  #define AppSourceDir "..\build\Release"
#endif
; WinDivert 驱动文件目录（含 WinDivert64.dll / WinDivert64.sys）。
#ifndef WinDivertDir
  #define WinDivertDir "..\third_party\windivert"
#endif

#define AppName "文明6 虚拟组网联机代理"
#define AppExeName "civ6proxy.exe"
#define AppPublisher "civ6proxy"

[Setup]
AppId={{9F5C9E2A-3D5B-4C7E-9B1A-CF6A0D2E7B10}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
; 安装到 64 位 Program Files（默认目录为纯 ASCII）。
DefaultDirName={autopf}\civ6proxy
DefaultGroupName=civ6proxy
; 安装需要管理员：写入 Program Files；且主程序本身要求以管理员运行。
PrivilegesRequired=admin
; WinDivert64 为 64 位原生内核驱动（不能在 ARM64 模拟下加载），仅在原生 64 位 Windows 上安装。
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
OutputDir=..\dist
OutputBaseFilename=civ6proxy-setup-{#AppVersion}
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
; 安装程序自身（Setup.exe）及“程序和功能”里的图标（与主程序同一图标）。
SetupIconFile=..\res\app_icon.ico
; 保留“选择安装目录”页（用于纯 ASCII 路径校验），不禁用该页。

[Languages]
; 使用仓库内自带的简体中文语言文件（Inno Setup 官方发行版默认不含），使安装包在任意机器上均可编译。
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#AppSourceDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppSourceDir}\slint_cpp.dll"; DestDir: "{app}"; Flags: ignoreversion
; 驱动文件随应用安装到安装目录，运行时直接从此加载（不再复制到 %ProgramData%）。
Source: "{#WinDivertDir}\WinDivert64.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WinDivertDir}\WinDivert64.sys"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
; runascurrentuser：主程序清单为 requireAdministrator，而 postinstall 默认以“原始（未提权）”
; 身份启动，会导致 CreateProcess 失败（错误 740）。加此标志让其继承安装程序已提权的令牌。
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent runascurrentuser

[Code]
{ 校验安装目标目录为纯 ASCII：含非 ASCII（如中文）字符会导致 WinDivert 驱动加载失败，拒绝继续。 }
function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir: String;
  I: Integer;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    Dir := WizardDirValue();
    for I := 1 to Length(Dir) do
    begin
      if Ord(Dir[I]) > 127 then
      begin
        MsgBox('安装路径包含非英文（非 ASCII）字符：' + #13#10 +
               Dir + #13#10#13#10 +
               '请改用仅含英文字母、数字的路径（例如 C:\civ6proxy），' + #13#10 +
               '否则 WinDivert 驱动可能无法加载。', mbError, MB_OK);
        Result := False;
        Exit;
      end;
    end;
  end;
end;

{ 卸载清理：停止并删除 WinDivert 内核服务，再删除驱动文件；被占用则安排下次重启删除。 }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
  ScExe: String;
begin
  if CurUninstallStep = usUninstall then
  begin
    ScExe := ExpandConstant('{sys}\sc.exe');
    { 忽略返回码：服务可能本就不存在或已停止。 }
    Exec(ScExe, 'stop WinDivert', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Exec(ScExe, 'delete WinDivert', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    { 删除驱动文件；若仍被内核占用，安排下次系统重启时删除。 }
    if FileExists(ExpandConstant('{app}\WinDivert64.sys')) then
      if not DeleteFile(ExpandConstant('{app}\WinDivert64.sys')) then
        RestartReplace(ExpandConstant('{app}\WinDivert64.sys'), '');
    if FileExists(ExpandConstant('{app}\WinDivert64.dll')) then
      if not DeleteFile(ExpandConstant('{app}\WinDivert64.dll')) then
        RestartReplace(ExpandConstant('{app}\WinDivert64.dll'), '');
  end;
end;
