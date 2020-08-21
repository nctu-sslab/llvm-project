##===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
##===----------------------------------------------------------------------===##
#
# Build offloading library libomptarget.so.
#
##===----------------------------------------------------------------------===##

libomptarget_say("Building offloading runtime library libomptarget.")

set(src_files
  api.cpp
  device.cpp
  interface.cpp
  rtl.cpp
  omptarget.cpp
  at.cpp
  segment.cpp
  perf.cpp
  rttype.cpp

  mymalloc/mem_layout.cpp
  mymalloc/mmap_mgr.cpp
  mymalloc/mymalloc.cpp
)

# Build libomptarget library with libdl dependency.
add_library(omptarget SHARED ${src_files})
include_directories(mymalloc)
include_directories(./)
#link_directories(/home/pschen/src-pschen/src/llvm-project/openmp/libomptarget/src)
#uvmmalloc
list(APPEND CMAKE_PREFIX_PATH "/home/pschen/src-pschen/src/llvm-project/openmp/libomptarget/src/mymalloc")
find_library(UVM_MALLOC_LIB uvmmalloc)
target_link_libraries(omptarget
  ${UVM_MALLOC_LIB}
  ${CMAKE_DL_LIBS}
  "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports")

# Install libomptarget under the lib destination folder.
install(TARGETS omptarget LIBRARY COMPONENT omptarget
  DESTINATION "${OPENMP_INSTALL_LIBDIR}")
