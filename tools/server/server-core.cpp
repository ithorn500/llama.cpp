#include "server-core.h"

#include "server-context.h"
#include "server-http.h"

#include <algorithm>
#include <cctype>
#include <cstring>

using handler_t = server_http_context::handler_t;

namespace {

thread_local std::function<bool()> tls_should_stop;

static void parse_query_string(const std::string & qs, std::map<std::string, std::string> & out) {
    size_t start = 0;
    while (start < qs.size()) {
        size_t amp = qs.find('&', start);
        std::string pair = qs.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        start = amp == std::string::npos ? qs.size() : amp + 1;
        const auto eq = pair.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        out.emplace(pair.substr(0, eq), pair.substr(eq + 1));
    }
}

static server_http_req build_http_req(const llama_server_core::dispatch_request & dr, const std::function<bool()> & stop) {
    std::string path = dr.path;
    std::string qs;
    const auto qpos = path.find('?');
    if (qpos != std::string::npos) {
        qs = path.substr(qpos + 1);
        path = path.substr(0, qpos);
    }
    std::map<std::string, std::string> params;
    parse_query_string(qs, params);

    tls_should_stop = stop;

    return server_http_req{
            std::move(params),
            dr.headers,
            std::move(path),
            std::move(qs),
            dr.body,
            {},
            tls_should_stop};
}

static llama_server_core::dispatch_response convert(
        server_http_res_ptr res,
        const std::function<void(const std::string &)> * stream_chunk) {
    llama_server_core::dispatch_response out;
    if (!res) {
        out.status = 404;
        out.body = R"({"error":{"message":"no handler","type":"not_found_error","code":404}})";
        return out;
    }
    out.status = res->status;
    out.content_type = res->content_type;
    out.headers = res->headers;

    if (!res->is_stream()) {
        out.body = std::move(res->data);
        return out;
    }

    std::string acc;
    std::string chunk;
    while (res->next(chunk)) {
        if (stream_chunk != nullptr && !chunk.empty()) {
            (*stream_chunk)(chunk);
        }
        acc += chunk;
    }
    out.body = std::move(acc);
    return out;
}

static llama_server_core::dispatch_response call_handler(
        handler_t fn,
        server_http_req & req,
        const std::function<void(const std::string &)> * stream_chunk) {
    try {
        return convert(fn(req), stream_chunk);
    } catch (const std::invalid_argument & e) {
        llama_server_core::dispatch_response r;
        r.status = 400;
        r.body = std::string("{\"error\":{\"message\":\"") + e.what() + "\",\"type\":\"invalid_request_error\",\"code\":400}}";
        return r;
    } catch (const std::exception & e) {
        llama_server_core::dispatch_response r;
        r.status = 500;
        r.body = std::string("{\"error\":{\"message\":\"") + e.what() + "\",\"type\":\"server_error\",\"code\":500}}";
        return r;
    } catch (...) {
        llama_server_core::dispatch_response r;
        r.status = 500;
        r.body = R"({"error":{"message":"unknown error","type":"server_error","code":500}})";
        return r;
    }
}

static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}

}  // namespace

