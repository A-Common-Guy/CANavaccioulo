#include "stablecops/lely/MotorDriver.hpp"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "stablecops/ds402/ObjectDictionary.hpp"

namespace stablecops::lely {

namespace {

// Per-PDO configuration the master pushes to the drive (over SDO) while the node
// is still pre-operational, mirroring the vendor's CSP bring-up recipe (manual
// Table 5-5). The drive ships its PDOs with transmission type 0 (event-driven),
// so it never streams TPDOs on SYNC and never runs the cyclic exchange; we must
// rewrite them to type 1 (cyclic synchronous). The COB-IDs and mappings are the
// vendor defaults and match dcf/master.dcf exactly so both ends stay coherent.
struct PdoSetup {
    const char* label;
    uint16_t comm_index;  // 0x14xx (RPDO) / 0x18xx (TPDO)
    uint16_t map_index;   // 0x16xx (RPDO) / 0x1Axx (TPDO)
    uint32_t cob_id;
    uint8_t transmission_type;
    std::array<uint32_t, 4> entries;  // index<<16 | subindex<<8 | bit length
    uint8_t entry_count;
};

constexpr std::array<PdoSetup, 5> kPdoSetup{{
    {"RPDO1", 0x1400, 0x1600, 0x201, 1,
     {0x60400010u, 0x60FF0020u, 0x60600008u, 0u}, 3},
    {"RPDO2", 0x1401, 0x1601, 0x301, 1,
     {0x607A0020u, 0x60810020u, 0u, 0u}, 2},
    {"RPDO3", 0x1402, 0x1602, 0x401, 1,
     {0x60830020u, 0x60840020u, 0u, 0u}, 2},
    {"TPDO1", 0x1800, 0x1A00, 0x181, 1,
     {0x60410010u, 0x60610008u, 0x60770010u, 0u}, 3},
    {"TPDO2", 0x1801, 0x1A01, 0x281, 1,
     {0x60640020u, 0x606C0020u, 0u, 0u}, 2},
}};

constexpr uint32_t kCobIdValidMask = 0x80000000u;

std::ostream& writeHex(std::ostream& stream, uint32_t value, int width) {
    const auto flags = stream.flags();
    const auto fill = stream.fill();
    stream << "0x" << std::uppercase << std::hex
           << std::setw(width) << std::setfill('0') << value;
    stream.flags(flags);
    stream.fill(fill);
    return stream;
}

void writeObjectName(std::ostream& stream,
                     const char* name,
                     uint16_t index,
                     uint8_t subindex) {
    const auto flags = stream.flags();
    const auto fill = stream.fill();
    stream << "  " << name << " ";
    writeHex(stream, index, 4)
        << ':' << std::setw(2) << std::setfill('0') << std::uppercase
        << std::hex << static_cast<int>(subindex) << std::dec << " = ";
    stream.flags(flags);
    stream.fill(fill);
}

}  // namespace

MotorDriver::MotorDriver(::lely::canopen::AsyncMaster& master,
                         uint8_t node_id,
                         BootActionConfig boot_actions)
    : ::lely::canopen::FiberDriver(master, node_id),
      drive_(*this),
      boot_actions_(std::move(boot_actions)) {}

ds402::DriveController& MotorDriver::drive() {
    return drive_;
}

const ds402::DriveController& MotorDriver::drive() const {
    return drive_;
}

bool MotorDriver::isCommandObject(uint16_t index) const {
    switch (index) {
        case ds402::od::controlword:
        case ds402::od::modes_of_operation:
        case ds402::od::target_position:
        case ds402::od::target_velocity:
        case ds402::od::profile_velocity:
        case ds402::od::profile_acceleration:
        case ds402::od::profile_deceleration:
            return true;
        default:
            return false;
    }
}

bool MotorDriver::isFeedbackObject(uint16_t index) const {
    switch (index) {
        case ds402::od::statusword:
        case ds402::od::modes_of_operation_display:
        case ds402::od::position_actual_value:
        case ds402::od::velocity_actual_value:
        case ds402::od::torque_actual_value:
            return true;
        default:
            return false;
    }
}

uint8_t MotorDriver::readU8(uint16_t index, uint8_t subindex) {
    if (index == ds402::od::modes_of_operation_display && rpdo_seen_) {
        return static_cast<uint8_t>(static_cast<int8_t>(feedback_.mode));
    }
    return Wait(AsyncRead<uint8_t>(index, subindex));
}

uint16_t MotorDriver::readU16(uint16_t index, uint8_t subindex) {
    if (rpdo_seen_) {
        if (index == ds402::od::statusword) {
            return feedback_.statusword;
        }
        if (index == ds402::od::torque_actual_value) {
            return static_cast<uint16_t>(feedback_.torque);
        }
    }
    return Wait(AsyncRead<uint16_t>(index, subindex));
}

uint32_t MotorDriver::readU32(uint16_t index, uint8_t subindex) {
    return Wait(AsyncRead<uint32_t>(index, subindex));
}

int32_t MotorDriver::readI32(uint16_t index, uint8_t subindex) {
    if (rpdo_seen_) {
        if (index == ds402::od::position_actual_value) {
            return feedback_.position;
        }
        if (index == ds402::od::velocity_actual_value) {
            return feedback_.velocity;
        }
    }
    return Wait(AsyncRead<int32_t>(index, subindex));
}

void MotorDriver::writeU8(uint16_t index, uint8_t subindex, uint8_t value) {
    if (index == ds402::od::modes_of_operation) {
        command_.mode = static_cast<int8_t>(value);
        return;
    }
    if (isCommandObject(index)) {
        return;
    }
    Wait(AsyncWrite(index, subindex, value));
}

void MotorDriver::writeU16(uint16_t index, uint8_t subindex, uint16_t value) {
    if (index == ds402::od::controlword) {
        command_.controlword = value;
        return;
    }
    if (isCommandObject(index)) {
        return;
    }
    Wait(AsyncWrite(index, subindex, value));
}

void MotorDriver::writeU32(uint16_t index, uint8_t subindex, uint32_t value) {
    switch (index) {
        case ds402::od::profile_velocity:
            command_.profile_velocity = value;
            return;
        case ds402::od::profile_acceleration:
            command_.profile_acceleration = value;
            return;
        case ds402::od::profile_deceleration:
            command_.profile_deceleration = value;
            return;
        default:
            break;
    }
    if (isCommandObject(index)) {
        return;
    }
    Wait(AsyncWrite(index, subindex, value));
}

void MotorDriver::writeI32(uint16_t index, uint8_t subindex, int32_t value) {
    if (index == ds402::od::target_position) {
        command_.target_position = value;
        return;
    }
    if (index == ds402::od::target_velocity) {
        command_.target_velocity = value;
        return;
    }
    if (isCommandObject(index)) {
        return;
    }
    Wait(AsyncWrite(index, subindex, value));
}

void MotorDriver::OnBoot(::lely::canopen::NmtState state,
                         char error,
                         const std::string& reason) noexcept {
    if (error != 0) {
        std::cerr << "node " << static_cast<int>(id())
                  << " boot failed: " << reason << " (" << error << ")\n";
        return;
    }

    std::cout << "node " << static_cast<int>(id())
              << " booted, NMT state " << static_cast<int>(state) << '\n';

    if (boot_actions_.inspect) {
        inspectNode();
    }
    runBootActions();
}

void MotorDriver::OnConfig(
    std::function<void(std::error_code)> result) noexcept {
    // The 'update configuration' step runs while the node is still
    // pre-operational, which is the only window in which this drive accepts SDO
    // writes to its command objects and PDO parameters (writing them once the
    // node is operational aborts with 0x00000002). We only reconfigure when a
    // motion action is requested so that --inspect stays read-only.
    std::error_code ec;
    if (wantsMotionAction()) {
        ec = configureDriveForCsp();
        if (ec) {
            std::cerr << "node " << static_cast<int>(id())
                      << " configuration failed: " << ec.message() << '\n';
        }
    }
    result(ec);
}

std::error_code MotorDriver::configureDriveForCsp() noexcept {
    std::cout << "configuring drive for cyclic synchronous operation\n";

    std::error_code ec;
    const auto download = [&](uint16_t index, uint8_t subindex, auto value,
                              const char* what) -> bool {
        Wait(AsyncWrite(index, subindex, value), ec);
        if (ec) {
            std::cerr << "  SDO write to " << what << " (";
            writeHex(std::cerr, index, 4)
                << ':' << std::setw(2) << std::setfill('0') << std::uppercase
                << std::hex << static_cast<int>(subindex) << std::dec
                << ") failed: " << ec.message() << '\n';
        }
        return !ec;
    };

    // Force cyclic synchronous position mode before the state machine moves.
    if (!download(ds402::od::modes_of_operation, ds402::od::default_subindex,
                  static_cast<int8_t>(8), "modes of operation")) {
        return ec;
    }

    for (const auto& pdo : kPdoSetup) {
        // Disable the PDO before touching its parameters, set the transmission
        // type, rewrite the mapping from scratch, then re-enable it. Rewriting
        // the mapping (rather than only the transmission type) defends against
        // the stale/oversized mapping the drive reports straight out of NVM.
        if (!download(pdo.comm_index, 0x01,
                      static_cast<uint32_t>(pdo.cob_id | kCobIdValidMask),
                      "PDO COB-ID (disable)")) {
            return ec;
        }
        if (!download(pdo.comm_index, 0x02,
                      static_cast<uint8_t>(pdo.transmission_type),
                      "PDO transmission type")) {
            return ec;
        }
        if (!download(pdo.map_index, 0x00, static_cast<uint8_t>(0),
                      "PDO mapping count (clear)")) {
            return ec;
        }
        for (uint8_t i = 0; i < pdo.entry_count; ++i) {
            if (!download(pdo.map_index, static_cast<uint8_t>(i + 1),
                          static_cast<uint32_t>(pdo.entries[i]),
                          "PDO mapping entry")) {
                return ec;
            }
        }
        if (!download(pdo.map_index, 0x00,
                      static_cast<uint8_t>(pdo.entry_count),
                      "PDO mapping count")) {
            return ec;
        }
        if (!download(pdo.comm_index, 0x01, static_cast<uint32_t>(pdo.cob_id),
                      "PDO COB-ID (enable)")) {
            return ec;
        }
        std::cout << "  " << pdo.label << " set to transmission type "
                  << static_cast<int>(pdo.transmission_type) << ", "
                  << static_cast<int>(pdo.entry_count) << " mapped objects\n";
    }

    return {};
}

void MotorDriver::OnRpdoWrite(uint16_t index, uint8_t /*subindex*/) noexcept {
    if (isFeedbackObject(index)) {
        rpdo_seen_ = true;
    }
}

void MotorDriver::OnSync(uint8_t /*counter*/, const time_point& /*time*/) noexcept {
    ++sync_count_;

    std::error_code ec;

    // Refresh the cached feedback from the objects the drive maps into its
    // TPDOs. These reads never hit the bus; they snapshot the last received PDO.
    if (rpdo_seen_) {
        const auto statusword =
            rpdo_mapped[ds402::od::statusword][0].Read<uint16_t>(ec);
        if (!ec) {
            feedback_.statusword = statusword;
            feedback_.state = ds402::decodeState(statusword);
        }
        const auto mode =
            rpdo_mapped[ds402::od::modes_of_operation_display][0].Read<int8_t>(ec);
        if (!ec) {
            feedback_.mode = static_cast<ds402::OperationMode>(mode);
        }
        const auto position =
            rpdo_mapped[ds402::od::position_actual_value][0].Read<int32_t>(ec);
        if (!ec) {
            feedback_.position = position;
        }
        const auto velocity =
            rpdo_mapped[ds402::od::velocity_actual_value][0].Read<int32_t>(ec);
        if (!ec) {
            feedback_.velocity = velocity;
        }
        const auto torque =
            rpdo_mapped[ds402::od::torque_actual_value][0].Read<int16_t>(ec);
        if (!ec) {
            feedback_.torque = torque;
        }
    }

    if (!cyclic_active_) {
        return;
    }

    // While bringing CSP up, keep the commanded target glued to the measured
    // position so the drive never sees a position step when it transitions to
    // operation enabled.
    if (csp_track_actual_ && rpdo_seen_) {
        command_.target_position = feedback_.position;
    }

    // Emit the entire command image every cycle. Writing all mapped objects
    // together guarantees the synchronous RPDOs leaving on the next SYNC carry
    // a coherent frame instead of zeroing a neighbour of whatever changed.
    tpdo_mapped[ds402::od::controlword][0].Write(static_cast<uint16_t>(command_.controlword), ec);
    tpdo_mapped[ds402::od::modes_of_operation][0].Write(static_cast<int8_t>(command_.mode), ec);
    tpdo_mapped[ds402::od::target_velocity][0].Write(static_cast<int32_t>(command_.target_velocity), ec);
    tpdo_mapped[ds402::od::target_position][0].Write(static_cast<int32_t>(command_.target_position), ec);
    tpdo_mapped[ds402::od::profile_velocity][0].Write(static_cast<uint32_t>(command_.profile_velocity), ec);
    tpdo_mapped[ds402::od::profile_acceleration][0].Write(static_cast<uint32_t>(command_.profile_acceleration), ec);
    tpdo_mapped[ds402::od::profile_deceleration][0].Write(static_cast<uint32_t>(command_.profile_deceleration), ec);
}

void MotorDriver::inspectNode() noexcept {
    std::cout << "inspecting CANopen/DS402 objects\n";

    auto read_u8 = [this](uint16_t index, uint8_t subindex, const char* name) {
        std::error_code ec;
        const auto value = Wait(AsyncRead<uint8_t>(index, subindex), ec);
        writeObjectName(std::cout, name, index, subindex);
        if (ec) {
            std::cout << "FAILED: " << ec.message() << '\n';
        } else {
            writeHex(std::cout, value, 2) << " (" << static_cast<int>(value) << ")\n";
        }
        return std::pair<uint8_t, bool>{value, !ec};
    };

    auto read_u16 = [this](uint16_t index, uint8_t subindex, const char* name) {
        std::error_code ec;
        const auto value = Wait(AsyncRead<uint16_t>(index, subindex), ec);
        writeObjectName(std::cout, name, index, subindex);
        if (ec) {
            std::cout << "FAILED: " << ec.message() << '\n';
        } else {
            writeHex(std::cout, value, 4) << " (" << value << ")\n";
        }
        return std::pair<uint16_t, bool>{value, !ec};
    };

    auto read_u32 = [this](uint16_t index, uint8_t subindex, const char* name) {
        std::error_code ec;
        const auto value = Wait(AsyncRead<uint32_t>(index, subindex), ec);
        writeObjectName(std::cout, name, index, subindex);
        if (ec) {
            std::cout << "FAILED: " << ec.message() << '\n';
        } else {
            writeHex(std::cout, value, 8) << " (" << value << ")\n";
        }
        return std::pair<uint32_t, bool>{value, !ec};
    };

    auto read_i32 = [this](uint16_t index, uint8_t subindex, const char* name) {
        std::error_code ec;
        const auto value = Wait(AsyncRead<int32_t>(index, subindex), ec);
        writeObjectName(std::cout, name, index, subindex);
        if (ec) {
            std::cout << "FAILED: " << ec.message() << '\n';
        } else {
            writeHex(std::cout, static_cast<uint32_t>(value), 8) << " (" << value << ")\n";
        }
        return std::pair<int32_t, bool>{value, !ec};
    };

    std::cout << " identity\n";
    read_u32(0x1018, 0x01, "vendor id");
    read_u32(0x1018, 0x02, "product code");
    read_u32(0x1018, 0x03, "revision");
    read_u32(0x1018, 0x04, "serial number");

    std::cout << " DS402\n";
    const auto [statusword, status_ok] =
        read_u16(ds402::od::statusword, ds402::od::default_subindex, "statusword");
    if (status_ok) {
        std::cout << "  decoded state = "
                  << ds402::toString(ds402::decodeState(statusword)) << '\n';
    }

    read_u32(0x6502, 0x00, "supported modes");
    read_u8(ds402::od::modes_of_operation, ds402::od::default_subindex, "commanded mode");
    const auto [display_mode, display_mode_ok] = read_u8(
        ds402::od::modes_of_operation_display,
        ds402::od::default_subindex,
        "displayed mode");
    if (display_mode_ok) {
        const auto mode = static_cast<ds402::OperationMode>(
            static_cast<int8_t>(display_mode));
        std::cout << "  decoded mode = " << ds402::toString(mode) << '\n';
    }

    read_i32(ds402::od::position_actual_value, ds402::od::default_subindex, "position");
    read_i32(ds402::od::velocity_actual_value, ds402::od::default_subindex, "velocity");
    read_u16(ds402::od::torque_actual_value, ds402::od::default_subindex, "torque");
    read_u16(ds402::od::error_code, ds402::od::default_subindex, "error code");

    // Drive-side PDO configuration. If the drive never emits TPDOs or ignores
    // RPDOs, the cause is almost always here: a COB-ID with the valid bit set
    // (0x8000_0000), a COB-ID that does not match the master's expectation, or a
    // transmission type that is not cyclic-synchronous (1..240).
    std::cout << " PDO configuration (drive side)\n";
    const auto dump_pdo = [&](const char* label,
                              uint16_t comm_index,
                              uint16_t map_index) {
        const auto [cob_id, cob_ok] = read_u32(comm_index, 0x01, label);
        if (cob_ok) {
            const bool valid = (cob_id & 0x80000000u) == 0;
            std::cout << "    -> COB-ID 0x" << std::hex << (cob_id & 0x7FF)
                      << std::dec << (valid ? ", ENABLED" : ", DISABLED (valid bit set)")
                      << '\n';
        }
        read_u8(comm_index, 0x02, "  transmission type");
        read_u8(map_index, 0x00, "  mapped object count");
    };

    dump_pdo("TPDO1 COB-ID", 0x1800, 0x1A00);
    dump_pdo("TPDO2 COB-ID", 0x1801, 0x1A01);
    dump_pdo("RPDO1 COB-ID", 0x1400, 0x1600);
    dump_pdo("RPDO2 COB-ID", 0x1401, 0x1601);

    read_u16(0x1017, 0x00, "producer heartbeat time");
    read_u32(0x1005, 0x00, "SYNC COB-ID");
}

bool MotorDriver::wantsMotionAction() const {
    return boot_actions_.enable ||
           boot_actions_.hold_position ||
           boot_actions_.csp_target_position.has_value() ||
           boot_actions_.csp_relative_move.has_value();
}

void MotorDriver::runBootActions() noexcept {
    if (!wantsMotionAction()) {
        return;
    }

    try {
        enableDrive(boot_actions_.hold_position ||
                    boot_actions_.csp_target_position.has_value() ||
                    boot_actions_.csp_relative_move.has_value());
        applyCspTarget();
    } catch (const std::exception& exception) {
        std::cerr << "boot action failed: " << exception.what() << '\n';
    }
}

ds402::Feedback MotorDriver::waitForDriveState(ds402::State expected,
                                               std::chrono::milliseconds timeout,
                                               std::chrono::milliseconds poll_interval) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    auto feedback = drive_.readFeedback();

