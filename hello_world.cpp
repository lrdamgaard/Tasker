#define UNICODE
#define _UNICODE
#include <windows.h>

namespace {
constexpr int GoButtonId = 1001;

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateWindowW(L"BUTTON", L"Go", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      90, 55, 120, 40, window,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(GoButtonId)),
                      GetModuleHandleW(nullptr), nullptr);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == GoButtonId) {
            MessageBoxW(window, L"Hello world", L"Message", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    constexpr wchar_t ClassName[] = L"HelloWorldWindowClass";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = ClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&windowClass)) {
        MessageBoxW(nullptr, L"Could not register the app window.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND window = CreateWindowExW(0, ClassName, L"Hello World App",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 320, 180,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) {
        MessageBoxW(nullptr, L"Could not create the app window.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
