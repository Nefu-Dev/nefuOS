// nefuOS browser engine - upgraded HTML/CSS rendering core
// Reference: LibHubbub (HTML5 tokenizer), LibCSS (CSS selection), Ladybird (layout)
// This is a lightweight reimplementation, not a direct port.

#include "browser_engine.h"
#include "../klib/klib.h"
#include <cstring>

namespace nefu {
namespace browser_eng {

// =====================================================================
// Token types (LibHubbub-inspired)
// =====================================================================
enum class TokenType {
    Doctype,
    StartTag,
    EndTag,
    Comment,
    Character,
    EOF
};

struct Token {
    TokenType type;
    String tag_name;      // for start/end tags
    List<Attribute> attrs; // for start tags
    String data;          // for character/comment tokens
};

// =====================================================================
// HTML Tokenizer (reference: LibHubbub HTML5 spec)
// =====================================================================
struct Tokenizer {
    const char* html;
    int len;
    int pos;

    Tokenizer(const char* h, int l) : html(h), len(l), pos(0) {}

    static bool is_tag_char(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    static char to_lower(char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
        return c;
    }

    bool next(Token& tok) {
        if (pos >= len) { tok.type = TokenType::EOF; return false; }

        // Skip whitespace between tags
        while (pos < len && (html[pos] == ' ' || html[pos] == '\n' ||
               html[pos] == '\r' || html[pos] == '\t')) {
            pos++;
        }
        if (pos >= len) { tok.type = TokenType::EOF; return false; }

        if (html[pos] == '<') {
            pos++;
            return parse_tag(tok);
        } else {
            // Character token: accumulate until next < or end
            tok.type = TokenType::Character;
            tok.data.clear();
            while (pos < len && html[pos] != '<') {
                tok.data += html[pos++];
            }
            return true;
        }
    }

private:
    bool parse_tag(Token& tok) {
        if (pos >= len) { tok.type = TokenType::EOF; return false; }

        // Comment <!-- -->
        if (pos + 3 < len && html[pos] == '!' && html[pos+1] == '-' && html[pos+2] == '-') {
            pos += 3;
            tok.type = TokenType::Comment;
            tok.data.clear();
            while (pos + 2 < len) {
                if (html[pos] == '-' && html[pos+1] == '-' && html[pos+2] == '>') {
                    pos += 3;
                    return true;
                }
                tok.data += html[pos++];
            }
            pos = len;
            return true;
        }

        // Doctype <!DOCTYPE html>
        if (pos < len && html[pos] == '!') {
            while (pos < len && html[pos] != '>') pos++;
            if (pos < len) pos++;
            tok.type = TokenType::Doctype;
            return true;
        }

        // End tag </div>
        bool is_end = false;
        if (pos < len && html[pos] == '/') {
            is_end = true;
            pos++;
        }

        // Tag name
        char tname[32];
        int tn = 0;
        while (pos < len && tn < 31 && is_tag_char(html[pos])) {
            tname[tn++] = to_lower(html[pos++]);
        }
        tname[tn] = 0;

        tok.type = is_end ? TokenType::EndTag : TokenType::StartTag;
        tok.tag_name = tname;
        tok.attrs.clear();

        // Parse attributes
        while (pos < len && html[pos] != '>') {
            // Skip whitespace and /
            while (pos < len && (html[pos] == ' ' || html[pos] == '\n' ||
                   html[pos] == '\t' || html[pos] == '/')) {
                pos++;
            }
            if (pos >= len || html[pos] == '>') break;

            // Attribute name
            char aname[64];
            int an = 0;
            while (pos < len && an < 63 && html[pos] != '=' &&
                   html[pos] != ' ' && html[pos] != '>' && html[pos] != '/') {
                aname[an++] = to_lower(html[pos++]);
            }
            aname[an] = 0;

            // Skip whitespace
            while (pos < len && (html[pos] == ' ' || html[pos] == '\t')) pos++;

            // Attribute value
            String aval;
            if (pos < len && html[pos] == '=') {
                pos++;
                while (pos < len && (html[pos] == ' ' || html[pos] == '\t')) pos++;
                if (pos < len && (html[pos] == '"' || html[pos] == '\'')) {
                    char quote = html[pos++];
                    while (pos < len && html[pos] != quote) {
                        aval += html[pos++];
                    }
                    if (pos < len) pos++; // skip closing quote
                } else {
                    while (pos < len && html[pos] != ' ' && html[pos] != '>' &&
                           html[pos] != '\n' && html[pos] != '/') {
                        aval += html[pos++];
                    }
                }
            }

            Attribute attr;
            attr.name = aname;
            attr.value = aval;
            tok.attrs.push(attr);
        }

        if (pos < len && html[pos] == '>') pos++;

        return true;
    }
};

// =====================================================================
// CSS Engine (LibCSS-inspired simplified version)
// =====================================================================
struct CSSRule {
    String selector;
    uint32_t color;
    int font_size;
    bool bold;
    bool display_none;

