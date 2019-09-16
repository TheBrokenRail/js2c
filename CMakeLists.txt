cmake_minimum_required(VERSION 3.10)

project(js2c)

file(STRINGS "quickjs/VERSION" QUICKJS_VERSION)

include(GNUInstallDirs)

set(INCLUDE_DIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}/js2c")
set(LIB_DIR "${CMAKE_INSTALL_FULL_LIBDIR}")

add_library(libjs2c SHARED quickjs/quickjs.c quickjs/libregexp.c quickjs/libunicode.c quickjs/cutils.c quickjs/libbf.c src/js_std.c)
target_compile_definitions(libjs2c PUBLIC -D_GNU_SOURCE PUBLIC -DCONFIG_VERSION=\"${QUICKJS_VERSION}\" PUBLIC -DCONFIG_CC=\"${CMAKE_C_COMPILER}\" PUBLIC -DCONFIG_INCLUDE_DIR=\"${INCLUDE_DIR}\" PUBLIC -DCONFIG_LIB_DIR=\"${LIB_DIR}\" PUBLIC -DCONFIG_BIGNUM)
set_target_properties(libjs2c PROPERTIES OUTPUT_NAME js2c)
target_include_directories(libjs2c PUBLIC quickjs)
target_link_libraries(libjs2c m)

add_executable(js2c src/js2c.c)
target_link_libraries(js2c libjs2c)

install(FILES quickjs/quickjs.h DESTINATION ${INCLUDE_DIR})
install(TARGETS libjs2c LIBRARY DESTINATION ${LIB_DIR})
install(TARGETS js2c RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

file(COPY quickjs/quickjs.h DESTINATION ${PROJECT_BINARY_DIR})
