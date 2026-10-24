#include "mixercontroller.h"
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <array>
#if STORM_HAS_ALSA
#include <alsa/asoundlib.h>
#endif

// Preset 1 "Reason Mixer 1" from the provided BC Manager .bcf backup.
// MIDI channels are zero-based here; strip 1 is intentionally reserved.
static constexpr int faderCC[8]={1,2,4,12,5,6,7,8};
static constexpr int encoderCC[8]={1,2,3,26,27,6,7,8};
static constexpr int noteCC[8]={48,50,52,53,55,57,59,60};
static double toDb(int v){
    // 0 -> -60 dB, 100 -> 0 dB, 127 -> +10 dB (approximation).
    if(v<=100)return -60.+v*.6;
    return (v-100)*10./27.;
}
static int fromDb(double db){
    return std::clamp(int(std::lround(db<=0?(db+60.)/.6:100.+db*27./10.)),0,127);
}
static int toCC(double pan){return std::clamp(int(std::lround((pan+100.)*127./200.)),0,127);}

// Bank 0: strips 2-8 -> CH1-7. Bank 1: strips 2-8 -> CH8, AUX1-4,
// MAIN, SELECTED CH. Bank 2: strips 2-8 -> CH1-7, but encoder adjusts
// send-to-AUX1 (other controls remain volume/mute/solo). MAIN is on bank 1.
// Return type: 0=channel,1=aux,2=master,3=selected-channel, -1=unused.
static int target(int bank,int strip,int &index){
    if(strip==0)return -1;
    if(bank==0||(bank>=2&&bank<=5)){index=strip-1;return 0;}
    if(strip==1){index=7;return 0;}
    if(strip>=2&&strip<=5){index=strip-2;return 1;}
    if(strip==6){index=0;return 2;}
    if(strip==7){index=0;return 3;}
    return -1;
}

// Banks 3..17 are DSP pages. Values are mapped linearly to 7-bit MIDI.
// Strip 1 remains reserved. The seven usable controls address bands 1..7
// or seven parameters of the selected processor.
static constexpr int bankCount=18;
static constexpr const char *bankNames[bankCount]={
 "CH1-7","CH8 AUX MAIN","SEND AUX1","SEND AUX2","SEND AUX3","SEND AUX4",
 "CH EQ GAIN","CH EQ FREQ","CH EQ Q","CH GATE","CH COMP","CH LIMIT",
 "MAIN EQ","AUX EQ","BUS DYN","FX","OUTPUT DSP","ROUTING"};
