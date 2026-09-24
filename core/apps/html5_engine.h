// nefuOS HTML5 Engine - based on Hubbub design (MIT License, NetSurf project)
// Simplified for bare-metal environment
#pragma once
#include "../klib/klib.h"

namespace nefu {

// HTML5 token types
enum HtmlTokenType {
    HTML_TOKEN_DOCTYPE,
    HTML_TOKEN_START_TAG,
    HTML_TOKEN_END_TAG,
    HTML_TOKEN_COMMENT,
    HTML_TOKEN_CHARACTER,
    HTML_TOKEN_EOF
};

// Attribute name-value pair
struct HtmlAttr {
    char name[64];
    char value[256];
};

// HTML5 token
struct HtmlToken {
    HtmlTokenType type;
    char data[512];         // tag name or character data
    bool self_closing;
    HtmlAttr attrs[16];     // up to 16 attributes
    int attr_count;
};

// HTML5 Tree node (DOM node)
struct HtmlNode {
    enum Type {
        NODE_ELEMENT,
        NODE_TEXT,
        NODE_COMMENT,
        NODE_DOCTYPE
    } type;
    
    char tag_name[32];      // for element nodes
    char* text;             // for text nodes
    HtmlAttr attrs[16];
    int attr_count;
    
    HtmlNode* parent;
    HtmlNode* first_child;
    HtmlNode* last_child;
    HtmlNode* next_sibling;
    HtmlNode* prev_sibling;
    
    // Rendering properties
    int font_size;
    bool bold;
    bool italic;
    uint32_t color;
    int indent;
    bool is_block;
    bool is_link;
    char link_url[256];
};

// HTML5 Parser context
struct HtmlParser {
    const char* input;
    int input_len;
    int pos;
    
    // Tokenizer state
    HtmlToken token;
    bool in_tag;
    bool in_attr_name;
    bool in_attr_value;
    
    // Tree builder
    HtmlNode* root;
    HtmlNode* current_node;
    HtmlNode* open_elements[32];  // stack of open elements
    int open_count;
    
    bool in_head;
    bool in_body;
    bool in_title;
    bool in_script;
    bool in_style;
    bool in_textarea;
};

// Initialize HTML parser
void html5_parser_init(HtmlParser* p, const char* input, int len);

// Parse HTML5 document
bool html5_parse(HtmlParser* p);

// Free parser resources
void html5_parser_destroy(HtmlParser* p);

// Walk the DOM tree and generate render lines
void html5_render(HtmlNode* root, class List<struct Line>& out, const char* base_url);

} // namespace nefu
