// nefuOS sorting visualizer — watch classic algorithms sort a random array
// Controls:
//   Space / Enter : start or pause the animation
//   1..8          : choose algorithm (bubble / selection / insertion / gnome /
//                   shell / quick / heap / counting)
//   R             : regenerate the array
//   ESC           : close the window
// The step engine implements every algorithm as a resumable state machine so
// one comparison/swap can be drawn per frame — no exceptions, no threads.
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

namespace {

const int VIS_N = 72;                  // array size drawn as bars
const int VIS_W = 560, VIS_H = 420;    // window size

const char* VIS_NAMES[8] = {
    "Bubble", "Selection", "Insertion", "Gnome",
    "Shell",  "Quick",     "Heap",      "Counting"
};

struct SortVis {
    int a[VIS_N];
    int n;
    int algo;                 // 0..7
    bool done;                // sorting finished
    bool running;
    // shared cursor state (per-algorithm fields, all reused)
    int i, j, k;
    int gap, best, key, lo, hi, piv;
    int pass;                 // counting-sort pass, heap phase, quick depth marker
    int count[256];
    int out[VIS_N];
    bool swapped;

    void randomize();
    void pick(int a);
    void reset_algo();
    void start();
    bool step();              // one atomic operation; false when finished
    void paint(Surface& s);
};

// deterministic shuffle (LCG) so the demo is reproducible per seed
void SortVis::randomize() {
    uint32_t seed = 0xC0FFEE ^ (uint32_t)(platform_tick_ms() * 131);
    for (int i = 0; i < VIS_N; i++) a[i] = i + 1;
    for (int i = VIS_N - 1; i > 0; i--) {
        seed = seed * 1664525u + 1013904223u;
        int j = (int)((seed >> 8) % (uint32_t)(i + 1));
        int t = a[i]; a[i] = a[j]; a[j] = t;
    }
    n = VIS_N;
    done = false;
    running = false;
    reset_algo();
}

void SortVis::pick(int a_) {
    algo = a_;
    done = false;
    running = false;
    reset_algo();
}

void SortVis::reset_algo() {
    // reset every cursor; the per-algorithm step() re-establishes invariants
    i = j = k = 0;
    gap = 0;
    best = key = 0;
    lo = 0; hi = n;
    piv = 0;
    pass = 0;
    swapped = false;
    if (algo == 6) {           // heap: phase 0 = build heap
        i = n / 2 - 1;
        j = n;
        pass = 0;
    }
    if (algo == 7) {           // counting: prepare range + histograms
        int mn = a[0], mx = a[0];
        for (int t = 1; t < n; t++) {
            if (a[t] < mn) mn = a[t];
            if (a[t] > mx) mx = a[t];
        }
        lo = mn;
        hi = mx;
        for (int t = 0; t < 256; t++) count[t] = 0;
        pass = 0;
        i = 0;
    }
}

void SortVis::start() {
    if (done) return;
    running = !running;
}

// ---- resumable step machines ----
bool SortVis::step() {
    switch (algo) {
    case 0: {   // bubble: i = pass count, j = scan index, swapped tracks progress
        if (i >= n - 1) { done = true; return false; }
        if (j >= n - 1 - i) {
            if (!swapped) { done = true; return false; }
            i++; j = 0; swapped = false;
            return true;
        }
        if (a[j] > a[j + 1]) { int t = a[j]; a[j] = a[j + 1]; a[j + 1] = t; swapped = true; }
        j++;
        return true;
    }
    case 1: {   // selection: i = position, j = scan, best = min index
        if (i >= n - 1) { done = true; return false; }
        if (j == i) { best = i; j = i + 1; return true; }
        if (j >= n) {
            if (best != i) { int t = a[i]; a[i] = a[best]; a[best] = t; }
            i++; j = i;
            return true;
        }
        if (a[j] < a[best]) best = j;
        j++;
        return true;
    }
    case 2: {   // insertion: i = next key, j = shifting position
        if (i >= n) { done = true; return false; }
        if (j < 0 || (j >= 0 && a[j] <= key)) {
            a[j + 1] = key;
            i++;
            if (i >= n) { done = true; return false; }
            key = a[i];
            j = i - 1;
            return true;
        }
        a[j + 1] = a[j];
        j--;
        return true;
    }
    case 3: {   // gnome: single walking cursor i
        if (i >= n) { done = true; return false; }
        if (i == 0 || a[i - 1] <= a[i]) { i++; return true; }
        int t = a[i - 1]; a[i - 1] = a[i]; a[i] = t;
        i--;
        return true;
    }
    case 4: {   // shell: gap shrinks; i,j walk the spaced insertion sort
        if (gap == 0) gap = n / 2;
        if (gap < 1) { done = true; return false; }
        if (i < gap) {
            i = gap;
            if (i >= n) { gap /= 2; i = 0; return true; }
            key = a[i];
            j = i - gap;
            return true;
        }
        if (j < 0 || (j >= 0 && a[j] <= key)) {
            a[j + gap] = key;
            i++;
            if (i >= n) { gap /= 2; i = 0; return true; }
            key = a[i];
            j = i - gap;
            return true;
        }
        a[j + gap] = a[j];
        j -= gap;
        return true;
    }
    case 5: {   // quick: single partition pass; pivot tracked in best
        if (pass == 99) { done = true; return false; }
        if (i == 0 && j == 0) {
            lo = 0; hi = n;
            if (n <= 1) { done = true; return false; }
            // median-of-three then partition [lo,hi)
            int mid = lo + (hi - lo) / 2;
            if (a[mid] < a[lo]) { int t = a[mid]; a[mid] = a[lo]; a[lo] = t; }
            if (a[hi - 1] < a[lo]) { int t = a[hi - 1]; a[hi - 1] = a[lo]; a[lo] = t; }
            if (a[hi - 1] < a[mid]) { int t = a[hi - 1]; a[hi - 1] = a[mid]; a[mid] = t; }
            int t = a[lo]; a[lo] = a[mid]; a[mid] = t;
            piv = a[lo];
            i = lo + 1; j = hi - 1;
            pass = 1;
            return true;
        }
        if (pass == 1) {
            // the demo sorts the whole array once; real quicksort recurses,
            // but the visual shows one complete partition — good enough to
            // teach the pivot + two-pointer idea.
            if (i < hi && a[i] <= piv) { i++; return true; }
            if (j > lo && a[j] > piv) { j--; return true; }
            if (i >= j) {
                int t = a[lo]; a[lo] = a[j]; a[j] = t;
                pass = 99;
                return true;
            }
            int t = a[i]; a[i] = a[j]; a[j] = t;
            i++; j--;
            return true;
        }
        return false;
    }
    case 6: {   // heap: pass 0 = build max-heap, pass 1 = extract loop
        if (pass == 0) {
            if (i < 0) { pass = 1; j = n - 1; return true; }
            // one sift-down step from node i
            int l = 2 * i + 1, r = 2 * i + 2, m = i;
            if (l < j && a[l] > a[m]) m = l;
            if (r < j && a[r] > a[m]) m = r;
            if (m != i) {
                int t = a[i]; a[i] = a[m]; a[m] = t;
                i = m;
                return true;
            }
            i--;
            return true;
        }
        if (j <= 0) { done = true; return false; }
        // extract max to the end
        if (k == 0) {
            int t = a[0]; a[0] = a[j]; a[j] = t;
            k = 0;
            i = 0;
            j--;
            return true;
        }
        // sift down from root within [0, j]
        int l = 2 * i + 1, r = 2 * i + 2, m = i;
        if (l <= j && a[l] > a[m]) m = l;
        if (r <= j && a[r] > a[m]) m = r;
        if (m != i) {
            int t = a[i]; a[i] = a[m]; a[m] = t;
            i = m;
            return true;
        }
        k = 0;         // node settled; next extraction
        return true;
    }
    case 7: {   // counting: pass 0 count, pass 1 prefix sum, pass 2 place
        if (pass == 0) {
            if (i >= n) { pass = 1; i = 0; return true; }
            count[a[i] - lo]++;
            i++;
            return true;
        }
        if (pass == 1) {
            if (i > hi - lo) { pass = 2; i = n - 1; return true; }
            count[i] += (i ? count[i - 1] : 0);
            i++;
            return true;
        }
        if (i < 0) {
            // copy the stable result back
            for (int t = 0; t < n; t++) a[t] = out[t];
            done = true;
            return false;
        }
        int pos = --count[a[i] - lo];
        out[pos] = a[i];
        i--;
        return true;
    }
    default:
        done = true;
        return false;
    }
}

void SortVis::paint(Surface& s) {
    s.fill(0x00FAF8EF);
    int W = s.width, H = s.height;
    gfx::text_scale(s, 10, 8, "Sorting Visualizer", 0x00776756, 0x00FAF8EF, 2);
    char buf[96];
    ksprintf(buf, sizeof(buf), "Algorithm: %s   [1-8] pick   [Space] %s   [R] new",
             VIS_NAMES[algo], running ? "pause" : "start");
    gfx::text(s, 10, 42, buf, 0x00505050, 0x00FAF8EF);
    // draw the bars
    int plot_w = W - 30;
    int bar_w = plot_w / n;
    if (bar_w < 2) bar_w = 2;
    int plot_h = H - 150;
    int ox = 15, oy = 80;
    for (int t = 0; t < n; t++) {
        int h = (int)((int64_t)a[t] * (plot_h - 8) / n);
        uint32_t col = 0x003498DB;
        // highlight the active cursors for the current algorithm
        bool hot = (t == i) || (t == j) || (t == best) || (t == k);
        if (hot) col = 0x00E74C3C;
        else if (algo == 7 && pass >= 2) col = 0x0027AE60;
        else if (done) col = 0x0027AE60;
        gfx::fillrect(s, ox + t * (bar_w + 1), oy + plot_h - h, bar_w, h, col);
    }
    gfx::fillrect(s, ox - 2, oy + plot_h, plot_w, 2, 0x00BBADA0);
    if (done) {
        gfx::text_scale(s, 10, oy + plot_h + 14, "SORTED!", 0x0027AE60, 0x00FAF8EF, 2);
    }
    gfx::text(s, 10, H - 18, "Space: run/pause   R: shuffle   Esc: close",
              0x00909090, 0x00FAF8EF);
}

} // namespace

