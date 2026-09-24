// nefuOS Browser Engine v2.0 - based on Ladybird/Hubbub design (MIT/BSD)
// Complete rewrite of browser rendering engine
#pragma once

#include "../klib/klib.h"
#include "../gui/gfx.h"
#include "../net/html5.h"

namespace nefu {
namespace browser2 {

// Browser configuration
struct BrowserConfig {
    int viewport_width;
    int viewport_height;
    int default_font_size;
    bool enable_images;
    bool enable_javascript;
    bool enable_css;

    BrowserConfig() : viewport_width(800), viewport_height(600),
        default_font_size(16), enable_images(true),
        enable_javascript(false), enable_css(true) {}
};

// Rendered text run
struct TextRun {
    String text;
    int font_size;
    bool bold;
    bool italic;
    uint32_t color;
    int x, y;
    int width, height;
    bool is_link;
    String link_url;
};

// Rendered image
struct RenderedImage {
    String url;
    int x, y;
    int width, height;
    Surface* surface;
    bool loaded;
    bool failed;
};

// Browser view: holds rendered content
struct BrowserView {
    BrowserConfig config;
    List<TextRun> text_runs;
    List<RenderedImage> images;
    String title;
    String url;
    int scroll_y;
    int total_height;

    BrowserView() : scroll_y(0), total_height(0) {}

    void clear() {
        text_runs.clear();
        images.clear();
        title.clear();
        url.clear();
        scroll_y = 0;
        total_height = 0;
    }
};

// Layout engine (based on Ladybird's LayoutBlock)
class LayoutEngine {
public:
    struct LayoutState {
        int x;
        int y;
        int line_height;
        int current_font_size;
        bool current_bold;
        bool current_italic;
        uint32_t current_color;
        int indent;
        BrowserView& view;

        LayoutState(BrowserView& v) : view(v), x(0), y(0), line_height(18),
            current_font_size(16), current_bold(false), current_italic(false),
            current_color(0), indent(0) {}
    };

    static void new_line(LayoutState& state) {
        state.x = state.indent * 8;
        state.y += state.line_height;
    }

    static void add_text(LayoutState& state, const String& text) {
        if (text.empty()) return;

        TextRun run;
        run.text = text;
        run.font_size = state.current_font_size;
        run.bold = state.current_bold;
        run.italic = state.current_italic;
        run.color = state.current_color;
        run.x = state.x;
        run.y = state.y;
        run.width = text.len() * 8;  // approximate
        run.height = state.line_height;

        state.view.text_runs.push(run);
        state.x += run.width;

        // Auto-wrap (simplified)
        if (state.x > state.view.config.viewport_width - 50) {
            new_line(state);
        }
    }

