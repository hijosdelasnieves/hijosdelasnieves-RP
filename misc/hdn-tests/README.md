# Remote player face texture regression checks

Production composes the tint masks directly into the face material of an
explicitly registered dynamic appearance actor. It does not change FormIDs or
patch engine branches. Runtime references use weak generation-bearing handles,
are checked against their exact dynamic base, and are retired on loadGame.

The pure queue tests cover 256 slots, coalescing, delay, expiry, cancellation,
fairness, reused identities and two renders per update for 200 actors. The
renderer test compiles the real production helper against small fake CommonLib
interfaces and exercises identity/readiness, allocation errors, shader type,
stale geometry/material and 1000 texture replacements without retained GPU
resources. They validate control flow and ownership, not Skyrim's runtime ABI.

Run `bash misc/hdn-tests/run-remote-faces.sh` on Linux. Windows CI builds the
actual implementation against the pinned CommonLib. Visual validation needs
two updated Skyrim clients (including a different race/sex, preview and reload).

Engine interface reference, independently adapted to CommonLib:
https://github.com/tiltedphoques/TiltedEvolution/blob/4917189f6ac382e874f9ee735c4fef6a350e449e/Code/client/Systems/FaceGenSystem.cpp
