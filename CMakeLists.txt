cmake_minimum_required (VERSION 3.5)
project (mt CXX)

# FIXME: clean up signed/unsigned comparisons and enable the warning.
add_compile_options(-std=c++11 -pedantic -Wall -Werror -Wno-sign-compare)
add_definitions(-DVERSION=\"0.1\" -D_XOPEN_SOURCE=600)

find_package(X11 REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FC REQUIRED fontconfig)
pkg_check_modules(FT REQUIRED freetype2)

include_directories(${FC_INCLUDE_DIRS} ${FT_INCLUDE_DIRS})
link_directories(${FC_LBIRARY_DIRS} ${FT_LIBRARY_DIRS})
add_compile_options(${FC_CFLAGS} ${FT_CFLAGS})

add_executable(mt mt.cc config.h mt.h x.h x.cc)
target_link_libraries(mt -lm -lrt -lutil
                      ${X11_LIBRARIES} ${X11_Xft_LIB}
                      ${FC_LIBRARIES} ${FT_LIBRARIES})
