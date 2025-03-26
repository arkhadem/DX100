option(USE_OPENMP "Enable support for OpenMP")


find_package(OpenMP)
set(COMMON_LINK_LIBRARIES ${COMMON_LINK_LIBRARIES} OpenMP::OpenMP_CXX)

