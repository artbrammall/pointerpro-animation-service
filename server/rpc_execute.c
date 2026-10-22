#define _POSIX_C_SOURCE 200809L

#include "rpc_execute.h"
#include "handles.h"
#include "../libanimate/include/animate/animate.h"
#include "auth.h"
#include "generate.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>

/* =========================================================================
 * Response formatting helpers
 * ========================================================================= */

/**
 * @brief Write a plain protocol response into the response buffer
 * @details
 * Copies a fixed response such as "-1", "-2", "-3", or "0" into the output
 * buffer used by the worker thread.
 *
 * @param response destination response buffer
 * @param size size of the response buffer
 * @param text protocol response text without trailing newline
 * @return 0 on success, otherwise -3 for internal buffer failure
 */
static int write_response(char *response, size_t size, const char *text) {
    if (snprintf(response, size, "%s", text) >= (int)size) {
        return -3;
    }

    return 0;
}

/**
 * @brief Write a successful handle return response
 * @details
 * Used by RPCs that create a canvas, sprite, or placement. The client receives
 * a uint64_t handle that it can use in later commands.
 *
 * @param response destination response buffer
 * @param size size of the response buffer
 * @param handle handle value to send back to the client
 * @return 0 on success, otherwise -3 for internal buffer failure
 */
static int write_handle_response(char *response, size_t size, uint64_t handle) {
    if (snprintf(response, size, "0 %llu",
                 (unsigned long long)handle) >= (int)size) {
        return -3;
    }

    return 0;
}

/**
 * @brief Write a successful boolean return response
 * @details
 * libanimate functions that return bool are sent back as integer values, as
 * required by the assignment protocol.
 *
 * @param response destination response buffer
 * @param size size of the response buffer
 * @param value boolean-style value to send as 0 or 1
 * @return 0 on success, otherwise -3 for internal buffer failure
 */
static int write_bool_response(char *response, size_t size, int value) {
    if (snprintf(response, size, "0 %d", value) >= (int)size) {
        return -3;
    }

    return 0;
}

/* =========================================================================
 * Generate argument validation
 * ========================================================================= */

/**
 * @brief Validate the frame range for generate
 * @details
 * Checks the generate frame bounds before any file writing occurs. The upper
 * span limit avoids accidentally asking the server to produce a ridiculous
 * number of frames. Computers have limits, tragic but true.
 *
 * @param start first frame index
 * @param end final frame index, inclusive
 * @param frame_rate requested frames per second
 * @return 1 if valid, otherwise 0
 */
static int valid_generate_range(long start, long end, long frame_rate) {
    if (start < 0 || end < 0) {
        return 0;
    }

    if (start > end) {
        return 0;
    }

    if (frame_rate <= 0) {
        return 0;
    }

    if (end - start > 100000) {
        return 0;
    }

    return 1;
}

/**
 * @brief Validate a base filename for generate output
 * @details
 * Allows only simple local filenames. This prevents path traversal, slash based
 * paths, and names that start with '-' from reaching file creation or ffmpeg.
 *
 * @param name base filename from the generate command
 * @return 1 if valid, otherwise 0
 */
static int valid_output_basename(const char *name) {
    size_t i;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }

    if (strlen(name) > 128) {
        return 0;
    }

    if (name[0] == '-') {
        return 0;
    }

    if (strstr(name, "..") != NULL) {
        return 0;
    }

    for (i = 0; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)name[i];

        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
            return 0;
        }
    }

    return 1;
}

/* =========================================================================
 * RPC execution
 * ========================================================================= */

/**
 * @brief Execute one parsed RPC command
 * @details
 * Runs the server side logic for one parsed request. This includes calling
 * libanimate, validating handles, checking sharing permissions, respecting
 * barriers, and formatting the response exactly as the protocol expects.
 *
 * @param client client that sent the request
 * @param command parsed RPC command to execute
 * @param response destination buffer for the protocol response
 * @param response_size size of the response buffer
 * @return 0 on normal response write, or a negative internal formatting code
 */
