// nefuOS HTML5 Parser implementation
// Based on Hubbub (NetSurf, MIT License) design principles
// Simplified for bare-metal OS

#include "html5.h"
#include "../klib/klib.h"
#include "../vfs/vfs.h"

namespace nefu {

// =====================================================================
// Tag classification tables (based on HTML5 spec)
// =====================================================================

struct TagInfo {
    const char* name;
    int flags;
};

static const TagInfo g_tag_table[] = {
    // Document
    { "html",       HTML_FLAG_BLOCK },
    { "head",       HTML_FLAG_BLOCK | HTML_FLAG_SKIP },
    { "body",       HTML_FLAG_BLOCK },

    // Block elements
    { "div",        HTML_FLAG_BLOCK },
    { "p",          HTML_FLAG_BLOCK },
    { "section",    HTML_FLAG_BLOCK },
    { "article",    HTML_FLAG_BLOCK },
    { "header",     HTML_FLAG_BLOCK },
    { "footer",     HTML_FLAG_BLOCK },
    { "main",       HTML_FLAG_BLOCK },
    { "nav",        HTML_FLAG_BLOCK },
    { "aside",      HTML_FLAG_BLOCK },

    // Headings
    { "h1",         HTML_FLAG_BLOCK | HTML_FLAG_HEADING },
    { "h2",         HTML_FLAG_BLOCK | HTML_FLAG_HEADING },
    { "h3",         HTML_FLAG_BLOCK | HTML_FLAG_HEADING },
    { "h4",         HTML_FLAG_BLOCK | HTML_FLAG_HEADING },
    { "h5",         HTML_FLAG_BLOCK | HTML_FLAG_HEADING },
    { "h6",         HTML_FLAG_BLOCK | HTML_FLAG_HEADING },

    // Lists
    { "ul",         HTML_FLAG_BLOCK | HTML_FLAG_LIST },
    { "ol",         HTML_FLAG_BLOCK | HTML_FLAG_LIST },
    { "li",         HTML_FLAG_BLOCK },
    { "dl",         HTML_FLAG_BLOCK | HTML_FLAG_LIST },
    { "dt",         HTML_FLAG_BLOCK },
    { "dd",         HTML_FLAG_BLOCK },

    // Tables
    { "table",      HTML_FLAG_BLOCK | HTML_FLAG_TABLE },
    { "tr",         HTML_FLAG_BLOCK | HTML_FLAG_TABLE },
    { "td",         HTML_FLAG_TABLE },
    { "th",         HTML_FLAG_TABLE | HTML_FLAG_HEADING },

    // Forms
    { "form",       HTML_FLAG_BLOCK | HTML_FLAG_FORM },
    { "input",      HTML_FLAG_VOID | HTML_FLAG_FORM },
    { "button",     HTML_FLAG_FORM },
    { "select",     HTML_FLAG_FORM },
    { "textarea",   HTML_FLAG_FORM },
    { "label",      HTML_FLAG_INLINE },

    // Inline elements
    { "span",       HTML_FLAG_INLINE },
    { "a",          HTML_FLAG_INLINE },
    { "b",          HTML_FLAG_INLINE },
    { "strong",     HTML_FLAG_INLINE },
    { "i",          HTML_FLAG_INLINE },
    { "em",         HTML_FLAG_INLINE },
    { "u",          HTML_FLAG_INLINE },
    { "small",      HTML_FLAG_INLINE },
    { "code",       HTML_FLAG_INLINE },
    { "pre",        HTML_FLAG_BLOCK },
    { "font",       HTML_FLAG_INLINE },

    // Void elements
    { "br",         HTML_FLAG_VOID },
    { "hr",         HTML_FLAG_VOID | HTML_FLAG_BLOCK },
    { "img",        HTML_FLAG_VOID },
    { "meta",       HTML_FLAG_VOID },
    { "link",       HTML_FLAG_VOID },
    { "input",      HTML_FLAG_VOID },

    // Skip elements (don't render content)
    { "script",     HTML_FLAG_SKIP },
    { "style",      HTML_FLAG_SKIP },
    { "noscript",   HTML_FLAG_SKIP },
    { "title",      HTML_FLAG_SKIP },
    { "head",       HTML_FLAG_SKIP },

    // Media
    { "video",      HTML_FLAG_INLINE },
    { "audio",      HTML_FLAG_INLINE },
    { "iframe",     HTML_FLAG_BLOCK },

    // Other
    { "blockquote", HTML_FLAG_BLOCK },
    { "center",     HTML_FLAG_BLOCK },
    { "details",    HTML_FLAG_BLOCK },
    { "summary",    HTML_FLAG_BLOCK },
    { "figure",     HTML_FLAG_BLOCK },
    { "figcaption", HTML_FLAG_BLOCK },
};

static const int g_tag_count = sizeof(g_tag_table) / sizeof(g_tag_table[0]);

int html_tag_flags(const char* tag) {
    for (int i = 0; i < g_tag_count; i++) {
        if (strcasecmp(tag, g_tag_table[i].name) == 0) {
            return g_tag_table[i].flags;
        }
    }
    return HTML_FLAG_INLINE; // default
}

bool html_is_block_tag(const char* tag) {
    return (html_tag_flags(tag) & HTML_FLAG_BLOCK) != 0;
}

bool html_is_void_tag(const char* tag) {
    return (html_tag_flags(tag) & HTML_FLAG_VOID) != 0;
}

bool html_is_skip_tag(const char* tag) {
    return (html_tag_flags(tag) & HTML_FLAG_SKIP) != 0;
}

// =====================================================================
// Tokenizer
// =====================================================================

struct Token {
    enum Type {
        TOKEN_EOF,
        TOKEN_TEXT,
        TOKEN_START_TAG,
        TOKEN_END_TAG,
        TOKEN_COMMENT,
        TOKEN_DOCTYPE,
    };

