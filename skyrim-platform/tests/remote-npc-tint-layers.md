# Remote NPC face layers (DEV candidate)

`configureRemoteNpcTintLayers(baseFormId, masks)` prepares immutable TESNPC layers before placing an appearance clone created by TESModPlatform.CreateNpc. It accepts complete mask batches, resolves TINI against the clone race and sex, and installs only game-heap allocations owned by TESNPC. The registry observes identities and never frees committed layers. Identical repeated configuration returns the original result; changes after configuration and first configuration after SP PlaceAtMe are rejected.

CreateNpc isolates mutable HeadRelatedData and detaches template tint layers. EvaluateLeveledNpc and the generic CloneNpc implementation are unchanged. Existing ClearTintMasks/PushTintMask and the Frida mask override are bypassed only for actors whose own registered base was configured. Player-local and legacy paths remain unchanged.

The placement guard covers Skyrim Platform PlaceAtMe calls. Direct calls by another native/Papyrus plugin are outside this API contract. Runtime is restricted to AE 1.6.1170. No manual GPU compositor, texture target lifetime changes, binary offsets, or player formID spoofing are introduced.

Run the CPU ownership and matching contracts from repository root:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g -I skyrim-platform/src/platform_se/skyrim_platform skyrim-platform/tests/remote-npc-tint-layers.contract.cpp -o /tmp/remote-npc-tint-layers
ASAN_OPTIONS=detect_leaks=1 /tmp/remote-npc-tint-layers
```

These tests validate matching, colors, alpha, allocation failures and ownership. Windows CI must compile and link the actual CommonLib binding. Visual acceptance still requires two Windows clients in DEV; CPU fixtures do not execute Skyrim FaceGen or the GPU.
