# Client-side OpenXR runtime for the headless encoder benchmark: an unpatched, in-process
# Monado build (null compositor + remote HMD), unrelated to wivrn-server's own Monado.
# Unpatched because patches/monado/0002 and 0008 assume wivrn-server supplies symbols this
# runtime doesn't have.

set(BENCH_RUNTIME_DIR ${CMAKE_BINARY_DIR}/bench-runtime)

# Not yet upstream: https://gitlab.freedesktop.org/monado/monado/-/merge_requests/3006
# (null compositor FPS + device size, remote HMD resolution). Fetched fresh each configure so it
# always reflects the MR's current state; drop this whole block once merged and monado-rev moves
# past it. Uses the .patch (mbox) form, not .diff: patches/apply.sh applies via `git am`.
set(MONADO_MR3006_PATCH ${CMAKE_BINARY_DIR}/monado-mr3006.patch)
file(DOWNLOAD https://gitlab.freedesktop.org/monado/monado/-/merge_requests/3006.patch
	${MONADO_MR3006_PATCH}
	STATUS MONADO_MR3006_STATUS
)
list(GET MONADO_MR3006_STATUS 0 MONADO_MR3006_STATUS_CODE)
if (NOT MONADO_MR3006_STATUS_CODE EQUAL 0)
	message(FATAL_ERROR "Could not fetch monado!3006 (merged already? network down?): ${MONADO_MR3006_STATUS}")
endif()

FetchContent_Declare(monado-bench
	GIT_REPOSITORY   https://gitlab.freedesktop.org/monado/monado.git
	GIT_TAG          ${MONADO_REV}
	PATCH_COMMAND    ${CMAKE_SOURCE_DIR}/patches/apply.sh ${MONADO_MR3006_PATCH}
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
		-DXRT_BUILD_DRIVER_REMOTE=ON
		-DXRT_FEATURE_SERVICE=OFF
		-DXRT_FEATURE_CLIENT_DEBUG_GUI=OFF
		-DXRT_BUILD_DRIVER_STEAMVR_LIGHTHOUSE=OFF
		-DXRT_BUILD_DRIVER_SURVIVE=OFF
	BUILD_COMMAND    ${CMAKE_COMMAND} --build <BINARY_DIR> --target openxr_monado
	BUILD_BYPRODUCTS
		${BENCH_RUNTIME_DIR}/openxr_monado-dev.json
)
