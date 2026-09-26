# -*- coding: utf-8 -*-
# Resident JS worker: keep quickjs on the load thread so click dispatch
# (event round-trip) reuses the page's DOM shim globals safely.
p = r'D:\mycppos1\nefuOS\core\apps\browser.cpp'
s = open(p, encoding='utf-8').read()

def rep(old, new, must=1):
    global s
    c = s.count(old)
    if must and c != 1:
        raise SystemExit('FAIL count=%d for: %s...' % (c, old[:80]))
    s = s.replace(old, new)

# 1) LoadJob fields
rep('''struct LoadJob {
    volatile int done;   // 0 running, 1 finished''',
'''struct LoadJob {
    volatile int done;   // 0 running, 1 finished
    volatile int committed;  // 1 = main thread has committed this page
    volatile int worker_done; // 1 = worker thread has fully exited
    TabState* tab;''')

# 2) TabState fields
rep('''    bool has_queued;
    uint32_t load_ms;''',
'''    bool has_queued;
    uint32_t load_ms;
    // resident JS worker queue (host): quickjs state stays on the load thread
    volatile int js_ev_pending, js_ev_done, js_ev_close;
    char js_ev_script[512];
    char js_ev_out[4096];
    char* js_ev_html;
    int js_ev_html_sz;
    LoadJob* job_keep;   // finished load job whose worker is still resident''')

rep('''          load_ms(0), css_n(0), img_gen(0), js_html(0), js_html_sz(0),
          pending_nfu(-1), has_pending_nfu(false) {''',
'''          load_ms(0), css_n(0), img_gen(0), js_html(0), js_html_sz(0),
          pending_nfu(-1), has_pending_nfu(false), js_ev_pending(0),
          js_ev_done(0), js_ev_close(0), js_ev_html(0), js_ev_html_sz(0),
          job_keep(0) {''')

# 3) TabState dtor: retire resident worker first, then the in-flight job
rep('''    ~TabState() {
        if (page) kfree(page);
        if (js_html) { bfree(js_html); js_html = 0; }
        layout.runs.erase_all();
        dom.erase_all();
        back.erase_all();
        fwd.erase_all();
        if (job) {
            job->cancelled = true;
            // bounded wait for the worker so we can free it (host thread);
            // on bare the load is synchronous, so it is already done.
            for (int i = 0; i < 60 && !job->done; i++) platform_thread_sleep(5);
            if (job->done) {
                if (job->data) kfree(job->data);
                if (job->dom) delete job->dom;
                delete job;
            }
        }
        clear_imgs(*this);
    }''',
'''    ~TabState() {
        if (page) kfree(page);
        if (js_html) { bfree(js_html); js_html = 0; }
        layout.runs.erase_all();
        dom.erase_all();
        back.erase_all();
        fwd.erase_all();
        LoadJob* jk = job_keep ? job_keep : job;
        if (jk) {
            jk->cancelled = true;
#ifndef NEFU_BARE
            if (jk->tab) jk->tab->js_ev_close = 1;   // let the resident worker exit
            for (int i = 0; i < 400 && !jk->worker_done; i++) platform_thread_sleep(5);
#else
            for (int i = 0; i < 60 && !jk->done; i++) platform_thread_sleep(5);
#endif
            if (jk->data) kfree(jk->data);
            if (jk->dom) delete jk->dom;
            delete jk;
        }
        clear_imgs(*this);
    }''')

# 4) start_job: retire the previous resident worker before a new load
rep('''static void start_job(TabState& tab, const char* url, bool push_hist) {
    if (tab.job) {''',
'''static void start_job(TabState& tab, const char* url, bool push_hist) {
    if (tab.job_keep) {
        // retire the previous page's resident JS worker (host): it must die
        // before a new worker creates a fresh quickjs runtime on ITS thread.
#ifndef NEFU_BARE
        tab.job_keep->tab->js_ev_close = 1;
        for (int i = 0; i < 400 && !tab.job_keep->worker_done; i++) platform_thread_sleep(5);
#endif
        if (tab.job_keep->data) kfree(tab.job_keep->data);
        if (tab.job_keep->dom) delete tab.job_keep->dom;
        delete tab.job_keep;
        tab.job_keep = 0;
        nefu::mini_js_reset();   // drop the old runtime: new page, new globals
    }
    if (tab.job) {''')

