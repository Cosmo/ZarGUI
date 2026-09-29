#pragma once

#include "zarpack.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

/// Runs one core operation on a background thread.
class Job {
public:
    struct Progress {
        uint64_t done = 0;
        uint64_t total = 0;
        std::wstring file;

        /// 0 to 1, or a negative value while the total is unknown.
        double Fraction() const { return total > 0 ? static_cast<double>(done) / static_cast<double>(total) : -1; }
    };

    /// Receives the progress callback and its context and returns the core's status.
    /// `message` is passed back by Finish(): an error, or e.g. the output path on success.
    using Work = std::function<zarpack_status(zarpack_progress_fn, void *, std::string &message)>;
    /// Called on the worker thread; the UI marshals to its own thread.
    using Notify = std::function<void()>;

    Job() = default;
    Job(const Job &) = delete;
    Job &operator=(const Job &) = delete;
    ~Job();

    /// `progress` fires at most once until Latest() is read; `done` fires once at the end.
    void Start(Work work, Notify progress, Notify done);
    void Cancel() { cancel_ = true; }
    bool Running() const { return thread_.joinable(); }

    /// Call after `done`; waits for the thread and returns the result.
    zarpack_status Finish(std::wstring &message);
    Progress Latest();

private:
    static int OnProgress(const zarpack_progress *progress, void *self);

    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> progressPending_{false};
    std::mutex mutex_;
    Progress progress_;
    Notify notifyProgress_;
    zarpack_status status_ = ZARPACK_OK;
    std::string message_;
};
