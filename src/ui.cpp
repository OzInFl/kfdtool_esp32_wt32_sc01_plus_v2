#include "ui.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "container_model.h"
#include <esp_system.h>  // esp_random()

#ifndef LV_SYMBOL_KEY
    #define LV_SYMBOL_KEY LV_SYMBOL_BELL
#endif

// ----------------------
// User roles
// ----------------------
enum UserRole {
    ROLE_NONE = 0,
    ROLE_OPERATOR,
    ROLE_ADMIN
};

static UserRole    current_role      = ROLE_NONE;
static const char* current_user_name = "NONE";

static const char* PIN_ADMIN    = "5000";
static const char* PIN_OPERATOR = "1111";

static lv_obj_t* factory_reset_mbox     = nullptr;

// ----------------------
// Screens
// ----------------------
static lv_obj_t* home_screen          = nullptr;
static lv_obj_t* containers_screen    = nullptr;
static lv_obj_t* keyload_screen       = nullptr;
static lv_obj_t* settings_screen      = nullptr;
static lv_obj_t* user_screen          = nullptr;

static lv_obj_t* container_detail_screen = nullptr;
static lv_obj_t* key_edit_screen         = nullptr;
static lv_obj_t* container_edit_screen   = nullptr;

// Common widgets
static lv_obj_t* status_label        = nullptr; // bottom bar status on home
static lv_obj_t* home_user_label     = nullptr; // top bar user indicator

// Keyload widgets
static lv_obj_t* keyload_status          = nullptr;
static lv_obj_t* keyload_bar             = nullptr;
static lv_obj_t* keyload_container_label = nullptr;
static lv_obj_t* keyload_container_dd    = nullptr; // select container from keyload screen
static lv_timer_t* keyload_timer         = nullptr;
static int        keyload_progress       = 0;

// User manager widgets
static lv_obj_t* user_role_label    = nullptr;
static lv_obj_t* pin_label          = nullptr;
static lv_obj_t* user_status_label  = nullptr;
static char      pin_buffer[8];
static uint8_t   pin_len            = 0;
static UserRole  pending_role       = ROLE_NONE;

// Container detail UI
static lv_obj_t* container_keys_list     = nullptr;
static lv_obj_t* container_detail_status = nullptr;
static int       current_container_index = -1;

// Key edit UI
static lv_obj_t* keyedit_label_ta       = nullptr;
static lv_obj_t* keyedit_algo_dd        = nullptr;
static lv_obj_t* keyedit_key_ta         = nullptr;
static lv_obj_t* keyedit_selected_cb    = nullptr;
static lv_obj_t* keyedit_status_label   = nullptr;
static lv_obj_t* keyedit_kb             = nullptr;
static lv_obj_t* keyedit_active_ta      = nullptr;
static int       key_edit_container_idx = -1;
static int       key_edit_key_idx       = -1;

// Container edit UI
static lv_obj_t* contedit_label_ta   = nullptr;
static lv_obj_t* contedit_agency_ta  = nullptr;
static lv_obj_t* contedit_band_ta    = nullptr;
static lv_obj_t* contedit_algo_dd    = nullptr;
static lv_obj_t* contedit_locked_cb  = nullptr;
static lv_obj_t* contedit_status     = nullptr;
static lv_obj_t* contedit_kb         = nullptr;
static int       cont_edit_idx       = -1;

// Delete container confirmation
static lv_obj_t* container_delete_mbox  = nullptr;

// ----------------------
// Layout helpers (NEW)
// ----------------------
static inline int scr_w() { return (int)lv_disp_get_hor_res(NULL); }
static inline int scr_h() { return (int)lv_disp_get_ver_res(NULL); }

static constexpr int PAD = 10;
static constexpr int TOP_BAR_H = 40;

static constexpr int BTN_H = 36;
static constexpr int BTN_W = 92;
static constexpr int BTN_GAP = 8;

static constexpr int STATUS_H = 18;

// Forward declarations
static void build_home_screen(void);
static void build_containers_screen(void);
static void build_keyload_screen(void);
static void build_settings_screen(void);
static void build_user_screen(void);

static void build_container_detail_screen(int container_index);
static void rebuild_container_keys_list(int container_index);
static void build_key_edit_screen(int container_index, int key_index);
static void build_container_edit_screen(int container_index);

static void show_home_screen(lv_event_t* e);
static void show_containers_screen(lv_event_t* e);
static void event_btn_keys(lv_event_t* e);
static void event_btn_factory_reset(lv_event_t* e);
static void event_factory_reset_confirm(lv_event_t* e);
static void event_btn_save_now(lv_event_t* e);
static void event_btn_keyload(lv_event_t* e);
static void event_btn_settings(lv_event_t* e);
static void event_btn_user_manager(lv_event_t* e);
static void event_btn_keyload_start(lv_event_t* e);

// user manager callbacks
static void event_select_admin(lv_event_t* e);
static void event_select_operator(lv_event_t* e);
static void event_keypad_digit(lv_event_t* e);
static void event_keypad_clear(lv_event_t* e);
static void event_keypad_ok(lv_event_t* e);

// containers callbacks
static void container_btn_event(lv_event_t* e);
static void event_add_container(lv_event_t* e);

// container detail callbacks
static void key_item_event(lv_event_t* e);
static void event_add_key(lv_event_t* e);
static void event_set_active_container(lv_event_t* e);
static void event_delete_container(lv_event_t* e);
static void event_delete_container_confirm(lv_event_t* e);
static void event_edit_container(lv_event_t* e);

// container edit callbacks
static void contedit_textarea_event(lv_event_t* e);
static void event_contedit_save(lv_event_t* e);
static void event_contedit_cancel(lv_event_t* e);

// key edit callbacks
static void keyedit_textarea_event(lv_event_t* e);
static void event_keyedit_gen_random(lv_event_t* e);
static void event_keyedit_save(lv_event_t* e);
static void event_keyedit_cancel(lv_event_t* e);

// keyload container dropdown callback
static void event_keyload_container_changed(lv_event_t* e);

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

    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1C3A4A), LV_STATE_PRESSED | LV_PART_MAIN);
}

static void style_moto_panel(lv_obj_t* panel) {
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x05121A), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x00C0FF), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 6, 0);
}

static void style_moto_screen(lv_obj_t* scr) {
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
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

    ContainerModel& model = ContainerModel::instance();
    const KeyContainer* kc = model.getActive();

    if (!kc) lv_label_set_text(keyload_container_label, "ACTIVE: NONE");
    else     lv_label_set_text_fmt(keyload_container_label, "ACTIVE: %s", kc->label.c_str());
}

static void rebuild_keyload_container_dropdown() {
    if (!keyload_container_dd) return;

    ContainerModel& model = ContainerModel::instance();
    const size_t count = model.getCount();

    if (count == 0) {
        lv_dropdown_set_options(keyload_container_dd, "NO CONTAINERS");
        lv_dropdown_set_selected(keyload_container_dd, 0);
        return;
    }

    std::string opts;
    opts.reserve(count * 32);

    for (size_t i = 0; i < count; ++i) {
        opts += model.get(i).label;
        if (i + 1 < count) opts += "\n";
    }

    lv_dropdown_set_options(keyload_container_dd, opts.c_str());

    int active = model.getActiveIndex();
    if (active < 0 || active >= (int)count) active = 0;
    lv_dropdown_set_selected(keyload_container_dd, (uint16_t)active);
}

// ----------------------
// HOME SCREEN
// ----------------------

