/* zarpack: create ZArchive (.zar) files from directories.
 *
 * Plain C API so it can be called from Swift, C#, or anything else.
 * All paths are UTF-8. All functions are thread-safe; a single pack call
 * blocks until finished and should be run off the UI thread.
 */
#ifndef ZARPACK_H
#define ZARPACK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(ZARPACK_SHARED)
#  ifdef ZARPACK_BUILDING
#    define ZARPACK_API __declspec(dllexport)
#  else
#    define ZARPACK_API __declspec(dllimport)
#  endif
#else
#  define ZARPACK_API
#endif

typedef enum zarpack_status {
    ZARPACK_OK = 0,
    ZARPACK_CANCELLED = 1,
    ZARPACK_ERR_INPUT = 2,        /* input is not a readable directory */
    ZARPACK_ERR_OUTPUT_EXISTS = 3,/* output exists and overwrite == 0 */
    ZARPACK_ERR_IO = 4,           /* read/write failure, see message */
    ZARPACK_ERR_INTERNAL = 5
} zarpack_status;

typedef struct zarpack_progress {
    uint64_t bytes_done;
    uint64_t bytes_total;
    uint64_t files_done;
    uint64_t files_total;
    const char *current_file;     /* relative path, valid during the callback */
} zarpack_progress;

/* Return non-zero to cancel. Called from the packing thread, roughly 20x/s. */
typedef int (*zarpack_progress_fn)(const zarpack_progress *progress, void *user);

typedef struct zarpack_options {
    const char *input_dir;        /* required */
    /* Optional. NULL or "" -> next to the input directory.
     * An existing directory -> "<output>/<input name>.zar".
     * Anything else is used as the archive file path. */
    const char *output;
    int overwrite;                /* replace an existing archive */
    zarpack_progress_fn progress; /* optional */
    void *user;
} zarpack_options;

/* Where pack() will write for these inputs. Returns bytes needed including
 * the terminating NUL (like snprintf + 1), or 0 on invalid input. */
ZARPACK_API size_t zarpack_resolve_output(const char *input_dir, const char *output,
                                          char *buf, size_t buf_size);

/* Packs options->input_dir. On success the final path is written to
 * out_path; on failure err_msg holds a human-readable reason. The archive is
 * written to a temporary file and renamed, so failures and cancellation never
 * leave a partial archive behind. Either buffer may be NULL. */
ZARPACK_API zarpack_status zarpack_pack(const zarpack_options *options,
                                        char *out_path, size_t out_path_size,
                                        char *err_msg, size_t err_msg_size);

#ifdef __cplusplus
}
#endif
#endif