static double fromMidi(int v,double lo,double hi){return lo+(hi-lo)*v/127.;}
static int toMidi(double v,double lo,double hi){return std::clamp(int(std::lround((v-lo)*127./(hi-lo))),0,127);}
// Selected DSP parameter: 0..6 (strip 2..8).
// Param reads and writes deliberately use the mixer's existing public API.
double MixerController::bcfDspRead(int bank,int slot) const{
 const int ch=selected(),bus=std::clamp(m_bcfAuxSelection,0,3),out=selectedOutput();
 if(bank>=6&&bank<=8)return bank==6?eq(ch,slot):bank==7?eqFreq(ch,slot):eqQ(ch,slot);
 if(bank>=9&&bank<=11){const int unit=bank-9;const char *keys[]={"threshold","ratio","attack","release"};return slot<4?dynamics(ch,unit,keys[slot]):0;}
 if(bank==12)return busEq(4,slot);
 if(bank==13)return busEq(bus,slot);
 if(bank==14){const char *keys[]={"threshold","ratio","attack","release"};return slot<4?busDynamics(m_bcfBusSelection,m_bcfDynUnit,keys[slot]):0;}
 if(bank==15)return slot<7?fx(slot/3,slot%3):0;
 if(bank==16)return slot<3?output(out,slot):slot<7?outputEq(out,slot-3):0;
 if(bank==17)return slot<7?double(mainRoute(slot)):0;
 return 0;
}
void MixerController::bcfDspWrite(int bank,int slot,int v){
 const int ch=selected(),bus=std::clamp(m_bcfAuxSelection,0,3),out=selectedOutput();
 if(bank>=6&&bank<=8){
   if(bank==6)setEq(ch,slot,fromMidi(v,-18,18));
   if(bank==7)setEqFreq(ch,slot,fromMidi(v,20,20000));
   if(bank==8)setEqQ(ch,slot,fromMidi(v,.2,12));
 }else if(bank>=9&&bank<=11){
   if(slot<4){const char *keys[]={"threshold","ratio","attack","release"};
      const double lo[]={-90,1,.1,1},hi[]={0,20,200,2000};
      setDynamics(ch,bank-9,keys[slot],fromMidi(v,lo[slot],hi[slot]));}
 }else if(bank==12)setBusEq(4,slot,fromMidi(v,-18,18));
 else if(bank==13)setBusEq(bus,slot,fromMidi(v,-18,18));
 else if(bank==14){if(slot<4){const char *keys[]={"threshold","ratio","attack","release"};
      const double lo[]={-90,1,.1,1},hi[]={0,20,200,2000};
      setBusDynamics(m_bcfBusSelection,m_bcfDynUnit,keys[slot],fromMidi(v,lo[slot],hi[slot]));}}
 else if(bank==15)setFx(slot/3,slot%3,fromMidi(v,0,100));
 else if(bank==16){if(slot<3)setOutput(out,slot,fromMidi(v,slot==2?20:-60,slot==2?20000:12));
      else setOutputEq(out,slot-3,fromMidi(v,-18,18));}
 else if(bank==17)setMainRoute(slot,std::clamp(int(std::lround(fromMidi(v,0,14))),0,14));
}
int MixerController::bcfDspMidi(int bank,int slot) const{
 const double x=bcfDspRead(bank,slot);
 if(bank==6||bank==12||bank==13)return toMidi(x,-18,18);
 if(bank==7)return toMidi(x,20,20000);
 if(bank==8)return toMidi(x,.2,12);
 if(bank>=9&&bank<=11||bank==14){if(slot>=4)return 0;
    const double lo[]={-90,1,.1,1},hi[]={0,20,200,2000};return toMidi(x,lo[slot],hi[slot]);}
 if(bank==15)return toMidi(x,0,100);
 if(bank==16)return slot<3?toMidi(x,slot==2?20:-60,slot==2?20000:12):toMidi(x,-18,18);
 if(bank==17)return toMidi(x,0,14);
 return 0;
}