rep('''    LoadJob* j = new LoadJob();
    j->done = 0;
    j->ok = false;''',
'''    LoadJob* j = new LoadJob();
    j->done = 0;
    j->committed = 0;
    j->worker_done = 0;
    j->tab = &tab;
    j->ok = false;''')

# 5) load_worker tail: resident event loop on host
rep('''    if (pp.sstream) { bfree(pp.sstream); pp.sstream = 0; }
    j->done = 1;
}''',
'''    if (pp.sstream) { bfree(pp.sstream); pp.sstream = 0; }
    j->done = 1;
#ifndef NEFU_BARE
    // resident JS worker: keep quickjs on THIS thread so event dispatch
    // (click listeners in the DOM shim) safely reuses the page's globals.
    while (!j->tab->js_ev_close) {
        if (j->tab->js_ev_pending) {
            j->tab->js_ev_pending = 0;
            char* h = j->tab->js_ev_html;
            int hsz = j->tab->js_ev_html_sz;
            if (h && hsz > 0) h[0] = 0;
            nefu::mini_js_run(j->tab->js_ev_script, j->tab->js_ev_out,
                              (int)sizeof(j->tab->js_ev_out), h, hsz);
            j->tab->js_ev_done = 1;
        }
        platform_thread_sleep(2);
    }
    j->worker_done = 1;
#endif
}''')

# 6) poll_jobs: mark committed, keep the job for the resident worker
rep('''        if (!j || !j->done) continue;
        tab.job = 0;''',
'''        if (!j || !j->done || j->committed) continue;
        tab.job = 0;
        j->committed = 1;''')

rep('''        if (j->data) kfree(j->data);
        if (j->dom) delete j->dom;
        delete j;
        if (tab.used && tab.has_queued) {''',
'''        if (j->data) kfree(j->data);
        if (j->dom) delete j->dom;
#ifndef NEFU_BARE
        tab.job_keep = j;   // worker stays resident for event dispatch
#else
        delete j;
#endif
        if (tab.used && tab.has_queued) {''')

# 7) on_paint pending block: dispatch on the resident worker (host)
rep('''        char ev[96], out[JS_OUT_MAX];
        out[0] = 0;
        ksprintf(ev, sizeof(ev), "print('evt-chan-ok'); __nfuDispatch(%d,0,0);", tab.pending_nfu);
        nefu::mini_js_set_env(tab.url, tab.title, 0, 0);
        mini_js_run(ev, out, (int)sizeof(out), tab.js_html, tab.js_html_sz);
        klogf("browser: ev out[0..120]=%.120s", out[0] ? out : "-");
        klogf("browser: js_html len=%d", tab.js_html ? (int)strlen(tab.js_html) : -1);''',
'''        char ev[96], out[JS_OUT_MAX];
        out[0] = 0;
        ksprintf(ev, sizeof(ev), "__nfuDispatch(%d,0,0);", tab.pending_nfu);
        nefu::mini_js_set_env(tab.url, tab.title, 0, 0);
#ifndef NEFU_BARE
        if (tab.job_keep && !tab.job_keep->worker_done) {
            clip_str(tab.js_ev_script, sizeof(tab.js_ev_script), ev);
            tab.js_ev_html = tab.js_html;
            tab.js_ev_html_sz = tab.js_html_sz;
            tab.js_ev_out[0] = 0;
            tab.js_ev_done = 0;
            tab.js_ev_pending = 1;
            for (int i = 0; i < 400 && !tab.js_ev_done; i++) platform_thread_sleep(5);
            memcpy(out, tab.js_ev_out, (int)sizeof(out) - 1);
            out[sizeof(out) - 1] = 0;
        }
#else
        mini_js_run(ev, out, (int)sizeof(out), tab.js_html, tab.js_html_sz);
#endif''')

open(p, 'w', encoding='utf-8', newline='').write(s)
print('browser.cpp resident worker patch OK')
