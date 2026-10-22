#ifndef THREADPOOL_H
#define THREADPOOL_H

/* =========================================================================
 * Server thread pool interface
 * ========================================================================= */

/**
 * @brief Start the fixed worker thread pool
 * @details
 * Creates the worker threads that process requests from the global request
 * queue. The thread count is chosen once at server startup and must not grow
 * based on the number of connected clients.
 *
 * @param thread_count number of worker threads to create
 * @return 0 on success, otherwise -1
 */
int threadpool_start(int thread_count);

#endif