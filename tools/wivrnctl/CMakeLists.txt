add_executable(wivrnctl main.cpp)
set_target_properties(wivrnctl PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/server)
target_link_libraries(wivrnctl PRIVATE CLI11::CLI11 PkgConfig::SYSTEMD)

install(TARGETS wivrnctl)

install(FILES dist/wivrnctl.bash
	RENAME wivrnctl
	DESTINATION share/bash-completion/completions)

install(FILES dist/_wivrnctl.zsh
	RENAME _wivrnctl
	DESTINATION share/zsh/site-functions)
