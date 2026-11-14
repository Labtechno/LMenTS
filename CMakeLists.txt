cmake_minimum_required(VERSION 3.22)
project(LMenTS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- La puce virtuelle (processeur Cortex-M4F) -------------------------------------------
add_library(lments_emu STATIC
  Source/emu/CortexM4.cpp
  Source/emu/CortexM4Fpu.cpp)
target_include_directories(lments_emu PUBLIC Source/emu)
# Le calcul flottant doit rester exactement celui qui est écrit (pas de fusion
# multiplication-addition décidée par le compilateur).
target_compile_options(lments_emu PRIVATE -ffp-contract=off)

# --- Banc d'essai du processeur (voir tools/armtests) -------------------------------------
add_executable(lments_emu_run tools/emu_run.cpp)
target_link_libraries(lments_emu_run PRIVATE lments_emu)

# Le plugin VST3 arrive à l'étape 2.
