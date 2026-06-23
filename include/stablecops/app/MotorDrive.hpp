#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "stablecops/app/MotorConfig.hpp"
#include "stablecops/ds402/DriveController.hpp"

namespace stablecops::app {

class CanopenApplication;

// High-level, thread-safe handle to a single CANopen drive. It runs the Lely
// event loop on a dedicated background thread and lets the application read
// feedback and issue setpoints from its own thread without touching Lely
// internals directly. Construction loads the profile and opens the bus; start()
// boots the node and applies the configured boot behaviour (monitor / enable /
// hold) and start cyclic SYNC.
class MotorDrive {
public:
    explicit MotorDrive(MotorConfig config);
    ~MotorDrive();

    MotorDrive(const MotorDrive&) = delete;
    MotorDrive& operator=(const MotorDrive&) = delete;

    // Launch the event-loop thread and reset the master (which boots the node).
    // The Lely application is constructed (and later destroyed) on that thread,
    // because a FiberDriver must live on the thread that runs its tasks. Blocks
    // until the application is constructed; rethrows any construction error
    // (e.g. bad profile path or CAN open failure). Idempotent while running.
    void start();

    // Request a graceful stop (ramp down / de-energise if enabled) and join the
    // loop thread. Idempotent and also called by the destructor.
    void stop();

    bool running() const;

    // Latest feedback snapshot, safe to call from any thread. feedbackLive()
    // becomes true once the first cyclic TPDO has been received and decoded.
    ds402::Feedback feedback() const;
    bool feedbackLive() const;

    // Runtime cyclic setpoints, applied on the loop thread for the next SYNC.
    // They take effect only when the drive is enabled in the matching cyclic
    // mode and the target object is mapped into an active RxPDO.
    void commandPosition(int32_t counts);
    void commandVelocity(int32_t units);
    void commandTorque(int16_t units);

private:
    MotorConfig config_;
    // Owned by the loop thread; exposed to other threads as an atomic pointer
    // that is non-null only while the loop is running.
    std::atomic<CanopenApplication*> app_{nullptr};
    std::thread loop_thread_;
    std::atomic<bool> running_{false};
};

}  // namespace stablecops::app
