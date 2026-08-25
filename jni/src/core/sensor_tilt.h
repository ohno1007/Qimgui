#pragma once

namespace aimgui {

class SensorTilt {
public:
    bool Init();
    void Shutdown();

    void Update(float dt);

    bool  Available() const { return m_Ok; }

    float x() const { return m_X; }
    float y() const { return m_Y; }

private:
    void* m_Lib   = nullptr;
    void* m_Queue = nullptr;
    void* m_Looper = nullptr;
    bool  m_Ok = false;
    float m_X = 0.0f, m_Y = 0.0f;
};

}
