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
#include <cerrno>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
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
#include "mcc_usb1608fsplus_v1.29.h"
#include "micos_moco_interface_v1.5.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")

namespace {
enum ControlId { TabsId=100, InfoId, ChartId, StartId, StopId, FrequencyId, FrequencyLabelId, FileButtonId, DisplayLengthId, DisplayLengthLabelId, CommentButtonId, CommentShortcutId,
                 MocoHeadingId, MocoPortLabelId, MocoPortId, MocoBaudLabelId, MocoBaudId, MocoAddressLabelId, MocoAddressId, MocoConnectId, MocoDisconnectId, MocoAdvancedId, MocoStatusId,
                 MocoPositionLabelId, MocoPositionInputId, StepLabelId, StepInputId, StepUpId, StepDownId, ScaleLabelId, ScaleInputId,
                 MocoCommandLabelId, MocoCommandId, MocoSendId, MocoConsoleLabelId, MocoConsoleId,
                 ProfilingStartLabelId, ProfilingStartInputId, StatusBarId, GraphHelpId, GraphBackId,
                 ProfileSettingsHeadingId, ProfileEndLabelId, ProfileEndInputId, ProfileStepLabelId, ProfileStepInputId, ProfileSafeLabelId, ProfileSafeInputId, ManualControlHeadingId,
                 GoToLabelId, GoToInputId, GoToButtonId, SetPositionLabelId, SetPositionInputId, SetPositionButtonId, ProfileWaitLabelId, ProfileWaitInputId, ProfileMeasureLabelId, ProfileMeasureInputId, ProfileOvershootLabelId, ProfileOvershootInputId, ProfileOvershootWaitLabelId, ProfileOvershootWaitInputId, ProfileRunButtonId };
constexpr UINT_PTR UiTimer = 1;
constexpr UINT RecordingFinishedMessage = WM_APP + 2;
constexpr UINT RecordingStartedMessage = WM_APP + 3;
constexpr UINT CommentMarkersChangedMessage = WM_APP + 4;
constexpr UINT AcquisitionStoppedMessage = WM_APP + 5;
constexpr UINT ProfileFinishedMessage = WM_APP + 6;
constexpr UINT ProfilePlotUpdatedMessage = WM_APP + 7;
constexpr int CalibrationControlBaseId = 6000;
constexpr int CalibrationControlStride = 8;
constexpr int CalibrationPlotToggleBaseId = 6200;
constexpr int CalibrationLiveLabelBaseId = 6300;
constexpr size_t MaxGraphSamples = 120000;
constexpr wchar_t WindowClassName[] = L"TaskerWindowClass";

HWND tabs, infoLabel, startButton, stopButton, frequencyInput, frequencyLabel, fileButton, displayLengthInput, displayLengthLabel, commentButton, statusBar, graphHelpButton, graphBackButton, graphToolTip;
HWND mocoHeading, mocoPortLabel, mocoPortInput, mocoBaudLabel, mocoBaudInput, mocoAddressLabel, mocoAddressInput, mocoConnectButton, mocoDisconnectButton, mocoAdvancedButton, mocoStatusLabel;
HWND mocoPositionLabel, mocoPositionInput, stepLabel, stepInput, stepUpButton, stepDownButton, scaleLabel, scaleInput;
HFONT arrowButtonFont = nullptr;
HWND profileSettingsHeading, profileEndLabel, profileEndInput, profileStepLabel, profileStepInput, profileSafeLabel, profileSafeInput, manualControlHeading;
HWND goToLabel, goToInput, goToButton, setPositionLabel, setPositionInput, setPositionButton, profileWaitLabel, profileWaitInput, profileMeasureLabel, profileMeasureInput, profileOvershootLabel, profileOvershootInput, profileOvershootWaitLabel, profileOvershootWaitInput, profileRunButton;
HWND mocoCommandLabel, mocoCommandInput, mocoSendButton, mocoConsoleLabel, mocoConsole;
HWND profilingStartLabel, profilingStartInput;
HWND calibrationColumnLabels[5]{};
HWND calibrationChannelLabels[8]{};
HWND calibrationConcentrationInputs[8]{};
HWND calibrationGetButtons[8]{};
HWND calibrationClearButtons[8]{};
HWND calibrationApplyButtons[8]{};
HWND calibrationSlopeFields[8]{};
HWND calibrationInterceptFields[8]{};
HWND calibrationR2Fields[8]{};
HWND calibrationPlotToggles[8]{};
HWND calibrationLiveValueLabels[8]{};
int activeTab = 0;
HWND channelsHeading, channelGridHeaders[4]{}, channelHardwareLabels[8]{}, channelSelectFields[8]{}, channelNameFields[8]{}, channelRangeFields[8]{};
constexpr int ChannelSelectBaseId = 7500, ChannelNameBaseId = 7520, ChannelRangeBaseId = 7540;
constexpr int channelRangeCodes[] = {1, 0, 14, 4};
std::array<std::atomic<bool>, 8> selectedChannels{};
std::array<std::atomic<int>, 8> channelRanges{};
std::array<std::wstring, 8> userChannelNames{};
std::atomic<unsigned long long> channelSettingsGeneration{0};
bool channelControlsReady=false;
bool ChannelEnabled(size_t channel) { return selectedChannels[channel].load(); }
std::wstring FullChannelName(size_t channel) { return L"CH" + std::to_wstring(channel) + L" – " + userChannelNames[channel]; }
std::wstring dataFilePath;
std::wstring savedComPort = L"COM1";
std::filesystem::path configFilePath;
std::filesystem::path logFilePath;
std::mutex logMutex;
std::thread acquisitionThread;
std::atomic<bool> shutdownRequested{false};
std::atomic<bool> acquisitionAlive{false};
std::atomic<bool> daqConnected{false};
std::atomic<bool> startRecordingRequested{false};
std::atomic<bool> stopRecordingRequested{false};
std::atomic<bool> recording{false};
std::mutex dataMutex, commandMutex, mocoMutex;
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
struct ProfilePlotPoint { double positionMicrons; std::array<double, 8> signals; };
std::deque<SignalSample> history;
std::vector<GraphComment> graphComments;
std::mutex profilePlotMutex;
struct ProfileDataset {
    unsigned long long number;
    std::wstring label;
    std::vector<ProfilePlotPoint> points;
    std::array<bool, 8> visible{{true,true,true,true,true,true,true,true}};
};
std::vector<ProfileDataset> profileDatasets;
HWND profileLegendWindow=nullptr;
bool profileGraphCalibrated=false;
int profileLegendFirst=0;
int profileLegendFocusRow=-1, profileLegendFocusColumn=0;
RECT ProfileLegendBounds(HWND window);
void RefreshProfileLegend(HWND window);
unsigned long long nextProfileDatasetNumber = 1;
HWND profileCyclesLabel, profileCyclesInput, profileLabelCaption, profileLabelInput;
HWND profileCycleIntervalLabel, profileCycleIntervalInput;
constexpr int ProfileCyclesLabelId = 7400, ProfileCyclesInputId = 7401;
unsigned long long nextSampleSequence = 0;
std::atomic<double> displayLengthSeconds{10.0};
bool graphFollowingLive = true;
bool closeAfterAcquisition = false;
bool graphPanning = false;
bool graphPanMoved = false;
bool graphZoomSelecting = false;
POINT graphZoomStart{}, graphZoomCurrent{};
bool trackingMouseLeave = false;
bool hoveringGraphComment = false;
POINT hoveredCommentAnchor{};
RECT hoveredCommentTooltipRect{};
std::wstring hoveredCommentText;
bool displayLengthSync = false;
int graphPanLastX = 0, graphPanLastY = 0;
double graphViewEndSeconds = 0.0;
std::atomic<long long> mocoCurrentPosition{0};
std::atomic<bool> mocoCurrentPositionValid{false};
std::atomic<double> softwarePositionOffsetMicrons{0.0};
std::thread profileThread;
std::atomic<bool> profileRunning{false};
std::atomic<bool> profileCancelRequested{false};
std::atomic<size_t> profileStepIndex{0}, profileStepCount{0};
bool closeAfterProfile = false;
bool numericSettingsSaved = false;
std::mutex profileResultMutex, profileCaptureMutex;
std::wstring profileResult;
struct ProfileCapture {
    bool active = false;
    std::chrono::steady_clock::time_point start{}, end{};
    std::array<double, 8> sums{};
    unsigned long long samples = 0;
} profileCapture;
bool mocoPositionQueryPending = false;
std::chrono::steady_clock::time_point mocoPositionQuerySent{};
std::chrono::steady_clock::time_point nextMocoPositionQuery{};
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
struct ZoomState {
    double viewSeconds = 10.0, viewEnd = 0.0, minV = -1.0, maxV = 1.0;
    bool manualYScale = false, followingLive = true;
};
std::deque<ZoomState> graphZoomHistory;
struct CalibrationGraphCache {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    int width = 0, height = 0;
    bool valid = false;
    double viewSeconds = 0.0, viewEnd = 0.0, minV = -1.0, maxV = 1.0;
    unsigned long long lastSequence = 0;
    std::array<POINT, 8> lastPoints{};
    std::array<bool, 8> hasLastPoint{};
} calibrationGraphCache;
MccUsb1608FsPlus daq;
MicosMocoInterface moco;
const wchar_t* tabNames[] = { L"Signals", L"Profiling", L"Calibration", L"Completed", L"Settings" };
const COLORREF colors[] = { RGB(25,116,210), RGB(226,94,62), RGB(42,155,99), RGB(160,92,190), RGB(226,167,42), RGB(43,163,174), RGB(212,89,142), RGB(104,118,139) };
struct CalibrationSample { std::wstring timestamp; double concentration = 0.0, signal = 0.0; };
struct AppliedCalibration { bool valid = false; double slope = 0.0, intercept = 0.0; };
double ProfileSignalValue(double raw, size_t channel, const std::array<AppliedCalibration,8>& calibrations) {
    if (!profileGraphCalibrated) return raw;
    const auto& calibration=calibrations[channel];
    if (!calibration.valid || !std::isfinite(calibration.slope) || std::abs(calibration.slope)<1e-15)
        return std::numeric_limits<double>::quiet_NaN();
    return (raw-calibration.intercept)/calibration.slope;
}
std::array<std::vector<CalibrationSample>, 8> calibrationData;
std::array<AppliedCalibration, 8> appliedCalibrations{};
std::mutex calibrationMutex;
std::filesystem::path calibrationFilePath;
std::wstring calibrationLoadedDataFile;
bool calibrationLiveManualYScale = false;
double calibrationLiveMinV = -0.01, calibrationLiveMaxV = 0.01;
void ClearCommentHover(HWND window);
RECT ProfileGraphBounds(HWND window);
void LoadCalibrationData();
bool ReadFiniteNumber(HWND input, double& value);
void UpdateCalibrationPlotControls();
void UpdateCalibrationLiveValues();
RECT CalibrationLiveGraphBounds(HWND window);
RECT CalibrationCurveGraphBounds(HWND window);
void PaintCalibrationGraphs(HWND window, HDC dc);
bool EnsureCalibrationGraphSurface(HWND window);
void RenderCalibrationGraphsCached(HWND window);
void UpdateCalibrationGraphIncrementally(HWND window);
double NiceAxisStep(double rawStep);

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

void LoadConfiguration() {
    wchar_t executable[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (!length || length >= std::size(executable)) return;
    configFilePath = std::filesystem::path(executable).parent_path() / L"Tasker_v1.7.ini";
    wchar_t value[32768]{};
    GetPrivateProfileStringW(L"Tasker", L"ComPort", L"COM1", value, static_cast<DWORD>(std::size(value)), configFilePath.c_str());
    savedComPort = value;
    GetPrivateProfileStringW(L"Tasker", L"DataFile", L"", value, static_cast<DWORD>(std::size(value)), configFilePath.c_str());
    dataFilePath = value;
    GetPrivateProfileStringW(L"Tasker", L"PositionOffsetMicrons", L"0", value, static_cast<DWORD>(std::size(value)), configFilePath.c_str());
    wchar_t* end = nullptr;
    const double offset = std::wcstod(value, &end);
    if (end != value && !*end && std::isfinite(offset)) softwarePositionOffsetMicrons.store(offset);
}

void RestoreNumericField(HWND control, const wchar_t* key) {
    if (!control || configFilePath.empty()) return;
    wchar_t value[128]{};
    GetPrivateProfileStringW(L"NumericFields", key, L"\x01", value,
        static_cast<DWORD>(std::size(value)), configFilePath.c_str());
    if (value[0] != L'\x01') SetWindowTextW(control, value);
}

void RestoreNumericSettings() {
    RestoreNumericField(frequencyInput, L"RecordingIntervalSeconds");
    RestoreNumericField(displayLengthInput, L"DisplayLengthSeconds");
    RestoreNumericField(scaleInput, L"EncoderIncrementsPerMicron");
    RestoreNumericField(stepInput, L"ManualStepMicrons");
    RestoreNumericField(goToInput, L"GoToMicrons");
    RestoreNumericField(setPositionInput, L"SetPositionMicrons");
    RestoreNumericField(profilingStartInput, L"ProfileStartMicrons");
    RestoreNumericField(profileEndInput, L"ProfileEndMicrons");
    RestoreNumericField(profileStepInput, L"ProfileStepMicrons");
    RestoreNumericField(profileSafeInput, L"ProfileSafeMicrons");
    RestoreNumericField(profileWaitInput, L"ProfileWaitSeconds");
    RestoreNumericField(profileMeasureInput, L"ProfileMeasureSeconds");
    RestoreNumericField(profileOvershootInput, L"ProfileOvershootMicrons");
    RestoreNumericField(profileOvershootWaitInput, L"ProfileOvershootWaitSeconds");
    RestoreNumericField(profileCyclesInput, L"ProfileCycles");
    RestoreNumericField(profileCycleIntervalInput, L"ProfileCycleIntervalSeconds");
}

void SaveNumericSettings() {
    if (numericSettingsSaved || configFilePath.empty()) return;
    numericSettingsSaved = true;
    const std::pair<HWND, const wchar_t*> fields[] = {
        {frequencyInput, L"RecordingIntervalSeconds"}, {displayLengthInput, L"DisplayLengthSeconds"},
        {scaleInput, L"EncoderIncrementsPerMicron"}, {stepInput, L"ManualStepMicrons"},
        {goToInput, L"GoToMicrons"}, {setPositionInput, L"SetPositionMicrons"},
        {profilingStartInput, L"ProfileStartMicrons"}, {profileEndInput, L"ProfileEndMicrons"},
        {profileStepInput, L"ProfileStepMicrons"}, {profileSafeInput, L"ProfileSafeMicrons"},
        {profileWaitInput, L"ProfileWaitSeconds"}, {profileMeasureInput, L"ProfileMeasureSeconds"},
        {profileOvershootInput, L"ProfileOvershootMicrons"}, {profileOvershootWaitInput, L"ProfileOvershootWaitSeconds"},
        {profileCyclesInput, L"ProfileCycles"}, {profileCycleIntervalInput, L"ProfileCycleIntervalSeconds"}
    };
    for (const auto& field : fields) {
        if (!field.first) continue;
        wchar_t value[128]{};
        GetWindowTextW(field.first, value, static_cast<int>(std::size(value)));
        WritePrivateProfileStringW(L"NumericFields", field.second, value, configFilePath.c_str());
    }
}

void CreateChannelSettings(HWND window) {
    channelsHeading = CreateWindowW(L"STATIC", L"Channels", WS_CHILD, 520,118,650,34,window,nullptr,GetModuleHandleW(nullptr),nullptr);
    const wchar_t* headings[] = {L"Hardware",L"Selected",L"User channel name",L"Voltage range"};
    const int x[] = {520,610,710,990};
    const int widths[] = {85,90,270,160};
    for (int column=0;column<4;++column) channelGridHeaders[column] = CreateWindowW(L"STATIC",headings[column],WS_CHILD,x[column],160,widths[column],28,window,nullptr,GetModuleHandleW(nullptr),nullptr);
    for (int channel=0;channel<8;++channel) {
        const std::wstring key = L"CH" + std::to_wstring(channel);
        wchar_t name[256]{};
        GetPrivateProfileStringW(L"Channels",(key+L"Name").c_str(),L"",name,256,configFilePath.c_str());
        userChannelNames[channel] = name;
        selectedChannels[channel] = GetPrivateProfileIntW(L"Channels",(key+L"Selected").c_str(),1,configFilePath.c_str()) != 0;
        const int range = GetPrivateProfileIntW(L"Channels",(key+L"Range").c_str(),1,configFilePath.c_str());
        int choice=0;
        for (int i=0;i<4;++i) if(channelRangeCodes[i]==range) choice=i;
        channelRanges[channel]=channelRangeCodes[choice];
        const int y=194+channel*42;
        channelHardwareLabels[channel]=CreateWindowW(L"STATIC",key.c_str(),WS_CHILD,520,y+5,85,28,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        channelSelectFields[channel]=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX,630,y,32,32,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ChannelSelectBaseId+channel)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(channelSelectFields[channel],BM_SETCHECK,ChannelEnabled(channel)?BST_CHECKED:BST_UNCHECKED,0);
        channelNameFields[channel]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",name,WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,710,y,270,32,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ChannelNameBaseId+channel)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(channelNameFields[channel],EM_SETLIMITTEXT,80,0);
        channelRangeFields[channel]=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,990,y,160,180,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ChannelRangeBaseId+channel)),GetModuleHandleW(nullptr),nullptr);
        for(const wchar_t* label:{L"±10 V",L"±5 V",L"±2 V",L"±1 V"}) SendMessageW(channelRangeFields[channel],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(channelRangeFields[channel],CB_SETCURSEL,choice,0);
    }
    channelControlsReady=true;
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
        L"Drag down and right with left mouse: zoom to the selected rectangle.\n"
        L"Drag up and left with left mouse: return to the previous zoom.\n"
        L"Mouse wheel: zoom voltage around the cursor. Ctrl + wheel: zoom time.\n"
        L"Right-drag: pan time and voltage. Right-click: previous zoom.\n"
        L"Magnifier/back button: previous zoom. Double-click: return to live view.\n"
        L"Display length (s): set the time span shown.";
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = window;
    tool.uId = reinterpret_cast<UINT_PTR>(graphHelpButton);
    tool.lpszText = instructions;
    SendMessageW(graphToolTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    if (graphBackButton) {
        static wchar_t backInstructions[] = L"Return to the previous Signals graph zoom setting.";
        tool.uId = reinterpret_cast<UINT_PTR>(graphBackButton);
        tool.lpszText = backInstructions;
        SendMessageW(graphToolTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
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

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
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
    if (!configFilePath.empty()) WritePrivateProfileStringW(L"Tasker", L"DataFile", dataFilePath.c_str(), configFilePath.c_str());
    if (!InitializeLogFile(dataFilePath)) {
        MessageBoxW(owner, L"Tasker could not create the log file beside the selected storage file.", L"Tasker", MB_OK | MB_ICONERROR);
        dataFilePath.clear();
        return false;
    }
    dataFileGeneration.fetch_add(1);
    UpdateDataFileSize();
    LoadCalibrationData();
    WriteLog(L"Selected storage file: " + dataFilePath);
    return true;
}

std::filesystem::path CalibrationPathForDataFile(const std::wstring& path) {
    const std::filesystem::path source(path);
    return source.parent_path() / (source.stem().wstring() + L"_calibration.txt");
}

void DisplayCalibrationFit(size_t channel) {
    std::vector<CalibrationSample> samples;
    {
        std::lock_guard<std::mutex> lock(calibrationMutex);
        samples = calibrationData[channel];
    }
    SetWindowTextW(calibrationSlopeFields[channel], L"");
    SetWindowTextW(calibrationInterceptFields[channel], L"");
    SetWindowTextW(calibrationR2Fields[channel], L"");
    if (samples.size() < 2) return;

    long double meanX = 0.0L, meanY = 0.0L;
    for (const auto& sample : samples) { meanX += sample.concentration; meanY += sample.signal; }
    meanX /= static_cast<long double>(samples.size());
    meanY /= static_cast<long double>(samples.size());
    long double xx = 0.0L, xy = 0.0L, yy = 0.0L;
    for (const auto& sample : samples) {
        const long double dx = sample.concentration - meanX;
        const long double dy = sample.signal - meanY;
        xx += dx * dx; xy += dx * dy; yy += dy * dy;
    }
    if (xx <= std::numeric_limits<long double>::epsilon()) return;
    const long double slope = xy / xx;
    const long double intercept = meanY - slope * meanX;
    wchar_t value[64]{};
    swprintf_s(value, L"%.9g", static_cast<double>(slope));
    SetWindowTextW(calibrationSlopeFields[channel], value);
    swprintf_s(value, L"%.9g", static_cast<double>(intercept));
    SetWindowTextW(calibrationInterceptFields[channel], value);
    if (yy > std::numeric_limits<long double>::epsilon()) {
        long double residual = 0.0L;
        for (const auto& sample : samples) {
            const long double error = sample.signal - (slope * sample.concentration + intercept);
            residual += error * error;
        }
        double rSquared = static_cast<double>(1.0L - residual / yy);
        rSquared = (std::max)(0.0, (std::min)(1.0, rSquared));
        swprintf_s(value, L"%.9g", rSquared);
        SetWindowTextW(calibrationR2Fields[channel], value);
    }
}

void RefreshCalibrationFits() {
    for (size_t channel = 0; channel < calibrationData.size(); ++channel) DisplayCalibrationFit(channel);
}

void UpdateCalibrationPlotControls(bool selectAvailable) {
    if (!calibrationPlotToggles[0]) return;
    for (size_t channel = 0; channel < calibrationData.size(); ++channel) {
        size_t count = 0;
        { std::lock_guard<std::mutex> lock(calibrationMutex); count = calibrationData[channel].size(); }
        if (count == 0) {
            SendMessageW(calibrationPlotToggles[channel], BM_SETCHECK, BST_UNCHECKED, 0);
            EnableWindow(calibrationPlotToggles[channel], FALSE);
        } else {
            EnableWindow(calibrationPlotToggles[channel], TRUE);
            if (selectAvailable) SendMessageW(calibrationPlotToggles[channel], BM_SETCHECK, BST_CHECKED, 0);
        }
    }
}

void UpdateCalibrationLiveValues() {
    std::array<float, 8> values{};
    bool valid = false;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        if (!history.empty()) { values = history.back().volts; valid = true; }
    }
    for (size_t channel = 0; channel < values.size(); ++channel) {
        wchar_t text[48]{};
        if (valid) swprintf_s(text, L"%.4g V", static_cast<double>(values[channel]));
        else wcscpy_s(text, L"— V");
        if (calibrationLiveValueLabels[channel]) {
            wchar_t previous[48]{};
            GetWindowTextW(calibrationLiveValueLabels[channel], previous, static_cast<int>(std::size(previous)));
            if (wcscmp(previous, text) != 0) SetWindowTextW(calibrationLiveValueLabels[channel], text);
        }
    }
}

void LoadCalibrationData() {
    {
        std::lock_guard<std::mutex> lock(calibrationMutex);
        for (auto& channel : calibrationData) channel.clear();
        for (auto& calibration : appliedCalibrations) calibration = {};
        calibrationLoadedDataFile = dataFilePath;
        calibrationFilePath = dataFilePath.empty() ? std::filesystem::path{} : CalibrationPathForDataFile(dataFilePath);
        if (!calibrationFilePath.empty()) {
            std::ifstream input(calibrationFilePath, std::ios::in);
            std::string line;
            while (std::getline(input, line)) {
                std::stringstream row(line);
                std::string timestamp, channelText, concentrationText, signalText;
                if (!std::getline(row, timestamp, '\t') || !std::getline(row, channelText, '\t') ||
                    !std::getline(row, concentrationText, '\t') || !std::getline(row, signalText, '\t')) continue;
                try {
                    const int channel = std::stoi(channelText);
                    const double concentration = std::stod(concentrationText);
                    const double signal = std::stod(signalText);
                    if (channel >= 0 && channel < static_cast<int>(calibrationData.size()) &&
                        std::isfinite(concentration) && std::isfinite(signal)) {
                        calibrationData[static_cast<size_t>(channel)].push_back({Utf8ToWide(timestamp), concentration, signal});
                    }
                } catch (...) { }
            }
        }
    }
    if (calibrationSlopeFields[0]) {
        RefreshCalibrationFits();
        UpdateCalibrationPlotControls(true);
        RECT plot = CalibrationCurveGraphBounds(GetParent(calibrationSlopeFields[0]));
        InvalidateRect(GetParent(calibrationSlopeFields[0]), &plot, TRUE);
    }
}

bool AddCalibrationSample(size_t channel, double concentration, double signal, std::wstring& error) {
    if (!ChannelEnabled(channel)) { error=L"Select this channel in Settings before capturing calibration data."; return false; }
    std::lock_guard<std::mutex> lock(calibrationMutex);
    if (calibrationFilePath.empty() || calibrationLoadedDataFile != dataFilePath) {
        error = L"Choose a storage file on the Signals tab first.";
        return false;
    }
    std::error_code fileError;
    const bool needsHeader = !std::filesystem::exists(calibrationFilePath, fileError) || std::filesystem::file_size(calibrationFilePath, fileError) == 0;
    std::ofstream output(calibrationFilePath, std::ios::out | std::ios::app);
    if (!output) { error = L"Could not open the calibration data file for writing."; return false; }
    if (needsHeader) output << "timestamp_local\tchannel\tconcentration\tsignal_V\n";
    const std::wstring timestamp = LocalTimestamp(L"%Y-%m-%d %H:%M:%S");
    output << WideToUtf8(timestamp) << '\t' << channel << '\t' << std::setprecision(12) << concentration << '\t' << signal << '\n';
    output.flush();
    if (!output) { error = L"Could not write the calibration sample."; return false; }
    calibrationData[channel].push_back({timestamp, concentration, signal});
    appliedCalibrations[channel].valid = false;
    return true;
}

bool ClearCalibrationChannel(size_t channel, std::wstring& error) {
    std::lock_guard<std::mutex> lock(calibrationMutex);
    if (calibrationFilePath.empty() || calibrationLoadedDataFile != dataFilePath) {
        calibrationData[channel].clear();
        appliedCalibrations[channel] = {};
        return true;
    }
    auto temporary = calibrationFilePath;
    temporary += L".tmp";
    std::ofstream output(temporary, std::ios::out | std::ios::trunc);
    if (!output) { error = L"Could not create a temporary calibration file."; return false; }
    output << "timestamp_local\tchannel\tconcentration\tsignal_V\n";
    for (size_t other = 0; other < calibrationData.size(); ++other) {
        if (other == channel) continue;
        for (const auto& sample : calibrationData[other])
            output << WideToUtf8(sample.timestamp) << '\t' << other << '\t' << std::setprecision(12)
                   << sample.concentration << '\t' << sample.signal << '\n';
    }
    output.flush(); output.close();
    if (!output || !MoveFileExW(temporary.c_str(), calibrationFilePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored; std::filesystem::remove(temporary, ignored);
        error = L"Could not replace the calibration data file.";
        return false;
    }
    calibrationData[channel].clear();
    appliedCalibrations[channel] = {};
    return true;
}

void GetCalibrationSignal(HWND window, size_t channel) {
    double concentration = 0.0;
    if (!ReadFiniteNumber(calibrationConcentrationInputs[channel], concentration)) {
        MessageBoxW(window, L"Enter a valid concentration before getting a signal.", L"Calibration", MB_OK | MB_ICONWARNING);
        SetFocus(calibrationConcentrationInputs[channel]);
        return;
    }
    if (!daqConnected.load() || !acquisitionAlive.load()) {
        MessageBoxW(window, L"The DAQ must be connected and acquiring live signals before calibration samples can be captured.", L"Calibration", MB_OK | MB_ICONWARNING);
        return;
    }
    float signal = 0.0f;
    bool alreadyCalibrated = false;
    { std::lock_guard<std::mutex> lock(calibrationMutex); alreadyCalibrated = !calibrationData[channel].empty(); }
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        if (history.empty()) {
            MessageBoxW(window, L"No live DAQ sample is available yet.", L"Calibration", MB_OK | MB_ICONWARNING);
            return;
        }
        signal = history.back().volts[channel];
    }
    std::wstring error;
    if (!AddCalibrationSample(channel, concentration, signal, error)) {
        MessageBoxW(window, error.c_str(), L"Calibration", MB_OK | MB_ICONERROR);
        return;
    }
    UpdateCalibrationPlotControls(false);
    if (!alreadyCalibrated) SendMessageW(calibrationPlotToggles[channel], BM_SETCHECK, BST_CHECKED, 0);
    UpdateCalibrationLiveValues();
    SetWindowTextW(calibrationSlopeFields[channel], L"");
    SetWindowTextW(calibrationInterceptFields[channel], L"");
    SetWindowTextW(calibrationR2Fields[channel], L"");
    wchar_t status[192]{};
    swprintf_s(status, L"CH%u calibration pair saved: concentration %.6g, signal %.6g V.", static_cast<unsigned>(channel), concentration, static_cast<double>(signal));
    SetWindowTextW(infoLabel, status);
    WriteLog(status);
    calibrationGraphCache.valid = false;
    const RECT plot = CalibrationCurveGraphBounds(window);
    InvalidateRect(window, &plot, TRUE);
}

void ApplyCalibration(HWND window, size_t channel) {
    size_t count = 0;
    bool distinctConcentrations = false;
    {
        std::lock_guard<std::mutex> lock(calibrationMutex);
        const auto& samples = calibrationData[channel];
        count = samples.size();
        if (samples.size() >= 2) {
            const double first = samples.front().concentration;
            distinctConcentrations = std::any_of(samples.begin() + 1, samples.end(), [first](const CalibrationSample& sample) { return sample.concentration != first; });
        }
    }
    if (count < 2 || !distinctConcentrations) {
        MessageBoxW(window, count < 2 ? L"At least two calibration samples are required." : L"Calibration samples must have at least two different concentrations.", L"Calibration", MB_OK | MB_ICONINFORMATION);
        return;
    }
    DisplayCalibrationFit(channel);
    if (GetWindowTextLengthW(calibrationR2Fields[channel]) == 0) {
        MessageBoxW(window, L"The slope and intercept are available, but r^2 is undefined because the captured signal values have no variation.", L"Calibration", MB_OK | MB_ICONINFORMATION);
        return;
    }
    double slope = 0.0, intercept = 0.0;
    if (!ReadFiniteNumber(calibrationSlopeFields[channel], slope) ||
        !ReadFiniteNumber(calibrationInterceptFields[channel], intercept) || std::abs(slope) < 1e-15) {
        MessageBoxW(window, L"This calibration has a zero or invalid slope and cannot convert signals to concentration.", L"Calibration", MB_OK | MB_ICONWARNING);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(calibrationMutex);
        appliedCalibrations[channel] = AppliedCalibration{true, slope, intercept};
    }
    wchar_t status[96]{}; swprintf_s(status, L"Linear calibration applied to CH%u.", static_cast<unsigned>(channel));
    SetWindowTextW(infoLabel, status);
    WriteLog(status);
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
    EnableWindow(mocoAddressInput, !connected);
    EnableWindow(mocoConnectButton, !connected);
    EnableWindow(mocoDisconnectButton, connected);
    EnableWindow(mocoAdvancedButton, connected);
    EnableWindow(mocoCommandInput, connected);
    EnableWindow(mocoSendButton, connected);
    if (!connected) {
        mocoPositionQueryPending = false;
        mocoCurrentPositionValid = false;
        SetWindowTextW(mocoPositionInput, L"—");
    }
}

bool ReadPositiveNumber(HWND input, double& value) {
    wchar_t text[96]{};
    GetWindowTextW(input, text, static_cast<int>(std::size(text)));
    wchar_t* end = nullptr;
    errno = 0;
    const double parsed = std::wcstod(text, &end);
    if (end == text || errno == ERANGE || !std::isfinite(parsed) || parsed <= 0.0) return false;
    while (*end && std::iswspace(*end)) ++end;
    if (*end) return false;
    value = parsed;
    return true;
}

bool ReadFiniteNumber(HWND input, double& value) {
    wchar_t text[96]{};
    GetWindowTextW(input, text, static_cast<int>(std::size(text)));
    wchar_t* end = nullptr;
    errno = 0;
    const double parsed = std::wcstod(text, &end);
    if (end == text || errno == ERANGE || !std::isfinite(parsed)) return false;
    while (*end && std::iswspace(*end)) ++end;
    if (*end) return false;
    value = parsed;
    return true;
}

bool ReadNonNegativeNumber(HWND input, double& value) {
    if (!ReadFiniteNumber(input, value) || value < 0.0) return false;
    return true;
}

void UpdateMocoPositionDisplay() {
    if (!mocoPositionInput) return;
    if (!mocoCurrentPositionValid.load()) { SetWindowTextW(mocoPositionInput, L"—"); return; }
    double incrementsPerMicron = 2.0;
    if (!ReadPositiveNumber(scaleInput, incrementsPerMicron)) return;
    wchar_t positionText[64]{};
    const double softwareMicrons = static_cast<double>(mocoCurrentPosition.load()) / incrementsPerMicron - softwarePositionOffsetMicrons.load();
    swprintf_s(positionText, L"%.1f", softwareMicrons);
    SetWindowTextW(mocoPositionInput, positionText);
}

bool ParseMocoPositionReply(const std::wstring& response, long long& position) {
    for (size_t marker = 0; (marker = response.find_first_of(L"Pp", marker)) != std::wstring::npos; ++marker) {
        if (marker + 2 >= response.size() || !std::iswdigit(response[marker + 1]) || response[marker + 2] != L':') continue;
        const wchar_t* number = response.c_str() + marker + 3;
        while (*number && std::iswspace(*number)) ++number;
        wchar_t* end = nullptr;
        errno = 0;
        const long long parsed = std::wcstoll(number, &end, 10);
        if (number != end && errno != ERANGE) {
            position = parsed;
            return true;
        }
    }
    return false;
}

bool QueryMocoInteger(const std::wstring& command, wchar_t responseCode, long long& value, std::wstring& error, wchar_t alternateResponseCode = 0) {
    if (!moco.IsOpen()) { error = L"The MoCo is not connected."; return false; }
    // A reply can arrive in several serial reads. Own the complete transaction,
    // and consume earlier replies before issuing a new query.
    std::unique_lock<std::mutex> serialLock(mocoMutex);
    std::wstring pending;
    auto quietSince = std::chrono::steady_clock::now();
    const auto drainDeadline = quietSince + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < drainDeadline) {
        std::wstring ignored;
        if (!moco.ReadAvailable(ignored, error)) return false;
        if (!ignored.empty()) quietSince = std::chrono::steady_clock::now();
        if (std::chrono::steady_clock::now() - quietSince >= std::chrono::milliseconds(100)) break;
        Sleep(5);
    }
    if (std::chrono::steady_clock::now() - quietSince < std::chrono::milliseconds(100)) {
        error = L"MoCo input did not become idle before querying " + command + L".";
        return false;
    }
    if (!moco.SendAsciiCommand(command, error)) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while (std::chrono::steady_clock::now() < deadline) {
        std::wstring response;
        if (!moco.ReadAvailable(response, error)) return false;
        pending += response;
        std::wstring marker = std::wstring(1, responseCode) + L"0:";
        size_t at = pending.find(marker);
        if (at == std::wstring::npos && alternateResponseCode) {
            marker = std::wstring(1, alternateResponseCode) + L"0:";
            at = pending.find(marker);
        }
        if (at != std::wstring::npos) {
            // Never accept a numeric prefix from an unfinished reply.
            const size_t lineEnd = pending.find(L'\n', at + marker.size());
            if (lineEnd == std::wstring::npos) { Sleep(5); continue; }
            const std::wstring numericText = pending.substr(at + marker.size(), lineEnd - at - marker.size());
            const wchar_t* number = numericText.c_str();
            while (*number && std::iswspace(*number)) ++number;
            wchar_t* end = nullptr;
            errno = 0;
            const long long parsed = std::wcstoll(number, &end, 10);
            const bool parsedDigits = number != end;
            while (*end && std::iswspace(*end)) ++end;
            if (parsedDigits && errno != ERANGE && *end == L'\0') { value = parsed; error.clear(); return true; }
            error = L"Invalid complete MoCo response to " + command + L": " + numericText;
            return false;
        }
        Sleep(15);
    }
    error = L"Timed out waiting for the MoCo response to " + command + L".";
    return false;
}

struct AdvancedMotorValues {
    std::array<std::wstring, 6> values{};
    std::wstring status;
};

AdvancedMotorValues ReadAdvancedMotorValues() {
    AdvancedMotorValues result;
    for (auto& value : result.values) value = L"Unavailable";
    double incrementsPerMicron = 0.0;
    const bool scaleValid = ReadPositiveNumber(scaleInput, incrementsPerMicron);
    struct Query { const wchar_t* command; wchar_t code; size_t field; wchar_t alternateCode; };
    constexpr Query queries[] = {{L"GV", L'Y', 0, 0}, {L"GA", L'A', 1, L'L'}, {L"GP", L'G', 2, 0},
                                  {L"GI", L'I', 3, 0}, {L"GD", L'D', 4, 0}, {L"GL", L'L', 5, 0}};
    std::vector<std::wstring> errors;
    for (const auto& query : queries) {
        long long raw = 0;
        std::wstring error;
        const bool read = QueryMocoInteger(query.command, query.code, raw, error, query.alternateCode);
        if (!read) { errors.push_back(error); continue; }
        if (query.field < 2) {
            if (!scaleValid) { errors.push_back(L"Set a valid Encoder increments per um value to convert velocity and acceleration."); continue; }
            const double microns = static_cast<double>(raw) / incrementsPerMicron;
            wchar_t text[64]{};
            swprintf_s(text, L"%.6g", microns);
            result.values[query.field] = text;
        } else {
            result.values[query.field] = std::to_wstring(raw);
        }
    }
    if (errors.empty()) result.status = L"Values read from the connected MoCo. Velocity and acceleration are converted using Encoder increments per um.";
    else {
        result.status = L"Some values could not be read: ";
        for (size_t i = 0; i < errors.size(); ++i) {
            if (i) result.status += L"  ";
            result.status += errors[i];
        }
    }
    return result;
}

constexpr wchar_t AdvancedDialogClassName[] = L"TaskerAdvancedMotorDialog";
constexpr int AdvancedFieldBaseId = 7300;
constexpr int AdvancedStatusId = 7310;
constexpr int AdvancedReadAgainId = 7311;
constexpr int AdvancedCloseId = 7312;
constexpr int AdvancedSetBaseId = 7320;
struct AdvancedDialogState {
    HWND owner = nullptr;
    HWND fields[6]{};
    HWND status = nullptr;
    AdvancedMotorValues values;
    bool closed = false;
};

void CloseAdvancedDialog(HWND dialog, HWND owner) {
    for (HWND control : {mocoCommandLabel, mocoCommandInput, mocoSendButton, mocoConsoleLabel, mocoConsole}) {
        ShowWindow(control, SW_HIDE);
        SetParent(control, owner);
    }
    DestroyWindow(dialog);
}

void SetAdvancedMotorValue(AdvancedDialogState& state, int field) {
    auto status = [&](const std::wstring& text) { SetWindowTextW(state.status, text.c_str()); };
    if (!moco.IsOpen() || profileRunning.load()) {
        status(L"Connect to MoCo and finish any profile scan before setting motor values.");
        return;
    }
    double entered = 0.0, scale = 1.0;
    if (!ReadNonNegativeNumber(state.fields[field], entered)) {
        status(L"Enter a finite, nonnegative number in this field.");
        SetFocus(state.fields[field]);
        return;
    }
    if (field < 2 && !ReadPositiveNumber(scaleInput, scale)) {
        status(L"Enter a valid Encoder increments per um value first.");
        return;
    }
    constexpr const wchar_t* setters[] = {L"DV", L"DA", L"DP", L"DI", L"DD", L"DL"};
    constexpr const wchar_t* getters[] = {L"GV", L"GA", L"GP", L"GI", L"GD", L"GL"};
    constexpr wchar_t codes[] = {L'Y', L'A', L'G', L'I', L'D', L'L'};
    constexpr long long limits[] = {1000000, 10000000, 32767, 32767, 32767, 32767};
    const double rawValue = entered * scale;
    if (!std::isfinite(rawValue) || rawValue > static_cast<double>(limits[field]) ||
        (field >= 2 && std::floor(entered) != entered)) {
        status(field < 2 ? L"Value exceeds the controller range after conversion to encoder units."
                         : L"Enter a whole number from 0 to 32767.");
        SetFocus(state.fields[field]);
        return;
    }
    const long long raw = std::llround(rawValue);
    const std::wstring command = std::wstring(setters[field]) + std::to_wstring(raw);
    std::wstring error;
    bool sent = false;
    {
        std::lock_guard<std::mutex> lock(mocoMutex);
        sent = moco.SendAsciiCommand(command, error);
    }
    if (!sent) { status(L"Could not set value: " + error); return; }
    WriteLog(L"Advanced motor setting sent: " + command);
    long long confirmed = 0;
    if (!QueryMocoInteger(getters[field], codes[field], confirmed, error, field == 1 ? L'L' : 0)) {
        status(L"Setting sent, but readback failed: " + error);
        return;
    }
    wchar_t text[64]{};
    if (field < 2) swprintf_s(text, L"%.10g", static_cast<double>(confirmed) / scale);
    else swprintf_s(text, L"%lld", confirmed);
    SetWindowTextW(state.fields[field], text);
    status(confirmed == raw ? L"Value set and confirmed by MoCo."
                            : L"MoCo returned a different value; the field shows its current setting.");
}

LRESULT CALLBACK AdvancedDialogProcedure(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AdvancedDialogState*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<AdvancedDialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    switch (message) {
    case WM_CREATE: {
        constexpr const wchar_t* labels[] = {L"Velocity (um/s)", L"Acceleration (um/s²)", L"P-Term", L"I-Term", L"D-Term", L"Integral-Limit"};
        for (int i = 0; i < 6; ++i) {
            const int y = 18 + i * 42;
            CreateWindowW(L"STATIC", labels[i], WS_CHILD | WS_VISIBLE | SS_LEFT, 22, y + 5, 180, 26,
                          dialog, nullptr, GetModuleHandleW(nullptr), nullptr);
            state->fields[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->values.values[i].c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_RIGHT, 210, y, 175, 32,
                dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(AdvancedFieldBaseId + i)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowW(L"BUTTON", L"Set", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                395, y, 65, 32, dialog,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(AdvancedSetBaseId + i)), GetModuleHandleW(nullptr), nullptr);
        }
        state->status = CreateWindowW(L"STATIC", state->values.status.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
            22, 274, 460, 44, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(AdvancedStatusId)), GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"BUTTON", L"Read again", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            260, 332, 100, 34, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(AdvancedReadAgainId)), GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            370, 332, 90, 34, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(AdvancedCloseId)), GetModuleHandleW(nullptr), nullptr);
        for (HWND control : {mocoCommandLabel, mocoCommandInput, mocoSendButton, mocoConsoleLabel, mocoConsole}) {
            SetParent(control, dialog);
            ShowWindow(control, SW_SHOW);
        }
        MoveWindow(mocoCommandLabel,22,388,460,28,TRUE);
        MoveWindow(mocoCommandInput,22,420,345,32,TRUE);
        MoveWindow(mocoSendButton,377,420,83,32,TRUE);
        MoveWindow(mocoConsoleLabel,22,464,460,28,TRUE);
        MoveWindow(mocoConsole,22,496,460,160,TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == MocoSendId && HIWORD(wParam) == BN_CLICKED) {
            wchar_t command[1024]{};
            GetWindowTextW(mocoCommandInput,command,static_cast<int>(std::size(command)));
            std::wstring error;
            bool sent = false;
            { std::lock_guard<std::mutex> lock(mocoMutex); sent = moco.SendAsciiCommand(command,error); }
            if (sent) AppendMocoConsole(L"> " + std::wstring(command) + L"\r\n");
            else SetWindowTextW(state->status,error.c_str());
            return 0;
        }
        if (LOWORD(wParam) >= AdvancedSetBaseId && LOWORD(wParam) < AdvancedSetBaseId + 6 && HIWORD(wParam) == BN_CLICKED) {
            SetAdvancedMotorValue(*state, LOWORD(wParam) - AdvancedSetBaseId);
            return 0;
        }
        if (LOWORD(wParam) == AdvancedReadAgainId && HIWORD(wParam) == BN_CLICKED) {
            state->values = ReadAdvancedMotorValues();
            for (int i = 0; i < 6; ++i) SetWindowTextW(state->fields[i], state->values.values[i].c_str());
            SetWindowTextW(state->status, state->values.status.c_str());
            return 0;
        }
        if (LOWORD(wParam) == AdvancedCloseId && HIWORD(wParam) == BN_CLICKED) { CloseAdvancedDialog(dialog, state->owner); return 0; }
        break;
    case WM_CLOSE:
        CloseAdvancedDialog(dialog, state->owner);
        return 0;
    case WM_DESTROY:
        state->closed = true;
        return 0;
    }
    return DefWindowProcW(dialog, message, wParam, lParam);
}

