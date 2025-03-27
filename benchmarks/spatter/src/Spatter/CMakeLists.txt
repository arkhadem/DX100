# Spatter/src/Spatter/CMakeLists.txt
enable_language(ASM)
if (USE_CUDA)
    add_library(cuda_backend SHARED CudaBackend.cu)
    set_target_properties(cuda_backend PROPERTIES CUDA_RUNTIME_LIBRARY Shared)
    set(CUDA_INCLUDE_FILES CudaBackend.hh)
endif()

set(SPATTER_INCLUDE_FILES
    ${CUDA_INCLUDE_FILES}
    Configuration.hh
    Input.hh
    JSONParser.hh
    PatternParser.hh
    SpatterTypes.hh
    AlignedAllocator.hh
    Timer.hh
    )

add_library(Spatter STATIC
    ${SPATTER_INCLUDE_FILES}
    Configuration.cc
    JSONParser.cc
    PatternParser.cc
    Timer.cc
    m5op.S
    )

add_library(Spatter_MAA STATIC
    ${SPATTER_INCLUDE_FILES}
    Configuration.cc
    JSONParser.cc
    PatternParser.cc
    Timer.cc
    m5op.S
)

add_library(Spatter_MAA_1K STATIC
    ${SPATTER_INCLUDE_FILES}
    Configuration.cc
    JSONParser.cc
    PatternParser.cc
    Timer.cc
    m5op.S
)

add_library(Spatter_MAA_2K STATIC
    ${SPATTER_INCLUDE_FILES}
    Configuration.cc
    JSONParser.cc
    PatternParser.cc
    Timer.cc
    m5op.S
)

add_library(Spatter_MAA_4K STATIC
    ${SPATTER_INCLUDE_FILES}
    Configuration.cc
    JSONParser.cc
    PatternParser.cc
    Timer.cc
    m5op.S
)

add_library(Spatter_MAA_8K STATIC
    ${SPATTER_INCLUDE_FILES}
    Configuration.cc
    JSONParser.cc
    PatternParser.cc
    Timer.cc
    m5op.S
)

add_library(Spatter_MAA_16K STATIC
    ${SPATTER_INCLUDE_FILES}
    Configuration.cc
    JSONParser.cc
    PatternParser.cc
    Timer.cc
    m5op.S
)

add_library(Spatter_MAA_32K STATIC
    ${SPATTER_INCLUDE_FILES}
    Configuration.cc
    JSONParser.cc
    PatternParser.cc
    Timer.cc
    m5op.S
)

add_library(Spatter_MAA_4C STATIC
    ${SPATTER_INCLUDE_FILES}
    Configuration.cc
    JSONParser.cc
    PatternParser.cc
    Timer.cc
    m5op.S
)

add_library(Spatter_MAA_8C STATIC
    ${SPATTER_INCLUDE_FILES}
    Configuration.cc
    JSONParser.cc
    PatternParser.cc
    Timer.cc
    m5op.S
)

add_library(Spatter_MAA_16C STATIC
    ${SPATTER_INCLUDE_FILES}
    Configuration.cc
    JSONParser.cc
    PatternParser.cc
    Timer.cc
    m5op.S
)

add_library(Spatter_shared SHARED
    ${SPATTER_INCLUDE_FILES}
    Configuration.cc
    JSONParser.cc
    PatternParser.cc
    Timer.cc
    )

set_target_properties(Spatter_MAA PROPERTIES
    COMPILE_DEFINITIONS "${COMMON_COMPILE_DEFINITIONS};MAA;NUM_CORES=4;TILE_SIZE=16384"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
    OUTPUT_NAME "Spatter_MAA"
)

set_target_properties(Spatter_MAA_1K PROPERTIES
    COMPILE_DEFINITIONS "${COMMON_COMPILE_DEFINITIONS};MAA;NUM_CORES=4;TILE_SIZE=1024"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
    OUTPUT_NAME "Spatter_MAA_1K"
)

set_target_properties(Spatter_MAA_2K PROPERTIES
    COMPILE_DEFINITIONS "${COMMON_COMPILE_DEFINITIONS};MAA;NUM_CORES=4;TILE_SIZE=2048"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
    OUTPUT_NAME "Spatter_MAA_2K"
)

