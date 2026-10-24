#include "mixercontroller.h"
#include <QRandomGenerator>
#include <QtGlobal>
#include <cmath>
#include <complex>
#include <QFileInfo>
#include <QDir>
#include <QMetaObject>
#include <chrono>
#include <thread>
#include <algorithm>
#if STORM_HAS_ALSA
#include <alsa/asoundlib.h>
#endif
MixerController::MixerController(QObject *parent) : QObject(parent) {
    const QStringList colors={"#2ed6ef","#43d6aa","#ffba58","#a896ff","#f178b6","#77bcff","#e2ce65","#70dfd4"};
    for(int i=0;i<8;++i){
        QVariantMap c{{"name",QString("CH %1").arg(i+1)},{"color",colors[i]},
          {"fader",0.0},{"gain",0.0},{"pan",0.0},{"mute",false},
          {"solo",false},{"pfl",false},{"meter",-90.0}};
        if(i==0)c["pan"]=-100.0; if(i==1)c["pan"]=100.0;
        const QString key=QString("channel%1/").arg(i);
        for(const QString &field : {"name","fader","gain","pan","mute","solo","pfl"})
            c[field]=m_settings.value(key+field,c[field]);
        m_channels.append(c);
        m_channelMeters.append(-90.0);m_channelGR.append(0.0);
    }
    for(int i=0;i<4;++i){
        QVariantMap a{{"name",QString("AUX %1").arg(i+1)},{"fader",0.0},
          {"mute",false},{"solo",false},{"pfl",false},{"meter",-90.0}};
        const QString key=QString("aux%1/").arg(i);
        for(const QString &field : {"fader","mute","solo","pfl"})
            a[field]=m_settings.value(key+field,a[field]);
        m_auxes.append(a);
        m_auxMeters.append(-90.0);m_auxGR.append(0.0);
    }
    m_master=m_settings.value("master",0.0).toDouble();
    connect(&m_audio,&AudioEngine::deviceChanged,this,&MixerController::audioDeviceChanged);
    connect(&m_audio,&AudioEngine::statsChanged,this,&MixerController::audioStatusChanged);
    connect(&m_audio,&AudioEngine::stateChanged,this,&MixerController::audioStatusChanged);
    connect(&m_audio,&AudioEngine::runningChanged,this,&MixerController::audioStatusChanged);
    refreshAudio();
    // Meter refresh never emits channelsChanged: channel controls remain stable.
    m_timer.setInterval(50);
    for(int i=0;i<192;i++){m_spectrum.append(-90.0);m_spectrumPeaks.append(-90.0);}
    for(int i=0;i<60;i++){m_rtaBands.append(-90.0);m_rtaPeaks.append(-90.0);}
    connect(&m_timer,&QTimer::timeout,this,[this]{
        if(m_audio.running()) {
            const auto values=m_audio.meters();
            const auto out=m_audio.outputMeters();
            const auto in=m_audio.inputMeters();
            const auto aux=m_audio.auxMeters();
            const auto chGr=m_audio.channelGR();
            const auto aGr=m_audio.auxGR();
            const auto mainGr=m_audio.mainGR();
            const auto limitGr=m_audio.limiterGR();
            bool changed=false;
            for(int i=0;i<m_channelMeters.size();++i){
                const double level=std::round(storm::dB(values[i])*2.0)/2.0;
                if(std::abs(m_channelMeters[i].toDouble()-level)>=0.5){
                    m_channelMeters[i]=level; changed=true;
                }
            }
            for(int i=0;i<4;i++){
                const double level=std::round(storm::dB(aux[i])*2.)/2.;
                if(std::abs(m_auxMeters[i].toDouble()-level)>=.5){m_auxMeters[i]=level;changed=true;}
                const double g=std::round(aGr[i]*10.)/10.;
                if(std::abs(m_auxGR[i].toDouble()-g)>=.1){m_auxGR[i]=g;changed=true;}
            }
            for(int i=0;i<8;i++){
                const double g=std::round(chGr[i]*10.)/10.;
                if(std::abs(m_channelGR[i].toDouble()-g)>=.1){m_channelGR[i]=g;changed=true;}
            }
            const double mg=std::max(mainGr[0],mainGr[1]),lg=std::max(limitGr[0],limitGr[1]);
            if(std::abs(m_mainGR-mg)>=.1||std::abs(m_limiterGR-lg)>=.1)changed=true;
            m_mainGR=mg;m_limiterGR=lg;
            const double outL=storm::dB(out[0]), outR=storm::dB(out[1]);
            const double inL=storm::dB(in[0]), inR=storm::dB(in[1]);
            if(std::abs(m_outputLevels[0]-outL)>=0.5 || std::abs(m_outputLevels[1]-outR)>=0.5 ||
               std::abs(m_inputLevels[0]-inL)>=0.5 || std::abs(m_inputLevels[1]-inR)>=0.5) changed=true;
            m_outputLevels[0]=outL; m_outputLevels[1]=outR;
            m_inputLevels[0]=inL; m_inputLevels[1]=inR;
            if(changed) emit metersChanged();
            return;
        }
        if(!m_demo) return;
        for(int i=0;i<m_channelMeters.size();++i)
            m_channelMeters[i]=m_channels[i].toMap().value("mute").toBool() ? -90.0 : -37.0+QRandomGenerator::global()->bounded(28.0);
        for(int i=0;i<m_auxMeters.size();++i)
            m_auxMeters[i]=m_auxes[i].toMap().value("mute").toBool() ? -90.0 : -35.0+QRandomGenerator::global()->bounded(24.0);
        emit metersChanged();
    });
    m_timer.start();
    m_fftThread=std::thread([this]{fftWorker();});
    std::fill(std::begin(m_midiLast),std::end(m_midiLast),-1);
    m_midiTimer.setInterval(30);
    connect(&m_midiTimer,&QTimer::timeout,this,&MixerController::pollBcf);
    connect(this,&MixerController::channelsChanged,this,[this]{m_midiDirty=true;});
    connect(this,&MixerController::auxesChanged,this,[this]{m_midiDirty=true;});
    connect(this,&MixerController::masterChanged,this,[this]{m_midiDirty=true;});
    connect(this,&MixerController::selectedChanged,this,[this]{m_midiDirty=true;});
    m_midiTimer.start();
}
void MixerController::setChannel(int index,const QString &key,const QVariant &value){
    if(index<0||index>=m_channels.size()) return;
    static const QStringList keys={"name","fader","gain","pan","mute","solo","pfl"};
    if(!keys.contains(key)) return;
    if(key=="pfl" && value.toBool()){
        for(int j=0;j<m_channels.size();j++)if(j!=index){
            auto other=m_channels[j].toMap();other["pfl"]=false;m_channels[j]=other;
            m_settings.setValue(QString("channel%1/pfl").arg(j),false);
        }
        for(int j=0;j<m_auxes.size();j++){
            auto other=m_auxes[j].toMap();other["pfl"]=false;m_auxes[j]=other;
            m_settings.setValue(QString("aux%1/pfl").arg(j),false);
        }
        emit auxesChanged();
    }
    auto c=m_channels[index].toMap(); c[key]=value; m_channels[index]=c;
    if(index%2==0 && linked(index/2) && key!="name" && key!="pan" && key!="pfl"){
        auto other=m_channels[index+1].toMap();other[key]=value;m_channels[index+1]=other;
    }else if(index%2==1 && linked(index/2) && key!="name" && key!="pan" && key!="pfl"){
        auto other=m_channels[index-1].toMap();other[key]=value;m_channels[index-1]=other;
    }
    m_settings.setValue(QString("channel%1/%2").arg(index).arg(key),value);
    if(key!="name" && key!="pan" && key!="pfl" && linked(index/2))m_settings.setValue(QString("channel%1/%2").arg(index^1).arg(key),value);
    emit channelsChanged(); if(key=="pfl")emit metersChanged(); refreshAudio();
}
void MixerController::setAux(int index,const QString &key,const QVariant &value){
    if(index<0||index>=m_auxes.size()) return;
    static const QStringList keys={"fader","mute","solo","pfl"};
    if(!keys.contains(key)) return;
    if(key=="pfl" && value.toBool()){
        for(int j=0;j<m_channels.size();j++){
            auto other=m_channels[j].toMap();other["pfl"]=false;m_channels[j]=other;
            m_settings.setValue(QString("channel%1/pfl").arg(j),false);
        }
        for(int j=0;j<m_auxes.size();j++)if(j!=index){
            auto other=m_auxes[j].toMap();other["pfl"]=false;m_auxes[j]=other;
            m_settings.setValue(QString("aux%1/pfl").arg(j),false);
        }
        emit channelsChanged();
    }
    auto c=m_auxes[index].toMap(); c[key]=value; m_auxes[index]=c;
    m_settings.setValue(QString("aux%1/%2").arg(index).arg(key),value);
    emit auxesChanged(); if(key=="pfl")emit metersChanged(); refreshAudio();
}
void MixerController::setSend(int channel,int aux,double level){
    if(channel<0||channel>=8||aux<0||aux>=4) return;
    m_settings.setValue(QString("send%1/%2").arg(channel).arg(aux),qBound(-90.0,level,10.0));
    if(linked(channel/2))m_settings.setValue(QString("send%1/%2").arg(channel^1).arg(aux),qBound(-90.0,level,10.0));
    notifyParameterChange(); refreshAudio();
}
double MixerController::send(int channel,int aux) const {
    if(channel<0||channel>=8||aux<0||aux>=4) return -60;
    return m_settings.value(QString("send%1/%2").arg(channel).arg(aux),-90.0).toDouble();
}
void MixerController::setMaster(double value){
    value=qBound(-60.0,value,10.0);
    if(qFuzzyCompare(m_master,value))return;
    m_master=value; m_settings.setValue("master",value); emit masterChanged(); refreshAudio();
}
void MixerController::setSelected(int value){
    if(value<0||value>7||value==m_selected)return;
    m_selected=value; m_audio.setAnalyzerSource(value);for(auto &v:m_spectrum)v=-90.0;for(auto &v:m_spectrumPeaks)v=-90.0;m_peakHoldFrames.fill(0);for(auto &v:m_rtaBands)v=-90.0;for(auto &v:m_rtaPeaks)v=-90.0;m_rtaHoldFrames.fill(0);emit spectrumChanged();emit selectedChanged();
}
void MixerController::setDemoMeters(bool value){
    if(value==m_demo)return;
    m_demo=value;
    if(!value)resetMeters();
    emit demoMetersChanged();
}
void MixerController::resetMeters(){
    for(int i=0;i<m_channelMeters.size();++i) m_channelMeters[i]=-90.0;
    for(int i=0;i<m_auxMeters.size();++i) m_auxMeters[i]=-90.0;
    emit metersChanged();
}
void MixerController::save(){m_settings.sync();}
QString MixerController::configFile() const {return m_settings.fileName();}
double MixerController::monitorMeter() const {
    for(int i=0;i<m_channels.size();++i)
        if(m_channels[i].toMap().value("pfl").toBool()) return m_channelMeters[i].toDouble();
    for(int i=0;i<m_auxes.size();++i)
        if(m_auxes[i].toMap().value("pfl").toBool()) return m_auxMeters[i].toDouble();
    return std::max(m_outputLevels[0],m_outputLevels[1]);
}
QString MixerController::pflLabel() const {
    for(int i=0;i<m_channels.size();++i)
        if(m_channels[i].toMap().value("pfl").toBool()) return QString("PFL CH%1").arg(i+1);
    for(int i=0;i<m_auxes.size();++i)
        if(m_auxes[i].toMap().value("pfl").toBool()) return QString("PFL AUX%1").arg(i+1);
    return {};
}
int MixerController::mainRoute(int outputIndex) const {
 if(outputIndex<0||outputIndex>1)return 0;
 return qBound(0,m_settings.value(QString("mainRoute/%1").arg(outputIndex),0).toInt(),8);
}
void MixerController::setMainRoute(int outputIndex,int source){
 if(outputIndex<0||outputIndex>1||source<0||source>8)return;
 m_settings.setValue(QString("mainRoute/%1").arg(outputIndex),source);
 notifyParameterChange();refreshAudio();
}
void MixerController::reloadShowState(){
 for(int i=0;i<8;i++){
  auto c=m_channels[i].toMap();
  for(const QString &field:{"name","fader","gain","pan","mute","solo","pfl"})
   c[field]=m_settings.value(QString("channel%1/%2").arg(i).arg(field),c[field]);
  m_channels[i]=c;
 }
 for(int i=0;i<4;i++){
  auto a=m_auxes[i].toMap();
  for(const QString &field:{"fader","mute","solo","pfl"})
   a[field]=m_settings.value(QString("aux%1/%2").arg(i).arg(field),a[field]);
  m_auxes[i]=a;
 }
 m_master=m_settings.value("master",0.).toDouble();
 emit channelsChanged();emit auxesChanged();emit masterChanged();
 notifyParameterChange();refreshAudio();
}
bool MixerController::exportShow(const QString &fileName){
 QString path=fileName.trimmed();
 if(path.isEmpty()){setShowMessage("Indica un nome file .conf");return false;}
 if(!path.endsWith(".conf",Qt::CaseInsensitive))path+=".conf";
 if(QFileInfo(path).isRelative())path=QDir::home().filePath("storm_shows/"+path);
 if(!QDir().mkpath(QFileInfo(path).absolutePath())){setShowMessage("Impossibile creare la cartella");return false;}
 m_settings.sync();
 QSettings destination(path,QSettings::IniFormat);
 destination.clear();
 for(const QString &key:m_settings.allKeys())destination.setValue(key,m_settings.value(key));
 destination.sync();
 const bool ok=destination.status()==QSettings::NoError;
 setShowMessage(ok?"SHOW salvato: "+path:"Errore salvataggio SHOW");return ok;
}
void MixerController::resetShow(){
 const auto input=m_settings.value("audio/inputDevice"),output=m_settings.value("audio/outputDevice"),device=m_settings.value("audio/device");
 m_settings.clear();
 m_settings.setValue("audio/inputDevice",input);m_settings.setValue("audio/outputDevice",output);m_settings.setValue("audio/device",device);
 const QStringList colors={"#2ed6ef","#43d6aa","#ffba58","#a896ff","#f178b6","#77bcff","#e2ce65","#70dfd4"};
 for(int i=0;i<8;i++){
  m_channels[i]=QVariantMap{{"name",QString("CH %1").arg(i+1)},{"color",colors[i]},
   {"fader",0.0},{"gain",0.0},{"pan",i==0?-100.0:i==1?100.0:0.0},
   {"mute",false},{"solo",false},{"pfl",false},{"meter",-90.0}};
 }
 for(int i=0;i<4;i++)m_auxes[i]=QVariantMap{{"name",QString("AUX %1").arg(i+1)},
   {"fader",0.0},{"mute",false},{"solo",false},{"pfl",false},{"meter",-90.0}};
 m_master=0.;m_settings.sync();
 emit channelsChanged();emit auxesChanged();emit masterChanged();notifyParameterChange();refreshAudio();
 setShowMessage("Parametri ripristinati (dispositivi ALSA invariati)");
}
bool MixerController::importShow(const QString &fileName){
 QString path=fileName.trimmed();
 if(path.isEmpty()){setShowMessage("Indica il file .conf");return false;}
 if(QFileInfo(path).isRelative())path=QDir::home().filePath("storm_shows/"+path);
 if(!QFileInfo::exists(path)){setShowMessage("File non trovato: "+path);return false;}
 if(QFileInfo(path).absoluteFilePath()==QFileInfo(m_settings.fileName()).absoluteFilePath()){
  setShowMessage("Il file attivo e gia caricato");return false;
 }
 QSettings source(path,QSettings::IniFormat);
 if(source.status()!=QSettings::NoError){setShowMessage("File SHOW non leggibile");return false;}
 // Preserve current ALSA device selections: changing them during playback is unsafe.
 const auto input=m_settings.value("audio/inputDevice"),output=m_settings.value("audio/outputDevice"),device=m_settings.value("audio/device");
 m_settings.clear();
 for(const QString &key:source.allKeys()){
  if(!key.startsWith("audio/"))m_settings.setValue(key,source.value(key));
 }
 m_settings.setValue("audio/inputDevice",input);m_settings.setValue("audio/outputDevice",output);m_settings.setValue("audio/device",device);
 m_settings.sync();reloadShowState();
 setShowMessage("SHOW caricato: "+path+" (audio I/O invariato)");return true;
}


