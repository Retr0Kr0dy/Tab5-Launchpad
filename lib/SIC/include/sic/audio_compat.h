#pragma once
#include "audio.h"
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Legacy compatibility helpers.
 *
 * The umbrella header <sic/sic.h> includes this file, so this header must not
 * force every target to define microphone pins.  Only expose the old 1-arg
 * sic_mic_start() / 2-arg sic_mic_read() macros when the build target actually
 * provided the legacy PDM pin macros.  New portable code should prefer typed
 * drivers (sic_mic(0)->v->start/read) or the explicit low-level API.
 */
#ifndef MIC_SLOT_RIGHT
#define MIC_SLOT_RIGHT 1
#endif

#ifndef SIC_AUDIO_NO_COMPAT

#if defined(SIC_MIC_CLK) && defined(SIC_MIC_DATA)
#define sic_mic_start(hz) sic_mic_open(SIC_MIC_CLK, SIC_MIC_DATA, (hz), MIC_SLOT_RIGHT)
int sic_mic_read_compat(int16_t* pcm, int n);
#define sic_mic_read(pcm, n) sic_mic_read_compat((pcm), (n))
#endif

/* No-op by default; replace with/use a real amp_t driver when available. */
int sic_amp_enable(int on);

#endif /* SIC_AUDIO_NO_COMPAT */

#ifdef __cplusplus
}
#endif