void ShowAdvancedMotorDialog(HWND owner) {
    if (!moco.IsOpen()) {
        MessageBoxW(owner, L"Connect to the MoCo controller before reading advanced values.", L"Advanced motor values", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (profileRunning.load()) {
        MessageBoxW(owner, L"Wait for the profile scan to finish before querying motor settings.", L"Advanced motor values", MB_OK | MB_ICONINFORMATION);
        return;
    }
    AdvancedDialogState state;
    state.owner = owner;
    state.values = ReadAdvancedMotorValues();
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = AdvancedDialogProcedure;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = AdvancedDialogClassName;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            MessageBoxW(owner, L"Could not open the Advanced dialog.", L"Tasker", MB_OK | MB_ICONERROR);
            return;
        }
        registered = true;
    }
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int width = 510, height = 710;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, AdvancedDialogClassName,
        L"Advanced motor values", WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height,
        owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!dialog) return;
    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    SetFocus(state.fields[0]);
    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    SetForegroundWindow(owner);
}

bool TryBuildStepCommand(int direction, std::wstring& command, std::wstring& error) {
    double stepMicrons = 0.0, incrementsPerMicron = 0.0;
    if (!ReadPositiveNumber(stepInput, stepMicrons)) {
        error = L"Enter a positive step distance in microns.";
        return false;
    }
    if (!ReadPositiveNumber(scaleInput, incrementsPerMicron)) {
        error = L"Enter the calibrated encoder increments per micron before moving.";
        return false;
    }
    if (!moco.IsOpen() || !mocoCurrentPositionValid.load()) {
        error = L"Connect to the MoCo and wait for a valid current position before moving.";
        return false;
    }
    constexpr long long maxPosition = 1073741843LL;
    const long double rawIncrements = static_cast<long double>(stepMicrons) * incrementsPerMicron;
    if (!std::isfinite(static_cast<double>(rawIncrements)) || rawIncrements < 0.5L || rawIncrements > maxPosition) {
        error = L"The step distance is outside the MoCo encoder-increment range.";
        return false;
    }
    const long long increments = static_cast<long long>(std::llround(rawIncrements));
    const long long target = mocoCurrentPosition.load() + direction * increments;
    if (target < -maxPosition || target > maxPosition) {
        error = L"That step would exceed the MoCo controller's documented position range.";
        return false;
    }
    command = L"MR" + std::to_wstring(direction * increments);
    error.clear();
    return true;
}

