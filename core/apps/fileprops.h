// nefuOS File Properties Dialog
// Shows detailed file information when right-clicking a file
#pragma once

#include "../vfs/vfs.h"
#include "../klib/klib.h"
#include "lvgl.h"

namespace nefu {
namespace apps {

// Properties dialog state
struct PropsState {
    lv_obj_t* win;
    lv_obj_t* name_label;
    lv_obj_t* type_label;
    lv_obj_t* size_label;
    lv_obj_t* path_label;
    lv_obj_t* created_label;
    lv_obj_t* modified_label;
    lv_obj_t* permissions_label;
    FSNode* file;
};

static PropsState s_props;

// Close dialog callback
static void on_props_close(lv_event_t* e) {
    if (s_props.win) {
        lv_obj_del(s_props.win);
        s_props.win = 0;
        s_props.file = 0;
    }
}

// Show file properties dialog
void show_properties(const char* filepath) {
    if (!filepath) return;
    
    FSNode* f = g_vfs->resolve(filepath);
    if (!f) return;
    
    // Create window
    s_props.win = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_props.win, 360, 280);
    lv_obj_center(s_props.win);
    lv_obj_set_style_bg_color(s_props.win, lv_color_white(), 0);
    lv_obj_set_style_border_color(s_props.win, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_border_width(s_props.win, 1, 0);
    lv_obj_set_style_radius(s_props.win, 8, 0);
    lv_obj_set_style_pad_all(s_props.win, 16, 0);
    
    s_props.file = f;
    
    // Title
    lv_obj_t* title = lv_label_create(s_props.win);
    lv_label_set_text(title, "File Properties");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // Close button
    lv_obj_t* close_btn = lv_button_create(s_props.win);
    lv_obj_set_size(close_btn, 32, 32);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "X");
    lv_obj_center(close_label);
    lv_obj_add_event_cb(close_btn, on_props_close, LV_EVENT_CLICKED, 0);
    
    // File name
    s_props.name_label = lv_label_create(s_props.win);
    char buf[128];
    ksprintf(buf, sizeof(buf), "Name: %s", filepath);
    lv_label_set_text(s_props.name_label, buf);
    lv_obj_align(s_props.name_label, LV_ALIGN_TOP_LEFT, 0, 40);
    
    // File type
    s_props.type_label = lv_label_create(s_props.win);
    ksprintf(buf, sizeof(buf), "Type: %s", (f->type == FSNode::FILE) ? "File" : "Directory");
    lv_label_set_text(s_props.type_label, buf);
    lv_obj_align(s_props.type_label, LV_ALIGN_TOP_LEFT, 0, 70);
    
    // File size
    s_props.size_label = lv_label_create(s_props.win);
    if (f->type == FSNode::FILE) {
        ksprintf(buf, sizeof(buf), "Size: %d bytes (%.1f KB)", f->size, f->size / 1024.0f);
    } else {
        ksprintf(buf, sizeof(buf), "Contains: items");
    }
    lv_label_set_text(s_props.size_label, buf);
    lv_obj_align(s_props.size_label, LV_ALIGN_TOP_LEFT, 0, 100);
    
    // Location
    s_props.path_label = lv_label_create(s_props.win);
    ksprintf(buf, sizeof(buf), "Location: /");
    lv_label_set_text(s_props.path_label, buf);
    lv_obj_align(s_props.path_label, LV_ALIGN_TOP_LEFT, 0, 130);
    
    // Created
    s_props.created_label = lv_label_create(s_props.win);
    ksprintf(buf, sizeof(buf), "Created: %s", "2024-01-01 00:00:00");
    lv_label_set_text(s_props.created_label, buf);
    lv_obj_align(s_props.created_label, LV_ALIGN_TOP_LEFT, 0, 160);
    
    // Modified
    s_props.modified_label = lv_label_create(s_props.win);
    ksprintf(buf, sizeof(buf), "Modified: %s", "2024-01-01 00:00:00");
    lv_label_set_text(s_props.modified_label, buf);
    lv_obj_align(s_props.modified_label, LV_ALIGN_TOP_LEFT, 0, 190);
    
    // Permissions
    s_props.permissions_label = lv_label_create(s_props.win);
    ksprintf(buf, sizeof(buf), "Permissions: rw-r--r--");
    lv_label_set_text(s_props.permissions_label, buf);
    lv_obj_align(s_props.permissions_label, LV_ALIGN_TOP_LEFT, 0, 220);
    
    // OK button
    lv_obj_t* ok_btn = lv_button_create(s_props.win);
    lv_obj_set_size(ok_btn, 80, 30);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t* ok_label = lv_label_create(ok_btn);
    lv_label_set_text(ok_label, "OK");
    lv_obj_center(ok_label);
    lv_obj_add_event_cb(ok_btn, on_props_close, LV_EVENT_CLICKED, 0);
}

} // namespace apps
} // namespace nefu
