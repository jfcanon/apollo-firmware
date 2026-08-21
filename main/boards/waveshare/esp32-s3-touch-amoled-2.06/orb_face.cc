#include "orb_face.h"

#include <math.h>

namespace {

constexpr float kPi = 3.14159265f;
constexpr uint32_t kFrameIntervalMs = 66;  // ~15 fps — light on CPU, GPU and battery

constexpr lv_color_t AmberColor() { return lv_color_hex(0xFFB000); }
constexpr lv_color_t CoreColor() { return lv_color_hex(0xFFD060); }

struct StateTuning {
    float sweep_step;      // radians per frame — the moving highlight
    float pulse_step;      // radians per frame — ring breathing
    float pulse_amplitude; // fraction of radius
    lv_opa_t ring_opacity;
};

StateTuning TuningFor(OrbFace::State state) {
    switch (state) {
        case OrbFace::State::kListening:
            return {0.10f, 0.16f, 0.05f, LV_OPA_COVER};
        case OrbFace::State::kThinking:
            return {0.28f, 0.30f, 0.03f, LV_OPA_COVER};
        case OrbFace::State::kSpeaking:
            return {0.14f, 0.34f, 0.09f, LV_OPA_COVER};
        case OrbFace::State::kIdle:
        default:
            return {0.05f, 0.06f, 0.02f, LV_OPA_80};
    }
}

}  // namespace

void OrbFace::Create(lv_obj_t* parent, int32_t diameter) {
    diameter_ = diameter;
    // The orb widget IS the black stage: full-screen, opaque black, so no
    // theme background can show through behind the hologram.
    orb_object_ = lv_obj_create(parent);
    lv_obj_remove_style_all(orb_object_);
    lv_obj_set_size(orb_object_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(orb_object_);
    lv_obj_add_flag(orb_object_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(orb_object_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(orb_object_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(orb_object_, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(orb_object_, DrawEventCallback, LV_EVENT_DRAW_MAIN, this);
    frame_timer_ = lv_timer_create(TimerCallback, kFrameIntervalMs, this);
}

void OrbFace::SetState(State state) {
    state_ = state;
}

void OrbFace::TimerCallback(lv_timer_t* timer) {
    auto* orb = static_cast<OrbFace*>(lv_timer_get_user_data(timer));
    const StateTuning tuning = TuningFor(orb->state_);
    orb->rotation_radians_ += tuning.sweep_step;
    if (orb->rotation_radians_ > 2.0f * kPi) {
        orb->rotation_radians_ -= 2.0f * kPi;
    }
    orb->pulse_phase_ += tuning.pulse_step;
    if (orb->pulse_phase_ > 2.0f * kPi) {
        orb->pulse_phase_ -= 2.0f * kPi;
    }
    if (orb->orb_object_ != nullptr) {
        lv_obj_invalidate(orb->orb_object_);
    }
}

void OrbFace::DrawEventCallback(lv_event_t* event) {
    auto* orb = static_cast<OrbFace*>(lv_event_get_user_data(event));
    lv_layer_t* layer = lv_event_get_layer(event);
    if (orb != nullptr && layer != nullptr) {
        orb->Draw(layer);
    }
}

void OrbFace::Draw(lv_layer_t* layer) {
    if (sleeping_) {
        return;  // standby: the black stage stays, but no hologram is drawn
    }
    lv_area_t coords;
    lv_obj_get_coords(orb_object_, &coords);
    const int32_t center_x = (coords.x1 + coords.x2) / 2;
    const int32_t center_y = (coords.y1 + coords.y2) / 2;

    const StateTuning tuning = TuningFor(state_);
    const float pulse = 1.0f + tuning.pulse_amplitude * sinf(pulse_phase_);
    const int32_t radius = (int32_t)((diameter_ / 2 - 8) * pulse);

    // 1. The main amber ring (one circle outline).
    lv_draw_arc_dsc_t ring_dsc;
    lv_draw_arc_dsc_init(&ring_dsc);
    ring_dsc.color = AmberColor();
    ring_dsc.center.x = center_x;
    ring_dsc.center.y = center_y;
    ring_dsc.width = 4;
    ring_dsc.radius = (uint16_t)radius;
    ring_dsc.start_angle = 0;
    ring_dsc.end_angle = 360;
    ring_dsc.opa = tuning.ring_opacity;
    lv_draw_arc(layer, &ring_dsc);

    // 2. A brighter highlight arc sweeping around the ring — the sign of life
    //    that also reads the state (fast = thinking, gentle = idle).
    lv_draw_arc_dsc_t sweep_dsc;
    lv_draw_arc_dsc_init(&sweep_dsc);
    sweep_dsc.color = CoreColor();
    sweep_dsc.center.x = center_x;
    sweep_dsc.center.y = center_y;
    sweep_dsc.width = 6;
    sweep_dsc.radius = (uint16_t)radius;
    const int32_t sweep_start = (int32_t)(rotation_radians_ * 180.0f / kPi) % 360;
    sweep_dsc.start_angle = sweep_start;
    sweep_dsc.end_angle = sweep_start + 60;
    sweep_dsc.opa = LV_OPA_COVER;
    sweep_dsc.rounded = 1;
    lv_draw_arc(layer, &sweep_dsc);

    // 3. A small breathing core dot at the centre.
    lv_draw_arc_dsc_t core_dsc;
    lv_draw_arc_dsc_init(&core_dsc);
    core_dsc.color = CoreColor();
    core_dsc.center.x = center_x;
    core_dsc.center.y = center_y;
    core_dsc.width = (int32_t)(radius * 0.12f);
    core_dsc.radius = (uint16_t)(radius * 0.12f);
    core_dsc.start_angle = 0;
    core_dsc.end_angle = 360;
    core_dsc.opa = tuning.ring_opacity;
    lv_draw_arc(layer, &core_dsc);
}

void OrbFace::Sleep() {
    if (sleeping_) {
        return;
    }
    sleeping_ = true;
    if (frame_timer_ != nullptr) {
        lv_timer_pause(frame_timer_);  // stop the 15 fps redraw — CPU/GPU idle
    }
    if (orb_object_ != nullptr) {
        lv_obj_invalidate(orb_object_);  // one last redraw -> Draw() returns early -> black
    }
}

void OrbFace::Wake() {
    if (!sleeping_) {
        return;
    }
    sleeping_ = false;
    if (frame_timer_ != nullptr) {
        lv_timer_resume(frame_timer_);
    }
    if (orb_object_ != nullptr) {
        lv_obj_invalidate(orb_object_);
    }
}
