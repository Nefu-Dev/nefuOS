// nefuOS HTML5 Parser - based on Hubbub (NetSurf, MIT License) design
// Simplified for bare-metal OS with minimal dependencies
// Original Hubbub: Copyright 2007-2010, NetSurf Project, MIT License

#ifndef NEFU_HTML_PARSER_H
#define NEFU_HTML_PARSER_H

#include "../klib/klib.h"

namespace nefu {

// HTML node types (DOM tree structure)
enum HtmlNodeType {
    HTML_NODE_DOCUMENT = 0,
    HTML_NODE_DOCTYPE,
    HTML_NODE_ELEMENT,
    HTML_NODE_TEXT,
    HTML_NODE_COMMENT,
};

// Element flags
enum HtmlElementFlags {
    HTML_FLAG_BLOCK = 1,
    HTML_FLAG_INLINE = 2,
    HTML_FLAG_VOID = 4,        // self-closing (img, br, input...)
    HTML_FLAG_HEADING = 8,
    HTML_FLAG_LIST = 16,
    HTML_FLAG_TABLE = 32,
    HTML_FLAG_FORM = 64,
    HTML_FLAG_SKIP = 128,      // script, style...
};

struct HtmlAttribute {
    String name;
    String value;
};

struct HtmlNode {
    HtmlNodeType type;
    String tag_name;        // for elements
    String text;            // for text nodes
    uint32_t flags;         // HtmlElementFlags
    uint32_t color;
    int font_size;
    bool bold;
    int indent;
    List<HtmlAttribute> attrs;
    List<HtmlNode*> children;
    HtmlNode* parent;

    HtmlNode() : type(HTML_NODE_DOCUMENT), flags(0), color(0),
                 font_size(16), bold(false), indent(0), parent(0) {}
    ~HtmlNode() {
        for (int i = 0; i < children.size(); i++) delete children[i];
    }
};

// CSS computed style (simplified)
struct HtmlStyle {
    uint32_t color;
    uint32_t bg_color;
    int font_size;
    bool bold;
    bool italic;
    int margin_top;
    int margin_bottom;
    int padding_left;
    int text_align; // 0=left 1=center 2=right
};

// Parser state
struct HtmlParser {
    const char* html;
    int length;
    int pos;
    HtmlNode* root;
    HtmlNode* current;
    bool in_title;
    int title_len;
    String title;

    HtmlParser() : html(0), length(0), pos(0), root(0), current(0),
                   in_title(false), title_len(0) {}

    ~HtmlParser() {
        if (root) delete root;
    }
};

// Public API
HtmlNode* html5_parse(const char* html, int len);
void html5_render_to_lines(HtmlNode* root, List<Line>& out);
const char* html5_get_title(HtmlNode* root);

// Tag classification
int html_tag_flags(const char* tag);
bool html_is_block_tag(const char* tag);
bool html_is_void_tag(const char* tag);
bool html_is_skip_tag(const char* tag);

} // namespace nefu

#endif // NEFU_HTML_PARSER_H