void MixerController::setSelectedOutput(int i) {
    if(i<0||i>7||i==m_selectedOutput) return;
    m_selectedOutput=i; emit selectedOutputChanged();
}
double MixerController::eq(int ch,int band) const {
    if(ch<0||ch>=8||band<0||band>=8)return 0;
    return m_settings.value(QString("eq/%1/%2").arg(ch).arg(band),0.).toDouble();
}
void MixerController::setEq(int ch,int band,double v) {
    if(ch<0||ch>=8||band<0||band>=8)return;
    m_settings.setValue(QString("eq/%1/%2").arg(ch).arg(band),qBound(-18.,v,18.));
    if(linked(ch/2))m_settings.setValue(QString("eq/%1/%2").arg(ch^1).arg(band),qBound(-18.,v,18.));
    notifyParameterChange(); refreshAudio();
}
double MixerController::dynamics(int ch,int unit,const QString &key) const {
    if(ch<0||ch>=8||unit<0||unit>=3)return 0;
    const double fallback=key=="threshold"?(unit==0?-60:unit==1?-18:-1):key=="ratio"?(unit==0?2:unit==1?2:20):key=="attack"?(unit==0?5:unit==1?10:1):120;
    return m_settings.value(QString("dyn/%1/%2/%3").arg(ch).arg(unit).arg(key),fallback).toDouble();
}
void MixerController::setDynamics(int ch,int unit,const QString &key,double v) {
    if(ch<0||ch>=8||unit<0||unit>=3)return;
    if(key!="threshold"&&key!="ratio"&&key!="attack"&&key!="release")return;
    m_settings.setValue(QString("dyn/%1/%2/%3").arg(ch).arg(unit).arg(key),v);
    if(linked(ch/2))m_settings.setValue(QString("dyn/%1/%2/%3").arg(ch^1).arg(unit).arg(key),v);
    notifyParameterChange(); refreshAudio();
}
bool MixerController::dynamicsEnabled(int ch,int unit) const {
 return ch>=0&&ch<8&&unit>=0&&unit<3&&m_settings.value(QString("dyn/%1/%2/enabled").arg(ch).arg(unit),false).toBool();
}
void MixerController::setDynamicsEnabled(int ch,int unit,bool enabled){
 if(ch<0||ch>=8||unit<0||unit>=3)return;
 m_settings.setValue(QString("dyn/%1/%2/enabled").arg(ch).arg(unit),enabled);
 if(linked(ch/2))m_settings.setValue(QString("dyn/%1/%2/enabled").arg(ch^1).arg(unit),enabled);
 notifyParameterChange();refreshAudio();
}
bool MixerController::busDynamicsEnabled(int bus,int unit) const {
 return bus>=0&&bus<=4&&unit>=0&&unit<3&&m_settings.value(QString("busDyn/%1/%2/enabled").arg(bus).arg(unit),false).toBool();
}
void MixerController::setBusDynamicsEnabled(int bus,int unit,bool enabled){
 if(bus<0||bus>4||unit<0||unit>=3)return;
 m_settings.setValue(QString("busDyn/%1/%2/enabled").arg(bus).arg(unit),enabled);
 notifyParameterChange();refreshAudio();
}
bool MixerController::outputDynamicsEnabled(int out,int unit) const {
 return out>=0&&out<8&&unit>=0&&unit<3&&m_settings.value(QString("outDyn/%1/%2/enabled").arg(out).arg(unit),false).toBool();
}
void MixerController::setOutputDynamicsEnabled(int out,int unit,bool enabled){
 if(out<0||out>=8||unit<0||unit>=3)return;
 m_settings.setValue(QString("outDyn/%1/%2/enabled").arg(out).arg(unit),enabled);
 notifyParameterChange();refreshAudio();
}
void MixerController::setSpectrumSource(int source){
 if(source<0||source>14)return;
 m_audio.setAnalyzerSource(source);
 for(auto &v:m_spectrum)v=-90.0;for(auto &v:m_spectrumPeaks)v=-90.0;m_peakHoldFrames.fill(0);for(auto &v:m_rtaBands)v=-90.0;for(auto &v:m_rtaPeaks)v=-90.0;m_rtaHoldFrames.fill(0);
 emit spectrumChanged();
}
static QString outputKey(int out,const QString &kind,int idx){return QString("outDsp/%1/%2/%3").arg(out).arg(kind).arg(idx);}
double MixerController::outputEq(int out,int b) const {return out>=0&&out<8&&b>=0&&b<8?m_settings.value(outputKey(out,"eq",b),0.).toDouble():0;}
double MixerController::outputEqFreq(int out,int b) const {static const double f[]={65,150,350,800,1700,3600,7500,13000};return out>=0&&out<8&&b>=0&&b<8?m_settings.value(outputKey(out,"freq",b),f[b]).toDouble():1000;}
double MixerController::outputEqQ(int out,int b) const {return out>=0&&out<8&&b>=0&&b<8?m_settings.value(outputKey(out,"q",b),1.).toDouble():1;}
void MixerController::setOutputEq(int out,int b,double v){if(out<0||out>=8||b<0||b>=8)return;m_settings.setValue(outputKey(out,"eq",b),qBound(-18.,v,18.));notifyParameterChange();refreshAudio();}
void MixerController::setOutputEqFreq(int out,int b,double v){if(out<0||out>=8||b<0||b>=8)return;m_settings.setValue(outputKey(out,"freq",b),qBound(20.,v,20000.));notifyParameterChange();refreshAudio();}
void MixerController::setOutputEqQ(int out,int b,double v){if(out<0||out>=8||b<0||b>=8)return;m_settings.setValue(outputKey(out,"q",b),qBound(.2,v,12.));notifyParameterChange();refreshAudio();}
double MixerController::outputDynamics(int out,int unit,const QString &key) const {if(out<0||out>=8||unit<0||unit>=3)return 0;return m_settings.value(QString("outDyn/%1/%2/%3").arg(out).arg(unit).arg(key),key=="threshold"?-18:key=="ratio"?2:key=="attack"?10:120).toDouble();}
void MixerController::setOutputDynamics(int out,int unit,const QString &key,double v){if(out<0||out>=8||unit<0||unit>=3)return;if(key!="threshold"&&key!="ratio"&&key!="attack"&&key!="release")return;m_settings.setValue(QString("outDyn/%1/%2/%3").arg(out).arg(unit).arg(key),v);notifyParameterChange();refreshAudio();}
double MixerController::output(int out,int param) const {
    if(out<0||out>=8||param<0||param>=8)return 0;
    const double defaults[]={0,0,20,20000,-3,12,12,0};
    return m_settings.value(QString("out/%1/%2").arg(out).arg(param),defaults[param]).toDouble();
}
void MixerController::setOutput(int out,int param,double v) {
    if(out<0||out>=8||param<0||param>=8)return;
    m_settings.setValue(QString("out/%1/%2").arg(out).arg(param),v);notifyParameterChange(); refreshAudio();
}
double MixerController::fx(int unit,int param) const {
    if(unit<0||unit>=3||param<0||param>=3)return 0;
    return m_settings.value(QString("fx/%1/%2").arg(unit).arg(param),param==2?0.:25.).toDouble();
}
void MixerController::setFx(int unit,int param,double v) {
    if(unit<0||unit>=3||param<0||param>=3)return;
    m_settings.setValue(QString("fx/%1/%2").arg(unit).arg(param),qBound(0.,v,100.));notifyParameterChange(); refreshAudio();
}

