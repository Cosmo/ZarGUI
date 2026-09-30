#pragma once

#include <glib-object.h>

#include "zarpack.h"

G_BEGIN_DECLS

/* Runs one core operation on a worker thread. Progress and completion are
 * reported on the main thread. */
typedef struct _ZarJob ZarJob;

/* Runs on the worker thread. Pass zar_job_progress_callback() and `job` to the core.
 * Write the archive path (on success) or the error into `message`. */
typedef zarpack_status (*ZarJobFunc) (ZarJob *job, gpointer data, char *message, gsize message_size);
typedef void (*ZarJobProgressFunc) (guint64 done, guint64 total, const char *file, gpointer user_data);
typedef void (*ZarJobDoneFunc) (zarpack_status status, const char *message, gpointer user_data);

/* `owner` is kept alive until `done` has run. */
ZarJob *zar_job_start (ZarJobFunc func,
                       gpointer data,
                       GDestroyNotify data_free,
                       ZarJobProgressFunc progress,
                       ZarJobDoneFunc done,
                       GObject *owner);
void zar_job_cancel (ZarJob *job);
zarpack_progress_fn zar_job_progress_callback (void);

G_END_DECLS
