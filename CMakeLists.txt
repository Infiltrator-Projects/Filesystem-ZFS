cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" ZFS_VERSION LIMIT_COUNT 1)
project(FilesystemZFS VERSION ${ZFS_VERSION} LANGUAGES C)
include(CTest)
find_package(OpenSSL REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(FetchContent)
set(INFILTRATR_COMMON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INFILTRATR_COMMON_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(infiltratr_common
  GIT_REPOSITORY https://github.com/Infiltrator-Projects/Libraries.git
  GIT_TAG d23a52693fd3212368d327e1c1302e07ae9510ec)
FetchContent_MakeAvailable(infiltratr_common)
# Common is a link-time dependency, never part of the ZFS installation payload.
set_property(DIRECTORY "${infiltratr_common_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.h" "#define LD_VERSION \"${PROJECT_VERSION}\"\n")
# Only the canonical V2 format inspector and Common adapter enter ZFS.so.
# The historical full analyser stays in a non-installed regression target.
set(ZFS_FIRMWARE_ELF "${CMAKE_CURRENT_BINARY_DIR}/firmware/ZFS.so")
add_custom_command(OUTPUT "${ZFS_FIRMWARE_ELF}"
 COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_SOURCE_DIR}/module/firmware"
   -B "${CMAKE_CURRENT_BINARY_DIR}/firmware" -DCMAKE_BUILD_TYPE=Release
   "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}" "-DCOMMON_INCLUDE_DIR=${infiltratr_common_SOURCE_DIR}/include"
 COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_CURRENT_BINARY_DIR}/firmware"
 DEPENDS module/firmware/CMakeLists.txt module/firmware/runtime.c module/zfs_module.c
   native/zfs_disk.c native/zfs_native.h native/zfs_hash.h native/zfs_hash.c
   "${infiltratr_common_SOURCE_DIR}/include/infiltratr/filesystem_module_v2.h"
 VERBATIM)
add_custom_target(zfs-firmware DEPENDS "${ZFS_FIRMWARE_ELF}")
add_library(zfs-driver SHARED
  native/zfs_disk.c native/zfs_hash.c native/zfs_mos.c native/zfs_create.c
  module/zfs_module.c)
add_dependencies(zfs-driver zfs-firmware)
set_property(TARGET zfs-driver APPEND PROPERTY LINK_DEPENDS "${ZFS_FIRMWARE_ELF}")
add_custom_command(TARGET zfs-driver POST_BUILD
 COMMAND "${CMAKE_OBJCOPY}" --add-section ".infiltrator_boot=${ZFS_FIRMWARE_ELF}"
   --set-section-flags .infiltrator_boot=readonly "$<TARGET_FILE:zfs-driver>" VERBATIM)
set_target_properties(zfs-driver PROPERTIES
  PREFIX "" OUTPUT_NAME "ZFS" SUFFIX ".so" C_VISIBILITY_PRESET hidden)
