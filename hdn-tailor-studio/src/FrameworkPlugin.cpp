// HDN fork adapter, GPL-3.0. Original MRF API exports remain in API.cpp.
// No hooks, global UI, background thread, or automatic animation worker.
#include "API.h"
#include "FrameworkView.hpp"
#include "PoseCapture.hpp"
#include "Runtime.hpp"

namespace {
bool loaded = false;
using IMesh = MeshRenderingFrameworkAPI::Internal::IMesh;
hdn::studio::PoseTransform valueTransform(const RE::NiTransform& transform)
{
  hdn::studio::PoseTransform result;
  for (size_t row = 0; row < 3; ++row)
    for (size_t column = 0; column < 3; ++column)
      result.rotation[row][column] = transform.rotate.entry[row][column];
  result.translation = { transform.translate.x, transform.translate.y,
                         transform.translate.z };
  result.scale = transform.scale;
  return result;
}
bool capturePose(RE::NiAVObject* object,
                 const hdn::studio::PoseTransform& root,
                 hdn::studio::CapturedPose& pose, unsigned depth = 0)
{
  if (!object)
    return true;
  if (depth > 64 || pose.names.size() >= hdn::studio::CapturedPose::maxBones)
    return false;
  const auto name = object->name.c_str();
  if (name && name[0] && !pose.add(name, root, valueTransform(object->world)))
    return false;
  const auto flattened = netimmerse_cast<RE::BSFlattenedBoneTree*>(object);
  if (flattened) {
    const auto& data = flattened->GetRuntimeData();
    if (!data.boneEntries || !data.numBones ||
        data.numBones > hdn::studio::CapturedPose::maxBones)
      return false;
    // Skyrim optimises twists, fingers and clothing bones out of NiNode
    // children. A null entry.node is a VALID bone, not something to omit.
    for (std::uint32_t i = 0; i < data.numBones; ++i) {
      const auto& entry = data.boneEntries[i];
      const auto boneName = entry.nodeName.c_str();
      const auto& world = entry.node ? entry.node->world : entry.world;
      if (!boneName || !pose.add(boneName, root, valueTransform(world)))
        return false;
    }
  }
  const auto node = object->AsNode();
  if (node)
    for (const auto& child : node->GetChildren())
      if (!capturePose(child.get(), root, pose, depth + 1))
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
  hdn::studio::CapturedPose pose;
  if (!capturePose(root, valueTransform(root->world), pose) ||
      pose.names.empty())
    return nullptr;
  const auto mesh =
    MeshRenderingFrameworkAPI::Internal::CreateFromActor(actor, 1024, 1536);
  if (!mesh)
    return nullptr;
  std::vector<const char*> names;
  names.reserve(pose.names.size());
  for (const auto& name : pose.names)
    names.push_back(name.c_str());
  std::vector<MeshRenderingFrameworkAPI::BoneTransform> transforms;
  transforms.reserve(pose.transforms.size());
  for (const auto& captured : pose.transforms) {
    auto& bone = transforms.emplace_back();
    std::copy(captured.translation.begin(), captured.translation.end(),
              bone.translation);
    std::copy(captured.rotation.begin(), captured.rotation.end(),
              bone.rotation);
    std::copy(captured.scale.begin(), captured.scale.end(), bone.scale);
  }
  if (!RenderManager::SetBoneLocalPose(
        mesh, names.data(), pose.parents.data(), transforms.data(),
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