void UpdateStepButtonState() {
    double stepMicrons = 0.0, incrementsPerMicron = 0.0;
    const bool enabled = moco.IsOpen() && mocoCurrentPositionValid.load() && !profileRunning.load() &&
        ReadPositiveNumber(stepInput, stepMicrons) && ReadPositiveNumber(scaleInput, incrementsPerMicron);
    EnableWindow(stepUpButton, enabled);
    EnableWindow(stepDownButton, enabled);
}

void MoveMoCoStep(HWND window, int direction) {
    std::wstring command, error;
    if (!TryBuildStepCommand(direction, command, error)) {
        SetWindowTextW(mocoStatusLabel, error.c_str());
        WriteLog(L"MoCo step not sent: " + error);
        return;
    }
    double stepMicrons = 0.0;
    ReadPositiveNumber(stepInput, stepMicrons);
    bool moveSent = false;
    { std::lock_guard<std::mutex> lock(mocoMutex); moveSent = moco.SendAsciiCommand(command, error); }
    if (!moveSent) {
        SetWindowTextW(mocoStatusLabel, error.c_str());
        WriteLog(L"MoCo step failed: " + error);
        return;
    }
    AppendMocoConsole(L"> " + command + L"  [" + std::to_wstring(stepMicrons) + L" microns]\r\n");
    WriteLog(L"Sent MoCo relative move " + command + L" for " + std::to_wstring(stepMicrons) + L" microns.");
    SetWindowTextW(mocoStatusLabel, L"Relative move command sent; current position continues to update.");
    (void)window;
}

void GoToMoCoPosition(HWND window) {
    double positionMicrons = 0.0, incrementsPerMicron = 0.0;
    if (!ReadFiniteNumber(goToInput, positionMicrons) || !ReadPositiveNumber(scaleInput, incrementsPerMicron)) {
        SetWindowTextW(mocoStatusLabel, L"Enter a valid target position and encoder scale.");
        return;
    }
    if (!moco.IsOpen()) { SetWindowTextW(mocoStatusLabel, L"Connect to the MoCo before moving."); return; }
    constexpr long long maxPosition = 1073741843LL;
    const long double rawIncrements = (static_cast<long double>(positionMicrons) + softwarePositionOffsetMicrons.load()) * incrementsPerMicron;
    if (!std::isfinite(static_cast<double>(rawIncrements)) || rawIncrements < -maxPosition || rawIncrements > maxPosition) {
        SetWindowTextW(mocoStatusLabel, L"The target is outside the MoCo controller's position range.");
        return;
    }
    const long long target = static_cast<long long>(std::llround(rawIncrements));
    const std::wstring command = L"MA" + std::to_wstring(target);
    std::wstring error;
    bool moveSent = false;
    { std::lock_guard<std::mutex> lock(mocoMutex); moveSent = moco.SendAsciiCommand(command, error); }
    if (!moveSent) { SetWindowTextW(mocoStatusLabel, error.c_str()); return; }
    AppendMocoConsole(L"> " + command + L"  [software target " + std::to_wstring(positionMicrons) + L" um]\r\n");
    WriteLog(L"Sent MoCo absolute move " + command + L" to software position " + std::to_wstring(positionMicrons) + L" um.");
    SetWindowTextW(mocoStatusLabel, L"Absolute move command sent; current position continues to update.");
    (void)window;
}

void SetSoftwarePosition(HWND window) {
    double desiredMicrons = 0.0, incrementsPerMicron = 0.0;
    if (!ReadFiniteNumber(setPositionInput, desiredMicrons) || !ReadPositiveNumber(scaleInput, incrementsPerMicron)) {
        SetWindowTextW(mocoStatusLabel, L"Enter a valid software position and encoder scale.");
        return;
    }
    if (!moco.IsOpen() || !mocoCurrentPositionValid.load()) {
        SetWindowTextW(mocoStatusLabel, L"Connect to the MoCo and wait for a position reading before setting software position.");
        return;
    }
    const double currentHardwareMicrons = static_cast<double>(mocoCurrentPosition.load()) / incrementsPerMicron;
    const double newOffset = currentHardwareMicrons - desiredMicrons;
    softwarePositionOffsetMicrons.store(newOffset);
    if (!configFilePath.empty()) {
        wchar_t offsetText[64]{};
        swprintf_s(offsetText, L"%.12g", newOffset);
        WritePrivateProfileStringW(L"Tasker", L"PositionOffsetMicrons", offsetText, configFilePath.c_str());
    }
    UpdateMocoPositionDisplay();
    const std::wstring note = L"Software position set to " + std::to_wstring(desiredMicrons) + L" um; MoCo position was not changed.";
    SetWindowTextW(mocoStatusLabel, note.c_str());
    AppendMocoConsole(L"[" + note + L"]\r\n");
    WriteLog(note);
    (void)window;
}

bool SendProfileTarget(long long targetIncrements, std::wstring& error) {
    std::lock_guard<std::mutex> lock(mocoMutex);
    if (!moco.IsOpen()) { error = L"MoCo connection is not available."; return false; }
    return moco.SendAsciiCommand(L"MA" + std::to_wstring(targetIncrements), error);
}

