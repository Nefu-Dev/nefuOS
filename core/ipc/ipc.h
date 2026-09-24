// nefuOS IPC (Inter-Process Communication, Linux-style C++)
// References: Linux ipc/, pipe.c, sem.c, shm.c

#ifndef NEFU_IPC_H
#define NEFU_IPC_H

#include "../klib/klib.h"

namespace nefu {

// Pipe (like Linux struct pipe_inode_info)
class Pipe {
private:
    uint8_t* buffer;
    uint32_t size;
    uint32_t readPos;
    uint32_t writePos;
    bool readClosed;
    bool writeClosed;

public:
    Pipe(uint32_t bufSize = 4096)
        : size(bufSize), readPos(0), writePos(0),
          readClosed(false), writeClosed(false) {
        buffer = new uint8_t[size];
    }

    ~Pipe() { delete[] buffer; }

    // Write to pipe (like pipe_write)
    int write(const void* data, uint32_t len) {
        if (writeClosed) return -1;
        uint32_t written = 0;
        const uint8_t* src = (const uint8_t*)data;
        while (written < len) {
            uint32_t avail = (readPos <= writePos) ?
                             (size - writePos + readPos) :
                             (readPos - writePos);
            if (avail == 1) break; // full
            buffer[writePos] = src[written];
            writePos = (writePos + 1) % size;
            written++;
        }
        return written;
    }

    // Read from pipe (like pipe_read)
    int read(void* data, uint32_t len) {
        if (readClosed && readPos == writePos) return 0; // EOF
        uint32_t read = 0;
        uint8_t* dst = (uint8_t*)data;
        while (read < len && readPos != writePos) {
            dst[read] = buffer[readPos];
            readPos = (readPos + 1) % size;
            read++;
        }
        return read;
    }

    void closeRead() { readClosed = true; }
    void closeWrite() { writeClosed = true; }
    bool isReadable() const { return readPos != writePos; }
    bool isWritable() const { return !writeClosed; }
};

// Semaphore (like Linux semaphore)
class Semaphore {
private:
    int count;
    int waiters;

public:
    Semaphore(int initial = 1) : count(initial), waiters(0) {}

    // P operation (down)
    void down() {
        while (count <= 0) {
            waiters++;
            // In real kernel: schedule()
            waiters--;
        }
        count--;
    }

    // V operation (up)
    void up() {
        count++;
    }

    int getCount() const { return count; }
};

// Mutex (like Linux struct mutex)
class Mutex {
private:
    Semaphore sem;

public:
    Mutex() : sem(1) {}

    void lock() { sem.down(); }
    void unlock() { sem.up(); }
    bool tryLock() { return sem.getCount() > 0; }
};

// Shared Memory (like Linux shmget/shmat)
class SharedMemory {
private:
    void* ptr;
    uint32_t size;
    int refCount;

public:
    SharedMemory(uint32_t _size) : size(_size), refCount(0) {
        ptr = new uint8_t[size];
    }

    ~SharedMemory() { delete[] (uint8_t*)ptr; }

    void* attach() { refCount++; return ptr; }
    void detach() { refCount--; }
    uint32_t getSize() const { return size; }
    int getRefCount() const { return refCount; }
};

// Message Queue (like Linux msgget/msgsnd/msgrcv)
struct Message {
    long type;
    char data[256];
    Message* next;
};

class MessageQueue {
private:
    Message* head;
    Message* tail;
    int count;

public:
    MessageQueue() : head(nullptr), tail(nullptr), count(0) {}

    // Send message (like msgsnd)
    int send(long type, const char* data, uint32_t len) {
        Message* msg = new Message;
        msg->type = type;
        uint32_t copyLen = (len < 256) ? len : 256;
        for (uint32_t i = 0; i < copyLen; i++) msg->data[i] = data[i];
        msg->data[copyLen] = 0;
        msg->next = nullptr;

        if (!tail) { head = tail = msg; }
        else { tail->next = msg; tail = msg; }
        count++;
        return 0;
    }

    // Receive message (like msgrcv)
    int receive(long type, char* buf, uint32_t bufLen) {
        Message* prev = nullptr;
        Message* cur = head;
        while (cur) {
            if (cur->type == type) {
                if (prev) prev->next = cur->next;
                else head = cur->next;
                if (!cur->next) tail = prev;

                uint32_t copyLen = (bufLen < 256) ? bufLen : 256;
                for (uint32_t i = 0; i < copyLen; i++) buf[i] = cur->data[i];
                delete cur;
                count--;
                return (int)copyLen;
            }
            prev = cur;
            cur = cur->next;
        }
        return -1; // no message
    }

    int getCount() const { return count; }
};

// IPC Manager
class IPCManager {
private:
    Pipe* pipes[16];
    int pipeCount;
    MessageQueue queues[8];
    int queueCount;

public:
    IPCManager() : pipeCount(0), queueCount(0) {
        for (int i = 0; i < 16; i++) pipes[i] = nullptr;
    }

    // Create pipe (like pipe() syscall)
    int createPipe(Pipe** readEnd, Pipe** writeEnd) {
        if (pipeCount >= 16) return -1;
        Pipe* p = new Pipe();
        pipes[pipeCount++] = p;
        *readEnd = p;
        *writeEnd = p; // same pipe, both ends
        return 0;
    }

    int getPipeCount() const { return pipeCount; }
    int getQueueCount() const { return queueCount; }
};

extern IPCManager* g_ipc;

} // namespace nefu

#endif // NEFU_IPC_H