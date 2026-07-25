# HDN vanilla authority — client source snapshot

This orphan branch preserves the exact, reviewable client-source integration
used by the production vanilla-authority release without importing unrelated
files from the shared HDN worktree.

Apply
`0001-feat-client-centralize-vanilla-mechanics-authority.patch` to an HDN
source line derived from:

`5ac4cb937bcd455ca5e3cfe754d657257ff9f820`

The patch SHA256 is:

`95e8081e97a4a66169ef947d26864ef986d6c34ea7233eb296c47a12135738ca`

It contains only:

- the fail-closed menu policy and controlled lease registry;
- the level/XP/perk/skill projection and reconciler;
- the pure policy tests;
- the compatible reactive `MenuPolicyService` update;
- registration of one `VanillaAuthorityService`;
- retirement of the four old client-owned progression services;
- the packaging guard update;
- release/pairing notes.

Production pairing on 2026-07-25:

- client `2026.07.25-234823`;
- client ZIP
  `ae185c4777ebdd0ee5078bfe3f10254a9ea397e51dee8e1119d38ec9613b3dd2`;
- client JS
  `7fbb9da47e45cdf7c3353b5ac99a35c186c7114ea00d4ee01231b7ece4d5e1f5`;
- native commit
  `c893ab9d13f503f3ed719404d7175dcb5e2c229c`;
- native runtime ZIP
  `a000b78e0d06e2282f5f171555fd5aacbbe796a3a61c9310771a07c3b01f3b61`.

The branch is intentionally orphaned because the HDN client base contains
local history not present in the publication remote. The patch itself remains
directly auditable and does not claim that unrelated concurrent work belongs
to this feature.
