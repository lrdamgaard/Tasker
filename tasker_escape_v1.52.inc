constexpr UINT MotorEscStoppedMessage=WM_APP+21;
std::atomic<bool> escapeMonitorShutdown{false};
std::atomic<ULONGLONG> previousEscTick{0};
std::thread escapeMonitorThread;
std::mutex escapeResultMutex;
struct EscStopResult {bool profiling=false,connected=false,sent=false;std::wstring error;} escapeResult;

void RequestMotorEscStop(HWND owner) {
    if(escapeMonitorShutdown.load())return;
    static std::atomic_flag sending=ATOMIC_FLAG_INIT;
    if(sending.test_and_set())return;
    struct SendingGuard {std::atomic_flag& flag;~SendingGuard(){flag.clear();}} sendingGuard{sending};
    const ULONGLONG now=GetTickCount64();auto previous=previousEscTick.load();
    if((previous && now-previous<150) || !previousEscTick.compare_exchange_strong(previous,now))return;
    EscStopResult result;
    result.profiling=profileRunning.load() && !profileCancelRequested.load();
    if(result.profiling) {
        // Stop the worker before waiting for serial access or a UI paint.
        profilePauseRequested.store(true);profilePaused.store(true);
    }
    motorStopGeneration.fetch_add(1);
    {
        std::lock_guard<std::mutex> captureLock(profileCaptureMutex);
        profileCapture.active=false;
    }
    {
        std::lock_guard<std::mutex> serialLock(mocoMutex);
        result.connected=moco.IsOpen();
        if(result.connected) {
            result.sent=moco.SendSubcommand('!',result.error);
            if(result.sent) {
                // Also explicitly reset the addressed motor's target to its
                // stopped position, as documented for abrupt abort AB0.
                std::wstring error;
                if(!moco.SendAsciiCommand(L"AB0",error))result.error=error;
            }
        } else result.error=L"The motor connection is not open.";
    }
    {std::lock_guard<std::mutex> lock(escapeResultMutex);escapeResult=result;}
    PostMessageW(owner,MotorEscStoppedMessage,0,0);
}

void StopMotorWithEsc() {
    if(!closeConfirmed)RequestMotorEscStop(GetParent(mocoStatusLabel));
}

void StartEscapeMonitor(HWND owner) {
    escapeMonitorShutdown.store(false);
    escapeMonitorThread=std::thread([owner] {
        bool wasDown=false;
        while(!escapeMonitorShutdown.load()) {
            DWORD foregroundProcess=0;
            GetWindowThreadProcessId(GetForegroundWindow(),&foregroundProcess);
            const bool down=foregroundProcess==GetCurrentProcessId() && (GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0;
            if(down && !wasDown)RequestMotorEscStop(owner);
            wasDown=down;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
}

void HandleMotorEscStopped(HWND window) {
    EscStopResult result;
    {std::lock_guard<std::mutex> lock(escapeResultMutex);result=escapeResult;}
    profileStartPending=false;
    mocoPositionQueryPending=false;nextMocoPositionQuery=std::chrono::steady_clock::now();
    std::wstring message;
    if(result.sent)message=result.profiling?L"Paused by Esc. Motor stop sent; Wait/Measuring interrupted. Resume retries this point; Abort ends the run.":L"Esc: motor stop sent.";
    else message=L"Esc: profile paused, but motor stop could not be sent: "+result.error;
    SetWindowTextW(mocoStatusLabel,message.c_str());WriteLog(message);
    AppendMocoConsole(result.sent?L"> ! AB0 [Esc stop]\r\n":L"[Esc stop transmission failed]\r\n");
    UpdateProfileControlState();UpdateStepButtonState();
    if(!result.sent)MessageBoxW(window,message.c_str(),L"Motor stop",MB_OK|MB_ICONERROR);
}
