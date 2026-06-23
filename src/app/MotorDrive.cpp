#include "stablecops/app/MotorDrive.hpp"

#include <exception>
#include <future>
#include <memory>
#include <utility>

#include "stablecops/app/CanopenApplication.hpp"
#include "stablecops/lely/MotorDriver.hpp"

namespace stablecops::app {

MotorDrive::MotorDrive(MotorConfig config) : config_(std::move(config)) {}

MotorDrive::~MotorDrive() {
    stop();
}

void MotorDrive::start() {
    if (running_.exchange(true)) {
        return;
    }

    std::promise<void> ready;
    auto ready_future = ready.get_future();

    loop_thread_ = std::thread([this, &ready] {
        // Construct the application here so the FiberDriver lives on the thread
        // that runs its tasks (required by Lely); destroy it here too.
        std::unique_ptr<CanopenApplication> app;
        try {
            app = std::make_unique<CanopenApplication>(config_);
        } catch (...) {
            running_.store(false);
            ready.set_exception(std::current_exception());
            return;
        }

        app_.store(app.get(), std::memory_order_release);
        app->resetMaster();
        ready.set_value();

        app->run();

        app_.store(nullptr, std::memory_order_release);
        app.reset();  // destroy Lely objects on the loop thread
    });

    try {
        ready_future.get();
    } catch (...) {
        if (loop_thread_.joinable()) {
            loop_thread_.join();
        }
        running_.store(false);
        throw;
    }
}

void MotorDrive::stop() {
    if (!loop_thread_.joinable()) {
        running_.store(false);
        return;
    }
    if (auto* app = app_.load(std::memory_order_acquire)) {
        app->post([app] { app->motor().requestGracefulStop(); });
    }
    loop_thread_.join();
    running_.store(false);
}

bool MotorDrive::running() const {
    return running_.load();
}

ds402::Feedback MotorDrive::feedback() const {
    if (auto* app = app_.load(std::memory_order_acquire)) {
        return app->feedback();
    }
    return {};
}

bool MotorDrive::feedbackLive() const {
    if (auto* app = app_.load(std::memory_order_acquire)) {
        return app->feedbackLive();
    }
    return false;
}

void MotorDrive::commandPosition(int32_t counts) {
    if (auto* app = app_.load(std::memory_order_acquire)) {
        app->post([app, counts] { app->motor().drive().setCspTargetPosition(counts); });
    }
}

void MotorDrive::commandVelocity(int32_t units) {
    if (auto* app = app_.load(std::memory_order_acquire)) {
        app->post([app, units] { app->motor().drive().setCsvTargetVelocity(units); });
    }
}

void MotorDrive::commandTorque(int16_t units) {
    if (auto* app = app_.load(std::memory_order_acquire)) {
        app->post([app, units] { app->motor().drive().setCstTargetTorque(units); });
    }
}

}  // namespace stablecops::app
