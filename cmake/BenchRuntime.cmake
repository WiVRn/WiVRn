# Client-side OpenXR runtime for the headless encoder benchmark: an unpatched, in-process
# Monado build (null compositor + simulated HMD), unrelated to wivrn-server's own Monado.
# Unpatched because patches/monado/0002 and 0008 assume wivrn-server supplies symbols this
# runtime doesn't have; 0012 (frame rate) is applied on its own until it's upstreamed.

set(BENCH_RUNTIME_DIR ${CMAKE_BINARY_DIR}/bench-runtime)

FetchContent_Declare(monado-bench
	GIT_REPOSITORY   https://gitlab.freedesktop.org/monado/monado.git
	GIT_TAG          ${MONADO_REV}
	PATCH_COMMAND    ${CMAKE_SOURCE_DIR}/patches/apply.sh
	                 ${CMAKE_SOURCE_DIR}/patches/monado/0012-c-null-make-the-frame-rate-configurable.patch
	EXCLUDE_FROM_ALL
)
if (POLICY CMP0169)
	cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(monado-bench)
if (NOT monado-bench_POPULATED)
	FetchContent_Populate(monado-bench)
endif()

ExternalProject_Add(bench-runtime
	SOURCE_DIR       ${monado-bench_SOURCE_DIR}
	BINARY_DIR       ${BENCH_RUNTIME_DIR}
	DOWNLOAD_COMMAND ""
	UPDATE_COMMAND   ""
	PATCH_COMMAND    ""
	INSTALL_COMMAND  ""
	BUILD_ALWAYS     TRUE
	CMAKE_ARGS
		-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
		-DXRT_FEATURE_COMPOSITOR_NULL=ON
		-DXRT_BUILD_DRIVER_SIMULATED=ON
		-DXRT_FEATURE_SERVICE=OFF
		-DXRT_FEATURE_CLIENT_DEBUG_GUI=OFF
		-DXRT_BUILD_DRIVER_STEAMVR_LIGHTHOUSE=OFF
		-DXRT_BUILD_DRIVER_SURVIVE=OFF
	BUILD_COMMAND    ${CMAKE_COMMAND} --build <BINARY_DIR> --target openxr_monado
	BUILD_BYPRODUCTS
		${BENCH_RUNTIME_DIR}/openxr_monado-dev.json
)