    Type type;
    String tag_name;
    String text;
    List<HtmlAttribute> attrs;
    bool self_closing;

    Token() : type(TOKEN_EOF), self_closing(false) {}
};

// Skip whitespace
static void skip_ws(HtmlParser* p) {
    while (p->pos < p->length &&
           (p->html[p->pos] == ' ' || p->html[p->pos] == '\t' ||
            p->html[p->pos] == '\n' || p->html[p->pos] == '\r')) {
        p->pos++;
    }
}

// Read tag name
static void read_tag_name(HtmlParser* p, String& out) {
    while (p->pos < p->length) {
        char c = p->html[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
            c == '>' || c == '/' || c == '=') break;
        out += c;
        p->pos++;
    }
    // lowercase
    for (int i = 0; i < out.len(); i++) {
        char c = out[i];
        if (c >= 'A' && c <= 'Z') out.set(i, c - 'A' + 'a');
    }
}

// Read attribute value
static void read_attr_value(HtmlParser* p, String& out) {
    if (p->pos >= p->length) return;

    char quote = p->html[p->pos];
    if (quote == '"' || quote == '\'') {
        p->pos++;
        while (p->pos < p->length && p->html[p->pos] != quote) {
            out += p->html[p->pos];
            p->pos++;
        }
        if (p->pos < p->length) p->pos++; // skip closing quote
    } else {
        while (p->pos < p->length) {
            char c = p->html[p->pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                c == '>' || c == '/') break;
            out += c;
            p->pos++;
        }
    }
}

// Parse attributes inside a tag
static void parse_attributes(HtmlParser* p, List<HtmlAttribute>& attrs) {
    while (p->pos < p->length) {
        skip_ws(p);
        if (p->pos >= p->length || p->html[p->pos] == '>') break;
        if (p->html[p->pos] == '/') {
            p->pos++;
            break;
        }

        // Read attribute name
        HtmlAttribute attr;
        while (p->pos < p->length) {
            char c = p->html[p->pos];
            if (c == '=' || c == ' ' || c == '\t' || c == '\n' ||
                c == '\r' || c == '>' || c == '/') break;
            attr.name += c;
            p->pos++;
        }

        // Read attribute value
        skip_ws(p);
        if (p->pos < p->length && p->html[p->pos] == '=') {
            p->pos++;
            skip_ws(p);
            read_attr_value(p, attr.value);
        }

        if (!attr.name.empty()) {
            attrs.push(attr);
        }
    }
}

// Get next token
static Token next_token(HtmlParser* p) {
    Token tok;

    if (p->pos >= p->length) {
        tok.type = Token::TOKEN_EOF;
        return tok;
    }

    char c = p->html[p->pos];

    // Text content
    if (c != '<') {
        tok.type = Token::TOKEN_TEXT;
        while (p->pos < p->length && p->html[p->pos] != '<') {
            tok.text += p->html[p->pos];
            p->pos++;
        }
        return tok;
    }

    // Tag
    p->pos++; // skip '<'

    // Comment
    if (p->pos + 3 < p->length &&
        p->html[p->pos] == '!' && p->html[p->pos+1] == '-' && p->html[p->pos+2] == '-') {
        p->pos += 3;
        tok.type = Token::TOKEN_COMMENT;
        while (p->pos + 2 < p->length) {
            if (p->html[p->pos] == '-' && p->html[p->pos+1] == '-' && p->html[p->pos+2] == '>') {
                p->pos += 3;
                break;
            }
            p->pos++;
        }
        return tok;
    }

    // Doctype
    if (p->pos + 1 < p->length && p->html[p->pos] == '!') {
        tok.type = Token::TOKEN_DOCTYPE;
        while (p->pos < p->length && p->html[p->pos] != '>') p->pos++;
        if (p->pos < p->length) p->pos++;
        return tok;
    }

    // End tag
    bool is_end = false;
    if (p->pos < p->length && p->html[p->pos] == '/') {
        is_end = true;
        p->pos++;
    }

    // Read tag name
    read_tag_name(p, tok.tag_name);

    if (tok.tag_name.empty()) {
        // Malformed tag, treat as text
        tok.type = Token::TOKEN_TEXT;
        tok.text = "<";
        return tok;
    }

    // Parse attributes
    parse_attributes(p, tok.attrs);

    // Check for self-closing
    skip_ws(p);
    if (p->pos < p->length && p->html[p->pos] == '/') {
        tok.self_closing = true;
        p->pos++;
    }

    // Skip '>'
    if (p->pos < p->length && p->html[p->pos] == '>') {
        p->pos++;
    }

    tok.type = is_end ? Token::TOKEN_END_TAG : Token::TOKEN_START_TAG;
    return tok;
}

// =====================================================================
// Tree builder (based on HTML5 spec, simplified)
// =====================================================================

// Find attribute by name
static const char* get_attr(List<HtmlAttribute>& attrs, const char* name) {
    for (int i = 0; i < attrs.size(); i++) {
        if (strcasecmp(attrs[i].name.c_str(), name) == 0) {
            return attrs[i].value.c_str();
        }
    }
    return 0;
}

// Create element node
static HtmlNode* create_element(const char* tag, List<HtmlAttribute>& attrs) {
    HtmlNode* node = new HtmlNode();
    node->type = HTML_NODE_ELEMENT;
    node->tag_name = tag;
    node->flags = html_tag_flags(tag);

    // Copy attributes
    for (int i = 0; i < attrs.size(); i++) {
        node->attrs.push(attrs[i]);
    }

    // Apply default styles based on tag
    if (node->flags & HTML_FLAG_HEADING) {
        node->bold = true;
        if (strcmp(tag, "h1") == 0) node->font_size = 32;
        else if (strcmp(tag, "h2") == 0) node->font_size = 24;
        else if (strcmp(tag, "h3") == 0) node->font_size = 20;
        else node->font_size = 16;
    }

    if (strcmp(tag, "b") == 0 || strcmp(tag, "strong") == 0) {
        node->bold = true;
    }

    if (strcmp(tag, "a") == 0) {
        node->color = 0x0000EE;
    }

    return node;
}

// Create text node
static HtmlNode* create_text(const char* text, int len) {
    HtmlNode* node = new HtmlNode();
    node->type = HTML_NODE_TEXT;
    node->text.assign(text, len);
    return node;
}

// Insert node into tree
static void insert_node(HtmlNode* parent, HtmlNode* child) {
    if (!parent || !child) return;
    child->parent = parent;
    parent->children.push(child);
}

// HTML5 parser main entry
HtmlNode* html5_parse(const char* html, int len) {
    if (!html || len <= 0) return 0;

    HtmlParser parser;
    parser.html = html;
    parser.length = len;
    parser.pos = 0;

    parser.root = new HtmlNode();
    parser.root->type = HTML_NODE_DOCUMENT;
    parser.current = parser.root;

    int skip_depth = 0;
    char skip_tag[32] = {0};

    while (true) {
        Token tok = next_token(&parser);
        if (tok.type == Token::TOKEN_EOF) break;

        // Skip content inside script/style/etc
        if (skip_depth > 0) {
            if (tok.type == Token::TOKEN_END_TAG &&
                strcmp(tok.tag_name.c_str(), skip_tag) == 0) {
                skip_depth--;
                if (skip_depth == 0) skip_tag[0] = 0;
            }
            continue;
        }

        switch (tok.type) {
            case Token::TOKEN_TEXT: {
                // Skip whitespace-only text between block elements
                HtmlNode* text = create_text(tok.text.c_str(), tok.text.len());
                insert_node(parser.current, text);
                break;
            }

            case Token::TOKEN_START_TAG: {
                const char* tag = tok.tag_name.c_str();

                // Check if this is a skip tag
                if (html_is_skip_tag(tag)) {
                    skip_depth = 1;
                    strncpy(skip_tag, tag, 31);
                    skip_tag[31] = 0;
                    continue;
                }

                HtmlNode* elem = create_element(tag, tok.attrs);

                // Special: title
                if (strcmp(tag, "title") == 0) {
                    parser.in_title = true;
                }

                // Special: img - create placeholder
                if (strcmp(tag, "img") == 0) {
                    const char* src = get_attr(tok.attrs, "src");
                    if (src) {
                        String img_line = "[img: ";
                        img_line += src;
                        img_line += "]";
                        HtmlNode* text = create_text(img_line.c_str(), img_line.len());
                        insert_node(parser.current, text);
                    }
                    delete elem;
                    continue;
                }

                // Special: br - just add a newline
                if (strcmp(tag, "br") == 0) {
                    HtmlNode* text = create_text("\n", 1);
                    insert_node(parser.current, text);
                    delete elem;
                    continue;
                }

                // Special: hr - horizontal rule
                if (strcmp(tag, "hr") == 0) {
                    HtmlNode* text = create_text("\n----------------------------------------\n", 42);
                    insert_node(parser.current, text);
                    delete elem;
                    continue;
                }

                insert_node(parser.current, elem);

                // Void elements don't have children
                if (!html_is_void_tag(tag)) {
                    parser.current = elem;
                }
                break;
            }

            case Token::TOKEN_END_TAG: {
                const char* tag = tok.tag_name.c_str();

                // Pop back to matching open tag
                HtmlNode* node = parser.current;
                int depth = 0;
                while (node && node->type != HTML_NODE_DOCUMENT) {
                    if (node->tag_name == tag) {
                        parser.current = node->parent ? node->parent : parser.root;
                        break;
                    }
                    node = node->parent;
                    depth++;
                }

                if (strcmp(tag, "title") == 0) {
                    parser.in_title = false;
                }
                break;
            }

            default:
                break;
        }
    }

    return parser.root;
}

// =====================================================================
// Render DOM tree to line list
// =====================================================================

static void render_node(HtmlNode* node, List<Line>& out,
                        int indent, uint32_t parent_color,
                        int parent_font_size, bool parent_bold) {
    if (!node) return;

    switch (node->type) {
        case HTML_NODE_TEXT: {
            Line l;
            l.s = node->text;
            l.color = node->color ? node->color : parent_color;
            l.font_size = node->font_size ? node->font_size : parent_font_size;
            l.bold = node->bold || parent_bold;
            l.indent = indent;
            out.push(l);
            break;
        }

        case HTML_NODE_ELEMENT: {
            int child_indent = indent;
            uint32_t child_color = node->color ? node->color : parent_color;
            int child_font_size = node->font_size ? node->font_size : parent_font_size;
            bool child_bold = node->bold || parent_bold;

            // Increase indent for lists
            if (strcmp(node->tag_name.c_str(), "ul") == 0 ||
                strcmp(node->tag_name.c_str(), "ol") == 0) {
                child_indent += 2;
            }

            // List items
            if (strcmp(node->tag_name.c_str(), "li") == 0) {
                Line l;
                l.s = "  * ";
                l.font_size = child_font_size;
                l.bold = child_bold;
                l.color = child_color;
                l.indent = indent;
                out.push(l);
            }

            // Table cells
            if (strcmp(node->tag_name.c_str(), "td") == 0 ||
                strcmp(node->tag_name.c_str(), "th") == 0) {
                // Will be rendered inline with other cells
            }

            // Render children
            for (int i = 0; i < node->children.size(); i++) {
                render_node(node->children[i], out, child_indent,
                           child_color, child_font_size, child_bold);
            }

            // Block elements get a newline after
            if (node->flags & HTML_FLAG_BLOCK) {
                Line l;
                l.s = "";
                l.font_size = child_font_size;
                out.push(l);
            }
            break;
        }

        default:
            break;
    }
}

void html5_render_to_lines(HtmlNode* root, List<Line>& out) {
    if (!root) return;

    // Start with body element styles
    for (int i = 0; i < root->children.size(); i++) {
        render_node(root->children[i], out, 0, 0, 16, false);
    }
}

const char* html5_get_title(HtmlNode* root) {
    if (!root) return 0;

    // Find title element
    for (int i = 0; i < root->children.size(); i++) {
        HtmlNode* child = root->children[i];
        if (child->type == HTML_NODE_ELEMENT &&
            strcmp(child->tag_name.c_str(), "head") == 0) {
            for (int j = 0; j < child->children.size(); j++) {
                HtmlNode* head_child = child->children[j];
                if (head_child->type == HTML_NODE_ELEMENT &&
                    strcmp(head_child->tag_name.c_str(), "title") == 0) {
                    // Get text content of title
                    if (head_child->children.size() > 0 &&
                        head_child->children[0]->type == HTML_NODE_TEXT) {
                        return head_child->children[0]->text.c_str();
                    }
                }
            }
        }
    }
    return 0;
}

} // namespace nefu
