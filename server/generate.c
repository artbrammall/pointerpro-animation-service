#define _POSIX_C_SOURCE 200809L

#include "generate.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

/* =========================================================================
 * Output filename validation
 * ========================================================================= */

/**
 * @brief Check whether a generate filename is safe to use
 * @details
 * The generate RPC receives a base filename, then the server writes .dat,
 * .mp4, and .log files from it. This helper only allows simple local filenames
 * so clients cannot write through paths such as "../bad" or use option-like
 * names that start with '-'.
 *
 * @param filename base filename from the generate command
 * @return 1 if the filename is valid, otherwise 0
 */
static int valid_output_basename(const char *filename) {
    size_t i;

    if (filename == NULL || filename[0] == '\0') {
        return 0;
    }

    if (strlen(filename) > 128) {
        return 0;
    }

    if (filename[0] == '-') {
        return 0;
    }

    if (strstr(filename, "..") != NULL) {
        return 0;
    }

    for (i = 0; filename[i] != '\0'; i++) {
        unsigned char c = (unsigned char)filename[i];

        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Build the output paths for a generate request
 * @details
 * Creates the three filenames required by the assignment protocol:
 * <filename>.dat, <filename>.mp4, and <filename>.log. If any path would not fit
 * in its destination buffer, the request is treated as a value error.
 *
 * @param filename validated base filename
 * @param dat_path destination buffer for the data file path
 * @param dat_size size of dat_path
 * @param mp4_path destination buffer for the movie file path
 * @param mp4_size size of mp4_path
 * @param log_path destination buffer for the log file path
 * @param log_size size of log_path
 * @return GENERATE_OK on success, otherwise GENERATE_VALUE_ERROR
 */
static GenerateResult build_output_paths(const char *filename,
                                         char *dat_path,
                                         size_t dat_size,
                                         char *mp4_path,
                                         size_t mp4_size,
                                         char *log_path,
                                         size_t log_size) {
    if (snprintf(dat_path, dat_size, "%s.dat", filename) >= (int)dat_size) {
        return GENERATE_VALUE_ERROR;
    }

    if (snprintf(mp4_path, mp4_size, "%s.mp4", filename) >= (int)mp4_size) {
        return GENERATE_VALUE_ERROR;
    }

    if (snprintf(log_path, log_size, "%s.log", filename) >= (int)log_size) {
        return GENERATE_VALUE_ERROR;
    }

    return GENERATE_OK;
}

/* =========================================================================
 * Frame data generation
 * ========================================================================= */

/**
 * @brief Write raw animation frame data to a .dat file
 * @details
 * Uses the provided libanimate canvas to generate each frame from start to end,
 * inclusive. The resulting bytes are written sequentially to the .dat file,
 * which is later used as ffmpeg input.
 *
 * @param canvas canvas to generate frames from
 * @param dat_path path of the .dat file to write
 * @param start first frame index
 * @param end final frame index, inclusive
 * @param frame_rate frame rate requested by the client
 * @return generate result code for success, internal error, or data write fail
 */
static GenerateResult write_frame_data(struct canvas *canvas,
                                       const char *dat_path,
                                       long start,
                                       long end,
                                       long frame_rate) {
    FILE *dat_file;
    void *buffer;
    size_t frame_size;
    long frame;

    frame_size = animate_frame_size_bytes(canvas);
    if (frame_size == 0) {
        return GENERATE_INTERNAL_ERROR;
    }

    buffer = malloc(frame_size);
    if (buffer == NULL) {
        return GENERATE_INTERNAL_ERROR;
    }

    dat_file = fopen(dat_path, "wb");
    if (dat_file == NULL) {
        free(buffer);
        return GENERATE_DATA_WRITE_FAILED;
    }

    for (frame = start; frame <= end; frame++) {
        animate_generate_frame(canvas,
                               (size_t)frame,
                               (size_t)frame_rate,
                               buffer);

        if (fwrite(buffer, 1, frame_size, dat_file) != frame_size) {
            fclose(dat_file);
            free(buffer);
            return GENERATE_DATA_WRITE_FAILED;
        }
    }

    if (fclose(dat_file) != 0) {
        free(buffer);
        return GENERATE_DATA_WRITE_FAILED;
    }

    free(buffer);
    return GENERATE_OK;
}

/* =========================================================================
 * Movie generation
 * ========================================================================= */

/**
 * @brief Run ffmpeg to convert raw frame data into an mp4
 * @details
 * Forks a child process and runs ffmpeg with the generated .dat file as raw
 * ARGB video input. Both stdout and stderr are redirected to the .log file, as
 * required by the assignment. Any ffmpeg, log, or movie failure maps to the
 * same movie write failure result.
 *
 * @param dat_path path to the generated raw frame data
 * @param mp4_path path to the mp4 file to create
 * @param log_path path to the ffmpeg log file
 * @param width canvas width in pixels
 * @param height canvas height in pixels
 * @param frame_rate frames per second for ffmpeg
 * @return GENERATE_OK on success, otherwise GENERATE_MOVIE_WRITE_FAILED
 */
static GenerateResult run_ffmpeg(const char *dat_path,
                                 const char *mp4_path,
                                 const char *log_path,
                                 size_t width,
                                 size_t height,
                                 long frame_rate) {
    int log_fd;
    pid_t pid;
    int status;
    char size_arg[64];
    char rate_arg[32];

    if (snprintf(size_arg, sizeof(size_arg), "%zux%zu", width, height) >=
        (int)sizeof(size_arg)) {
        return GENERATE_MOVIE_WRITE_FAILED;
    }

    if (snprintf(rate_arg, sizeof(rate_arg), "%ld", frame_rate) >=
        (int)sizeof(rate_arg)) {
        return GENERATE_MOVIE_WRITE_FAILED;
    }

    log_fd = open(log_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (log_fd < 0) {
        return GENERATE_MOVIE_WRITE_FAILED;
    }

    pid = fork();
    if (pid < 0) {
        close(log_fd);
        return GENERATE_MOVIE_WRITE_FAILED;
    }

    if (pid == 0) {
        /* Send all ffmpeg output to the required log file. */
        if (dup2(log_fd, STDOUT_FILENO) < 0) {
            _exit(127);
        }

        if (dup2(log_fd, STDERR_FILENO) < 0) {
            _exit(127);
        }

        close(log_fd);

        execlp("ffmpeg",
            "ffmpeg",
            "-y",
            "-f", "rawvideo",
            "-pix_fmt", "argb",
            "-s", size_arg,
            "-r", rate_arg,
            "-i", dat_path,
            "-c:v", "libx264",
            "-pix_fmt", "yuv420p",
            "-movflags", "+faststart",
            mp4_path,
            (char *)NULL);

        _exit(127);
    }

    close(log_fd);

    if (waitpid(pid, &status, 0) < 0) {
        return GENERATE_MOVIE_WRITE_FAILED;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return GENERATE_MOVIE_WRITE_FAILED;
    }

    return GENERATE_OK;
}

/* =========================================================================
 * Public generate RPC helper
 * ========================================================================= */

/**
 * @brief Generate animation files for a canvas
 * @details
 * Implements the server side work for:
 * generate <canvas> <filename> <start> <end> <frame rate>
 *
 * The canvas handle and access checks should already be done before this
 * helper is called. This function validates the remaining values, writes the
 * raw frame data, then invokes ffmpeg to create the movie and log files.
 *
 * @param canvas canvas to generate animation frames from
 * @param width canvas width in pixels
 * @param height canvas height in pixels
 * @param filename base output filename without extension
 * @param start first frame index
 * @param end final frame index, inclusive
 * @param frame_rate frames per second
 * @return GenerateResult value used by RPC response formatting
 */
GenerateResult server_generate(struct canvas *canvas,
                               size_t width,
                               size_t height,
                               const char *filename,
                               long start,
                               long end,
                               long frame_rate) {
    GenerateResult path_result;
    GenerateResult data_result;

    char dat_path[256];
    char mp4_path[256];
    char log_path[256];

    if (canvas == NULL || !valid_output_basename(filename)) {
        return GENERATE_VALUE_ERROR;
    }

    if (width == 0 || height == 0) {
        return GENERATE_VALUE_ERROR;
    }

    if (start < 0 || end < 0 || start > end || frame_rate <= 0) {
        return GENERATE_VALUE_ERROR;
    }

    path_result = build_output_paths(filename,
                                     dat_path,
                                     sizeof(dat_path),
                                     mp4_path,
                                     sizeof(mp4_path),
                                     log_path,
                                     sizeof(log_path));

    if (path_result != GENERATE_OK) {
        return path_result;
    }

    data_result = write_frame_data(canvas, dat_path, start, end, frame_rate);
    if (data_result != GENERATE_OK) {
        return data_result;
    }

    return run_ffmpeg(dat_path,
                      mp4_path,
                      log_path,
                      width,
                      height,
                      frame_rate);
}