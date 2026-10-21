#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include "mcc_usb1608fsplus.h"
#include "micos_moco_interface_v1.3.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")

namespace {
enum ControlId { TabsId=100, InfoId, ChartId, StartId, StopId, FrequencyId, FrequencyLabelId, FileButtonId, DisplayLengthId, DisplayLengthLabelId, CommentButtonId, CommentShortcutId,
                 MocoHeadingId, MocoPortLabelId, MocoPortId, MocoBaudLabelId, MocoBaudId, MocoConnectId, MocoDisconnectId, MocoStatusId, MocoCommandLabelId, MocoCommandId, MocoSendId, MocoConsoleLabelId, MocoConsoleId,
                 ProfilingStartLabelId, ProfilingStartInputId, StatusBarId, GraphHelpId };
constexpr UINT_PTR UiTimer = 1;
constexpr UINT RecordingFinishedMessage = WM_APP + 2;
constexpr UINT RecordingStartedMessage = WM_APP + 3;
constexpr UINT CommentMarkersChangedMessage = WM_APP + 4;
constexpr UINT AcquisitionStoppedMessage = WM_APP + 5;
constexpr size_t MaxGraphSamples = 120000;
constexpr wchar_t WindowClassName[] = L"TaskerWindowClass";

HWND tabs, infoLabel, startButton, stopButton, frequencyInput, frequencyLabel, fileButton, displayLengthInput, displayLengthLabel, commentButton, statusBar, graphHelpButton, graphToolTip;
HWND mocoHeading, mocoPortLabel, mocoPortInput, mocoBaudLabel, mocoBaudInput, mocoConnectButton, mocoDisconnectButton, mocoStatusLabel, mocoCommandLabel, mocoCommandInput, mocoSendButton, mocoConsoleLabel, mocoConsole;
HWND profilingStartLabel, profilingStartInput;
int activeTab = 0;
std::wstring dataFilePath;
std::filesystem::path logFilePath;
std::mutex logMutex;
std::thread acquisitionThread;
std::atomic<bool> shutdownRequested{false};
std::atomic<bool> acquisitionAlive{false};
std::atomic<bool> startRecordingRequested{false};
std::atomic<bool> stopRecordingRequested{false};
std::atomic<bool> recording{false};
std::mutex dataMutex, commandMutex;
std::wstring pendingFilePath;
std::atomic<unsigned long long> dataFileGeneration{0};
unsigned long long pendingFileGeneration = 0;
double pendingIntervalSeconds = 1.0;
struct CommentRequest { std::chrono::system_clock::time_point timestamp; std::wstring text; };
std::mutex commentMutex;
std::deque<CommentRequest> commentRequests;
std::array<float, 8> latest{};
struct SignalSample { double seconds; unsigned long long sequence; long long wallMilliseconds; int localDateKey; std::array<float,8> volts; };
struct GraphComment { double seconds; long long wallMilliseconds; std::wstring text; };
std::deque<SignalSample> history;
std::vector<GraphComment> graphComments;
unsigned long long nextSampleSequence = 0;
std::atomic<double> displayLengthSeconds{10.0};
bool graphFollowingLive = true;
bool closeAfterAcquisition = false;
bool graphPanning = false;
bool trackingMouseLeave = false;
bool hoveringGraphComment = false;
POINT hoveredCommentAnchor{};
RECT hoveredCommentTooltipRect{};
std::wstring hoveredCommentText;
bool displayLengthSync = false;
int graphPanLastX = 0, graphPanLastY = 0;
double graphViewEndSeconds = 0.0;
struct AxisDateSpan { int dateKey; int firstX; int lastX; };
struct GraphCache {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    int width = 0, height = 0;
    bool valid = false;
    double viewSeconds = 0.0, viewEnd = 0.0, minV = -0.01, maxV = 0.01;
    double requestedMinV = -0.01, requestedMaxV = 0.01;
    bool manualYScale = false;
    unsigned long long lastSequence = 0;
    long long wallEndMilliseconds = 0;
    std::array<POINT,8> lastPoints{};
    std::array<bool,8> hasLastPoint{};
    std::vector<AxisDateSpan> dateSpans;
} graphCache;
MccUsb1608FsPlus daq;
MicosMocoInterface moco;
const wchar_t* tabNames[] = { L"Signals", L"Profiling", L"Upcoming", L"Completed", L"Settings" };
const COLORREF colors[] = { RGB(25,116,210), RGB(226,94,62), RGB(42,155,99), RGB(160,92,190), RGB(226,167,42), RGB(43,163,174), RGB(212,89,142), RGB(104,118,139) };
void ClearCommentHover(HWND window);

std::wstring FormatDataFileSize() {
    if (dataFilePath.empty()) return L"Size on disk: —";
    std::error_code error;
    const auto bytes = std::filesystem::file_size(std::filesystem::path(dataFilePath), error);
    if (error) return L"Size on disk: unavailable";

    static constexpr const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double size = static_cast<double>(bytes);
    size_t unit = 0;
    while (size >= 1024.0 && unit + 1 < std::size(units)) {
        size /= 1024.0;
        ++unit;
    }
    wchar_t text[96]{};
    swprintf_s(text, L"Size on disk: %.1f %ls", size, units[unit]);
    return text;
}

void UpdateDataFileSize() {
    if (!statusBar) return;
    const wchar_t* pathText = dataFilePath.empty() ? L"" : dataFilePath.c_str();
    SendMessageW(statusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(pathText));
    const std::wstring text = FormatDataFileSize();
    SendMessageW(statusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(text.c_str()));
}

void LayoutStatusBar(HWND window) {
    if (!statusBar) return;
    SendMessageW(statusBar, WM_SIZE, 0, 0);
    RECT client{};
    GetClientRect(window, &client);
    int parts[] = { (std::max)(0L, client.right - 250L), -1 };
    SendMessageW(statusBar, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));
    UpdateDataFileSize();
}

void CreateGraphHelpTooltip(HWND window) {
    graphToolTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX | TTS_BALLOON,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        window, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!graphToolTip || !graphHelpButton) return;
    static wchar_t instructions[] =
        L"Mouse wheel: zoom the Y-axis around the cursor.\n"
        L"Ctrl + mouse wheel: zoom the time axis.\n"
        L"Right-click and drag: pan the graph horizontally and vertically.\n"
        L"Double-click the graph: return to the live view.\n"
        L"Display length (s): set the time span shown.";
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = window;
    tool.uId = reinterpret_cast<UINT_PTR>(graphHelpButton);
    tool.lpszText = instructions;
    SendMessageW(graphToolTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
}

std::wstring LocalTimestamp(const wchar_t* format) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &seconds);
    std::wostringstream text;
    text << std::put_time(&local, format);
    return text.str();
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

bool InitializeLogFile(const std::wstring& storagePath) {
    const std::filesystem::path dataPath(storagePath);
    const std::wstring logName = dataPath.stem().wstring() + L"_" + LocalTimestamp(L"%Y-%m-%d_%H.%M.%S") + L".log";
    const std::filesystem::path candidate = dataPath.parent_path() / logName;
    std::lock_guard<std::mutex> lock(logMutex);
    logFilePath.clear();
    std::ofstream file(candidate, std::ios::out | std::ios::trunc);
    if (!file) return false;
    logFilePath = candidate;
    file << WideToUtf8(LocalTimestamp(L"%Y-%m-%d %H:%M:%S")) << " Log started\n"
         << "Log file: " << WideToUtf8(logFilePath.wstring()) << "\n"
         << "Data file: " << WideToUtf8(storagePath) << "\n";
    file.flush();
    return static_cast<bool>(file);
}

void WriteLog(const std::wstring& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFilePath.empty()) return;
    std::ofstream file(logFilePath, std::ios::out | std::ios::app);
    if (!file) return;
    file << WideToUtf8(LocalTimestamp(L"%Y-%m-%d %H:%M:%S")) << "  " << WideToUtf8(message) << '\n';
    file.flush();
}

void NotifyStatus(HWND, const std::wstring& status) {
    WriteLog(status);
}

std::wstring BaseName(const std::wstring& path) {
    return std::filesystem::path(path).filename().wstring();
}

std::wstring FormatCommentTime(std::chrono::system_clock::time_point timestamp) {
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch());
    const std::time_t seconds = std::chrono::system_clock::to_time_t(timestamp);
    std::tm local{};
    localtime_s(&local, &seconds);
    std::wostringstream text;
    text << std::put_time(&local, L"%Y-%m-%d %H:%M:%S") << L'.'
         << std::setw(3) << std::setfill(L'0') << (milliseconds.count() % 1000);
    return text.str();
}

bool ParseCommentTime(const std::wstring& text, std::chrono::system_clock::time_point& result) {
    std::wistringstream input(text);
    std::tm local{};
    input >> std::get_time(&local, L"%Y-%m-%d %H:%M:%S");
    if (input.fail()) return false;
    int milliseconds = 0;
    if (input.peek() == L'.') {
        input.get();
        input >> milliseconds;
        if (input.fail() || milliseconds < 0 || milliseconds > 999) return false;
    }
    input >> std::ws;
    if (!input.eof()) return false;
    const int year = local.tm_year, month = local.tm_mon, day = local.tm_mday;
    const int hour = local.tm_hour, minute = local.tm_min, second = local.tm_sec;
    local.tm_isdst = -1;
    const std::time_t seconds = std::mktime(&local);
    if (seconds == static_cast<std::time_t>(-1)) return false;
    std::tm normalized{};
    localtime_s(&normalized, &seconds);
    if (normalized.tm_year != year || normalized.tm_mon != month || normalized.tm_mday != day ||
        normalized.tm_hour != hour || normalized.tm_min != minute || normalized.tm_sec != second) return false;
    result = std::chrono::system_clock::from_time_t(seconds) + std::chrono::milliseconds(milliseconds);
    return true;
}

struct CommentDialogState {
    std::chrono::system_clock::time_point initialTime;
    std::chrono::system_clock::time_point commentTime;
    std::wstring comment;
    bool accepted = false;
};

constexpr int CommentTimeEditId = 5101;
constexpr int CommentTextEditId = 5102;
constexpr int CommentUpdateTimeId = 5103;

LRESULT CALLBACK CommentTimeEditProc(HWND edit, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_KEYDOWN && (wParam == VK_UP || wParam == VK_DOWN)) {
        wchar_t text[96]{};
        GetWindowTextW(edit, text, static_cast<int>(std::size(text)));
        std::chrono::system_clock::time_point value;
        if (ParseCommentTime(text, value)) {
            value += (wParam == VK_UP) ? std::chrono::seconds(1) : -std::chrono::seconds(1);
            SetWindowTextW(edit, FormatCommentTime(value).c_str());
            SendMessageW(edit, EM_SETSEL, 0, -1);
            return 0;
        }
    }
    return DefSubclassProc(edit, message, wParam, lParam);
}