target_include_directories(zfs-driver PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/native"
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat"
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(zfs-driver PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
target_compile_options(zfs-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(zfs-driver PRIVATE -Wl,-z,defs
  "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/module/v2-only.map")
target_link_libraries(zfs-driver PRIVATE InfiltratrCommon::Common)
if(BUILD_TESTING)
  add_library(zfs-historical-analysis-test-support SHARED
    native/zfs_native.c native/zfs_analysis.c native/zfs_disk.c native/zfs_hash.c)
  target_compile_definitions(zfs-historical-analysis-test-support PRIVATE
    _GNU_SOURCE _FILE_OFFSET_BITS=64)
  target_include_directories(zfs-historical-analysis-test-support PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/native"
    "${infiltratr_common_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat")
  target_compile_options(zfs-historical-analysis-test-support PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(zfs-historical-analysis-test-support PRIVATE
    InfiltratrCommon::Common OpenSSL::Crypto)
  add_executable(zfs-hash-test tests/test_zfs_hash.c native/zfs_hash.c)
  target_include_directories(zfs-hash-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native"
    "${infiltratr_common_SOURCE_DIR}/include")
  target_link_libraries(zfs-hash-test PRIVATE OpenSSL::Crypto)
  add_test(NAME zfs-sha256-openssl-oracle COMMAND zfs-hash-test)
  add_executable(zfs-mos-test tests/test_zfs_mos.c native/zfs_disk.c native/zfs_hash.c native/zfs_mos.c)
  target_include_directories(zfs-mos-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native" "${infiltratr_common_SOURCE_DIR}/include")
  target_compile_options(zfs-mos-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_executable(zfs-format-test tests/test_zfs_format.c)
  target_include_directories(zfs-format-test PRIVATE "${infiltratr_common_SOURCE_DIR}/include")
  target_compile_options(zfs-format-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(zfs-format-test PRIVATE ${CMAKE_DL_LIBS})
  add_test(NAME zfs-native-format COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/format-media.sh" "$<TARGET_FILE:zfs-format-test>" "$<TARGET_FILE:zfs-driver>")
  add_executable(zfs-v2-media tests/test_zfs_v2_media.c)
  target_include_directories(zfs-v2-media PRIVATE "${infiltratr_common_SOURCE_DIR}/include")
  target_compile_options(zfs-v2-media PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(zfs-v2-media PRIVATE ${CMAKE_DL_LIBS})
  add_executable(zfs-native-test tests/test_zfs_native.c)
  target_compile_definitions(zfs-native-test PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
  target_include_directories(zfs-native-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/native")
  target_link_libraries(zfs-native-test PRIVATE zfs-historical-analysis-test-support InfiltratrCommon::Common OpenSSL::Crypto)
  add_test(NAME zfs-original-native-regressions COMMAND zfs-native-test)
  # Preserve the historical ZFS analyser command as a test-only native
  # executable. It is not installed and is never loaded as a filesystem ABI.
  add_executable(zfs-worker-regression native/zfs_worker.c)
  target_include_directories(zfs-worker-regression PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/native"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}")
  target_compile_definitions(zfs-worker-regression PRIVATE
    _GNU_SOURCE _FILE_OFFSET_BITS=64)
  target_link_libraries(zfs-worker-regression PRIVATE
    zfs-historical-analysis-test-support InfiltratrCommon::Common OpenSSL::Crypto)
  add_test(NAME zfs-analyser-source-preserved
    COMMAND zfs-worker-regression --version)
  add_executable(zfs-module-test tests/test_zfs_module.c)
  target_include_directories(zfs-module-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(zfs-module-test PRIVATE OpenSSL::Crypto zfs-driver InfiltratrCommon::Common)
  add_test(NAME zfs-versioned-interface COMMAND zfs-module-test)
  add_test(NAME zfs-v2-only-exports
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_v2_exports.sh"
      "$<TARGET_FILE:zfs-driver>")
endif()
# The tracked snapshot is not the versioned PDF generated by release.py.
set(ZFS_GENERATED_PDF "${CMAKE_CURRENT_BINARY_DIR}/Filesystem-ZFS.pdf")
add_custom_command(
 OUTPUT "${ZFS_GENERATED_PDF}"
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/standalone/generate_manual.py"
         "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
         "${CMAKE_CURRENT_SOURCE_DIR}/docs/ZFS.pages"
         "${ZFS_GENERATED_PDF}"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
         "${CMAKE_CURRENT_SOURCE_DIR}/docs/ZFS.pages"
         "${CMAKE_CURRENT_SOURCE_DIR}/standalone/manual.py"
         "${CMAKE_CURRENT_SOURCE_DIR}/standalone/generate_manual.py"
 VERBATIM)
add_custom_target(zfs-manual ALL DEPENDS "${ZFS_GENERATED_PDF}")
install(TARGETS zfs-driver LIBRARY DESTINATION /System/Filesystems)
install(FILES "${ZFS_GENERATED_PDF}"
        DESTINATION /System/Resources/Documentation)
