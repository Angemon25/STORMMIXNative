#include "dsp.h"
#include <cstdio>
#include <cmath>
int main(){
 storm::Processor dsp;storm::Params p;
 for(int i=0;i<8;i++){p.ch[i].dyn[0].threshold=-90;p.ch[i].dyn[1].threshold=0;p.ch[i].dyn[2].threshold=0;}
 p.out[0].hpf=p.out[1].hpf=20;p.out[0].lpf=p.out[1].lpf=20000;
 dsp.configure(p);
 float in[512]{},out[512]{};for(int i=0;i<256;i++){in[i*2]=.1f*std::sin(2*3.14159265f*440*i/48000);in[i*2+1]=.1f*std::sin(2*3.14159265f*660*i/48000);}
 dsp.process(in,out,256);float max=0;for(float v:out){if(!std::isfinite(v))return 1;max=std::max(max,std::abs(v));}
 if(max<.0001f)return 2;
 p.ch[0].mute=true;p.ch[1].mute=true;dsp.configure(p);for(int i=0;i<40;i++)dsp.process(in,out,256);
 // Verify that the analyzer tap reflects the actual post-EQ channel audio.
 // Use independent processors to avoid inheriting filter state.
 auto measure=[](float gainDb){
   storm::Params test;
   test.ch[0].eq[3]=gainDb;
   test.ch[0].eqFreq[3]=1000.f;
   test.ch[0].eqQ[3]=2.f;
   storm::Processor processor;
   processor.configure(test);
   float input[512]{},output[512]{},tap[256]{};
   double sum=0;int count=0;
   for(int block=0;block<80;block++){
     for(int n=0;n<256;n++){
       input[n*2]=.08f*std::sin(2*3.14159265358979323846*1000*(block*256+n)/48000.);
       input[n*2+1]=0;
     }
     processor.process(input,output,256,tap,0);
     if(block>=60)for(float v:tap){sum+=v*v;count++;}
   }
   return std::sqrt(sum/count);
 };
 const double flat=measure(0),boosted=measure(12);
 if(!(boosted>flat*2.5)){
   std::fprintf(stderr,"Post-EQ analyzer regression: flat=%g boosted=%g\n",flat,boosted);
   return 3;
 }
 std::printf("POST-EQ ANALYZER PASS: flat=%g boosted=%g\n",flat,boosted);
 std::printf("DSP PASS: nonzero output %.4f, muted output %.5f\n",max,out[0]);return 0;
}
