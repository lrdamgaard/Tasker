#pragma once
#include <windows.h>
#include <array>
#include <string>

// Adapter for Measurement Computing's Universal Library (UL).
// Supports either the USB-1608FS or USB-1608FS-Plus model.
class MccUsb1608FsPlus {
public:
    MccUsb1608FsPlus() = default;
    ~MccUsb1608FsPlus();
    MccUsb1608FsPlus(const MccUsb1608FsPlus&) = delete;
    MccUsb1608FsPlus& operator=(const MccUsb1608FsPlus&) = delete;

    bool Connect(std::wstring& status);
    bool ReadAll(std::array<float, 8>& volts, std::wstring& error,
                 const std::array<bool, 8>& enabled, const std::array<int, 8>& ranges);
    void Disconnect();

private:
    struct DeviceDescriptor {
        char productName[64];
        unsigned int productId;
        int interfaceType;
        char deviceString[64];
        char uniqueId[64];
        unsigned long long numericUniqueId;
        char reserved[512];
    };
    using GetBoardNameFn = int (WINAPI*)(int, char*);
    using AInFn = int (WINAPI*)(int, int, int, unsigned short*);
    using ToEngUnitsFn = int (WINAPI*)(int, int, unsigned short, float*);
    using GetInventoryFn = int (WINAPI*)(int, DeviceDescriptor*, int*);
    using CreateDeviceFn = int (WINAPI*)(int, DeviceDescriptor);
    using ReleaseDeviceFn = int (WINAPI*)(int);

    HMODULE library_ = nullptr;
    GetBoardNameFn getBoardName_ = nullptr;
    AInFn aIn_ = nullptr;
    ToEngUnitsFn toEngUnits_ = nullptr;
    GetInventoryFn getInventory_ = nullptr;
    CreateDeviceFn createDevice_ = nullptr;
    ReleaseDeviceFn releaseDevice_ = nullptr;
    int boardNumber_ = -1;
    bool createdByApi_ = false;
};