    CSSRule() : color(0), font_size(0), bold(false), display_none(false) {}
};

struct CSSStyleSheet {
    List<CSSRule> rules;

    void parse(const char* css, int len) {
        // Very simple CSS parser: just handle basic selectors and properties
        int i = 0;
        while (i < len) {
            // Skip whitespace
            while (i < len && (css[i] == ' ' || css[i] == '\n' || css[i] == '\t')) i++;
            if (i >= len) break;

            // Find selector
            char sel[64];
            int sn = 0;
            while (i < len && sn < 63 && css[i] != '{' && css[i] != '}') {
                sel[sn++] = css[i++];
            }
            sel[sn] = 0;

            if (i >= len || css[i] != '{') { i++; continue; }
            i++; // skip {

            // Parse declarations
            CSSRule rule;
            rule.selector = sel;

            while (i < len && css[i] != '}') {
                // Property name
                char prop[32];
                int pn = 0;
                while (i < len && pn < 31 && css[i] != ':' && css[i] != ';' &&
                       css[i] != '}' && css[i] != '\n') {
                    prop[pn++] = css[i++];
                }
                prop[pn] = 0;

                // Skip : and whitespace
                while (i < len && (css[i] == ':' || css[i] == ' ' || css[i] == '\t')) i++;

                // Property value
                char val[64];
                int vn = 0;
                while (i < len && vn < 63 && css[i] != ';' && css[i] != '}' && css[i] != '\n') {
                    val[vn++] = css[i++];
                }
                val[vn] = 0;

                // Skip ; and whitespace
                while (i < len && (css[i] == ';' || css[i] == ' ' || css[i] == '\n' ||
                       css[i] == '\t')) i++;

                // Parse common properties (simple string compare)
                if (strcmp(prop, "color") == 0) {
                    if (val[0] == '#') {
                        int r = 0, g = 0, b = 0;
                        if (strlen(val) >= 7) {
                            // Manual hex parsing
                            auto hex2val = [](char c) -> int {
                                if (c >= '0' && c <= '9') return c - '0';
                                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                                return 0;
                            };
                            r = (hex2val(val[1]) << 4) | hex2val(val[2]);
                            g = (hex2val(val[3]) << 4) | hex2val(val[4]);
                            b = (hex2val(val[5]) << 4) | hex2val(val[6]);
                        }
                        rule.color = 0xFF000000 | (r << 16) | (g << 8) | b;
                    } else if (strcmp(val, "red") == 0) rule.color = 0xFF0000;
                    else if (strcmp(val, "blue") == 0) rule.color = 0xFF00;
                    else if (strcmp(val, "green") == 0) rule.color = 0xFF00;
                    else if (strcmp(val, "white") == 0) rule.color = 0xFFFFFF;
                    else if (strcmp(val, "black") == 0) rule.color = 0;
                } else if (strcmp(prop, "font-size") == 0) {
                    int fs = 0;
                    for (int k = 0; k < strlen(val); k++) {
                        if (val[k] >= '0' && val[k] <= '9') {
                            fs = fs * 10 + (val[k] - '0');
                        }
                    }
                    if (fs > 0) rule.font_size = fs;
                } else if (strcmp(prop, "font-weight") == 0) {
                    if (strcmp(val, "bold") == 0 || strcmp(val, "bolder") == 0) rule.bold = true;
                } else if (strcmp(prop, "display") == 0) {
                    if (strcmp(val, "none") == 0) rule.display_none = true;
                }
            }

            if (i < len && css[i] == '}') i++;

            rules.push(rule);
        }
    }

    // Check if a selector matches a node
    bool matches(const String& sel, const DOMNode* node) const {
        if (sel.len() == 0) return false;

        char s0 = sel[0];
        if (s0 == '.') {
            // Class selector: .title
            const char* cls = sel.c_str() + 1;
            return strstr(node->class_name.c_str(), cls) != 0;
        } else if (s0 == '#') {
            // ID selector: #main
            const char* id = sel.c_str() + 1;
            return strcmp(node->id.c_str(), id) == 0;
        } else {
            // Tag selector: div, p, h1
            return strcmp(node->tag.c_str(), sel.c_str()) == 0;
        }
    }