    while (true) {
        if (feedback.state == expected) {
            return feedback;
        }
        if (feedback.state == ds402::State::Fault ||
            feedback.state == ds402::State::FaultReactionActive) {
            throw std::runtime_error("drive entered fault state while waiting for " +
                                     ds402::toString(expected));
        }
        if (feedback.error_code != 0) {
            throw std::runtime_error("drive reported nonzero error code while waiting for " +
                                     ds402::toString(expected));
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error("timed out waiting for " + ds402::toString(expected) +
                                     "; last state was " + ds402::toString(feedback.state));
        }

        USleep(static_cast<uint_least64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(poll_interval).count()));
        feedback = drive_.readFeedback();
    }
}

void MotorDriver::primeCommandImage() {
    // Seed the streamed image from the live drive (over SDO) so the first cyclic
    // frames echo the drive's own setup and never zero a shared-PDO neighbour.
    std::error_code ec;

    const auto mode =
        Wait(AsyncRead<uint8_t>(ds402::od::modes_of_operation_display, 0), ec);
    if (!ec) {
        command_.mode = static_cast<int8_t>(mode);
    }
    const auto position =
        Wait(AsyncRead<int32_t>(ds402::od::position_actual_value, 0), ec);
    if (!ec) {
        command_.target_position = position;
    }
    command_.target_velocity = 0;

    const auto profile_velocity =
        Wait(AsyncRead<uint32_t>(ds402::od::profile_velocity, 0), ec);
    if (!ec) {
        command_.profile_velocity = profile_velocity;
    }
    const auto profile_acceleration =
        Wait(AsyncRead<uint32_t>(ds402::od::profile_acceleration, 0), ec);
    if (!ec) {
        command_.profile_acceleration = profile_acceleration;
    }
    const auto profile_deceleration =
        Wait(AsyncRead<uint32_t>(ds402::od::profile_deceleration, 0), ec);
    if (!ec) {
        command_.profile_deceleration = profile_deceleration;
    }

    command_.controlword = ds402::controlword::shutdown;
}

