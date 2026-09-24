// nefuOS Process Management (Linux-style, pure C++)
// References: Linux kernel sched.h, task_struct

#ifndef NEFU_PROCESS_H
#define NEFU_PROCESS_H

#include "../klib/klib.h"
#include "../vfs/vfs.h"

namespace nefu {

// Process states (like Linux TASK_*)
enum class ProcessState : int {
    READY       = 0,
    RUNNING     = 1,
    INTERRUPTIBLE = 2,
    UNINTERRUPTIBLE = 3,
    STOPPED     = 4,
    ZOMBIE      = 16,
    DEAD        = 32,
};

// Process flags (like Linux PF_*)
enum ProcessFlag : uint32_t {
    PF_KTHREAD  = 0x00200000,
    PF_EXITING  = 0x00000004,
    PF_FORKNOEXEC = 0x00000040,
};

// Signal numbers (Linux-aligned)
enum Signal : int {
    SIGHUP  = 1,
    SIGINT  = 2,
    SIGQUIT = 3,
    SIGILL  = 4,
    SIGTRAP = 5,
    SIGABRT = 6,
    SIGBUS  = 7,
    SIGFPE  = 8,
    SIGKILL = 9,
    SIGUSR1 = 10,
    SIGSEGV = 11,
    SIGUSR2 = 12,
    SIGPIPE = 13,
    SIGALRM = 14,
    SIGTERM = 15,
    SIGCHLD = 17,
    SIGCONT = 18,
    SIGSTOP = 19,
    SIGTSTP = 20,
};

// Per-process open file descriptor
struct FileDescriptor {
    FSNode* node;
    uint32_t flags;
    uint32_t pos;
};

// Task control block (like Linux task_struct)
class Task {
public:
    int pid;
    int tgid;
    int ppid;
    uid_t uid;
    gid_t gid;
    ProcessState state;
    uint32_t flags;
    int priority;
    uint64_t startTime;
    uint64_t totalRuntime;

    char comm[16];
    char cwd[256];

    FileDescriptor fdTable[64];
    int maxFd;

    uint32_t pendingSignals;
    uint32_t blockedSignals;

    uintptr_t mmStart, mmEnd;
    uint32_t heapSize;

    Task* next;

    Task(int _pid, int _ppid, const char* name)
        : pid(_pid), tgid(_pid), ppid(_ppid),
          uid(1000), gid(1000),
          state(ProcessState::READY), flags(0),
          priority(0), startTime(0), totalRuntime(0),
          maxFd(0), pendingSignals(0), blockedSignals(0),
          mmStart(0), mmEnd(0), heapSize(0), next(nullptr) {
        ksprintf(comm, sizeof(comm), "%s", name ? name : "unknown");
        cwd[0] = '/'; cwd[1] = 0;
    }

    bool isKernelThread() const { return (flags & PF_KTHREAD) != 0; }
    bool isExiting() const { return (flags & PF_EXITING) != 0; }
    bool isZombie() const { return state == ProcessState::ZOMBIE; }
};

// Process manager (like Linux fork/exec/wait)
class ProcessManager {
private:
    Task* taskList;
    Task* current;
    int nextPid;
    int processCount;

public:
    ProcessManager() : taskList(nullptr), current(nullptr), nextPid(1), processCount(0) {}

    int createProcess(const char* name, int flags = 0) {
        int pid = nextPid++;
        int ppid = current ? current->pid : 0;
        Task* t = new Task(pid, ppid, name);
        t->flags = flags;
        t->startTime = 0;
        t->next = taskList;
        taskList = t;
        processCount++;
        return pid;
    }

    void exitProcess(int code) {
        if (!current) return;
        current->state = ProcessState::ZOMBIE;
        current->flags |= PF_EXITING;
    }

    int sendSignal(int pid, int sig) {
        Task* t = taskList;
        while (t) {
            if (t->pid == pid) {
                t->pendingSignals |= (1 << sig);
                if (sig == SIGKILL) {
                    t->state = ProcessState::ZOMBIE;
                    t->flags |= PF_EXITING;
                } else if (sig == SIGSTOP) {
                    t->state = ProcessState::STOPPED;
                } else if (sig == SIGCONT) {
                    t->state = ProcessState::READY;
                }
                return 0;
            }
            t = t->next;
        }
        return -1;
    }

    Task* getCurrent() { return current; }
    Task* getTask(int pid) {
        Task* t = taskList;
        while (t) {
            if (t->pid == pid) return t;
            t = t->next;
        }
        return nullptr;
    }

    int listProcesses(Task** out, int max) {
        int count = 0;
        Task* t = taskList;
        while (t && count < max) {
            out[count++] = t;
            t = t->next;
        }
        return count;
    }

    int getProcessCount() const { return processCount; }
};

extern ProcessManager* g_proc;

} // namespace nefu

#endif // NEFU_PROCESS_H