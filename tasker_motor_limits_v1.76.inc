// MoCo V3.50: TS/% byte 4 enables; byte 5 switch inputs and hardware jumpers.
struct MotorSwitchStatus {unsigned char bytes[7]{};size_t count=0;};
std::atomic<bool> motorSwitchStatusValid{false},motorReverseActive{false},motorForwardActive{false},motorSwitchProtection{false};
std::atomic<ULONGLONG> motorSwitchStatusAt{0};
std::atomic<bool> manualMotorMovePending{false},motorLimitsConfirmed{false};
std::atomic<int> manualMotorDirection{0};
std::atomic<long long> manualMotorTarget{0},motorReverseLimitRaw{0},motorForwardLimitRaw{0};
std::atomic<ULONGLONG> manualMotorFinishedAt{0};
bool motorLimitsConfigured=false;
HWND motorLimitsButton=nullptr,motorLimitsSummary=nullptr;
constexpr int MotorLimitsButtonId=7920;
std::wstring MotorLimitConfigSection(){
 wchar_t port[64]{},address[32]{};GetWindowTextW(mocoPortInput,port,64);GetWindowTextW(mocoAddressInput,address,32);
 return std::wstring(L"MotorLimits_")+port+L"_"+address;
}
void RefreshMotorLimitSummary(){
 if(!motorLimitsSummary)return;
 if(!motorLimitsConfigured){SetWindowTextW(motorLimitsSummary,L"Travel limits: not configured");return;}
 double scale=0;{SilentNumericScope silent;if(!ReadPositiveNumber(scaleInput,scale))return;}
 wchar_t text[256]{};swprintf_s(text,L"Limits: %.6g to %.6g %ls\n%ls",DepthValue(motorReverseLimitRaw.load()/scale-softwarePositionOffsetMicrons.load()),
  DepthValue(motorForwardLimitRaw.load()/scale-softwarePositionOffsetMicrons.load()),DepthUnit(),motorLimitsConfirmed?L"Confirmed for this connection":L"Confirm limits before profiling");
 SetWindowTextW(motorLimitsSummary,text);
}
void ResetMotorLimitSession(bool loadConfiguration){
 motorSwitchStatusValid=false;motorSwitchStatusAt=0;manualMotorMovePending=false;manualMotorFinishedAt=0;manualMotorDirection=0;motorLimitsConfirmed=false;
 motorReverseActive=false;motorForwardActive=false;motorSwitchProtection=false;
 if(loadConfiguration){
  wchar_t low[64]{},high[64]{};const auto section=MotorLimitConfigSection();
  GetPrivateProfileStringW(section.c_str(),L"ReverseEncoder",L"",low,64,configFilePath.c_str());
  GetPrivateProfileStringW(section.c_str(),L"ForwardEncoder",L"",high,64,configFilePath.c_str());
  wchar_t* endLow=nullptr;wchar_t* endHigh=nullptr;errno=0;
  const auto a=std::wcstoll(low,&endLow,10),b=std::wcstoll(high,&endHigh,10);
  motorLimitsConfigured=errno!=ERANGE && *low && *high && !*endLow && !*endHigh && a<b && a>=-1073741843LL && b<=1073741843LL;
  if(motorLimitsConfigured){motorReverseLimitRaw=a;motorForwardLimitRaw=b;}
 }
 RefreshMotorLimitSummary();
}
bool ParseMotorSwitchStatus(const std::wstring& replies,MotorSwitchStatus& result){
 bool valid=false;size_t start=0;
 while(start<replies.size()){
  const auto end=replies.find(L'\n',start);if(end==std::wstring::npos)break;
  const auto line=replies.substr(start,end-start);start=end+1;const auto marker=line.find(L"S0:");if(marker==std::wstring::npos)continue;
  std::wstring digits,token;std::vector<std::wstring> tokens;bool bad=false;
  for(size_t i=marker+3;i<line.size();++i){wchar_t c=line[i];
   if(std::iswxdigit(c)){digits+=c;token+=c;}
   else if(std::iswspace(c)||c==L','||c==L';'){if(!token.empty()){tokens.push_back(token);token.clear();}}
   else {bad=true;break;}
  }
  if(!token.empty())tokens.push_back(token);if(bad)continue;
  MotorSwitchStatus parsed;
  if(digits.size()==12||digits.size()==14){parsed.count=digits.size()/2;for(size_t i=0;i<parsed.count;++i)parsed.bytes[i]=static_cast<unsigned char>(std::wcstoul(digits.substr(i*2,2).c_str(),nullptr,16));}
  else if(tokens.size()==6||tokens.size()==7){parsed.count=tokens.size();for(size_t i=0;i<tokens.size();++i){if(tokens[i].size()>2){bad=true;break;}parsed.bytes[i]=static_cast<unsigned char>(std::wcstoul(tokens[i].c_str(),nullptr,16));}}
  else continue;
  if(!bad){result=parsed;valid=true;}
 }
 return valid;
}
void StopAtMotorLimit(HWND owner,const std::wstring& reason){
 const bool profiling=profileRunning.load();manualMotorMovePending=false;manualMotorFinishedAt=0;
 if(profiling){profileCancelRequested=true;profilePauseRequested=false;}
 motorStopGeneration.fetch_add(1);
 std::wstring error;bool stopped=false;
 {std::lock_guard<std::mutex> lock(mocoMutex);if(moco.IsOpen()){
    stopped=moco.SendSubcommand('!',error);std::wstring abortError;
    if(!moco.SendAsciiCommand(L"AB0",abortError)){stopped=false;error+=L" "+abortError;}
 }}
 std::wstring text=reason+(stopped?L"\nMotor stop sent.":L"\nCould not confirm the stop command: "+error);
 if(profiling)text+=L"\nThe profiling sequence has been aborted. Correct the position and parameters before starting again.";
 SetWindowTextW(mocoStatusLabel,text.c_str());WriteLog(text);TaskerMessageBox(owner,text.c_str(),L"Motor limit switch",MB_OK|MB_ICONWARNING);
}
void RecordMotorSwitchStatus(const MotorSwitchStatus& status,HWND owner){
 const bool previousReverse=motorReverseActive.load(),previousForward=motorForwardActive.load();
 const bool reverse=(status.bytes[5]&0x01)!=0,forward=(status.bytes[5]&0x04)!=0;
 const bool protection=(status.bytes[4]&0x41)==0x41 && (status.bytes[5]&0x0a)==0x0a;
 motorReverseActive=reverse;motorForwardActive=forward;motorSwitchProtection=protection;
 motorSwitchStatusAt=GetTickCount64();motorSwitchStatusValid=true;
 const bool profiling=profileRunning.load() && !profileCancelRequested.load();
 const bool moving=manualMotorMovePending.load()||profiling;
 if(moving && !protection){StopAtMotorLimit(owner,L"Limit-switch protection is disabled. Check the controller configuration and limit-switch jumpers.");return;}
 const int direction=manualMotorDirection.load();
 const ULONGLONG finished=manualMotorFinishedAt.load();
 const bool recentlyFinished=finished && GetTickCount64()-finished<=2000;
 const bool manualHit=(manualMotorMovePending.load() && ((direction<0&&reverse)||(direction>0&&forward))) ||
  (recentlyFinished && ((direction<0&&reverse&&!previousReverse)||(direction>0&&forward&&!previousForward)));
 if((profiling && (reverse||forward)) || manualHit){
  StopAtMotorLimit(owner,reverse?L"The reverse limit switch is active. Move in the positive direction, away from this switch.":L"The forward limit switch is active. Move in the negative direction, away from this switch.");
 }
}
bool ReadFreshMotorSwitchStatus(HWND owner,std::wstring& error){
 MotorSwitchStatus status;bool found=false;
 {
  std::unique_lock<std::mutex> lock(mocoMutex);
  if(!moco.IsOpen()){error=L"Connect to the MoCo controller first.";return false;}
  // Finish old input before requesting an unambiguous, fresh status reply.
  auto quietSince=std::chrono::steady_clock::now();
  const auto drainEnd=quietSince+std::chrono::milliseconds(350);
  while(std::chrono::steady_clock::now()<drainEnd){
   std::wstring old;if(!moco.ReadAvailable(old,error))return false;
   if(!old.empty())quietSince=std::chrono::steady_clock::now();
   if(std::chrono::steady_clock::now()-quietSince>=std::chrono::milliseconds(35))break;
   Sleep(2);
  }
  if(std::chrono::steady_clock::now()-quietSince<std::chrono::milliseconds(35)){error=L"The MoCo serial input did not become idle. No movement was started.";return false;}
  mocoPositionReplyBuffer.clear();mocoPositionQueryPending=false;
  if(!moco.SendSubcommand('%',error))return false;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(1200);std::wstring buffer;
  while(std::chrono::steady_clock::now()<deadline){
   std::wstring reply;if(!moco.ReadAvailable(reply,error))return false;buffer+=reply;
   if(ParseMotorSwitchStatus(buffer,status)){found=true;break;}Sleep(5);
  }
 }
 if(!found){motorSwitchStatusValid=false;error=L"Could not read a complete MoCo limit-switch status. No movement was started.";return false;}
 RecordMotorSwitchStatus(status,owner);
 if(!motorSwitchProtection.load()){error=L"Both limit switches must be enabled in the controller, including their hardware jumpers, before Tasker moves the motor.";return false;}
 return true;
}
bool CheckManualMotorLimit(HWND owner,long long target){
 if(manualMotorMovePending.load()){
  TaskerMessageBox(owner,L"The motor is still moving. Wait until it stops, or press Esc before issuing another manual move.",L"Manual motor control",MB_OK|MB_ICONINFORMATION);return false;
 }
 std::wstring error;if(!ReadFreshMotorSwitchStatus(owner,error)){TaskerMessageBox(owner,error.c_str(),L"Motor limit switches",MB_OK|MB_ICONWARNING);return false;}
 if(manualMotorMovePending.load()){
  TaskerMessageBox(owner,L"The motor is still moving. Wait until it stops, or press Esc before issuing another manual move.",L"Manual motor control",MB_OK|MB_ICONINFORMATION);return false;
 }
 const long long current=mocoCurrentPosition.load();const int direction=target>current?1:target<current?-1:0;
 if((motorReverseActive && direction<0)||(motorForwardActive && direction>0)||(motorReverseActive&&motorForwardActive)){
  const wchar_t* text=motorReverseActive&&motorForwardActive?L"Both limit switches are active. Check the switch wiring and polarity before moving.":
    direction<0?L"The reverse limit switch is active. Move in the positive direction, away from the switch.":L"The forward limit switch is active. Move in the negative direction, away from the switch.";
  TaskerMessageBox(owner,text,L"Motor limit switch",MB_OK|MB_ICONWARNING);return false;
 }
 manualMotorTarget=target;manualMotorDirection=direction;manualMotorFinishedAt=0;return true;
}
bool ProfileRawTargetAllowed(long long target){
 return motorLimitsConfirmed.load() && motorSwitchStatusValid.load() && motorSwitchProtection.load() &&
  !motorReverseActive.load() && !motorForwardActive.load() && GetTickCount64()-motorSwitchStatusAt.load()<2000 &&
  target>motorReverseLimitRaw.load() && target<motorForwardLimitRaw.load();
}
bool CheckProfileMotorTravel(HWND owner,double start,double end,double safe,double overshoot,double scale,double offset,bool reverse,int reverseMode){
 if(!motorLimitsConfirmed.load()){
  TaskerMessageBox(owner,L"Set and confirm the reverse and forward switch positions in Settings > Travel limits before profiling. Confirmation is required after each motor connection.",L"Profile travel limits",MB_OK|MB_ICONWARNING);return false;
 }
 if(manualMotorMovePending.load()){TaskerMessageBox(owner,L"Wait for the manual motor move to finish before starting a profile.",L"Profile travel limits",MB_OK|MB_ICONWARNING);return false;}
 std::wstring error;if(!ReadFreshMotorSwitchStatus(owner,error)){TaskerMessageBox(owner,error.c_str(),L"Profile travel limits",MB_OK|MB_ICONWARNING);return false;}
 if(motorReverseActive||motorForwardActive){TaskerMessageBox(owner,L"A limit switch is active. Move in the opposite direction, away from that switch, before profiling.",L"Profile travel limits",MB_OK|MB_ICONWARNING);return false;}
 const double direction=end>=start?1.0:-1.0;
 std::vector<std::pair<std::wstring,double>> targets={{L"Start",start},{L"End",end},{L"Safe",safe}};
 if(overshoot>0){targets.push_back({L"Start overshoot",start+direction*overshoot});targets.push_back({L"End overshoot",end+direction*overshoot});
  if(reverse && reverseMode!=0){const double d=reverseMode==2?-direction:direction;targets.push_back({L"Reverse overshoot at Start",start+d*overshoot});targets.push_back({L"Reverse overshoot at End",end+d*overshoot});}}
 const long long low=motorReverseLimitRaw.load(),high=motorForwardLimitRaw.load();
 if(mocoCurrentPosition.load()<=low || mocoCurrentPosition.load()>=high){TaskerMessageBox(owner,L"The current motor position is at or outside the configured switch positions. Move inside the travel range or correct the configured limits before profiling.",L"Profile travel limits",MB_OK|MB_ICONWARNING);return false;}
 for(const auto& target:targets){
  const long double raw=(static_cast<long double>(target.second)+offset)*scale;
  if(!std::isfinite(static_cast<double>(raw))||raw<-1073741843LL||raw>1073741843LL || std::llround(raw)<=low || std::llround(raw)>=high){
   wchar_t message[512]{};swprintf_s(message,L"%ls (%.6g %ls) reaches or passes a limit switch.\nKeep every scan point, overshoot and Safe position strictly between %.6g and %.6g %ls.\nNo profiling movement was started.",
    target.first.c_str(),DepthValue(target.second),DepthUnit(),DepthValue(low/scale-offset),DepthValue(high/scale-offset),DepthUnit());
   TaskerMessageBox(owner,message,L"Profile travel limit conflict",MB_OK|MB_ICONWARNING);return false;
  }
 }
 return true;
}
struct MotorLimitsDialogState {HWND reverseField=nullptr,forwardField=nullptr;double scale=0;};
INT_PTR CALLBACK MotorLimitsDialogProcedure(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam){
 auto* state=reinterpret_cast<MotorLimitsDialogState*>(GetWindowLongPtrW(dialog,DWLP_USER));
 if(message==WM_INITDIALOG){state=reinterpret_cast<MotorLimitsDialogState*>(lParam);SetWindowLongPtrW(dialog,DWLP_USER,lParam);SetWindowTextW(dialog,L"Motor travel limits");
  RECT r{};GetClientRect(dialog,&r);const auto instance=GetModuleHandleW(nullptr);
  CreateWindowW(L"STATIC",L"Enter the actual switch positions in Tasker's current coordinate system. Apply confirms these limits for this connection.",WS_CHILD|WS_VISIBLE,16,12,r.right-32,46,dialog,nullptr,instance,nullptr);
  const std::wstring reverse=std::wstring(L"Reverse switch (")+DepthUnit()+L")",forward=std::wstring(L"Forward switch (")+DepthUnit()+L")";
  CreateWindowW(L"STATIC",reverse.c_str(),WS_CHILD|WS_VISIBLE,16,70,190,24,dialog,nullptr,instance,nullptr);
  CreateWindowW(L"STATIC",forward.c_str(),WS_CHILD|WS_VISIBLE,16,108,190,24,dialog,nullptr,instance,nullptr);
  state->reverseField=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_RIGHT|ES_AUTOHSCROLL,210,66,130,28,dialog,reinterpret_cast<HMENU>(61001),instance,nullptr);
  state->forwardField=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_RIGHT|ES_AUTOHSCROLL,210,104,130,28,dialog,reinterpret_cast<HMENU>(61002),instance,nullptr);
  if(motorLimitsConfigured){wchar_t value[80]{};swprintf_s(value,L"%.12g",DepthValue(motorReverseLimitRaw.load()/state->scale-softwarePositionOffsetMicrons.load()));SetWindowTextW(state->reverseField,value);swprintf_s(value,L"%.12g",DepthValue(motorForwardLimitRaw.load()/state->scale-softwarePositionOffsetMicrons.load()));SetWindowTextW(state->forwardField,value);}
  CreateWindowW(L"BUTTON",L"Apply",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,r.right-204,r.bottom-44,88,28,dialog,reinterpret_cast<HMENU>(IDOK),instance,nullptr);
  CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,r.right-108,r.bottom-44,88,28,dialog,reinterpret_cast<HMENU>(IDCANCEL),instance,nullptr);return TRUE;
 }
 if(message==WM_COMMAND){NumericActionScope numericScope(HIWORD(wParam)==BN_CLICKED);
  if(LOWORD(wParam)==IDCANCEL){EndDialog(dialog,IDCANCEL);return TRUE;}
  if(LOWORD(wParam)==IDOK && state){
   double low=0,high=0;if(!ReadFiniteNumber(state->reverseField,low)||!ReadFiniteNumber(state->forwardField,high))return TRUE;
   const long double a=(static_cast<long double>(low)*DepthMicronsPerUnit(depthUnitIndex.load())+softwarePositionOffsetMicrons.load())*state->scale;
   const long double b=(static_cast<long double>(high)*DepthMicronsPerUnit(depthUnitIndex.load())+softwarePositionOffsetMicrons.load())*state->scale;
   if(!std::isfinite(static_cast<double>(a))||!std::isfinite(static_cast<double>(b))||a<-1073741843LL||b>1073741843LL||a>=b||std::llround(a)>=std::llround(b)){
    NumericFailure(state->forwardField,L"Enter a forward switch position greater than the reverse position, within the controller's position range.");TaskerMessageBox(dialog,L"Invalid motor travel range.",L"Motor travel limits",MB_OK|MB_ICONWARNING);return TRUE;
   }
   if(profileRunning||manualMotorMovePending){TaskerMessageBox(dialog,L"Stop the motor and finish the current profiling sequence before confirming new limits.",L"Motor travel limits",MB_OK|MB_ICONWARNING);return TRUE;}
   motorLimitsConfirmed=false;motorReverseLimitRaw=std::llround(a);motorForwardLimitRaw=std::llround(b);motorLimitsConfigured=true;motorLimitsConfirmed=true;
   const auto section=MotorLimitConfigSection();WritePrivateProfileStringW(section.c_str(),L"ReverseEncoder",std::to_wstring(motorReverseLimitRaw.load()).c_str(),configFilePath.c_str());WritePrivateProfileStringW(section.c_str(),L"ForwardEncoder",std::to_wstring(motorForwardLimitRaw.load()).c_str(),configFilePath.c_str());
   RefreshMotorLimitSummary();WriteLog(L"Confirmed motor switch coordinates for this connection.");EndDialog(dialog,IDOK);return TRUE;
  }
 }
 if(message==WM_CLOSE){EndDialog(dialog,IDCANCEL);return TRUE;}return FALSE;
}
void ShowMotorLimitsDialog(HWND owner){
 MotorLimitsDialogState state;if(!ReadPositiveNumber(scaleInput,state.scale))return;
 struct {DLGTEMPLATE dialog;WORD menu,windowClass,title;} layout{};
 layout.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER;layout.dialog.cx=265;layout.dialog.cy=132;
 DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&layout.dialog,owner,MotorLimitsDialogProcedure,reinterpret_cast<LPARAM>(&state));
}
void CreateMotorLimitsControls(HWND owner){
 const auto instance=GetModuleHandleW(nullptr);
 motorLimitsSummary=CreateWindowW(L"STATIC",L"Travel limits: not configured",WS_CHILD,32,540,363,42,owner,nullptr,instance,nullptr);
 motorLimitsButton=CreateWindowW(L"BUTTON",L"Travel limits…",WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,32,590,140,32,owner,reinterpret_cast<HMENU>(static_cast<INT_PTR>(MotorLimitsButtonId)),instance,nullptr);
}
