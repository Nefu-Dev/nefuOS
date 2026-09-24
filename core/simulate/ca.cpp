// nefuOS 仿真引擎库 —— 元胞自动机实现
#include "ca.h"
#include <string.h>

namespace nefu {
namespace simulate {

// 小工具：带边界的坐标访问
static inline int wrap_fixed(int v, int n) { return v < 0 ? -1 : (v >= n ? -1 : v); }
static inline int wrap_torus(int v, int n) {
    if (v < 0) v += n;
    else if (v >= n) v -= n;
    return v;
}

// ============================================================
// ElementaryCA
// ============================================================
void ElementaryCA::init(int w, uint8_t r, Boundary b) {
    width = w > 0 ? w : 64;
    rule = r;
    boundary = b;
    cell = new uint8_t[(size_t)width];
    next = new uint8_t[(size_t)width];
    for (int i = 0; i < width; i++) { cell[i] = 0; next[i] = 0; }
}
void ElementaryCA::shutdown() {
    delete[] cell; cell = 0;
    delete[] next; next = 0;
}
void ElementaryCA::set_cell(int x, uint8_t v) {
    if (x >= 0 && x < width) cell[x] = v ? 1 : 0;
}
uint8_t ElementaryCA::get_cell(int x) const {
    if (x < 0 || x >= width) return 0;
    return cell[x];
}
void ElementaryCA::step() {
    for (int i = 0; i < width; i++) {
        int l, m, r;
        m = cell[i];
        if (boundary == Boundary::Torus) {
            l = cell[wrap_torus(i - 1, width)];
            r = cell[wrap_torus(i + 1, width)];
        } else {
            l = (i > 0) ? cell[i - 1] : 0;
            r = (i < width - 1) ? cell[i + 1] : 0;
        }
        int idx = (l << 2) | (m << 1) | r;   // 0..7
        next[i] = (rule >> idx) & 1;
    }
    uint8_t* t = cell; cell = next; next = t;
}
void ElementaryCA::fill_single_seed() {
    for (int i = 0; i < width; i++) cell[i] = 0;
    cell[width / 2] = 1;
}
int ElementaryCA::count_live() const {
    int c = 0;
    for (int i = 0; i < width; i++) c += cell[i];
    return c;
}

// ============================================================
// GameOfLife
// ============================================================
void GameOfLife::init(int w_, int h_, Boundary b) {
    w = w_; h = h_; boundary = b;
    cell = new uint8_t[(size_t)w * h];
    next = new uint8_t[(size_t)w * h];
    clear();
}
void GameOfLife::shutdown() {
    delete[] cell; cell = 0;
    delete[] next; next = 0;
}
void GameOfLife::clear() {
    for (int i = 0; i < w * h; i++) cell[i] = 0;
}
void GameOfLife::set(int x, int y, uint8_t v) {
    if (x >= 0 && x < w && y >= 0 && y < h) cell[y * w + x] = v ? 1 : 0;
}
uint8_t GameOfLife::get(int x, int y) const {
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    return cell[y * w + x];
}
int GameOfLife::neighbors(int x, int y) const {
    int cnt = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (boundary == Boundary::Torus) {
                nx = wrap_torus(nx, w); ny = wrap_torus(ny, h);
                cnt += cell[ny * w + nx];
            } else {
                if (nx >= 0 && nx < w && ny >= 0 && ny < h)
                    cnt += cell[ny * w + nx];
            }
        }
    return cnt;
}
void GameOfLife::step() {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int n = neighbors(x, y);
            uint8_t c = get(x, y);
            // Life: birth on 3; survive on 2/3
            next[y * w + x] = (n == 3) ? 1 : ((c && n == 2) ? 1 : 0);
        }
    uint8_t* t = cell; cell = next; next = t;
}
int GameOfLife::count_live() const {
    int c = 0;
    for (int i = 0; i < w * h; i++) c += cell[i];
    return c;
}
void GameOfLife::glider() {
    // 经典滑翔机（右上方向）
    int cx = w / 2, cy = h / 2;
    set(cx + 1, cy, 1);
    set(cx + 2, cy + 1, 1);
    set(cx,     cy + 2, 1);
    set(cx + 1, cy + 2, 1);
    set(cx + 2, cy + 2, 1);
}
void GameOfLife::blinker() {
    int cx = w / 2, cy = h / 2;
    // 横向 3 连： (cx-1,cy)(cx,cy)(cx+1,cy)
    set(cx - 1, cy, 1);
    set(cx,     cy, 1);
    set(cx + 1, cy, 1);
}

