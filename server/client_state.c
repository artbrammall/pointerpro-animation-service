#define _POSIX_C_SOURCE 200809L

#include "client_state.h"
#include "handles.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_CLIENTS 128

/* =========================================================================
 * Connected client registry
 * ========================================================================= */

/*
 * The server keeps one ClientState for each connected client.
 * The thread pool is separate from this list, so the number of clients does
 * not cause the number of worker threads to grow.
 */
static ClientState *clients[MAX_CLIENTS];
static size_t client_count = 0;

/* =========================================================================
 * Client liveness checks
 * ========================================================================= */

/**
 * @brief Check whether a client process still exists
 * @details
 * Uses kill(pid, 0), which does not send a signal but asks the kernel whether
 * the process can be reached. ESRCH means the process is gone. EPERM still
 * means the process exists, but this server does not have permission to signal
 * it.
 *
 * @param pid client process ID to check
 * @return 1 if the process appears alive, otherwise 0
 */
int client_process_alive(pid_t pid) {
    if (kill(pid, 0) == 0) {
        return 1;
    }

    if (errno == ESRCH) {
        return 0;
    }

    if (errno == EPERM) {
        return 1;
    }

    return 1;
}

/**
 * @brief Perform one pass of ungraceful disconnect detection
 * @details
 * Walks the connected client list and removes any client whose process has
 * died without sending the Disconnect RPC. This supports the assignment
 * requirement that dead clients are treated like normal disconnects.
 */
void check_client_liveness_once(void) {
    size_t i = 0;

    while (i < client_count) {
        ClientState *client = clients[i];

        if (client != NULL && !client->disconnected) {
            if (!client_process_alive(client->pid)) {
                remove_client(client->pid);
                continue;
            }
        }

        i++;
    }
}

/* =========================================================================
 * Client creation and registration
 * ========================================================================= */

/**
 * @brief Create a ClientState and open its FIFO endpoints
 * @details
 * Allocates the server side state for one client, stores its FIFO paths,
 * initialises locks used for ordered responses and lifetime tracking, then
 * opens the client to server FIFO for reading and the server to client FIFO
 * for writing.
 *
 * @param pid client process ID
 * @param c2s_path path to FIFO_C2S_<client_pid>
 * @param s2c_path path to FIFO_S2C_<client_pid>
 * @return newly allocated client state, or NULL on failure
 */
ClientState *create_client(pid_t pid,
                           const char *c2s_path,
                           const char *s2c_path) {
    ClientState *client = calloc(1, sizeof(ClientState));
    if (client == NULL) {
        return NULL;
    }

    client->pid = pid;
    client->logged_in = 0;
    client->disconnected = 0;
    client->c2s_fd = -1;
    client->s2c_fd = -1;
    client->next_request_seq = 0;
    client->next_response_seq = 0;
    client->responses = NULL;
    client->ref_count = 0;
    client->pending_free = 0;

    if (pthread_mutex_init(&client->response_lock, NULL) != 0) {
        free(client);
        return NULL;
    }

    if (pthread_mutex_init(&client->lifetime_lock, NULL) != 0) {
        pthread_mutex_destroy(&client->response_lock);
        free(client);
        return NULL;
    }

    strncpy(client->c2s_path, c2s_path, sizeof(client->c2s_path) - 1);
    strncpy(client->s2c_path, s2c_path, sizeof(client->s2c_path) - 1);

    /*
     * The server reads requests from C2S. Non blocking mode lets the server
     * monitor multiple clients without getting stuck on one quiet FIFO.
     */
    client->c2s_fd = open(client->c2s_path, O_RDONLY | O_NONBLOCK);
    if (client->c2s_fd == -1) {
        cleanup_client(client);
        return NULL;
    }

    /*
     * The server writes replies to S2C. The client should already have opened
     * the read end after receiving SIGUSR2.
     */
    client->s2c_fd = open(client->s2c_path, O_WRONLY);
    if (client->s2c_fd == -1) {
        cleanup_client(client);
        return NULL;
    }

    return client;
}

/**
 * @brief Add a client to the server registry
 * @details
 * Stores the new client in the fixed size client array so it can be found by
 * the main server loop, request handling code, and liveness checks.
 *
 * @param client client state to register
 * @return 0 on success, otherwise -1
 */
int add_client(ClientState *client) {
    if (client == NULL) {
        return -1;
    }

    if (client_count >= MAX_CLIENTS) {
        return -1;
    }

    clients[client_count] = client;
    client_count++;

    return 0;
}

/* =========================================================================
 * Client lifetime helpers
 * ========================================================================= */

/**
 * @brief Acquire a temporary reference to a client
 * @details
 * Worker threads can use this before storing a response or touching client
 * state. A client that is already disconnected or waiting to be freed cannot
 * be acquired.
 *
 * @param client client state to acquire
 * @return 1 if the reference was acquired, otherwise 0
 */
