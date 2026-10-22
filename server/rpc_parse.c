#define _POSIX_C_SOURCE 200809L

#include "rpc_parse.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 8

/* =========================================================================
 * Numeric parsing helpers
 * ========================================================================= */

/**
 * @brief Parse an unsigned 64-bit integer argument
 * @details
 * Converts a decimal string into a uint64_t value. Negative values and
 * malformed strings are rejected so bad client input becomes an RPC parse
 * failure instead of reaching libanimate.
 *
 * @param text text to parse
 * @param out output location for the parsed value
 * @return 1 if parsing succeeds, otherwise 0
 */
static int parse_u64(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    if (text[0] == '-') {
        return 0;
    }

    errno = 0;
    value = strtoull(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    *out = (uint64_t)value;
    return 1;
}

/**
 * @brief Parse a signed 64-bit integer argument
 * @details
 * Converts a decimal string into an int64_t value. This is used for coordinates
 * and animation parameters, which may be negative.
 *
 * @param text text to parse
 * @param out output location for the parsed value
 * @return 1 if parsing succeeds, otherwise 0
 */
static int parse_i64(const char *text, int64_t *out) {
    char *end = NULL;
    long long value;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    errno = 0;
    value = strtoll(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    *out = (int64_t)value;
    return 1;
}

/* =========================================================================
 * Command tokenising helpers
 * ========================================================================= */

/**
 * @brief Split a command line into whitespace-separated tokens
 * @details
 * Copies the input line into a fixed buffer and uses strtok_r to split on
 * spaces, tabs, and newlines. If too many tokens are supplied, parsing fails.
 *
 * @param line raw command line from the client
 * @param tokens fixed token output array
 * @return number of tokens, 0 for empty input, or -1 for too many tokens
 */
static int tokenise(const char *line, char tokens[MAX_TOKENS][256]) {
    char copy[1024];
    char *saveptr = NULL;
    char *token;
    int count = 0;

    if (line == NULL) {
        return 0;
    }

    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    token = strtok_r(copy, " \t\n", &saveptr);

    while (token != NULL) {
        if (count >= MAX_TOKENS) {
            return -1;
        }

        strncpy(tokens[count], token, 255);
        tokens[count][255] = '\0';
        count++;

        token = strtok_r(NULL, " \t\n", &saveptr);
    }

    return count;
}

/**
 * @brief Check whether an integer is a protocol boolean
 * @details
 * The animate RPC protocol represents boolean values as integers. Only 0 and
 * 1 are accepted.
 *
 * @param value value to check
 * @return 1 if value is 0 or 1, otherwise 0
 */
static int is_bool_value(uint64_t value) {
    return value == 0 || value == 1;
}

/* =========================================================================
 * RPC parser
 * ========================================================================= */

/**
 * @brief Parse a raw command line into an RpcCommand
 * @details
 * Recognises all supported Assignment 2 RPC commands, validates argument
 * counts, parses numeric fields, and copies filename or username fields into
 * the command text buffer. Unsupported commands or malformed arguments return
 * failure, which the server maps to "-1".
 *
 * @param line raw command line from the client
 * @param out output location for the parsed command
 * @return 1 if the command is valid and supported, otherwise 0
 */
int parse_rpc_command(const char *line, RpcCommand *out) {
    char tokens[MAX_TOKENS][256];
    int count;

    if (out == NULL) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->type = RPC_INVALID;

    count = tokenise(line, tokens);

    if (count <= 0) {
        return 0;
    }

    if (strcmp(tokens[0], "Disconnect") == 0) {
        if (count != 1) {
            return 0;
        }

        out->type = RPC_DISCONNECT;
        return 1;
    }

    if (strcmp(tokens[0], "create_canvas") == 0) {
        if (count != 4) {
            return 0;
        }

        if (!parse_u64(tokens[1], &out->a) ||
            !parse_u64(tokens[2], &out->b) ||
            !parse_u64(tokens[3], &out->c)) {
            return 0;
        }

        out->type = RPC_CREATE_CANVAS;
        return 1;
    }

    if (strcmp(tokens[0], "create_sprite") == 0) {
        if (count != 2) {
            return 0;
        }

        strncpy(out->text, tokens[1], sizeof(out->text) - 1);
        out->type = RPC_CREATE_SPRITE;
        return 1;
    }

    if (strcmp(tokens[0], "create_rectangle") == 0) {
        if (count != 5) {
            return 0;
        }

        if (!parse_u64(tokens[1], &out->a) ||
            !parse_u64(tokens[2], &out->b) ||
            !parse_u64(tokens[3], &out->c) ||
            !parse_u64(tokens[4], &out->d)) {
            return 0;
        }

        if (!is_bool_value(out->d)) {
            return 0;
        }

        out->type = RPC_CREATE_RECTANGLE;
        return 1;
    }

    if (strcmp(tokens[0], "create_circle") == 0) {
        if (count != 4) {
            return 0;
        }

        if (!parse_u64(tokens[1], &out->a) ||
            !parse_u64(tokens[2], &out->b) ||
            !parse_u64(tokens[3], &out->c)) {
            return 0;
        }

        if (!is_bool_value(out->c)) {
            return 0;
        }

        out->type = RPC_CREATE_CIRCLE;
        return 1;
    }

    if (strcmp(tokens[0], "destroy_canvas") == 0) {
        if (count != 2 || !parse_u64(tokens[1], &out->a)) {
            return 0;
        }

        out->type = RPC_DESTROY_CANVAS;
        return 1;
    }

    if (strcmp(tokens[0], "destroy_sprite") == 0) {
        if (count != 2 || !parse_u64(tokens[1], &out->a)) {
            return 0;
        }

        out->type = RPC_DESTROY_SPRITE;
        return 1;
    }

    if (strcmp(tokens[0], "place_sprite") == 0) {
        if (count != 5) {
            return 0;
        }

        if (!parse_u64(tokens[1], &out->a) ||
            !parse_u64(tokens[2], &out->b) ||
            !parse_i64(tokens[3], &out->sa) ||
            !parse_i64(tokens[4], &out->sb)) {
            return 0;
        }

        out->type = RPC_PLACE_SPRITE;
        return 1;
    }

    if (strcmp(tokens[0], "placement_up") == 0) {
        if (count != 2 || !parse_u64(tokens[1], &out->a)) {
            return 0;
        }

        out->type = RPC_PLACEMENT_UP;
        return 1;
    }

    if (strcmp(tokens[0], "placement_down") == 0) {
        if (count != 2 || !parse_u64(tokens[1], &out->a)) {
            return 0;
        }

        out->type = RPC_PLACEMENT_DOWN;
        return 1;
    }

    if (strcmp(tokens[0], "placement_top") == 0) {
        if (count != 2 || !parse_u64(tokens[1], &out->a)) {
            return 0;
        }

        out->type = RPC_PLACEMENT_TOP;
        return 1;
    }

    if (strcmp(tokens[0], "placement_bottom") == 0) {
        if (count != 2 || !parse_u64(tokens[1], &out->a)) {
            return 0;
        }

        out->type = RPC_PLACEMENT_BOTTOM;
        return 1;
    }

    if (strcmp(tokens[0], "destroy_placement") == 0) {
        if (count != 2 || !parse_u64(tokens[1], &out->a)) {
            return 0;
        }

        out->type = RPC_DESTROY_PLACEMENT;
        return 1;
    }

    if (strcmp(tokens[0], "set_animation_params") == 0) {
        if (count != 6) {
            return 0;
        }

        if (!parse_u64(tokens[1], &out->a) ||
            !parse_i64(tokens[2], &out->sa) ||
            !parse_i64(tokens[3], &out->sb) ||
            !parse_i64(tokens[4], &out->sc) ||
            !parse_i64(tokens[5], &out->sd)) {
            return 0;
        }

        out->type = RPC_SET_ANIMATION_PARAMS;
        return 1;
    }

    if (strcmp(tokens[0], "generate") == 0) {
        if (count != 6) {
            return 0;
        }

        if (!parse_u64(tokens[1], &out->a) ||
            !parse_i64(tokens[3], &out->sa) ||
            !parse_i64(tokens[4], &out->sb) ||
            !parse_i64(tokens[5], &out->sc)) {
            return 0;
        }

        strncpy(out->text, tokens[2], sizeof(out->text) - 1);
        out->text[sizeof(out->text) - 1] = '\0';

        out->type = RPC_GENERATE;
        return 1;
    }

    if (strcmp(tokens[0], "share_canvas") == 0) {
        if (count != 3 || !parse_u64(tokens[1], &out->a)) {
            return 0;
        }

        strncpy(out->text, tokens[2], sizeof(out->text) - 1);
        out->type = RPC_SHARE_CANVAS;
        return 1;
    }

    if (strcmp(tokens[0], "barrier") == 0) {
        if (count != 2 || !parse_u64(tokens[1], &out->a)) {
            return 0;
        }

        out->type = RPC_BARRIER;
        return 1;
    }

    return 0;
}