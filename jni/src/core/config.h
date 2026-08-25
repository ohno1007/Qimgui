#pragma once

namespace aimgui {

struct UiState;

namespace config {

const char* Path();

void Load(UiState* state);
void Save(const UiState* state);

bool Dirty(const UiState* state);

}
}
