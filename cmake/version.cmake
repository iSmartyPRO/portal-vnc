# Single source of the embedded product version: the VERSION file.
file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" PVNC_VERSION LIMIT_COUNT 1)
string(STRIP "${PVNC_VERSION}" PVNC_VERSION)
# Numeric form for the Win32 VERSIONINFO resource (a.b.c -> a,b,c,0).
string(REPLACE "." ";" _pvnc_v "${PVNC_VERSION}")
list(GET _pvnc_v 0 PVNC_VER_MAJOR)
list(GET _pvnc_v 1 PVNC_VER_MINOR)
list(GET _pvnc_v 2 PVNC_VER_PATCH)
set(PVNC_VERSION_COMMA "${PVNC_VER_MAJOR},${PVNC_VER_MINOR},${PVNC_VER_PATCH},0")
