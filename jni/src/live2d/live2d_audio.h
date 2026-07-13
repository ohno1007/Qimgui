// Minimal native audio for the Live2D character's "talking": plays a 16-bit
// PCM WAV via AAudio (no APK / JNI) and exposes a running RMS envelope that
// drives the mouth (lip-sync). One voice plays at a time.
#pragma once

namespace aimgui {
namespace live2d {
namespace audio {

// Play a 16-bit PCM WAV from disk. Any currently-playing clip is stopped.
// Returns false if the file is missing or not a supported WAV.
bool PlayFile(const char* path);

// True while a clip is playing.
bool Playing();

// Current lip-sync amplitude in [0,1] (0 when idle). Smoothed for the mouth.
float Rms();

// Stop playback and release the stream.
void Stop();

} // namespace audio
} // namespace live2d
} // namespace aimgui
