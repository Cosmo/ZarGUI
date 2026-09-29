#pragma once

#include "zarpack.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <new>
#include <string>
#include <string_view>

namespace zarpack {

namespace fs = std::filesystem;

/// Thrown inside the library and turned into a status + message by Guard().
struct Error {
    zarpack_status status;
    std::string message;
};

[[noreturn]] void Fail(zarpack_status status, std::string message);

fs::path FromUtf8(std::string_view s);
/// UTF-8 with forward slashes, as used inside archives.
std::string ToUtf8(const fs::path &p);
/// UTF-8 in the platform's native form.
std::string NativeUtf8(const fs::path &p);
/// NativeUtf8 in quotes, for messages.
std::string QuotedPath(const fs::path &p);
std::string Quoted(std::string_view s);
/// Strips trailing separators so "/a/b/" behaves like "/a/b".
fs::path Normalize(fs::path p);

/// ZArchive compares names case-insensitively (ASCII only).
std::string FoldCase(std::string_view s);
/// False for names that could escape the destination folder.
bool IsSafeName(std::string_view name);
/// False for names the current platform cannot store (only restrictive on Windows).
bool IsNativeFileName(std::string_view name);

void CopyOut(std::string_view s, char *buf, size_t size);

/// A file that is deleted on destruction unless it was moved into place.
class TempFile {
public:
    explicit TempFile(fs::path path) : path_(std::move(path)) {}
    TempFile(const TempFile &) = delete;
    TempFile &operator=(const TempFile &) = delete;
    ~TempFile();

    const fs::path &path() const { return path_; }
    void MoveTo(const fs::path &target);

private:
    fs::path path_;
};

/// Throttled progress reporting. Throws a ZARPACK_CANCELLED Error when the
/// callback asks to stop.
class ProgressReporter {
public:
    ProgressReporter(zarpack_progress_fn fn, void *user, uint64_t bytesTotal, uint64_t filesTotal)
        : fn_(fn), user_(user), progress_{0, bytesTotal, 0, filesTotal, ""} {}

    void Start() { Report(true); }
    /// `path` must stay valid until EndFile().
    void BeginFile(const std::string &path) { progress_.current_file = path.c_str(); }
    void AddBytes(uint64_t n);
    void EndFile();
    void Finish();

private:
    void Report(bool force);

    zarpack_progress_fn fn_;
    void *user_;
    zarpack_progress progress_;
    std::chrono::steady_clock::time_point last_{};
};

/// Runs `body` and converts any exception into a status and message.
template <class Body>
zarpack_status Guard(char *errMsg, size_t errMsgSize, Body &&body) {
    try {
        body();
        return ZARPACK_OK;
    } catch (const Error &e) {
        CopyOut(e.message, errMsg, errMsgSize);
        return e.status;
    } catch (const std::bad_alloc &) {
        CopyOut("Out of memory.", errMsg, errMsgSize);
    } catch (const std::exception &e) {
        CopyOut(e.what(), errMsg, errMsgSize);
    } catch (...) {
        CopyOut("Unknown error.", errMsg, errMsgSize);
    }
    return ZARPACK_ERR_INTERNAL;
}

} // namespace zarpack