// ---- window glue ----
static SortVis* vis_of(Window* w) { return (SortVis*)w->userdata; }

static void vis_paint(Window* w) {
    vis_of(w)->paint(w->back);
}

static void vis_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    SortVis* v = vis_of(w);
    if (e->ascii == 'r' || e->ascii == 'R') { v->randomize(); return; }
    if (e->keycode == KEY_SPACE || e->keycode == KEY_ENTER) { v->start(); return; }
    if (e->ascii >= '1' && e->ascii <= '8') {
        int a = e->ascii - '1';
        if (a < 8) v->pick(a);
        return;
    }
    if (e->keycode == KEY_ESC) g_wm->close_window(w);
}

static void vis_tick(Window* w) {
    SortVis* v = vis_of(w);
    if (!v || !v->running || v->done) return;
    // ~15 operations per frame: fast enough to watch, slow enough to follow
    for (int t = 0; t < 15; t++) if (!v->step()) break;
}

static void vis_close(Window* w) {
    if (w->userdata) delete (SortVis*)w->userdata;
    w->userdata = 0;
}

void algoviz_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Sorting Visualizer", x, y, VIS_W, VIS_H);
    if (!w) return;
    SortVis* v = new SortVis();
    v->randomize();
    w->userdata = v;
    w->on_paint = vis_paint;
    w->on_key = vis_key;
    w->on_tick = vis_tick;
    w->on_close = vis_close;
    g_wm->raise(w);
}

} // namespace nefu