static void build_home_screen(void) {
    if (home_screen) {
        lv_obj_del(home_screen);
        home_screen = nullptr;
    }

    home_screen = lv_obj_create(NULL);
    style_moto_screen(home_screen);

    // ---------- Fonts (optional; safe if enabled in lv_conf.h) ----------
    // If these fonts are not enabled, comment these out.
    extern const lv_font_t lv_font_montserrat_20;
    extern const lv_font_t lv_font_montserrat_16;

    // ---------- Top Bar ----------
    lv_obj_t* top_bar = lv_obj_create(home_screen);
    lv_obj_set_size(top_bar, scr_w(), TOP_BAR_H);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);

    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "KFD TERMINAL");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    home_user_label = lv_label_create(top_bar);
    lv_obj_set_style_text_color(home_user_label, lv_color_hex(0x90E4FF), 0);
    lv_obj_set_style_text_font(home_user_label, &lv_font_montserrat_16, 0);
    lv_obj_align(home_user_label, LV_ALIGN_RIGHT_MID, -6, 0);
    update_home_user_label();

    // ---------- Bottom Bar ----------
    lv_obj_t* bottom_bar = lv_obj_create(home_screen);
    lv_obj_set_size(bottom_bar, scr_w(), 36);
    lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_bar, 0, 0);

    status_label = lv_label_create(bottom_bar);
    lv_label_set_text(status_label, "READY - LOGIN RECOMMENDED");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x80E0FF), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(status_label, LV_ALIGN_LEFT_MID, 6, 0);

    // ---------- Layout Geometry ----------
    const int content_top = TOP_BAR_H + PAD;
    const int content_bottom = 36 + PAD; // bottom bar height + padding
    const int avail_h = scr_h() - content_top - content_bottom;

    // We want: 3 big tiles + a login row underneath them, evenly spaced.
    const int tile_w = scr_w() - (PAD * 2);
    const int tile_h = 68;          // bigger tiles
    const int gap = 14;

    const int login_h = 44;         // separate login row (bigger)
    const int gap_before_login = 16;

    // Total vertical space required
    const int block_h = (tile_h * 3) + (gap * 2) + gap_before_login + login_h;

    // Start Y so the block sits a bit lower (user asked "move buttons lower")
    // We bias it downward by adding +10 while still keeping it on screen.
    int y0 = content_top + (avail_h - block_h) / 2 + 10;
    if (y0 < content_top) y0 = content_top;

    // ---------- Tiles ----------
    lv_obj_t* btn_keys = lv_btn_create(home_screen);
    lv_obj_set_size(btn_keys, tile_w, tile_h);
    lv_obj_align(btn_keys, LV_ALIGN_TOP_MID, 0, y0);
    style_moto_tile_button(btn_keys);
    lv_obj_add_event_cb(btn_keys, event_btn_keys, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_keys = lv_label_create(btn_keys);
    lv_label_set_text(lbl_keys, "KEY CONTAINERS");
    lv_obj_set_style_text_font(lbl_keys, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_keys);

    lv_obj_t* btn_keyload = lv_btn_create(home_screen);
    lv_obj_set_size(btn_keyload, tile_w, tile_h);
    lv_obj_align(btn_keyload, LV_ALIGN_TOP_MID, 0, y0 + tile_h + gap);
    style_moto_tile_button(btn_keyload);
    lv_obj_add_event_cb(btn_keyload, event_btn_keyload, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_keyload = lv_label_create(btn_keyload);
    lv_label_set_text(lbl_keyload, "KEYLOAD TO RADIO");
    lv_obj_set_style_text_font(lbl_keyload, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_keyload);

    lv_obj_t* btn_settings = lv_btn_create(home_screen);
    lv_obj_set_size(btn_settings, tile_w, tile_h);
    lv_obj_align(btn_settings, LV_ALIGN_TOP_MID, 0, y0 + (tile_h + gap) * 2);
    style_moto_tile_button(btn_settings);
    lv_obj_add_event_cb(btn_settings, event_btn_settings, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_settings = lv_label_create(btn_settings);
    lv_label_set_text(lbl_settings, "SECURITY / SETTINGS");
    lv_obj_set_style_text_font(lbl_settings, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_settings);

    // ---------- USER / LOGIN (moved away from tiles) ----------
    lv_obj_t* btn_user = lv_btn_create(home_screen);
    lv_obj_set_size(btn_user, tile_w, login_h);
    lv_obj_align(btn_user, LV_ALIGN_TOP_MID, 0, y0 + (tile_h + gap) * 3 + gap_before_login);
    style_moto_tile_button(btn_user);
    lv_obj_add_event_cb(btn_user, event_btn_user_manager, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_user = lv_label_create(btn_user);
    lv_label_set_text(lbl_user, "USER / LOGIN");
    lv_obj_set_style_text_font(lbl_user, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_user);
}


// ----------------------
// CONTAINERS SCREEN
// ----------------------

static void container_btn_event(lv_event_t* e) {
    uintptr_t idx_val = (uintptr_t) lv_event_get_user_data(e);
    int idx = static_cast<int>(idx_val);

    ContainerModel& model = ContainerModel::instance();
    if (idx < 0 || static_cast<size_t>(idx) >= model.getCount()) return;

    model.setActiveIndex(idx);
    current_container_index = idx;

    if (status_label) {
        lv_label_set_text_fmt(status_label, "CONTAINER SELECTED: %s", model.get(idx).label.c_str());
    }

    update_keyload_container_label();
    if (keyload_container_dd) rebuild_keyload_container_dropdown();

    build_container_detail_screen(idx);
    if (container_detail_screen) lv_scr_load(container_detail_screen);
}

static void build_containers_screen(void) {
    if (containers_screen) {
        lv_obj_del(containers_screen);
        containers_screen = nullptr;
    }

    containers_screen = lv_obj_create(NULL);
    style_moto_screen(containers_screen);

    // Fonts (optional; comment out if not enabled)
    extern const lv_font_t lv_font_montserrat_20;
    extern const lv_font_t lv_font_montserrat_16;

    // Top bar
    lv_obj_t* top_bar = lv_obj_create(containers_screen);
    lv_obj_set_size(top_bar, scr_w(), TOP_BAR_H);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);

    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "CONTAINER INVENTORY");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 92, 32);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -6, 0);
    style_moto_tile_button(btn_back);
    lv_obj_add_event_cb(btn_back, show_home_screen, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " HOME");
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_back);

    // Geometry
    const int list_w = scr_w() - (PAD * 2);
    const int footer_h = 54;           // bigger "NEW CONTAINER"
    const int footer_gap = 12;

    const int list_top = TOP_BAR_H + PAD;
    const int list_h = scr_h() - list_top - PAD - footer_h - footer_gap;

    // List
    lv_obj_t* list = lv_list_create(containers_screen);
    lv_obj_set_size(list, list_w, (list_h > 60 ? list_h : 60));
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, list_top);
    style_moto_panel(list);

    // Make list items bigger and easier to tap
    lv_obj_set_style_pad_row(list, 8, LV_PART_MAIN);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_16, LV_PART_MAIN);

    // The LVGL list buttons are children; increase their min height
    // (Works well across LVGL 8.x)
    lv_obj_set_style_pad_ver(list, 8, LV_PART_ITEMS);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_16, LV_PART_ITEMS);

    ContainerModel& model = ContainerModel::instance();
    size_t count = model.getCount();

    for (size_t i = 0; i < count; ++i) {
        const KeyContainer& kc = model.get(i);
        lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_EDIT, kc.label.c_str());
        lv_obj_add_event_cb(btn, container_btn_event, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        // Per-item sizing
        lv_obj_set_height(btn, 44); // larger row
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_16, 0);
    }

    // Footer: New container button (full width, centered)
    lv_obj_t* btn_new = lv_btn_create(containers_screen);
    lv_obj_set_size(btn_new, list_w, footer_h);
    lv_obj_align(btn_new, LV_ALIGN_BOTTOM_MID, 0, -PAD);
    style_moto_tile_button(btn_new);
    lv_obj_add_event_cb(btn_new, event_add_container, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_new = lv_label_create(btn_new);
    lv_label_set_text(lbl_new, LV_SYMBOL_PLUS " NEW CONTAINER");
    lv_obj_set_style_text_font(lbl_new, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_new);
}


// ----------------------
// CONTAINER DETAIL + KEYS
// ----------------------

