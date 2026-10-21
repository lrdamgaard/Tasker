#include "micos_moco_interface_v1.4.h"

#include <algorithm>
#include <vector>

namespace {
std::string ToAscii(const std::wstring& text, bool& valid) {
    std::string result;
    result.reserve(text.size());
    valid = true;
    for (wchar_t ch : text) {
        if (ch < 0x20 || ch > 0x7e) {
            valid = false;
            return {};
        }
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

std::wstring Win32Error(DWORD code) {
    wchar_t* message = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                       FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                                       reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = count && message ? message : L"Unknown Windows serial-port error.";
    if (message) LocalFree(message);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) result.pop_back();
    return result;
}
}

MicosMocoInterface::~MicosMocoInterface() { Close(); }

bool MicosMocoInterface::Open(const std::wstring& portName, DWORD baudRate, std::wstring& error) {
    Close();
    std::wstring deviceName = portName;
    if (deviceName.rfind(L"\\\\.\\", 0) != 0) deviceName = L"\\\\.\\" + deviceName;
    port_ = CreateFileW(deviceName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (port_ == INVALID_HANDLE_VALUE) {
        error = L"Could not open " + portName + L": " + Win32Error(GetLastError());
        return false;
    }

    DCB settings{};
    settings.DCBlength = sizeof(settings);
    if (!GetCommState(port_, &settings)) {
        error = L"Could not read serial-port settings: " + Win32Error(GetLastError());
        Close();
        return false;
    }
    settings.BaudRate = baudRate;
    settings.ByteSize = 8;
    settings.Parity = NOPARITY;
    settings.StopBits = ONESTOPBIT;
    settings.fBinary = TRUE;
    settings.fParity = FALSE;
    settings.fOutxCtsFlow = FALSE;
    settings.fOutxDsrFlow = FALSE;
    settings.fDtrControl = DTR_CONTROL_ENABLE;
    settings.fDsrSensitivity = FALSE;
    settings.fTXContinueOnXoff = TRUE;
    settings.fOutX = FALSE;
    settings.fInX = FALSE;
    settings.fRtsControl = RTS_CONTROL_ENABLE;
    settings.fAbortOnError = FALSE;
    if (!SetCommState(port_, &settings)) {
        error = L"Could not configure the serial port: " + Win32Error(GetLastError());
        Close();
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 1000;
    if (!SetCommTimeouts(port_, &timeouts) || !SetupComm(port_, 4096, 4096) ||
        !PurgeComm(port_, PURGE_RXCLEAR | PURGE_TXCLEAR)) {
        error = L"Could not initialize serial communication: " + Win32Error(GetLastError());
        Close();
        return false;
    }
    error.clear();
    return true;
}

void MicosMocoInterface::Close() {
    if (port_ != INVALID_HANDLE_VALUE) {
        CloseHandle(port_);
        port_ = INVALID_HANDLE_VALUE;
    }
}

bool MicosMocoInterface::IsOpen() const { return port_ != INVALID_HANDLE_VALUE; }

bool MicosMocoInterface::SendAsciiCommand(const std::wstring& command, std::wstring& error) {
    if (!IsOpen()) { error = L"The MoCo serial port is not connected."; return false; }
    bool valid = false;
    std::string bytes = ToAscii(command, valid);
    if (!valid || bytes.empty()) { error = L"Enter a non-empty ASCII command using printable characters."; return false; }
    bytes.push_back('\r');
    return WriteBytes(bytes.data(), static_cast<DWORD>(bytes.size()), error);
}

bool MicosMocoInterface::SelectBoardAddress(char address, std::wstring& error) {
    const bool valid = (address >= '0' && address <= '9') || (address >= 'A' && address <= 'F');
    if (!IsOpen()) { error = L"The MoCo serial port is not connected."; return false; }
    if (!valid) { error = L"Choose a MoCo board address from 0 through F."; return false; }
    const char bytes[] = { '\x01', address };
    return WriteBytes(bytes, static_cast<DWORD>(sizeof(bytes)), error);
}

bool MicosMocoInterface::SendSubcommand(char command, std::wstring& error) {
    if (!IsOpen()) { error = L"The MoCo serial port is not connected."; return false; }
    if (command < 0x20 || command > 0x7e) { error = L"The MoCo subcommand must be one printable ASCII character."; return false; }
    return WriteBytes(&command, 1, error);
}

bool MicosMocoInterface::WriteBytes(const char* bytes, DWORD count, std::wstring& error) {
    DWORD written = 0;
    if (!WriteFile(port_, bytes, count, &written, nullptr) || written != count) {
        error = L"Could not send the command: " + Win32Error(GetLastError());
        return false;
    }
    error.clear();
    return true;
}

bool MicosMocoInterface::ReadAvailable(std::wstring& text, std::wstring& error) {
    text.clear();
    if (!IsOpen()) { error = L"The MoCo serial port is not connected."; return false; }
    COMSTAT status{};
    DWORD errors = 0;
    if (!ClearCommError(port_, &errors, &status)) {
        error = L"Could not query serial input: " + Win32Error(GetLastError());
        return false;
    }
    if (!status.cbInQue) { error.clear(); return true; }
    const DWORD amount = (std::min)(status.cbInQue, 4096UL);
    std::vector<char> bytes(amount);
    DWORD received = 0;
    if (!ReadFile(port_, bytes.data(), amount, &received, nullptr)) {
        error = L"Could not read the serial response: " + Win32Error(GetLastError());
        return false;
    }
    text.reserve(received);
    for (DWORD i = 0; i < received; ++i) {
        const unsigned char ch = static_cast<unsigned char>(bytes[i]);
        text.push_back(ch == '\r' ? L'\n' : ch >= 0x20 || ch == '\n' || ch == '\t' ? static_cast<wchar_t>(ch) : L'.');
    }
    error.clear();
    return true;
}
