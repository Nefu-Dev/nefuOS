// core/apps/net_selftest.cpp
//
// Bare-metal network self-test (headless COM1): proves the real update/npkg
// path works on the in-house TCP stack + native TLS 1.3.
//   1) npkg registry over http://  (slowAES challenge + __test cookie solved)
//   2) OTA manifest over https://  (TLS 1.3, NoVerify)
// Runs once at boot under NEFU_BARE; logs everything to klogf (COM1 in QEMU).
#include "../klib/klib.h"
#include "../platform.h"
#include "../net/httpbare.h"
#include "../net/net.h"      // nefu::net_ping (ICMP reachability probe)
#include "npkg.h"

namespace nefu {

// decode UTF-8-ish ASCII only for the one-line intro preview (kept short)
static void netst_print_preview(const uint8_t* p, uint32_t len, int maxch) {
    int printed = 0;
    for (uint32_t i = 0; i < len && printed < maxch; i++) {
        char c = (char)p[i];
        if (c == '\n' || c == '\r') { klogf(" "); continue; }
        if (c < 0x20 || c > 0x7e) continue;      // skip non-ASCII/control bytes
        klogf("%c", c);
        printed++;
    }
}

} // namespace nefu

extern "C" void nefu_net_selftest(void) {
    using namespace nefu;
    klogf("net-selftest: enter\n");

    // ---- 0) ICMP reachability to the real package server ----
    // proves the slirp NAT path forwards outbound traffic (not just gateway)
    uint32_t server_ip = 185u << 24 | 27u << 16 | 134u << 8 | 202u;
    {
        uint32_t t0 = platform_tick_ms();
        bool pong = net_ping(server_ip, 4000);
        klogf("net-selftest: ping server %s (%u ms)\n",
              pong ? "OK" : "FAIL", (unsigned)(platform_tick_ms() - t0));
    }

    // ---- 1) npkg registry (http://npkg.site.je, challenge-aware) ----
    uint8_t* body = 0;
    uint32_t blen = 0;
    bool ok = npkg_gui_fetch_registry(&body, &blen);
    if (ok && body && blen > 0) {
        int n = npkg_gui_registry_count(body, blen);
        klogf("net-selftest: npkg registry OK bytes=%u packages=%d\n",
              (unsigned)blen, n);
        for (int i = 0; i < n && i < 4; i++) {
            char name[64], ver[64], intro[128];
            if (npkg_gui_registry_entry(body, blen, i,
                                        name, sizeof(name),
                                        ver, sizeof(ver),
                                        intro, sizeof(intro)))
                klogf("  pkg[%d] %s v%s\n", i, name, ver);
        }
        kfree(body);
    } else {
        if (body) kfree(body);
        klogf("net-selftest: npkg registry FAIL\n");
    }

    // ---- 2) OTA manifest (https://nefuos.site.je/update.json, TLS 1.3) ----
    int status = 0;
    uint8_t* ub = 0;
    uint32_t ulen = 0;
    ok = net::httpbare_request("GET", "https://nefuos.site.je/update.json",
                               0, 0, 0, &status, &ub, &ulen);
    if (ok && ub && ulen > 0) {
        klogf("net-selftest: update manifest OK status=%d bytes=%u\n",
              status, (unsigned)ulen);
        klogf("net-selftest: manifest preview: ");
        netst_print_preview(ub, ulen, 160);
        klogf("\n");
        kfree(ub);
    } else {
        if (ub) kfree(ub);
        klogf("net-selftest: update manifest FAIL\n");
    }

    klogf("net-selftest: done\n");
}
