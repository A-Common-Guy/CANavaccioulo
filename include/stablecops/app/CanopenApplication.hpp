#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "stablecops/app/MotorConfig.hpp"
#include "stablecops/ds402/DriveController.hpp"
#include "stablecops/lely/MotorDriver.hpp"

namespace stablecops::app {

// One CANopen master on one CAN interface, driving one or more DS402 drives off
// a single shared SYNC. The bus-level settings (interface, dcf, summary, master
// node id, sync period, rt) are taken from the first node config; the remaining
// per-node fields apply to each node's own MotorDriver.
class CanopenApplication {
public:
    // Multi-node: node_configs must be non-empty; node_configs.front() supplies
    // the bus-level settings, every entry contributes one drive.
    explicit CanopenApplication(std::vector<MotorConfig> node_configs);
    // Single-node convenience.
    explicit CanopenApplication(const MotorConfig& config);
    ~CanopenApplication();

    CanopenApplication(const CanopenApplication&) = delete;
    CanopenApplication& operator=(const CanopenApplication&) = delete;

    // The drives on this bus, in the order their configs were supplied.
    const std::vector<uint8_t>& nodeIds() const;
    // First drive (kept for single-node call sites).
    stablecops::lely::MotorDriver& motor();
    // The driver for a specific node id; nullptr if this bus has no such node.
    stablecops::lely::MotorDriver* motorIfPresent(uint8_t node_id);

    void resetMaster();
    void run();
    void stop();

    // Begin a coordinated shutdown of the whole bus: graceful-stop every drive,
    // then reset all nodes and stop the loop once they are de-energised (or a
    // bounded grace period elapses). Safe to call from the loop thread.
    void requestShutdown();

    // Schedule a function to run on the event-loop thread. Thread-safe; this is
    // the only correct way to touch Lely/driver state from another thread.
    void post(std::function<void()> task);

    // Thread-safe telemetry, readable from any thread while run() executes.
    // Node-addressed variants target one drive; the no-arg forms use the first.
    ds402::Feedback feedback() const;
    ds402::Feedback feedback(uint8_t node_id) const;
    bool feedbackLive() const;
    bool feedbackLive(uint8_t node_id) const;
    // Shared cyclic cadence of the bus (measured on the first drive's SYNC).
    stablecops::lely::CyclicStats cyclicStats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace stablecops::app