// ============================================================
// DayNightCA  (B3678/S34678)
// ============================================================
void DayNightCA::init(int w_, int h_, Boundary b) {
    w = w_; h = h_; boundary = b;
    cell = new uint8_t[(size_t)w * h];
    next = new uint8_t[(size_t)w * h];
    for (int i = 0; i < w * h; i++) cell[i] = 0;
}
void DayNightCA::shutdown() {
    delete[] cell; cell = 0;
    delete[] next; next = 0;
}
void DayNightCA::set(int x, int y, uint8_t v) {
    if (x >= 0 && x < w && y >= 0 && y < h) cell[y * w + x] = v ? 1 : 0;
}
uint8_t DayNightCA::get(int x, int y) const {
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    return cell[y * w + x];
}
int DayNightCA::neighbors(int x, int y) const {
    int cnt = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (boundary == Boundary::Torus) {
                nx = wrap_torus(nx, w); ny = wrap_torus(ny, h);
                cnt += cell[ny * w + nx];
            } else {
                if (nx >= 0 && nx < w && ny >= 0 && ny < h)
                    cnt += cell[ny * w + nx];
            }
        }
    return cnt;
}
void DayNightCA::step() {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int n = neighbors(x, y);
            uint8_t c = get(x, y);
            // Day & Night: birth on {3,6,7,8}; survive on {3,4,6,7,8}
            bool born = (n == 3 || n == 6 || n == 7 || n == 8);
            bool surv = (n == 3 || n == 4 || n == 6 || n == 7 || n == 8);
            next[y * w + x] = (c ? (surv ? 1 : 0) : (born ? 1 : 0));
        }
    uint8_t* t = cell; cell = next; next = t;
}
int DayNightCA::count_live() const {
    int c = 0;
    for (int i = 0; i < w * h; i++) c += cell[i];
    return c;
}

// ============================================================
// BrianBrain
// ============================================================
void BrianBrain::init(int w_, int h_, Boundary b) {
    w = w_; h = h_; boundary = b;
    cell = new uint8_t[(size_t)w * h];
    next = new uint8_t[(size_t)w * h];
    for (int i = 0; i < w * h; i++) cell[i] = 0;
}
void BrianBrain::shutdown() {
    delete[] cell; cell = 0;
    delete[] next; next = 0;
}
void BrianBrain::set(int x, int y, uint8_t v) {
    if (x >= 0 && x < w && y >= 0 && y < h) cell[y * w + x] = v;
}
uint8_t BrianBrain::get(int x, int y) const {
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    return cell[y * w + x];
}
int BrianBrain::firing_neighbors(int x, int y) const {
    int cnt = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (boundary == Boundary::Torus) {
                nx = wrap_torus(nx, w); ny = wrap_torus(ny, h);
                if (cell[ny * w + nx] == 1) cnt++;
            } else {
                if (nx >= 0 && nx < w && ny >= 0 && ny < h &&
                    cell[ny * w + nx] == 1) cnt++;
            }
        }
    return cnt;
}
void BrianBrain::step() {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t c = get(x, y);
            uint8_t out;
            if (c == 1) out = 2;              // firing -> refractory
            else if (c == 2) out = 0;         // refractory -> off
            else out = (firing_neighbors(x, y) == 2) ? 1 : 0;  // off
            next[y * w + x] = out;
        }
    uint8_t* t = cell; cell = next; next = t;
}

// ============================================================
// SeedsCA
// ============================================================
void SeedsCA::init(int w_, int h_, Boundary b) {
    w = w_; h = h_; boundary = b;
    cell = new uint8_t[(size_t)w * h];
    next = new uint8_t[(size_t)w * h];
    for (int i = 0; i < w * h; i++) cell[i] = 0;
}
void SeedsCA::shutdown() {
    delete[] cell; cell = 0;
    delete[] next; next = 0;
}
void SeedsCA::set(int x, int y, uint8_t v) {
    if (x >= 0 && x < w && y >= 0 && y < h) cell[y * w + x] = v ? 1 : 0;
}
uint8_t SeedsCA::get(int x, int y) const {
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    return cell[y * w + x];
}
int SeedsCA::alive_neighbors(int x, int y) const {
    int cnt = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (boundary == Boundary::Torus) {
                nx = wrap_torus(nx, w); ny = wrap_torus(ny, h);
                if (cell[ny * w + nx] == 1) cnt++;
            } else {
                if (nx >= 0 && nx < w && ny >= 0 && ny < h &&
                    cell[ny * w + nx] == 1) cnt++;
            }
        }
    return cnt;
}
void SeedsCA::step() {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t c = get(x, y);
            uint8_t out;
            if (c == 1) out = 2;              // alive -> dying
            else if (c == 2) out = 0;         // dying -> dead
            else out = (alive_neighbors(x, y) == 2) ? 1 : 0;
            next[y * w + x] = out;
        }
    uint8_t* t = cell; cell = next; next = t;
}

