cmake_minimum_required (VERSION 3.5)
project (mt C CXX)

add_compile_options(-std=c99 -pedantic -Wall)
add_definitions(-DVERSION=\"0.1\" -D_XOPEN_SOURCE=600)

find_package(X11 REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FC REQUIRED fontconfig)
pkg_check_modules(FT REQUIRED freetype2)

include_directories(${FC_INCLUDE_DIRS} ${FT_INCLUDE_DIRS})
link_directories(${FC_LBIRARY_DIRS} ${FT_LIBRARY_DIRS})
add_compile_options(${FC_CFLAGS} ${FT_CFLAGS})

add_executable(mt mt.c arg.h config.h mt.h win.h x.c)
target_link_libraries(mt -lm -lrt -lutil
                      ${X11_LIBRARIES} ${X11_Xft_LIB}
                      ${FC_LIBRARIES} ${FT_LIBRARIES})
