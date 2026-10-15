#pragma once

#include <RE/B/BSTArray.h>

#include <cstdint>

namespace RE {
class Actor;
class TintMask;
class NiSourceTexture;
}

namespace Frida {
// Implemented beside g_queueNiNodeActorId. It must retain/restore the old TLS
// context so the automatic hook cannot substitute another actor's tint list.
void ApplyRemoteFaceTintMasks(std::uint32_t actorId,
                             const RE::BSTArray<RE::TintMask*>& masks,
                             RE::NiSourceTexture* texture);
}

namespace Hdn {
enum class RemoteFaceTintResult
{
  Applied,
  Retry,
  Drop
};

// Call from the game thread after re-resolving the actor by a live handle and
// verifying the exact appearance-base identity. Never retain the actor here.
RemoteFaceTintResult RenderRemoteFaceTints(
  RE::Actor* actor, std::uint32_t expectedBaseId,
  const RE::BSTArray<RE::TintMask*>& masks);
}
