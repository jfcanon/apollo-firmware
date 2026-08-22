#include "menu_layer.h"

#include <esp_log.h>

#include <algorithm>

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

    // "Buscar redes" sits next to provisioning: the AP portal needs a second
    // device to drive it, and in someone else's house the phone is usually the
    // thing that has no internet either.
    lv_obj_t* scan_button = lv_button_create(wifi_panel_);
    lv_obj_set_width(scan_button, LV_PCT(100));
    lv_obj_set_style_bg_color(scan_button, lv_color_hex(kDimColor), 0);
    lv_obj_add_event_cb(scan_button, ScanEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t* scan_button_label = lv_label_create(scan_button);
    lv_label_set_text(scan_button_label, "Buscar redes");
    lv_obj_set_style_text_color(scan_button_label, lv_color_hex(kAccentColor), 0);
    lv_obj_center(scan_button_label);

    // Scan results.
    scan_panel_ = lv_obj_create(catcher_);
    lv_obj_set_size(scan_panel_, LV_PCT(86), LV_PCT(70));
    lv_obj_align(scan_panel_, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(scan_panel_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scan_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(scan_panel_, lv_color_hex(kDimColor), 0);
    lv_obj_set_style_border_width(scan_panel_, 2, 0);
    lv_obj_set_style_radius(scan_panel_, 16, 0);
    lv_obj_set_style_pad_all(scan_panel_, 10, 0);
    lv_obj_set_flex_flow(scan_panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(scan_panel_, LV_OBJ_FLAG_HIDDEN);

    scan_status_label_ = lv_label_create(scan_panel_);
    lv_obj_set_style_text_color(scan_status_label_, lv_color_hex(kAccentColor), 0);
    lv_label_set_text(scan_status_label_, "");

    scan_list_ = lv_list_create(scan_panel_);
    lv_obj_set_width(scan_list_, LV_PCT(100));
    lv_obj_set_flex_grow(scan_list_, 1);
    lv_obj_set_style_bg_color(scan_list_, lv_color_black(), 0);
    lv_obj_set_style_border_width(scan_list_, 0, 0);

    // Password entry for the chosen network.
    password_panel_ = lv_obj_create(catcher_);
    lv_obj_set_size(password_panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(password_panel_);
    lv_obj_set_style_bg_color(password_panel_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(password_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(password_panel_, 0, 0);
    lv_obj_set_style_pad_all(password_panel_, 6, 0);
    lv_obj_remove_flag(password_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(password_panel_, LV_OBJ_FLAG_HIDDEN);

    password_prompt_label_ = lv_label_create(password_panel_);
    lv_obj_align(password_prompt_label_, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_style_text_color(password_prompt_label_, lv_color_hex(kAccentColor), 0);
    lv_label_set_text(password_prompt_label_, "");

    password_input_ = lv_textarea_create(password_panel_);
    lv_obj_set_width(password_input_, LV_PCT(94));
    lv_obj_align(password_input_, LV_ALIGN_TOP_MID, 0, 28);
    lv_textarea_set_one_line(password_input_, true);
    lv_textarea_set_password_mode(password_input_, true);
    lv_textarea_set_placeholder_text(password_input_, "Contrasena");

    keyboard_ = lv_keyboard_create(password_panel_);
    lv_obj_set_size(keyboard_, LV_PCT(100), LV_PCT(58));
    lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard_, password_input_);
    // READY commits the join, CANCEL backs out to the network list.
    lv_obj_add_event_cb(keyboard_, KeyboardEventCallback, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, KeyboardEventCallback, LV_EVENT_CANCEL, this);

    auto_hide_timer_ = lv_timer_create(AutoHideTimerCallback, kAutoHideMilliseconds, this);
    lv_timer_pause(auto_hide_timer_);
}

void MenuLayer::HidePanels() {
    lv_obj_add_flag(wifi_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scan_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(password_panel_, LV_OBJ_FLAG_HIDDEN);
}

void MenuLayer::ShowScanPanel() {
    HidePanels();
    lv_obj_clean(scan_list_);
    lv_label_set_text(scan_status_label_, "Buscando redes...");
    lv_obj_remove_flag(scan_panel_, LV_OBJ_FLAG_HIDDEN);
    // A scan takes a couple of seconds and the result arrives on another task;
    // the menu must not time out underneath it.
    if (auto_hide_timer_ != nullptr) {
        lv_timer_pause(auto_hide_timer_);
    }
    if (callbacks_.on_scan_request) {
        callbacks_.on_scan_request();
    }
}

void MenuLayer::ShowNetworkList(const std::vector<Network>& network_list) {
    if (scan_list_ == nullptr) {
        return;
    }
    lv_obj_clean(scan_list_);
    if (network_list.empty()) {
        lv_label_set_text(scan_status_label_, "No encontre redes. Proba de nuevo.");
        return;
    }
    lv_label_set_text(scan_status_label_, "Elegi una red:");
    for (const Network& network : network_list) {
        lv_obj_t* button = lv_list_add_button(
            scan_list_, network.secured ? LV_SYMBOL_WIFI : LV_SYMBOL_OK, network.ssid.c_str());
        lv_obj_set_style_bg_color(button, lv_color_black(), 0);
        lv_obj_set_style_text_color(button, lv_color_hex(kAccentColor), 0);
        lv_obj_add_event_cb(button, NetworkChosenEventCallback, LV_EVENT_CLICKED, this);
    }
    lv_obj_remove_flag(scan_panel_, LV_OBJ_FLAG_HIDDEN);
}

void MenuLayer::ShowPasswordPanel(const std::string& ssid) {
    pending_ssid_ = ssid;
    HidePanels();
    lv_textarea_set_text(password_input_, "");
    const std::string prompt = "Clave de " + ssid;
    lv_label_set_text(password_prompt_label_, prompt.c_str());
    lv_obj_remove_flag(password_panel_, LV_OBJ_FLAG_HIDDEN);
    if (auto_hide_timer_ != nullptr) {
        lv_timer_pause(auto_hide_timer_);
    }
}

void MenuLayer::ScanEventCallback(lv_event_t* event) {
    auto* self = static_cast<MenuLayer*>(lv_event_get_user_data(event));
    if (self->callbacks_.on_wake) {
        self->callbacks_.on_wake();
    }
    self->ShowScanPanel();
}

void MenuLayer::NetworkChosenEventCallback(lv_event_t* event) {
    auto* self = static_cast<MenuLayer*>(lv_event_get_user_data(event));
    auto* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
    const char* ssid = lv_list_get_button_text(self->scan_list_, button);
    if (ssid == nullptr) {
        return;
    }
    if (self->callbacks_.on_wake) {
        self->callbacks_.on_wake();
    }
    self->ShowPasswordPanel(ssid);
}

void MenuLayer::KeyboardEventCallback(lv_event_t* event) {
    auto* self = static_cast<MenuLayer*>(lv_event_get_user_data(event));
    if (lv_event_get_code(event) == LV_EVENT_CANCEL) {
        self->HidePanels();
        lv_obj_remove_flag(self->scan_panel_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const char* password = lv_textarea_get_text(self->password_input_);
    const std::string ssid = self->pending_ssid_;
    ESP_LOGI(TAG, "Joining %s from the menu", ssid.c_str());
    lv_label_set_text(self->scan_status_label_, "Conectando...");
    self->Dismiss();
    if (self->callbacks_.on_join_network) {
        self->callbacks_.on_join_network(ssid, password != nullptr ? password : "");
    }
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
        HidePanels();
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
