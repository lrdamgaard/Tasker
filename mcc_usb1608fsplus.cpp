#include "mcc_usb1608fsplus.h"
#include <cstring>

namespace {
constexpr int Bip10Volts = 1; // UL range enum: BIP10VOLTS, +/-10 V
constexpr int UsbInterface = 1;
constexpr unsigned int Usb1608FsId = 125;
constexpr unsigned int Usb1608FsPlusId = 234;

std::wstring ErrorText(const wchar_t* prefix, int code) {
    return std::wstring(prefix) + L" (MCC Universal Library error " + std::to_wstring(code) + L").";
}
bool IsSupportedBoardName(const char* name) {
    return std::strstr(name, "USB-1608FS-Plus") || std::strstr(name, "USB-1608FS Plus") ||
           std::strcmp(name, "USB-1608FS") == 0;
}
}

MccUsb1608FsPlus::~MccUsb1608FsPlus() { Disconnect(); }

bool MccUsb1608FsPlus::Connect(std::wstring& status) {
    Disconnect();
    library_ = LoadLibraryW(L"cbw64.dll");
    if (!library_) library_ = LoadLibraryW(L"cbw32.dll");
    if (!library_) {
        wchar_t programFilesX86[MAX_PATH]{};
        DWORD length = GetEnvironmentVariableW(L"ProgramFiles(x86)", programFilesX86, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            std::wstring mccPath(programFilesX86);
            mccPath += L"\\Measurement Computing\\DAQ\\cbw64.dll";
            library_ = LoadLibraryW(mccPath.c_str());
            if (!library_) {
                mccPath = programFilesX86;
                mccPath += L"\\Measurement Computing\\DAQ\\cbw32.dll";
                library_ = LoadLibraryW(mccPath.c_str());
            }
        }
    }
    if (!library_) {
        status = L"MCC Universal Library not found. Install MCC DAQ Software.";
        return false;
    }
    getBoardName_ = reinterpret_cast<GetBoardNameFn>(GetProcAddress(library_, "cbGetBoardName"));
    aIn_ = reinterpret_cast<AInFn>(GetProcAddress(library_, "cbAIn"));
    toEngUnits_ = reinterpret_cast<ToEngUnitsFn>(GetProcAddress(library_, "cbToEngUnits"));
    getInventory_ = reinterpret_cast<GetInventoryFn>(GetProcAddress(library_, "cbGetDaqDeviceInventory"));
    createDevice_ = reinterpret_cast<CreateDeviceFn>(GetProcAddress(library_, "cbCreateDaqDevice"));
    releaseDevice_ = reinterpret_cast<ReleaseDeviceFn>(GetProcAddress(library_, "cbReleaseDaqDevice"));
    if (!getBoardName_ || !aIn_ || !toEngUnits_) {
        status = L"MCC Universal Library is missing required analog-input functions.";
        Disconnect();
        return false;
    }

    // Prefer a board configured in InstaCal.
    for (int board = 0; board < 100; ++board) {
        char name[64]{};
        if (getBoardName_(board, name) == 0 && IsSupportedBoardName(name)) {
            boardNumber_ = board;
            status = L"Connected to " + std::wstring(name, name + std::strlen(name)) +
                     L" (InstaCal board " + std::to_wstring(board) + L"). Range: +/-10 V.";
            return true;
        }
    }

    // If InstaCal has no matching board, discover the connected USB unit and add it for this run.
    if (!getInventory_ || !createDevice_ || !releaseDevice_) {
        status = L"No configured USB-1608FS board found, and MCC device-discovery functions are unavailable.";
        return false;
    }
    DeviceDescriptor devices[32]{};
    int count = 32;
    int result = getInventory_(UsbInterface, devices, &count);
    if (result != 0) {
        status = ErrorText(L"MCC USB device discovery failed", result);
        return false;
    }
    int deviceIndex = -1;
    for (int i = 0; i < count && i < 32; ++i) {
        if (devices[i].productId == Usb1608FsId || devices[i].productId == Usb1608FsPlusId) {
            deviceIndex = i;
            break;
        }
    }
    if (deviceIndex < 0) {
        status = L"No USB-1608FS or USB-1608FS-Plus was detected. Check the USB connection and MCC driver.";
        return false;
    }
    for (int board = 0; board < 100; ++board) {
        char name[64]{};
        if (getBoardName_(board, name) != 0 || name[0] == '\0') {
            result = createDevice_(board, devices[deviceIndex]);
            if (result == 0) {
                boardNumber_ = board;
                createdByApi_ = true;
                const wchar_t* detectedName = devices[deviceIndex].productId == Usb1608FsPlusId
                    ? L"USB-1608FS-Plus" : L"USB-1608FS";
                status = L"Connected to detected " + std::wstring(detectedName) +
                         L" (board " + std::to_wstring(board) + L"). Range: +/-10 V.";
                return true;
            }
            status = ErrorText(L"MCC detected the device, but could not initialize it", result);
            return false;
        }
    }
    status = L"No free MCC board number is available for the detected USB-1608FS.";
    return false;
}

bool MccUsb1608FsPlus::ReadAll(std::array<float, 8>& volts, std::wstring& error) {
    if (boardNumber_ < 0 || !aIn_ || !toEngUnits_) {
        error = L"No USB-1608FS device is connected.";
        return false;
    }
    for (int channel = 0; channel < 8; ++channel) {
        unsigned short raw = 0;
        int result = aIn_(boardNumber_, channel, Bip10Volts, &raw);
        if (result != 0) {
            error = ErrorText((L"Reading analog input channel " + std::to_wstring(channel)).c_str(), result);
            return false;
        }
        float value = 0.0f;
        result = toEngUnits_(boardNumber_, Bip10Volts, raw, &value);
        if (result != 0) {
            error = ErrorText((L"Converting channel " + std::to_wstring(channel) + L" to volts").c_str(), result);
            return false;
        }
        volts[static_cast<size_t>(channel)] = value;
    }
    return true;
}

void MccUsb1608FsPlus::Disconnect() {
    if (createdByApi_ && boardNumber_ >= 0 && releaseDevice_) releaseDevice_(boardNumber_);
    boardNumber_ = -1;
    createdByApi_ = false;
    getBoardName_ = nullptr; aIn_ = nullptr; toEngUnits_ = nullptr;
    getInventory_ = nullptr; createDevice_ = nullptr; releaseDevice_ = nullptr;
    if (library_) { FreeLibrary(library_); library_ = nullptr; }
}
