// nefuOS HTML5 Parser - based on Ladybird/Hubbub design (MIT/BSD style)
// Simplified for bare metal / host runtime
#pragma once
#include "../klib/klib.h"

namespace nefu {

// HTML token types (simplified from Ladybird)
enum HtmlTokenType {
    HTML_TOKEN_DOCTYPE,
    HTML_TOKEN_START_TAG,
    HTML_TOKEN_END_TAG,
    HTML_TOKEN_CHARACTER,
    HTML_TOKEN_COMMENT,
    HTML_TOKEN_EOF
};

// HTML tag IDs (simplified subset of HTML5)
enum HtmlTagId {
    HTML_TAG_UNKNOWN = 0,
    HTML_TAG_HTML,
    HTML_TAG_HEAD,
    HTML_TAG_BODY,
    HTML_TAG_TITLE,
    HTML_TAG_META,
    HTML_TAG_LINK,
    HTML_TAG_STYLE,
    HTML_TAG_SCRIPT,
    HTML_TAG_DIV,
    HTML_TAG_SPAN,
    HTML_TAG_P,
    HTML_TAG_H1,
    HTML_TAG_H2,
    HTML_TAG_H3,
    HTML_TAG_H4,
    HTML_TAG_H5,
    HTML_TAG_H6,
    HTML_TAG_BR,
    HTML_TAG_HR,
    HTML_TAG_B,
    HTML_TAG_STRONG,
    HTML_TAG_I,
    HTML_TAG_EM,
    HTML_TAG_U,
    HTML_TAG_A,
    HTML_TAG_IMG,
    HTML_TAG_TABLE,
    HTML_TAG_TR,
    HTML_TAG_TD,
    HTML_TAG_TH,
    HTML_TAG_UL,
    HTML_TAG_OL,
    HTML_TAG_LI,
    HTML_TAG_PRE,
    HTML_TAG_CODE,
    HTML_TAG_BLOCKQUOTE,
    HTML_TAG_FORM,
    HTML_TAG_INPUT,
    HTML_TAG_BUTTON,
    HTML_TAG_SELECT,
    HTML_TAG_TEXTAREA,
    HTML_TAG_LABEL,
    HTML_TAG_IFRAME,
    HTML_TAG_OBJECT,
    HTML_TAG_PARAM,
    HTML_TAG_EMBED,
    HTML_TAG_VIDEO,
    HTML_TAG_AUDIO,
    HTML_TAG_SOURCE,
    HTML_TAG_CANVAS,
    HTML_TAG_SVG,
    HTML_TAG_HEADER,
    HTML_TAG_FOOTER,
    HTML_TAG_NAV,
    HTML_TAG_MAIN,
    HTML_TAG_ASIDE,
    HTML_TAG_SECTION,
    HTML_TAG_ARTICLE,
    HTML_TAG_FIGURE,
    HTML_TAG_FIGCAPTION,
    HTML_TAG_MARK,
    HTML_TAG_SMALL,
    HTML_TAG_SUB,
    HTML_TAG_SUP,
    HTML_TAG_DEL,
    HTML_TAG_INS,
    HTML_TAG_ABBR,
    HTML_TAG_ACRONYM,
    HTML_TAG_ADDRESS,
    HTML_TAG_BIG,
    HTML_TAG_CITE,
    HTML_TAG_DFN,
    HTML_TAG_KBD,
    HTML_TAG_SAMP,
    HTML_TAG_VAR,
    HTML_TAG_DL,
    HTML_TAG_DT,
    HTML_TAG_DD,
    HTML_TAG_FIELDSET,
    HTML_TAG_LEGEND,
    HTML_TAG_OPTGROUP,
    HTML_TAG_OPTION,
    HTML_TAG_TFOOT,
    HTML_TAG_THEAD,
    HTML_TAG_CAPTION,
    HTML_TAG_COL,
    HTML_TAG_COLGROUP,
    HTML_TAG_DETAILS,
    HTML_TAG_SUMMARY,
    HTML_TAG_MENU,
    HTML_TAG_MENUITEM,
    HTML_TAG_PROGRESS,
    HTML_TAG_METER,
    HTML_TAG_TIME,
    HTML_TAG_DATA,
    HTML_TAG_OUTPUT,
    HTML_TAG_RUBY,
    HTML_TAG_RT,
    HTML_TAG_RP,
    HTML_TAG_BDI,
    HTML_TAG_BDO,
    HTML_TAG_WBR,
    HTML_TAG_TEMPLATE,
    HTML_TAG_SLOT,
    HTML_TAG_MAX
};

// HTML attribute
struct HtmlAttribute {
    String name;
    String value;
};

// HTML token
struct HtmlToken {
    HtmlTokenType type;
    HtmlTagId tag_id;
    String tag_name;
    String data;  // text content or tag name
    List<HtmlAttribute> attributes;
    bool self_closing;

