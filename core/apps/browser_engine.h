// nefuOS browser engine header
// Lightweight HTML/CSS rendering core inspired by LibHubbub, LibCSS, and Ladybird
#ifndef NEFU_BROWSER_ENGINE_H
#define NEFU_BROWSER_ENGINE_H

#include "../klib/klib.h"

namespace nefu {
namespace browser_eng {

// DOM Node structure (simplified)
struct DOMNode {
    enum Type { ELEMENT, TEXT } type;
    String tag;          // for ELEMENT nodes
    String text;         // for TEXT nodes
    String class_name;
    String id;
    List<struct Attribute> attrs;
    List<DOMNode*> children;
    DOMNode* parent;

    // Computed style
    uint32_t color;
    int font_size;
    bool bold;
    bool italic;
    int display; // 0 block, 1 inline, 2 none

    DOMNode(Type t) : type(t), parent(0), color(0), font_size(16),
                      bold(false), italic(false), display(0) {}
    ~DOMNode() {
        for (int i = 0; i < children.size(); i++) delete children[i];
    }
};

struct Attribute {
    String name;
    String value;
};

// Parse HTML into a DOM tree.
// Returns the root node (caller owns it).
// This is a simplified implementation inspired by LibHubbub's tokenizer
// and Ladybird's DOM tree.
void parse_html_to_dom(const char* html, int len, DOMNode*& out_root);

} // namespace browser_eng
} // namespace nefu

#endif // NEFU_BROWSER_ENGINE_H