INT_PTR CALLBACK CommentDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto* state = reinterpret_cast<CommentDialogState*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        CreateWindowW(L"STATIC", L"Time (local: YYYY-MM-DD HH:MM:SS.mmm)", WS_CHILD | WS_VISIBLE, 16, 12, 350, 22, dialog, nullptr, GetModuleHandleW(nullptr), nullptr);
        HWND timeEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", FormatCommentTime(state->initialTime).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 16, 36, 340, 30, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CommentTimeEditId)), GetModuleHandleW(nullptr), nullptr);
        SetWindowSubclass(timeEdit, CommentTimeEditProc, 1, 0);
        CreateWindowW(L"BUTTON", L"Update time", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 368, 36, 112, 30, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CommentUpdateTimeId)), GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"STATIC", L"Comment", WS_CHILD | WS_VISIBLE, 16, 78, 120, 22, dialog, nullptr, GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 16, 102, 464, 122, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CommentTextEditId)), GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 296, 240, 84, 32, dialog, reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 392, 240, 88, 32, dialog, reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(dialog, DM_SETDEFID, IDOK, 0);
        SetFocus(GetDlgItem(dialog, CommentTextEditId));
        return FALSE;
    }
    if (message == WM_COMMAND) {
        auto* state = reinterpret_cast<CommentDialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (LOWORD(wParam) == CommentUpdateTimeId) {
            SetWindowTextW(GetDlgItem(dialog, CommentTimeEditId), FormatCommentTime(std::chrono::system_clock::now()).c_str());
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK && state) {
            wchar_t timeText[96]{};
            GetWindowTextW(GetDlgItem(dialog, CommentTimeEditId), timeText, static_cast<int>(std::size(timeText)));
            if (!ParseCommentTime(timeText, state->commentTime)) {
                MessageBoxW(dialog, L"Enter a valid local time in YYYY-MM-DD HH:MM:SS.mmm format.", L"Comment", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(dialog, CommentTimeEditId));
                return TRUE;
            }
            const int length = GetWindowTextLengthW(GetDlgItem(dialog, CommentTextEditId));
            if (length == 0) {
                MessageBoxW(dialog, L"Enter a comment before pressing OK.", L"Comment", MB_OK | MB_ICONINFORMATION);
                SetFocus(GetDlgItem(dialog, CommentTextEditId));
                return TRUE;
            }
            std::vector<wchar_t> commentText(static_cast<size_t>(length) + 1, L'\0');
            if (length) GetWindowTextW(GetDlgItem(dialog, CommentTextEditId), commentText.data(), length + 1);
            state->comment.assign(commentText.data(), static_cast<size_t>(length));
            state->accepted = true;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}

void QueueComment(std::chrono::system_clock::time_point timestamp, std::wstring comment) {
    std::lock_guard<std::mutex> lock(commentMutex);
    commentRequests.push_back({timestamp, std::move(comment)});
}

void ShowCommentDialog(HWND owner) {
    if (!recording.load()) {
        MessageBoxW(owner, L"Start recording before adding a comment.", L"Comment", MB_OK | MB_ICONINFORMATION);
        return;
    }
    CommentDialogState state{std::chrono::system_clock::now()};
    std::vector<BYTE> bytes(sizeof(DLGTEMPLATE));
    auto* templateHeader = reinterpret_cast<DLGTEMPLATE*>(bytes.data());
    templateHeader->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | DS_SETFONT;
    templateHeader->dwExtendedStyle = 0;
    templateHeader->cdit = 0;
    templateHeader->x = 10; templateHeader->y = 10; templateHeader->cx = 320; templateHeader->cy = 205;
    auto appendWord = [&bytes](WORD word) { const size_t at = bytes.size(); bytes.resize(at + sizeof(word)); std::memcpy(bytes.data() + at, &word, sizeof(word)); };
    auto appendString = [&appendWord](const wchar_t* value) { while (*value) appendWord(static_cast<WORD>(*value++)); appendWord(0); };
    appendWord(0); appendWord(0); appendString(L"Comment"); appendWord(9); appendString(L"Segoe UI");
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<DLGTEMPLATE*>(bytes.data()), owner, CommentDialogProc, reinterpret_cast<LPARAM>(&state));
    if (state.accepted && recording.load()) {
        QueueComment(state.commentTime, std::move(state.comment));
        WriteLog(L"Comment entered for " + FormatCommentTime(state.commentTime));
    } else if (state.accepted) {
        WriteLog(L"Comment was not stored because recording stopped before it could be queued.");
        MessageBoxW(owner, L"Recording has stopped; the comment was not stored.", L"Comment", MB_OK | MB_ICONWARNING);
    }
}

bool ChooseDataFile(HWND owner) {
    wchar_t path[MAX_PATH]{};
    if (!dataFilePath.empty()) wcsncpy_s(path, dataFilePath.c_str(), _TRUNCATE);
    const wchar_t filter[] = L"Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"txt";
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&dialog)) return false;
    std::ofstream resetFile(std::filesystem::path(path), std::ios::out | std::ios::trunc);
    if (!resetFile) {
        MessageBoxW(owner, L"Tasker could not create the selected storage file.", L"Tasker", MB_OK | MB_ICONERROR);
        return false;
    }
    resetFile.close();
    dataFilePath = path;
    if (!InitializeLogFile(dataFilePath)) {
        MessageBoxW(owner, L"Tasker could not create the log file beside the selected storage file.", L"Tasker", MB_OK | MB_ICONERROR);
        dataFilePath.clear();
        return false;
    }
    dataFileGeneration.fetch_add(1);
    UpdateDataFileSize();
    WriteLog(L"Selected storage file: " + dataFilePath);
    return true;
}