// ============================================================
// Wireworld
// ============================================================
void Wireworld::init(int w_, int h_, Boundary b) {
    w = w_; h = h_; boundary = b;
    cell = new uint8_t[(size_t)w * h];
    next = new uint8_t[(size_t)w * h];
    for (int i = 0; i < w * h; i++) cell[i] = 0;
}
void Wireworld::shutdown() {
    delete[] cell; cell = 0;
    delete[] next; next = 0;
}
void Wireworld::set(int x, int y, uint8_t v) {
    if (x >= 0 && x < w && y >= 0 && y < h) cell[y * w + x] = v;
}
uint8_t Wireworld::get(int x, int y) const {
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    return cell[y * w + x];
}
int Wireworld::head_neighbors(int x, int y) const {
    int cnt = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (boundary == Boundary::Torus) {
                nx = wrap_torus(nx, w); ny = wrap_torus(ny, h);
                if (cell[ny * w + nx] == 1) cnt++;
            } else {
                if (nx >= 0 && nx < w && ny >= 0 && ny < h &&
                    cell[ny * w + nx] == 1) cnt++;
            }
        }
    return cnt;
}
void Wireworld::step() {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t c = get(x, y);
            uint8_t out;
            if (c == 1) out = 2;              // head -> tail
            else if (c == 2) out = 3;         // tail -> conductor
            else if (c == 3) out = (head_neighbors(x, y) == 1) ? 1 : 3;
            else out = 0;                     // empty
            next[y * w + x] = out;
        }
    uint8_t* t = cell; cell = next; next = t;
}
int Wireworld::count_state(uint8_t s) const {
    int c = 0;
    for (int i = 0; i < w * h; i++) if (cell[i] == s) c++;
    return c;
}

// ============================================================
// LangtonAnt
// ============================================================
void LangtonAnt::init(int w_, int h_, int sx, int sy) {
    w = w_; h = h_;
    cell = new uint8_t[(size_t)w * h];
    for (int i = 0; i < w * h; i++) cell[i] = 0;
    ax = sx; ay = sy; dir = 0; steps_done = 0;
}
void LangtonAnt::shutdown() {
    delete[] cell; cell = 0;
}
void LangtonAnt::step() {
    int idx = ay * w + ax;
    if (cell[idx] == 0) {
        dir = (dir + 1) & 3;        // 白格：右转
        cell[idx] = 1;
    } else {
        dir = (dir + 3) & 3;        // 黑格：左转(-90)
        cell[idx] = 0;
    }
    int dx[4] = { 1, 0, -1, 0 };
    int dy[4] = { 0, 1, 0, -1 };
    ax += dx[dir]; ay += dy[dir];
    if (ax < 0) ax += w; else if (ax >= w) ax -= w;
    if (ay < 0) ay += h; else if (ay >= h) ay -= h;
    steps_done++;
}
uint8_t LangtonAnt::get(int x, int y) const {
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    return cell[y * w + x];
}
int LangtonAnt::colored_count() const {
    int c = 0;
    for (int i = 0; i < w * h; i++) c += cell[i];
    return c;
}

// ============================================================
// HexCA (奇-R 偏移，6 邻居)
// ============================================================
void HexCA::init(int w_, int h_) {
    w = w_; h = h_;
    cell = new uint8_t[(size_t)w * h];
    next = new uint8_t[(size_t)w * h];
    for (int i = 0; i < w * h; i++) cell[i] = 0;
}
void HexCA::shutdown() {
    delete[] cell; cell = 0;
    delete[] next; next = 0;
}
void HexCA::set(int x, int y, uint8_t v) {
    if (x >= 0 && x < w && y >= 0 && y < h) cell[y * w + x] = v ? 1 : 0;
}
uint8_t HexCA::get(int x, int y) const {
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    return cell[y * w + x];
}
int HexCA::neighbors(int x, int y) const {
    // 奇-R 偏移：偶数行邻居略偏左，奇数行略偏右
    // dx 表：对偶数行 (y%2==0)
    static const int odd_dx[6] = { +1,  0, -1,  0, +1, -1 };
    static const int odd_dy[6] = {  0, -1,  0, +1, -1, +1 };
    static const int eve_dx[6] = { +1,  0, -1,  0, -1, +1 };
    static const int eve_dy[6] = {  0, -1,  0, +1, -1, +1 };
    const int* dxs = (y & 1) ? odd_dx : eve_dx;
    const int* dys = (y & 1) ? odd_dy : eve_dy;
    int cnt = 0;
    for (int k = 0; k < 6; k++) {
        int nx = x + dxs[k], ny = y + dys[k];
        if (nx >= 0 && nx < w && ny >= 0 && ny < h)
            cnt += cell[ny * w + nx];
    }
    return cnt;
}
void HexCA::step() {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int n = neighbors(x, y);
            uint8_t c = get(x, y);
            // 六边形 Life：出生 {2}, 存活 {2,3} 简化
            next[y * w + x] = (n == 2) ? 1 : ((c && n == 3) ? 1 : 0);
        }
    uint8_t* t = cell; cell = next; next = t;
}
int HexCA::count_live() const {
    int c = 0;
    for (int i = 0; i < w * h; i++) c += cell[i];
    return c;
}

