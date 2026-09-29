/* zarpack: create ZArchive (.zar) files from directories, and list/extract them.
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
    ZARPACK_ERR_INPUT = 2,        /* input is not a readable directory / not a valid archive */
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
    /* 0 (default): skip system metadata such as .DS_Store, ._* files,
     * .Spotlight-V100, .Trashes, Thumbs.db and desktop.ini. */
    int keep_system_files;
    /* zstd level 1-19; 0 means the default (6). Readers need no setting. */
    int compression_level;
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

/* Reading.
 * An opened archive is validated and its whole tree is loaded up front
 * (names checked for path traversal, cycles and duplicates rejected).
 * Entries are in depth-first order, directories first, then by name;
 * a parent always comes before its children. */

typedef struct zarpack_archive zarpack_archive;

typedef struct zarpack_entry {
    const char *path;             /* "dir/sub/file.bin", valid until zarpack_close */
    const char *name;             /* last path component */
    uint64_t size;                /* file size, or total size of a directory's files */
    int64_t parent;               /* index of the parent entry, -1 for top level */
    int is_dir;
} zarpack_entry;

ZARPACK_API zarpack_status zarpack_open(const char *archive_path, zarpack_archive **out_archive,
                                        char *err_msg, size_t err_msg_size);
ZARPACK_API void zarpack_close(zarpack_archive *archive);
ZARPACK_API size_t zarpack_entry_count(const zarpack_archive *archive);
/* Returns 0 if index is out of range. */
ZARPACK_API int zarpack_entry_get(const zarpack_archive *archive, size_t index, zarpack_entry *out_entry);

/* Extracts the given entries into dest_dir: each one becomes dest_dir/<name>
 * (directories with all their contents). Entries inside another selected
 * directory are skipped. count == 0 extracts everything into dest_dir.
 * dest_dir is created if needed. If any target file already exists and
 * overwrite == 0, nothing is written and ZARPACK_ERR_OUTPUT_EXISTS is returned
 * (err_msg names the first conflict). Files are written to a temporary name
 * and renamed when complete; on failure or cancellation everything created by
 * this call is removed again. Progress reports bytes of file data. */
ZARPACK_API zarpack_status zarpack_extract(zarpack_archive *archive, const size_t *indices, size_t count,
                                           const char *dest_dir, int overwrite,
                                           zarpack_progress_fn progress, void *user,
                                           char *err_msg, size_t err_msg_size);

/* Extracts one entry (a directory with all its contents) to exactly
 * target_path, e.g. a location and name chosen by a drag and drop. Same
 * conflict, rollback and progress rules as zarpack_extract. */
ZARPACK_API zarpack_status zarpack_extract_entry(zarpack_archive *archive, size_t index, const char *target_path,
                                                 int overwrite, zarpack_progress_fn progress, void *user,
                                                 char *err_msg, size_t err_msg_size);

#ifdef __cplusplus
}
#endif
#endif
