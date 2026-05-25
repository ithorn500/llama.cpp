include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

# server-context containing the core server logic, used by llama-server and CLI

set(TARGET server-context)

add_library(${TARGET} STATIC
    server-chat.cpp
    server-chat.h
    server-task.cpp
    server-task.h
    server-queue.cpp
    server-queue.h
    server-common.cpp
    server-common.h
    server-context.cpp
    server-context.h
    server-tools.cpp
    server-tools.h
    server-core.cpp
    server-core.h
)

if (BUILD_SHARED_LIBS)
    set_target_properties(${TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

target_include_directories(${TARGET} PRIVATE ../mtmd)
target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR})
set(GG_GATEWAY_ROOT /opt/AIGateway CACHE PATH "Gemma Gateway repo root for Epic514 server hook")
set(GG_GATEWAY_ORT_DIR "${GG_GATEWAY_ROOT}/vendor/ryzen-ai-ort-1.23" CACHE PATH "ONNX Runtime headers+lib for Epic514 NPU projector")
find_library(GG_GATEWAY_ONNXRUNTIME_LIB onnxruntime PATHS "${GG_GATEWAY_ORT_DIR}/lib" NO_DEFAULT_PATH)
find_package(OpenMP COMPONENTS CXX QUIET)
target_compile_definitions(${TARGET} PRIVATE
    GG_GATEWAY_EPIC514_SERVER_MTMD=1
    GEMMA_ENGINE_EPIC514_NPU_MTMD=1
    GEMMA_ENGINE_EPIC514_ORT_LINKED=1
    GEMMA_ENGINE_EPIC514_ORT_OPENMP=$<BOOL:${OpenMP_CXX_FOUND}>)
target_include_directories(${TARGET} PRIVATE
    "${GG_GATEWAY_ROOT}/native"
    "${GG_GATEWAY_ROOT}/native/engine/include"
    "${GG_GATEWAY_ROOT}/native/engine/.."
    "${GG_GATEWAY_ORT_DIR}/include")
target_sources(${TARGET} PRIVATE
    "${GG_GATEWAY_ROOT}/native/engine/src/epic514_server_mtmd_hook.cpp"
    "${GG_GATEWAY_ROOT}/native/engine/src/epic514_ort_projector.cpp")
target_link_libraries(${TARGET} PUBLIC llama-common mtmd ${CMAKE_THREAD_LIBS_INIT})
if(GG_GATEWAY_ONNXRUNTIME_LIB)
    target_link_libraries(${TARGET} PRIVATE "${GG_GATEWAY_ONNXRUNTIME_LIB}")
    if(OpenMP_CXX_FOUND)
        target_link_libraries(${TARGET} PRIVATE OpenMP::OpenMP_CXX)
    endif()
else()
    message(FATAL_ERROR "Epic514 NPU projector requires ONNX Runtime under ${GG_GATEWAY_ORT_DIR}")
endif()

# Gemma Gateway F-005 — publish transport-free core as libllama-server-core.a (path expected by
# scripts/ci/check_server_core_link_gate.sh). Same objects as target server-context; excludes
# server-http.cpp / server.cpp (httplib). Further extraction (dispatch API, pybind) tracks F-005.
set_target_properties(${TARGET} PROPERTIES ARCHIVE_OUTPUT_NAME llama-server-core)


# llama-server executable

set(TARGET llama-server)

set(TARGET_SRCS
    server.cpp
    server-http.cpp
    server-http.h
    server-models.cpp
    server-models.h
)

# Option to specify custom HF bucket for webui (defaults to llama-ui)
# Usage: cmake -B build -DLLAMA_WEBUI_HF_BUCKET=llama-ui
set(LLAMA_WEBUI_HF_BUCKET "llama-ui" CACHE STRING "Hugging Face bucket name for prebuilt webui assets")

