#include "stablecops/ds402/DriveController.hpp"

#include "stablecops/ds402/ObjectDictionary.hpp"

namespace stablecops::ds402 {

DriveController::DriveController(ObjectAccess& object_access)
    : object_access_(object_access) {}

Feedback DriveController::readFeedback() {
    Feedback feedback;
    feedback.statusword = readStatusword();
    feedback.state = decodeState(feedback.statusword);
    feedback.mode = readMode();
    feedback.position = object_access_.readI32(od::position_actual_value, od::default_subindex);
    feedback.velocity = object_access_.readI32(od::velocity_actual_value, od::default_subindex);
    feedback.torque = static_cast<int16_t>(
        object_access_.readU16(od::torque_actual_value, od::default_subindex));
    feedback.error_code = object_access_.readU16(od::error_code, od::default_subindex);
    return feedback;
}

OperationMode DriveController::readMode() {
    return static_cast<OperationMode>(
        static_cast<int8_t>(object_access_.readU8(
            od::modes_of_operation_display,
            od::default_subindex)));
}

void DriveController::setCspTargetPosition(int32_t target_position) {
    object_access_.writeI32(od::target_position, od::default_subindex, target_position);
}

void DriveController::setCsvTargetVelocity(int32_t target_velocity) {
    object_access_.writeI32(od::target_velocity, od::default_subindex, target_velocity);
}

void DriveController::setCstTargetTorque(int16_t target_torque) {
    object_access_.writeU16(
        od::target_torque,
        od::default_subindex,
        static_cast<uint16_t>(target_torque));
}

void DriveController::setCurrentPositionAsZero() {
    object_access_.writeI32(od::set_current_position_zero, od::default_subindex, 1);
}

void DriveController::storeApplicationParameters() {
    object_access_.writeU32(od::store_parameters,
                            od::store_application_parameters_subindex,
                            od::store_parameters_signature);
}

uint16_t DriveController::readStatusword() {
    return object_access_.readU16(od::statusword, od::default_subindex);
}

}  // namespace stablecops::ds402
