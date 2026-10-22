#include "handles.h"

#include <stdlib.h>
#include <string.h>

#define MAX_HANDLES 1024
#define MAX_SHARED_USERS 16

/* =========================================================================
 * Handle table state
 * ========================================================================= */

/*
 * The server gives clients uint64_t handle IDs instead of exposing raw
 * libanimate pointers directly. Handle 0 is kept invalid, as required by the
 * assignment brief.
 */
static HandleRecord *handle_table[MAX_HANDLES];
static uint64_t next_handle_id = 1;
static pthread_mutex_t handle_table_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef TESTING
/**
 * @brief Add a handle directly during tests
 * @details
 * Test builds can use this wrapper to seed the handle table without going
 * through a full RPC path.
 *
 * @param type type of handle to create
 * @param ptr backing libanimate pointer
 * @param owner client that owns the handle
 * @param out_id output location for the new handle ID
 * @return HANDLE_OK on success, otherwise an error code
 */
int handles_add_test_handle(HandleType type,
                            void *ptr,
                            const ClientState *owner,
                            uint64_t *out_id) {
    return handle_add(type, ptr, owner, out_id);
}
#endif

/* =========================================================================
 * Handle table setup and cleanup
 * ========================================================================= */

/**
 * @brief Initialise the global handle table
 * @details
 * Clears all handle slots and resets the next handle ID. This should be called
 * during server startup before RPCs begin creating canvases or sprites.
 *
 * @return HANDLE_OK on success
 */
int handles_init(void) {
    pthread_mutex_lock(&handle_table_lock);

    for (size_t i = 0; i < MAX_HANDLES; i++) {
        handle_table[i] = NULL;
    }

    next_handle_id = 1;

    pthread_mutex_unlock(&handle_table_lock);
    return HANDLE_OK;
}

/**
 * @brief Free a canvas share request list
 * @details
 * Share records track the two-sided share_canvas handshake for a canvas.
 *
 * @param share first share node to free
 */
static void free_canvas_shares(canvas_share_t *share) {
    while (share != NULL) {
        canvas_share_t *next = share->next;
        free(share);
        share = next;
    }
}

/**
 * @brief Free all barrier waiter records
 * @details
 * Barrier waiters are temporary per-generation records for users waiting at a
 * shared canvas barrier.
 *
 * @param waiter first waiter node to free
 */
static void free_barrier_waiters(BarrierWaiter *waiter) {
    while (waiter != NULL) {
        BarrierWaiter *next = waiter->next;
        free(waiter);
        waiter = next;
    }
}

/**
 * @brief Free one handle record
 * @details
 * Destroys the locks and condition variable owned by the handle record, then
 * frees any share or barrier metadata. The libanimate pointer itself is not
 * freed here because the RPC executor handles the matching destroy call.
 *
 * @param record handle record to free
 */
static void free_handle_record(HandleRecord *record) {
    if (record == NULL) {
        return;
    }

    pthread_mutex_destroy(&record->lock);
    pthread_cond_destroy(&record->barrier_cond);
    pthread_mutex_destroy(&record->barrier_mutex);

    free_canvas_shares(record->shares);
    free_barrier_waiters(record->barrier_waiters);

    free(record);
}

/**
 * @brief Free all handle metadata
 * @details
 * Clears the handle table during server shutdown or test cleanup. This removes
 * server side handle records and their synchronisation data.
 */
void handles_cleanup(void) {
    pthread_mutex_lock(&handle_table_lock);

    for (size_t i = 0; i < MAX_HANDLES; i++) {
        if (handle_table[i] != NULL) {
            free_handle_record(handle_table[i]);
            handle_table[i] = NULL;
        }
    }

    pthread_mutex_unlock(&handle_table_lock);
}

/* =========================================================================
 * Handle lookup and access helpers
 * ========================================================================= */

/**
 * @brief Find an active handle record by ID
 * @details
 * Searches the global handle table. The caller must hold handle_table_lock.
 *
 * @param id handle ID to find
 * @return matching active record, or NULL if not found
 */
