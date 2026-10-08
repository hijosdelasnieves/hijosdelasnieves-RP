# HDN modification, 2026-10-08. GPL-3.0. Only private NIF renderer core;
# upstream plugin/UI/hooks/periodic worker are deliberately NOT built.
FetchContent_Declare(meshframework
  GIT_REPOSITORY https://github.com/QTR-Modding/Mesh-Rendering-Framework.git
  GIT_TAG c92dd6a71b7a8e71fd9f1d5a76f82e2b14c22833
  GIT_SUBMODULES "")
FetchContent_GetProperties(meshframework)
if(NOT meshframework_POPULATED)
  FetchContent_Populate(meshframework)
endif()
FetchContent_Declare(nifly
  GIT_REPOSITORY https://github.com/Thiago099/nifly.git
  GIT_TAG cca0a770094bb962fb28ea1fec5ea903e68fda8e)
FetchContent_MakeAvailable(nifly)
find_package(directxtex CONFIG REQUIRED)

# Generate a modified copy in OUR build directory, not in the reference tree.
set(HDN_MRF_COPY "${CMAKE_BINARY_DIR}/hdn-mesh-core")
file(MAKE_DIRECTORY "${HDN_MRF_COPY}")
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchFramework.cmake)
foreach(source Mesh.cpp RenderManager.cpp API.cpp GameAnimation.cpp)
  hdn_patch_framework(${source})
endforeach()
add_commonlibsse_plugin(MeshRenderingFramework AUTHOR "QTR-Modding; HDN fork"
  STRUCT_DEPENDENT COMPATIBLE_RUNTIMES ${HDN_COMPATIBLE_RUNTIMES}
  SOURCES src/FrameworkPlugin.cpp
    ${HDN_MRF_COPY}/Mesh.cpp ${HDN_MRF_COPY}/RenderManager.cpp
    ${HDN_MRF_COPY}/API.cpp ${HDN_MRF_COPY}/GameAnimation.cpp)
target_include_directories(MeshRenderingFramework PRIVATE include
  "${meshframework_SOURCE_DIR}/include")
target_precompile_headers(MeshRenderingFramework PRIVATE
  "${meshframework_SOURCE_DIR}/include/PCH.h")
target_compile_definitions(MeshRenderingFramework PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
  UNICODE _UNICODE)
target_link_libraries(MeshRenderingFramework PRIVATE nifly Microsoft::DirectXTex
  d3d11 d3dcompiler dxgi windowscodecs)
set_target_properties(MeshRenderingFramework PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/package/Data/SKSE/Plugins"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/package/Data/SKSE/Plugins")
