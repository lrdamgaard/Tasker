#pragma once

#include <windows.h>
#include <string>

// Low-level serial transport for MICOS MoCo ASCII command interfaces.
// The caller supplies the controller-specific command text.
class MicosMocoInterface {
public:
    MicosMocoInterface() = default;
    ~MicosMocoInterface();
    MicosMocoInterface(const MicosMocoInterface&) = delete;
    MicosMocoInterface& operator=(const MicosMocoInterface&) = delete;

    bool Open(const std::wstring& portName, DWORD baudRate, std::wstring& error);
    void Close();
    bool IsOpen() const;
    bool SendAsciiCommand(const std::wstring& command, std::wstring& error);
    bool ReadAvailable(std::wstring& text, std::wstring& error);

private:
    HANDLE port_ = INVALID_HANDLE_VALUE;
};
