cmake_minimum_required(VERSION 4.1.6)
project(timer C)

include(CTest)

set(CMAKE_C_STANDARD 23)

set(sources
	main.c
	parse_time_str.c
	display.c
	timespec_math.c
)
list(TRANSFORM sources PREPEND src/)

# list(APPEND sources ${PROJECT_BINARY_DIR}/parse-datetime.c)
# set(GNULIB_DIR gnulib/lib)
# find_package(BISON REQUIRED)
# find_package(PkgConfig REQUIRED)
# pkg_check_modules(GNULIB REQUIRED IMPORTED_TARGET gnulib)
# target_link_libraries(timer PkgConfig::GNULIB)
# add_dependencies(timer PkgConfig::gnulib)

add_executable(timer ${sources})

include(ExternalProject)
