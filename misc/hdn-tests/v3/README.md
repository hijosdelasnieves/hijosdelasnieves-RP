# Native face regeneration contracts

Run from any directory with Python 3 and g++ supporting C++20 and ASan/UBSan:

```sh
python3 misc/hdn-tests/v3/run.py
```

An alternate checkout can be selected with `--repo /path/to/checkout`. The runner copies test inputs into a temporary directory, extracts the actual current source, verifies its hashes did not change during the run, and removes generated source, mock SDK wrappers, reports and executables afterwards. It does not install or publish anything.

The tests cover queue limits, scoped mask ownership, generation-bearing actor handles, load/clear/delete cancellation, stale callbacks, component lifetime and the engine entry point. Mocks model identities and the documented call, without implementing GPU allocation or destruction. They cannot establish whether Skyrim actually composes a remote actor's masks; the Windows build and a two-player DEV trial remain required.
