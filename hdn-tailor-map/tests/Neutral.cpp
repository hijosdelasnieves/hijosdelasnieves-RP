#include <Windows.h>

// Recoverable tombstone: a valid Windows DLL with no SKSE entry points.
// The launcher overwrites files but does not delete abandoned plugins.
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
  return TRUE;
}
