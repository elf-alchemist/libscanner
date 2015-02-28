message(${CMAKE_BINARY_DIR})

include(CheckFunctionExists)

check_function_exists(stricmp STRICMP_EXISTS)
if(NOT STRICMP_EXISTS)
	add_definitions(-Dstricmp=strcasecmp)
endif()
check_function_exists(strnicmp STRNICMP_EXISTS)
if(NOT STRNICMP_EXISTS)
	add_definitions(-Dstrnicmp=strncasecmp)
endif()

add_executable(declarate
	main.cpp
	scanner.cpp
)

set_target_properties(declarate PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
