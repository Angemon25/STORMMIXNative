#include "audioengine.h"
#include <QMetaObject>
#include <QDebug>
#include <QThread>
#include <vector>
#include <QSettings>
#include <QFile>
#include <QRegularExpression>
#include <chrono>
#include <algorithm>
#include <cmath>
#ifndef STORM_HAS_ALSA
#define STORM_HAS_ALSA 0
#endif
#if STORM_HAS_ALSA
#include <alsa/asoundlib.h>
#endif
AudioEngine::AudioEngine(QObject *p):QObject(p){
 m_device=QSettings().value("audio/device",qEnvironmentVariable("STORM_ALSA_DEVICE","default")).toString();
 m_inputDevice=QSettings().value("audio/inputDevice",m_device).toString();
 m_outputDevice=QSettings().value("audio/outputDevice",m_device).toString();
 for(auto &x:m_meters)x.store(0);for(auto &x:m_outputs)x.store(0);for(auto &x:m_inputs)x.store(0);for(auto &x:m_auxMeters)x.store(0);for(auto &x:m_auxGR)x.store(0);for(auto &x:m_channelGR)x.store(0);for(auto &x:m_mainGR)x.store(0);for(auto &x:m_limiterGR)x.store(0);
 std::atomic_store(&m_params,std::make_shared<const storm::Params>());
}
AudioEngine::~AudioEngine(){stop();}
void AudioEngine::setState(const QString &s){
 if(QThread::currentThread()!=thread()){
  QMetaObject::invokeMethod(this,[this,s]{setState(s);},Qt::QueuedConnection);return;
 }
 if(m_state!=s){m_state=s;emit stateChanged();}
}
void AudioEngine::update(const storm::Params &p){
 std::atomic_store(&m_params,std::make_shared<const storm::Params>(p));
}
std::array<float,8> AudioEngine::meters() const {std::array<float,8> v{};for(int i=0;i<8;i++)v[i]=m_meters[i].load();return v;}
std::array<float,2> AudioEngine::outputMeters() const {std::array<float,2> v{};for(int i=0;i<2;i++)v[i]=m_outputs[i].load();return v;}
std::array<float,2> AudioEngine::inputMeters() const {
 std::array<float,2> v{};
 for(int i=0;i<2;i++)v[i]=m_inputs[i].load();
 return v;
}
std::array<float,4> AudioEngine::auxMeters() const {std::array<float,4> a{};for(int i=0;i<4;i++)a[i]=m_auxMeters[i].load();return a;}
std::array<float,8> AudioEngine::channelGR() const {std::array<float,8> a{};for(int i=0;i<8;i++)a[i]=m_channelGR[i].load();return a;}
std::array<float,4> AudioEngine::auxGR() const {std::array<float,4> a{};for(int i=0;i<4;i++)a[i]=m_auxGR[i].load();return a;}
std::array<float,2> AudioEngine::mainGR() const {std::array<float,2> a{};for(int i=0;i<2;i++)a[i]=m_mainGR[i].load();return a;}
std::array<float,2> AudioEngine::limiterGR() const {std::array<float,2> a{};for(int i=0;i<2;i++)a[i]=m_limiterGR[i].load();return a;}
bool AudioEngine::spectrumSamples(std::array<float,8192> &result,unsigned &sequence) const {
 const int slot=m_publishedSlot.load(std::memory_order_acquire);
 if(slot<0)return false;
 auto &frame=m_spectrumSlots[size_t(slot)];
 int ready=2;
 if(!frame.state.compare_exchange_strong(ready,3,std::memory_order_acq_rel))return false;
 result=frame.samples; // The producer cannot overwrite a reader-owned slot.
 sequence=m_publishedSequence.load(std::memory_order_acquire);
 frame.state.store(2,std::memory_order_release);
 return true;
}

