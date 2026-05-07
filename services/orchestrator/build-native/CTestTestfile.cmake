# CMake generated Testfile for 
# Source directory: /home/itunz/Work/AI_work/Poot/services/orchestrator
# Build directory: /home/itunz/Work/AI_work/Poot/services/orchestrator/build-native
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[orchestrator-tests]=] "/home/itunz/Work/AI_work/Poot/services/orchestrator/build-native/orchestrator-tests")
set_tests_properties([=[orchestrator-tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/itunz/Work/AI_work/Poot/services/orchestrator/CMakeLists.txt;35;add_test;/home/itunz/Work/AI_work/Poot/services/orchestrator/CMakeLists.txt;0;")
subdirs("_deps/catch2-build")
