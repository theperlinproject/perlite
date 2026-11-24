cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
endif()
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0074 NEW)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

project(perlite VERSION 0.2.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(PERLITE_TOP_LEVEL ON)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release)
    endif()
    if(WIN32)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set(OPENSSL_MSVC_STATIC_RT ON)
    endif()
    if(NOT DEFINED OPENSSL_USE_STATIC_LIBS AND (WIN32 OR APPLE))
        set(OPENSSL_USE_STATIC_LIBS ON)
    endif()
    if(OPENSSL_ROOT_DIR)
        file(TO_CMAKE_PATH "${OPENSSL_ROOT_DIR}" OPENSSL_ROOT_DIR)
    endif()
else()
    set(PERLITE_TOP_LEVEL OFF)
endif()

option(PERLITE_NETHERNET "NetherNet transport (libdatachannel); without it only loopback connections work" ON)
option(PERLITE_EMBED "Build perlite_embed: the server without a wasm runtime, for hosts that bring their own (the Perlin client)" OFF)
option(PERLITE_BUILD_SHARED "Build the perlite shared library with the C embedding API" ${PERLITE_TOP_LEVEL})
option(PERLITE_BUILD_EXE "Build the perlite executable" ${PERLITE_TOP_LEVEL})
option(PERLITE_BUILD_TESTS "Build the Perlite tests" ${PERLITE_TOP_LEVEL})
option(PERLITE_WASM3 "Run plugins on wasm3 (32-bit modules only) even when WasmEdge is available" OFF)
if(EMSCRIPTEN)
    set(PERLITE_LEVELDB_DEFAULT OFF)
else()
    set(PERLITE_LEVELDB_DEFAULT ON)
endif()
option(PERLITE_LEVELDB "Save worlds in the Bedrock LevelDB format (needs zlib)" ${PERLITE_LEVELDB_DEFAULT})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PerliteDependencies)
include(PerliteProtocol)
include(PerliteBedrockData)