void MixerController::bcfEvent(int type,int ch,int param,int value){

    // Bottom-right buttons 49/50 on MIDI channel 4: bank backward/forward.
    // Preset toggleoff sends 127 on press and 0 on release.
    if(type==0&&ch==3&&value>=64){
        if(param==4 || param==3){bcfSetBank((m_midiBank+bankCount-1)%bankCount);return;}
        if(param==2 || param==1){bcfSetBank((m_midiBank+1)%bankCount);return;}
    }
    if(m_midiBank>=3){
        int strip=-1;
        if(type==0&&ch==2){for(int i=1;i<8;i++)if(param==faderCC[i])strip=i;}
        if(type==0&&ch==1){for(int i=1;i<8;i++)if(param==encoderCC[i])strip=i;}
        if(strip>=1){
            const int slot=strip-1;
            m_midiLast[slot*4+(ch==1?1:0)]=value;
            // DSP faders and encoders both adjust the same parameter.
            bcfDspWrite(m_midiBank,slot,value);
            return;
        }
        // On DSP pages upper/lower buttons are navigation/enable controls.
        if(type==0&&ch==0&&value>=64){
            if(param>=2&&param<=8){
                const int n=param-2;
                if(m_midiBank==13)m_bcfAuxSelection=n%4;
                else if(m_midiBank==14){m_bcfBusSelection=n%5;m_bcfDynUnit=n%3;}
                else if(m_midiBank==16)setSelectedOutput(n%8);
                else if(m_midiBank>=6&&m_midiBank<=11)setSelected(n%8);
                std::fill(std::begin(m_midiLast),std::end(m_midiLast),-1);
                m_midiDirty=true;
            }else if(param>=10&&param<=16){
                if(m_midiBank>=9&&m_midiBank<=11)setDynamicsEnabled(selected(),m_midiBank-9,!dynamicsEnabled(selected(),m_midiBank-9));
                else if(m_midiBank==14)setBusDynamicsEnabled(m_bcfBusSelection,m_bcfDynUnit,!busDynamicsEnabled(m_bcfBusSelection,m_bcfDynUnit));
                else if(m_midiBank==16)setOutputDynamicsEnabled(selectedOutput(),1,!outputDynamicsEnabled(selectedOutput(),1));
            }
        }
        return;
    }
    // Physical upper buttons: MUTE. Lower buttons: SOLO on a short
    // press; holding SOLO for >= 2 seconds resets the mapped fader to 0 dB.
    // The target is captured at press time, so a bank change while holding
    // cannot reset a different strip. A long press never toggles SOLO.
    if(type==0 && ch==0 && param>=2 && param<=16){
        const bool isMute=(param>=2 && param<=8);
        const bool isSolo=(param>=10 && param<=16);
        if(isMute || isSolo){
            const int physical=isMute?param-1:param-9;
            if(physical>=1 && physical<=7){
                int ix=-1;
                const int k=target(m_midiBank,physical,ix);
                if(isMute){
                    if(value>=64){
                        if(k==0 || k==3){
                            const int chIndex=(k==3)?selected():ix;
                            const bool current=m_channels[chIndex].toMap().value("mute").toBool();
                            setChannel(chIndex,"mute",!current);
                        }else if(k==1){
                            const bool current=m_auxes[ix].toMap().value("mute").toBool();
                            setAux(ix,"mute",!current);
                        }
                    }
                    return;
                }
                // The BCF preset sends 127 on press, 0 on release.
                if(value>=64){
                    if(m_bcfSoloHeld[physical])return; // repeated press
                    m_bcfSoloHeld[physical]=true;
                    m_bcfSoloLongFired[physical]=false;
                    m_bcfSoloKind[physical]=k;
                    m_bcfSoloIndex[physical]=(k==3)?selected():ix;
                    const unsigned token=++m_bcfSoloGeneration[physical];
                    QTimer::singleShot(2000,this,[this,physical,token]{
                        if(!m_bcfSoloHeld[physical] ||
                           m_bcfSoloGeneration[physical]!=token)return;
                        m_bcfSoloLongFired[physical]=true;
                        const int kind=m_bcfSoloKind[physical];
                        const int index=m_bcfSoloIndex[physical];
                        if(kind==0 || kind==3)setChannel(index,"fader",0.0);
                        else if(kind==1)setAux(index,"fader",0.0);
                        else if(kind==2)setMaster(0.0);
                        // Feedback detects the new fader state and moves the motor.
                    });
                }else{
                    if(!m_bcfSoloHeld[physical])return;
                    m_bcfSoloHeld[physical]=false;
                    ++m_bcfSoloGeneration[physical]; // cancel pending timer
                    if(m_bcfSoloLongFired[physical])return;
                    const int kind=m_bcfSoloKind[physical];
                    const int index=m_bcfSoloIndex[physical];
                    if(kind==0 || kind==3){
                        const bool current=m_channels[index].toMap().value("solo").toBool();
                        setChannel(index,"solo",!current);
                    }else if(kind==1){
                        const bool current=m_auxes[index].toMap().value("solo").toBool();
                        setAux(index,"solo",!current);
                    }
                }
                return;
            }
        }
    }
    // Preset 1 buttons 33-48: channel 1 CC 1-16. These are real
    // bottom-row controls; use CC 1-8 for channel selection in the bank.
    if(false && type==0&&ch==0&&param>=2&&param<=8&&value>=64){
        int ix=-1;const int k=target(m_midiBank,param-1,ix);
        if(k==0)setSelected(ix);
        return;
    }
    int strip=-1,kind=-1,index=-1;
    if(type==0&&ch==2){for(int i=1;i<8;i++)if(param==faderCC[i]){strip=i;break;}}
    else if(type==0&&ch==1){for(int i=1;i<8;i++)if(param==encoderCC[i]){strip=i;break;}}
    else if(type==0&&ch==0){
        if(param>=40&&param<=46)strip=param-39;
        else if(param>=54&&param<=60)strip=param-53;
        else if(param>=62&&param<=68)strip=param-61;
    }
    else if(type==1&&ch==0){for(int i=1;i<8;i++)if(param==noteCC[i]){strip=i;break;}}
    if(strip<1)return;
    kind=target(m_midiBank,strip,index);
    if(kind<0)return;
    if(type==0&&ch==2){
        // Track physical position; avoid sending a motor command back for
        // each hardware fader movement (feedback loop + UI stalls).
        const int slot=(strip-1)*4;
        m_midiLast[slot]=value;
        const double db=toDb(value);
        if(kind==0&&std::abs(m_channels[index].toMap().value("fader").toDouble()-db)>.05)setChannel(index,"fader",db);
        else if(kind==1&&std::abs(m_auxes[index].toMap().value("fader").toDouble()-db)>.05)setAux(index,"fader",db);
        else if(kind==2&&std::abs(master()-db)>.05)setMaster(db);
        else if(kind==3&&std::abs(m_channels[selected()].toMap().value("fader").toDouble()-db)>.05)setChannel(selected(),"fader",db);
    }else if(type==0&&ch==1){
        m_midiLast[(strip-1)*4+1]=value;
        if(m_midiBank>=2&&m_midiBank<=5&&kind==0)setSend(index,m_midiBank-2,-90.+value*100./127.);
        else if(kind==0&&std::abs(m_channels[index].toMap().value("pan").toDouble()-((value*200./127.)-100.))>.5)setChannel(index,"pan",(value*200./127.)-100.);
        else if(kind==3&&std::abs(m_channels[selected()].toMap().value("pan").toDouble()-((value*200./127.)-100.))>.5)setChannel(selected(),"pan",(value*200./127.)-100.);
    }else if(type==0&&ch==0){
        // CC buttons 9-32 use inverted 127->0 toggleon.
        if(param>=40&&param<=46){
            const bool v=value<64;
            if(kind==0&&m_channels[index].toMap().value("mute").toBool()!=v)setChannel(index,"mute",v);
            if(kind==1&&m_auxes[index].toMap().value("mute").toBool()!=v)setAux(index,"mute",v);
        }else if(param>=54&&param<=60){
            const bool v=value<64;
            if(kind==0&&m_channels[index].toMap().value("solo").toBool()!=v)setChannel(index,"solo",v);
            if(kind==1&&m_auxes[index].toMap().value("solo").toBool()!=v)setAux(index,"solo",v);
        }else if(param>=62&&param<=68&&value<64){
            if(kind==0)setSelected(index);
            if(kind==3)setSelected((selected()+1)%8);
        }
    }else if(type==1&&value>0){
        // Note buttons 1-8: select corresponding strip/channel.
        if(kind==0)setSelected(index);
    }
}

