#pragma once
#include <deque>

#include "sthread.h"

typedef void (*handler_t) (void *); 

struct Task {
    handler_t handler;
    void* arg;
};

/*
 * ------------------------------------------------------------------
 * TaskQueue --
 * 
 *      A thread-safe task queue. This queue should be implemented
 *      as a monitor.
 *
 * ------------------------------------------------------------------
 */
class TaskQueue {
    private:
    std::deque<Task> q;
    int size();
    bool empty();

    public:
    TaskQueue();
    ~TaskQueue();
    
    // no default copy constructor and assignment operators. this will prevent some
    // painful bugs by converting them into compiler errors.
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue &) = delete;

    void enqueue(Task task);
    Task dequeue();

    private:
    
    smutex_t mtx;
    scond_t not_empty;
    
    
};

