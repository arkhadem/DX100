# Spatter/src/CMakeLists.txt

add_subdirectory(Spatter)

# Enable assembly language
enable_language(ASM)

# Compile m5op.o from the assembly source file
if(BUILD_GEM5)
    add_library(m5op OBJECT ${GEM5_HOME}/util/m5/build/x86/abi/x86/m5op.S)
    target_include_directories(m5op PRIVATE ${GEM5_HOME}/include/ ${GEM5_HOME}/util/m5/src/)
endif()

if(BUILD_FUNC)
    add_definitions(-DFUNC)
endif()

if(BUILD_GEM5)
    add_definitions(-DGEM5)
endif()

# Base version of Spatter
if(BUILD_GEM5)
    add_executable(spatter_base main.cc $<TARGET_OBJECTS:m5op>)
    target_include_directories(spatter_base PRIVATE ${GEM5_HOME}/include/ ${GEM5_HOME}/util/m5/src/ ${MAA_HOME})
else()
    add_executable(spatter_base main.cc)
    target_include_directories(spatter_base PRIVATE ${MAA_HOME})
endif()
target_link_libraries(spatter_base ${COMMON_LINK_LIBRARIES} Spatter)
target_compile_definitions(spatter_base PRIVATE ${COMMON_COMPILE_DEFINITIONS})
set_target_properties(spatter_base PROPERTIES
    COMPILE_OPTIONS "${WARNING_FLAGS}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
)

# MAA version of Spatter
if(BUILD_GEM5)
    add_executable(spatter_maa main.cc $<TARGET_OBJECTS:m5op>)
    target_include_directories(spatter_maa PRIVATE ${GEM5_HOME}/include/ ${GEM5_HOME}/util/m5/src/ ${MAA_HOME})
    add_executable(spatter_maa_1K main.cc $<TARGET_OBJECTS:m5op>)
    target_include_directories(spatter_maa_1K PRIVATE ${GEM5_HOME}/include/ ${GEM5_HOME}/util/m5/src/ ${MAA_HOME})
    add_executable(spatter_maa_2K main.cc $<TARGET_OBJECTS:m5op>)
    target_include_directories(spatter_maa_2K PRIVATE ${GEM5_HOME}/include/ ${GEM5_HOME}/util/m5/src/ ${MAA_HOME})
    add_executable(spatter_maa_4K main.cc $<TARGET_OBJECTS:m5op>)
    target_include_directories(spatter_maa_4K PRIVATE ${GEM5_HOME}/include/ ${GEM5_HOME}/util/m5/src/ ${MAA_HOME})
    add_executable(spatter_maa_8K main.cc $<TARGET_OBJECTS:m5op>)
    target_include_directories(spatter_maa_8K PRIVATE ${GEM5_HOME}/include/ ${GEM5_HOME}/util/m5/src/ ${MAA_HOME})
    add_executable(spatter_maa_16K main.cc $<TARGET_OBJECTS:m5op>)
    target_include_directories(spatter_maa_16K PRIVATE ${GEM5_HOME}/include/ ${GEM5_HOME}/util/m5/src/ ${MAA_HOME})
    add_executable(spatter_maa_32K main.cc $<TARGET_OBJECTS:m5op>)
    target_include_directories(spatter_maa_32K PRIVATE ${GEM5_HOME}/include/ ${GEM5_HOME}/util/m5/src/ ${MAA_HOME})
    add_executable(spatter_maa_4C main.cc $<TARGET_OBJECTS:m5op>)
    target_include_directories(spatter_maa_4C PRIVATE ${GEM5_HOME}/include/ ${GEM5_HOME}/util/m5/src/ ${MAA_HOME})
    add_executable(spatter_maa_8C main.cc $<TARGET_OBJECTS:m5op>)
    target_include_directories(spatter_maa_8C PRIVATE ${GEM5_HOME}/include/ ${GEM5_HOME}/util/m5/src/ ${MAA_HOME})
    add_executable(spatter_maa_16C main.cc $<TARGET_OBJECTS:m5op>)
    target_include_directories(spatter_maa_16C PRIVATE ${GEM5_HOME}/include/ ${GEM5_HOME}/util/m5/src/ ${MAA_HOME})

else()
    add_executable(spatter_maa main.cc)
    target_include_directories(spatter_maa PRIVATE ${MAA_HOME})
    add_executable(spatter_maa_1K main.cc)
    target_include_directories(spatter_maa_1K PRIVATE ${MAA_HOME})
    add_executable(spatter_maa_2K main.cc)
    target_include_directories(spatter_maa_2K PRIVATE ${MAA_HOME})
    add_executable(spatter_maa_4K main.cc)
    target_include_directories(spatter_maa_4K PRIVATE ${MAA_HOME})
    add_executable(spatter_maa_8K main.cc)
    target_include_directories(spatter_maa_8K PRIVATE ${MAA_HOME})
    add_executable(spatter_maa_16K main.cc)
    target_include_directories(spatter_maa_16K PRIVATE ${MAA_HOME})
    add_executable(spatter_maa_32K main.cc)
    target_include_directories(spatter_maa_32K PRIVATE ${MAA_HOME})
    add_executable(spatter_maa_4C main.cc)
    target_include_directories(spatter_maa_4C PRIVATE ${MAA_HOME})
    add_executable(spatter_maa_8C main.cc)
    target_include_directories(spatter_maa_8C PRIVATE ${MAA_HOME})
    add_executable(spatter_maa_16C main.cc)
    target_include_directories(spatter_maa_16C PRIVATE ${MAA_HOME})
