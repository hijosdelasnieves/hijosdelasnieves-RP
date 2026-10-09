cmake_minimum_required(VERSION 3.22)
project(HdnTailorStudio VERSION 0.2.2 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(HDN_BUILD_PLUGIN "Build the experimental Windows SKSE plugin" ${WIN32})
option(HDN_BUILD_ISOLATED "Build the independent NIF mesh backend" OFF)
enable_testing()
add_executable(studio-pose-capture-tests tests/PoseCaptureTests.cpp)
target_include_directories(studio-pose-capture-tests PRIVATE include)
target_compile_options(studio-pose-capture-tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>)
add_test(NAME studio-pose-capture COMMAND studio-pose-capture-tests)
add_executable(studio-policy-tests tests/PolicyTests.cpp)
target_include_directories(studio-policy-tests PRIVATE include)
add_test(NAME studio-policy COMMAND studio-policy-tests)
add_executable(studio-isolated-view-tests tests/IsolatedViewTests.cpp)
target_include_directories(studio-isolated-view-tests PRIVATE include)
target_compile_options(studio-isolated-view-tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>)
add_test(NAME studio-isolated-view COMMAND studio-isolated-view-tests)
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
  set(HDN_STUDIO_SOURCES src/Plugin.cpp src/Studio.cpp)
  if(HDN_BUILD_ISOLATED)
    include(cmake/Framework.cmake)
    set(HDN_STUDIO_SOURCES src/IsolatedPlugin.cpp src/IsolatedStudio.cpp)
  endif()
  add_commonlibsse_plugin(HdnTailorStudio AUTHOR "Hijos de las Nieves"
    STRUCT_DEPENDENT COMPATIBLE_RUNTIMES ${HDN_COMPATIBLE_RUNTIMES}
    SOURCES ${HDN_STUDIO_SOURCES})
  target_include_directories(HdnTailorStudio PRIVATE include)
  target_compile_definitions(HdnTailorStudio PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(HdnTailorStudio PRIVATE d3d11 dxgi d3dcompiler)
  target_precompile_headers(HdnTailorStudio PRIVATE include/PCH.hpp)
  set_target_properties(HdnTailorStudio PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/package/Data/SKSE/Plugins"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/package/Data/SKSE/Plugins")
  add_executable(studio-loader-tests tests/LoaderTests.cpp)
  add_executable(studio-d3d-state-tests tests/D3DStateTests.cpp)
  target_include_directories(studio-d3d-state-tests PRIVATE include)
  target_link_libraries(studio-d3d-state-tests PRIVATE d3d11)
  add_test(NAME studio-d3d-state COMMAND studio-d3d-state-tests)
  add_dependencies(studio-loader-tests HdnTailorStudio)
  add_test(NAME studio-loader-disabled
    COMMAND studio-loader-tests "$<TARGET_FILE:HdnTailorStudio>" "disabled")
  add_test(NAME studio-loader-missing-address-library
    COMMAND studio-loader-tests "$<TARGET_FILE:HdnTailorStudio>" "missing-library")
  add_test(NAME studio-loader-quarantine-with-library
    COMMAND studio-loader-tests "$<TARGET_FILE:HdnTailorStudio>" "quarantined")
  add_test(NAME studio-loader-quarantine-crlf
    COMMAND studio-loader-tests "$<TARGET_FILE:HdnTailorStudio>" "quarantined-crlf")
  add_test(NAME studio-loader-isolated-missing-address-library
    COMMAND studio-loader-tests "$<TARGET_FILE:HdnTailorStudio>" "isolated-missing-library")
  if(HDN_BUILD_ISOLATED)
    add_dependencies(studio-loader-tests MeshRenderingFramework)
    foreach(mode disabled quarantined quarantined-crlf isolated-missing-library)
      add_test(NAME mesh-loader-${mode}
        COMMAND studio-loader-tests "$<TARGET_FILE:MeshRenderingFramework>"
        "${mode}" "MeshRenderingFramework")
    endforeach()
  endif()
endif()