int execute_rpc(ClientState *client,
                const RpcCommand *command,
                char *response,
                size_t response_size) {
    void *ptr = NULL;
    void *ptr2 = NULL;
    uint64_t handle = 0;

    if (client == NULL || command == NULL || response == NULL) {
        return write_response(response, response_size, "-3");
    }

    switch (command->type) {
        case RPC_CREATE_CANVAS: {
            struct canvas *canvas = animate_create_canvas(
                (size_t)command->a,
                (size_t)command->b,
                (color_t)command->c
            );

            if (canvas == NULL) {
                return write_response(response, response_size, "-3");
            }

            if (handle_add(HANDLE_CANVAS, canvas, client, &handle) !=
                HANDLE_OK) {
                animate_destroy_canvas(canvas);
                return write_response(response, response_size, "-3");
            }

            /*
             * libanimate receives height then width. ffmpeg later needs width
             * then height, so store both in the handle metadata.
             */
            if (handle_set_canvas_dimensions(handle,
                                             (size_t)command->b,
                                             (size_t)command->a) != HANDLE_OK) {
                void *removed = NULL;

                if (handle_remove(handle, HANDLE_CANVAS, client, &removed) ==
                    HANDLE_OK) {
                    animate_destroy_canvas((struct canvas *)removed);
                }

                return write_response(response, response_size, "-3");
            }

            return write_handle_response(response, response_size, handle);
        }

        case RPC_CREATE_RECTANGLE: {
            struct sprite *sprite = animate_create_rectangle(
                (size_t)command->a,
                (size_t)command->b,
                (color_t)command->c,
                command->d != 0
            );

            if (sprite == NULL) {
                return write_response(response, response_size, "-3");
            }

            if (handle_add(HANDLE_SPRITE, sprite, client, &handle) !=
                HANDLE_OK) {
                animate_destroy_sprite(sprite);
                return write_response(response, response_size, "-3");
            }

            return write_handle_response(response, response_size, handle);
        }

        case RPC_CREATE_CIRCLE: {
            struct sprite *sprite = animate_create_circle(
                (size_t)command->a,
                (color_t)command->b,
                command->c != 0
            );

            if (sprite == NULL) {
                return write_response(response, response_size, "-3");
            }

            if (handle_add(HANDLE_SPRITE, sprite, client, &handle) !=
                HANDLE_OK) {
                animate_destroy_sprite(sprite);
                return write_response(response, response_size, "-3");
            }

            return write_handle_response(response, response_size, handle);
        }

        case RPC_CREATE_SPRITE: {
            struct sprite *sprite = animate_create_sprite(command->text);

            if (sprite == NULL) {
                return write_response(response, response_size, "-3");
            }

            if (handle_add(HANDLE_SPRITE, sprite, client, &handle) !=
                HANDLE_OK) {
                animate_destroy_sprite(sprite);
                return write_response(response, response_size, "-3");
            }

            return write_handle_response(response, response_size, handle);
        }

        case RPC_PLACE_SPRITE: {
            struct sprite_placement *placement;

            /*
             * Placing a sprite mutates the canvas, so it must wait behind any
             * active barrier for this client on that canvas.
             */
            if (handle_wait_if_blocked_by_barrier(command->a, client) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_lock(command->a, HANDLE_CANVAS, client, &ptr) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_lookup(command->b, HANDLE_SPRITE, client, &ptr2) !=
                HANDLE_OK) {
                handle_unlock(command->a);
                return write_response(response, response_size, "-2");
            }

            placement = animate_place_sprite(
                (struct canvas *)ptr,
                (struct sprite *)ptr2,
                (ssize_t)command->sa,
                (ssize_t)command->sb
            );

            handle_unlock(command->a);

            if (placement == NULL) {
                return write_response(response, response_size, "-3");
            }

            if (handle_add_placement(placement, client, command->a, &handle) !=
                HANDLE_OK) {
                animate_destroy_placement(placement);
                return write_response(response, response_size, "-3");
            }

            return write_handle_response(response, response_size, handle);
        }

        case RPC_PLACEMENT_UP: {
            uint64_t canvas_id = handle_get_parent_canvas(command->a, client);
            void *canvas_ptr = NULL;

            if (canvas_id == 0) {
                return write_response(response, response_size, "-2");
            }

            if (handle_wait_if_blocked_by_barrier(canvas_id, client) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            /*
             * Placement ordering changes mutate the parent canvas, so lock the
             * canvas while calling libanimate.
             */
            if (handle_lock(canvas_id, HANDLE_CANVAS, client, &canvas_ptr) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_lookup(command->a, HANDLE_PLACEMENT, client, &ptr) !=
                HANDLE_OK) {
                handle_unlock(canvas_id);
                return write_response(response, response_size, "-2");
            }

            animate_placement_up((struct sprite_placement *)ptr);

            handle_unlock(canvas_id);
            return write_response(response, response_size, "0");
        }

        case RPC_PLACEMENT_DOWN: {
            uint64_t canvas_id = handle_get_parent_canvas(command->a, client);
            void *canvas_ptr = NULL;

            if (canvas_id == 0) {
                return write_response(response, response_size, "-2");
            }

            if (handle_wait_if_blocked_by_barrier(canvas_id, client) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_lock(canvas_id, HANDLE_CANVAS, client, &canvas_ptr) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_lookup(command->a, HANDLE_PLACEMENT, client, &ptr) !=
                HANDLE_OK) {
                handle_unlock(canvas_id);
                return write_response(response, response_size, "-2");
            }

            animate_placement_down((struct sprite_placement *)ptr);

            handle_unlock(canvas_id);
            return write_response(response, response_size, "0");
        }

        case RPC_PLACEMENT_TOP: {
            uint64_t canvas_id = handle_get_parent_canvas(command->a, client);
            void *canvas_ptr = NULL;

            if (canvas_id == 0) {
                return write_response(response, response_size, "-2");
            }

            if (handle_wait_if_blocked_by_barrier(canvas_id, client) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_lock(canvas_id, HANDLE_CANVAS, client, &canvas_ptr) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_lookup(command->a, HANDLE_PLACEMENT, client, &ptr) !=
                HANDLE_OK) {
                handle_unlock(canvas_id);
                return write_response(response, response_size, "-2");
            }

            animate_placement_top((struct sprite_placement *)ptr);

            handle_unlock(canvas_id);
            return write_response(response, response_size, "0");
        }

        case RPC_PLACEMENT_BOTTOM: {
            uint64_t canvas_id = handle_get_parent_canvas(command->a, client);
            void *canvas_ptr = NULL;

            if (canvas_id == 0) {
                return write_response(response, response_size, "-2");
            }

            if (handle_wait_if_blocked_by_barrier(canvas_id, client) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_lock(canvas_id, HANDLE_CANVAS, client, &canvas_ptr) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_lookup(command->a, HANDLE_PLACEMENT, client, &ptr) !=
                HANDLE_OK) {
                handle_unlock(canvas_id);
                return write_response(response, response_size, "-2");
            }

            animate_placement_bottom((struct sprite_placement *)ptr);

            handle_unlock(canvas_id);
            return write_response(response, response_size, "0");
        }

        case RPC_SET_ANIMATION_PARAMS: {
            uint64_t canvas_id = handle_get_parent_canvas(command->a, client);
            void *canvas_ptr = NULL;

            if (canvas_id == 0) {
                return write_response(response, response_size, "-2");
            }

            if (handle_wait_if_blocked_by_barrier(canvas_id, client) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_lock(canvas_id, HANDLE_CANVAS, client, &canvas_ptr) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_lookup(command->a, HANDLE_PLACEMENT, client, &ptr) !=
                HANDLE_OK) {
                handle_unlock(canvas_id);
                return write_response(response, response_size, "-2");
            }

            animate_set_animation_params(
                (struct sprite_placement *)ptr,
                (ssize_t)command->sa,
                (ssize_t)command->sb,
                (ssize_t)command->sc,
                (ssize_t)command->sd
            );

            handle_unlock(canvas_id);

            return write_response(response, response_size, "0");
        }

        case RPC_DESTROY_PLACEMENT: {
            uint64_t canvas_id = handle_get_parent_canvas(command->a, client);
            void *canvas_ptr = NULL;

            if (canvas_id == 0) {
                return write_response(response, response_size, "-2");
            }

            if (handle_wait_if_blocked_by_barrier(canvas_id, client) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_lock(canvas_id, HANDLE_CANVAS, client, &canvas_ptr) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_remove(command->a, HANDLE_PLACEMENT, client, &ptr) !=
                HANDLE_OK) {
                handle_unlock(canvas_id);
                return write_response(response, response_size, "-2");
            }

            animate_destroy_placement((struct sprite_placement *)ptr);

            handle_unlock(canvas_id);
            return write_response(response, response_size, "0");
        }

        case RPC_DESTROY_SPRITE: {
            int result;

            if (handle_remove(command->a, HANDLE_SPRITE, client, &ptr) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            result = animate_destroy_sprite((struct sprite *)ptr);
            return write_bool_response(response, response_size, result ? 1 : 0);
        }

        case RPC_DESTROY_CANVAS:
            if (handle_wait_if_blocked_by_barrier(command->a, client) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_remove(command->a, HANDLE_CANVAS, client, &ptr) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            animate_destroy_canvas((struct canvas *)ptr);
            return write_response(response, response_size, "0");

        case RPC_GENERATE: {
            GenerateResult result;
            size_t width;
            size_t height;

            /*
             * generate is read-only for the canvas, but it still needs valid
             * access, valid output names, and valid frame parameters.
             */
            if (!valid_generate_range(command->sa, command->sb, command->sc)) {
                return write_response(response, response_size, "-2");
            }

            if (!valid_output_basename(command->text)) {
                return write_response(response, response_size, "-2");
            }

            if (handle_wait_if_blocked_by_barrier(command->a, client) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_get_canvas_dimensions(command->a,
                                             client,
                                             &width,
                                             &height) != HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            if (handle_lock(command->a, HANDLE_CANVAS, client, &ptr) !=
                HANDLE_OK) {
                return write_response(response, response_size, "-2");
            }

            result = server_generate((struct canvas *)ptr,
                                     width,
                                     height,
                                     command->text,
                                     command->sa,
                                     command->sb,
                                     command->sc);

            handle_unlock(command->a);

            switch (result) {
                case GENERATE_OK:
                    return write_response(response, response_size, "0 0 0");

                case GENERATE_DATA_WRITE_FAILED:
                    return write_response(response, response_size, "0 -1");

                case GENERATE_MOVIE_WRITE_FAILED:
                    return write_response(response, response_size, "0 0 -1");

                case GENERATE_VALUE_ERROR:
                    return write_response(response, response_size, "-2");

                case GENERATE_INTERNAL_ERROR:
                default:
                    return write_response(response, response_size, "-3");
            }
        }

        case RPC_SHARE_CANVAS: {
            int result;

            /*
             * The target username must exist, and the canvas handle must be
             * non-zero before the share state is updated.
             */
            if (command->a == 0 || !user_exists(command->text)) {
                return write_response(response, response_size, "-2");
            }

            result = handle_share_with_user(command->a, client, command->text);

            if (result == HANDLE_OK) {
                return write_response(response, response_size, "0");
            }

            if (result == HANDLE_INVALID) {
                return write_response(response, response_size, "-2");
            }

            return write_response(response, response_size, "-3");
        }

        case RPC_BARRIER: {
            int result = handle_barrier_arrive(command->a, client);

            if (result == HANDLE_OK) {
                return write_response(response, response_size, "0");
            }

            if (result == HANDLE_INVALID) {
                return write_response(response, response_size, "-2");
            }

            return write_response(response, response_size, "-3");
        }

        default:
            return write_response(response, response_size, "-1");
    }
}