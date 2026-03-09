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

/* Shared banker state */
int available[NUMBER_OF_RESOURCES];
int maximum[NUMBER_OF_CUSTOMERS][NUMBER_OF_RESOURCES];
int allocation[NUMBER_OF_CUSTOMERS][NUMBER_OF_RESOURCES];
int need[NUMBER_OF_CUSTOMERS][NUMBER_OF_RESOURCES];

/* Per-customer event queues */
EventQueue queues[NUMBER_OF_CUSTOMERS];
int queue_index[NUMBER_OF_CUSTOMERS]; /* thread-local progress index by cid */

/* Synchronization + global event id */
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
int global_event_id = 0;

/* ---------- Helpers ---------- */

static int vector_leq(const int a[NUMBER_OF_RESOURCES], const int b[NUMBER_OF_RESOURCES]) {
    int j;
    for (j = 0; j < NUMBER_OF_RESOURCES; j++) {
        if (a[j] > b[j]) return 0;
    }
    return 1;
}

static void vector_add(int dst[NUMBER_OF_RESOURCES], const int x[NUMBER_OF_RESOURCES]) {
    int j;
    for (j = 0; j < NUMBER_OF_RESOURCES; j++) dst[j] += x[j];
}

static void vector_sub(int dst[NUMBER_OF_RESOURCES], const int x[NUMBER_OF_RESOURCES]) {
    int j;
    for (j = 0; j < NUMBER_OF_RESOURCES; j++) dst[j] -= x[j];
}

static void queue_init(EventQueue *q) {
    q->cap = INITIAL_EVENT_CAP;
    q->size = 0;
    q->items = (Event *)malloc(sizeof(Event) * q->cap);
    if (!q->items) exit(1);
}

static void queue_push(EventQueue *q, Event e) {
    if (q->size == q->cap) {
        q->cap *= 2;
        q->items = (Event *)realloc(q->items, sizeof(Event) * q->cap);
        if (!q->items) exit(1);
    }
    q->items[q->size++] = e;
}

static void queue_free(EventQueue *q) {
    free(q->items);
    q->items = NULL;
    q->size = q->cap = 0;
}

static void init_need_and_allocation(void) {
    int i, j;
    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        for (j = 0; j < NUMBER_OF_RESOURCES; j++) {
            allocation[i][j] = 0;
            need[i][j] = maximum[i][j];
        }
    }
}

static void log_event_locked(int eid, int cid, char op, const int v[NUMBER_OF_RESOURCES], int ok) {
    printf("E=%d C=%d OP=%c V=[%d,%d,%d] OK=%d AVAIL=[%d,%d,%d] ALLOC=[%d,%d,%d] NEED=[%d,%d,%d]\n",
           eid, cid, op, v[0], v[1], v[2], ok,
           available[0], available[1], available[2],
           allocation[cid][0], allocation[cid][1], allocation[cid][2],
           need[cid][0], need[cid][1], need[cid][2]);
}

/* ---------- Banker core ---------- */

int is_safe_state(void) {
    int work[NUMBER_OF_RESOURCES];
    int finish[NUMBER_OF_CUSTOMERS] = {0};
    int i, j, progressed;

    for (j = 0; j < NUMBER_OF_RESOURCES; j++) work[j] = available[j];

    do {
        progressed = 0;
        for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
            if (finish[i]) continue;
            if (vector_leq(need[i], work)) {
                for (j = 0; j < NUMBER_OF_RESOURCES; j++) work[j] += allocation[i][j];
                finish[i] = 1;
                progressed = 1;
            }
        }
    } while (progressed);

    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        if (!finish[i]) return 0;
    }
    return 1;
}

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
        /* Rollback */
        vector_add(available, request);
        vector_sub(allocation[customer_num], request);
        vector_add(need[customer_num], request);
        return 0;
    }

    return 1;
}

int release_resources(int customer_num, int release[]) {
    /* release must not exceed allocation */
    if (!vector_leq(release, allocation[customer_num])) return 0;

    vector_add(available, release);
    vector_sub(allocation[customer_num], release);
    vector_add(need[customer_num], release);
    return 1;
}

/* ---------- Input parsing ---------- */

static int read_initial_input(void) {
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

static void read_events(void) {
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

void *customer_thread(void *arg) {
    int cid = *(int *)arg;
    int k;

    for (k = 0; k < queues[cid].size; k++) {
        Event *e = &queues[cid].items[k];
        int ok;

        pthread_mutex_lock(&mtx);

        if (e->op == 'R') ok = request_resources(cid, e->v);
        else ok = release_resources(cid, e->v);

        log_event_locked(global_event_id, cid, e->op, e->v, ok);
        global_event_id++;

        pthread_mutex_unlock(&mtx);
    }

    return NULL;
}

int main(void) {
    int i;
    pthread_t tids[NUMBER_OF_CUSTOMERS];
    int cids[NUMBER_OF_CUSTOMERS];

    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) queue_init(&queues[i]);

    if (!read_initial_input()) return 1;
    init_need_and_allocation();
    read_events();

    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        cids[i] = i;
        pthread_create(&tids[i], NULL, customer_thread, &cids[i]);
    }

    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        pthread_join(tids[i], NULL);
    }

    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++) queue_free(&queues[i]);
    pthread_mutex_destroy(&mtx);
    return 0;
}