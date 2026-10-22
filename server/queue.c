#include "queue.h"

#include <pthread.h>
#include <stdlib.h>

/* =========================================================================
 * Global request queue state
 * ========================================================================= */

/*
 * All clients share this one request queue.
 * Worker threads pull from the same queue, which avoids creating one worker
 * per client and keeps the thread pool size fixed.
 */
static Request *head = NULL;
static Request *tail = NULL;

static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;

/* =========================================================================
 * Queue lifecycle
 * ========================================================================= */

/**
 * @brief Initialise the request queue
 * @details
 * Resets the queue pointers before the server starts accepting RPC work.
 * The mutex and condition variable are statically initialised.
 *
 * @return 0 on success
 */
int request_queue_init(void) {
    head = NULL;
    tail = NULL;
    return 0;
}

/**
 * @brief Destroy all queued requests
 * @details
 * Frees any requests that are still waiting in the queue. This is mainly useful
 * during shutdown or test cleanup. Requests already popped by worker threads
 * are owned by those workers instead.
 */
void request_queue_destroy(void) {
    pthread_mutex_lock(&queue_lock);

    while (head != NULL) {
        Request *next = head->next;
        free(head);
        head = next;
    }

    tail = NULL;

    pthread_mutex_unlock(&queue_lock);
}

/* =========================================================================
 * Queue operations
 * ========================================================================= */

/**
 * @brief Push an RPC request onto the global queue
 * @details
 * Copies the parsed command into a new Request node and appends it to the
 * queue. The client is acquired before queueing so its ClientState cannot be
 * freed while a worker is still holding a request for it.
 *
 * @param client client that sent the request
 * @param command parsed RPC command to execute
 * @param seq per client request sequence number
 * @return 0 on success, otherwise -1
 */
int request_queue_push(ClientState *client,
                       const RpcCommand *command,
                       uint64_t seq) {
    Request *request;

    if (client == NULL || command == NULL) {
        return -1;
    }

    if (!client_acquire(client)) {
        return -1;
    }

    request = malloc(sizeof(*request));
    if (request == NULL) {
        client_release(client);
        return -1;
    }

    request->client = client;
    request->command = *command;
    request->seq = seq;
    request->next = NULL;

    pthread_mutex_lock(&queue_lock);

    if (tail == NULL) {
        head = request;
        tail = request;
    } else {
        tail->next = request;
        tail = request;
    }

    /*
     * Wake one worker. No need to wake the whole pool for a single request.
     */
    pthread_cond_signal(&queue_not_empty);
    pthread_mutex_unlock(&queue_lock);

    return 0;
}

/**
 * @brief Pop the next request from the global queue
 * @details
 * Called by worker threads. If no work is available, the worker sleeps on the
 * condition variable instead of spinning. The returned request must be freed by
 * the worker after execution.
 *
 * @return next request to process
 */
Request *request_queue_pop(void) {
    Request *request;

    pthread_mutex_lock(&queue_lock);

    while (head == NULL) {
        pthread_cond_wait(&queue_not_empty, &queue_lock);
    }

    request = head;
    head = head->next;

    if (head == NULL) {
        tail = NULL;
    }

    pthread_mutex_unlock(&queue_lock);

    return request;
}