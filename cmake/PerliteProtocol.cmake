include_guard(GLOBAL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PERLITE_PROTOCOL_SPEC "${CMAKE_CURRENT_SOURCE_DIR}/protocol/bedrock-1.26.60.json")
set(PERLITE_PROTOCOL_OVERRIDES "${CMAKE_CURRENT_SOURCE_DIR}/protocol/overrides.json")
set(PERLITE_PROTOGEN "${CMAKE_CURRENT_SOURCE_DIR}/tools/protogen/protogen.py")
set(PERLITE_PROTO_OUT "${CMAKE_CURRENT_BINARY_DIR}/generated/protocol")
set(PERLITE_PROTO_SOURCES)
foreach(i RANGE 0 5)
    list(APPEND PERLITE_PROTO_SOURCES "${PERLITE_PROTO_OUT}/Codec${i}.cpp")
endforeach()
add_custom_command(
    OUTPUT "${PERLITE_PROTO_OUT}/Types.hpp" "${PERLITE_PROTO_OUT}/Packets.hpp" ${PERLITE_PROTO_SOURCES}
           "${PERLITE_PROTO_OUT}/RoundTrip.cpp" "${PERLITE_PROTO_OUT}/PacketJson.cpp"
    COMMAND "${Python3_EXECUTABLE}" -I "${PERLITE_PROTOGEN}"
            --spec "${PERLITE_PROTOCOL_SPEC}" --overrides "${PERLITE_PROTOCOL_OVERRIDES}"
            --out "${PERLITE_PROTO_OUT}" --parts 6 --namespace proto2225 --root-namespace perlite
    DEPENDS "${PERLITE_PROTOGEN}" "${PERLITE_PROTOCOL_SPEC}" "${PERLITE_PROTOCOL_OVERRIDES}"
    COMMENT "Generating the Bedrock protocol codecs"
    VERBATIM)
add_library(perlite_protocol STATIC src/protocol/Nbt.cpp ${PERLITE_PROTO_SOURCES}
    "${PERLITE_PROTO_OUT}/Types.hpp" "${PERLITE_PROTO_OUT}/Packets.hpp")
target_include_directories(perlite_protocol PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>")
target_compile_features(perlite_protocol PUBLIC cxx_std_23)
set_target_properties(perlite_protocol PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_source_files_properties(${PERLITE_PROTO_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