int client_acquire(ClientState *client) {
    int ok = 0;

    if (client == NULL) {
        return 0;
    }

    pthread_mutex_lock(&client->lifetime_lock);

    if (!client->disconnected && !client->pending_free) {
        client->ref_count++;
        ok = 1;
    }

    pthread_mutex_unlock(&client->lifetime_lock);

    return ok;
}

/**
 * @brief Mark a client as disconnected
 * @details
 * Prevents new references and response writes from being accepted. The actual
 * free may be delayed until worker threads release existing references.
 *
 * @param client client state to mark as disconnected
 */
void client_mark_disconnected(ClientState *client) {
    if (client == NULL) {
        return;
    }

    pthread_mutex_lock(&client->lifetime_lock);
    client->disconnected = 1;
    client->pending_free = 1;
    pthread_mutex_unlock(&client->lifetime_lock);
}

/**
 * @brief Release a temporary client reference
 * @details
 * Decrements the reference count after a worker has finished with the client.
 * If the client was already removed and this was the final reference, the
 * client state is cleaned up here.
 *
 * @param client client state to release
 */
void client_release(ClientState *client) {
    int should_free = 0;

    if (client == NULL) {
        return;
    }

    pthread_mutex_lock(&client->lifetime_lock);

    if (client->ref_count > 0) {
        client->ref_count--;
    }

    if (client->pending_free && client->ref_count == 0) {
        should_free = 1;
    }

    pthread_mutex_unlock(&client->lifetime_lock);

    if (should_free) {
        cleanup_client(client);
    }
}

/* =========================================================================
 * Client lookup helpers
 * ========================================================================= */

/**
 * @brief Find a connected client by process ID
 * @details
 * Used when signal handling, disconnect handling, or liveness code needs to
 * map a PID back to the server side client state.
 *
 * @param pid client process ID
 * @return matching client, or NULL if not found
 */
ClientState *find_client_by_pid(pid_t pid) {
    for (size_t i = 0; i < client_count; i++) {
        if (clients[i] != NULL && clients[i]->pid == pid) {
            return clients[i];
        }
    }

    return NULL;
}

/**
 * @brief Find a connected client by its request FIFO descriptor
 * @details
 * Used by server polling code after a readable file descriptor is detected.
 * The descriptor belongs to FIFO_C2S_<client_pid>.
 *
 * @param fd client to server FIFO descriptor
 * @return matching client, or NULL if not found
 */
ClientState *find_client_by_fd(int fd) {
    for (size_t i = 0; i < client_count; i++) {
        if (clients[i] != NULL && clients[i]->c2s_fd == fd) {
            return clients[i];
        }
    }

    return NULL;
}

/* =========================================================================
 * Client removal and cleanup
 * ========================================================================= */

/**
 * @brief Remove a client from the active registry
 * @details
 * Marks the client as disconnected, removes it from the active client array,
 * releases any barriers waiting on that user's presence, closes its FIFOs, and
 * frees the client once no worker thread still holds a reference.
 *
 * @param pid process ID of the client to remove
 */
void remove_client(pid_t pid) {
    for (size_t i = 0; i < client_count; i++) {
        ClientState *client = clients[i];

        if (client != NULL && client->pid == pid) {
            char username[33];
            int should_free = 0;

            /*
             * Save the username before cleanup so barrier logic can release
             * shared canvases waiting on this disconnected user.
             */
            strncpy(username, client->username, sizeof(username) - 1);
            username[sizeof(username) - 1] = '\0';

            client_mark_disconnected(client);

            /* Compact the registry so later scans do not leave a hole. */
            for (size_t j = i; j + 1 < client_count; j++) {
                clients[j] = clients[j + 1];
            }

            clients[client_count - 1] = NULL;
            client_count--;

            handle_barrier_client_disconnected(username);

            client_close_io(client);

            pthread_mutex_lock(&client->lifetime_lock);
            if (client->ref_count == 0) {
                should_free = 1;
            }
            pthread_mutex_unlock(&client->lifetime_lock);

            if (should_free) {
                cleanup_client(client);
            }

            return;
        }
    }
}

/**
 * @brief Close and unlink a client's FIFO resources
 * @details
 * Closes any open FIFO descriptors and removes the FIFO paths from the file
 * system. This is needed for both graceful and ungraceful disconnect cleanup.
 *
 * @param client client whose FIFO resources should be closed
 */
void client_close_io(ClientState *client) {
    if (client == NULL) {
        return;
    }

    if (client->c2s_fd != -1) {
        close(client->c2s_fd);
        client->c2s_fd = -1;
    }

    if (client->s2c_fd != -1) {
        close(client->s2c_fd);
        client->s2c_fd = -1;
    }

    if (client->c2s_path[0] != '\0') {
        unlink(client->c2s_path);
        client->c2s_path[0] = '\0';
    }

    if (client->s2c_path[0] != '\0') {
        unlink(client->s2c_path);
        client->s2c_path[0] = '\0';
    }
}

