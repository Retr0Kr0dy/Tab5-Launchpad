#include "sic_audio.h"
#include <math.h>
#include <string.h>

size_t sic_audio_upsample16to48(const int16_t* in, size_t n_in, int16_t* out, size_t out_cap){
  if (!in || !out || !n_in) return 0;
  if (out_cap < n_in*3) n_in = out_cap/3;
  if (!n_in) return 0;
  for (size_t i=0;i<n_in-1;i++){
    int16_t s0 = in[i], s1 = in[i+1];
    out[3*i+0] = s0;
    out[3*i+1] = (int16_t)((2*s0 + s1)/3);
    out[3*i+2] = (int16_t)((s0 + 2*s1)/3);
  }
  out[3*(n_in-1)+0] = in[n_in-1];
  out[3*(n_in-1)+1] = in[n_in-1];
  out[3*(n_in-1)+2] = in[n_in-1];
  return n_in*3;
}

void sic_audio_postprocess(int16_t* s, size_t n){
  if (!s || !n) return;
  float y=0, xprev=0, yprev=0;
  const float R = 0.995f;
  for (size_t i=0;i<n;i++){
    float x = (float)s[i];
    y = R*yprev + x - xprev;
    xprev = x; yprev = y;
    int32_t v = (int32_t)(y * 1.6f);
    if (v >  32767) v= 32767;
    if (v < -32768) v=-32768;
    s[i] = (int16_t)v;
  }
}
