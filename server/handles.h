#ifndef HANDLES_H
#define HANDLES_H

#include "client_state.h"

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Handle result codes
 * ========================================================================= */

/*
 * These values map closely to the assignment RPC response codes.
 * HANDLE_INVALID becomes "-2" for value errors.
 * HANDLE_INTERNAL becomes "-3" for internal server errors.
 */
#define HANDLE_OK 0
#define HANDLE_INVALID -2
#define HANDLE_INTERNAL -3

/* =========================================================================
 * Handle types
 * ========================================================================= */

/**
 * @brief Type tag for a server side handle
 * @details
 * Each client supplied handle must match the expected type before the backing
 * libanimate pointer is used. This prevents a sprite handle being treated like
 * a canvas handle, which would be absolute clown shoes.
 */
typedef enum {
    HANDLE_CANVAS = 1,
    HANDLE_SPRITE,
    HANDLE_PLACEMENT
} HandleType;

#ifdef TESTING
/**
 * @brief Add a handle directly for tests
 * @details
 * Test builds can use this to seed the handle table without going through the
 * full RPC execution path.
 *
 * @param type type of handle to create
 * @param ptr backing object pointer
 * @param owner client that owns the handle
 * @param out_id output location for the created handle ID
 * @return HANDLE_OK on success, otherwise an error code
 */
int handles_add_test_handle(HandleType type,
                            void *ptr,
                            const ClientState *owner,
                            uint64_t *out_id);
#endif

/* =========================================================================
 * Barrier and sharing metadata
 * ========================================================================= */

/**
 * @brief One user's arrival state for a canvas barrier
 * @details
 * Barrier waits are tracked per username and per generation. The generation
 * value prevents arrivals from an old barrier being reused by a later barrier.
 */
typedef struct BarrierWaiter {
    char username[33];

    int generation;
    int arrived;

    struct BarrierWaiter *next;
} BarrierWaiter;

/**
 * @brief Pending or active two-user canvas share request
 * @details
 * share_canvas requires both users to issue compatible requests. This record
 * stores each side of that handshake before the canvas becomes shared.
 */
typedef struct canvas_share {
    char user_a[33];
    char user_b[33];
    int user_a_requested;
    int user_b_requested;
    int active;
    struct canvas_share *next;
} canvas_share_t;

/* =========================================================================
 * Handle record
 * ========================================================================= */

/**
 * @brief Server side record for one client-visible handle
 * @details
 * Stores the actual libanimate pointer, ownership details, sharing state,
 * optional parent canvas metadata for placements, canvas dimensions for
 * generate, and locks used to safely coordinate concurrent RPC execution.
 */
typedef struct HandleRecord {
    uint64_t id;
    HandleType type;
    void *ptr;

    /* Placement handles use this to find the canvas they belong to. */
    uint64_t parent_canvas_id;

    /* Canvas dimensions are needed by generate when calling ffmpeg. */
    size_t width;
    size_t height;

    /* Owner identity used for access validation. */
    pid_t owner_pid;
    char owner_username[33];

    int active;
    int use_count;
    int shared_count;
    char shared_users[16][33];

    /* Pending and completed share_canvas handshake records. */
    canvas_share_t *shares;

    /* Protects the backing object while workers call libanimate. */
    pthread_mutex_t lock;

    /* Barrier synchronisation state for shared canvases. */
    pthread_mutex_t barrier_mutex;
    pthread_cond_t barrier_cond;

    int barrier_generation;

    BarrierWaiter *barrier_waiters;
} HandleRecord;

/* =========================================================================
 * Handle table lifecycle
 * ========================================================================= */

/**
 * @brief Initialise the server handle table
 * @details
 * Clears handle storage and resets handle IDs. Should be called during server
 * startup before processing RPCs.
 *
 * @return HANDLE_OK on success
 */
int handles_init(void);

/**
 * @brief Clean up all server handle metadata
 * @details
 * Frees handle records, share records, and barrier waiter records. The RPC
 * layer is still responsible for calling the correct libanimate destroy
 * functions for actual canvas, sprite, and placement objects.
 */
void handles_cleanup(void);

/* =========================================================================
 * Basic handle operations
 * ========================================================================= */

/**
 * @brief Add a new client-visible handle
 * @details
 * Stores a backing pointer and returns a non-zero uint64_t handle ID that the
 * client can use in later RPC commands.
 *
 * @param type type of object being stored
 * @param ptr backing libanimate pointer
 * @param owner client that owns the object
 * @param out_id output location for the new handle ID
 * @return HANDLE_OK on success, otherwise an error code
 */
int handle_add(HandleType type,
               void *ptr,
               const ClientState *owner,
               uint64_t *out_id);

