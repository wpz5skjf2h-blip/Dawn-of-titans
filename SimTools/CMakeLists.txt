# SimTools/CMakeLists.txt
# Standalone build of the deterministic battle simulation (the same sources the Unreal
# DoTSim module compiles) plus a CLI runner, unit tests and the AI-vs-AI balance harness.
#
#   cmake -S SimTools -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ctest --test-dir build --output-on-failure
#
# WebAssembly (same sources as the browser demo), with the Emscripten SDK active:
#   emcmake cmake -S SimTools -B build-wasm -DCMAKE_BUILD_TYPE=Release && cmake --build build-wasm -j
#   node build-wasm/dot_runner.js --scenario scenarios/m0_sandbox.json --determinism 20
cmake_minimum_required(VERSION 3.16)
project(DoTSimTools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
file(GLOB DOT_SIM_SOURCES ${DOT_ROOT}/Source/DoTSim/Private/*.cpp)
# The Unreal module boilerplate is the only file that includes engine headers.
list(FILTER DOT_SIM_SOURCES EXCLUDE REGEX ".*DoTSimModule\\.cpp$")

add_library(dotsim STATIC ${DOT_SIM_SOURCES})
target_include_directories(dotsim PUBLIC ${DOT_ROOT}/Source/DoTSim/Public)

# Mirror Unreal's constraints: no exceptions, no RTTI, warnings as errors, no shadowing.
if(MSVC)
  # Same as UnrealBuildTool: /W4 with shadowing promoted to errors, no exceptions.
  set(DOT_WARNINGS /W4 /wd4251 /we4456 /we4457 /we4458 /we4459)
  set(DOT_LANG /GR- /EHs-c- /D_HAS_EXCEPTIONS=0)
  string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
else()
  set(DOT_WARNINGS -Wall -Wextra -Wshadow -Wpedantic -Werror)
  set(DOT_LANG -fno-exceptions -fno-rtti)
endif()
target_compile_options(dotsim PRIVATE ${DOT_WARNINGS} ${DOT_LANG})

# Kingdom economy (city, buildings, resources, lands): same rules, depends on the sim's JSON/hash utilities.
file(GLOB DOT_KINGDOM_SOURCES ${DOT_ROOT}/Source/DoTKingdom/Private/*.cpp)
list(FILTER DOT_KINGDOM_SOURCES EXCLUDE REGEX ".*DoTKingdomModule\\.cpp$")
add_library(dotkingdom STATIC ${DOT_KINGDOM_SOURCES})
target_include_directories(dotkingdom PUBLIC ${DOT_ROOT}/Source/DoTKingdom/Public)
target_link_libraries(dotkingdom PUBLIC dotsim)
target_compile_options(dotkingdom PRIVATE ${DOT_WARNINGS} ${DOT_LANG})

set(DOT_CONTENT_DIR "${DOT_ROOT}/Content/Data")

foreach(tool dot_runner dot_tests dot_harness dot_kingdom_tests)
  string(REPLACE "dot_" "" src ${tool})
  add_executable(${tool} ${src}.cpp)
  target_link_libraries(${tool} PRIVATE dotsim dotkingdom)
  target_compile_options(${tool} PRIVATE ${DOT_WARNINGS} ${DOT_LANG})
  target_compile_definitions(${tool} PRIVATE DOT_DEFAULT_CONTENT_DIR="${DOT_CONTENT_DIR}")
  if(EMSCRIPTEN)
    # WebAssembly build (emcmake cmake ...): runs under Node with direct file access.
    target_link_options(${tool} PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1MB)
  endif()
endforeach()

enable_testing()
add_test(NAME dot_tests COMMAND dot_tests)
add_test(NAME dot_kingdom_tests COMMAND dot_kingdom_tests)
add_test(NAME dot_runner_sandbox COMMAND dot_runner --scenario scenarios/m0_sandbox.json --attacker-ai veteran --quiet)
add_test(NAME dot_runner_titan_duel COMMAND dot_runner --scenario scenarios/m0_titan_duel.json --quiet)
add_test(NAME dot_runner_cavalry COMMAND dot_runner --scenario scenarios/m0_cavalry_test.json --quiet)
add_test(NAME dot_runner_raise_dead COMMAND dot_runner --scenario scenarios/m1_raise_dead.json --attacker-ai veteran --determinism 3 --quiet)
add_test(NAME dot_runner_chains_and_fire COMMAND dot_runner --scenario scenarios/m1_chains_and_fire.json --attacker-ai veteran --determinism 3 --quiet)
