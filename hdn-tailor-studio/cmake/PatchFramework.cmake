# Mechanical, preimage-checked modifications of the pinned upstream core.
function(hdn_replace old new)
  string(FIND "${text}" "${old}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "MRF pinned preimage changed: ${filename}")
  endif()
  string(REPLACE "${old}" "${new}" text "${text}")
  set(text "${text}" PARENT_SCOPE)
endfunction()
function(hdn_patch_framework filename)
  file(READ "${meshframework_SOURCE_DIR}/src/${filename}" text)
  if(filename STREQUAL "Mesh.cpp")
    hdn_replace("if (size <= 0)" "if (size <= 0 || size > 64 * 1024 * 1024)")
    hdn_replace("stream.stream->totalSize == 0)"
      "stream.stream->totalSize == 0 || stream.stream->totalSize > 64 * 1024 * 1024)")
    hdn_replace([=[    mesh->alwaysUpdate = true;
    mesh->mustUpdate = true;
    UpdateFaceMorphs();
    return true;]=] [=[    // HDN: capture facial geometry once ON THE MAIN THREAD. No actor handle
    // or engine geometry may survive into Present on the independent backend.
    mesh->alwaysUpdate = false;
    mesh->mustUpdate = true;
    const bool captured = UpdateFaceMorphs();
    faceMorphActor = {};
    return captured;]=])
  elseif(filename STREQUAL "RenderManager.cpp")
    hdn_replace("#include <cstring>" "#include <cstring>\n#include <chrono>\n#include \"FrameworkView.hpp\"")
    hdn_replace("const float aspect = static_cast<float>(target->width) / static_cast<float>(target->height);"
      "const float aspect = hdn::studio::frameworkAspect(target);")
    hdn_replace([=[    if (!sourceMesh || !sourceMesh->IsValid() || !target || !target->initialized ||]=]
      [=[    const auto fitted = hdn::studio::frameworkCamera(sourceMesh, target);
    if (!fitted || !sourceMesh || !sourceMesh->IsValid() || !target || !target->initialized ||]=])
    set(old [=[    constexpr float cameraY = 320.0f;
    constexpr float subjectY = -500.0f;
    constexpr float horizontalHalfSpan = 130.0f;
    constexpr float cameraDistance = cameraY - subjectY;
    const float verticalHalfSpan = horizontalHalfSpan / aspect;]=])
    hdn_replace("${old}" [=[    const float cameraY = static_cast<float>(fitted->distance);
    constexpr float subjectY = 0.0f;
    // Fit all current posed parts, including head/feet/capes, at every yaw.
    const RE::NiPoint3 center{static_cast<float>(fitted->center.x),
        static_cast<float>(fitted->center.y), static_cast<float>(fitted->center.z)};
    sourceMesh->mesh->position = sourceMesh->mesh->rotation *
        (center * -sourceMesh->mesh->scale);]=])
    hdn_replace([=[        2.0f * std::atan(verticalHalfSpan / cameraDistance), aspect, 1.0f, 10000.0f);]=]
      [=[        2.0f * std::atan(static_cast<float>(fitted->verticalTangent)), aspect,
        static_cast<float>(fitted->nearPlane), static_cast<float>(fitted->farPlane));]=])
    hdn_replace("while (completionResult == S_FALSE) {" [=[const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (completionResult == S_FALSE) {
        if (std::chrono::steady_clock::now() >= deadline ||
            FAILED(device->GetDeviceRemovedReason())) {
            return false;
        }]=])
  endif()
  file(WRITE "${HDN_MRF_COPY}/${filename}" "${text}")
endfunction()
