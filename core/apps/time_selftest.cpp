/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// core/apps/time_selftest.cpp
//
// Boot-time check of the clock stack on the bare-metal backend. The clock has
// pieces that used to disagree with each other, and the failure mode was
// invisible: the on-screen time looked right while every timestamp (logs, HTTP
// Date, TLS certificate validity) was shifted by the zone offset.
//
// Checks, each with a real assertion instead of a reassuring log line:
//   1. gmtime -> localtime -> mktime round-trips back to the same UTC epoch.
//   2. localtime(t) - gmtime(t) equals the configured offset, exactly.
//   3. platform_rtc_date() reports a sane calendar date, and reading it twice
//      never goes backwards (the UIP wait works, registers are not torn).
//   4. platform_seconds_of_day() (what the clock face draws) agrees with that
//      date - the old bug was that these two came from separate decoders.
//   5. face == (time() + offset) mod 86400, and the absolute UTC epoch is
//      printed so a self-consistent but zone-shifted clock cannot hide.
//   6. the browser's Date (QuickJS) agrees: getTimezoneOffset, Date.parse of
//      zone-less and Z strings, toISOString, getHours vs getUTCHours.
//
// NEFU_BARE only: the host build links the real CRT and Windows owns the zone.
#include "../klib/klib.h"
#include "../platform.h"
#include "minijs.h"              // mini_js_run: the browser's own JS engine
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifdef NEFU_BARE

