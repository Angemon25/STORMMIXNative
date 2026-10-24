#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <vector>
namespace storm {
constexpr int NCH=8, NAUX=4, NOUT=8;
inline float dbGain(float d){return d<=-90?0.f:std::pow(10.f,d/20.f);}
inline float dB(float a){return 20.f*std::log10(std::max(0.0000316228f,a));}
inline float clip(float v){return std::clamp(v,-1.f,1.f);}
struct Dynamics {float threshold=-24,ratio=4,attack=10,release=120; bool enabled=false;};
struct Channel {float fader=0,gain=0,pan=0; bool mute=false,solo=false,pfl=false,linked=false; std::array<float,8> eq{}; std::array<float,8> eqFreq{{65,150,350,800,1700,3600,7500,13000}},eqQ{{1,1,1,1,1,1,1,1}}; std::array<Dynamics,3> dyn{}; std::array<float,NAUX> sends{{-90,-90,-90,-90}};};
struct Aux {float fader=0; bool mute=false; std::array<float,8> eq{}; std::array<float,8> eqFreq{{65,150,350,800,1700,3600,7500,13000}},eqQ{{1,1,1,1,1,1,1,1}}; std::array<Dynamics,3> dyn{};};
struct Output {float trim=0,delayMs=0,hpf=20,lpf=20000,limit=-1;
 int hpSlope=12,lpSlope=12,filterType=0;
 std::array<float,8> eq{},eqFreq{{65,150,350,800,1700,3600,7500,13000}},eqQ{{1,1,1,1,1,1,1,1}};
 std::array<Dynamics,3> dyn{};};
struct Params {std::array<Channel,NCH> ch;std::array<Aux,NAUX> aux;std::array<Output,NOUT> out; std::array<float,8> mainEq{}; std::array<float,8> mainFreq{{65,150,350,800,1700,3600,7500,13000}},mainQ{{1,1,1,1,1,1,1,1}}; std::array<Dynamics,3> mainDyn{};float master=0;std::array<int,2> mainRouting{{0,0}};std::array<std::array<float,3>,3> fx{{{{35,30,0}},{{35,35,0}},{{35,25,0}}}};bool fxEnabled=true;};
struct Biquad {
 float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
 void setPeaking(float fs,float hz,float gainDb,float q=1.1f){
   hz=std::clamp(hz,20.f,fs*0.45f);float w=2*3.14159265359f*hz/fs,cs=std::cos(w),sn=std::sin(w),a=std::pow(10.f,gainDb/40.f),al=sn/(2*q);
   float d=1+al/a; b0=(1+al*a)/d;b1=-2*cs/d;b2=(1-al*a)/d;a1=-2*cs/d;a2=(1-al/a)/d;
 }
 void setCrossover(float fs,float hz,bool high,float q){
  hz=std::clamp(hz,20.f,fs*.45f);q=std::max(.3f,q);
  float w=2*3.14159265359f*hz/fs,cs=std::cos(w),sn=std::sin(w),al=sn/(2*q),d=1+al;
  b0=(high?(1+cs):(1-cs))/(2*d);b1=(high?-(1+cs):(1-cs))/d;b2=b0;
  a1=-2*cs/d;a2=(1-al)/d;
 }
 float process(float x){float y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return std::isfinite(y)?y:0.f;}
};
struct Envelope {
 float env=0;
 float gateGain=1.f;
 float process(float x,float attackMs,float releaseMs,float fs){
  float t=std::abs(x),ms=t>env?attackMs:releaseMs;
  float k=std::exp(-1.f/(fs*std::max(.001f,ms)*.001f));env=k*env+(1-k)*t;return env;
 }
};
class Processor {
public:
 explicit Processor(float sampleRate=48000):fs(sampleRate){
  for(auto &d:delay) d.resize(size_t(fs*.52f)+2);
  for(auto &d:effectDelay) d.resize(size_t(fs*.8f)+2);
  reverbL.resize(size_t(fs*.031f)+2);reverbR.resize(size_t(fs*.037f)+2);
 }
 void configure(const Params &p){params=p;
  masterGain=dbGain(params.master);
  // Targets change immediately; smoothed values are preserved across configure() calls.
  // ~15 ms gain ramps avoid zipper noise while keeping controls responsive.
  gainSmoothing=1.f-std::exp(-1.f/(fs*.015f));
  fxActive=params.fxEnabled && (params.fx[0][2]>0.01f || params.fx[1][2]>0.01f || params.fx[2][2]>0.01f);
  for(int c=0;c<NCH;c++){
   const auto &ch=params.ch[c];
   channelGain[c]=dbGain(ch.gain); faderGain[c]=dbGain(ch.fader);
   float pan=std::clamp(ch.pan/100.f,-1.f,1.f);
   panL[c]=std::sqrt((1-pan)*.5f);
   panR[c]=std::sqrt((1+pan)*.5f);
   for(int a=0;a<NAUX;a++)sendGain[c][a]=dbGain(ch.sends[a]);
   for(int u=0;u<3;u++){
    const auto &dyn=ch.dyn[u];
    dynThreshold[c][u]=dbGain(dyn.threshold);
    dynSlope[c][u]=1.f-1.f/std::max(1.f,dyn.ratio);
    envAttack[c][u]=std::exp(-1.f/(fs*std::max(.001f,dyn.attack)*.001f));
    envRelease[c][u]=std::exp(-1.f/(fs*std::max(.001f,dyn.release)*.001f));
   }
  }
  for(int a=0;a<NAUX;a++)auxGain[a]=params.aux[a].mute?0.f:dbGain(params.aux[a].fader);
  for(int o=0;o<2;o++){
   const auto &v=params.out[o];
   outTrim[o]=dbGain(v.trim);
   hpCoeff[o]=std::exp(-2*3.14159265f*std::clamp(v.hpf,20.f,fs*.45f)/fs);
   lpCoeff[o]=1-std::exp(-2*3.14159265f*std::clamp(v.lpf,20.f,fs*.45f)/fs);
   outCeiling[o]=dbGain(std::min(-.1f,v.limit));
   outDelay[o]=size_t(std::clamp(v.delayMs,0.f,500.f)*fs/1000.f);
   const float q=(v.filterType==1 && v.hpSlope==24)?.70710678f:.70710678f;
   for(int j=0;j<2;j++){
    hpFilter[o][j].setCrossover(fs,v.hpf,true,q);
    lpFilter[o][j].setCrossover(fs,v.lpf,false,q);
   }
   for(int j=0;j<8;j++)outputEq[o][j].setPeaking(fs,v.eqFreq[j],v.eq[j],v.eqQ[j]);
  }
  for(int ch=0;ch<NCH;ch++)for(int b=0;b<8;b++)eq[ch][b].setPeaking(fs,params.ch[ch].eqFreq[b],params.ch[ch].eq[b],params.ch[ch].eqQ[b]);
  for(int a=0;a<NAUX;a++)for(int b=0;b<8;b++)auxEq[a][b].setPeaking(fs,params.aux[a].eqFreq[b],params.aux[a].eq[b],params.aux[a].eqQ[b]);
  for(int c=0;c<2;c++)for(int b=0;b<8;b++)mainEq[c][b].setPeaking(fs,params.mainFreq[b],params.mainEq[b],params.mainQ[b]);
 }
 void process(const float *input,float *output,int frames,float *analyzer=nullptr,int analyzerSource=12){
  bool anySolo=false;for(const auto &c:params.ch)anySolo|=c.solo;
  for(int n=0;n<frames;n++){
   float aux[NAUX]={};float processedAux[NAUX]={};float L=0,R=0;float analysis=0;
   for(int c=0;c<NCH;c++){
    // 2x2 test: only CH1/CH2 are mapped; CH3..8 stay visible for future 8-input hardware.
    float x=c<2?input[n*2+c]:0;
    if(!std::isfinite(x))x=0;
    auto &p=params.ch[c];
    smoothChannelGain[c]+=(channelGain[c]-smoothChannelGain[c])*gainSmoothing;
    smoothFaderGain[c]+=(faderGain[c]-smoothFaderGain[c])*gainSmoothing;
    x*=smoothChannelGain[c];
    for(auto &b:eq[c])x=b.process(x);
    float a=envelope(c,0,x);
    // Gate: smooth attenuation with hysteresis-like soft knee, never the
    // previous envelope/threshold instantaneous multiplication.
    if(p.dyn[0].enabled){
      const float target=std::clamp(a/std::max(dynThreshold[c][0],1.e-7f),0.f,1.f);
      const float gateTarget=target*target;
      channelGateGain[c]+=(gateTarget-channelGateGain[c])*(gateTarget>channelGateGain[c]?.003f:.00035f);
      x*=channelGateGain[c];
    } else channelGateGain[c]+=(1.f-channelGateGain[c])*.003f;
    a=envelope(c,1,x);
    float over=a/dynThreshold[c][1];
    if(p.dyn[1].enabled && over>1.f){float g=std::pow(over,-dynSlope[c][1]); gr[c]=std::max(gr[c]*.9995f,-20.f*std::log10(std::max(g,0.0001f)));x*=g;}
    a=envelope(c,2,x);
    over=a/dynThreshold[c][2];
    if(p.dyn[2].enabled && over>1.f){float g=std::pow(over,-dynSlope[c][2]); gr[c]=std::max(gr[c]*.9995f,-20.f*std::log10(std::max(g,0.0001f)));x*=g;}
    x*=smoothFaderGain[c];
    if(p.mute||(anySolo&&!p.solo))x=0;
    if(analyzerSource==c)analysis=x;
    L+=x*panL[c];R+=x*panR[c];
    for(int aidx=0;aidx<NAUX;aidx++)aux[aidx]+=x*sendGain[c][aidx];
    meter[c]=std::max(meter[c]*.9995f,std::abs(x));
   }
   // Aux busses are summed to stereo at -6dB for the 2-output prototype.
   for(int a=0;a<NAUX;a++){
    float x=aux[a];
    for(auto &b:auxEq[a])x=b.process(x);
    x=applyBusDynamics(x,params.aux[a].dyn,auxEnv[a],auxGr[a]);
    smoothAuxGain[a]+=(auxGain[a]-smoothAuxGain[a])*gainSmoothing;
    x*=smoothAuxGain[a];processedAux[a]=x;if(analyzerSource==8+a)analysis=x;auxMeter[a]=std::max(auxMeter[a]*.9995f,std::abs(x));
    // 2x2 prototype: AUX buses fold to MAIN, no independent physical AUX outputs.
    L+=x*.35f;R+=x*.35f;
   }
   if(fxActive){
    float mono=(L+R)*.5f;
    float dt=std::clamp(params.fx[0][0],0.f,100.f)/100.f;
    size_t dSamples=size_t(fs*(.08f+dt*.62f));
    auto &dl=effectDelay[0];auto &dr=effectDelay[1];
    float oldL=dl[(fxPos+dl.size()-dSamples)%dl.size()];
    float oldR=dr[(fxPos+dr.size()-dSamples)%dr.size()];
    float feedback=std::clamp(params.fx[0][1]/100.f,0.f,.85f);
    dl[fxPos]=mono+oldR*feedback;dr[fxPos]=mono+oldL*feedback;
    float mix=std::clamp(params.fx[0][2]/100.f,0.f,1.f);
    L+=oldL*mix*.45f;R+=oldR*mix*.45f;
    fxPos=(fxPos+1)%dl.size();
    // lightweight stereo comb reverb
    float rv=std::clamp(params.fx[1][1]/100.f,.0f,.90f);
    float rL=reverbL[revPosL],rR=reverbR[revPosR];
    reverbL[revPosL]=mono+rL*rv;reverbR[revPosR]=mono+rR*rv;
    revPosL=(revPosL+1)%reverbL.size();revPosR=(revPosR+1)%reverbR.size();
    mix=std::clamp(params.fx[1][2]/100.f,0.f,1.f);L+=rL*mix*.18f;R+=rR*mix*.18f;
    // short modulated chorus taps on the shared delay lines
    float depth=.002f+params.fx[2][0]*.00008f;
    size_t tap=size_t(fs*(.014f+depth*(.5f+.5f*std::sin(phase))));
    size_t tapR=size_t(fs*(.014f+depth*(.5f+.5f*std::sin(phase+1.7f))));
    phase+=2*3.14159265f*(.15f+params.fx[2][1]*.018f)/fs;if(phase>6.2831853f)phase-=6.2831853f;
    mix=std::clamp(params.fx[2][2]/100.f,0.f,1.f);
    L+=dl[(fxPos+dl.size()-tap)%dl.size()]*mix*.12f;
    R+=dr[(fxPos+dr.size()-tapR)%dr.size()]*mix*.12f;
   }
   L=applyBusDynamics(L,params.mainDyn,mainEnv[0],mainGr[0]);
   R=applyBusDynamics(R,params.mainDyn,mainEnv[1],mainGr[1]);
   for(auto &b:mainEq[0])L=b.process(L);
   for(auto &b:mainEq[1])R=b.process(R);
   smoothMasterGain+=(masterGain-smoothMasterGain)*gainSmoothing;
   L*=smoothMasterGain;R*=smoothMasterGain;
   if(analyzerSource==12)analysis=(L+R)*.5f;
   float s[2]={L,R};
   // Physical output 1/2 routing: 0=legacy L/R, 1=mono, 2..5=AUX, 6=mute, 7=MAIN L, 8=MAIN R.
   for(int c=0;c<2;c++){
    const int source=params.mainRouting[c];
    if(source==1)s[c]=(L+R)*.5f;
    else if(source>=2 && source<=5)s[c]=processedAux[source-2];
    else if(source==6)s[c]=0.f;
    else if(source==7)s[c]=L;
    else if(source==8)s[c]=R;
   }
   for(int c=0;c<2;c++){
    float x=s[c];
    // AUX sources already have their own EQ and dynamics. Do not process twice.
    const bool auxSource=params.mainRouting[c]>=2 && params.mainRouting[c]<=5;
    if(!auxSource){for(auto &b:outputEq[c])x=b.process(x);x=applyBusDynamics(x,params.out[c].dyn,outputEnv[c],outputGr[c]);}
    smoothOutTrim[c]+=(outTrim[c]-smoothOutTrim[c])*gainSmoothing;
    x*=smoothOutTrim[c];
    // Cascaded Butterworth sections: 12/24 dB/oct, or Linkwitz-Riley 24 dB/oct.
    for(int section=0;section<params.out[c].hpSlope/12;section++)x=hpFilter[c][section].process(x);
    for(int section=0;section<params.out[c].lpSlope/12;section++)x=lpFilter[c][section].process(x);
    auto &buf=delay[c];size_t offset=outDelay[c];
    buf[delayPos]=x;x=buf[(delayPos+buf.size()-offset)%buf.size()];
    // hard peak ceiling: not a lookahead loudspeaker protection limiter.
    const float preLimit=x;
    x=std::clamp(x,-outCeiling[c],outCeiling[c]);
    const float reduction=std::abs(preLimit)>0.00001f?20.f*std::log10(std::max(std::abs(preLimit),0.00001f)/std::max(std::abs(x),0.00001f)):0.f;
    limitGr[c]=std::max(limitGr[c]*.9995f,reduction);
    if(analyzerSource==13+c)analysis=x;
    output[n*2+c]=clip(x);
    outMeter[c]=std::max(outMeter[c]*.9995f,std::abs(x));
   }
   if(analyzer)analyzer[n]=analysis;
   delayPos=(delayPos+1)%delay[0].size();
  }
 }
 std::array<float,2> outputGr{};
 std::array<float,NCH> meter{},gr{};std::array<float,NAUX> auxMeter{},auxGr{};std::array<float,2> outMeter{},mainGr{},limitGr{};
private:
 float applyBusDynamics(float x,const std::array<Dynamics,3> &d,std::array<Envelope,3> &e,float &gainReduction){
   for(int u=0;u<3;u++){
    const auto &v=d[u];float level=e[u].process(x,v.attack,v.release,fs);
    const float threshold=dbGain(v.threshold);
    if(!v.enabled)continue;
    if(u==0){
      // Gate disabled by default; enabled gate has smooth gain envelope.
      const float target=std::clamp(level/std::max(threshold,1.e-7f),0.f,1.f);
      const float g=target*target;
      const float coeff=g>e[u].gateGain?.003f:.00035f;
      e[u].gateGain+=(g-e[u].gateGain)*coeff;
      x*=e[u].gateGain;
    } else if(level>threshold){float ratio=std::max(1.f,v.ratio);
     float gain=std::pow(level/threshold,-(1.f-1.f/ratio));
     gainReduction=std::max(gainReduction*.9995f,-20.f*std::log10(std::max(.00001f,gain)));x*=gain;
    }
   }
   return x;
 }
 float envelope(int ch,int unit,float x){
  auto &v=env[ch][unit].env;
  float t=std::abs(x);
  float k=t>v?envAttack[ch][unit]:envRelease[ch][unit];
  v=k*v+(1-k)*t;
  return v;
 }
 float channelGain[NCH]{},faderGain[NCH]{},panL[NCH]{},panR[NCH]{},sendGain[NCH][NAUX]{};
 float envAttack[NCH][3]{},envRelease[NCH][3]{},dynThreshold[NCH][3]{},dynSlope[NCH][3]{},auxGain[NAUX]{};
 bool fxActive=false;
 float masterGain=1,outTrim[2]{1,1},hpCoeff[2]{},lpCoeff[2]{},outCeiling[2]{1,1};
 float gainSmoothing=0.001f;
 std::array<float,NCH> channelGateGain{{1,1,1,1,1,1,1,1}};
 float smoothMasterGain=1.f;
 std::array<float,NCH> smoothChannelGain{{1,1,1,1,1,1,1,1}},smoothFaderGain{{1,1,1,1,1,1,1,1}};
 std::array<float,NAUX> smoothAuxGain{{1,1,1,1}};
 float smoothOutTrim[2]{1.f,1.f};
 size_t outDelay[2]{};
 float fs;Params params{};std::array<std::array<Biquad,8>,NCH> eq{}; std::array<std::array<Biquad,8>,NAUX> auxEq{}; std::array<std::array<Biquad,8>,2> mainEq{}; std::array<std::array<Envelope,3>,NAUX> auxEnv{}; std::array<std::array<Envelope,3>,2> mainEnv{};
 std::array<std::array<Envelope,3>,NCH> env{};
 std::array<std::vector<float>,2> delay,effectDelay;
 std::vector<float> reverbL,reverbR;size_t delayPos=0,fxPos=0,revPosL=0,revPosR=0;
 std::array<std::array<Biquad,2>,2> hpFilter{},lpFilter{};
 std::array<std::array<Biquad,8>,2> outputEq{};
 std::array<std::array<Envelope,3>,2> outputEnv{};
 float phase=0;
};
}