void AudioEngine::setDevice(const QString &v){
 if(v.isEmpty()||m_running.load()||v==m_device)return;
 m_device=v;QSettings().setValue("audio/device",v);
 setInputDevice(v);setOutputDevice(v);emit deviceChanged();
}
void AudioEngine::setInputDevice(const QString &v){
 if(v.trimmed().isEmpty()||m_running.load()||v==m_inputDevice)return;
 m_inputDevice=v.trimmed();QSettings().setValue("audio/inputDevice",m_inputDevice);emit deviceChanged();
}
void AudioEngine::setOutputDevice(const QString &v){
 if(v.trimmed().isEmpty()||m_running.load()||v==m_outputDevice)return;
 m_outputDevice=v.trimmed();QSettings().setValue("audio/outputDevice",m_outputDevice);emit deviceChanged();
}
QVariantList AudioEngine::devices() const {
 QVariantList result;
 auto add=[&result](const QString &name,const QString &description){
  for(const auto &item:result)if(item.toMap().value("name").toString()==name)return;
  result.append(QVariantMap{{"name",name},{"description",description}});
 };
 add("default","ALSA default");
#if STORM_HAS_ALSA
 void **hints=nullptr;
 if(snd_device_name_hint(-1,"pcm",&hints)==0&&hints){
  for(void **it=hints;*it;++it){
   char *n=snd_device_name_get_hint(*it,"NAME");
   char *d=snd_device_name_get_hint(*it,"DESC");
   char *io=snd_device_name_get_hint(*it,"IOID");
   if(n&&(!io||QString::fromUtf8(io)!="Output"))add(QString::fromUtf8(n),d?QString::fromUtf8(d).replace('\n'," · ").left(100):QString::fromUtf8(n));
   if(n)free(n);if(d)free(d);if(io)free(io);
  }
  snd_device_name_free_hint(hints);
 }
 int card=-1;
 if(snd_card_next(&card)==0)while(card>=0){
  snd_ctl_t *ctl=nullptr;
  if(snd_ctl_open(&ctl,QString("hw:%1").arg(card).toUtf8().constData(),0)>=0){
   int dev=-1;
   while(snd_ctl_pcm_next_device(ctl,&dev)>=0&&dev>=0)
    add(QString("plughw:%1,%2").arg(card).arg(dev),QString("Scheda %1 · device %2").arg(card).arg(dev));
   snd_ctl_close(ctl);
  }
  if(snd_card_next(&card)<0)break;
 }
 // Some virtualized ALSA installations enumerate only default/null/pipewire
 // through the name-hint and control APIs. /proc/asound/pcm is a useful
 // fallback for USB devices already registered by snd-usb-audio.
 QFile pcmFile("/proc/asound/pcm");
 if(pcmFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
  const QString pcmText=QString::fromUtf8(pcmFile.readAll());
  const QRegularExpression pcmLine(R"(^(\d+)-(\d+):\s*(.+)$)", QRegularExpression::MultilineOption);
  auto it=pcmLine.globalMatch(pcmText);
  while(it.hasNext()) {
   const auto match=it.next();
   const QString cardNo=QString::number(match.captured(1).toInt());
   const QString deviceNo=QString::number(match.captured(2).toInt());
   const QString description=match.captured(3).trimmed();
   if(description.contains("playback") && description.contains("capture"))
    add(QString("plughw:%1,%2").arg(cardNo,deviceNo),description.left(110));
  }
 }
#endif
 // Show the persisted choice even if a USB device has been disconnected.
 add(m_device,QString("Dispositivo precedente: %1").arg(m_device));
 add(m_inputDevice,QString("Ingresso selezionato: %1").arg(m_inputDevice));
 add(m_outputDevice,QString("Uscita selezionata: %1").arg(m_outputDevice));
 return result;
}
void AudioEngine::restart(){stop();start();}
void AudioEngine::start(){
#if STORM_HAS_ALSA
 if(m_running.load())return;
 if(m_thread.joinable())m_thread.join();
 m_xruns=0;m_sampleRate=0;m_blockFrames=0;emit statsChanged();
 m_stop=false;m_running=true;emit runningChanged();setState("OPENING ALSA 2x2...");
 m_thread=std::thread([this]{worker();});
#else
 setState("ALSA NOT AVAILABLE: install libasound2-dev and rebuild");
#endif
}
void AudioEngine::stop(){
 m_stop=true;
 if(m_thread.joinable())m_thread.join();
 if(m_running.exchange(false))emit runningChanged();
 setState("AUDIO STOPPED");
}
void AudioEngine::worker(){
#if STORM_HAS_ALSA
 snd_pcm_t *cap=nullptr,*play=nullptr;
 const QByteArray inputDev=m_inputDevice.toUtf8();
 const QByteArray outputDev=m_outputDevice.toUtf8();
 auto fail=[this](const QString &err){
  m_running=false;
  QMetaObject::invokeMethod(this,[this,err]{emit runningChanged();setState(err);},Qt::QueuedConnection);
 };
 int err=snd_pcm_open(&cap,inputDev.constData(),SND_PCM_STREAM_CAPTURE,SND_PCM_NONBLOCK);
 if(err<0){fail(QString("ALSA CAPTURE [%1]: ").arg(QString::fromUtf8(inputDev))+snd_strerror(err));return;}
 err=snd_pcm_open(&play,outputDev.constData(),SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK);
 if(err<0){snd_pcm_close(cap);fail(QString("ALSA PLAYBACK [%1]: ").arg(QString::fromUtf8(outputDev))+snd_strerror(err));return;}
 // Negotiate the actual PCM format independently for capture and playback.
 // UMC202HD hw capture advertises S32_LE only; ALSA plug devices may
 // support S16_LE as well. Never interpret a 32-bit frame as int16_t.
 auto setup=[](snd_pcm_t *pcm,snd_pcm_format_t &format){
  int result=snd_pcm_set_params(pcm,SND_PCM_FORMAT_S32_LE,
      SND_PCM_ACCESS_RW_INTERLEAVED,2,48000,1,40000);
  if(result>=0){format=SND_PCM_FORMAT_S32_LE;return result;}
  result=snd_pcm_set_params(pcm,SND_PCM_FORMAT_S16_LE,
      SND_PCM_ACCESS_RW_INTERLEAVED,2,48000,1,40000);
  if(result>=0)format=SND_PCM_FORMAT_S16_LE;
  return result;
 };
 snd_pcm_format_t capFormat=SND_PCM_FORMAT_UNKNOWN,playFormat=SND_PCM_FORMAT_UNKNOWN;
 err=setup(cap,capFormat);if(err>=0)err=setup(play,playFormat);
 if(err<0){snd_pcm_close(cap);snd_pcm_close(play);fail(QString("ALSA FORMAT 48k/2ch: ")+snd_strerror(err));return;}
 m_sampleRate=48000;m_blockFrames=256;
 // A nonblocking capture stream may remain PREPARED forever if the application
 // only calls readi() and waits for POLLIN. Start capture explicitly.
 err=snd_pcm_start(cap);
 if(err<0){
  const QString msg=QString("ALSA CAPTURE START [%1]: %2").arg(QString::fromUtf8(inputDev),QString::fromUtf8(snd_strerror(err)));
  snd_pcm_close(cap);snd_pcm_close(play);fail(msg);return;
 }
 QMetaObject::invokeMethod(this,[this,inputDev,outputDev]{emit statsChanged();setState(QString("WAITING FOR INPUT · 48 kHz · IN: %1 · OUT: %2").arg(QString::fromUtf8(inputDev),QString::fromUtf8(outputDev)));},Qt::QueuedConnection);
 bool streaming=false;
 int idleWaits=0;
 constexpr int BLOCK=256;
 std::vector<int16_t> pcmIn16(BLOCK*2),pcmOut16(BLOCK*2);
 std::vector<int32_t> pcmIn32(BLOCK*2),pcmOut32(BLOCK*2);
 std::vector<float> input(BLOCK*2),output(BLOCK*2),analysis(BLOCK);
 storm::Processor dsp(48000);
 std::shared_ptr<const storm::Params> previous;
 while(!m_stop.load()){
  snd_pcm_sframes_t got=snd_pcm_readi(cap,capFormat==SND_PCM_FORMAT_S32_LE ? static_cast<void*>(pcmIn32.data()) : static_cast<void*>(pcmIn16.data()),BLOCK);
  if(got==-EAGAIN){
   snd_pcm_wait(cap,20);
   if(++idleWaits==150){
    qWarning() << "STORM MIX: no capture frames after ~3 seconds; check loopback producer, matching subdevice, rate and channels";
   }
   continue;
  }
  if(got<0){
   m_xruns++;
   const int recovered=snd_pcm_recover(cap,int(got),1);
   if(recovered>=0 && snd_pcm_state(cap)==SND_PCM_STATE_PREPARED) snd_pcm_start(cap);
   QMetaObject::invokeMethod(this,[this]{emit statsChanged();},Qt::QueuedConnection);
   continue;
  }
  if(got==0){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
  idleWaits=0;
  if(!streaming){
   streaming=true;
   QMetaObject::invokeMethod(this,[this,inputDev,outputDev]{setState(QString("RUNNING · 48 kHz · IN: %1 · OUT: %2").arg(QString::fromUtf8(inputDev),QString::fromUtf8(outputDev)));},Qt::QueuedConnection);
  }
  float inputPeaks[2]={0,0};
  for(int i=0;i<got*2;i++){
   input[i]=capFormat==SND_PCM_FORMAT_S32_LE ? float(double(pcmIn32[i])/2147483648.0) : float(pcmIn16[i])/32768.f;
   inputPeaks[i%2]=std::max(inputPeaks[i%2],std::abs(input[i]));
  }
  for(int i=0;i<2;i++)m_inputs[i].store(std::max(inputPeaks[i],m_inputs[i].load()*0.9f));
  auto p=std::atomic_load(&m_params);
  if(p!=previous){dsp.configure(*p);previous=p;}
  dsp.process(input.data(),output.data(),int(got),analysis.data(),m_analyzerSource.load());
  // Publish contiguous, immutable post-DSP 8192-frame windows. Never
  // reassemble from a ring while another thread writes into it.
  const int currentSource=m_analyzerSource.load(std::memory_order_acquire);
  if(currentSource!=m_lastAnalyzerSource){m_lastAnalyzerSource=currentSource;m_assembleCount=0;}
  for(snd_pcm_sframes_t i=0;i<got;i++){
   m_assemble[m_assembleCount++]=analysis[size_t(i)];
   if(m_assembleCount==m_assemble.size()){
    const int published=m_publishedSlot.load(std::memory_order_acquire);
    for(int n=0;n<3;n++){
     if(n==published)continue;
     int state=m_spectrumSlots[size_t(n)].state.load(std::memory_order_acquire);
     if(state==3||state==1)continue;
     if(!m_spectrumSlots[size_t(n)].state.compare_exchange_strong(state,1,std::memory_order_acq_rel))continue;
     m_spectrumSlots[size_t(n)].samples=m_assemble;
     m_spectrumSlots[size_t(n)].state.store(2,std::memory_order_release);
     m_publishedSlot.store(n,std::memory_order_release);
     m_publishedSequence.fetch_add(1,std::memory_order_release);
     break;
    }
    // 75% overlap: keep 6144 samples and publish every 2048 new
    // samples (~23.4 spectra/s at 48 kHz), not once per 8192.
    std::move(m_assemble.begin()+2048,m_assemble.end(),m_assemble.begin());
    m_assembleCount=6144;
   }
  }
  for(int i=0;i<got*2;i++){
   const double value=std::clamp(double(output[i]),-1.0,1.0-1.0/2147483648.0);
   if(playFormat==SND_PCM_FORMAT_S32_LE)
    pcmOut32[i]=int32_t(std::llround(value*2147483647.0));
   else pcmOut16[i]=int16_t(std::lrint(std::clamp(value,-1.0,0.999969)*32767.0));
  }
  snd_pcm_sframes_t sent=0;
  while(sent<got&&!m_stop.load()){
   auto n=snd_pcm_writei(play,playFormat==SND_PCM_FORMAT_S32_LE ? static_cast<void*>(pcmOut32.data()+sent*2) : static_cast<void*>(pcmOut16.data()+sent*2),got-sent);
   if(n==-EAGAIN){snd_pcm_wait(play,20);continue;}
   if(n<0){m_xruns++;snd_pcm_recover(play,int(n),1);QMetaObject::invokeMethod(this,[this]{emit statsChanged();},Qt::QueuedConnection);break;}
   if(n==0){snd_pcm_wait(play,20);continue;}sent+=n;
  }
  for(int i=0;i<8;i++)m_meters[i].store(dsp.meter[i]);
  for(int i=0;i<2;i++){m_outputs[i].store(dsp.outMeter[i]);m_mainGR[i].store(dsp.mainGr[i]);m_limiterGR[i].store(dsp.limitGr[i]);}
  for(int i=0;i<8;i++)m_channelGR[i].store(dsp.gr[i]);
  for(int i=0;i<4;i++){m_auxMeters[i].store(dsp.auxMeter[i]);m_auxGR[i].store(dsp.auxGr[i]);}
 }
 snd_pcm_drop(cap);snd_pcm_drop(play);snd_pcm_close(cap);snd_pcm_close(play);
 m_running=false;
 QMetaObject::invokeMethod(this,[this]{emit runningChanged();setState("AUDIO STOPPED");},Qt::QueuedConnection);
#endif
}
