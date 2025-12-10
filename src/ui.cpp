#include "ui.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "container_model.h"

#include <esp_system.h>  // for esp_random()

// Some LVGL builds don't define LV_SYMBOL_KEY / LV_SYMBOL_TRASH; alias them
#ifndef LV_SYMBOL_KEY
    #define LV_SYMBOL_KEY LV_SYMBOL_BELL
#endif

#ifndef LV_SYMBOL_TRASH
    #define LV_SYMBOL_TRASH LV_SYMBOL_CLOSE
#endif

// ----------------------
// User roles
// ----------------------
enum UserRole {
    ROLE_NONE = 0,
    ROLE_OPERATOR,
    ROLE_ADMIN
};

static UserRole    current_role       = ROLE_NONE;
static const char* current_user_name  = "NONE";

static const char* PIN_ADMIN          = "5000";
static const char* PIN_OPERATOR       = "1111";

// ----------------------
// Screens
// ----------------------
static lv_obj_t* home_screen             = nullptr;
static lv_obj_t* containers_screen       = nullptr;
static lv_obj_t* keyload_screen          = nullptr;
static lv_obj_t* settings_screen         = nullptr;
static lv_obj_t* user_screen             = nullptr;
static lv_obj_t* container_detail_screen = nullptr;
static lv_obj_t* key_edit_screen         = nullptr;
static lv_obj_t* container_edit_screen   = nullptr;

// ----------------------
// Common widgets
// ----------------------
static lv_obj_t* status_label     = nullptr;
static lv_obj_t* home_user_label  = nullptr;

// Keyload widgets
static lv_obj_t*   keyload_status          = nullptr;
static lv_obj_t*   keyload_bar             = nullptr;
static lv_obj_t*   keyload_container_label = nullptr;
static lv_timer_t* keyload_timer           = nullptr;
static int         keyload_progress        = 0;

// User manager widgets
static lv_obj_t* user_role_label   = nullptr;
static lv_obj_t* pin_label         = nullptr;
static lv_obj_t* user_status_label = nullptr;
static char      pin_buffer[8];
static uint8_t   pin_len      = 0;
static UserRole  pending_role = ROLE_NONE;

// Container detail UI
static lv_obj_t* container_keys_list     = nullptr;
static lv_obj_t* container_detail_status = nullptr;
static int       current_container_index = -1;

// Key edit UI
static lv_obj_t* keyedit_label_ta        = nullptr;
static lv_obj_t* keyedit_algo_dd         = nullptr;
static lv_obj_t* keyedit_key_ta          = nullptr;
static lv_obj_t* keyedit_selected_cb     = nullptr;
static lv_obj_t* keyedit_status_label    = nullptr;
static lv_obj_t* keyedit_kb              = nullptr;
static lv_obj_t* keyedit_active_ta       = nullptr;
static int       key_edit_container_idx  = -1;
static int       key_edit_key_idx        = -1;

// Container edit UI
static lv_obj_t* cedit_label_ta          = nullptr;
static lv_obj_t* cedit_agency_ta         = nullptr;
static lv_obj_t* cedit_band_ta           = nullptr;
static lv_obj_t* cedit_algo_dd           = nullptr;
static lv_obj_t* cedit_locked_cb         = nullptr;
static lv_obj_t* cedit_status_label      = nullptr;
static lv_obj_t* cedit_kb                = nullptr;
static int       cedit_index             = -1;

// Delete confirmation msgbox
static lv_obj_t* container_delete_mbox   = nullptr;

// ----------------------
// Forward declarations
// ----------------------

// Screens
static void build_home_screen(void);
static void build_containers_screen(void);
static void build_keyload_screen(void);
static void build_settings_screen(void);
static void build_user_screen(void);
static void build_container_detail_screen(int container_index);
static void build_key_edit_screen(int container_index, int key_index);
static void build_container_edit_screen(int container_index);

// Navigation
static void show_home_screen(lv_event_t* e);
static void event_btn_keys(lv_event_t* e);
static void event_btn_keyload(lv_event_t* e);
static void event_btn_settings(lv_event_t* e);
static void event_btn_user_manager(lv_event_t* e);

// User manager callbacks
static void event_select_admin(lv_event_t* e);
static void event_select_operator(lv_event_t* e);
static void event_keypad_digit(lv_event_t* e);
static void event_keypad_clear(lv_event_t* e);
static void event_keypad_ok(lv_event_t* e);

// Container list/detail callbacks
static void container_btn_event(lv_event_t* e);
static void event_add_container(lv_event_t* e);
static void event_edit_container_meta(lv_event_t* e);
static void event_delete_container(lv_event_t* e);
static void event_delete_container_confirm(lv_event_t* e);
static void event_add_key(lv_event_t* e);
static void event_set_active_container(lv_event_t* e);
static void key_item_event(lv_event_t* e);

// Key edit callbacks
static void keyedit_textarea_event(lv_event_t* e);
static void keyedit_keyboard_event(lv_event_t* e);
static void event_keyedit_gen_random(lv_event_t* e);
static void event_keyedit_save(lv_event_t* e);
static void event_keyedit_cancel(lv_event_t* e);

// Container edit callbacks
static void cedit_textarea_event(lv_event_t* e);
static void cedit_keyboard_event(lv_event_t* e);
static void event_container_edit_save(lv_event_t* e);
static void event_container_edit_cancel(lv_event_t* e);

// Keyload
static void event_btn_keyload_start(lv_event_t* e);
static void keyload_timer_cb(lv_timer_t* t);

// Helpers
static void update_home_user_label();
static bool check_access(bool admin_only, const char* action_name);
static void update_keyload_container_label();

// ----------------------
// Styling helpers
// ----------------------

static void style_moto_tile_button(lv_obj_t* btn) {
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x10202A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x00C0FF), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);

    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1C3A4A),
                              LV_STATE_PRESSED | LV_PART_MAIN);
}

static void style_moto_screen(lv_obj_t* scr) {
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000810), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

// ----------------------
// Role + access helpers
// ----------------------

static void update_home_user_label() {
    const char* role_str = "NONE";
    switch (current_role) {
        case ROLE_OPERATOR: role_str = "OPERATOR"; break;
        case ROLE_ADMIN:    role_str = "ADMIN";    break;
        default:            role_str = "NONE";     break;
    }

    if (home_user_label) {
        char buf[48];
        lv_snprintf(buf, sizeof(buf), "USER: %s", role_str);
        lv_label_set_text(home_user_label, buf);
    }
}

static bool check_access(bool admin_only, const char* action_name) {
    if (!status_label) return false;

    if (current_role == ROLE_NONE) {
        lv_label_set_text(status_label, "LOGIN REQUIRED - USE 'USER / LOGIN'");
        return false;
    }

    if (admin_only && current_role != ROLE_ADMIN) {
        lv_label_set_text(status_label, "ACCESS DENIED - ADMIN ONLY");
        return false;
    }

    char buf[64];
    const char* role_str = (current_role == ROLE_ADMIN) ? "ADMIN" : "OPERATOR";
    lv_snprintf(buf, sizeof(buf), "%s (%s)", action_name, role_str);
    lv_label_set_text(status_label, buf);
    return true;
}

