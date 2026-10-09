# Same target/CRT/linkage as upstream's triplet. Only a host build tool is passed.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED PKG_CONFIG)
