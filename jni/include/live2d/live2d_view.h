#pragma once

#ifdef AIMGUI_LIVE2D
#include "core/live2d_vk_bridge.h"
#endif

namespace aimgui {
namespace live2d {

#ifdef AIMGUI_LIVE2D

bool VkInit(const Live2DVkContext* ctx);
#endif

bool LoadModel(const char* dir, const char* model3json);

bool AutoLoad(const char* root);

bool LoadEmbedded();

bool IsLoaded();

void Resize(int width, int height);

void Update(float dt);

void SetView(float expandT);

void SetBall(float x, float y);

void SetBallScale(float scale);

void SetLookScreen(float x, float y, bool active);

void Poke();

void Speak();

bool HitCollapsed(float x, float y);

void Draw();

void Shutdown();

}
}