void AppendMocoConsole(const std::wstring& text) {
    if (!mocoConsole || text.empty()) return;
    const int length = GetWindowTextLengthW(mocoConsole);
    if (length > 30000) SetWindowTextW(mocoConsole, L"[Earlier controller output cleared]\r\n");
    SendMessageW(mocoConsole, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(mocoConsole, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(mocoConsole, EM_SCROLLCARET, 0, 0);
}

void UpdateMocoConnectionControls(bool connected) {
    EnableWindow(mocoPortInput, !connected);
    EnableWindow(mocoBaudInput, !connected);
    EnableWindow(mocoConnectButton, !connected);
    EnableWindow(mocoDisconnectButton, connected);
    EnableWindow(mocoCommandInput, connected);
    EnableWindow(mocoSendButton, connected);
}

void SetView(HWND window, int tab) {
    ClearCommentHover(window);
    activeTab = tab;
    const bool signals = tab == 0;
    const bool profiling = tab == 1;
    if(signals) graphCache.valid=false;
    ShowWindow(infoLabel, (signals || profiling) ? SW_HIDE : SW_SHOW);
    ShowWindow(startButton, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(stopButton, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(frequencyInput, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(frequencyLabel, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(displayLengthInput, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(displayLengthLabel, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(fileButton, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(commentButton, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(graphHelpButton, signals ? SW_SHOW : SW_HIDE);
    HWND profilingControls[] = { mocoHeading, mocoPortLabel, mocoPortInput, mocoBaudLabel, mocoBaudInput, mocoConnectButton,
                                 mocoDisconnectButton, mocoStatusLabel, mocoCommandLabel, mocoCommandInput, mocoSendButton,
                                 mocoConsoleLabel, mocoConsole, profilingStartLabel, profilingStartInput };
    for (HWND control : profilingControls) ShowWindow(control, profiling ? SW_SHOW : SW_HIDE);
    if (!signals) {
        const wchar_t* text[] = { L"", L"Select the serial port and settings for your MICOS MoCo controller. Commands can move the motor; use only commands from its manual.", L"Plan ahead: upcoming tasks will appear here.", L"Finished items will appear here.", L"Tasker settings will go here." };
        SetWindowTextW(infoLabel, text[tab]);
    }
    InvalidateRect(window, nullptr, TRUE);
}

struct WrittenDataRow {
    std::chrono::system_clock::time_point timestamp;
    size_t lineNumber;
    std::wstring comment;
};

std::wstring SanitizeComment(std::wstring text) {
    for (wchar_t& ch : text) if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
    return text;
}

bool RewriteCommentColumn(const std::filesystem::path& path, const std::map<size_t, std::string>& replacements) {
    if (replacements.empty()) return true;
    std::filesystem::path temporary = path;
    temporary += L".comment.tmp";
    std::ifstream input(path, std::ios::in | std::ios::binary);
    std::ofstream output(temporary, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!input || !output) return false;
    std::string line;
    size_t lineNumber = 0;
    std::map<size_t, bool> replaced;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto replacement = replacements.find(lineNumber);
        if (replacement != replacements.end()) {
            const size_t lastTab = line.find_last_of('\t');
            if (lastTab == std::string::npos) {
                input.close(); output.close();
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return false;
            }
            line.resize(lastTab + 1);
            line += replacement->second;
            replaced[lineNumber] = true;
        }
        output << line << "\r\n";
        ++lineNumber;
    }
    output.flush();
    if (!input.eof() || !output || replaced.size() != replacements.size()) {
        output.close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    input.close(); output.close();
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

bool AppendAverage(std::ofstream& file, std::chrono::system_clock::time_point timestamp, const std::array<double,8>& sums, unsigned long long samples, size_t lineNumber, std::vector<WrittenDataRow>& rows) {
    if (samples == 0) return false;
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch());
    const std::time_t timestampSeconds = std::chrono::system_clock::to_time_t(timestamp);
    std::tm localTime{};
    localtime_s(&localTime, &timestampSeconds);
    file << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << '.'
         << std::setw(3) << std::setfill('0') << (milliseconds.count() % 1000) << std::setfill(' ');
    file << std::fixed << std::setprecision(6);
    for (double sum : sums) file << '\t' << (sum / static_cast<double>(samples));
    file << '\t' << '\n';
    file.flush();
    if (!file) return false;
    rows.push_back({timestamp, lineNumber, L""});
    return true;
}

void ReceiveCommentRequests(std::vector<CommentRequest>& pending) {
    std::lock_guard<std::mutex> lock(commentMutex);
    while (!commentRequests.empty()) {
        pending.push_back(std::move(commentRequests.front()));
        commentRequests.pop_front();
    }
}

void AddGraphComment(HWND window, std::chrono::system_clock::time_point timestamp, const std::wstring& text) {
    const long long wallMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();
    double graphSeconds = 0.0;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        if (!history.empty()) {
            const auto& newest = history.back();
            graphSeconds = newest.seconds + (wallMilliseconds - newest.wallMilliseconds) / 1000.0;
        } else {
            const double steadyNow = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            const long long wallNow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            graphSeconds = steadyNow + (wallMilliseconds - wallNow) / 1000.0;
        }
        GraphComment item{graphSeconds, wallMilliseconds, text};
        const auto position = std::upper_bound(graphComments.begin(), graphComments.end(), item.seconds,
            [](double value, const GraphComment& existing) { return value < existing.seconds; });
        graphComments.insert(position, std::move(item));
    }
    PostMessageW(window, CommentMarkersChangedMessage, 0, 0);
}

std::vector<GraphComment> CopyGraphCommentsForPlotRange(double viewEnd, double viewSeconds, int left, int right,
                                                        int clipLeft, int clipRight) {
    std::vector<GraphComment> result;
    if (viewSeconds <= 0.0 || right <= left) return result;
    clipLeft = (std::max)(left, clipLeft - 16);
    clipRight = (std::min)(right, clipRight + 16);
    const double viewStart = viewEnd - viewSeconds;
    const double timePerPixel = viewSeconds / (right - left);
    const double firstTime = viewStart + (clipLeft - left) * timePerPixel;
    const double lastTime = viewStart + (clipRight - left) * timePerPixel;
    std::lock_guard<std::mutex> lock(dataMutex);
    auto item = std::lower_bound(graphComments.begin(), graphComments.end(), firstTime,
        [](const GraphComment& existing, double value) { return existing.seconds < value; });
    for (; item != graphComments.end() && item->seconds <= lastTime; ++item) result.push_back(*item);
    return result;
}

double NiceAxisStep(double rawStep) {
    if (!std::isfinite(rawStep) || rawStep <= 0.0) return 1.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(rawStep)));
    const double fraction = rawStep / magnitude;
    const double niceFraction = fraction <= 1.0 ? 1.0 : fraction <= 2.0 ? 2.0 : fraction <= 5.0 ? 5.0 : 10.0;
    return niceFraction * magnitude;
}

void DrawHorizontalGrid(HDC dc, int left, int right, int top, int bottom, double minV, double maxV) {
    if (maxV <= minV) return;
    const double step = NiceAxisStep((maxV - minV) / 4.0);
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(225,230,237));
    HGDIOBJ old = SelectObject(dc, grid);
    const double first = std::ceil(minV / step) * step;
    for (double value = first; value <= maxV + step * 1e-8; value += step) {
        const int y = bottom - static_cast<int>(std::lround((value - minV) * (bottom - top) / (maxV - minV)));
        MoveToEx(dc, left, y, nullptr); LineTo(dc, right, y);
    }
    SelectObject(dc, old); DeleteObject(grid);
}

void ResolveCommentRequests(HWND window, std::ofstream& file, const std::filesystem::path& path, std::vector<WrittenDataRow>& rows,
                            std::vector<CommentRequest>& pending, bool finalPass) {
    if (pending.empty()) return;
    if (rows.empty()) {
        if (finalPass) {
            for (const auto& request : pending) WriteLog(L"Comment could not be stored because no recording row was available: " + request.text);
            pending.clear();
        }
        return;
    }
    std::map<size_t, std::string> replacements;
    std::map<size_t, std::wstring> updatedComments;
    std::vector<CommentRequest> appliedComments;
    auto request = pending.begin();
    while (request != pending.end()) {
        if (!finalPass && request->timestamp > rows.back().timestamp) { ++request; continue; }
        auto nearest = std::min_element(rows.begin(), rows.end(), [&](const WrittenDataRow& a, const WrittenDataRow& b) {
            return std::chrono::abs(a.timestamp - request->timestamp) < std::chrono::abs(b.timestamp - request->timestamp);
        });
        std::wstring updated = updatedComments.count(nearest->lineNumber) ? updatedComments[nearest->lineNumber] : nearest->comment;
        const std::wstring cleaned = SanitizeComment(request->text);
        if (!updated.empty() && !cleaned.empty()) updated += L" | ";
        updated += cleaned;
        updatedComments[nearest->lineNumber] = updated;
        replacements[nearest->lineNumber] = WideToUtf8(updated);
        appliedComments.push_back({request->timestamp, cleaned});
        request = pending.erase(request);
    }
    if (replacements.empty()) return;
    file.flush();
    file.close();
    const bool rewritten = RewriteCommentColumn(path, replacements);
    file.open(path, std::ios::out | std::ios::app);
    if (!file) {
        WriteLog(L"ERROR: Could not reopen the data file after updating a comment.");
        return;
    }
    if (!rewritten) {
        WriteLog(L"ERROR: Could not update the comment column in the data file.");
        return;
    }
    for (auto& row : rows) {
        const auto updated = updatedComments.find(row.lineNumber);
        if (updated != updatedComments.end()) row.comment = updated->second;
    }
    for (const auto& comment : appliedComments) {
        AddGraphComment(window, comment.timestamp, comment.text);
        WriteLog(L"Comment assigned to recording row nearest " + FormatCommentTime(comment.timestamp) + L": " + comment.text);
    }
}

void AcquisitionLoop(HWND window) {
    std::wstring status;
    if (!daq.Connect(status)) {
        daq.Disconnect();
        NotifyStatus(window, status);
        acquisitionAlive.store(false);
        PostMessageW(window, RecordingFinishedMessage, 0, 0);
        PostMessageW(window, AcquisitionStoppedMessage, 0, 0);
        return;
    }
    const std::wstring connectedStatus = status;
    NotifyStatus(window, connectedStatus + L" Live signals are updating. Choose a file and start recording to store averages.");

    std::ofstream file;
    std::filesystem::path activeDataPath;
    unsigned long long indexedFileGeneration = (std::numeric_limits<unsigned long long>::max)();
    size_t nextFileLine = 0;
    std::vector<WrittenDataRow> writtenRows;
    std::vector<CommentRequest> pendingComments;
    bool sessionActive = false;
    double intervalSeconds = 1.0;
    std::chrono::steady_clock::time_point start{}, nextWindow{};
    std::chrono::steady_clock::duration period{};
    std::chrono::system_clock::time_point wallStart{};
    std::array<double,8> sums{};
    unsigned long long samples = 0;

    while (!shutdownRequested.load()) {
        if (startRecordingRequested.exchange(false)) {
            std::wstring path;
            unsigned long long fileGeneration = 0;
            {
                std::lock_guard<std::mutex> lock(commandMutex);
                path = pendingFilePath;
                intervalSeconds = pendingIntervalSeconds;
                fileGeneration = pendingFileGeneration;
            }
            if (fileGeneration != indexedFileGeneration || std::filesystem::path(path) != activeDataPath) {
                writtenRows.clear(); pendingComments.clear(); nextFileLine = 0;
                activeDataPath = std::filesystem::path(path);
                indexedFileGeneration = fileGeneration;
            }
            file.open(std::filesystem::path(path), std::ios::out | std::ios::app);
            if (!file) {
                NotifyStatus(window, L"Could not open storage file for writing: " + BaseName(path));
                recording.store(false);
                PostMessageW(window, RecordingFinishedMessage, 0, 0);
                continue;
            }
            file << "# Tasker recording; average interval=" << std::setprecision(8) << intervalSeconds << " s\n";
            file << "timestamp_local\tCH0_V\tCH1_V\tCH2_V\tCH3_V\tCH4_V\tCH5_V\tCH6_V\tCH7_V\tComment\n";
            file.flush();
            nextFileLine += 2;
            start = std::chrono::steady_clock::now();
            wallStart = std::chrono::system_clock::now();
            period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(intervalSeconds));
            if (period <= std::chrono::steady_clock::duration::zero()) period = std::chrono::steady_clock::duration(1);
            nextWindow = start + period;
            sums.fill(0.0);
            samples = 0;
            sessionActive = true;
            recording.store(true);
            PostMessageW(window, RecordingStartedMessage, 0, 0);
            NotifyStatus(window, connectedStatus + L" Recording; live graph continues. Averaging every " + std::to_wstring(intervalSeconds) + L" seconds.");
        }

        if (sessionActive) {
            ReceiveCommentRequests(pendingComments);
            ResolveCommentRequests(window, file, activeDataPath, writtenRows, pendingComments, false);
        }

        if (stopRecordingRequested.exchange(false) && sessionActive) {
            if (samples != 0 && AppendAverage(file, std::chrono::system_clock::now(), sums, samples, nextFileLine, writtenRows)) ++nextFileLine;
            sums.fill(0.0); samples = 0;
            ReceiveCommentRequests(pendingComments);
            ResolveCommentRequests(window, file, activeDataPath, writtenRows, pendingComments, true);
            file.flush();
            file.close();
            sessionActive = false;
            recording.store(false);
            sums.fill(0.0);
            samples = 0;
            NotifyStatus(window, connectedStatus + L" Recording stopped; live graph continues. Data saved to " + BaseName(dataFilePath) + L".");
            PostMessageW(window, RecordingFinishedMessage, 0, 0);
        }

        std::array<float,8> volts{};
        std::wstring error;
        if (!daq.ReadAll(volts, error)) {
            status = error;
            break;
        }
        const auto sampleTime = std::chrono::steady_clock::now();
        const auto wallNow=std::chrono::system_clock::now();
        const auto wallMilliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(wallNow.time_since_epoch()).count();
        const std::time_t wallSeconds=std::chrono::system_clock::to_time_t(wallNow);
        std::tm localSampleTime{}; localtime_s(&localSampleTime,&wallSeconds);
        const int dateKey=(localSampleTime.tm_year+1900)*10000+(localSampleTime.tm_mon+1)*100+localSampleTime.tm_mday;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            latest = volts;
            if(history.size()>=MaxGraphSamples){
                std::deque<SignalSample> reduced;
                for(size_t i=0;i<history.size();i+=2) reduced.push_back(history[i]);
                if(reduced.back().sequence!=history.back().sequence) reduced.push_back(history.back());
                history.swap(reduced);
            }
            history.push_back({std::chrono::duration<double>(sampleTime.time_since_epoch()).count(), ++nextSampleSequence, wallMilliseconds, dateKey, volts});
        }
        if (sessionActive) {
            if (sampleTime >= nextWindow) {
                const auto intervalTimestamp = wallStart + std::chrono::duration_cast<std::chrono::system_clock::duration>(nextWindow - start);
                if (AppendAverage(file, intervalTimestamp, sums, samples, nextFileLine, writtenRows)) ++nextFileLine;
                sums.fill(0.0);
                samples = 0;
                do { nextWindow += period; } while (nextWindow <= sampleTime);
                ReceiveCommentRequests(pendingComments);
                ResolveCommentRequests(window, file, activeDataPath, writtenRows, pendingComments, false);
            }
            for (int ch=0; ch<8; ++ch) sums[ch] += volts[ch];
            ++samples;
        }
    }

    if (sessionActive && samples != 0 && AppendAverage(file, std::chrono::system_clock::now(), sums, samples, nextFileLine, writtenRows)) ++nextFileLine;
    if (sessionActive) {
        ReceiveCommentRequests(pendingComments);
        ResolveCommentRequests(window, file, activeDataPath, writtenRows, pendingComments, true);
    }
    if (file.is_open()) { file.flush(); file.close(); }
    daq.Disconnect();
    recording.store(false);
    acquisitionAlive.store(false);
    if (status.empty()) status = L"Live acquisition stopped.";
    NotifyStatus(window, status);
    PostMessageW(window, RecordingFinishedMessage, 0, 0);
    PostMessageW(window, AcquisitionStoppedMessage, 0, 0);
}

void StartAcquisition(HWND window) {
    if (acquisitionAlive.load()) return;
    if (acquisitionThread.joinable()) acquisitionThread.join();
    shutdownRequested.store(false);
    stopRecordingRequested.store(false);
    startRecordingRequested.store(false);
    acquisitionAlive.store(true);
    acquisitionThread = std::thread(AcquisitionLoop, window);
}

void StartRecording(HWND window) {
    if (recording.load() || startRecordingRequested.load()) return;
    if (dataFilePath.empty() && !ChooseDataFile(window)) {
        WriteLog(L"Cannot start recording: choose a .txt storage file first.");
        MessageBoxW(window, L"Choose a .txt storage file before recording.", L"Tasker", MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t text[64]{};
    GetWindowTextW(frequencyInput, text, 64);
    wchar_t* end = nullptr;
    const double intervalSeconds = std::wcstod(text, &end);
    if (end == text || *end != L'\0' || !std::isfinite(intervalSeconds) || intervalSeconds < 0.00001 || intervalSeconds > 86400.0) {
        WriteLog(L"Invalid recording interval entered; expected 0.00001 to 86400 seconds.");
        MessageBoxW(window, L"Enter a recording interval from 0.00001 to 86400 seconds.", L"Tasker", MB_OK | MB_ICONWARNING);
        SetFocus(frequencyInput);
        return;
    }
    if (!acquisitionAlive.load()) StartAcquisition(window);
    {
        std::lock_guard<std::mutex> lock(commandMutex);
        pendingFilePath = dataFilePath;
        pendingFileGeneration = dataFileGeneration.load();
        pendingIntervalSeconds = intervalSeconds;
    }
    stopRecordingRequested.store(false);
    startRecordingRequested.store(true);
    EnableWindow(startButton, FALSE);
    EnableWindow(stopButton, TRUE);
    EnableWindow(frequencyInput, FALSE);
    EnableWindow(fileButton, FALSE);
    EnableWindow(commentButton, FALSE);
    WriteLog(L"Starting file recording; live graph remains active.");
}

void StopRecording() {
    if (!recording.load() && !startRecordingRequested.load()) return;
    stopRecordingRequested.store(true);
    WriteLog(L"Stopping recording and flushing the data file.");
}

RECT GraphBounds(HWND window) {
    RECT client{}; GetClientRect(window,&client);
    return RECT{32,190,client.right-32,client.bottom-130};
}

void SyncDisplayLengthField() {
    if(!displayLengthInput || GetFocus()==displayLengthInput) return;
    wchar_t text[64]{}; swprintf_s(text,L"%.6g",displayLengthSeconds.load());
    displayLengthSync=true; SetWindowTextW(displayLengthInput,text); displayLengthSync=false;
}

void ClampGraphViewToHistory(double& viewEnd, double viewSeconds, double* latestTime=nullptr) {
    std::lock_guard<std::mutex> lock(dataMutex);
    if(history.empty()) return;
    const double newest=history.back().seconds;
    double oldestAllowed=history.front().seconds+viewSeconds;
    if(oldestAllowed>newest) oldestAllowed=newest;
    viewEnd=(std::max)(oldestAllowed,(std::min)(newest,viewEnd));
    if(latestTime) *latestTime=newest;
}

void DestroyGraphSurface() {
    if(graphCache.dc){
        if(graphCache.previousBitmap) SelectObject(graphCache.dc,graphCache.previousBitmap);
        if(graphCache.bitmap) DeleteObject(graphCache.bitmap);
        DeleteDC(graphCache.dc);
    }
    graphCache={};
}

bool EnsureGraphSurface(HWND window) {
    const RECT bounds=GraphBounds(window);
    const int width=bounds.right-bounds.left, height=bounds.bottom-bounds.top;
    if(width<=0 || height<=0) return false;
    if(graphCache.dc && graphCache.width==width && graphCache.height==height) return true;
    DestroyGraphSurface();
    HDC screen=GetDC(window);
    graphCache.dc=CreateCompatibleDC(screen);
    graphCache.bitmap=CreateCompatibleBitmap(screen,width,height);
    ReleaseDC(window,screen);
    if(!graphCache.dc || !graphCache.bitmap){ DestroyGraphSurface(); return false; }
    graphCache.previousBitmap=SelectObject(graphCache.dc,graphCache.bitmap);
    graphCache.width=width; graphCache.height=height; graphCache.valid=false;
    return true;
}

void DrawGraphLegend(HDC dc, int right, int top, int bottom, const std::array<float,8>& values) {
    RECT panel{right+13,0,graphCache.width,graphCache.height};
    HBRUSH bg=CreateSolidBrush(RGB(250,251,253)); FillRect(dc,&panel,bg); DeleteObject(bg);
    HPEN divider=CreatePen(PS_SOLID,1,RGB(225,230,237)); HGDIOBJ old=SelectObject(dc,divider);
    MoveToEx(dc,right+12,top-10,nullptr); LineTo(dc,right+12,bottom+8); SelectObject(dc,old); DeleteObject(divider);
    const int legendX=right+24;
    const wchar_t legendTitle[]=L"Channels"; SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(45,55,70));
    TextOutW(dc,legendX,8,legendTitle,static_cast<int>(wcslen(legendTitle)));
    for(int ch=0;ch<8;++ch){
        const int y=top+ch*34;
        HPEN pen=CreatePen(PS_SOLID,3,colors[ch]); old=SelectObject(dc,pen);
        MoveToEx(dc,legendX,y+8,nullptr); LineTo(dc,legendX+20,y+8); SelectObject(dc,old); DeleteObject(pen);
        wchar_t label[64]{}; swprintf_s(label,L"CH%d  %+.4f V",ch,values[ch]); SetTextColor(dc,RGB(55,65,80));
        TextOutW(dc,legendX+28,y,label,static_cast<int>(wcslen(label)));
    }
}

std::wstring FormatLocalDateTime(long long wallMilliseconds, const wchar_t* format) {
    const std::time_t seconds=static_cast<std::time_t>(wallMilliseconds/1000);
    std::tm local{};
    localtime_s(&local,&seconds);
    wchar_t text[32]{};
    wcsftime(text,32,format,&local);
    return text;
}

std::wstring FormatDateKey(int dateKey) {
    wchar_t text[16]{};
    swprintf_s(text,L"%04d-%02d-%02d",dateKey/10000,(dateKey/100)%100,dateKey%100);
    return text;
}

void DrawTimeAxis(HDC dc, int width, int height, double viewSeconds, long long wallEndMilliseconds, const std::vector<AxisDateSpan>& dates) {
    const int left=72,right=width-220,bottom=height-62;
    RECT axis{0,bottom+1,width,height};
    HBRUSH bg=CreateSolidBrush(RGB(250,251,253)); FillRect(dc,&axis,bg); DeleteObject(bg);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(65,75,90));
    long long tickSeconds=1;
    const double targetInterval=viewSeconds/5.0;
    if(targetInterval<60.0){
        const long long choices[]={1,2,5,10,15,30}; tickSeconds=30;
        for(long long choice:choices) if(choice>=targetInterval){tickSeconds=choice;break;}
    }else if(targetInterval<3600.0){
        const long long choices[]={60,120,300,600,900,1800}; tickSeconds=1800;
        for(long long choice:choices) if(choice>=targetInterval){tickSeconds=choice;break;}
    }else{
        const long long choices[]={3600,7200,10800,21600,43200,86400}; tickSeconds=86400;
        for(long long choice:choices) if(choice>=targetInterval){tickSeconds=choice;break;}
    }
    HPEN tickPen=CreatePen(PS_SOLID,1,RGB(105,115,130)); HGDIOBJ oldTickPen=SelectObject(dc,tickPen);
    if(wallEndMilliseconds){
        const double endSeconds=wallEndMilliseconds/1000.0;
        const long long firstTick=static_cast<long long>(std::floor(endSeconds/tickSeconds))*tickSeconds;
        const long long firstVisible=static_cast<long long>(std::ceil(endSeconds-viewSeconds));
        for(long long tick=firstTick;tick>=firstVisible;tick-=tickSeconds){
            const int x=left+static_cast<int>((right-left)*(tick-(endSeconds-viewSeconds))/viewSeconds);
            if(x<left || x>right) continue;
            MoveToEx(dc,x,bottom-4,nullptr); LineTo(dc,x,bottom+3);
            const std::wstring label=FormatLocalDateTime(tick*1000,L"%H:%M:%S");
            SIZE size{}; GetTextExtentPoint32W(dc,label.c_str(),static_cast<int>(label.size()),&size);
            TextOutW(dc,x-size.cx/2,bottom+5,label.c_str(),static_cast<int>(label.size()));
        }
        const double secondsPerPixel=viewSeconds/(right-left);
        long long minorTickSeconds=static_cast<long long>(NiceAxisStep(secondsPerPixel*8.0));
        if(minorTickSeconds>=tickSeconds) minorTickSeconds=tickSeconds>=5 ? tickSeconds/5 : 0;
        if(minorTickSeconds>0 && minorTickSeconds<tickSeconds){
            const long long firstMinor=static_cast<long long>(std::floor(endSeconds/minorTickSeconds))*minorTickSeconds;
            for(long long tick=firstMinor;tick>=firstVisible;tick-=minorTickSeconds){
                if(tick%tickSeconds==0) continue;
                const int x=left+static_cast<int>((right-left)*(tick-(endSeconds-viewSeconds))/viewSeconds);
                if(x<left || x>right) continue;
                MoveToEx(dc,x,bottom-2,nullptr); LineTo(dc,x,bottom+2);
            }
        }
    }
    SelectObject(dc,oldTickPen); DeleteObject(tickPen);
    SetTextColor(dc,RGB(105,115,130));
    for(const auto& span:dates){
        const std::wstring date=FormatDateKey(span.dateKey);
        SIZE size{}; GetTextExtentPoint32W(dc,date.c_str(),static_cast<int>(date.size()),&size);
        const int center=(span.firstX+span.lastX)/2;
        TextOutW(dc,center-size.cx/2,bottom+25,date.c_str(),static_cast<int>(date.size()));
    }
    wchar_t unitLabel[64]{};
    if(tickSeconds<60) swprintf_s(unitLabel,L"Tick spacing: %lld s",tickSeconds);
    else if(tickSeconds<3600) swprintf_s(unitLabel,L"Tick spacing: %lld min",tickSeconds/60);
    else swprintf_s(unitLabel,L"Tick spacing: %lld h",tickSeconds/3600);
    SetTextColor(dc,RGB(105,115,130)); TextOutW(dc,left,bottom+43,unitLabel,static_cast<int>(wcslen(unitLabel)));
}

void DrawGraphComments(HDC dc, const std::vector<GraphComment>& comments, double viewEnd, double viewSeconds,
                       int left, int right, int top, int bottom, int redrawLeft, int redrawRight) {
    if (comments.empty() || viewSeconds <= 0.0 || right <= left || bottom <= top) return;
    const double viewStart = viewEnd - viewSeconds;
    const int markerY = top + (bottom - top) / 10;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, redrawLeft, top, redrawRight + 1, bottom + 1);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(92, 48, 28));
    HFONT font = CreateFontW(-14, 0, 900, 900, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    HBRUSH dotBrush = CreateSolidBrush(RGB(210, 70, 45));
    HPEN dotPen = CreatePen(PS_SOLID, 1, RGB(145, 45, 30));
    HGDIOBJ oldBrush = SelectObject(dc, dotBrush);
    HGDIOBJ oldPen = SelectObject(dc, dotPen);
    for (const auto& comment : comments) {
        if (comment.seconds < viewStart || comment.seconds > viewEnd || comment.text.empty()) continue;
        const int x = left + static_cast<int>(std::lround((right - left) * (comment.seconds - viewStart) / viewSeconds));
        if (x < left || x > right) continue;
        if (x < redrawLeft - 16 || x > redrawRight + 16) continue;
        SIZE extent{};
        GetTextExtentPoint32W(dc, comment.text.c_str(), static_cast<int>(comment.text.size()), &extent);
        const int textX = x;
        // At 90 degrees the text advances upward from its starting baseline,
        // so begin just below the marker to keep the rotated label above it.
        const int textY = markerY + 5;
        TextOutW(dc, textX, textY, comment.text.c_str(), static_cast<int>(comment.text.size()));
        Ellipse(dc, x - 4, markerY - 4, x + 5, markerY + 5);
    }
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldFont);
    DeleteObject(dotPen);
    DeleteObject(dotBrush);
    DeleteObject(font);
    RestoreDC(dc, saved);
}

void RenderGraphFully(HWND window) {
    if(!EnsureGraphSurface(window)) return;
    std::vector<SignalSample> visible;
    std::vector<GraphComment> comments;
    std::array<float,8> liveValues{};
    const double viewSeconds=displayLengthSeconds.load();
    double viewEnd=0.0;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        liveValues=latest;
        if(!history.empty()){
            const double newest=history.back().seconds;
            if(graphFollowingLive) graphViewEndSeconds=newest;
            double oldestAllowed=history.front().seconds+viewSeconds;
            if(oldestAllowed>newest) oldestAllowed=newest;
            graphViewEndSeconds=(std::max)(oldestAllowed,(std::min)(newest,graphViewEndSeconds));
            viewEnd=graphViewEndSeconds;
            const double cutoff=viewEnd-viewSeconds;
            for(const auto& sample:history) if(sample.seconds>=cutoff && sample.seconds<=viewEnd) visible.push_back(sample);
            if(!graphFollowingLive && !visible.empty()) liveValues=visible.back().volts;
        }
    }
    HDC dc=graphCache.dc;
    RECT r{0,0,graphCache.width,graphCache.height};
    HBRUSH bg=CreateSolidBrush(RGB(250,251,253)); FillRect(dc,&r,bg); DeleteObject(bg);
    const int left=72,right=graphCache.width-220,top=66,bottom=graphCache.height-62;
    if(right<=left || bottom<=top) return;
    comments=CopyGraphCommentsForPlotRange(viewEnd,viewSeconds,left,right,left,right);
    double minV=-0.01,maxV=0.01;
    if(graphCache.manualYScale){
        minV=graphCache.requestedMinV; maxV=graphCache.requestedMaxV;
    }else if(!visible.empty()){
        minV=(std::numeric_limits<double>::max)(); maxV=(std::numeric_limits<double>::lowest)();
        for(const auto& sample:visible) for(float v:sample.volts){ minV=(std::min)(minV,static_cast<double>(v)); maxV=(std::max)(maxV,static_cast<double>(v)); }
        const double center=(minV+maxV)*0.5, span=(std::max)(maxV-minV,0.02);
        minV=center-span*0.6; maxV=center+span*0.6;
    }
    graphCache.wallEndMilliseconds=visible.empty()?0:visible.back().wallMilliseconds;
    if (!graphCache.manualYScale) {
        const double step=NiceAxisStep((maxV-minV)/4.0);
        minV=std::floor(minV/step)*step; maxV=std::ceil(maxV/step)*step;
        if(maxV<=minV) maxV=minV+step;
    }
    DrawHorizontalGrid(dc,left,right,top,bottom,minV,maxV);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(65,75,90));
    const double yStep=NiceAxisStep((maxV-minV)/4.0);
    const double firstYTick=std::ceil(minV/yStep)*yStep;
    for(double value=firstYTick;value<=maxV+yStep*1e-8;value+=yStep){
        wchar_t label[32]{}; swprintf_s(label,L"%.4g V",value);
        const int y=bottom-static_cast<int>(std::lround((value-minV)*(bottom-top)/(maxV-minV)));
        TextOutW(dc,5,y-9,label,static_cast<int>(wcslen(label)));
    }
    graphCache.dateSpans.clear();
    if(!visible.empty()){
        const double timeStart=viewEnd-viewSeconds;
        for(const auto& sample:visible){
            const int x=left+static_cast<int>((right-left)*(sample.seconds-timeStart)/viewSeconds);
            if(graphCache.dateSpans.empty() || graphCache.dateSpans.back().dateKey!=sample.localDateKey) graphCache.dateSpans.push_back({sample.localDateKey,x,x});
            else graphCache.dateSpans.back().lastX=x;
        }
    }
    DrawGraphLegend(dc,right,top,bottom,liveValues);
    graphCache.minV=minV; graphCache.maxV=maxV; graphCache.viewSeconds=viewSeconds;
    if(!graphCache.manualYScale){ graphCache.requestedMinV=minV; graphCache.requestedMaxV=maxV; }
    graphCache.hasLastPoint.fill(false); graphCache.lastSequence=0;
    graphCache.viewEnd=viewEnd;
    if(!visible.empty()){
        const double timeStart=graphCache.viewEnd-viewSeconds;
        for(int ch=0;ch<8;++ch){
            HPEN pen=CreatePen(PS_SOLID,2,colors[ch]); HGDIOBJ previous=SelectObject(dc,pen);
            if (visible.size() > static_cast<size_t>((right-left) * 2)) {
                const int plotWidth=right-left+1;
                std::vector<int> minY(static_cast<size_t>(plotWidth),bottom+1), maxY(static_cast<size_t>(plotWidth),top-1);
                for(const auto& sample:visible){
                    const int x=left+static_cast<int>((right-left)*(sample.seconds-timeStart)/viewSeconds);
                    if(x<left || x>right) continue;
                    const double v=(std::max)(minV,(std::min)(maxV,static_cast<double>(sample.volts[ch])));
                    const int y=bottom-static_cast<int>((v-minV)*(bottom-top)/(maxV-minV));
                    const size_t column=static_cast<size_t>(x-left);
                    minY[column]=(std::min)(minY[column],y); maxY[column]=(std::max)(maxY[column],y);
                }
                std::vector<POINT> envelope;
                std::vector<DWORD> segmentCounts;
                envelope.reserve(static_cast<size_t>(plotWidth)*2); segmentCounts.reserve(static_cast<size_t>(plotWidth));
                for(int column=0;column<plotWidth;++column){
                    if(minY[static_cast<size_t>(column)]>maxY[static_cast<size_t>(column)]) continue;
                    envelope.push_back(POINT{left+column,minY[static_cast<size_t>(column)]});
                    envelope.push_back(POINT{left+column,maxY[static_cast<size_t>(column)]});
                    segmentCounts.push_back(2);
                }
                if(!segmentCounts.empty()) PolyPolyline(dc,envelope.data(),segmentCounts.data(),static_cast<DWORD>(segmentCounts.size()));
                const auto& sample=visible.back();
                const int x=left+static_cast<int>((right-left)*(sample.seconds-timeStart)/viewSeconds);
                const double v=(std::max)(minV,(std::min)(maxV,static_cast<double>(sample.volts[ch])));
                const int y=bottom-static_cast<int>((v-minV)*(bottom-top)/(maxV-minV));
                graphCache.lastPoints[ch]={x,y}; graphCache.hasLastPoint[ch]=true;
            }else{
                for(const auto& sample:visible){
                    const int x=left+static_cast<int>((right-left)*(sample.seconds-timeStart)/viewSeconds);
                    double v=(std::max)(minV,(std::min)(maxV,static_cast<double>(sample.volts[ch])));
                    const int y=bottom-static_cast<int>((v-minV)*(bottom-top)/(maxV-minV));
                    if(graphCache.hasLastPoint[ch]) MoveToEx(dc,graphCache.lastPoints[ch].x,graphCache.lastPoints[ch].y,nullptr); else MoveToEx(dc,x,y,nullptr);
                    LineTo(dc,x,y); graphCache.lastPoints[ch]={x,y}; graphCache.hasLastPoint[ch]=true;
                }
            }
            SelectObject(dc,previous); DeleteObject(pen);
        }
        graphCache.lastSequence=visible.back().sequence;
    }else{
        const wchar_t waiting[]=L"Waiting for live samples..."; SetTextColor(dc,RGB(115,125,140));
        TextOutW(dc,left+24,top+24,waiting,static_cast<int>(wcslen(waiting)));
    }
    DrawGraphComments(dc,comments,viewEnd,viewSeconds,left,right,top,bottom,left,right);
    DrawTimeAxis(dc,graphCache.width,graphCache.height,viewSeconds,graphCache.wallEndMilliseconds,graphCache.dateSpans);
    graphCache.valid=true;
}

