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

# When stableCOPS is the top-level project we build everything and install by
# default; when it is pulled into another build (add_subdirectory / FetchContent)
# only the library is built unless the parent opts back in.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(STABLECOPS_TOPLEVEL ON)
else()
    set(STABLECOPS_TOPLEVEL OFF)
endif()
option(STABLECOPS_BUILD_TOOLS "Build the stableCOPS CLI tools" ${STABLECOPS_TOPLEVEL})
option(STABLECOPS_BUILD_EXAMPLES "Build the stableCOPS examples" ${STABLECOPS_TOPLEVEL})
option(STABLECOPS_INSTALL "Generate stableCOPS install and CMake-package rules" ${STABLECOPS_TOPLEVEL})

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
# In-tree tools and examples (skipped by default when embedded as a subproject).
# ---------------------------------------------------------------------------
if(STABLECOPS_BUILD_TOOLS)
    add_executable(stablecops_master src/tools/stablecops_master.cpp)
    # The master tool uses the advanced CanopenApplication API, whose header
    # still exposes Lely types, so it needs the Lely include path directly
    # (unlike the MotorDrive-based examples below).
    target_link_libraries(stablecops_master PRIVATE stablecops PkgConfig::LELY_COAPP)

    add_executable(stablecops_commissiond src/tools/stablecops_commissiond.cpp)
    target_include_directories(stablecops_commissiond PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    target_link_libraries(stablecops_commissiond PRIVATE stablecops)
endif()

if(STABLECOPS_BUILD_EXAMPLES)
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

    # Leg-specific tooling lives under leg/ to keep it separate from the generic
    # examples. zero_leg is CAN-only (no EtherCAT); the EtherCAT-backed leg tools
    # are built in the STABLECOPS_BUILD_ECAT block below.
    add_executable(zero_leg leg/examples/zero_leg.cpp)
    set_target_properties(zero_leg PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples)
    target_link_libraries(zero_leg PRIVATE stablecops)
endif()

# ---------------------------------------------------------------------------
# Optional EtherCAT integration (the vendored motion-faster library), for mixed
# CAN + EtherCAT applications - e.g. reading EtherCAT encoders while moving CAN
# motors. Off by default because motion-faster pulls SOEM + yaml-cpp over the
# network via FetchContent and requires CMake >= 3.28 (its own floor).
# Enable with: cmake --preset default -DSTABLECOPS_BUILD_ECAT=ON
# ---------------------------------------------------------------------------
option(STABLECOPS_BUILD_ECAT "Build the EtherCAT (motion-faster) examples" OFF)

if(STABLECOPS_BUILD_ECAT)
    # Build only motion-faster's library (ecat_core), not its own examples.
    set(MOTION_FASTER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(mm/motion-faster)

    # EtherCAT-only: print the auxiliary (load) encoder of every EtherCAT drive.
    add_executable(motion_faster_encoder examples/motion_faster_encoder.cpp)
    set_target_properties(motion_faster_encoder PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples)
    target_link_libraries(motion_faster_encoder PRIVATE ecat_core)

    # Combined: read EtherCAT encoders (motion-faster) while moving a CAN motor
    # (stableCOPS MotorDrive), both on their own RT/loop threads.
    add_executable(motion_faster_can_move examples/motion_faster_can_move.cpp)
    set_target_properties(motion_faster_can_move PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples)
    target_link_libraries(motion_faster_can_move PRIVATE ecat_core stablecops)

    # Validation: replay a recorded joint trajectory on the two CAN drives (CSP)
    # while logging the foot pitch/roll measured by the two EtherCAT encoders
    # against the expected pitch/roll from the trajectory CSV.
    add_executable(validate_trajectory leg/examples/validate_trajectory.cpp)
    set_target_properties(validate_trajectory PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples)
    target_link_libraries(validate_trajectory PRIVATE ecat_core stablecops)

    # Calibration: drive both CAN joints to their NVM zero and latch the two
    # EtherCAT encoder offsets there (saved to a file for capture_rom).
    add_executable(capture_ecat_zero leg/examples/capture_ecat_zero.cpp)
    set_target_properties(capture_ecat_zero PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples)
    target_link_libraries(capture_ecat_zero PRIVATE ecat_core stablecops)

    # Range-of-motion: read the EtherCAT encoders only (CAN drives left
    # de-energised so the joint backdrives freely) and log centered roll/pitch.
    add_executable(capture_rom leg/examples/capture_rom.cpp)
    set_target_properties(capture_rom PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples)
    target_link_libraries(capture_rom PRIVATE ecat_core)
endif()

# ---------------------------------------------------------------------------
# Install + CMake package export, so a consumer can:
#   find_package(stableCOPS REQUIRED)
#   target_link_libraries(app PRIVATE stableCOPS::stablecops)
# ---------------------------------------------------------------------------
if(STABLECOPS_INSTALL)
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
endif()  # STABLECOPS_INSTALL