    HtmlToken() : type(HTML_TOKEN_CHARACTER), tag_id(HTML_TAG_UNKNOWN), self_closing(false) {}
};

// HTML node (DOM tree)
struct HtmlNode {
    HtmlTokenType type;
    HtmlTagId tag_id;
    String tag_name;
    String text_content;
    List<HtmlAttribute> attributes;
    List<HtmlNode*> children;
    HtmlNode* parent;

    HtmlNode() : type(HTML_TOKEN_CHARACTER), tag_id(HTML_TAG_UNKNOWN), parent(0) {}
    ~HtmlNode() {
        for (int i = 0; i < children.size(); i++) delete children[i];
    }

    const HtmlAttribute* get_attribute(const char* name) const {
        for (int i = 0; i < attributes.size(); i++) {
            if (attributes[i].name == name) return &attributes[i];
        }
        return 0;
    }

    String get_attribute_value(const char* name, const char* def = "") const {
        const HtmlAttribute* a = get_attribute(name);
        return a ? a->value : String(def);
    }
};

// HTML parser context (based on Ladybird's HTMLParser)
struct HtmlParserContext {
    String input;
    int pos;
    HtmlNode* document;
    List<HtmlNode*> open_elements;

    HtmlParserContext() : pos(0), document(0) {}

    void init(const char* html, int len) {
        input = String(html);
        pos = 0;
        if (document) delete document;
        document = new HtmlNode();
        document->type = HTML_TOKEN_START_TAG;
        document->tag_id = HTML_TAG_HTML;
        open_elements.clear();
        open_elements.push(document);
    }

    ~HtmlParserContext() {
        if (document) delete document;
    }

    char peek(int offset = 0) const {
        int p = pos + offset;
        if (p < 0 || p >= (int)input.len()) return 0;
        return input[p];
    }

    char consume() {
        if (pos >= (int)input.len()) return 0;
        return input[pos++];
    }

    bool match(const char* str) {
        int len = (int)strlen(str);
        if (pos + len > (int)input.len()) return false;
        for (int i = 0; i < len; i++) {
            char c = input[pos + i];
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
            char s = str[i];
            if (s >= 'A' && s <= 'Z') s = s - 'A' + 'a';
            if (c != s) return false;
        }
        return true;
    }