#if STORM_HAS_ALSA
static void sendCC(snd_seq_t *seq,int port,int destClient,int destPort,int channel,int cc,int value){
    snd_seq_event_t ev;snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev,port);
    snd_seq_ev_set_dest(&ev,destClient,destPort);
    snd_seq_ev_set_direct(&ev);
    snd_seq_ev_set_controller(&ev,channel,cc,std::clamp(value,0,127));
    // ALSA output is nonblocking; never wait for the USB MIDI queue.
    const int rc=snd_seq_event_output_direct(seq,&ev);
    static int printed=0;
    if(printed<8){
        qInfo().noquote() << "[BCF OUTPUT] dest=" << destClient << ":" << destPort
                          << "ch=" << channel+1 << "CC=" << cc << "value=" << value << "result=" << rc;
        ++printed;
    }
    if(rc<0){
        static int failures=0;
        if(failures++<5)qWarning("BCF2000 MIDI output failed: %s",snd_strerror(rc));
    }
}
#endif

void MixerController::bcfFeedback(){
#if STORM_HAS_ALSA
    if(!m_midiSeq||m_midiRemoteClient<0)return;
    auto *seq=static_cast<snd_seq_t*>(m_midiSeq);
    // Scan the entire cached state on every tick: changed controls are
    // transmitted immediately instead of waiting for a 28-tick round trip.
    // Cap actual USB writes to avoid congesting the controller/UI.
    constexpr int maxWritesPerTick=3;
    int writes=0;
    for(int scanned=0;scanned<28 && writes<maxWritesPerTick;++scanned){
        const int slot=(m_midiFeedbackIndex+scanned)%28;
        const int strip=1+slot/4,field=slot%4;
        if(m_midiBank>=6){
            const int slotIndex=strip-1;
            // Fader and encoder mirror the current DSP parameter. Buttons
            // are input-only on DSP pages, to avoid stale LED assumptions.
            if(field>=2)continue;
            const int value=bcfDspMidi(m_midiBank,slotIndex);
            const int ch=(field==0)?2:1;
            const int cc=(field==0)?faderCC[strip]:encoderCC[strip];
            if(m_midiLast[slot]!=value){
                sendCC(seq,m_midiPort,m_midiRemoteClient,m_midiRemotePort,ch,cc,value);
                m_midiLast[slot]=value;++writes;
            }
            continue;
        }
        int index=0;const int kind=target(m_midiBank,strip,index);
        if(kind<0)continue;
        QVariantMap c;
        if(kind==0)c=m_channels[index].toMap();
        else if(kind==1)c=m_auxes[index].toMap();
        else if(kind==3)c=m_channels[selected()].toMap();
        int ch=0,cc=0,value=0;
        if(field==0){ch=2;cc=faderCC[strip];value=fromDb(kind==2?master():c.value("fader").toDouble());}
        else if(field==1){
            if(kind==2)continue;
            ch=1;cc=encoderCC[strip];
            value=(m_midiBank>=2&&m_midiBank<=5&&kind==0)?std::clamp(int(std::lround((send(index,m_midiBank-2)+90.)*127./100.)),0,127):toCC(c.value("pan").toDouble());
        }else if(field==2){ch=0;cc=1+strip;value=c.value("mute").toBool()?127:0;}
        else {ch=0;cc=9+strip;value=c.value("solo").toBool()?127:0;}
        if(m_midiLast[slot]!=value){
            sendCC(seq,m_midiPort,m_midiRemoteClient,m_midiRemotePort,ch,cc,value);
            m_midiLast[slot]=value;
            ++writes;
        }
    }
    m_midiFeedbackIndex=(m_midiFeedbackIndex+1)%28;
    m_midiDirty=false;
#endif
}

