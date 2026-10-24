#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include <thread>
#include <array>
#include <QVariantList>
#include "dsp.h"
class AudioEngine:public QObject {
 Q_OBJECT
 Q_PROPERTY(bool running READ running NOTIFY runningChanged)
 Q_PROPERTY(QString state READ state NOTIFY stateChanged)
 Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY deviceChanged)
 Q_PROPERTY(QString inputDevice READ inputDevice WRITE setInputDevice NOTIFY deviceChanged)
 Q_PROPERTY(QString outputDevice READ outputDevice WRITE setOutputDevice NOTIFY deviceChanged)
 Q_PROPERTY(int xruns READ xruns NOTIFY statsChanged)
 Q_PROPERTY(int sampleRate READ sampleRate NOTIFY statsChanged)
 Q_PROPERTY(int blockFrames READ blockFrames NOTIFY statsChanged)
public:
 explicit AudioEngine(QObject *parent=nullptr);
 ~AudioEngine() override;
 bool running() const {return m_running.load();}
 QString state() const {return m_state;}
 QString device() const {return m_device;}
 void setDevice(const QString &v);
 QString inputDevice() const {return m_inputDevice;}
 QString outputDevice() const {return m_outputDevice;}
 void setInputDevice(const QString &v);
 void setOutputDevice(const QString &v);
 int xruns() const {return m_xruns.load();}
 int sampleRate() const {return m_sampleRate.load();}
 int blockFrames() const {return m_blockFrames.load();}
 Q_INVOKABLE QVariantList devices() const;
 Q_INVOKABLE void restart();
 Q_INVOKABLE void start();
 Q_INVOKABLE void stop();
 void update(const storm::Params &p);
 std::array<float,8> meters() const;
 std::array<float,2> outputMeters() const;
 std::array<float,2> inputMeters() const;
 std::array<float,4> auxMeters() const;
 std::array<float,8> channelGR() const;
 std::array<float,4> auxGR() const;
 std::array<float,2> mainGR() const;
 std::array<float,2> limiterGR() const;
 bool spectrumSamples(std::array<float,8192> &result, unsigned &sequence) const;
 void setAnalyzerSource(int src){m_analyzerSource.store(std::clamp(src,0,14));}

signals:
 void runningChanged();void stateChanged();void deviceChanged();void statsChanged();
private:
 void worker();
 void setState(const QString &s);
 std::atomic<bool> m_stop{false},m_running{false};
 std::shared_ptr<const storm::Params> m_params;
 std::thread m_thread;
 QString m_state="AUDIO STOPPED";
 QString m_device="default";
 QString m_inputDevice="default",m_outputDevice="default";
 std::atomic<int> m_xruns{0},m_sampleRate{0},m_blockFrames{0};
 std::array<std::atomic<float>,8> m_meters;
 std::array<std::atomic<float>,2> m_outputs;
 std::array<std::atomic<float>,2> m_inputs;
 std::array<std::atomic<float>,4> m_auxMeters,m_auxGR;
 std::array<std::atomic<float>,8> m_channelGR;
 std::array<std::atomic<float>,2> m_mainGR,m_limiterGR;
 // Audio thread owns m_assemble. Completed windows are immutable until
 // the GUI releases its slot. No mutex, allocation, or wait in audio callback.
 struct SpectrumSlot {
  std::array<float,8192> samples{};
  std::atomic<int> state{0}; // 0=free, 1=writing, 2=ready, 3=reading
 };
 mutable std::array<SpectrumSlot,3> m_spectrumSlots{};
 std::array<float,8192> m_assemble{};
 unsigned m_assembleCount=0;
 int m_lastAnalyzerSource=-1;
 std::atomic<int> m_publishedSlot{-1};
 std::atomic<unsigned> m_publishedSequence{0};
 std::atomic<int> m_analyzerSource{0};
};