static HandleRecord *find_record(uint64_t id) {
    for (size_t i = 0; i < MAX_HANDLES; i++) {
        if (handle_table[i] != NULL &&
            handle_table[i]->active &&
            handle_table[i]->id == id) {
            return handle_table[i];
        }
    }

    return NULL;
}

/**
 * @brief Check whether a client owns a handle
 * @details
 * Ownership can match by PID or username. Username matching is useful when
 * later RPC logic cares about the logged-in user rather than only the process.
 *
 * @param record handle record to check
 * @param client requesting client
 * @return 1 if the client owns the handle, otherwise 0
 */
static int client_matches_owner(const HandleRecord *record,
                                const ClientState *client) {
    if (record == NULL || client == NULL) {
        return 0;
    }

    if (record->owner_pid == client->pid) {
        return 1;
    }

    if (client->username[0] != '\0' &&
        strcmp(record->owner_username, client->username) == 0) {
        return 1;
    }

    return 0;
}

/**
 * @brief Check whether a client is in a handle's shared user list
 * @details
 * Shared access is username based because different clients may connect using
 * the same authorised username at different times.
 *
 * @param record handle record to check
 * @param client requesting client
 * @return 1 if the client is a shared user, otherwise 0
 */
static int client_is_shared_user(const HandleRecord *record,
                                 const ClientState *client) {
    if (record == NULL || client == NULL) {
        return 0;
    }

    if (client->username[0] == '\0') {
        return 0;
    }

    for (int i = 0; i < record->shared_count; i++) {
        if (strcmp(record->shared_users[i], client->username) == 0) {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief Add a username to a canvas shared user list
 * @details
 * Duplicate usernames are ignored. The list is capped to keep the handle
 * record fixed size and simple.
 *
 * @param record canvas handle record to update
 * @param username username to add
 * @return HANDLE_OK on success, otherwise HANDLE_INTERNAL
 */
static int add_shared_user(HandleRecord *record, const char *username) {
    if (record == NULL || username == NULL) {
        return HANDLE_INTERNAL;
    }

    for (int i = 0; i < record->shared_count; i++) {
        if (strcmp(record->shared_users[i], username) == 0) {
            return HANDLE_OK;
        }
    }

    if (record->shared_count >= MAX_SHARED_USERS) {
        return HANDLE_INTERNAL;
    }

    strncpy(record->shared_users[record->shared_count], username, 32);
    record->shared_users[record->shared_count][32] = '\0';
    record->shared_count++;

    return HANDLE_OK;
}

/**
 * @brief Check whether a client can access a handle
 * @details
 * A client can access a handle if they own it or are listed as a shared user.
 *
 * @param record handle record to check
 * @param client requesting client
 * @return 1 if access is allowed, otherwise 0
 */
static int client_has_access(const HandleRecord *record,
                             const ClientState *client) {
    return client_matches_owner(record, client) ||
           client_is_shared_user(record, client);
}

/**
 * @brief Check whether a username appeared earlier in the shared list
 * @details
 * Used by barrier counting to avoid counting duplicate shared users twice.
 *
 * @param record canvas handle record
 * @param username username to check
 * @param limit only entries before this index are checked
 * @return 1 if seen before, otherwise 0
 */
static int username_seen_before(const HandleRecord *record,
                                const char *username,
                                int limit) {
    for (int i = 0; i < limit; i++) {
        if (strcmp(record->shared_users[i], username) == 0) {
            return 1;
        }
    }

    return 0;
}

/* =========================================================================
 * Barrier counting helpers
 * ========================================================================= */

/**
 * @brief Count connected users required for the current canvas barrier
 * @details
 * Counts the connected owner and connected shared users. Disconnected users do
 * not block the barrier, matching the assignment disconnect requirement.
 *
 * @param record canvas handle record
 * @return number of connected users required to arrive
 */
static int canvas_required_barrier_count(const HandleRecord *record) {
    int count = 0;

    if (record == NULL || record->type != HANDLE_CANVAS) {
        return 0;
    }

    if (is_user_connected(record->owner_username)) {
        count++;
    }

    for (int i = 0; i < record->shared_count; i++) {
        const char *username = record->shared_users[i];

        if (strcmp(username, record->owner_username) == 0) {
            continue;
        }

        if (username_seen_before(record, username, i)) {
            continue;
        }

        if (is_user_connected(username)) {
            count++;
        }
    }

    return count;
}

/**
 * @brief Check whether a user has arrived at a barrier generation
 * @details
 * Barrier state is tracked per generation so a later barrier does not reuse
 * arrivals from an earlier barrier.
 *
 * @param record canvas handle record
 * @param username username to check
 * @param generation barrier generation to inspect
 * @return 1 if the user has arrived, otherwise 0
 */
static int barrier_user_arrived(const HandleRecord *record,
                                const char *username,
                                int generation) {
    if (record == NULL || username == NULL) {
        return 0;
    }

    BarrierWaiter *waiter = record->barrier_waiters;

    while (waiter != NULL) {
        if (waiter->arrived &&
            waiter->generation == generation &&
            strcmp(waiter->username, username) == 0) {
            return 1;
        }

        waiter = waiter->next;
    }

    return 0;
}

/**
 * @brief Count connected users that have reached a barrier generation
 * @details
 * This is compared against the required barrier count to decide whether the
 * barrier can release.
 *
 * @param record canvas handle record
 * @param generation barrier generation to inspect
 * @return number of connected arrived users
 */
static int barrier_arrival_count(const HandleRecord *record, int generation) {
    int count = 0;

    if (record == NULL || record->type != HANDLE_CANVAS) {
        return 0;
    }

    if (is_user_connected(record->owner_username) &&
        barrier_user_arrived(record, record->owner_username, generation)) {
        count++;
    }

    for (int i = 0; i < record->shared_count; i++) {
        const char *username = record->shared_users[i];

        if (strcmp(username, record->owner_username) == 0) {
            continue;
        }

        if (username_seen_before(record, username, i)) {
            continue;
        }

        if (is_user_connected(username) &&
            barrier_user_arrived(record, username, generation)) {
            count++;
        }
    }

    return count;
}

/**
 * @brief Remove waiter records for a completed barrier generation
 * @details
 * Once a barrier releases, old waiter nodes are no longer needed and must not
 * affect future barrier calls.
 *
 * @param record canvas handle record
 * @param generation completed barrier generation
 */
static void clear_barrier_generation(HandleRecord *record, int generation) {
    BarrierWaiter **current = &record->barrier_waiters;

    while (*current != NULL) {
        BarrierWaiter *waiter = *current;

        if (waiter->generation == generation) {
            *current = waiter->next;
            free(waiter);
        } else {
            current = &waiter->next;
        }
    }
}

/**
 * @brief Mark a user as arrived at a canvas barrier
 * @details
 * Updates an existing waiter node if present, otherwise creates a new one for
 * the current barrier generation.
 *
 * @param record canvas handle record
 * @param username username that reached the barrier
 * @param generation current barrier generation
 * @return HANDLE_OK on success, otherwise HANDLE_INTERNAL
 */
static int mark_barrier_arrived(HandleRecord *record,
                                const char *username,
                                int generation) {
    BarrierWaiter *waiter = record->barrier_waiters;

    while (waiter != NULL) {
        if (waiter->generation == generation &&
            strcmp(waiter->username, username) == 0) {
            waiter->arrived = 1;
            return HANDLE_OK;
        }

        waiter = waiter->next;
    }

    waiter = calloc(1, sizeof(*waiter));
    if (waiter == NULL) {
        return HANDLE_INTERNAL;
    }

    strncpy(waiter->username, username, sizeof(waiter->username) - 1);
    waiter->generation = generation;
    waiter->arrived = 1;

    waiter->next = record->barrier_waiters;
    record->barrier_waiters = waiter;

    return HANDLE_OK;
}

/* =========================================================================
 * Handle creation and basic access
 * ========================================================================= */

/**
 * @brief Add a new handle record to the table
 * @details
 * Creates a server side uint64_t handle for a libanimate object and records
 * its owner. The ID returned to the client is never 0.
 *
 * @param type kind of object being stored
 * @param ptr backing libanimate pointer
 * @param owner client that owns the object
 * @param out_id output location for the created handle ID
 * @return HANDLE_OK on success, otherwise an error code
 */
int handle_add(HandleType type,
               void *ptr,
               const ClientState *owner,
               uint64_t *out_id) {
    HandleRecord *record;
    size_t slot = MAX_HANDLES;

    if (ptr == NULL || owner == NULL || out_id == NULL) {
        return HANDLE_INTERNAL;
    }

    record = calloc(1, sizeof(*record));
    if (record == NULL) {
        return HANDLE_INTERNAL;
    }

    record->type = type;
    record->ptr = ptr;
    record->owner_pid = owner->pid;
    strncpy(record->owner_username, owner->username,
            sizeof(record->owner_username) - 1);
    record->active = 1;
    record->use_count = 0;
    record->shared_count = 0;
    record->shares = NULL;
    record->barrier_generation = 0;
    record->barrier_waiters = NULL;

    if (pthread_mutex_init(&record->lock, NULL) != 0) {
        free(record);
        return HANDLE_INTERNAL;
    }

    if (pthread_mutex_init(&record->barrier_mutex, NULL) != 0) {
        pthread_mutex_destroy(&record->lock);
        free(record);
        return HANDLE_INTERNAL;
    }

    if (pthread_cond_init(&record->barrier_cond, NULL) != 0) {
        pthread_mutex_destroy(&record->barrier_mutex);
        pthread_mutex_destroy(&record->lock);
        free(record);
        return HANDLE_INTERNAL;
    }

    pthread_mutex_lock(&handle_table_lock);

    for (size_t i = 0; i < MAX_HANDLES; i++) {
        if (handle_table[i] == NULL) {
            slot = i;
            break;
        }
    }

    if (slot == MAX_HANDLES) {
        pthread_mutex_unlock(&handle_table_lock);
        free_handle_record(record);
        return HANDLE_INTERNAL;
    }

    record->id = next_handle_id++;
    if (record->id == 0) {
        record->id = next_handle_id++;
    }

    handle_table[slot] = record;
    *out_id = record->id;

    pthread_mutex_unlock(&handle_table_lock);

    return HANDLE_OK;
}

/**
 * @brief Look up a handle pointer after validating access
 * @details
 * Validates that the ID is non-zero, active, the expected type, and accessible
 * by the requesting client. The object is not left locked after lookup.
 *
 * @param id handle ID supplied by the client
 * @param expected_type required handle type
 * @param client requesting client
 * @param out_ptr output location for the backing pointer
 * @return HANDLE_OK on success, otherwise HANDLE_INVALID
 */
int handle_lookup(uint64_t id,
                  HandleType expected_type,
                  const ClientState *client,
                  void **out_ptr) {
    HandleRecord *record;

    if (id == 0 || client == NULL || out_ptr == NULL) {
        return HANDLE_INVALID;
    }

    pthread_mutex_lock(&handle_table_lock);
    record = find_record(id);

    if (record == NULL ||
        record->type != expected_type ||
        !client_has_access(record, client)) {
        pthread_mutex_unlock(&handle_table_lock);
        return HANDLE_INVALID;
    }

    pthread_mutex_lock(&record->lock);
    *out_ptr = record->ptr;
    pthread_mutex_unlock(&record->lock);

    pthread_mutex_unlock(&handle_table_lock);

    return HANDLE_OK;
}

/**
 * @brief Remove a handle from the table
 * @details
 * Validates access, removes the record from the table, and returns the backing
 * pointer to the caller so the RPC layer can call the correct libanimate
 * destroy function.
 *
 * @param id handle ID to remove
 * @param expected_type required handle type
 * @param client requesting client
 * @param out_ptr output location for the backing pointer
 * @return HANDLE_OK on success, otherwise HANDLE_INVALID
 */
int handle_remove(uint64_t id,
                  HandleType expected_type,
                  const ClientState *client,
                  void **out_ptr) {
    HandleRecord *record;

    if (id == 0 || client == NULL || out_ptr == NULL) {
        return HANDLE_INVALID;
    }

    pthread_mutex_lock(&handle_table_lock);
    record = find_record(id);

    if (record == NULL ||
        record->type != expected_type ||
        !client_has_access(record, client)) {
        pthread_mutex_unlock(&handle_table_lock);
        return HANDLE_INVALID;
    }

    /*
     * Shared users cannot destroy a shared canvas. This avoids one client
     * deleting a canvas while another user still has access.
     */
    if (record->type == HANDLE_CANVAS &&
        !client_matches_owner(record, client) &&
        record->shared_count > 0) {
        pthread_mutex_unlock(&handle_table_lock);
        return HANDLE_INVALID;
    }

    /* Do not destroy a canvas while clients are waiting at its barrier. */
    if (record->type == HANDLE_CANVAS && record->barrier_waiters != NULL) {
        pthread_mutex_unlock(&handle_table_lock);
        return HANDLE_INVALID;
    }

    *out_ptr = record->ptr;
    record->active = 0;

    for (size_t i = 0; i < MAX_HANDLES; i++) {
        if (handle_table[i] == record) {
            handle_table[i] = NULL;
            break;
        }
    }

    pthread_mutex_unlock(&handle_table_lock);

    free_handle_record(record);

    return HANDLE_OK;
}

/**
 * @brief Lock a handle and return its backing pointer
 * @details
 * Used for operations that need to safely call libanimate on a shared object.
 * The caller must later call handle_unlock with the same handle ID.
 *
 * @param id handle ID to lock
 * @param expected_type required handle type
 * @param client requesting client
 * @param out_ptr output location for the backing pointer
 * @return HANDLE_OK on success, otherwise HANDLE_INVALID
 */
int handle_lock(uint64_t id,
                HandleType expected_type,
                const ClientState *client,
                void **out_ptr) {
    HandleRecord *record;

    if (id == 0 || client == NULL || out_ptr == NULL) {
        return HANDLE_INVALID;
    }

    pthread_mutex_lock(&handle_table_lock);
    record = find_record(id);

    if (record == NULL ||
        record->type != expected_type ||
        !client_has_access(record, client)) {
        pthread_mutex_unlock(&handle_table_lock);
        return HANDLE_INVALID;
    }

    pthread_mutex_lock(&record->lock);
    *out_ptr = record->ptr;

    pthread_mutex_unlock(&handle_table_lock);

    return HANDLE_OK;
}

/**
 * @brief Unlock a previously locked handle
 * @details
 * Releases the per-handle mutex acquired by handle_lock.
 *
 * @param id handle ID to unlock
 */
void handle_unlock(uint64_t id) {
    HandleRecord *record;

    if (id == 0) {
        return;
    }

    pthread_mutex_lock(&handle_table_lock);
    record = find_record(id);

    if (record != NULL) {
        pthread_mutex_unlock(&record->lock);
    }

    pthread_mutex_unlock(&handle_table_lock);
}

/* =========================================================================
 * Placement parent tracking
 * ========================================================================= */

/**
 * @brief Add a placement handle with its parent canvas ID
 * @details
 * Placements belong to a canvas. Storing the parent canvas ID lets placement
 * operations find and lock the related canvas correctly.
 *
 * @param ptr backing sprite placement pointer
 * @param owner client that owns the placement
 * @param parent_canvas_id canvas handle that owns the placement
 * @param out_id output location for the new placement handle
 * @return HANDLE_OK on success, otherwise an error code
 */
int handle_add_placement(void *ptr,
                         const ClientState *owner,
                         uint64_t parent_canvas_id,
                         uint64_t *out_id) {
    int result;
    HandleRecord *record;

    result = handle_add(HANDLE_PLACEMENT, ptr, owner, out_id);
    if (result != HANDLE_OK) {
        return result;
    }

    pthread_mutex_lock(&handle_table_lock);
    record = find_record(*out_id);

    if (record != NULL) {
        record->parent_canvas_id = parent_canvas_id;
    }

    pthread_mutex_unlock(&handle_table_lock);

    return HANDLE_OK;
}

/**
 * @brief Get the parent canvas ID for a placement
 * @details
 * Validates that the placement exists and the client can access it before
 * returning the canvas handle associated with that placement.
 *
 * @param placement_id placement handle ID
 * @param client requesting client
 * @return parent canvas handle ID, or 0 if invalid
 */
uint64_t handle_get_parent_canvas(uint64_t placement_id,
                                  const ClientState *client) {
    HandleRecord *record;
    uint64_t parent = 0;

    if (placement_id == 0 || client == NULL) {
        return 0;
    }

    pthread_mutex_lock(&handle_table_lock);
    record = find_record(placement_id);

    if (record != NULL &&
        record->type == HANDLE_PLACEMENT &&
        client_has_access(record, client)) {
        parent = record->parent_canvas_id;
    }

    pthread_mutex_unlock(&handle_table_lock);

    return parent;
}

/* =========================================================================
 * Canvas sharing
 * ========================================================================= */

/**
 * @brief Register or complete a two-sided canvas share
 * @details
 * A canvas is only fully shared after both users issue compatible share_canvas
 * requests. The owner or an already authorised user may start the share, and
 * the target user can complete it by naming the original requester.
 *
 * @param id canvas handle ID
 * @param client requesting client
 * @param username other username in the share request
 * @return HANDLE_OK on success, HANDLE_INVALID for invalid access, otherwise
 * HANDLE_INTERNAL
 */
int handle_share_with_user(uint64_t id,
                           const ClientState *client,
                           const char *username) {
    HandleRecord *record;
    canvas_share_t *share;

    if (id == 0 || client == NULL || username == NULL ||
        username[0] == '\0' || strlen(username) > 32 ||
        client->username[0] == '\0') {
        return HANDLE_INVALID;
    }

    pthread_mutex_lock(&handle_table_lock);
    record = find_record(id);

    if (record == NULL || record->type != HANDLE_CANVAS) {
        pthread_mutex_unlock(&handle_table_lock);
        return HANDLE_INVALID;
    }

    pthread_mutex_lock(&record->lock);
    pthread_mutex_unlock(&handle_table_lock);

    int allowed = client_has_access(record, client);

    /*
     * The target user is allowed to complete a pending share even before they
     * have access to the canvas.
     */
    for (share = record->shares; share != NULL; share = share->next) {
        if (strcmp(share->user_a, username) == 0 &&
            strcmp(share->user_b, client->username) == 0 &&
            share->user_a_requested) {
            allowed = 1;
            break;
        }
    }

    if (!allowed) {
        pthread_mutex_unlock(&record->lock);
        return HANDLE_INVALID;
    }

    for (share = record->shares; share != NULL; share = share->next) {
        int same_pair =
            (strcmp(share->user_a, client->username) == 0 &&
             strcmp(share->user_b, username) == 0) ||
            (strcmp(share->user_a, username) == 0 &&
             strcmp(share->user_b, client->username) == 0);

        if (same_pair) {
            if (strcmp(share->user_a, client->username) == 0) {
                share->user_a_requested = 1;
            } else {
                share->user_b_requested = 1;
            }

            if (share->user_a_requested && share->user_b_requested) {
                share->active = 1;

                if (add_shared_user(record, share->user_a) != HANDLE_OK ||
                    add_shared_user(record, share->user_b) != HANDLE_OK) {
                    pthread_mutex_unlock(&record->lock);
                    return HANDLE_INTERNAL;
                }
            }

            pthread_mutex_unlock(&record->lock);
            return HANDLE_OK;
        }
    }

    share = calloc(1, sizeof(*share));
    if (share == NULL) {
        pthread_mutex_unlock(&record->lock);
        return HANDLE_INTERNAL;
    }

    strncpy(share->user_a, client->username, sizeof(share->user_a) - 1);
    strncpy(share->user_b, username, sizeof(share->user_b) - 1);

    share->user_a_requested = 1;
    share->user_b_requested = 0;
    share->active = 0;

    share->next = record->shares;
    record->shares = share;

    pthread_mutex_unlock(&record->lock);
    return HANDLE_OK;
}

/**
 * @brief Check whether a client has access to a handle
 * @details
 * Lightweight access check for RPC code that needs a yes/no answer before
 * doing other work.
 *
 * @param id handle ID to check
 * @param expected_type required handle type
 * @param client requesting client
 * @return 1 if the client has access, otherwise 0
 */
int handle_user_has_access(uint64_t id,
                           HandleType expected_type,
                           const ClientState *client) {
    HandleRecord *record;
    int result = 0;

    if (id == 0 || client == NULL) {
        return 0;
    }

    pthread_mutex_lock(&handle_table_lock);
    record = find_record(id);

    if (record != NULL &&
        record->type == expected_type &&
        client_has_access(record, client)) {
        result = 1;
    }

    pthread_mutex_unlock(&handle_table_lock);

    return result;
}

/* =========================================================================
 * Canvas barrier synchronisation
 * ========================================================================= */

/**
 * @brief Block later canvas work while this user is waiting at a barrier
 * @details
 * If the user has already arrived at the current barrier generation, later
 * operations on the same canvas wait until the barrier releases.
 *
 * @param canvas_id canvas handle ID
 * @param client requesting client
 * @return HANDLE_OK on success, otherwise HANDLE_INVALID
 */
int handle_wait_if_blocked_by_barrier(uint64_t canvas_id,
                                      const ClientState *client) {
    HandleRecord *record;

    if (canvas_id == 0 || client == NULL || client->username[0] == '\0') {
        return HANDLE_INVALID;
    }

    pthread_mutex_lock(&handle_table_lock);
    record = find_record(canvas_id);

    if (record == NULL ||
        record->type != HANDLE_CANVAS ||
        !client_has_access(record, client)) {
        pthread_mutex_unlock(&handle_table_lock);
        return HANDLE_INVALID;
    }

    pthread_mutex_lock(&record->barrier_mutex);
    pthread_mutex_unlock(&handle_table_lock);

    while (barrier_user_arrived(record,
                                client->username,
                                record->barrier_generation)) {
        pthread_cond_wait(&record->barrier_cond, &record->barrier_mutex);
    }

    pthread_mutex_unlock(&record->barrier_mutex);

    return HANDLE_OK;
}

/**
 * @brief Mark a client as arrived at a canvas barrier
 * @details
 * Waits until all connected users with access to the canvas have also arrived.
 * Once everyone required is present, the barrier generation advances and all
 * waiting clients are released.
 *
 * @param canvas_id canvas handle ID
 * @param client requesting client
 * @return HANDLE_OK on success, otherwise an error code
 */
int handle_barrier_arrive(uint64_t canvas_id,
                          const ClientState *client) {
    HandleRecord *record;
    int generation;
    int result;

    if (canvas_id == 0 || client == NULL || client->username[0] == '\0') {
        return HANDLE_INVALID;
    }

    pthread_mutex_lock(&handle_table_lock);
    record = find_record(canvas_id);

    if (record == NULL ||
        record->type != HANDLE_CANVAS ||
        !client_has_access(record, client)) {
        pthread_mutex_unlock(&handle_table_lock);
        return HANDLE_INVALID;
    }

    pthread_mutex_lock(&record->barrier_mutex);
    pthread_mutex_unlock(&handle_table_lock);

    generation = record->barrier_generation;

    result = mark_barrier_arrived(record, client->username, generation);
    if (result != HANDLE_OK) {
        pthread_mutex_unlock(&record->barrier_mutex);
        return result;
    }

    pthread_cond_broadcast(&record->barrier_cond);

    while (record->barrier_generation == generation &&
           barrier_arrival_count(record, generation) <
           canvas_required_barrier_count(record)) {
        pthread_cond_wait(&record->barrier_cond, &record->barrier_mutex);
    }

    if (record->barrier_generation == generation) {
        record->barrier_generation++;
        clear_barrier_generation(record, generation);
        pthread_cond_broadcast(&record->barrier_cond);
    }

    pthread_mutex_unlock(&record->barrier_mutex);

    return HANDLE_OK;
}

/**
 * @brief Wake barriers after a client disconnects
 * @details
 * When a user disconnects, barriers may no longer need that user to arrive.
 * Broadcasting every canvas barrier condition is simple and safe enough here,
 * because each waiting thread rechecks the barrier condition before continuing.
 *
 * @param username username that disconnected
 */
void handle_barrier_client_disconnected(const char *username) {
    if (username == NULL || username[0] == '\0') {
        return;
    }

    pthread_mutex_lock(&handle_table_lock);

    for (size_t i = 0; i < MAX_HANDLES; i++) {
        HandleRecord *record = handle_table[i];

        if (record != NULL &&
            record->active &&
            record->type == HANDLE_CANVAS) {
            pthread_mutex_lock(&record->barrier_mutex);
            pthread_cond_broadcast(&record->barrier_cond);
            pthread_mutex_unlock(&record->barrier_mutex);
        }
    }

    pthread_mutex_unlock(&handle_table_lock);
}

/* =========================================================================
 * Canvas dimension metadata
 * ========================================================================= */

/**
 * @brief Store the dimensions for a canvas handle
 * @details
 * libanimate can report frame size but the generate RPC also needs width and
 * height for ffmpeg's raw video input. This metadata is saved when the canvas
 * is created.
 *
 * @param id canvas handle ID
 * @param width canvas width in pixels
 * @param height canvas height in pixels
 * @return HANDLE_OK on success, otherwise HANDLE_INVALID
 */
int handle_set_canvas_dimensions(uint64_t id,
                                 size_t width,
                                 size_t height) {
    HandleRecord *record;

    if (id == 0 || width == 0 || height == 0) {
        return HANDLE_INVALID;
    }

    pthread_mutex_lock(&handle_table_lock);

    record = find_record(id);
    if (record == NULL || record->type != HANDLE_CANVAS) {
        pthread_mutex_unlock(&handle_table_lock);
        return HANDLE_INVALID;
    }

    record->width = width;
    record->height = height;

    pthread_mutex_unlock(&handle_table_lock);

    return HANDLE_OK;
}

/**
 * @brief Get stored dimensions for an accessible canvas
 * @details
 * Validates the canvas handle and client access before returning the dimensions
 * needed by generate.
 *
 * @param id canvas handle ID
 * @param client requesting client
 * @param out_width output location for canvas width
 * @param out_height output location for canvas height
 * @return HANDLE_OK on success, otherwise HANDLE_INVALID
 */
int handle_get_canvas_dimensions(uint64_t id,
                                 const ClientState *client,
                                 size_t *out_width,
                                 size_t *out_height) {
    HandleRecord *record;

    if (id == 0 || client == NULL || out_width == NULL || out_height == NULL) {
        return HANDLE_INVALID;
    }

    pthread_mutex_lock(&handle_table_lock);

    record = find_record(id);
    if (record == NULL ||
        record->type != HANDLE_CANVAS ||
        !client_has_access(record, client) ||
        record->width == 0 ||
        record->height == 0) {
        pthread_mutex_unlock(&handle_table_lock);
        return HANDLE_INVALID;
    }

    *out_width = record->width;
    *out_height = record->height;

    pthread_mutex_unlock(&handle_table_lock);

    return HANDLE_OK;
}