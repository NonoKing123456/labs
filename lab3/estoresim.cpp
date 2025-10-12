#include <cstdio>
#include <vector>
#include <ctime>
#include <cstring>
#include <cstdlib>

#include "EStore.h"
#include "TaskQueue.h"
#include "sthread.h"
#include "RequestGenerator.h"
#include "RequestHandlers.h"  

class Simulation {
    public:
    TaskQueue supplierTasks;
    TaskQueue customerTasks;
    EStore store;

    int maxTasks;
    int numSuppliers;
    int numCustomers;

    explicit Simulation(bool useFineMode) : store(useFineMode) { }
};

/*
 * ------------------------------------------------------------------
 * supplierGenerator --
 *
 *      The supplier generator thread. The argument is a pointer to
 *      the shared Simulation object.
 *
 *      Enqueue arg->maxTasks requests to the supplier queue, then
 *      stop all supplier threads by enqueuing arg->numSuppliers
 *      stop requests.
 *
 *      Use a SupplierRequestGenerator to generate and enqueue
 *      requests.
 *
 *      This thread should exit when done.
 *
 * Results:
 *      Does not return. Exit instead.
 *
 * ------------------------------------------------------------------
 */

 static inline Task make_stop() {
    Task t; t.handler = nullptr; t.arg = nullptr; return t;
}

static void*
supplierGenerator(void* arg)
{
   Simulation* sim = static_cast<Simulation*>(arg);

    SupplierRequestGenerator gen(&sim->supplierTasks);
    gen.enqueueTasks(sim->maxTasks, &sim->store);   // produce supplier tasks
    gen.enqueueStops(sim->numSuppliers);            // one stop per supplier worker

    sthread_exit();
    return nullptr;
}

/*
 * ------------------------------------------------------------------
 * customerGenerator --
 *
 *      The customer generator thread. The argument is a pointer to
 *      the shared Simulation object.
 *
 *      Enqueue arg->maxTasks requests to the customer queue, then
 *      stop all customer threads by enqueuing arg->numCustomers
 *      stop requests.
 *
 *      Use a CustomerRequestGenerator to generate and enqueue
 *      requests.  For the fineMode argument to the constructor
 *      of CustomerRequestGenerator, use the output of
 *      store.fineModeEnabled() method, where store is a field
 *      in the Simulation class.
 *
 *      This thread should exit when done.
 *
 * Results:
 *      Does not return. Exit instead.
 *
 * ------------------------------------------------------------------
 */
static void*
customerGenerator(void* arg)
{
    Simulation* sim = static_cast<Simulation*>(arg);

    CustomerRequestGenerator gen(&sim->customerTasks, sim->store.fineModeEnabled());
    gen.enqueueTasks(sim->maxTasks, &sim->store);   // produce customer tasks
    gen.enqueueStops(sim->numCustomers);            // one stop per customer worker

    sthread_exit();
    return nullptr;
}
/*
 * ------------------------------------------------------------------
 * supplier --
 *
 *      The main supplier thread. The argument is a pointer to the
 *      shared Simulation object.
 *
 *      Dequeue Tasks from the supplier queue and execute them.
 *
 * Results:
 *      Does not return.
 *
 * ------------------------------------------------------------------
 */
static void*
supplier(void* arg)
{
    Simulation* sim = static_cast<Simulation*>(arg);

    for (;;) {
        Task t = sim->supplierTasks.dequeue();
        if (t.handler == stop_handler) {      // ← explicit stop
            sthread_exit();
        }
        t.handler(t.arg);
    }
    return nullptr; // not reached
}

/*
 * ------------------------------------------------------------------
 * customer --
 *
 *      The main customer thread. The argument is a pointer to the
 *      shared Simulation object.
 *
 *      Dequeue Tasks from the customer queue and execute them.
 *
 * Results:
 *      Does not return.
 *
 * ------------------------------------------------------------------
 */
static void*
customer(void* arg)
{
    Simulation* sim = static_cast<Simulation*>(arg);

    for (;;) {
        Task t = sim->customerTasks.dequeue();
        if (t.handler == stop_handler) {      // ← explicit stop
            sthread_exit();
        }
        t.handler(t.arg);
    }
    return nullptr; // not reached
}

/*
 * ------------------------------------------------------------------
 * startSimulation --
 *      Create a new Simulation object. This object will serve as
 *      the shared state for the simulation. 
 *
 *      Create the following threads:
 *          - 1 supplier generator thread.
 *          - 1 customer generator thread.
 *          - numSuppliers supplier threads.
 *          - numCustomers customer threads.
 *
 *      After creating the worker threads, the main thread
 *      should wait until all of them exit, at which point it
 *      should return.
 *
 *      Hint: Use sthread_join.
 *
 * Results:
 *      None.
 *
 * ------------------------------------------------------------------
 */
static void
startSimulation(int numSuppliers, int numCustomers, int maxTasks, bool useFineMode)
{
    Simulation* sim = new Simulation(useFineMode);
    sim->numSuppliers = numSuppliers;
    sim->numCustomers = numCustomers;
    sim->maxTasks     = maxTasks;

    sthread_t genSupTid, genCusTid;
    std::vector<sthread_t> supTids(numSuppliers);
    std::vector<sthread_t> cusTids(numCustomers);

    // Start workers first so they block on the queues, ready to consume
    for (int i = 0; i < numSuppliers; ++i) {
        sthread_create(&supTids[i], supplier, sim);
    }
    for (int i = 0; i < numCustomers; ++i) {
        sthread_create(&cusTids[i], customer, sim);
    }

    // Start generators
    sthread_create(&genSupTid, supplierGenerator, sim);
    sthread_create(&genCusTid, customerGenerator, sim);

    // Join generators
    sthread_join(genSupTid);
    sthread_join(genCusTid);

    // Join workers (they terminate via stop_handler)
    for (int i = 0; i < numSuppliers; ++i) {
        sthread_join(supTids[i]);
    }
    for (int i = 0; i < numCustomers; ++i) {
        sthread_join(cusTids[i]);
    }

    delete sim;
}

int main(int argc, char **argv)
{
    bool useFineMode = false;
    // Seed the random number generator.
    // You can remove this line or set it to some constant to get deterministic
    // results, but make sure you put it back before turning in.
    srand(time(NULL));
    
    if (argc > 1)
        useFineMode = strcmp(argv[1], "--fine") == 0;
    startSimulation(10, 10, 100, useFineMode);
    return 0;
}