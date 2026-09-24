// nefuOS 系统工具扩展库 —— 进程管理模块实现
#include "process.h"
#include "../klib/klib.h"

namespace nefu {
namespace sysutil {

// 全局进程管理器单例
ProcessManager g_procman;

// ---------------------------------------------------------------------------
ProcessManager::ProcessManager()
    : next_pid_(1), current_(-1), now_ms_(0), policy_(SCHED_POL_RR), quantum_(4), csw_(0) {
    // 手动清零，避免 MinGW -O2 把 memset 优化成死循环（项目已知坑）。
    // procs_ 由 List 默认构造为空，无需额外清零。
    for (int i = 0; i < 64; i++) exit_codes_[i] = 0;
    for (int i = 0; i < 64; i++) { ipc_head_[i]=ipc_tail_[i]=ipc_cnt_[i]=0; }
}

ProcessManager::~ProcessManager() {
    // List 析构会释放内部数组；PCB 里的 String 也会析构。
}

// 分配一个 PID：简单递增，绕开已被终止回收的号段。
int ProcessManager::alloc_pid() {
    return next_pid_++;
}

int ProcessManager::create_process(const char* name, ProcPriority prio,
                                    int ticks, int owner) {
    if (!name) return -1;
    if (ticks < 1) ticks = 1;
    Process p;
    // 手动初始化 POD 字段（不用 memset，规避误优化坑）。
    p.pid = alloc_pid();
    p.name = name;
    p.state = PROC_READY;       // 创建后直接进入就绪队列，立即参与调度
    p.prio = prio;
    p.owner_uid = owner;
    p.arrival_ms = (int)now_ms_;
    p.total_ticks = ticks;
    p.used_ticks = 0;
    p.quantum_left = quantum_;
    p.mem_pages = 4 + (ticks & 0x1F);   // 模拟：任务越大占页越多
    p.cpu_share = 0;
    p.wakeup_tick = 0;
    p.pgid = 0;
    p.rlimit_pages = 0;
    p.nice = 0;
    p.stopped = false;
    // 清空信箱
    p.mbox.head = 0;
    p.mbox.tail = 0;
    p.mbox.count = 0;
    for (int i = 0; i < IPC_MBOX_CAP; i++) {
        p.mbox.ring[i].src_pid = 0;
        p.mbox.ring[i].type = 0;
        p.mbox.ring[i].len = 0;
        // data 不整体清零：len 决定有效区，节省开销。
    }
    procs_.push(p);
    return p.pid;
}

bool ProcessManager::terminate(int pid) {
    Process* p = find_mut(pid);
    if (!p) return false;
    p->state = PROC_TERMINATED;
    if (current_ == pid) current_ = -1;
    clean_terminated();
    return true;
}

void ProcessManager::terminate_current() {
    if (current_ < 0) return;
    Process* p = find_mut(current_);
    if (p) p->state = PROC_TERMINATED;
    current_ = -1;
    clean_terminated();
}

// 回收 TERMINATED 的 PCB：从 List 里删掉。
void ProcessManager::clean_terminated() {
    for (int i = procs_.size() - 1; i >= 0; i--) {
        if (procs_[i].state == PROC_TERMINATED) {
            procs_.remove(i);
        }
    }
}

// ---------------- 调度核心 ----------------
// 推进虚拟时钟 ms 毫秒。这里 1 tick = 10ms，便于在 self_test 里快速跑完。
int ProcessManager::advance(uint32_t ms) {
    // 先把到期的 BLOCKED 进程唤醒
    int steps = (int)(ms / 10);
    if (steps < 1) steps = 1;
    for (int s = 0; s < steps; s++) {
        now_ms_ += 10;
        // 唤醒到期的阻塞进程
        for (int i = 0; i < procs_.size(); i++) {
            Process& p = procs_[i];
            if (p.state == PROC_BLOCKED && p.wakeup_tick != 0 &&
                (int)now_ms_ >= p.wakeup_tick) {
                p.state = PROC_READY;
                p.wakeup_tick = 0;
                p.quantum_left = quantum_;
            }
        }
        // 选一个进程跑 1 tick
        if (current_ < 0 || !find_mut(current_) ||
            find_mut(current_)->state != PROC_RUNNING) {
            current_ = pick_next();
        }
        Process* cur = find_mut(current_);
        if (!cur) continue;     // 没有可运行进程，CPU 空闲
        // 这个进程跑 1 tick
        cur->used_ticks++;
        cur->cpu_share++;
        cur->quantum_left--;
        // 结束条件 1：任务跑完
        if (cur->used_ticks >= cur->total_ticks) {
            cur->state = PROC_TERMINATED;
            current_ = -1;
            clean_terminated();
            continue;
        }
        // 结束条件 2：时间片用完，退回就绪队列
        if (policy_ == SCHED_POL_RR && cur->quantum_left <= 0) {
            cur->state = PROC_READY;
            cur->quantum_left = quantum_;
            current_ = -1;
        }
    }
    return current_;
}

int ProcessManager::pick_next() {
    csw_++;   // 发生一次调度决策 = 一次上下文切换
    switch (policy_) {
    case SCHED_POL_FIFO:   return pick_fifo();
    case SCHED_POL_BATCH:  return pick_batch();
    case SCHED_POL_RR:
    default:           return pick_rr();
    }
}

// 轮转：在所有 READY 进程里选优先级最高的；同优先级选到达最早的。
int ProcessManager::pick_rr() {
    int best = -1;
    for (int i = 0; i < procs_.size(); i++) {
        Process& p = procs_[i];
        if (p.state != PROC_READY) continue;
        if (p.stopped) continue;
        if (best < 0) { best = i; continue; }
        Process& b = procs_[best];
        // 优先级数值越小越高
        if ((int)p.prio < (int)b.prio) { best = i; continue; }
        if ((int)p.prio == (int)b.prio && p.arrival_ms < b.arrival_ms) best = i;
    }
    if (best < 0) return -1;
    procs_[best].state = PROC_RUNNING;
    return procs_[best].pid;
}

// FIFO：选最早进入 READY 的（到达时间最早），不管优先级。
int ProcessManager::pick_fifo() {
    int best = -1;
    for (int i = 0; i < procs_.size(); i++) {
        Process& p = procs_[i];
        if (p.state != PROC_READY) continue;
        if (best < 0) { best = i; continue; }
        if (p.arrival_ms < procs_[best].arrival_ms) best = i;
    }
    if (best < 0) return -1;
    procs_[best].state = PROC_RUNNING;
    return procs_[best].pid;
}

// 批处理：综合优先级(70%)与已运行时间(30%)做权重，偏向低 CPU 占用的高优先级进程。
int ProcessManager::pick_batch() {
    int best = -1;
    int best_score = 0x7FFFFFFF;
    for (int i = 0; i < procs_.size(); i++) {
        Process& p = procs_[i];
        if (p.state != PROC_READY) continue;
        // 分数：优先级越负越小越好；cpu_share 越少越好。
        int score = (int)p.prio * 1000 + p.cpu_share;
        if (score < best_score) { best_score = score; best = i; }
    }
    if (best < 0) return -1;
    procs_[best].state = PROC_RUNNING;
    return procs_[best].pid;
}

void ProcessManager::block_current(int wakeup_ms) {
    if (current_ < 0) return;
    Process* p = find_mut(current_);
    if (!p) return;
    p->state = PROC_BLOCKED;
    p->wakeup_tick = wakeup_ms;
    current_ = -1;
}

bool ProcessManager::wakeup(int pid) {
    Process* p = find_mut(pid);
    if (!p || p->state != PROC_BLOCKED) return false;
    p->state = PROC_READY;
    p->wakeup_tick = 0;
    p->quantum_left = quantum_;
    return true;
}

bool ProcessManager::set_priority(int pid, ProcPriority p) {
    Process* pr = find_mut(pid);
    if (!pr) return false;
    pr->prio = p;
    return true;
}

int ProcessManager::count_state(ProcState s) const {
    int c = 0;
    for (int i = 0; i < procs_.size(); i++)
        if (procs_[i].state == s) c++;
    return c;
}

const Process* ProcessManager::find(int pid) const {
    for (int i = 0; i < procs_.size(); i++)
        if (procs_[i].pid == pid) return &procs_[i];
    return 0;
}

Process* ProcessManager::find_mut(int pid) {
    for (int i = 0; i < procs_.size(); i++)
        if (procs_[i].pid == pid) return &procs_[i];
    return 0;
}

// ---------------- IPC ----------------
bool ProcessManager::send_msg(int dest_pid, int type, const void* data, int len) {
    if (len < 0) len = 0;
    if (len > IPC_MSG_BYTES) len = IPC_MSG_BYTES;
    if (dest_pid == 0) {
        // 广播：发给所有存活进程
        bool any = false;
        for (int i = 0; i < procs_.size(); i++) {
            if (procs_[i].state == PROC_TERMINATED) continue;
            if (procs_[i].pid == (current_ < 0 ? -1 : current_)) continue; // 不回给发送者
            if (send_msg(procs_[i].pid, type, data, len)) any = true;
        }
        return any;
    }
    Process* p = find_mut(dest_pid);
    if (!p) return false;
    if (p->mbox.count >= IPC_MBOX_CAP) return false;   // 信箱满
    IpcMessage& m = p->mbox.ring[p->mbox.tail];
    m.src_pid = current_;
    m.type = type;
    m.len = len;
    for (int i = 0; i < len; i++) m.data[i] = ((const char*)data)[i];
    p->mbox.tail = (p->mbox.tail + 1) % IPC_MBOX_CAP;
    p->mbox.count++;
    return true;
}

bool ProcessManager::recv_msg(int pid, IpcMessage* out) {
    Process* p = find_mut(pid);
    if (!p || p->mbox.count <= 0) return false;
    IpcMessage& m = p->mbox.ring[p->mbox.head];
    if (out) {
        out->src_pid = m.src_pid;
        out->type = m.type;
        out->len = m.len;
        for (int i = 0; i < m.len && i < IPC_MSG_BYTES; i++) out->data[i] = m.data[i];
        for (int i = m.len; i < IPC_MSG_BYTES; i++) out->data[i] = 0;
    }
    p->mbox.head = (p->mbox.head + 1) % IPC_MBOX_CAP;
    p->mbox.count--;
    return true;
}

int ProcessManager::mbox_count(int pid) const {
    const Process* p = find(pid);
    return p ? p->mbox.count : -1;
}

void ProcessManager::describe(int idx, char* buf, int bufsz) const {
    if (idx < 0 || idx >= procs_.size() || !buf || bufsz < 32) {
        if (buf && bufsz > 0) buf[0] = 0;
        return;
    }
    const Process& p = procs_[idx];
    const char* st = "?";
    switch (p.state) {
    case PROC_NEW: st = "NEW"; break;
    case PROC_READY: st = "RDY"; break;
    case PROC_RUNNING: st = "RUN"; break;
    case PROC_BLOCKED: st = "BLK"; break;
    case PROC_TERMINATED: st = "ZOM"; break;
    }
    ksprintf(buf, (size_t)bufsz, "pid=%d %-10s prio=%d %s cpu=%d/%d pages=%d",
             p.pid, p.name.c_str(), (int)p.prio, st,
             p.used_ticks, p.total_ticks, p.mem_pages);
}

// ---------------- 信号 / 进程组 / 资源限制 ----------------
bool ProcessManager::send_signal(int pid, int sig) {
    Process* p = find_mut(pid);
    if (!p) return false;
    switch (sig) {
    case 0:   // KILL：立即终止
        p->state = PROC_TERMINATED;
        if (current_ == pid) current_ = -1;
        clean_terminated();
        break;
    case 1:   // TERM：优雅终止（这里直接等同 KILL）
        p->state = PROC_TERMINATED;
        if (current_ == pid) current_ = -1;
        clean_terminated();
        break;
    case 2:   // STOP：暂停，不再参与调度
        p->stopped = true;
        if (current_ == pid) { p->state = PROC_READY; current_ = -1; }
        break;
    case 3:   // CONT：恢复
        p->stopped = false;
        break;
    default:
        return false;
    }
    return true;
}

int ProcessManager::join_group(int pid, int pgid) {
    Process* p = find_mut(pid);
    if (!p) return -1;
    if (pgid <= 0) pgid = pid;   // 新建组，组长就是自己
    p->pgid = pgid;
    return pgid;
}

int ProcessManager::group_count(int pgid) const {
    int c = 0;
    for (int i = 0; i < procs_.size(); i++)
        if (procs_[i].pgid == pgid) c++;
    return c;
}

bool ProcessManager::signal_group(int pgid, int sig) {
    bool any = false;
    for (int i = 0; i < procs_.size(); i++) {
        if (procs_[i].pgid == pgid) {
            int pid = procs_[i].pid;
            if (send_signal(pid, sig)) any = true;
        }
    }
    return any;
}

bool ProcessManager::set_rlimit_pages(int pid, int max_pages) {
    Process* p = find_mut(pid);
    if (!p) return false;
    p->rlimit_pages = max_pages;
    return true;
}

int ProcessManager::rlimit_pages(int pid) const {
    const Process* p = find(pid);
    return p ? p->rlimit_pages : -1;
}

void ProcessManager::usage_snapshot(int* total_cpu_ticks, int* total_mem_pages) const {
    int cpu = 0, mem = 0;
    for (int i = 0; i < procs_.size(); i++) {
        cpu += procs_[i].used_ticks;
        mem += procs_[i].mem_pages;
    }
    if (total_cpu_ticks) *total_cpu_ticks = cpu;
    if (total_mem_pages) *total_mem_pages = mem;
}
bool ProcessManager::set_nice(int pid, int nice) {
    Process* p = find_mut(pid);
    if (!p) return false;
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;
    p->nice = nice;
    return true;
}

int ProcessManager::nice(int pid) const {
    const Process* p = find(pid);
    return p ? p->nice : 0;
}
int ProcessManager::cpu_time_by_uid(int uid) const {
    int t = 0;
    for (int i = 0; i < procs_.size(); i++)
        if (procs_[i].owner_uid == uid) t += procs_[i].used_ticks;
    return t;
}

void ProcessManager::set_exit_code(int pid, int code) {
    if (pid >= 0 && pid < 64) exit_codes_[pid] = code;
}

int ProcessManager::exit_code(int pid) const {
    if (pid >= 0 && pid < 64) return exit_codes_[pid];
    return -1;
}

int ProcessManager::account_list(ProcAccount* out, int max) const {
    int n = procs_.size() < max ? procs_.size() : max;
    for (int i = 0; i < n; i++) {
        out[i].pid = procs_[i].pid;
        int j = 0;
        const char* nm = procs_[i].name.c_str();
        for (; nm[j] && j < 19; j++) out[i].name[j] = nm[j];
        out[i].name[j] = 0;
        out[i].cpu_ticks = procs_[i].used_ticks;
    }
    return n;
}
bool ProcessManager::ipc_send(int dst_pid, const char* msg, int len) {
    if (dst_pid < 0 || dst_pid >= 64) return false;
    if (!msg) return false;
    if (ipc_cnt_[dst_pid] >= IPC_QUEUE_CAP) return false;   // 队列满
    if (len > IPC_MSG_BYTES - 1) len = IPC_MSG_BYTES - 1;
    if (len < 0) len = 0;
    int slot = ipc_tail_[dst_pid];
    for (int i = 0; i < len; i++) ipc_buf_[dst_pid][slot][i] = msg[i];
    ipc_buf_[dst_pid][slot][len] = 0;   // 保证 NUL 结尾
    ipc_tail_[dst_pid] = (slot + 1) % IPC_QUEUE_CAP;
    ipc_cnt_[dst_pid]++;
    return true;
}

int ProcessManager::ipc_recv(int src_pid, char* out) {
    if (src_pid < 0 || src_pid >= 64) return -1;
    if (!out) return -1;
    if (ipc_cnt_[src_pid] == 0) return -1;
    int slot = ipc_head_[src_pid];
    int i = 0;
    for (; i < IPC_MSG_BYTES - 1; i++) {
        char ch = ipc_buf_[src_pid][slot][i];
        out[i] = ch;
        if (ch == 0) break;
    }
    out[IPC_MSG_BYTES - 1] = 0;
    ipc_head_[src_pid] = (slot + 1) % IPC_QUEUE_CAP;
    ipc_cnt_[src_pid]--;
    int len = 0;
    while (out[len]) len++;
    return len;
}

int ProcessManager::waitpid() {
    for (int i = 0; i < procs_.size(); i++) {
        if (procs_[i].state == PROC_TERMINATED) {
            int pid = procs_[i].pid;
            procs_.remove(i);
            if (current_ == pid) current_ = -1;
            return pid;
        }
    }
    return -1;
}

int ProcessManager::zombie_count() const {
    int n = 0;
    for (int i = 0; i < procs_.size(); i++)
        if (procs_[i].state == PROC_TERMINATED) n++;
    return n;
}
int ProcessManager::ipc_pending(int src_pid) const {
    if (src_pid < 0 || src_pid >= 64) return 0;
    return ipc_cnt_[src_pid];
}
void ProcessManager::ps_header(char* buf, int bufsz) {
    if (!buf || bufsz < 20) { if (buf && bufsz > 0) buf[0] = 0; return; }
    const char* h = "PID  COMMAND       PRIO  STAT  CPU%%";
    int i = 0;
    for (; h[i] && i < bufsz - 1; i++) buf[i] = h[i];
    buf[i] = 0;
}
int ProcessManager::ps_names(const char** out, int max) const {
    int n = procs_.size() < max ? procs_.size() : max;
    for (int i = 0; i < n; i++) out[i] = procs_[i].name.c_str();
    return n;
}
int ProcessManager::mem_used_pages() const {
    int total = 0;
    for (int i = 0; i < procs_.size(); i++)
        if (procs_[i].state != PROC_TERMINATED) total += procs_[i].mem_pages;
    return total;
}
bool ProcessManager::over_rlimit(int pid) const {
    const Process* p = find(pid);
    if (!p) return false;
    if (p->rlimit_pages <= 0) return false;   // 无限制
    return p->mem_pages > p->rlimit_pages;
}
bool ProcessManager::set_rlimit(int pid, int pages) {
    Process* p = find_mut(pid);
    if (!p) return false;
    p->rlimit_pages = pages;
    return true;
}
const char* proc_state_name(ProcState s) {
    switch (s) {
    case PROC_READY:      return "READY";
    case PROC_RUNNING:    return "RUNNING";
    case PROC_BLOCKED:    return "BLOCKED";
    case PROC_TERMINATED: return "ZOMBIE";
    }
    return "?";
}
const char* proc_prio_name(ProcPriority p) {
    switch (p) {
    case PRIO_IDLE:    return "IDLE";
    case PRIO_LOW:     return "LOW";
    case PRIO_NORMAL:   return "NORMAL";
    case PRIO_HIGH:     return "HIGH";
    case PRIO_REALTIME: return "REALTIME";
    }
    return "?";
}
// ---------------- 便捷自由函数 ----------------
int proc_create(const char* name, ProcPriority prio, int ticks, int owner) {
    return g_procman.create_process(name, prio, ticks, owner);
}
bool proc_terminate(int pid) { return g_procman.terminate(pid); }
int  proc_advance(uint32_t ms) { return g_procman.advance(ms); }
bool proc_send(int dest, int type, const void* data, int len) {
    return g_procman.send_msg(dest, type, data, len);
}
bool proc_recv(int pid, IpcMessage* out) { return g_procman.recv_msg(pid, out); }

// ===========================================================================
// 自检：创建 / 调度 / 优先级 / IPC 全链路验证
// ===========================================================================
int ProcessManager::self_test() {
    int fails = 0;
    // 每个用例都先复位管理器：通过清空表来隔离。
    procs_.clear();
    next_pid_ = 1;
    current_ = -1;
    now_ms_ = 0;
    policy_ = SCHED_POL_RR;

    // 用例 1：创建 3 个进程，跑完它们，验证都能终止回收。
    int a = create_process("alpha", PRIO_NORMAL, 5, 0);
    int b = create_process("beta", PRIO_NORMAL, 3, 0);
    int c = create_process("gamma", PRIO_LOW, 2, 0);
    if (a != 1 || b != 2 || c != 3) { fails++; }
    if (count() != 3) fails++;
    // 推进足够多 tick（每个 tick=10ms，5 tick 最多，给 100 tick 余量）
    for (int i = 0; i < 200; i++) advance(10);
    if (count() != 0) fails++;     // 全部跑完应被回收
    if (current_ != -1) fails++;

    // 用例 2：优先级抢占 —— 高优先级进程应立即抢到 CPU 并跑完，低优先级抢不到
    procs_.clear(); next_pid_ = 1; current_ = -1; now_ms_ = 0;
    int lo = create_process("low", PRIO_LOW, 20, 0);
    int hi = create_process("high", PRIO_REALTIME, 1, 0);
    advance(10);
    if (find(hi) != 0) fails++;
    if (find(lo)->used_ticks != 0) fails++;
    advance(10);
    if (current_ != lo) fails++;

    // 用例 3：阻塞 / 唤醒
    procs_.clear(); next_pid_ = 1; current_ = -1; now_ms_ = 0;
    int w = create_process("worker", PRIO_NORMAL, 10, 0);
    advance(10);                  // worker 跑起来
    if (current_ != w) fails++;
    block_current((int)now_ms_ + 50);   // 阻塞 50ms
    if (find(w)->state != PROC_BLOCKED) fails++;
    // 在阻塞到期前推进，不应唤醒
    advance(10);
    if (find(w)->state != PROC_BLOCKED) fails++;
    // 再推进到到期后
    advance(50);
    if (find(w)->state != PROC_READY && find(w)->state != PROC_RUNNING) fails++;

    // 用例 4：IPC 收发
    procs_.clear(); next_pid_ = 1; current_ = -1; now_ms_ = 0;
    int snd = create_process("sender", PRIO_NORMAL, 50, 0);
    int rcv = create_process("receiver", PRIO_NORMAL, 50, 0);
    (void)snd;
    // 模拟发送者就是内核（current_ = snd）
    current_ = snd;
    const char* payload = "hello-pipe";
    bool sent = send_msg(rcv, 42, payload, (int)strlen(payload) + 1);
    if (!sent) fails++;
    if (mbox_count(rcv) != 1) fails++;
    IpcMessage m;
    bool got = recv_msg(rcv, &m);
    if (!got) fails++;
    if (m.type != 42) fails++;
    if (strcmp(m.data, "hello-pipe") != 0) fails++;
    if (mbox_count(rcv) != 0) fails++;

    // 用例 5：信箱满保护
    for (int i = 0; i < IPC_MBOX_CAP + 2; i++) {
        send_msg(rcv, 1, "x", 1);
    }
    if (mbox_count(rcv) > IPC_MBOX_CAP) fails++;   // 不能超过容量

    // 用例 6：set_priority
    if (!set_priority(rcv, PRIO_HIGH)) fails++;
    if (find(rcv)->prio != PRIO_HIGH) fails++;

    // 用例 7：terminate 回收
    if (!terminate(rcv)) fails++;
    if (find(rcv) != 0) fails++;

    // 用例 8：信号 / 进程组 / 资源限制
    procs_.clear(); next_pid_ = 1; current_ = -1; now_ms_ = 0;
    int w1 = create_process("w1", PRIO_NORMAL, 100, 0);
    int w2 = create_process("w2", PRIO_NORMAL, 100, 0);
    int g  = join_group(w1, 0);          // w1 自建组
    join_group(w2, g);                   // w2 加入
    if (group_count(g) != 2) fails++;
    // STOP 暂停 w1，它不应再分到 CPU
    if (!send_signal(w1, 2)) fails++;
    advance(10);
    if (find(w1)->used_ticks != 0) fails++;   // w1 被暂停
    // KILL w2（先终止它，避免它一直占着 CPU）
    send_signal(w2, 0);
    if (find(w2) != 0) fails++;
    if (group_count(g) != 1) fails++;
    // CONT 恢复 w1，再推进让它分到 CPU
    send_signal(w1, 3);
    advance(10);
    // 资源限制
    if (!set_rlimit_pages(w1, 16)) fails++;
    if (rlimit_pages(w1) != 16) fails++;
    // 快照
    int cpu=0, mem=0;
    usage_snapshot(&cpu, &mem);
    if (cpu <= 0) fails++;
    if (mem <= 0) fails++;

    // 上下文切换 / 按 uid 记账 / account_list
    procs_.clear();
    current_ = -1;
    int csw_before = csw_;
    int u1 = create_process("u1task", PRIO_NORMAL, 20, 7);
    advance(30);    // 跑 3 tick，保持存活
    if (cpu_time_by_uid(7) <= 0) fails++;
    if (cpu_time_by_uid(999) != 0) fails++;
    // exit_code 记账（用一个小号 pid 槽位，避免 pid 越界）
    set_exit_code(0, 137);
    if (exit_code(0) != 137) fails++;
    if (exit_code(-1) != -1) fails++;
    if (csw_ <= csw_before) fails++;
    ProcAccount acc[8];
    int an = account_list(acc, 8);
    if (an < 1) fails++;

    // IPC 消息队列
    procs_.clear();
    current_ = -1;
    int pa = create_process("pa", PRIO_NORMAL, 50, 0);
    int pb = create_process("pb", PRIO_NORMAL, 50, 0);
    if (!ipc_send(pb, "hello", 6)) fails++;
    if (!ipc_send(pb, "world", 6)) fails++;
    if (ipc_pending(pb) != 2) fails++;
    char mbuf[128];
    int r1 = ipc_recv(pb, mbuf);
    if (r1 != 5 || mbuf[0] != 'h') fails++;
    int r2 = ipc_recv(pb, mbuf);
    if (r2 != 5 || mbuf[0] != 'w') fails++;
    if (ipc_recv(pb, mbuf) != -1) fails++;   // 空队列
    if (!ipc_send(pa, "x", 2)) fails++;
    if (ipc_pending(pa) != 1) fails++;

    // nice 调整
    if (!set_nice(pa, 5)) fails++;
    if (nice(pa) != 5) fails++;
    if (!set_nice(pa, -99)) fails++;   // 越界裁剪到 -20
    if (nice(pa) != -20) fails++;
    if (nice(9999) != 0) fails++;

    // waitpid / zombie_count（send_signal 会立即回收，故表空时应无 zombie）
    procs_.clear(); current_ = -1;
    if (zombie_count() != 0) fails++;
    if (waitpid() != -1) fails++;
    // ps_header
    char ph[48];
    ProcessManager::ps_header(ph, sizeof(ph));
    if (ph[0] != 'P') fails++;
    create_process("psdemo", PRIO_NORMAL, 50, 0);
    const char* nms[8];
    int pn = ps_names(nms, 8);
    if (pn < 1) fails++;
    if (mem_used_pages() <= 0) fails++;
    if (over_rlimit(9999)) fails++;   // 无此进程
    // set_rlimit 后超过则 over
    int rp = create_process("rlim", PRIO_NORMAL, 10, 0);
    if (!set_rlimit(rp, 2)) fails++;
    if (!over_rlimit(rp)) fails++;

    // 收尾：清空表，避免影响后续调用方（如 smoke 测试）
    procs_.clear();
    current_ = -1;

    if (nefu::strcmp(proc_state_name(PROC_READY), "READY") != 0) fails++;
    if (nefu::strcmp(proc_prio_name(PRIO_HIGH), "HIGH") != 0) fails++;
    return fails;
}

} // namespace sysutil
} // namespace nefu