set_target_properties(Spatter_MAA_4K PROPERTIES
    COMPILE_DEFINITIONS "${COMMON_COMPILE_DEFINITIONS};MAA;NUM_CORES=4;TILE_SIZE=4096"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
    OUTPUT_NAME "Spatter_MAA_4K"
)

set_target_properties(Spatter_MAA_8K PROPERTIES
    COMPILE_DEFINITIONS "${COMMON_COMPILE_DEFINITIONS};MAA;NUM_CORES=4;TILE_SIZE=8192"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
    OUTPUT_NAME "Spatter_MAA_8K"
)

set_target_properties(Spatter_MAA_16K PROPERTIES
    COMPILE_DEFINITIONS "${COMMON_COMPILE_DEFINITIONS};MAA;NUM_CORES=4;TILE_SIZE=16384"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
    OUTPUT_NAME "Spatter_MAA_16K"
)

set_target_properties(Spatter_MAA_32K PROPERTIES
    COMPILE_DEFINITIONS "${COMMON_COMPILE_DEFINITIONS};MAA;NUM_CORES=4;TILE_SIZE=32768"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
    OUTPUT_NAME "Spatter_MAA_32K"
)

set_target_properties(Spatter_MAA_4C PROPERTIES
    COMPILE_DEFINITIONS "${COMMON_COMPILE_DEFINITIONS};MAA;NUM_CORES=4;TILE_SIZE=16384"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
    OUTPUT_NAME "Spatter_MAA_4C"
)

set_target_properties(Spatter_MAA_8C PROPERTIES
    COMPILE_DEFINITIONS "${COMMON_COMPILE_DEFINITIONS};MAA;NUM_CORES=8;TILE_SIZE=16384"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
    OUTPUT_NAME "Spatter_MAA_8C"
)

set_target_properties(Spatter_MAA_16C PROPERTIES
    COMPILE_DEFINITIONS "${COMMON_COMPILE_DEFINITIONS};MAA;NUM_CORES=16;TILE_SIZE=16384"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
    OUTPUT_NAME "Spatter_MAA_16C"
)

set_target_properties(Spatter PROPERTIES
    COMPILE_DEFINITIONS "${COMMON_COMPILE_DEFINITIONS}"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
    OUTPUT_NAME "Spatter"
    )

set_target_properties(Spatter_shared PROPERTIES
    COMPILE_DEFINITIONS "${COMMON_COMPILE_DEFINITIONS}"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
    OUTPUT_NAME "Spatter"
    )

target_include_directories(Spatter
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../experiments>  # Add this line
    $<INSTALL_INTERFACE:include/Spatter>
)

target_include_directories(Spatter_MAA
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../experiments>  # Add this line
    $<INSTALL_INTERFACE:include/Spatter>
)

target_include_directories(Spatter_MAA_1K
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../experiments>  # Add this line
    $<INSTALL_INTERFACE:include/Spatter>
)

target_include_directories(Spatter_MAA_2K
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../experiments>  # Add this line
    $<INSTALL_INTERFACE:include/Spatter>
)

target_include_directories(Spatter_MAA_4K
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../experiments>  # Add this line
    $<INSTALL_INTERFACE:include/Spatter>
)

target_include_directories(Spatter_MAA_8K
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../experiments>  # Add this line
    $<INSTALL_INTERFACE:include/Spatter>
)

target_include_directories(Spatter_MAA_16K
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../experiments>  # Add this line
    $<INSTALL_INTERFACE:include/Spatter>
)

target_include_directories(Spatter_MAA_32K
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../experiments>  # Add this line
    $<INSTALL_INTERFACE:include/Spatter>
)

target_include_directories(Spatter_MAA_4C
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../experiments>  # Add this line
    $<INSTALL_INTERFACE:include/Spatter>
)

target_include_directories(Spatter_MAA_8C
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../experiments>  # Add this line
    $<INSTALL_INTERFACE:include/Spatter>
)

target_include_directories(Spatter_MAA_16C
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../experiments>  # Add this line
    $<INSTALL_INTERFACE:include/Spatter>
)

target_include_directories(Spatter_shared
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../experiments>  # Add this line
    $<INSTALL_INTERFACE:include/Spatter>
    )

if (USE_CUDA)
    set(COMMON_LINK_LIBRARIES ${COMMON_LINK_LIBRARIES} cuda_backend)
