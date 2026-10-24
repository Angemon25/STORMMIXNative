#pragma once
#include <thread>
#include <algorithm>
#include <iterator>
#include <atomic>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QSettings>
#include <QTimer>
#include "audioengine.h"
class MixerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString midiStatus READ midiStatus NOTIFY midiStatusChanged)
    Q_PROPERTY(int midiBank READ midiBank NOTIFY midiBankChanged)
    Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)
    Q_PROPERTY(QVariantList auxes READ auxes NOTIFY auxesChanged)
    Q_PROPERTY(QVariantList channelMeters READ channelMeters NOTIFY metersChanged)
    Q_PROPERTY(QVariantList auxMeters READ auxMeters NOTIFY metersChanged)
    Q_PROPERTY(QVariantList channelGR READ channelGR NOTIFY metersChanged)
    Q_PROPERTY(QVariantList auxGR READ auxGR NOTIFY metersChanged)
    Q_PROPERTY(QVariantList spectrum READ spectrum NOTIFY spectrumChanged)
    Q_PROPERTY(double dominantHz READ dominantHz NOTIFY spectrumChanged)
    Q_PROPERTY(double dominantDb READ dominantDb NOTIFY spectrumChanged)
    Q_PROPERTY(QVariantList spectrumPeaks READ spectrumPeaks NOTIFY spectrumChanged)
    Q_PROPERTY(QVariantList rtaBands READ rtaBands NOTIFY spectrumChanged)
    Q_PROPERTY(QVariantList rtaPeaks READ rtaPeaks NOTIFY spectrumChanged)
    Q_PROPERTY(QString showMessage READ showMessage NOTIFY showMessageChanged)
    Q_PROPERTY(double mainGR READ mainGR NOTIFY metersChanged)
    Q_PROPERTY(double limiterGR READ limiterGR NOTIFY metersChanged)
    Q_PROPERTY(int parameterRevision READ parameterRevision NOTIFY parameterRevisionChanged)
    Q_PROPERTY(double master READ master WRITE setMaster NOTIFY masterChanged)
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY selectedChanged)
    Q_PROPERTY(int selectedOutput READ selectedOutput WRITE setSelectedOutput NOTIFY selectedOutputChanged)
    Q_PROPERTY(bool demoMeters READ demoMeters WRITE setDemoMeters NOTIFY demoMetersChanged)
    Q_PROPERTY(QString status READ status NOTIFY audioStatusChanged)
    Q_PROPERTY(bool audioRunning READ audioRunning NOTIFY audioStatusChanged)
    Q_PROPERTY(QString audioDevice READ audioDevice WRITE setAudioDevice NOTIFY audioDeviceChanged)
    Q_PROPERTY(QString audioInputDevice READ audioInputDevice WRITE setAudioInputDevice NOTIFY audioDeviceChanged)
    Q_PROPERTY(QString audioOutputDevice READ audioOutputDevice WRITE setAudioOutputDevice NOTIFY audioDeviceChanged)
    Q_PROPERTY(int audioXruns READ audioXruns NOTIFY audioStatusChanged)
    Q_PROPERTY(int audioSampleRate READ audioSampleRate NOTIFY audioStatusChanged)
    Q_PROPERTY(int audioBlockFrames READ audioBlockFrames NOTIFY audioStatusChanged)
    Q_PROPERTY(double inMeterL READ inMeterL NOTIFY metersChanged)
    Q_PROPERTY(double inMeterR READ inMeterR NOTIFY metersChanged)
    Q_PROPERTY(double outMeterL READ outMeterL NOTIFY metersChanged)
    Q_PROPERTY(double outMeterR READ outMeterR NOTIFY metersChanged)
    Q_PROPERTY(double monitorMeter READ monitorMeter NOTIFY metersChanged)
    Q_PROPERTY(QString pflLabel READ pflLabel NOTIFY metersChanged)
