#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace clock_example {

enum class Page { Alarm = 0, Clock = 1, Timer = 2, Stopwatch = 3 };

struct Timer {
    double durationSeconds = 1500;
    double remainingSeconds = 1500;
    bool running = false;
    bool finished = false;
    int preset = 2;
    float minutes = 25.f;
};

struct Stopwatch {
    double elapsedSeconds = 0;
    bool running = false;
    // Cumulative elapsed times, in capture order; at most 100 entries.
    std::vector<double> laps;
};

// Editable in-app settings. This example does not schedule Android OS alarms.
struct Alarm {
    int hour = 7;
    int minute = 0;
    bool enabled = true;
    std::string label;
};

struct State {
    Page page = Page::Timer;
    bool dark = false;
    bool reducedMotion = false;
    Timer timer;
    Stopwatch stopwatch;
    std::vector<Alarm> alarms = {
        {7, 0, true, "Morning"},
        {8, 30, true, "Work"},
        {9, 0, false, "Weekend"},
    };
    double monotonicSeconds = 0;
    std::time_t wallTime = 0;
    bool hasTimeSample = false;
    bool use24Hour = true;
    bool alarmEditorOpen = false;
    int editingAlarm = -1;
    int draftHour = 7;
    int draftMinute = 0;
    char draftLabel[64] = "Morning";
    bool draftEnabled = true;
};

// Call every frame, including hidden frames. The first finite sample establishes
// a baseline; non-finite/backward samples never advance the counters or move
// that baseline backwards. Wall time is independent of elapsed time.
void Update(State& state, double monotonicSeconds, std::time_t wallTime);

void ToggleTimer(State& state);
void ResetTimer(State& state);
// Stops/resets the timer; finite values clamp to 1 second..24 hours.
void SetTimerDuration(State& state, double seconds);
void ToggleStopwatch(State& state);
void ResetStopwatch(State& state);
// Captures a distinct positive cumulative elapsed time while running.
bool AddLap(State& state);

// MM:SS or H:MM:SS, optionally .CC. Values truncate; countdowns may pass ceil().
std::string FormatDuration(double seconds, bool centiseconds = false);

} // namespace clock_example