endif()

target_link_libraries(spatter_maa ${COMMON_LINK_LIBRARIES} Spatter_MAA)
target_compile_definitions(spatter_maa PRIVATE ${COMMON_COMPILE_DEFINITIONS} MAA NUM_CORES=4 TILE_SIZE=16384)
set_target_properties(spatter_maa PROPERTIES
    COMPILE_OPTIONS "${WARNING_FLAGS}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
)

target_link_libraries(spatter_maa_1K ${COMMON_LINK_LIBRARIES} Spatter_MAA_1K)
target_compile_definitions(spatter_maa_1K PRIVATE ${COMMON_COMPILE_DEFINITIONS} MAA NUM_CORES=4 TILE_SIZE=1024)
set_target_properties(spatter_maa_1K PROPERTIES
    COMPILE_OPTIONS "${WARNING_FLAGS}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
)

target_link_libraries(spatter_maa_2K ${COMMON_LINK_LIBRARIES} Spatter_MAA_2K)
target_compile_definitions(spatter_maa_2K PRIVATE ${COMMON_COMPILE_DEFINITIONS} MAA NUM_CORES=4 TILE_SIZE=2048)
set_target_properties(spatter_maa_2K PROPERTIES
    COMPILE_OPTIONS "${WARNING_FLAGS}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
)

target_link_libraries(spatter_maa_4K ${COMMON_LINK_LIBRARIES} Spatter_MAA_4K)
target_compile_definitions(spatter_maa_4K PRIVATE ${COMMON_COMPILE_DEFINITIONS} MAA NUM_CORES=4 TILE_SIZE=4096)
set_target_properties(spatter_maa_4K PROPERTIES
    COMPILE_OPTIONS "${WARNING_FLAGS}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
)

target_link_libraries(spatter_maa_8K ${COMMON_LINK_LIBRARIES} Spatter_MAA_8K)
target_compile_definitions(spatter_maa_8K PRIVATE ${COMMON_COMPILE_DEFINITIONS} MAA NUM_CORES=4 TILE_SIZE=8192)
set_target_properties(spatter_maa_8K PROPERTIES
    COMPILE_OPTIONS "${WARNING_FLAGS}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
)

target_link_libraries(spatter_maa_16K ${COMMON_LINK_LIBRARIES} Spatter_MAA_16K)
target_compile_definitions(spatter_maa_16K PRIVATE ${COMMON_COMPILE_DEFINITIONS} MAA NUM_CORES=4 TILE_SIZE=16384)
set_target_properties(spatter_maa_16K PROPERTIES
    COMPILE_OPTIONS "${WARNING_FLAGS}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
)

target_link_libraries(spatter_maa_32K ${COMMON_LINK_LIBRARIES} Spatter_MAA_32K)
target_compile_definitions(spatter_maa_32K PRIVATE ${COMMON_COMPILE_DEFINITIONS} MAA NUM_CORES=4 TILE_SIZE=32768)
set_target_properties(spatter_maa_32K PROPERTIES
    COMPILE_OPTIONS "${WARNING_FLAGS}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
)

target_link_libraries(spatter_maa_4C ${COMMON_LINK_LIBRARIES} Spatter_MAA_4C)
target_compile_definitions(spatter_maa_4C PRIVATE ${COMMON_COMPILE_DEFINITIONS} MAA NUM_CORES=4 TILE_SIZE=16384)
set_target_properties(spatter_maa_4C PROPERTIES
    COMPILE_OPTIONS "${WARNING_FLAGS}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
)

target_link_libraries(spatter_maa_8C ${COMMON_LINK_LIBRARIES} Spatter_MAA_8C)
target_compile_definitions(spatter_maa_8C PRIVATE ${COMMON_COMPILE_DEFINITIONS} MAA NUM_CORES=8 TILE_SIZE=16384)
set_target_properties(spatter_maa_8C PROPERTIES
    COMPILE_OPTIONS "${WARNING_FLAGS}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
)

target_link_libraries(spatter_maa_16C ${COMMON_LINK_LIBRARIES} Spatter_MAA_16C)
target_compile_definitions(spatter_maa_16C PRIVATE ${COMMON_COMPILE_DEFINITIONS} MAA NUM_CORES=16 TILE_SIZE=16384)
set_target_properties(spatter_maa_16C PROPERTIES
    COMPILE_OPTIONS "${WARNING_FLAGS}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMPILE_OPTIONS "${COMMON_COMPILE_OPTIONS}"
)

# Add GEM5 library directories
if(BUILD_GEM5)
    link_directories(${GEM5_HOME}/util/m5/build/x86/out)
endif()