void UpdateGraphIncrementally(HWND window) {
    if(!graphFollowingLive) return;
    if(!EnsureGraphSurface(window)) return;
    std::vector<SignalSample> newSamples;
    std::vector<GraphComment> comments;
    std::array<float,8> liveValues{};
    bool historyLost=false;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        liveValues=latest;
        if(!history.empty()){
            historyLost=graphCache.lastSequence && history.front().sequence>graphCache.lastSequence+1;
            for(const auto& sample:history){
                if(sample.sequence>graphCache.lastSequence) newSamples.push_back(sample);
            }
        }
    }
    const double viewSeconds=displayLengthSeconds.load();
    if(!graphCache.valid || std::abs(graphCache.viewSeconds-viewSeconds)>1e-9 || historyLost){
        RenderGraphFully(window);
        RECT bounds=GraphBounds(window); InvalidateRect(window,&bounds,FALSE); return;
    }
    if(newSamples.empty()) return;
    const int left=72,right=graphCache.width-220,top=66,bottom=graphCache.height-62;
    if(right<=left || bottom<=top) return;
    const double newEnd=newSamples.back().seconds;
    const int dx=static_cast<int>(std::floor((newEnd-graphCache.viewEnd)*(right-left)/viewSeconds+0.5));
    bool needsFull=dx<0 || dx>=right-left;
    if(needsFull){ RenderGraphFully(window); RECT bounds=GraphBounds(window); InvalidateRect(window,&bounds,FALSE); return; }

    const RECT screenBounds=GraphBounds(window);
    const RECT dataLocal{left,top,right+1,bottom+1};
    RECT changedLocal{};
    if(dx>0){
        ScrollDC(graphCache.dc,-dx,0,&dataLocal,&dataLocal,nullptr,&changedLocal);
        for(auto& point:graphCache.lastPoints) point.x-=dx;
        RECT exposed{right-dx+1,top,right+1,bottom+1};
        HBRUSH bg=CreateSolidBrush(RGB(250,251,253)); FillRect(graphCache.dc,&exposed,bg); DeleteObject(bg);
        DrawHorizontalGrid(graphCache.dc,right-dx+1,right+1,top,bottom,graphCache.minV,graphCache.maxV);
        RECT screenData{screenBounds.left+left,screenBounds.top+top,screenBounds.left+right+1,screenBounds.top+bottom+1};
        ScrollWindowEx(window,-dx,0,&screenData,&screenData,nullptr,nullptr,SW_INVALIDATE);
        changedLocal=exposed;
    }else{
        changedLocal=RECT{(std::max)(left,right-3),top,right+1,bottom+1};
        HBRUSH bg=CreateSolidBrush(RGB(250,251,253)); FillRect(graphCache.dc,&changedLocal,bg); DeleteObject(bg);
        DrawHorizontalGrid(graphCache.dc,changedLocal.left,changedLocal.right,top,bottom,graphCache.minV,graphCache.maxV);
    }

    const double timeStart=newEnd-viewSeconds;
    for(auto& span:graphCache.dateSpans){ span.firstX-=dx; span.lastX-=dx; }
    while(!graphCache.dateSpans.empty() && graphCache.dateSpans.front().lastX<left) graphCache.dateSpans.erase(graphCache.dateSpans.begin());
    if(!graphCache.dateSpans.empty() && graphCache.dateSpans.front().firstX<left) graphCache.dateSpans.front().firstX=left;
    for(int ch=0;ch<8;++ch){
        HPEN pen=CreatePen(PS_SOLID,2,colors[ch]); HGDIOBJ old=SelectObject(graphCache.dc,pen);
        for(const auto& sample:newSamples){
            const int x=left+static_cast<int>((right-left)*(sample.seconds-timeStart)/viewSeconds);
            if(x<left || x>right) continue;
            const double value=(std::max)(graphCache.minV,(std::min)(graphCache.maxV,static_cast<double>(sample.volts[ch])));
            const int y=bottom-static_cast<int>((value-graphCache.minV)*(bottom-top)/(graphCache.maxV-graphCache.minV));
            if(graphCache.hasLastPoint[ch]) MoveToEx(graphCache.dc,graphCache.lastPoints[ch].x,graphCache.lastPoints[ch].y,nullptr); else MoveToEx(graphCache.dc,x,y,nullptr);
            LineTo(graphCache.dc,x,y);
            graphCache.lastPoints[ch]={x,y}; graphCache.hasLastPoint[ch]=true;
        }
        SelectObject(graphCache.dc,old); DeleteObject(pen);
    }
    for(const auto& sample:newSamples){
        const int x=left+static_cast<int>((right-left)*(sample.seconds-timeStart)/viewSeconds);
        if(x<left || x>right) continue;
        if(graphCache.dateSpans.empty() || graphCache.dateSpans.back().dateKey!=sample.localDateKey) graphCache.dateSpans.push_back({sample.localDateKey,x,x});
        else graphCache.dateSpans.back().lastX=x;
    }
    graphCache.viewEnd=newEnd; graphViewEndSeconds=newEnd; graphCache.lastSequence=newSamples.back().sequence;
    graphCache.wallEndMilliseconds=newSamples.back().wallMilliseconds;

    comments=CopyGraphCommentsForPlotRange(newEnd,viewSeconds,left,right,changedLocal.left,changedLocal.right-1);
    DrawGraphComments(graphCache.dc,comments,newEnd,viewSeconds,left,right,top,bottom,changedLocal.left,changedLocal.right-1);
    DrawGraphLegend(graphCache.dc,right,top,bottom,liveValues);
    DrawTimeAxis(graphCache.dc,graphCache.width,graphCache.height,viewSeconds,graphCache.wallEndMilliseconds,graphCache.dateSpans);
    RECT legendLocal{right+13,0,graphCache.width,graphCache.height};
    RECT legendScreen{screenBounds.left+legendLocal.left,screenBounds.top,screenBounds.left+legendLocal.right,screenBounds.top+legendLocal.bottom};
    InvalidateRect(window,&legendScreen,FALSE);
    RECT changedScreen{screenBounds.left+changedLocal.left,screenBounds.top+changedLocal.top,screenBounds.left+changedLocal.right,screenBounds.top+changedLocal.bottom};
    InvalidateRect(window,&changedScreen,FALSE);
    RECT axisScreen{screenBounds.left,screenBounds.top+bottom+1,screenBounds.left+graphCache.width,screenBounds.top+graphCache.height};
    InvalidateRect(window,&axisScreen,FALSE);
}