bool WaitForProfileTarget(long long targetIncrements) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
    while (!profileCancelRequested.load() && !shutdownRequested.load() && std::chrono::steady_clock::now() < deadline) {
        { std::lock_guard<std::mutex> lock(mocoMutex); if (!moco.IsOpen()) return false; }
        if (mocoCurrentPositionValid.load() && std::llabs(mocoCurrentPosition.load() - targetIncrements) <= 2) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

bool InterruptibleDelay(double seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
    while (!profileCancelRequested.load() && !shutdownRequested.load() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return !profileCancelRequested.load() && !shutdownRequested.load();
}

bool MeasureProfileSignals(double seconds, std::array<double, 8>& averages, unsigned long long& count) {
    const auto start = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(profileCaptureMutex);
        profileCapture.active = true;
        profileCapture.start = start;
        profileCapture.end = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
        profileCapture.sums.fill(0.0);
        profileCapture.samples = 0;
    }
    const bool elapsed = InterruptibleDelay(seconds);
    {
        std::lock_guard<std::mutex> lock(profileCaptureMutex);
        profileCapture.active = false;
        averages = profileCapture.sums;
        count = profileCapture.samples;
    }
    if (!elapsed || !count) return false;
    for (double& value : averages) value /= static_cast<double>(count);
    return true;
}

std::filesystem::path ProfileDataPath(const std::wstring& selectedDataFile) {
    const std::filesystem::path base(selectedDataFile);
    return base.parent_path() / (base.stem().wstring() + L"_profiles.txt");
}

struct ProfileSettings {
    double startMicrons = 0.0, endMicrons = 0.0, stepMicrons = 0.0, safeMicrons = 0.0;
    double waitSeconds = 0.0, measureSeconds = 0.0, overshootMicrons = 0.0, overshootWaitSeconds = 0.0;
    double incrementsPerMicron = 0.0, offsetMicrons = 0.0;
    std::wstring sourceDataFile;
    std::wstring label;
    size_t cycles = 1;
    double cycleIntervalSeconds = 0.0;
};

void ProfileScanWorker(HWND window, ProfileSettings settings, std::vector<double> points) {
    std::wstring result;
    bool complete = true;
    const auto outputPath = ProfileDataPath(settings.sourceDataFile);
    std::error_code fileError;
    bool needsHeader = !std::filesystem::exists(outputPath, fileError) || std::filesystem::file_size(outputPath, fileError) == 0;
    // Upgrade existing profile files before appending, preserving old rows with
    // empty cells for any columns that were added in a newer version.
    if (!needsHeader) {
        std::ifstream existing(outputPath, std::ios::binary);
        std::string header;
        if (std::getline(existing, header)) {
            while (!header.empty() && header.back() == '\r') header.pop_back();
            const bool addCalibrated = header.find("_calibrated") == std::string::npos;
            const bool addComment = header.find("\tComment") == std::string::npos;
            const bool addDataset = header.rfind("Profile dataset\t", 0) != 0;
            const int addedColumns = (addCalibrated ? 8 : 0) + (addComment ? 1 : 0);
            std::vector<std::string> lines;
            std::string line;
            bool needsCleanup = false;
            while (std::getline(existing, line)) {
                while (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.find_first_not_of(" \t\r\n") == std::string::npos) { needsCleanup = true; continue; }
                lines.push_back(line);
            }
            existing.close();
            if (addedColumns > 0 || addDataset || needsCleanup) {
                const auto upgradePath = outputPath.wstring() + L".upgrade";
                std::ofstream upgraded(upgradePath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (upgraded) {
                    if (addCalibrated) header += "\tCH0_calibrated\tCH1_calibrated\tCH2_calibrated\tCH3_calibrated\tCH4_calibrated\tCH5_calibrated\tCH6_calibrated\tCH7_calibrated";
                    if (addComment) header += "\tComment";
                    if (addDataset) upgraded << "Profile dataset\t";
                    upgraded << header << '\n';
                    for (const auto& oldLine : lines) {
                        if (addDataset) upgraded << '\t';
                        upgraded << oldLine;
                        for (int column = 0; column < addedColumns; ++column) upgraded << '\t';
                        upgraded << '\n';
                    }
                    upgraded.close();
                    if (upgraded) {
                        MoveFileExW(upgradePath.c_str(), outputPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
                    }
                }
            }
        }
    }
    std::ofstream output(outputPath, std::ios::out | std::ios::binary | std::ios::app);
    if (!output) {
        result = L"Could not create or open profile data file: " + outputPath.filename().wstring();
        complete = false;
    } else if (needsHeader) {
        output << "Profile dataset\ttimestamp_local\tPosition (um)";
        for (int channel = 0; channel < 8; ++channel) output << "\tCH" << channel << "_V";
        for (int channel = 0; channel < 8; ++channel) output << "\tCH" << channel << "_calibrated";
        output << "\tComment";
        output << '\n';
        output.flush();
        if (!output) { complete = false; result = L"Could not write the profile data file header."; }
    }

    constexpr long long maxPosition = 1073741843LL;
    auto toTarget = [&](double softwarePosition, long long& increments) {
        const long double raw = (static_cast<long double>(softwarePosition) + settings.offsetMicrons) * settings.incrementsPerMicron;
        if (!std::isfinite(static_cast<double>(raw)) || raw < -maxPosition || raw > maxPosition) return false;
        increments = static_cast<long long>(std::llround(raw));
        return true;
    };
    auto moveAndWait = [&](double position, const wchar_t* stage) {
        long long target = 0;
        if (!toTarget(position, target)) { result = L"A profile target is outside the MoCo position range."; return false; }
        std::wstring error;
        if (!SendProfileTarget(target, error)) { result = L"Could not send profile move: " + error; return false; }
        WriteLog(L"Sent profile MA" + std::to_wstring(target) + L" for " + std::wstring(stage) + L" at software position " + std::to_wstring(position) + L" um.");
        if (!WaitForProfileTarget(target)) {
            result = profileCancelRequested.load() || shutdownRequested.load() ? L"Profile scan cancelled." : L"Timed out waiting for the MoCo to reach its target.";
            return false;
        }
        return true;
    };

    const double scanDirection = settings.endMicrons >= settings.startMicrons ? 1.0 : -1.0;
    for (size_t cycle = 0; cycle < settings.cycles && complete; ++cycle) {
    if (profileCancelRequested.load() || shutdownRequested.load()) { complete = false; result = L"Profile scan cancelled."; break; }
    const auto cycleStarted = std::chrono::steady_clock::now();
    unsigned long long datasetNumber = 0;
    {
        std::lock_guard<std::mutex> lock(profilePlotMutex);
        datasetNumber = nextProfileDatasetNumber++;
        profileDatasets.push_back(ProfileDataset{datasetNumber, settings.label, {}});
    }
    PostMessageW(window, ProfilePlotUpdatedMessage, 0, 0);
    const long long profileStartWallMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::vector<long long> assignedProfileCommentTimes;
    if (complete && points.size() && output) {
        for (size_t i = 0; i < points.size(); ++i) {
            if (profileCancelRequested.load() || shutdownRequested.load()) { complete = false; result = L"Profile scan cancelled."; break; }
            profileStepIndex.store(cycle * points.size() + i + 1);
            if (settings.overshootMicrons > 0.0) {
                const double overshootTarget = points[i] + scanDirection * settings.overshootMicrons;
                if (!moveAndWait(overshootTarget, L"overshoot")) { complete = false; break; }
                if (!InterruptibleDelay(settings.overshootWaitSeconds)) { complete = false; result = L"Profile scan cancelled."; break; }
            }
            if (!moveAndWait(points[i], L"scan point")) { complete = false; break; }
            if (!InterruptibleDelay(settings.waitSeconds)) { complete = false; result = L"Profile scan cancelled."; break; }
            std::array<double, 8> averages{};
            unsigned long long sampleCount = 0;
            if (!MeasureProfileSignals(settings.measureSeconds, averages, sampleCount)) {
                complete = false;
                result = profileCancelRequested.load() || shutdownRequested.load() ? L"Profile scan cancelled." : L"No DAQ samples were collected during a measurement interval.";
                break;
            }
            const auto rowTimestamp = std::chrono::system_clock::now();
            if (i == 0) output << datasetNumber << " - " << WideToUtf8(settings.label);
            output << '\t';
            output << WideToUtf8(LocalTimestamp(L"%Y-%m-%d %H:%M:%S")) << '\t' << std::fixed << std::setprecision(6) << points[i];
            for (size_t channel=0;channel<averages.size();++channel) { output << '\t'; if(ChannelEnabled(channel)) output << averages[channel]; }
            std::array<AppliedCalibration, 8> calibrations{};
            { std::lock_guard<std::mutex> lock(calibrationMutex); calibrations = appliedCalibrations; }
            for (int channel = 0; channel < 8; ++channel) {
                output << '\t';
                const auto& calibration = calibrations[channel];
                if (ChannelEnabled(channel) && calibration.valid && std::isfinite(calibration.slope) && std::abs(calibration.slope) >= 1e-15) {
                    const double concentration = (averages[channel] - calibration.intercept) / calibration.slope;
                    if (std::isfinite(concentration)) output << concentration;
                }
            }
            std::wstring profileComment;
            const long long rowWallMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(rowTimestamp.time_since_epoch()).count();
            {
                std::lock_guard<std::mutex> lock(dataMutex);
                long long nearestDistance = (std::numeric_limits<long long>::max)();
                long long nearestTime = 0;
                for (const auto& marker : graphComments) {
                    if (marker.wallMilliseconds < profileStartWallMilliseconds ||
                        std::find(assignedProfileCommentTimes.begin(), assignedProfileCommentTimes.end(), marker.wallMilliseconds) != assignedProfileCommentTimes.end() ||
                        marker.wallMilliseconds > rowWallMilliseconds) continue;
                    const long long distance = std::llabs(marker.wallMilliseconds - rowWallMilliseconds);
                    if (distance < nearestDistance) { nearestDistance = distance; nearestTime = marker.wallMilliseconds; profileComment = marker.text; }
                }
                if (nearestTime != 0 && !profileComment.empty()) assignedProfileCommentTimes.push_back(nearestTime);
            }
            for (wchar_t& character : profileComment) if (character == L'\t' || character == L'\r' || character == L'\n') character = L' ';
            output << '\t' << WideToUtf8(profileComment);
            output << '\n';
            output.flush();
            if (!output) { complete = false; result = L"A profile data row could not be written."; break; }
            {
                std::lock_guard<std::mutex> lock(profilePlotMutex);
                auto plotted=averages;
                for(size_t channel=0;channel<8;++channel) if(!ChannelEnabled(channel)) plotted[channel]=std::numeric_limits<double>::quiet_NaN();
                profileDatasets.back().points.push_back(ProfilePlotPoint{points[i], plotted});
            }
            PostMessageW(window, ProfilePlotUpdatedMessage, 0, 0);
        }
    }
    if (complete && !profileCancelRequested.load() && !shutdownRequested.load()) {
        if (!moveAndWait(settings.safeMicrons, L"safe")) complete = false;
        else result = L"Profile scan complete. Returned to Safe position " + std::to_wstring(settings.safeMicrons) + L" um. Data saved to " + outputPath.filename().wstring() + L".";
    }
    if (complete && cycle + 1 < settings.cycles) {
        // Start-to-start interval: scan and Safe return time count toward it.
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - cycleStarted).count();
        const double remaining = settings.cycleIntervalSeconds - elapsed;
        if (remaining > 0.0 && !InterruptibleDelay(remaining)) {
            complete = false;
            result = L"Profile cycles cancelled while waiting for the next start.";
        }
    }
    } // Each cycle includes the return to Safe.
    if (profileCancelRequested.load() || shutdownRequested.load()) {
        std::wstring abortError;
        std::lock_guard<std::mutex> lock(mocoMutex);
        if (moco.IsOpen()) moco.SendAsciiCommand(L"AB", abortError);
        if (result.empty()) result = L"Profile scan cancelled; motion abort requested.";
    }
    if (output.is_open()) { output.flush(); output.close(); }
    {
        std::lock_guard<std::mutex> lock(profileResultMutex);
        profileResult = result.empty() ? L"Profile scan stopped." : result;
    }
    PostMessageW(window, ProfileFinishedMessage, 0, 0);
}

void UpdateProfileControlState() {
    const bool connected = moco.IsOpen();
    const bool running = profileRunning.load();
    const bool canMove = connected && !running;
    double incrementsPerMicron = 0.0;
    const bool calibrated = ReadPositiveNumber(scaleInput, incrementsPerMicron);
    EnableWindow(stepInput, !running);
    EnableWindow(stepUpButton, canMove && calibrated && mocoCurrentPositionValid.load());
    EnableWindow(stepDownButton, canMove && calibrated && mocoCurrentPositionValid.load());
    EnableWindow(goToInput, !running);
    EnableWindow(goToButton, canMove && calibrated && mocoCurrentPositionValid.load());
    EnableWindow(setPositionInput, !running);
    EnableWindow(setPositionButton, canMove && calibrated && mocoCurrentPositionValid.load());
    EnableWindow(profileRunButton, canMove && calibrated && acquisitionAlive.load() && daqConnected.load() && !dataFilePath.empty());
    EnableWindow(profileCyclesLabel, TRUE);
    EnableWindow(profileCyclesInput, !running);
    EnableWindow(profileCycleIntervalInput, !running);
    for(size_t channel=0;channel<8;++channel) {
        EnableWindow(channelSelectFields[channel],!running);
        EnableWindow(channelRangeFields[channel],!running);
        EnableWindow(calibrationGetButtons[channel],ChannelEnabled(channel));
        EnableWindow(calibrationApplyButtons[channel],ChannelEnabled(channel));
    }
    EnableWindow(profileLabelInput, !running);
    EnableWindow(profileSettingsHeading, TRUE);
    EnableWindow(profilingStartInput, !running);
    EnableWindow(profileEndInput, !running);
    EnableWindow(profileStepInput, !running);
    EnableWindow(profileSafeInput, !running);
    EnableWindow(profileWaitInput, !running);
    EnableWindow(profileMeasureInput, !running);
    EnableWindow(profileOvershootInput, !running);
    EnableWindow(profileOvershootWaitInput, !running);
    EnableWindow(scaleInput, !running);
    UpdateMocoConnectionControls(connected);
    EnableWindow(mocoDisconnectButton, connected && !running);
    EnableWindow(mocoCommandInput, connected && !running);
    EnableWindow(mocoSendButton, connected && !running);
}

void StartProfileScan(HWND window) {
    if (profileRunning.load()) return;
    ProfileSettings settings;
    if (!ReadNonNegativeNumber(profileCycleIntervalInput, settings.cycleIntervalSeconds) || settings.cycleIntervalSeconds > 86400.0) {
        SetWindowTextW(mocoStatusLabel, L"Cycle interval must be from 0 to 86400 seconds (0 starts the next scan immediately after completion).");
        return;
    }
    {
        double cycles = 0.0;
        if (!ReadPositiveNumber(profileCyclesInput, cycles) || std::floor(cycles) != cycles || cycles > 1000000) {
            SetWindowTextW(mocoStatusLabel, L"Cycles must be a whole number from 1 to 1000000.");
            return;
        }
        settings.cycles = static_cast<size_t>(cycles);
    }
    std::vector<wchar_t> labelText(static_cast<size_t>(GetWindowTextLengthW(profileLabelInput)) + 1);
    GetWindowTextW(profileLabelInput, labelText.data(), static_cast<int>(labelText.size()));
    settings.label = labelText.data();
    for (wchar_t& character : settings.label) if (character == L'\t' || character == L'\r' || character == L'\n') character = L' ';
    if (!ReadFiniteNumber(profilingStartInput, settings.startMicrons) ||
        !ReadFiniteNumber(profileEndInput, settings.endMicrons) ||
        !ReadPositiveNumber(profileStepInput, settings.stepMicrons) ||
        !ReadFiniteNumber(profileSafeInput, settings.safeMicrons) ||
        !ReadNonNegativeNumber(profileWaitInput, settings.waitSeconds) ||
        !ReadPositiveNumber(profileMeasureInput, settings.measureSeconds) ||
        !ReadNonNegativeNumber(profileOvershootInput, settings.overshootMicrons) ||
        !ReadNonNegativeNumber(profileOvershootWaitInput, settings.overshootWaitSeconds) ||
        !ReadPositiveNumber(scaleInput, settings.incrementsPerMicron)) {
        SetWindowTextW(mocoStatusLabel, L"Enter valid Start, End, Step, Safe, Wait, Measure, Overshoot, Overshoot wait, and encoder scale values.");
        return;
    }
    if (settings.waitSeconds > 86400.0 || settings.measureSeconds > 86400.0 || settings.overshootWaitSeconds > 86400.0) {
        SetWindowTextW(mocoStatusLabel, L"Wait, Measure, and Overshoot wait values must not exceed 86400 seconds.");
        return;
    }
    if (!moco.IsOpen() || !mocoCurrentPositionValid.load()) {
        SetWindowTextW(mocoStatusLabel, L"Connect to the MoCo and wait for a position reading before starting a scan.");
        return;
    }
    if (!acquisitionAlive.load() || !daqConnected.load()) {
        SetWindowTextW(mocoStatusLabel, L"Live DAQ acquisition must be active before measuring a profile.");
        return;
    }
    if (dataFilePath.empty()) {
        SetWindowTextW(mocoStatusLabel, L"Choose a data file on the Signals tab before starting a profile.");
        return;
    }
    const double distance = std::abs(settings.endMicrons - settings.startMicrons);
    const long double wholeSteps = std::floor(static_cast<long double>(distance) / settings.stepMicrons);
    if (!std::isfinite(static_cast<double>(wholeSteps)) || wholeSteps > 999999.0L) {
        SetWindowTextW(mocoStatusLabel, L"The profile contains too many steps.");
        return;
    }
    const double direction = settings.endMicrons >= settings.startMicrons ? 1.0 : -1.0;
    const double lastRegular = settings.startMicrons + direction * static_cast<double>(wholeSteps) * settings.stepMicrons;
    const bool needsExactEnd = std::abs(settings.endMicrons - lastRegular) > 1e-9 * (std::max)(1.0, std::abs(settings.endMicrons));
    const size_t count = static_cast<size_t>(wholeSteps) + 1 + (needsExactEnd ? 1 : 0);
    std::vector<double> points;
    points.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (needsExactEnd && i == count - 1) points.push_back(settings.endMicrons);
        else points.push_back(settings.startMicrons + direction * static_cast<double>(i) * settings.stepMicrons);
    }
    settings.offsetMicrons = softwarePositionOffsetMicrons.load();
    settings.sourceDataFile = dataFilePath;
    constexpr long long maxPosition = 1073741843LL;
    for (double position : points) {
        const long double target = (static_cast<long double>(position) + settings.offsetMicrons) * settings.incrementsPerMicron;
        if (!std::isfinite(static_cast<double>(target)) || target < -maxPosition || target > maxPosition) {
            SetWindowTextW(mocoStatusLabel, L"A scan point exceeds the MoCo controller's position range.");
            return;
        }
        const long double overshootTarget = (static_cast<long double>(position + direction * settings.overshootMicrons) + settings.offsetMicrons) * settings.incrementsPerMicron;
        if (!std::isfinite(static_cast<double>(overshootTarget)) || overshootTarget < -maxPosition || overshootTarget > maxPosition) {
            SetWindowTextW(mocoStatusLabel, L"An overshoot point exceeds the MoCo controller's position range.");
            return;
        }
    }
    const long double safeTarget = (static_cast<long double>(settings.safeMicrons) + settings.offsetMicrons) * settings.incrementsPerMicron;
    if (!std::isfinite(static_cast<double>(safeTarget)) || safeTarget < -maxPosition || safeTarget > maxPosition) {
        SetWindowTextW(mocoStatusLabel, L"Safe position exceeds the MoCo controller's position range.");
        return;
    }
    if (profileThread.joinable()) profileThread.join();
    { const RECT chart = ProfileGraphBounds(window); InvalidateRect(window, &chart, TRUE); }
    profileCancelRequested.store(false);
    profileStepIndex.store(0);
    profileStepCount.store(points.size() * settings.cycles);
    profileRunning.store(true);
    UpdateProfileControlState();
    SetWindowTextW(mocoStatusLabel, L"Profile scan starting; creating the _profiles data file.");
    try {
        profileThread = std::thread(ProfileScanWorker, window, settings, std::move(points));
    } catch (const std::system_error&) {
        profileRunning.store(false);
        UpdateProfileControlState();
        SetWindowTextW(mocoStatusLabel, L"Tasker could not start the profile scan worker.");
    }
}

void SetView(HWND window, int tab) {
    ClearCommentHover(window);
    activeTab = tab;
    if (tab == 2) calibrationGraphCache.valid = false;
    const bool signals = tab == 0;
    const bool profiling = tab == 1;
    if(signals) graphCache.valid=false;
    RECT client{}; GetClientRect(window, &client);
    MoveWindow(infoLabel, 32, 184, tab == 2 ? 850 : (std::max)(100, static_cast<int>(client.right) - 64), 28, FALSE);
    ShowWindow(infoLabel, (signals || profiling || tab == 4) ? SW_HIDE : SW_SHOW);
    ShowWindow(startButton, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(stopButton, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(frequencyInput, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(frequencyLabel, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(displayLengthInput, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(displayLengthLabel, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(fileButton, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(commentButton, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(graphHelpButton, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(graphBackButton, signals ? SW_SHOW : SW_HIDE);
    ShowWindow(profileLegendWindow,profiling?SW_SHOW:SW_HIDE);
    if(profiling) RefreshProfileLegend(window);
    HWND motorsControls[] = { mocoHeading, mocoPortLabel, mocoPortInput, mocoBaudLabel, mocoBaudInput, mocoAddressLabel,
                              mocoAddressInput, mocoConnectButton, mocoDisconnectButton, mocoAdvancedButton, scaleLabel, scaleInput };
    for (HWND control : motorsControls) ShowWindow(control, tab == 4 ? SW_SHOW : SW_HIDE);
    ShowWindow(mocoStatusLabel, tab == 4 ? SW_SHOW : SW_HIDE);
    ShowWindow(channelsHeading,tab==4?SW_SHOW:SW_HIDE);
    for(HWND control:channelGridHeaders) ShowWindow(control,tab==4?SW_SHOW:SW_HIDE);
    for(size_t channel=0;channel<8;++channel) {
        for(HWND control:{channelHardwareLabels[channel],channelSelectFields[channel],channelNameFields[channel],channelRangeFields[channel]}) ShowWindow(control,tab==4?SW_SHOW:SW_HIDE);
    }
    for (HWND control : {mocoCommandLabel, mocoCommandInput, mocoSendButton, mocoConsoleLabel, mocoConsole}) ShowWindow(control, SW_HIDE);
    HWND profilingControls[] = { mocoPositionLabel,
                                 mocoPositionInput, stepLabel, stepInput, stepUpButton, stepDownButton,
                                 profilingStartLabel, profilingStartInput, profileSettingsHeading, profileEndLabel, profileEndInput,
                                 profileStepLabel, profileStepInput, profileSafeLabel, profileSafeInput, manualControlHeading,
                                 goToLabel, goToInput, goToButton, setPositionLabel, setPositionInput, setPositionButton,
                                 profileWaitLabel, profileWaitInput, profileMeasureLabel, profileMeasureInput,
                                 profileOvershootLabel, profileOvershootInput, profileOvershootWaitLabel, profileOvershootWaitInput, profileRunButton,
                                 profileCyclesLabel, profileCyclesInput, profileLabelCaption, profileLabelInput,
                                 profileCycleIntervalLabel, profileCycleIntervalInput };
    for (HWND control : profilingControls) ShowWindow(control, profiling ? SW_SHOW : SW_HIDE);
    for (HWND control : calibrationColumnLabels) ShowWindow(control, tab == 2 ? SW_SHOW : SW_HIDE);
    for (size_t channel = 0; channel < 8; ++channel) {
        HWND controls[] = { calibrationChannelLabels[channel], calibrationConcentrationInputs[channel], calibrationGetButtons[channel],
                            calibrationClearButtons[channel], calibrationApplyButtons[channel], calibrationSlopeFields[channel],
                            calibrationInterceptFields[channel], calibrationR2Fields[channel], calibrationPlotToggles[channel], calibrationLiveValueLabels[channel] };
        for (HWND control : controls) ShowWindow(control, tab == 2 ? SW_SHOW : SW_HIDE);
        ShowWindow(calibrationPlotToggles[channel], tab==2 && ChannelEnabled(channel) ? SW_SHOW : SW_HIDE);
        ShowWindow(calibrationLiveValueLabels[channel], tab==2 && ChannelEnabled(channel) ? SW_SHOW : SW_HIDE);
    }
    UpdateStepButtonState();
    if (!signals) {
        const wchar_t* text[] = { L"", L"Set profile limits and step size, then use Manual control for individual moves.", L"Enter a concentration for each channel and capture its current live signal to build a calibration curve.", L"Finished items will appear here.", L"" };
        SetWindowTextW(infoLabel, text[tab]);
    }
    InvalidateRect(window, nullptr, TRUE);
}

RECT ProfileGraphBounds(HWND window) {
    RECT client{};
    GetClientRect(window, &client);
    return RECT{520, 100, client.right - 20, 620};
}

RECT ProfileLegendBounds(HWND window) {
    const RECT chart=ProfileGraphBounds(window);
    const int width=chart.right-chart.left;
    return RECT{chart.left+(std::max)(300,width-720),chart.top+30,chart.right,chart.bottom-20};
}

std::vector<size_t> ProfileLegendChannels() {
    std::vector<size_t> channels;
    for(size_t channel=0;channel<8;++channel) if(ChannelEnabled(channel)) channels.push_back(channel);
    return channels;
}

void RefreshProfileLegend(HWND window) {
    if(!profileLegendWindow) return;
    RECT bounds=ProfileLegendBounds(window), current{};
    GetWindowRect(profileLegendWindow,&current);
    MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&current),2);
    if(!EqualRect(&bounds,&current)) MoveWindow(profileLegendWindow,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,TRUE);
    RECT client{}; GetClientRect(profileLegendWindow,&client);
    size_t count=0;
    { std::lock_guard<std::mutex> lock(profilePlotMutex); count=profileDatasets.size(); }
    const int page=(std::max)(1,(static_cast<int>(client.right)-300)/60);
    profileLegendFirst=(std::max)(0,(std::min)(profileLegendFirst,static_cast<int>(count)-page));
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS|SIF_DISABLENOSCROLL};
    info.nMin=0; info.nMax=(std::max)(0,static_cast<int>(count)-1); info.nPage=page; info.nPos=profileLegendFirst;
    SetScrollInfo(profileLegendWindow,SB_HORZ,&info,TRUE);
    InvalidateRect(profileLegendWindow,nullptr,FALSE);
}

void ProfileLegendChanged(HWND legend) {
    HWND owner=GetParent(legend);
    const RECT chart=ProfileGraphBounds(owner);
    InvalidateRect(owner,&chart,FALSE);
    InvalidateRect(legend,nullptr,FALSE);
}

LRESULT CALLBACK ProfileLegendProcedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC screen=BeginPaint(window,&paint);
        RECT client{}; GetClientRect(window,&client);
        HDC dc=CreateCompatibleDC(screen);
        HBITMAP bitmap=CreateCompatibleBitmap(screen,(std::max)(1L,client.right),(std::max)(1L,client.bottom));
        HGDIOBJ oldBitmap=SelectObject(dc,bitmap);
        HBRUSH background=CreateSolidBrush(RGB(250,251,253)); FillRect(dc,&client,background);
        HFONT font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HGDIOBJ oldFont=SelectObject(dc,font);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(42,52,66));
        auto text=[&](const std::wstring& value,RECT rect,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX) { DrawTextW(dc,value.c_str(),-1,&rect,flags); };
        RECT calibrated{10,9,30,29};
        DrawFrameControl(dc,&calibrated,DFC_BUTTON,DFCS_BUTTONCHECK|(profileGraphCalibrated?DFCS_CHECKED:0));
        text(L"Calibrated",RECT{38,5,170,33});
        if(GetFocus()==window && profileLegendFocusRow<0) { RECT focus{7,5,165,33}; DrawFocusRect(dc,&focus); }
        text(L"Profile no.",RECT{174,40,298,68});
        text(L"Profile label",RECT{174,70,298,98});
        struct Header { unsigned long long number; std::wstring label; std::array<bool,8> visible; };
        std::vector<Header> datasets;
        { std::lock_guard<std::mutex> lock(profilePlotMutex);
          for(auto it=profileDatasets.rbegin();it!=profileDatasets.rend();++it) datasets.push_back({it->number,it->label,it->visible}); }
        std::array<AppliedCalibration,8> calibrations{};
        { std::lock_guard<std::mutex> lock(calibrationMutex); calibrations=appliedCalibrations; }
        std::array<float,8> values{}; bool hasValues=false;
        { std::lock_guard<std::mutex> lock(dataMutex); if(!history.empty()) {values=history.back().volts;hasValues=true;} }
        const int columns=(std::max)(1,(static_cast<int>(client.right)-300)/60);
        const auto channels=ProfileLegendChannels();
        for(int col=0;col<columns;++col) {
            const size_t index=static_cast<size_t>(profileLegendFirst+col);
            if(index>=datasets.size()) break;
            const int x=300+col*60;
            Rectangle(dc,x,40,x+60,69); Rectangle(dc,x,70,x+60,99);
            text(std::to_wstring(datasets[index].number),RECT{x+3,41,x+57,68},DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            text(datasets[index].label,RECT{x+3,71,x+57,98});
        }
        for(size_t row=0;row<channels.size();++row) {
            const size_t channel=channels[row]; const int y=104+static_cast<int>(row)*40;
            HPEN pen=CreatePen(PS_SOLID,3,colors[channel]); HGDIOBJ oldPen=SelectObject(dc,pen);
            MoveToEx(dc,8,y+19,nullptr); LineTo(dc,28,y+19); SelectObject(dc,oldPen); DeleteObject(pen);
            text(FullChannelName(channel),RECT{34,y,174,y+38});
            std::wstring value;
            const double signal=ProfileSignalValue(values[channel],channel,calibrations);
            if(profileGraphCalibrated && !std::isfinite(signal)) value=L"no calib.";
            else if(!hasValues) value=L"—";
            else { wchar_t number[48]{}; swprintf_s(number,L"%.4f",std::abs(signal)<0.00005?0.0:signal); value=number; }
            text(value,RECT{176,y,239,y+38},DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            RECT all{242,y+2,296,y+36}; DrawFrameControl(dc,&all,DFC_BUTTON,DFCS_BUTTONPUSH);
            text(L"All /\nNone",RECT{244,y+3,294,y+35},DT_CENTER|DT_VCENTER|DT_NOPREFIX);
            for(int col=0;col<columns;++col) {
                const size_t index=static_cast<size_t>(profileLegendFirst+col); if(index>=datasets.size()) break;
                const int x=300+col*60; Rectangle(dc,x,y,x+60,y+39);
                RECT check{x+20,y+9,x+40,y+29};
                DrawFrameControl(dc,&check,DFC_BUTTON,DFCS_BUTTONCHECK|(datasets[index].visible[channel]?DFCS_CHECKED:0));
                if(GetFocus()==window && profileLegendFocusRow==static_cast<int>(row) && profileLegendFocusColumn==col) { RECT focus{x+5,y+3,x+55,y+35}; DrawFocusRect(dc,&focus); }
            }
        }
        if(datasets.empty()) text(L"No profile datasets yet",RECT{300,104,client.right,144});
        BitBlt(screen,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);
        SelectObject(dc,oldFont); SelectObject(dc,oldBitmap); DeleteObject(font); DeleteObject(bitmap); DeleteObject(background); DeleteDC(dc);
        EndPaint(window,&paint); return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(window);
        const int x=static_cast<short>(LOWORD(lParam)),y=static_cast<short>(HIWORD(lParam));
        if(y<36 && x<170) {
            profileLegendFocusRow=-1; profileGraphCalibrated=!profileGraphCalibrated;
            WritePrivateProfileStringW(L"Tasker",L"ProfileGraphCalibrated",profileGraphCalibrated?L"1":L"0",configFilePath.c_str());
            ProfileLegendChanged(window); return 0;
        }
        const auto channels=ProfileLegendChannels();
        const int row=(y-104)/40;
        if(y<104 || row<0 || row>=static_cast<int>(channels.size())) return 0;
        const size_t channel=channels[row];
        {
            std::lock_guard<std::mutex> lock(profilePlotMutex);
            if(x>=242 && x<296) {
                bool all=true; for(const auto& dataset:profileDatasets) all &= dataset.visible[channel];
                for(auto& dataset:profileDatasets) dataset.visible[channel]=!all;
            } else if(x>=300) {
                const int col=(x-300)/60;
                const size_t reverse=static_cast<size_t>(profileLegendFirst+col);
                if(reverse<profileDatasets.size()) {
                    auto& dataset=profileDatasets[profileDatasets.size()-1-reverse];
                    dataset.visible[channel]=!dataset.visible[channel];
                    profileLegendFocusRow=row; profileLegendFocusColumn=col;
                }
            }
        }
        ProfileLegendChanged(window); return 0;
    }
    case WM_HSCROLL: {
        SCROLLINFO info{sizeof(info),SIF_ALL}; GetScrollInfo(window,SB_HORZ,&info);
        int position=profileLegendFirst;
        switch(LOWORD(wParam)) {
        case SB_LINELEFT: --position; break; case SB_LINERIGHT: ++position; break;
        case SB_PAGELEFT: position-=info.nPage; break; case SB_PAGERIGHT: position+=info.nPage; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: position=info.nTrackPos; break;
        case SB_LEFT: position=0; break; case SB_RIGHT: position=info.nMax; break;
        }
        profileLegendFirst=(std::max)(0,(std::min)(position,info.nMax-static_cast<int>(info.nPage)+1));
        RefreshProfileLegend(GetParent(window)); return 0;
    }
    case WM_SETFOCUS: case WM_KILLFOCUS: InvalidateRect(window,nullptr,FALSE); return 0;
    case WM_KEYDOWN: {
        const auto channels=ProfileLegendChannels();
        RECT client{}; GetClientRect(window,&client);
        const int columns=(std::max)(1,(static_cast<int>(client.right)-300)/60);
        if(wParam==VK_DOWN) profileLegendFocusRow=(std::min)(static_cast<int>(channels.size())-1,profileLegendFocusRow+1);
        else if(wParam==VK_UP) profileLegendFocusRow=(std::max)(-1,profileLegendFocusRow-1);
        else if(wParam==VK_LEFT) profileLegendFocusColumn=(std::max)(0,profileLegendFocusColumn-1);
        else if(wParam==VK_RIGHT) profileLegendFocusColumn=(std::min)(columns-1,profileLegendFocusColumn+1);
        else if(wParam==VK_SPACE) {
            const int x=profileLegendFocusRow<0?10:320+profileLegendFocusColumn*60;
            const int y=profileLegendFocusRow<0?10:114+profileLegendFocusRow*40;
            SendMessageW(window,WM_LBUTTONDOWN,0,MAKELPARAM(x,y));
        } else return DefWindowProcW(window,message,wParam,lParam);
        InvalidateRect(window,nullptr,FALSE); return 0;
    }
    }
    return DefWindowProcW(window,message,wParam,lParam);
}

void CreateProfileLegend(HWND window) {
    WNDCLASSW wc{}; wc.lpfnWndProc=ProfileLegendProcedure; wc.hInstance=GetModuleHandleW(nullptr);
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.lpszClassName=L"TaskerProfileLegend";
    RegisterClassW(&wc);
    profileGraphCalibrated=GetPrivateProfileIntW(L"Tasker",L"ProfileGraphCalibrated",0,configFilePath.c_str())!=0;
    profileLegendWindow=CreateWindowExW(0,wc.lpszClassName,L"Profile legend",WS_CHILD|WS_TABSTOP|WS_HSCROLL,0,0,720,470,window,nullptr,wc.hInstance,nullptr);
    RefreshProfileLegend(window);
}

RECT CalibrationLiveGraphBounds(HWND window) {
    RECT client{}; GetClientRect(window, &client);
    const int top = 122, gap = 12, bottom = (std::max)(top + 220, static_cast<int>(client.bottom) - 125);
    const int each = (bottom - top - gap) / 2;
    return RECT{950, top, client.right - 24, top + each};
}

RECT CalibrationCurveGraphBounds(HWND window) {
    RECT client{}; GetClientRect(window, &client);
    const RECT first = CalibrationLiveGraphBounds(window);
    const int gap = 12, bottom = (std::max)(static_cast<int>(first.bottom) + gap + 180, static_cast<int>(client.bottom) - 125);
    return RECT{950, first.bottom + gap, client.right - 24, bottom};
}

void PositionCalibrationLegend(HWND window) {
    const RECT legend = CalibrationCurveGraphBounds(window);
    for (int channel = 0; channel < 8; ++channel) {
        const int y = legend.top + 48 + channel * 24;
        MoveWindow(calibrationPlotToggles[channel], legend.right - 198, y, 82, 22, TRUE);
        MoveWindow(calibrationLiveValueLabels[channel], legend.right - 112, y, 96, 22, TRUE);
    }
}

void CalibrationLiveAutoVoltageRange(const std::vector<SignalSample>& samples, double& minimum, double& maximum) {
    minimum = (std::numeric_limits<double>::max)();
    maximum = (std::numeric_limits<double>::lowest)();
    for (const auto& sample : samples) for (size_t channel=0;channel<8;++channel) { if(!ChannelEnabled(channel)) continue; const float voltage=sample.volts[channel];
        minimum = (std::min)(minimum, static_cast<double>(voltage));
        maximum = (std::max)(maximum, static_cast<double>(voltage));
    }
    if (minimum == (std::numeric_limits<double>::max)()) { minimum = -1.0; maximum = 1.0; }
    double span = maximum - minimum;
    if (span < 1e-9) span = (std::max)(1.0, std::abs(minimum) * 0.1);
    minimum -= span * 0.08;
    maximum += span * 0.08;
}

void PaintCalibrationGraphs(HWND window, HDC dc) {
    const RECT liveChart = CalibrationLiveGraphBounds(window);
    const RECT curveChart = CalibrationCurveGraphBounds(window);
    if (liveChart.right - liveChart.left < 300 || curveChart.right - curveChart.left < 300) return;
    const int saved = SaveDC(dc);
    const COLORREF backgroundColor = RGB(250, 251, 253);
    HBRUSH background = CreateSolidBrush(backgroundColor);
    FillRect(dc, &liveChart, background);
    FillRect(dc, &curveChart, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(42, 52, 66));
    HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(221, 226, 233));
    HPEN axisPen = CreatePen(PS_SOLID, 1, RGB(76, 86, 100));
    HGDIOBJ oldPen = SelectObject(dc, gridPen);
    const wchar_t liveTitle[] = L"Live signals";
    TextOutW(dc, liveChart.left + 10, liveChart.top + 7, liveTitle, static_cast<int>(std::size(liveTitle) - 1));

    std::vector<SignalSample> samples;
    double viewSeconds = displayLengthSeconds.load();
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        if (!history.empty()) {
            const double newest = history.back().seconds;
            const double cutoff = newest - viewSeconds;
            for (const auto& sample : history) if (sample.seconds >= cutoff && sample.seconds <= newest) samples.push_back(sample);
        }
    }
    const int liveLeft = liveChart.left + 100, liveRight = liveChart.right - 100;
    const int liveTop = liveChart.top + 34, liveBottom = liveChart.bottom - 43;
    double minVoltage = 0.0, maxVoltage = 0.0;
    CalibrationLiveAutoVoltageRange(samples, minVoltage, maxVoltage);
    if (calibrationLiveManualYScale) { minVoltage = calibrationLiveMinV; maxVoltage = calibrationLiveMaxV; }
    for (int tick = 0; tick <= 4; ++tick) {
        const int x = liveLeft + (liveRight - liveLeft) * tick / 4;
        const int y = liveBottom - (liveBottom - liveTop) * tick / 4;
        MoveToEx(dc, x, liveTop, nullptr); LineTo(dc, x, liveBottom);
        MoveToEx(dc, liveLeft, y, nullptr); LineTo(dc, liveRight, y);
        wchar_t label[32]{};
        const double secondsAgo = viewSeconds * (4 - tick) / 4.0;
        swprintf_s(label, L"-%.3g", secondsAgo);
        SIZE extent{}; GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &extent);
        TextOutW(dc, x - extent.cx / 2, liveBottom + 5, label, static_cast<int>(wcslen(label)));
        swprintf_s(label, L"%.4g", minVoltage + (maxVoltage - minVoltage) * tick / 4.0);
        GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &extent);
        TextOutW(dc, liveLeft - extent.cx - 6, y - extent.cy / 2, label, static_cast<int>(wcslen(label)));
    }
    SelectObject(dc, axisPen);
    Rectangle(dc, liveLeft, liveTop, liveRight + 1, liveBottom + 1);
    const int plotSave = SaveDC(dc);
    IntersectClipRect(dc, liveLeft, liveTop, liveRight + 1, liveBottom + 1);
    const int liveWidth = liveRight - liveLeft + 1;
    if (!samples.empty() && viewSeconds > 0.0) {
        const double newest = samples.back().seconds;
        std::array<std::vector<int>, 8> minY, maxY, lastY;
        for (size_t ch = 0; ch < 8; ++ch) { if (!ChannelEnabled(ch)) continue;
            minY[ch].assign(static_cast<size_t>(liveWidth), liveBottom + 1);
            maxY[ch].assign(static_cast<size_t>(liveWidth), liveTop - 1);
            lastY[ch].assign(static_cast<size_t>(liveWidth), liveBottom + 1);
            for (const auto& sample : samples) {
                const int x = liveLeft + static_cast<int>((liveRight - liveLeft) * (sample.seconds - (newest - viewSeconds)) / viewSeconds);
                if (x < liveLeft || x > liveRight) continue;
                const double value = sample.volts[ch];
                const int y = liveBottom - static_cast<int>(std::lround((liveBottom - liveTop) * (value - minVoltage) / (maxVoltage - minVoltage)));
                const size_t column = static_cast<size_t>(x - liveLeft);
                minY[ch][column] = (std::min)(minY[ch][column], y);
                maxY[ch][column] = (std::max)(maxY[ch][column], y);
                lastY[ch][column] = y;
            }
            HPEN pen = CreatePen(PS_SOLID, 2, colors[ch]); HGDIOBJ previous = SelectObject(dc, pen);
            bool previousColumn = false; int previousX = 0, previousY = 0;
            for (int column = 0; column < liveWidth; ++column) {
                if (minY[ch][static_cast<size_t>(column)] > maxY[ch][static_cast<size_t>(column)]) { previousColumn = false; continue; }
                const int x = liveLeft + column;
                MoveToEx(dc, x, minY[ch][static_cast<size_t>(column)], nullptr);
                LineTo(dc, x, maxY[ch][static_cast<size_t>(column)] + 1);
                const int y = lastY[ch][static_cast<size_t>(column)];
                if (previousColumn) { MoveToEx(dc, previousX, previousY, nullptr); LineTo(dc, x, y); }
                previousColumn = true; previousX = x; previousY = y;
            }
            SelectObject(dc, previous); DeleteObject(pen);
        }
    } else {
        SetTextColor(dc, RGB(115, 125, 140));
        const wchar_t waiting[] = L"Waiting for live DAQ samples...";
        TextOutW(dc, liveLeft + 16, liveTop + 20, waiting, static_cast<int>(std::size(waiting) - 1));
    }
    RestoreDC(dc, plotSave);
    for (size_t ch = 0; ch < 8; ++ch) { if (!ChannelEnabled(ch)) continue;
        const int y = liveTop + static_cast<int>(ch) * 20;
        HPEN pen = CreatePen(PS_SOLID, 3, colors[ch]); HGDIOBJ previous = SelectObject(dc, pen);
        MoveToEx(dc, liveRight + 12, y + 8, nullptr); LineTo(dc, liveRight + 30, y + 8);
        SelectObject(dc, previous); DeleteObject(pen);
        wchar_t name[16]{}; swprintf_s(name, L"CH%u", static_cast<unsigned>(ch));
        TextOutW(dc, liveRight + 34, y, name, static_cast<int>(wcslen(name)));
    }
    const wchar_t liveXLabel[] = L"Time (s)";
    SIZE extent{}; GetTextExtentPoint32W(dc, liveXLabel, static_cast<int>(std::size(liveXLabel) - 1), &extent);
    TextOutW(dc, (liveLeft + liveRight - extent.cx) / 2, liveChart.bottom - 22, liveXLabel, static_cast<int>(std::size(liveXLabel) - 1));
    HFONT verticalFont = CreateFontW(-13, 0, 900, 900, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(dc, verticalFont);
    const wchar_t liveYLabel[] = L"Signal (V)";
    TextOutW(dc, liveChart.left + 13, (liveTop + liveBottom) / 2, liveYLabel, static_cast<int>(std::size(liveYLabel) - 1));
    SelectObject(dc, font);

    const wchar_t curveTitle[] = L"Calibration curves";
    SetTextColor(dc, RGB(42, 52, 66));
    TextOutW(dc, curveChart.left + 10, curveChart.top + 7, curveTitle, static_cast<int>(std::size(curveTitle) - 1));
    std::array<std::vector<CalibrationSample>, 8> calibration;
    std::array<bool, 8> selected{};
    {
        std::lock_guard<std::mutex> lock(calibrationMutex);
        calibration = calibrationData;
    }
    for (size_t ch = 0; ch < 8; ++ch) selected[ch] = ChannelEnabled(ch) && SendMessageW(calibrationPlotToggles[ch], BM_GETCHECK, 0, 0) == BST_CHECKED;
    const int curveLeft = curveChart.left + 100, curveRight = curveChart.right - 210;
    const int curveTop = curveChart.top + 38, curveBottom = curveChart.bottom - 46;
    double minConcentration = (std::numeric_limits<double>::max)(), maxConcentration = (std::numeric_limits<double>::lowest)();
    double minSignal = (std::numeric_limits<double>::max)(), maxSignal = (std::numeric_limits<double>::lowest)();
    for (size_t ch = 0; ch < 8; ++ch) if (selected[ch]) for (const auto& sample : calibration[ch]) {
        minConcentration = (std::min)(minConcentration, sample.concentration); maxConcentration = (std::max)(maxConcentration, sample.concentration);
        minSignal = (std::min)(minSignal, sample.signal); maxSignal = (std::max)(maxSignal, sample.signal);
    }
    const bool hasCalibration = minConcentration != (std::numeric_limits<double>::max)();
    if (!hasCalibration) { minConcentration = 0.0; maxConcentration = 1.0; minSignal = -1.0; maxSignal = 1.0; }
    auto pad = [](double& low, double& high) { double span = high - low; if (span < 1e-9) span = (std::max)(1.0, std::abs(low) * 0.1); low -= span * 0.08; high += span * 0.08; };
    pad(minConcentration, maxConcentration); pad(minSignal, maxSignal);
    SelectObject(dc, gridPen);
    for (int tick = 0; tick <= 4; ++tick) {
        const int x = curveLeft + (curveRight - curveLeft) * tick / 4;
        const int y = curveBottom - (curveBottom - curveTop) * tick / 4;
        MoveToEx(dc, x, curveTop, nullptr); LineTo(dc, x, curveBottom);
        MoveToEx(dc, curveLeft, y, nullptr); LineTo(dc, curveRight, y);
        wchar_t label[32]{};
        swprintf_s(label, L"%.4g", minConcentration + (maxConcentration - minConcentration) * tick / 4.0);
        SIZE size{}; GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &size);
        TextOutW(dc, x - size.cx / 2, curveBottom + 5, label, static_cast<int>(wcslen(label)));
        swprintf_s(label, L"%.4g", minSignal + (maxSignal - minSignal) * tick / 4.0);
        GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &size);
        TextOutW(dc, curveLeft - size.cx - 6, y - size.cy / 2, label, static_cast<int>(wcslen(label)));
    }
    SelectObject(dc, axisPen);
    Rectangle(dc, curveLeft, curveTop, curveRight + 1, curveBottom + 1);
    const int curveSave = SaveDC(dc);
    IntersectClipRect(dc, curveLeft, curveTop, curveRight + 1, curveBottom + 1);
    for (size_t ch = 0; ch < 8; ++ch) { if (!ChannelEnabled(ch)) continue;
        if (!selected[ch] || calibration[ch].empty()) continue;
        const auto& channelSamples = calibration[ch];
        const size_t markerStride = (std::max)(static_cast<size_t>(1), channelSamples.size() / 1000);
        HPEN markerPen = CreatePen(PS_SOLID, 1, colors[ch]);
        HBRUSH markerBrush = CreateSolidBrush(colors[ch]);
        HGDIOBJ oldChannelPen = SelectObject(dc, markerPen);
        HGDIOBJ oldBrush = SelectObject(dc, markerBrush);
        for (size_t i = 0; i < channelSamples.size(); i += markerStride) {
            const auto& sample = channelSamples[i];
            const int x = curveLeft + static_cast<int>(std::lround((curveRight - curveLeft) * (sample.concentration - minConcentration) / (maxConcentration - minConcentration)));
            const int y = curveBottom - static_cast<int>(std::lround((curveBottom - curveTop) * (sample.signal - minSignal) / (maxSignal - minSignal)));
            Ellipse(dc, x - 3, y - 3, x + 4, y + 4);
        }
        if (channelSamples.size() >= 2) {
            long double meanX = 0.0L, meanY = 0.0L;
            for (const auto& sample : channelSamples) { meanX += sample.concentration; meanY += sample.signal; }
            meanX /= channelSamples.size(); meanY /= channelSamples.size();
            long double xx = 0.0L, xy = 0.0L;
            for (const auto& sample : channelSamples) { const long double dx = sample.concentration - meanX; xx += dx * dx; xy += dx * (sample.signal - meanY); }
            if (xx > std::numeric_limits<long double>::epsilon()) {
                const long double slope = xy / xx, intercept = meanY - slope * meanX;
                const double x0 = channelSamples.front().concentration;
                double lowX = x0, highX = x0;
                for (const auto& sample : channelSamples) { lowX = (std::min)(lowX, sample.concentration); highX = (std::max)(highX, sample.concentration); }
                const int x1 = curveLeft + static_cast<int>(std::lround((curveRight - curveLeft) * (lowX - minConcentration) / (maxConcentration - minConcentration)));
                const int x2 = curveLeft + static_cast<int>(std::lround((curveRight - curveLeft) * (highX - minConcentration) / (maxConcentration - minConcentration)));
                const double y1v = static_cast<double>(slope * lowX + intercept), y2v = static_cast<double>(slope * highX + intercept);
                const int y1 = curveBottom - static_cast<int>(std::lround((curveBottom - curveTop) * (y1v - minSignal) / (maxSignal - minSignal)));
                const int y2 = curveBottom - static_cast<int>(std::lround((curveBottom - curveTop) * (y2v - minSignal) / (maxSignal - minSignal)));
                MoveToEx(dc, x1, y1, nullptr); LineTo(dc, x2, y2);
            }
        }
        SelectObject(dc, oldBrush); SelectObject(dc, oldChannelPen);
        DeleteObject(markerBrush); DeleteObject(markerPen);
    }
    RestoreDC(dc, curveSave);
    if (!hasCalibration) {
        SetTextColor(dc, RGB(115, 125, 140));
        const wchar_t emptyText[] = L"No selected calibration data to plot";
        TextOutW(dc, curveLeft + 18, curveTop + 16, emptyText, static_cast<int>(std::size(emptyText) - 1));
    }
    const wchar_t legendChannel[] = L"Channel";
    const wchar_t legendValue[] = L"Live";
    TextOutW(dc, curveChart.right - 198, curveChart.top + 25, legendChannel, static_cast<int>(std::size(legendChannel) - 1));
    TextOutW(dc, curveChart.right - 112, curveChart.top + 25, legendValue, static_cast<int>(std::size(legendValue) - 1));
    const wchar_t xLabel[] = L"Concentration";
    SIZE xExtent{}; GetTextExtentPoint32W(dc, xLabel, static_cast<int>(wcslen(xLabel)), &xExtent);
    TextOutW(dc, (curveLeft + curveRight - xExtent.cx) / 2, curveChart.bottom - 22, xLabel, static_cast<int>(wcslen(xLabel)));
    SelectObject(dc, verticalFont);
    const wchar_t yLabel[] = L"Signal (V)";
    TextOutW(dc, curveChart.left + 14, (curveTop + curveBottom) / 2, yLabel, static_cast<int>(std::size(yLabel) - 1));
    SelectObject(dc, oldFont);
    SelectObject(dc, oldPen);
    DeleteObject(verticalFont); DeleteObject(font); DeleteObject(gridPen); DeleteObject(axisPen);
    RestoreDC(dc, saved);
}

