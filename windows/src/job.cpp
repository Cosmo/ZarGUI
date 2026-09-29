#include "job.hpp"

Job::~Job() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

void Job::Start(HWND window, Work work) {
    window_ = window;
    cancel_ = false;
    progress_ = {};
    message_.clear();
    thread_ = std::thread([this, work = std::move(work)] {
        status_ = work(&Job::OnProgress, this, message_);
        PostMessageW(window_, WM_APP_DONE, 0, 0);
    });
}

zarpack_status Job::Finish(std::wstring &message) {
    if (thread_.joinable()) thread_.join();
    message = ToWide(message_);
    return status_;
}

Job::Progress Job::Latest() {
    progressPosted_ = false;
    std::lock_guard lock(mutex_);
    return progress_;
}

int Job::OnProgress(const zarpack_progress *progress, void *self) {
    auto *job = static_cast<Job *>(self);
    {
        std::lock_guard lock(job->mutex_);
        job->progress_.done = progress->bytes_done;
        job->progress_.total = progress->bytes_total;
        job->progress_.file = ToWide(progress->current_file ? progress->current_file : "");
    }
    // One pending message at a time; the window reads the latest state.
    if (!job->progressPosted_.exchange(true)) PostMessageW(job->window_, WM_APP_PROGRESS, 0, 0);
    return job->cancel_ ? 1 : 0;
}
