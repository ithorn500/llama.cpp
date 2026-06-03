include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

# server-context containing the core server logic, used by llama-server and CLI

set(TARGET server-context)

add_library(${TARGET} STATIC
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
)

if (BUILD_SHARED_LIBS)
    set_target_properties(${TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

target_include_directories(${TARGET} PRIVATE ../mtmd)
target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR})
set(GG_GATEWAY_ROOT /opt/AIGateway CACHE PATH "Gemma Gateway repo root for F-121 server hook")
set(GG_GATEWAY_ORT_DIR "${GG_GATEWAY_ROOT}/vendor/ryzen-ai-ort-1.23" CACHE PATH "ONNX Runtime headers+lib for F-121 NPU projector")
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
target_link_libraries(${TARGET} PUBLIC common mtmd ${CMAKE_THREAD_LIBS_INIT})
if(GG_GATEWAY_ONNXRUNTIME_LIB)
    target_link_libraries(${TARGET} PRIVATE "${GG_GATEWAY_ONNXRUNTIME_LIB}")
    if(OpenMP_CXX_FOUND)
        target_link_libraries(${TARGET} PRIVATE OpenMP::OpenMP_CXX)
    endif()
else()
    message(FATAL_ERROR "F-121 NPU projector requires ONNX Runtime under ${GG_GATEWAY_ORT_DIR}")
endif()


# llama-server executable

set(TARGET llama-server)

set(TARGET_SRCS
    server.cpp
    server-http.cpp
    server-http.h
    server-models.cpp
    server-models.h
)

option(LLAMA_BUILD_WEBUI "Build the embedded Web UI" ON)

if (LLAMA_BUILD_WEBUI)
    set(PUBLIC_ASSETS
        index.html
        bundle.js
        bundle.css
        loading.html
    )

    foreach(asset ${PUBLIC_ASSETS})
        set(input "${CMAKE_CURRENT_SOURCE_DIR}/public/${asset}")
        set(output "${CMAKE_CURRENT_BINARY_DIR}/${asset}.hpp")
        list(APPEND TARGET_SRCS ${output})
        add_custom_command(
            DEPENDS "${input}"
            OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT=${input}" "-DOUTPUT=${output}" -P "${PROJECT_SOURCE_DIR}/scripts/xxd.cmake"
        )
        set_source_files_properties(${output} PROPERTIES GENERATED TRUE)
    endforeach()
    add_definitions(-DLLAMA_BUILD_WEBUI)
else()
endif()

add_executable(${TARGET} ${TARGET_SRCS})
install(TARGETS ${TARGET} RUNTIME)

target_include_directories(${TARGET} PRIVATE ../mtmd)
target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${TARGET} PRIVATE server-context PUBLIC common cpp-httplib ${CMAKE_THREAD_LIBS_INIT})

target_compile_features(${TARGET} PRIVATE cxx_std_17)
