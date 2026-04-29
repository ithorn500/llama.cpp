#include "mtmd-input-chunks.h"

#include <cstring>
#include <vector>

struct mtmd_audio_pcm_stream {
    std::vector<float> samples;
};

mtmd_audio_pcm_stream * mtmd_audio_pcm_stream_new() {
    return new mtmd_audio_pcm_stream{};
}

void mtmd_audio_pcm_stream_free(mtmd_audio_pcm_stream * st) {
    delete st;
}

void mtmd_input_chunks_append_audio_pcm(mtmd_audio_pcm_stream * st, const float * pcm, size_t n_samples) {
    if (!st || !pcm || n_samples == 0) {
        return;
    }
    const size_t off = st->samples.size();
    st->samples.resize(off + n_samples);
    std::memcpy(st->samples.data() + off, pcm, n_samples * sizeof(float));
}

mtmd_bitmap * mtmd_audio_pcm_stream_finalize_bitmap(mtmd_audio_pcm_stream * st) {
    if (!st) {
        return nullptr;
    }
    mtmd_bitmap * bmp = mtmd_bitmap_init_from_audio(st->samples.size(), st->samples.data());
    delete st;
    return bmp;
}