static void update_keyload_container_label() {
    if (!keyload_container_label) return;

    const ContainerModel& model = ContainerModel::instance();
    const KeyContainer* kc = model.getActive();

    if (!kc) {
        lv_label_set_text(keyload_container_label, "ACTIVE: NONE");
        return;
    }

    size_t selected_count = 0;
    for (const auto& ke : kc->keys) {
        if (ke.selected) ++selected_count;
    }

    if (selected_count == 0) {
        lv_label_set_text_fmt(
            keyload_container_label,
            "ACTIVE: %s (0 selected)",
            kc->label.c_str()
        );
    } else {
        lv_label_set_text_fmt(
            keyload_container_label,
            "ACTIVE: %s (%u selected)",
            kc->label.c_str(),
            (unsigned)selected_count
        );
    }
}

// ----------------------
// HOME SCREEN
// ----------------------

static void build_home_screen(void) {
    home_screen = lv_obj_create(NULL);
    style_moto_screen(home_screen);

    // Top bar
    lv_obj_t* top_bar = lv_obj_create(home_screen);
    lv_obj_set_size(top_bar, 320, 44);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);

    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "KFD TERMINAL");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    home_user_label = lv_label_create(top_bar);
    lv_obj_set_style_text_color(home_user_label, lv_color_hex(0x90E4FF), 0);
    lv_obj_align(home_user_label, LV_ALIGN_RIGHT_MID, -6, 0);
    update_home_user_label();

    // Main menu tiles
    lv_obj_t* btn_keys = lv_btn_create(home_screen);
    lv_obj_set_size(btn_keys, 260, 55);
    lv_obj_align(btn_keys, LV_ALIGN_CENTER, 0, -70);
    style_moto_tile_button(btn_keys);
    lv_obj_add_event_cb(btn_keys, event_btn_keys, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_keys = lv_label_create(btn_keys);
    lv_label_set_text(lbl_keys, "KEY CONTAINERS");
    lv_obj_center(lbl_keys);

    lv_obj_t* btn_keyload = lv_btn_create(home_screen);
    lv_obj_set_size(btn_keyload, 260, 55);
    lv_obj_align(btn_keyload, LV_ALIGN_CENTER, 0, 0);
    style_moto_tile_button(btn_keyload);
    lv_obj_add_event_cb(btn_keyload, event_btn_keyload, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_keyload = lv_label_create(btn_keyload);
    lv_label_set_text(lbl_keyload, "KEYLOAD TO RADIO");
    lv_obj_center(lbl_keyload);

    lv_obj_t* btn_settings = lv_btn_create(home_screen);
    lv_obj_set_size(btn_settings, 260, 55);
    lv_obj_align(btn_settings, LV_ALIGN_CENTER, 0, 70);
    style_moto_tile_button(btn_settings);
    lv_obj_add_event_cb(btn_settings, event_btn_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_settings = lv_label_create(btn_settings);
    lv_label_set_text(lbl_settings, "SECURITY / SETTINGS");
    lv_obj_center(lbl_settings);

    // User manager button
    lv_obj_t* btn_user = lv_btn_create(home_screen);
    lv_obj_set_size(btn_user, 120, 32);
    lv_obj_align(btn_user, LV_ALIGN_TOP_RIGHT, -10, 50);
    style_moto_tile_button(btn_user);
    lv_obj_add_event_cb(btn_user, event_btn_user_manager, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_user = lv_label_create(btn_user);
    lv_label_set_text(lbl_user, "USER / LOGIN");
    lv_obj_center(lbl_user);

    // Bottom status strip
    lv_obj_t* bottom_bar = lv_obj_create(home_screen);
    lv_obj_set_size(bottom_bar, 320, 36);
    lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_bar, 0, 0);

    status_label = lv_label_create(bottom_bar);
    lv_label_set_text(status_label, "READY - LOGIN RECOMMENDED");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x80E0FF), 0);
    lv_obj_align(status_label, LV_ALIGN_LEFT_MID, 6, 0);
}

// ----------------------
// CONTAINERS SCREEN (inventory)
// ----------------------

static void build_containers_screen(void) {
    containers_screen = lv_obj_create(NULL);
    style_moto_screen(containers_screen);

    // Header
    lv_obj_t* top_bar = lv_obj_create(containers_screen);
    lv_obj_set_size(top_bar, 320, 40);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);

    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "CONTAINER INVENTORY");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -4, 0);
    style_moto_tile_button(btn_back);
    lv_obj_add_event_cb(btn_back, show_home_screen, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " HOME");
    lv_obj_center(lbl_back);

    // List
    lv_obj_t* list = lv_list_create(containers_screen);
    lv_obj_set_size(list, 300, 280);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x05121A), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(list, lv_color_hex(0x00C0FF), 0);
    lv_obj_set_style_border_width(list, 1, 0);

    ContainerModel& model = ContainerModel::instance();
    size_t count = model.getCount();

    for (size_t i = 0; i < count; ++i) {
        const KeyContainer& kc = model.get(i);

        char buf[96];
        lv_snprintf(
            buf, sizeof(buf),
            "%s (%s)",
            kc.label.c_str(),
            kc.agency.c_str()
        );

        lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_EDIT, buf);
        lv_obj_add_event_cb(btn, container_btn_event, LV_EVENT_CLICKED,
                            (void*)(uintptr_t)i);
    }

    // Bottom "ADD CONTAINER"
    lv_obj_t* bottom_bar = lv_obj_create(containers_screen);
    lv_obj_set_size(bottom_bar, 320, 50);
    lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_bar, 0, 0);

    lv_obj_t* btn_add = lv_btn_create(bottom_bar);
    lv_obj_set_size(btn_add, 180, 36);
    lv_obj_align(btn_add, LV_ALIGN_CENTER, 0, 0);
    style_moto_tile_button(btn_add);
    lv_obj_add_event_cb(btn_add, event_add_container, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_add = lv_label_create(btn_add);
    lv_label_set_text(lbl_add, LV_SYMBOL_PLUS " ADD CONTAINER");
    lv_obj_center(lbl_add);
}

static void event_add_container(lv_event_t* e) {
    (void)e;

    if (!check_access(true, "ADD CONTAINER")) return;

    ContainerModel& model = ContainerModel::instance();

    int idx = model.addContainer(
        "NEW CONTAINER",
        "AGENCY",
        "700/800",
        "AES256"
    );
    if (idx < 0) {
        if (status_label) {
            lv_label_set_text(status_label, "FAILED TO CREATE CONTAINER");
        }
        return;
    }

    current_container_index = idx;
    build_container_edit_screen(idx);
    if (container_edit_screen) {
        lv_scr_load(container_edit_screen);
    }
}

static void container_btn_event(lv_event_t* e) {
    uintptr_t idx_val = (uintptr_t) lv_event_get_user_data(e);
    size_t idx = static_cast<size_t>(idx_val);

    ContainerModel& model = ContainerModel::instance();
    if (idx >= model.getCount()) return;

    model.setActiveIndex(static_cast<int>(idx));
    const KeyContainer& kc = model.get(idx);
    current_container_index = static_cast<int>(idx);

    if (status_label) {
        lv_label_set_text_fmt(
            status_label,
            "CONTAINER SELECTED: %s",
            kc.label.c_str()
        );
    }

    update_keyload_container_label();

    build_container_detail_screen(static_cast<int>(idx));
    if (container_detail_screen) {
        lv_scr_load(container_detail_screen);
    }
}

// ----------------------
// CONTAINER DETAIL (keys inside container)
// ----------------------

