# Client-side OpenXR runtime for the headless encoder benchmark.
#
# wivrn-server's own Monado is built without the null compositor and the simulated HMD, so it
# cannot stand in for the headset. This builds a second Monado configuration from the same
# patched source that can, and tools/perfetto/wivrn_session.py runs it as the client's runtime.
#
# Produces, under ${CMAKE_BINARY_DIR}/bench-runtime:
#   src/xrt/targets/service/monado-service
#   src/xrt/targets/openxr/libopenxr_monado.so
#   openxr_monado-dev.json
#
# Requires ${monado_SOURCE_DIR}, so include this after FetchContent_MakeAvailable(monado).

set(BENCH_RUNTIME_DIR ${CMAKE_BINARY_DIR}/bench-runtime)

# Linked into every executable and shared library of the sub-build. C_STANDARD_LIBRARIES lands at
# the end of the link line, which is where an archive has to be to resolve undefined symbols.
add_library(bench-runtime-symbols STATIC ${CMAKE_CURRENT_LIST_DIR}/bench_runtime_symbols.c)
set_target_properties(bench-runtime-symbols PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(BENCH_RUNTIME_SYMBOLS $<TARGET_FILE:bench-runtime-symbols>)

ExternalProject_Add(bench-runtime
	SOURCE_DIR       ${monado_SOURCE_DIR}
	BINARY_DIR       ${BENCH_RUNTIME_DIR}
	DOWNLOAD_COMMAND ""
	UPDATE_COMMAND   ""
	PATCH_COMMAND    ""
	INSTALL_COMMAND  ""
	BUILD_ALWAYS     TRUE
	DEPENDS          bench-runtime-symbols
	CMAKE_ARGS
		-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
		-DCMAKE_C_STANDARD_LIBRARIES=${BENCH_RUNTIME_SYMBOLS}
		-DCMAKE_CXX_STANDARD_LIBRARIES=${BENCH_RUNTIME_SYMBOLS}
		# What wivrn-server's Monado leaves out, and what the headless client needs.
		-DXRT_FEATURE_COMPOSITOR_NULL=ON
		-DXRT_BUILD_DRIVER_SIMULATED=ON
		-DXRT_FEATURE_SERVICE=ON
		# Socket activation and these two drivers do not link without wivrn-server.
		-DXRT_FEATURE_SERVICE_SYSTEMD=OFF
		-DXRT_BUILD_DRIVER_STEAMVR_LIGHTHOUSE=OFF
		-DXRT_BUILD_DRIVER_SURVIVE=OFF
	BUILD_COMMAND    ${CMAKE_COMMAND} --build <BINARY_DIR> --target monado-service openxr_monado
	BUILD_BYPRODUCTS
		${BENCH_RUNTIME_DIR}/src/xrt/targets/service/monado-service
		${BENCH_RUNTIME_DIR}/openxr_monado-dev.json
)