static void rebuild_container_keys_list(int container_index) {
    ContainerModel& model = ContainerModel::instance();
    if (container_index < 0 || static_cast<size_t>(container_index) >= model.getCount()) return;

    const KeyContainer& kc = model.get(container_index);

    if (container_keys_list) {
        lv_obj_del(container_keys_list);
        container_keys_list = nullptr;
    }

    // We size/position this list in build_container_detail_screen()
    container_keys_list = lv_list_create(container_detail_screen);
    style_moto_panel(container_keys_list);

    for (size_t i = 0; i < kc.keys.size(); ++i) {
        const KeySlot& ks = kc.keys[i];
        char line[96];
        lv_snprintf(line, sizeof(line),
                    "%02u  %s (%s)%s",
                    (unsigned)(i + 1),
                    ks.label.c_str(),
                    ks.algo.c_str(),
                    ks.selected ? " [SEL]" : "");

        lv_obj_t* btn = lv_list_add_btn(container_keys_list, LV_SYMBOL_KEY, line);
        lv_obj_add_event_cb(btn, key_item_event, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
}

static void build_container_detail_screen(int container_index) {
    ContainerModel& model = ContainerModel::instance();
    if (container_index < 0 || static_cast<size_t>(container_index) >= model.getCount()) return;

    current_container_index = container_index;
    const KeyContainer& kc = model.get(container_index);

    if (container_detail_screen) {
        lv_obj_del(container_detail_screen);
        container_detail_screen = nullptr;
        container_keys_list = nullptr;
        container_detail_status = nullptr;
    }

    // Fonts (optional; comment out if not enabled)
    extern const lv_font_t lv_font_montserrat_20;
    extern const lv_font_t lv_font_montserrat_16;

    container_detail_screen = lv_obj_create(NULL);
    style_moto_screen(container_detail_screen);

    // Top bar
    lv_obj_t* top_bar = lv_obj_create(container_detail_screen);
    lv_obj_set_size(top_bar, scr_w(), TOP_BAR_H);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);

    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "CONTAINER DETAIL");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 92, 32);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -6, 0);
    style_moto_tile_button(btn_back);
    lv_obj_add_event_cb(btn_back, show_containers_screen, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " LIST");
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_back);

    // Meta panel
    const int meta_h = 96;
    lv_obj_t* meta = lv_obj_create(container_detail_screen);
    lv_obj_set_size(meta, scr_w() - (PAD * 2), meta_h);
    lv_obj_align(meta, LV_ALIGN_TOP_MID, 0, TOP_BAR_H + PAD);
    lv_obj_clear_flag(meta, LV_OBJ_FLAG_SCROLLABLE);
    style_moto_panel(meta);

    lv_obj_t* label_line = lv_label_create(meta);
    lv_label_set_text_fmt(label_line, "%s", kc.label.c_str());
    lv_obj_set_style_text_color(label_line, lv_color_hex(0xC8F4FF), 0);
    lv_obj_set_style_text_font(label_line, &lv_font_montserrat_20, 0);
    lv_obj_align(label_line, LV_ALIGN_TOP_LEFT, 2, 2);

    lv_obj_t* agency_line = lv_label_create(meta);
    lv_label_set_text_fmt(agency_line, "Agency: %s", kc.agency.c_str());
    lv_obj_set_style_text_color(agency_line, lv_color_hex(0x80E0FF), 0);
    lv_obj_set_style_text_font(agency_line, &lv_font_montserrat_16, 0);
    lv_obj_align(agency_line, LV_ALIGN_TOP_LEFT, 2, 30);

    lv_obj_t* band_line = lv_label_create(meta);
    lv_label_set_text_fmt(band_line, "Band/Algo: %s / %s", kc.band.c_str(), kc.algo.c_str());
    lv_obj_set_style_text_color(band_line, lv_color_hex(0x80E0FF), 0);
    lv_obj_set_style_text_font(band_line, &lv_font_montserrat_16, 0);
    lv_obj_align(band_line, LV_ALIGN_TOP_LEFT, 2, 50);

    lv_obj_t* lock_line = lv_label_create(meta);
    lv_label_set_text_fmt(lock_line, "Locked: %s", kc.locked ? "YES" : "NO");
    lv_obj_set_style_text_color(lock_line, lv_color_hex(kc.locked ? 0xFF8080 : 0x80FF80), 0);
    lv_obj_set_style_text_font(lock_line, &lv_font_montserrat_16, 0);
    lv_obj_align(lock_line, LV_ALIGN_TOP_LEFT, 2, 70);

    lv_obj_t* btn_edit = lv_btn_create(meta);
    lv_obj_set_size(btn_edit, 84, 34);
    lv_obj_align(btn_edit, LV_ALIGN_RIGHT_MID, -4, 0);
    style_moto_tile_button(btn_edit);
    lv_obj_add_event_cb(btn_edit, event_edit_container, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_edit = lv_label_create(btn_edit);
    lv_label_set_text(lbl_edit, "EDIT");
    lv_obj_set_style_text_font(lbl_edit, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_edit);

    // Bottom row: larger buttons, evenly spaced
    const int bottom_pad = PAD;
    const int btn_h = 52;                         // bigger
    const int btn_gap = 10;
    const int btn_w = (scr_w() - (PAD * 2) - (btn_gap * 2)) / 3; // 3 across

    const int btn_row_y = scr_h() - bottom_pad - btn_h;
    const int status_y  = btn_row_y - STATUS_H - 8;

    lv_obj_t* btn_active = lv_btn_create(container_detail_screen);
    lv_obj_set_size(btn_active, btn_w, btn_h);
    lv_obj_align(btn_active, LV_ALIGN_TOP_LEFT, PAD, btn_row_y);
    style_moto_tile_button(btn_active);
    lv_obj_add_event_cb(btn_active, event_set_active_container, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_active = lv_label_create(btn_active);
    lv_label_set_text(lbl_active, "SET ACTIVE");
    lv_obj_set_style_text_font(lbl_active, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_active);

    lv_obj_t* btn_add_key = lv_btn_create(container_detail_screen);
    lv_obj_set_size(btn_add_key, btn_w, btn_h);
    lv_obj_align(btn_add_key, LV_ALIGN_TOP_LEFT, PAD + btn_w + btn_gap, btn_row_y);
    style_moto_tile_button(btn_add_key);
    lv_obj_add_event_cb(btn_add_key, event_add_key, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_add_key = lv_label_create(btn_add_key);
    lv_label_set_text(lbl_add_key, LV_SYMBOL_KEY " ADD KEY");
    lv_obj_set_style_text_font(lbl_add_key, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_add_key);

    lv_obj_t* btn_del = lv_btn_create(container_detail_screen);
    lv_obj_set_size(btn_del, btn_w, btn_h);
    lv_obj_align(btn_del, LV_ALIGN_TOP_RIGHT, -PAD, btn_row_y);
    style_moto_tile_button(btn_del);
    lv_obj_add_event_cb(btn_del, event_delete_container, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_del = lv_label_create(btn_del);
    lv_label_set_text(lbl_del, "DELETE");
    lv_obj_set_style_text_font(lbl_del, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_del);

    // Status line above buttons
    container_detail_status = lv_label_create(container_detail_screen);
    lv_label_set_text(container_detail_status, "CONTAINER READY");
    lv_obj_set_style_text_color(container_detail_status, lv_color_hex(0x80E0FF), 0);
    lv_obj_set_style_text_font(container_detail_status, &lv_font_montserrat_16, 0);
    lv_obj_align(container_detail_status, LV_ALIGN_TOP_LEFT, PAD, status_y);

    // Keys list fills the remaining space
    rebuild_container_keys_list(container_index);

    const int list_top = TOP_BAR_H + PAD + meta_h + PAD;
    const int list_bottom = status_y - 10;
    int list_h = list_bottom - list_top;
    if (list_h < 60) list_h = 60;

    lv_obj_set_size(container_keys_list, scr_w() - (PAD * 2), list_h);
    lv_obj_align(container_keys_list, LV_ALIGN_TOP_MID, 0, list_top);

    // Make key list items larger
    lv_obj_set_style_pad_row(container_keys_list, 8, LV_PART_MAIN);
    lv_obj_set_style_text_font(container_keys_list, &lv_font_montserrat_16, LV_PART_MAIN);
}


// --- container detail events ---

static void show_containers_screen(lv_event_t* e) {
    (void)e;
    build_containers_screen();
    if (containers_screen) lv_scr_load(containers_screen);
}

static void key_item_event(lv_event_t* e) {
    if (current_container_index < 0) return;
    uintptr_t idx_val = (uintptr_t) lv_event_get_user_data(e);
    int key_idx = static_cast<int>(idx_val);

    build_key_edit_screen(current_container_index, key_idx);
    if (key_edit_screen) lv_scr_load(key_edit_screen);
}

static void event_add_key(lv_event_t* e) {
    (void)e;
    if (!check_access(false, "ADD KEY")) return;
    if (current_container_index < 0) return;

    build_key_edit_screen(current_container_index, -1);
    if (key_edit_screen) lv_scr_load(key_edit_screen);
}

static void event_set_active_container(lv_event_t* e) {
    (void)e;
    if (current_container_index < 0) return;

    ContainerModel& model = ContainerModel::instance();
    model.setActiveIndex(current_container_index);

    if (container_detail_status) lv_label_set_text(container_detail_status, "ACTIVE CONTAINER SET");
    update_keyload_container_label();
    if (keyload_container_dd) rebuild_keyload_container_dropdown();
}

static void event_btn_save_now(lv_event_t* e) {
    (void)e;
    ContainerModel& model = ContainerModel::instance();
    if (!model.saveNow()) {
        if (status_label) lv_label_set_text(status_label, "SAVE FAILED (LittleFS)");
        return;
    }
    if (status_label) lv_label_set_text(status_label, "CONTAINERS SAVED");
}

static void event_btn_factory_reset(lv_event_t* e) {
    (void)e;
    if (!check_access(true, "FACTORY RESET")) return;

    if (factory_reset_mbox) {
        lv_obj_del(factory_reset_mbox);
        factory_reset_mbox = nullptr;
    }

    static const char* btns[] = { "ERASE ALL", "CANCEL", nullptr };

    factory_reset_mbox = lv_msgbox_create(
        NULL,
        "FACTORY RESET",
        "This will ERASE all containers/keys stored internally.\n"
        "You cannot undo this.\n\nProceed?",
        btns,
        false
    );
    lv_obj_center(factory_reset_mbox);
    lv_obj_add_event_cb(factory_reset_mbox, event_factory_reset_confirm, LV_EVENT_VALUE_CHANGED, NULL);
}

static void event_factory_reset_confirm(lv_event_t* e) {
    (void)e;
    if (!factory_reset_mbox) return;

    const char* btn_txt = lv_msgbox_get_active_btn_text(factory_reset_mbox);
    lv_obj_del(factory_reset_mbox);
    factory_reset_mbox = nullptr;

    if (!btn_txt) return;

    if (strcmp(btn_txt, "ERASE ALL") != 0) {
        if (status_label) lv_label_set_text(status_label, "FACTORY RESET CANCELED");
        return;
    }

    ContainerModel& model = ContainerModel::instance();
    if (!model.factoryReset()) {
        if (status_label) lv_label_set_text(status_label, "FACTORY RESET FAILED");
        return;
    }

    if (status_label) lv_label_set_text(status_label, "FACTORY RESET COMPLETE");
}

static void event_delete_container_confirm(lv_event_t* e) {
    (void)e;
    if (!container_delete_mbox) return;

    const char* btn_txt = lv_msgbox_get_active_btn_text(container_delete_mbox);
    bool delete_it = (btn_txt && strcmp(btn_txt, "DELETE") == 0);

    lv_obj_del(container_delete_mbox);
    container_delete_mbox = nullptr;

    if (!delete_it) {
        if (container_detail_status) lv_label_set_text(container_detail_status, "DELETE CANCELED");
        return;
    }

    if (current_container_index < 0) return;

    ContainerModel& model = ContainerModel::instance();
    model.removeContainer(current_container_index);
    current_container_index = -1;

    if (status_label) lv_label_set_text(status_label, "CONTAINER DELETED");

    build_containers_screen();
    if (containers_screen) lv_scr_load(containers_screen);
}

static void event_delete_container(lv_event_t* e) {
    (void)e;

    if (!check_access(true, "DELETE CONTAINER")) return;
    if (current_container_index < 0) return;

    ContainerModel& model = ContainerModel::instance();
    if ((size_t)current_container_index >= model.getCount()) return;
    const KeyContainer& kc = model.get(current_container_index);

    if (kc.keys.empty()) {
        model.removeContainer(current_container_index);
        current_container_index = -1;

        if (status_label) lv_label_set_text(status_label, "CONTAINER DELETED");
        build_containers_screen();
        if (containers_screen) lv_scr_load(containers_screen);
        return;
    }

    static const char* btns[] = { "DELETE", "CANCEL", nullptr };
    container_delete_mbox = lv_msgbox_create(
        NULL,
        "CONFIRM",
        "Container contains keys.\nDelete container and all keys?",
        btns,
        false
    );
    lv_obj_center(container_delete_mbox);
    lv_obj_add_event_cb(container_delete_mbox, event_delete_container_confirm, LV_EVENT_VALUE_CHANGED, NULL);
}

// ----------------------
// CONTAINER EDIT SCREEN
// ----------------------

static void contedit_textarea_event(lv_event_t* e) {
    lv_obj_t* ta = lv_event_get_target(e);
    if (contedit_kb) lv_keyboard_set_textarea(contedit_kb, ta);
}

static void event_contedit_cancel(lv_event_t* e) {
    (void)e;
    if (cont_edit_idx >= 0) {
        build_container_detail_screen(cont_edit_idx);
        if (container_detail_screen) lv_scr_load(container_detail_screen);
    } else {
        show_containers_screen(nullptr);
    }
}

static uint16_t cont_algo_to_index(const std::string& algo) {
    if (algo == "AES256")   return 0;
    if (algo == "AES128")   return 1;
    if (algo == "DES-OFB")  return 2;
    if (algo == "ADP")      return 3;
    return 4;
}

static std::string cont_index_to_algo(uint16_t idx) {
    switch (idx) {
        case 0: return "AES256";
        case 1: return "AES128";
        case 2: return "DES-OFB";
        case 3: return "ADP";
        default: return "Other";
    }
}

static void event_contedit_save(lv_event_t* e) {
    (void)e;

    if (!check_access(true, "EDIT CONTAINER")) return;
    if (cont_edit_idx < 0) return;

    ContainerModel& model = ContainerModel::instance();
    if ((size_t)cont_edit_idx >= model.getCount()) return;

    KeyContainer kc = model.get(cont_edit_idx);

    const char* label_txt  = contedit_label_ta  ? lv_textarea_get_text(contedit_label_ta)  : "";
    const char* agency_txt = contedit_agency_ta ? lv_textarea_get_text(contedit_agency_ta) : "";
    const char* band_txt   = contedit_band_ta   ? lv_textarea_get_text(contedit_band_ta)   : "";

    if (!label_txt || strlen(label_txt) == 0) {
        if (contedit_status) lv_label_set_text(contedit_status, "LABEL REQUIRED");
        return;
    }

    uint16_t aidx = 4;
    if (contedit_algo_dd) aidx = lv_dropdown_get_selected(contedit_algo_dd);

    kc.label  = label_txt;
    kc.agency = agency_txt ? agency_txt : "";
    kc.band   = band_txt ? band_txt : "";
    kc.algo   = cont_index_to_algo(aidx);
    kc.locked = contedit_locked_cb && lv_obj_has_state(contedit_locked_cb, LV_STATE_CHECKED);

    if (!model.updateContainer((size_t)cont_edit_idx, kc)) {
        if (contedit_status) lv_label_set_text(contedit_status, "SAVE FAILED");
        return;
    }

    update_keyload_container_label();
    if (keyload_container_dd) rebuild_keyload_container_dropdown();

    build_container_detail_screen(cont_edit_idx);
    if (container_detail_screen) lv_scr_load(container_detail_screen);
}

static void build_container_edit_screen(int container_index) {
    ContainerModel& model = ContainerModel::instance();
    if (container_index < 0 || (size_t)container_index >= model.getCount()) return;

    cont_edit_idx = container_index;
    const KeyContainer& kc = model.get(container_index);

    if (container_edit_screen) {
        lv_obj_del(container_edit_screen);
        container_edit_screen = nullptr;
        contedit_label_ta = nullptr;
        contedit_agency_ta = nullptr;
        contedit_band_ta = nullptr;
        contedit_algo_dd = nullptr;
        contedit_locked_cb = nullptr;
        contedit_status = nullptr;
        contedit_kb = nullptr;
    }

    container_edit_screen = lv_obj_create(NULL);
    style_moto_screen(container_edit_screen);

    lv_obj_t* top_bar = lv_obj_create(container_edit_screen);
    lv_obj_set_size(top_bar, scr_w(), TOP_BAR_H);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);

    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "EDIT CONTAINER");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 90, 30);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -4, 0);
    style_moto_tile_button(btn_back);
    lv_obj_add_event_cb(btn_back, event_contedit_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " BACK");
    lv_obj_center(lbl_back);

    // Form panel
    lv_obj_t* form = lv_obj_create(container_edit_screen);
    lv_obj_set_size(form, scr_w() - (PAD * 2), 240);
    lv_obj_align(form, LV_ALIGN_TOP_MID, 0, TOP_BAR_H + PAD);
    lv_obj_clear_flag(form, LV_OBJ_FLAG_SCROLLABLE);
    style_moto_panel(form);

    // Labels & fields
    lv_obj_t* lbl1 = lv_label_create(form);
    lv_label_set_text(lbl1, "Label:");
    lv_obj_set_style_text_color(lbl1, lv_color_hex(0x80E0FF), 0);
    lv_obj_align(lbl1, LV_ALIGN_TOP_LEFT, 2, 2);

    contedit_label_ta = lv_textarea_create(form);
    lv_obj_set_size(contedit_label_ta, 210, 30);
    lv_obj_align(contedit_label_ta, LV_ALIGN_TOP_LEFT, 70, 0);
    lv_textarea_set_max_length(contedit_label_ta, 48);
    lv_obj_add_event_cb(contedit_label_ta, contedit_textarea_event, LV_EVENT_FOCUSED, NULL);

    lv_obj_t* lbl2 = lv_label_create(form);
    lv_label_set_text(lbl2, "Agency:");
    lv_obj_set_style_text_color(lbl2, lv_color_hex(0x80E0FF), 0);
    lv_obj_align(lbl2, LV_ALIGN_TOP_LEFT, 2, 42);

    contedit_agency_ta = lv_textarea_create(form);
    lv_obj_set_size(contedit_agency_ta, 210, 30);
    lv_obj_align(contedit_agency_ta, LV_ALIGN_TOP_LEFT, 70, 40);
    lv_textarea_set_max_length(contedit_agency_ta, 48);
    lv_obj_add_event_cb(contedit_agency_ta, contedit_textarea_event, LV_EVENT_FOCUSED, NULL);

    lv_obj_t* lbl3 = lv_label_create(form);
    lv_label_set_text(lbl3, "Band:");
    lv_obj_set_style_text_color(lbl3, lv_color_hex(0x80E0FF), 0);
    lv_obj_align(lbl3, LV_ALIGN_TOP_LEFT, 2, 82);

    contedit_band_ta = lv_textarea_create(form);
    lv_obj_set_size(contedit_band_ta, 210, 30);
    lv_obj_align(contedit_band_ta, LV_ALIGN_TOP_LEFT, 70, 80);
    lv_textarea_set_max_length(contedit_band_ta, 32);
    lv_obj_add_event_cb(contedit_band_ta, contedit_textarea_event, LV_EVENT_FOCUSED, NULL);

    lv_obj_t* lbl4 = lv_label_create(form);
    lv_label_set_text(lbl4, "Algo:");
    lv_obj_set_style_text_color(lbl4, lv_color_hex(0x80E0FF), 0);
    lv_obj_align(lbl4, LV_ALIGN_TOP_LEFT, 2, 122);

    contedit_algo_dd = lv_dropdown_create(form);
    lv_dropdown_set_options(contedit_algo_dd, "AES256\nAES128\nDES-OFB\nADP\nOther");
    lv_obj_set_width(contedit_algo_dd, 140);
    lv_obj_align(contedit_algo_dd, LV_ALIGN_TOP_LEFT, 70, 118);

    contedit_locked_cb = lv_checkbox_create(form);
    lv_checkbox_set_text(contedit_locked_cb, "Locked (prevent edits)");
    lv_obj_set_style_text_color(contedit_locked_cb, lv_color_hex(0xC8F4FF), 0);
    lv_obj_align(contedit_locked_cb, LV_ALIGN_TOP_LEFT, 2, 160);

    contedit_status = lv_label_create(form);
    lv_label_set_text(contedit_status, "");
    lv_obj_set_style_text_color(contedit_status, lv_color_hex(0xFFD0A0), 0);
    lv_obj_align(contedit_status, LV_ALIGN_TOP_LEFT, 2, 192);

    // Buttons + keyboard reserve
    const int kb_h = 90;
    const int btn_y = scr_h() - kb_h - PAD - 44;

    lv_obj_t* btn_save = lv_btn_create(container_edit_screen);
    lv_obj_set_size(btn_save, (scr_w() - (PAD * 3)) / 2, 44);
    lv_obj_align(btn_save, LV_ALIGN_TOP_LEFT, PAD, btn_y);
    style_moto_tile_button(btn_save);
    lv_obj_add_event_cb(btn_save, event_contedit_save, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "SAVE");
    lv_obj_center(lbl_save);

    lv_obj_t* btn_cancel = lv_btn_create(container_edit_screen);
    lv_obj_set_size(btn_cancel, (scr_w() - (PAD * 3)) / 2, 44);
    lv_obj_align(btn_cancel, LV_ALIGN_TOP_RIGHT, -PAD, btn_y);
    style_moto_tile_button(btn_cancel);
    lv_obj_add_event_cb(btn_cancel, event_contedit_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "CANCEL");
    lv_obj_center(lbl_cancel);

    contedit_kb = lv_keyboard_create(container_edit_screen);
    lv_obj_set_size(contedit_kb, scr_w(), kb_h);
    lv_obj_align(contedit_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(contedit_kb, contedit_label_ta);

    // Populate
    lv_textarea_set_text(contedit_label_ta, kc.label.c_str());
    lv_textarea_set_text(contedit_agency_ta, kc.agency.c_str());
    lv_textarea_set_text(contedit_band_ta, kc.band.c_str());
    lv_dropdown_set_selected(contedit_algo_dd, cont_algo_to_index(kc.algo));
    if (kc.locked) lv_obj_add_state(contedit_locked_cb, LV_STATE_CHECKED);
    else           lv_obj_clear_state(contedit_locked_cb, LV_STATE_CHECKED);
}

