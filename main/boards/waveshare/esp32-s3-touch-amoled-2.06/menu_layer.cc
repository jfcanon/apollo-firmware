#include "menu_layer.h"

#include <esp_log.h>

#define TAG "MenuLayer"

namespace {
// Amber, to match the orb — the menu should read as part of the same device.
constexpr uint32_t kAccentColor = 0xFFB347;
constexpr uint32_t kDimColor = 0x5A4630;
constexpr uint32_t kAutoHideMilliseconds = 8000;
constexpr int32_t kIconSize = 84;
}  // namespace

void MenuLayer::Create(Callbacks callbacks) {
    callbacks_ = std::move(callbacks);

    // Full-screen transparent tap catcher on the top layer. It sits above the
    // face but paints nothing, so the black stage still shows through.
    catcher_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(catcher_);
    lv_obj_set_size(catcher_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(catcher_, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(catcher_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(catcher_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(catcher_, CatcherEventCallback, LV_EVENT_CLICKED, this);

    // Icon row, hidden until the first tap.
    icon_row_ = lv_obj_create(catcher_);
    lv_obj_remove_style_all(icon_row_);
    lv_obj_set_size(icon_row_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(icon_row_, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_flex_flow(icon_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(icon_row_, 28, 0);
    lv_obj_remove_flag(icon_row_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(icon_row_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* jarvis_button = BuildIconButton(icon_row_, LV_SYMBOL_AUDIO, "Jarvis");
    lv_obj_add_event_cb(jarvis_button, JarvisEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t* wifi_button = BuildIconButton(icon_row_, LV_SYMBOL_WIFI, "Wi-Fi");
    lv_obj_add_event_cb(wifi_button, WifiEventCallback, LV_EVENT_CLICKED, this);

    // Wi-Fi panel, built once and toggled.
    wifi_panel_ = lv_obj_create(catcher_);
    lv_obj_set_size(wifi_panel_, LV_PCT(80), LV_SIZE_CONTENT);
    lv_obj_align(wifi_panel_, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(wifi_panel_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(wifi_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(wifi_panel_, lv_color_hex(kDimColor), 0);
    lv_obj_set_style_border_width(wifi_panel_, 2, 0);
    lv_obj_set_style_radius(wifi_panel_, 16, 0);
    lv_obj_set_style_pad_all(wifi_panel_, 14, 0);
    lv_obj_set_flex_flow(wifi_panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wifi_panel_, 10, 0);
    lv_obj_remove_flag(wifi_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_panel_, LV_OBJ_FLAG_HIDDEN);

    wifi_summary_label_ = lv_label_create(wifi_panel_);
    lv_obj_set_style_text_color(wifi_summary_label_, lv_color_hex(kAccentColor), 0);
    lv_label_set_long_mode(wifi_summary_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wifi_summary_label_, LV_PCT(100));
    lv_label_set_text(wifi_summary_label_, "");

    lv_obj_t* configure_button = lv_button_create(wifi_panel_);
    lv_obj_set_width(configure_button, LV_PCT(100));
    lv_obj_set_style_bg_color(configure_button, lv_color_hex(kDimColor), 0);
    lv_obj_add_event_cb(configure_button, ConfigureEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t* configure_label = lv_label_create(configure_button);
    // Adding the phone hotspot is exactly this flow: the device raises its own
    // AP, the phone joins it and hands over the hotspot's SSID and password.
    lv_label_set_text(configure_label, "Agregar red / hotspot");
    lv_obj_set_style_text_color(configure_label, lv_color_hex(kAccentColor), 0);
    lv_obj_center(configure_label);

    auto_hide_timer_ = lv_timer_create(AutoHideTimerCallback, kAutoHideMilliseconds, this);
    lv_timer_pause(auto_hide_timer_);
}

lv_obj_t* MenuLayer::BuildIconButton(lv_obj_t* parent, const char* symbol, const char* label_text) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, kIconSize, kIconSize);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_70, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(kAccentColor), 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* symbol_label = lv_label_create(button);
    lv_label_set_text(symbol_label, symbol);
    lv_obj_set_style_text_color(symbol_label, lv_color_hex(kAccentColor), 0);

    lv_obj_t* caption = lv_label_create(button);
    lv_label_set_text(caption, label_text);
    lv_obj_set_style_text_color(caption, lv_color_hex(kAccentColor), 0);

    return button;
}

void MenuLayer::Reveal() {
    revealed_ = true;
    lv_obj_remove_flag(icon_row_, LV_OBJ_FLAG_HIDDEN);
    RestartAutoHide();
}

void MenuLayer::RestartAutoHide() {
    if (auto_hide_timer_ != nullptr) {
        lv_timer_reset(auto_hide_timer_);
        lv_timer_resume(auto_hide_timer_);
    }
}

void MenuLayer::Dismiss() {
    revealed_ = false;
    if (icon_row_ != nullptr) {
        lv_obj_add_flag(icon_row_, LV_OBJ_FLAG_HIDDEN);
    }
    if (wifi_panel_ != nullptr) {
        lv_obj_add_flag(wifi_panel_, LV_OBJ_FLAG_HIDDEN);
    }
    if (auto_hide_timer_ != nullptr) {
        lv_timer_pause(auto_hide_timer_);
    }
}

void MenuLayer::ShowWifiPanel() {
    if (callbacks_.read_network_summary) {
        const std::string summary = callbacks_.read_network_summary();
        lv_label_set_text(wifi_summary_label_, summary.c_str());
    }
    lv_obj_remove_flag(wifi_panel_, LV_OBJ_FLAG_HIDDEN);
    RestartAutoHide();
}

void MenuLayer::CatcherEventCallback(lv_event_t* event) {
    auto* self = static_cast<MenuLayer*>(lv_event_get_user_data(event));
    // Every tap wakes the device first — the panel is dark and the wake word is
    // off in power save, so this is the user's way back in.
    if (self->callbacks_.on_wake) {
        self->callbacks_.on_wake();
    }
    if (!self->revealed_) {
        self->Reveal();
        return;
    }
    // A tap outside the icons while the menu is up puts it away again.
    self->Dismiss();
}

void MenuLayer::JarvisEventCallback(lv_event_t* event) {
    auto* self = static_cast<MenuLayer*>(lv_event_get_user_data(event));
    if (self->callbacks_.on_wake) {
        self->callbacks_.on_wake();
    }
    self->Dismiss();
}

void MenuLayer::WifiEventCallback(lv_event_t* event) {
    auto* self = static_cast<MenuLayer*>(lv_event_get_user_data(event));
    if (self->callbacks_.on_wake) {
        self->callbacks_.on_wake();
    }
    self->ShowWifiPanel();
}

void MenuLayer::ConfigureEventCallback(lv_event_t* event) {
    auto* self = static_cast<MenuLayer*>(lv_event_get_user_data(event));
    ESP_LOGI(TAG, "Wi-Fi provisioning requested from the menu");
    self->Dismiss();
    if (self->callbacks_.on_enter_wifi_config) {
        self->callbacks_.on_enter_wifi_config();
    }
}

void MenuLayer::AutoHideTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<MenuLayer*>(lv_timer_get_user_data(timer));
    self->Dismiss();
}
