// WAVs stay in memory for the lifetime of asynchronous PlaySound playback.
constexpr unsigned TaskerAudioRate=44100;
std::vector<char> taskerPlayingSound;
ULONGLONG taskerCompletionSoundUntil=0;
unsigned ReadAudioLE(const std::vector<char>& bytes,size_t offset,size_t count) {
    unsigned value=0;for(size_t i=0;i<count;++i)value|=static_cast<unsigned>(static_cast<unsigned char>(bytes[offset+i]))<<(8*i);return value;
}
std::vector<char> EncodeTaskerWave(const std::vector<double>& samples) {
    std::vector<char> bytes(44+samples.size()*2,0);
    auto put=[&](size_t at,unsigned value,size_t count){for(size_t i=0;i<count;++i)bytes[at+i]=static_cast<char>((value>>(i*8))&255);};
    memcpy(bytes.data(),"RIFF",4);put(4,static_cast<unsigned>(bytes.size()-8),4);memcpy(bytes.data()+8,"WAVEfmt ",8);
    put(16,16,4);put(20,1,2);put(22,1,2);put(24,TaskerAudioRate,4);put(28,TaskerAudioRate*2,4);put(32,2,2);put(34,16,2);
    memcpy(bytes.data()+36,"data",4);put(40,static_cast<unsigned>(samples.size()*2),4);
    for(size_t i=0;i<samples.size();++i){const double value=(std::max)(-1.0,(std::min)(1.0,samples[i]));const short pcm=static_cast<short>(std::lround(value*32767));put(44+i*2,static_cast<unsigned short>(pcm),2);}
    return bytes;
}
std::vector<double> MakeFineBell(double frequency=1396.91) {
    std::vector<double> samples(static_cast<size_t>(TaskerAudioRate*0.85));
    constexpr double pi=3.14159265358979323846;
    for(size_t i=0;i<samples.size();++i){const double t=static_cast<double>(i)/TaskerAudioRate;
        const double attack=1-std::exp(-t/0.003);
        const double tone=std::sin(2*pi*frequency*t)*std::exp(-t/0.24)+0.27*std::sin(2*pi*frequency*2.756*t)*std::exp(-t/0.13)+0.10*std::sin(2*pi*frequency*4.07*t)*std::exp(-t/0.08);
        samples[i]=0.30*attack*tone*(1-static_cast<double>(i)/samples.size());}
    return samples;
}
std::vector<double> ReadExclamationAudio() {
    wchar_t configured[32768]{};DWORD size=sizeof(configured);
    RegGetValueW(HKEY_CURRENT_USER,L"AppEvents\\Schemes\\Apps\\.Default\\SystemExclamation\\.Current",nullptr,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ,nullptr,configured,&size);
    wchar_t windowsDirectory[MAX_PATH]{};GetWindowsDirectoryW(windowsDirectory,MAX_PATH);
    const auto media=std::filesystem::path(windowsDirectory)/L"Media";
    const std::filesystem::path paths[]={std::filesystem::path(configured),media/L"Windows Exclamation.wav",media/L"Windows Notify System Generic.wav"};
    for(const auto& path:paths) {
        std::ifstream file(path,std::ios::binary);if(!file)continue;
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        if(bytes.size()<44 || memcmp(bytes.data(),"RIFF",4)||memcmp(bytes.data()+8,"WAVE",4))continue;
        unsigned format=0,channels=0,rate=0,align=0,bits=0;size_t data=0,length=0;
        for(size_t at=12;at+8<=bytes.size();) {
            const size_t chunk=ReadAudioLE(bytes,at+4,4);if(chunk>bytes.size()-at-8)break;
            if(!memcmp(bytes.data()+at,"fmt ",4) && chunk>=16){format=ReadAudioLE(bytes,at+8,2);channels=ReadAudioLE(bytes,at+10,2);rate=ReadAudioLE(bytes,at+12,4);align=ReadAudioLE(bytes,at+20,2);bits=ReadAudioLE(bytes,at+22,2);
                if(format==0xfffe && chunk>=40)format=ReadAudioLE(bytes,at+32,2);}
            if(!memcmp(bytes.data()+at,"data",4)){data=at+8;length=chunk;}
            at+=8+chunk+(chunk&1);
        }
        if(!data || !channels || !rate || !align || !bits || (format!=1 && format!=3))continue;
        if((format==1 && bits!=8 && bits!=16 && bits!=24 && bits!=32)||(format==3 && bits!=32))continue;
        if(align<channels*(bits/8))continue;
        const size_t frames=length/align;if(frames<2)continue;
        auto sample=[&](size_t frame){double sum=0;for(unsigned channel=0;channel<channels;++channel){const size_t at=data+frame*align+channel*(bits/8);
            if(format==3){float value=0;memcpy(&value,bytes.data()+at,4);if(std::isfinite(value))sum+=value;}
            else if(bits==8)sum+=(static_cast<unsigned char>(bytes[at])-128)/128.0;
            else {const unsigned raw=ReadAudioLE(bytes,at,bits/8);const long long signedValue=(raw&(1u<<(bits-1)))?static_cast<long long>(raw)-(1LL<<bits):raw;sum+=static_cast<double>(signedValue)/static_cast<double>(1LL<<(bits-1));}}
            return sum/channels;};
        const size_t outputFrames=static_cast<size_t>(static_cast<double>(frames)*TaskerAudioRate/rate);
        std::vector<double> samples(outputFrames);double peak=0;
        for(size_t i=0;i<samples.size();++i){const double position=static_cast<double>(i)*rate/TaskerAudioRate;const size_t frame=(std::min)(frames-2,static_cast<size_t>(position));const double fraction=position-frame;
            samples[i]=sample(frame)*(1-fraction)+sample(frame+1)*fraction;peak=(std::max)(peak,std::abs(samples[i]));}
        if(peak<0.0001)continue;
        for(double& value:samples)value*=0.75/peak;
        return samples;
    }
    // Audible fallback when the Windows scheme is silent or lacks a usable WAV.
    auto samples=MakeFineBell(880.0);const auto second=MakeFineBell(1174.66);
    for(size_t i=0;i<samples.size();++i)samples[i]+=second[i];return samples;
}
bool PlayTaskerWave(const std::vector<char>& wave) {
    // Stop the prior buffer before replacing its storage.
    PlaySoundW(nullptr,nullptr,0);taskerPlayingSound=wave;
    const bool played=PlaySoundW(reinterpret_cast<LPCWSTR>(taskerPlayingSound.data()),nullptr,SND_MEMORY|SND_ASYNC|SND_NODEFAULT)!=FALSE;
    if(!played)WriteLog(L"Could not play notification audio; check the Windows output device and Tasker volume.");return played;
}
void PlayTaskerBell() {
    if(GetTickCount64()<taskerCompletionSoundUntil)return;
    static const auto bell=EncodeTaskerWave(MakeFineBell());PlayTaskerWave(bell);
}
void PlayTaskerCompletion(int count) {
    static const auto source=ReadExclamationAudio();
    const size_t spacing=TaskerAudioRate*3/10;
    std::vector<double> mix(source.size()+spacing*static_cast<size_t>(count-1),0.0);
    for(int cue=0;cue<count;++cue)for(size_t i=0;i<source.size();++i)mix[i+cue*spacing]+=source[i];
    double peak=0;for(double value:mix)peak=(std::max)(peak,std::abs(value));if(peak>0.85)for(double& value:mix)value*=0.85/peak;
    PlayTaskerWave(EncodeTaskerWave(mix));
    taskerCompletionSoundUntil=GetTickCount64()+static_cast<ULONGLONG>(mix.size()*1000/TaskerAudioRate)+50;
}
