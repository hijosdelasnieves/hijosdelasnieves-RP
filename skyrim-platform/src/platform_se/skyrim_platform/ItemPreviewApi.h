#pragma once

#include "NapiHelper.h"

// Converts an actual Skyrim DDS asset (loose or BSA-backed) to a PNG data URI
// for CEF. It deliberately accepts only relative texture paths; browser code
// never receives arbitrary filesystem access.
namespace ItemPreviewApi {
Napi::Value GetPngDataUrl(const Napi::CallbackInfo& info);

inline void Register(Napi::Env env, Napi::Object& exports)
{
  exports.Set(
    "getPngDataUrl",
    Napi::Function::New(env, NapiHelper::WrapCppExceptions(GetPngDataUrl)));
}
}
