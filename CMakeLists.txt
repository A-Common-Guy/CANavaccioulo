cmake_minimum_required(VERSION 3.16)

project(stableCOPS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LELY_COAPP REQUIRED IMPORTED_TARGET liblely-coapp)
message(STATUS "Lely CANopen dependency found through pkg-config: liblely-coapp")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# The library: one shared object that consumers link against.
#
# All sources live in a single target so the install/export is trivial and so
# the Lely dependency stays a private implementation detail. The public headers
# (MotorDrive.hpp and the ds402/app value types) do not include any Lely header,
# so consumers need neither the Lely headers nor an explicit Lely link.
# ---------------------------------------------------------------------------
add_library(stablecops SHARED
    src/log/Log.cpp
    src/ds402/DriveController.cpp
    src/ds402/State.cpp
    src/config/PdoMap.cpp
    src/app/CanopenApplication.cpp
    src/app/Bus.cpp
    src/app/MotorDrive.cpp
    src/app/RealtimeScheduling.cpp
    src/lely/MotorDriver.cpp
)
add_library(stableCOPS::stablecops ALIAS stablecops)

target_include_directories(stablecops
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_compile_features(stablecops PUBLIC cxx_std_17)
target_compile_options(stablecops PRIVATE -Wall -Wextra -Wpedantic)

# PRIVATE: because the library is shared and its public headers don't expose
# Lely/pthread types, these are not propagated to consumers. libstablecops.so
# records liblely-coapp as a runtime dependency (DT_NEEDED); it must be present
# on the target at load time.
target_link_libraries(stablecops PRIVATE PkgConfig::LELY_COAPP Threads::Threads)

set_target_properties(stablecops PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ---------------------------------------------------------------------------
# In-tree tools and examples.
# ---------------------------------------------------------------------------
add_executable(stablecops_master src/tools/stablecops_master.cpp)
# The master tool uses the advanced CanopenApplication API, whose header still
# exposes Lely types, so it needs the Lely include path directly (unlike the
# MotorDrive-based examples below).
target_link_libraries(stablecops_master PRIVATE stablecops PkgConfig::LELY_COAPP)

add_executable(stablecops_commissiond src/tools/stablecops_commissiond.cpp)
target_include_directories(stablecops_commissiond PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(stablecops_commissiond PRIVATE stablecops)

# Every example uses only the Lely-free MotorDrive API.
set(STABLECOPS_EXAMPLES
    pdo_feedback_monitor
    enable_and_hold
    csp_position
    csv_velocity
    cst_torque
    pp_move
    pv_velocity
    pt_torque
    multi_drive
)
foreach(example ${STABLECOPS_EXAMPLES})
    add_executable(${example} examples/${example}.cpp)
    set_target_properties(${example} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
    )
    target_link_libraries(${example} PRIVATE stablecops)
endforeach()

# ---------------------------------------------------------------------------
# Install + CMake package export, so a consumer can:
#   find_package(stableCOPS REQUIRED)
#   target_link_libraries(app PRIVATE stableCOPS::stablecops)
# ---------------------------------------------------------------------------
install(TARGETS stablecops
    EXPORT stableCOPSTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT stableCOPSTargets
    FILE stableCOPSTargets.cmake
    NAMESPACE stableCOPS::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stableCOPS
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/stableCOPSConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/stableCOPSConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stableCOPS
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/stableCOPSConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/stableCOPSConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/stableCOPSConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stableCOPS
)

# Runtime data the library loads by path at boot (the generated master DCF, the
# PDO summary the cyclic layout is built from, and the motor profiles/EDS).
# Installed as sample data under the datadir; integrators point MotorConfig's
# master_dcf_path / summary_path at their own copies.
install(DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/dcf
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/config
    DESTINATION ${CMAKE_INSTALL_DATADIR}/stablecops
)