    void advance(int n) { pos += n; }
};

// HTML tokenizer (based on Ladybird's HTMLTokenizer)
class HtmlTokenizer {
public:
    static HtmlTagId tag_id_from_name(const char* name) {
        if (!name || !*name) return HTML_TAG_UNKNOWN;

        // Convert to lowercase and match
        // This is a simplified lookup table
        struct TagMapping { const char* name; HtmlTagId id; };
        static const TagMapping tags[] = {
            { "html", HTML_TAG_HTML },
            { "head", HTML_TAG_HEAD },
            { "body", HTML_TAG_BODY },
            { "title", HTML_TAG_TITLE },
            { "meta", HTML_TAG_META },
            { "link", HTML_TAG_LINK },
            { "style", HTML_TAG_STYLE },
            { "script", HTML_TAG_SCRIPT },
            { "div", HTML_TAG_DIV },
            { "span", HTML_TAG_SPAN },
            { "p", HTML_TAG_P },
            { "h1", HTML_TAG_H1 },
            { "h2", HTML_TAG_H2 },
            { "h3", HTML_TAG_H3 },
            { "h4", HTML_TAG_H4 },
            { "h5", HTML_TAG_H5 },
            { "h6", HTML_TAG_H6 },
            { "br", HTML_TAG_BR },
            { "hr", HTML_TAG_HR },
            { "b", HTML_TAG_B },
            { "strong", HTML_TAG_STRONG },
            { "i", HTML_TAG_I },
            { "em", HTML_TAG_EM },
            { "u", HTML_TAG_U },
            { "a", HTML_TAG_A },
            { "img", HTML_TAG_IMG },
            { "table", HTML_TAG_TABLE },
            { "tr", HTML_TAG_TR },
            { "td", HTML_TAG_TD },
            { "th", HTML_TAG_TH },
            { "ul", HTML_TAG_UL },
            { "ol", HTML_TAG_OL },
            { "li", HTML_TAG_LI },
            { "pre", HTML_TAG_PRE },
            { "code", HTML_TAG_CODE },
            { "blockquote", HTML_TAG_BLOCKQUOTE },
            { "form", HTML_TAG_FORM },
            { "input", HTML_TAG_INPUT },
            { "button", HTML_TAG_BUTTON },
            { "select", HTML_TAG_SELECT },
            { "textarea", HTML_TAG_TEXTAREA },
            { "label", HTML_TAG_LABEL },
            { "iframe", HTML_TAG_IFRAME },
            { "header", HTML_TAG_HEADER },
            { "footer", HTML_TAG_FOOTER },
            { "nav", HTML_TAG_NAV },
            { "main", HTML_TAG_MAIN },
            { "aside", HTML_TAG_ASIDE },
            { "section", HTML_TAG_SECTION },
            { "article", HTML_TAG_ARTICLE },
            { "figure", HTML_TAG_FIGURE },
            { "figcaption", HTML_TAG_FIGCAPTION },
            { "mark", HTML_TAG_MARK },
            { "small", HTML_TAG_SMALL },
            { "sub", HTML_TAG_SUB },
            { "sup", HTML_TAG_SUP },
            { "del", HTML_TAG_DEL },
            { "ins", HTML_TAG_INS },
            { NULL, HTML_TAG_UNKNOWN }
        };

        char lower[64];
        int i = 0;
        while (name[i] && i < 63) {
            char c = name[i];
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
            lower[i] = c;
            i++;
        }
        lower[i] = 0;

        for (int t = 0; tags[t].name; t++) {
            if (strcmp(lower, tags[t].name) == 0) return tags[t].id;
        }
        return HTML_TAG_UNKNOWN;
    }

