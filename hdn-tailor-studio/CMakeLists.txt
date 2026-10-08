cmake_minimum_required(VERSION 3.22)
project(HdnTailorStudio VERSION 0.1.3 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(HDN_BUILD_PLUGIN "Build the experimental Windows SKSE plugin" ${WIN32})
enable_testing()
add_executable(studio-policy-tests tests/PolicyTests.cpp)
target_include_directories(studio-policy-tests PRIVATE include)
add_test(NAME studio-policy COMMAND studio-policy-tests)
if(HDN_BUILD_PLUGIN)
  if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The plugin requires Windows/MSVC x64; policy tests are portable")
  endif()
  include(FetchContent)
  set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
  set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
  set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(commonlib
    GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
    GIT_TAG 94faaed0c60eddd8347767f2d4d29a97c93bde8c)
  FetchContent_MakeAvailable(commonlib)
  include(${commonlib_SOURCE_DIR}/cmake/CommonLibSSE.cmake)
  # CommonLib defaults unused VersionNumber entries to 1.0.0; SKSE needs zeros.
  set(HDN_COMPATIBLE_RUNTIMES 1.5.97 1.6.1170)
  foreach(index RANGE 1 14)
    list(APPEND HDN_COMPATIBLE_RUNTIMES 0.0.0)
  endforeach()
  add_commonlibsse_plugin(HdnTailorStudio AUTHOR "Hijos de las Nieves"
    STRUCT_DEPENDENT COMPATIBLE_RUNTIMES ${HDN_COMPATIBLE_RUNTIMES}
    SOURCES src/Plugin.cpp src/Studio.cpp)
  target_include_directories(HdnTailorStudio PRIVATE include)
  target_compile_definitions(HdnTailorStudio PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(HdnTailorStudio PRIVATE d3d11 dxgi)
  target_precompile_headers(HdnTailorStudio PRIVATE include/PCH.hpp)
  set_target_properties(HdnTailorStudio PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/package/Data/SKSE/Plugins"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/package/Data/SKSE/Plugins")
  add_executable(studio-loader-tests tests/LoaderTests.cpp)
  add_dependencies(studio-loader-tests HdnTailorStudio)
  add_test(NAME studio-loader-disabled
    COMMAND studio-loader-tests "$<TARGET_FILE:HdnTailorStudio>" "disabled")
  add_test(NAME studio-loader-missing-address-library
    COMMAND studio-loader-tests "$<TARGET_FILE:HdnTailorStudio>" "missing-library")
  add_test(NAME studio-loader-quarantine-with-library
    COMMAND studio-loader-tests "$<TARGET_FILE:HdnTailorStudio>" "quarantined")
  add_test(NAME studio-loader-quarantine-crlf
    COMMAND studio-loader-tests "$<TARGET_FILE:HdnTailorStudio>" "quarantined-crlf")
endif()
