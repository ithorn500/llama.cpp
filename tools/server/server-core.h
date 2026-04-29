#pragma once

/** Transport-free dispatch entry point for Gemma Gateway F-005 (see docs/llama-fork-patches/p5-server-core-extraction.md). */

#include <functional>
#include <map>
#include <string>

struct server_routes;

namespace llama_server_core {

struct dispatch_request {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;
};

struct dispatch_response {
    int status = 404;
    std::string body;
    std::string content_type = "application/json; charset=utf-8";
    std::map<std::string, std::string> headers;
};

/** Invoke the same handler lambdas registered by ``tools/server/server.cpp`` against ``server_routes``.
 *  Does not start an HTTP server; omits router-mode proxy / ``server_tools`` / ``cors-proxy`` paths unless extended later. */
dispatch_response dispatch(
        server_routes & routes,
        const dispatch_request & req,
        const std::function<bool()> & should_stop);

}  // namespace llama_server_core
