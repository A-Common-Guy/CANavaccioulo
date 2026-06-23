#pragma once

#include <functional>
#include <memory>

#include "stablecops/app/MotorConfig.hpp"
#include "stablecops/ds402/DriveController.hpp"
#include "stablecops/lely/MotorDriver.hpp"

namespace stablecops::app {

class CanopenApplication {
public:
    explicit CanopenApplication(const MotorConfig& config);
    ~CanopenApplication();

    CanopenApplication(const CanopenApplication&) = delete;
    CanopenApplication& operator=(const CanopenApplication&) = delete;

    stablecops::lely::MotorDriver& motor();

    void resetMaster();
    void run();
    void stop();

    // Schedule a function to run on the event-loop thread. Thread-safe; this is
    // the only correct way to touch Lely/driver state from another thread.
    void post(std::function<void()> task);

    // Thread-safe telemetry, readable from any thread while run() executes.
    ds402::Feedback feedback() const;
    bool feedbackLive() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace stablecops::app
