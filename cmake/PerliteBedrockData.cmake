include_guard(GLOBAL)

set(PERLITE_MINECRAFT_DATA_DIR "" CACHE PATH "Local PrismarineJS/minecraft-data bedrock version folder to use instead of downloading it")
set(PERLITE_MINECRAFT_DATA_COMMIT "68c9e840e127858e9548b0d477b992202120dfaa" CACHE STRING "PrismarineJS/minecraft-data revision")
set(PERLITE_MINECRAFT_DATA_VERSION "1.26.60")

set(_perlite_md_files
    "items.json|9f9754125015a95a48b97cc82fb7e642f6b1c492fe406c1722a08c6fa541a243"
    "blocks.json|4591c34c981b1a4f4845e1d06959cf40e6499c7504f6c0e23fb800fd9d66d3e9")

if(PERLITE_MINECRAFT_DATA_DIR)
    get_filename_component(_perlite_md_dir "${PERLITE_MINECRAFT_DATA_DIR}" ABSOLUTE)
else()
    if(DEFINED ENV{CPM_SOURCE_CACHE} AND NOT "$ENV{CPM_SOURCE_CACHE}" STREQUAL "")
        set(_perlite_md_dir "$ENV{CPM_SOURCE_CACHE}/minecraft_data/${PERLITE_MINECRAFT_DATA_COMMIT}")
    elseif(CPM_SOURCE_CACHE)
        set(_perlite_md_dir "${CPM_SOURCE_CACHE}/minecraft_data/${PERLITE_MINECRAFT_DATA_COMMIT}")
    else()
        set(_perlite_md_dir "${CMAKE_BINARY_DIR}/_deps/minecraft_data")
    endif()
    foreach(entry IN LISTS _perlite_md_files)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 file)
        list(GET parts 1 hash)
        set(target "${_perlite_md_dir}/${file}")
        if(EXISTS "${target}")
            file(SHA256 "${target}" have)
            if(have STREQUAL hash)
                continue()
            endif()
        endif()
        message(STATUS "perlite: downloading minecraft-data ${PERLITE_MINECRAFT_DATA_VERSION}/${file}")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/PrismarineJS/minecraft-data/${PERLITE_MINECRAFT_DATA_COMMIT}/data/bedrock/${PERLITE_MINECRAFT_DATA_VERSION}/${file}"
            "${target}.part"
            EXPECTED_HASH SHA256=${hash}
            STATUS status
            TLS_VERIFY ON)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${target}.part")
            list(GET status 1 reason)
            message(FATAL_ERROR "perlite: downloading minecraft-data ${file} failed: ${reason}")
        endif()
        file(RENAME "${target}.part" "${target}")
    endforeach()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PERLITE_BEDROCK_DIR "${CMAKE_CURRENT_BINARY_DIR}/bedrock")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_perlite_md_dir}/items.json" "${_perlite_md_dir}/blocks.json" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bedrock_data.py")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/bedrock_data.py"
            --items "${_perlite_md_dir}/items.json" --blocks "${_perlite_md_dir}/blocks.json"
            --out "${PERLITE_BEDROCK_DIR}"
    RESULT_VARIABLE _perlite_md_result)
if(NOT _perlite_md_result EQUAL 0)
    message(FATAL_ERROR "perlite: converting minecraft-data failed")
endif()
message(STATUS "perlite: Bedrock item and block data from minecraft-data ${PERLITE_MINECRAFT_DATA_VERSION}")
