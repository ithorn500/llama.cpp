
#ifndef LLAMA_COMMON_ROTATE_BITS_H
#define LLAMA_COMMON_ROTATE_BITS_H

#ifdef _MSC_VER

#include <stdlib.h>

#define ROTL32(v, n) _rotl((v), (n))
#define ROTL64(v, n) _rotl64((v), (n))

#define ROTR32(v, n) _rotr((v), (n))
#define ROTR64(v, n) _rotr64((v), (n))

#else

#include <stdint.h>

#define U8V(v) ((uint8_t)(v) & 0xFFU)
#define U16V(v) ((uint16_t)(v) & 0xFFFFU)
#define U32V(v) ((uint32_t)(v) & 0xFFFFFFFFU)
#define U64V(v) ((uint64_t)(v) & 0xFFFFFFFFFFFFFFFFU)

#define ROTL32(v, n) \
  (U32V((uint32_t)(v) << (n)) | ((uint32_t)(v) >> (32 - (n))))

#define ROTL64(v, n) \
  (U64V((uint64_t)(v) << (n)) | ((uint64_t)(v) >> (64 - (n))))

#define ROTR32(v, n) ROTL32(v, 32 - (n))
#define ROTR64(v, n) ROTL64(v, 64 - (n))

#endif

#endif
