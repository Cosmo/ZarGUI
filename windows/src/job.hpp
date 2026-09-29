#pragma once

#include "common.hpp"

#include "zarpack.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

/// Runs one core operation on a background thread. Progress and completion are
/// posted to the owning window as WM_APP_PROGRESS and WM_APP_DONE.
class Job {
public:
    struct Progress {
        uint64_t done = 0;
        uint64_t total = 0;
        std::wstring file;
    };

    /// Receives the progress callback and its context and returns the core's status.
    /// `message` is passed back by Finish(): an error, or e.g. the output path on success.
    using Work = std::function<zarpack_status(zarpack_progress_fn, void *, std::string &message)>;

    Job() = default;
    Job(const Job &) = delete;
    Job &operator=(const Job &) = delete;
    ~Job();

    /// Runs `work`; progress and completion are posted to `window`.
    void Start(HWND window, Work work);
    void Cancel() { cancel_ = true; }
    bool Running() const { return thread_.joinable(); }

    /// Call when WM_APP_DONE arrives; waits for the thread and returns the result.
    zarpack_status Finish(std::wstring &message);
    Progress Latest();

private:
    static int OnProgress(const zarpack_progress *progress, void *self);

    HWND window_ = nullptr;
    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> progressPosted_{false};
    std::mutex mutex_;
    Progress progress_;
    zarpack_status status_ = ZARPACK_OK;
    std::string message_;
};
