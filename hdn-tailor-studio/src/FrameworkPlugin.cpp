// HDN fork adapter, GPL-3.0. Original MRF API exports remain in API.cpp.
// No hooks, global UI, background thread, or automatic animation worker.
#include "API.h"
#include "FrameworkView.hpp"
#include "Runtime.hpp"

namespace {
bool loaded = false;
using IMesh = MeshRenderingFrameworkAPI::Internal::IMesh;
struct Pose
{
  std::vector<std::string> names;
  std::vector<std::int16_t> parents;
  std::vector<MeshRenderingFrameworkAPI::BoneTransform> transforms;
};
bool capturePose(RE::NiAVObject* object, std::int16_t parent, Pose& pose,
                 unsigned depth = 0)
{
  if (!object)
    return true;
  if (depth > 64 || pose.names.size() >= 4096)
    return false;
  const auto index = static_cast<std::int16_t>(pose.names.size());
  pose.names.emplace_back(object->name.c_str() ? object->name.c_str() : "");
  pose.parents.push_back(parent);
  auto& transform = pose.transforms.emplace_back();
  // The top skeleton root is model-local, not the actor's world transform.
  if (parent >= 0) {
    const RE::NiQuaternion rotation(object->local.rotate);
    transform.translation[0] = object->local.translate.x;
    transform.translation[1] = object->local.translate.y;
    transform.translation[2] = object->local.translate.z;
    transform.rotation[0] = rotation.x;
    transform.rotation[1] = rotation.y;
    transform.rotation[2] = rotation.z;
    transform.rotation[3] = rotation.w;
    for (float& scale : transform.scale)
      scale = object->local.scale;
  }
  for (const float value : transform.translation)
    if (!std::isfinite(value) || std::abs(value) > 10000)
      return false;
  for (const float value : transform.rotation)
    if (!std::isfinite(value))
      return false;
  for (const float value : transform.scale)
    if (!std::isfinite(value) || value <= 0 || value > 10)
      return false;
  const auto node = object->AsNode();
  if (node)
    for (const auto& child : node->GetChildren())
      if (!capturePose(child.get(), index, pose, depth + 1))
        return false;
  return true;
}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
  if (!skse)
    return false;
  const auto runtime = skse->RuntimeVersion();
  if (!hdn::studio::supportedRuntime(
        { runtime[0], runtime[1], runtime[2], runtime[3] }))
    return false;
  const auto config =
    std::filesystem::absolute("Data/SKSE/Plugins/HdnTailorStudio.ini");
  if (GetPrivateProfileIntW(L"Studio", L"EnableIsolatedMeshBackend", 0,
                            config.c_str()) != 1)
    return true;
  const auto library = std::filesystem::path("Data/SKSE/Plugins") /
    ((runtime == SKSE::RUNTIME_SSE_1_5_97 ? "version-" : "versionlib-") +
     runtime.string() + ".bin");
  if (!std::filesystem::is_regular_file(library))
    return true;
  SKSE::Init(skse, SKSE::InitInfo{ .log = false });
  loaded = true;
  return true;
}

FUNCTION_PREFIX bool HdnMesh_Init(ID3D11Device* device)
{
  return loaded && device && RenderManager::Init(device, nullptr);
}
FUNCTION_PREFIX IMesh* HdnMesh_CaptureActor(RE::Actor* actor)
{
  // Called by an SKSE main-thread task, NEVER by Present/Papyrus's worker.
  if (!loaded || !actor || actor->GetFormID() == 0x14 ||
      (actor->GetFormID() >> 24) != 0xff || actor->IsDisabled() ||
      actor->IsDead() || !actor->Get3D(false))
    return nullptr;
  const auto root = actor->Get3D(false)->GetObjectByName("NPC Root [Root]");
  if (!root)
    return nullptr;
  Pose pose;
  if (!capturePose(root, -1, pose) || pose.names.empty())
    return nullptr;
  const auto mesh =
    MeshRenderingFrameworkAPI::Internal::CreateFromActor(actor, 1024, 1536);
  if (!mesh)
    return nullptr;
  std::vector<const char*> names;
  names.reserve(pose.names.size());
  for (const auto& name : pose.names)
    names.push_back(name.c_str());
  if (!RenderManager::SetBoneLocalPose(
        mesh, names.data(), pose.parents.data(), pose.transforms.data(),
        static_cast<std::uint32_t>(names.size()))) {
    RenderManager::Delete(mesh);
    return nullptr;
  }
  mesh->position = {};
  mesh->rotation = RE::NiMatrix3{};
  mesh->scale = 1;
  mesh->mustUpdate = true;
  mesh->alwaysUpdate = false;
  return mesh;
}
FUNCTION_PREFIX bool HdnMesh_Render(IMesh* mesh, float yaw, float zoom,
                                    float aspect)
{
  if (!loaded || !mesh || !std::isfinite(yaw) || !std::isfinite(zoom) ||
      !std::isfinite(aspect) || aspect < 0.05f || aspect > 20)
    return false;
  const hdn::studio::AspectScope aspectScope(aspect);
  mesh->rotation.SetEulerAnglesXYZ(
    0, 0, std::remainder(yaw, 360.f) * 3.14159265358979323846f / 180);
  mesh->scale = std::clamp(zoom, 0.75f, 1.f);
  mesh->mustUpdate = true;
  return RenderManager::Render(mesh);
}
FUNCTION_PREFIX ID3D11ShaderResourceView* HdnMesh_Texture(IMesh* mesh)
{
  return mesh ? mesh->SRV
              : nullptr; // Borrowed only while frontend mutex held.
}
FUNCTION_PREFIX void HdnMesh_Delete(IMesh* mesh)
{
  if (mesh)
    RenderManager::Delete(mesh);
}