/**
 * @brief Free all resources owned by a ClientState
 * @details
 * Closes FIFO resources, frees any queued responses that were not sent yet,
 * destroys mutexes, and releases the ClientState allocation itself.
 *
 * @param client client state to destroy
 */
void cleanup_client(ClientState *client) {
    if (client == NULL) {
        return;
    }

    client_close_io(client);

    ResponseNode *current = client->responses;

    while (current != NULL) {
        ResponseNode *next = current->next;
        free(current);
        current = next;
    }

    pthread_mutex_destroy(&client->response_lock);
    pthread_mutex_destroy(&client->lifetime_lock);

    free(client);
}

/* =========================================================================
 * Ordered response handling
 * ========================================================================= */

/**
 * @brief Allocate the next request sequence number for a client
 * @details
 * Each request from a client gets a sequence number when it is read. Workers
 * may finish out of order, but these numbers let the server send replies back
 * to that client in request order.
 *
 * @param client client receiving the request
 * @return sequence number for the request
 */
uint64_t client_next_request_seq(ClientState *client) {
    uint64_t seq;

    pthread_mutex_lock(&client->response_lock);
    seq = client->next_request_seq;
    client->next_request_seq++;
    pthread_mutex_unlock(&client->response_lock);

    return seq;
}

/**
 * @brief Store a completed response for ordered output
 * @details
 * Inserts the response into the client's response list sorted by sequence
 * number. The flush step will only send responses when all earlier replies for
 * that client have already been sent.
 *
 * @param client client that should receive the response
 * @param seq request sequence number associated with the response
 * @param response protocol response text without the trailing newline
 * @return 0 on success, otherwise -1
 */
int client_store_response(ClientState *client,
                          uint64_t seq,
                          const char *response) {
    ResponseNode *node;
    ResponseNode **current;

    if (client == NULL || response == NULL) {
        return -1;
    }

    pthread_mutex_lock(&client->lifetime_lock);
    if (client->disconnected || client->pending_free) {
        pthread_mutex_unlock(&client->lifetime_lock);
        return -1;
    }
    pthread_mutex_unlock(&client->lifetime_lock);

    node = malloc(sizeof(*node));
    if (node == NULL) {
        return -1;
    }

    node->seq = seq;
    snprintf(node->response, sizeof(node->response), "%s", response);
    node->next = NULL;

    pthread_mutex_lock(&client->response_lock);

    current = &client->responses;

    while (*current != NULL && (*current)->seq < seq) {
        current = &(*current)->next;
    }

    node->next = *current;
    *current = node;

    pthread_mutex_unlock(&client->response_lock);

    return 0;
}

/**
 * @brief Send all ready responses to a client in request order
 * @details
 * Writes responses to FIFO_S2C_<client_pid> only while the first queued
 * response matches the next expected sequence number. Later responses stay
 * buffered until the missing earlier response arrives.
 *
 * @param client client whose queued responses should be flushed
 */
void client_flush_responses(ClientState *client) {
    if (client == NULL) {
        return;
    }

    pthread_mutex_lock(&client->lifetime_lock);
    if (client->disconnected || client->pending_free || client->s2c_fd == -1) {
        pthread_mutex_unlock(&client->lifetime_lock);
        return;
    }
    pthread_mutex_unlock(&client->lifetime_lock);

    pthread_mutex_lock(&client->response_lock);

    while (client->responses != NULL &&
           client->responses->seq == client->next_response_seq) {
        ResponseNode *node = client->responses;

        dprintf(client->s2c_fd, "%s\n", node->response);

        client->responses = node->next;
        client->next_response_seq++;

        free(node);
    }

    pthread_mutex_unlock(&client->response_lock);
}

/* =========================================================================
 * Client list access helpers
 * ========================================================================= */

/**
 * @brief Get the number of active clients
 * @details
 * Used by server loops that need to scan the current client registry.
 *
 * @return number of clients currently stored in the registry
 */
size_t get_client_count(void) {
    return client_count;
}

/**
 * @brief Get a client by registry index
 * @details
 * Provides indexed access to the active client array. Returns NULL for out of
 * range indexes so callers can fail safely.
 *
 * @param index index into the active client list
 * @return client at that index, or NULL if the index is invalid
 */
ClientState *get_client_at_index(size_t index) {
    if (index >= client_count) {
        return NULL;
    }

    return clients[index];
}

/**
 * @brief Check whether a logged in user is currently connected
 * @details
 * Searches active clients for a matching username. This is useful for sharing
 * and barrier logic where disconnected users should not block active clients.
 *
 * @param username username to search for
 * @return 1 if a logged in matching client is connected, otherwise 0
 */
int is_user_connected(const char *username) {
    if (username == NULL || username[0] == '\0') {
        return 0;
    }

    for (size_t i = 0; i < client_count; i++) {
        if (clients[i] != NULL &&
            clients[i]->logged_in &&
            !clients[i]->disconnected &&
            strcmp(clients[i]->username, username) == 0) {
            return 1;
        }
    }

    return 0;
}