#pragma once

namespace hdn::studio {
// Incident 2026-10-08, DLL 0.1.2: EndSession destroyed the copied skeleton
// and failed in aligned_free. The same backend could not render in HDN's
// normal UI state. An INI opt-in is not evidence of correct engine ownership.
// Keep this backend quarantined until both problems have an engine-tested
// replacement. Changing configuration must never re-enable the known CTD.
inline constexpr bool privateBackendValidated = false;

constexpr bool admitPrivateBackend(bool requested, bool runtimeSupported)
{
  return privateBackendValidated && requested && runtimeSupported;
}
}
