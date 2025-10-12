
#include "TaskQueue.h"

TaskQueue::
TaskQueue()
{
    smutex_init(&mtx);
    scond_init(&not_empty);
}

TaskQueue::
~TaskQueue()
{
    scond_destroy(&not_empty);
    smutex_destroy(&mtx);
}

/*
 * ------------------------------------------------------------------
 * size --
 *
 *      Return the current size of the queue.
 *
 * Results:
 *      The size of the queue.
 *
 * ------------------------------------------------------------------
 */
int TaskQueue::
size()
{
   smutex_lock(&mtx);
    int n = static_cast<int>(q.size());
    smutex_unlock(&mtx);
    return n;
}

/*
 * ------------------------------------------------------------------
 * empty --
 *
 *      Return whether or not the queue is empty.
 *
 * Results:
 *      true if the queue is empty and false otherwise.
 *
 * ------------------------------------------------------------------
 */
bool TaskQueue::
empty()
{
    smutex_lock(&mtx);
    bool e = q.empty();
    smutex_unlock(&mtx);
    return e;
}

/*
 * ------------------------------------------------------------------
 * enqueue --
 *
 *      Insert the task at the back of the queue.
 *
 * Results:
 *      None.
 *
 * ------------------------------------------------------------------
 */
void TaskQueue::
enqueue(Task task)
{
    smutex_lock(&mtx);
    q.push_back(task);
    // Wake one waiter (there is now at least one task)
    scond_signal(&not_empty, &mtx);
    smutex_unlock(&mtx);
}

/*
 * ------------------------------------------------------------------
 * dequeue --
 *
 *      Remove the Task at the front of the queue and return it.
 *      If the queue is empty, block until a Task is inserted.
 *
 * Results:
 *      The Task at the front of the queue.
 *
 * ------------------------------------------------------------------
 */
Task TaskQueue::
dequeue()
{
    smutex_lock(&mtx);
    while (q.empty()) {
        // Wait atomically: release mtx and sleep; upon wakeup, re-acquire mtx
        scond_wait(&not_empty, &mtx);
    }
    Task t = q.front();
    q.pop_front();
    smutex_unlock(&mtx);
    return t;
}

