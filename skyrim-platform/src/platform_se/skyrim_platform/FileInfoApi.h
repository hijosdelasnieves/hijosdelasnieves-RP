#pragma once

#include "NapiHelper.h"

namespace FileInfoApi {

Napi::Value FileInfo(const Napi::CallbackInfo& info);

inline void Register(Napi::Env env, Napi::Object& exports)
{
  // HDN 2026-10-14: con WrapCppExceptions, como el resto del módulo. Sin ella, ValidateFilename (nombres con espacio o
  // apóstrofo: «JKs Whiterun.esp») y FileInfo («can't read») lanzaban std::runtime_error, que node-addon-api solo
  // convierte si es Napi::Error: la excepción de C++ atravesaba V8 sin pasar por ningún catch de JS y dejaba el motor
  // tocado en el primer fotograma tras la carga (pantalla negra; hdn-diag/sp-nativo-carga/DIAGNOSTICO-Y-ARREGLO.md).
  exports.Set("getFileInfo",
              Napi::Function::New(env, NapiHelper::WrapCppExceptions(FileInfo)));
}

}
