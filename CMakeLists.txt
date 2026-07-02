cmake_minimum_required(VERSION 3.16)

project(stableCOPS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LELY_COAPP REQUIRED IMPORTED_TARGET liblely-coapp)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(stablecops_lely_project_options INTERFACE)
target_compile_features(stablecops_lely_project_options INTERFACE cxx_std_17)
target_compile_options(stablecops_lely_project_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(stablecops_lely_project_options INTERFACE PkgConfig::LELY_COAPP)
message(STATUS "Lely CANopen dependency found through pkg-config: liblely-coapp")

add_library(stablecops_ds402
    src/ds402/DriveController.cpp
    src/ds402/State.cpp
)
target_include_directories(stablecops_ds402 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(stablecops_ds402 PUBLIC stablecops_lely_project_options)

add_library(stablecops_config
    src/config/PdoMap.cpp
)
target_include_directories(stablecops_config
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(stablecops_config PUBLIC stablecops_lely_project_options)

add_library(stablecops_runtime
    src/app/CanopenApplication.cpp
    src/app/Bus.cpp
    src/app/MotorDrive.cpp
    src/app/RealtimeScheduling.cpp
    src/lely/MotorDriver.cpp
)
target_include_directories(stablecops_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(stablecops_runtime PUBLIC stablecops_ds402 stablecops_config Threads::Threads)

add_executable(stablecops_master
    src/tools/stablecops_master.cpp
)
target_link_libraries(stablecops_master PRIVATE stablecops_runtime)

add_executable(stablecops_commissiond
    src/tools/stablecops_commissiond.cpp
)
target_include_directories(stablecops_commissiond PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(stablecops_commissiond PRIVATE stablecops_runtime)

add_executable(pdo_feedback_monitor
    examples/pdo_feedback_monitor.cpp
)
set_target_properties(pdo_feedback_monitor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
)
target_link_libraries(pdo_feedback_monitor PRIVATE stablecops_runtime)

add_executable(enable_and_hold
    examples/enable_and_hold.cpp
)
set_target_properties(enable_and_hold PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
)
target_link_libraries(enable_and_hold PRIVATE stablecops_runtime)

add_executable(csp_position
    examples/csp_position.cpp
)
set_target_properties(csp_position PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
)
target_link_libraries(csp_position PRIVATE stablecops_runtime)

add_executable(csv_velocity
    examples/csv_velocity.cpp
)
set_target_properties(csv_velocity PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
)
target_link_libraries(csv_velocity PRIVATE stablecops_runtime)

add_executable(cst_torque
    examples/cst_torque.cpp
)
set_target_properties(cst_torque PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
)
target_link_libraries(cst_torque PRIVATE stablecops_runtime)

add_executable(pp_move
    examples/pp_move.cpp
)
set_target_properties(pp_move PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
)
target_link_libraries(pp_move PRIVATE stablecops_runtime)

add_executable(pv_velocity
    examples/pv_velocity.cpp
)
set_target_properties(pv_velocity PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
)
target_link_libraries(pv_velocity PRIVATE stablecops_runtime)

add_executable(pt_torque
    examples/pt_torque.cpp
)
set_target_properties(pt_torque PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
)
target_link_libraries(pt_torque PRIVATE stablecops_runtime)

add_executable(multi_drive
    examples/multi_drive.cpp
)
set_target_properties(multi_drive PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
)
target_link_libraries(multi_drive PRIVATE stablecops_runtime)
