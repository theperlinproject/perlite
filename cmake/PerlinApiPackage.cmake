include_guard(GLOBAL)

if(NOT perlin_api_SOURCE_DIR)
    set(PERLIN_API_SOURCE_DIR "" CACHE PATH "Local perlin-api checkout to build against instead of fetching it")
    set(PERLIN_API_TAG "main" CACHE STRING "perlin-api revision fetched when PERLIN_API_SOURCE_DIR is empty")
    if(PERLIN_API_SOURCE_DIR)
        get_filename_component(perlin_api_SOURCE_DIR "${PERLIN_API_SOURCE_DIR}" ABSOLUTE)
    else()
        include(CPM)
        CPMAddPackage(NAME perlin_api
            GITHUB_REPOSITORY theperlinproject/perlin-api
            GIT_TAG ${PERLIN_API_TAG}
            GIT_SHALLOW TRUE
            DOWNLOAD_ONLY YES)
    endif()
endif()
message(STATUS "perlite: perlin-api at ${perlin_api_SOURCE_DIR}")
include("${perlin_api_SOURCE_DIR}/cmake/PerlinApi.cmake")
# Content mods and the manifest target came with API revision 3.
if(EXISTS "${perlin_api_SOURCE_DIR}/api/70_rev3.papi")
    set(PERLITE_PERLIN_REV3 ON)
else()
    set(PERLITE_PERLIN_REV3 OFF)
    message(STATUS "perlite: perlin-api before revision 3: content mods are not served")
endif()
