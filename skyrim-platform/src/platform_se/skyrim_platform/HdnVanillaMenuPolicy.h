#pragma once

#include <cstdint>
#include <string_view>

namespace HdnVanillaMenuPolicy {

enum MenuFlag : std::uint32_t
{
  kMagic = 1u << 0,
  kStats = 1u << 1,
  kJournal = 1u << 2,
  kFavorites = 1u << 3,
  kCrafting = 1u << 4,
  kTraining = 1u << 5,
  kLevelUp = 1u << 6,
  kSleepWait = 1u << 7,
};

inline constexpr std::uint32_t kKnownMask = kMagic | kStats | kJournal |
  kFavorites | kCrafting | kTraining | kLevelUp | kSleepWait;

// HDN is a managed runtime. These menus stay closed before JavaScript starts,
// while loading, and after a network disconnect. A future standalone profile
// can explicitly replace this mask; silently falling back to vanilla is never
// an authority decision.
inline constexpr std::uint32_t kFailClosedMask = kKnownMask;
inline constexpr std::uint32_t kPolicyVersion = 1;

constexpr bool IsAnyOf(std::string_view value, std::string_view first,
                       std::string_view second = {},
                       std::string_view third = {},
                       std::string_view fourth = {},
                       std::string_view fifth = {}) noexcept
{
  return value == first || (!second.empty() && value == second) ||
    (!third.empty() && value == third) ||
    (!fourth.empty() && value == fourth) || (!fifth.empty() && value == fifth);
}

constexpr std::uint32_t MenuFlagForName(std::string_view name) noexcept
{
  if (IsAnyOf(name, "MagicMenu", "Magic Menu")) {
    return kMagic;
  }
  if (IsAnyOf(name, "StatsMenu", "Stats Menu")) {
    return kStats;
  }
  if (IsAnyOf(name, "Journal Menu", "JournalMenu")) {
    return kJournal;
  }
  if (IsAnyOf(name, "FavoritesMenu", "Favorites Menu")) {
    return kFavorites;
  }
  if (IsAnyOf(name, "Training Menu", "TrainingMenu")) {
    return kTraining;
  }
  if (IsAnyOf(name, "LevelUp Menu", "LevelUpMenu")) {
    return kLevelUp;
  }
  if (IsAnyOf(name, "Sleep/Wait Menu", "SleepWaitMenu")) {
    return kSleepWait;
  }
  if (IsAnyOf(name, "Crafting Menu", "CraftingMenu", "SmithingMenu",
              "TanningMenu", "Cooking Menu") ||
      IsAnyOf(name, "CookingMenu", "AlchemyMenu", "EnchantConstructMenu")) {
    return kCrafting;
  }
  return 0;
}

constexpr std::uint32_t InputFlagForName(std::string_view name) noexcept
{
  if (IsAnyOf(name, "Magic", "Magic Menu", "MagicMenu", "Quick Magic")) {
    return kMagic;
  }
  if (IsAnyOf(name, "Stats", "Stats Menu", "StatsMenu", "Skills",
              "Quick Stats")) {
    return kStats;
  }
  if (IsAnyOf(name, "Journal", "Journal Menu", "JournalMenu")) {
    return kJournal;
  }
  if (IsAnyOf(name, "Favorites", "Favorites Menu", "FavoritesMenu")) {
    return kFavorites;
  }
  if (IsAnyOf(name, "Wait", "Sleep/Wait", "Sleep/Wait Menu")) {
    return kSleepWait;
  }
  return 0;
}

struct State
{
  std::uint32_t policyVersion = kPolicyVersion;
  std::uint32_t mask = kFailClosedMask;
  bool queueHookInstalled = false;
  bool inputFenceInstalled = false;
  std::uint64_t blockedShowCount = 0;
  std::uint64_t blockedInputCount = 0;
};

// Installs the pre-show UIMessageQueue detour. Safe to call repeatedly.
bool Initialize() noexcept;

// MenuControls is not guaranteed to exist at SKSEPlugin_Load, so its input
// wrapper is installed at DataLoaded and retried from the JS-facing setters.
bool EnsureInputFence() noexcept;

void SetMask(std::uint32_t mask) noexcept;
// Legacy compatibility is intentionally one-way. Old clients call false while
// disconnecting; accepting that would reopen MagicMenu outside HDN authority.
// Selective reuse must go through SetMask instead.
void SetMagicBlocked(bool blocked) noexcept;
std::uint32_t GetMask() noexcept;
State GetState() noexcept;

}
