#pragma once

namespace aimgui {
namespace live2d {
namespace audio {

bool PlayFile(const char* path);

bool PlayMemory(const void* wav, unsigned long size);

bool Playing();

float Rms();

void Stop();

}
}
}
