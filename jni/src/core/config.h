#pragma once

namespace aimgui {

struct UiState;

namespace config {

const char* Path();

void Load(UiState* state, const char* path = Path());
void Save(const UiState* state, const char* path = Path());

bool Dirty(const UiState* state);

}
}