RECT CommentTooltipRect(HWND window, POINT anchor, const std::wstring& text) {
    RECT client{}; GetClientRect(window, &client);
    const int maxWidth = (std::max)(80, (std::min)(420, static_cast<int>(client.right) - 24));
    RECT measured{0, 0, maxWidth - 20, 0};
    HDC dc = GetDC(window);
    HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, oldFont); DeleteObject(font); ReleaseDC(window, dc);
    const int width = (std::min)(maxWidth, static_cast<int>(measured.right) + 20);
    const int height = (std::min)(static_cast<int>(client.bottom) - 12, static_cast<int>(measured.bottom) + 18);
    int x = anchor.x + 12;
    int y = anchor.y + 10;
    if (x + width > client.right - 6) x = anchor.x - width - 12;
    if (y + height > client.bottom - 6) y = static_cast<int>(client.bottom) - height - 6;
    x = (std::max)(6, (std::min)(x, static_cast<int>(client.right) - width - 6));
    y = (std::max)(6, y);
    return RECT{x, y, x + width, y + height};
}

void DrawCommentTooltip(HWND window, HDC dc) {
    if (!hoveringGraphComment || hoveredCommentText.empty()) return;
    (void)window;
    RECT bounds = hoveredCommentTooltipRect;
    HBRUSH background = CreateSolidBrush(RGB(255, 255, 238));
    FillRect(dc, &bounds, background); DeleteObject(background);
    HBRUSH border = CreateSolidBrush(RGB(95, 95, 75));
    FrameRect(dc, &bounds, border); DeleteObject(border);
    InflateRect(&bounds, -9, -7);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(35, 35, 30));
    DrawTextW(dc, hoveredCommentText.c_str(), static_cast<int>(hoveredCommentText.size()), &bounds, DT_WORDBREAK | DT_NOPREFIX);
}

