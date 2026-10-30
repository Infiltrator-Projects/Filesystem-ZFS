cmake_minimum_required(VERSION 3.20)
project(FilesystemZFS VERSION 1.0.0 LANGUAGES C)
include(CTest)
find_package(OpenSSL REQUIRED)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(FetchContent)
set(INFILTRATR_COMMON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INFILTRATR_COMMON_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(infiltratr_common
  GIT_REPOSITORY https://github.com/Infiltrator-Projects/Libraries.git
  GIT_TAG c76ec95ef66fc728df6a906ad75a6a4dd63b419b)
FetchContent_MakeAvailable(infiltratr_common)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.h" "#define LD_VERSION \"1.0.0\"\n")
add_library(zfs-driver SHARED
  native/zfs_native.c
  native/zfs_analysis.c
  native/zfs_worker.c
  module/zfs_module.c
  module/zfs_fixture_bridge.c
  fixtures/zfs_codec.c)
set_source_files_properties(native/zfs_worker.c
  PROPERTIES COMPILE_DEFINITIONS "main=ifs_zfs_worker_main")
set_target_properties(zfs-driver PROPERTIES PREFIX "" OUTPUT_NAME "ZFS" SUFFIX ".so")
target_include_directories(zfs-driver PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/native"
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat"
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(zfs-driver PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
target_compile_options(zfs-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(zfs-driver PRIVATE InfiltratrCommon::Common OpenSSL::Crypto)
if(BUILD_TESTING)
  add_executable(zfs-native-test tests/test_zfs_native.c)
  target_compile_definitions(zfs-native-test PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
  target_include_directories(zfs-native-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/native")
  target_link_libraries(zfs-native-test PRIVATE zfs-driver OpenSSL::Crypto)
  add_test(NAME zfs-original-native-regressions COMMAND zfs-native-test)
  add_executable(zfs-module-test tests/test_zfs_module.c)
  target_include_directories(zfs-module-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(zfs-module-test PRIVATE zfs-driver)
  add_test(NAME zfs-versioned-interface COMMAND zfs-module-test)
endif()
install(TARGETS zfs-driver LIBRARY DESTINATION /System/Filesystems)
