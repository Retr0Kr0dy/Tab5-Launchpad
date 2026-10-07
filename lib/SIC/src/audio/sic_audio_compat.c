#include <stddef.h>
#include <stdint.h>
#include "sic/audio.h"
#include "sic/audio_compat.h"

// Avoid macro recursion inside this TU
#ifdef sic_mic_read
#undef sic_mic_read
#endif

int sic_mic_read_compat(int16_t* pcm, int n){
  if (!pcm || n<=0) return -1;
  return sic_mic_read(pcm, (size_t)n, 20);
}

int sic_amp_enable(int on){
  (void)on;
  return 0; // no-op stub; return success
}
