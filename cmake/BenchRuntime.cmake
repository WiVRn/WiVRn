# Client-side OpenXR runtime for the headless encoder benchmark: an in-process Monado build
# (null compositor + remote HMD), unrelated to wivrn-server's own Monado. Built unpatched:
# patches/monado/0002 expects wivrn-server to define listen_socket, and 0008 drops Monado's
# u_git_tag.c generation in favour of WiVRn's, so the patched tree cannot link on its own.

set(BENCH_RUNTIME_DIR ${CMAKE_BINARY_DIR}/bench-runtime)

ExternalProject_Add(bench-runtime
	GIT_REPOSITORY   https://gitlab.freedesktop.org/monado/monado.git
	GIT_TAG          ${MONADO_REV}
	BINARY_DIR       ${BENCH_RUNTIME_DIR}
	UPDATE_COMMAND   ""
	INSTALL_COMMAND  ""
	# Rebuilt on every `cmake --build`, so a bisect step cannot reuse a stale runtime.
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