public:
    ~MixerController() override;
    explicit MixerController(QObject *parent=nullptr);
    QString midiStatus() const {return m_midiStatus;}
    int midiBank() const {return m_midiBank;}
    QVariantList channels() const { return m_channels; }
    QVariantList auxes() const { return m_auxes; }
    QVariantList channelMeters() const { return m_channelMeters; }
    QVariantList auxMeters() const { return m_auxMeters; }
    QVariantList channelGR() const {return m_channelGR;}
    QVariantList auxGR() const {return m_auxGR;}
    QVariantList spectrum() const {return m_spectrum;}
    double dominantHz() const {return m_dominantHz;}
    double dominantDb() const {return m_dominantDb;}
    QVariantList spectrumPeaks() const {return m_spectrumPeaks;}
    QVariantList rtaBands() const {return m_rtaBands;}
    QVariantList rtaPeaks() const {return m_rtaPeaks;}
    QString showMessage() const {return m_showMessage;}
    double mainGR() const {return m_mainGR;}
    double limiterGR() const {return m_limiterGR;}
    int parameterRevision() const { return m_parameterRevision; }
    double master() const { return m_master; }
    int selected() const { return m_selected; }
    int selectedOutput() const {return m_selectedOutput;}
    void setSelectedOutput(int i);
    bool demoMeters() const { return m_demo; }
    QString status() const { return m_audio.state(); }
    bool audioRunning() const {return m_audio.running();}
    double inMeterL() const {return m_inputLevels[0];}
    double inMeterR() const {return m_inputLevels[1];}
    double outMeterL() const {return m_outputLevels[0];}
    double outMeterR() const {return m_outputLevels[1];}
    double monitorMeter() const;
    QString pflLabel() const;
    QString audioDevice() const {return m_audio.device();}
    void setAudioDevice(const QString &v) {m_audio.setDevice(v);}
    QString audioInputDevice() const {return m_audio.inputDevice();}
    QString audioOutputDevice() const {return m_audio.outputDevice();}
    void setAudioInputDevice(const QString &v) {m_audio.setInputDevice(v);}
    void setAudioOutputDevice(const QString &v) {m_audio.setOutputDevice(v);}
    int audioXruns() const {return m_audio.xruns();}
    int audioSampleRate() const {return m_audio.sampleRate();}
    int audioBlockFrames() const {return m_audio.blockFrames();}
    Q_INVOKABLE QVariantList audioDevices() const {return m_audio.devices();}
    Q_INVOKABLE void restartAudio() {m_audio.restart();}
    Q_INVOKABLE void startAudio() {m_audio.start();}
    Q_INVOKABLE void stopAudio() {m_audio.stop();}
    Q_INVOKABLE void setChannel(int index, const QString &key, const QVariant &value);
    Q_INVOKABLE void setAux(int index, const QString &key, const QVariant &value);
    Q_INVOKABLE void setSend(int channel, int aux, double level);
    Q_INVOKABLE double send(int channel, int aux) const;
    Q_INVOKABLE double eq(int ch,int band) const;
    Q_INVOKABLE void setEq(int ch,int band,double value);
    Q_INVOKABLE double eqFreq(int ch,int band) const;
    Q_INVOKABLE double eqQ(int ch,int band) const;
    Q_INVOKABLE void setEqFreq(int ch,int band,double value);
    Q_INVOKABLE void setEqQ(int ch,int band,double value);
    Q_INVOKABLE bool linked(int pair) const;
    Q_INVOKABLE void setLinked(int pair,bool value);
    Q_INVOKABLE double busEq(int bus,int band) const;
    Q_INVOKABLE double busEqFreq(int bus,int band) const;
    Q_INVOKABLE double busEqQ(int bus,int band) const;
    Q_INVOKABLE void setBusEq(int bus,int band,double v);
    Q_INVOKABLE void setBusEqFreq(int bus,int band,double v);
    Q_INVOKABLE void setBusEqQ(int bus,int band,double v);
    Q_INVOKABLE double busDynamics(int bus,int unit,const QString &key) const;
    Q_INVOKABLE void setBusDynamics(int bus,int unit,const QString &key,double v);
    Q_INVOKABLE double dynamics(int ch,int unit,const QString &key) const;
    Q_INVOKABLE void setDynamics(int ch,int unit,const QString &key,double value);
    Q_INVOKABLE bool dynamicsEnabled(int ch,int unit) const;
    Q_INVOKABLE void setDynamicsEnabled(int ch,int unit,bool enabled);
    Q_INVOKABLE bool busDynamicsEnabled(int bus,int unit) const;
    Q_INVOKABLE void setBusDynamicsEnabled(int bus,int unit,bool enabled);
    Q_INVOKABLE bool outputDynamicsEnabled(int out,int unit) const;
    Q_INVOKABLE void setOutputDynamicsEnabled(int out,int unit,bool enabled);
    Q_INVOKABLE void setSpectrumSource(int source);
    Q_INVOKABLE double outputEq(int out,int band) const;
    Q_INVOKABLE double outputEqFreq(int out,int band) const;
    Q_INVOKABLE double outputEqQ(int out,int band) const;
    Q_INVOKABLE void setOutputEq(int out,int band,double value);
    Q_INVOKABLE void setOutputEqFreq(int out,int band,double value);
    Q_INVOKABLE void setOutputEqQ(int out,int band,double value);
    Q_INVOKABLE double outputDynamics(int out,int unit,const QString &key) const;
    Q_INVOKABLE void setOutputDynamics(int out,int unit,const QString &key,double value);
    Q_INVOKABLE double output(int out,int param) const;
    Q_INVOKABLE void setOutput(int out,int param,double value);
    Q_INVOKABLE double fx(int unit,int param) const;
    Q_INVOKABLE void setFx(int unit,int param,double value);
    Q_INVOKABLE void save();
    Q_INVOKABLE bool exportShow(const QString &fileName);
    Q_INVOKABLE bool importShow(const QString &fileName);
    Q_INVOKABLE void resetShow();
    Q_INVOKABLE QString configFile() const;
    Q_INVOKABLE int mainRoute(int outputIndex) const;
    Q_INVOKABLE void setMainRoute(int outputIndex,int source);
    Q_INVOKABLE void resetMeters();
    void setMaster(double value);
    void setSelected(int value);
    void setDemoMeters(bool value);