/**
 * @brief Look up a handle after validating type and access
 * @details
 * Checks that the handle exists, has the expected type, and is owned by or
 * shared with the requesting client.
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
                  void **out_ptr);

/**
 * @brief Remove a handle from the table
 * @details
 * Validates access and removes the handle metadata. The backing pointer is
 * returned so the caller can run the matching libanimate destroy function.
 *
 * @param id handle ID to remove
 * @param expected_type required handle type
 * @param client requesting client
 * @param out_ptr output location for the backing pointer
 * @return HANDLE_OK on success, otherwise an error code
 */
int handle_remove(uint64_t id,
                  HandleType expected_type,
                  const ClientState *client,
                  void **out_ptr);

/**
 * @brief Share a canvas with another username
 * @details
 * Records or completes a two-sided share_canvas request. A shared canvas can
 * then be accessed by both authorised users.
 *
 * @param id canvas handle ID
 * @param client requesting client
 * @param username other username in the share request
 * @return HANDLE_OK on success, otherwise an error code
 */
int handle_share_with_user(uint64_t id,
                           const ClientState *client,
                           const char *username);

/**
 * @brief Check whether a client can access a handle
 * @details
 * Returns a simple yes/no result after validating the handle ID, type, and
 * ownership or sharing relationship.
 *
 * @param id handle ID to check
 * @param expected_type required handle type
 * @param client requesting client
 * @return non-zero if access is allowed, otherwise 0
 */
int handle_user_has_access(uint64_t id,
                           HandleType expected_type,
                           const ClientState *client);

/* =========================================================================
 * Handle locking helpers
 * ========================================================================= */

/**
 * @brief Lock an accessible handle and return its backing pointer
 * @details
 * Used before mutating or reading libanimate objects from worker threads. The
 * caller must later call handle_unlock with the same handle ID.
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
                void **out_ptr);

/**
 * @brief Unlock a handle previously locked with handle_lock
 * @param id handle ID to unlock
 */
void handle_unlock(uint64_t id);

/* =========================================================================
 * Placement parent helpers
 * ========================================================================= */

/**
 * @brief Add a placement handle with its parent canvas ID
 * @details
 * Placement operations need to know which canvas they affect so the server can
 * synchronise canvas level mutations properly.
 *
 * @param ptr backing sprite placement pointer
 * @param owner client that owns the placement
 * @param parent_canvas_id canvas handle the placement belongs to
 * @param out_id output location for the new placement handle
 * @return HANDLE_OK on success, otherwise an error code
 */
int handle_add_placement(void *ptr,
                         const ClientState *owner,
                         uint64_t parent_canvas_id,
                         uint64_t *out_id);

/**
 * @brief Get the parent canvas for a placement
 * @details
 * Returns 0 if the placement is invalid or the client does not have access.
 *
 * @param placement_id placement handle ID
 * @param client requesting client
 * @return parent canvas handle ID, or 0 on failure
 */
uint64_t handle_get_parent_canvas(uint64_t placement_id,
                                  const ClientState *client);

/* =========================================================================
 * Canvas barrier helpers
 * ========================================================================= */

/**
 * @brief Wait if this client is blocked by a canvas barrier
 * @details
 * After a client reaches barrier <canvas>, later operations on that same canvas
 * should wait until the barrier has released.
 *
 * @param canvas_id canvas handle ID
 * @param client requesting client
 * @return HANDLE_OK on success, otherwise HANDLE_INVALID
 */
int handle_wait_if_blocked_by_barrier(uint64_t canvas_id,
                                      const ClientState *client);

/**
 * @brief Mark a client as arrived at a canvas barrier
 * @details
 * Blocks until all currently connected users sharing the canvas have also
 * arrived. Disconnected users are ignored so barriers do not wait forever.
 *
 * @param canvas_id canvas handle ID
 * @param client requesting client
 * @return HANDLE_OK on success, otherwise an error code
 */
int handle_barrier_arrive(uint64_t canvas_id,
                          const ClientState *client);

/**
 * @brief Wake barrier waiters after a client disconnects
 * @details
 * Called during disconnect cleanup so barriers can re-check their required
 * connected user count.
 *
 * @param username username that disconnected
 */
void handle_barrier_client_disconnected(const char *username);

/* =========================================================================
 * Canvas dimension helpers
 * ========================================================================= */

/**
 * @brief Store dimensions for a canvas handle
 * @details
 * The generate RPC needs width and height when building the ffmpeg raw video
 * input arguments.
 *
 * @param id canvas handle ID
 * @param width canvas width in pixels
 * @param height canvas height in pixels
 * @return HANDLE_OK on success, otherwise HANDLE_INVALID
 */
int handle_set_canvas_dimensions(uint64_t id,
                                 size_t width,
                                 size_t height);

/**
 * @brief Get stored canvas dimensions after validating access
 * @details
 * Used by generate after the canvas handle and client access have been checked.
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
                                 size_t *out_height);

#endif