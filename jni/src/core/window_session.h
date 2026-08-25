#pragma once

#include "renderer.h"

struct ANativeWindow;

namespace aimgui {

class WindowSession {
public:
    ~WindowSession() { Destroy(); }
    bool Build(int side, bool permeate_record);
    void Destroy();
    IRenderer* renderer() const { return m_Renderer.get(); }
    ANativeWindow* window() const { return m_Window; }
private:
    ANativeWindow*             m_Window = nullptr;
    std::unique_ptr<IRenderer> m_Renderer;
};

}
