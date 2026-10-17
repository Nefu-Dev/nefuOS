// =============================================================================
//  npkg.h — nefuOS npkg command-line package manager
// -----------------------------------------------------------------------------
//  npkg install <pkg>            download & install an npkg package
//  npkg update [server_url]      refresh package.json from the npkg server
//  npkg remove <pkg>             uninstall a package
//  npkg start <pkg>[/<script>] [args...]   run a package entry (.ncode)
//  npkg list                     show installed packages + registry
//
//  Server layout: package.json lives at <origin>/package.json; each package's
//  files live under <origin>/<rel>/ where rel is the "server_in" value with
//  everything up to (and including) "htdocs/" truncated.
// =============================================================================
#pragma once
#include <stdint.h>

namespace nefu {

struct TermState;   // opaque (defined in terminal.cpp)

void npkg_cmd(TermState* t, int argc, const char** argv);

// ---- GUI store integration (no TermState) ----
// Fetch the registry JSON from npkg.site.je (challenge-aware).  Caller kfree()s.
bool npkg_gui_fetch_registry(uint8_t** body, uint32_t* blen);
// Number of packages in a fetched registry.
int  npkg_gui_registry_count(const uint8_t* body, uint32_t blen);
// Fill entry #idx fields: name / version / Introduction (bounded copies).
bool npkg_gui_registry_entry(const uint8_t* body, uint32_t blen, int idx,
                             char* name, int ncap, char* version, int vcap,
                             char* intro, int icap);
// Download + install a registry package.  A .deb server path goes through the
// nefud .deb chain (install + run); ncode packages land in /pkg/<name>.
bool npkg_gui_install(const char* name, char* msg, int mcap);

} // namespace nefu
