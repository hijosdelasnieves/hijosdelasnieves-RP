'use strict';
// Source-contract checks only; these do not emulate Skyrim or prove ownership.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const read = file => fs.readFileSync(path.join(__dirname, '..', file), 'utf8');
const loader = read('src/Plugin.cpp');
const studio = read('src/Studio.cpp');
const safety = read('include/BackendSafety.hpp');
let checks = 0;
function check(value) { checks++; assert(value); }
check(safety.includes('privateBackendValidated = false;'));
const quarantine = loader.indexOf('if (!hdn::studio::privateBackendValidated)');
check(quarantine > 0);
check(quarantine < loader.indexOf('is_regular_file(library)'));
check(quarantine < loader.indexOf('SKSE::Init(skse'));
check(quarantine < loader.indexOf('Register(hdn::studio::registerPapyrus)'));
check(/if \(!hdn::studio::privateBackendValidated\) \{[\s\S]*?return true;\s*\}/.test(loader));
const snapshot = studio.slice(studio.indexOf('bool snapshot('), studio.indexOf('bool frame('));
check(snapshot.indexOf('if (!privateBackendValidated || !enabled || !hooked)') < snapshot.indexOf('policy.select('));
check(snapshot.indexOf('sceneReason(state)') < snapshot.indexOf('actor->Get3D('));
check(snapshot.indexOf('sceneReason(state)') < snapshot.indexOf('source->Clone()'));
check(snapshot.indexOf('return false;', snapshot.indexOf('if (reason != RenderReason::none)')) < snapshot.indexOf('source->Clone()'));
check(studio.includes('return privateBackendValidated && enabled && hooked ? 1 : 0;'));
check(studio.includes('enabled = admitPrivateBackend('));
check(!/numCustomRendering\s*=/.test(studio));
check(!/numPausesGame\s*=/.test(studio));
check(!/Clear3D\s*\(/.test(studio));
console.log(`${checks} safety source contracts passed (not a Skyrim render test)`);
