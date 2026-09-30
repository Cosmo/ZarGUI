#include "zar-job.h"

struct _ZarJob {
    ZarJobFunc func;
    gpointer data;
    GDestroyNotify data_free;
    ZarJobProgressFunc progress;
    ZarJobDoneFunc done;
    GObject *owner;
    GThread *thread;
    gint cancel;

    GMutex lock;
    gboolean progress_pending;
    guint64 done_bytes, total_bytes;
    char *file;

    zarpack_status status;
    char message[32768];
};

static gboolean
report_progress (gpointer user_data)
{
    ZarJob *job = user_data;
    g_mutex_lock (&job->lock);
    guint64 done = job->done_bytes, total = job->total_bytes;
    g_autofree char *file = g_strdup (job->file);
    job->progress_pending = FALSE;
    g_mutex_unlock (&job->lock);
    job->progress (done, total, file ? file : "", job->owner);
    return G_SOURCE_REMOVE;
}

static int
on_core_progress (const zarpack_progress *progress, void *user)
{
    ZarJob *job = user;
    g_mutex_lock (&job->lock);
    job->done_bytes = progress->bytes_done;
    job->total_bytes = progress->bytes_total;
    g_free (job->file);
    job->file = g_strdup (progress->current_file ? progress->current_file : "");
    gboolean schedule = !job->progress_pending;
    job->progress_pending = TRUE;
    g_mutex_unlock (&job->lock);
    if (schedule && job->progress)
        g_idle_add (report_progress, job); /* queued before the completion, so it never outlives the job */
    return g_atomic_int_get (&job->cancel);
}

zarpack_progress_fn
zar_job_progress_callback (void)
{
    return on_core_progress;
}

static gboolean
finish (gpointer user_data)
{
    ZarJob *job = user_data;
    g_thread_join (job->thread);
    job->done (job->status, job->message, job->owner);
    if (job->data_free)
        job->data_free (job->data);
    g_object_unref (job->owner);
    g_mutex_clear (&job->lock);
    g_free (job->file);
    g_free (job);
    return G_SOURCE_REMOVE;
}

static gpointer
run (gpointer user_data)
{
    ZarJob *job = user_data;
    job->status = job->func (job, job->data, job->message, sizeof job->message);
    g_idle_add (finish, job);
    return NULL;
}

ZarJob *
zar_job_start (ZarJobFunc func, gpointer data, GDestroyNotify data_free,
               ZarJobProgressFunc progress, ZarJobDoneFunc done, GObject *owner)
{
    ZarJob *job = g_new0 (ZarJob, 1);
    job->func = func;
    job->data = data;
    job->data_free = data_free;
    job->progress = progress;
    job->done = done;
    job->owner = g_object_ref (owner);
    g_mutex_init (&job->lock);
    job->thread = g_thread_new ("zargui-job", run, job);
    return job;
}

void
zar_job_cancel (ZarJob *job)
{
    g_atomic_int_set (&job->cancel, 1);
}
