#ifndef ORB_FACE_H_
#define ORB_FACE_H_

#include <lvgl.h>

// A procedural JARVIS-style hologram orb: an amber wireframe sphere drawn
// with LVGL layer primitives every frame — no image assets, no GIF playback,
// no PSRAM frame strips. State changes retune rotation speed, glow and pulse
// so idle/listening/thinking/speaking read differently at a glance.
class OrbFace {
public:
    enum class State { kIdle, kListening, kThinking, kSpeaking };

    // Creates the orb widget inside `parent`, centered, sized `diameter` px.
    // The returned object is owned by LVGL; OrbFace itself must outlive it
    // (make it a static or a member of the display).
    void Create(lv_obj_t* parent, int32_t diameter);

    void SetState(State state);

private:
    static void DrawEventCallback(lv_event_t* event);
    static void TimerCallback(lv_timer_t* timer);
    void Draw(lv_layer_t* layer);

    lv_obj_t* orb_object_ = nullptr;
    lv_timer_t* frame_timer_ = nullptr;
    State state_ = State::kIdle;
    float rotation_radians_ = 0.0f;
    float pulse_phase_ = 0.0f;
    int32_t diameter_ = 300;
};

#endif  // ORB_FACE_H_
