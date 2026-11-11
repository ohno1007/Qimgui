#include "model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace clock_example {

void Update(State& state, double now, std::time_t wallTime) {
    state.wallTime = wallTime;
    if (!std::isfinite(now)) return;
    if (!state.hasTimeSample) {
        state.monotonicSeconds = now;
        state.hasTimeSample = true;
        return;
    }
    if (now < state.monotonicSeconds) return;
    const double delta = now - state.monotonicSeconds;
    state.monotonicSeconds = now;
    if (!std::isfinite(delta)) return;
    if (state.timer.running) {
        state.timer.remainingSeconds = std::max(0.0, state.timer.remainingSeconds - delta);
        if (state.timer.remainingSeconds <= 0) {
            state.timer.running = false;
            state.timer.finished = true;
        }
    }
    if (state.stopwatch.running) {
        const double maximum = std::numeric_limits<double>::max();
        state.stopwatch.elapsedSeconds = delta > maximum - state.stopwatch.elapsedSeconds
            ? maximum : state.stopwatch.elapsedSeconds + delta;
    }
}

void ToggleTimer(State& state) {
    if (state.timer.running) {
        state.timer.running = false;
        return;
    }
    if (state.timer.finished || state.timer.remainingSeconds <= 0)
        state.timer.remainingSeconds = state.timer.durationSeconds;
    state.timer.finished = false;
    state.timer.running = true;
}

void ResetTimer(State& state) {
    state.timer.remainingSeconds = state.timer.durationSeconds;
    state.timer.running = false;
    state.timer.finished = false;
}

void SetTimerDuration(State& state, double seconds) {
    if (!std::isfinite(seconds)) return;
    state.timer.durationSeconds = std::clamp(seconds, 1.0, 86400.0);
    state.timer.minutes = static_cast<float>(state.timer.durationSeconds / 60.0);
    constexpr double presets[] = {300, 900, 1500, 2700};
    state.timer.preset = -1;
    for (int i = 0; i < 4; ++i)
        if (std::abs(state.timer.durationSeconds - presets[i]) < 1e-6)
            state.timer.preset = i;
    ResetTimer(state);
}

void ToggleStopwatch(State& state) { state.stopwatch.running = !state.stopwatch.running; }

void ResetStopwatch(State& state) {
    state.stopwatch.running = false;
    state.stopwatch.elapsedSeconds = 0;
    state.stopwatch.laps.clear();
}

bool AddLap(State& state) {
    const auto& watch = state.stopwatch;
    const double last = watch.laps.empty() ? 0 : watch.laps.back();
    if (!watch.running || !std::isfinite(watch.elapsedSeconds) ||
        watch.elapsedSeconds <= last || watch.laps.size() >= 100) return false;
    state.stopwatch.laps.push_back(watch.elapsedSeconds);
    return true;
}

std::string FormatDuration(double seconds, bool centiseconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    seconds = std::min(seconds, 999999999.99);
    const auto ticks = static_cast<std::uint64_t>(std::floor(seconds * 100.0 + 1e-5));
    const auto total = ticks / 100, hours = total / 3600;
    const auto minutes = total / 60 % 60, remainder = total % 60;
    char value[64]{};
    if (hours) {
        std::snprintf(value, sizeof(value), "%llu:%02llu:%02llu",
                      static_cast<unsigned long long>(hours),
                      static_cast<unsigned long long>(minutes),
                      static_cast<unsigned long long>(remainder));
    } else {
        std::snprintf(value, sizeof(value), "%02llu:%02llu",
                      static_cast<unsigned long long>(minutes),
                      static_cast<unsigned long long>(remainder));
    }
    std::string result(value);
    if (centiseconds) {
        std::snprintf(value, sizeof(value), ".%02llu",
                      static_cast<unsigned long long>(ticks % 100));
        result += value;
    }
    return result;
}

} // namespace clock_example
