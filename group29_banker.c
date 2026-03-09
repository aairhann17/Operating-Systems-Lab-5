#include <stdio.h> // library for input/output functions
#include <stdlib.h> // library for memory allocation and process control
#include <string.h> // library for string handling functions
#include <pthread.h> // library for POSIX threads

#define NUMBER_OF_CUSTOMERS 5 // fixed number of customers (threads) in the system
#define NUMBER_OF_RESOURCES 3 // fixed number of resource types in the system
#define INITIAL_EVENT_CAP 16 // initial capacity for event queues, will grow dynamically if needed

// Event structure represents a single request or release operation by a customer.
typedef struct {
    int cid;
    char op;                  /* 'R' or 'L' */
    int v[NUMBER_OF_RESOURCES];
} Event;

// EventQueue is a simple dynamic array-based queue for storing events for each customer.
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


// Returns 1 if vector a is less than or equal to vector b element-wise, otherwise returns 0.
static int vector_leq(const int a[NUMBER_OF_RESOURCES], const int b[NUMBER_OF_RESOURCES]) {
    /* Returns 1 iff a[j] <= b[j] for all resource types j. */
    int j;
    for (j = 0; j < NUMBER_OF_RESOURCES; j++) {
        if (a[j] > b[j]) return 0;
    }
    return 1;
}


// Performs element-wise addition: dst[j] += x[j] for all resource types j.
static void vector_add(int dst[NUMBER_OF_RESOURCES], const int x[NUMBER_OF_RESOURCES]) {
    /* Element-wise: dst += x */
    int j;
    for (j = 0; j < NUMBER_OF_RESOURCES; j++) dst[j] += x[j];
}

// Performs element-wise subtraction: dst[j] -= x[j] for all resource types j.
static void vector_sub(int dst[NUMBER_OF_RESOURCES], const int x[NUMBER_OF_RESOURCES]) {
    /* Element-wise: dst -= x */
    int j;
    for (j = 0; j < NUMBER_OF_RESOURCES; j++) dst[j] -= x[j];
}

// Initializes an EventQueue by allocating memory for the initial capacity and setting size and capacity.
static void queue_init(EventQueue *q) {
    /* Simple dynamic array for storing parsed events. */
    q->cap = INITIAL_EVENT_CAP;
    q->size = 0;
    q->items = (Event *)malloc(sizeof(Event) * q->cap);
    if (!q->items) exit(1);
}

// Pushes an event onto the EventQueue, growing the capacity if necessary by doubling it.
static void queue_push(EventQueue *q, Event e) {
    /* Grow capacity by doubling when full. */
    if (q->size == q->cap) {
        q->cap *= 2;
        q->items = (Event *)realloc(q->items, sizeof(Event) * q->cap);
        if (!q->items) exit(1);
    }
    q->items[q->size++] = e;
}

// Frees the memory allocated for the EventQueue and resets its fields to indicate it's empty.
static void queue_free(EventQueue *q) {
    free(q->items);
    q->items = NULL;
    q->size = q->cap = 0;
}

// Initializes the allocation and need matrices based on the maximum matrix and the fact that initially no resources are allocated.
static void init_need_and_allocation(void) {
    /* Start with zero allocation; therefore need starts as maximum. */
    int i, j;
    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        for (j = 0; j < NUMBER_OF_RESOURCES; j++) {
            allocation[i][j] = 0;
            need[i][j] = maximum[i][j];
        }
    }
}

// Logs an event with the current state of the system. Must be called while holding the mutex to ensure consistency between the event and the state snapshot.
static void log_event_locked(int eid, int cid, char op, const int v[NUMBER_OF_RESOURCES], int ok) {
    /*
     * Must be called while mutex is held.
     * This guarantees the printed AVAIL/ALLOC/NEED snapshot matches this event.
     */
    printf("E=%d C=%d OP=%c V=[%d,%d,%d] OK=%d AVAIL=[%d,%d,%d] ALLOC=[%d,%d,%d] NEED=[%d,%d,%d]\n",
           eid, cid, op, v[0], v[1], v[2], ok,
           available[0], available[1], available[2],
           allocation[cid][0], allocation[cid][1], allocation[cid][2],
           need[cid][0], need[cid][1], need[cid][2]);
}

/* ---------- Banker core ---------- */

// Checks if the current state is safe according to the Banker's algorithm.
int is_safe_state(void) {
    /*
     * Standard Banker's safety check:
     * - work is a temporary copy of available resources
     * - finish[i] indicates whether customer i can complete in the simulation
     */
    int work[NUMBER_OF_RESOURCES];
    int finish[NUMBER_OF_CUSTOMERS] = {0};
    int i, j, progressed;

    // Initialize work to the current available resources.
    for (j = 0; j < NUMBER_OF_RESOURCES; j++) work[j] = available[j];

    // Repeatedly find a customer that can finish with the current work, simulate it finishing, and mark it as finished until no more progress can be made.
    do {
        progressed = 0;
        for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
            if (finish[i]) continue;
            if (vector_leq(need[i], work)) {
                /* Simulate customer i finishing and releasing all its allocation. */
                for (j = 0; j < NUMBER_OF_RESOURCES; j++) work[j] += allocation[i][j];
                finish[i] = 1;
                progressed = 1;
            }
        }
    } while (progressed);

    // If all customers can finish, the state is safe.
    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        if (!finish[i]) return 0;
    }
    return 1;
}