static void rebuild_container_keys_list(int container_index) {
    if (!container_keys_list) return;

    lv_obj_clean(container_keys_list);

    ContainerModel& model = ContainerModel::instance();
    if (container_index < 0 ||
        static_cast<size_t>(container_index) >= model.getCount()) return;

    const KeyContainer& kc = model.get(container_index);

    for (size_t i = 0; i < kc.keys.size(); ++i) {
        const KeyEntry& ke = kc.keys[i];

        char buf[96];
        lv_snprintf(
            buf, sizeof(buf),
            "%02u: %s (%s)%s",
            ke.slot,
            ke.label.c_str(),
            ke.algo.c_str(),
            ke.selected ? " [SEL]" : ""
        );

        lv_obj_t* btn = lv_list_add_btn(container_keys_list, LV_SYMBOL_KEY, buf);
        lv_obj_add_event_cb(btn, key_item_event, LV_EVENT_CLICKED,
                            (void*)(uintptr_t)i);
    }
}

static void key_item_event(lv_event_t* e) {
    uintptr_t key_idx_val = (uintptr_t) lv_event_get_user_data(e);
    int key_idx = static_cast<int>(key_idx_val);

    if (current_container_index < 0) return;

    build_key_edit_screen(current_container_index, key_idx);
    if (key_edit_screen) {
        lv_scr_load(key_edit_screen);
    }
}

static void event_add_key(lv_event_t* e) {
    (void)e;
    if (current_container_index < 0) return;
    build_key_edit_screen(current_container_index, -1);
    if (key_edit_screen) {
        lv_scr_load(key_edit_screen);
    }
}

static void event_set_active_container(lv_event_t* e) {
    (void)e;
    if (current_container_index < 0) return;

    ContainerModel& model = ContainerModel::instance();
    model.setActiveIndex(current_container_index);

    const KeyContainer& kc = model.get(current_container_index);

    if (status_label) {
        lv_label_set_text_fmt(
            status_label,
            "ACTIVE: %s",
            kc.label.c_str()
        );
    }

    update_keyload_container_label();
}

static void event_edit_container_meta(lv_event_t* e) {
    (void)e;
    if (current_container_index < 0) return;

    if (!check_access(true, "EDIT CONTAINER")) return;

    build_container_edit_screen(current_container_index);
    if (container_edit_screen) {
        lv_scr_load(container_edit_screen);
    }
}

static void event_delete_container_confirm(lv_event_t* e);

static void event_delete_container(lv_event_t* e) {
    (void)e;
    if (current_container_index < 0) return;

    if (!check_access(true, "DELETE CONTAINER")) {
        return;
    }

    ContainerModel& model = ContainerModel::instance();
    if ((size_t)current_container_index >= model.getCount()) return;

    const KeyContainer& kc = model.get(current_container_index);

    // If no keys, delete directly
    if (kc.keys.empty()) {
        model.removeContainer(static_cast<size_t>(current_container_index));

        if (containers_screen) {
            lv_obj_del(containers_screen);
            containers_screen = nullptr;
        }
        build_containers_screen();
        lv_scr_load(containers_screen);
        return;
    }

    // Has keys -> show confirmation msgbox
    static const char* btns[] = { "Delete", "Cancel", "" };

    container_delete_mbox = lv_msgbox_create(
        NULL,
        "DELETE CONTAINER?",
        "This container has keys.\nDelete it permanently?",
        btns,
        true
    );
    lv_obj_center(container_delete_mbox);
    lv_obj_add_event_cb(container_delete_mbox,
                        event_delete_container_confirm,
                        LV_EVENT_VALUE_CHANGED,
                        NULL);
}

static void event_delete_container_confirm(lv_event_t* e) {
    lv_obj_t* mbox = lv_event_get_target(e);
    const char* btn_txt = lv_msgbox_get_active_btn_text(mbox);

    if (btn_txt && strcmp(btn_txt, "Delete") == 0) {
        ContainerModel& model = ContainerModel::instance();
        if (current_container_index >= 0 &&
            (size_t)current_container_index < model.getCount()) {
            model.removeContainer(static_cast<size_t>(current_container_index));
        }

        if (containers_screen) {
            lv_obj_del(containers_screen);
            containers_screen = nullptr;
        }
        build_containers_screen();
        lv_scr_load(containers_screen);
    } else {
        // Cancel – stay on detail screen
        if (container_detail_screen) {
            lv_scr_load(container_detail_screen);
        }
    }

    lv_obj_del(mbox);
    container_delete_mbox = nullptr;
}