bool EnsureCalibrationGraphSurface(HWND window) {
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left, height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return false;
    if (calibrationGraphCache.dc && calibrationGraphCache.width == width && calibrationGraphCache.height == height) return true;
    if (calibrationGraphCache.dc) {
        SelectObject(calibrationGraphCache.dc, calibrationGraphCache.previousBitmap);
        DeleteObject(calibrationGraphCache.bitmap);
        DeleteDC(calibrationGraphCache.dc);
        calibrationGraphCache = {};
    }
    HDC screen = GetDC(window);
    calibrationGraphCache.dc = CreateCompatibleDC(screen);
    calibrationGraphCache.bitmap = CreateCompatibleBitmap(screen, width, height);
    ReleaseDC(window, screen);
    if (!calibrationGraphCache.dc || !calibrationGraphCache.bitmap) {
        if (calibrationGraphCache.dc) DeleteDC(calibrationGraphCache.dc);
        if (calibrationGraphCache.bitmap) DeleteObject(calibrationGraphCache.bitmap);
        calibrationGraphCache = {};
        return false;
    }
    calibrationGraphCache.previousBitmap = SelectObject(calibrationGraphCache.dc, calibrationGraphCache.bitmap);
    calibrationGraphCache.width = width;
    calibrationGraphCache.height = height;
    return true;
}

void RenderCalibrationGraphsCached(HWND window) {
    if (!EnsureCalibrationGraphSurface(window)) return;
    PaintCalibrationGraphs(window, calibrationGraphCache.dc);
    const RECT chart = CalibrationLiveGraphBounds(window);
    const int left = chart.left + 100, right = chart.right - 100;
    const int top = chart.top + 34, bottom = chart.bottom - 43;
    const double seconds = displayLengthSeconds.load();
    std::vector<SignalSample> visible;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        if (!history.empty()) {
            const double end = history.back().seconds;
            for (const auto& sample : history) if (sample.seconds >= end - seconds && sample.seconds <= end) visible.push_back(sample);
        }
    }
    calibrationGraphCache.viewSeconds = seconds;
    calibrationGraphCache.lastSequence = visible.empty() ? 0 : visible.back().sequence;
    calibrationGraphCache.viewEnd = visible.empty() ? 0.0 : visible.back().seconds;
    CalibrationLiveAutoVoltageRange(visible, calibrationGraphCache.minV, calibrationGraphCache.maxV);
    if (calibrationLiveManualYScale) {
        calibrationGraphCache.minV = calibrationLiveMinV;
        calibrationGraphCache.maxV = calibrationLiveMaxV;
    }
    calibrationGraphCache.hasLastPoint.fill(false);
    if (!visible.empty() && seconds > 0.0 && right > left && bottom > top) {
        const double start = calibrationGraphCache.viewEnd - seconds;
        for (int channel = 0; channel < 8; ++channel) { if (!ChannelEnabled(channel)) continue;
            for (const auto& sample : visible) {
                if (sample.sequence == calibrationGraphCache.lastSequence) {
                    const int x = left + static_cast<int>(std::lround((right - left) * (sample.seconds - start) / seconds));
                    const double value = (std::max)(calibrationGraphCache.minV, (std::min)(calibrationGraphCache.maxV, static_cast<double>(sample.volts[channel])));
                    const int y = bottom - static_cast<int>(std::lround((bottom - top) * (value - calibrationGraphCache.minV) / (calibrationGraphCache.maxV - calibrationGraphCache.minV)));
                    calibrationGraphCache.lastPoints[channel] = POINT{x, y};
                    calibrationGraphCache.hasLastPoint[channel] = true;
                    break;
                }
            }
        }
    }
    calibrationGraphCache.valid = true;
}

void UpdateCalibrationGraphIncrementally(HWND window) {
    if (!EnsureCalibrationGraphSurface(window)) return;
    if (!calibrationGraphCache.valid || std::abs(calibrationGraphCache.viewSeconds - displayLengthSeconds.load()) > 1e-9) {
        RenderCalibrationGraphsCached(window);
        const RECT chart = CalibrationLiveGraphBounds(window);
        InvalidateRect(window, &chart, FALSE);
        const RECT curve = CalibrationCurveGraphBounds(window);
        InvalidateRect(window, &curve, FALSE);
        return;
    }
    std::vector<SignalSample> newSamples;
    bool historyLost = false;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        if (!history.empty()) {
            historyLost = calibrationGraphCache.lastSequence && history.front().sequence > calibrationGraphCache.lastSequence + 1;
            for (const auto& sample : history) if (sample.sequence > calibrationGraphCache.lastSequence) newSamples.push_back(sample);
        }
    }
    if (historyLost) {
        calibrationGraphCache.valid = false;
        UpdateCalibrationGraphIncrementally(window);
        return;
    }
    if (newSamples.empty()) return;
    const RECT chart = CalibrationLiveGraphBounds(window);
    const int left = chart.left + 100, right = chart.right - 100;
    const int top = chart.top + 34, bottom = chart.bottom - 43;
    const int width = right - left;
    const double seconds = calibrationGraphCache.viewSeconds;
    const double newEnd = newSamples.back().seconds;
    const int dx = static_cast<int>(std::floor((newEnd - calibrationGraphCache.viewEnd) * width / seconds + 0.5));
    if (dx < 0 || dx >= width || (calibrationGraphCache.lastSequence < 20)) {
        calibrationGraphCache.valid = false;
        UpdateCalibrationGraphIncrementally(window);
        return;
    }
    bool outsideRange = false;
    for (const auto& sample : newSamples) for (size_t channel=0;channel<8;++channel)
        if (ChannelEnabled(channel) && (sample.volts[channel] < calibrationGraphCache.minV || sample.volts[channel] > calibrationGraphCache.maxV)) outsideRange = true;
    if (outsideRange) {
        calibrationGraphCache.valid = false;
        UpdateCalibrationGraphIncrementally(window);
        return;
    }
    RECT exposed{};
    if (dx > 0) {
        // Move cached pixels without copying axis borders into the plot.
        BitBlt(calibrationGraphCache.dc, left + 1, top + 1,
            (std::max)(0, right - left - 1 - dx), bottom - top - 1,
            calibrationGraphCache.dc, left + 1 + dx, top + 1, SRCCOPY);
        exposed = RECT{(std::max)(left + 1, right - dx), top + 1, right, bottom};
        for (POINT& point : calibrationGraphCache.lastPoints) point.x -= dx;
    } else {
        exposed = RECT{right - 3, top + 1, right, bottom};
    }
    if (exposed.left < left) exposed.left = left;
    if (exposed.right > right + 1) exposed.right = right + 1;
    const int gridClip = SaveDC(calibrationGraphCache.dc);
    IntersectClipRect(calibrationGraphCache.dc, exposed.left, exposed.top, exposed.right, exposed.bottom);
    HBRUSH bg = CreateSolidBrush(RGB(250, 251, 253));
    FillRect(calibrationGraphCache.dc, &exposed, bg);
    DeleteObject(bg);
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(221, 226, 233));
    HGDIOBJ oldPen = SelectObject(calibrationGraphCache.dc, grid);
    for (int tick = 0; tick <= 4; ++tick) {
        const int y = bottom - (bottom - top) * tick / 4;
        MoveToEx(calibrationGraphCache.dc, exposed.left, y, nullptr);
        LineTo(calibrationGraphCache.dc, exposed.right, y);
        const int x = left + width * tick / 4;
        if (x >= exposed.left && x < exposed.right) {
            MoveToEx(calibrationGraphCache.dc, x, top, nullptr);
            LineTo(calibrationGraphCache.dc, x, bottom);
        }
    }
    SelectObject(calibrationGraphCache.dc, oldPen);
    RestoreDC(calibrationGraphCache.dc, gridClip);
    DeleteObject(grid);
    const int saved = SaveDC(calibrationGraphCache.dc);
    IntersectClipRect(calibrationGraphCache.dc, left, top, right + 1, bottom + 1);
    const double timeStart = newEnd - seconds;
    for (int channel = 0; channel < 8; ++channel) { if (!ChannelEnabled(channel)) continue;
        HPEN pen = CreatePen(PS_SOLID, 2, colors[channel]);
        HGDIOBJ old = SelectObject(calibrationGraphCache.dc, pen);
        for (const auto& sample : newSamples) {
            const int x = left + static_cast<int>(std::lround(width * (sample.seconds - timeStart) / seconds));
            if (x < left || x > right) continue;
            const double value = (std::max)(calibrationGraphCache.minV, (std::min)(calibrationGraphCache.maxV, static_cast<double>(sample.volts[channel])));
            const int y = bottom - static_cast<int>(std::lround((bottom - top) * (value - calibrationGraphCache.minV) / (calibrationGraphCache.maxV - calibrationGraphCache.minV)));
            if (calibrationGraphCache.hasLastPoint[channel]) MoveToEx(calibrationGraphCache.dc, calibrationGraphCache.lastPoints[channel].x, calibrationGraphCache.lastPoints[channel].y, nullptr);
            else MoveToEx(calibrationGraphCache.dc, x, y, nullptr);
            LineTo(calibrationGraphCache.dc, x, y);
            calibrationGraphCache.lastPoints[channel] = POINT{x, y};
            calibrationGraphCache.hasLastPoint[channel] = true;
        }
        SelectObject(calibrationGraphCache.dc, old);
        DeleteObject(pen);
    }
    RestoreDC(calibrationGraphCache.dc, saved);
    calibrationGraphCache.viewEnd = newEnd;
    calibrationGraphCache.lastSequence = newSamples.back().sequence;
    const RECT border{left, top, right + 1, bottom + 1};
    InvalidateRect(window, &border, FALSE);
}

