project(savefile)
include(${CMAKE_SOURCE_DIR}/cmake/apply_default_settings.cmake)

file(GLOB_RECURSE savefile_src "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/src/*.h" "${CMAKE_CURRENT_LIST_DIR}/include/*.h")

list(APPEND savefile_src ${CMAKE_SOURCE_DIR}/.clang-format)

add_library(savefile STATIC ${savefile_src})
target_include_directories(savefile PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)
target_include_directories(savefile PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
apply_default_settings(TARGETS savefile)

find_package(ZLIB REQUIRED)
target_link_libraries(savefile PUBLIC ZLIB::ZLIB)

if(BUILD_UNIT_TESTS AND NOT EMSCRIPTEN)
  add_executable(savefile_refid_test tests/RefIdTest.cpp)
  target_link_libraries(savefile_refid_test PRIVATE savefile)
  target_compile_definitions(savefile_refid_test PRIVATE
    SKYMP_TEMPLATE_SAVE_PATH="${CMAKE_SOURCE_DIR}/skyrim-platform/src/platform_se/skyrim_platform/assets/template.ess"
  )
  apply_default_settings(TARGETS savefile_refid_test)
  add_test(NAME test_savefile_refid COMMAND savefile_refid_test)
endif()
