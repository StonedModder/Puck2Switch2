#include "motion_fusion.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
using namespace s2s;
static int failures=0;
static void check(bool ok,const char* name,double v,double limit) {
  std::printf("%s %s: %.6f (limit %.6f)\n",ok?"PASS":"FAIL",name,v,limit);
  failures+=!ok;
}
// Independent copy of the v1.1.0 update: unconditional correction, no bias.
struct Legacy {
  float q[4]={1,0,0,0}, integral[3]={0,0,0};
  void update(const float g[3],const float a[3],float dt) {
    float n=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
    if(n<.01f)return;
    float ax=a[0]/n,ay=a[1]/n,az=a[2]/n;
    float w=q[0],x=q[1],y=q[2],z=q[3];
    float vx=2*(x*z-w*y),vy=2*(w*x+y*z),vz=w*w-x*x-y*y+z*z;
    float e[3]={ay*vz-az*vy,az*vx-ax*vz,ax*vy-ay*vx};
    float r[3]; for(int i=0;i<3;i++){integral[i]+=.01f*e[i]*dt;r[i]=g[i]*.017453292519943295f+.5f*e[i]+integral[i];}
    float h=dt*.5f;
    float t[4]={w+h*(-x*r[0]-y*r[1]-z*r[2]),x+h*(w*r[0]+y*r[2]-z*r[1]),y+h*(w*r[1]-x*r[2]+z*r[0]),z+h*(w*r[2]+x*r[1]-y*r[0])};
    n=std::sqrt(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]+t[3]*t[3]);
    for(int i=0;i<4;i++)q[i]=t[i]/n;
  }
};
static void identity(MahonyFilter &f){f.q[0]=1;f.q[1]=f.q[2]=f.q[3]=0;}
static float angle(const float *q){return 2*std::atan2(std::sqrt(q[1]*q[1]+q[2]*q[2]+q[3]*q[3]),std::fabs(q[0]))*57.2957795f;}
static float yaw(const float *q){return std::atan2(2*(q[0]*q[3]+q[1]*q[2]),1-2*(q[2]*q[2]+q[3]*q[3]))*57.2957795f;}
int main(){
  const float a[3]={0,0,1}, b[3]={2,2,2};
  Legacy old; MotionPipeline fixed; identity(fixed.attitude);
  for(int i=0;i<1500;i++){old.update(b,a,.02f);fixed.update(b,a,.02f);}
  std::printf("STATIC 30s bias (2,2,2) dps: before=%.4f deg after=%.4f deg, estimated=(%.3f,%.3f,%.3f) dps\n",angle(old.q),angle(fixed.attitude.q),fixed.bias.bias[0],fixed.bias.bias[1],fixed.bias.bias[2]);
  check(angle(old.q)>30,"static baseline drift",angle(old.q),30);
  check(angle(fixed.attitude.q)<2,"static error",angle(fixed.attitude.q),2);
  check(fixed.bias.calibrated,"bias acquired",fixed.bias.calibrated,1);
  // 90-degree yaw in 0.5 s; 0.8g lateral load for duration. Then
  // no rotation but same load for 0.25s, followed by gravity only for 1s.
  Legacy o; MahonyFilter f; identity(f); float prev=0,rev=0;
  const float turn[3]={0,0,180}, zero[3]={0,0,0}, disturbed[3]={.8f,0,1};
  for(int i=0;i<25;i++){o.update(turn,disturbed,.02f);f.update(turn,disturbed,.02f);float y=yaw(f.q);rev=std::max(rev,prev-y);prev=y;}
  float beforeyaw=yaw(o.q), afteryaw=yaw(f.q);
  for(int i=0;i<12;i++){o.update(zero,disturbed,.02f);f.update(zero,disturbed,.02f);float y=yaw(f.q);rev=std::max(rev,prev-y);prev=y;}
  // Tilt induced by false gravity shows the direction the filter later undoes.
  float beforetilt=std::sqrt(o.q[1]*o.q[1]+o.q[2]*o.q[2])*114.591559f;
  float aftertilt=std::sqrt(f.q[1]*f.q[1]+f.q[2]*f.q[2])*114.591559f;
  for(int i=0;i<50;i++){o.update(zero,a,.02f);f.update(zero,a,.02f);float y=yaw(f.q);rev=std::max(rev,prev-y);prev=y;}
  std::printf("SWING 90deg: yaw before=%.4f after=%.4f deg; spurious tilt before=%.4f after=%.4f deg; max yaw reversal=%.6f deg\n",beforeyaw,afteryaw,beforetilt,aftertilt,rev);
  check(std::fabs(afteryaw-90)<1,"swing yaw error",std::fabs(afteryaw-90),1);
  check(rev<.05,"swing max yaw reversal",rev,.05);
  check(aftertilt<beforetilt*.1f,"spurious tilt reduction",aftertilt,beforetilt*.1f);
  // Independent seeded sensor noise and random interarrival intervals.
  std::mt19937 rng(12345); std::normal_distribution<float> noise(0,.20f); std::uniform_real_distribution<float> jitter(.007f,.045f);
  MotionPipeline p; identity(p.attitude); float t=0,maxerr=0;
  while(t<30){float dt=jitter(rng);t+=dt;float g[3]={2+noise(rng),2+noise(rng),2+noise(rng)};p.update(g,a,dt);maxerr=std::max(maxerr,angle(p.attitude.q));}
  std::printf("JITTER 30s 7-45ms, noise sigma .2 dps: final=%.4f max=%.4f deg\n",angle(p.attitude.q),maxerr);
  check(maxerr<2.5f,"jitter worst attitude error",maxerr,2.5);
  // Compatibility: v1.1.0 advances the 12-bit tick by exactly the emitted
  // saturated 4-bit interval. Preserve that relationship across long gaps.
  MotionClock clock;
  auto elapsed = clock.advance(.030f);
  unsigned delta=clock.tick;
  check(elapsed==15 && delta==15,"30ms timestamp increment matches 4-bit field",delta,15);
  clock.tick=4090;
  elapsed=clock.advance(.020f);
  check(elapsed==15 && clock.tick==9,"timestamp wraps consistently",clock.tick,9);
  // Golden bytes obtained independently from the unmodified encoder.
  const uint8_t golden[30]={0xBC,0xFA,0,0x0C,0,0xA8,0x17,0x15,0x03,0,0,0,0x01,0,0,0x80,0xCE,0x04,0xAF,0x01,0x64,0xF6,0xA1,0xFC,0x90,0xAD,0xFE,0x0F,0,0x02};
  const float quat[4]={.92387953f,.38268343f,0,0},acc[3]={.1f,-.2f,.95f}; uint8_t bytes[30]; Ns2MotionEncoder enc; enc.build(bytes,acc,0xABC,15,quat);
  int diffs=0;for(int i=0;i<30;i++)diffs+=bytes[i]!=golden[i];
  check(diffs==0,"30-byte original golden differences",diffs,0);
  // Gravity invalid must not suppress gyro integration.
  MahonyFilter fall;identity(fall);const float noacc[3]={0,0,0};fall.update(turn,noacc,.1f);
  check(yaw(fall.q)>17,"free-fall integrates gyro",yaw(fall.q),17);
  // Once calibrated a real slow pan must not be absorbed into the bias.
  float prior=p.bias.bias[2];const float slow[3]={2,2,3};for(int i=0;i<500;i++)p.update(slow,a,.02f);
  check(std::fabs(p.bias.bias[2]-prior)<.001f,"bias frozen during slow pan",std::fabs(p.bias.bias[2]-prior),.001);
  std::printf("RESULT %s (%d failures)\n",failures?"FAIL":"PASS",failures);
  return failures?1:0;
}