static void event_edit_container(lv_event_t* e) {
    (void)e;
    if (!check_access(true, "EDIT CONTAINER")) return;
    if (current_container_index < 0) return;

    build_container_edit_screen(current_container_index);
    if (container_edit_screen) lv_scr_load(container_edit_screen);
}

// ----------------------
// KEY EDIT SCREEN
// ----------------------

static void keyedit_textarea_event(lv_event_t* e) {
    lv_obj_t* ta = lv_event_get_target(e);
    keyedit_active_ta = ta;
    if (keyedit_kb) lv_keyboard_set_textarea(keyedit_kb, ta);
}

static void event_keyedit_gen_random(lv_event_t* e) {
    (void)e;
    if (!keyedit_key_ta || !keyedit_algo_dd) return;

    char algo[32] = {0};
    lv_dropdown_get_selected_str(keyedit_algo_dd, algo, sizeof(algo));

    size_t key_bytes = 16;
    if (strstr(algo, "256") != nullptr) key_bytes = 32;

    std::string hex;
    hex.reserve(key_bytes * 2);

    for (size_t i = 0; i < key_bytes; ++i) {
        uint8_t b = (uint8_t) esp_random();
        char tmp[3];
        snprintf(tmp, sizeof(tmp), "%02X", b);
        hex += tmp;
    }

    lv_textarea_set_text(keyedit_key_ta, hex.c_str());
    if (keyedit_status_label) lv_label_set_text(keyedit_status_label, "RANDOM KEY GENERATED");
}

