

set(BENCHMARK_DOWNLOAD_DEPENDENCIES true)
add_subdirectory(benchmark)
add_subdirectory(json)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmark)
add_subdirectory(tinymembench)
unset(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
add_library(cxxopts INTERFACE)
target_include_directories(cxxopts INTERFACE cxxopts/include)


# Only configure jemalloc for non-Apple builds, as we cannot properly replace the allocator on Mac using static linking:
#   If you ./configure jemalloc with --with-jemalloc-prefix="" on OSX and static link the library, your malloc's will
#   be handled by jemalloc. However, dynamically linked libraries will still use the default system zone. That is, any
#   *.so files that get dynamically linked to your application will not use jemalloc, even if the main object file has
#   been statically linked to jemalloc. This is different from linux. On linux, if the main object file is statically
#   linked to jemalloc, then all malloc's performed by libraries dynamically linked to your application will go
#   through jemalloc.
#  https://github.com/exabytes18/jemalloc-example
set(JEMALLOC_LIB_PATH ${CMAKE_CURRENT_BINARY_DIR}/jemalloc/lib/libjemalloc.so)

if(CMAKE_CURRENT_SOURCE_DIR MATCHES "[ \t\r\n]" OR CMAKE_CURRENT_BINARY_DIR MATCHES "[ \t\r\n]")
  # It looks like `--prefix=<...>` does not work too well if the prefix has a whitespace. We did not spend too long
  # trying to get this to work as (1) this is easy to fix by the user, (2) it has only come up once in a long time, and
  # (3) Makefiles are notoriously bad with spaces: http://savannah.gnu.org/bugs/?712
  message(FATAL_ERROR "jemalloc cannot be built with whitespaces in the directory path.")
endif()

include(ExternalProject)
set(JEMALLOC_COMPILER_ENVIRONMENT "CC=\"${CMAKE_C_COMPILER_LAUNCHER} ${CMAKE_C_COMPILER}\" CXX=\"${CMAKE_CXX_COMPILER_LAUNCHER} ${CMAKE_CXX_COMPILER}\"")

externalproject_add(
    libjemalloc

    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/jemalloc
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/jemalloc
    # Executing autoconf and the configure script with suppressed output, only printing it in case of non-zero return
    CONFIGURE_COMMAND bash -c "cd <SOURCE_DIR> &&  if [ ! -f ./configure ] $<SEMICOLON> then autoconf $<SEMICOLON> fi && \
                               cd <BINARY_DIR> && output=$(${JEMALLOC_COMPILER_ENVIRONMENT} <SOURCE_DIR>/configure --with-malloc-conf=\"retain:true\" --prefix=<BINARY_DIR> 2>&1) || \
                               (printf \"$output\\n\" && false)"
    BUILD_COMMAND bash -c "output=$(make 2>&1) || (printf \"$output\\n\" && false)"
    INSTALL_COMMAND ""  # No install command
    STEP_TARGETS build
    BUILD_BYPRODUCTS ${JEMALLOC_LIB_PATH}
)

file(GLOB_RECURSE JEMALLOC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/jemalloc/* EXCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/jemalloc/configure)
externalproject_add_step(
  libjemalloc
  check_for_changes
  # Make sure that we rebuild jemalloc when a file changes
  DEPENDERS configure
  DEPENDS "${JEMALLOC_FILES}"
  COMMAND bash -c "(cd ${CMAKE_CURRENT_BINARY_DIR}/jemalloc && make distclean >/dev/null 2>/dev/null) || true"
)

add_library(custom_jemalloc SHARED IMPORTED GLOBAL)
add_dependencies(custom_jemalloc libjemalloc)
set_target_properties(custom_jemalloc PROPERTIES IMPORTED_LOCATION ${JEMALLOC_LIB_PATH})