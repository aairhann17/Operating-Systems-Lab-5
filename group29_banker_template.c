#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define NUMBER_OF_CUSTOMERS 5
#define NUMBER_OF_RESOURCES 3
#define INITIAL_EVENT_CAP 16

typedef struct {
    int cid;
    char op;                  /* 'R' or 'L' */
    int v[NUMBER_OF_RESOURCES];
} Event;

typedef struct {
    Event *items;
    int size;
    int cap;
} EventQueue;

/*
 * Shared banker state used by the safety and request/release logic:
 * - available[r]: currently free instances of resource type r
 * - maximum[c][r]: declared upper bound for customer c
 * - allocation[c][r]: currently allocated instances to customer c
 * - need[c][r]: remaining demand = maximum - allocation
 */
int available[NUMBER_OF_RESOURCES];
int maximum[NUMBER_OF_CUSTOMERS][NUMBER_OF_RESOURCES];
int allocation[NUMBER_OF_CUSTOMERS][NUMBER_OF_RESOURCES];
int need[NUMBER_OF_CUSTOMERS][NUMBER_OF_RESOURCES];

/*
 * Input events are partitioned by customer.
 * Each thread consumes only its own queue, preserving per-customer order.
 */
EventQueue queues[NUMBER_OF_CUSTOMERS];
int queue_index[NUMBER_OF_CUSTOMERS]; /* thread-local progress index by cid */

/*
 * One mutex protects all shared state + logging.
 * global_event_id records the actual execution order across all threads.
 */
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
int global_event_id = 0;

/* ---------- Helpers ---------- */

static int vector_leq(const int a[NUMBER_OF_RESOURCES], const int b[NUMBER_OF_RESOURCES]) {
    /* Returns 1 iff a[j] <= b[j] for all resource types j. */
    // TODO: Implement this function
    return 0;
}

static void vector_add(int dst[NUMBER_OF_RESOURCES], const int x[NUMBER_OF_RESOURCES]) {
    /* Element-wise: dst += x */
    // TODO: Implement this function
}

static void vector_sub(int dst[NUMBER_OF_RESOURCES], const int x[NUMBER_OF_RESOURCES]) {
    /* Element-wise: dst -= x */
    // TODO: Implement this function
}

static void queue_init(EventQueue *q) {
    /* Simple dynamic array for storing parsed events. */
    // TODO: Implement this function
}

static void queue_push(EventQueue *q, Event e) {
    /* Grow capacity by doubling when full. */
    // TODO: Implement this function
}

static void queue_free(EventQueue *q) {
    // TODO: Implement this function
}

static void init_need_and_allocation(void) {
    /* Start with zero allocation; therefore need starts as maximum. */
    // TODO: Implement this function
}

static void log_event_locked(int eid, int cid, char op, const int v[NUMBER_OF_RESOURCES], int ok) {
    /*
     * Must be called while mutex is held.
     * This guarantees the printed AVAIL/ALLOC/NEED snapshot matches this event.
     */
    // TODO: Implement this function
}

/* ---------- Banker core ---------- */

int is_safe_state(void) {
    /*
     * Standard Banker's safety check:
     * - work is a temporary copy of available resources
     * - finish[i] indicates whether customer i can complete in the simulation
     */
    // TODO: Implement this function
    return 0;
}

int request_resources(int customer_num, int request[]) {
    // TODO: Implement this function
    return 0;
}

int release_resources(int customer_num, int release[]) {
    // TODO: Implement this function
    return 0;
}

/* ---------- Input parsing ---------- */

static int read_initial_input(void) {
    /* Read initial available vector and the full maximum matrix. */
    // TODO: Implement this function
    return 0;
}

static void read_events(void) {
    /*
     * Parse remaining lines until EOF:
     * cid op v0 v1 v2
     * Invalid rows are ignored to keep parser robust.
     */
    // TODO: Implement this function
}

/* ---------- Thread ---------- */

void *customer_thread(void *arg) {
    /* One thread per customer, processing only that customer's queued events. */
    // TODO: Implement this function
    return NULL;
}

int main(void) {
    /*
     * Pipeline:
     * 1) initialize queues
     * 2) read initial state
     * 3) initialize allocation/need
     * 4) read all events
     * 5) launch/join 5 customer threads
     */
    // TODO: Implement this function
    return 0;
}