static double defaultFreq(int band){static const double f[]={65,150,350,800,1700,3600,7500,13000};return f[band];}
bool MixerController::linked(int pair) const {return pair>=0&&pair<4&&m_settings.value(QString("link/%1").arg(pair),false).toBool();}
void MixerController::setLinked(int pair,bool value){
 if(pair<0||pair>=4)return;
 const bool wasLinked=linked(pair);
 if(wasLinked==value)return;
 const int a=pair*2;
 if(value){
  // Snapshot the right channel BEFORE mirroring it. Otherwise a mute/solo or
  // fader state copied during LINK may leave CH2 apparently dead after UNLINK.
  m_unlinkedRightState[pair]=m_channels[a+1].toMap();
  m_hasUnlinkedRightState[pair]=true;
  const auto left=m_channels[a].toMap();
  auto right=m_channels[a+1].toMap();
  for(const auto &key:{"fader","gain","mute","solo"}){
   right[key]=left.value(key);
   m_settings.setValue(QString("channel%1/%2").arg(a+1).arg(key),left.value(key));
  }
  // PFL remains independent of stereo link.
  m_channels[a+1]=right;
  for(int j=0;j<4;j++)m_settings.setValue(QString("send%1/%2").arg(a+1).arg(j),send(a,j));
  for(int band=0;band<8;band++)
   for(const QString &prefix:{QString("eq/"),QString("eqfreq/"),QString("eqq/")})
    m_settings.setValue(prefix+QString("%1/%2").arg(a+1).arg(band),
                        m_settings.value(prefix+QString("%1/%2").arg(a).arg(band)));
  for(int unit=0;unit<3;unit++)
   for(const QString &key:{"threshold","ratio","attack","release"})
    m_settings.setValue(QString("dyn/%1/%2/%3").arg(a+1).arg(unit).arg(key),dynamics(a,unit,key));
 } else {
  // Return the right channel to its own fader/gain/mute/solo state.
  if(m_hasUnlinkedRightState[pair]){
   auto right=m_channels[a+1].toMap();
   for(const auto &key:{"fader","gain","mute","solo"}){
    right[key]=m_unlinkedRightState[pair].value(key);
    m_settings.setValue(QString("channel%1/%2").arg(a+1).arg(key),right.value(key));
   }
   m_channels[a+1]=right;
   m_hasUnlinkedRightState[pair]=false;
  }
 }
 m_settings.setValue(QString("link/%1").arg(pair),value);
 emit channelsChanged();
 notifyParameterChange();
 refreshAudio();
}
double MixerController::eqFreq(int ch,int band) const {if(ch<0||ch>=8||band<0||band>=8)return 1000;return m_settings.value(QString("eqfreq/%1/%2").arg(ch).arg(band),defaultFreq(band)).toDouble();}
double MixerController::eqQ(int ch,int band) const {if(ch<0||ch>=8||band<0||band>=8)return 1;return m_settings.value(QString("eqq/%1/%2").arg(ch).arg(band),1.).toDouble();}
void MixerController::setEqFreq(int ch,int band,double v){if(ch<0||ch>=8||band<0||band>=8)return;v=qBound(20.,v,20000.);m_settings.setValue(QString("eqfreq/%1/%2").arg(ch).arg(band),v);if(linked(ch/2))m_settings.setValue(QString("eqfreq/%1/%2").arg(ch^1).arg(band),v);notifyParameterChange();refreshAudio();}
void MixerController::setEqQ(int ch,int band,double v){if(ch<0||ch>=8||band<0||band>=8)return;v=qBound(.2,v,12.);m_settings.setValue(QString("eqq/%1/%2").arg(ch).arg(band),v);if(linked(ch/2))m_settings.setValue(QString("eqq/%1/%2").arg(ch^1).arg(band),v);notifyParameterChange();refreshAudio();}
double MixerController::busEq(int bus,int band) const {if(bus<0||bus>4||band<0||band>=8)return 0;return m_settings.value(QString("busEq/%1/%2").arg(bus).arg(band),0.).toDouble();}
double MixerController::busEqFreq(int bus,int band) const {if(bus<0||bus>4||band<0||band>=8)return 1000;return m_settings.value(QString("busFreq/%1/%2").arg(bus).arg(band),defaultFreq(band)).toDouble();}
double MixerController::busEqQ(int bus,int band) const {if(bus<0||bus>4||band<0||band>=8)return 1;return m_settings.value(QString("busQ/%1/%2").arg(bus).arg(band),1.).toDouble();}
void MixerController::setBusEq(int bus,int band,double v){if(bus<0||bus>4||band<0||band>=8)return;m_settings.setValue(QString("busEq/%1/%2").arg(bus).arg(band),qBound(-18.,v,18.));notifyParameterChange();refreshAudio();}
void MixerController::setBusEqFreq(int bus,int band,double v){if(bus<0||bus>4||band<0||band>=8)return;m_settings.setValue(QString("busFreq/%1/%2").arg(bus).arg(band),qBound(20.,v,20000.));notifyParameterChange();refreshAudio();}
void MixerController::setBusEqQ(int bus,int band,double v){if(bus<0||bus>4||band<0||band>=8)return;m_settings.setValue(QString("busQ/%1/%2").arg(bus).arg(band),qBound(.2,v,12.));notifyParameterChange();refreshAudio();}
double MixerController::busDynamics(int bus,int unit,const QString &key) const {if(bus<0||bus>4||unit<0||unit>=3)return 0;
 const double fallback=key=="threshold"?(unit==0?-60:unit==1?-18:-1):key=="ratio"?(unit==0?2:unit==1?2:20):key=="attack"?(unit==0?5:unit==1?10:1):120;
 return m_settings.value(QString("busDyn/%1/%2/%3").arg(bus).arg(unit).arg(key),fallback).toDouble();}
