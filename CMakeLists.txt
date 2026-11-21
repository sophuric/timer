cmake_minimum_required(VERSION 4.1.6)
project(timer C)

include(CTest)

set(CMAKE_C_STANDARD 23)

set(sources
	main.c
	parse_time_str.c
	display.c
	timespec_math.c
	io.c
	draw_text.c
)
list(TRANSFORM sources PREPEND src/)

find_program(DATE_BIN_PATH date REQUIRED)
add_compile_definitions(DATE_BIN_PATH=${DATE_BIN_PATH})
message("Date path: " ${DATE_BIN_PATH})

find_program(FIGLET_BIN_PATH figlet REQUIRED)
add_compile_definitions(FIGLET_BIN_PATH=${FIGLET_BIN_PATH})
message("Figlet path: " ${FIGLET_BIN_PATH})

add_executable(timer ${sources})

add_compile_options(
	-Wall
	-Werror
	-fsanitize=address
)
add_link_options(
	-fsanitize=address
)

include(ExternalProject)