// Handles a resource request from a customer. Returns 1 if the request is granted, otherwise returns 0. The function performs the necessary checks and updates the state accordingly while ensuring that the system remains in a safe state.
int request_resources(int customer_num, int request[]) {
    /* 1) request <= need ? */
    if (!vector_leq(request, need[customer_num])) return 0;

    /* 2) request <= available ? */
    if (!vector_leq(request, available)) return 0;

    /* 3) Tentative allocation */
    vector_sub(available, request);
    vector_add(allocation[customer_num], request);
    vector_sub(need[customer_num], request);

    /* 4) Safety check */
    if (!is_safe_state()) {
        /* Roll back tentative changes if resulting state is unsafe. */
        vector_add(available, request);
        vector_sub(allocation[customer_num], request);
        vector_add(need[customer_num], request);
        return 0;
    }

    return 1;
}

// Handles a resource release from a customer. Returns 1 if the release is valid and processed, otherwise returns 0. The function checks that the release does not exceed the customer's current allocation and updates the state accordingly.
int release_resources(int customer_num, int release[]) {
    /* release must not exceed allocation */
    if (!vector_leq(release, allocation[customer_num])) return 0;

    /* A valid release always moves the system toward more availability. */
    vector_add(available, release);
    vector_sub(allocation[customer_num], release);
    vector_add(need[customer_num], release);
    return 1;
}

/* ---------- Input parsing ---------- */

// Reads the initial available vector and the maximum matrix from standard input. Returns 1 on success, otherwise returns 0 if the input is invalid (e.g., missing values or negative maximums).
static int read_initial_input(void) {
    /* Read initial available vector and the full maximum matrix. */
    int i, j;
    if (scanf("%d %d %d", &available[0], &available[1], &available[2]) != 3) return 0;

    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        if (scanf("%d %d %d",
                  &maximum[i][0], &maximum[i][1], &maximum[i][2]) != 3) return 0;
        for (j = 0; j < NUMBER_OF_RESOURCES; j++) {
            if (maximum[i][j] < 0) return 0;
        }
    }
    return 1;
}

// Reads the sequence of events from standard input until EOF. Each event is parsed and validated, and valid events are added to the corresponding customer's event queue. Invalid lines are ignored to ensure robustness of the parser.
static void read_events(void) {
    /*
     * Parse remaining lines until EOF:
     * cid op v0 v1 v2
     * Invalid rows are ignored to keep parser robust.
     */
    Event e;
    while (scanf("%d %c %d %d %d",
                 &e.cid, &e.op, &e.v[0], &e.v[1], &e.v[2]) == 5) {
        if (e.cid < 0 || e.cid >= NUMBER_OF_CUSTOMERS) continue;
        if (!(e.op == 'R' || e.op == 'L')) continue;
        if (e.v[0] < 0 || e.v[1] < 0 || e.v[2] < 0) continue;
        queue_push(&queues[e.cid], e);
    }
}

/* ---------- Thread ---------- */

// Each customer thread processes its own queue of events in order, performing the necessary synchronization to ensure that the shared state is updated correctly and that the logs reflect a consistent view of the system state.
void *customer_thread(void *arg) {
    /* One thread per customer, processing only that customer's queued events. */
    int cid = *(int *)arg;
    int k;

    // Iterate through the events in the customer's queue and process each event while holding the mutex to ensure that the state updates and logging are atomic with respect to other threads.
    for (k = 0; k < queues[cid].size; k++) {
        Event *e = &queues[cid].items[k];
        int ok;

        /*
         * Critical section order required by the lab:
         * lock -> process event -> print log -> unlock
         */
        pthread_mutex_lock(&mtx);

        // Process the event based on its type (request or release) and update the state accordingly. The result of the operation (success or failure) is stored in 'ok' for logging purposes.
        if (e->op == 'R') ok = request_resources(cid, e->v);
        else ok = release_resources(cid, e->v);

        // Log the event with the current state of the system. The log includes the global event ID, customer ID, operation type, requested/released vector, whether the operation was successful, and the current available, allocation, and need vectors.
        log_event_locked(global_event_id, cid, e->op, e->v, ok);
        global_event_id++;

        // Unlock the mutex to allow other threads to process their events and update the state.
        pthread_mutex_unlock(&mtx);
    }

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
    int i;
    pthread_t tids[NUMBER_OF_CUSTOMERS];
    int cids[NUMBER_OF_CUSTOMERS];

    // Initialize the event queues for each customer before reading the input to ensure that events can be stored properly as they are parsed.
    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) queue_init(&queues[i]);

    // Read the initial available resources and maximum demands from standard input. If the input is invalid, the program exits with an error code.
    if (!read_initial_input()) return 1;
    init_need_and_allocation();
    read_events();

    // Create a thread for each customer, passing the customer ID as an argument. Each thread will process its own queue of events independently while synchronizing access to the shared state.
    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        cids[i] = i;
        pthread_create(&tids[i], NULL, customer_thread, &cids[i]);
    }

    // Wait for all customer threads to finish processing their events before exiting the program. This ensures that all events are processed and logged before the program terminates.
    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        pthread_join(tids[i], NULL);
    }

    // Free the memory allocated for the event queues and destroy the mutex before exiting the program to clean up resources.
    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) queue_free(&queues[i]);
    pthread_mutex_destroy(&mtx);
    return 0;
}