void PaintProfileGraph(HWND window, HDC dc) {
    const RECT chart = ProfileGraphBounds(window);
    if (chart.right - chart.left < 300 || chart.bottom - chart.top < 220) return;
    const int saved = SaveDC(dc);
    HBRUSH background = CreateSolidBrush(RGB(250, 251, 253));
    FillRect(dc, &chart, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(42, 52, 66));

    std::vector<ProfileDataset> datasets;
    std::array<AppliedCalibration,8> calibrations{};
    { std::lock_guard<std::mutex> lock(calibrationMutex); calibrations=appliedCalibrations; }
    {
        std::lock_guard<std::mutex> lock(profilePlotMutex);
        datasets = profileDatasets;
    }
    const int left = chart.left + 82;
    const int right = ProfileLegendBounds(window).left - 20;
    const int top = chart.top + 42;
    const int bottom = chart.bottom - 62;
    if (right <= left || bottom <= top) { RestoreDC(dc, saved); return; }

    double minSignal = (std::numeric_limits<double>::max)();
    double maxSignal = (std::numeric_limits<double>::lowest)();
    double minPosition = (std::numeric_limits<double>::max)();
    double maxPosition = (std::numeric_limits<double>::lowest)();
    for (const auto& dataset : datasets) for (const auto& point : dataset.points) {
        bool datasetShown=false;
        for(size_t channel=0;channel<8;++channel) datasetShown |= ChannelEnabled(channel) && dataset.visible[channel];
        if (!datasetShown) continue;
        if (!std::isfinite(point.positionMicrons)) continue;
        minPosition = (std::min)(minPosition, point.positionMicrons);
        maxPosition = (std::max)(maxPosition, point.positionMicrons);
        for (int channel = 0; channel < 8; ++channel) { if (!ChannelEnabled(channel)) continue; const double signal = ProfileSignalValue(point.signals[channel],channel,calibrations); if (ChannelEnabled(channel) && dataset.visible[channel] && std::isfinite(signal)) {
            minSignal = (std::min)(minSignal, signal);
            maxSignal = (std::max)(maxSignal, signal);
        } }
    }
    if (minPosition == (std::numeric_limits<double>::max)()) { minPosition = -1.0; maxPosition = 1.0; }
    if (minSignal == (std::numeric_limits<double>::max)()) { minSignal = -1.0; maxSignal = 1.0; }
    auto padRange = [](double& low, double& high) {
        double span = high - low;
        if (!std::isfinite(span) || span < 1e-9) span = (std::max)(1.0, std::abs(low) * 0.1);
        low -= span * 0.08;
        high += span * 0.08;
    };
    padRange(minSignal, maxSignal);
    double positionSpan = maxPosition - minPosition;
    if (!std::isfinite(positionSpan) || positionSpan < 1e-9)
        positionSpan = (std::max)(1.0, std::abs((minPosition + maxPosition) * 0.5) * 0.001);
    const double positionPadding = positionSpan * 0.015;
    minPosition -= positionPadding;
    maxPosition += positionPadding;
    const double signalStep = NiceAxisStep((maxSignal - minSignal) / 5.0);
    const double positionStep = NiceAxisStep((maxPosition - minPosition) / 8.0);
    minSignal = std::floor(minSignal / signalStep) * signalStep;
    maxSignal = std::ceil(maxSignal / signalStep) * signalStep;
    minPosition = std::floor(minPosition / positionStep) * positionStep;
    maxPosition = std::ceil(maxPosition / positionStep) * positionStep;
    if (maxSignal <= minSignal) maxSignal = minSignal + signalStep;
    if (maxPosition <= minPosition) maxPosition = minPosition + positionStep;

    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(221, 226, 233));
    HPEN axisPen = CreatePen(PS_SOLID, 1, RGB(76, 86, 100));
    HGDIOBJ oldPen = SelectObject(dc, gridPen);
    HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    const wchar_t title[] = L"Stepped scan signals by position";
    TextOutW(dc, chart.left + 12, chart.top + 10, title, static_cast<int>(std::size(title) - 1));

    for (double signal = minSignal; signal <= maxSignal + signalStep * 1e-8; signal += signalStep) {
        const int x = left + static_cast<int>(std::lround((right - left) * (signal - minSignal) / (maxSignal - minSignal)));
        MoveToEx(dc, x, top, nullptr); LineTo(dc, x, bottom);
        wchar_t label[32]{};
        swprintf_s(label, L"%.6g", std::abs(signal) < signalStep * 1e-8 ? 0.0 : signal);
        SIZE extent{}; GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &extent);
        TextOutW(dc, x - extent.cx / 2, bottom + 7, label, static_cast<int>(wcslen(label)));
    }
    for (double position = minPosition; position <= maxPosition + positionStep * 1e-8; position += positionStep) {
        const int y = top + static_cast<int>(std::lround((bottom - top) * (position - minPosition) / (maxPosition - minPosition)));
        MoveToEx(dc, left, y, nullptr); LineTo(dc, right, y);
        wchar_t label[32]{};
        SIZE extent{};
        swprintf_s(label, L"%.6g", position == 0.0 ? 0.0 : position);
        GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &extent);
        TextOutW(dc, left - extent.cx - 9, y - extent.cy / 2, label, static_cast<int>(wcslen(label)));
    }
    SelectObject(dc, axisPen);
    Rectangle(dc, left, top, right + 1, bottom + 1);

    const int plotSave = SaveDC(dc);
    IntersectClipRect(dc, left, top, right + 1, bottom + 1);
    for (int channel = 0; channel < 8; ++channel) { if (!ChannelEnabled(channel)) continue;
        HPEN pen = CreatePen(PS_SOLID, 2, colors[channel]);
        HGDIOBJ previous = SelectObject(dc, pen);
        for (const auto& dataset : datasets) {
        if (!dataset.visible[channel]) continue;
        const auto& points = dataset.points;
        bool havePrevious = false;
        POINT previousPoint{};
        for (const auto& point : points) {
            const double signal = ProfileSignalValue(point.signals[static_cast<size_t>(channel)],channel,calibrations);
            if (!std::isfinite(signal) || !std::isfinite(point.positionMicrons)) { havePrevious = false; continue; }
            const int x = left + static_cast<int>(std::lround((right - left) * (signal - minSignal) / (maxSignal - minSignal)));
            const int y = top + static_cast<int>(std::lround((bottom - top) * (point.positionMicrons - minPosition) / (maxPosition - minPosition)));
            if (havePrevious) { MoveToEx(dc, previousPoint.x, previousPoint.y, nullptr); LineTo(dc, x, y); }
            if (points.size() <= 400) Ellipse(dc, x - 3, y - 3, x + 4, y + 4);
            previousPoint = POINT{x, y};
            havePrevious = true;
        }
        }
        SelectObject(dc, previous); DeleteObject(pen);
    }
    RestoreDC(dc, plotSave);

    const wchar_t* xLabel = profileGraphCalibrated ? L"Calibrated signals" : L"Signals (V)";
    SIZE xExtent{}; GetTextExtentPoint32W(dc, xLabel, static_cast<int>(wcslen(xLabel)), &xExtent);
    TextOutW(dc, (left + right - xExtent.cx) / 2, chart.bottom - 25, xLabel, static_cast<int>(wcslen(xLabel)));
    HFONT verticalFont = CreateFontW(-15, 0, 900, 900, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(dc, verticalFont);
    const wchar_t yLabel[] = L"Depth (um)";
    TextOutW(dc, chart.left + 18, (top + bottom) / 2, yLabel, static_cast<int>(std::size(yLabel) - 1));
    SelectObject(dc, oldFont);
    SelectObject(dc, oldPen);
    DeleteObject(verticalFont);
    DeleteObject(font);
    DeleteObject(gridPen);
    DeleteObject(axisPen);
    RestoreDC(dc, saved);
}

void FitProfilingNumericFields(HWND window) {
    const HWND fields[] = {mocoPositionInput, stepInput, goToInput, setPositionInput,
        profilingStartInput, profileEndInput, profileStepInput, profileSafeInput,
        profileWaitInput, profileMeasureInput, profileOvershootInput, profileOvershootWaitInput,
        profileCyclesInput, profileCycleIntervalInput};
    HDC dc = GetDC(window);
    if (!dc) return;
    for (HWND field : fields) {
        if (!field) continue;
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(field, WM_GETFONT, 0, 0));
        HGDIOBJ previous = SelectObject(dc, font ? font : GetStockObject(SYSTEM_FONT));
        SIZE digits{};
        GetTextExtentPoint32W(dc, L"0000000", 7, &digits);
        SelectObject(dc, previous);
        RECT bounds{}; GetWindowRect(field, &bounds);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
        MoveWindow(field, bounds.left, bounds.top, digits.cx + 12, bounds.bottom - bounds.top, TRUE);
    }
    ReleaseDC(window, dc);
}

