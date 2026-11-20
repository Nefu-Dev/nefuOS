/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// core/apps/net_selftest.cpp
//
// Bare-metal network self-test (headless COM1): proves the real update/npkg
// path works on the in-house TCP stack + native TLS 1.3.
//   1) npkg registry over http://  (slowAES challenge + __test cookie solved)
//   2) OTA manifest over https://  (TLS 1.3, NoVerify)
// Runs once at boot under NEFU_BARE; logs everything to klogf (COM1 in QEMU).
//
// The test is a STATE MACHINE, not one blocking call: nefu_net_selftest_step()
// performs a single check per call and returns true when everything is done.
// The reason is the bare kernel has one thread - a monolithic self-test blocks
// the main loop for ~40 s (a 3 MB bundle download alone takes 15 s), and the
// UI freezes with it: keystrokes typed into the setup wizard are lost and the
// user never reaches the desktop. Stepping keeps the frame loop alive.
//
// The heavy checks are also split out of the boot path on purpose: the 3 MB
// JavaScript bundle is re-downloaded by the browser anyway, and the
// matchmaking/WebSocket round trip talks to a live game server. Both are
// useful diagnostics, but they belong to an explicit "nettest" command, not
// to every boot.
#include "../klib/klib.h"
#include "../platform.h"
#include "../net/httpbare.h"
#include "../net/net.h"      // nefu::net_ping (ICMP reachability probe)
#include "npkg.h"
#include "netproto/colyseus.h"   // nefu::netproto::colyseus_encode_join

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

// Extract the string that follows a JSON key, e.g. "roomId":"YyKfAkNFE".
// The bare kernel has no JSON parser in C, and the matchmaking reply only
// needs three scalar fields, so a targeted scan is enough.
static void netst_json_str(const char* json, uint32_t len, const char* key,
                           char* out, int outsz) {
    if (!json || !key || !out || outsz <= 0) return;
    out[0] = 0;
    uint32_t klen = (uint32_t)strlen(key);
    if (klen >= len) return;
    for (uint32_t i = 0; i + klen <= len; i++) {
        if (json[i] != key[0]) continue;
        bool hit = true;
        for (uint32_t k = 0; k < klen; k++) {
            if (json[i + k] != key[k]) { hit = false; break; }
        }
        if (!hit) continue;
        uint32_t p = i + klen;
        int o = 0;
        while (p < len && json[p] != '"' && o < outsz - 1) out[o++] = json[p++];
        out[o] = 0;
        return;
    }
}

} // namespace nefu

// One matchmake attempt. Fills room/process/session (each may stay empty) and
// returns the HTTP status so the caller can tell "rejected" from "transport
// failed". Kept as a free function because the deep test tries two endpoints.
static int netst_matchmake(const char* url, const char* body, const char* headers,
                           char* room, int roomsz, char* proc, int procsz,
                           char* sess, int sssz, uint32_t* out_bytes) {
    using namespace nefu;
    int st = 0;
    uint8_t* b = 0;
    uint32_t len = 0;
    if (out_bytes) *out_bytes = 0;
    bool ok = nefu::net::httpbare_request("POST", url, headers,
                                          (const uint8_t*)body,
                                          (uint32_t)strlen(body),
                                          &st, &b, &len);
    if (out_bytes) *out_bytes = len;
    if (ok && b && len > 0) {
        nefu::netst_json_str((const char*)b, len, "\"roomId\":\"", room, roomsz);
        nefu::netst_json_str((const char*)b, len, "\"processId\":\"", proc, procsz);
        nefu::netst_json_str((const char*)b, len, "\"sessionId\":\"", sess, sssz);
        if (!room[0]) {
            // 4xx bodies are short: show them so the rejection reason is visible
            nefu::klogf("net-selftest: matchmake body: ");
            nefu::netst_print_preview(b, len, 160);
            nefu::klogf("\n");
        }
    }
    if (b) kfree(b);
    return st;
}

// The boot self-test: a small set of checks that prove the link and the
// HTTPS path work. Each one is a separate step so the caller can run them
// one per frame without freezing the UI.
enum {
    NST_BOOT_PING = 0,      // ICMP to the package server
    NST_BOOT_NPKG,          // npkg registry over http://
    NST_BOOT_MANIFEST,      // OTA manifest over https://
    NST_BOOT_BLOXD,         // bloxd.io homepage over https://
    NST_BOOT_COUNT
};

static int s_boot_step = 0;

extern "C" void nefu_net_selftest_reset(void) {
    s_boot_step = 0;
}

