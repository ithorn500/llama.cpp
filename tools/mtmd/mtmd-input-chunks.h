#pragma once

/**
 * F-006 — Incremental mono float32 PCM accumulation before mtmd_tokenize().
 *
 * Server code and helpers feed PCM frames via mtmd_input_chunks_append_audio_pcm(),
 * then materialize a mtmd_bitmap with mtmd_audio_pcm_stream_finalize_bitmap()
 * (same logical content as mtmd_bitmap_init_from_audio on one contiguous buffer).
 */

#include "mtmd.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mtmd_audio_pcm_stream mtmd_audio_pcm_stream;

MTMD_API mtmd_audio_pcm_stream * mtmd_audio_pcm_stream_new(void);

MTMD_API void mtmd_audio_pcm_stream_free(mtmd_audio_pcm_stream * st);

/** Append mono f32 PCM samples (same layout as mtmd_bitmap_init_from_audio). */
MTMD_API void mtmd_input_chunks_append_audio_pcm(mtmd_audio_pcm_stream * st, const float * pcm, size_t n_samples);

/** Build audio bitmap from accumulated PCM; destroys and NULLs the stream handle. */
MTMD_API mtmd_bitmap * mtmd_audio_pcm_stream_finalize_bitmap(mtmd_audio_pcm_stream * st);

#ifdef __cplusplus
}
#endif
