// nefuOS 系统工具扩展库 —— 进程管理模块
// 纯软件模拟的进程表 / 创建 / 终止 / 调度 / 优先级 / 进程间通信(IPC)。
// 不依赖真实硬件与线程：调度器是一个可单步推进的状态机，时间由虚拟时钟驱动，
// 便于在宿主(Win32)裸跑测试，也便于在裸机环境里把真实 tick 注入进来。
//
// 设计约束：无 STL 容器 / 无异常 / 无 RTTI / 不直接 malloc；
// 动态对象一律走 new[]/delete[] 或 nefu::List / nefu::String。
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace sysutil {

// ---------------- 进程状态 ----------------
// 一个进程的生命周期：NEW -> READY -> RUNNING -> (BLOCKED | TERMINATED)
enum ProcState {
    PROC_NEW = 0,       // 刚创建，尚未加入调度
    PROC_READY,         // 就绪，等待 CPU
    PROC_RUNNING,       // 正在占用 CPU（单 CPU 模拟，同一时刻只有一个）
    PROC_BLOCKED,       // 阻塞在 IPC / 资源上
    PROC_TERMINATED     // 已终止，等待回收 PCB
};

// 优先级：数值越小越高。调度器按优先级抢占；同优先级轮转(RR)。
enum ProcPriority {
    PRIO_IDLE = 19,     // 最低：后台空闲任务
    PRIO_LOW = 10,
    PRIO_NORMAL = 0,    // 默认
    PRIO_HIGH = -8,
    PRIO_REALTIME = -19 // 最高：关键内核线程
};

// IPC 消息：固定长度小消息，避免动态分配带来的碎片。
const int IPC_MSG_BYTES = 56;
struct IpcMessage {
    int  src_pid;                   // 发送方 PID（0 = 内核广播）
    int  type;                      // 消息类型（自定义，>0 为业务消息）
    int  len;                       // payload 有效长度
    char data[IPC_MSG_BYTES];       // 负载
};

// 每个进程的信箱：环形缓冲，容量固定。
const int IPC_MBOX_CAP = 8;
struct IpcMbox {
    IpcMessage ring[IPC_MBOX_CAP];
    int head;       // 读指针
    int tail;       // 写指针
    int count;      // 当前消息数
};

// 进程控制块(PCB)。值语义放进 List<PCB>，所以所有成员都是可拷贝的。
struct Process {
    int     pid;                // 进程号，从 1 递增
    String  name;               // 进程名，例如 "init" / "sysmon"
    ProcState state;            // 当前状态
    ProcPriority prio;          // 优先级
    int     owner_uid;          // 属主用户 ID
    int     arrival_ms;         // 进入 READY 的虚拟时刻
    int     total_ticks;        // 总共需要的 CPU tick 数
    int     used_ticks;         // 已经消耗的 CPU tick 数
    int     quantum_left;       // 当前时间片剩余（RR 用）
    int     mem_pages;          // 占用内存页数（模拟，1 页 = 4KB）
    int     cpu_share;          // 累计 CPU 时间统计
    int     wakeup_tick;        // BLOCKED 状态下，到点自动唤醒的时刻（0 = 不自动唤醒）
    int     pgid;               // 进程组号（0 = 未加入）
    int     rlimit_pages;       // 内存页数软上限（0 = 无限制）
    int     nice;               // nice 值（-20..+19，演示用）
    bool    stopped;            // 收到 STOP 信号，暂时不参与调度
    IpcMbox mbox;               // 接收信箱
};

// ---------------- 调度策略 ----------------
enum SchedPolicy {
    SCHED_POL_RR = 0,   // 轮转（同优先级内）
    SCHED_POL_FIFO,     // 先到先服务，直到阻塞或结束
    SCHED_POL_BATCH     // 批处理：按优先级 + 到达时间综合排序
};

// ---------------- 进程管理器 ----------------
// 单例风格：进程表放在管理器内部，调用方通过全局函数访问。
class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    // 生命周期 ----------------------------------------------------------
    // 创建一个进程：name = 进程名，prio = 优先级，ticks = 需要的 CPU tick 数，
    // owner = 属主 uid。返回新 PID（<0 表示失败，例如进程表满）。
    int  create_process(const char* name, ProcPriority prio, int ticks, int owner);
    // 终止指定进程：置为 TERMINATED，并回收其 PCB（从表中移除）。
    bool terminate(int pid);
    // 结束当前 RUNNING 进程（调度器内部用）。
    void terminate_current();

    // 调度 --------------------------------------------------------------
    // 把虚拟时钟推进 ms 毫秒。内部按 tick 粒度切分，跑调度。
    // 返回这一时刻实际占用 CPU 的进程 PID（-1 = 空闲）。
    int  advance(uint32_t ms);
    // 执行一次调度决策：按策略选出下一个要跑的进程。
    int  pick_next();
    // 把当前 RUNNING 进程设为 BLOCKED，直到 wakeup_ms 时刻自动唤醒。
    void block_current(int wakeup_ms);
    // 主动唤醒指定 pid 的进程（从 BLOCKED -> READY）。
    bool wakeup(int pid);

    // 属性 --------------------------------------------------------------
    void set_policy(SchedPolicy p) { policy_ = p; }
    SchedPolicy policy() const { return policy_; }
    bool set_priority(int pid, ProcPriority p);
    // nice 调整：在 -20..+19 范围微调优先级权重（演示：只记 nice 值）。
    bool set_nice(int pid, int nice);
    int  nice(int pid) const;
    int  current_pid() const { return current_; }
    int  count() const { return procs_.size(); }
    // 统计：就绪/阻塞/存活进程数
    int  count_state(ProcState s) const;
    // 按 pid 查找，返回指针（只读）；找不到返回 0。
    const Process* find(int pid) const;
    Process* find_mut(int pid);

    // IPC ---------------------------------------------------------------
    // 向 dest_pid 发送一条消息；dest=0 表示广播给所有进程。
    // 信箱满时返回 false。type/data/len 由调用方给出。
    bool send_msg(int dest_pid, int type, const void* data, int len);
    // 取出调用进程（或指定 pid）信箱里的一条消息；没有则返回 false。
    bool recv_msg(int pid, IpcMessage* out);
    int  mbox_count(int pid) const;

    // 信号 ---------------------------------------------------------------
    // 模拟 POSIX 信号：向 pid 发信号（0=KILL 立即终止, 1=TERM 请求终止,
    // 2=STOP 暂停, 3=CONT 继续）。返回是否投递成功。
    bool send_signal(int pid, int sig);
    // 进程组：把 pid 加入组。返回组号；创建新组时 pgid=0 表示新建。
    int  join_group(int pid, int pgid);
    int  group_count(int pgid) const;
    // 向整个进程组广播信号
    bool signal_group(int pgid, int sig);

    // 资源限制（每进程软上限，模拟）-------------------------------------
    bool set_rlimit_pages(int pid, int max_pages);
    int  rlimit_pages(int pid) const;

    // 统计快照 -----------------------------------------------------------
    void usage_snapshot(int* total_cpu_ticks, int* total_mem_pages) const;
    // 上下文切换次数（每次调度切走 current 记一次）。
    int  context_switches() const { return csw_; }
    // 按 uid 汇总 CPU 时间。
    int  cpu_time_by_uid(int uid) const;
    // 进程退出码记账：进程终止时记录 exit_code。
    void set_exit_code(int pid, int code);
    int  exit_code(int pid) const;

    // 进程会计：导出每个存活进程的 (pid, name, cpu_ticks) 摘要。
    struct ProcAccount { int pid; char name[20]; int cpu_ticks; };
    int  account_list(ProcAccount* out, int max) const;

    // IPC 消息队列 -------------------------------------------------------
    // 每条消息定长 128 字节，每个进程一个入队。
    static const int IPC_MSG_BYTES = 128;
    static const int IPC_QUEUE_CAP = 8;
    // 向 dst_pid 发一条消息（拷贝 len 字节，最多 127）。返回 false = 队列满/无此进程。
    bool ipc_send(int dst_pid, const char* msg, int len);
    // 从 src_pid 的出队取一条消息到 out（out 至少 128）。返回消息长度，-1 = 空。
    int  ipc_recv(int src_pid, char* out);
    // src_pid 当前排队消息数。
    int  ipc_pending(int src_pid) const;
    // waitpid：回收一个已 TERMINATED 的进程，返回其 pid（-1 = 无已终止进程）。
    int  waitpid();
    // 当前已终止待回收的进程数。
    int  zombie_count() const;

    // 虚拟时钟 ----------------------------------------------------------
    uint32_t now() const { return now_ms_; }
    void     set_now(uint32_t t) { now_ms_ = t; }

    // 调试：导出进程表一行摘要（ksprintf 到 buf）。
    void describe(int idx, char* buf, int bufsz) const;
    // ps 表头（"PID COMMAND ..."），写到 buf。
    static void ps_header(char* buf, int bufsz);
    // 导出所有存活进程名到 out（每行一个名字），返回数量。
    int  ps_names(const char** out, int max) const;
    // 所有存活进程占用内存页总和。
    int  mem_used_pages() const;
    // 检查 pid 是否超过其内存页软上限。
    bool over_rlimit(int pid) const;
    // 设置 pid 的内存页软上限。
    bool set_rlimit(int pid, int pages);

    // 自检 --------------------------------------------------------------
    int self_test();

private:
    void clean_terminated();                 // 回收 TERMINATED 的 PCB
    int  alloc_pid();                        // 分配下一个 PID
    int  pick_rr();                          // 轮转选择
    int  pick_fifo();                        // FIFO 选择
    int  pick_batch();                       // 批处理选择
    int  quantum_;                           // RR 时间片（tick）

    List<Process> procs_;
    int     next_pid_;
    int     current_;                        // 当前 RUNNING 的 PID（-1 = 无）
    uint32_t now_ms_;                        // 虚拟时钟
    int     csw_;                            // 上下文切换计数
    int     exit_codes_[64];                 // pid -> exit code（记账）
    SchedPolicy policy_;
    // IPC 消息队列：按 pid 槽位环形缓冲
    char    ipc_buf_[64][IPC_QUEUE_CAP][IPC_MSG_BYTES];
    int     ipc_head_[64], ipc_tail_[64], ipc_cnt_[64];
};

// 状态名转换
const char* proc_state_name(ProcState s);
// 优先级名
const char* proc_prio_name(ProcPriority p);

// 全局进程管理器实例（在 process.cpp 里定义）。
extern ProcessManager g_procman;

// 便捷自由函数：操作全局 g_procman。
int  proc_create(const char* name, ProcPriority prio, int ticks, int owner);
bool proc_terminate(int pid);
int  proc_advance(uint32_t ms);
bool proc_send(int dest, int type, const void* data, int len);
bool proc_recv(int pid, IpcMessage* out);

} // namespace sysutil
} // namespace nefu