set(PERLITE_SOURCES
    src/common/Json.cpp
    src/common/Log.cpp
    src/common/Https.cpp
    src/common/ModSocket.cpp
    src/common/WebSocket.cpp
    src/embed/CApi.cpp
    src/network/Batch.cpp
    src/network/Connection.cpp
    src/network/PacketCipher.cpp
    src/network/auth/LoginVerifier.cpp
    src/network/loopback/LoopbackListener.cpp
    src/network/session/ChatHandler.cpp
    src/network/session/ChunkStreamer.cpp
    src/network/session/EntityView.cpp
    src/network/session/Health.cpp
    src/network/session/InteractionHandler.cpp
    src/network/session/InventoryHandler.cpp
    src/network/session/LoginHandler.cpp
    src/network/session/MovementHandler.cpp
    src/network/session/BlockBreaking.cpp
    src/network/session/PlayerData.cpp
    src/network/session/PlayerState.cpp
    src/network/session/Session.cpp
    src/network/session/SocialHandler.cpp
    src/network/session/WorldSync.cpp
    src/plugin/PluginHooks.cpp
    src/plugin/Content.cpp
    src/plugin/PluginHost.cpp
    src/server/Config.cpp
    src/server/Server.cpp
    src/server/Placement.cpp
    src/server/Fire.cpp
    src/server/Fluids.cpp
    src/server/RandomTicks.cpp
    src/server/VanillaPack.cpp
    src/server/ServerData.cpp
    src/server/vanilla/BedrockData.cpp
    src/server/ServerStorage.cpp
    src/server/packs/ResourcePackManager.cpp
    src/server/access/Access.cpp
    src/server/access/BanList.cpp
    src/server/commands/Commands.cpp
    src/server/commands/ExecuteCommand.cpp
    src/server/commands/builtin/AccessCommands.cpp
    src/server/commands/builtin/ExtraCommands.cpp
    src/server/commands/builtin/Builtins.cpp
    src/server/commands/builtin/InfoCommands.cpp
    src/server/commands/builtin/PlayerCommands.cpp
    src/server/commands/builtin/ServerCommands.cpp
    src/server/commands/builtin/WorldCommands.cpp
    src/world/actor/ActorManager.cpp
    src/world/actor/Explosion.cpp
    src/world/actor/ai/MobBrain.cpp
    src/world/actor/ai/MobPhysics.cpp
    src/world/actor/combat/MobCombat.cpp
    src/world/actor/misc/ExperienceOrb.cpp
    src/world/actor/misc/FallingBlock.cpp
    src/world/actor/misc/PrimedTnt.cpp
    src/world/actor/mob/MobRegistry.cpp
    src/world/actor/mob/VanillaEntities.cpp
    src/world/level/block/VoxelShapes.cpp
    src/world/actor/mob/VanillaMobs.cpp
    src/world/actor/projectile/Arrow.cpp
    src/world/actor/boss/EnderDragon.cpp
    src/world/actor/spawn/NaturalSpawner.cpp
    src/world/container/Containers.cpp
    src/world/crafting/RecipeData.cpp
    src/world/crafting/Recipes.cpp
    src/world/inventory/Inventory.cpp
    src/world/inventory/Stations.cpp
    src/server/Connections.cpp
    src/world/inventory/ItemNbt.cpp
    src/world/level/block/BlockCatalog.cpp
    src/world/level/block/Blocks.cpp
    src/world/level/block/Waterlog.cpp
    src/world/level/chunk/Chunk.cpp
    src/world/level/chunk/ChunkManager.cpp
    src/world/level/levelgen/Backends.cpp
    src/world/level/levelgen/Generator.cpp
    src/world/level/levelgen/vanilla/BiomeData.cpp
    src/world/level/levelgen/vanilla/DataPack.cpp
    src/world/level/levelgen/vanilla/DensityFunction.cpp
    src/world/level/levelgen/vanilla/FeatureData.cpp
    src/world/level/levelgen/vanilla/JavaDecorator.cpp
    src/world/level/levelgen/vanilla/JavaFeatureData.cpp
    src/world/level/levelgen/vanilla/Noise.cpp
    src/world/level/levelgen/vanilla/OverworldShapeData.cpp
    src/world/level/levelgen/vanilla/VanillaAquifer.cpp
    src/world/level/levelgen/vanilla/VanillaCarvers.cpp
    src/world/level/levelgen/vanilla/VanillaDimension.cpp
    src/world/level/levelgen/vanilla/NetherGen.cpp
    src/world/level/end/EndGen.cpp
    src/world/level/end/DragonFight.cpp
    src/world/level/structure/JavaNbt.cpp
    src/world/level/structure/JavaBlocks.cpp
    src/world/level/structure/Templates.cpp
    src/world/level/structure/Structures.cpp
    src/world/level/structure/CodeStructures.cpp
    src/world/level/levelgen/vanilla/VanillaFeatures.cpp
    src/world/level/levelgen/vanilla/VanillaMaterials.cpp
    src/world/level/levelgen/vanilla/VanillaSurface.cpp
    src/world/level/levelgen/vanilla/VanillaTerrain.cpp
    src/world/level/portal/Portals.cpp
    src/protocol/NbtTree.cpp
    src/world/level/storage/LevelDb.cpp
    src/world/level/storage/WorldStorage.cpp
    src/world/loot/Loot.cpp
    src/world/loot/LootData.cpp)