bool FocusNextTabStop(HWND window, bool reverse) {
    std::vector<HWND> controls;
    for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
        if ((style & WS_TABSTOP) && IsWindowVisible(child) && IsWindowEnabled(child)) controls.push_back(child);
    }
    // Child windows are inserted at the front of the z-order. Reverse that
    // enumeration to follow their natural creation/tab order.
    std::reverse(controls.begin(), controls.end());
    if (controls.empty()) return false;
    const HWND focused = GetFocus();
    const auto current = std::find(controls.begin(), controls.end(), focused);
    size_t next = 0;
    if (current != controls.end()) {
        const size_t index = static_cast<size_t>(current - controls.begin());
        next = reverse ? (index + controls.size() - 1) % controls.size() : (index + 1) % controls.size();
    } else if (reverse) next = controls.size() - 1;
    SetFocus(controls[next]);
    return true;
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
    for (size_t channel=0;channel<sums.size();++channel) { file << '\t'; if(ChannelEnabled(channel)) file << (sums[channel]/static_cast<double>(samples)); }
    std::array<AppliedCalibration, 8> calibrations{};
    { std::lock_guard<std::mutex> lock(calibrationMutex); calibrations = appliedCalibrations; }
    for (size_t channel = 0; channel < calibrations.size(); ++channel) {
        file << '\t';
        const auto& calibration = calibrations[channel];
        if (ChannelEnabled(channel) && calibration.valid && std::abs(calibration.slope) >= 1e-15) {
            const double voltage = sums[channel] / static_cast<double>(samples);
            file << ((voltage - calibration.intercept) / calibration.slope);
        }
    }
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
    daqConnected.store(true);
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
    auto channelGeneration = channelSettingsGeneration.load();
    unsigned long long samples = 0;

    while (!shutdownRequested.load()) {
        if(channelGeneration != channelSettingsGeneration.load()) {
            channelGeneration = channelSettingsGeneration.load(); sums.fill(0.0); samples=0;
        }
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
            file << "timestamp_local\tCH0_V\tCH1_V\tCH2_V\tCH3_V\tCH4_V\tCH5_V\tCH6_V\tCH7_V"
                    "\tCH0_calibrated\tCH1_calibrated\tCH2_calibrated\tCH3_calibrated"
                    "\tCH4_calibrated\tCH5_calibrated\tCH6_calibrated\tCH7_calibrated\tComment\n";
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
        std::array<bool,8> enabled{}; std::array<int,8> ranges{};
        bool anySelected=false;
        for(size_t channel=0;channel<8;++channel) { enabled[channel]=ChannelEnabled(channel); ranges[channel]=channelRanges[channel].load(); anySelected |= enabled[channel]; }
        if(!anySelected) Sleep(10);
        if (!daq.ReadAll(volts, error, enabled, ranges)) {
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
        {
            std::lock_guard<std::mutex> lock(profileCaptureMutex);
            if (profileCapture.active && sampleTime >= profileCapture.start && sampleTime <= profileCapture.end) {
                for (size_t channel = 0; channel < volts.size(); ++channel) profileCapture.sums[channel] += volts[channel];
                ++profileCapture.samples;
            }
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
    daqConnected.store(false);
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
    for(int ch=0;ch<8;++ch){ if(!ChannelEnabled(ch)) continue;
        const int y=top+ch*34;
        HPEN pen=CreatePen(PS_SOLID,3,colors[ch]); old=SelectObject(dc,pen);
        MoveToEx(dc,legendX,y+8,nullptr); LineTo(dc,legendX+20,y+8); SelectObject(dc,old); DeleteObject(pen);
        wchar_t voltage[32]{}; swprintf_s(voltage,L"%+.4f V",values[ch]); SetTextColor(dc,RGB(55,65,80));
        const std::wstring label=FullChannelName(ch);
        RECT nameRect{legendX+28,y,graphCache.width-8,y+18};
        DrawTextW(dc,label.c_str(),-1,&nameRect,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        TextOutW(dc,legendX+28,y+17,voltage,static_cast<int>(wcslen(voltage)));
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
    const int left=72,right=graphCache.width-340,top=66,bottom=graphCache.height-62;
    if(right<=left || bottom<=top) return;
    comments=CopyGraphCommentsForPlotRange(viewEnd,viewSeconds,left,right,left,right);
    double minV=-0.01,maxV=0.01;
    if(graphCache.manualYScale){
        minV=graphCache.requestedMinV; maxV=graphCache.requestedMaxV;
    }else if(!visible.empty()){
        minV=(std::numeric_limits<double>::max)(); maxV=(std::numeric_limits<double>::lowest)();
        for(const auto& sample:visible) for(size_t ch=0;ch<8;++ch) if(ChannelEnabled(ch)){ const float v=sample.volts[ch]; minV=(std::min)(minV,static_cast<double>(v)); maxV=(std::max)(maxV,static_cast<double>(v)); } if(minV>maxV){minV=-0.01;maxV=0.01;}
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
        wchar_t label[32]{}; swprintf_s(label,L"%.4g V",std::abs(value) < yStep * 1e-8 ? 0.0 : value);
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
        for(int ch=0;ch<8;++ch){ if(!ChannelEnabled(ch)) continue;
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

void SaveCurrentZoom(HWND window) {
    if (!EnsureGraphSurface(window)) return;
    if (!graphCache.valid) RenderGraphFully(window);
    ZoomState state;
    state.viewSeconds = displayLengthSeconds.load();
    state.viewEnd = graphFollowingLive ? graphCache.viewEnd : graphViewEndSeconds;
    state.minV = graphCache.manualYScale ? graphCache.requestedMinV : graphCache.minV;
    state.maxV = graphCache.manualYScale ? graphCache.requestedMaxV : graphCache.maxV;
    state.manualYScale = graphCache.manualYScale;
    state.followingLive = graphFollowingLive;
    graphZoomHistory.push_back(state);
    while (graphZoomHistory.size() > 10) graphZoomHistory.pop_front();
    EnableWindow(graphBackButton, !graphZoomHistory.empty());
}

void GoToPreviousZoom(HWND window) {
    if (graphZoomHistory.empty()) return;
    const ZoomState state = graphZoomHistory.back();
    graphZoomHistory.pop_back();
    EnableWindow(graphBackButton, !graphZoomHistory.empty());
    displayLengthSeconds.store(state.viewSeconds);
    graphViewEndSeconds = state.viewEnd;
    graphFollowingLive = state.followingLive;
    graphCache.manualYScale = state.manualYScale;
    graphCache.requestedMinV = state.minV;
    graphCache.requestedMaxV = state.maxV;
    graphCache.valid = false;
    SyncDisplayLengthField();
    RenderGraphFully(window);
    const RECT bounds = GraphBounds(window);
    InvalidateRect(window, &bounds, FALSE);
}

void ZoomGraphToSelection(HWND window, POINT start, POINT end) {
    if (!EnsureGraphSurface(window)) return;
    if (!graphCache.valid) RenderGraphFully(window);
    if (end.x < start.x && end.y < start.y) {
        GoToPreviousZoom(window);
        return;
    }
    if (end.x <= start.x || end.y <= start.y || end.x - start.x < 12 || end.y - start.y < 12) return;
    const RECT bounds = GraphBounds(window);
    const int left = static_cast<int>(bounds.left) + 72, right = static_cast<int>(bounds.left) + graphCache.width - 340;
    const int top = static_cast<int>(bounds.top) + 66, bottom = static_cast<int>(bounds.top) + graphCache.height - 62;
    const int x1 = (std::max)(left, (std::min)(right, static_cast<int>(start.x)));
    const int x2 = (std::max)(left, (std::min)(right, static_cast<int>(end.x)));
    const int y1 = (std::max)(top, (std::min)(bottom, static_cast<int>(start.y)));
    const int y2 = (std::max)(top, (std::min)(bottom, static_cast<int>(end.y)));
    if (x2 - x1 < 12 || y2 - y1 < 12) return;
    SaveCurrentZoom(window);
    const double oldSeconds = displayLengthSeconds.load();
    const double oldStart = graphCache.viewEnd - oldSeconds;
    const double fraction1 = (x1 - left) / static_cast<double>(right - left);
    const double fraction2 = (x2 - left) / static_cast<double>(right - left);
    const double selectedStart = oldStart + fraction1 * oldSeconds;
    const double selectedEnd = oldStart + fraction2 * oldSeconds;
    displayLengthSeconds.store((std::max)(0.001, selectedEnd - selectedStart));
    graphViewEndSeconds = selectedEnd;
    graphFollowingLive = graphFollowingLive && x2 >= right - 2;
    const double voltageRange = graphCache.maxV - graphCache.minV;
    const double topValue = graphCache.maxV - (y1 - top) * voltageRange / (bottom - top);
    const double bottomValue = graphCache.maxV - (y2 - top) * voltageRange / (bottom - top);
    graphCache.requestedMinV = (std::min)(topValue, bottomValue);
    graphCache.requestedMaxV = (std::max)(topValue, bottomValue);
    if (graphCache.requestedMaxV - graphCache.requestedMinV < 1e-9) return;
    graphCache.manualYScale = true;
    graphCache.valid = false;
    SyncDisplayLengthField();
    RenderGraphFully(window);
    InvalidateRect(window, &bounds, FALSE);
}

void ShowGraphZoomHelp(HWND owner) {
    const wchar_t instructions[] =
        L"\x2022 Drag down and right with the left mouse button to zoom into the selected time and voltage range.\n"
        L"\x2022 Drag up and left with the left mouse button to return to the previous zoom.\n"
        L"\x2022 Mouse wheel zooms the voltage axis around the cursor.\n"
        L"\x2022 Ctrl + mouse wheel zooms the time axis.\n"
        L"\x2022 Right-drag moves the displayed time and voltage ranges.\n"
        L"\x2022 Right-click the graph or click the magnifier/back button to return to the previous zoom.\n"
        L"\x2022 Double-click the graph to return to the live view.\n"
        L"\x2022 Display length (s) sets the live time window.";
    MessageBoxW(owner, instructions, L"Signals graph zoom and pan", MB_OK | MB_ICONINFORMATION);
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
    const int left=72,right=graphCache.width-340,top=66,bottom=graphCache.height-62;
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
    for(int ch=0;ch<8;++ch){ if(!ChannelEnabled(ch)) continue;
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
    const int left = 72, right = graphCache.width - 340;
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
            if (graphZoomSelecting) {
                RECT selection{(std::min)(graphZoomStart.x, graphZoomCurrent.x), (std::min)(graphZoomStart.y, graphZoomCurrent.y),
                               (std::max)(graphZoomStart.x, graphZoomCurrent.x), (std::max)(graphZoomStart.y, graphZoomCurrent.y)};
                DrawFocusRect(dc, &selection);
            }
            DrawCommentTooltip(window,dc);
        } else if (activeTab == 1) PaintProfileGraph(window, dc);
        else if (activeTab == 2 && EnsureCalibrationGraphSurface(window)) {
            if (!calibrationGraphCache.valid) RenderCalibrationGraphsCached(window);
            const RECT live = CalibrationLiveGraphBounds(window);
            const RECT curve = CalibrationCurveGraphBounds(window);
            BitBlt(dc, live.left, live.top, live.right - live.left, live.bottom - live.top,
                   calibrationGraphCache.dc, live.left, live.top, SRCCOPY);
            BitBlt(dc, curve.left, curve.top, curve.right - curve.left, curve.bottom - curve.top,
                   calibrationGraphCache.dc, curve.left, curve.top, SRCCOPY);
        }
        EndPaint(window,&ps); return 0;
    }
    case WM_CREATE:{
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES|ICC_BAR_CLASSES|ICC_WIN95_CLASSES}; InitCommonControlsEx(&controls);
        statusBar=CreateWindowExW(0,STATUSCLASSNAMEW,L"",WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,0,0,0,0,window,reinterpret_cast<HMENU>(StatusBarId),GetModuleHandleW(nullptr),nullptr);
        LayoutStatusBar(window);
        tabs=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_TABSTOP,20,18,1080,46,window,reinterpret_cast<HMENU>(TabsId),GetModuleHandleW(nullptr),nullptr);
        for(int i=0;i<5;++i){ TCITEMW item{}; item.mask=TCIF_TEXT; item.pszText=const_cast<wchar_t*>(tabNames[i]); TabCtrl_InsertItem(tabs,i,&item); }
        frequencyLabel=CreateWindowW(L"STATIC",L"Recording interval (s)",WS_CHILD|WS_VISIBLE,610,746,150,34,window,reinterpret_cast<HMENU>(FrequencyLabelId),GetModuleHandleW(nullptr),nullptr);
        frequencyInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"1",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|WS_TABSTOP,770,742,80,40,window,reinterpret_cast<HMENU>(FrequencyId),GetModuleHandleW(nullptr),nullptr);
        displayLengthLabel=CreateWindowW(L"STATIC",L"Display length (s)",WS_CHILD|WS_VISIBLE,32,746,130,34,window,reinterpret_cast<HMENU>(DisplayLengthLabelId),GetModuleHandleW(nullptr),nullptr);
        displayLengthInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"10",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|WS_TABSTOP,164,742,90,40,window,reinterpret_cast<HMENU>(DisplayLengthId),GetModuleHandleW(nullptr),nullptr);
        fileButton=CreateWindowW(L"BUTTON",L"Choose/change file...",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,32,118,176,40,window,reinterpret_cast<HMENU>(FileButtonId),GetModuleHandleW(nullptr),nullptr);
        graphHelpButton=CreateWindowW(L"BUTTON",L"?",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,1088,196,28,28,window,reinterpret_cast<HMENU>(GraphHelpId),GetModuleHandleW(nullptr),nullptr);
        graphBackButton=CreateWindowW(L"BUTTON",L"🔍←",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,1040,196,40,28,window,reinterpret_cast<HMENU>(GraphBackId),GetModuleHandleW(nullptr),nullptr);
        EnableWindow(graphBackButton, FALSE);
        CreateGraphHelpTooltip(window);
        infoLabel=CreateWindowW(L"STATIC",L"",WS_CHILD|SS_LEFT,32,184,1020,28,window,reinterpret_cast<HMENU>(InfoId),GetModuleHandleW(nullptr),nullptr);
        mocoAddressLabel=CreateWindowW(L"STATIC",L"Address",WS_CHILD|SS_LEFT,415,174,58,28,window,reinterpret_cast<HMENU>(MocoAddressLabelId),GetModuleHandleW(nullptr),nullptr);
        mocoAddressInput=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,473,168,55,300,window,reinterpret_cast<HMENU>(MocoAddressId),GetModuleHandleW(nullptr),nullptr);
        for(const wchar_t* address : {L"0",L"1",L"2",L"3",L"4",L"5",L"6",L"7",L"8",L"9",L"A",L"B",L"C",L"D",L"E",L"F"})
            SendMessageW(mocoAddressInput,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(address));
        SendMessageW(mocoAddressInput,CB_SETCURSEL,0,0);
        manualControlHeading=CreateWindowW(L"STATIC",L"Manual control",WS_CHILD|SS_LEFT,32,118,355,32,window,reinterpret_cast<HMENU>(ManualControlHeadingId),GetModuleHandleW(nullptr),nullptr);
        mocoPositionLabel=CreateWindowW(L"STATIC",L"Current position (um)",WS_CHILD|SS_LEFT,32,162,205,30,window,reinterpret_cast<HMENU>(MocoPositionLabelId),GetModuleHandleW(nullptr),nullptr);
        mocoPositionInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"—",WS_CHILD|ES_AUTOHSCROLL|ES_READONLY|ES_RIGHT,242,264,100,36,window,reinterpret_cast<HMENU>(MocoPositionInputId),GetModuleHandleW(nullptr),nullptr);
        stepLabel=CreateWindowW(L"STATIC",L"Step (um)",WS_CHILD|SS_LEFT,32,208,116,30,window,reinterpret_cast<HMENU>(StepLabelId),GetModuleHandleW(nullptr),nullptr);
        stepInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"100",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,150,394,80,36,window,reinterpret_cast<HMENU>(StepInputId),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(stepInput,EM_SETLIMITTEXT,32,0);
        stepUpButton=CreateWindowW(L"BUTTON",L"↓",WS_CHILD|BS_PUSHBUTTON|WS_TABSTOP,248,394,42,36,window,reinterpret_cast<HMENU>(StepUpId),GetModuleHandleW(nullptr),nullptr);
        stepDownButton=CreateWindowW(L"BUTTON",L"↑",WS_CHILD|BS_PUSHBUTTON|WS_TABSTOP,296,394,42,36,window,reinterpret_cast<HMENU>(StepDownId),GetModuleHandleW(nullptr),nullptr);
        arrowButtonFont=CreateFontW(30,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Symbol");
        if(arrowButtonFont){ SendMessageW(stepUpButton,WM_SETFONT,reinterpret_cast<WPARAM>(arrowButtonFont),TRUE); SendMessageW(stepDownButton,WM_SETFONT,reinterpret_cast<WPARAM>(arrowButtonFont),TRUE); }
        goToLabel=CreateWindowW(L"STATIC",L"Go to (um)",WS_CHILD|SS_LEFT,32,254,116,30,window,reinterpret_cast<HMENU>(GoToLabelId),GetModuleHandleW(nullptr),nullptr);
        goToInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,150,248,80,36,window,reinterpret_cast<HMENU>(GoToInputId),GetModuleHandleW(nullptr),nullptr);
        goToButton=CreateWindowW(L"BUTTON",L"Go",WS_CHILD|BS_PUSHBUTTON|WS_TABSTOP,312,248,75,36,window,reinterpret_cast<HMENU>(GoToButtonId),GetModuleHandleW(nullptr),nullptr);
        setPositionLabel=CreateWindowW(L"STATIC",L"Set position to (um)",WS_CHILD|SS_LEFT,32,300,116,30,window,reinterpret_cast<HMENU>(SetPositionLabelId),GetModuleHandleW(nullptr),nullptr);
        setPositionInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,150,294,80,36,window,reinterpret_cast<HMENU>(SetPositionInputId),GetModuleHandleW(nullptr),nullptr);
        setPositionButton=CreateWindowW(L"BUTTON",L"Set to",WS_CHILD|BS_PUSHBUTTON|WS_TABSTOP,312,294,75,36,window,reinterpret_cast<HMENU>(SetPositionButtonId),GetModuleHandleW(nullptr),nullptr);
        profileSettingsHeading=CreateWindowW(L"STATIC",L"Profile settings",WS_CHILD|SS_LEFT,32,360,468,32,window,reinterpret_cast<HMENU>(ProfileSettingsHeadingId),GetModuleHandleW(nullptr),nullptr);
        profilingStartLabel=CreateWindowW(L"STATIC",L"Start (um)",WS_CHILD|SS_LEFT,32,402,100,28,window,reinterpret_cast<HMENU>(ProfilingStartLabelId),GetModuleHandleW(nullptr),nullptr);
        profilingStartInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"0",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,138,396,80,36,window,reinterpret_cast<HMENU>(ProfilingStartInputId),GetModuleHandleW(nullptr),nullptr);
        profileEndLabel=CreateWindowW(L"STATIC",L"End (um)",WS_CHILD|SS_LEFT,286,402,90,28,window,reinterpret_cast<HMENU>(ProfileEndLabelId),GetModuleHandleW(nullptr),nullptr);
        profileEndInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,380,396,80,36,window,reinterpret_cast<HMENU>(ProfileEndInputId),GetModuleHandleW(nullptr),nullptr);
        profileStepLabel=CreateWindowW(L"STATIC",L"Step (um)",WS_CHILD|SS_LEFT,32,448,100,28,window,reinterpret_cast<HMENU>(ProfileStepLabelId),GetModuleHandleW(nullptr),nullptr);
        profileStepInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,138,442,80,36,window,reinterpret_cast<HMENU>(ProfileStepInputId),GetModuleHandleW(nullptr),nullptr);
        profileSafeLabel=CreateWindowW(L"STATIC",L"Safe (um)",WS_CHILD|SS_LEFT,286,448,90,28,window,reinterpret_cast<HMENU>(ProfileSafeLabelId),GetModuleHandleW(nullptr),nullptr);
        profileSafeInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,380,442,80,36,window,reinterpret_cast<HMENU>(ProfileSafeInputId),GetModuleHandleW(nullptr),nullptr);
        profileWaitLabel=CreateWindowW(L"STATIC",L"Wait (s)",WS_CHILD|SS_LEFT,32,494,100,28,window,reinterpret_cast<HMENU>(ProfileWaitLabelId),GetModuleHandleW(nullptr),nullptr);
        profileWaitInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,138,488,80,36,window,reinterpret_cast<HMENU>(ProfileWaitInputId),GetModuleHandleW(nullptr),nullptr);
        profileMeasureLabel=CreateWindowW(L"STATIC",L"Measure (s)",WS_CHILD|SS_LEFT,286,494,90,28,window,reinterpret_cast<HMENU>(ProfileMeasureLabelId),GetModuleHandleW(nullptr),nullptr);
        profileMeasureInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,380,488,80,36,window,reinterpret_cast<HMENU>(ProfileMeasureInputId),GetModuleHandleW(nullptr),nullptr);
        profileOvershootLabel=CreateWindowW(L"STATIC",L"Overshoot (um)",WS_CHILD|SS_LEFT,32,540,100,28,window,reinterpret_cast<HMENU>(ProfileOvershootLabelId),GetModuleHandleW(nullptr),nullptr);
        profileOvershootInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"0",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,138,534,80,36,window,reinterpret_cast<HMENU>(ProfileOvershootInputId),GetModuleHandleW(nullptr),nullptr);
        profileOvershootWaitLabel=CreateWindowW(L"STATIC",L"Overshoot wait (s)",WS_CHILD|SS_LEFT,286,540,90,28,window,reinterpret_cast<HMENU>(ProfileOvershootWaitLabelId),GetModuleHandleW(nullptr),nullptr);
        profileOvershootWaitInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"0",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,380,534,80,36,window,reinterpret_cast<HMENU>(ProfileOvershootWaitInputId),GetModuleHandleW(nullptr),nullptr);
        scaleLabel=CreateWindowW(L"STATIC",L"Encoder increments per um",WS_CHILD|SS_LEFT,32,540,220,30,window,reinterpret_cast<HMENU>(ScaleLabelId),GetModuleHandleW(nullptr),nullptr);
        scaleInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"2",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,260,534,135,36,window,reinterpret_cast<HMENU>(ScaleInputId),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(scaleInput,EM_SETLIMITTEXT,32,0);
        profileRunButton=CreateWindowW(L"BUTTON",L"Start",WS_CHILD|BS_PUSHBUTTON|WS_TABSTOP,32,586,100,40,window,reinterpret_cast<HMENU>(ProfileRunButtonId),GetModuleHandleW(nullptr),nullptr);
        profileLabelCaption=CreateWindowW(L"STATIC",L"Profile label",WS_CHILD,32,580,100,28,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        profileLabelInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,138,576,180,32,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        profileCyclesInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"1",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL|ES_RIGHT|ES_NUMBER,380,576,80,32,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ProfileCyclesInputId)),GetModuleHandleW(nullptr),nullptr);
        profileCyclesLabel=CreateWindowW(L"STATIC",L"Cycles",WS_CHILD|SS_LEFT,324,580,54,28,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ProfileCyclesLabelId)),GetModuleHandleW(nullptr),nullptr);
        profileCycleIntervalLabel=CreateWindowW(L"STATIC",L"Cycle interval (s)",WS_CHILD|SS_LEFT,150,622,150,28,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        profileCycleIntervalInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"0",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL|ES_RIGHT,310,616,80,32,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        commentButton=CreateWindowW(L"BUTTON",L"Comment",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,280,738,120,48,window,reinterpret_cast<HMENU>(CommentButtonId),GetModuleHandleW(nullptr),nullptr); EnableWindow(commentButton,FALSE);
        startButton=CreateWindowW(L"BUTTON",L"Start recording",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,870,738,160,48,window,reinterpret_cast<HMENU>(StartId),GetModuleHandleW(nullptr),nullptr);
        stopButton=CreateWindowW(L"BUTTON",L"Stop",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,1040,738,90,48,window,reinterpret_cast<HMENU>(StopId),GetModuleHandleW(nullptr),nullptr); EnableWindow(stopButton,FALSE);
        mocoHeading=CreateWindowW(L"STATIC",L"Motors",WS_CHILD|SS_LEFT,32,118,1060,34,window,reinterpret_cast<HMENU>(MocoHeadingId),GetModuleHandleW(nullptr),nullptr);
        mocoPortLabel=CreateWindowW(L"STATIC",L"COM port",WS_CHILD|SS_LEFT,32,174,90,28,window,reinterpret_cast<HMENU>(MocoPortLabelId),GetModuleHandleW(nullptr),nullptr);
        mocoPortInput=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,132,168,140,300,window,reinterpret_cast<HMENU>(MocoPortId),GetModuleHandleW(nullptr),nullptr);
        for(int port=1;port<=32;++port){ wchar_t name[16]{}; swprintf_s(name,L"COM%d",port); SendMessageW(mocoPortInput,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name)); }
        for(LRESULT i=0;i<SendMessageW(mocoPortInput,CB_GETCOUNT,0,0);++i){ wchar_t name[16]{}; SendMessageW(mocoPortInput,CB_GETLBTEXT,i,reinterpret_cast<LPARAM>(name)); if(savedComPort==name){ SendMessageW(mocoPortInput,CB_SETCURSEL,i,0); break; } }
        mocoBaudLabel=CreateWindowW(L"STATIC",L"Baud rate",WS_CHILD|SS_LEFT,32,234,90,28,window,reinterpret_cast<HMENU>(MocoBaudLabelId),GetModuleHandleW(nullptr),nullptr);
        mocoBaudInput=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,132,228,140,120,window,reinterpret_cast<HMENU>(MocoBaudId),GetModuleHandleW(nullptr),nullptr);
        const wchar_t* baudRates[]={L"9600",L"19200",L"34400",L"38400"};
        for(const wchar_t* rate:baudRates) SendMessageW(mocoBaudInput,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(rate));
        SendMessageW(mocoBaudInput,CB_SETCURSEL,0,0);
        mocoAddressLabel=CreateWindowW(L"STATIC",L"Address",WS_CHILD|SS_LEFT,32,294,90,28,window,reinterpret_cast<HMENU>(MocoAddressLabelId),GetModuleHandleW(nullptr),nullptr);
        MoveWindow(mocoAddressInput,132,288,140,300,FALSE);
        mocoConnectButton=CreateWindowW(L"BUTTON",L"Connect",WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,32,348,110,40,window,reinterpret_cast<HMENU>(MocoConnectId),GetModuleHandleW(nullptr),nullptr);
        mocoDisconnectButton=CreateWindowW(L"BUTTON",L"Disconnect",WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,154,348,120,40,window,reinterpret_cast<HMENU>(MocoDisconnectId),GetModuleHandleW(nullptr),nullptr);
        mocoAdvancedButton=CreateWindowW(L"BUTTON",L"Advanced",WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,290,348,110,40,window,reinterpret_cast<HMENU>(MocoAdvancedId),GetModuleHandleW(nullptr),nullptr);
        mocoStatusLabel=CreateWindowW(L"STATIC",L"Disconnected",WS_CHILD|SS_LEFT,32,510,1020,28,window,reinterpret_cast<HMENU>(MocoStatusId),GetModuleHandleW(nullptr),nullptr);
        mocoCommandLabel=CreateWindowW(L"STATIC",L"MoCo ASCII command (sent with carriage return)",WS_CHILD|SS_LEFT,32,550,520,28,window,reinterpret_cast<HMENU>(MocoCommandLabelId),GetModuleHandleW(nullptr),nullptr);
        mocoCommandInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|WS_TABSTOP,32,582,650,36,window,reinterpret_cast<HMENU>(MocoCommandId),GetModuleHandleW(nullptr),nullptr);
        mocoSendButton=CreateWindowW(L"BUTTON",L"Send",WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,696,582,110,36,window,reinterpret_cast<HMENU>(MocoSendId),GetModuleHandleW(nullptr),nullptr);
        mocoConsoleLabel=CreateWindowW(L"STATIC",L"Controller response",WS_CHILD|SS_LEFT,32,634,520,28,window,reinterpret_cast<HMENU>(MocoConsoleLabelId),GetModuleHandleW(nullptr),nullptr);
        mocoConsole=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|WS_VSCROLL|WS_HSCROLL,32,664,1060,150,window,reinterpret_cast<HMENU>(MocoConsoleId),GetModuleHandleW(nullptr),nullptr);
        calibrationColumnLabels[0]=CreateWindowW(L"STATIC",L"Channel",WS_CHILD|SS_LEFT,32,228,48,26,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        calibrationColumnLabels[1]=CreateWindowW(L"STATIC",L"Concentration",WS_CHILD|SS_LEFT,86,228,110,26,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        calibrationColumnLabels[2]=CreateWindowW(L"STATIC",L"Slope",WS_CHILD|SS_LEFT,590,228,105,26,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        calibrationColumnLabels[3]=CreateWindowW(L"STATIC",L"Intercept",WS_CHILD|SS_LEFT,705,228,105,26,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        for (int channel = 0; channel < 8; ++channel) {
            const int y = 256 + channel * 44;
            const int base = CalibrationControlBaseId + channel * CalibrationControlStride;
            wchar_t channelName[16]{}; swprintf_s(channelName, L"CH%d", channel);
            calibrationChannelLabels[channel]=CreateWindowW(L"STATIC",channelName,WS_CHILD|SS_LEFT,32,y+7,48,24,window,nullptr,GetModuleHandleW(nullptr),nullptr);
            calibrationConcentrationInputs[channel]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,86,y,110,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(base)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(calibrationConcentrationInputs[channel],EM_SETLIMITTEXT,32,0);
            calibrationGetButtons[channel]=CreateWindowW(L"BUTTON",L"Get signal",WS_CHILD|BS_PUSHBUTTON|WS_TABSTOP,204,y,92,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(base+1)),GetModuleHandleW(nullptr),nullptr);
            calibrationClearButtons[channel]=CreateWindowW(L"BUTTON",L"Clear calibration",WS_CHILD|BS_PUSHBUTTON|WS_TABSTOP,304,y,128,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(base+2)),GetModuleHandleW(nullptr),nullptr);
            calibrationApplyButtons[channel]=CreateWindowW(L"BUTTON",L"Apply calibration",WS_CHILD|BS_PUSHBUTTON|WS_TABSTOP,440,y,128,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(base+3)),GetModuleHandleW(nullptr),nullptr);
            calibrationSlopeFields[channel]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|ES_READONLY|WS_TABSTOP,590,y,105,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(base+4)),GetModuleHandleW(nullptr),nullptr);
            calibrationInterceptFields[channel]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|ES_READONLY|WS_TABSTOP,705,y,105,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(base+5)),GetModuleHandleW(nullptr),nullptr);
            calibrationR2Fields[channel]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT|ES_READONLY|WS_TABSTOP,820,y,105,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(base+6)),GetModuleHandleW(nullptr),nullptr);
            wchar_t toggleName[16]{}; swprintf_s(toggleName, L"CH%d", channel);
            calibrationPlotToggles[channel]=CreateWindowW(L"BUTTON",toggleName,WS_CHILD|BS_AUTOCHECKBOX|WS_TABSTOP,0,0,78,24,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(CalibrationPlotToggleBaseId+channel)),GetModuleHandleW(nullptr),nullptr);
            calibrationLiveValueLabels[channel]=CreateWindowW(L"STATIC",L"— V",WS_CHILD|SS_LEFT,0,0,82,24,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(CalibrationLiveLabelBaseId+channel)),GetModuleHandleW(nullptr),nullptr);
        }
        calibrationColumnLabels[4]=CreateWindowW(L"STATIC",L"r^2",WS_CHILD|SS_LEFT,820,228,105,26,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        RestoreNumericSettings();
        CreateChannelSettings(window);
        CreateProfileLegend(window);
        { double restoredLength = 0.0; if (ReadPositiveNumber(displayLengthInput, restoredLength) && restoredLength <= 604800.0) displayLengthSeconds.store(restoredLength); }
        UpdateCalibrationPlotControls(false);
        PositionCalibrationLegend(window);
        UpdateCalibrationLiveValues();
        UpdateMocoConnectionControls(false);
        UpdateProfileControlState();
        SetTimer(window,UiTimer,graphPanning ? 30 : 100,nullptr);
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(MocoConnectId, BN_CLICKED), reinterpret_cast<LPARAM>(mocoConnectButton));
        if (!moco.IsOpen()) {
            wchar_t connectionError[512]{};
            GetWindowTextW(mocoStatusLabel, connectionError, static_cast<int>(std::size(connectionError)));
            std::wstring statusText = L"ERROR: Automatic MoCo connection failed. ";
            statusText += connectionError[0] ? connectionError : L"Check the saved COM port, baud rate, board address, and controller power.";
            TabCtrl_SetCurSel(tabs, 1);
            SetView(window, 1);
            SetWindowTextW(mocoStatusLabel, statusText.c_str());
            WriteLog(statusText);
        }
        return 0;
    }
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        const HWND control = reinterpret_cast<HWND>(lParam);
        const UINT_PTR id = static_cast<UINT_PTR>(GetDlgCtrlID(control));
        size_t channel = 8;
        if (id >= CalibrationPlotToggleBaseId && id < CalibrationPlotToggleBaseId + 8)
            channel = static_cast<size_t>(id - CalibrationPlotToggleBaseId);
        else if (id >= CalibrationLiveLabelBaseId && id < CalibrationLiveLabelBaseId + 8)
            channel = static_cast<size_t>(id - CalibrationLiveLabelBaseId);
        if (channel < 8) {
            SetTextColor(reinterpret_cast<HDC>(wParam), colors[channel]);
            SetBkColor(reinterpret_cast<HDC>(wParam), RGB(250,251,253));
            SetBkMode(reinterpret_cast<HDC>(wParam), OPAQUE);
            static HBRUSH legendBackground = CreateSolidBrush(RGB(250,251,253));
            return reinterpret_cast<LRESULT>(legendBackground);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == GraphHelpId && HIWORD(wParam) == BN_CLICKED) { ShowGraphZoomHelp(window); return 0; }
        {
            const int id=LOWORD(wParam), notice=HIWORD(wParam);
            const bool select=id>=ChannelSelectBaseId && id<ChannelSelectBaseId+8 && notice==BN_CLICKED;
            const bool name=id>=ChannelNameBaseId && id<ChannelNameBaseId+8 && notice==EN_CHANGE;
            const bool range=id>=ChannelRangeBaseId && id<ChannelRangeBaseId+8 && notice==CBN_SELCHANGE;
            if(channelControlsReady && (select || name || range)) {
                const size_t channel=static_cast<size_t>(id-(select?ChannelSelectBaseId:name?ChannelNameBaseId:ChannelRangeBaseId));
                const std::wstring key=L"CH"+std::to_wstring(channel);
                if(select) {
                    selectedChannels[channel]=SendMessageW(channelSelectFields[channel],BM_GETCHECK,0,0)==BST_CHECKED;
                    WritePrivateProfileStringW(L"Channels",(key+L"Selected").c_str(),ChannelEnabled(channel)?L"1":L"0",configFilePath.c_str());
                } else if(name) {
                    wchar_t text[256]{}; GetWindowTextW(channelNameFields[channel],text,256);
                    userChannelNames[channel]=text;
                    WritePrivateProfileStringW(L"Channels",(key+L"Name").c_str(),text,configFilePath.c_str());
                } else {
                    const LRESULT choice=SendMessageW(channelRangeFields[channel],CB_GETCURSEL,0,0);
                    if(choice>=0 && choice<4) channelRanges[channel]=channelRangeCodes[choice];
                    WritePrivateProfileStringW(L"Channels",(key+L"Range").c_str(),std::to_wstring(channelRanges[channel].load()).c_str(),configFilePath.c_str());
                }
                if(select || range) {
                    channelSettingsGeneration.fetch_add(1);
                    std::lock_guard<std::mutex> lock(dataMutex); history.clear();
                }
                graphCache.valid=false; calibrationGraphCache.valid=false;
                UpdateProfileControlState();
                InvalidateRect(window,nullptr,FALSE);
                return 0;
            }
        }
        if (LOWORD(wParam) == GraphBackId && HIWORD(wParam) == BN_CLICKED) { GoToPreviousZoom(window); return 0; }
        if((LOWORD(wParam)==CommentButtonId && HIWORD(wParam)==BN_CLICKED) || LOWORD(wParam)==CommentShortcutId) { ShowCommentDialog(window); return 0; }
        if (LOWORD(wParam) >= CalibrationPlotToggleBaseId && LOWORD(wParam) < CalibrationPlotToggleBaseId + 8 && HIWORD(wParam) == BN_CLICKED) {
            calibrationGraphCache.valid = false;
            const RECT plot = CalibrationCurveGraphBounds(window);
            InvalidateRect(window, &plot, TRUE);
            return 0;
        }
        if (LOWORD(wParam) >= CalibrationControlBaseId &&
            LOWORD(wParam) < CalibrationControlBaseId + 8 * CalibrationControlStride && HIWORD(wParam) == BN_CLICKED) {
            const int relative = LOWORD(wParam) - CalibrationControlBaseId;
            const size_t channel = static_cast<size_t>(relative / CalibrationControlStride);
            const int control = relative % CalibrationControlStride;
            if (control == 1) { GetCalibrationSignal(window, channel); return 0; }
            if (control == 2) {
                std::wstring error;
                if (!ClearCalibrationChannel(channel, error)) MessageBoxW(window, error.c_str(), L"Calibration", MB_OK | MB_ICONERROR);
                else {
                    SetWindowTextW(calibrationSlopeFields[channel], L"");
                    SetWindowTextW(calibrationInterceptFields[channel], L"");
                    SetWindowTextW(calibrationR2Fields[channel], L"");
                    UpdateCalibrationPlotControls(false);
                    wchar_t status[64]{}; swprintf_s(status, L"CH%u calibration data cleared.", static_cast<unsigned>(channel));
                    SetWindowTextW(infoLabel, status); WriteLog(status);
                    const RECT plot = CalibrationCurveGraphBounds(window);
                    calibrationGraphCache.valid = false;
                    InvalidateRect(window, &plot, TRUE);
                }
                return 0;
            }
            if (control == 3) { ApplyCalibration(window, channel); return 0; }
        }
        if(LOWORD(wParam)==MocoPortId && HIWORD(wParam)==CBN_SELCHANGE) {
            const LRESULT index=SendMessageW(mocoPortInput,CB_GETCURSEL,0,0);
            if(index!=CB_ERR){ wchar_t port[32]{}; SendMessageW(mocoPortInput,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(port)); savedComPort=port; if(!configFilePath.empty()) WritePrivateProfileStringW(L"Tasker",L"ComPort",savedComPort.c_str(),configFilePath.c_str()); }
            return 0;
        }
        if(LOWORD(wParam)==MocoConnectId && HIWORD(wParam)==BN_CLICKED) {
            wchar_t port[32]{}, baudText[32]{}, addressText[8]{};
            const LRESULT portIndex=SendMessageW(mocoPortInput,CB_GETCURSEL,0,0);
            const LRESULT baudIndex=SendMessageW(mocoBaudInput,CB_GETCURSEL,0,0);
            const LRESULT addressIndex=SendMessageW(mocoAddressInput,CB_GETCURSEL,0,0);
            if(portIndex==CB_ERR || baudIndex==CB_ERR || addressIndex==CB_ERR){ SetWindowTextW(mocoStatusLabel,L"Select the COM port, baud rate, and board address."); return 0; }
            SendMessageW(mocoPortInput,CB_GETLBTEXT,portIndex,reinterpret_cast<LPARAM>(port));
            SendMessageW(mocoBaudInput,CB_GETLBTEXT,baudIndex,reinterpret_cast<LPARAM>(baudText));
            SendMessageW(mocoAddressInput,CB_GETLBTEXT,addressIndex,reinterpret_cast<LPARAM>(addressText));
            const DWORD baud=static_cast<DWORD>(std::wcstoul(baudText,nullptr,10));
            std::wstring error;
            std::lock_guard<std::mutex> serialLock(mocoMutex);
            if(moco.Open(port,baud,error)){
                if(!moco.SelectBoardAddress(static_cast<char>(addressText[0]),error)) {
                    moco.Close();
                    SetWindowTextW(mocoStatusLabel,error.c_str());
                    UpdateMocoConnectionControls(false);
                } else {
                    mocoPositionQueryPending=false;
                    mocoCurrentPositionValid=false;
                    SetWindowTextW(mocoPositionInput,L"—");
                    nextMocoPositionQuery=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
                    SetWindowTextW(mocoStatusLabel,(L"Connected to "+std::wstring(port)+L" at "+baudText+L" baud; selected board "+addressText+L".").c_str());
                    AppendMocoConsole(L"[Connected to "+std::wstring(port)+L" at "+baudText+L" baud; addressed board "+addressText+L"]\r\n");
                    WriteLog(L"Connected to MICOS MoCo serial port "+std::wstring(port)+L" at "+baudText+L" baud; selected board "+addressText+L".");
                    UpdateMocoConnectionControls(true);
                    UpdateStepButtonState();
                    UpdateProfileControlState();
                }
            }else SetWindowTextW(mocoStatusLabel,error.c_str());
            return 0;
        }
        if(LOWORD(wParam)==MocoDisconnectId && HIWORD(wParam)==BN_CLICKED) {
            { std::lock_guard<std::mutex> lock(mocoMutex); moco.Close(); }
            SetWindowTextW(mocoStatusLabel,L"Disconnected");
            AppendMocoConsole(L"[Disconnected]\r\n"); WriteLog(L"Disconnected from MICOS MoCo."); UpdateMocoConnectionControls(false); UpdateStepButtonState(); UpdateProfileControlState(); return 0;
        }
        if(LOWORD(wParam)==MocoSendId && HIWORD(wParam)==BN_CLICKED) {
            wchar_t command[1024]{}; GetWindowTextW(mocoCommandInput,command,static_cast<int>(std::size(command)));
            std::wstring error;
            bool sent = false; { std::lock_guard<std::mutex> lock(mocoMutex); sent = moco.SendAsciiCommand(command,error); }
            if(sent) AppendMocoConsole(L"> "+std::wstring(command)+L"\r\n");
            else SetWindowTextW(mocoStatusLabel,error.c_str());
            return 0;
        }
        if(LOWORD(wParam)==MocoAdvancedId && HIWORD(wParam)==BN_CLICKED) { ShowAdvancedMotorDialog(window); return 0; }
        if((LOWORD(wParam)==StepInputId || LOWORD(wParam)==ScaleInputId) && HIWORD(wParam)==EN_CHANGE) {
            if(LOWORD(wParam)==ScaleInputId) UpdateMocoPositionDisplay();
            UpdateStepButtonState();
            UpdateProfileControlState();
            return 0;
        }
        if(LOWORD(wParam)==StepUpId && HIWORD(wParam)==BN_CLICKED) { MoveMoCoStep(window,1); return 0; }
        if(LOWORD(wParam)==StepDownId && HIWORD(wParam)==BN_CLICKED) { MoveMoCoStep(window,-1); return 0; }
        if(LOWORD(wParam)==GoToButtonId && HIWORD(wParam)==BN_CLICKED) { GoToMoCoPosition(window); return 0; }
        if(LOWORD(wParam)==SetPositionButtonId && HIWORD(wParam)==BN_CLICKED) { SetSoftwarePosition(window); return 0; }
        if(LOWORD(wParam)==ProfileRunButtonId && HIWORD(wParam)==BN_CLICKED) { StartProfileScan(window); return 0; }

        if(LOWORD(wParam)==StartId && HIWORD(wParam)==BN_CLICKED) StartRecording(window);
        if(LOWORD(wParam)==StopId && HIWORD(wParam)==BN_CLICKED) StopRecording();
        if(LOWORD(wParam)==DisplayLengthId && HIWORD(wParam)==EN_CHANGE && !displayLengthSync) {
            wchar_t text[64]{}; GetWindowTextW(displayLengthInput,text,64);
            wchar_t* end=nullptr; const double seconds=std::wcstod(text,&end);
            if(end!=text && *end==L'\0' && std::isfinite(seconds) && seconds>0.0 && seconds<=604800.0) displayLengthSeconds.store(seconds);
            calibrationGraphCache.valid = false;
            if(!graphFollowingLive) ClampGraphViewToHistory(graphViewEndSeconds,displayLengthSeconds.load());
            graphCache.valid=false; RECT plot=GraphBounds(window); InvalidateRect(window,&plot,FALSE);
        }
        if(LOWORD(wParam)==FileButtonId && HIWORD(wParam)==BN_CLICKED && !recording.load()) {
            if(ChooseDataFile(window)) WriteLog(L"Ready to record to " + dataFilePath + L".");
        }
        return 0;
    case WM_TIMER:
        if(wParam==UiTimer && activeTab==1) RefreshProfileLegend(window);
        if(wParam==UiTimer) UpdateDataFileSize();
        if(wParam==UiTimer) UpdateProfileControlState();
        if(wParam==UiTimer && activeTab==2) {
            UpdateCalibrationLiveValues();
            UpdateCalibrationGraphIncrementally(window);
        }
        if(wParam==UiTimer && profileRunning.load()) {
            wchar_t progress[128]{};
            swprintf_s(progress, L"Profile scan: step %llu of %llu.", static_cast<unsigned long long>(profileStepIndex.load()),
                       static_cast<unsigned long long>(profileStepCount.load()));
            SetWindowTextW(mocoStatusLabel, progress);
        }
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
        }
        if(wParam==UiTimer && moco.IsOpen()) {
            std::wstring response,error;
            bool readOk = false;
            { std::lock_guard<std::mutex> lock(mocoMutex); readOk = moco.ReadAvailable(response,error); }
            if(!readOk){
                { std::lock_guard<std::mutex> lock(mocoMutex); moco.Close(); }
                SetWindowTextW(mocoStatusLabel,error.c_str()); UpdateMocoConnectionControls(false); UpdateStepButtonState(); UpdateProfileControlState();
                AppendMocoConsole(L"[Serial connection closed after read error]\r\n");
            }else {
                long long position = 0;
                if(!response.empty() && ParseMocoPositionReply(response,position)) {
                    mocoCurrentPosition=position;
                    mocoCurrentPositionValid=true;
                    UpdateMocoPositionDisplay();
                    mocoPositionQueryPending=false;
                    nextMocoPositionQuery=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
                    UpdateStepButtonState();
                    UpdateProfileControlState();
                }else if(!response.empty()) AppendMocoConsole(response);

                const auto now=std::chrono::steady_clock::now();
                if(mocoPositionQueryPending && now-mocoPositionQuerySent>std::chrono::seconds(1)) {
                    mocoPositionQueryPending=false;
                    nextMocoPositionQuery=now+std::chrono::milliseconds(500);
                    SetWindowTextW(mocoStatusLabel,L"No position reply yet; check the selected MoCo board address.");
                }
                if(!mocoPositionQueryPending && now>=nextMocoPositionQuery) {
                    bool sent = false;
                    { std::lock_guard<std::mutex> lock(mocoMutex); sent = moco.SendSubcommand('\'',error); }
                    if(sent) {
                        mocoPositionQueryPending=true;
                        mocoPositionQuerySent=now;
                    }else SetWindowTextW(mocoStatusLabel,error.c_str());
                }
            }
        }
        return 0;
    case WM_LBUTTONDOWN:{
        const int x=static_cast<short>(LOWORD(lParam)), y=static_cast<short>(HIWORD(lParam));
        if (activeTab == 0 && EnsureGraphSurface(window)) {
            const RECT bounds = GraphBounds(window);
            const int left = bounds.left + 72, right = bounds.left + graphCache.width - 340;
            const int top = bounds.top + 66, bottom = bounds.top + graphCache.height - 62;
            if (x >= left && x <= right && y >= top && y <= bottom) {
                if (!graphCache.valid) RenderGraphFully(window);
                graphZoomSelecting = true;
                graphZoomStart = graphZoomCurrent = POINT{x, y};
                SetFocus(window);
                SetCapture(window);
                return 0;
            }
        }
        if(activeTab==0 && x>=32 && y>=190){ SetFocus(window); return 0; }
        return DefWindowProcW(window,message,wParam,lParam);
    }
    case WM_LBUTTONDBLCLK:{
        const int x=static_cast<short>(LOWORD(lParam)), y=static_cast<short>(HIWORD(lParam));
        if(activeTab==0 && x>=32 && y>=190){ graphFollowingLive=true; graphCache.manualYScale=false; graphZoomHistory.clear(); EnableWindow(graphBackButton,FALSE); graphCache.valid=false; RECT plot=GraphBounds(window); InvalidateRect(window,&plot,FALSE); return 0; }
        if(activeTab==2){ const RECT live=CalibrationLiveGraphBounds(window); if(x>=live.left && x<=live.right && y>=live.top && y<=live.bottom){ calibrationLiveManualYScale=false; InvalidateRect(window,&live,TRUE); return 0; } }
        return DefWindowProcW(window,message,wParam,lParam);
    }
    case WM_RBUTTONDOWN:{
        if(activeTab==0 && EnsureGraphSurface(window)){
            const int x=static_cast<short>(LOWORD(lParam)), y=static_cast<short>(HIWORD(lParam));
            const RECT bounds=GraphBounds(window);
            const int left=bounds.left+72,right=bounds.left+graphCache.width-340;
            const int top=bounds.top+66,bottom=bounds.top+graphCache.height-62;
            if(x>=left && x<=right && y>=top && y<=bottom){
                if(!graphCache.valid) RenderGraphFully(window);
                graphPanning=true; graphPanMoved=false; graphPanLastX=x; graphPanLastY=y; SetTimer(window,UiTimer,30,nullptr); SetFocus(window); SetCapture(window); return 0;
            }
        }
        return DefWindowProcW(window,message,wParam,lParam);
    }
    case WM_MOUSEMOVE:{
        if (graphZoomSelecting) {
            graphZoomCurrent = POINT{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
            const RECT bounds = GraphBounds(window);
            InvalidateRect(window, &bounds, FALSE);
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return 0;
        }
        if(graphPanning){
            const int x=static_cast<short>(LOWORD(lParam)), y=static_cast<short>(HIWORD(lParam));
            const int dx=x-graphPanLastX, dy=y-graphPanLastY; graphPanLastX=x; graphPanLastY=y;
            if(dx!=0 || dy!=0){
                graphPanMoved = true;
                if(dx!=0){
                    if(graphFollowingLive){ graphViewEndSeconds=graphCache.viewEnd; graphFollowingLive=false; }
                    const int plotWidth=graphCache.width-340-72;
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
    case WM_LBUTTONUP:
        if (graphZoomSelecting) {
            const POINT end{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
            const POINT start = graphZoomStart;
            graphZoomSelecting = false;
            ReleaseCapture();
            ZoomGraphToSelection(window, start, end);
            const RECT bounds = GraphBounds(window);
            InvalidateRect(window, &bounds, FALSE);
            return 0;
        }
        return DefWindowProcW(window,message,wParam,lParam);
    case WM_RBUTTONUP:
        if(graphPanning){
            const bool wasDrag = graphPanMoved;
            graphPanning=false; SetTimer(window,UiTimer,100,nullptr);
            if (wasDrag) {
                graphCache.valid=false; RenderGraphFully(window);
                const RECT bounds=GraphBounds(window); InvalidateRect(window,&bounds,FALSE);
            }
            ReleaseCapture();
            if (!wasDrag) GoToPreviousZoom(window);
            return 0;
        }
        return DefWindowProcW(window,message,wParam,lParam);
    case WM_CAPTURECHANGED:
        graphPanning=false; graphZoomSelecting=false; SetTimer(window,UiTimer,100,nullptr); return 0;
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
            if(!graphCache.valid) RenderGraphFully(window);
            SaveCurrentZoom(window);
            if(controlHeld){
                double seconds=displayLengthSeconds.load()*std::pow(0.8,steps);
                seconds=(std::max)(0.001,(std::min)(604800.0,seconds)); displayLengthSeconds.store(seconds);
                if(!graphFollowingLive) ClampGraphViewToHistory(graphViewEndSeconds,seconds);
                SyncDisplayLengthField();
            }else{
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
        if (activeTab == 2) {
            const RECT live = CalibrationLiveGraphBounds(window);
            const int left = live.left + 100, right = live.right - 100;
            const int top = live.top + 34, bottom = live.bottom - 43;
            if (point.x >= left && point.x <= right && point.y >= top && point.y <= bottom) {
                const int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
                const bool controlHeld = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
                if (controlHeld) {
                    double seconds = displayLengthSeconds.load() * std::pow(0.8, steps);
                    seconds = (std::max)(0.001, (std::min)(604800.0, seconds));
                    displayLengthSeconds.store(seconds);
                    SyncDisplayLengthField();
                    graphCache.valid = false;
                    calibrationGraphCache.valid = false;
                } else {
                    std::vector<SignalSample> visible;
                    const double seconds = displayLengthSeconds.load();
                    {
                        std::lock_guard<std::mutex> lock(dataMutex);
                        if (!history.empty()) {
                            const double cutoff = history.back().seconds - seconds;
                            for (const auto& sample : history) if (sample.seconds >= cutoff) visible.push_back(sample);
                        }
                    }
                    double minV = 0.0, maxV = 0.0;
                    if (calibrationLiveManualYScale) { minV = calibrationLiveMinV; maxV = calibrationLiveMaxV; }
                    else CalibrationLiveAutoVoltageRange(visible, minV, maxV);
                    const double fraction = (std::max)(0.0, (std::min)(1.0, (point.y - top) / static_cast<double>(bottom - top)));
                    const double anchor = maxV - fraction * (maxV - minV);
                    const double factor = std::pow(0.8, steps);
                    minV = anchor + (minV - anchor) / factor;
                    maxV = anchor + (maxV - anchor) / factor;
                    if (maxV - minV < 1e-6) { const double center = (minV + maxV) * 0.5; minV = center - 5e-7; maxV = center + 5e-7; }
                    if (maxV - minV > 1000.0) { const double center = (minV + maxV) * 0.5; minV = center - 500.0; maxV = center + 500.0; }
                    calibrationLiveMinV = minV; calibrationLiveMaxV = maxV;
                    calibrationLiveManualYScale = true;
                    calibrationGraphCache.valid = false;
                }
                InvalidateRect(window, &live, TRUE);
                return 0;
            }
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
        calibrationGraphCache.valid=false;
        const RECT bounds=GraphBounds(window);
        InvalidateRect(window,&bounds,FALSE);
        return 0;
    }
    case ProfileFinishedMessage: {
        if (profileThread.joinable()) profileThread.join();
        profileRunning.store(false);
        UpdateProfileControlState();
        std::wstring result;
        { std::lock_guard<std::mutex> lock(profileResultMutex); result = profileResult; }
        SetWindowTextW(mocoStatusLabel, result.c_str());
        if (closeAfterProfile) PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    }
    case ProfilePlotUpdatedMessage: {
        RefreshProfileLegend(window);
        const RECT chart = ProfileGraphBounds(window);
        InvalidateRect(window, &chart, TRUE);
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
        RefreshProfileLegend(window);
        ClearCommentHover(window);
        int w=LOWORD(lParam),h=HIWORD(lParam);
        LayoutStatusBar(window);
        graphCache.valid=false;
        MoveWindow(tabs,20,18,w-40,46,TRUE);
        MoveWindow(frequencyLabel,w-550,h-104,150,34,TRUE); MoveWindow(frequencyInput,w-390,h-108,80,40,TRUE);
        MoveWindow(displayLengthLabel,32,h-104,130,34,TRUE); MoveWindow(displayLengthInput,164,h-108,90,40,TRUE);
        MoveWindow(fileButton,32,118,176,40,TRUE); MoveWindow(infoLabel,32,184,activeTab==2?850:w-64,28,TRUE);
        MoveWindow(graphHelpButton,w-76,196,28,28,TRUE);
        MoveWindow(graphBackButton,w-122,196,40,28,TRUE);
        MoveWindow(mocoHeading,32,118,363,34,TRUE);
        MoveWindow(mocoPortLabel,32,174,90,28,TRUE); MoveWindow(mocoPortInput,132,168,140,300,TRUE);
        MoveWindow(mocoBaudLabel,32,234,90,28,TRUE); MoveWindow(mocoBaudInput,132,228,140,120,TRUE);
        MoveWindow(mocoAddressLabel,32,294,90,28,TRUE); MoveWindow(mocoAddressInput,132,288,140,300,TRUE);
        MoveWindow(mocoConnectButton,32,348,110,40,TRUE); MoveWindow(mocoDisconnectButton,154,348,120,40,TRUE); MoveWindow(mocoAdvancedButton,290,348,110,40,TRUE);
        MoveWindow(manualControlHeading,32,118,355,32,TRUE);
        MoveWindow(mocoPositionLabel,32,162,205,30,TRUE); MoveWindow(mocoPositionInput,242,156,100,36,TRUE);
        MoveWindow(stepLabel,32,208,116,30,TRUE); MoveWindow(stepInput,150,202,80,36,TRUE);
        MoveWindow(stepUpButton,248,197,42,46,TRUE); MoveWindow(stepDownButton,296,197,42,46,TRUE);
        MoveWindow(goToLabel,32,254,116,30,TRUE); MoveWindow(goToInput,150,248,80,36,TRUE); MoveWindow(goToButton,258,248,75,36,TRUE);
        MoveWindow(setPositionLabel,32,300,116,30,TRUE); MoveWindow(setPositionInput,150,294,80,36,TRUE); MoveWindow(setPositionButton,258,294,75,36,TRUE);
        MoveWindow(profileSettingsHeading,32,360,468,32,TRUE);
        MoveWindow(profilingStartLabel,32,402,100,28,TRUE); MoveWindow(profilingStartInput,138,396,80,36,TRUE);
        MoveWindow(profileEndLabel,286,402,90,28,TRUE); MoveWindow(profileEndInput,380,396,80,36,TRUE);
        MoveWindow(profileStepLabel,32,448,100,28,TRUE); MoveWindow(profileStepInput,138,442,80,36,TRUE);
        MoveWindow(profileSafeLabel,286,448,90,28,TRUE); MoveWindow(profileSafeInput,380,442,80,36,TRUE);
        MoveWindow(profileWaitLabel,32,494,100,28,TRUE); MoveWindow(profileWaitInput,138,488,80,36,TRUE);
        MoveWindow(profileMeasureLabel,286,494,90,28,TRUE); MoveWindow(profileMeasureInput,380,488,80,36,TRUE);
        MoveWindow(profileOvershootLabel,32,540,100,28,TRUE); MoveWindow(profileOvershootInput,138,534,80,36,TRUE);
        MoveWindow(profileOvershootWaitLabel,286,540,90,28,TRUE); MoveWindow(profileOvershootWaitInput,380,534,80,36,TRUE);
        MoveWindow(scaleLabel,32,414,220,30,TRUE); MoveWindow(scaleInput,260,408,135,36,TRUE);
        MoveWindow(profileLabelCaption,32,580,100,28,TRUE); MoveWindow(profileLabelInput,138,576,180,32,TRUE);
        MoveWindow(profileCyclesInput,380,576,80,32,TRUE); MoveWindow(profileCyclesLabel,324,580,54,28,TRUE);
        MoveWindow(profileRunButton,32,616,100,32,TRUE);
        MoveWindow(profileCycleIntervalLabel,150,622,150,28,TRUE); MoveWindow(profileCycleIntervalInput,310,616,80,32,TRUE);
        FitProfilingNumericFields(window);
        MoveWindow(mocoStatusLabel,32,454,363,70,TRUE);
        MoveWindow(mocoCommandLabel,32,688,180,28,TRUE);
        MoveWindow(mocoCommandInput,220,684,w-666,32,TRUE); MoveWindow(mocoSendButton,w-430,684,110,32,TRUE);
        MoveWindow(mocoConsoleLabel,32,722,w-64,24,TRUE); MoveWindow(mocoConsole,32,748,w-64,(std::max)(60,h-850),TRUE);
        MoveWindow(commentButton,280,h-112,120,48,TRUE);
        MoveWindow(startButton,w-290,h-112,160,48,TRUE); MoveWindow(stopButton,w-120,h-112,90,48,TRUE);
        PositionCalibrationLegend(window);
        if (activeTab == 2) {
            const RECT liveGraph = CalibrationLiveGraphBounds(window);
            const RECT curveGraph = CalibrationCurveGraphBounds(window);
            InvalidateRect(window, &liveGraph, TRUE); InvalidateRect(window, &curveGraph, TRUE);
        }
        { RECT plot=GraphBounds(window); InvalidateRect(window,&plot,FALSE); }
        if (activeTab == 1) { const RECT chart = ProfileGraphBounds(window); InvalidateRect(window, &chart, TRUE); }
        return 0;
    }
    case WM_GETMINMAXINFO:{
        // Keep the bottom controls and the graph's plotting area usable while
        // resizing. Track sizes are window sizes, so account for the current
        // caption and border styles rather than assuming a fixed frame width.
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        RECT minimumClient{0, 0, 1450, 850};
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
        SaveNumericSettings();
        if(profileRunning.load()) {
            closeAfterProfile = true;
            profileCancelRequested.store(true);
            SetWindowTextW(mocoStatusLabel, L"Stopping the profile scan before closing...");
            EnableWindow(profileRunButton, FALSE);
            return 0;
        }
        closeAfterAcquisition = true;
        { std::lock_guard<std::mutex> lock(mocoMutex); moco.Close(); }
        shutdownRequested.store(true);
        SetWindowTextW(window,L"Tasker v1.30 — closing acquisition safely...");
        EnableWindow(startButton,FALSE); EnableWindow(stopButton,FALSE); EnableWindow(fileButton,FALSE); EnableWindow(commentButton,FALSE);
        if(!acquisitionAlive.load()){
            if(acquisitionThread.joinable()) acquisitionThread.join();
            recording.store(false);
            DestroyWindow(window);
        }
        return 0;
    case WM_DESTROY:
        KillTimer(window,UiTimer);
        if(profileThread.joinable()) profileThread.join();
        { std::lock_guard<std::mutex> lock(mocoMutex); moco.Close(); }
        if(arrowButtonFont) { DeleteObject(arrowButtonFont); arrowButtonFont=nullptr; }
        DestroyGraphSurface(); PostQuitMessage(0); return 0;
    default:return DefWindowProcW(window,message,wParam,lParam);
    }
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int showCommand){
    LoadConfiguration();
    WNDCLASSW wc{}; wc.style=CS_DBLCLKS; wc.lpfnWndProc=WindowProcedure; wc.hInstance=instance; wc.lpszClassName=WindowClassName; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassW(&wc)) return 1;
    HWND window=CreateWindowExW(0,WindowClassName,L"Tasker v1.30",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1740,935,nullptr,nullptr,instance,nullptr);
    if(!window)return 1;
    ACCEL commentShortcut{static_cast<BYTE>(FVIRTKEY | FCONTROL), static_cast<WORD>('K'), static_cast<WORD>(CommentShortcutId)};
    HACCEL accelerators=CreateAcceleratorTableW(&commentShortcut,1);
    ShowWindow(window,showCommand); UpdateWindow(window);
    // Show the main window before opening the modal file picker. Opening it
    // during WM_CREATE can leave the owner hidden behind the dialog.
    if (ChooseDataFile(window)) WriteLog(L"Ready to record to " + dataFilePath + L".");
    else MessageBoxW(window, L"No storage file selected. Choose one before starting recording.", L"Tasker", MB_OK | MB_ICONINFORMATION);
    StartAcquisition(window);
    MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0){
        if (accelerators && TranslateAcceleratorW(window,accelerators,&msg)) continue;
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB) {
            if (FocusNextTabStop(window, (GetKeyState(VK_SHIFT) & 0x8000) != 0)) continue;
        }
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if(accelerators) DestroyAcceleratorTable(accelerators);
    return static_cast<int>(msg.wParam);
}





