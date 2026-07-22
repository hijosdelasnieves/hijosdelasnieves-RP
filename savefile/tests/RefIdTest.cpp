#include "savefile/SFReader.h"
#include "savefile/SFStructure.h"
#include "savefile/SFWriter.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void Require(bool condition, std::string_view message)
{
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

uint32_t DecodeArrayIndex(const SaveFile_::RefID& refId)
{
  return (static_cast<uint32_t>(refId.byte0) << 16) |
    (static_cast<uint32_t>(refId.byte1) << 8) |
    static_cast<uint32_t>(refId.byte2);
}

class TemporarySave
{
public:
  TemporarySave()
  {
    const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() /
      ("skymp-savefile-refid-" + std::to_string(nonce) + ".ess");
  }

  ~TemporarySave()
  {
    std::error_code error;
    std::filesystem::remove(path, error);
  }

  std::filesystem::path path;
};
}

int main()
{
  try {
    SaveFile_::SaveFile save;
    save.formIDArray = { 0x10, 0x20, 0x30, 0x40 };
    save.formIDArrayCount = static_cast<uint32_t>(save.formIDArray.size());
    save.fileLocationTable.unknownTable3Offset = 4096;

    const auto original = save.formIDArray;
    const auto existing = SaveFile_::RefID::CreateRefId(save, 0x30);
    Require(DecodeArrayIndex(existing) == 3,
            "an existing FormID must reuse its one-based array index");
    Require(save.formIDArray == original,
            "reusing a FormID must not modify the array");
    Require(save.fileLocationTable.unknownTable3Offset == 4096,
            "reusing a FormID must not move unknownTable3");

    const auto appended = SaveFile_::RefID::CreateRefId(save, 0x50);
    Require(DecodeArrayIndex(appended) == 5,
            "an appended FormID must return its one-based array index");
    Require(save.formIDArray.size() == 5 && save.formIDArrayCount == 5,
            "the FormID array count must track the vector");
    Require(
      std::equal(original.begin(), original.end(), save.formIDArray.begin()),
      "appending a FormID must preserve every existing uint32_t");
    Require(save.formIDArray.back() == 0x50,
            "the requested FormID must be appended");
    Require(save.fileLocationTable.unknownTable3Offset == 4100,
            "appending a FormID must move unknownTable3 by four bytes");

    save.formIDArrayCount = 1;
    bool rejectedInconsistentCount = false;
    try {
      (void)SaveFile_::RefID::CreateRefId(save, 0x60);
    } catch (const std::runtime_error&) {
      rejectedInconsistentCount = true;
    }
    Require(rejectedInconsistentCount,
            "an inconsistent FormID array must fail closed");

    auto templateSave =
      SaveFile_::Reader(SKYMP_TEMPLATE_SAVE_PATH).GetStructure();
    Require(templateSave != nullptr, "the embedded template save must parse");
    const auto templateFormIds = templateSave->formIDArray;
    const auto templateUnknownOffset =
      templateSave->fileLocationTable.unknownTable3Offset;
    constexpr uint32_t whiterunWorld = 0x0001A26F;
    Require(std::find(templateFormIds.begin(), templateFormIds.end(),
                      whiterunWorld) == templateFormIds.end(),
            "the fixture expects WhiterunWorld to be appended");

    const auto templateRef =
      SaveFile_::RefID::CreateRefId(*templateSave, whiterunWorld);
    Require(DecodeArrayIndex(templateRef) ==
              static_cast<uint32_t>(templateFormIds.size() + 1),
            "the template must reference the newly appended FormID");
    Require(std::equal(templateFormIds.begin(), templateFormIds.end(),
                       templateSave->formIDArray.begin()),
            "all template FormIDs must survive the append");
    Require(templateSave->fileLocationTable.unknownTable3Offset ==
              templateUnknownOffset + sizeof(uint32_t),
            "the template unknownTable3 offset must account for the append");

    TemporarySave generated;
    Require(SaveFile_::Writer(templateSave).CreateSaveFile(generated.path),
            "the modified template must be writable");
    auto reparsed = SaveFile_::Reader(generated.path.string()).GetStructure();
    Require(reparsed->formIDArray == templateSave->formIDArray,
            "every FormID must survive a write/read round trip");
    Require(reparsed->formIDArray.back() == whiterunWorld,
            "the generated save must end with WhiterunWorld");
  } catch (const std::exception& error) {
    std::cerr << "savefile_refid_test: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
