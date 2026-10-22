#ifndef GENERATE_H
#define GENERATE_H

#include "../libanimate/include/animate/animate.h"

#include <stddef.h>

/* =========================================================================
 * Generate RPC result codes
 * ========================================================================= */

/**
 * @brief Result codes returned by the generate helper
 * @details
 * These values describe the internal result of the server side generate work.
 * The RPC layer should translate them into the exact protocol responses:
 * GENERATE_OK                 -> "0 0 0"
 * GENERATE_DATA_WRITE_FAILED  -> "0 -1"
 * GENERATE_MOVIE_WRITE_FAILED -> "0 0 -1"
 * GENERATE_VALUE_ERROR        -> "-2"
 * GENERATE_INTERNAL_ERROR     -> "-3"
 */
typedef enum GenerateResult {
    GENERATE_OK = 0,
    GENERATE_DATA_WRITE_FAILED,
    GENERATE_MOVIE_WRITE_FAILED,
    GENERATE_VALUE_ERROR,
    GENERATE_INTERNAL_ERROR
} GenerateResult;

/* =========================================================================
 * Generate RPC interface
 * ========================================================================= */

/**
 * @brief Generate animation output files for a canvas
 * @details
 * Implements the file generation work behind the assignment's generate RPC.
 * The caller should already have checked that the canvas handle is valid and
 * that the requesting client has access to it. This function validates the
 * remaining generate arguments, writes raw frame data, runs ffmpeg, and records
 * ffmpeg output in the log file.
 *
 * @param canvas libanimate canvas to generate frames from
 * @param width canvas width in pixels
 * @param height canvas height in pixels
 * @param filename base output filename without extension
 * @param start first frame index
 * @param end final frame index, inclusive
 * @param frame_rate frames per second for the output movie
 * @return GenerateResult describing success or the type of failure
 */
GenerateResult server_generate(struct canvas *canvas,
                               size_t width,
                               size_t height,
                               const char *filename,
                               long start,
                               long end,
                               long frame_rate);

#endif