#pragma once

#include "llama.h"

#include <cstdint>

struct llama_sampler;
struct llama_vocab;

/** LRU cache of compiled grammar samplers (SHA256 key over vocab + grammar + lazy triggers). */
struct llama_sampler * common_grammar_cache_get_sampler(
        const struct llama_vocab * vocab,
                      const char * grammar_str,
                              bool lazy,
                     const char ** trigger_patterns,
                            size_t num_trigger_patterns,
               const llama_token * trigger_tokens,
                            size_t num_trigger_tokens,
                            int32_t grammar_cache_size_explicit);