endif()

target_link_libraries(Spatter
    PUBLIC
    ${COMMON_LINK_LIBRARIES}
    )

target_link_libraries(Spatter_MAA
    PUBLIC
    ${COMMON_LINK_LIBRARIES}
    )

target_link_libraries(Spatter_MAA_1K
    PUBLIC
    ${COMMON_LINK_LIBRARIES}
    )

target_link_libraries(Spatter_MAA_2K
    PUBLIC
    ${COMMON_LINK_LIBRARIES}
    )

target_link_libraries(Spatter_MAA_4K
    PUBLIC
    ${COMMON_LINK_LIBRARIES}
    )

target_link_libraries(Spatter_MAA_8K
    PUBLIC
    ${COMMON_LINK_LIBRARIES}
    )

target_link_libraries(Spatter_MAA_16K
    PUBLIC
    ${COMMON_LINK_LIBRARIES}
    )

target_link_libraries(Spatter_MAA_32K
    PUBLIC
    ${COMMON_LINK_LIBRARIES}
    )

target_link_libraries(Spatter_MAA_4C
    PUBLIC
    ${COMMON_LINK_LIBRARIES}
    )

target_link_libraries(Spatter_MAA_8C
    PUBLIC
    ${COMMON_LINK_LIBRARIES}
    )

target_link_libraries(Spatter_MAA_16C
    PUBLIC
    ${COMMON_LINK_LIBRARIES}
    )

target_link_libraries(Spatter_shared
    PUBLIC
    ${COMMON_LINK_LIBRARIES}
    )

target_compile_options(Spatter
    PRIVATE ${WARNING_FLAGS}
    )

target_compile_options(Spatter_MAA
    PRIVATE ${WARNING_FLAGS}
    )

target_compile_options(Spatter_MAA_1K
    PRIVATE ${WARNING_FLAGS}
    )

target_compile_options(Spatter_MAA_2K
    PRIVATE ${WARNING_FLAGS}
    )

target_compile_options(Spatter_MAA_4K
    PRIVATE ${WARNING_FLAGS}
    )

target_compile_options(Spatter_MAA_8K
    PRIVATE ${WARNING_FLAGS}
    )

target_compile_options(Spatter_MAA_16K
    PRIVATE ${WARNING_FLAGS}
    )

target_compile_options(Spatter_MAA_32K
    PRIVATE ${WARNING_FLAGS}
    )

target_compile_options(Spatter_MAA_4C
    PRIVATE ${WARNING_FLAGS}
    )

target_compile_options(Spatter_MAA_8C
    PRIVATE ${WARNING_FLAGS}
    )

target_compile_options(Spatter_MAA_16C
    PRIVATE ${WARNING_FLAGS}
    )

target_compile_options(Spatter_shared
    PRIVATE
    ${WARNING_FLAGS}
    )

# Library/Header installation section

#set(ConfigPackageLocation lib/cmake/Spatter)

#install(TARGETS Spatter
#    EXPORT SpatterTargets
#    LIBRARY DESTINATION lib
#    ARCHIVE DESTINATION lib
#    RUNTIME DESTINATION lib
#    INCLUDES DESTINATION include
#    )

#install(FILES ${SPATTER_INCLUDE_FILES}
#    DESTINATION include
#    )

#install(EXPORT SpatterTargets
#    FILE SpatterTargets.cmake
#    NAMESPACE Spatter::
#    DESTINATION ${ConfigPackageLocation}
#    )

#include(CMakePackageConfigHelpers)

#write_basic_package_version_file(
#    SpatterConfigVersion.cmake
#    VERSION ${PACKAGE_VERSION}
#    COMPATIBILITY AnyNewerVersion
#)

#configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
#    "${CMAKE_CURRENT_BINARY_DIR}/SpatterConfig.cmake"
#    INSTALL_DESTINATION ${ConfigPackageLocation}
#    )

#configure_file(SpatterConfig.cmake.in Spatter.cmake @ONLY)

#export(EXPORT SpatterTargets
#       FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/SpatterTargets.cmake"
#       NAMESPACE Spatter::
#)

#install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SpatterConfig.cmake"
#              "${CMAKE_CURRENT_BINARY_DIR}/SpatterConfigVersion.cmake"
#        DESTINATION ${ConfigPackageLocation}
#        )
