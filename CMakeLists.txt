cmake_minimum_required(VERSION 3.20)
project(SatSolv VERSION 0.1.0 LANGUAGES CXX)

option(SATSOLV_ENABLE_SANITIZERS "Enable ASan+UBSan" OFF)
option(SATSOLV_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

add_library(satsolv_core
    src/parser.cpp
    src/solver.cpp
)
target_include_directories(satsolv_core PUBLIC include)
target_compile_features(satsolv_core PUBLIC cxx_std_20)

if (MSVC)
  target_compile_options(satsolv_core PRIVATE /W4)
else()
  target_compile_options(satsolv_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  if(SATSOLV_WARNINGS_AS_ERRORS)
    target_compile_options(satsolv_core PRIVATE -Werror)
  endif()
  if(SATSOLV_ENABLE_SANITIZERS)
    target_compile_options(satsolv_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(satsolv_core PRIVATE -fsanitize=address,undefined)
  endif()
endif()

add_executable(satsolv src/main.cpp)
target_link_libraries(satsolv PRIVATE satsolv_core)
if (NOT MSVC AND SATSOLV_ENABLE_SANITIZERS)
  target_compile_options(satsolv PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(satsolv PRIVATE -fsanitize=address,undefined)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(satsolv_unit tests/test_solver.cpp)
  target_link_libraries(satsolv_unit PRIVATE satsolv_core)
  if (NOT MSVC AND SATSOLV_ENABLE_SANITIZERS)
    target_compile_options(satsolv_unit PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(satsolv_unit PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME unit COMMAND satsolv_unit)
endif()

install(TARGETS satsolv RUNTIME DESTINATION bin)
