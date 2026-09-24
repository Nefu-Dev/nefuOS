// nefuOS Scheduler (Linux-style CFS, pure C++)
// References: Linux kernel sched/, CFS (Completely Fair Scheduler)

#ifndef NEFU_SCHED_H
#define NEFU_SCHED_H

#include "process.h"
#include "../klib/klib.h"

namespace nefu {

// Scheduling classes (like Linux sched_class)
enum class SchedClass {
    NORMAL = 0,     // SCHED_OTHER (CFS)
    FIFO = 1,       // SCHED_FIFO (real-time)
    RR = 2,         // SCHED_RR (round-robin)
    BATCH = 3,      // SCHED_BATCH
    IDLE = 5,       // SCHED_IDLE
};

// Per-entity scheduling info (like Linux sched_entity)
struct SchedEntity {
    uint64_t vruntime;      // virtual runtime (CFS key metric)
    uint64_t sumExecRuntime;
    uint64_t lastEnqueue;
    uint64_t slice;         // time slice in ns
};

// Run queue (like Linux rq)
struct RunQueue {
    int nrRunning;
    uint64_t minVruntime;
    uint64_t timestamp;
};

// CFS Scheduler (Completely Fair Scheduler)
class Scheduler {
private:
    RunQueue rq;
    Task* current;
    uint64_t schedClock;

public:
    Scheduler() : current(nullptr), schedClock(0) {
        rq.nrRunning = 0;
        rq.minVruntime = 0;
        rq.timestamp = 0;
    }

    // Enqueue task (like enqueue_task_fair)
    void enqueueTask(Task* t) {
        if (!t) return;
        // Simplified: add to end of list
        rq.nrRunning++;
    }

    // Dequeue task (like dequeue_task_fair)
    void dequeueTask(Task* t) {
        if (!t) return;
        rq.nrRunning--;
    }

    // Pick next task to run (like pick_next_task_fair)
    Task* pickNextTask() {
        // Simplified: round-robin for now
        // Real CFS would pick the task with minimum vruntime
        return nullptr; // idle
    }

    // Schedule (like schedule())
    void schedule() {
        Task* next = pickNextTask();
        if (next && next != current) {
            // Context switch would happen here
            current = next;
            current->state = ProcessState::RUNNING;
        }
    }

    // Set task nice value (like set_user_nice)
    void setNice(Task* t, int nice) {
        if (!t) return;
        // Clamp to [-20, 19]
        if (nice < -20) nice = -20;
        if (nice > 19) nice = 19;
        t->priority = nice;
    }

    // Get load average (like Linux avenrun)
    void getLoadAvg(uint64_t& oneMin, uint64_t& fiveMin, uint64_t& fifteenMin) {
        // Simplified: return current runqueue size
        oneMin = rq.nrRunning;
        fiveMin = rq.nrRunning;
        fifteenMin = rq.nrRunning;
    }

    int getRunQueueSize() const { return rq.nrRunning; }
    uint64_t getSchedClock() const { return schedClock; }
};

extern Scheduler* g_sched;

} // namespace nefu

#endif // NEFU_SCHED_H