#pragma once

#include <GLES3/gl3.h>

#include <chrono>

namespace aimgui {

class BloomGL {
public:
    bool Init(int width, int height);
    void Shutdown();
    bool Ready() const { return m_Ready; }

    void SetIntensity(float i) { m_Intensity = i; }
    void SetSnapshotFrozen(bool frozen) { m_SnapshotFrozen = frozen; }

    void SetCompositeOverDest(bool b) { m_OverDest = b; }

    unsigned int GetSnapshotTex() const { return m_PrevSceneTex; }

    void BeginScene();
    void EndSceneAndComposite();

private:

    bool SnapshotDue();

    bool m_Ready  = false;
    int  m_Width  = 0;
    int  m_Height = 0;
    int  m_BlurW  = 0;
    int  m_BlurH  = 0;
    float m_Intensity     = 0.75f;
    bool  m_SnapshotFrozen = false;
    bool  m_OverDest       = false;
    std::chrono::steady_clock::time_point m_LastSnapshot{};

    GLuint m_SceneFBO     = 0;
    GLuint m_SceneTex     = 0;
    GLuint m_PrevSceneTex = 0;
    GLuint m_BlurFBO[2]   = { 0, 0 };
    GLuint m_BlurTex[2]   = { 0, 0 };

    GLuint m_QuadVAO = 0;
    GLuint m_QuadVBO = 0;

    GLuint m_ProgThreshold = 0;
    GLuint m_ProgBlur      = 0;
    GLuint m_ProgComposite = 0;

    GLint  m_LocThreshScene = -1;
    GLint  m_LocBlurImage   = -1;
    GLint  m_LocBlurDir     = -1;
    GLint  m_LocCompScene   = -1;
    GLint  m_LocCompBloom   = -1;
    GLint  m_LocCompIntens  = -1;
};

}
