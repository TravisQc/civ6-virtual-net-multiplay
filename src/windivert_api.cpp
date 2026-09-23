#include "windivert_api.hpp"

#include <shlobj.h>

#include <cstdlib>
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

std::wstring stable_driver_dir() {
    // %ProgramData%；非 ASCII 则退回 C:\ProgramData（对齐 _stable_driver_dir）。
    std::wstring base;
    wchar_t* env = nullptr;
    std::size_t len = 0;
    if (_wdupenv_s(&env, &len, L"ProgramData") == 0 && env) {
        base = env;
        free(env);
    }
    if (base.empty() || !is_ascii(base)) base = L"C:\\ProgramData";
    return base + L"\\civ6proxy\\windivert";
}

void WinDivertApi::load(const LogFn& log) {
    if (module_) return;

    std::wstring dst = stable_driver_dir();
    std::wstring dll_path = dst + L"\\WinDivert64.dll";
    try {
        fs::create_directories(dst);
        // 从 exe 同目录复制 dll/sys 到稳定英文目录（若尚不存在）。
        std::wstring src = exe_dir();
        for (const wchar_t* fn : {L"WinDivert64.dll", L"WinDivert64.sys"}) {
            fs::path s = fs::path(src) / fn;
            fs::path d = fs::path(dst) / fn;
            if (fs::exists(s) && !fs::exists(d))
                fs::copy_file(s, d);
        }
        if (log) log(LogLevel::Info, "WinDivert 驱动目录: " +
                                         std::filesystem::path(dst).string());
    } catch (const std::exception& e) {
        if (log) log(LogLevel::Warn,
                     std::string("复制 WinDivert 驱动到稳定目录失败，将回退默认路径: ") + e.what());
    }

    // 从稳定目录加载；失败则回退 exe 同目录。
    module_ = LoadLibraryW(dll_path.c_str());
    if (!module_) {
        std::wstring fallback = exe_dir() + L"\\WinDivert64.dll";
        module_ = LoadLibraryW(fallback.c_str());
    }
    if (!module_) {
        DWORD err = GetLastError();
        throw std::runtime_error(
            "加载 WinDivert64.dll 失败 (WinError " + std::to_string(err) + ")。\n\n"
            "常见原因：未以管理员身份运行；杀毒软件拦截/删除了 WinDivert 驱动；"
            "或驱动文件缺失（应与本程序放在同一目录）。");
    }

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

}  // namespace civ6