namespace nefu {

extern "C" void nefu_time_selftest(void) {
    int tz = platform_tz_offset_minutes();
    klogf("[TIME] selftest enter (tz=%d min, rtc=%s)\n", tz,
          platform_rtc_is_utc() ? "utc" : "local");

    // ---- 1 + 2: the libc contract ------------------------------------
    // A fixed instant so the arithmetic is unambiguous regardless of today.
    time_t t0 = (time_t)1700000000;              // 2023-11-14 22:13:20 UTC
    struct tm* g = gmtime(&t0);
    if (!g) { klogf("[TIME] FAIL gmtime returned null\n"); return; }
    int gh = g->tm_hour;
    int gday = g->tm_mday;

    struct tm* l = localtime(&t0);
    if (!l) { klogf("[TIME] FAIL localtime returned null\n"); return; }
    // Hour difference folded into -12..+12, so a date wrap cannot hide a bug.
    int hours = l->tm_hour - gh;
    if (l->tm_mday != gday) hours += (l->tm_mday > gday) ? 24 : -24;
    if (hours > 12) hours -= 24;
    if (hours < -12) hours += 24;
    if (hours * 60 != tz) {
        klogf("[TIME] FAIL local-utc=%d min, expected %d\n", hours * 60, tz);
        return;
    }
    klogf("[TIME] local-utc=%d min (localtime and the offset agree)\n", hours * 60);

    time_t back = mktime(l);
    if (back != t0) {
        klogf("[TIME] FAIL round trip %lld -> %lld (mktime/localtime disagree)\n",
              (long long)t0, (long long)back);
        return;
    }
    klogf("[TIME] gmtime -> localtime -> mktime round trip ok\n");

    // ---- 1b: absolute UTC, tied to the screen clock ------------------
    // Two paths can be shifted by the same hours and still agree with each
    // other, so do both:
    //   (a) assert the identity the taskbar depends on,
    //       face == (epoch + offset) mod 86400  - this ties time() to the pixels;
    //   (b) print the absolute epoch, which is the only value that can catch
    //       "self-consistent but off by the zone".
    // QEMU default (-rtc base=utc) leaves the guest chip on UTC; a Windows
    // dual-boot box writes local time into it. The identity holds either way;
    // the absolute epoch only matches a real clock when the rtc= setting
    // matches the machine, so print it and compare by hand.
    time_t now = time((time_t*)0);
    int64_t sod = (int64_t)now % 86400;
    if (sod < 0) sod += 86400;
    int64_t want_face = (sod + (int64_t)tz * 60) % 86400;
    if (want_face < 0) want_face += 86400;
    uint32_t face_now = platform_seconds_of_day();
    if (want_face != (int64_t)face_now) {
        klogf("[TIME] FAIL face %u s-of-day, expected %lld (utc sod %lld, tz %d)\n",
              (unsigned)face_now, (long long)want_face, (long long)sod, tz);
        return;
    }
    klogf("[TIME] utc epoch %lld (utc sod %lld)\n", (long long)now, (long long)sod);
    klogf("[TIME] face %u s-of-day == utc sod + tz*60 (utc %d:%d, local %d:%d)\n",
          (unsigned)face_now, (int)(sod / 3600), (int)((sod / 60) % 60),
          (int)(want_face / 3600), (int)((want_face / 60) % 60));

    // ---- 3: the hardware side ---------------------------------------
    DateInfo a, b;
    if (!platform_rtc_date(&a)) { klogf("[TIME] FAIL RTC unreadable\n"); return; }
    if (a.year < 2000 || a.year > 2199 || a.month < 1 || a.month > 12 ||
        a.day < 1 || a.day > 31 || a.hour > 23 || a.min > 59 || a.sec > 59) {
        klogf("[TIME] FAIL RTC nonsense %d-%d-%d %d:%d:%d\n",
              a.year, a.month, a.day, a.hour, a.min, a.sec);
        return;
    }
    if (!platform_rtc_date(&b)) { klogf("[TIME] FAIL RTC unreadable on 2nd read\n"); return; }
    int64_t ea = (int64_t)a.hour * 3600 + a.min * 60 + a.sec;
    int64_t eb = (int64_t)b.hour * 3600 + b.min * 60 + b.sec;
    int64_t dsec = eb - ea;
    if (dsec < 0) dsec += 86400;                 // midnight rollover is legal
    if (dsec > 5) {
        klogf("[TIME] FAIL RTC jumped %lld s between two reads\n", (long long)dsec);
        return;
    }
    klogf("[TIME] rtc %d-%d-%d %d:%d:%d (monotonic, sane)\n",
          a.year, a.month, a.day, a.hour, a.min, a.sec);

    // ---- 4: the two public paths must agree -------------------------
    // The taskbar draws platform_seconds_of_day(); `date` prints the DateInfo
    // above. A gap between them is exactly the "clock looks fine, logs lie"
    // bug this selftest exists to catch.
    // The comparison has to know what the chip stores: with rtc=local the face
    // *is* the RTC wall clock, with rtc=utc the face is that clock plus the
    // offset. Checking only the first case made the utc convention fail a
    // correct boot.
    uint32_t face = platform_seconds_of_day();
    int64_t rtc_sod = (int64_t)b.hour * 3600 + b.min * 60 + b.sec;
    int64_t want = rtc_sod + (platform_rtc_is_utc() ? (int64_t)tz * 60 : 0);
    want %= 86400;
    if (want < 0) want += 86400;
    int64_t gap = (int64_t)face - want;
    if (gap < 0) gap = -gap;
    if (gap > 86400 / 2) gap = 86400 - gap;
    if (gap > 5) {
        klogf("[TIME] FAIL clock face %d:%d vs rtc %d:%d %s (expected %d:%d, %lld s apart)\n",
              (int)((face / 3600) % 24u), (int)((face / 60) % 60u),
              b.hour, b.min, platform_rtc_is_utc() ? "utc" : "local",
              (int)(want / 3600), (int)((want / 60) % 60), (long long)gap);
        return;
    }
    klogf("[TIME] clock face %d:%d matches the RTC (%s)\n",
          (int)((face / 3600) % 24u), (int)((face / 60) % 60u),
          platform_rtc_is_utc() ? "chip stores UTC" : "chip stores local time");

    // ---- 5: the browser engine sees the same clock -------------------
    // The browser's JavaScript Date is QuickJS, and this target builds QuickJS
    // with _WIN32, so its zone offset comes from the very same trio proven
    // above:  offset = (mktime(gmtime(t)) - mktime(localtime(t))) / 60.
    // Reasoning about that is not proof, so run real JS through the real engine
    // and assert inside it. A page must not see UTC while the taskbar shows
    // local time - that was the browser-side symptom of the old split clock.
    {
        char script[1400];
        char jout[512], jhtml[64];
        ksprintf(script, sizeof(script),
            "var bad=[];"
            "var TZ=%d, EXP=%lld;"
            "var off=new Date().getTimezoneOffset();"
            "if(off!==-TZ) bad.push('offset='+off);"
            "var A=Date.parse('2026-11-06T02:34:33Z');"
            "if(A!==1793932473000) bad.push('parseZ='+A);"
            "var B=Date.parse('2026-11-06T10:34:33');"
            "if(B!==EXP) bad.push('parseLocal='+B);"
            "var d=new Date(1793932473000);"
            "if(d.toISOString().indexOf('2026-11-06T02:34:33')!==0) bad.push('iso='+d.toISOString());"
            "var dm=(d.getHours()*60+d.getMinutes())-(d.getUTCHours()*60+d.getUTCMinutes());"
            "dm=dm-1440*Math.floor(dm/1440);"
            "if(dm!==TZ) bad.push('localMinusUtc='+dm);"
            // Intl used to be a stub returning '' - a page asking for a date
            // rendered nothing, which reads exactly like a dead clock.
            "var p2=function(n){ n=''+n; return n.length<2?'0'+n:n; };"
            "var hh=p2(d.getHours()), mi=p2(d.getMinutes()), se=p2(d.getSeconds());"
            "var df=new Intl.DateTimeFormat('zh-CN',{year:'numeric',month:'numeric',day:'numeric',hour:'2-digit',minute:'2-digit',second:'2-digit'});"
            "var fs=df.format(d);"
            "if(fs.indexOf('2026-11-06')<0) bad.push('intlDate='+fs);"
            "if(fs.indexOf(hh+':'+mi+':'+se)<0) bad.push('intlTime='+fs);"
            "var nf=new Intl.NumberFormat('zh-CN').format(1234567);"
            "if(nf!=='1,234,567') bad.push('numFmt='+nf);"
            "var ls=d.toLocaleString();"
            "if(!ls||ls.length<10) bad.push('toLocaleString='+ls);"
            "print(bad.length?('[JS] zone FAIL '+bad.join(' ')):('[JS] zone ok offset=-'+TZ+' parseZ=1793932473000 parseLocal='+EXP+' iso='+d.toISOString()+' intl='+fs+' num='+nf+' locale='+ls));",
            tz, (long long)1793961273000LL - (long long)tz * 60000LL);
        jout[0] = 0;
        jhtml[0] = 0;
        int rc = mini_js_run(script, jout, (int)sizeof(jout), jhtml, (int)sizeof(jhtml));
        if (rc != 0 || !jout[0]) {
            klogf("[JS] zone FAIL engine rc=%d out=%s\n", rc, jout[0] ? jout : "(empty)");
            klogf("[TIME] SELFTEST FAIL (browser Date probe did not run)\n");
            return;
        }
        klogf("%s", jout);              // print() output already ends in \n
        if (strstr(jout, "FAIL")) {
            klogf("[TIME] SELFTEST FAIL (browser Date disagrees with the clock)\n");
            return;
        }
        mini_js_reset();                // leave no runtime behind for the browser app
    }

    klogf("[TIME] SELFTEST PASS\n");
}

} // namespace nefu

#endif // NEFU_BARE
