// Transposed profile selector: selected channels are columns, profiles are rows.
constexpr int ProfileMatrixLeft=180,ProfileMatrixTop=190,ProfileColumnWidth=60,ProfileRowHeight=30,ProfileVisibleRows=6;
std::vector<size_t> ProfileLegendChannels() {
    std::vector<size_t> result;
    for(size_t channel=0;channel<8;++channel)if(ChannelEnabled(channel))result.push_back(channel);
    return result;
}
RECT ProfileLegendBounds(HWND window) {
    const RECT chart=ProfileGraphBounds(window);
    const int width=(std::max)(360,ProfileMatrixLeft+static_cast<int>(ProfileLegendChannels().size())*ProfileColumnWidth+22);
    return RECT{chart.right-width,chart.top+30,chart.right,chart.top+30+ProfileMatrixTop+ProfileVisibleRows*ProfileRowHeight+4};
}
void RefreshProfileLegend(HWND window) {
    if(!profileLegendWindow)return;
    RECT bounds=ProfileLegendBounds(window),current{};
    GetWindowRect(profileLegendWindow,&current);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&current),2);
    if(!EqualRect(&bounds,&current))MoveWindow(profileLegendWindow,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,TRUE);
    RECT client{};GetClientRect(profileLegendWindow,&client);
    MoveWindow(profileLegendScroll,client.right-18,ProfileMatrixTop,GetSystemMetrics(SM_CXVSCROLL),ProfileVisibleRows*ProfileRowHeight,TRUE);
    size_t count=0;{std::lock_guard<std::mutex> lock(profilePlotMutex);count=profileDatasets.size();}
    profileLegendFirst=(std::max)(0,(std::min)(profileLegendFirst,static_cast<int>(count)-ProfileVisibleRows));
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS|SIF_DISABLENOSCROLL};
    info.nMax=(std::max)(0,static_cast<int>(count)-1);info.nPage=ProfileVisibleRows;info.nPos=profileLegendFirst;
    SetScrollInfo(profileLegendScroll,SB_CTL,&info,TRUE);InvalidateRect(profileLegendWindow,nullptr,FALSE);
}
void ProfileLegendChanged(HWND legend) {
    const RECT chart=ProfileGraphBounds(GetParent(legend));InvalidateRect(GetParent(legend),&chart,FALSE);InvalidateRect(legend,nullptr,FALSE);
}
LRESULT CALLBACK ProfileLegendProcedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message) {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};HDC screen=BeginPaint(window,&paint);RECT client{};GetClientRect(window,&client);
        HDC dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,(std::max)(1L,client.right),(std::max)(1L,client.bottom));HGDIOBJ oldBitmap=SelectObject(dc,bitmap);
        HBRUSH bg=CreateSolidBrush(RGB(250,251,253));FillRect(dc,&client,bg);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(42,52,66));
        HFONT font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");HGDIOBJ oldFont=SelectObject(dc,font);
        auto text=[&](const std::wstring& value,RECT rect,UINT flags=DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX){DrawTextW(dc,value.c_str(),-1,&rect,flags);};
        RECT calibrated{10,9,30,29};DrawFrameControl(dc,&calibrated,DFC_BUTTON,DFCS_BUTTONCHECK|(profileGraphCalibrated?DFCS_CHECKED:0));text(L"Calibrated",RECT{38,5,150,33},DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if(GetFocus()==window && profileLegendFocusRow<0){RECT focus{7,5,150,33};DrawFocusRect(dc,&focus);}
        text(L"Channel",RECT{12,42,175,82});text(L"Live value",RECT{12,92,175,120});
        text(L"Profile no.",RECT{2,163,62,188});text(L"Profile label",RECT{65,163,175,188});
        struct Header{unsigned long long number;std::wstring label;std::array<bool,8> visible;};std::vector<Header> datasets;
        {std::lock_guard<std::mutex> lock(profilePlotMutex);for(auto it=profileDatasets.rbegin();it!=profileDatasets.rend();++it)datasets.push_back({it->number,it->label,it->visible});}
        std::array<AppliedCalibration,8> calibrations{};{std::lock_guard<std::mutex> lock(calibrationMutex);calibrations=appliedCalibrations;}
        std::array<float,8> values{};bool hasValues=false;{std::lock_guard<std::mutex> lock(dataMutex);if(!history.empty()){values=history.back().volts;hasValues=true;}}
        const auto channels=ProfileLegendChannels();
        HFONT allNoneFont=CreateFontW(-10,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        for(size_t col=0;col<channels.size();++col) {
            const size_t channel=channels[col];const int x=ProfileMatrixLeft+static_cast<int>(col)*ProfileColumnWidth;
            HPEN pen=CreatePen(PS_SOLID,3,colors[channel]);HGDIOBJ previous=SelectObject(dc,pen);MoveToEx(dc,x+19,42,nullptr);LineTo(dc,x+41,42);SelectObject(dc,previous);DeleteObject(pen);
            text(FullChannelName(channel),RECT{x+2,48,x+58,92},DT_CENTER|DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
            const double signal=ProfileSignalValue(values[channel],channel,calibrations);std::wstring value;
            if(profileGraphCalibrated && !std::isfinite(signal))value=L"no calib.";else if(!hasValues)value=L"—";else{wchar_t number[48]{};swprintf_s(number,L"%.4g",signal);value=number;}
            text(value,RECT{x+1,92,x+59,120});
            RECT channelCheck{x+20,125,x+40,145};DrawFrameControl(dc,&channelCheck,DFC_BUTTON,DFCS_BUTTONCHECK|(profileChannelVisible[channel]?DFCS_CHECKED:0));
            RECT all{x+2,155,x+58,185};DrawFrameControl(dc,&all,DFC_BUTTON,DFCS_BUTTONPUSH|(profileChannelVisible[channel]?0:DFCS_INACTIVE));
            SelectObject(dc,allNoneFont);SetTextColor(dc,profileChannelVisible[channel]?RGB(42,52,66):RGB(150,150,150));text(L"All / None",RECT{x+3,157,x+57,183});SetTextColor(dc,RGB(42,52,66));SelectObject(dc,font);
        }
        for(int row=0;row<ProfileVisibleRows;++row) {
            const size_t index=static_cast<size_t>(profileLegendFirst+row);if(index>=datasets.size())break;
            const int y=ProfileMatrixTop+row*ProfileRowHeight;
            Rectangle(dc,0,y,64,y+ProfileRowHeight);Rectangle(dc,64,y,ProfileMatrixLeft,y+ProfileRowHeight);
            if(datasets[index].number==activeProfileDatasetNumber.load() && profileRunning.load()){HBRUSH blink=CreateSolidBrush((GetTickCount64()/500)%2?RGB(210,210,210):RGB(255,255,255));RECT cell{1,y+1,63,y+29};FillRect(dc,&cell,blink);DeleteObject(blink);}
            text(std::to_wstring(datasets[index].number),RECT{2,y+1,62,y+29});text(datasets[index].label,RECT{68,y+1,176,y+29},DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            for(size_t col=0;col<channels.size();++col){const int x=ProfileMatrixLeft+static_cast<int>(col)*ProfileColumnWidth;Rectangle(dc,x,y,x+60,y+30);RECT check{x+20,y+5,x+40,y+25};DrawFrameControl(dc,&check,DFC_BUTTON,DFCS_BUTTONCHECK|(datasets[index].visible[channels[col]]?DFCS_CHECKED:0)|(profileChannelVisible[channels[col]]?0:DFCS_INACTIVE));
                if(GetFocus()==window && profileLegendFocusRow==row && profileLegendFocusColumn==static_cast<int>(col)){RECT focus{x+3,y+2,x+57,y+28};DrawFocusRect(dc,&focus);}}
        }
        if(datasets.empty())text(L"No profile datasets yet",RECT{0,ProfileMatrixTop,client.right-20,ProfileMatrixTop+30});
        BitBlt(screen,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);SelectObject(dc,oldFont);SelectObject(dc,oldBitmap);DeleteObject(font);DeleteObject(allNoneFont);DeleteObject(bitmap);DeleteObject(bg);DeleteDC(dc);EndPaint(window,&paint);return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(window);const int x=static_cast<short>(LOWORD(lParam)),y=static_cast<short>(HIWORD(lParam));
        if(y<36 && x<150){profileLegendFocusRow=-1;profileGraphCalibrated=!profileGraphCalibrated;WritePrivateProfileStringW(L"Tasker",L"ProfileGraphCalibrated",profileGraphCalibrated?L"1":L"0",configFilePath.c_str());ProfileLegendChanged(window);return 0;}
        const auto channels=ProfileLegendChannels();const int col=(x-ProfileMatrixLeft)/ProfileColumnWidth;if(x<ProfileMatrixLeft || col<0 || col>=static_cast<int>(channels.size()))return 0;
        if(y>=125 && y<145){profileChannelVisible[channels[col]]=!profileChannelVisible[channels[col]];ProfileLegendChanged(window);return 0;}
        if(!profileChannelVisible[channels[col]])return 0;
        {std::lock_guard<std::mutex> lock(profilePlotMutex);const size_t channel=channels[col];
            if(y>=155 && y<185){bool all=true;for(const auto& dataset:profileDatasets)all &= dataset.visible[channel];for(auto& dataset:profileDatasets)dataset.visible[channel]=!all;}
            else if(y>=ProfileMatrixTop && y<ProfileMatrixTop+ProfileVisibleRows*ProfileRowHeight){const int row=(y-ProfileMatrixTop)/ProfileRowHeight;const size_t reverse=static_cast<size_t>(profileLegendFirst+row);if(reverse<profileDatasets.size()){auto& dataset=profileDatasets[profileDatasets.size()-1-reverse];dataset.visible[channel]=!dataset.visible[channel];profileLegendFocusRow=row;profileLegendFocusColumn=col;}}}
        ProfileLegendChanged(window);return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(profileLegendScroll,SB_CTL,&info);int position=profileLegendFirst;
        switch(LOWORD(wParam)){case SB_LINEUP:--position;break;case SB_LINEDOWN:++position;break;case SB_PAGEUP:position-=ProfileVisibleRows;break;case SB_PAGEDOWN:position+=ProfileVisibleRows;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:position=info.nTrackPos;break;case SB_TOP:position=0;break;case SB_BOTTOM:position=info.nMax;break;}
        profileLegendFirst=(std::max)(0,(std::min)(position,info.nMax-ProfileVisibleRows+1));RefreshProfileLegend(GetParent(window));return 0;
    }
    case WM_SETFOCUS:case WM_KILLFOCUS:InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_KEYDOWN: {
        const int columns=static_cast<int>(ProfileLegendChannels().size());
        if(wParam==VK_DOWN)profileLegendFocusRow=(std::min)(ProfileVisibleRows-1,profileLegendFocusRow+1);else if(wParam==VK_UP)profileLegendFocusRow=(std::max)(-1,profileLegendFocusRow-1);
        else if(wParam==VK_LEFT)profileLegendFocusColumn=(std::max)(0,profileLegendFocusColumn-1);else if(wParam==VK_RIGHT)profileLegendFocusColumn=(std::max)(0,(std::min)(columns-1,profileLegendFocusColumn+1));
        else if(wParam==VK_SPACE){const int x=profileLegendFocusRow<0?10:ProfileMatrixLeft+profileLegendFocusColumn*ProfileColumnWidth+30;const int y=profileLegendFocusRow<0?10:ProfileMatrixTop+profileLegendFocusRow*ProfileRowHeight+15;SendMessageW(window,WM_LBUTTONDOWN,0,MAKELPARAM(x,y));}
        else return DefWindowProcW(window,message,wParam,lParam);InvalidateRect(window,nullptr,FALSE);return 0;
    }
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
void CreateProfileLegend(HWND window) {
    WNDCLASSW wc{};wc.lpfnWndProc=ProfileLegendProcedure;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"TaskerProfileLegend";RegisterClassW(&wc);
    profileGraphCalibrated=GetPrivateProfileIntW(L"Tasker",L"ProfileGraphCalibrated",0,configFilePath.c_str())!=0;
    profileLegendWindow=CreateWindowExW(0,wc.lpszClassName,L"Profile legend",WS_CHILD|WS_TABSTOP|WS_CLIPCHILDREN,0,0,680,344,window,nullptr,wc.hInstance,nullptr);
    profileLegendScroll=CreateWindowW(L"SCROLLBAR",L"",WS_CHILD|WS_VISIBLE|SBS_VERT,660,160,GetSystemMetrics(SM_CXVSCROLL),180,profileLegendWindow,nullptr,wc.hInstance,nullptr);RefreshProfileLegend(window);
}
