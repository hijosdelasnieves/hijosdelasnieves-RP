#include "ArmorTextures.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>

using namespace hdn::studio;
std::vector<std::string> split(std::string value, char delimiter)
{
  std::vector<std::string> fields;
  std::size_t start = 0;
  for (;;) {
    const auto end = value.find(delimiter, start);
    fields.push_back(value.substr(start, end - start));
    if (end == std::string::npos)
      return fields;
    start = end + 1;
  }
}
int main(int argc, char** argv)
{
  assert(armorTextures == nullptr);
  assert(textureKey("Data\\Meshes\\Clothes/Boots.NIF", true) ==
         "clothes/boots.nif");
  std::vector<ArmorTextureOverride> values{
    { "clothes/boots.nif",
      "boots",
      0,
      { "green.dds", "green_n.dds", "mask.dds", "glow.dds", "height.dds",
        "environment.dds", "multilayer.dds", "backlight.dds" } },
    { "clothes/cape.nif", "hooded", 0, { "black.dds", "linen_n.dds" } },
    { "clothes/duplicate.nif", "cloth", 1, { "purple.dds" } }
  };
  {
    const ArmorTextureScope scope(values);
    const auto boots = findArmorTexture("Meshes/Clothes/Boots.NIF", "Boots", 0,
                                        { "boots", "body" });
    assert(boots == &values[0]);
    assert(
      !findArmorTexture("clothes/boots.nif", "body", 1, { "boots", "body" }));
    assert(!findArmorTexture("other/boots.nif", "boots", 0, { "boots" }));
    assert(
      findArmorTexture("clothes/boots.nif", "boots", 3, { "body", "boots" }));
    assert(!findArmorTexture("clothes/duplicate.nif", "cloth", 0,
                             { "cloth", "cloth" }));
    assert(findArmorTexture("clothes/duplicate.nif", "cloth", 1,
                            { "cloth", "cloth" }));
    assert(!findArmorTexture("clothes/duplicate.nif", "missing", 1,
                             { "cloth", "cloth" }));
    assert(findArmorTexture("clothes/cape.nif", "main", 0, { "main" }));
    assert(
      !findArmorTexture("clothes/cape.nif", "body", 1, { "main", "body" }));
    std::array<std::string, 10> maps;
    maps.fill("original.dds");
    applyArmorTexture(maps, *boots);
    assert(maps[0] == "green.dds" && maps[1] == "green_n.dds");
    assert(maps[5] == "mask.dds" && maps[2] == "glow.dds");
    assert(maps[3] == "height.dds" && maps[4] == "environment.dds");
    assert(maps[6] == "multilayer.dds" && maps[7] == "backlight.dds");
    assert(maps[8] == "original.dds" && maps[9] == "original.dds");
    applyArmorTexture(maps, values[1]);
    assert(maps[0] == "black.dds" && maps[1] == "linen_n.dds");
    for (std::size_t i = 2; i < 8; ++i)
      assert(maps[i].empty());
    {
      const std::vector<ArmorTextureOverride> empty;
      const ArmorTextureScope nested(empty);
      assert(!findArmorTexture("clothes/boots.nif", "boots", 0, { "boots" }));
    }
    assert(armorTextures == &values);
    std::thread worker([] { assert(armorTextures == nullptr); });
    worker.join();
  }
  assert(armorTextures == nullptr);
  assert(!findArmorTexture("clothes/boots.nif", "boots", 0, { "boots" }));
  assert(argc == 2);
  std::ifstream fixtures(argv[1]);
  assert(fixtures.good());
  std::string line;
  std::set<std::string> skus;
  std::size_t tested = 0, nifVerified = 0;
  while (std::getline(fixtures, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty() || line.starts_with('#'))
      continue;
    const auto fields = split(line, '|');
    assert(fields.size() == 15);
    ArmorTextureOverride value{ textureKey(fields[2], true),
                                textureKey(fields[3]),
                                static_cast<std::uint32_t>(
                                  std::stoul(fields[4])),
                                {} };
    std::copy_n(fields.begin() + 6, 8, value.textures.begin());
    const auto names = split(fields[5], '~');
    const std::vector<ArmorTextureOverride> variant{ value };
    const ArmorTextureScope scope(variant);
    const auto geometry = std::find(names.begin(), names.end(), value.shape);
    const auto index = std::count(names.begin(), names.end(), value.shape) == 1
      ? static_cast<std::size_t>(geometry - names.begin())
      : value.index;
    assert(index < names.size());
    const auto selected =
      findArmorTexture(fields[2], names[index], index, names);
    assert(selected);
    std::array<std::string, 10> maps;
    maps.fill("previous-variant.dds");
    applyArmorTexture(maps, *selected);
    constexpr std::array<std::size_t, 8> expected{ 0, 1, 5, 2, 3, 4, 6, 7 };
    for (std::size_t i = 0; i < expected.size(); ++i)
      assert(maps[expected[i]] == fields[6 + i]);
    assert(!findArmorTexture("unrelated/skin.nif", fields[3], index, names));
    skus.insert(fields[0]);
    ++tested;
    if (fields[14] == "NIF")
      ++nifVerified;
  }
  assert(skus.size() == 105 && tested >= 210);
  std::cout << skus.size() << " catalog SKUs; " << tested
            << " sex/shape swaps; " << nifVerified
            << " parsed-NIF fixtures PASS\n";
  std::cout
    << "Armor textures: shape/model isolation, all TXST slots, empty maps, "
       "duplicates, nested scopes and thread isolation PASS\n";
}