void MixerController::setBusDynamics(int bus,int unit,const QString &key,double v){if(bus<0||bus>4||unit<0||unit>=3)return;if(key!="threshold"&&key!="ratio"&&key!="attack"&&key!="release")return;m_settings.setValue(QString("busDyn/%1/%2/%3").arg(bus).arg(unit).arg(key),v);notifyParameterChange();refreshAudio();}
void MixerController::refreshAudio(){
 storm::Params p;
 p.master=float(m_master);
 for(int i=0;i<2;i++)p.mainRouting[i]=mainRoute(i);
 for(int i=0;i<8;i++){
  auto v=m_channels[i].toMap();auto &c=p.ch[i];
  c.fader=float(v.value("fader").toDouble());c.gain=float(v.value("gain").toDouble());
  c.pan=float(v.value("pan").toDouble());c.mute=v.value("mute").toBool();c.solo=v.value("solo").toBool();c.pfl=v.value("pfl").toBool();
  for(int j=0;j<8;j++){c.eq[j]=float(eq(i,j));c.eqFreq[j]=float(eqFreq(i,j));c.eqQ[j]=float(eqQ(i,j));}
  for(int j=0;j<4;j++)c.sends[j]=float(send(i,j));
  for(int j=0;j<3;j++){
   c.dyn[j].enabled=dynamicsEnabled(i,j);
   c.dyn[j].threshold=float(dynamics(i,j,"threshold"));
   c.dyn[j].ratio=float(dynamics(i,j,"ratio"));
   c.dyn[j].attack=float(dynamics(i,j,"attack"));
   c.dyn[j].release=float(dynamics(i,j,"release"));
  }
 }
 for(int i=0;i<4;i++){
  auto a=m_auxes[i].toMap();p.aux[i].fader=float(a.value("fader").toDouble());p.aux[i].mute=a.value("mute").toBool();
  for(int j=0;j<8;j++){p.aux[i].eq[j]=float(busEq(i,j));p.aux[i].eqFreq[j]=float(busEqFreq(i,j));p.aux[i].eqQ[j]=float(busEqQ(i,j));}
  for(int u=0;u<3;u++){auto &d=p.aux[i].dyn[u];d.enabled=busDynamicsEnabled(i,u);d.threshold=float(busDynamics(i,u,"threshold"));d.ratio=float(busDynamics(i,u,"ratio"));d.attack=float(busDynamics(i,u,"attack"));d.release=float(busDynamics(i,u,"release"));}
 }
 for(int j=0;j<8;j++){p.mainEq[j]=float(busEq(4,j));p.mainFreq[j]=float(busEqFreq(4,j));p.mainQ[j]=float(busEqQ(4,j));}
 for(int u=0;u<3;u++){auto &d=p.mainDyn[u];d.enabled=busDynamicsEnabled(4,u);d.threshold=float(busDynamics(4,u,"threshold"));d.ratio=float(busDynamics(4,u,"ratio"));d.attack=float(busDynamics(4,u,"attack"));d.release=float(busDynamics(4,u,"release"));}
 for(int i=0;i<8;i++){
  p.out[i].trim=float(output(i,0));p.out[i].delayMs=float(output(i,1));
  p.out[i].hpf=float(output(i,2));p.out[i].lpf=float(output(i,3));p.out[i].limit=float(output(i,4));
  p.out[i].hpSlope=int(output(i,5));p.out[i].lpSlope=int(output(i,6));p.out[i].filterType=int(output(i,7));
  for(int j=0;j<8;j++){p.out[i].eq[j]=float(outputEq(i,j));p.out[i].eqFreq[j]=float(outputEqFreq(i,j));p.out[i].eqQ[j]=float(outputEqQ(i,j));}
  for(int u=0;u<3;u++){auto &d=p.out[i].dyn[u];d.enabled=outputDynamicsEnabled(i,u);d.threshold=float(outputDynamics(i,u,"threshold"));d.ratio=float(outputDynamics(i,u,"ratio"));d.attack=float(outputDynamics(i,u,"attack"));d.release=float(outputDynamics(i,u,"release"));}
 }
 for(int i=0;i<3;i++)for(int j=0;j<3;j++)p.fx[i][j]=float(fx(i,j));
 m_audio.update(p);
}