static void event_keyedit_cancel(lv_event_t* e) {
    (void)e;
    if (current_container_index >= 0) {
        build_container_detail_screen(current_container_index);
        if (container_detail_screen) lv_scr_load(container_detail_screen);
    } else {
        show_containers_screen(nullptr);
    }
}

static void event_keyedit_save(lv_event_t* e) {
    (void)e;
    if (key_edit_container_idx < 0) return;

    ContainerModel& model = ContainerModel::instance();
    if ((size_t)key_edit_container_idx >= model.getCount()) return;

    KeyContainer& kc = model.getMutable(key_edit_container_idx);

    if (kc.locked && current_role != ROLE_ADMIN) {
        if (keyedit_status_label) lv_label_set_text(keyedit_status_label, "CONTAINER LOCKED (ADMIN ONLY)");
        return;
    }

    const char* label_txt = keyedit_label_ta ? lv_textarea_get_text(keyedit_label_ta) : "";
    const char* hex_txt   = keyedit_key_ta   ? lv_textarea_get_text(keyedit_key_ta)   : "";

    char algo[32] = {0};
    if (keyedit_algo_dd) lv_dropdown_get_selected_str(keyedit_algo_dd, algo, sizeof(algo));

    bool selected = keyedit_selected_cb && lv_obj_has_state(keyedit_selected_cb, LV_STATE_CHECKED);

    if (!label_txt || strlen(label_txt) == 0) {
        if (keyedit_status_label) lv_label_set_text(keyedit_status_label, "LABEL REQUIRED");
        return;
    }

    if (!hex_txt || strlen(hex_txt) < 2) {
        if (keyedit_status_label) lv_label_set_text(keyedit_status_label, "KEY HEX REQUIRED");
        return;
    }

    std::string clean_hex;
    clean_hex.reserve(strlen(hex_txt));
    for (const char* p = hex_txt; *p; ++p) {
        char c = *p;
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c >= 'a' && c <= 'f') c = c - 'a' + 'A';
        clean_hex.push_back(c);
    }

    if (clean_hex.size() % 2 != 0) {
        if (keyedit_status_label) lv_label_set_text(keyedit_status_label, "HEX LENGTH MUST BE EVEN");
        return;
    }

    KeySlot slot;
    slot.label    = label_txt;
    slot.algo     = algo;
    slot.hex      = clean_hex;
    slot.selected = selected;

    if (key_edit_key_idx >= 0 && (size_t)key_edit_key_idx < kc.keys.size()) {
        model.updateKey(key_edit_container_idx, key_edit_key_idx, slot);
    } else {
        model.addKey(key_edit_container_idx, slot);
    }

    build_container_detail_screen(key_edit_container_idx);
    if (container_detail_screen) lv_scr_load(container_detail_screen);
}