file(GLOB_RECURSE PERLITE_MOB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/world/actor/mob/*/*/*.cpp")
list(APPEND PERLITE_SOURCES ${PERLITE_MOB_SOURCES})

if(PERLITE_NETHERNET)
    list(APPEND PERLITE_SOURCES
        src/network/nethernet/NetherNetListener.cpp
        src/network/nethernet/SignalingServer.cpp
        src/network/nethernet/LanDiscovery.cpp
        src/network/nethernet/ServerIdentity.cpp)
else()
    list(APPEND PERLITE_SOURCES src/network/nethernet/NetherNetStub.cpp)
endif()

list(APPEND PERLITE_SOURCES "${PERLITE_BEDROCK_EMBED}")
add_library(perlite_objects OBJECT ${PERLITE_SOURCES})
add_library(perlite_core STATIC $<TARGET_OBJECTS:perlite_objects> ${PERLITE_WASM_SOURCES})
target_link_libraries(perlite_core PRIVATE ${PERLITE_WASM_LIB})
set(PERLITE_LIBS perlite_core)
if(PERLITE_EMBED)
    add_library(perlite_embed STATIC $<TARGET_OBJECTS:perlite_objects>)
    target_include_directories(perlite_embed INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(perlite_embed INTERFACE PERLITE_STATIC)
    list(APPEND PERLITE_LIBS perlite_embed)
endif()

foreach(t perlite_objects ${PERLITE_LIBS})
    target_include_directories(${t} PRIVATE src include ${PERLIN_API_INCLUDE_DIRS})
    target_compile_features(${t} PUBLIC cxx_std_23)
    set_target_properties(${t} PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    target_compile_definitions(${t} PRIVATE PERLITE_BUILD_SHARED "PERLITE_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"" "PERLITE_BEDROCK_DIR=\"${PERLITE_BEDROCK_DIR}\"")
    target_link_libraries(${t} PRIVATE perlite_protocol miniz OpenSSL::Crypto OpenSSL::SSL Threads::Threads)
    if(PERLITE_NETHERNET)
        target_link_libraries(${t} PRIVATE ${PERLITE_DATACHANNEL})
        target_compile_definitions(${t} PRIVATE PERLITE_HAS_NETHERNET=1)
    endif()
    if(PERLITE_LEVELDB)
        target_link_libraries(${t} PRIVATE leveldb)
        target_compile_definitions(${t} PRIVATE PERLITE_HAS_LEVELDB=1)
    endif()
    if(WIN32)
        target_compile_definitions(${t} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(${t} PRIVATE ws2_32 crypt32 bcrypt iphlpapi advapi32 user32)
    endif()
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endforeach()
target_include_directories(perlite_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(NOT MSVC)
    set(PERLITE_STRICT_FP
        src/world/level/levelgen/vanilla/Noise.cpp
        src/world/level/levelgen/vanilla/DensityFunction.cpp
        src/world/level/levelgen/vanilla/VanillaTerrain.cpp
        src/world/level/levelgen/vanilla/VanillaSurface.cpp
        src/world/level/levelgen/vanilla/VanillaAquifer.cpp
        src/world/level/levelgen/vanilla/VanillaMaterials.cpp
        src/world/level/levelgen/vanilla/VanillaCarvers.cpp
        src/world/level/levelgen/vanilla/VanillaDimension.cpp)
    set_source_files_properties(${PERLITE_STRICT_FP} TARGET_DIRECTORY perlite_objects PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

if(PERLITE_BUILD_SHARED)
    add_library(perlite_shared SHARED src/embed/ShimExports.cpp)
    set_target_properties(perlite_shared PROPERTIES OUTPUT_NAME perlite CXX_VISIBILITY_PRESET hidden)
    target_include_directories(perlite_shared PRIVATE include)
    target_compile_definitions(perlite_shared PRIVATE PERLITE_BUILD_SHARED)
    if(MSVC)
        target_link_options(perlite_shared PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:perlite_core>")
    elseif(APPLE)
        target_link_options(perlite_shared PRIVATE "LINKER:-force_load,$<TARGET_FILE:perlite_core>")
    else()
        target_link_options(perlite_shared PRIVATE -Wl,--whole-archive "$<TARGET_FILE:perlite_core>" -Wl,--no-whole-archive)
    endif()
    target_link_libraries(perlite_shared PRIVATE perlite_core)
    install(TARGETS perlite_shared RUNTIME DESTINATION . LIBRARY DESTINATION . ARCHIVE DESTINATION sdk/lib)
    install(DIRECTORY include/ DESTINATION sdk/include)
endif()

if(PERLITE_BUILD_EXE)
    add_executable(perlite src/main/Main.cpp)
    target_include_directories(perlite PRIVATE src include ${PERLIN_API_INCLUDE_DIRS})
    target_link_libraries(perlite PRIVATE perlite_core perlite_protocol Threads::Threads)
    if(UNIX AND NOT APPLE)
        target_link_options(perlite PRIVATE $<$<CONFIG:Release>:-s>)
    endif()
    # Ship the commented default settings next to the program.
    add_custom_command(TARGET perlite POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/server.properties"
                "$<TARGET_FILE_DIR:perlite>/server.properties")
    install(TARGETS perlite RUNTIME DESTINATION .)
    install(FILES server.properties DESTINATION .)
    install(DIRECTORY data/ DESTINATION data)
    install(DIRECTORY "${PERLITE_BEDROCK_DIR}/" DESTINATION data/bedrock)
endif()

if(PERLITE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_FOUND AND COMMAND perlin_api_add_checks)
        perlin_api_add_checks(SERVER "${CMAKE_CURRENT_SOURCE_DIR}/src")
    endif()
endif()
