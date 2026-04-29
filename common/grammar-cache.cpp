#include "grammar-cache.h"

#include "grammar-sha256.h"

#include "log.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

static int32_t effective_cap(int32_t explicit_cap) {
    if (explicit_cap >= 0) {
        return explicit_cap;
    }
    const char * e = std::getenv("LLAMA_GRAMMAR_CACHE_SIZE");
    if (e && e[0]) {
        return std::atoi(e);
    }
    return 128;
}

static std::string grammar_cache_make_key(
        const llama_vocab * vocab,
        const std::string & grammar_str,
        bool lazy,
        const char ** trigger_patterns,
        size_t num_trigger_patterns,
        const llama_token * trigger_tokens,
        size_t num_trigger_tokens) {
    std::string blob;
    const uint64_t vp = (uint64_t)(uintptr_t) vocab;
    blob.append(reinterpret_cast<const char *>(&vp), sizeof(vp));
    const uint8_t lz = lazy ? uint8_t(1) : uint8_t(0);
    blob.push_back(char(lz));
    const uint32_t glen = (uint32_t) grammar_str.size();
    blob.append(reinterpret_cast<const char *>(&glen), sizeof(glen));
    blob.append(grammar_str);
    const uint32_t np = (uint32_t) num_trigger_patterns;
    blob.append(reinterpret_cast<const char *>(&np), sizeof(np));
    for (size_t i = 0; i < num_trigger_patterns; ++i) {
        const char * p = trigger_patterns[i] ? trigger_patterns[i] : "";
        const uint32_t plen = (uint32_t) std::strlen(p);
        blob.append(reinterpret_cast<const char *>(&plen), sizeof(plen));
        blob.append(p, plen);
    }
    const uint32_t nt = (uint32_t) num_trigger_tokens;
    blob.append(reinterpret_cast<const char *>(&nt), sizeof(nt));
    if (num_trigger_tokens > 0 && trigger_tokens) {
        blob.append(reinterpret_cast<const char *>(trigger_tokens), num_trigger_tokens * sizeof(llama_token));
    }
    unsigned char digest[SHA256_DIGEST_SIZE];
    sha256_hash(digest, reinterpret_cast<const unsigned char *>(blob.data()), blob.size());
    static const char * hex = "0123456789abcdef";
    std::string key;
    key.reserve(64);
    for (int i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        key.push_back(hex[digest[i] >> 4]);
        key.push_back(hex[digest[i] & 15]);
    }
    return key;
}

struct grammar_cache_slot {
    llama_sampler * tmpl;
};

static std::mutex g_grammar_cache_mu;
static std::list<std::pair<std::string, grammar_cache_slot>> g_grammar_cache_lru;
static std::unordered_map<std::string, decltype(g_grammar_cache_lru)::iterator> g_grammar_cache_map;

struct llama_sampler * common_grammar_cache_get_sampler(
        const llama_vocab * vocab,
                      const char * grammar_str,
                              bool lazy,
                     const char ** trigger_patterns,
                            size_t num_trigger_patterns,
               const llama_token * trigger_tokens,
                            size_t num_trigger_tokens,
                            int32_t grammar_cache_size_explicit) {
    if (!grammar_str || grammar_str[0] == '\0') {
        return nullptr;
    }
    if (std::strncmp(grammar_str, "%llguidance", 11) == 0) {
        return nullptr;
    }

    const int32_t cap = effective_cap(grammar_cache_size_explicit);
    if (cap <= 0) {
        if (lazy) {
            return llama_sampler_init_grammar_lazy_patterns(
                    vocab, grammar_str, "root",
                    trigger_patterns, num_trigger_patterns,
                    trigger_tokens, num_trigger_tokens);
        }
        return llama_sampler_init_grammar(vocab, grammar_str, "root");
    }

    const std::string gstr(grammar_str);
    const std::string key = grammar_cache_make_key(
            vocab, gstr, lazy, trigger_patterns, num_trigger_patterns, trigger_tokens, num_trigger_tokens);

    std::lock_guard<std::mutex> lock(g_grammar_cache_mu);

    auto it = g_grammar_cache_map.find(key);
    if (it != g_grammar_cache_map.end()) {
        g_grammar_cache_lru.splice(g_grammar_cache_lru.begin(), g_grammar_cache_lru, it->second);
        llama_sampler * tmpl = it->second->second.tmpl;
        llama_sampler * cl = llama_sampler_clone(tmpl);
        if (!cl) {
            LOG_ERR("%s: llama_sampler_clone failed for grammar cache hit\n", __func__);
        }
        return cl;
    }

    llama_sampler * tmpl = nullptr;
    if (lazy) {
        tmpl = llama_sampler_init_grammar_lazy_patterns(
                vocab, grammar_str, "root",
                trigger_patterns, num_trigger_patterns,
                trigger_tokens, num_trigger_tokens);
    } else {
        tmpl = llama_sampler_init_grammar(vocab, grammar_str, "root");
    }
    if (!tmpl) {
        return nullptr;
    }

    grammar_cache_slot slot{ tmpl };
    g_grammar_cache_lru.push_front(std::make_pair(key, slot));
    g_grammar_cache_map[key] = g_grammar_cache_lru.begin();

    while ((int32_t) g_grammar_cache_map.size() > cap) {
        auto last = g_grammar_cache_lru.end();
        --last;
        llama_sampler_free(last->second.tmpl);
        g_grammar_cache_map.erase(last->first);
        g_grammar_cache_lru.pop_back();
    }

    llama_sampler * cl = llama_sampler_clone(tmpl);
    if (!cl) {
        LOG_ERR("%s: llama_sampler_clone failed after grammar cache insert\n", __func__);
    }
    return cl;
}