static void build_key_edit_screen(int container_index, int key_index) {
    // (kept as-is from your working version; layout already stable due to keyboard docking)
    // If you want, I can apply the same “computed layout” pattern here too.
    ContainerModel& model = ContainerModel::instance();
    if (container_index < 0 || (size_t)container_index >= model.getCount()) return;

    key_edit_container_idx = container_index;
    key_edit_key_idx       = key_index;

    const KeyContainer& kc = model.get(container_index);
    const KeySlot* ks = nullptr;
    if (key_index >= 0 && (size_t)key_index < kc.keys.size()) ks = &kc.keys[key_index];

    if (key_edit_screen) {
        lv_obj_del(key_edit_screen);
        key_edit_screen = nullptr;
        keyedit_label_ta = nullptr;
        keyedit_algo_dd = nullptr;
        keyedit_key_ta = nullptr;
        keyedit_selected_cb = nullptr;
        keyedit_status_label = nullptr;
        keyedit_kb = nullptr;
        keyedit_active_ta = nullptr;
    }

    key_edit_screen = lv_obj_create(NULL);
    style_moto_screen(key_edit_screen);

    lv_obj_t* top_bar = lv_obj_create(key_edit_screen);
    lv_obj_set_size(top_bar, scr_w(), TOP_BAR_H);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);

    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, ks ? "EDIT KEY" : "ADD KEY");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 90, 30);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -4, 0);
    style_moto_tile_button(btn_back);
    lv_obj_add_event_cb(btn_back, event_keyedit_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " BACK");
    lv_obj_center(lbl_back);

    lv_obj_t* cont_lbl = lv_label_create(key_edit_screen);
    lv_label_set_text_fmt(cont_lbl, "CONTAINER: %s", kc.label.c_str());
    lv_obj_set_style_text_color(cont_lbl, lv_color_hex(0xC8F4FF), 0);
    lv_obj_align(cont_lbl, LV_ALIGN_TOP_LEFT, PAD, TOP_BAR_H + 8);

    lv_obj_t* lbl_label = lv_label_create(key_edit_screen);
    lv_label_set_text(lbl_label, "Key Label:");
    lv_obj_set_style_text_color(lbl_label, lv_color_hex(0x80E0FF), 0);
    lv_obj_align(lbl_label, LV_ALIGN_TOP_LEFT, PAD, TOP_BAR_H + 34);

    keyedit_label_ta = lv_textarea_create(key_edit_screen);
    lv_obj_set_size(keyedit_label_ta, scr_w() - 120, 30);
    lv_obj_align(keyedit_label_ta, LV_ALIGN_TOP_LEFT, PAD + 90, TOP_BAR_H + 28);
    lv_textarea_set_max_length(keyedit_label_ta, 32);
    lv_obj_add_event_cb(keyedit_label_ta, keyedit_textarea_event, LV_EVENT_FOCUSED, NULL);

    lv_obj_t* lbl_algo = lv_label_create(key_edit_screen);
    lv_label_set_text(lbl_algo, "Algo:");
    lv_obj_set_style_text_color(lbl_algo, lv_color_hex(0x80E0FF), 0);
    lv_obj_align(lbl_algo, LV_ALIGN_TOP_LEFT, PAD, TOP_BAR_H + 70);

    keyedit_algo_dd = lv_dropdown_create(key_edit_screen);
    lv_dropdown_set_options(keyedit_algo_dd, "AES256\nAES128\nDES-OFB\nADP\nOther");
    lv_obj_set_width(keyedit_algo_dd, 140);
    lv_obj_align(keyedit_algo_dd, LV_ALIGN_TOP_LEFT, PAD + 90, TOP_BAR_H + 64);

    lv_obj_t* lbl_key = lv_label_create(key_edit_screen);
    lv_label_set_text(lbl_key, "Key (HEX):");
    lv_obj_set_style_text_color(lbl_key, lv_color_hex(0x80E0FF), 0);
    lv_obj_align(lbl_key, LV_ALIGN_TOP_LEFT, PAD, TOP_BAR_H + 110);

    keyedit_key_ta = lv_textarea_create(key_edit_screen);
    lv_obj_set_size(keyedit_key_ta, scr_w() - (PAD * 2), 80);
    lv_obj_align(keyedit_key_ta, LV_ALIGN_TOP_MID, 0, TOP_BAR_H + 130);
    lv_textarea_set_max_length(keyedit_key_ta, 128);
    lv_textarea_set_one_line(keyedit_key_ta, false);
    lv_obj_add_event_cb(keyedit_key_ta, keyedit_textarea_event, LV_EVENT_FOCUSED, NULL);

    keyedit_selected_cb = lv_checkbox_create(key_edit_screen);
    lv_checkbox_set_text(keyedit_selected_cb, "Selected for keyload");
    lv_obj_set_style_text_color(keyedit_selected_cb, lv_color_hex(0xC8F4FF), 0);
    lv_obj_align(keyedit_selected_cb, LV_ALIGN_TOP_LEFT, PAD, TOP_BAR_H + 220);

    keyedit_status_label = lv_label_create(key_edit_screen);
    lv_label_set_text(keyedit_status_label, "");
    lv_obj_set_style_text_color(keyedit_status_label, lv_color_hex(0xFFD0A0), 0);
    lv_obj_align(keyedit_status_label, LV_ALIGN_TOP_LEFT, PAD, TOP_BAR_H + 246);

    lv_obj_t* btn_rand = lv_btn_create(key_edit_screen);
    lv_obj_set_size(btn_rand, 90, 35);
    lv_obj_align(btn_rand, LV_ALIGN_BOTTOM_LEFT, PAD, -90);
    style_moto_tile_button(btn_rand);
    lv_obj_add_event_cb(btn_rand, event_keyedit_gen_random, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_rand = lv_label_create(btn_rand);
    lv_label_set_text(lbl_rand, "RAND");
    lv_obj_center(lbl_rand);

    lv_obj_t* btn_save = lv_btn_create(key_edit_screen);
    lv_obj_set_size(btn_save, 90, 35);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_MID, 0, -90);
    style_moto_tile_button(btn_save);
    lv_obj_add_event_cb(btn_save, event_keyedit_save, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "SAVE");
    lv_obj_center(lbl_save);

    lv_obj_t* btn_cancel = lv_btn_create(key_edit_screen);
    lv_obj_set_size(btn_cancel, 90, 35);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -PAD, -90);
    style_moto_tile_button(btn_cancel);
    lv_obj_add_event_cb(btn_cancel, event_keyedit_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "CANCEL");
    lv_obj_center(lbl_cancel);

    // Keyboard docked bottom
    const int kb_h = 90;
    keyedit_kb = lv_keyboard_create(key_edit_screen);
    lv_obj_set_size(keyedit_kb, scr_w(), kb_h);
    lv_obj_align(keyedit_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyedit_kb, keyedit_label_ta);

    auto algo_to_index = [](const std::string& algo) -> uint16_t {
        if (algo == "AES256")   return 0;
        if (algo == "AES128")   return 1;
        if (algo == "DES-OFB")  return 2;
        if (algo == "ADP")      return 3;
        if (algo == "Other")    return 4;
        return 0;
    };

    if (ks) {
        lv_textarea_set_text(keyedit_label_ta, ks->label.c_str());
        lv_textarea_set_text(keyedit_key_ta, ks->hex.c_str());
        if (ks->selected) lv_obj_add_state(keyedit_selected_cb, LV_STATE_CHECKED);
        else              lv_obj_clear_state(keyedit_selected_cb, LV_STATE_CHECKED);
        lv_dropdown_set_selected(keyedit_algo_dd, algo_to_index(ks->algo));
    } else {
        lv_dropdown_set_selected(keyedit_algo_dd, algo_to_index(kc.algo));
        lv_textarea_set_text(keyedit_label_ta, "");
        lv_textarea_set_text(keyedit_key_ta, "");
        lv_obj_add_state(keyedit_selected_cb, LV_STATE_CHECKED);
    }
}

// ----------------------
// ADD CONTAINER
// ----------------------

static void event_add_container(lv_event_t* e) {
    (void)e;
    if (!check_access(true, "ADD CONTAINER")) return;

    ContainerModel& model = ContainerModel::instance();

    KeyContainer kc;
    kc.label  = "NEW CONTAINER";
    kc.agency = "AGENCY";
    kc.band   = "BAND";
    kc.algo   = "AES256";
    kc.locked = false;

    int idx = model.addContainer(kc);
    if (idx < 0) {
        if (status_label) lv_label_set_text(status_label, "FAILED TO ADD CONTAINER");
        return;
    }

    current_container_index = idx;

    // Immediately edit (clean flow)
    build_container_edit_screen(idx);
    if (container_edit_screen) lv_scr_load(container_edit_screen);

    update_keyload_container_label();
    if (keyload_container_dd) rebuild_keyload_container_dropdown();
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
        if (keyload_status) lv_label_set_text(keyload_status, "KEYLOAD COMPLETE - VERIFY RADIO");
        if (status_label) lv_label_set_text(status_label, "KEYLOAD COMPLETE");
    }
    if (keyload_bar) lv_bar_set_value(keyload_bar, keyload_progress, LV_ANIM_ON);
}

static void event_keyload_container_changed(lv_event_t* e) {
    (void)e;
    if (!keyload_container_dd) return;

    ContainerModel& model = ContainerModel::instance();
    size_t count = model.getCount();
    if (count == 0) return;

    uint16_t sel = lv_dropdown_get_selected(keyload_container_dd);
    if ((size_t)sel >= count) sel = 0;

    model.setActiveIndex((int)sel);

    update_keyload_container_label();
    if (status_label) {
        const KeyContainer* kc = model.getActive();
        if (kc) lv_label_set_text_fmt(status_label, "ACTIVE SET: %s", kc->label.c_str());
    }
}

static void event_btn_keyload_start(lv_event_t* e) {
    (void)e;

    if (!check_access(false, "KEYLOAD START")) return;

    ContainerModel& model = ContainerModel::instance();
    const KeyContainer* kc = model.getActive();
    if (!kc) {
        if (keyload_status) lv_label_set_text(keyload_status, "NO ACTIVE CONTAINER");
        if (status_label) lv_label_set_text(status_label, "SELECT CONTAINER FIRST");
        return;
    }

    keyload_progress = 0;
    if (keyload_bar) lv_bar_set_value(keyload_bar, 0, LV_ANIM_OFF);

    if (keyload_status) lv_label_set_text_fmt(keyload_status, "KEYLOAD: %s", kc->label.c_str());

    if (!keyload_timer) keyload_timer = lv_timer_create(keyload_timer_cb, 200, NULL);
}

