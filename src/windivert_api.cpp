#include "windivert_api.hpp"

#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace civ6 {

namespace {

std::wstring exe_dir() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    std::size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
}

bool is_ascii(const std::wstring& s) {
    for (wchar_t c : s)
        if (c > 127) return false;
    return true;
}

}  // namespace

void WinDivertApi::load(const LogFn& log) {
    if (module_) return;

    // 驱动随应用安装到安装目录，运行时直接从可执行文件所在目录加载
    // （安装器已把安装路径约束为纯 ASCII，不再复制到 %ProgramData% 中转）。
    std::wstring dir = exe_dir();
    std::wstring dll_path = dir + L"\\WinDivert64.dll";
    module_ = LoadLibraryW(dll_path.c_str());
    if (!module_) {
        DWORD err = GetLastError();
        std::string msg =
            "加载 WinDivert64.dll 失败 (WinError " + std::to_string(err) + ")。\n\n"
            "常见原因：未以管理员身份运行；杀毒软件拦截/删除了 WinDivert 驱动；"
            "或驱动文件缺失（应与本程序放在同一目录）。";
        // 加载失败且自身路径含非 ASCII 字符时，明确提示改用英文路径（不再自动复制到别处）。
        if (!is_ascii(dir)) {
            msg += "\n\n另外，检测到本程序所在路径包含非英文（非 ASCII）字符，"
                   "这可能导致 WinDivert 驱动无法加载。请将本程序安装或移动到"
                   "仅含英文字母、数字的路径（例如 C:\\civ6proxy）后重试。";
        }
        throw std::runtime_error(msg);
    }
    if (log)
        log(LogLevel::Info, "从安装目录加载 WinDivert 驱动: " + fs::path(dir).string());

    auto get = [&](const char* name) -> FARPROC {
        FARPROC p = GetProcAddress(module_, name);
        if (!p) throw std::runtime_error(std::string("WinDivert 缺少导出函数: ") + name);
        return p;
    };

    Open = reinterpret_cast<WinDivertOpen_t>(get("WinDivertOpen"));
    Recv = reinterpret_cast<WinDivertRecv_t>(get("WinDivertRecv"));
    Send = reinterpret_cast<WinDivertSend_t>(get("WinDivertSend"));
    Close = reinterpret_cast<WinDivertClose_t>(get("WinDivertClose"));
    Shutdown = reinterpret_cast<WinDivertShutdown_t>(get("WinDivertShutdown"));
    CalcChecksums = reinterpret_cast<WinDivertHelperCalcChecksums_t>(get("WinDivertHelperCalcChecksums"));
    ParsePacket = reinterpret_cast<WinDivertHelperParsePacket_t>(get("WinDivertHelperParsePacket"));
}

bool is_admin() {
    BOOL admin = FALSE;
    PSID group = nullptr;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) {
        if (!CheckTokenMembership(nullptr, group, &admin)) admin = FALSE;
        FreeSid(group);
    }
    return admin == TRUE;
}

void unregister_windivert_service(const LogFn& log) {
    // 停止并删除 WinDivertOpen 自动注册的 "WinDivert" 内核服务。
    // 任何一步失败都只记日志、不抛异常——残留服务会在下次系统重启时自动清理。
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        if (log) log(LogLevel::Warn, "注销 WinDivert 服务：打开服务管理器失败 (WinError " +
                                         std::to_string(GetLastError()) + ")");
        return;
    }
    SC_HANDLE svc = OpenServiceW(scm, L"WinDivert",
                                 SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (!svc) {
        DWORD err = GetLastError();
        // 服务不存在视为已清理，属正常路径，不记为告警。
        if (err != ERROR_SERVICE_DOES_NOT_EXIST && log)
            log(LogLevel::Warn, "注销 WinDivert 服务：打开服务失败 (WinError " +
                                    std::to_string(err) + ")");
        CloseServiceHandle(scm);
        return;
    }

    SERVICE_STATUS status{};
    if (!ControlService(svc, SERVICE_CONTROL_STOP, &status)) {
        DWORD err = GetLastError();
        // 服务本就未运行 (ERROR_SERVICE_NOT_ACTIVE) 可忽略，继续尝试删除。
        if (err != ERROR_SERVICE_NOT_ACTIVE && log)
            log(LogLevel::Warn, "停止 WinDivert 服务失败 (WinError " + std::to_string(err) +
                                    ")，仍尝试删除服务");
    }

    if (DeleteService(svc)) {
        if (log) log(LogLevel::Info, "WinDivert 内核服务已停止并注销");
    } else {
        DWORD err = GetLastError();
        // 已标记删除 (ERROR_SERVICE_MARKED_FOR_DELETE) 也视为成功路径。
        if (err == ERROR_SERVICE_MARKED_FOR_DELETE) {
            if (log) log(LogLevel::Info, "WinDivert 内核服务已标记为删除");
        } else if (log) {
            log(LogLevel::Warn, "删除 WinDivert 服务失败 (WinError " + std::to_string(err) +
                                    ")，将于下次系统重启由系统自动清理");
        }
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
}

}  // namespace civ6
