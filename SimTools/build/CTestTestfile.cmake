# CMake generated Testfile for 
# Source directory: /home/claude/dawn-of-titans/SimTools
# Build directory: /home/claude/dawn-of-titans/SimTools/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(dot_tests "/home/claude/dawn-of-titans/SimTools/build/dot_tests")
set_tests_properties(dot_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;62;add_test;/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;0;")
add_test(dot_kingdom_tests "/home/claude/dawn-of-titans/SimTools/build/dot_kingdom_tests")
set_tests_properties(dot_kingdom_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;63;add_test;/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;0;")
add_test(dot_runner_sandbox "/home/claude/dawn-of-titans/SimTools/build/dot_runner" "--scenario" "scenarios/m0_sandbox.json" "--attacker-ai" "veteran" "--quiet")
set_tests_properties(dot_runner_sandbox PROPERTIES  _BACKTRACE_TRIPLES "/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;64;add_test;/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;0;")
add_test(dot_runner_titan_duel "/home/claude/dawn-of-titans/SimTools/build/dot_runner" "--scenario" "scenarios/m0_titan_duel.json" "--quiet")
set_tests_properties(dot_runner_titan_duel PROPERTIES  _BACKTRACE_TRIPLES "/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;65;add_test;/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;0;")
add_test(dot_runner_cavalry "/home/claude/dawn-of-titans/SimTools/build/dot_runner" "--scenario" "scenarios/m0_cavalry_test.json" "--quiet")
set_tests_properties(dot_runner_cavalry PROPERTIES  _BACKTRACE_TRIPLES "/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;66;add_test;/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;0;")
add_test(dot_runner_raise_dead "/home/claude/dawn-of-titans/SimTools/build/dot_runner" "--scenario" "scenarios/m1_raise_dead.json" "--attacker-ai" "veteran" "--determinism" "3" "--quiet")
set_tests_properties(dot_runner_raise_dead PROPERTIES  _BACKTRACE_TRIPLES "/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;67;add_test;/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;0;")
add_test(dot_runner_chains_and_fire "/home/claude/dawn-of-titans/SimTools/build/dot_runner" "--scenario" "scenarios/m1_chains_and_fire.json" "--attacker-ai" "veteran" "--determinism" "3" "--quiet")
set_tests_properties(dot_runner_chains_and_fire PROPERTIES  _BACKTRACE_TRIPLES "/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;68;add_test;/home/claude/dawn-of-titans/SimTools/CMakeLists.txt;0;")