    static String read_tag_name(HtmlParserContext& ctx) {
        String name;
        while (ctx.pos < (int)ctx.input.len()) {
            char c = ctx.peek();
            if (c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\n' || c == '=') break;
            name += c;
            ctx.consume();
        }
        return name;
    }

    static String read_attribute_name(HtmlParserContext& ctx) {
        String name;
        while (ctx.pos < (int)ctx.input.len()) {
            char c = ctx.peek();
            if (c == '=' || c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\n') break;
            name += c;
            ctx.consume();
        }
        return name;
    }

    static String read_attribute_value(HtmlParserContext& ctx) {
        String value;
        char quote = 0;
        char c = ctx.peek();
        if (c == '"' || c == '\'') {
            quote = c;
            ctx.consume();
        }
        while (ctx.pos < (int)ctx.input.len()) {
            c = ctx.peek();
            if (quote && c == quote) { ctx.consume(); break; }
            if (!quote && (c == ' ' || c == '>' || c == '/')) break;
            value += c;
            ctx.consume();
        }
        return value;
    }

    static HtmlToken* next_token(HtmlParserContext& ctx) {
        if (ctx.pos >= (int)ctx.input.len()) {
            HtmlToken* t = new HtmlToken();
            t->type = HTML_TOKEN_EOF;
            return t;
        }

        char c = ctx.peek();

        // Comment
        if (ctx.match("<!--")) {
            ctx.advance(4);
            HtmlToken* t = new HtmlToken();
            t->type = HTML_TOKEN_COMMENT;
            while (ctx.pos < (int)ctx.input.len()) {
                if (ctx.match("-->")) { ctx.advance(3); break; }
                t->data += ctx.consume();
            }
            return t;
        }

        // DOCTYPE
        if (ctx.match("<!doctype") || ctx.match("<!DOCTYPE")) {
            ctx.advance(9);
            HtmlToken* t = new HtmlToken();
            t->type = HTML_TOKEN_DOCTYPE;
            while (ctx.pos < (int)ctx.input.len() && ctx.peek() != '>') {
                t->data += ctx.consume();
            }
            if (ctx.peek() == '>') ctx.consume();
            return t;
        }

        // Tags
        if (c == '<') {
            ctx.consume();
            bool is_close = (ctx.peek() == '/');
            if (is_close) ctx.consume();

            HtmlToken* t = new HtmlToken();
            t->type = is_close ? HTML_TOKEN_END_TAG : HTML_TOKEN_START_TAG;
            t->tag_name = read_tag_name(ctx);
            t->tag_id = tag_id_from_name(t->tag_name.c_str());

            // Read attributes
            while (ctx.pos < (int)ctx.input.len()) {
                // Skip whitespace
                while (ctx.pos < (int)ctx.input.len() &&
                       (ctx.peek() == ' ' || ctx.peek() == '\t' || ctx.peek() == '\n' || ctx.peek() == '\r')) {
                    ctx.consume();
                }
                if (ctx.peek() == '>') { ctx.consume(); break; }
                if (ctx.peek() == '/') {
                    ctx.consume();
                    t->self_closing = true;
                    if (ctx.peek() == '>') { ctx.consume(); break; }
                    continue;
                }

                HtmlAttribute attr;
                attr.name = read_attribute_name(ctx);
                if (attr.name.empty()) break;

                // Skip whitespace
                while (ctx.pos < (int)ctx.input.len() &&
                       (ctx.peek() == ' ' || ctx.peek() == '\t' || ctx.peek() == '\n')) {
                    ctx.consume();
                }

                if (ctx.peek() == '=') {
                    ctx.consume();
                    // Skip whitespace
                    while (ctx.pos < (int)ctx.input.len() &&
                           (ctx.peek() == ' ' || ctx.peek() == '\t' || ctx.peek() == '\n')) {
                        ctx.consume();
                    }
                    attr.value = read_attribute_value(ctx);
                }
                t->attributes.push(attr);
            }

            return t;
        }

        // Text character
        HtmlToken* t = new HtmlToken();
        t->type = HTML_TOKEN_CHARACTER;
        while (ctx.pos < (int)ctx.input.len() && ctx.peek() != '<') {
            t->data += ctx.consume();
        }
        return t;
    }
};

// HTML tree builder (based on Ladybird's HTMLDocumentParser)
class HtmlTreeBuilder {
public:
    static bool is_void_element(HtmlTagId tag) {
        switch (tag) {
            case HTML_TAG_META:
            case HTML_TAG_LINK:
            case HTML_TAG_BR:
            case HTML_TAG_HR:
            case HTML_TAG_IMG:
            case HTML_TAG_INPUT:
            case HTML_TAG_COL:
                return true;
            default:
                return false;
        }
    }

    static bool is_inline_element(HtmlTagId tag) {
        switch (tag) {
            case HTML_TAG_SPAN:
            case HTML_TAG_B:
            case HTML_TAG_STRONG:
            case HTML_TAG_I:
            case HTML_TAG_EM:
            case HTML_TAG_U:
            case HTML_TAG_A:
            case HTML_TAG_SMALL:
            case HTML_TAG_SUB:
            case HTML_TAG_SUP:
            case HTML_TAG_CODE:
            case HTML_TAG_BR:
                return true;
            default:
                return false;
        }
    }

    static HtmlNode* parse(const char* html, int len) {
        HtmlParserContext ctx;
        ctx.init(html, len);

        while (true) {
            HtmlToken* token = HtmlTokenizer::next_token(ctx);
            if (!token) break;

            if (token->type == HTML_TOKEN_EOF) {
                delete token;
                break;
            }

            if (token->type == HTML_TOKEN_CHARACTER) {
                // Add text node to current element
                HtmlNode* current = ctx.open_elements.empty() ? 0 : ctx.open_elements[ctx.open_elements.size()-1];
                if (current) {
                    HtmlNode* text_node = new HtmlNode();
                    text_node->type = HTML_TOKEN_CHARACTER;
                    text_node->text_content = token->data;
                    text_node->parent = current;
                    current->children.push(text_node);
                }
            } else if (token->type == HTML_TOKEN_START_TAG) {
                HtmlNode* node = new HtmlNode();
                node->type = HTML_TOKEN_START_TAG;
                node->tag_id = token->tag_id;
                node->tag_name = token->tag_name;
                node->attributes = token->attributes;

                HtmlNode* parent = ctx.open_elements.empty() ? 0 : ctx.open_elements[ctx.open_elements.size()-1];
                if (parent) {
                    node->parent = parent;
                    parent->children.push(node);
                }

                // Self-closing or void elements don't go on the stack
                if (!token->self_closing && !is_void_element(token->tag_id)) {
                    ctx.open_elements.push(node);
                }
            } else if (token->type == HTML_TOKEN_END_TAG) {
                // Find matching open element
                for (int i = ctx.open_elements.size() - 1; i >= 0; i--) {
                    if (ctx.open_elements[i]->tag_id == token->tag_id) {
                        // Pop all elements above it
                        while ((int)ctx.open_elements.size() > i) {
                            ctx.open_elements.pop();
                        }
                        break;
                    }
                }
            }

            delete token;
        }

        return ctx.document;
    }
};

// Renderer: walk DOM tree and produce line list (based on Ladybird's LayoutTree)
struct RenderLine {
    String text;
    int font_size;
    bool bold;
    bool italic;
    uint32_t color;
    int indent;
    bool is_link;
    bool is_heading;
    bool is_list_item;
    int form_kind;  // 0=none, 1=input, 2=button
    String form_name;
    String form_value;
    String link_url;

    RenderLine() : font_size(16), bold(false), italic(false), color(0),
                   indent(0), is_link(false), is_heading(false), is_list_item(false),
                   form_kind(0) {}
};

class HtmlRenderer {
public:
    struct RenderState {
        int font_size;
        bool bold;
        bool italic;
        uint32_t color;
        int indent;
        bool in_pre;
        bool in_title;
        String base_url;
        List<RenderLine>& lines;
        String title;
        String current_text;

        RenderState(List<RenderLine>& out) : lines(out), font_size(16), bold(false),
            italic(false), color(0), indent(0), in_pre(false), in_title(false) {}
    };

    static void flush_text(RenderState& state) {
        if (state.current_text.empty()) return;

        RenderLine line;
        line.text = state.current_text;
        line.font_size = state.font_size;
        line.bold = state.bold;
        line.italic = state.italic;
        line.color = state.color;
        line.indent = state.indent;

        state.lines.push(line);
        state.current_text.clear();
    }

    static void render_node(HtmlNode* node, RenderState& state) {
        if (!node) return;

        if (node->type == HTML_TOKEN_CHARACTER) {
            if (state.in_title) {
                state.title += node->text_content;
            } else {
                state.current_text += node->text_content;
            }
            return;
        }

        if (node->type != HTML_TOKEN_START_TAG) return;

        // Save state
        int old_font_size = state.font_size;
        bool old_bold = state.bold;
        bool old_italic = state.italic;
        uint32_t old_color = state.color;
        int old_indent = state.indent;
        bool old_in_pre = state.in_pre;
        bool old_in_title = state.in_title;

        // Apply tag-specific styles
        switch (node->tag_id) {
            case HTML_TAG_H1:
                flush_text(state);
                state.font_size = 32;
                state.bold = true;
                break;
            case HTML_TAG_H2:
                flush_text(state);
                state.font_size = 24;
                state.bold = true;
                break;
            case HTML_TAG_H3:
                flush_text(state);
                state.font_size = 20;
                state.bold = true;
                break;
            case HTML_TAG_H4:
            case HTML_TAG_H5:
            case HTML_TAG_H6:
                flush_text(state);
                state.font_size = 16;
                state.bold = true;
                break;
            case HTML_TAG_B:
            case HTML_TAG_STRONG:
                state.bold = true;
                break;
            case HTML_TAG_I:
            case HTML_TAG_EM:
                state.italic = true;
                break;
            case HTML_TAG_A:
                state.color = 0x0000EE;
                break;
            case HTML_TAG_BR:
                flush_text(state);
                break;
            case HTML_TAG_HR:
                flush_text(state);
                {
                    RenderLine line;
                    line.text = "----------------------------------------";
                    state.lines.push(line);
                }
                break;
            case HTML_TAG_PRE:
                flush_text(state);
                state.in_pre = true;
                state.indent += 2;
                break;
            case HTML_TAG_TITLE:
                state.in_title = true;
                break;
            case HTML_TAG_UL:
            case HTML_TAG_OL:
                state.indent += 2;
                break;
            case HTML_TAG_LI:
                flush_text(state);
                break;
            case HTML_TAG_BLOCKQUOTE:
                flush_text(state);
                state.indent += 4;
                break;
            case HTML_TAG_TABLE:
                flush_text(state);
                break;
            case HTML_TAG_TR:
                flush_text(state);
                break;
            case HTML_TAG_TD:
            case HTML_TAG_TH:
                if (!state.current_text.empty()) {
                    state.current_text += " | ";
                }
                if (node->tag_id == HTML_TAG_TH) state.bold = true;
                break;
            case HTML_TAG_P:
                flush_text(state);
                break;
            default:
                break;
        }

        // Render children
        for (int i = 0; i < node->children.size(); i++) {
            render_node(node->children[i], state);
        }

        // Restore state
        switch (node->tag_id) {
            case HTML_TAG_H1:
            case HTML_TAG_H2:
            case HTML_TAG_H3:
            case HTML_TAG_H4:
            case HTML_TAG_H5:
            case HTML_TAG_H6:
                flush_text(state);
                break;
            case HTML_TAG_A:
                break;
            case HTML_TAG_PRE:
                flush_text(state);
                break;
            case HTML_TAG_TITLE:
                break;
            case HTML_TAG_UL:
            case HTML_TAG_OL:
                break;
            case HTML_TAG_LI:
                break;
            case HTML_TAG_BLOCKQUOTE:
                flush_text(state);
                break;
            case HTML_TAG_TABLE:
                flush_text(state);
                break;
            case HTML_TAG_TR:
                break;
            case HTML_TAG_TD:
            case HTML_TAG_TH:
                break;
            case HTML_TAG_P:
                flush_text(state);
                break;
            default:
                break;
        }

        state.font_size = old_font_size;
        state.bold = old_bold;
        state.italic = old_italic;
        state.color = old_color;
        state.indent = old_indent;
        state.in_pre = old_in_pre;
        state.in_title = old_in_title;
    }

    static void render(const char* html, int len, List<RenderLine>& out, String& title) {
        HtmlNode* root = HtmlTreeBuilder::parse(html, len);
        if (!root) return;

        RenderState state(out);
        render_node(root, state);
        flush_text(state);

        title = state.title;
        delete root;
    }
};

} // namespace nefu