static void build_keyload_screen(void) {
    if (keyload_screen) {
        lv_obj_del(keyload_screen);
        keyload_screen = nullptr;
        keyload_container_dd = nullptr;
        keyload_container_label = nullptr;
        keyload_status = nullptr;
        keyload_bar = nullptr;
    }

    // Fonts (optional; comment out if not enabled)
    extern const lv_font_t lv_font_montserrat_20;
    extern const lv_font_t lv_font_montserrat_16;

    keyload_screen = lv_obj_create(NULL);
    style_moto_screen(keyload_screen);

    // Top bar
    lv_obj_t* top_bar = lv_obj_create(keyload_screen);
    lv_obj_set_size(top_bar, scr_w(), TOP_BAR_H);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);

    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "KEYLOAD CONSOLE");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 92, 32);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -6, 0);
    style_moto_tile_button(btn_back);
    lv_obj_add_event_cb(btn_back, show_home_screen, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " HOME");
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_back);

    // Geometry (make space for a larger start button)
    const int panel_w = scr_w() - (PAD * 2);
    const int start_h = 64;          // bigger start button
    const int start_gap = 14;

    const int bottom_reserved = PAD + start_h + start_gap;
    const int top_content = TOP_BAR_H + PAD;

    // Panel height chosen to keep UI balanced
    const int panel_h = 186;

    // Panel
    lv_obj_t* panel = lv_obj_create(keyload_screen);
    lv_obj_set_size(panel, panel_w, panel_h);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, top_content);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    style_moto_panel(panel);

    lv_obj_t* info = lv_label_create(panel);
    lv_label_set_text(info,
        "CONNECT RADIO VIA KVL CABLE\n"
        "LINK: STANDBY\n"
        "MODE: APX / P25"
    );
    lv_obj_set_style_text_color(info, lv_color_hex(0xC8F4FF), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_16, 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 2, 2);

    // Container row (label + dropdown) with clean spacing
    lv_obj_t* dd_lbl = lv_label_create(panel);
    lv_label_set_text(dd_lbl, "Container:");
    lv_obj_set_style_text_color(dd_lbl, lv_color_hex(0x80E0FF), 0);
    lv_obj_set_style_text_font(dd_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(dd_lbl, LV_ALIGN_TOP_LEFT, 2, 86);

    keyload_container_dd = lv_dropdown_create(panel);
    lv_obj_set_size(keyload_container_dd, panel_w - 110, 34);
    lv_obj_align(keyload_container_dd, LV_ALIGN_TOP_LEFT, 98, 80);
    lv_obj_add_event_cb(keyload_container_dd, event_keyload_container_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // Active container line (below dropdown)
    keyload_container_label = lv_label_create(panel);
    lv_obj_set_style_text_color(keyload_container_label, lv_color_hex(0x80E0FF), 0);
    lv_obj_set_style_text_font(keyload_container_label, &lv_font_montserrat_16, 0);
    lv_obj_align(keyload_container_label, LV_ALIGN_TOP_LEFT, 2, 128);

    rebuild_keyload_container_dropdown();
    update_keyload_container_label();

    // Middle area (bar + status) centered and spaced
    const int after_panel_y = top_content + panel_h + 16;

    keyload_bar = lv_bar_create(keyload_screen);
    lv_obj_set_size(keyload_bar, panel_w, 22);
    lv_obj_align(keyload_bar, LV_ALIGN_TOP_MID, 0, after_panel_y);
    lv_bar_set_range(keyload_bar, 0, 100);
    lv_bar_set_value(keyload_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(keyload_bar, lv_color_hex(0x1A2630), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(keyload_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyload_bar, lv_color_hex(0x00C0FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(keyload_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    keyload_status = lv_label_create(keyload_screen);
    lv_label_set_text(keyload_status, "IDLE - READY");
    lv_obj_set_style_text_color(keyload_status, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(keyload_status, &lv_font_montserrat_16, 0);
    lv_obj_align(keyload_status, LV_ALIGN_TOP_MID, 0, after_panel_y + 32);

    // Start button (bigger and with clear margin)
    lv_obj_t* btn_start = lv_btn_create(keyload_screen);
    lv_obj_set_size(btn_start, panel_w, start_h);
    lv_obj_align(btn_start, LV_ALIGN_BOTTOM_MID, 0, -PAD);
    style_moto_tile_button(btn_start);
    lv_obj_add_event_cb(btn_start, event_btn_keyload_start, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_start = lv_label_create(btn_start);
    lv_label_set_text(lbl_start, LV_SYMBOL_PLAY "  START LOAD");
    lv_obj_set_style_text_font(lbl_start, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_start);
}


// ----------------------
// SETTINGS + USER screens
// ----------------------
// (left as in your working version; can be cleaned similarly if desired)

static void build_settings_screen(void) {
    if (settings_screen) { lv_obj_del(settings_screen); settings_screen = nullptr; }
    settings_screen = lv_obj_create(NULL);
    style_moto_screen(settings_screen);

    lv_obj_t* top_bar = lv_obj_create(settings_screen);
    lv_obj_set_size(top_bar, scr_w(), TOP_BAR_H);
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
    lv_obj_set_size(btn_back, 90, 30);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -4, 0);
    style_moto_tile_button(btn_back);
    lv_obj_add_event_cb(btn_back, show_home_screen, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " HOME");
    lv_obj_center(lbl_back);

    lv_obj_t* cb_confirm = lv_checkbox_create(settings_screen);
    lv_checkbox_set_text(cb_confirm, "Require PIN before keyload");
    lv_obj_set_style_text_color(cb_confirm, lv_color_hex(0xC8F4FF), 0);
    lv_obj_align(cb_confirm, LV_ALIGN_TOP_LEFT, PAD, TOP_BAR_H + 20);

    lv_obj_t* cb_wipe = lv_checkbox_create(settings_screen);
    lv_checkbox_set_text(cb_wipe, "Wipe containers after 10 failed PINs");
    lv_obj_set_style_text_color(cb_wipe, lv_color_hex(0xFFD0A0), 0);
    lv_obj_align(cb_wipe, LV_ALIGN_TOP_LEFT, PAD, TOP_BAR_H + 60);

    lv_obj_t* cb_audit = lv_checkbox_create(settings_screen);
    lv_checkbox_set_text(cb_audit, "Enable audit log to SD");
    lv_obj_set_style_text_color(cb_audit, lv_color_hex(0xC8F4FF), 0);
    lv_obj_align(cb_audit, LV_ALIGN_TOP_LEFT, PAD, TOP_BAR_H + 100);

    lv_obj_t* btn_save = lv_btn_create(settings_screen);
    lv_obj_set_size(btn_save, scr_w() - (PAD * 2), 50);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_MID, 0, -80);
    style_moto_tile_button(btn_save);
    lv_obj_add_event_cb(btn_save, event_btn_save_now, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "SAVE CONTAINERS NOW");
    lv_obj_center(lbl_save);

    lv_obj_t* btn_factory = lv_btn_create(settings_screen);
    lv_obj_set_size(btn_factory, scr_w() - (PAD * 2), 50);
    lv_obj_align(btn_factory, LV_ALIGN_BOTTOM_MID, 0, -20);
    style_moto_tile_button(btn_factory);
    lv_obj_add_event_cb(btn_factory, event_btn_factory_reset, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_factory = lv_label_create(btn_factory);
    lv_label_set_text(lbl_factory, "FACTORY RESET (ERASE)");
    lv_obj_center(lbl_factory);
}

static void reset_pin_buffer() {
    pin_len = 0;
    memset(pin_buffer, 0, sizeof(pin_buffer));
    if (pin_label) lv_label_set_text(pin_label, "----");
}

static void set_pending_role(UserRole role, const char* label_text) {
    pending_role = role;
    reset_pin_buffer();
    if (user_role_label) lv_label_set_text(user_role_label, label_text);
    if (user_status_label) lv_label_set_text(user_status_label, "ENTER PIN");
}

static void build_user_screen(void) {
    if (user_screen) { lv_obj_del(user_screen); user_screen = nullptr; }

    user_screen = lv_obj_create(NULL);
    style_moto_screen(user_screen);

    // Fonts (optional; comment out if not enabled)
    extern const lv_font_t lv_font_montserrat_20;
    extern const lv_font_t lv_font_montserrat_16;

    // ---------- Top bar ----------
    lv_obj_t* top_bar = lv_obj_create(user_screen);
    lv_obj_set_size(top_bar, scr_w(), TOP_BAR_H);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x001522), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);

    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "USER LOGIN / ROLE");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00C0FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 92, 32);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -6, 0);
    style_moto_tile_button(btn_back);
    lv_obj_add_event_cb(btn_back, show_home_screen, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " HOME");
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_back);

    // ---------- Centered layout block ----------
    // We'll center the keypad and place role + pin above it.
    const int btn_w = 92;   // bigger keypad buttons
    const int btn_h = 54;
    const int col_gap = 14;
    const int row_gap = 12;

    const int grid_w = (btn_w * 3) + (col_gap * 2);
    const int grid_h = (btn_h * 4) + (row_gap * 3);

    const int role_h = 44;
    const int role_gap = 12;

    const int info_h = 80; // role label + pin line + status line

    const int block_h = role_h + role_gap + info_h + 12 + grid_h;

    int start_y = TOP_BAR_H + PAD + ( (scr_h() - TOP_BAR_H - 36 - PAD*2) - block_h ) / 2;
    if (start_y < TOP_BAR_H + PAD) start_y = TOP_BAR_H + PAD;

    const int center_x = scr_w() / 2;

    // ---------- Role buttons (centered) ----------
    lv_obj_t* btn_admin = lv_btn_create(user_screen);
    lv_obj_set_size(btn_admin, 140, role_h);
    lv_obj_align(btn_admin, LV_ALIGN_TOP_MID, -(140/2 + 10), start_y);
    style_moto_tile_button(btn_admin);
    lv_obj_add_event_cb(btn_admin, event_select_admin, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_admin = lv_label_create(btn_admin);
    lv_label_set_text(lbl_admin, "ADMIN");
    lv_obj_set_style_text_font(lbl_admin, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_admin);

    lv_obj_t* btn_operator = lv_btn_create(user_screen);
    lv_obj_set_size(btn_operator, 140, role_h);
    lv_obj_align(btn_operator, LV_ALIGN_TOP_MID, +(140/2 + 10), start_y);
    style_moto_tile_button(btn_operator);
    lv_obj_add_event_cb(btn_operator, event_select_operator, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_operator = lv_label_create(btn_operator);
    lv_label_set_text(lbl_operator, "OPERATOR");
    lv_obj_set_style_text_font(lbl_operator, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_operator);

    int y = start_y + role_h + role_gap;

    // ---------- Labels (centered) ----------
    user_role_label = lv_label_create(user_screen);
    lv_label_set_text(user_role_label, "LOGIN: (SELECT ROLE)");
    lv_obj_set_style_text_color(user_role_label, lv_color_hex(0xC8F4FF), 0);
    lv_obj_set_style_text_font(user_role_label, &lv_font_montserrat_16, 0);
    lv_obj_align(user_role_label, LV_ALIGN_TOP_MID, 0, y);

    y += 26;

    lv_obj_t* pin_row = lv_obj_create(user_screen);
    lv_obj_set_size(pin_row, grid_w, 32);
    lv_obj_align(pin_row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_clear_flag(pin_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(pin_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pin_row, 0, 0);
    lv_obj_set_style_pad_all(pin_row, 0, 0);

    lv_obj_t* pin_caption = lv_label_create(pin_row);
    lv_label_set_text(pin_caption, "PIN:");
    lv_obj_set_style_text_color(pin_caption, lv_color_hex(0x80E0FF), 0);
    lv_obj_set_style_text_font(pin_caption, &lv_font_montserrat_20, 0);
    lv_obj_align(pin_caption, LV_ALIGN_LEFT_MID, 0, 0);

    pin_label = lv_label_create(pin_row);
    lv_label_set_text(pin_label, "----");
    lv_obj_set_style_text_color(pin_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(pin_label, &lv_font_montserrat_20, 0);
    lv_obj_align(pin_label, LV_ALIGN_LEFT_MID, 60, 0);

    y += 36;

    user_status_label = lv_label_create(user_screen);
    lv_label_set_text(user_status_label, "SELECT ROLE");
    lv_obj_set_style_text_color(user_status_label, lv_color_hex(0xFFD0A0), 0);
    lv_obj_set_style_text_font(user_status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(user_status_label, LV_ALIGN_TOP_MID, 0, y);

    y += 28;

    // ---------- Keypad grid (centered) ----------
    const char* keys[12] = { "1","2","3","4","5","6","7","8","9","CLR","0","OK" };

    const int grid_left = center_x - (grid_w / 2);
    const int grid_top  = y;

    int idx = 0;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 3; ++col) {
            const char* txt = keys[idx++];

            lv_obj_t* btn = lv_btn_create(user_screen);
            lv_obj_set_size(btn, btn_w, btn_h);

            int x = grid_left + col * (btn_w + col_gap);
            int yy = grid_top + row * (btn_h + row_gap);

            lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, yy);
            style_moto_tile_button(btn);

            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, txt);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
            lv_obj_center(lbl);

            if (strcmp(txt, "CLR") == 0) lv_obj_add_event_cb(btn, event_keypad_clear, LV_EVENT_CLICKED, NULL);
            else if (strcmp(txt, "OK") == 0) lv_obj_add_event_cb(btn, event_keypad_ok, LV_EVENT_CLICKED, NULL);
            else lv_obj_add_event_cb(btn, event_keypad_digit, LV_EVENT_CLICKED, NULL);
        }
    }

    reset_pin_buffer();
}


static void event_select_admin(lv_event_t* e) { (void)e; set_pending_role(ROLE_ADMIN, "LOGIN: ADMIN"); }
static void event_select_operator(lv_event_t* e) { (void)e; set_pending_role(ROLE_OPERATOR, "LOGIN: OPERATOR"); }

static void event_keypad_digit(lv_event_t* e) {
    if (pending_role == ROLE_NONE) {
        if (user_status_label) lv_label_set_text(user_status_label, "SELECT ROLE FIRST");
        return;
    }
    if (pin_len >= sizeof(pin_buffer) - 1) return;

    lv_obj_t* btn = lv_event_get_target(e);
    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
    const char* txt = lv_label_get_text(lbl);
    if (!txt || strlen(txt) != 1) return;

    pin_buffer[pin_len++] = txt[0];
    pin_buffer[pin_len] = '\0';

    char stars[8];
    uint8_t count = (pin_len < (sizeof(stars) - 1)) ? pin_len : (sizeof(stars) - 1);
    for (uint8_t i = 0; i < count; ++i) stars[i] = '*';
    stars[count] = '\0';
    lv_label_set_text(pin_label, stars);
}

static void event_keypad_clear(lv_event_t* e) {
    (void)e;
    reset_pin_buffer();
    if (user_status_label) lv_label_set_text(user_status_label, "PIN CLEARED");
}

static void event_keypad_ok(lv_event_t* e) {
    (void)e;
    if (pending_role == ROLE_NONE) {
        if (user_status_label) lv_label_set_text(user_status_label, "SELECT ROLE FIRST");
        return;
    }

    const char* expected = nullptr;
    const char* user_name = nullptr;

    if (pending_role == ROLE_ADMIN) { expected = PIN_ADMIN; user_name = "ADMIN"; }
    else if (pending_role == ROLE_OPERATOR) { expected = PIN_OPERATOR; user_name = "OPERATOR"; }

    if (!expected) return;

    if (strcmp(pin_buffer, expected) == 0) {
        current_role = pending_role;
        current_user_name = user_name;
        if (user_status_label) lv_label_set_text(user_status_label, "LOGIN OK");
        if (status_label) lv_label_set_text(status_label, "LOGIN OK");
        update_home_user_label();
        reset_pin_buffer();
        if (home_screen) lv_scr_load(home_screen);
    } else {
        if (user_status_label) lv_label_set_text(user_status_label, "PIN INVALID");
        reset_pin_buffer();
    }
}

// ----------------------
// Navigation
// ----------------------

static void show_home_screen(lv_event_t* e) {
    (void)e;
    if (!home_screen) build_home_screen();
    if (home_screen) {
        lv_scr_load(home_screen);
        if (status_label) lv_label_set_text(status_label, "READY - LOGIN RECOMMENDED");
    }
}

static void event_btn_keys(lv_event_t* e) {
    (void)e;
    if (!check_access(false, "CONTAINER VIEW OPEN")) return;
    build_containers_screen();
    if (containers_screen) lv_scr_load(containers_screen);
}

static void event_btn_keyload(lv_event_t* e) {
    (void)e;
    if (!check_access(false, "KEYLOAD CONSOLE OPEN")) return;
    build_keyload_screen();
    if (keyload_screen) {
        update_keyload_container_label();
        lv_scr_load(keyload_screen);
    }
}

static void event_btn_settings(lv_event_t* e) {
    (void)e;
    if (!check_access(true, "SETTINGS OPEN")) return;
    build_settings_screen();
    if (settings_screen) lv_scr_load(settings_screen);
}

static void event_btn_user_manager(lv_event_t* e) {
    (void)e;
    if (status_label) lv_label_set_text(status_label, "USER LOGIN SCREEN");
    build_user_screen();
    if (user_screen) lv_scr_load(user_screen);
}

// ----------------------
// Public entrypoint
// ----------------------

void ui_init(void) {
    build_home_screen();
    lv_scr_load(home_screen);
}