void ClearCommentHover(HWND window) {
    if (hoveringGraphComment) {
        InvalidateRect(window, &hoveredCommentTooltipRect, FALSE);
    }
    hoveringGraphComment = false;
    hoveredCommentText.clear();
}

void UpdateCommentHover(HWND window, POINT cursor) {
    if (activeTab != 0 || !graphCache.valid) {
        ClearCommentHover(window);
        return;
    }
    const RECT bounds = GraphBounds(window);
    const int left = 72, right = graphCache.width - 220;
    const int top = 66, bottom = graphCache.height - 62;
    const int markerY = bounds.top + top + (bottom - top) / 10;
    std::wstring hitText;
    POINT hitAnchor{};
    int bestDistance = 8 * 8 + 1;
    if (graphCache.viewSeconds > 0.0 && cursor.y >= bounds.top + top - 3 && cursor.y <= markerY + 8 &&
        cursor.x >= bounds.left + left - 8 && cursor.x <= bounds.left + right + 8) {
        const double viewStart = graphCache.viewEnd - graphCache.viewSeconds;
        const double timePerPixel = graphCache.viewSeconds / (right - left);
        const double firstTime = viewStart + (cursor.x - (bounds.left + left) - 9) * timePerPixel;
        const double lastTime = viewStart + (cursor.x - (bounds.left + left) + 9) * timePerPixel;
        std::vector<GraphComment> nearby;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            auto item = std::lower_bound(graphComments.begin(), graphComments.end(), firstTime,
                [](const GraphComment& existing, double value) { return existing.seconds < value; });
            for (; item != graphComments.end() && item->seconds <= lastTime; ++item) nearby.push_back(*item);
        }
        for (const auto& comment : nearby) {
            const int x = bounds.left + left + static_cast<int>(std::lround((right - left) * (comment.seconds - viewStart) / graphCache.viewSeconds));
            const int dx = cursor.x - x;
            if (std::abs(dx) <= 8) {
                const int distance = dx * dx;
                if (distance >= bestDistance) continue;
                bestDistance = distance;
                hitText = comment.text;
                hitAnchor = POINT{x, markerY};
            }
        }
    }
    if (hitText == hoveredCommentText && hitAnchor.x == hoveredCommentAnchor.x && hitAnchor.y == hoveredCommentAnchor.y) return;
    const bool hadTooltip = hoveringGraphComment;
    const RECT old = hadTooltip ? hoveredCommentTooltipRect : RECT{};
    hoveredCommentText = std::move(hitText);
    hoveredCommentAnchor = hitAnchor;
    hoveringGraphComment = !hoveredCommentText.empty();
    if (hadTooltip) InvalidateRect(window, &old, FALSE);
    if (hoveringGraphComment) {
        hoveredCommentTooltipRect = CommentTooltipRect(window, hoveredCommentAnchor, hoveredCommentText);
        InvalidateRect(window, &hoveredCommentTooltipRect, FALSE);
    }
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch(message){
    case WM_PAINT:{
        PAINTSTRUCT ps; HDC dc=BeginPaint(window,&ps);
        if(activeTab==0 && EnsureGraphSurface(window)){
            if(!graphCache.valid) RenderGraphFully(window);
            const RECT bounds=GraphBounds(window);
            BitBlt(dc,bounds.left,bounds.top,graphCache.width,graphCache.height,graphCache.dc,0,0,SRCCOPY);
            DrawCommentTooltip(window,dc);
        }
        EndPaint(window,&ps); return 0;
    }
    case WM_CREATE:{
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES|ICC_BAR_CLASSES|ICC_WIN95_CLASSES}; InitCommonControlsEx(&controls);
        statusBar=CreateWindowExW(0,STATUSCLASSNAMEW,L"",WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,0,0,0,0,window,reinterpret_cast<HMENU>(StatusBarId),GetModuleHandleW(nullptr),nullptr);
        LayoutStatusBar(window);
        tabs=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,20,18,1080,46,window,reinterpret_cast<HMENU>(TabsId),GetModuleHandleW(nullptr),nullptr);
        for(int i=0;i<5;++i){ TCITEMW item{}; item.mask=TCIF_TEXT; item.pszText=const_cast<wchar_t*>(tabNames[i]); TabCtrl_InsertItem(tabs,i,&item); }
        frequencyLabel=CreateWindowW(L"STATIC",L"Recording interval (s)",WS_CHILD|WS_VISIBLE,610,746,150,34,window,reinterpret_cast<HMENU>(FrequencyLabelId),GetModuleHandleW(nullptr),nullptr);
        frequencyInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"1",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|WS_TABSTOP,770,742,80,40,window,reinterpret_cast<HMENU>(FrequencyId),GetModuleHandleW(nullptr),nullptr);
        displayLengthLabel=CreateWindowW(L"STATIC",L"Display length (s)",WS_CHILD|WS_VISIBLE,32,746,130,34,window,reinterpret_cast<HMENU>(DisplayLengthLabelId),GetModuleHandleW(nullptr),nullptr);
        displayLengthInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"10",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|WS_TABSTOP,164,742,90,40,window,reinterpret_cast<HMENU>(DisplayLengthId),GetModuleHandleW(nullptr),nullptr);
        fileButton=CreateWindowW(L"BUTTON",L"Choose/change file...",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,32,118,176,40,window,reinterpret_cast<HMENU>(FileButtonId),GetModuleHandleW(nullptr),nullptr);
        graphHelpButton=CreateWindowW(L"BUTTON",L"?",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,1088,196,28,28,window,reinterpret_cast<HMENU>(GraphHelpId),GetModuleHandleW(nullptr),nullptr);
        CreateGraphHelpTooltip(window);
        infoLabel=CreateWindowW(L"STATIC",L"",WS_CHILD|SS_LEFT,32,184,1020,28,window,reinterpret_cast<HMENU>(InfoId),GetModuleHandleW(nullptr),nullptr);
        commentButton=CreateWindowW(L"BUTTON",L"Comment",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,280,738,120,48,window,reinterpret_cast<HMENU>(CommentButtonId),GetModuleHandleW(nullptr),nullptr); EnableWindow(commentButton,FALSE);
        startButton=CreateWindowW(L"BUTTON",L"Start recording",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,870,738,160,48,window,reinterpret_cast<HMENU>(StartId),GetModuleHandleW(nullptr),nullptr);
        stopButton=CreateWindowW(L"BUTTON",L"Stop",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,1040,738,90,48,window,reinterpret_cast<HMENU>(StopId),GetModuleHandleW(nullptr),nullptr); EnableWindow(stopButton,FALSE);
        mocoHeading=CreateWindowW(L"STATIC",L"MICOS MoCo serial interface — commands may move or configure the motor; send only commands listed in its manual.",WS_CHILD|SS_LEFT,32,118,1060,34,window,reinterpret_cast<HMENU>(MocoHeadingId),GetModuleHandleW(nullptr),nullptr);
        mocoPortLabel=CreateWindowW(L"STATIC",L"COM port",WS_CHILD|SS_LEFT,32,174,80,28,window,reinterpret_cast<HMENU>(MocoPortLabelId),GetModuleHandleW(nullptr),nullptr);
        mocoPortInput=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,116,168,130,300,window,reinterpret_cast<HMENU>(MocoPortId),GetModuleHandleW(nullptr),nullptr);
        for(int port=1;port<=32;++port){ wchar_t name[16]{}; swprintf_s(name,L"COM%d",port); SendMessageW(mocoPortInput,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name)); }
        SendMessageW(mocoPortInput,CB_SETCURSEL,0,0);
        mocoBaudLabel=CreateWindowW(L"STATIC",L"Baud rate",WS_CHILD|SS_LEFT,274,174,80,28,window,reinterpret_cast<HMENU>(MocoBaudLabelId),GetModuleHandleW(nullptr),nullptr);
        mocoBaudInput=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,358,168,130,120,window,reinterpret_cast<HMENU>(MocoBaudId),GetModuleHandleW(nullptr),nullptr);
        const wchar_t* baudRates[]={L"9600",L"19200",L"34400",L"38400"};
        for(const wchar_t* rate:baudRates) SendMessageW(mocoBaudInput,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(rate));
        SendMessageW(mocoBaudInput,CB_SETCURSEL,3,0);
        mocoConnectButton=CreateWindowW(L"BUTTON",L"Connect",WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,510,164,120,40,window,reinterpret_cast<HMENU>(MocoConnectId),GetModuleHandleW(nullptr),nullptr);
        mocoDisconnectButton=CreateWindowW(L"BUTTON",L"Disconnect",WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,642,164,120,40,window,reinterpret_cast<HMENU>(MocoDisconnectId),GetModuleHandleW(nullptr),nullptr);
        profilingStartLabel=CreateWindowW(L"STATIC",L"Start",WS_CHILD|SS_LEFT,790,174,52,28,window,reinterpret_cast<HMENU>(ProfilingStartLabelId),GetModuleHandleW(nullptr),nullptr);
        profilingStartInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"0.0",WS_CHILD|ES_AUTOHSCROLL|WS_TABSTOP,846,168,120,40,window,reinterpret_cast<HMENU>(ProfilingStartInputId),GetModuleHandleW(nullptr),nullptr);
        mocoStatusLabel=CreateWindowW(L"STATIC",L"Disconnected",WS_CHILD|SS_LEFT,32,220,1020,28,window,reinterpret_cast<HMENU>(MocoStatusId),GetModuleHandleW(nullptr),nullptr);
        mocoCommandLabel=CreateWindowW(L"STATIC",L"MoCo ASCII command (sent with carriage return)",WS_CHILD|SS_LEFT,32,272,520,28,window,reinterpret_cast<HMENU>(MocoCommandLabelId),GetModuleHandleW(nullptr),nullptr);
        mocoCommandInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|WS_TABSTOP,32,304,650,40,window,reinterpret_cast<HMENU>(MocoCommandId),GetModuleHandleW(nullptr),nullptr);
        mocoSendButton=CreateWindowW(L"BUTTON",L"Send",WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,696,304,110,40,window,reinterpret_cast<HMENU>(MocoSendId),GetModuleHandleW(nullptr),nullptr);
        mocoConsoleLabel=CreateWindowW(L"STATIC",L"Controller response",WS_CHILD|SS_LEFT,32,360,520,28,window,reinterpret_cast<HMENU>(MocoConsoleLabelId),GetModuleHandleW(nullptr),nullptr);
        mocoConsole=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|WS_VSCROLL|WS_HSCROLL,32,390,1060,300,window,reinterpret_cast<HMENU>(MocoConsoleId),GetModuleHandleW(nullptr),nullptr);
        UpdateMocoConnectionControls(false);
        SetTimer(window,UiTimer,graphPanning ? 30 : 100,nullptr);
        return 0;
    }
    case WM_COMMAND:
        if((LOWORD(wParam)==CommentButtonId && HIWORD(wParam)==BN_CLICKED) || LOWORD(wParam)==CommentShortcutId) { ShowCommentDialog(window); return 0; }
        if(LOWORD(wParam)==MocoConnectId && HIWORD(wParam)==BN_CLICKED) {
            wchar_t port[32]{}, baudText[32]{};
            const LRESULT portIndex=SendMessageW(mocoPortInput,CB_GETCURSEL,0,0);
            const LRESULT baudIndex=SendMessageW(mocoBaudInput,CB_GETCURSEL,0,0);
            if(portIndex==CB_ERR || baudIndex==CB_ERR){ SetWindowTextW(mocoStatusLabel,L"Select a COM port and baud rate."); return 0; }
            SendMessageW(mocoPortInput,CB_GETLBTEXT,portIndex,reinterpret_cast<LPARAM>(port));
            SendMessageW(mocoBaudInput,CB_GETLBTEXT,baudIndex,reinterpret_cast<LPARAM>(baudText));
            const DWORD baud=static_cast<DWORD>(std::wcstoul(baudText,nullptr,10));
            std::wstring error;
            if(moco.Open(port,baud,error)){
                SetWindowTextW(mocoStatusLabel,(L"Connected to "+std::wstring(port)+L" at "+baudText+L" baud (8-N-1). No command sent automatically.").c_str());
                AppendMocoConsole(L"[Connected to "+std::wstring(port)+L" at "+baudText+L" baud]\r\n");
                WriteLog(L"Connected to MICOS MoCo serial port "+std::wstring(port)+L" at "+baudText+L" baud.");
                UpdateMocoConnectionControls(true);
            }else SetWindowTextW(mocoStatusLabel,error.c_str());
            return 0;
        }
        if(LOWORD(wParam)==MocoDisconnectId && HIWORD(wParam)==BN_CLICKED) {
            moco.Close(); SetWindowTextW(mocoStatusLabel,L"Disconnected");
            AppendMocoConsole(L"[Disconnected]\r\n"); WriteLog(L"Disconnected from MICOS MoCo."); UpdateMocoConnectionControls(false); return 0;
        }
        if(LOWORD(wParam)==MocoSendId && HIWORD(wParam)==BN_CLICKED) {
            wchar_t command[1024]{}; GetWindowTextW(mocoCommandInput,command,static_cast<int>(std::size(command)));
            std::wstring error;
            if(moco.SendAsciiCommand(command,error)) AppendMocoConsole(L"> "+std::wstring(command)+L"\r\n");
            else SetWindowTextW(mocoStatusLabel,error.c_str());
            return 0;
        }
        if(LOWORD(wParam)==StartId && HIWORD(wParam)==BN_CLICKED) StartRecording(window);
        if(LOWORD(wParam)==StopId && HIWORD(wParam)==BN_CLICKED) StopRecording();
        if(LOWORD(wParam)==DisplayLengthId && HIWORD(wParam)==EN_CHANGE && !displayLengthSync) {
            wchar_t text[64]{}; GetWindowTextW(displayLengthInput,text,64);
            wchar_t* end=nullptr; const double seconds=std::wcstod(text,&end);
            if(end!=text && *end==L'\0' && std::isfinite(seconds) && seconds>0.0 && seconds<=604800.0) displayLengthSeconds.store(seconds);
            if(!graphFollowingLive) ClampGraphViewToHistory(graphViewEndSeconds,displayLengthSeconds.load());
            graphCache.valid=false; RECT plot=GraphBounds(window); InvalidateRect(window,&plot,FALSE);
        }
        if(LOWORD(wParam)==FileButtonId && HIWORD(wParam)==BN_CLICKED && !recording.load()) {
            if(ChooseDataFile(window)) WriteLog(L"Ready to record to " + dataFilePath + L".");
        }
        return 0;
    case WM_TIMER:
        if(wParam==UiTimer) UpdateDataFileSize();
        if(wParam==UiTimer && activeTab==0) {
            SyncDisplayLengthField();
            if (graphPanning) {
                // Keep the dragged view refreshed from acquisition history even while
                // the user is holding the mouse still between move events.
                graphCache.valid = false;
                RenderGraphFully(window);
                const RECT bounds = GraphBounds(window);
                InvalidateRect(window, &bounds, FALSE);
            } else {
                UpdateGraphIncrementally(window);
            }
        } else if(wParam==UiTimer && activeTab==1 && moco.IsOpen()) {
            std::wstring response,error;
            if(!moco.ReadAvailable(response,error)){
                moco.Close(); SetWindowTextW(mocoStatusLabel,error.c_str()); UpdateMocoConnectionControls(false);
                AppendMocoConsole(L"[Serial connection closed after read error]\r\n");
            }else AppendMocoConsole(response);
        }
        return 0;
    case WM_LBUTTONDOWN:{
        const int x=static_cast<short>(LOWORD(lParam)), y=static_cast<short>(HIWORD(lParam));
        if(activeTab==0 && x>=32 && y>=190){ SetFocus(window); return 0; }
        return DefWindowProcW(window,message,wParam,lParam);
    }
    case WM_LBUTTONDBLCLK:{
        const int x=static_cast<short>(LOWORD(lParam)), y=static_cast<short>(HIWORD(lParam));
        if(activeTab==0 && x>=32 && y>=190){ graphFollowingLive=true; graphCache.manualYScale=false; graphCache.valid=false; RECT plot=GraphBounds(window); InvalidateRect(window,&plot,FALSE); return 0; }
        return DefWindowProcW(window,message,wParam,lParam);
    }
    case WM_RBUTTONDOWN:{
        if(activeTab==0 && EnsureGraphSurface(window)){
            const int x=static_cast<short>(LOWORD(lParam)), y=static_cast<short>(HIWORD(lParam));
            const RECT bounds=GraphBounds(window);
            const int left=bounds.left+72,right=bounds.left+graphCache.width-220;
            const int top=bounds.top+66,bottom=bounds.top+graphCache.height-62;
            if(x>=left && x<=right && y>=top && y<=bottom){
                if(!graphCache.valid) RenderGraphFully(window);
                graphPanning=true; graphPanLastX=x; graphPanLastY=y; SetTimer(window,UiTimer,30,nullptr); SetFocus(window); SetCapture(window); return 0;
            }
        }
        return DefWindowProcW(window,message,wParam,lParam);
    }
    case WM_MOUSEMOVE:{
        if(graphPanning){
            const int x=static_cast<short>(LOWORD(lParam)), y=static_cast<short>(HIWORD(lParam));
            const int dx=x-graphPanLastX, dy=y-graphPanLastY; graphPanLastX=x; graphPanLastY=y;
            if(dx!=0 || dy!=0){
                if(dx!=0){
                    if(graphFollowingLive){ graphViewEndSeconds=graphCache.viewEnd; graphFollowingLive=false; }
                    const int plotWidth=graphCache.width-220-72;
                    graphViewEndSeconds-=dx*displayLengthSeconds.load()/plotWidth;
                    double newest=0.0; ClampGraphViewToHistory(graphViewEndSeconds,displayLengthSeconds.load(),&newest);
                    if(graphViewEndSeconds>=newest-0.005) graphFollowingLive=true;
                }
                const double axisRange=graphCache.manualYScale ? graphCache.requestedMaxV-graphCache.requestedMinV : graphCache.maxV-graphCache.minV;
                if(!graphCache.manualYScale){
                    graphCache.requestedMinV=graphCache.minV; graphCache.requestedMaxV=graphCache.maxV; graphCache.manualYScale=true;
                }
                const int plotHeight=graphCache.height-66-62;
                const double voltageShift=dy*axisRange/plotHeight;
                graphCache.requestedMinV+=voltageShift; graphCache.requestedMaxV+=voltageShift;
                // Coalesce high-rate mouse messages; the 30 ms UI timer redraws
                // the latest requested pan position from the bounded graph history.
                graphCache.valid=false;
            }
            SetCursor(LoadCursorW(nullptr,IDC_SIZEALL)); return 0;
        }
        if(!trackingMouseLeave){
            TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};
            trackingMouseLeave=TrackMouseEvent(&tracking)!=FALSE;
        }
        if(activeTab==0 && !graphCache.valid && EnsureGraphSurface(window)) RenderGraphFully(window);
        UpdateCommentHover(window,POINT{static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))});
        return DefWindowProcW(window,message,wParam,lParam);
    }
    case WM_MOUSELEAVE:
        trackingMouseLeave=false;
        ClearCommentHover(window);
        return 0;
    case WM_RBUTTONUP:
        if(graphPanning){
            graphPanning=false; SetTimer(window,UiTimer,100,nullptr);
            graphCache.valid=false; RenderGraphFully(window);
            const RECT bounds=GraphBounds(window); InvalidateRect(window,&bounds,FALSE);
            ReleaseCapture(); return 0;
        }
        return DefWindowProcW(window,message,wParam,lParam);
    case WM_CAPTURECHANGED:
        graphPanning=false; SetTimer(window,UiTimer,100,nullptr); return 0;
    case WM_MOUSEWHEEL:{
        ClearCommentHover(window);
        POINT point{static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))};
        ScreenToClient(window,&point);
        if(activeTab==0) EnsureGraphSurface(window);
        const RECT graphBounds=GraphBounds(window);
        const int plotLeft=graphBounds.left+72, plotRight=graphBounds.right-220;
        const int plotTop=graphBounds.top+66, plotBottom=graphBounds.top+graphCache.height-62;
        if(activeTab==0 && point.x>=plotLeft && point.x<=plotRight && point.y>=plotTop && point.y<=plotBottom){
            const int steps=GET_WHEEL_DELTA_WPARAM(wParam)/WHEEL_DELTA;
            const bool controlHeld=(GET_KEYSTATE_WPARAM(wParam)&MK_CONTROL)!=0;
            if(controlHeld){
                double seconds=displayLengthSeconds.load()*std::pow(0.8,steps);
                seconds=(std::max)(0.001,(std::min)(604800.0,seconds)); displayLengthSeconds.store(seconds);
                if(!graphFollowingLive) ClampGraphViewToHistory(graphViewEndSeconds,seconds);
                SyncDisplayLengthField();
            }else{
                if(!graphCache.valid) RenderGraphFully(window);
                const int top=graphBounds.top+66, bottom=graphBounds.top+graphCache.height-62;
                const double fraction=(std::max)(0.0,(std::min)(1.0,(point.y-top)/static_cast<double>(bottom-top)));
                const double anchor=graphCache.maxV-fraction*(graphCache.maxV-graphCache.minV);
                const double factor=std::pow(0.8,steps);
                double minV=anchor+(graphCache.minV-anchor)/factor;
                double maxV=anchor+(graphCache.maxV-anchor)/factor;
                if(maxV-minV<1e-6){const double center=(minV+maxV)*0.5; minV=center-5e-7; maxV=center+5e-7;}
                if(maxV-minV>1000.0){const double center=(minV+maxV)*0.5; minV=center-500.0; maxV=center+500.0;}
                graphCache.requestedMinV=minV; graphCache.requestedMaxV=maxV; graphCache.manualYScale=true;
            }
            graphCache.valid=false; RECT plot=GraphBounds(window); InvalidateRect(window,&plot,FALSE);
            return 0;
        }
        return DefWindowProcW(window,message,wParam,lParam);
    }
    case WM_APP+2:
        recording.store(false);
        if(!closeAfterAcquisition){ EnableWindow(startButton,TRUE); EnableWindow(stopButton,FALSE); EnableWindow(frequencyInput,TRUE); EnableWindow(fileButton,TRUE); EnableWindow(commentButton,FALSE); }
        return 0;
    case WM_APP+3:
        EnableWindow(commentButton,TRUE);
        return 0;
    case CommentMarkersChangedMessage:{
        ClearCommentHover(window);
        graphCache.valid=false;
        const RECT bounds=GraphBounds(window);
        InvalidateRect(window,&bounds,FALSE);
        return 0;
    }
    case AcquisitionStoppedMessage:
        if(closeAfterAcquisition){
            if(acquisitionThread.joinable()) acquisitionThread.join();
            recording.store(false);
            DestroyWindow(window);
        }
        return 0;
    case WM_NOTIFY:
        if(reinterpret_cast<NMHDR*>(lParam)->idFrom==TabsId && reinterpret_cast<NMHDR*>(lParam)->code==TCN_SELCHANGE) SetView(window,TabCtrl_GetCurSel(tabs));
        return 0;
    case WM_SIZE:{
        if (wParam == SIZE_MINIMIZED) return 0;
        ClearCommentHover(window);
        int w=LOWORD(lParam),h=HIWORD(lParam);
        LayoutStatusBar(window);
        graphCache.valid=false;
        MoveWindow(tabs,20,18,w-40,46,TRUE);
        MoveWindow(frequencyLabel,w-550,h-104,150,34,TRUE); MoveWindow(frequencyInput,w-390,h-108,80,40,TRUE);
        MoveWindow(displayLengthLabel,32,h-104,130,34,TRUE); MoveWindow(displayLengthInput,164,h-108,90,40,TRUE);
        MoveWindow(fileButton,32,118,176,40,TRUE); MoveWindow(infoLabel,32,184,w-64,28,TRUE);
        MoveWindow(graphHelpButton,w-76,196,28,28,TRUE);
        MoveWindow(mocoHeading,32,118,w-64,34,TRUE);
        MoveWindow(mocoPortLabel,32,174,80,28,TRUE); MoveWindow(mocoPortInput,116,168,130,300,TRUE);
        MoveWindow(mocoBaudLabel,274,174,80,28,TRUE); MoveWindow(mocoBaudInput,358,168,130,120,TRUE);
        MoveWindow(mocoConnectButton,510,164,120,40,TRUE); MoveWindow(mocoDisconnectButton,642,164,120,40,TRUE);
        MoveWindow(profilingStartLabel,790,174,52,28,TRUE); MoveWindow(profilingStartInput,846,168,120,40,TRUE);
        MoveWindow(mocoStatusLabel,32,220,w-64,28,TRUE); MoveWindow(mocoCommandLabel,32,272,w-64,28,TRUE);
        MoveWindow(mocoCommandInput,32,304,w-478,40,TRUE); MoveWindow(mocoSendButton,w-430,304,110,40,TRUE);
        MoveWindow(mocoConsoleLabel,32,360,w-64,28,TRUE); MoveWindow(mocoConsole,32,390,w-64,h-520,TRUE);
        MoveWindow(commentButton,280,h-112,120,48,TRUE);
        MoveWindow(startButton,w-290,h-112,160,48,TRUE); MoveWindow(stopButton,w-120,h-112,90,48,TRUE);
        { RECT plot=GraphBounds(window); InvalidateRect(window,&plot,FALSE); }
        return 0;
    }
    case WM_GETMINMAXINFO:{
        // Keep the bottom controls and the graph's plotting area usable while
        // resizing. Track sizes are window sizes, so account for the current
        // caption and border styles rather than assuming a fixed frame width.
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        RECT minimumClient{0, 0, 1000, 700};
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
        if (AdjustWindowRectEx(&minimumClient, style, FALSE, exStyle)) {
            limits->ptMinTrackSize.x = minimumClient.right - minimumClient.left;
            limits->ptMinTrackSize.y = minimumClient.bottom - minimumClient.top;
        }
        return 0;
    }
    case WM_CLOSE:
        if(closeAfterAcquisition) return 0;
        closeAfterAcquisition = true;
        moco.Close();
        shutdownRequested.store(true);
        SetWindowTextW(window,L"Tasker — closing acquisition safely...");
        EnableWindow(startButton,FALSE); EnableWindow(stopButton,FALSE); EnableWindow(fileButton,FALSE); EnableWindow(commentButton,FALSE);
        if(!acquisitionAlive.load()){
            if(acquisitionThread.joinable()) acquisitionThread.join();
            recording.store(false);
            DestroyWindow(window);
        }
        return 0;
    case WM_DESTROY: KillTimer(window,UiTimer); moco.Close(); DestroyGraphSurface(); PostQuitMessage(0); return 0;
    default:return DefWindowProcW(window,message,wParam,lParam);
    }
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int showCommand){
    WNDCLASSW wc{}; wc.style=CS_DBLCLKS; wc.lpfnWndProc=WindowProcedure; wc.hInstance=instance; wc.lpszClassName=WindowClassName; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassW(&wc)) return 1;
    HWND window=CreateWindowExW(0,WindowClassName,L"Tasker",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1160,850,nullptr,nullptr,instance,nullptr);
    if(!window)return 1;
    ACCEL commentShortcut{static_cast<BYTE>(FVIRTKEY | FCONTROL), static_cast<WORD>('K'), static_cast<WORD>(CommentShortcutId)};
    HACCEL accelerators=CreateAcceleratorTableW(&commentShortcut,1);
    ShowWindow(window,showCommand); UpdateWindow(window);
    // Show the main window before opening the modal file picker. Opening it
    // during WM_CREATE can leave the owner hidden behind the dialog.
    if (ChooseDataFile(window)) WriteLog(L"Ready to record to " + dataFilePath + L".");
    else MessageBoxW(window, L"No storage file selected. Choose one before starting recording.", L"Tasker", MB_OK | MB_ICONINFORMATION);
    StartAcquisition(window);
    MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0){if(!accelerators || !TranslateAcceleratorW(window,accelerators,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(accelerators) DestroyAcceleratorTable(accelerators);
    return static_cast<int>(msg.wParam);
}