static void build_container_detail_screen(int container_index) {
    current_container_index = container_index;

    ContainerModel& model = ContainerModel::instance();
    if (container_index < 0 ||
        static_cast<size_t>(container_index) >= model.getCount()) return;
    const KeyContainer& kc = model.get(container_index);

    if (!container_detail_screen) {
        container_detail_screen = lv_obj_create(NULL);
        style_moto_screen(container_detail_screen);

        // Header
        lv_obj_t* top_bar = lv_obj_create(container_detail_screen);
        lv_obj_set_size(top_bar, 320, 40);
        lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
        lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(top_bar, 0, 0);

        lv_obj_t* title = lv_label_create(top_bar);
        lv_label_set_text(title, "CONTAINER DETAIL");
        lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
        lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t* btn_back = lv_btn_create(top_bar);
        lv_obj_set_size(btn_back, 80, 30);
        lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -4, 0);
        style_moto_tile_button(btn_back);
        lv_obj_add_event_cb(btn_back, [](lv_event_t* ev){
            (void)ev;
            if (containers_screen) {
                lv_scr_load(containers_screen);
            }
        }, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_back = lv_label_create(btn_back);
        lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " LIST");
        lv_obj_center(lbl_back);

        // Keys list
        container_keys_list = lv_list_create(container_detail_screen);
        lv_obj_set_size(container_keys_list, 300, 250);
        lv_obj_align(container_keys_list, LV_ALIGN_TOP_MID, 0, 45);
        lv_obj_set_style_bg_color(container_keys_list, lv_color_hex(0x05121A), 0);
        lv_obj_set_style_bg_opa(container_keys_list, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(container_keys_list, lv_color_hex(0x00C0FF), 0);
        lv_obj_set_style_border_width(container_keys_list, 1, 0);

        // Bottom bar
        lv_obj_t* bottom_bar = lv_obj_create(container_detail_screen);
        lv_obj_set_size(bottom_bar, 320, 90);
        lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(0x001522), 0);
        lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bottom_bar, 0, 0);

        container_detail_status = lv_label_create(bottom_bar);
        lv_label_set_text(container_detail_status, "TAP KEY TO EDIT OR ADD");
        lv_obj_set_style_text_color(container_detail_status, lv_color_hex(0x80E0FF), 0);
        lv_obj_align(container_detail_status, LV_ALIGN_TOP_LEFT, 6, 4);

        // Row of three buttons
        lv_obj_t* btn_add = lv_btn_create(bottom_bar);
        lv_obj_set_size(btn_add, 90, 32);
        lv_obj_align(btn_add, LV_ALIGN_BOTTOM_LEFT, 6, -6);
        style_moto_tile_button(btn_add);
        lv_obj_add_event_cb(btn_add, event_add_key, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_add = lv_label_create(btn_add);
        lv_label_set_text(lbl_add, LV_SYMBOL_PLUS " KEY");
        lv_obj_center(lbl_add);

        lv_obj_t* btn_edit = lv_btn_create(bottom_bar);
        lv_obj_set_size(btn_edit, 90, 32);
        lv_obj_align(btn_edit, LV_ALIGN_BOTTOM_MID, 0, -6);
        style_moto_tile_button(btn_edit);
        lv_obj_add_event_cb(btn_edit, event_edit_container_meta, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_edit = lv_label_create(btn_edit);
        lv_label_set_text(lbl_edit, "EDIT");
        lv_obj_center(lbl_edit);

        lv_obj_t* btn_active = lv_btn_create(bottom_bar);
        lv_obj_set_size(btn_active, 90, 32);
        lv_obj_align(btn_active, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
        style_moto_tile_button(btn_active);
        lv_obj_add_event_cb(btn_active, event_set_active_container, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_active = lv_label_create(btn_active);
        lv_label_set_text(lbl_active, "ACTIVE");
        lv_obj_center(lbl_active);

        // Delete button row above
        lv_obj_t* btn_delete = lv_btn_create(bottom_bar);
        lv_obj_set_size(btn_delete, 160, 28);
        lv_obj_align(btn_delete, LV_ALIGN_BOTTOM_MID, 0, -44);
        style_moto_tile_button(btn_delete);
        lv_obj_add_event_cb(btn_delete, event_delete_container, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_del = lv_label_create(btn_delete);
        lv_label_set_text(lbl_del, LV_SYMBOL_TRASH " DELETE");
        lv_obj_center(lbl_del);
    }

    rebuild_container_keys_list(container_index);

    if (container_detail_status) {
        lv_label_set_text_fmt(
            container_detail_status,
            "%s / %s  (%u keys)",
            kc.agency.c_str(),
            kc.label.c_str(),
            (unsigned)kc.keys.size()
        );
    }
}

// ----------------------
// KEY EDIT SCREEN (tidied layout + keyboard logic)
// ----------------------

static void keyedit_textarea_event(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        keyedit_active_ta = ta;
        if (keyedit_kb) {
            lv_keyboard_set_textarea(keyedit_kb, ta);
            lv_obj_clear_flag(keyedit_kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void keyedit_keyboard_event(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (!keyedit_kb) return;

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        // Hide keyboard; user will use SAVE button to commit
        lv_obj_add_flag(keyedit_kb, LV_OBJ_FLAG_HIDDEN);
        keyedit_active_ta = nullptr;
    }
}

static void event_keyedit_gen_random(lv_event_t* e) {
    (void)e;
    if (!keyedit_key_ta || !keyedit_algo_dd) return;

    char algo_buf[32];
    lv_dropdown_get_selected_str(keyedit_algo_dd, algo_buf, sizeof(algo_buf));

    size_t key_bytes = 32; // default AES256
    if (strcmp(algo_buf, "AES128") == 0) {
        key_bytes = 16;
    } else if (strcmp(algo_buf, "DES-OFB") == 0) {
        key_bytes = 8;
    }

    uint8_t buf[32];
    if (key_bytes > sizeof(buf)) key_bytes = sizeof(buf);

    for (size_t i = 0; i < key_bytes; i += 4) {
        uint32_t r = esp_random();
        size_t chunk = (key_bytes - i) < 4 ? (key_bytes - i) : 4;
        memcpy(&buf[i], &r, chunk);
    }

    char hex[65];
    size_t hex_len = key_bytes * 2;
    if (hex_len >= sizeof(hex)) hex_len = sizeof(hex) - 1;

    for (size_t i = 0; i < key_bytes; ++i) {
        sprintf(&hex[i * 2], "%02X", buf[i]);
    }
    hex[hex_len] = '\0';

    lv_textarea_set_text(keyedit_key_ta, hex);

    if (keyedit_status_label) {
        lv_label_set_text(keyedit_status_label, "RANDOM KEY GENERATED");
    }
}

static void event_keyedit_save(lv_event_t* e) {
    (void)e;

    if (key_edit_container_idx < 0) return;

    ContainerModel& model = ContainerModel::instance();
    if ((size_t)key_edit_container_idx >= model.getCount()) return;

    KeyContainer& kc = model.getMutable(key_edit_container_idx);

    const char* label = lv_textarea_get_text(keyedit_label_ta);
    const char* keyhx = lv_textarea_get_text(keyedit_key_ta);

    char algo_buf[32];
    lv_dropdown_get_selected_str(keyedit_algo_dd, algo_buf, sizeof(algo_buf));

    bool selected = lv_obj_has_state(keyedit_selected_cb, LV_STATE_CHECKED);

    if (!label || strlen(label) == 0 || !keyhx || strlen(keyhx) < 16) {
        if (keyedit_status_label) {
            lv_label_set_text(keyedit_status_label, "LABEL/KEY TOO SHORT");
        }
        return;
    }

    if (key_edit_key_idx >= 0 &&
        (size_t)key_edit_key_idx < kc.keys.size()) {
        // Edit existing
        KeyEntry& ke = kc.keys[key_edit_key_idx];
        ke.label    = label;
        ke.algo     = algo_buf;
        ke.keyHex   = keyhx;
        ke.selected = selected;
    } else {
        // New key
        KeyEntry ke;
        uint8_t max_slot = 0;
        for (const auto& existing : kc.keys) {
            if (existing.slot > max_slot) max_slot = existing.slot;
        }
        ke.slot     = max_slot + 1;
        ke.label    = label;
        ke.algo     = algo_buf;
        ke.keyHex   = keyhx;
        ke.selected = selected;
        kc.keys.push_back(ke);
    }

    kc.hasKeys = !kc.keys.empty();
    model.save();

    if (keyedit_status_label) {
        lv_label_set_text(keyedit_status_label, "KEY SAVED");
    }

    build_container_detail_screen(key_edit_container_idx);
    if (container_detail_screen) {
        lv_scr_load(container_detail_screen);
    }
}

static void event_keyedit_cancel(lv_event_t* e) {
    (void)e;
    if (container_detail_screen) {
        lv_scr_load(container_detail_screen);
    }
}

static void build_key_edit_screen(int container_index, int key_index) {
    key_edit_container_idx = container_index;
    key_edit_key_idx       = key_index;

    ContainerModel& model = ContainerModel::instance();
    if (container_index < 0 ||
        (size_t)container_index >= model.getCount()) return;
    KeyContainer& kc = model.getMutable(container_index);

    bool is_new = (key_index < 0 ||
                   (size_t)key_index >= kc.keys.size());
    KeyEntry* ke = nullptr;
    if (!is_new) ke = &kc.keys[key_index];

    if (!key_edit_screen) {
        key_edit_screen = lv_obj_create(NULL);
        style_moto_screen(key_edit_screen);

        // Header
        lv_obj_t* top_bar = lv_obj_create(key_edit_screen);
        lv_obj_set_size(top_bar, 320, 40);
        lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
        lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(top_bar, 0, 0);

        lv_obj_t* title = lv_label_create(top_bar);
        lv_label_set_text(title, "KEY EDIT");
        lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
        lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t* btn_back = lv_btn_create(top_bar);
        lv_obj_set_size(btn_back, 80, 30);
        lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -4, 0);
        style_moto_tile_button(btn_back);
        lv_obj_add_event_cb(btn_back, event_keyedit_cancel, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_back = lv_label_create(btn_back);
        lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " BACK");
        lv_obj_center(lbl_back);

        // LABEL
        lv_obj_t* lbl1 = lv_label_create(key_edit_screen);
        lv_label_set_text(lbl1, "LABEL:");
        lv_obj_set_style_text_color(lbl1, lv_color_hex(0x80E0FF), 0);
        lv_obj_align(lbl1, LV_ALIGN_TOP_LEFT, 10, 50);

        keyedit_label_ta = lv_textarea_create(key_edit_screen);
        lv_textarea_set_max_length(keyedit_label_ta, 32);
        lv_obj_set_width(keyedit_label_ta, 280);
        lv_obj_align(keyedit_label_ta, LV_ALIGN_TOP_MID, 0, 70);
        lv_obj_add_event_cb(keyedit_label_ta, keyedit_textarea_event, LV_EVENT_ALL, NULL);

        // ALGO
        lv_obj_t* lbl2 = lv_label_create(key_edit_screen);
        lv_label_set_text(lbl2, "ALGO:");
        lv_obj_set_style_text_color(lbl2, lv_color_hex(0x80E0FF), 0);
        lv_obj_align(lbl2, LV_ALIGN_TOP_LEFT, 10, 105);

        keyedit_algo_dd = lv_dropdown_create(key_edit_screen);
        lv_dropdown_set_options_static(keyedit_algo_dd,
                                       "AES256\nAES128\nDES-OFB");
        lv_obj_set_width(keyedit_algo_dd, 140);
        lv_obj_align(keyedit_algo_dd, LV_ALIGN_TOP_LEFT, 70, 100);

        // Selected checkbox
        keyedit_selected_cb = lv_checkbox_create(key_edit_screen);
        lv_checkbox_set_text(keyedit_selected_cb, "Select for load");
        lv_obj_set_style_text_color(keyedit_selected_cb, lv_color_hex(0xC8F4FF), 0);
        lv_obj_align(keyedit_selected_cb, LV_ALIGN_TOP_RIGHT, -10, 100);

        // KEY textarea
        lv_obj_t* lbl3 = lv_label_create(key_edit_screen);
        lv_label_set_text(lbl3, "KEY (HEX):");
        lv_obj_set_style_text_color(lbl3, lv_color_hex(0x80E0FF), 0);
        lv_obj_align(lbl3, LV_ALIGN_TOP_LEFT, 10, 140);

        keyedit_key_ta = lv_textarea_create(key_edit_screen);
        lv_textarea_set_max_length(keyedit_key_ta, 64);
        lv_textarea_set_one_line(keyedit_key_ta, false);
        lv_obj_set_size(keyedit_key_ta, 300, 80);
        lv_obj_align(keyedit_key_ta, LV_ALIGN_TOP_MID, 0, 160);
        lv_obj_add_event_cb(keyedit_key_ta, keyedit_textarea_event, LV_EVENT_ALL, NULL);

        // Status
        keyedit_status_label = lv_label_create(key_edit_screen);
        lv_label_set_text(keyedit_status_label, "EDIT OR GENERATE KEY");
        lv_obj_set_style_text_color(keyedit_status_label, lv_color_hex(0xFFD0A0), 0);
        lv_obj_align(keyedit_status_label, LV_ALIGN_TOP_LEFT, 10, 245);

        // Buttons (just above keyboard zone)
        lv_obj_t* btn_gen = lv_btn_create(key_edit_screen);
        lv_obj_set_size(btn_gen, 130, 35);
        lv_obj_align(btn_gen, LV_ALIGN_TOP_LEFT, 10, 275);
        style_moto_tile_button(btn_gen);
        lv_obj_add_event_cb(btn_gen, event_keyedit_gen_random, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_gen = lv_label_create(btn_gen);
        lv_label_set_text(lbl_gen, "GEN RANDOM");
        lv_obj_center(lbl_gen);

        lv_obj_t* btn_save = lv_btn_create(key_edit_screen);
        lv_obj_set_size(btn_save, 130, 35);
        lv_obj_align(btn_save, LV_ALIGN_TOP_RIGHT, -10, 275);
        style_moto_tile_button(btn_save);
        lv_obj_add_event_cb(btn_save, event_keyedit_save, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_save = lv_label_create(btn_save);
        lv_label_set_text(lbl_save, "SAVE");
        lv_obj_center(lbl_save);

        // On-screen keyboard (bottom 150px)
        keyedit_kb = lv_keyboard_create(key_edit_screen);
        lv_obj_set_size(keyedit_kb, 320, 150);
        lv_obj_align(keyedit_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(keyedit_kb, keyedit_keyboard_event, LV_EVENT_ALL, NULL);
        lv_obj_add_flag(keyedit_kb, LV_OBJ_FLAG_HIDDEN); // hidden until focus
    }

    // Populate fields
    if (is_new) {
        lv_textarea_set_text(keyedit_label_ta, "");
        lv_dropdown_set_selected(keyedit_algo_dd, 0); // AES256
        lv_obj_clear_state(keyedit_selected_cb, LV_STATE_CHECKED);
        lv_textarea_set_text(keyedit_key_ta, "");
        lv_label_set_text(keyedit_status_label, "NEW KEY - ENTER OR GENERATE");
    } else if (ke) {
        lv_textarea_set_text(keyedit_label_ta, ke->label.c_str());

        if (ke->algo == "AES256")      lv_dropdown_set_selected(keyedit_algo_dd, 0);
        else if (ke->algo == "AES128") lv_dropdown_set_selected(keyedit_algo_dd, 1);
        else if (ke->algo == "DES-OFB")lv_dropdown_set_selected(keyedit_algo_dd, 2);
        else                           lv_dropdown_set_selected(keyedit_algo_dd, 0);

        if (ke->selected) {
            lv_obj_add_state(keyedit_selected_cb, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(keyedit_selected_cb, LV_STATE_CHECKED);
        }

        lv_textarea_set_text(keyedit_key_ta, ke->keyHex.c_str());
        lv_label_set_text(keyedit_status_label, "EDIT EXISTING KEY");
    }

    // Hide keyboard initially
    if (keyedit_kb) {
        lv_obj_add_flag(keyedit_kb, LV_OBJ_FLAG_HIDDEN);
        keyedit_active_ta = nullptr;
    }
}

// ----------------------
// CONTAINER EDIT SCREEN (tidied layout + keyboard logic)
// ----------------------

static void cedit_textarea_event(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        if (cedit_kb) {
            lv_keyboard_set_textarea(cedit_kb, ta);
            lv_obj_clear_flag(cedit_kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void cedit_keyboard_event(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (!cedit_kb) return;

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(cedit_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void event_container_edit_save(lv_event_t* e) {
    (void)e;
    if (cedit_index < 0) return;

    ContainerModel& model = ContainerModel::instance();
    if ((size_t)cedit_index >= model.getCount()) return;

    KeyContainer& kc = model.getMutable(cedit_index);

    const char* label  = lv_textarea_get_text(cedit_label_ta);
    const char* agency = lv_textarea_get_text(cedit_agency_ta);
    const char* band   = lv_textarea_get_text(cedit_band_ta);

    char algo_buf[32];
    lv_dropdown_get_selected_str(cedit_algo_dd, algo_buf, sizeof(algo_buf));

    bool locked = lv_obj_has_state(cedit_locked_cb, LV_STATE_CHECKED);

    if (!label || strlen(label) == 0) {
        if (cedit_status_label) {
            lv_label_set_text(cedit_status_label, "LABEL REQUIRED");
        }
        return;
    }

    kc.label  = label;
    kc.agency = agency ? agency : "";
    kc.band   = band   ? band   : "";
    kc.algo   = algo_buf;
    kc.locked = locked;

    model.save();

    if (cedit_status_label) {
        lv_label_set_text(cedit_status_label, "CONTAINER SAVED");
    }

    build_container_detail_screen(cedit_index);
    if (container_detail_screen) {
        lv_scr_load(container_detail_screen);
    }
}

static void event_container_edit_cancel(lv_event_t* e) {
    (void)e;
    if (container_detail_screen) {
        lv_scr_load(container_detail_screen);
    }
}

static void build_container_edit_screen(int container_index) {
    cedit_index = container_index;

    ContainerModel& model = ContainerModel::instance();
    if (container_index < 0 ||
        (size_t)container_index >= model.getCount()) return;
    KeyContainer& kc = model.getMutable(container_index);

    if (!container_edit_screen) {
        container_edit_screen = lv_obj_create(NULL);
        style_moto_screen(container_edit_screen);

        // Header
        lv_obj_t* top_bar = lv_obj_create(container_edit_screen);
        lv_obj_set_size(top_bar, 320, 40);
        lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
        lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(top_bar, 0, 0);

        lv_obj_t* title = lv_label_create(top_bar);
        lv_label_set_text(title, "CONTAINER EDIT");
        lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
        lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t* btn_back = lv_btn_create(top_bar);
        lv_obj_set_size(btn_back, 80, 30);
        lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -4, 0);
        style_moto_tile_button(btn_back);
        lv_obj_add_event_cb(btn_back, event_container_edit_cancel, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_back = lv_label_create(btn_back);
        lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " BACK");
        lv_obj_center(lbl_back);

        // LABEL
        lv_obj_t* lbl1 = lv_label_create(container_edit_screen);
        lv_label_set_text(lbl1, "LABEL:");
        lv_obj_set_style_text_color(lbl1, lv_color_hex(0x80E0FF), 0);
        lv_obj_align(lbl1, LV_ALIGN_TOP_LEFT, 10, 50);

        cedit_label_ta = lv_textarea_create(container_edit_screen);
        lv_textarea_set_max_length(cedit_label_ta, 32);
        lv_obj_set_width(cedit_label_ta, 280);
        lv_obj_align(cedit_label_ta, LV_ALIGN_TOP_MID, 0, 70);
        lv_obj_add_event_cb(cedit_label_ta, cedit_textarea_event, LV_EVENT_ALL, NULL);

        // AGENCY
        lv_obj_t* lbl2 = lv_label_create(container_edit_screen);
        lv_label_set_text(lbl2, "AGENCY:");
        lv_obj_set_style_text_color(lbl2, lv_color_hex(0x80E0FF), 0);
        lv_obj_align(lbl2, LV_ALIGN_TOP_LEFT, 10, 105);

        cedit_agency_ta = lv_textarea_create(container_edit_screen);
        lv_textarea_set_max_length(cedit_agency_ta, 32);
        lv_obj_set_width(cedit_agency_ta, 280);
        lv_obj_align(cedit_agency_ta, LV_ALIGN_TOP_MID, 0, 125);
        lv_obj_add_event_cb(cedit_agency_ta, cedit_textarea_event, LV_EVENT_ALL, NULL);

        // BAND
        lv_obj_t* lbl3 = lv_label_create(container_edit_screen);
        lv_label_set_text(lbl3, "BAND:");
        lv_obj_set_style_text_color(lbl3, lv_color_hex(0x80E0FF), 0);
        lv_obj_align(lbl3, LV_ALIGN_TOP_LEFT, 10, 160);

        cedit_band_ta = lv_textarea_create(container_edit_screen);
        lv_textarea_set_max_length(cedit_band_ta, 16);
        lv_obj_set_width(cedit_band_ta, 120);
        lv_obj_align(cedit_band_ta, LV_ALIGN_TOP_LEFT, 70, 180);
        lv_obj_add_event_cb(cedit_band_ta, cedit_textarea_event, LV_EVENT_ALL, NULL);

        // ALGO dropdown
        lv_obj_t* lbl4 = lv_label_create(container_edit_screen);
        lv_label_set_text(lbl4, "DEFAULT ALGO:");
        lv_obj_set_style_text_color(lbl4, lv_color_hex(0x80E0FF), 0);
        lv_obj_align(lbl4, LV_ALIGN_TOP_RIGHT, -10, 160);

        cedit_algo_dd = lv_dropdown_create(container_edit_screen);
        lv_dropdown_set_options_static(cedit_algo_dd,
                                       "AES256\nAES128\nDES-OFB");
        lv_obj_set_width(cedit_algo_dd, 120);
        lv_obj_align(cedit_algo_dd, LV_ALIGN_TOP_RIGHT, -10, 180);

        // Locked checkbox
        cedit_locked_cb = lv_checkbox_create(container_edit_screen);
        lv_checkbox_set_text(cedit_locked_cb, "Lock container (view-only)");
        lv_obj_set_style_text_color(cedit_locked_cb, lv_color_hex(0xC8F4FF), 0);
        lv_obj_align(cedit_locked_cb, LV_ALIGN_TOP_LEFT, 10, 215);

        // Status
        cedit_status_label = lv_label_create(container_edit_screen);
        lv_label_set_text(cedit_status_label, "EDIT CONTAINER METADATA");
        lv_obj_set_style_text_color(cedit_status_label, lv_color_hex(0xFFD0A0), 0);
        lv_obj_align(cedit_status_label, LV_ALIGN_TOP_LEFT, 10, 240);

        // Buttons (above keyboard zone)
        lv_obj_t* btn_save = lv_btn_create(container_edit_screen);
        lv_obj_set_size(btn_save, 120, 35);
        lv_obj_align(btn_save, LV_ALIGN_TOP_RIGHT, -10, 275);
        style_moto_tile_button(btn_save);
        lv_obj_add_event_cb(btn_save, event_container_edit_save, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_save = lv_label_create(btn_save);
        lv_label_set_text(lbl_save, "SAVE");
        lv_obj_center(lbl_save);

        lv_obj_t* btn_cancel = lv_btn_create(container_edit_screen);
        lv_obj_set_size(btn_cancel, 120, 35);
        lv_obj_align(btn_cancel, LV_ALIGN_TOP_LEFT, 10, 275);
        style_moto_tile_button(btn_cancel);
        lv_obj_add_event_cb(btn_cancel, event_container_edit_cancel, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
        lv_label_set_text(lbl_cancel, "CANCEL");
        lv_obj_center(lbl_cancel);

        // On-screen keyboard
        cedit_kb = lv_keyboard_create(container_edit_screen);
        lv_obj_set_size(cedit_kb, 320, 150);
        lv_obj_align(cedit_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(cedit_kb, cedit_keyboard_event, LV_EVENT_ALL, NULL);
        lv_obj_add_flag(cedit_kb, LV_OBJ_FLAG_HIDDEN);
    }

    // Populate from kc
    lv_textarea_set_text(cedit_label_ta, kc.label.c_str());
    lv_textarea_set_text(cedit_agency_ta, kc.agency.c_str());
    lv_textarea_set_text(cedit_band_ta, kc.band.c_str());

    if (kc.algo == "AES256")      lv_dropdown_set_selected(cedit_algo_dd, 0);
    else if (kc.algo == "AES128") lv_dropdown_set_selected(cedit_algo_dd, 1);
    else if (kc.algo == "DES-OFB")lv_dropdown_set_selected(cedit_algo_dd, 2);
    else                          lv_dropdown_set_selected(cedit_algo_dd, 0);

    if (kc.locked)
        lv_obj_add_state(cedit_locked_cb, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(cedit_locked_cb, LV_STATE_CHECKED);

    lv_label_set_text(cedit_status_label, "EDIT CONTAINER METADATA");

    if (cedit_kb) {
        lv_obj_add_flag(cedit_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

// ----------------------
// KEYLOAD SCREEN
// ----------------------

static void keyload_timer_cb(lv_timer_t* t) {
    (void)t;
    keyload_progress += 5;
    if (keyload_progress > 100) {
        keyload_progress = 100;
        if (keyload_timer) {
            lv_timer_del(keyload_timer);
            keyload_timer = nullptr;
        }
        lv_label_set_text(keyload_status, "KEYLOAD COMPLETE - VERIFY RADIO");
        if (status_label) {
            lv_label_set_text(status_label, "KEYLOAD COMPLETE");
        }
    }
    lv_bar_set_value(keyload_bar, keyload_progress, LV_ANIM_ON);
}

static void event_btn_keyload_start(lv_event_t* e) {
    (void)e;

    if (!check_access(false, "KEYLOAD START")) {
        return;
    }

    const ContainerModel& model = ContainerModel::instance();
    const KeyContainer* kc = model.getActive();
    if (!kc) {
        lv_label_set_text(keyload_status, "NO ACTIVE CONTAINER");
        if (status_label) {
            lv_label_set_text(status_label, "SELECT CONTAINER FIRST");
        }
        return;
    }

    size_t selected_count = 0;
    for (const auto& ke : kc->keys) {
        if (ke.selected) ++selected_count;
    }

    if (selected_count == 0) {
        lv_label_set_text(keyload_status, "NO KEYS SELECTED IN CONTAINER");
        if (status_label) {
            lv_label_set_text(status_label, "SELECT KEYS IN CONTAINER DETAIL");
        }
        return;
    }

    keyload_progress = 0;
    lv_bar_set_value(keyload_bar, 0, LV_ANIM_OFF);

    lv_label_set_text_fmt(
        keyload_status,
        "KEYLOAD: %s (%u keys)",
        kc->label.c_str(),
        (unsigned)selected_count
    );

    if (!keyload_timer) {
        keyload_timer = lv_timer_create(keyload_timer_cb, 200, NULL);
    }
}

static void build_keyload_screen(void) {
    keyload_screen = lv_obj_create(NULL);
    style_moto_screen(keyload_screen);

    // Header
    lv_obj_t* top_bar = lv_obj_create(keyload_screen);
    lv_obj_set_size(top_bar, 320, 40);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);

    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "KEYLOAD CONSOLE");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -4, 0);
    style_moto_tile_button(btn_back);
    lv_obj_add_event_cb(btn_back, show_home_screen, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " HOME");
    lv_obj_center(lbl_back);

    // Panel
    lv_obj_t* panel = lv_obj_create(keyload_screen);
    lv_obj_set_size(panel, 300, 110);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x05121A), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x00C0FF), 0);
    lv_obj_set_style_border_width(panel, 1, 0);

    lv_obj_t* info = lv_label_create(panel);
    lv_label_set_text(info,
        "CONNECT RADIO VIA KVL CABLE\n"
        "LINK: STANDBY\n"
        "MODE: APX / P25"
    );
    lv_obj_set_style_text_color(info, lv_color_hex(0xC8F4FF), 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 6, 4);

    keyload_container_label = lv_label_create(panel);
    lv_obj_set_style_text_color(keyload_container_label, lv_color_hex(0x80E0FF), 0);
    lv_obj_align(keyload_container_label, LV_ALIGN_BOTTOM_LEFT, 6, -4);
    update_keyload_container_label();

    // Progress bar
    keyload_bar = lv_bar_create(keyload_screen);
    lv_obj_set_size(keyload_bar, 260, 20);
    lv_obj_align(keyload_bar, LV_ALIGN_CENTER, 0, 0);
    lv_bar_set_range(keyload_bar, 0, 100);
    lv_bar_set_value(keyload_bar, 0, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(keyload_bar, lv_color_hex(0x1A2630), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(keyload_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyload_bar, lv_color_hex(0x00C0FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(keyload_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    // Status label
    keyload_status = lv_label_create(keyload_screen);
    lv_label_set_text(keyload_status, "IDLE - READY");
    lv_obj_set_style_text_color(keyload_status, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(keyload_status, LV_ALIGN_CENTER, 0, 40);

    // START button
    lv_obj_t* btn_start = lv_btn_create(keyload_screen);
    lv_obj_set_size(btn_start, 160, 55);
    lv_obj_align(btn_start, LV_ALIGN_CENTER, 0, 110);
    style_moto_tile_button(btn_start);
    lv_obj_add_event_cb(btn_start, event_btn_keyload_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_start = lv_label_create(btn_start);
    lv_label_set_text(lbl_start, LV_SYMBOL_PLAY "  START LOAD");
    lv_obj_center(lbl_start);
}

// ----------------------
// SETTINGS SCREEN
// ----------------------

static void build_settings_screen(void) {
    settings_screen = lv_obj_create(NULL);
    style_moto_screen(settings_screen);

    lv_obj_t* top_bar = lv_obj_create(settings_screen);
    lv_obj_set_size(top_bar, 320, 40);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);

    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "SECURITY / SETTINGS");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -4, 0);
    style_moto_tile_button(btn_back);
    lv_obj_add_event_cb(btn_back, show_home_screen, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " HOME");
    lv_obj_center(lbl_back);

    lv_obj_t* cb_confirm = lv_checkbox_create(settings_screen);
    lv_checkbox_set_text(cb_confirm, "Require PIN before keyload");
    lv_obj_set_style_text_color(cb_confirm, lv_color_hex(0xC8F4FF), 0);
    lv_obj_align(cb_confirm, LV_ALIGN_TOP_LEFT, 20, 60);

    lv_obj_t* cb_wipe = lv_checkbox_create(settings_screen);
    lv_checkbox_set_text(cb_wipe, "Wipe containers after 10 failed PINs");
    lv_obj_set_style_text_color(cb_wipe, lv_color_hex(0xFFD0A0), 0);
    lv_obj_align(cb_wipe, LV_ALIGN_TOP_LEFT, 20, 100);

    lv_obj_t* cb_audit = lv_checkbox_create(settings_screen);
    lv_checkbox_set_text(cb_audit, "Enable audit log to SD");
    lv_obj_set_style_text_color(cb_audit, lv_color_hex(0xC8F4FF), 0);
    lv_obj_align(cb_audit, LV_ALIGN_TOP_LEFT, 20, 140);
}

// ----------------------
// USER MANAGER
// ----------------------

static void reset_pin_buffer() {
    pin_len = 0;
    memset(pin_buffer, 0, sizeof(pin_buffer));
    if (pin_label) {
        lv_label_set_text(pin_label, "----");
    }
}

static void set_pending_role(UserRole role, const char* label_text) {
    pending_role = role;
    reset_pin_buffer();
    if (user_role_label) {
        lv_label_set_text(user_role_label, label_text);
    }
    if (user_status_label) {
        lv_label_set_text(user_status_label, "ENTER PIN");
    }
}

static void build_user_screen(void) {
    user_screen = lv_obj_create(NULL);
    style_moto_screen(user_screen);

    // Header
    lv_obj_t* top_bar = lv_obj_create(user_screen);
    lv_obj_set_size(top_bar, 320, 40);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);

    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "USER LOGIN / ROLE");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -4, 0);
    style_moto_tile_button(btn_back);
    lv_obj_add_event_cb(btn_back, show_home_screen, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " HOME");
    lv_obj_center(lbl_back);

    // Role selection
    lv_obj_t* btn_admin = lv_btn_create(user_screen);
    lv_obj_set_size(btn_admin, 120, 40);
    lv_obj_align(btn_admin, LV_ALIGN_TOP_LEFT, 15, 55);
    style_moto_tile_button(btn_admin);
    lv_obj_add_event_cb(btn_admin, event_select_admin, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_admin = lv_label_create(btn_admin);
    lv_label_set_text(lbl_admin, "ADMIN");
    lv_obj_center(lbl_admin);

    lv_obj_t* btn_operator = lv_btn_create(user_screen);
    lv_obj_set_size(btn_operator, 120, 40);
    lv_obj_align(btn_operator, LV_ALIGN_TOP_RIGHT, -15, 55);
    style_moto_tile_button(btn_operator);
    lv_obj_add_event_cb(btn_operator, event_select_operator, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_operator = lv_label_create(btn_operator);
    lv_label_set_text(lbl_operator, "OPERATOR");
    lv_obj_center(lbl_operator);

    // Role + PIN display
    user_role_label = lv_label_create(user_screen);
    lv_label_set_text(user_role_label, "LOGIN: (SELECT ROLE)");
    lv_obj_set_style_text_color(user_role_label, lv_color_hex(0xC8F4FF), 0);
    lv_obj_align(user_role_label, LV_ALIGN_TOP_MID, 0, 110);

    lv_obj_t* pin_caption = lv_label_create(user_screen);
    lv_label_set_text(pin_caption, "PIN:");
    lv_obj_set_style_text_color(pin_caption, lv_color_hex(0x80E0FF), 0);
    lv_obj_align(pin_caption, LV_ALIGN_TOP_LEFT, 40, 135);

    pin_label = lv_label_create(user_screen);
    lv_label_set_text(pin_label, "----");
    lv_obj_set_style_text_color(pin_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(pin_label, LV_ALIGN_TOP_LEFT, 90, 135);

    user_status_label = lv_label_create(user_screen);
    lv_label_set_text(user_status_label, "SELECT ROLE");
    lv_obj_set_style_text_color(user_status_label, lv_color_hex(0xFFD0A0), 0);
    lv_obj_align(user_status_label, LV_ALIGN_TOP_MID, 0, 160);

    // Numeric keypad
    const char* keys[12] = {
        "1","2","3",
        "4","5","6",
        "7","8","9",
        "CLR","0","OK"
    };
    int idx = 0;
    int start_y = 190;

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 3; ++col) {
            const char* txt = keys[idx++];
            lv_obj_t* btn = lv_btn_create(user_screen);
            lv_obj_set_size(btn, 70, 40);
            lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 20 + col * 90, start_y + row * 45);
            style_moto_tile_button(btn);

            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, txt);
            lv_obj_center(lbl);

            if (strcmp(txt, "CLR") == 0) {
                lv_obj_add_event_cb(btn, event_keypad_clear, LV_EVENT_CLICKED, NULL);
            } else if (strcmp(txt, "OK") == 0) {
                lv_obj_add_event_cb(btn, event_keypad_ok, LV_EVENT_CLICKED, NULL);
            } else {
                lv_obj_add_event_cb(btn, event_keypad_digit, LV_EVENT_CLICKED, NULL);
            }
        }
    }

    reset_pin_buffer();
}

static void event_select_admin(lv_event_t* e) {
    (void)e;
    set_pending_role(ROLE_ADMIN, "LOGIN: ADMIN");
}

static void event_select_operator(lv_event_t* e) {
    (void)e;
    set_pending_role(ROLE_OPERATOR, "LOGIN: OPERATOR");
}

static void event_keypad_digit(lv_event_t* e) {
    if (pending_role == ROLE_NONE) {
        if (user_status_label) {
            lv_label_set_text(user_status_label, "SELECT ROLE FIRST");
        }
        return;
    }

    if (pin_len >= sizeof(pin_buffer) - 1) {
        return;
    }

    lv_obj_t* btn = lv_event_get_target(e);
    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
    const char* txt = lv_label_get_text(lbl);
    if (!txt || strlen(txt) != 1) return;

    pin_buffer[pin_len++] = txt[0];
    pin_buffer[pin_len] = '\0';

    char stars[8];
    uint8_t count = (pin_len < (sizeof(stars) - 1)) ? pin_len : (sizeof(stars) - 1);
    for (uint8_t i = 0; i < count; ++i) {
        stars[i] = '*';
    }
    stars[count] = '\0';
    lv_label_set_text(pin_label, stars);
}

static void event_keypad_clear(lv_event_t* e) {
    (void)e;
    reset_pin_buffer();
    if (user_status_label) {
        lv_label_set_text(user_status_label, "PIN CLEARED");
    }
}

static void event_keypad_ok(lv_event_t* e) {
    (void)e;
    if (pending_role == ROLE_NONE) {
        if (user_status_label) {
            lv_label_set_text(user_status_label, "SELECT ROLE FIRST");
        }
        return;
    }

    const char* expected = nullptr;
    const char* user_name = nullptr;

    if (pending_role == ROLE_ADMIN) {
        expected  = PIN_ADMIN;
        user_name = "ADMIN";
    } else if (pending_role == ROLE_OPERATOR) {
        expected  = PIN_OPERATOR;
        user_name = "OPERATOR";
    }

    if (!expected) return;

    if (strcmp(pin_buffer, expected) == 0) {
        current_role = pending_role;
        current_user_name = user_name;

        if (user_status_label) {
            lv_label_set_text(user_status_label, "LOGIN OK");
        }
        if (status_label) {
            lv_label_set_text(status_label, "LOGIN OK");
        }
        update_home_user_label();
        reset_pin_buffer();

        if (home_screen) {
            lv_scr_load(home_screen);
        }
    } else {
        if (user_status_label) {
            lv_label_set_text(user_status_label, "PIN INVALID");
        }
        reset_pin_buffer();
    }
}

// ----------------------
// Navigation helpers
// ----------------------

static void show_home_screen(lv_event_t* e) {
    (void)e;
    if (home_screen) {
        lv_scr_load(home_screen);
        if (status_label) {
            lv_label_set_text(status_label, "READY - LOGIN RECOMMENDED");
        }
    }
}

static void event_btn_keys(lv_event_t* e) {
    (void)e;

    if (!check_access(false, "CONTAINER VIEW OPEN")) return;

    if (!containers_screen) {
        build_containers_screen();
    }
    lv_scr_load(containers_screen);
}

static void event_btn_keyload(lv_event_t* e) {
    (void)e;

    if (!check_access(false, "KEYLOAD CONSOLE OPEN")) return;

    if (!keyload_screen) {
        build_keyload_screen();
    } else {
        update_keyload_container_label();
    }

    lv_scr_load(keyload_screen);
}

static void event_btn_settings(lv_event_t* e) {
    (void)e;

    if (!check_access(true, "SETTINGS OPEN")) return;

    if (!settings_screen) {
        build_settings_screen();
    }
    lv_scr_load(settings_screen);
}

static void event_btn_user_manager(lv_event_t* e) {
    (void)e;
    if (status_label) {
        lv_label_set_text(status_label, "USER LOGIN SCREEN");
    }
    if (!user_screen) {
        build_user_screen();
    }
    lv_scr_load(user_screen);
}

// ----------------------
// Public entrypoint
// ----------------------

void ui_init(void) {
    build_home_screen();
    lv_scr_load(home_screen);
}