void MixerController::fftWorker(){
    constexpr int N=8192;
    constexpr double pi=3.14159265358979323846;
    std::array<float,N> hann{};
    for(int i=0;i<N;i++)hann[i]=float(.5-.5*std::cos(2*pi*i/(N-1)));
    unsigned lastSequence=0;
    while(!m_fftStop.load(std::memory_order_acquire)){
        if(m_audio.running()){
                constexpr int N=8192;
                constexpr double pi=3.14159265358979323846;
                std::array<float,N> samples{};
                unsigned sequence=0;
                if(m_audio.spectrumSamples(samples,sequence) && sequence!=lastSequence){
                lastSequence=sequence;

                std::array<std::complex<float>,N> bins{};
                for(int i=0;i<N;i++){
                    const float window=hann[size_t(i)];
                    bins[i]={samples[i]*window,0.f};
                }
                for(int i=1,j=0;i<N;i++){
                    int bit=N/2;
                    for(;j&bit;bit>>=1)j^=bit;
                    j^=bit;
                    if(i<j)std::swap(bins[i],bins[j]);
                }
                for(int len=2;len<=N;len<<=1){
                    const auto wlen=std::polar(1.f,float(-2*pi/len));
                    for(int i=0;i<N;i+=len){
                        std::complex<float> w(1,0);
                        for(int j=0;j<len/2;j++){
                            auto u=bins[i+j],v=bins[i+j+len/2]*w;
                            bins[i+j]=u+v;bins[i+j+len/2]=u-v;w*=wlen;
                        }
                    }
                }
                // Use the original FFT powers for both views, before any
                // log-frequency interpolation or temporal smoothing.
                const double fs=std::max(8000,m_audio.sampleRate());
                const double binHz=fs/N;
                std::array<double,N/2+1> power{};
                for(int j=1;j<=N/2;j++) {
                    // Hann coherent gain = 0.5; single-sided amplitude = 4*|X|/N.
                    const double a=4.*std::abs(bins[j])/N;
                    power[j]=a*a;
                }

                // Keep at most one spectrum update queued. This prevents the
                // analyzer from flooding Qt's event loop when rendering is slow.
                if(!m_fftUiPending.exchange(true,std::memory_order_acq_rel)){
                    QMetaObject::invokeMethod(this,[this,power,fs]{
                        applySpectrum(power,fs);
                        m_fftUiPending.store(false,std::memory_order_release);
                    },Qt::QueuedConnection);
                }
                }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}
void MixerController::applySpectrum(const std::array<double,4097> &power,double fs){
    constexpr int N=8192;
    const double binHz=fs/N;
    int strongestBin=1;
    for(int j=2;j<=N/2;j++)if(power[j]>power[strongestBin])strongestBin=j;
    m_dominantHz=strongestBin*binHz;
    m_dominantDb=10.*std::log10(std::max(1.e-15,power[strongestBin]));
                // No frequency-dependent display gate. In particular do not
                // hide real 20–90 Hz tones when high-frequency content is present.
                auto cleanLow=[](double, double db) {return db;};
                QVariantList levels;
                for(int k=0;k<192;k++) {
                    const double centerHz=20.*std::pow(1000.,(k+.5)/192.);
                    const double fractional=centerHz/binHz;
                    const int lo=std::clamp(int(std::floor(fractional)),1,N/2-1);
                    const int hi=std::min(lo+1,N/2);
                    const double t=std::clamp(fractional-lo,0.,1.);
                    // Interpolate LINEAR POWER, never dB; avoids shared max-bin
                    // plateaus at 20–50 Hz. No arbitrary +3dB/oct weighting.
                    const double p=(1.-t)*power[lo]+t*power[hi];
                    const double db=cleanLow(centerHz,10.*std::log10(std::max(1.e-15,p)));
                    const double previous=m_spectrum[k].toDouble();
                    const double next=db>=previous?db:std::max(db,previous-3.5);
                    levels.append(std::clamp(next,-90.,0.));
                    const double oldPeak=m_spectrumPeaks[k].toDouble();
                    if(db>=oldPeak){m_spectrumPeaks[k]=std::clamp(db,-90.,0.);m_peakHoldFrames[k]=7;}
                    else if(m_peakHoldFrames[k]>0)--m_peakHoldFrames[k];
                    else m_spectrumPeaks[k]=std::max(-90.,oldPeak-2.);
                }
                // 60 bands, 1/6 octave, computed directly from FFT bin power.
                // Integrate fractional overlap at band edges, avoiding the
                // empty-bin artifacts of narrow low-frequency bands.
                // Below ~40Hz the 8192-point FFT has limited resolution;
                // these readings are estimates, not IEC class measurements.
                for(int band=0;band<60;band++) {
                    const double center=20.*std::pow(2.,band/6.);
                    const double low=center/std::pow(2.,1./12.);
                    const double high=center*std::pow(2.,1./12.);
                    const int first=std::max(1,int(std::floor(low/binHz-0.5)));
                    const int last=std::min(N/2,int(std::ceil(high/binHz+0.5)));
                    double energy=0., covered=0.;
                    for(int j=first;j<=last;j++) {
                        const double binLow=(j-0.5)*binHz;
                        const double binHigh=(j+0.5)*binHz;
                        const double overlap=std::max(0.,std::min(high,binHigh)-std::max(low,binLow))/binHz;
                        energy+=power[j]*overlap;
                        covered+=overlap;
                    }
                    // For sub-bin-wide bands interpolate local spectral density.
                    // Never label these low-frequency estimates as calibrated.
                    if(covered<0.5) {
                        const double pos=std::clamp(center/binHz,1.,double(N/2-1));
                        const int j=int(pos);const double t=pos-j;
                        const double local=(1.-t)*power[j]+t*power[j+1];
                        energy=local*std::max(0.05,(high-low)/binHz);
                    }
                    const double db=cleanLow(center,10.*std::log10(std::max(1.e-15,energy/1.5)));
                    const double prev=m_rtaBands[band].toDouble();
                    m_rtaBands[band]=std::clamp(db>=prev?db:std::max(db,prev-3.5),-90.,0.);
                    const double held=m_rtaPeaks[band].toDouble();
                    if(db>=held){m_rtaPeaks[band]=std::clamp(db,-90.,0.);m_rtaHoldFrames[band]=7;}
                    else if(m_rtaHoldFrames[band]>0)--m_rtaHoldFrames[band];
                    else m_rtaPeaks[band]=std::max(-90.,held-2.);
                }
                m_spectrum=levels;
                emit spectrumChanged();
}
MixerController::~MixerController(){
#if STORM_HAS_ALSA
    if(m_midiSeq){snd_seq_close(static_cast<snd_seq_t*>(m_midiSeq));m_midiSeq=nullptr;}
#endif
    m_fftStop.store(true,std::memory_order_release);
    if(m_fftThread.joinable())m_fftThread.join();
}