if (LLAMA_BUILD_WEBUI)
    set(PUBLIC_ASSETS
        index.html
        bundle.js
        bundle.css
        loading.html
    )

    # Determine source of webui assets (priority: local > HF Bucket)
    set(WEBUI_SOURCE "")
    set(WEBUI_SOURCE_DIR "")

    # Priority 1: Check for local webui build output
    set(LOCAL_WEBUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/public")

    # Verify all required assets exist before declaring local source valid
    set(ALL_ASSETS_PRESENT TRUE)
    foreach(asset ${PUBLIC_ASSETS})
        if(NOT EXISTS "${LOCAL_WEBUI_DIR}/${asset}")
            set(ALL_ASSETS_PRESENT FALSE)
            break()
        endif()
    endforeach()

    if(ALL_ASSETS_PRESENT)
        set(WEBUI_SOURCE "local")
        set(WEBUI_SOURCE_DIR "${LOCAL_WEBUI_DIR}")
        message(STATUS "WebUI: using local build from ${WEBUI_SOURCE_DIR}")
    endif()

    # Priority 2: Build-time asset provisioning (npm build → HF Bucket fallback)
    if(NOT WEBUI_SOURCE_DIR)
        # Environment variable takes precedence (e.g., from CI workflows)
        if(DEFINED ENV{HF_WEBUI_VERSION})
            set(HF_WEBUI_VERSION "$ENV{HF_WEBUI_VERSION}")
            # Validate against allowed characters to prevent CMake list separator
            # or path-traversal issues in stamp filenames and download URLs
            if(NOT HF_WEBUI_VERSION MATCHES "^[A-Za-z0-9._-]+$")
                message(FATAL_ERROR "WebUI: invalid HF_WEBUI_VERSION='${HF_WEBUI_VERSION}' - must match ^[A-Za-z0-9._-]+$")
            endif()
            message(STATUS "WebUI: using HF_WEBUI_VERSION from environment=${HF_WEBUI_VERSION}")
        elseif(DEFINED LLAMA_BUILD_NUMBER)
            set(HF_WEBUI_VERSION "b${LLAMA_BUILD_NUMBER}")
            message(STATUS "WebUI: using LLAMA_BUILD_NUMBER=${HF_WEBUI_VERSION}")
        else()
            set(HF_WEBUI_VERSION "")
            message(STATUS "WebUI: version not specified (will use HF 'latest')")
        endif()

        # Stamp file embeds the version tag so a changed build number triggers
        # a fresh provision run on the next `cmake --build` without reconfiguring.
        if("${HF_WEBUI_VERSION}" STREQUAL "")
            set(WEBUI_VERSION_TAG "provisioned")
        else()
            set(WEBUI_VERSION_TAG "${HF_WEBUI_VERSION}")
        endif()
        set(WEBUI_STAMP "${CMAKE_CURRENT_BINARY_DIR}/.webui-${WEBUI_VERSION_TAG}.stamp")

        # Join assets with + separator (safe across all platforms, unlike ; and |)
        string(REPLACE ";" "+" PUBLIC_ASSETS_JOINED "${PUBLIC_ASSETS}")

        add_custom_command(
            OUTPUT ${WEBUI_STAMP}
            COMMAND ${CMAKE_COMMAND}
                "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DPUBLIC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/public"
                "-DHF_BUCKET=${LLAMA_WEBUI_HF_BUCKET}"
                "-DHF_VERSION=${HF_WEBUI_VERSION}"
                "-DHF_ENABLED=${LLAMA_USE_PREBUILT_WEBUI}"
                "-DASSETS=${PUBLIC_ASSETS_JOINED}"
                "-DSTAMP_FILE=${WEBUI_STAMP}"
                "-DNPM_DIR=${CMAKE_CURRENT_SOURCE_DIR}/webui"
                -P ${PROJECT_SOURCE_DIR}/scripts/webui-download.cmake
            COMMENT "Building/provisioning WebUI assets (npm build -> HF Bucket fallback)"
        )

        set(WEBUI_SOURCE "provisioned")
        set(WEBUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/public")
    endif()

    # Process assets from the determined source
    if(WEBUI_SOURCE_DIR)
        foreach(asset ${PUBLIC_ASSETS})
            set(input "${WEBUI_SOURCE_DIR}/${asset}")
            set(output "${CMAKE_CURRENT_BINARY_DIR}/${asset}.hpp")
            list(APPEND TARGET_SRCS ${output})

            if(WEBUI_SOURCE STREQUAL "local")
                # Local build: files exist at configure time
                if(NOT EXISTS "${input}")
                    message(FATAL_ERROR "WebUI asset not found: ${input}")
                endif()
                set(dependency "${input}")
            else()
                # HF Bucket: files are downloaded at build time
                set(dependency "${WEBUI_STAMP}")
            endif()

            add_custom_command(
                DEPENDS ${dependency}
                OUTPUT "${output}"
                COMMAND "${CMAKE_COMMAND}" "-DINPUT=${input}" "-DOUTPUT=${output}" -P "${PROJECT_SOURCE_DIR}/scripts/xxd.cmake"
            )
            set_source_files_properties(${output} PROPERTIES GENERATED TRUE)
        endforeach()

        add_definitions(-DLLAMA_BUILD_WEBUI)
        add_definitions(-DLLAMA_WEBUI_DEFAULT_ENABLED=1)
        message(STATUS "WebUI: embedded with source: ${WEBUI_SOURCE}")
    else()
        # WebUI source not found - issue warning but don't fail the build
        # The server will still build but without webui embedded
        message(WARNING "WebUI: no source available. Neither local build (tools/server/public/) nor HF Bucket download succeeded.")
        message(WARNING "WebUI: building server without embedded WebUI. Set LLAMA_BUILD_WEBUI=OFF to suppress this warning.")
        add_definitions(-DLLAMA_WEBUI_DEFAULT_ENABLED=0)
    endif()
else()
    # WebUI is disabled at build time
    add_definitions(-DLLAMA_WEBUI_DEFAULT_ENABLED=0)
endif()

add_executable(${TARGET} ${TARGET_SRCS})
install(TARGETS ${TARGET} RUNTIME)

target_include_directories(${TARGET} PRIVATE ../mtmd)
target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${TARGET} PRIVATE server-context PUBLIC llama-common cpp-httplib ${CMAKE_THREAD_LIBS_INIT})

target_compile_features(${TARGET} PRIVATE cxx_std_17)
