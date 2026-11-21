include_guard(GLOBAL)

if(NOT TARGET conduit::conduit)
    set(PERLITE_CONDUIT_DIR "" CACHE PATH "Local conduit checkout to build instead of fetching it")
    set(PERLITE_CONDUIT_TAG "main" CACHE STRING "conduit revision fetched when PERLITE_CONDUIT_DIR is empty")
    set(CONDUIT_BUILD_TESTS OFF CACHE BOOL "")
    set(CONDUIT_INSTALL OFF CACHE BOOL "")
    set(CONDUIT_D3D12 OFF CACHE BOOL "" FORCE)
    set(CONDUIT_STREAMLINE OFF CACHE BOOL "" FORCE)
    if(PERLITE_CONDUIT_DIR)
        add_subdirectory("${PERLITE_CONDUIT_DIR}" "${CMAKE_BINARY_DIR}/_deps/conduit-build")
    else()
        include(FetchContent)
        FetchContent_Declare(conduit GIT_REPOSITORY https://github.com/theperlinproject/conduit.git GIT_TAG ${PERLITE_CONDUIT_TAG} GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(conduit)
    endif()
endif()

set(PERLITE_SLANGC "" CACHE FILEPATH "slangc to compile the terrain shader with; fetched when empty")
if(NOT PERLITE_SLANGC)
    set(_slang_version 2026.20)
    if(CMAKE_HOST_WIN32)
        set(_slang_os windows)
        set(_slang_ext zip)
    elseif(CMAKE_HOST_APPLE)
        set(_slang_os macos)
        set(_slang_ext zip)
    else()
        set(_slang_os linux)
        set(_slang_ext tar.gz)
    endif()
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
        set(_slang_arch aarch64)
    else()
        set(_slang_arch x86_64)
    endif()
    include(FetchContent)
    FetchContent_Declare(slang URL "https://github.com/shader-slang/slang/releases/download/v${_slang_version}/slang-${_slang_version}-${_slang_os}-${_slang_arch}.${_slang_ext}")
    FetchContent_MakeAvailable(slang)
    set(PERLITE_SLANGC "${slang_SOURCE_DIR}/bin/slangc${CMAKE_EXECUTABLE_SUFFIX}")
endif()

set(_gpu_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/world/level/levelgen/gpu")
set(_gpu_out "${CMAKE_CURRENT_BINARY_DIR}/gpu")
file(WRITE "${_gpu_out}/slots.json" "{}")
set(PERLITE_TERRAIN_PACK_CPP "${_gpu_out}/terrain_pack.cpp")
add_custom_command(
    OUTPUT "${_gpu_out}/terrain.pack" "${PERLITE_TERRAIN_PACK_CPP}"
    COMMAND "${Python3_EXECUTABLE}" -I "${CONDUIT_PACK_SHADERS}" --slangc "${PERLITE_SLANGC}"
            --slots "${_gpu_out}/slots.json" --targets spirv
            -o "${_gpu_out}/terrain.pack" "${_gpu_dir}/shaders/terrain_flat.slang" "${_gpu_dir}/shaders/terrain_vanilla.slang"
    COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_bytes.py" "${_gpu_out}/terrain.pack"
            "${PERLITE_TERRAIN_PACK_CPP}" kTerrainShaderPack
    DEPENDS "${_gpu_dir}/shaders/terrain_flat.slang" "${_gpu_dir}/shaders/terrain_vanilla.slang"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_bytes.py" "${CONDUIT_PACK_SHADERS}"
    COMMENT "Compiling the terrain compute shader" VERBATIM)