// ============================================================
// MultiStateCA
// ============================================================
void MultiStateCA::init(int w_, int h_, Boundary b) {
    w = w_; h = h_; boundary = b;
    cell = new uint8_t[(size_t)w * h];
    next = new uint8_t[(size_t)w * h];
    for (int i = 0; i < w * h; i++) cell[i] = 0;
    for (int i = 0; i < 9; i++) { surv[i] = false; birth[i] = false; }
    // 默认 Life: S23/B3
    surv[2] = surv[3] = true; birth[3] = true;
}
void MultiStateCA::shutdown() {
    delete[] cell; cell = 0;
    delete[] next; next = 0;
}
void MultiStateCA::set_rules(const int* sv, int ns, const int* br, int nb) {
    for (int i = 0; i < 9; i++) { surv[i] = false; birth[i] = false; }
    for (int i = 0; i < ns; i++) if (sv[i] >= 0 && sv[i] < 9) surv[sv[i]] = true;
    for (int i = 0; i < nb; i++) if (br[i] >= 0 && br[i] < 9) birth[br[i]] = true;
}
void MultiStateCA::set(int x, int y, uint8_t v) {
    if (x >= 0 && x < w && y >= 0 && y < h) cell[y * w + x] = v ? 1 : 0;
}
uint8_t MultiStateCA::get(int x, int y) const {
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    return cell[y * w + x];
}
int MultiStateCA::neighbors(int x, int y) const {
    int cnt = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (boundary == Boundary::Torus) {
                nx = wrap_torus(nx, w); ny = wrap_torus(ny, h);
                cnt += cell[ny * w + nx];
            } else {
                if (nx >= 0 && nx < w && ny >= 0 && ny < h)
                    cnt += cell[ny * w + nx];
            }
        }
    return cnt;
}
void MultiStateCA::step() {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int n = neighbors(x, y);
            uint8_t c = get(x, y);
            bool out = c ? surv[n] : birth[n];
            next[y * w + x] = out ? 1 : 0;
        }
    uint8_t* t = cell; cell = next; next = t;
}
int MultiStateCA::count_live() const {
    int c = 0;
    for (int i = 0; i < w * h; i++) c += cell[i];
    return c;
}

// 辅助：判断蚂蚁是否出界
static inline bool ant_in_range(const LangtonAnt& a) {
    return a.ax >= 0 && a.ax < a.w && a.ay >= 0 && a.ay < a.h;
}

