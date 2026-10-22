#ifndef CLIENT_STATE_H
#define CLIENT_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <pthread.h>

/* =========================================================================
 * Ordered response state
 * ========================================================================= */

/**
 * @brief Queued response waiting to be sent to a client
 * @details
 * Worker threads can finish requests out of order. Each response is stored
 * with the request sequence number so the server can still write replies back
 * to the client in the original request order.
 */
typedef struct ResponseNode {
    uint64_t seq;
    char response[128];
    struct ResponseNode *next;
} ResponseNode;

/* =========================================================================
 * Connected client state
 * ========================================================================= */

/**
 * @brief Server side state for one connected client
 * @details
 * Stores the client's PID, login state, FIFO descriptors, FIFO paths, ordered
 * response queue, and lifetime tracking fields. This lets the server cleanly
 * handle normal Disconnect commands and ungraceful client termination.
 */
typedef struct {
    pid_t pid;

    /* Authorisation state from the Login protocol. */
    char username[33];
    int logged_in;
    int disconnected;

    /* FIFO descriptors used for request and response traffic. */
    int c2s_fd;
    int s2c_fd;

    /* FIFO paths so cleanup can unlink them on disconnect. */
    char c2s_path[256];
    char s2c_path[256];

    /* Sequence numbers used to preserve per-client response order. */
    uint64_t next_request_seq;
    uint64_t next_response_seq;

    ResponseNode *responses;
    pthread_mutex_t response_lock;

    /*
     * Lifetime tracking protects the client while worker threads may still
     * hold references to it after it has been removed from the active list.
     */
    int ref_count;
    int pending_free;
    pthread_mutex_t lifetime_lock;
} ClientState;

/* =========================================================================
 * Client creation and registry management
 * ========================================================================= */

/**
 * @brief Create server side state for a newly connected client
 * @details
 * Allocates a ClientState, stores the FIFO paths, initialises locks, and opens
 * the server side FIFO descriptors.
 *
 * @param pid client process ID
 * @param c2s_path path to FIFO_C2S_<client_pid>
 * @param s2c_path path to FIFO_S2C_<client_pid>
 * @return newly allocated client state, or NULL on failure
 */
ClientState *create_client(pid_t pid,
                           const char *c2s_path,
                           const char *s2c_path);

/**
 * @brief Add a client to the active client registry
 * @details
 * Makes the client visible to lookup, polling, liveness, and request handling
 * code.
 *
 * @param client client state to register
 * @return 0 on success, otherwise -1
 */
int add_client(ClientState *client);

/**
 * @brief Find a connected client by process ID
 * @param pid client process ID
 * @return matching client, or NULL if not found
 */
ClientState *find_client_by_pid(pid_t pid);

/**
 * @brief Find a connected client by request FIFO descriptor
 * @param fd FIFO_C2S descriptor to search for
 * @return matching client, or NULL if not found
 */
ClientState *find_client_by_fd(int fd);

/* =========================================================================
 * Liveness and lifetime helpers
 * ========================================================================= */

/**
 * @brief Check whether a process ID still refers to a live client process
 * @param pid client process ID to check
 * @return 1 if the process appears alive, otherwise 0
 */
int client_process_alive(pid_t pid);

/**
 * @brief Run one pass of ungraceful disconnect detection
 * @details
 * Removes clients whose processes have died without sending Disconnect.
 */
void check_client_liveness_once(void);

/**
 * @brief Acquire a temporary reference to a client
 * @details
 * Used by worker threads before touching client state so cleanup does not free
 * the client underneath them. Tiny race goblin avoided.
 *
 * @param client client to acquire
 * @return 1 if acquired, otherwise 0
 */
int client_acquire(ClientState *client);

/**
 * @brief Release a temporary reference to a client
 * @details
 * Frees the client if it was already removed and this was the last outstanding
 * reference.
 *
 * @param client client to release
 */
void client_release(ClientState *client);

/**
 * @brief Mark a client as disconnected
 * @details
 * Prevents new references and response writes from being accepted.
 *
 * @param client client to mark
 */
void client_mark_disconnected(ClientState *client);

/**
 * @brief Close and unlink a client's FIFO resources
 * @details
 * Used during both graceful and ungraceful disconnect cleanup.
 *
 * @param client client whose FIFO resources should be closed
 */
void client_close_io(ClientState *client);

/**
 * @brief Remove a client from the active registry
 * @details
 * Handles disconnect cleanup, releases any barriers waiting on that user, and
 * frees the client once no worker references remain.
 *
 * @param pid process ID of the client to remove
 */
void remove_client(pid_t pid);

/**
 * @brief Free all memory and locks owned by a client
 * @details
 * Also closes FIFO resources and frees any queued responses.
 *
 * @param client client state to destroy
 */
void cleanup_client(ClientState *client);

/* =========================================================================
 * Client list access helpers
 * ========================================================================= */

/**
 * @brief Get the number of active clients
 * @return current number of registered clients
 */
size_t get_client_count(void);

/**
 * @brief Get a client by active registry index
 * @param index index in the active client list
 * @return client at that index, or NULL if out of range
 */
ClientState *get_client_at_index(size_t index);

/* =========================================================================
 * Ordered response helpers
 * ========================================================================= */

/**
 * @brief Get the next request sequence number for a client
 * @details
 * Called when a request is read so replies can later be flushed in the same
 * order, even if worker threads finish out of order.
 *
 * @param client client receiving a new request
 * @return sequence number assigned to the request
 */
uint64_t client_next_request_seq(ClientState *client);

/**
 * @brief Store a completed response for ordered delivery
 * @details
 * Adds the response to the client's response queue using the request sequence
 * number.
 *
 * @param client client that should receive the response
 * @param seq sequence number of the completed request
 * @param response protocol response text without trailing newline
 * @return 0 on success, otherwise -1
 */
int client_store_response(ClientState *client,
                          uint64_t seq,
                          const char *response);

/**
 * @brief Flush ready responses to a client
 * @details
 * Writes queued responses only while the next expected sequence number is
 * available, preserving the assignment's per-client response order rule.
 *
 * @param client client whose response queue should be flushed
 */
void client_flush_responses(ClientState *client);

/* =========================================================================
 * User lookup helpers
 * ========================================================================= */

/**
 * @brief Check whether a logged in user is currently connected
 * @details
 * Used by sharing and barrier logic so disconnected clients do not block active
 * users forever.
 *
 * @param username username to search for
 * @return 1 if the user is connected and logged in, otherwise 0
 */
int is_user_connected(const char *username);

#endif