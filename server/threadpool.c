#define _POSIX_C_SOURCE 200809L

#include "threadpool.h"
#include "queue.h"
#include "rpc_execute.h"
#include "client_state.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* =========================================================================
 * Worker thread pool state
 * ========================================================================= */

/*
 * The assignment requires the number of worker threads to stay fixed after
 * server initialisation. These workers are the threads that execute libanimate
 * and generate work from the shared request queue.
 */
static pthread_t *workers = NULL;
static int worker_count = 0;

/* =========================================================================
 * Worker thread loop
 * ========================================================================= */

/**
 * @brief Run one worker thread
 * @details
 * Repeatedly pops requests from the global request queue, executes the parsed
 * RPC, and stores the response for ordered delivery back to the client. The
 * request queue blocks when empty, so workers sleep instead of busy waiting.
 *
 * @param arg unused worker argument
 * @return always NULL, though this loop normally runs for the server lifetime
 */
static void *worker_main(void *arg) {
    (void)arg;

    while (1) {
        Request *request = request_queue_pop();

        if (request != NULL) {
            char response[128];

            /*
             * Execute the RPC on this fixed worker thread. This is where
             * libanimate and generate work is performed.
             */
            execute_rpc(request->client,
                        &request->command,
                        response,
                        sizeof(response));

            /*
             * Workers may finish out of order. Store the response by sequence
             * number, then flush only the responses now ready for this client.
             */
            if (client_store_response(request->client,
                                      request->seq,
                                      response) == 0) {
                client_flush_responses(request->client);
            }

            /*
             * The queue acquired the client before storing this request, so the
             * worker must release that reference after finishing.
             */
            client_release(request->client);
            free(request);
        }
    }

    return NULL;
}

/* =========================================================================
 * Thread pool startup
 * ========================================================================= */

/**
 * @brief Start the fixed server worker thread pool
 * @details
 * Allocates and starts exactly thread_count worker threads. These workers all
 * share the same global request queue, so the server does not create extra
 * worker threads as more clients connect.
 *
 * @param thread_count number of worker threads requested at server startup
 * @return 0 on success, otherwise -1
 */
int threadpool_start(int thread_count) {
    if (thread_count < 1) {
        return -1;
    }

    workers = calloc((size_t)thread_count, sizeof(*workers));
    if (workers == NULL) {
        return -1;
    }

    worker_count = thread_count;

    for (int i = 0; i < worker_count; i++) {
        if (pthread_create(&workers[i], NULL, worker_main, NULL) != 0) {
            return -1;
        }
    }

    return 0;
}