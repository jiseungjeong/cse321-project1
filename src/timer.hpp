#pragma once

#include <chrono>

class Timer {
public:
    using Clock = std::chrono::steady_clock;

    void   start() { t0_ = Clock::now(); }
    double elapsed_us() const {
        return std::chrono::duration<double, std::micro>(Clock::now() - t0_).count();
    }
    double elapsed_ms() const { return elapsed_us() / 1000.0; }

private:
    Clock::time_point t0_ = Clock::now();
};
