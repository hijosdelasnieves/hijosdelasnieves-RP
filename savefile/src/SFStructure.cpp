#include "savefile/SFStructure.h"

#include <algorithm>
#include <stdexcept>

SaveFile_::RefID SaveFile_::RefID::CreateRefId(SaveFile& parentSaveFile,
                                               uint32_t formId)
{
  RefID res;

  if (static_cast<size_t>(parentSaveFile.formIDArrayCount) !=
      parentSaveFile.formIDArray.size()) {
    throw std::runtime_error("formIDArray count does not match its data");
  }

  const auto existing = std::find(parentSaveFile.formIDArray.begin(),
                                  parentSaveFile.formIDArray.end(), formId);
  size_t index = 0;
  if (existing != parentSaveFile.formIDArray.end()) {
    index = static_cast<size_t>(
              std::distance(parentSaveFile.formIDArray.begin(), existing)) +
      1;
  } else {
    index = parentSaveFile.formIDArray.size() + 1;
    if (index >= 65536) {
      throw std::runtime_error(
        "too many elements was in FormIDArray (" +
        std::to_string(parentSaveFile.formIDArrayCount) + ")");
    }

    // Keep the existing vector intact. The old implementation passed the
    // element count to memcpy as a byte count, corrupting roughly 75% of the
    // generated save's FormID array and leaking the temporary allocation.
    parentSaveFile.formIDArray.push_back(formId);
    parentSaveFile.formIDArrayCount =
      static_cast<uint32_t>(parentSaveFile.formIDArray.size());

    // The appended uint32_t is immediately before unknownTable3.
    parentSaveFile.fileLocationTable.unknownTable3Offset += sizeof(formId);
  }

  // 255 => 00 00 FF
  // 256 => 00 01 00
  // 65536 => error (the current savefile implementation uses 16-bit array
  // indices even though the on-disk RefID has room for more).
  if (index >= 65536) {
    throw std::runtime_error("too many elements was in FormIDArray (" +
                             std::to_string(parentSaveFile.formIDArrayCount) +
                             ")");
  }
  res.byte0 = 0;
  res.byte1 = static_cast<uint8_t>((index / 256) % 256);
  res.byte2 = static_cast<uint8_t>(index % 256);

  return res;
}

SaveFile_::ChangeForm* SaveFile_::SaveFile::GetChangeFormByRefID(
  SaveFile_::RefID refID, const uint8_t& type)
{
  for (auto& form : this->changeForms) {
    if ((form.type & 0b00111111) == type &&
        form.formID == refID) /// Upper 2 bits represent the size of the data
                              /// lengths: zero them
      return &form;
  }
  return nullptr;
}

SaveFile_::GlobalVariables::GlobalVariable*
SaveFile_::SaveFile::GetGlobalvariableByRefID(SaveFile_::RefID& refID)
{
  GlobalData& gData = this->globalDataTable1[GLOBAL_VARIABLES_INDEX];

  if (gData.type != GLOBAL_VARIABLES_INDEX)
    return nullptr;

  GlobalVariables* globalsVar =
    reinterpret_cast<GlobalVariables*>(gData.data.get());

  if (!globalsVar)
    return nullptr;

  for (auto& gVar : globalsVar->globals) {
    if (gVar.formID == refID) {
      return &gVar;
    }
  }
  return nullptr;
}

int64_t SaveFile_::SaveFile::FindIndexInFormIdArray(uint32_t refID)
{
  for (uint32_t i = 0; i < this->formIDArray.size(); ++i) {
    if (this->formIDArray[i] == refID) {
      return i;
    }
  }
  return -1;
}

void SaveFile_::SaveFile::OverwritePluginInfo(
  std::vector<std::string>& newPluginNames)
{
  uint32_t oldSize = this->pluginInfoSize;

  this->pluginInfoSize = 1;
  this->pluginInfo.numPlugins = 0;
  this->pluginInfo.pluginsName.clear();

  this->pluginInfo.numPlugins = static_cast<uint8_t>(newPluginNames.size());

  for (auto& plugin : newPluginNames) {
    this->pluginInfo.pluginsName.push_back(plugin);
    this->pluginInfoSize += uint32_t(2 + plugin.size());
  }

  uint32_t addSize = this->pluginInfoSize - oldSize;

  this->fileLocationTable.formIDArrayCountOffset += addSize;
  this->fileLocationTable.unknownTable3Offset += addSize;
  this->fileLocationTable.globalDataTable1Offset += addSize;
  this->fileLocationTable.globalDataTable2Offset += addSize;
  this->fileLocationTable.changeFormsOffset += addSize;
  this->fileLocationTable.globalDataTable3Offset += addSize;
}