void MotorDriver::setControlword(uint16_t controlword) {
    command_.controlword = controlword;
}

void MotorDriver::enableDrive(bool prime_csp_target) {
    auto feedback = drive_.readFeedback();
    std::cout << "enabling DS402 operation from state "
              << ds402::toString(feedback.state)
              << ", mode " << ds402::toString(feedback.mode) << '\n';

    if (feedback.state == ds402::State::Fault ||
        feedback.state == ds402::State::FaultReactionActive) {
        throw std::runtime_error("refusing to enable while the drive is in fault");
    }
    if (feedback.error_code != 0) {
        throw std::runtime_error("refusing to enable with nonzero drive error code");
    }

    const bool csp_mode =
        feedback.mode == ds402::OperationMode::CyclicSynchronousPosition;
    if (prime_csp_target && !csp_mode) {
        throw std::runtime_error("CSP hold/target requires cyclic synchronous position mode");
    }

    // Start streaming a coherent command image before touching the state machine.
    // In a cyclic mode the drive will only walk the DS402 states while it is
    // receiving a valid, position-tracking command frame on every SYNC.
    primeCommandImage();
    csp_track_actual_ = csp_mode;
    setControlword(ds402::controlword::shutdown);
    cyclic_active_ = true;

    // Let a few SYNC cycles flow so the drive has the image before we command it.
    {
        const auto deadline = std::chrono::steady_clock::now() +
                              boot_actions_.state_transition_timeout;
        const auto first_cycle = sync_count_;
        while (sync_count_ < first_cycle + 4) {
            if (std::chrono::steady_clock::now() >= deadline) {
                throw std::runtime_error(
                    "no SYNC cycles observed; is the master producing SYNC?");
            }
            USleep(1000);
        }
    }

    if (feedback.state == ds402::State::OperationEnabled) {
        setControlword(ds402::controlword::enable_operation);
        csp_track_actual_ = false;
        std::cout << "  drive already in operation enabled; holding position "
                  << command_.target_position << '\n';
        return;
    }

    // Fault-reset pulse per the vendor recipe (manual Table 5-5): a rising edge
    // on controlword bit 7 clears any latched fault and drops the drive to
    // switch-on-disabled before we walk it up the DS402 ladder. The pulse is
    // streamed for a few SYNC cycles and is harmless when no fault is latched.
    setControlword(ds402::controlword::fault_reset);
    USleep(4000);

    setControlword(ds402::controlword::shutdown);
    feedback = waitForDriveState(
        ds402::State::ReadyToSwitchOn,
        boot_actions_.state_transition_timeout);

    setControlword(ds402::controlword::switch_on);
    feedback = waitForDriveState(
        ds402::State::SwitchedOn,
        boot_actions_.state_transition_timeout);

    setControlword(ds402::controlword::enable_operation);
    feedback = waitForDriveState(
        ds402::State::OperationEnabled,
        boot_actions_.state_transition_timeout);

    // Freeze the held target at the position captured the instant we enabled.
    csp_track_actual_ = false;

    std::cout << "  enabled operation, statusword=";
    writeHex(std::cout, feedback.statusword, 4)
        << " state=" << ds402::toString(feedback.state) << '\n';

    if (csp_mode) {
        std::cout << "  holding current position at "
                  << command_.target_position << '\n';
    }
}