namespace llama_server_core {

static dispatch_response dispatch_with_chunk_sink(
        server_routes & routes,
        const dispatch_request & req,
        const std::function<bool()> & should_stop,
        const std::function<void(const std::string &)> * stream_chunk) {
    auto http_req = build_http_req(req, should_stop);
    std::string method = upper(req.method);
    std::string path = http_req.path;

    // --- GET ---
    if (method == "GET") {
        if (path == "/health" || path == "/v1/health") {
            return call_handler(routes.get_health, http_req, stream_chunk);
        }
        if (path == "/metrics") {
            return call_handler(routes.get_metrics, http_req, stream_chunk);
        }
        if (path == "/props") {
            return call_handler(routes.get_props, http_req, stream_chunk);
        }
        if (path == "/models" || path == "/v1/models") {
            return call_handler(routes.get_models, http_req, stream_chunk);
        }
        if (path == "/slots") {
            return call_handler(routes.get_slots, http_req, stream_chunk);
        }
        if (path == "/lora-adapters") {
            return call_handler(routes.get_lora_adapters, http_req, stream_chunk);
        }
        dispatch_response r;
        r.status = 404;
        r.body = R"({"error":{"message":"route not found","type":"not_found_error","code":404}})";
        return r;
    }

    // --- POST ---
    if (method == "POST") {
        if (path == "/props") {
            return call_handler(routes.post_props, http_req, stream_chunk);
        }
        if (path == "/completion") {
            return call_handler(routes.post_completions, http_req, stream_chunk);
        }
        if (path == "/completions") {
            return call_handler(routes.post_completions, http_req, stream_chunk);
        }
        if (path == "/v1/completions") {
            return call_handler(routes.post_completions_oai, http_req, stream_chunk);
        }
        if (path == "/chat/completions" || path == "/v1/chat/completions") {
            return call_handler(routes.post_chat_completions, http_req, stream_chunk);
        }
        if (path == "/v1/responses" || path == "/responses") {
            return call_handler(routes.post_responses_oai, http_req, stream_chunk);
        }
        if (path == "/v1/audio/transcriptions" || path == "/audio/transcriptions") {
            return call_handler(routes.post_transcriptions_oai, http_req, stream_chunk);
        }
        if (path == "/v1/messages") {
            return call_handler(routes.post_anthropic_messages, http_req, stream_chunk);
        }
        if (path == "/v1/messages/count_tokens") {
            return call_handler(routes.post_anthropic_count_tokens, http_req, stream_chunk);
        }
        if (path == "/infill") {
            return call_handler(routes.post_infill, http_req, stream_chunk);
        }
        if (path == "/embedding" || path == "/embeddings") {
            return call_handler(routes.post_embeddings, http_req, stream_chunk);
        }
        if (path == "/v1/embeddings") {
            return call_handler(routes.post_embeddings_oai, http_req, stream_chunk);
        }
        if (path == "/rerank" || path == "/reranking" || path == "/v1/rerank" || path == "/v1/reranking") {
            return call_handler(routes.post_rerank, http_req, stream_chunk);
        }
        if (path == "/tokenize") {
            return call_handler(routes.post_tokenize, http_req, stream_chunk);
        }
        if (path == "/detokenize") {
            return call_handler(routes.post_detokenize, http_req, stream_chunk);
        }
        if (path == "/apply-template") {
            return call_handler(routes.post_apply_template, http_req, stream_chunk);
        }
        if (path == "/lora-adapters") {
            return call_handler(routes.post_lora_adapters, http_req, stream_chunk);
        }

        // POST /slots/:id_slot — mirror ``server.cpp`` registration (path param id_slot).
        if (path.size() > 7 && path.compare(0, 7, "/slots/") == 0) {
            const std::string rest = path.substr(7);
            if (!rest.empty() && rest.find('/') == std::string::npos) {
                http_req.params["id_slot"] = rest;
                return call_handler(routes.post_slots, http_req, stream_chunk);
            }
        }

        dispatch_response r;
        r.status = 404;
        r.body = R"({"error":{"message":"route not found","type":"not_found_error","code":404}})";
        return r;
    }

    dispatch_response r;
    r.status = 405;
    r.body = R"({"error":{"message":"method not allowed","type":"invalid_request_error","code":405}})";
    return r;
}

dispatch_response dispatch(
        server_routes & routes,
        const dispatch_request & req,
        const std::function<bool()> & should_stop) {
    return dispatch_with_chunk_sink(routes, req, should_stop, nullptr);
}

dispatch_response dispatch_streaming(
        server_routes & routes,
        const dispatch_request & req,
        const std::function<bool()> & should_stop,
        const std::function<void(const std::string &)> & on_chunk) {
    return dispatch_with_chunk_sink(routes, req, should_stop, &on_chunk);
}

}  // namespace llama_server_core
