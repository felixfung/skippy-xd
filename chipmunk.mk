# Chipmunk feature policy, matching Meson's skippy_chipmunk feature option:
#   enabled  - use the system library, or build the bundled static fallback
#   auto     - use the system library when available; do not use the fallback
#   disabled - build without Chipmunk support

ifneq "${SKIPPY_CHIPMUNK}" "enabled"
ifneq "${SKIPPY_CHIPMUNK}" "auto"
ifneq "${SKIPPY_CHIPMUNK}" "disabled"
$(error SKIPPY_CHIPMUNK must be enabled, auto, or disabled)
endif
endif
endif

CHIPMUNK_SOURCE_DIR := subprojects/chipmunk2d
CHIPMUNK_SOURCE_MARKER := ${CHIPMUNK_SOURCE_DIR}/CMakeLists.txt
CHIPMUNK_BUILD_DIR ?= build/chipmunk2d-make
CHIPMUNK_STATIC_LIB := ${CHIPMUNK_BUILD_DIR}/src/libchipmunk.a
CHIPMUNK_BUILD_DEP =
CHIPMUNK_SOURCE_DEP =

ifeq "${CFG_DEV}" ""
	CHIPMUNK_CMAKE_BUILD_TYPE ?= Release
else
	CHIPMUNK_CMAKE_BUILD_TYPE ?= Debug
endif

ifneq "${SKIPPY_CHIPMUNK}" "disabled"
	# This compile/link probe does not run the resulting executable, so it also
	# works when cross-compiling.
	CHIPMUNK_SYSTEM_AVAILABLE := $(shell \
		printf '%s\n' \
		'#include <chipmunk/chipmunk.h>' \
		'int main(void) { cpSpace *space = cpSpaceNew(); cpSpaceFree(space); return 0; }' | \
		${CC} ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} \
		-x c - -o /dev/null -lchipmunk -lm >/dev/null 2>&1 && \
		printf yes)

	ifeq "${CHIPMUNK_SYSTEM_AVAILABLE}" "yes"
		CPPFLAGS += -DCFG_CHIPMUNK
		LIBS += -lchipmunk
	else ifeq "${SKIPPY_CHIPMUNK}" "enabled"
		CPPFLAGS += -DCFG_CHIPMUNK
		CPPFLAGS += -I${CHIPMUNK_SOURCE_DIR}/include
		LIBS += ${CHIPMUNK_STATIC_LIB}
		CHIPMUNK_BUILD_DEP := ${CHIPMUNK_STATIC_LIB}
		CHIPMUNK_SOURCE_DEP := ${CHIPMUNK_SOURCE_MARKER}
	endif
endif

CHIPMUNK_SOURCES = \
	$(wildcard ${CHIPMUNK_SOURCE_DIR}/src/*.c) \
	$(wildcard ${CHIPMUNK_SOURCE_DIR}/include/chipmunk/*.h) \
	$(wildcard ${CHIPMUNK_SOURCE_DIR}/include/chipmunk/constraints/*.h)

# mesonbuild/meson#12075: the downloader returns failure for CMake-only
# subprojects even after downloading them successfully. Accept that failure
# only when it created the requested source-tree marker.
${CHIPMUNK_SOURCE_MARKER}: subprojects/chipmunk2d.wrap
	${MESON} subprojects download chipmunk2d || test -f "$@"

${CHIPMUNK_STATIC_LIB}: ${CHIPMUNK_SOURCE_MARKER} ${CHIPMUNK_SOURCES}
	CC="${CC}" ${CMAKE} \
		-S "${CHIPMUNK_SOURCE_DIR}" \
		-B "${CHIPMUNK_BUILD_DIR}" \
		-DBUILD_DEMOS=OFF \
		-DINSTALL_DEMOS=OFF \
		-DBUILD_SHARED=OFF \
		-DBUILD_STATIC=ON \
		-DINSTALL_STATIC=OFF \
		-DCMAKE_BUILD_TYPE="${CHIPMUNK_CMAKE_BUILD_TYPE}" \
		-DCMAKE_C_FLAGS="${CFLAGS} -DCHIPMUNK_FFI"
	${CMAKE} --build "${CHIPMUNK_BUILD_DIR}" --target chipmunk_static

.PHONY: clean-chipmunk
clean-chipmunk:
	@if [ -d "${CHIPMUNK_BUILD_DIR}" ]; then \
		${CMAKE} -E remove_directory "${CHIPMUNK_BUILD_DIR}"; \
	fi