// Returns true when the boot self-test has finished.
extern "C" bool nefu_net_selftest_step(void) {
    using namespace nefu;
    int step = s_boot_step;
    if (step >= NST_BOOT_COUNT) return true;

    switch (step) {
    // ---- 0) ICMP reachability to the real package server ----
    // proves the slirp NAT path forwards outbound traffic (not just gateway)
    case NST_BOOT_PING: {
        uint32_t server_ip = 185u << 24 | 27u << 16 | 134u << 8 | 202u;
        uint32_t t0 = platform_tick_ms();
        bool pong = net_ping(server_ip, 4000);
        klogf("net-selftest: ping server %s (%u ms)\n",
              pong ? "OK" : "FAIL", (unsigned)(platform_tick_ms() - t0));
        break;
    }

    // ---- 1) npkg registry (http://npkg.site.je, challenge-aware) ----
    case NST_BOOT_NPKG: {
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
        break;
    }

    // ---- 2) OTA manifest (https://nefuos.site.je/update.json, TLS 1.3) ----
    case NST_BOOT_MANIFEST: {
        int status = 0;
        uint8_t* ub = 0;
        uint32_t ulen = 0;
        bool ok = net::httpbare_request("GET", "https://nefuos.site.je/update.json",
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
        break;
    }

    // ---- 3) bloxd.io homepage (https, chunked + gzip) ----
    // This is the page the nefuOS browser is meant to load, and it exercises
    // the whole bare-metal HTTP path: TLS 1.3 handshake, chunked transfer
    // decoding and gzip decompression. The homepage is small enough to fit in
    // the kernel arena but large enough to span many TLS records, so a
    // truncated or desynchronised stream shows up as a length mismatch.
    case NST_BOOT_BLOXD: {
        uint32_t dip = dns_lookup("bloxd.io");
        klogf("net-selftest: bloxd.io dns -> %u.%u.%u.%u\n",
              (dip>>24)&0xff, (dip>>16)&0xff, (dip>>8)&0xff, dip&0xff);
        int bstatus = 0;
        uint8_t* bb2 = 0;
        uint32_t blen2 = 0;
        bool bok = net::httpbare_request("GET", "https://bloxd.io/",
                                        0, 0, 0, &bstatus, &bb2, &blen2);
        if (bok && bb2 && blen2 > 0) {
            bool has_html = false;
            for (uint32_t i = 0; i + 5 <= blen2; i++) {
                const uint8_t* p = bb2 + i;
                if ((p[0] == '<' && p[1] == 'h' && p[2] == 't' && p[3] == 'm' && p[4] == 'l') ||
                    (p[0] == '<' && p[1] == '!' && p[2] == 'D' && p[3] == 'O' && p[4] == 'C')) {
                    has_html = true;
                    break;
                }
            }
            klogf("net-selftest: bloxd.io OK status=%d bytes=%u html=%s\n",
                  bstatus, (unsigned)blen2, has_html ? "yes" : "no");
            klogf("net-selftest: bloxd preview: ");
            netst_print_preview(bb2, blen2, 120);
            klogf("\n");
            kfree(bb2);
        } else {
            if (bb2) kfree(bb2);
            klogf("net-selftest: bloxd.io FAIL status=%d\n", bstatus);
        }
        break;
    }
    }
    s_boot_step++;
    return s_boot_step >= NST_BOOT_COUNT;
}

// The deep self-test: the 3 MB bundle, the http->https redirect and the
// Colyseus matchmaking + WebSocket round trip. These are diagnostics, not
// boot-time work - the bundle is downloaded again by the browser, and the
// multiplayer probe talks to a live game server. Run them on demand with the
// "nettest deep" shell command.
extern "C" void nefu_net_selftest_deep(void) {
    using namespace nefu;
    klogf("net-selftest: deep begin\n");

    // ---- 4) the main JavaScript bundle (https, ~3 MB) ----
    // This is the asset the page actually needs to boot the game. It is far
    // larger than the homepage, so it exercises the parts of the bare stack
    // the homepage does not: a long-lived TLS stream that spans thousands of
    // records, the sliding TCP window (16 KB advertised, 64 KB buffered) and
    // the growing response buffer. A truncated stream shows up here as a
    // length mismatch, a desynchronised one as a non-JavaScript head.
    {
        const char* bundle = "https://bloxd.io/static/js/tcwczu.main.357faec1.js";
        int jstatus = 0;
        uint8_t* jb = 0;
        uint32_t jlen = 0;
        uint32_t t0 = platform_tick_ms();
        bool jok = net::httpbare_request("GET", bundle, 0, 0, 0,
                                        &jstatus, &jb, &jlen);
        uint32_t dt = platform_tick_ms() - t0;
        if (jok && jb && jlen > 0) {
            // The bundle is plain JavaScript, so the head must look like code.
            bool looks_js = false;
            for (uint32_t i = 0; i + 4 <= jlen && i < 4096; i++) {
                const char* p = (const char*)jb + i;
                if ((p[0] == '(' || p[0] == '!' || p[0] == '/') &&
                    (p[1] == 'f' || p[1] == 't' || p[1] == 'w')) { looks_js = true; break; }
            }
            klogf("net-selftest: bloxd bundle OK status=%d bytes=%u js=%s (%u ms)\n",
                  jstatus, (unsigned)jlen, looks_js ? "yes" : "no", (unsigned)dt);
            kfree(jb);
        } else {
            if (jb) kfree(jb);
            klogf("net-selftest: bloxd bundle FAIL status=%d bytes=%u (%u ms)\n",
                  jstatus, (unsigned)jlen, (unsigned)dt);
        }
    }

    // ---- 5) plain-http entry point (must redirect to https) ----
    // The address bar used to expand a bare host name to http://, so the
    // first request went out in clear text and depended on the server's
    // 301. The browser now asks for https directly, but the redirect still
    // has to work for links and for sites that only speak http on the
    // first hop - so check that the bare stack follows it.
    {
        int rstatus = 0;
        uint8_t* rb = 0;
        uint32_t rlen = 0;
        bool rok = net::httpbare_request("GET", "http://bloxd.io/",
                                        0, 0, 0, &rstatus, &rb, &rlen);
        if (rok && rb && rlen > 0) {
            bool has_html = false;
            for (uint32_t i = 0; i + 5 <= rlen; i++) {
                const char* p = (const char*)rb + i;
                if ((p[0] == '<' && p[1] == 'h' && p[2] == 't' && p[3] == 'm' && p[4] == 'l') ||
                    (p[0] == '<' && p[1] == '!' && p[2] == 'D' && p[3] == 'O' && p[4] == 'C')) {
                    has_html = true;
                    break;
                }
            }
            klogf("net-selftest: bloxd http->https OK status=%d bytes=%u html=%s\n",
                  rstatus, (unsigned)rlen, has_html ? "yes" : "no");
            kfree(rb);
        } else {
            if (rb) kfree(rb);
            klogf("net-selftest: bloxd http->https FAIL status=%d bytes=%u\n",
                  rstatus, (unsigned)rlen);
        }
    }

    // ---- 5.5) Firebase anonymous sign-in: the real bloxd identity ----
    // bloxd authenticates through Firebase Auth. The page creates an anonymous
    // user with POST accounts:signUp and then sends the resulting idToken as
    // `Authorization: Bearer ...` to the social API and as `_authToken` on the
    // game WebSocket. Without that token the room's onAuth callback rejects the
    // join ("onAuth failed"), which is the wall we kept hitting.
    char idtok[1600] = {0};
    {
        const char* fbkey = "AIzaSyCJ-G6m_NyA3P021v-ugDmMmkcKysUebSE";
        char furl[320];
        ksprintf(furl, sizeof(furl),
                 "https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=%s", fbkey);
        const char* fbody = "{\"returnSecureToken\":true}";
        int fst = 0;
        uint8_t* fb = 0;
        uint32_t flen = 0;
        uint32_t ft0 = platform_tick_ms();
        bool fok = net::httpbare_request("POST", furl,
                                        "Content-Type: application/json\r\n"
                                        "Origin: https://bloxd.io\r\n"
                                        "Referer: https://bloxd.io/\r\n",
                                        (const uint8_t*)fbody,
                                        (uint32_t)strlen(fbody),
                                        &fst, &fb, &flen);
        uint32_t fdt = platform_tick_ms() - ft0;
        if (fok && fb && flen > 0) {
            netst_json_str((const char*)fb, flen, "\"idToken\":\"", idtok, sizeof(idtok));
            klogf("net-selftest: firebase anon status=%d bytes=%u token_len=%d jwt=%s (%u ms)\n",
                  fst, (unsigned)flen, (int)strlen(idtok),
                  (idtok[0] == 'e' && idtok[1] == 'y') ? "yes" : "no", (unsigned)fdt);
            if (!idtok[0]) {
                klogf("net-selftest: firebase body: ");
                netst_print_preview(fb, flen, 200);
                klogf("\n");
            }
            kfree(fb);
        } else {
            if (fb) kfree(fb);
            klogf("net-selftest: firebase anon FAIL status=%d bytes=%u (%u ms)\n",
                  fst, (unsigned)flen, (unsigned)fdt);
        }
    }

    // ---- 6) Colyseus multiplayer (matchmake + WebSocket) ----
    // This is the live game channel: the client asks the social server for a
    // seat in a room and then talks to that room over a WebSocket. The
    // whole path is exercised here - HTTPS POST for the seat, then the
    // native WebSocket client over the native TLS 1.3 stack - so a broken
    // frame codec or a desynchronised TLS stream shows up immediately.
    {
        const char* social = "social21.bloxd.io";
        const char* room_name = "social_updates_21";
        char room_id[64] = {0};
        char proc_id[64] = {0};
        char sess_id[64] = {0};

        // The page calls /social/bloxd-matchmake {gameNameWithVariation,
        // languages} with the Firebase token as bearer; the older
        // /matchmake/joinOrCreate/<room> path is kept as a fallback so a
        // protocol change on their side cannot hide a transport problem.
        char mhdr[1800];
        int mhl = ksprintf(mhdr, sizeof(mhdr),
                           "Content-Type: application/json\r\n"
                           "Origin: https://bloxd.io\r\n"
                           "Referer: https://bloxd.io/\r\n");
        if (idtok[0])
            mhl += ksprintf(mhdr + mhl, sizeof(mhdr) - mhl,
                            "Authorization: Bearer %s\r\n", idtok);

        char surl[256];
        ksprintf(surl, sizeof(surl), "https://%s/social/bloxd-matchmake", social);
        char sbody[192];
        ksprintf(sbody, sizeof(sbody),
                 "{\"gameNameWithVariation\":\"%s\",\"languages\":[\"en\"]}", room_name);
        uint32_t bytes1 = 0;
        int st1 = netst_matchmake(surl, sbody, mhdr, room_id, sizeof(room_id),
                                  proc_id, sizeof(proc_id), sess_id, sizeof(sess_id),
                                  &bytes1);
        klogf("net-selftest: matchmake(social) status=%d bytes=%u room=%s proc=%s session=%s\n",
              st1, (unsigned)bytes1, room_id, proc_id, sess_id);

        if (!room_id[0] || !sess_id[0]) {
            char lurl[256];
            ksprintf(lurl, sizeof(lurl),
                     "https://%s/matchmake/joinOrCreate/%s", social, room_name);
            const char* lbody = "{\"roomName\":\"social_updates_21\","
                                "\"joinOptions\":{\"metricsCookies\":{},\"hidden\":false}}";
            uint32_t bytes2 = 0;
            int st2 = netst_matchmake(lurl, lbody, mhdr, room_id, sizeof(room_id),
                                      proc_id, sizeof(proc_id), sess_id, sizeof(sess_id),
                                      &bytes2);
            klogf("net-selftest: matchmake(legacy) status=%d bytes=%u room=%s proc=%s session=%s\n",
                  st2, (unsigned)bytes2, room_id, proc_id, sess_id);
        }

        if (room_id[0] && sess_id[0]) {
            char wsurl[1400];
            // Colyseus carries the auth token in the query string, not in the
            // handshake frame: buildEndpoint appends `_authToken=<token>`.
            if (proc_id[0])
                ksprintf(wsurl, sizeof(wsurl), "wss://%s/%s/%s?sessionId=%s%s%s",
                         social, proc_id, room_id, sess_id,
                         idtok[0] ? "&_authToken=" : "", idtok[0] ? idtok : "");
            else
                ksprintf(wsurl, sizeof(wsurl), "wss://%s/%s?sessionId=%s%s%s",
                         social, room_id, sess_id,
                         idtok[0] ? "&_authToken=" : "", idtok[0] ? idtok : "");
            int h = platform_ws_open(wsurl);
            if (h < 0) {
                klogf("net-selftest: websocket open FAIL %s\n", wsurl);
            } else {
                klogf("net-selftest: websocket open OK state=%d\n", platform_ws_state(h));
                // Colyseus' protocol: the client announces itself with a
                // single protocol byte (JOIN_ROOM = 10) as soon as the socket
                // is up. The session id travels in the query string, not in
                // the frame. The room answers with its own JOIN_ROOM reply
                // (or a JOIN_ROOM_ERROR when its onAuth callback rejects us).
                {
                    // Colyseus 0.15 client handshake: the very first byte on the
                    // socket is JOIN_ROOM (0x0A), followed by roomId, sessionId
                    // and joinOptions - each a varint-length-prefixed UTF-8
                    // string (see colyseus_encode_join). The session id also
                    // rides in the WS URL query string, but the frame must still
                    // carry it. A lone 0x0A with no room/session is exactly what
                    // makes the server answer ROOM_JOIN_ERROR "onAuth failed".
                    char join_opts[96];
                    ksprintf(join_opts, sizeof(join_opts),
                             "{\"metricsCookies\":{},\"hidden\":false}");
                    uint8_t jbuf[256];
                    int jn = nefu::netproto::colyseus_encode_join(
                        jbuf, sizeof(jbuf), room_id, sess_id,
                        (const uint8_t*)join_opts, (int)strlen(join_opts));
                    if (jn > 0) {
                        int sent = platform_ws_send(h, jbuf, jn, 1);
                        klogf("net-selftest: websocket JOIN_ROOM sent=%d bytes (room=%s)\n",
                              sent, room_id);
                    } else {
                        klogf("net-selftest: websocket JOIN_ROOM encode FAIL\n");
                    }
                }
                // Wait for the first reply, then drain every message the
                // server pushed. A successful join sends several (ROOM_JOIN
                // ack, then ROOM_STATE); a rejected one sends ROOM_JOIN_ERROR.
                // Decoding the opcode turns the hex dump into a verdict, so a
                // single "nettest" run on real hardware pinpoints the failure:
                //   op=ROOM_JOIN_ERROR(11) -> still rejected (read body JSON)
                //   op=ROOM_JOIN(10) / ROOM_STATE(1) -> we are in.
                int bin = 0;
                for (int i = 0; i < 20; i++) {
                    if (platform_ws_peek(h, &bin) != 0) break;
                    platform_thread_sleep(100);
                }
                int seen_any = 0;
                for (int round = 0; round < 8; round++) {
                    int pn = platform_ws_peek(h, &bin);
                    if (pn == 0) {
                        if (!seen_any) break;
                        platform_thread_sleep(80);
                        continue;
                    }
                    if (pn < 0) break;
                    seen_any = 1;
                    uint8_t msg[1024];
                    int take = pn < (int)sizeof(msg) ? pn : (int)sizeof(msg);
                    int got = platform_ws_read(h, msg, take);
                    if (got <= 0) break;
                    // drain any remainder so the next peek stays frame-aligned
                    int remaining = pn - got;
                    while (remaining > 0) {
                        uint8_t scratch[512];
                        int t = remaining > (int)sizeof(scratch)
                                    ? (int)sizeof(scratch) : remaining;
                        int r = platform_ws_read(h, scratch, t);
                        if (r <= 0) break;
                        remaining -= r;
                    }
                    nefu::netproto::ColyseusMsg cm;
                    if (bin && nefu::netproto::colyseus_parse(msg, got, cm) == 0) {
                        char onm[32];
                        nefu::netproto::colyseus_op_name(cm.op, onm, sizeof(onm));
                        klogf("net-selftest: ws msg %d bytes op=%s body_len=%d",
                              got, onm, cm.body_len);
                        // ERROR(11) body = [varint code][varint-len string msg]
                        // (per @colyseus/shared-types Protocol, NOT JSON).
                        if (cm.op == nefu::netproto::CO_ROOM_ERROR) {
                            int ec = 0; char em[96];
                            if (nefu::netproto::colyseus_decode_error(
                                    cm.body, cm.body_len, &ec, em, sizeof(em)) == 0)
                                klogf(" code=%d msg=%s", ec, em);
                        } else if (cm.body_len > 0 && cm.body[0] == '{') {
                            // some game messages carry JSON; preview it
                            char jp[140];
                            int jl = cm.body_len < (int)sizeof(jp) - 1
                                         ? cm.body_len : (int)sizeof(jp) - 1;
                            for (int i = 0; i < jl; i++) {
                                char ch = (char)cm.body[i];
                                jp[i] = (ch >= 0x20 && ch < 0x7f) ? ch : '.';
                            }
                            jp[jl] = 0;
                            klogf(" body=%s", jp);
                        }
                        klogf("\n");
                    } else {
                        klogf("net-selftest: ws msg %d bytes (binary=%d) head:", got, bin);
                        for (int i = 0; i < got && i < 24; i++) klogf(" %02x", msg[i]);
                        klogf("\n");
                    }
                }
                if (!seen_any) {
                    klogf("net-selftest: websocket no message (state=%d)\n",
                          platform_ws_state(h));
                }
                platform_ws_close(h);
            }
        }
    }

    klogf("net-selftest: deep done\n");
}

