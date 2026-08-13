#include "orb_face.h"

#include <math.h>
#include <initializer_list>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTiltRadians = 0.35f;  // fixed X-axis tilt so latitude rings read as 3D
constexpr int kMeridianCount = 3;
constexpr int kSegmentsPerCircle = 40;
constexpr uint32_t kFrameIntervalMs = 40;  // 25 fps

constexpr lv_color_t AmberColor() { return lv_color_hex(0xFFB000); }
constexpr lv_color_t CoreColor() { return lv_color_hex(0xFFD870); }

struct StateTuning {
    float rotation_step;   // radians per frame
    float pulse_step;      // radians per frame
    float pulse_amplitude; // 0..1 of radius
    lv_opa_t line_opacity;
    lv_opa_t glow_opacity;
};

StateTuning TuningFor(OrbFace::State state) {
    switch (state) {
        case OrbFace::State::kListening:
            return {0.030f, 0.16f, 0.05f, LV_OPA_COVER, LV_OPA_40};
        case OrbFace::State::kThinking:
            return {0.085f, 0.30f, 0.03f, LV_OPA_90, LV_OPA_30};
        case OrbFace::State::kSpeaking:
            return {0.040f, 0.34f, 0.09f, LV_OPA_COVER, LV_OPA_50};
        case OrbFace::State::kIdle:
        default:
            return {0.012f, 0.06f, 0.02f, LV_OPA_60, LV_OPA_20};
    }
}

}  // namespace

void OrbFace::Create(lv_obj_t* parent, int32_t diameter) {
    diameter_ = diameter;
    orb_object_ = lv_obj_create(parent);
    lv_obj_remove_style_all(orb_object_);
    lv_obj_set_size(orb_object_, diameter, diameter);
    lv_obj_center(orb_object_);
    lv_obj_add_flag(orb_object_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(orb_object_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(orb_object_, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(orb_object_, DrawEventCallback, LV_EVENT_DRAW_MAIN, this);
    frame_timer_ = lv_timer_create(TimerCallback, kFrameIntervalMs, this);
}

void OrbFace::SetState(State state) {
    state_ = state;
}

void OrbFace::TimerCallback(lv_timer_t* timer) {
    auto* orb = static_cast<OrbFace*>(lv_timer_get_user_data(timer));
    const StateTuning tuning = TuningFor(orb->state_);
    orb->rotation_radians_ += tuning.rotation_step;
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
    lv_area_t coords;
    lv_obj_get_coords(orb_object_, &coords);
    const float center_x = (coords.x1 + coords.x2) / 2.0f;
    const float center_y = (coords.y1 + coords.y2) / 2.0f;

    const StateTuning tuning = TuningFor(state_);
    const float pulse = 1.0f + tuning.pulse_amplitude * sinf(pulse_phase_);
    const float radius = (diameter_ / 2.0f - 12.0f) * pulse;

    const float sin_tilt = sinf(kTiltRadians);
    const float cos_tilt = cosf(kTiltRadians);

    // Soft radial glow: three concentric rings fading outward.
    lv_draw_arc_dsc_t glow_dsc;
    lv_draw_arc_dsc_init(&glow_dsc);
    glow_dsc.color = AmberColor();
    glow_dsc.center.x = (int32_t)center_x;
    glow_dsc.center.y = (int32_t)center_y;
    glow_dsc.start_angle = 0;
    glow_dsc.end_angle = 360;
    for (int ring = 0; ring < 3; ring += 1) {
        glow_dsc.radius = (uint16_t)(radius * (0.55f + 0.18f * ring));
        glow_dsc.width = (int32_t)(radius * 0.16f);
        glow_dsc.opa = (lv_opa_t)(tuning.glow_opacity >> ring);
        lv_draw_arc(layer, &glow_dsc);
    }

    // Bright core.
    lv_draw_arc_dsc_t core_dsc;
    lv_draw_arc_dsc_init(&core_dsc);
    core_dsc.color = CoreColor();
    core_dsc.center.x = (int32_t)center_x;
    core_dsc.center.y = (int32_t)center_y;
    core_dsc.start_angle = 0;
    core_dsc.end_angle = 360;
    core_dsc.radius = (uint16_t)(radius * 0.16f);
    core_dsc.width = (int32_t)(radius * 0.16f);
    core_dsc.opa = tuning.line_opacity;
    lv_draw_arc(layer, &core_dsc);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = AmberColor();
    line_dsc.width = 2;
    line_dsc.opa = tuning.line_opacity;
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;

    // Wireframe: points on the unit sphere rotated by `rotation_radians_`
    // about Y, then tilted about X, orthographically projected (x, y).
    auto project = [&](float px, float py, float pz, float* out_x, float* out_y) {
        const float tilted_y = py * cos_tilt - pz * sin_tilt;
        *out_x = center_x + radius * px;
        *out_y = center_y - radius * tilted_y;
    };

    const float step = 2.0f * kPi / kSegmentsPerCircle;

    // Meridians (great circles through the poles).
    for (int meridian = 0; meridian < kMeridianCount; meridian += 1) {
        const float longitude = rotation_radians_ + meridian * kPi / kMeridianCount;
        const float sin_lon = sinf(longitude);
        const float cos_lon = cosf(longitude);
        float previous_x = 0.0f;
        float previous_y = 0.0f;
        for (int segment = 0; segment <= kSegmentsPerCircle; segment += 1) {
            const float theta = segment * step;
            const float ring_x = sinf(theta) * sin_lon;
            const float ring_y = cosf(theta);
            const float ring_z = sinf(theta) * cos_lon;
            float screen_x = 0.0f;
            float screen_y = 0.0f;
            project(ring_x, ring_y, ring_z, &screen_x, &screen_y);
            if (segment > 0) {
                line_dsc.p1.x = previous_x;
                line_dsc.p1.y = previous_y;
                line_dsc.p2.x = screen_x;
                line_dsc.p2.y = screen_y;
                lv_draw_line(layer, &line_dsc);
            }
            previous_x = screen_x;
            previous_y = screen_y;
        }
    }

    // Two latitude rings; they do not rotate (rotation about Y maps each
    // latitude circle onto itself) but the X tilt makes them ellipses.
    for (const float latitude : {kPi / 3.0f, 2.0f * kPi / 3.0f}) {
        const float ring_radius = sinf(latitude);
        const float ring_height = cosf(latitude);
        float previous_x = 0.0f;
        float previous_y = 0.0f;
        for (int segment = 0; segment <= kSegmentsPerCircle; segment += 1) {
            const float theta = segment * step;
            float screen_x = 0.0f;
            float screen_y = 0.0f;
            project(ring_radius * sinf(theta), ring_height, ring_radius * cosf(theta),
                    &screen_x, &screen_y);
            if (segment > 0) {
                line_dsc.p1.x = previous_x;
                line_dsc.p1.y = previous_y;
                line_dsc.p2.x = screen_x;
                line_dsc.p2.y = screen_y;
                lv_draw_line(layer, &line_dsc);
            }
            previous_x = screen_x;
            previous_y = screen_y;
        }
    }
}