signals:
    void parameterRevisionChanged();
    void spectrumChanged();
    void showMessageChanged();
    void channelsChanged();
    void metersChanged();
    void auxesChanged();
    void masterChanged();
    void selectedChanged();
    void selectedOutputChanged();
    void demoMetersChanged();
    void audioStatusChanged();
    void audioDeviceChanged();
    void midiStatusChanged();
    void midiBankChanged();
private:
    double m_dominantHz=0.,m_dominantDb=-90.;
    unsigned m_lastSpectrumSequence=0;
    QVariantList m_channels, m_auxes;
    // Preserve each channel's independent state across LINK/UNLINK.
    std::array<QVariantMap,4> m_unlinkedRightState{};
    std::array<bool,4> m_hasUnlinkedRightState{{false,false,false,false}};
    QVariantList m_channelMeters, m_auxMeters, m_channelGR, m_auxGR; double m_mainGR=0,m_limiterGR=0;
    int m_parameterRevision=0;
    QVariantList m_spectrum;
    QVariantList m_spectrumPeaks;
    QVariantList m_rtaBands, m_rtaPeaks;
    std::array<int,60> m_rtaHoldFrames{};
    std::array<int,192> m_peakHoldFrames{};
    QString m_showMessage;
    int m_spectrumTick=0;
    void reloadShowState();
    void setShowMessage(const QString &message){m_showMessage=message;emit showMessageChanged();}
    void notifyParameterChange() { ++m_parameterRevision; emit parameterRevisionChanged(); }
    double m_master=0.0;
    int m_selected=0;
    int m_selectedOutput=0;
    bool m_demo=false;
    QSettings m_settings;
    QTimer m_timer;
    QTimer m_midiTimer;
    QString m_midiStatus="BCF2000: ricerca";
    void *m_midiSeq=nullptr;
    int m_midiPort=-1, m_midiRemoteClient=-1, m_midiRemotePort=-1;
    int m_midiBank=0;
    int m_bcfAuxSelection=0;
    int m_bcfBusSelection=4;
    int m_bcfDynUnit=1;
    bool m_midiDirty=true;
    int m_midiFeedbackIndex=0;
    int m_midiLast[64]{};
    // Physical SOLO buttons (strips 2..8): press/release and long-press state.
    bool m_bcfSoloHeld[8]{};
    unsigned m_bcfSoloGeneration[8]{};
    int m_bcfSoloKind[8]{};
    int m_bcfSoloIndex[8]{};
    bool m_bcfSoloLongFired[8]{};

    void pollBcf();
    void bcfEvent(int type,int ch,int param,int value);
    void bcfFeedback();
    double bcfDspRead(int bank,int slot) const;
    void bcfDspWrite(int bank,int slot,int value);
    int bcfDspMidi(int bank,int slot) const;
    void bcfSetStatus(const QString &s){if(s!=m_midiStatus){m_midiStatus=s;emit midiStatusChanged();}}
    void bcfSetBank(int bank){bank=qBound(0,17,bank);if(bank!=m_midiBank){m_midiBank=bank;m_midiDirty=true;std::fill(std::begin(m_midiLast),std::end(m_midiLast),-1);emit midiBankChanged();}}

    AudioEngine m_audio;
    double m_outputLevels[2]{-90,-90};
    double m_inputLevels[2]{-90,-90};
    void refreshAudio();
    std::thread m_fftThread;
    std::atomic<bool> m_fftStop{false};
    std::atomic<bool> m_fftUiPending{false};
    void fftWorker();
    void applySpectrum(const std::array<double,4097> &power, double fs);
};
