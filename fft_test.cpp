#include <array>
#include <complex>
#include <cmath>
#include <algorithm>
#include <cstdio>
int main(){
 constexpr int N=8192;constexpr double pi=3.14159265358979323846,fs=48000.;
 for(double tone:{60.,1000.,10000.}){
  std::array<std::complex<float>,N> bins{};
  for(int i=0;i<N;i++)bins[i]={float(.5*(1.-std::cos(2*pi*i/(N-1)))*std::sin(2*pi*tone*i/fs)),0};
  for(int i=1,j=0;i<N;i++){int bit=N/2;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(bins[i],bins[j]);}
  for(int len=2;len<=N;len<<=1){auto wlen=std::polar(1.f,float(-2*pi/len));for(int i=0;i<N;i+=len){std::complex<float> w(1,0);for(int j=0;j<len/2;j++){auto u=bins[i+j],v=bins[i+j+len/2]*w;bins[i+j]=u+v;bins[i+j+len/2]=u-v;w*=wlen;}}}
  int peak=1;for(int j=2;j<=N/2;j++)if(std::norm(bins[j])>std::norm(bins[peak]))peak=j;
  double hz=peak*fs/N;
  std::printf("tone %.1f Hz -> peak %.2f Hz\n",tone,hz);
  if(std::abs(hz-tone)>fs/N)return 1;
 }
 return 0;
}