    // Apply rules to a node
    void apply(DOMNode* node) const {
        for (int i = 0; i < rules.size(); i++) {
            if (matches(rules[i].selector, node)) {
                if (rules[i].color) node->color = rules[i].color;
                if (rules[i].font_size) node->font_size = rules[i].font_size;
                if (rules[i].bold) node->bold = true;
                if (rules[i].display_none) node->display = 2;
            }
        }
    }
};

// =====================================================================
// Tree Builder (LibHubbub-inspired)
// =====================================================================
class TreeBuilder {
public:
    DOMNode* root;
    DOMNode* current;
    CSSStyleSheet style;

    TreeBuilder() : root(0), current(0) {}
    ~TreeBuilder() { delete root; }

    void push(Token& tok) {
        switch (tok.type) {
            case TokenType::Doctype:
                if (!root) {
                    root = new DOMNode(DOMNode::ELEMENT);
                    root->tag = "html";
                    current = root;
                }
                break;

            case TokenType::StartTag: {
                DOMNode* el = new DOMNode(DOMNode::ELEMENT);
                el->tag = tok.tag_name;
                el->parent = current;

                // Extract class and id from attributes
                for (int i = 0; i < tok.attrs.size(); i++) {
                    if (strcmp(tok.attrs[i].name.c_str(), "class") == 0) {
                        el->class_name = tok.attrs[i].value;
                    } else if (strcmp(tok.attrs[i].name.c_str(), "id") == 0) {
                        el->id = tok.attrs[i].value;
                    } else {
                        el->attrs.push(tok.attrs[i]);
                    }
                }

                // Apply default styles for common tags
                apply_default_styles(el);

                // Apply CSS rules
                style.apply(el);

                if (current) {
                    current->children.push(el);
                }

                // Void elements don't get added as parent
                if (!is_void_element(tok.tag_name)) {
                    current = el;
                }
                break;
            }

            case TokenType::EndTag: {
                // Find matching open tag and pop up
                DOMNode* node = current;
                while (node && strcmp(node->tag.c_str(), tok.tag_name.c_str()) != 0) {
                    node = node->parent;
                }
                if (node && node->parent) {
                    current = node->parent;
                }
                break;
            }

            case TokenType::Character: {
                if (current && tok.data.len() > 0) {
                    // Merge into previous text node if possible
                    if (current->children.size() > 0 &&
                        current->children[current->children.size()-1]->type == DOMNode::TEXT) {
                        current->children[current->children.size()-1]->text += tok.data;
                    } else {
                        DOMNode* txt = new DOMNode(DOMNode::TEXT);
                        txt->text = tok.data;
                        txt->parent = current;
                        current->children.push(txt);
                    }
                }
                break;
            }

            case TokenType::Comment:
                // Ignore comments
                break;

            case TokenType::EOF:
                break;
        }
    }

private:
    bool is_void_element(const String& tag) const {
        return strcmp(tag.c_str(), "br") == 0 || strcmp(tag.c_str(), "hr") == 0 ||
               strcmp(tag.c_str(), "img") == 0 || strcmp(tag.c_str(), "input") == 0 ||
               strcmp(tag.c_str(), "meta") == 0 || strcmp(tag.c_str(), "link") == 0;
    }

    void apply_default_styles(DOMNode* el) {
        const char* t = el->tag.c_str();
        if (strcmp(t, "h1") == 0) { el->font_size = 32; el->bold = true; }
        else if (strcmp(t, "h2") == 0) { el->font_size = 28; el->bold = true; }
        else if (strcmp(t, "h3") == 0) { el->font_size = 24; el->bold = true; }
        else if (strcmp(t, "h4") == 0) { el->font_size = 20; el->bold = true; }
        else if (strcmp(t, "b") == 0 || strcmp(t, "strong") == 0) { el->bold = true; }
        else if (strcmp(t, "title") == 0) { el->display = 2; } // hidden
    }
};

// =====================================================================
// Public API
// =====================================================================
void parse_html_to_dom(const char* html, int len, DOMNode*& out_root) {
    Tokenizer tok(html, len);
    TreeBuilder builder;

    Token t;
    while (tok.next(t)) {
        builder.push(t);
    }

    out_root = builder.root;
    builder.root = 0; // transfer ownership
}

} // namespace browser_eng
} // namespace nefu
