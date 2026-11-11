#pragma once
// Only logging is stubbed; sensor/looper declarations come from the real NDK.
enum { ANDROID_LOG_INFO = 4 };
extern "C" int __android_log_print(int priority, const char* tag, const char* format, ...);