void MixerController::pollBcf(){
#if STORM_HAS_ALSA
    auto *seq=static_cast<snd_seq_t*>(m_midiSeq);
    if(!seq){
        if(snd_seq_open(&seq,"default",SND_SEQ_OPEN_DUPLEX,SND_SEQ_NONBLOCK)<0){bcfSetStatus("BCF2000: ALSA MIDI non disponibile");return;}
        snd_seq_set_client_name(seq,"STORM MIX BCF2000");
        m_midiPort=snd_seq_create_simple_port(seq,"BCF2000 control",SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_READ|SND_SEQ_PORT_CAP_SUBS_WRITE,SND_SEQ_PORT_TYPE_MIDI_GENERIC|SND_SEQ_PORT_TYPE_APPLICATION);
        if(m_midiPort<0){snd_seq_close(seq);bcfSetStatus("BCF2000: errore porta ALSA");return;}
        m_midiSeq=seq;
    }
    if(m_midiRemoteClient<0){
        snd_seq_client_info_t *ci;snd_seq_port_info_t *pi;
        snd_seq_client_info_alloca(&ci);snd_seq_port_info_alloca(&pi);
        snd_seq_client_info_set_client(ci,-1);
        while(snd_seq_query_next_client(seq,ci)>=0){
            const int client=snd_seq_client_info_get_client(ci);
            if(client==snd_seq_client_id(seq))continue;
            const QString name=QString::fromUtf8(snd_seq_client_info_get_name(ci)).toLower();
            if(!name.contains("bcf2000")&&!name.contains("bcf 2000"))continue;
            snd_seq_port_info_set_client(pi,client);snd_seq_port_info_set_port(pi,-1);
            while(snd_seq_query_next_port(seq,pi)>=0){
                const unsigned caps=snd_seq_port_info_get_capability(pi);
                if((caps&SND_SEQ_PORT_CAP_READ)==0||(caps&SND_SEQ_PORT_CAP_WRITE)==0)continue;
                const int remote=snd_seq_port_info_get_port(pi);
                if(snd_seq_connect_from(seq,m_midiPort,client,remote)<0)continue;
                // Explicit ALSA output subscription (visible in aconnect -l).
                // Direct event delivery below still targets the USB port;
                // the subscription is also useful for connection diagnostics.
                const int outRc=snd_seq_connect_to(seq,m_midiPort,client,remote);
                qInfo("BCF2000: MIDI output subscription %s (%d)",
                      outRc>=0?"OK":"FAILED",outRc);
                m_midiRemoteClient=client;m_midiRemotePort=remote;
                std::fill(std::begin(m_midiLast),std::end(m_midiLast),-1);
                m_midiDirty=true;
                bcfSetStatus(QString("BCF2000 %1:%2 | Banco %3 | Feedback ON").arg(client).arg(remote).arg(m_midiBank+1));
                break;
            }
            if(m_midiRemoteClient>=0)break;
        }
        if(m_midiRemoteClient<0)bcfSetStatus("BCF2000: in attesa USB MIDI");
    }
    if(m_midiRemoteClient>=0){
        snd_seq_client_info_t *ci;snd_seq_client_info_alloca(&ci);
        if(snd_seq_get_any_client_info(seq,m_midiRemoteClient,ci)<0){
            m_midiRemoteClient=-1;m_midiRemotePort=-1;
            bcfSetStatus("BCF2000: scollegato");return;
        }
    }
    // fetch=1 is essential: pending(...,0) checks only ALSA's local queue,
    // so a quiet local queue prevents incoming USB MIDI from being read.
    for(int n=0;n<32&&snd_seq_event_input_pending(seq,1)>0;++n){
        snd_seq_event_t *ev=nullptr;
        if(snd_seq_event_input(seq,&ev)<0||!ev)break;
        if(ev->source.client!=m_midiRemoteClient||ev->source.port!=m_midiRemotePort){
            static int ignored=0;
            if(ignored++<8)qInfo() << "[BCF IGNORED SOURCE]" << ev->source.client << ev->source.port;
            continue;
        }
        if(ev->type==SND_SEQ_EVENT_CONTROLLER)bcfEvent(0,ev->data.control.channel,ev->data.control.param,ev->data.control.value);
        else if(ev->type==SND_SEQ_EVENT_NOTEON)bcfEvent(1,ev->data.note.channel,ev->data.note.note,ev->data.note.velocity);
    }
    bcfFeedback();
#else
    bcfSetStatus("BCF2000: ALSA non compilato");
#endif
}
