#ifndef RPC_PARSE_H
#define RPC_PARSE_H

#include <stdint.h>

/* =========================================================================
 * RPC command types
 * ========================================================================= */

/**
 * @brief Supported parsed RPC command types
 * @details
 * The parser maps raw client command strings into these enum values. The
 * executor then switches on the type and calls the matching server side logic.
 */
typedef enum {
    RPC_INVALID = 0,
    RPC_DISCONNECT,
    RPC_CREATE_CANVAS,
    RPC_CREATE_SPRITE,
    RPC_CREATE_RECTANGLE,
    RPC_CREATE_CIRCLE,
    RPC_DESTROY_CANVAS,
    RPC_DESTROY_SPRITE,
    RPC_PLACE_SPRITE,
    RPC_PLACEMENT_UP,
    RPC_PLACEMENT_DOWN,
    RPC_PLACEMENT_TOP,
    RPC_PLACEMENT_BOTTOM,
    RPC_DESTROY_PLACEMENT,
    RPC_SET_ANIMATION_PARAMS,
    RPC_GENERATE,
    RPC_SHARE_CANVAS,
    RPC_BARRIER
} RpcType;

/* =========================================================================
 * Parsed RPC command
 * ========================================================================= */

/**
 * @brief Parsed representation of one client RPC command
 * @details
 * Stores the command type plus parsed arguments. Unsigned fields are used for
 * handles, sizes, colours, and boolean values. Signed fields are used for
 * coordinates, animation values, and generate frame ranges. The text field is
 * used for filenames and usernames.
 */
typedef struct {
    RpcType type;

    /* Unsigned numeric arguments, usually handles, sizes, colours, or flags. */
    uint64_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;

    /* Signed numeric arguments, usually coordinates or animation parameters. */
    int64_t sa;
    int64_t sb;
    int64_t sc;
    int64_t sd;

    /* Text argument for filenames or usernames. */
    char text[256];
} RpcCommand;

/* =========================================================================
 * RPC parser interface
 * ========================================================================= */

/**
 * @brief Parse a raw client command into an RpcCommand
 * @details
 * Validates the command name, argument count, and argument formats. On success,
 * fills out the supplied RpcCommand structure for execution by worker threads.
 *
 * @param line raw command line received from a client FIFO
 * @param out output location for the parsed command
 * @return 1 if parsing succeeds, otherwise 0
 */
int parse_rpc_command(const char *line, RpcCommand *out);

#endif