// WinDivert 2.2 的最小动态加载封装：只声明本项目用到的函数与结构。
// 结构布局严格对齐 pydivert 的 windivert_dll/structs.py（bundle 的是 WinDivert-2.2.2）。
// 采用 LoadLibraryW + GetProcAddress 动态加载（复刻 pydivert），无需 .lib 导入库。
#pragma once

// 必须在 <windows.h> 前定义，避免拉入 winsock1（与各 .cpp 里的 winsock2 冲突）。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <string>

#include "log_bus.hpp"

namespace civ6 {

// WINDIVERT_LAYER
enum WinDivertLayer {
    WINDIVERT_LAYER_NETWORK = 0,
    WINDIVERT_LAYER_NETWORK_FORWARD = 1,
    WINDIVERT_LAYER_FLOW = 2,
    WINDIVERT_LAYER_SOCKET = 3,
    WINDIVERT_LAYER_REFLECT = 4,
};

// WINDIVERT_ADDRESS（WinDivert 2.2）。位域顺序与 structs.py 一致。
// 不加 pack：需保留 Timestamp(8)+flags(4) 之后到 8 字节对齐 union 的 4 字节隐式填充，
// 这样 union 从偏移 16 开始，与 pydivert(ctypes 默认对齐) 及原生头一致。
struct WINDIVERT_ADDRESS {
    INT64 Timestamp;
    UINT32 Layer : 8;
    UINT32 Event : 8;
    UINT32 Sniffed : 1;
    UINT32 Outbound : 1;
    UINT32 Loopback : 1;
    UINT32 Impostor : 1;
    UINT32 IPv4 : 1;
    UINT32 IPv6 : 1;
    UINT32 IPChecksum : 1;
    UINT32 TCPChecksum : 1;
    UINT32 UDPChecksum : 1;
    UINT32 Reserved1 : 7;
    union {
        struct {
            UINT32 IfIdx;
            UINT32 SubIfIdx;
        } Network;
        struct {
            UINT64 EndpointId;
            UINT64 ParentEndpointId;
            UINT32 ProcessId;
            UINT32 LocalAddr[4];
            UINT32 RemoteAddr[4];
            UINT16 LocalPort;
            UINT16 RemotePort;
            UINT8 Protocol;
        } Flow;
        struct {
            UINT64 EndpointId;
            UINT64 ParentEndpointId;
            UINT32 ProcessId;
            UINT32 LocalAddr[4];
            UINT32 RemoteAddr[4];
            UINT16 LocalPort;
            UINT16 RemotePort;
            UINT8 Protocol;
        } Socket;
        struct {
            INT64 Timestamp;
            UINT32 ProcessId;
            UINT32 Layer : 8;
            UINT32 Reserved2 : 24;
        } Reflect;
        UINT32 Reserved3[16];
    };
};

#pragma pack(push, 1)
// WinDivert 的 IPv4 头（用于就地改写源/目的地址）。
struct WINDIVERT_IPHDR {
    UINT8 HdrLength : 4;
    UINT8 Version : 4;
    UINT8 TOS;
    UINT16 Length;
    UINT16 Id;
    UINT16 FragOff0;
    UINT8 TTL;
    UINT8 Protocol;
    UINT16 Checksum;
    UINT32 SrcAddr;  // 网络字节序，可直接用 inet_pton 结果赋值
    UINT32 DstAddr;
};
#pragma pack(pop)

// 函数指针类型
using WinDivertOpen_t = HANDLE(WINAPI*)(const char* filter, WinDivertLayer layer,
                                        INT16 priority, UINT64 flags);
using WinDivertRecv_t = BOOL(WINAPI*)(HANDLE handle, void* pPacket, UINT packetLen,
                                      UINT* pRecvLen, WINDIVERT_ADDRESS* pAddr);
using WinDivertSend_t = BOOL(WINAPI*)(HANDLE handle, const void* pPacket, UINT packetLen,
                                      UINT* pSendLen, const WINDIVERT_ADDRESS* pAddr);
using WinDivertClose_t = BOOL(WINAPI*)(HANDLE handle);
using WinDivertShutdown_t = BOOL(WINAPI*)(HANDLE handle, int how);
using WinDivertHelperCalcChecksums_t = BOOL(WINAPI*)(void* pPacket, UINT packetLen,
                                                     WINDIVERT_ADDRESS* pAddr, UINT64 flags);
using WinDivertHelperParsePacket_t = BOOL(WINAPI*)(
    const void* pPacket, UINT packetLen, WINDIVERT_IPHDR** ppIpHdr, void** ppIpv6Hdr,
    UINT8* pProtocol, void** ppIcmpHdr, void** ppIcmpv6Hdr, void** ppTcpHdr, void** ppUdpHdr,
    void** ppData, UINT* pDataLen, void** ppNext, UINT* pNextLen);

// 动态加载器。首次 load() 时把 dll/sys 复制到稳定英文目录并从那里加载。
class WinDivertApi {
public:
    WinDivertOpen_t Open = nullptr;
    WinDivertRecv_t Recv = nullptr;
    WinDivertSend_t Send = nullptr;
    WinDivertClose_t Close = nullptr;
    WinDivertShutdown_t Shutdown = nullptr;
    WinDivertHelperCalcChecksums_t CalcChecksums = nullptr;
    WinDivertHelperParsePacket_t ParsePacket = nullptr;

    // 复制驱动到稳定目录并加载 DLL。失败抛 std::runtime_error（含可读中文原因）。
    void load(const LogFn& log);
    bool loaded() const { return module_ != nullptr; }

private:
    HMODULE module_ = nullptr;
};

// 返回稳定的纯 ASCII 驱动目录：%ProgramData%\civ6proxy\windivert（非 ASCII 则退回 C:\ProgramData）。
std::wstring stable_driver_dir();

// 当前进程是否以管理员运行。
bool is_admin();

}  // namespace civ6
