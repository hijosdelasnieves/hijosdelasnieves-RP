#include "ItemPreviewApi.h"

#include <DirectXTex.h>
#include <bsa/bsa.hpp>
#include <objbase.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr size_t kMaxSourceBytes = 16 * 1024 * 1024;
constexpr size_t kMaxPreviewBytes = 6 * 1024 * 1024;
constexpr char kBase64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::mutex previewMutex;
std::unordered_map<std::string, std::optional<std::string>> previewCache;

std::optional<std::string> NormalizeTexturePath(std::string path)
{
  std::replace(path.begin(), path.end(), '/', '\\');
  std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (path.size() < 13 || path.rfind("textures\\", 0) != 0 ||
      path.find("..") != std::string::npos ||
      path.substr(path.size() - 4) != ".dds") {
    return std::nullopt;
  }
  return path;
}

std::optional<std::vector<uint8_t>> ReadLooseFile(const std::string& path)
{
  const auto fullPath = std::filesystem::path("Data") / path;
  std::error_code ec;
  const auto size = std::filesystem::file_size(fullPath, ec);
  if (ec || size == 0 || size > kMaxSourceBytes) return std::nullopt;
  std::ifstream input(fullPath, std::ios::binary);
  if (!input) return std::nullopt;
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return input.good() ? std::optional<std::vector<uint8_t>>(std::move(bytes))
                      : std::nullopt;
}

std::optional<std::vector<uint8_t>> ReadBsaFile(const std::string& path)
{
  const auto split = path.find_last_of('\\');
  if (split == std::string::npos) return std::nullopt;
  const auto directory = path.substr(0, split);
  const auto filename = path.substr(split + 1);
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator("Data", ec)) {
    if (ec || !entry.is_regular_file() || entry.path().extension() != ".bsa") continue;
    try {
      bsa::tes4::archive archive;
      const auto version = archive.read(entry.path());
      auto file = archive[directory][filename];
      if (!file) continue;
      if (file->compressed()) file->decompress(version);
      if (file->empty() || file->size() > kMaxSourceBytes) continue;
      const auto* data = reinterpret_cast<const uint8_t*>(file->data());
      return std::vector<uint8_t>(data, data + file->size());
    } catch (...) {
      // An unrelated/corrupt archive must not break an otherwise valid store.
    }
  }
  return std::nullopt;
}

std::string Base64Encode(const uint8_t* data, size_t size)
{
  std::string result;
  result.reserve(((size + 2) / 3) * 4);
  for (size_t i = 0; i < size; i += 3) {
    const uint32_t value = (uint32_t(data[i]) << 16) |
      (i + 1 < size ? uint32_t(data[i + 1]) << 8 : 0) |
      (i + 2 < size ? uint32_t(data[i + 2]) : 0);
    result.push_back(kBase64[(value >> 18) & 0x3f]);
    result.push_back(kBase64[(value >> 12) & 0x3f]);
    result.push_back(i + 1 < size ? kBase64[(value >> 6) & 0x3f] : '=');
    result.push_back(i + 2 < size ? kBase64[value & 0x3f] : '=');
  }
  return result;
}

std::optional<std::string> ConvertToPngDataUrl(const std::vector<uint8_t>& source)
{
  DirectX::ScratchImage image;
  if (FAILED(DirectX::LoadFromDDSMemory(source.data(), source.size(),
                                        DirectX::DDS_FLAGS_NONE, nullptr, image))) {
    return std::nullopt;
  }
  const auto* first = image.GetImage(0, 0, 0);
  if (!first) return std::nullopt;

  const HRESULT initResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  DirectX::Blob png;
  const auto result = DirectX::SaveToWICMemory(
    *first, DirectX::WIC_FLAGS_NONE,
    DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), png);
  if (SUCCEEDED(initResult)) CoUninitialize();
  if (FAILED(result) || png.GetBufferSize() == 0 || png.GetBufferSize() > kMaxPreviewBytes) {
    return std::nullopt;
  }
  return "data:image/png;base64," + Base64Encode(
    static_cast<const uint8_t*>(png.GetBufferPointer()), png.GetBufferSize());
}
}

Napi::Value ItemPreviewApi::GetPngDataUrl(const Napi::CallbackInfo& info)
{
  const auto requested = NapiHelper::ExtractString(info[0], "texturePath");
  const auto path = NormalizeTexturePath(requested);
  if (!path) return info.Env().Null();

  std::lock_guard<std::mutex> lock(previewMutex);
  if (const auto it = previewCache.find(*path); it != previewCache.end()) {
    if (it->second) return Napi::String::New(info.Env(), *it->second);
    return info.Env().Null();
  }
  auto source = ReadLooseFile(*path);
  if (!source) source = ReadBsaFile(*path);
  auto preview = source ? ConvertToPngDataUrl(*source) : std::nullopt;
  previewCache.emplace(*path, preview);
  if (preview) return Napi::String::New(info.Env(), *preview);
  return info.Env().Null();
}
