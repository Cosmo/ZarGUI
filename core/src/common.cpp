#include "common.hpp"

#include <algorithm>
#include <cstring>

namespace zarpack {

void Fail(zarpack_status status, std::string message) { throw Error{status, std::move(message)}; }

fs::path FromUtf8(std::string_view s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string ToUtf8(const fs::path &p) {
    auto u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

std::string NativeUtf8(const fs::path &p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string QuotedPath(const fs::path &p) { return Quoted(NativeUtf8(p)); }

std::string Quoted(std::string_view s) { return "\"" + std::string(s) + "\""; }

fs::path Normalize(fs::path p) {
    p = p.lexically_normal();
    if (!p.has_filename() && p.has_parent_path())
        p = p.parent_path();
    return p;
}

std::string FoldCase(std::string_view s) {
    std::string r(s);
    for (char &c : r)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}

bool IsSafeName(std::string_view name) {
    if (name.empty() || name == "." || name == "..") return false;
    return name.find_first_of(std::string_view("/\\\0", 3)) == std::string_view::npos;
}

bool IsNativeFileName(std::string_view name) {
#ifdef _WIN32
    for (unsigned char c : name)
        if (c < 32) return false;
    if (name.find_first_of("<>:\"|?*") != std::string_view::npos) return false;
    if (name.back() == '.' || name.back() == ' ') return false;
    static constexpr std::string_view kReserved[] = {
        "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4", "com5", "com6", "com7",
        "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    std::string stem = FoldCase(name.substr(0, name.find('.')));
    return std::find(std::begin(kReserved), std::end(kReserved), stem) == std::end(kReserved);
#else
    (void)name;
    return true;
#endif
}

void CopyOut(std::string_view s, char *buf, size_t size) {
    if (!buf || size == 0) return;
    size_t n = std::min(s.size(), size - 1);
    std::memcpy(buf, s.data(), n);
    buf[n] = '\0';
}

TempFile::~TempFile() {
    if (path_.empty()) return;
    std::error_code ec;
    fs::remove(path_, ec);
}

void TempFile::MoveTo(const fs::path &target) {
    std::error_code ec;
    fs::rename(path_, target, ec); // replaces an existing file
    if (ec) Fail(ZARPACK_ERR_IO, "Cannot write " + QuotedPath(target) + ": " + ec.message());
    path_.clear();
}

void ProgressReporter::AddBytes(uint64_t n) {
    progress_.bytes_done += n;
    Report(false);
}

void ProgressReporter::EndFile() {
    progress_.files_done++;
    Report(false);
}

void ProgressReporter::Finish() {
    progress_.current_file = "";
    if (fn_) fn_(&progress_, user_); // too late to cancel
}

void ProgressReporter::Report(bool force) {
    if (!fn_) return;
    auto now = std::chrono::steady_clock::now();
    if (!force && now - last_ < std::chrono::milliseconds(50)) return;
    last_ = now;
    if (fn_(&progress_, user_) != 0) Fail(ZARPACK_CANCELLED, "Cancelled.");
}

} // namespace zarpack