    static void layout_node(HtmlNode* node, LayoutState& state) {
        if (!node) return;

        if (node->type == HTML_TOKEN_CHARACTER) {
            // Skip whitespace-only text between block elements
            add_text(state, node->text_content);
            return;
        }

        if (node->type != HTML_TOKEN_START_TAG) return;

        // Save state
        int old_x = state.x;
        int old_y = state.y;
        int old_font_size = state.current_font_size;
        bool old_bold = state.current_bold;
        bool old_italic = state.current_italic;
        uint32_t old_color = state.current_color;
        int old_indent = state.indent;

        // Apply tag styles
        switch (node->tag_id) {
            case HTML_TAG_H1:
                new_line(state);
                state.current_font_size = 32;
                state.current_bold = true;
                state.line_height = 38;
                break;
            case HTML_TAG_H2:
                new_line(state);
                state.current_font_size = 24;
                state.current_bold = true;
                state.line_height = 28;
                break;
            case HTML_TAG_H3:
                new_line(state);
                state.current_font_size = 20;
                state.current_bold = true;
                state.line_height = 24;
                break;
            case HTML_TAG_H4:
            case HTML_TAG_H5:
            case HTML_TAG_H6:
                new_line(state);
                state.current_font_size = 16;
                state.current_bold = true;
                break;
            case HTML_TAG_B:
            case HTML_TAG_STRONG:
                state.current_bold = true;
                break;
            case HTML_TAG_I:
            case HTML_TAG_EM:
                state.current_italic = true;
                break;
            case HTML_TAG_A:
                state.current_color = 0x0000EE;
                break;
            case HTML_TAG_BR:
                new_line(state);
                break;
            case HTML_TAG_HR:
                new_line(state);
                add_text(state, "----------------------------------------");
                new_line(state);
                break;
            case HTML_TAG_P:
                new_line(state);
                break;
            case HTML_TAG_DIV:
                new_line(state);
                break;
            case HTML_TAG_PRE:
                new_line(state);
                state.indent += 2;
                break;
            case HTML_TAG_BLOCKQUOTE:
                new_line(state);
                state.indent += 4;
                break;
            case HTML_TAG_UL:
            case HTML_TAG_OL:
                state.indent += 2;
                break;
            case HTML_TAG_LI:
                new_line(state);
                add_text(state, "  * ");
                break;
            case HTML_TAG_TABLE:
                new_line(state);
                break;
            case HTML_TAG_TR:
                new_line(state);
                break;
            case HTML_TAG_TD:
            case HTML_TAG_TH:
                add_text(state, " | ");
                if (node->tag_id == HTML_TAG_TH) state.current_bold = true;
                break;
            case HTML_TAG_TITLE:
                // Title is handled separately
                break;
            case HTML_TAG_SCRIPT:
            case HTML_TAG_STYLE:
                // Skip script and style content
                return;
            default:
                break;
        }

        // Layout children
        for (int i = 0; i < node->children.size(); i++) {
            layout_node(node->children[i], state);
        }

        // Post-tag cleanup
        switch (node->tag_id) {
            case HTML_TAG_H1:
            case HTML_TAG_H2:
            case HTML_TAG_H3:
            case HTML_TAG_H4:
            case HTML_TAG_H5:
            case HTML_TAG_H6:
                new_line(state);
                state.line_height = 18;
                break;
            case HTML_TAG_DIV:
            case HTML_TAG_P:
                new_line(state);
                break;
            default:
                break;
        }

        // Restore state
        state.x = old_x;
        state.y = old_y;
        state.current_font_size = old_font_size;
        state.current_bold = old_bold;
        state.current_italic = old_italic;
        state.current_color = old_color;
        state.indent = old_indent;
    }

    static void layout(const char* html, int len, BrowserView& view) {
        HtmlNode* root = HtmlTreeBuilder::parse(html, len);
        if (!root) return;

        LayoutState state(view);

        // Find title
        // (simplified - real implementation would walk the DOM properly)
        layout_node(root, state);

        view.total_height = state.y + state.line_height;

        delete root;
    }
};

// Paint engine (based on Ladybird's Painter)
class PaintEngine {
public:
    static void paint(BrowserView& view, Surface& surf, int scroll_y = 0) {
        // Fill background
        gfx::fillrect(surf, 0, 0, view.config.viewport_width, view.config.viewport_height, 0xFFFFFF);

        // Paint text runs
        for (int i = 0; i < view.text_runs.size(); i++) {
            TextRun& run = view.text_runs[i];
            int screen_y = run.y - scroll_y;

            // Cull off-screen
            if (screen_y + run.height < 0 || screen_y > view.config.viewport_height) continue;

            uint32_t fg = run.color ? run.color : 0x000000;
            uint32_t bg = 0xFFFFFF;

            // Simple text rendering (8x16 font)
            for (int c = 0; c < run.text.len(); c++) {
                int cx = run.x + c * 8;
                gfx::char8x16(surf, cx, screen_y, run.text[c], fg, bg);
            }
        }

        // Paint images
        for (int i = 0; i < view.images.size(); i++) {
            RenderedImage& img = view.images[i];
            if (!img.loaded || !img.surface) continue;

            int screen_y = img.y - scroll_y;
            if (screen_y + img.height < 0 || screen_y > view.config.viewport_height) continue;

            gfx::blit_clip(surf, *img.surface, img.x, screen_y,
                          0, 0, img.width, img.height);
        }
    }
};

// Browser engine main class
class BrowserEngine {
public:
    BrowserView view;

    void load_html(const char* html, int len) {
        view.clear();
        LayoutEngine::layout(html, len, view);
    }

    void paint(Surface& surf) {
        PaintEngine::paint(view, surf, view.scroll_y);
    }

    void scroll(int delta) {
        view.scroll_y += delta;
        if (view.scroll_y < 0) view.scroll_y = 0;
        int max_scroll = view.total_height - view.config.viewport_height;
        if (max_scroll < 0) max_scroll = 0;
        if (view.scroll_y > max_scroll) view.scroll_y = max_scroll;
    }
};

} // namespace browser2
} // namespace nefu
