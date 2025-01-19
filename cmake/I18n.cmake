find_package(Gettext REQUIRED)

function(CREATE_MO_FILES TARGET_NAME I18N_DOMAIN LOCALE_DIR)
	# .po files are located in: ${LOCALE_DIR}/${LANG}/${I18N_DOMAIN}.po
	# for every supported language

	set(MO_FILES)
	file(GLOB PO_FILES CONFIGURE_DEPENDS "${LOCALE_DIR}/*/${I18N_DOMAIN}.po")

	foreach(PO_FILE IN LISTS PO_FILES)
		cmake_path(GET PO_FILE PARENT_PATH PO_DIR)
		cmake_path(GET PO_DIR FILENAME LANG)

		if(ANDROID)
			set(MO_DIR ${CMAKE_ANDROID_ASSETS_DIRECTORIES}/locale/${LANG}/LC_MESSAGES)
		else()
			set(MO_DIR ${CMAKE_CURRENT_BINARY_DIR}/locale/${LANG}/LC_MESSAGES)
		endif()

		set(MO_FILE ${MO_DIR}/${I18N_DOMAIN}.mo)

		add_custom_command(OUTPUT ${MO_FILE}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${MO_DIR}
			COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} ${PO_FILE} -o ${MO_FILE}
			DEPENDS ${PO_FILE})

		set(MO_FILES ${MO_FILES} ${MO_FILE})

		install(FILES ${MO_FILE} DESTINATION ${CMAKE_INSTALL_DATADIR}/locale/${LANG}/LC_MESSAGES)
	endforeach()

	add_custom_target(${TARGET_NAME}-translations ALL DEPENDS ${MO_FILES})
	add_dependencies(${TARGET_NAME} ${TARGET_NAME}-translations)
endfunction()

function(CREATE_GLYPHSET TARGET_NAME I18N_DOMAIN LOCALE_DIR)
	file(GLOB PO_FILES CONFIGURE_DEPENDS "${LOCALE_DIR}/*/${I18N_DOMAIN}.po")

	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/glyph_set.cpp
		COMMAND python3 ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/extract_charset.py ${PO_FILES} > ${CMAKE_CURRENT_BINARY_DIR}/glyph_set.cpp
		DEPENDS ${PO_FILES})

	add_custom_target(${TARGET_NAME}-glyphset ALL DEPENDS ${PO_FILES} ${CMAKE_CURRENT_BINARY_DIR}/glyph_set.cpp)
	add_dependencies(${TARGET_NAME} ${TARGET_NAME}-glyphset)

	target_sources(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/glyph_set.cpp)
endfunction()