void MotorDriver::applyCspTarget() {
    if (!boot_actions_.csp_target_position && !boot_actions_.csp_relative_move) {
        return;
    }

    const auto feedback = drive_.readFeedback();
    if (feedback.state != ds402::State::OperationEnabled) {
        throw std::runtime_error("CSP target requires operation enabled");
    }
    if (feedback.mode != ds402::OperationMode::CyclicSynchronousPosition) {
        throw std::runtime_error("CSP target requires cyclic synchronous position mode");
    }

    int64_t target = feedback.position;
    if (boot_actions_.csp_target_position) {
        target = *boot_actions_.csp_target_position;
    }
    if (boot_actions_.csp_relative_move) {
        target += *boot_actions_.csp_relative_move;
    }

    if (target < std::numeric_limits<int32_t>::min() ||
        target > std::numeric_limits<int32_t>::max()) {
        throw std::runtime_error("requested CSP target is outside int32 range");
    }

    const auto delta = target - feedback.position;
    if (std::llabs(delta) > boot_actions_.max_position_step) {
        throw std::runtime_error("requested CSP step exceeds --max-position-step");
    }

    drive_.setCspTargetPosition(static_cast<int32_t>(target));
    std::cout << "  CSP target position set to " << target
              << " (delta " << delta << " counts)\n";
}

}  // namespace stablecops::lely
