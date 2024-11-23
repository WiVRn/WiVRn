add_executable(wivrnctl main.cpp)
set_target_properties(wivrnctl PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/server)
target_link_libraries(wivrnctl PRIVATE CLI11::CLI11 PkgConfig::SYSTEMD)

install(TARGETS wivrnctl)

