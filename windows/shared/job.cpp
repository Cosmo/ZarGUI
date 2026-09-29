#include "job.hpp"

#include "common.hpp"

Job::~Job() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

void Job::Start(Work work, Notify progress, Notify done) {
    cancel_ = false;
    progressPending_ = false;
    progress_ = {};
    message_.clear();
    notifyProgress_ = std::move(progress);
    thread_ = std::thread([this, work = std::move(work), done = std::move(done)] {
        status_ = work(&Job::OnProgress, this, message_);
        done();
    });
}

zarpack_status Job::Finish(std::wstring &message) {
    if (thread_.joinable()) thread_.join();
    message = ToWide(message_);
    return status_;
}

Job::Progress Job::Latest() {
    progressPending_ = false;
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
    if (!job->progressPending_.exchange(true)) job->notifyProgress_();
    return job->cancel_ ? 1 : 0;
}
