#ifndef MENU_LAYER_H_
#define MENU_LAYER_H_

#include <functional>
#include <string>

#include <lvgl.h>

// A touch menu that floats above the orb on LVGL's top layer, so nothing in
// the face or the status bar has to know it exists.
//
// The whole screen is a tap target while the menu is hidden: the panel is dark
// after 60 s idle and the wake word is gated off in that state, so a tap is one
// of only two ways back in (the button is the other). The first tap wakes the
// device and reveals the icon row; it never falls through to whatever is
// underneath.
//
// Two icons, per the owner's request:
//   Jarvis — dismiss the menu and go back to the face.
//   Wi-Fi  — show the current network, and open provisioning so a phone
//            hotspot can be added while away from home.
class MenuLayer {
public:
    struct Callbacks {
        // Called on every tap, before anything else: cancels power save.
        std::function<void()> on_wake;
        // Starts the provisioning AP (WifiBoard::EnterWifiConfigMode).
        std::function<void()> on_enter_wifi_config;
        // "MiRed · 192.168.0.10" or a not-connected string.
        std::function<std::string()> read_network_summary;
    };

    void Create(Callbacks callbacks);

    // Hides the icon row and the Wi-Fi panel without destroying them.
    void Dismiss();

private:
    static void CatcherEventCallback(lv_event_t* event);
    static void JarvisEventCallback(lv_event_t* event);
    static void WifiEventCallback(lv_event_t* event);
    static void ConfigureEventCallback(lv_event_t* event);
    static void AutoHideTimerCallback(lv_timer_t* timer);

    lv_obj_t* BuildIconButton(lv_obj_t* parent, const char* symbol, const char* label_text);
    void Reveal();
    void ShowWifiPanel();
    void RestartAutoHide();

    Callbacks callbacks_;
    lv_obj_t* catcher_ = nullptr;
    lv_obj_t* icon_row_ = nullptr;
    lv_obj_t* wifi_panel_ = nullptr;
    lv_obj_t* wifi_summary_label_ = nullptr;
    lv_timer_t* auto_hide_timer_ = nullptr;
    bool revealed_ = false;
};

#endif  // MENU_LAYER_H_
