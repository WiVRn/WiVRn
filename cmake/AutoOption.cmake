function(auto_option name description default)
	set(${name} ${default} CACHE STRING "${description}")
	set_property(CACHE ${name} PROPERTY STRINGS AUTO ON OFF)
endfunction()
