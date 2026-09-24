// nefuOS terminal extension commands interface
// Implemented in term_ext.cpp; wired into terminal.cpp dispatch.
#pragma once

namespace nefu {

struct TermState;   // opaque (defined in terminal.cpp)

// print a line to the terminal (implemented in terminal.cpp)
void term_ext_print(TermState* t, const char* s);

// command registry
bool term_ext_handles(const char* cmd);
int  term_ext_count();
const char* term_ext_name(int i);
void term_ext_dispatch(TermState* t, int argc, const char** argv);

} // namespace nefu