// ============================================================
// 自检
// ============================================================
int ca_self_test() {
    int fail = 0;

    // --- 1. Rule 90：单种子，第1代应为 ...01010... (对称) ---
    {
        ElementaryCA eca;
        eca.init(31, 90, Boundary::Fixed);
        eca.fill_single_seed();
        eca.step();
        // 中心(15)左边邻居：原中心是1，左边0 -> 模式 010 -> idx=2 -> bit2 of 90
        // 90 = 01011010b：bit2=1, bit1=1, bit3=1, bit5=1, bit7=0...
        // 第1代：中心位置由邻居 (left=0,me=1,right=0) idx=2 -> bit2=0 -> 仍为0?
        // 实际 Rule90 单种子：第1代在中心两侧各出现一个1，中心变0
        // 我们验证：第1代 live 数应为 2（左右各一个）
        if (eca.count_live() != 2) fail++;
        // Rule90 等价 new[i]=left XOR right；第n代是帕斯卡三角 mod 2：
        //   gen1: 2 个; gen2: 2 个(中心互消); gen3: 4 个
        eca.step();  // gen2
        if (eca.count_live() != 2) fail++;
        eca.step();  // gen3
        if (eca.count_live() != 4) fail++;
    }

    // --- 2. Rule 30 不发散/不崩溃：跑10代至少有活细胞 ---
    {
        ElementaryCA eca;
        eca.init(63, 30, Boundary::Fixed);
        eca.fill_single_seed();
        for (int i = 0; i < 10; i++) eca.step();
        if (eca.count_live() < 5) fail++;
    }

    // --- 3. Conway Life：blinker 一代后旋转90° ---
    {
        GameOfLife g;
        g.init(11, 11, Boundary::Fixed);
        g.blinker();   // 横向3连
        g.step();
        // 下一代应为纵向3连，在原中心处
        int cy = 5, cx = 5;
        if (g.get(cx, cy - 1) != 1) fail++;
        if (g.get(cx, cy)     != 1) fail++;
        if (g.get(cx, cy + 1) != 1) fail++;
        if (g.get(cx - 1, cy) != 0) fail++;
        if (g.get(cx + 1, cy) != 0) fail++;
        // 再一代回到横向
        g.step();
        if (g.get(cx - 1, cy) != 1) fail++;
        if (g.get(cx, cy)     != 1) fail++;
        if (g.get(cx + 1, cy) != 1) fail++;
    }

    // --- 4. Life 死寂：孤立细胞一代后消失 ---
    {
        GameOfLife g;
        g.init(7, 7, Boundary::Fixed);
        g.set(3, 3, 1);
        g.step();
        if (g.count_live() != 0) fail++;
    }

    // --- 5. Day&Night 至少能演化 ---
    {
        DayNightCA d;
        d.init(21, 21, Boundary::Fixed);
        d.set(10, 10, 1);
        d.set(11, 10, 1);
        d.set(10, 11, 1);
        d.set(12, 11, 1);
        d.set(11, 12, 1);
        d.step();
        if (d.count_live() == 0) fail++;
    }

    // --- 6. Brian's Brain：firing -> refractory -> off 周期 ---
    {
        BrianBrain b;
        b.init(9, 9, Boundary::Fixed);
        b.set(4, 4, 1);
        b.step();
        if (b.get(4, 4) != 2) fail++;   // firing -> refractory
        b.step();
        if (b.get(4, 4) != 0) fail++;   // refractory -> off
    }

    // --- 7. Seeds：alive -> dying -> dead ---
    {
        SeedsCA s;
        s.init(9, 9, Boundary::Fixed);
        s.set(4, 4, 1);
        s.step();
        if (s.get(4, 4) != 2) fail++;
        s.step();
        if (s.get(4, 4) != 0) fail++;
    }

    // --- 8. Wireworld：head -> tail -> conductor ---
    {
        Wireworld w;
        w.init(9, 9, Boundary::Fixed);
        w.set(4, 4, 1);   // head
        w.set(5, 4, 3);   // conductor
        w.step();
        if (w.get(4, 4) != 2) fail++;   // head -> tail
        w.step();
        if (w.get(4, 4) != 3) fail++;   // tail -> conductor
    }

    // --- 9. Langton's Ant：100步后必然染色若干格，蚂蚁位置合法 ---
    {
        LangtonAnt ant;
        ant.init(41, 41, 20, 20);
        for (int i = 0; i < 100; i++) ant.step();
        if (ant.steps_done != 100) fail++;
        if (ant.colored_count() == 0) fail++;
        if (!ant_in_range(ant)) fail++;
    }

    // --- 10. HexCA：邻居计数为 6 ---
    {
        HexCA h;
        h.init(11, 11);
        h.set(5, 5, 1);
        h.set(4, 5, 1);
        h.set(6, 5, 1);
        int n = h.neighbors(5, 5);
        if (n < 1 || n > 6) fail++;
    }

    // --- 11. MultiStateCA：用自定义规则 B3/S23 等价 Life ---
    {
        MultiStateCA m;
        m.init(11, 11, Boundary::Fixed);
        int sv[2] = { 2, 3 };
        int br[1] = { 3 };
        m.set_rules(sv, 2, br, 1);
        m.set(5, 4, 1); m.set(5, 5, 1); m.set(5, 6, 1);  // 纵向 blinker
        m.step();
        // 纵向 blinker 应变成横向
        if (m.get(4, 5) != 1) fail++;
        if (m.get(5, 5) != 1) fail++;
        if (m.get(6, 5) != 1) fail++;
    }

    return fail;
}

} // namespace simulate
} // namespace nefu
