// quilt.cpp — single-file amalgamation (Windows platform)
// $ c++ -std=c++20 -o quilt.exe quilt.cpp -lshell32
// $ cl /std:c++20 /EHsc quilt.cpp shell32.lib
// This is free and unencumbered software released into the public domain.

#define QUILT_VERSION "0.69"

// === src/quilt.hpp ===

// This is free and unencumbered software released into the public domain.
#include <cassert>
#include <charconv>
#include <format>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Checked integer cast: asserts value is representable in the target type.
template<typename To, typename From>
constexpr To checked_cast(From value) {
    static_assert(std::is_integral_v<To> && std::is_integral_v<From>);
    assert(std::in_range<To>(value));
    return static_cast<To>(value);
}

// Parse a decimal integer from a string_view. Returns 0 on failure.
inline ptrdiff_t parse_int(std::string_view s) {
    ptrdiff_t val = 0;
    std::from_chars(s.data(), s.data() + s.size(), val);
    return val;
}

// find/rfind wrappers returning ptrdiff_t (-1 for not-found).
inline ptrdiff_t str_find(std::string_view s, char c, ptrdiff_t pos = 0) {
    auto r = s.find(c, checked_cast<size_t>(pos));
    return r == std::string_view::npos ? ptrdiff_t{-1} : static_cast<ptrdiff_t>(r);
}
inline ptrdiff_t str_find(std::string_view s, std::string_view needle, ptrdiff_t pos = 0) {
    auto r = s.find(needle, checked_cast<size_t>(pos));
    return r == std::string_view::npos ? ptrdiff_t{-1} : static_cast<ptrdiff_t>(r);
}
inline ptrdiff_t str_rfind(std::string_view s, char c) {
    auto r = s.rfind(c);
    return r == std::string_view::npos ? ptrdiff_t{-1} : static_cast<ptrdiff_t>(r);
}

// Quilt .pc/ directory state
struct QuiltState {
    std::string work_dir;        // project root
    std::string patches_dir;     // typically "patches"
    std::string pc_dir;          // typically ".pc"
    std::string series_file;     // "patches/series"
    std::string subdir;          // cwd relative to work_dir (empty at root)
    bool series_file_exists = false;

    std::vector<std::string> series;   // ordered patch names from series file
    std::vector<std::string> applied;  // applied patch names from .pc/applied-patches
    std::map<std::string, int> patch_strip_level;  // per-patch strip level from series
    std::set<std::string> patch_reversed;          // patches marked -R in series
    std::map<std::string, std::string> config;     // merged quiltrc + env settings

    // Computed helpers
    ptrdiff_t top_index() const;     // index of topmost applied in series (-1 if none)
    bool is_applied(std::string_view patch) const;
    std::optional<ptrdiff_t> find_in_series(std::string_view patch) const;
    int get_strip_level(std::string_view patch) const;  // returns 1 if not set
    std::string get_p_format(std::string_view patch) const;  // "0" or "1"
};

// I/O helpers
void out(std::string_view s);
void out_line(std::string_view s);
void err(std::string_view s);
void err_line(std::string_view s);

// Path utilities
std::string path_join(std::string_view a, std::string_view b);
std::string path_join(std::string_view a, std::string_view b, std::string_view c);
std::string basename(std::string_view path);
std::string dirname(std::string_view path);
std::string strip_trailing_slash(std::string_view s);

// String utilities
std::string trim(std::string_view s);
std::vector<std::string> split_lines(std::string_view s);
std::vector<std::string> split_on_whitespace(std::string_view s);
std::vector<std::string> shell_split(std::string_view s);

// Built-in patch engine
struct PatchOptions {
    int strip_level = 1;       // -pN
    int fuzz = 2;              // --fuzz=N (default 2)
    bool reverse = false;      // -R
    bool dry_run = false;      // --dry-run
    bool force = false;        // -f
    bool remove_empty = false; // -E
    bool quiet = false;        // -s
    bool merge = false;        // --merge
    std::string merge_style;   // "" or "diff3"
    // In-memory filesystem for fuzz testing. When non-null, all file I/O
    // in builtin_patch uses this map instead of real syscalls.
    // Key present = file exists, value = content.
    std::map<std::string, std::string> *fs = nullptr;
};

struct PatchResult {
    int exit_code;             // 0=success, 1=rejects
    std::string out;           // stdout-equivalent messages
    std::string err;           // stderr-equivalent messages
};

PatchResult builtin_patch(std::string_view patch_text, const PatchOptions &opts);

// Built-in diff engine
enum class DiffFormat { unified, context };
enum class DiffAlgorithm { myers, minimal, patience, histogram };

std::optional<DiffAlgorithm> parse_diff_algorithm(std::string_view name);

struct DiffResult {
    int exit_code;       // 0 = identical, 1 = different
    std::string output;  // formatted diff text
};

DiffResult builtin_diff(std::string_view old_path, std::string_view new_path,
                         int context_lines = 3,
                         std::string_view old_label = {},
                         std::string_view new_label = {},
                         DiffFormat format = DiffFormat::unified,
                         DiffAlgorithm algorithm = DiffAlgorithm::myers,
                         std::map<std::string, std::string> *fs = nullptr);

// Patch name helpers — shared across command files
inline std::string_view strip_patches_prefix(const QuiltState &q, std::string_view name) {
    if (name.starts_with(q.patches_dir) &&
        std::ssize(name) > std::ssize(q.patches_dir) &&
        name[checked_cast<size_t>(std::ssize(q.patches_dir))] == '/') {
        return name.substr(checked_cast<size_t>(std::ssize(q.patches_dir) + 1));
    }
    return name;
}

std::string format_patch(const QuiltState &q, std::string_view name);

inline std::string patch_path_display(const QuiltState &q, std::string_view name) {
    return format_patch(q, name);
}

// Resolve a user-provided file path relative to the current subdirectory.
inline std::string subdir_path(const QuiltState &q, std::string_view file) {
    if (q.subdir.empty()) return std::string(file);
    return q.subdir + "/" + std::string(file);
}

// Core helpers — defined in core.cpp
bool ensure_pc_dir(QuiltState &q);
std::string pc_patch_dir(const QuiltState &q, std::string_view patch);
std::vector<std::string> files_in_patch(const QuiltState &q, std::string_view patch);
bool backup_file(QuiltState &q, std::string_view patch, std::string_view file);
bool restore_file(QuiltState &q, std::string_view patch, std::string_view file);
std::vector<std::string> read_series(std::string_view path,
                                     std::map<std::string, int> *strip_levels,
                                     std::set<std::string> *reversed);
bool write_series(std::string_view path, std::span<const std::string> patches,
                  const std::map<std::string, int> &strip_levels,
                  const std::set<std::string> &reversed);
std::vector<std::string> read_applied(std::string_view path);
bool write_applied(std::string_view path, std::span<const std::string> patches);

// Command function type
using CmdFn = int (*)(QuiltState &q, int argc, char **argv);

struct Command {
    const char *name;
    CmdFn       fn;
    const char *usage;
    const char *description;
};

// Command implementations — cmd_stack.cpp
int cmd_series(QuiltState &q, int argc, char **argv);
int cmd_applied(QuiltState &q, int argc, char **argv);
int cmd_unapplied(QuiltState &q, int argc, char **argv);
int cmd_top(QuiltState &q, int argc, char **argv);
int cmd_next(QuiltState &q, int argc, char **argv);
int cmd_previous(QuiltState &q, int argc, char **argv);
int cmd_push(QuiltState &q, int argc, char **argv);
int cmd_pop(QuiltState &q, int argc, char **argv);

// Command implementations — cmd_patch.cpp
int cmd_new(QuiltState &q, int argc, char **argv);
int cmd_add(QuiltState &q, int argc, char **argv);
int cmd_remove(QuiltState &q, int argc, char **argv);
int cmd_edit(QuiltState &q, int argc, char **argv);
int cmd_refresh(QuiltState &q, int argc, char **argv);
int cmd_diff(QuiltState &q, int argc, char **argv);
int cmd_revert(QuiltState &q, int argc, char **argv);

// Command implementations — cmd_manage.cpp
int cmd_delete(QuiltState &q, int argc, char **argv);
int cmd_rename(QuiltState &q, int argc, char **argv);
int cmd_import(QuiltState &q, int argc, char **argv);
int cmd_header(QuiltState &q, int argc, char **argv);
int cmd_files(QuiltState &q, int argc, char **argv);
int cmd_patches(QuiltState &q, int argc, char **argv);
int cmd_fold(QuiltState &q, int argc, char **argv);
int cmd_fork(QuiltState &q, int argc, char **argv);

// Command implementations — cmd_mail.cpp
int cmd_mail(QuiltState &q, int argc, char **argv);

// Command implementations — cmd_patch.cpp (continued)
int cmd_snapshot(QuiltState &q, int argc, char **argv);
int cmd_init(QuiltState &q, int argc, char **argv);

// Command implementations — cmd_manage.cpp (continued)
int cmd_upgrade(QuiltState &q, int argc, char **argv);

// Command implementations — cmd_annotate.cpp
int cmd_annotate(QuiltState &q, int argc, char **argv);

// Command implementations — cmd_graph.cpp
int cmd_graph(QuiltState &q, int argc, char **argv);

// Command stubs — cmd_stubs.cpp
int cmd_grep(QuiltState &q, int argc, char **argv);
int cmd_setup(QuiltState &q, int argc, char **argv);
int cmd_shell(QuiltState &q, int argc, char **argv);

// === src/platform.hpp ===

// This is free and unencumbered software released into the public domain.
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

// Process execution
struct ProcessResult {
    int  exit_code;
    std::string out;
    std::string err;
};

ProcessResult run_cmd(const std::vector<std::string> &argv);
ProcessResult run_cmd_input(const std::vector<std::string> &argv,
                            std::string_view stdin_data);
int run_cmd_tty(const std::vector<std::string> &argv);

// File system operations
std::string read_file(std::string_view path);
bool write_file(std::string_view path, std::string_view content);
bool append_file(std::string_view path, std::string_view content);
bool copy_file(std::string_view src, std::string_view dst);
bool rename_path(std::string_view old_path, std::string_view new_path);
bool delete_file(std::string_view path);
bool delete_dir_recursive(std::string_view path);
bool make_dir(std::string_view path);
bool make_dirs(std::string_view path);
bool file_exists(std::string_view path);
bool is_directory(std::string_view path);
int64_t file_mtime(std::string_view path);  // -1 on failure

struct DirEntry {
    std::string name;
    bool        is_dir;
};
std::vector<DirEntry> list_dir(std::string_view path);

// Find all regular files recursively under a directory (relative paths)
std::vector<std::string> find_files_recursive(std::string_view dir);

// Create a new unique temporary directory; returns its path, or empty on failure
std::string make_temp_dir();

// Environment
std::string get_env(std::string_view name);
void set_env(std::string_view name, std::string_view value);
std::string get_home_dir();
std::string get_cwd();
bool set_cwd(std::string_view path);
std::string get_system_quiltrc();

// I/O
void fd_write_stdout(std::string_view s);
void fd_write_stderr(std::string_view s);
bool stdout_is_tty();

// Read all of stdin
std::string read_stdin();

// Time
struct DateTime {
    int year;       // e.g. 2026
    int month;      // 1-12
    int day;        // 1-31
    int hour;       // 0-23
    int min;        // 0-59
    int sec;        // 0-59
    int weekday;    // 0=Sun, 1=Mon, ..., 6=Sat
    int utc_offset; // seconds east of UTC
};

int64_t current_time();                    // seconds since Unix epoch
DateTime local_time(int64_t timestamp);    // broken-down local time + UTC offset

// Entry point called by platform main
int quilt_main(int argc, char **argv);

// === src/core.cpp ===

// This is free and unencumbered software released into the public domain.

ptrdiff_t QuiltState::top_index() const {
    if (applied.empty()) return -1;
    const std::string &top = applied.back();
    for (ptrdiff_t i = 0; i < std::ssize(series); ++i) {
        if (series[checked_cast<size_t>(i)] == top) return i;
    }
    return -1;
}

bool QuiltState::is_applied(std::string_view patch) const {
    for (const auto &a : applied) {
        if (a == patch) return true;
    }
    return false;
}

std::optional<ptrdiff_t> QuiltState::find_in_series(std::string_view patch) const {
    for (ptrdiff_t i = 0; i < std::ssize(series); ++i) {
        if (series[checked_cast<size_t>(i)] == patch) return i;
    }
    return std::nullopt;
}

int QuiltState::get_strip_level(std::string_view patch) const {
    auto it = patch_strip_level.find(std::string(patch));
    if (it != patch_strip_level.end()) return it->second;
    return 1;
}

std::string QuiltState::get_p_format(std::string_view patch) const {
    if (get_strip_level(patch) == 0) return "0";
    return "1";
}

void out(std::string_view s) {
    fd_write_stdout(s);
}

void out_line(std::string_view s) {
    fd_write_stdout(s);
    fd_write_stdout("\n");
}

void err(std::string_view s) {
    fd_write_stderr(s);
}

void err_line(std::string_view s) {
    fd_write_stderr(s);
    fd_write_stderr("\n");
}

static bool is_absolute_path(std::string_view p) {
    if (!p.empty() && p[0] == '/') return true;
    // Windows: drive letter (e.g. C:\, D:/)
    if (p.size() >= 3 && ((p[0] >= 'A' && p[0] <= 'Z') ||
                           (p[0] >= 'a' && p[0] <= 'z')) &&
        p[1] == ':' && (p[2] == '/' || p[2] == '\\')) return true;
    return false;
}

std::string path_join(std::string_view a, std::string_view b) {
    if (a.empty()) return std::string(b);
    if (b.empty()) return std::string(a);
    if (is_absolute_path(b)) return std::string(b);
    if (a.back() == '/' || a.back() == '\\') return std::string(a) + std::string(b);
    return std::string(a) + "/" + std::string(b);
}

std::string path_join(std::string_view a, std::string_view b, std::string_view c) {
    return path_join(path_join(a, b), c);
}

std::string basename(std::string_view path) {
    if (path.empty()) return "";
    // Strip trailing slashes
    while (std::ssize(path) > 1 && path.back() == '/')
        path.remove_suffix(1);
    auto pos = str_rfind(path, '/');
    if (pos < 0) return std::string(path);
    return std::string(path.substr(checked_cast<size_t>(pos + 1)));
}

std::string dirname(std::string_view path) {
    if (path.empty()) return ".";
    // Strip trailing slashes
    while (std::ssize(path) > 1 && path.back() == '/')
        path.remove_suffix(1);
    auto pos = str_rfind(path, '/');
    if (pos < 0) return ".";
    if (pos == 0) return "/";
    return std::string(path.substr(0, checked_cast<size_t>(pos)));
}

std::string strip_trailing_slash(std::string_view s) {
    while (!s.empty() && s.back() == '/')
        s.remove_suffix(1);
    return s.empty() ? std::string("/") : std::string(s);
}

std::string format_patch(const QuiltState &q, std::string_view name) {
    if (!get_env("QUILT_PATCHES_PREFIX").empty()) {
        return q.patches_dir + "/" + std::string(name);
    }
    return std::string(name);
}

std::string trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                          s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                          s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return std::string(s);
}

std::vector<std::string> split_lines(std::string_view s) {
    std::vector<std::string> lines;
    while (!s.empty()) {
        auto pos = str_find(s, '\n');
        if (pos < 0) {
            if (!s.empty() && s.back() == '\r')
                s.remove_suffix(1);
            lines.emplace_back(s);
            break;
        }
        auto end = checked_cast<size_t>(pos);
        if (end > 0 && s[end - 1] == '\r')
            --end;
        lines.emplace_back(s.substr(0, end));
        s.remove_prefix(checked_cast<size_t>(pos + 1));
    }
    return lines;
}


std::vector<std::string> split_on_whitespace(std::string_view s) {
    std::vector<std::string> tokens;
    ptrdiff_t i = 0;
    while (i < std::ssize(s)) {
        while (i < std::ssize(s) && (s[checked_cast<size_t>(i)] == ' ' || s[checked_cast<size_t>(i)] == '\t'))
            ++i;
        if (i >= std::ssize(s)) break;
        ptrdiff_t start = i;
        while (i < std::ssize(s) && s[checked_cast<size_t>(i)] != ' ' && s[checked_cast<size_t>(i)] != '\t')
            ++i;
        tokens.emplace_back(s.substr(checked_cast<size_t>(start), checked_cast<size_t>(i - start)));
    }
    return tokens;
}

static bool is_varname_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool is_varname_char(char c) {
    return is_varname_start(c) || (c >= '0' && c <= '9');
}

static std::string expand_var(std::string_view s, ptrdiff_t &i) {
    // i points just past '$'
    std::string name;
    if (i < std::ssize(s) && s[checked_cast<size_t>(i)] == '{') {
        ++i; // skip '{'
        while (i < std::ssize(s) && s[checked_cast<size_t>(i)] != '}') {
            name += s[checked_cast<size_t>(i++)];
        }
        if (i < std::ssize(s)) ++i; // skip '}'
    } else {
        while (i < std::ssize(s) && is_varname_char(s[checked_cast<size_t>(i)])) {
            name += s[checked_cast<size_t>(i++)];
        }
    }
    if (name.empty()) return "$";
    return get_env(name);
}

std::vector<std::string> shell_split(std::string_view s) {
    std::vector<std::string> tokens;
    ptrdiff_t i = 0;
    while (i < std::ssize(s)) {
        // Skip whitespace between tokens
        while (i < std::ssize(s) && (s[checked_cast<size_t>(i)] == ' ' || s[checked_cast<size_t>(i)] == '\t'))
            ++i;
        if (i >= std::ssize(s)) break;

        std::string tok;
        // Accumulate segments until unquoted whitespace
        while (i < std::ssize(s) && s[checked_cast<size_t>(i)] != ' ' && s[checked_cast<size_t>(i)] != '\t') {
            if (s[checked_cast<size_t>(i)] == '\'') {
                // Single-quoted: literal, no escapes, no variable expansion
                ++i;
                while (i < std::ssize(s) && s[checked_cast<size_t>(i)] != '\'')
                    tok += s[checked_cast<size_t>(i++)];
                if (i < std::ssize(s)) ++i; // skip closing '
            } else if (s[checked_cast<size_t>(i)] == '"') {
                // Double-quoted: backslash escapes and variable expansion
                ++i;
                while (i < std::ssize(s) && s[checked_cast<size_t>(i)] != '"') {
                    if (s[checked_cast<size_t>(i)] == '\\' && i + 1 < std::ssize(s)) {
                        char next = s[checked_cast<size_t>(i + 1)];
                        if (next == '"' || next == '\\' || next == '$') {
                            tok += next;
                            i += 2;
                            continue;
                        }
                    }
                    if (s[checked_cast<size_t>(i)] == '$') {
                        ++i;
                        tok += expand_var(s, i);
                        continue;
                    }
                    tok += s[checked_cast<size_t>(i++)];
                }
                if (i < std::ssize(s)) ++i; // skip closing "
            } else if (s[checked_cast<size_t>(i)] == '$') {
                // Unquoted variable expansion
                ++i;
                tok += expand_var(s, i);
            } else if (s[checked_cast<size_t>(i)] == '\\' && i + 1 < std::ssize(s)) {
                // Unquoted backslash escape
                tok += s[checked_cast<size_t>(i + 1)];
                i += 2;
            } else {
                tok += s[checked_cast<size_t>(i++)];
            }
        }
        if (!tok.empty()) {
            tokens.push_back(std::move(tok));
        }
    }
    return tokens;
}

std::vector<std::string> read_series(std::string_view path,
                                     std::map<std::string, int> *strip_levels,
                                     std::set<std::string> *reversed) {
    std::vector<std::string> patches;
    std::string content = read_file(path);
    if (content.empty()) return patches;
    auto lines = split_lines(content);
    for (auto &line : lines) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;
        // Strip inline comments
        auto hash = str_find(std::string_view(trimmed), " #");
        if (hash >= 0) {
            trimmed = trim(std::string_view(trimmed).substr(0, checked_cast<size_t>(hash)));
        }
        // Split into tokens
        auto tokens = split_on_whitespace(trimmed);
        if (tokens.empty()) continue;
        std::string name = tokens[0];
        // Parse options (e.g., "-p0", "-p 0", "-R")
        int strip = 1;
        bool is_reversed = false;
        for (ptrdiff_t i = 1; i < std::ssize(tokens); ++i) {
            if (tokens[checked_cast<size_t>(i)] == "-p" && i + 1 < std::ssize(tokens)) {
                strip = checked_cast<int>(parse_int(tokens[checked_cast<size_t>(i + 1)]));
                ++i;
            } else if (tokens[checked_cast<size_t>(i)].starts_with("-p") && std::ssize(tokens[checked_cast<size_t>(i)]) > 2) {
                strip = checked_cast<int>(parse_int(tokens[checked_cast<size_t>(i)].substr(2)));
            } else if (tokens[checked_cast<size_t>(i)] == "-R") {
                is_reversed = true;
            }
        }
        if (strip_levels && strip != 1) {
            (*strip_levels)[name] = strip;
        }
        if (reversed && is_reversed) {
            reversed->insert(name);
        }
        patches.push_back(std::move(name));
    }
    return patches;
}

bool write_series(std::string_view path, std::span<const std::string> patches,
                  const std::map<std::string, int> &strip_levels,
                  const std::set<std::string> &reversed) {
    std::string content;
    for (const auto &p : patches) {
        content += p;
        auto it = strip_levels.find(p);
        if (it != strip_levels.end() && it->second != 1) {
            content += " -p";
            content += std::to_string(it->second);
        }
        if (reversed.contains(p)) {
            content += " -R";
        }
        content += '\n';
    }
    return write_file(path, content);
}

std::vector<std::string> read_applied(std::string_view path) {
    std::vector<std::string> patches;
    std::string content = read_file(path);
    if (content.empty()) return patches;
    auto lines = split_lines(content);
    for (auto &line : lines) {
        std::string trimmed = trim(line);
        if (!trimmed.empty()) {
            patches.push_back(std::move(trimmed));
        }
    }
    return patches;
}

bool write_applied(std::string_view path, std::span<const std::string> patches) {
    std::string content;
    for (const auto &p : patches) {
        content += p;
        content += '\n';
    }
    return write_file(path, content);
}

bool ensure_pc_dir(QuiltState &q) {
    std::string pc = path_join(q.work_dir, q.pc_dir);
    if (!is_directory(pc)) {
        if (!make_dirs(pc)) {
            err_line("Failed to create " + pc);
            return false;
        }
    }
    // Write .version
    std::string version_path = path_join(pc, ".version");
    if (!file_exists(version_path)) {
        if (!write_file(version_path, "2\n")) {
            err_line("Failed to write " + version_path);
            return false;
        }
    }
    // Write .quilt_patches
    std::string qp_path = path_join(pc, ".quilt_patches");
    if (!file_exists(qp_path)) {
        if (!write_file(qp_path, q.patches_dir + "\n")) {
            err_line("Failed to write " + qp_path);
            return false;
        }
    }
    // Write .quilt_series
    std::string qs_path = path_join(pc, ".quilt_series");
    if (!file_exists(qs_path)) {
        std::string series_name = get_env("QUILT_SERIES");
        if (series_name.empty()) series_name = "series";
        if (!write_file(qs_path, series_name + "\n")) {
            err_line("Failed to write " + qs_path);
            return false;
        }
    }
    return true;
}

// Parse a simplified subset of bash KEY=VALUE assignments from a quiltrc file.
// Supports: KEY=value, KEY="value", KEY='value', export KEY=value
// Skips comments (#), blank lines, and lines that aren't assignments.
static std::map<std::string, std::string> parse_quiltrc(std::string_view content) {
    std::map<std::string, std::string> result;
    auto lines = split_lines(content);
    for (auto &line : lines) {
        std::string_view sv = line;
        // Strip leading whitespace
        while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t'))
            sv.remove_prefix(1);
        // Skip blank lines and comments
        if (sv.empty() || sv.front() == '#') continue;
        // Strip optional "export " prefix
        if (std::ssize(sv) > 7 && sv.substr(0, 7) == "export ") {
            sv.remove_prefix(7);
            while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t'))
                sv.remove_prefix(1);
        }
        // Find '=' for KEY=VALUE
        auto eq = str_find(sv, '=');
        if (eq <= 0) continue;
        // Validate key: must be alphanumeric/underscore
        std::string_view key = sv.substr(0, checked_cast<size_t>(eq));
        bool valid_key = true;
        for (char c : key) {
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_')) {
                valid_key = false;
                break;
            }
        }
        if (!valid_key) continue;
        // Parse value
        std::string_view rest = sv.substr(checked_cast<size_t>(eq + 1));
        std::string value;
        if (!rest.empty() && rest.front() == '"') {
            // Double-quoted: handle \" and \\ escapes
            rest.remove_prefix(1);
            while (!rest.empty() && rest.front() != '"') {
                if (rest.front() == '\\' && std::ssize(rest) > 1) {
                    char next = rest[1];
                    if (next == '"' || next == '\\') {
                        value += next;
                        rest.remove_prefix(2);
                        continue;
                    }
                }
                value += rest.front();
                rest.remove_prefix(1);
            }
        } else if (!rest.empty() && rest.front() == '\'') {
            // Single-quoted: literal, no escapes
            rest.remove_prefix(1);
            while (!rest.empty() && rest.front() != '\'') {
                value += rest.front();
                rest.remove_prefix(1);
            }
        } else {
            // Unquoted: ends at whitespace, line ending, or #
            while (!rest.empty() && rest.front() != ' ' && rest.front() != '\t' &&
                   rest.front() != '\r' && rest.front() != '\n' &&
                   rest.front() != '#') {
                value += rest.front();
                rest.remove_prefix(1);
            }
        }
        result[std::string(key)] = value;
    }
    return result;
}

// Load and parse the quiltrc file. If quiltrc_path is empty, use default
// search order (~/.quiltrc, /etc/quilt.quiltrc). If "-", skip loading.
static std::map<std::string, std::string> load_quiltrc(std::string_view quiltrc_path) {
    if (quiltrc_path == "-") return {};

    if (!quiltrc_path.empty()) {
        std::string content = read_file(quiltrc_path);
        if (!content.empty()) return parse_quiltrc(content);
        return {};
    }

    // Default search order
    std::string home = get_home_dir();
    if (!home.empty()) {
        std::string user_rc = path_join(home, ".quiltrc");
        std::string content = read_file(user_rc);
        if (!content.empty()) return parse_quiltrc(content);
    }

    std::string sys_rc = get_system_quiltrc();
    std::string content = read_file(sys_rc);
    if (!content.empty()) return parse_quiltrc(content);

    return {};
}

QuiltState load_state() {
    QuiltState q;
    q.patches_dir = "patches";
    q.pc_dir = ".pc";

    // Read environment variable overrides
    std::string env_pc = get_env("QUILT_PC");
    if (!env_pc.empty()) q.pc_dir = env_pc;
    std::string env_patches = get_env("QUILT_PATCHES");
    if (!env_patches.empty()) q.patches_dir = env_patches;

    // Read QUILT_SERIES override for series filename
    std::string env_series = get_env("QUILT_SERIES");

    // Upward directory scan: find project root containing .pc/ or patches/
    // Only use relative paths for scanning; absolute paths are used as-is.
    std::string cwd = get_cwd();
    std::string scan = cwd;
    bool patches_dir_is_abs = is_absolute_path(q.patches_dir);
    while (true) {
        if (is_directory(path_join(scan, q.pc_dir)) ||
            (!patches_dir_is_abs && is_directory(path_join(scan, q.patches_dir)))) {
            break;
        }
        std::string parent = dirname(scan);
        if (parent == scan) {
            // Reached filesystem root without finding anything; use cwd
            scan = cwd;
            break;
        }
        scan = parent;
    }
    q.work_dir = scan;
    if (q.work_dir != cwd) {
        // Compute subdirectory: strip work_dir prefix + '/' from cwd
        q.subdir = cwd.substr(q.work_dir.size() + 1);
        set_cwd(q.work_dir);
    }

    // Check if .pc/ exists and read overrides
    std::string pc_abs = path_join(q.work_dir, q.pc_dir);
    std::string series_name_override;
    if (is_directory(pc_abs)) {
        // Read .quilt_patches override
        std::string qp = trim(read_file(path_join(pc_abs, ".quilt_patches")));
        if (!qp.empty()) {
            q.patches_dir = qp;
        }
        // Read .quilt_series override
        std::string qs = trim(read_file(path_join(pc_abs, ".quilt_series")));
        if (!qs.empty()) {
            series_name_override = qs;
        }
    }

    // Series file search order (when not overridden by .quilt_series)
    if (q.series_file.empty()) {
        std::string series_name = !series_name_override.empty()
            ? series_name_override
            : (env_series.empty() ? "series" : env_series);
        std::string s1 = path_join(q.work_dir, series_name);
        std::string s2 = path_join(q.work_dir, q.patches_dir, series_name);
        std::string s3 = path_join(q.work_dir, q.pc_dir, series_name);
        if (file_exists(s3)) {
            q.series_file = path_join(q.pc_dir, series_name);
        } else if (file_exists(s1)) {
            q.series_file = series_name;
        } else if (file_exists(s2)) {
            q.series_file = path_join(q.patches_dir, series_name);
        } else {
            q.series_file = path_join(q.patches_dir, series_name);
        }
    }

    // Read series file
    std::string series_abs = path_join(q.work_dir, q.series_file);
    q.series_file_exists = file_exists(series_abs);
    q.series = read_series(series_abs, &q.patch_strip_level, &q.patch_reversed);

    // Read applied-patches file
    std::string applied_abs = path_join(q.work_dir, q.pc_dir, "applied-patches");
    q.applied = read_applied(applied_abs);

    return q;
}

std::string pc_patch_dir(const QuiltState &q, std::string_view patch) {
    return path_join(q.work_dir, q.pc_dir, patch);
}

std::vector<std::string> files_in_patch(const QuiltState &q, std::string_view patch) {
    std::string dir = pc_patch_dir(q, patch);
    if (!is_directory(dir)) return {};
    auto all = find_files_recursive(dir);
    std::vector<std::string> result;
    for (auto &f : all) {
        // Skip quilt metadata files (e.g. .timestamp, .needs_refresh)
        auto slash = str_rfind(std::string_view(f), '/');
        std::string_view base = (slash >= 0)
            ? std::string_view(f).substr(checked_cast<size_t>(slash + 1))
            : std::string_view(f);
        if (!base.empty() && base[0] == '.') continue;
        result.push_back(std::move(f));
    }
    return result;
}

bool backup_file(QuiltState &q, std::string_view patch, std::string_view file) {
    std::string src = path_join(q.work_dir, file);
    std::string dst = path_join(pc_patch_dir(q, patch), file);

    // Ensure destination directory exists
    std::string dst_dir = dirname(dst);
    if (!is_directory(dst_dir)) {
        if (!make_dirs(dst_dir)) {
            err_line("Failed to create directory: " + dst_dir);
            return false;
        }
    }

    if (file_exists(src)) {
        return copy_file(src, dst);
    } else {
        // File doesn't exist yet; create an empty placeholder
        return write_file(dst, "");
    }
}

bool restore_file(QuiltState &q, std::string_view patch, std::string_view file) {
    std::string backup = path_join(pc_patch_dir(q, patch), file);
    std::string target = path_join(q.work_dir, file);

    if (!file_exists(backup)) {
        err("No backup for "); err_line(file);
        return false;
    }

    std::string content = read_file(backup);
    if (content.empty()) {
        // Zero-length backup = file didn't exist before the patch — remove target
        if (file_exists(target) && !delete_file(target)) {
            err_line("Failed to remove " + target);
            return false;
        }
        return true;
    }

    // Ensure target directory exists
    std::string target_dir = dirname(target);
    if (!is_directory(target_dir)) {
        if (!make_dirs(target_dir)) {
            err_line("Failed to create directory: " + target_dir);
            return false;
        }
    }

    return write_file(target, content);
}

std::string to_cstr(std::string_view s) {
    return std::string(s);
}

static Command commands[] = {
    {"new", cmd_new,
     "Usage: quilt new [-p n] patchname\n"
     "\n"
     "Create a new empty patch and insert it after the topmost applied\n"
     "patch in the series. The new patch becomes the top of the stack\n"
     "immediately, but no patch file is written until quilt refresh.\n"
     "\n"
     "Options:\n"
     "  -p n        Set the strip level for the patch (default: 1).\n",
     "Create a new empty patch"},

    {"add", cmd_add,
     "Usage: quilt add [-P patch] file ...\n"
     "\n"
     "Register files with the topmost patch by backing up their current\n"
     "contents. Files must be added before modification so that quilt\n"
     "can capture the pre-change state. Use quilt edit to add and open\n"
     "files in a single step.\n"
     "\n"
     "Options:\n"
     "  -P patch    Add files to the named patch instead of the top.\n",
     "Add files to the topmost patch"},

    {"push", cmd_push,
     "Usage: quilt push [-afqv] [--fuzz=N] [-m] [--merge[=merge|diff3]]\n"
     "       [--leave-rejects] [--refresh] [num|patch]\n"
     "\n"
     "Apply the next unapplied patch from the series. Without arguments,\n"
     "applies one patch. With a patch name, applies patches up to and\n"
     "including it. With a number, applies that many patches.\n"
     "\n"
     "Options:\n"
     "  -a                      Apply all unapplied patches.\n"
     "  -f                      Force apply even when the patch has rejects.\n"
     "  -q                      Quiet; print only error messages.\n"
     "  -v                      Verbose; pass --verbose to patch.\n"
     "  --fuzz=N                Set the maximum fuzz factor for patch.\n"
     "  -m, --merge[=merge|diff3]\n"
     "                          Merge using patch's merge mode.\n"
     "  --leave-rejects         Leave .rej files in the working tree.\n"
     "  --refresh               Refresh each patch after applying.\n",
     "Apply patches to the source tree"},

    {"pop", cmd_pop,
     "Usage: quilt pop [-afRqv] [--refresh] [num|patch]\n"
     "\n"
     "Remove the topmost applied patch by restoring files from backup.\n"
     "Without arguments, removes one patch. With a patch name, removes\n"
     "patches until the named patch is on top. With a number, removes\n"
     "that many patches.\n"
     "\n"
     "Options:\n"
     "  -a          Remove all applied patches.\n"
     "  -f          Force removal even if the patch needs refresh.\n"
     "  -R          Always verify that the patch removes cleanly.\n"
     "  -q          Quiet; print only error messages.\n"
     "  -v          Verbose; print file-level restore messages.\n"
     "  --refresh   Automatically refresh every patch before it gets unapplied.\n",
     "Remove applied patches from the stack"},

    {"refresh", cmd_refresh,
     "Usage: quilt refresh [-p n] [-u | -U num | -c | -C num] [-z [new_name]]\n"
     "       [-f] [--no-timestamps] [--no-index] [--diffstat] [--sort]\n"
     "       [--strip-trailing-whitespace] [--backup]\n"
     "       [--diff-algorithm={myers|minimal|patience|histogram}] [patch]\n"
     "\n"
     "Regenerate the topmost or named patch by diffing backup copies in\n"
     ".pc/ against the current working tree. This is what actually writes\n"
     "the patch file; changes are not recorded until you refresh.\n"
     "\n"
     "Options:\n"
     "  -p n              Set the path label style (0, 1, or ab).\n"
     "  -u                Create a unified diff (default).\n"
     "  -U num            Create a unified diff with num lines of context.\n"
     "  -c                Create a context diff.\n"
     "  -C num            Create a context diff with num lines of context.\n"
     "  -z [new_name]     Create a new patch (fork) containing the changes;\n"
     "                    the current patch is left as-is.\n"
     "  -f                Refresh even when files are shadowed by patches\n"
     "                    applied above.\n"
     "  --no-timestamps   Omit timestamps from diff headers.\n"
     "  --no-index        Omit Index: lines from the patch.\n"
     "  --diffstat        Add a diffstat section to the patch header.\n"
     "  --sort            Sort files alphabetically in the patch.\n"
     "  --strip-trailing-whitespace\n"
     "                    Strip trailing whitespace from each line.\n"
     "  --backup          Save the old patch file as name~ before updating.\n"
     "  --diff-algorithm=name\n"
     "                    Select the diff algorithm: myers (default),\n"
     "                    minimal, patience, or histogram.\n"
     "\n"
     "The QUILT_DIFF_ALGORITHM environment variable sets the default\n"
     "algorithm (overridden by --diff-algorithm on the command line).\n",
     "Regenerate a patch from working tree changes"},

    {"diff", cmd_diff,
     "Usage: quilt diff [-p n] [-u | -U num | -c | -C num]\n"
     "       [--combine patch] [-P patch] [-z] [-R] [--snapshot]\n"
     "       [--diff=utility] [--no-timestamps] [--no-index] [--sort]\n"
     "       [--diff-algorithm={myers|minimal|patience|histogram}] [file ...]\n"
     "\n"
     "Show the diff that quilt refresh would produce for the topmost or\n"
     "named patch. Without -z, shows the full patch content (backup vs.\n"
     "working tree). With -z, shows only uncommitted changes since the\n"
     "last refresh.\n"
     "\n"
     "Options:\n"
     "  -p n              Set the path label style (0, 1, or ab).\n"
     "  -u                Create a unified diff (default).\n"
     "  -U num            Create a unified diff with num lines of context.\n"
     "  -c                Create a context diff.\n"
     "  -C num            Create a context diff with num lines of context.\n"
     "  --combine patch   Create a combined diff for all patches between\n"
     "                    this patch and the topmost or specified patch.\n"
     "                    A patch name of '-' is the first applied patch.\n"
     "  -P patch          Show the diff for the named patch.\n"
     "  -z                Show only changes since the last refresh.\n"
     "  -R                Produce a reverse diff.\n"
     "  --snapshot        Diff against a previously saved snapshot.\n"
     "  --diff=utility    Use the specified diff utility instead of the\n"
     "                    built-in diff engine.\n"
     "  --no-timestamps   Omit timestamps from diff headers.\n"
     "  --no-index        Omit Index: lines from the output.\n"
     "  --sort            Sort files alphabetically in the output.\n"
     "  --diff-algorithm=name\n"
     "                    Select the diff algorithm: myers (default),\n"
     "                    minimal, patience, or histogram.\n"
     "\n"
     "The QUILT_DIFF_ALGORITHM environment variable sets the default\n"
     "algorithm (overridden by --diff-algorithm on the command line).\n",
     "Show the diff of the topmost or a specified patch"},

    {"series", cmd_series,
     "Usage: quilt series [-v]\n"
     "\n"
     "List all patches in the series file, both applied and unapplied.\n"
     "\n"
     "Options:\n"
     "  -v          Mark applied patches with = and the top with =.\n",
     "List all patches in the series"},

    {"applied", cmd_applied,
     "Usage: quilt applied [patch]\n"
     "\n"
     "List the currently applied patches in stack order. With a patch\n"
     "name, lists all applied patches up to and including it.\n",
     "List applied patches"},

    {"unapplied", cmd_unapplied,
     "Usage: quilt unapplied [patch]\n"
     "\n"
     "List the patches that have not been applied yet. With a patch\n"
     "name, lists all patches after the named one in the series.\n",
     "List patches not yet applied"},

    {"top", cmd_top,
     "Usage: quilt top\n"
     "\n"
     "Print the name of the topmost applied patch.\n",
     "Show the topmost applied patch"},

    {"next", cmd_next,
     "Usage: quilt next [patch]\n"
     "\n"
     "Print the patch after the topmost applied patch, or after the\n"
     "named patch in the series.\n",
     "Show the next patch after the top or a given patch"},

    {"previous", cmd_previous,
     "Usage: quilt previous [patch]\n"
     "\n"
     "Print the patch before the topmost applied patch, or before the\n"
     "named patch in the series.\n",
     "Show the patch before the top or a given patch"},

    {"delete", cmd_delete,
     "Usage: quilt delete [-r] [--backup] [-n] [patch]\n"
     "\n"
     "Remove the topmost applied patch or a named unapplied patch from\n"
     "the series. The patch file is kept unless -r is given.\n"
     "\n"
     "Options:\n"
     "  -r          Remove the patch file as well.\n"
     "  --backup    Rename the patch file to name~ instead of deleting.\n"
     "  -n          Delete the next unapplied patch instead of the top.\n",
     "Remove a patch from the series"},

    {"rename", cmd_rename,
     "Usage: quilt rename [-P patch] new_name\n"
     "\n"
     "Rename the topmost or named patch. Updates the series file and\n"
     "renames the patch file in the patches directory.\n"
     "\n"
     "Options:\n"
     "  -P patch    Rename the named patch instead of the top.\n",
     "Rename a patch"},

    {"import", cmd_import,
     "Usage: quilt import [-p n] [-R] [-P name] [-f] [-d {o|a|n}] file ...\n"
     "\n"
     "Copy an external patch file into the patches directory and add it\n"
     "to the series after the topmost applied patch. The patch is not\n"
     "applied; use quilt push afterward.\n"
     "\n"
     "Options:\n"
     "  -p n        Set the strip level for the imported patch.\n"
     "  -R          Apply patch in reverse.\n"
     "  -P name     Use this name instead of the original filename.\n"
     "  -f          Overwrite if a patch with the same name exists.\n"
     "  -d {o|a|n}  When overwriting: keep old, append all, or use\n"
     "              new header.\n",
     "Import an external patch into the series"},

    {"header", cmd_header,
     "Usage: quilt header [-a|-r|-e] [--backup] [--dep3]\n"
     "       [--strip-diffstat] [--strip-trailing-whitespace] [patch]\n"
     "\n"
     "Print the header (description) of the topmost or named patch.\n"
     "The header is all text in the patch file before the first diff.\n"
     "\n"
     "Options:\n"
     "  -a                Append text from standard input to the header.\n"
     "  -r                Replace the header with text from standard input.\n"
     "  -e                Open the header in $EDITOR.\n"
     "  --backup          Save the old patch file as name~ before modifying.\n"
     "  --dep3            Insert DEP-3 template when editing empty headers.\n"
     "  --strip-diffstat  Remove the diffstat section from the header.\n"
     "  --strip-trailing-whitespace\n"
     "                    Strip trailing whitespace from each header line.\n",
     "Print or modify a patch header"},

    {"files", cmd_files,
     "Usage: quilt files [-v] [-a] [-l] [--combine patch] [patch]\n"
     "\n"
     "List the files that the topmost or named patch modifies.\n"
     "\n"
     "Options:\n"
     "  -v              Show the patch name alongside each filename.\n"
     "  -a              List files for all applied patches, not just one.\n"
     "  -l              Add patch name to output lines.\n"
     "  --combine patch List files for a range of patches.\n",
     "List files modified by a patch"},

    {"patches", cmd_patches,
     "Usage: quilt patches [-v] file ...\n"
     "\n"
     "List the patches that modify the given file or files. Searches\n"
     "both applied patches (via .pc/ metadata) and unapplied patches\n"
     "(by parsing patch files).\n"
     "\n"
     "Options:\n"
     "  -v          Mark applied patches in the output.\n",
     "List patches that modify a given file"},

    {"edit", cmd_edit,
     "Usage: quilt edit file ...\n"
     "\n"
     "Add files to the topmost patch and open them in $EDITOR. This is\n"
     "a shortcut for quilt add followed by $EDITOR, and is the safest\n"
     "way to modify tracked files.\n",
     "Add files to the topmost patch and open an editor"},

    {"revert", cmd_revert,
     "Usage: quilt revert [-P patch] file ...\n"
     "\n"
     "Discard uncommitted changes to files by restoring them from the\n"
     "backup copies in .pc/. Only reverts changes not yet captured by\n"
     "quilt refresh.\n"
     "\n"
     "Options:\n"
     "  -P patch    Revert files in the named patch instead of the top.\n",
     "Discard working tree changes to files in a patch"},

    {"remove", cmd_remove,
     "Usage: quilt remove [-P patch] file ...\n"
     "\n"
     "Remove files from the topmost or named patch and restore them\n"
     "from backup. The opposite of quilt add.\n"
     "\n"
     "Options:\n"
     "  -P patch    Remove files from the named patch instead of the top.\n",
     "Remove files from the topmost patch"},

    {"fold", cmd_fold,
     "Usage: quilt fold [-R] [-q] [-f] [-p n]\n"
     "\n"
     "Fold a diff read from standard input into the topmost patch.\n"
     "Files touched by the incoming diff are automatically added to\n"
     "the patch. Run quilt refresh afterward to update the patch file.\n"
     "\n"
     "Options:\n"
     "  -R          Apply the diff in reverse.\n"
     "  -q          Quiet; print only error messages.\n"
     "  -f          Force apply even when the diff has rejects.\n"
     "  -p n        Set the strip level for the incoming diff.\n",
     "Fold a diff from stdin into the topmost patch"},

    {"fork", cmd_fork,
     "Usage: quilt fork [new_name]\n"
     "\n"
     "Copy the topmost patch to a new name. The series is updated to\n"
     "reference the copy; the original file is kept but removed from\n"
     "the series. If no name is given, -2 is appended (or -3, etc.).\n",
     "Create a copy of the topmost patch under a new name"},

    // Implemented analysis commands
    {"annotate", cmd_annotate,
     "Usage: quilt annotate [-P patch] file\n"
     "\n"
     "Show which applied patch last modified each line of a file,\n"
     "similar to git blame. Works by comparing successive backup\n"
     "copies in .pc/.\n"
     "\n"
     "Options:\n"
     "  -P patch    Stop at the named patch instead of the top.\n",
     "Show which patch modified each line of a file"},

    {"graph", cmd_graph,
     "Usage: quilt graph [--all] [--reduce] [--lines[=num]]\n"
     "                   [--edge-labels=files] [patch]\n"
     "\n"
     "Print a dot-format dependency graph of applied patches. Two\n"
     "patches are dependent if they modify the same file, or with\n"
     "--lines, if their changes overlap.\n"
     "\n"
     "Options:\n"
     "  --all             Include all applied patches (default: only\n"
     "                    dependencies of the top or named patch).\n"
     "  --reduce          Remove transitive edges from the graph.\n"
     "  --lines[=num]     Compute line-level dependencies using num\n"
     "                    lines of context (default: 2).\n"
     "  --edge-labels=files  Label edges with shared filenames.\n",
     "Print a dot dependency graph of applied patches"},

    {"mail", cmd_mail,
     "Usage: quilt mail {--mbox file} [--prefix prefix] [--sender addr]\n"
     "                  [--from addr] [--to addr] [--cc addr] [--bcc addr]\n"
     "                  [first_patch [last_patch]]\n"
     "\n"
     "Generate an mbox file containing one message per patch in the\n"
     "given range. Output is intended for git am. Either --from or\n"
     "--sender is required.\n"
     "\n"
     "Options:\n"
     "  --mbox file       Write output to file (required).\n"
     "  --prefix prefix   Subject line prefix (default: PATCH).\n"
     "  --sender addr     Set the envelope sender address.\n"
     "  --from addr       Set the From: header address.\n"
     "  --to addr         Add a To: recipient (repeatable).\n"
     "  --cc addr         Add a Cc: recipient (repeatable).\n"
     "  --bcc addr        Add a Bcc: recipient (repeatable).\n",
     "Generate an mbox file from a range of patches"},

    // Stubs
    {"grep", cmd_grep,
     "Usage: quilt grep [-h|options] pattern\n"
     "\n"
     "Search source files, skipping patches/ and .pc/ directories.\n"
     "Not yet implemented.\n",
     "Search source files (not implemented)"},

    {"setup", cmd_setup,
     "Usage: quilt setup [-d path] series\n"
     "\n"
     "Initialize a source tree from a series file or RPM spec.\n"
     "Not yet implemented.\n",
     "Set up a source tree from a series file (not implemented)"},

    {"shell", cmd_shell,
     "Usage: quilt shell [command]\n"
     "\n"
     "Open a shell or run a command in the quilt environment.\n"
     "Not yet implemented.\n",
     "Open a subshell (not implemented)"},

    {"snapshot", cmd_snapshot,
     "Usage: quilt snapshot [-d]\n"
     "\n"
     "Save a copy of the current working tree state for later\n"
     "comparison with quilt diff --snapshot.\n"
     "\n"
     "Options:\n"
     "  -d          Remove the current snapshot instead of creating one.\n",
     "Save a snapshot of the working tree for later diff"},

    {"upgrade", cmd_upgrade,
     "Usage: quilt upgrade\n"
     "\n"
     "Upgrade quilt metadata in .pc/ to the current format. This is\n"
     "a no-op because only the version 2 format is supported.\n",
     "Upgrade quilt metadata to the current format"},

    {"init", cmd_init,
     "Usage: quilt init\n"
     "\n"
     "Initialize quilt metadata in the current directory. This is\n"
     "optional since any quilt command creates .pc/ and patches/ as\n"
     "needed, but it lets you establish the project root before\n"
     "working from a subdirectory.\n",
     "Initialize quilt metadata in the current directory"},
};

static constexpr int num_commands = sizeof(commands) / sizeof(commands[0]);

static std::string to_upper(std::string_view s) {
    std::string result(s);
    for (char &c : result) {
        if (c >= 'a' && c <= 'z') c -= 32;
    }
    return result;
}

int quilt_main(int argc, char **argv) {
    // --- Phase 1: Extract global options (--quiltrc) from argv ---
    std::string quiltrc_path;   // empty = default search, "-" = disabled
    bool quiltrc_set = false;
    std::vector<char *> clean_argv;
    clean_argv.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--quiltrc" && i + 1 < argc) {
            quiltrc_path = argv[i + 1];
            quiltrc_set = true;
            ++i; // skip the argument
            continue;
        }
        if (a.starts_with("--quiltrc=")) {
            quiltrc_path = std::string(a.substr(10));
            quiltrc_set = true;
            continue;
        }
        if (a == "--trace") {
            continue;  // accepted but ignored
        }
        clean_argv.push_back(argv[i]);
    }
    int clean_argc = checked_cast<int>(std::ssize(clean_argv));

    // Handle no arguments
    if (clean_argc < 2) {
        err_line("Usage: quilt [--quiltrc file] <command> [options] [args]");
        err_line("Use \"quilt --help\" for a list of commands.");
        return 1;
    }

    std::string_view arg1 = clean_argv[1];

    // Handle --version
    if (arg1 == "--version" || arg1 == "-v") {
        out_line(QUILT_VERSION);
        return 0;
    }

    // Handle --help
    if (arg1 == "--help" || arg1 == "-h" || arg1 == "help") {
        out_line("Usage: quilt [--quiltrc file] <command> [options] [args]");
        out_line("");
        out_line("Commands:");
        ptrdiff_t max_len = 0;
        for (int i = 0; i < num_commands; ++i) {
            ptrdiff_t len = std::ssize(std::string_view(commands[i].name));
            if (len > max_len) max_len = len;
        }
        for (int i = 0; i < num_commands; ++i) {
            std::string line = "  ";
            line += commands[i].name;
            line.append(checked_cast<size_t>(max_len + 2 - std::ssize(std::string_view(commands[i].name))), ' ');
            line += commands[i].description;
            out_line(line);
        }
        out_line("");
        out_line("Use \"quilt <command> --help\" for details on a specific command.");
        return 0;
    }

    // --- Phase 2: Load quiltrc and populate environment ---
    auto rc_vars = load_quiltrc(quiltrc_set ? quiltrc_path : std::string());
    // Apply quiltrc values to environment. GNU quilt sources quiltrc into the
    // command process, so assignments there override inherited environment
    // values unless the rc itself chooses otherwise.
    for (auto &kv : rc_vars) {
        set_env(kv.first, kv.second);
    }

    // --- Phase 3: Find command ---
    std::string cmd_name(arg1);

    // Find command (supports unique prefix abbreviation)
    Command *found = nullptr;
    int match_count = 0;
    for (int i = 0; i < num_commands; ++i) {
        if (cmd_name == commands[i].name) {
            found = &commands[i];
            match_count = 1;
            break;
        }
        if (std::string_view(commands[i].name).starts_with(cmd_name)) {
            found = &commands[i];
            match_count++;
        }
    }

    if (match_count > 1) {
        err_line("quilt: command '" + cmd_name + "' is ambiguous");
        return 1;
    }

    if (!found) {
        err_line("quilt: unknown command '" + cmd_name + "'");
        err_line("Use \"quilt --help\" for a list of commands.");
        return 1;
    }

    // Handle per-command -h/--help before dispatching
    for (int i = 2; i < clean_argc; ++i) {
        std::string_view a = clean_argv[checked_cast<size_t>(i)];
        if (a == "-h" || a == "--help") {
            out_line(found->usage);
            return 0;
        }
    }

    // --- Phase 4: Load state ---
    std::string original_cwd = get_cwd();
    QuiltState q = load_state();
    if (std::string_view(found->name) == "init" && get_cwd() != original_cwd) {
        set_cwd(original_cwd);
    }
    q.config = rc_vars;
    // Merge env overrides into config
    for (auto &kv : rc_vars) {
        std::string env_val = get_env(kv.first);
        if (!env_val.empty()) {
            q.config[kv.first] = env_val;
        }
    }

    // --- Phase 5: Inject QUILT_COMMAND_ARGS ---
    std::string args_key = "QUILT_" + to_upper(found->name) + "_ARGS";
    std::string cmd_args = get_env(args_key);
    auto extra_args = shell_split(cmd_args);

    // Build the final argv for the command: [cmd_name, extra_args..., user_args...]
    // Command argv starts at clean_argv+1
    std::vector<std::string> final_argv_storage;
    std::vector<char *> final_argv;

    final_argv_storage.push_back(std::string(found->name));
    for (auto &ea : extra_args) {
        final_argv_storage.push_back(ea);
    }
    for (int i = 2; i < clean_argc; ++i) {
        final_argv_storage.push_back(clean_argv[checked_cast<size_t>(i)]);
    }

    for (auto &s : final_argv_storage) {
        final_argv.push_back(const_cast<char *>(s.c_str()));
    }

    // Dispatch
    return found->fn(q, checked_cast<int>(std::ssize(final_argv)), final_argv.data());
}

// === src/diff.cpp ===

// This is free and unencumbered software released into the public domain.
//
// Built-in diff engine: Myers, minimal, and patience algorithms.
// Produces unified or context diff output, replacing the need for an
// external diff binary.

#include <algorithm>
#include <cstdio>
#include <optional>
#include <unordered_map>

std::optional<DiffAlgorithm> parse_diff_algorithm(std::string_view name)
{
    if (name == "myers")     return DiffAlgorithm::myers;
    if (name == "minimal")   return DiffAlgorithm::minimal;
    if (name == "patience")  return DiffAlgorithm::patience;
    if (name == "histogram") return DiffAlgorithm::histogram;
    return std::nullopt;
}

// Approximate integer square root: next power of 2 >= sqrt(n).
// Matches libxdiff's xdl_bogosqrt().
static ptrdiff_t bogosqrt(ptrdiff_t n)
{
    ptrdiff_t r = 1;
    while (r * r < n)
        r <<= 1;
    return r;
}

// Split content into lines, preserving the information about whether
// the file ended with a newline.  Each element is one line WITHOUT its
// terminating '\n'.
struct FileLines {
    std::vector<std::string_view> lines;
    bool has_trailing_newline = true;
};

static FileLines split_file_lines(std::string_view content)
{
    FileLines fl;
    if (content.empty()) {
        fl.has_trailing_newline = true;  // empty file is fine
        return fl;
    }

    fl.has_trailing_newline = (content.back() == '\n');

    ptrdiff_t start = 0;
    ptrdiff_t len = std::ssize(content);
    for (ptrdiff_t i = 0; i < len; ++i) {
        if (content[checked_cast<size_t>(i)] == '\n') {
            fl.lines.push_back(content.substr(checked_cast<size_t>(start),
                                              checked_cast<size_t>(i - start)));
            start = i + 1;
        }
    }
    // If there's content after the last newline (no trailing newline)
    if (start < len) {
        fl.lines.push_back(content.substr(checked_cast<size_t>(start),
                                          checked_cast<size_t>(len - start)));
    }

    return fl;
}

// Myers diff algorithm.
// Returns a list of edit operations: 'E' (equal), 'D' (delete from old),
// 'I' (insert from new).
struct EditOp {
    char type;       // 'E', 'D', 'I'
    ptrdiff_t old_idx; // index in old_lines (-1 for Insert)
    ptrdiff_t new_idx; // index in new_lines (-1 for Delete)
};

// Backtrack through a Myers trace from (x,y) back to (0,0), building
// edit operations in reverse order.  Returns ops in forward order.
static std::vector<EditOp> backtrack_trace(
    const std::vector<std::vector<ptrdiff_t>> &trace,
    ptrdiff_t final_d, ptrdiff_t x, ptrdiff_t y, ptrdiff_t offset)
{
    std::vector<EditOp> ops;

    for (ptrdiff_t d = final_d; d > 0; --d) {
        const auto &prev_v = trace[checked_cast<size_t>(d)];
        ptrdiff_t k = x - y;

        ptrdiff_t prev_k;
        if (k == -d || (k != d && prev_v[checked_cast<size_t>(offset + k - 1)] < prev_v[checked_cast<size_t>(offset + k + 1)])) {
            prev_k = k + 1;  // came from insert (down)
        } else {
            prev_k = k - 1;  // came from delete (right)
        }

        ptrdiff_t prev_x = prev_v[checked_cast<size_t>(offset + prev_k)];
        ptrdiff_t prev_y = prev_x - prev_k;

        // Diagonal (equal lines) — add in reverse
        while (x > prev_x && y > prev_y) {
            --x;
            --y;
            ops.push_back({'E', x, y});
        }

        // The actual edit
        if (x > prev_x) {
            --x;
            ops.push_back({'D', x, -1});
        } else if (y > prev_y) {
            --y;
            ops.push_back({'I', -1, y});
        }
    }

    // Remaining diagonal at d=0
    while (x > 0 && y > 0) {
        --x;
        --y;
        ops.push_back({'E', x, y});
    }

    std::ranges::reverse(ops);
    return ops;
}

static std::vector<EditOp> myers_diff(
    std::span<const std::string_view> old_lines,
    std::span<const std::string_view> new_lines,
    DiffAlgorithm algorithm = DiffAlgorithm::myers)
{
    ptrdiff_t n = std::ssize(old_lines);
    ptrdiff_t m = std::ssize(new_lines);

    // Trivial cases
    if (n == 0 && m == 0) {
        return {};
    }
    if (n == 0) {
        std::vector<EditOp> ops;
        ops.reserve(checked_cast<size_t>(m));
        for (ptrdiff_t j = 0; j < m; ++j)
            ops.push_back({'I', -1, j});
        return ops;
    }
    if (m == 0) {
        std::vector<EditOp> ops;
        ops.reserve(checked_cast<size_t>(n));
        for (ptrdiff_t i = 0; i < n; ++i)
            ops.push_back({'D', i, -1});
        return ops;
    }

    // Myers' algorithm with linear-space trace recording.
    // We store the V array for each D step to reconstruct the path.
    ptrdiff_t max_d = n + m;
    // V is indexed by k + offset where k ranges from -max_d to max_d
    ptrdiff_t offset = max_d;
    ptrdiff_t v_size = 2 * max_d + 1;

    // Cost cap for myers mode (heuristic, matches libxdiff).
    // minimal mode searches the full O(ND) space.
    ptrdiff_t mxcost = max_d;
    if (algorithm == DiffAlgorithm::myers) {
        mxcost = bogosqrt(n + m);
        if (mxcost < 256) mxcost = 256;
        if (mxcost > max_d) mxcost = max_d;
    }

    // Store a copy of V for each d value to reconstruct the edit path
    std::vector<std::vector<ptrdiff_t>> trace;
    std::vector<ptrdiff_t> v(checked_cast<size_t>(v_size), -1);
    v[checked_cast<size_t>(offset + 1)] = 0;

    ptrdiff_t final_d = -1;
    for (ptrdiff_t d = 0; d <= max_d; ++d) {
        trace.push_back(v);  // save state before this round
        for (ptrdiff_t k = -d; k <= d; k += 2) {
            ptrdiff_t x;
            if (k == -d || (k != d && v[checked_cast<size_t>(offset + k - 1)] < v[checked_cast<size_t>(offset + k + 1)])) {
                x = v[checked_cast<size_t>(offset + k + 1)];  // move down (insert)
            } else {
                x = v[checked_cast<size_t>(offset + k - 1)] + 1;  // move right (delete)
            }
            ptrdiff_t y = x - k;

            // Follow diagonal (equal lines)
            while (x < n && y < m && old_lines[checked_cast<size_t>(x)] == new_lines[checked_cast<size_t>(y)]) {
                ++x;
                ++y;
            }

            v[checked_cast<size_t>(offset + k)] = x;

            if (x >= n && y >= m) {
                final_d = d;
                goto found;
            }
        }

        // Cost heuristic: if we've exceeded the budget, pick the
        // furthest-reaching endpoint and construct a suboptimal script.
        // This matches libxdiff's behavior for --diff-algorithm=myers.
        if (d >= mxcost && d < max_d) {
            // Find the diagonal with maximum progress (x + y)
            ptrdiff_t best_x = -1, best_y = -1;
            for (ptrdiff_t k = -d; k <= d; k += 2) {
                ptrdiff_t x = v[checked_cast<size_t>(offset + k)];
                if (x < 0) continue;
                ptrdiff_t y = x - k;
                if (x > n || y > m || y < 0) continue;
                if (best_x < 0 || (x + y) > (best_x + best_y)) {
                    best_x = x;
                    best_y = y;
                }
            }

            if (best_x >= 0) {
                // Backtrack from the best endpoint to (0,0)
                final_d = d;
                auto ops = backtrack_trace(trace, final_d, best_x, best_y, offset);

                // Recurse on the remaining portion with a fresh search
                // budget, so matching lines past the cutoff are found.
                auto tail_old = old_lines.subspan(checked_cast<size_t>(best_x));
                auto tail_new = new_lines.subspan(checked_cast<size_t>(best_y));
                auto tail_ops = myers_diff(tail_old, tail_new, algorithm);

                // Adjust indices back to the original coordinate space
                for (auto &op : tail_ops) {
                    if (op.old_idx >= 0) op.old_idx += best_x;
                    if (op.new_idx >= 0) op.new_idx += best_y;
                    ops.push_back(op);
                }
                return ops;
            }
        }
    }
found:

    return backtrack_trace(trace, final_d, n, m, offset);
}

// Patience diff algorithm.
//
// Anchors on lines that appear exactly once in each file, computes their
// longest increasing subsequence (LIS) via patience sorting, and recurses
// on the gaps between anchors.  Falls back to Myers (minimal) when no
// unique common lines exist.
static std::vector<EditOp> patience_diff(
    std::span<const std::string_view> old_lines,
    std::span<const std::string_view> new_lines)
{
    ptrdiff_t n = std::ssize(old_lines);
    ptrdiff_t m = std::ssize(new_lines);

    if (n == 0 && m == 0) return {};
    if (n == 0) {
        std::vector<EditOp> ops;
        for (ptrdiff_t j = 0; j < m; ++j)
            ops.push_back({'I', -1, j});
        return ops;
    }
    if (m == 0) {
        std::vector<EditOp> ops;
        for (ptrdiff_t i = 0; i < n; ++i)
            ops.push_back({'D', i, -1});
        return ops;
    }

    // Step 1: Match common prefix and suffix.
    ptrdiff_t prefix = 0;
    while (prefix < n && prefix < m &&
           old_lines[checked_cast<size_t>(prefix)] == new_lines[checked_cast<size_t>(prefix)])
        ++prefix;

    ptrdiff_t suffix = 0;
    while (suffix < (n - prefix) && suffix < (m - prefix) &&
           old_lines[checked_cast<size_t>(n - 1 - suffix)] == new_lines[checked_cast<size_t>(m - 1 - suffix)])
        ++suffix;

    ptrdiff_t inner_old_len = n - prefix - suffix;
    ptrdiff_t inner_new_len = m - prefix - suffix;

    // If prefix + suffix covers everything, no interior to diff.
    if (inner_old_len <= 0 && inner_new_len <= 0) {
        std::vector<EditOp> ops;
        for (ptrdiff_t i = 0; i < n; ++i)
            ops.push_back({'E', i, i});
        return ops;
    }

    // Step 2: Find unique common lines in the interior.
    // Map line content → (count_in_old, old_idx, count_in_new, new_idx).
    struct LineInfo {
        int old_count = 0;
        ptrdiff_t old_idx = -1;
        int new_count = 0;
        ptrdiff_t new_idx = -1;
    };
    std::unordered_map<std::string_view, LineInfo> line_map;

    for (ptrdiff_t i = 0; i < inner_old_len; ++i) {
        auto &info = line_map[old_lines[checked_cast<size_t>(prefix + i)]];
        info.old_count++;
        info.old_idx = i;  // keeps last occurrence, but we only care when count==1
    }
    for (ptrdiff_t j = 0; j < inner_new_len; ++j) {
        auto &info = line_map[new_lines[checked_cast<size_t>(prefix + j)]];
        info.new_count++;
        info.new_idx = j;
    }

    // Collect unique matches, sorted by old_idx (natural insertion order
    // from scanning, but we sort explicitly to be safe).
    struct Match { ptrdiff_t old_idx; ptrdiff_t new_idx; };
    std::vector<Match> unique_matches;
    for (const auto &[line, info] : line_map) {
        if (info.old_count == 1 && info.new_count == 1) {
            unique_matches.push_back({info.old_idx, info.new_idx});
        }
    }
    std::ranges::sort(unique_matches, {}, &Match::old_idx);

    // Step 3: LIS of new_idx values via patience sorting.
    // Each pile stores (new_idx, back_pointer into flat list).
    struct Card {
        ptrdiff_t new_idx;
        ptrdiff_t old_idx;
        ptrdiff_t back;  // index into cards[] of predecessor, or -1
    };
    std::vector<Card> cards;
    std::vector<ptrdiff_t> pile_tops;  // indices into cards[] for top of each pile

    for (const auto &match : unique_matches) {
        // Binary search: find leftmost pile whose top new_idx >= match.new_idx
        ptrdiff_t lo = 0, hi = std::ssize(pile_tops);
        while (lo < hi) {
            ptrdiff_t mid = lo + (hi - lo) / 2;
            if (cards[checked_cast<size_t>(pile_tops[checked_cast<size_t>(mid)])].new_idx < match.new_idx)
                lo = mid + 1;
            else
                hi = mid;
        }

        ptrdiff_t back = (lo > 0) ? pile_tops[checked_cast<size_t>(lo - 1)] : ptrdiff_t{-1};
        ptrdiff_t card_idx = std::ssize(cards);
        cards.push_back({match.new_idx, match.old_idx, back});

        if (lo == std::ssize(pile_tops))
            pile_tops.push_back(card_idx);
        else
            pile_tops[checked_cast<size_t>(lo)] = card_idx;
    }

    // Reconstruct LIS by walking back-pointers from the last pile's top.
    std::vector<Match> anchors;
    if (!pile_tops.empty()) {
        ptrdiff_t idx = pile_tops.back();
        while (idx >= 0) {
            const auto &c = cards[checked_cast<size_t>(idx)];
            anchors.push_back({c.old_idx, c.new_idx});
            idx = c.back;
        }
        std::ranges::reverse(anchors);
    }

    // If no unique anchors found, fall back to Myers (minimal) on the interior.
    if (anchors.empty()) {
        auto inner_old = old_lines.subspan(checked_cast<size_t>(prefix),
                                           checked_cast<size_t>(inner_old_len));
        auto inner_new = new_lines.subspan(checked_cast<size_t>(prefix),
                                           checked_cast<size_t>(inner_new_len));
        auto inner_ops = myers_diff(inner_old, inner_new, DiffAlgorithm::minimal);

        // Assemble: prefix + inner + suffix
        std::vector<EditOp> ops;
        for (ptrdiff_t i = 0; i < prefix; ++i)
            ops.push_back({'E', i, i});
        for (auto &op : inner_ops) {
            if (op.old_idx >= 0) op.old_idx += prefix;
            if (op.new_idx >= 0) op.new_idx += prefix;
            ops.push_back(op);
        }
        for (ptrdiff_t i = 0; i < suffix; ++i)
            ops.push_back({'E', n - suffix + i, m - suffix + i});
        return ops;
    }

    // Step 4: Recurse on gaps between anchors.
    std::vector<EditOp> ops;

    // Emit prefix
    for (ptrdiff_t i = 0; i < prefix; ++i)
        ops.push_back({'E', i, i});

    ptrdiff_t prev_old = 0;  // interior-relative
    ptrdiff_t prev_new = 0;

    for (const auto &anchor : anchors) {
        // Gap before this anchor
        ptrdiff_t gap_old_len = anchor.old_idx - prev_old;
        ptrdiff_t gap_new_len = anchor.new_idx - prev_new;

        if (gap_old_len > 0 || gap_new_len > 0) {
            auto gap_old = old_lines.subspan(checked_cast<size_t>(prefix + prev_old),
                                             checked_cast<size_t>(gap_old_len));
            auto gap_new = new_lines.subspan(checked_cast<size_t>(prefix + prev_new),
                                             checked_cast<size_t>(gap_new_len));
            auto gap_ops = patience_diff(gap_old, gap_new);
            for (auto &op : gap_ops) {
                if (op.old_idx >= 0) op.old_idx += prefix + prev_old;
                if (op.new_idx >= 0) op.new_idx += prefix + prev_new;
                ops.push_back(op);
            }
        }

        // Emit the anchor as an equal match
        ops.push_back({'E', prefix + anchor.old_idx, prefix + anchor.new_idx});
        prev_old = anchor.old_idx + 1;
        prev_new = anchor.new_idx + 1;
    }

    // Gap after last anchor
    ptrdiff_t tail_old_len = inner_old_len - prev_old;
    ptrdiff_t tail_new_len = inner_new_len - prev_new;
    if (tail_old_len > 0 || tail_new_len > 0) {
        auto tail_old = old_lines.subspan(checked_cast<size_t>(prefix + prev_old),
                                          checked_cast<size_t>(tail_old_len));
        auto tail_new = new_lines.subspan(checked_cast<size_t>(prefix + prev_new),
                                          checked_cast<size_t>(tail_new_len));
        auto tail_ops = patience_diff(tail_old, tail_new);
        for (auto &op : tail_ops) {
            if (op.old_idx >= 0) op.old_idx += prefix + prev_old;
            if (op.new_idx >= 0) op.new_idx += prefix + prev_new;
            ops.push_back(op);
        }
    }

    // Emit suffix
    for (ptrdiff_t i = 0; i < suffix; ++i)
        ops.push_back({'E', n - suffix + i, m - suffix + i});

    return ops;
}

// Histogram diff algorithm.
//
// Extends patience diff by relaxing the strict uniqueness requirement.
// Instead of only anchoring on lines unique in both files, histogram diff
// builds an occurrence-count index of the old file and finds the longest
// contiguous matching block anchored at the lowest-occurrence line.  It
// then recurses on the regions before and after the block.  Falls back to
// Myers (minimal) when no suitable anchor exists.
static constexpr int MAX_CHAIN_LENGTH = 64;
static constexpr int MAX_RECURSION = 1024;  // same as Git's MAX_CNT_RECURSIVE

static std::vector<EditOp> histogram_diff_impl(
    std::span<const std::string_view> old_lines,
    std::span<const std::string_view> new_lines,
    int depth)
{
    ptrdiff_t n = std::ssize(old_lines);
    ptrdiff_t m = std::ssize(new_lines);

    if (n == 0 && m == 0) return {};
    if (n == 0) {
        std::vector<EditOp> ops;
        for (ptrdiff_t j = 0; j < m; ++j)
            ops.push_back({'I', -1, j});
        return ops;
    }
    if (m == 0) {
        std::vector<EditOp> ops;
        for (ptrdiff_t i = 0; i < n; ++i)
            ops.push_back({'D', i, -1});
        return ops;
    }

    // Step 1: Match common prefix and suffix.
    ptrdiff_t prefix = 0;
    while (prefix < n && prefix < m &&
           old_lines[checked_cast<size_t>(prefix)] == new_lines[checked_cast<size_t>(prefix)])
        ++prefix;

    ptrdiff_t suffix = 0;
    while (suffix < (n - prefix) && suffix < (m - prefix) &&
           old_lines[checked_cast<size_t>(n - 1 - suffix)] == new_lines[checked_cast<size_t>(m - 1 - suffix)])
        ++suffix;

    ptrdiff_t inner_old_len = n - prefix - suffix;
    ptrdiff_t inner_new_len = m - prefix - suffix;

    if (inner_old_len <= 0 && inner_new_len <= 0) {
        std::vector<EditOp> ops;
        for (ptrdiff_t i = 0; i < n; ++i)
            ops.push_back({'E', i, i});
        return ops;
    }

    // If recursion is too deep, fall back to Myers to avoid O(N²) behavior
    // on files with many unique lines (where each split removes only one line).
    if (depth >= MAX_RECURSION) {
        auto inner_old = old_lines.subspan(checked_cast<size_t>(prefix),
                                           checked_cast<size_t>(inner_old_len));
        auto inner_new = new_lines.subspan(checked_cast<size_t>(prefix),
                                           checked_cast<size_t>(inner_new_len));
        auto inner_ops = myers_diff(inner_old, inner_new, DiffAlgorithm::minimal);
        std::vector<EditOp> ops;
        for (ptrdiff_t i = 0; i < prefix; ++i)
            ops.push_back({'E', i, i});
        for (auto &op : inner_ops) {
            if (op.old_idx >= 0) op.old_idx += prefix;
            if (op.new_idx >= 0) op.new_idx += prefix;
            ops.push_back(op);
        }
        for (ptrdiff_t i = 0; i < suffix; ++i)
            ops.push_back({'E', n - suffix + i, m - suffix + i});
        return ops;
    }

    // Step 2: Build histogram of old interior lines.
    // Map line content → (occurrence count, list of positions in old).
    // Also build a parallel count array for O(1) lookup during extension.
    struct HistEntry {
        int count = 0;
        std::vector<ptrdiff_t> positions;  // interior-relative positions in old
    };
    std::unordered_map<std::string_view, HistEntry> hist;
    std::vector<int> old_count(checked_cast<size_t>(inner_old_len));

    for (ptrdiff_t i = 0; i < inner_old_len; ++i) {
        auto &entry = hist[old_lines[checked_cast<size_t>(prefix + i)]];
        entry.count++;
        entry.positions.push_back(i);
    }
    for (ptrdiff_t i = 0; i < inner_old_len; ++i) {
        auto it = hist.find(old_lines[checked_cast<size_t>(prefix + i)]);
        old_count[checked_cast<size_t>(i)] = it->second.count;
    }

    // Step 3: Scan new interior lines to find the best contiguous matching block.
    // Best block: lowest min-occurrence-count, then longest.
    //
    // Key optimizations vs. naive approach:
    //  (a) Skip lines in B whose A-count exceeds the current best threshold.
    //  (b) Skip A-occurrences whose count exceeds the threshold.
    //  (c) After extending a block forward, advance j past the extension so
    //      positions already covered are not re-scanned.
    //  (d) Use precomputed old_count[] instead of hash lookups during extension.
    ptrdiff_t best_old_start = -1, best_new_start = -1, best_len = 0;
    int best_threshold = MAX_CHAIN_LENGTH + 1;

    for (ptrdiff_t j = 0; j < inner_new_len; ) {
        auto it = hist.find(new_lines[checked_cast<size_t>(prefix + j)]);
        if (it == hist.end()) { ++j; continue; }
        const auto &entry = it->second;
        if (entry.count > MAX_CHAIN_LENGTH || entry.count > best_threshold) {
            ++j; continue;   // (a) can't improve on current best
        }

        ptrdiff_t j_advance = 1;  // how far to advance j after this iteration

        for (ptrdiff_t pi = 0; pi < std::ssize(entry.positions); ++pi) {
            ptrdiff_t oi = entry.positions[checked_cast<size_t>(pi)];

            // Extend backwards
            ptrdiff_t back = 0;
            int min_occ = entry.count;
            while (oi - back - 1 >= 0 && j - back - 1 >= 0 &&
                   old_lines[checked_cast<size_t>(prefix + oi - back - 1)] ==
                   new_lines[checked_cast<size_t>(prefix + j - back - 1)]) {
                ++back;
                int occ = old_count[checked_cast<size_t>(oi - back)];
                if (occ < min_occ) min_occ = occ;
            }

            // Extend forwards (past the initial match at (oi, j))
            ptrdiff_t fwd = 0;
            while (oi + fwd + 1 < inner_old_len && j + fwd + 1 < inner_new_len &&
                   old_lines[checked_cast<size_t>(prefix + oi + fwd + 1)] ==
                   new_lines[checked_cast<size_t>(prefix + j + fwd + 1)]) {
                ++fwd;
                int occ = old_count[checked_cast<size_t>(oi + fwd)];
                if (occ < min_occ) min_occ = occ;
            }

            ptrdiff_t block_len = back + 1 + fwd;
            ptrdiff_t block_old_start = oi - back;
            ptrdiff_t block_new_start = j - back;

            // (c) Skip j past forward extension to avoid re-scanning
            if (fwd + 1 > j_advance)
                j_advance = fwd + 1;

            // Accept if lower occurrence threshold, or same threshold but longer
            if (min_occ < best_threshold ||
                (min_occ == best_threshold && block_len > best_len)) {
                best_threshold = min_occ;
                best_old_start = block_old_start;
                best_new_start = block_new_start;
                best_len = block_len;
            }
        }
        j += j_advance;
    }

    // Step 4: If no block found, fall back to Myers (minimal).
    auto inner_old = old_lines.subspan(checked_cast<size_t>(prefix),
                                       checked_cast<size_t>(inner_old_len));
    auto inner_new = new_lines.subspan(checked_cast<size_t>(prefix),
                                       checked_cast<size_t>(inner_new_len));

    if (best_len == 0) {
        auto inner_ops = myers_diff(inner_old, inner_new, DiffAlgorithm::minimal);
        std::vector<EditOp> ops;
        for (ptrdiff_t i = 0; i < prefix; ++i)
            ops.push_back({'E', i, i});
        for (auto &op : inner_ops) {
            if (op.old_idx >= 0) op.old_idx += prefix;
            if (op.new_idx >= 0) op.new_idx += prefix;
            ops.push_back(op);
        }
        for (ptrdiff_t i = 0; i < suffix; ++i)
            ops.push_back({'E', n - suffix + i, m - suffix + i});
        return ops;
    }

    // Step 5: Recurse on regions before and after the matching block.
    std::vector<EditOp> ops;

    // Emit prefix
    for (ptrdiff_t i = 0; i < prefix; ++i)
        ops.push_back({'E', i, i});

    // Region before the block
    if (best_old_start > 0 || best_new_start > 0) {
        auto before_old = old_lines.subspan(checked_cast<size_t>(prefix),
                                            checked_cast<size_t>(best_old_start));
        auto before_new = new_lines.subspan(checked_cast<size_t>(prefix),
                                            checked_cast<size_t>(best_new_start));
        auto before_ops = histogram_diff_impl(before_old, before_new, depth + 1);
        for (auto &op : before_ops) {
            if (op.old_idx >= 0) op.old_idx += prefix;
            if (op.new_idx >= 0) op.new_idx += prefix;
            ops.push_back(op);
        }
    }

    // Emit the matching block as Equal ops
    for (ptrdiff_t k = 0; k < best_len; ++k) {
        ops.push_back({'E', prefix + best_old_start + k,
                            prefix + best_new_start + k});
    }

    // Region after the block
    ptrdiff_t after_old_start = best_old_start + best_len;
    ptrdiff_t after_new_start = best_new_start + best_len;
    ptrdiff_t after_old_len = inner_old_len - after_old_start;
    ptrdiff_t after_new_len = inner_new_len - after_new_start;

    if (after_old_len > 0 || after_new_len > 0) {
        auto after_old = old_lines.subspan(checked_cast<size_t>(prefix + after_old_start),
                                           checked_cast<size_t>(after_old_len));
        auto after_new = new_lines.subspan(checked_cast<size_t>(prefix + after_new_start),
                                           checked_cast<size_t>(after_new_len));
        auto after_ops = histogram_diff_impl(after_old, after_new, depth + 1);
        for (auto &op : after_ops) {
            if (op.old_idx >= 0) op.old_idx += prefix + after_old_start;
            if (op.new_idx >= 0) op.new_idx += prefix + after_new_start;
            ops.push_back(op);
        }
    }

    // Emit suffix
    for (ptrdiff_t i = 0; i < suffix; ++i)
        ops.push_back({'E', n - suffix + i, m - suffix + i});

    return ops;
}

static std::vector<EditOp> histogram_diff(
    std::span<const std::string_view> old_lines,
    std::span<const std::string_view> new_lines)
{
    return histogram_diff_impl(old_lines, new_lines, 0);
}

// A hunk groups consecutive edits with surrounding context lines.
struct Hunk {
    ptrdiff_t old_start;  // 1-based
    ptrdiff_t old_count;
    ptrdiff_t new_start;  // 1-based
    ptrdiff_t new_count;
    std::vector<EditOp> ops;  // the operations in this hunk (including context)
};

static std::vector<Hunk> build_hunks(const std::vector<EditOp> &ops,
                                      ptrdiff_t context_lines)
{
    std::vector<Hunk> hunks;
    if (ops.empty()) return hunks;

    // Find ranges of change (non-Equal) ops
    struct ChangeRange { ptrdiff_t first; ptrdiff_t last; }; // inclusive indices into ops
    std::vector<ChangeRange> changes;
    for (ptrdiff_t i = 0; i < std::ssize(ops); ++i) {
        if (ops[checked_cast<size_t>(i)].type != 'E') {
            if (changes.empty() || i > changes.back().last + 1) {
                changes.push_back({i, i});
            } else {
                changes.back().last = i;
            }
        }
    }

    if (changes.empty()) return hunks;

    // Merge change ranges that overlap when context is added
    std::vector<ChangeRange> merged;
    merged.push_back(changes[0]);
    for (ptrdiff_t i = 1; i < std::ssize(changes); ++i) {
        // If the context windows overlap or are adjacent, merge
        if (changes[checked_cast<size_t>(i)].first - merged.back().last <= 2 * context_lines) {
            merged.back().last = changes[checked_cast<size_t>(i)].last;
        } else {
            merged.push_back(changes[checked_cast<size_t>(i)]);
        }
    }

    // Build hunks from merged ranges
    ptrdiff_t total_ops = std::ssize(ops);
    for (const auto &range : merged) {
        ptrdiff_t hunk_start = std::max(ptrdiff_t{0}, range.first - context_lines);
        ptrdiff_t hunk_end = std::min(total_ops - 1, range.last + context_lines);

        Hunk h;
        h.ops.assign(ops.begin() + hunk_start, ops.begin() + hunk_end + 1);

        // Compute old_start, old_count, new_start, new_count
        h.old_count = 0;
        h.new_count = 0;
        h.old_start = 0;
        h.new_start = 0;
        bool first_old = true, first_new = true;

        for (const auto &op : h.ops) {
            if (op.type == 'E') {
                if (first_old) { h.old_start = op.old_idx + 1; first_old = false; }
                if (first_new) { h.new_start = op.new_idx + 1; first_new = false; }
                h.old_count++;
                h.new_count++;
            } else if (op.type == 'D') {
                if (first_old) { h.old_start = op.old_idx + 1; first_old = false; }
                h.old_count++;
            } else { // 'I'
                if (first_new) { h.new_start = op.new_idx + 1; first_new = false; }
                h.new_count++;
            }
        }

        // If a side has no lines in the hunk (pure insert or pure delete),
        // find the position from the preceding ops in the full ops list.
        if (first_old) {
            // Pure insert: old_start = last old line before insertion point
            for (ptrdiff_t i = hunk_start - 1; i >= 0; --i) {
                auto &prev = ops[checked_cast<size_t>(i)];
                if (prev.old_idx >= 0) {
                    h.old_start = prev.old_idx + 1;  // 1-based
                    break;
                }
            }
        }
        if (first_new) {
            // Pure delete: new_start = last new line before deletion point
            for (ptrdiff_t i = hunk_start - 1; i >= 0; --i) {
                auto &prev = ops[checked_cast<size_t>(i)];
                if (prev.new_idx >= 0) {
                    h.new_start = prev.new_idx + 1;  // 1-based
                    break;
                }
            }
        }

        hunks.push_back(std::move(h));
    }

    return hunks;
}

// Format unified diff output
static std::string format_unified(
    std::span<const std::string_view> old_lines,
    std::span<const std::string_view> new_lines,
    bool old_has_trailing_nl,
    bool new_has_trailing_nl,
    const std::vector<Hunk> &hunks,
    std::string_view old_label,
    std::string_view new_label)
{
    std::string result;

    // File headers
    result += "--- ";
    result += old_label;
    result += '\n';
    result += "+++ ";
    result += new_label;
    result += '\n';

    ptrdiff_t old_total = std::ssize(old_lines);
    ptrdiff_t new_total = std::ssize(new_lines);

    for (const auto &hunk : hunks) {
        // Hunk header: @@ -old_start[,old_count] +new_start[,new_count] @@
        if (hunk.old_count == 1 && hunk.new_count == 1) {
            result += std::format("@@ -{} +{} @@\n",
                                  hunk.old_start, hunk.new_start);
        } else if (hunk.old_count == 1) {
            result += std::format("@@ -{} +{},{} @@\n",
                                  hunk.old_start, hunk.new_start, hunk.new_count);
        } else if (hunk.new_count == 1) {
            result += std::format("@@ -{},{} +{} @@\n",
                                  hunk.old_start, hunk.old_count, hunk.new_start);
        } else {
            result += std::format("@@ -{},{} +{},{} @@\n",
                                  hunk.old_start, hunk.old_count,
                                  hunk.new_start, hunk.new_count);
        }

        // Hunk body
        for (const auto &op : hunk.ops) {
            if (op.type == 'E') {
                bool last_old = (op.old_idx == old_total - 1);
                bool last_new = (op.new_idx == new_total - 1);
                bool old_need_annot = last_old && !old_has_trailing_nl;
                bool new_need_annot = last_new && !new_has_trailing_nl;
                // When the trailing-newline annotation differs between
                // sides, emit as D+I so each gets its own marker.
                if (old_need_annot != new_need_annot) {
                    result += '-';
                    result += old_lines[checked_cast<size_t>(op.old_idx)];
                    result += '\n';
                    if (!old_has_trailing_nl) {
                        result += "\\ No newline at end of file\n";
                    }
                    result += '+';
                    result += new_lines[checked_cast<size_t>(op.new_idx)];
                    result += '\n';
                    if (!new_has_trailing_nl) {
                        result += "\\ No newline at end of file\n";
                    }
                } else {
                    result += ' ';
                    result += old_lines[checked_cast<size_t>(op.old_idx)];
                    result += '\n';
                    if (last_old && !old_has_trailing_nl &&
                        last_new && !new_has_trailing_nl) {
                        result += "\\ No newline at end of file\n";
                    }
                }
            } else if (op.type == 'D') {
                result += '-';
                result += old_lines[checked_cast<size_t>(op.old_idx)];
                result += '\n';
                // Check if this is the last old line with no trailing newline
                if (op.old_idx == old_total - 1 && !old_has_trailing_nl) {
                    result += "\\ No newline at end of file\n";
                }
            } else { // 'I'
                result += '+';
                result += new_lines[checked_cast<size_t>(op.new_idx)];
                result += '\n';
                // Check if this is the last new line with no trailing newline
                if (op.new_idx == new_total - 1 && !new_has_trailing_nl) {
                    result += "\\ No newline at end of file\n";
                }
            }
        }
    }

    return result;
}

// Format context diff output
static std::string format_context(
    std::span<const std::string_view> old_lines,
    std::span<const std::string_view> new_lines,
    bool old_has_trailing_nl,
    bool new_has_trailing_nl,
    const std::vector<Hunk> &hunks,
    std::string_view old_label,
    std::string_view new_label)
{
    std::string result;

    // File headers
    result += "*** ";
    result += old_label;
    result += '\n';
    result += "--- ";
    result += new_label;
    result += '\n';

    ptrdiff_t old_total = std::ssize(old_lines);
    ptrdiff_t new_total = std::ssize(new_lines);

    for (const auto &hunk : hunks) {
        result += "***************\n";

        // Classify each edit group: adjacent D and I runs form "changes" (! prefix)
        // We need to build old-side and new-side lines with proper prefixes.
        struct SideLine { char prefix; std::string_view text; bool no_newline; };
        std::vector<SideLine> old_side, new_side;

        ptrdiff_t num_ops = std::ssize(hunk.ops);
        for (ptrdiff_t k = 0; k < num_ops; ) {
            const auto &op = hunk.ops[checked_cast<size_t>(k)];
            if (op.type == 'E') {
                bool onl = (op.old_idx == old_total - 1 && !old_has_trailing_nl &&
                            op.new_idx == new_total - 1 && !new_has_trailing_nl);
                old_side.push_back({' ', old_lines[checked_cast<size_t>(op.old_idx)], onl});
                new_side.push_back({' ', new_lines[checked_cast<size_t>(op.new_idx)], onl});
                ++k;
            } else {
                // Collect consecutive D then I runs
                ptrdiff_t ds = k;
                while (k < num_ops && hunk.ops[checked_cast<size_t>(k)].type == 'D') ++k;
                ptrdiff_t de = k;  // exclusive
                while (k < num_ops && hunk.ops[checked_cast<size_t>(k)].type == 'I') ++k;
                ptrdiff_t ie = k;  // exclusive

                bool is_change = (de > ds && ie > de);
                for (ptrdiff_t j = ds; j < de; ++j) {
                    auto &dop = hunk.ops[checked_cast<size_t>(j)];
                    bool onl = (dop.old_idx == old_total - 1 && !old_has_trailing_nl);
                    old_side.push_back({is_change ? '!' : '-',
                                       old_lines[checked_cast<size_t>(dop.old_idx)], onl});
                }
                for (ptrdiff_t j = de; j < ie; ++j) {
                    auto &iop = hunk.ops[checked_cast<size_t>(j)];
                    bool onl = (iop.new_idx == new_total - 1 && !new_has_trailing_nl);
                    new_side.push_back({is_change ? '!' : '+',
                                       new_lines[checked_cast<size_t>(iop.new_idx)], onl});
                }
            }
        }

        // Old range header
        ptrdiff_t oe = hunk.old_count == 0 ? hunk.old_start : hunk.old_start + hunk.old_count - 1;
        result += std::format("*** {},{} ****\n", hunk.old_start, oe);

        // Print old-side lines only if there are changes (not just context)
        bool has_old_changes = false;
        for (const auto &sl : old_side) {
            if (sl.prefix != ' ') { has_old_changes = true; break; }
        }
        if (has_old_changes) {
            for (const auto &sl : old_side) {
                result += sl.prefix;
                result += ' ';
                result += sl.text;
                result += '\n';
                if (sl.no_newline) {
                    result += "\\ No newline at end of file\n";
                }
            }
        }

        // New range header
        ptrdiff_t ne = hunk.new_count == 0 ? hunk.new_start : hunk.new_start + hunk.new_count - 1;
        result += std::format("--- {},{} ----\n", hunk.new_start, ne);

        // Print new-side lines only if there are changes
        bool has_new_changes = false;
        for (const auto &sl : new_side) {
            if (sl.prefix != ' ') { has_new_changes = true; break; }
        }
        if (has_new_changes) {
            for (const auto &sl : new_side) {
                result += sl.prefix;
                result += ' ';
                result += sl.text;
                result += '\n';
                if (sl.no_newline) {
                    result += "\\ No newline at end of file\n";
                }
            }
        }
    }

    return result;
}

DiffResult builtin_diff(std::string_view old_path, std::string_view new_path,
                         int context_lines,
                         std::string_view old_label, std::string_view new_label,
                         DiffFormat format,
                         DiffAlgorithm algorithm,
                         std::map<std::string, std::string> *fs)
{
    // Read files — treat /dev/null or non-existent as empty
    std::string old_content, new_content;
    bool old_is_null = (old_path == "/dev/null" || old_path.empty());
    bool new_is_null = (new_path == "/dev/null" || new_path.empty());

    auto fs_exists = [&](std::string_view p) -> bool {
        if (fs) return fs->contains(std::string(p));
        return file_exists(p);
    };
    auto fs_read = [&](std::string_view p) -> std::string {
        if (fs) {
            auto it = fs->find(std::string(p));
            return it != fs->end() ? it->second : std::string{};
        }
        return read_file(p);
    };

    if (!old_is_null && fs_exists(old_path)) {
        old_content = fs_read(old_path);
    }
    if (!new_is_null && fs_exists(new_path)) {
        new_content = fs_read(new_path);
    }

    // Split into lines
    auto old_fl = split_file_lines(old_content);
    auto new_fl = split_file_lines(new_content);

    // Run diff algorithm
    std::vector<EditOp> ops;
    if (algorithm == DiffAlgorithm::patience)
        ops = patience_diff(old_fl.lines, new_fl.lines);
    else if (algorithm == DiffAlgorithm::histogram)
        ops = histogram_diff(old_fl.lines, new_fl.lines);
    else
        ops = myers_diff(old_fl.lines, new_fl.lines, algorithm);

    // Check if there are any differences
    bool has_diff = false;
    for (const auto &op : ops) {
        if (op.type != 'E') { has_diff = true; break; }
    }

    // Also check trailing newline difference
    if (!has_diff && !old_fl.lines.empty() &&
        old_fl.has_trailing_newline != new_fl.has_trailing_newline) {
        has_diff = true;
    }

    if (!has_diff) {
        return {0, ""};
    }

    // When trailing newlines differ the last line must appear as a D+I
    // pair (not a context 'E') so each side gets the right "\ No newline"
    // annotation.  Replace the trailing 'E' with D+I before building hunks
    // so that build_hunks sees a real change and includes it in a hunk.
    if (!old_fl.lines.empty() &&
        old_fl.has_trailing_newline != new_fl.has_trailing_newline) {
        // Find the last 'E' op that covers the final line of both files
        for (ptrdiff_t i = std::ssize(ops) - 1; i >= 0; --i) {
            auto &op = ops[checked_cast<size_t>(i)];
            if (op.type == 'E' &&
                op.old_idx == std::ssize(old_fl.lines) - 1 &&
                op.new_idx == std::ssize(new_fl.lines) - 1) {
                // Replace with D then I
                EditOp d_op{'D', op.old_idx, -1};
                EditOp i_op{'I', -1, op.new_idx};
                ops[checked_cast<size_t>(i)] = d_op;
                ops.insert(ops.begin() + i + 1, i_op);
                break;
            }
            if (op.type == 'E') break;  // only check the last equal op
        }
    }

    // Use labels or default to paths
    std::string old_lbl = old_label.empty() ? std::string(old_path) : std::string(old_label);
    std::string new_lbl = new_label.empty() ? std::string(new_path) : std::string(new_label);

    // Build hunks
    auto hunks = build_hunks(ops, context_lines);

    std::string output;
    if (format == DiffFormat::context) {
        output = format_context(old_fl.lines, new_fl.lines,
                                old_fl.has_trailing_newline,
                                new_fl.has_trailing_newline,
                                hunks, old_lbl, new_lbl);
    } else {
        output = format_unified(old_fl.lines, new_fl.lines,
                                old_fl.has_trailing_newline,
                                new_fl.has_trailing_newline,
                                hunks, old_lbl, new_lbl);
    }

    return {1, std::move(output)};
}

// === src/patch.cpp ===

// This is free and unencumbered software released into the public domain.
//
// Built-in patch engine for applying unified diffs.
// Implements spiral search with offset tracking, fuzz matching,
// reverse application, merge conflict markers, and reject files.

#include <algorithm>
#include <cstdio>
#include <cstdlib>

// ── Patch parsing data structures ──────────────────────────────────────

struct PatchHunk {
    ptrdiff_t old_start = 0;  // 1-based line from @@ header
    ptrdiff_t old_count = 0;
    ptrdiff_t new_start = 0;
    ptrdiff_t new_count = 0;
    std::vector<std::string> lines;  // prefixed with ' ', '+', '-'
    // Flags for "\ No newline at end of file" on old/new side
    bool old_no_newline = false;
    bool new_no_newline = false;
};

struct PatchFile {
    std::string old_path;
    std::string new_path;
    std::string target_path;   // after strip-level
    bool is_creation = false;  // old = /dev/null
    bool is_deletion = false;  // new = /dev/null
    std::vector<PatchHunk> hunks;
};

// ── Path stripping ─────────────────────────────────────────────────────

// Strip N leading path components.  Adjacent slashes count as one separator.
static std::string strip_path(std::string_view path, int strip)
{
    if (strip < 0) return std::string(path);

    std::string_view p = path;
    for (int i = 0; i < strip && !p.empty(); ++i) {
        // Skip to next slash
        ptrdiff_t slash = str_find(p, '/');
        if (slash < 0) {
            // No more slashes — strip everything
            return std::string(p);
        }
        p = p.substr(checked_cast<size_t>(slash) + 1);
        // Skip consecutive slashes
        while (!p.empty() && p[0] == '/') p = p.substr(1);
    }
    return std::string(p);
}

// Extract filename from a --- or +++ header line.
// Strips trailing tab+timestamp if present.
static std::string extract_path(std::string_view line)
{
    // line is everything after "--- " or "+++ "
    std::string_view rest = line;
    ptrdiff_t tab = str_find(rest, '\t');
    if (tab >= 0) {
        rest = rest.substr(0, checked_cast<size_t>(tab));
    }
    // Trim trailing whitespace
    while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\r')) {
        rest = rest.substr(0, checked_cast<size_t>(std::ssize(rest) - 1));
    }
    return std::string(rest);
}

// ── Unified diff parser ────────────────────────────────────────────────

// Parse a complete unified diff into a list of per-file patch descriptions.
static std::vector<PatchFile> parse_patch(std::string_view text, int strip_level,
                                           bool reverse)
{
    std::vector<PatchFile> files;
    auto lines = split_lines(text);
    ptrdiff_t n = std::ssize(lines);
    ptrdiff_t i = 0;

    while (i < n) {
        // Look for "--- " header
        if (!lines[checked_cast<size_t>(i)].starts_with("--- ")) {
            ++i;
            continue;
        }

        // Peek ahead for "+++ "
        if (i + 1 >= n || !lines[checked_cast<size_t>(i + 1)].starts_with("+++ ")) {
            ++i;
            continue;
        }

        PatchFile pf;
        std::string raw_old = extract_path(std::string_view(lines[checked_cast<size_t>(i)]).substr(4));
        std::string raw_new = extract_path(std::string_view(lines[checked_cast<size_t>(i + 1)]).substr(4));

        if (reverse) {
            std::swap(raw_old, raw_new);
        }

        pf.old_path = raw_old;
        pf.new_path = raw_new;
        pf.is_creation = (raw_old == "/dev/null");
        pf.is_deletion = (raw_new == "/dev/null");

        // Determine target path
        // Prefer new path like GNU patch does for the common -p0 case
        // where old has a .orig suffix (e.g., "--- f.txt.orig" / "+++ f.txt")
        if (pf.is_creation) {
            pf.target_path = strip_path(raw_new, strip_level);
        } else if (pf.is_deletion) {
            pf.target_path = strip_path(raw_old, strip_level);
        } else {
            std::string stripped_old = strip_path(raw_old, strip_level);
            std::string stripped_new = strip_path(raw_new, strip_level);
            // Use new path when old has .orig suffix, otherwise use
            // the shorter path (GNU patch heuristic)
            if (stripped_old.ends_with(".orig")) {
                pf.target_path = stripped_new;
            } else if (stripped_new.size() <= stripped_old.size()) {
                pf.target_path = stripped_new;
            } else {
                pf.target_path = stripped_old;
            }
        }

        i += 2;  // skip --- and +++ lines

        // Parse hunks
        while (i < n && lines[checked_cast<size_t>(i)].starts_with("@@ ")) {
            PatchHunk hunk;

            // Parse @@ -old_start[,old_count] +new_start[,new_count] @@
            std::string_view hdr = std::string_view(lines[checked_cast<size_t>(i)]);
            ptrdiff_t at1 = str_find(hdr, '-', 3);
            if (at1 < 0) { ++i; continue; }

            // Parse old range
            ptrdiff_t pos = at1 + 1;
            ptrdiff_t comma = str_find(hdr, ',', pos);
            ptrdiff_t space = str_find(hdr, ' ', pos);
            ptrdiff_t plus_pos = str_find(hdr, '+', pos);

            // Need '+' marker and a space or comma delimiter before it
            if (plus_pos < 0) { ++i; continue; }

            if (comma >= 0 && comma < plus_pos && plus_pos - comma >= 2) {
                hunk.old_start = parse_int(hdr.substr(checked_cast<size_t>(pos), checked_cast<size_t>(comma - pos)));
                hunk.old_count = parse_int(hdr.substr(checked_cast<size_t>(comma + 1), checked_cast<size_t>(plus_pos - comma - 2)));
            } else if (space >= 0 && space < plus_pos) {
                hunk.old_start = parse_int(hdr.substr(checked_cast<size_t>(pos), checked_cast<size_t>(space - pos)));
                hunk.old_count = 1;
            } else {
                ++i; continue;
            }

            // Parse new range
            pos = plus_pos + 1;
            comma = str_find(hdr, ',', pos);
            ptrdiff_t end_at = str_find(hdr, ' ', pos);
            if (end_at < 0) end_at = std::ssize(hdr);

            if (end_at <= pos) { ++i; continue; }

            if (comma >= 0 && comma < end_at) {
                hunk.new_start = parse_int(hdr.substr(checked_cast<size_t>(pos), checked_cast<size_t>(comma - pos)));
                hunk.new_count = parse_int(hdr.substr(checked_cast<size_t>(comma + 1), checked_cast<size_t>(end_at - comma - 1)));
            } else {
                hunk.new_start = parse_int(hdr.substr(checked_cast<size_t>(pos), checked_cast<size_t>(end_at - pos)));
                hunk.new_count = 1;
            }

            if (reverse) {
                std::swap(hunk.old_start, hunk.new_start);
                std::swap(hunk.old_count, hunk.new_count);
            }

            ++i;  // skip @@ line

            // Collect hunk body
            ptrdiff_t old_seen = 0, new_seen = 0;
            while (i < n) {
                std::string_view ln = lines[checked_cast<size_t>(i)];

                if (ln.starts_with("\\ No newline at end of file") ||
                    ln.starts_with("\\ no newline at end of file")) {
                    // Applies to the preceding line
                    if (!hunk.lines.empty()) {
                        // Prefixes are already swapped if reverse=true, so
                        // '-' is always the old side and '+' the new side.
                        char prev_prefix = hunk.lines.back()[0];
                        if (prev_prefix == '-')
                            hunk.old_no_newline = true;
                        else if (prev_prefix == '+')
                            hunk.new_no_newline = true;
                        else
                            hunk.old_no_newline = hunk.new_no_newline = true;
                    }
                    ++i;
                    continue;
                }

                if (ln.empty()) {
                    // Empty line in diff = context line (space was stripped)
                    if (old_seen >= hunk.old_count && new_seen >= hunk.new_count) break;
                    std::string line_str = " ";
                    hunk.lines.push_back(line_str);
                    old_seen++;
                    new_seen++;
                    ++i;
                    continue;
                }

                char prefix = ln[0];
                if (prefix == ' ' || prefix == '-' || prefix == '+') {
                    std::string line_str(ln);

                    if (reverse) {
                        if (prefix == '-') line_str[0] = '+';
                        else if (prefix == '+') line_str[0] = '-';
                    }

                    char actual_prefix = line_str[0];
                    if (actual_prefix == ' ') {
                        if (old_seen >= hunk.old_count && new_seen >= hunk.new_count) break;
                        old_seen++;
                        new_seen++;
                    } else if (actual_prefix == '-') {
                        if (old_seen >= hunk.old_count) break;
                        old_seen++;
                    } else { // '+'
                        if (new_seen >= hunk.new_count) break;
                        new_seen++;
                    }

                    hunk.lines.push_back(std::move(line_str));
                    ++i;
                } else {
                    // Start of next file section or unknown line
                    break;
                }
            }

            pf.hunks.push_back(std::move(hunk));
        }

        files.push_back(std::move(pf));
    }

    return files;
}

// ── Line-based file representation ─────────────────────────────────────

// Split file content into lines.  Each line does NOT include its trailing '\n'.
// Returns whether the file had a trailing newline.
struct FileContent {
    std::vector<std::string> lines;
    bool has_trailing_newline = true;
    bool crlf = false;  // true if original file used \r\n line endings
};

static FileContent load_file_lines(std::string_view content)
{
    FileContent fc;
    if (content.empty()) {
        fc.has_trailing_newline = true;
        return fc;
    }

    fc.has_trailing_newline = (content.back() == '\n');

    // Detect \r\n from the first line ending
    auto first_lf = str_find(content, '\n');
    if (first_lf > 0 && content[checked_cast<size_t>(first_lf - 1)] == '\r')
        fc.crlf = true;

    ptrdiff_t start = 0;
    ptrdiff_t len = std::ssize(content);
    for (ptrdiff_t i = 0; i < len; ++i) {
        if (content[checked_cast<size_t>(i)] == '\n') {
            ptrdiff_t end = i;
            if (end > start && content[checked_cast<size_t>(end - 1)] == '\r')
                --end;
            fc.lines.emplace_back(content.substr(checked_cast<size_t>(start), checked_cast<size_t>(end - start)));
            start = i + 1;
        }
    }
    if (start < len) {
        std::string tail(content.substr(checked_cast<size_t>(start), checked_cast<size_t>(len - start)));
        if (!tail.empty() && tail.back() == '\r')
            tail.pop_back();
        fc.lines.push_back(std::move(tail));
    }

    return fc;
}

// ── Hunk matching ──────────────────────────────────────────────────────

// Extract the context+deletion lines (the "old" side pattern) from a hunk.
// Returns pairs of (line_text, is_context) for matching purposes.
struct PatternLine {
    std::string_view text;  // line content (without prefix)
    bool is_context;        // true = context line, false = deletion line
};

static std::vector<PatternLine> get_old_pattern(const PatchHunk &hunk)
{
    std::vector<PatternLine> pattern;
    for (const auto &line : hunk.lines) {
        char prefix = line[0];
        std::string_view text(line);
        text = text.substr(1);
        if (prefix == ' ') {
            pattern.push_back({text, true});
        } else if (prefix == '-') {
            pattern.push_back({text, false});
        }
        // '+' lines are not part of the old-side pattern
    }
    return pattern;
}

// Count prefix and suffix context lines from the full hunk (including +/-
// lines).  This gives the true context extent: prefix context is the number
// of ' ' lines before the first '+' or '-' line, and suffix context is the
// number of ' ' lines after the last '+' or '-' line.
struct HunkContext {
    ptrdiff_t prefix = 0;
    ptrdiff_t suffix = 0;
};

// Per-hunk fuzz amounts used when matching (for trimming during application).
struct HunkFuzz {
    ptrdiff_t prefix = 0;
    ptrdiff_t suffix = 0;
};

static HunkContext get_hunk_context(const PatchHunk &hunk)
{
    HunkContext ctx;
    for (const auto &line : hunk.lines) {
        if (line[0] == ' ') ++ctx.prefix;
        else break;
    }
    for (auto it = hunk.lines.rbegin(); it != hunk.lines.rend(); ++it) {
        if ((*it)[0] == ' ') ++ctx.suffix;
        else break;
    }
    return ctx;
}

// Try to match a hunk's old-side pattern against file lines starting at
// position `pos` (0-based), with `fuzz` context lines skipped at top/bottom.
// prefix_ctx/suffix_ctx are the real context extents from the full hunk.
// Returns true if the pattern matches.
static bool try_match(std::span<const std::string> file_lines,
                      ptrdiff_t pos,
                      const std::vector<PatternLine> &pattern,
                      int fuzz,
                      ptrdiff_t prefix_ctx,
                      ptrdiff_t suffix_ctx)
{
    ptrdiff_t pat_len = std::ssize(pattern);
    if (pat_len == 0) return true;

    ptrdiff_t prefix_fuzz = std::min(static_cast<ptrdiff_t>(fuzz), prefix_ctx);
    ptrdiff_t suffix_fuzz = std::min(static_cast<ptrdiff_t>(fuzz), suffix_ctx);

    // Lines to match: skip prefix_fuzz from top, suffix_fuzz from bottom
    ptrdiff_t match_start = prefix_fuzz;
    ptrdiff_t match_end = pat_len - suffix_fuzz;

    // Adjust file position: we start matching at pos + prefix_fuzz
    ptrdiff_t file_pos = pos + prefix_fuzz;
    ptrdiff_t file_len = std::ssize(file_lines);

    for (ptrdiff_t j = match_start; j < match_end; ++j) {
        if (file_pos < 0 || file_pos >= file_len) return false;
        if (file_lines[checked_cast<size_t>(file_pos)] != pattern[checked_cast<size_t>(j)].text) return false;
        ++file_pos;
    }

    return true;
}

// Spiral search: find where a hunk matches in the file.
// Returns the 0-based file position, or -1 if not found.
// Updates cumulative_offset on success.
static ptrdiff_t locate_hunk(std::span<const std::string> file_lines,
                              const PatchHunk &hunk,
                              const std::vector<PatternLine> &pattern,
                              ptrdiff_t last_frozen_line,
                              ptrdiff_t cumulative_offset,
                              int max_fuzz)
{
    ptrdiff_t file_len = std::ssize(file_lines);
    ptrdiff_t pat_old_count = std::ssize(pattern);

    // Get real prefix/suffix context from full hunk (not just old-side pattern)
    auto ctx = get_hunk_context(hunk);

    // First guess: hunk header's old_start (1-based) converted to 0-based + offset
    ptrdiff_t first_guess = hunk.old_start - 1 + cumulative_offset;

    // Clamp to valid range
    ptrdiff_t max_pos = file_len - pat_old_count;
    if (max_pos < 0) max_pos = 0;

    for (int fuzz = 0; fuzz <= max_fuzz; ++fuzz) {
        ptrdiff_t prefix_fuzz = std::min(static_cast<ptrdiff_t>(fuzz), ctx.prefix);
        ptrdiff_t suffix_fuzz = std::min(static_cast<ptrdiff_t>(fuzz), ctx.suffix);
        ptrdiff_t effective_pat_len = pat_old_count - prefix_fuzz - suffix_fuzz;

        ptrdiff_t max_search = file_len - effective_pat_len;
        if (effective_pat_len == 0) max_search = file_len;  // empty pattern matches anywhere

        // Try exact position first
        if (first_guess >= 0 && first_guess <= max_search &&
            first_guess > last_frozen_line - 1) {
            if (try_match(file_lines, first_guess, pattern, fuzz, ctx.prefix, ctx.suffix)) {
                return first_guess;
            }
        }

        // Spiral outward
        ptrdiff_t max_offset_forward = max_search - first_guess;
        ptrdiff_t max_offset_backward = first_guess - last_frozen_line;
        ptrdiff_t max_range = std::max(max_offset_forward, max_offset_backward);
        if (max_range < 0) max_range = 0;

        for (ptrdiff_t delta = 1; delta <= max_range; ++delta) {
            // Try forward
            ptrdiff_t pos = first_guess + delta;
            if (pos >= 0 && pos <= max_search && pos > last_frozen_line - 1) {
                if (try_match(file_lines, pos, pattern, fuzz, ctx.prefix, ctx.suffix)) {
                    return pos;
                }
            }

            // Try backward
            pos = first_guess - delta;
            if (pos >= 0 && pos <= max_search && pos > last_frozen_line - 1) {
                if (try_match(file_lines, pos, pattern, fuzz, ctx.prefix, ctx.suffix)) {
                    return pos;
                }
            }
        }
    }

    return -1;  // no match found
}

// ── Hunk application ───────────────────────────────────────────────────

// Get the new-side (replacement) lines from a hunk.
static std::vector<std::string_view> get_new_lines(const PatchHunk &hunk)
{
    std::vector<std::string_view> result;
    for (const auto &line : hunk.lines) {
        char prefix = line[0];
        if (prefix == ' ' || prefix == '+') {
            result.push_back(std::string_view(line).substr(1));
        }
    }
    return result;
}

// Build the output file content after applying all successfully matched hunks.
// hunks_positions[i] = 0-based file position where hunk i matched, or -1 if rejected.
// hunk_fuzz[i] = fuzz amounts used for hunk i (to trim context from both sides).
static std::string build_output(std::span<const std::string> file_lines,
                                 bool has_trailing_newline,
                                 const PatchFile &pf,
                                 const std::vector<ptrdiff_t> &hunk_positions,
                                 const std::vector<HunkFuzz> &hunk_fuzz)
{
    std::string output;
    ptrdiff_t file_len = std::ssize(file_lines);
    ptrdiff_t last_copied = 0;  // next line to copy from input

    for (ptrdiff_t h = 0; h < std::ssize(pf.hunks); ++h) {
        ptrdiff_t pos = hunk_positions[checked_cast<size_t>(h)];
        if (pos < 0) continue;  // rejected hunk, skip

        const auto &hunk = pf.hunks[checked_cast<size_t>(h)];
        auto pattern = get_old_pattern(hunk);
        ptrdiff_t pat_len = std::ssize(pattern);
        auto new_lines = get_new_lines(hunk);

        // When fuzz was used, trim the fuzzed context lines from both sides.
        // The fuzzed prefix/suffix context lines were not matched against the
        // file, so we must not replace them.
        auto fz = hunk_fuzz[checked_cast<size_t>(h)];
        pos += fz.prefix;
        pat_len -= fz.prefix + fz.suffix;
        if (pat_len < 0) pat_len = 0;
        ptrdiff_t new_start = fz.prefix;
        ptrdiff_t new_end = std::ssize(new_lines) - fz.suffix;
        if (new_end < new_start) new_end = new_start;

        // Clamp to file bounds
        if (pos > file_len) pos = file_len;

        // Copy unchanged lines from last_copied to pos
        for (ptrdiff_t j = last_copied; j < pos; ++j) {
            output += file_lines[checked_cast<size_t>(j)];
            output += '\n';
        }

        // Write replacement lines (trimmed by fuzz)
        for (ptrdiff_t j = new_start; j < new_end; ++j) {
            output += new_lines[checked_cast<size_t>(j)];
            bool is_last_new_line = (j == new_end - 1);
            if (is_last_new_line && fz.suffix == 0 && hunk.new_no_newline) {
                // Don't add trailing newline (only when suffix not trimmed)
            } else {
                output += '\n';
            }
        }

        last_copied = pos + pat_len;
        if (last_copied > file_len) last_copied = file_len;
    }

    // Copy remaining lines
    for (ptrdiff_t j = last_copied; j < file_len; ++j) {
        output += file_lines[checked_cast<size_t>(j)];
        if (j < file_len - 1) {
            output += '\n';
        } else {
            // Last line: preserve original trailing newline status
            // unless a hunk changed it
            if (has_trailing_newline) {
                output += '\n';
            }
        }
    }

    return output;
}

// ── Merge conflict markers ─────────────────────────────────────────────

// Build output with merge conflict markers for rejected hunks.
// Applies successful hunks normally, inserts conflict markers for failed ones.
static std::string build_merge_output(std::span<const std::string> file_lines,
                                       bool has_trailing_newline,
                                       const PatchFile &pf,
                                       const std::vector<ptrdiff_t> &hunk_positions,
                                       const std::vector<HunkFuzz> &hunk_fuzz,
                                       std::string_view merge_style)
{
    // For merge mode, we first apply successful hunks, then for rejected hunks
    // we insert conflict markers at the hunk's expected position.
    std::string output;
    ptrdiff_t file_len = std::ssize(file_lines);
    ptrdiff_t last_copied = 0;

    // Process all hunks in order
    for (ptrdiff_t h = 0; h < std::ssize(pf.hunks); ++h) {
        const auto &hunk = pf.hunks[checked_cast<size_t>(h)];
        ptrdiff_t pos = hunk_positions[checked_cast<size_t>(h)];

        if (pos >= 0) {
            // Successfully matched — apply normally
            if (pos > file_len) pos = file_len;
            auto pattern = get_old_pattern(hunk);
            ptrdiff_t pat_len = std::ssize(pattern);
            auto new_lines = get_new_lines(hunk);

            // Trim fuzzed context lines
            auto fz = hunk_fuzz[checked_cast<size_t>(h)];
            pos += fz.prefix;
            pat_len -= fz.prefix + fz.suffix;
            if (pat_len < 0) pat_len = 0;
            ptrdiff_t new_start = fz.prefix;
            ptrdiff_t new_end = std::ssize(new_lines) - fz.suffix;
            if (new_end < new_start) new_end = new_start;

            if (pos > file_len) pos = file_len;

            for (ptrdiff_t j = last_copied; j < pos; ++j) {
                output += file_lines[checked_cast<size_t>(j)];
                output += '\n';
            }
            for (ptrdiff_t j = new_start; j < new_end; ++j) {
                output += new_lines[checked_cast<size_t>(j)];
                bool is_last = (j == new_end - 1);
                if (is_last && fz.suffix == 0 && hunk.new_no_newline) {
                    // no trailing newline
                } else {
                    output += '\n';
                }
            }
            last_copied = pos + pat_len;
            if (last_copied > file_len) last_copied = file_len;
        } else {
            // Rejected — insert per-change conflict markers at expected position
            ptrdiff_t expected = hunk.old_start - 1;
            if (expected < last_copied) expected = last_copied;
            if (expected > file_len) expected = file_len;

            // Copy up to expected position
            for (ptrdiff_t j = last_copied; j < expected; ++j) {
                output += file_lines[checked_cast<size_t>(j)];
                output += '\n';
            }

            // Walk through hunk lines, emitting context outside markers
            // and changed regions inside markers.
            ptrdiff_t file_pos = expected;
            ptrdiff_t hi = 0;
            ptrdiff_t hunk_len = std::ssize(hunk.lines);

            while (hi < hunk_len) {
                char prefix = hunk.lines[checked_cast<size_t>(hi)][0];

                if (prefix == ' ') {
                    // Context line — emit the file's actual line
                    if (file_pos < file_len) {
                        output += file_lines[checked_cast<size_t>(file_pos)];
                        output += '\n';
                        ++file_pos;
                    }
                    ++hi;
                } else {
                    // Changed region — collect contiguous -/+ lines
                    std::vector<std::string_view> old_lines, new_change;
                    while (hi < hunk_len && hunk.lines[checked_cast<size_t>(hi)][0] == '-') {
                        old_lines.push_back(std::string_view(hunk.lines[checked_cast<size_t>(hi)]).substr(1));
                        ++hi;
                    }
                    while (hi < hunk_len && hunk.lines[checked_cast<size_t>(hi)][0] == '+') {
                        new_change.push_back(std::string_view(hunk.lines[checked_cast<size_t>(hi)]).substr(1));
                        ++hi;
                    }

                    output += "<<<<<<<\n";

                    // Current file content for the old-side span
                    ptrdiff_t span = std::ssize(old_lines);
                    ptrdiff_t end = file_pos + span;
                    if (end > file_len) end = file_len;
                    for (ptrdiff_t j = file_pos; j < end; ++j) {
                        output += file_lines[checked_cast<size_t>(j)];
                        output += '\n';
                    }

                    if (merge_style == "diff3") {
                        output += "|||||||\n";
                        for (const auto &ol : old_lines) {
                            output += ol;
                            output += '\n';
                        }
                    }

                    output += "=======\n";
                    for (const auto &nl : new_change) {
                        output += nl;
                        output += '\n';
                    }
                    output += ">>>>>>>\n";

                    file_pos = end;
                }
            }

            last_copied = file_pos;
        }
    }

    // Copy remaining
    for (ptrdiff_t j = last_copied; j < file_len; ++j) {
        output += file_lines[checked_cast<size_t>(j)];
        if (j < file_len - 1) {
            output += '\n';
        } else if (has_trailing_newline) {
            output += '\n';
        }
    }

    return output;
}

// ── Reject file generation ─────────────────────────────────────────────

// Format rejected hunks as a unified diff .rej file.
static std::string format_rejects(const PatchFile &pf,
                                   const std::vector<bool> &rejected)
{
    std::string result;
    bool has_any = false;

    for (ptrdiff_t h = 0; h < std::ssize(pf.hunks); ++h) {
        if (!rejected[checked_cast<size_t>(h)]) continue;
        const auto &hunk = pf.hunks[checked_cast<size_t>(h)];

        if (!has_any) {
            // Write file headers
            result += "--- ";
            result += pf.old_path;
            result += '\n';
            result += "+++ ";
            result += pf.new_path;
            result += '\n';
            has_any = true;
        }

        // Write hunk header
        result += std::format("@@ -{},{} +{},{} @@\n",
                              hunk.old_start, hunk.old_count,
                              hunk.new_start, hunk.new_count);

        // Write hunk lines
        for (const auto &line : hunk.lines) {
            result += line;
            result += '\n';
        }
        if (hunk.old_no_newline) {
            result += "\\ No newline at end of file\n";
        }
    }

    return result;
}

// ── Main patch engine ──────────────────────────────────────────────────

PatchResult builtin_patch(std::string_view patch_text, const PatchOptions &opts)
{
    PatchResult result;
    result.exit_code = 0;

    // Filesystem abstraction: use in-memory map when opts.fs is set
    auto fs_exists = [&](std::string_view p) -> bool {
        if (opts.fs) return opts.fs->contains(std::string(p));
        return file_exists(p);
    };
    auto fs_read = [&](std::string_view p) -> std::string {
        if (opts.fs) {
            auto it = opts.fs->find(std::string(p));
            return it != opts.fs->end() ? it->second : std::string{};
        }
        return read_file(p);
    };
    auto fs_write = [&](std::string_view p, std::string_view c) -> bool {
        if (opts.fs) { (*opts.fs)[std::string(p)] = std::string(c); return true; }
        return write_file(p, c);
    };
    auto fs_delete = [&](std::string_view p) -> bool {
        if (opts.fs) { opts.fs->erase(std::string(p)); return true; }
        return delete_file(p);
    };

    auto files = parse_patch(patch_text, opts.strip_level, opts.reverse);

    if (files.empty()) {
        return result;
    }

    bool had_rejects = false;

    for (const auto &pf : files) {
        if (pf.target_path.empty()) continue;

        if (!opts.quiet) {
            result.out += "patching file " + pf.target_path + "\n";
        }

        // Load current file contents
        FileContent fc;
        bool file_existed = fs_exists(pf.target_path);

        if (pf.is_creation && file_existed) {
            // File exists but patch says it should be new — still try to apply
        }

        if (file_existed) {
            fc = load_file_lines(fs_read(pf.target_path));
        } else if (!pf.is_creation) {
            // File doesn't exist and this isn't a creation patch
            result.err += "can't find file to patch at input line 0\n";
            if (!opts.force) {
                result.exit_code = 1;
                if (!opts.dry_run) {
                    had_rejects = true;
                    // Write all hunks as rejects
                    std::vector<bool> all_rejected(checked_cast<size_t>(std::ssize(pf.hunks)), true);
                    std::string rej_content = format_rejects(pf, all_rejected);
                    if (!rej_content.empty()) {
                        fs_write(pf.target_path + ".rej", rej_content);
                    }
                }
                continue;
            }
        }

        // Try to match each hunk
        std::vector<ptrdiff_t> hunk_positions(checked_cast<size_t>(std::ssize(pf.hunks)), -1);
        std::vector<HunkFuzz> hunk_fuzz(checked_cast<size_t>(std::ssize(pf.hunks)));
        std::vector<bool> rejected(checked_cast<size_t>(std::ssize(pf.hunks)), false);
        ptrdiff_t cumulative_offset = 0;
        ptrdiff_t last_frozen_line = 0;  // 0-based, exclusive: lines before this are frozen
        bool file_has_rejects = false;

        for (ptrdiff_t h = 0; h < std::ssize(pf.hunks); ++h) {
            const auto &hunk = pf.hunks[checked_cast<size_t>(h)];
            auto pattern = get_old_pattern(hunk);

            ptrdiff_t pos = locate_hunk(fc.lines, hunk, pattern,
                                         last_frozen_line, cumulative_offset,
                                         opts.fuzz);

            if (pos >= 0) {
                hunk_positions[checked_cast<size_t>(h)] = pos;
                ptrdiff_t pat_len = std::ssize(pattern);
                ptrdiff_t actual_offset = pos - (std::max(hunk.old_start, ptrdiff_t{1}) - 1);
                auto ctx = get_hunk_context(hunk);

                // Determine fuzz level used for this hunk
                int fuzz_used = 0;
                if (opts.fuzz > 0) {
                    for (int f = 0; f <= opts.fuzz; ++f) {
                        if (try_match(fc.lines, pos, pattern, f, ctx.prefix, ctx.suffix)) {
                            fuzz_used = f;
                            break;
                        }
                    }
                }

                // Record fuzz amounts for build_output trimming
                hunk_fuzz[checked_cast<size_t>(h)] = {
                    std::min(static_cast<ptrdiff_t>(fuzz_used), ctx.prefix),
                    std::min(static_cast<ptrdiff_t>(fuzz_used), ctx.suffix)
                };
                auto &fz = hunk_fuzz[checked_cast<size_t>(h)];

                if (actual_offset != cumulative_offset && !opts.quiet) {
                    if (fuzz_used > 0) {
                        result.out += std::format(
                            "Hunk #{} succeeded at {} with fuzz {} (offset {} lines).\n",
                            h + 1, pos + 1, fuzz_used, actual_offset - cumulative_offset);
                    } else {
                        result.out += std::format(
                            "Hunk #{} succeeded at {} (offset {} lines).\n",
                            h + 1, pos + 1, actual_offset - cumulative_offset);
                    }
                } else if (fuzz_used > 0 && !opts.quiet) {
                    result.out += std::format(
                        "Hunk #{} succeeded at {} with fuzz {}.\n",
                        h + 1, pos + 1, fuzz_used);
                }

                // Update offset and frozen line (adjusted for fuzz)
                cumulative_offset = actual_offset;
                last_frozen_line = pos + pat_len - fz.suffix;
            } else {
                // Hunk failed
                rejected[checked_cast<size_t>(h)] = true;
                file_has_rejects = true;
                if (!opts.quiet) {
                    if (opts.merge) {
                        result.err += std::format("Hunk #{} NOT MERGED at {}.\n",
                                                  h + 1, hunk.old_start);
                    } else {
                        result.err += std::format("Hunk #{} FAILED at {}.\n",
                                                  h + 1, hunk.old_start);
                    }
                }
            }
        }

        if (file_has_rejects) {
            had_rejects = true;
            result.exit_code = 1;
        }

        // Apply changes
        if (!opts.dry_run) {
            bool any_applied = false;
            for (ptrdiff_t h = 0; h < std::ssize(pf.hunks); ++h) {
                if (hunk_positions[checked_cast<size_t>(h)] >= 0) { any_applied = true; break; }
            }

            if (any_applied || pf.is_creation || (opts.merge && file_has_rejects)) {
                std::string new_content;

                if (opts.merge && file_has_rejects) {
                    new_content = build_merge_output(fc.lines, fc.has_trailing_newline,
                                                      pf, hunk_positions, hunk_fuzz,
                                                      opts.merge_style);
                } else {
                    new_content = build_output(fc.lines, fc.has_trailing_newline,
                                               pf, hunk_positions, hunk_fuzz);
                }

                // Restore \r\n line endings if the original file used them
                if (fc.crlf) {
                    std::string crlf_content;
                    crlf_content.reserve(new_content.size() + new_content.size() / 40);
                    for (size_t k = 0; k < new_content.size(); ++k) {
                        if (new_content[k] == '\n' &&
                            (k == 0 || new_content[k - 1] != '\r')) {
                            crlf_content += '\r';
                        }
                        crlf_content += new_content[k];
                    }
                    new_content = std::move(crlf_content);
                }

                // Create parent directories if needed
                if (!opts.fs) {
                    std::string dir = dirname(pf.target_path);
                    if (!dir.empty() && dir != "." && !is_directory(dir)) {
                        make_dirs(dir);
                    }
                }

                // Check if we should remove the file (-E flag)
                if (opts.remove_empty && new_content.empty() && !pf.is_creation) {
                    if (file_existed) {
                        fs_delete(pf.target_path);
                    }
                } else {
                    fs_write(pf.target_path, new_content);
                }
            }

            // Write reject file if needed (and not in merge mode)
            if (file_has_rejects && !opts.merge) {
                std::string rej_content = format_rejects(pf, rejected);
                if (!rej_content.empty()) {
                    fs_write(pf.target_path + ".rej", rej_content);
                }
                if (!opts.quiet) {
                    ptrdiff_t rej_count = 0;
                    for (bool r : rejected) if (r) ++rej_count;
                    result.err += std::format(
                        "{} out of {} {} FAILED -- saving rejects to file {}.rej\n",
                        rej_count, std::ssize(pf.hunks),
                        std::ssize(pf.hunks) == 1 ? "hunk" : "hunks",
                        pf.target_path);
                }
            }
        }
    }

    if (had_rejects) {
        result.exit_code = 1;
    }

    return result;
}

// === src/cmd_stack.cpp ===

// This is free and unencumbered software released into the public domain.
#include <cstdlib>


static void write_applied_patches(QuiltState &q) {
    std::string path = path_join(q.work_dir, q.pc_dir, "applied-patches");
    write_applied(path, q.applied);
}

// Parse affected files from a unified diff, stripping path components
// to match what `patch -pN` would do.
static std::vector<std::string> parse_patch_files(std::string_view content, int strip = 1) {
    std::vector<std::string> files;
    auto lines = split_lines(content);
    for (const auto &line : lines) {
        if (!line.starts_with("+++ ")) continue;
        std::string_view rest = std::string_view(line).substr(4);
        // Skip /dev/null
        if (rest.starts_with("/dev/null")) continue;
        // Strip trailing tab and timestamp (e.g., "\t2024-01-01 ...")
        ptrdiff_t tab = str_find(rest, '\t');
        if (tab >= 0) {
            rest = rest.substr(0, checked_cast<size_t>(tab));
        }
        std::string f = trim(rest);
        // Strip N leading path components (like patch -pN)
        for (int i = 0; i < strip && !f.empty(); ++i) {
            ptrdiff_t slash = str_find(std::string_view(f), '/');
            if (slash >= 0) {
                f = f.substr(checked_cast<size_t>(slash) + 1);
            }
        }
        if (!f.empty()) {
            files.push_back(std::move(f));
        }
    }
    return files;
}

int cmd_series(QuiltState &q, int argc, char **argv) {
    bool verbose = false;
    // color: 0=never, 1=auto, 2=always
    int color_mode = 0;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-v") {
            verbose = true;
        } else if (arg == "--color") {
            color_mode = 1;  // auto
        } else if (arg.starts_with("--color=")) {
            auto val = arg.substr(8);
            if (val == "always") color_mode = 2;
            else if (val == "auto") color_mode = 1;
            else if (val == "never") color_mode = 0;
            else {
                err("Invalid --color value: "); err_line(val);
                return 1;
            }
        } else if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
    }

    bool use_color = (color_mode == 2) || (color_mode == 1 && stdout_is_tty());

    if (q.series.empty()) {
        if (q.series_file_exists) {
            // Empty series file: nothing to print, success
            return 0;
        } else {
            err_line("No series file found");
            return 1;
        }
    }

    for (const auto &patch : q.series) {
        if (verbose) {
            if (!q.applied.empty() && patch == q.applied.back()) {
                out("= ");
            } else if (q.is_applied(patch)) {
                out("+ ");
            } else {
                out("  ");
            }
        }
        std::string name = format_patch(q, patch);
        if (use_color) {
            if (!q.applied.empty() && patch == q.applied.back()) {
                out("\033[33m");  // yellow for top
            } else if (q.is_applied(patch)) {
                out("\033[32m");  // green for applied
            } else {
                out("\033[00m");  // default for unapplied
            }
            out(name);
            out_line("\033[00m");
        } else {
            out_line(name);
        }
    }
    return 0;
}

int cmd_applied(QuiltState &q, int argc, char **argv) {
    std::string_view target;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
        target = strip_patches_prefix(q, arg);
    }

    if (!target.empty()) {
        // Print all applied patches up to and including target
        auto idx = q.find_in_series(target);
        if (!idx.has_value()) {
            err("Patch "); err(format_patch(q, target)); err_line(" is not in series");
            return 1;
        }
        if (!q.is_applied(target)) {
            err("Patch "); err(format_patch(q, target)); err_line(" is not applied");
            return 1;
        }
        for (const auto &a : q.applied) {
            out_line(format_patch(q, a));
            if (a == target) break;
        }
        return 0;
    }

    if (q.series.empty()) {
        if (q.series_file_exists) {
            err_line("No patches in series");
        } else {
            err_line("No series file found");
        }
        return 1;
    }
    if (q.applied.empty()) {
        err_line("No patches applied");
        return 1;
    }

    for (const auto &a : q.applied) {
        out_line(format_patch(q, a));
    }
    return 0;
}

int cmd_unapplied(QuiltState &q, int argc, char **argv) {
    std::string_view target;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
        target = strip_patches_prefix(q, arg);
    }

    if (q.series.empty()) {
        if (q.series_file_exists) {
            err_line("No patches in series");
        } else {
            err_line("No series file found");
        }
        return 1;
    }

    ptrdiff_t start_idx;
    if (!target.empty()) {
        auto idx = q.find_in_series(target);
        if (!idx.has_value()) {
            err("Patch "); err(format_patch(q, target)); err_line(" is not in series");
            return 1;
        }
        start_idx = idx.value() + 1;
    } else {
        ptrdiff_t top = q.top_index();
        start_idx = top + 1;
    }

    if (start_idx >= std::ssize(q.series)) {
        // With an explicit target patch, having no patches after it is not
        // an error — just print nothing.
        if (!target.empty()) {
            return 0;
        }
        std::string_view top_name = q.applied.empty() ? std::string_view("??") : std::string_view(q.applied.back());
        err("File series fully applied, ends at patch "); err_line(format_patch(q, top_name));
        return 1;
    }

    for (ptrdiff_t i = start_idx; i < std::ssize(q.series); ++i) {
        out_line(format_patch(q, q.series[checked_cast<size_t>(i)]));
    }
    return 0;
}

int cmd_top(QuiltState &q, int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
    }
    if (q.series.empty()) {
        if (q.series_file_exists) {
            err_line("No patches in series");
            return 2;
        } else {
            err_line("No series file found");
            return 1;
        }
    }
    if (q.applied.empty()) {
        err_line("No patches applied");
        return 2;
    }
    out_line(format_patch(q, q.applied.back()));
    return 0;
}

int cmd_next(QuiltState &q, int argc, char **argv) {
    std::string_view target;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
        target = strip_patches_prefix(q, arg);
    }

    if (q.series.empty()) {
        if (q.series_file_exists) {
            err_line("No patches in series");
            return 2;
        } else {
            err_line("No series file found");
            return 1;
        }
    }

    ptrdiff_t after_idx;
    if (!target.empty()) {
        auto idx = q.find_in_series(target);
        if (!idx.has_value()) {
            err("Patch "); err(format_patch(q, target)); err_line(" is not in series");
            return 2;
        }
        // Original quilt: if the named patch is applied, error
        if (q.is_applied(target)) {
            err("Patch "); err(format_patch(q, target)); err_line(" is currently applied");
            return 2;
        }
        // If unapplied, return the patch itself (it's the "next" to be pushed)
        out_line(format_patch(q, target));
        return 0;
    } else {
        ptrdiff_t top = q.top_index();
        after_idx = top + 1;
    }

    if (after_idx >= std::ssize(q.series)) {
        std::string_view top_name = q.applied.empty() ? std::string_view("??") : std::string_view(q.applied.back());
        err("File series fully applied, ends at patch "); err_line(format_patch(q, top_name));
        return 2;
    }

    out_line(format_patch(q, q.series[checked_cast<size_t>(after_idx)]));
    return 0;
}

int cmd_previous(QuiltState &q, int argc, char **argv) {
    std::string_view target;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
        target = strip_patches_prefix(q, arg);
    }

    if (!target.empty()) {
        auto idx = q.find_in_series(target);
        if (!idx.has_value()) {
            err("Patch "); err(format_patch(q, target)); err_line(" is not in series");
            return 2;
        }
        if (idx.value() == 0) {
            return 2;
        }
        out_line(format_patch(q, q.series[checked_cast<size_t>(idx.value() - 1)]));
        return 0;
    }

    if (q.series.empty()) {
        if (q.series_file_exists) {
            err_line("No patches in series");
            return 2;
        } else {
            err_line("No series file found");
            return 1;
        }
    }

    if (q.applied.empty()) {
        err_line("No patches applied");
        return 1;
    }

    if (std::ssize(q.applied) == 1) {
        return 2;
    }

    out_line(format_patch(q, q.applied[checked_cast<size_t>(std::ssize(q.applied) - 2)]));
    return 0;
}

int cmd_push(QuiltState &q, int argc, char **argv) {
    bool push_all = false;
    bool force = false;
    bool quiet = false;
    bool verbose = false;
    int fuzz = -1;
    bool merge = false;
    std::string merge_style;
    bool leave_rejects = false;
    bool do_refresh = false;
    int push_count = -1;
    std::string_view target;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-a") { push_all = true; }
        else if (arg == "-f") { force = true; }
        else if (arg == "-q" || arg == "--quiet") { quiet = true; }
        else if (arg == "-v" || arg == "--verbose") { verbose = true; }
        else if (arg.starts_with("--fuzz=")) { fuzz = checked_cast<int>(parse_int(arg.substr(7))); }
        else if (arg == "-m" || arg == "--merge") { merge = true; }
        else if (arg.starts_with("--merge=")) { merge = true; merge_style = std::string(arg.substr(8)); }
        else if (arg == "--leave-rejects") { leave_rejects = true; }
        else if (arg == "--refresh") { do_refresh = true; }
        else if (arg == "--color" || arg.starts_with("--color=")) {
            if (arg.starts_with("--color=")) {
                auto val = arg.substr(8);
                if (val != "always" && val != "auto" && val != "never") {
                    err("Invalid --color value: "); err_line(val);
                    return 1;
                }
            }
        }
        else if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
        else {
            // Try as number first
            int val = 0;
            auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), val);
            if (ec == std::errc{} && ptr == arg.data() + arg.size() && val > 0) {
                push_count = val;
            } else {
                target = strip_patches_prefix(q, arg);
            }
        }
    }

    // Refuse to push if top patch needs refresh (was force-applied)
    if (!q.applied.empty()) {
        std::string nr = path_join(pc_patch_dir(q, q.applied.back()), ".needs_refresh");
        if (file_exists(nr)) {
            err_line("The topmost patch " + patch_path_display(q, q.applied.back()) +
                     " needs to be refreshed first.");
            return 1;
        }
    }

    ptrdiff_t top = q.top_index();
    ptrdiff_t start_idx = top + 1;

    if (q.series.empty()) {
        if (q.series_file_exists) {
            err_line("No patches in series");
        } else {
            err_line("No series file found");
        }
        return 2;
    }

    if (start_idx >= std::ssize(q.series)) {
        err_line("File series fully applied, ends at patch " +
                 patch_path_display(q, q.applied.back()));
        return 2;
    }

    ptrdiff_t end_idx;  // inclusive
    if (push_all) {
        end_idx = std::ssize(q.series) - 1;
    } else if (!target.empty()) {
        auto idx = q.find_in_series(target);
        if (!idx.has_value()) {
            err("Patch "); err(format_patch(q, target)); err_line(" is not in series");
            return 1;
        }
        end_idx = idx.value();
        if (end_idx < start_idx) {
            err("Patch "); err(format_patch(q, target)); err_line(" is currently applied");
            return 2;
        }
    } else if (push_count > 0) {
        end_idx = start_idx + push_count - 1;
        if (end_idx >= std::ssize(q.series)) {
            end_idx = std::ssize(q.series) - 1;
        }
    } else {
        end_idx = start_idx;
    }

    if (!ensure_pc_dir(q)) return 1;

    // Read QUILT_PATCH_OPTS
    auto extra_patch_opts = shell_split(get_env("QUILT_PATCH_OPTS"));

    std::string last_applied;
    for (ptrdiff_t i = start_idx; i <= end_idx; ++i) {
        const std::string &name = q.series[checked_cast<size_t>(i)];
        std::string display = patch_path_display(q, name);

        if (i > start_idx) {
            out_line("");
        }
        out_line("Applying patch " + display);

        // Read patch file
        std::string patch_path = path_join(q.work_dir, q.patches_dir, name);
        std::string patch_content = read_file(patch_path);
        if (patch_content.empty() && !file_exists(patch_path)) {
            err_line("Patch " + display + " does not exist");
            return 1;
        }

        // Parse affected files and back them up
        int strip_level = q.get_strip_level(name);
        auto affected = parse_patch_files(patch_content, strip_level);
        std::string pc_dir = pc_patch_dir(q, name);
        if (!is_directory(pc_dir)) {
            make_dirs(pc_dir);
        }

        for (const auto &file : affected) {
            backup_file(q, name, file);
        }

        // Apply the patch using built-in patch engine
        PatchOptions patch_opts;
        patch_opts.strip_level = strip_level;
        patch_opts.remove_empty = true;
        patch_opts.force = force;
        if (q.patch_reversed.contains(name)) patch_opts.reverse = true;
        if (fuzz >= 0) patch_opts.fuzz = fuzz;
        if (merge) {
            patch_opts.merge = true;
            patch_opts.merge_style = merge_style;
        }
        // Parse QUILT_PATCH_OPTS for additional options
        for (const auto &opt : extra_patch_opts) {
            std::string_view o = opt;
            if (o == "-R") patch_opts.reverse = true;
            else if (o == "-f" || o == "--force") patch_opts.force = true;
            else if (o == "-s") patch_opts.quiet = true;
            else if (o == "-E") patch_opts.remove_empty = true;
            else if (o.starts_with("--fuzz=")) {
                patch_opts.fuzz = checked_cast<int>(parse_int(o.substr(7)));
            }
        }

        // Print verbose file list ourselves instead of relying on
        // patch --verbose, which is not available on busybox.
        if (verbose && !quiet) {
            for (const auto &file : affected) {
                out_line("patching file " + file);
            }
        }

        // Suppress builtin_patch's own "patching file" messages when we do verbose ourselves
        if (verbose) patch_opts.quiet = true;

        PatchResult result = builtin_patch(patch_content, patch_opts);

        if (!quiet && !verbose && !result.out.empty()) {
            out(result.out);
        }

        if (result.exit_code != 0) {
            if (!result.err.empty()) {
                err(result.err);
            }
            if (force) {
                // Force-applied: record as applied but mark as needing refresh
                q.applied.push_back(name);
                write_applied_patches(q);
                write_file(path_join(pc_dir, ".timestamp"), "");
                write_file(path_join(pc_dir, ".needs_refresh"), "");
                out_line("Applied patch " + display + " (forced; needs refresh)");
                return 1;
            } else {
                // Not forced: restore files from backups and clean up
                for (const auto &file : affected) {
                    restore_file(q, name, file);
                }
                err_line("Patch " + display + " does not apply (enforce with -f)");
                if (!leave_rejects) {
                    for (const auto &file : affected) {
                        std::string rej = path_join(q.work_dir, file + ".rej");
                        if (file_exists(rej)) {
                            delete_file(rej);
                        }
                    }
                }
                delete_dir_recursive(pc_dir);
                return 1;
            }
        }

        // Record as applied
        q.applied.push_back(name);
        write_applied_patches(q);

        // Create .timestamp
        write_file(path_join(pc_dir, ".timestamp"), "");

        if (do_refresh) {
            char arg0[] = "refresh";
            char *refresh_argv[] = {arg0, nullptr};
            int rr = cmd_refresh(q, 1, refresh_argv);
            if (rr != 0) return rr;
        }

        last_applied = name;
    }

    if (!last_applied.empty()) {
        if (!quiet) out_line("");
        out_line("Now at patch " + patch_path_display(q, last_applied));
    }
    return 0;
}

int cmd_pop(QuiltState &q, int argc, char **argv) {
    bool pop_all = false;
    bool force = false;
    bool quiet = false;
    [[maybe_unused]] bool verbose = false;  // accepted for compat, pop is verbose by default
    bool auto_refresh = false;
    int pop_count = -1;
    std::string_view target;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-a") { pop_all = true; }
        else if (arg == "-f") { force = true; }
        else if (arg == "-q" || arg == "--quiet") { quiet = true; }
        else if (arg == "-v" || arg == "--verbose") { verbose = true; }
        else if (arg == "-R") { /* accepted for compat, always verified now */ }
        else if (arg == "--refresh") { auto_refresh = true; }
        else if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
        else {
            // Try as number first
            int val = 0;
            auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), val);
            if (ec == std::errc{} && ptr == arg.data() + arg.size() && val > 0) {
                pop_count = val;
            } else {
                target = strip_patches_prefix(q, arg);
            }
        }
    }

    if (q.applied.empty()) {
        if (!q.series_file_exists) {
            err_line("No series file found");
            return 1;
        }
        err_line("No patch removed");
        return 2;
    }

    ptrdiff_t stop_idx;  // index in applied to stop BEFORE (exclusive); pop down to this
    if (pop_all) {
        stop_idx = 0;
    } else if (!target.empty()) {
        // Find target in applied list
        ptrdiff_t found_idx = -1;
        for (ptrdiff_t i = 0; i < std::ssize(q.applied); ++i) {
            if (q.applied[checked_cast<size_t>(i)] == target) {
                found_idx = i;
                break;
            }
        }
        if (found_idx < 0) {
            err("Patch "); err(format_patch(q, target)); err_line(" is not applied");
            return 1;
        }
        // Pop down to (but not including) the target patch
        stop_idx = found_idx + 1;
        if (stop_idx >= std::ssize(q.applied)) {
            err_line("No patch removed");
            return 2;
        }
    } else if (pop_count > 0) {
        stop_idx = std::ssize(q.applied) - pop_count;
        if (stop_idx < 0) stop_idx = 0;
    } else {
        // Pop just the top patch
        stop_idx = std::ssize(q.applied) - 1;
    }

    // Pop from the top down to stop_idx
    bool first_pop = true;
    while (std::ssize(q.applied) > stop_idx) {
        const std::string &name = q.applied.back();
        std::string display = patch_path_display(q, name);

        // Auto-refresh before popping if requested
        if (auto_refresh) {
            auto extra = shell_split(get_env("QUILT_REFRESH_ARGS"));
            std::vector<std::string> r_storage;
            r_storage.push_back("refresh");
            for (auto &e : extra) r_storage.push_back(e);
            std::vector<char *> r_argv;
            for (auto &s : r_storage) r_argv.push_back(s.data());
            int rr = cmd_refresh(q, checked_cast<int>(std::ssize(r_argv)), r_argv.data());
            if (rr != 0) {
                err_line("Refresh of patch " + display + " failed, aborting pop");
                return 1;
            }
        }

        // Check if patch needs refresh (force-applied) and -f not given
        std::string pc_dir = pc_patch_dir(q, name);
        std::string nr = path_join(pc_dir, ".needs_refresh");
        if (file_exists(nr) && !force && !auto_refresh) {
            err_line("Patch " + display + " needs to be refreshed first.");
            return 1;
        }

        // Check if patch removes cleanly (detects dirty/unrefreshed changes)
        if (!force) {
            std::string patch_path = path_join(q.work_dir, q.patches_dir, name);
            std::string patch_content = read_file(patch_path);
            if (!patch_content.empty()) {
                int strip_level = q.get_strip_level(name);
                PatchOptions verify_opts;
                verify_opts.strip_level = strip_level;
                verify_opts.reverse = true;
                verify_opts.dry_run = true;
                verify_opts.force = true;
                verify_opts.quiet = true;
                PatchResult vr = builtin_patch(patch_content, verify_opts);
                if (vr.exit_code != 0) {
                    err_line("Patch " + display +
                             " does not remove cleanly (refresh it or enforce with -f)");
                    err_line("Hint: `quilt diff -z' will show the pending changes.");
                    return 1;
                }
            }
        }

        if (!first_pop) {
            out_line("");
        }
        out_line("Removing patch " + display);
        first_pop = false;

        // Restore backed-up files
        auto files = files_in_patch(q, name);
        for (const auto &file : files) {
            restore_file(q, name, file);
            if (!quiet) {
                // Show what happened to each file: "Removing" if the file
                // was deleted (created by the patch), "Restoring" otherwise.
                if (!file_exists(path_join(q.work_dir, file))) {
                    out_line("Removing " + file);
                } else {
                    out_line("Restoring " + file);
                }
            }
        }

        // Remove the backup directory
        delete_dir_recursive(pc_dir);

        // Remove from applied list
        q.applied.pop_back();
        write_applied_patches(q);
    }

    if (!quiet) out_line("");
    if (q.applied.empty()) {
        out_line("No patches applied");
    } else {
        out_line("Now at patch " + patch_path_display(q, q.applied.back()));
    }
    return 0;
}

// === src/cmd_annotate.cpp ===

// This is free and unencumbered software released into the public domain.

#include <optional>

namespace {

struct AnnotateOptions {
    std::string patch;
    std::string file;
};

static bool path_has_content(std::string_view path)
{
    return file_exists(path) && !read_file(path).empty();
}

static std::vector<std::string> read_lines(std::string_view path)
{
    if (!path_has_content(path)) {
        return {};
    }
    return split_lines(read_file(path));
}

static std::string next_patch_for_file(const QuiltState &q,
                                std::string_view patch,
                                std::string_view file)
{
    bool after_target = false;
    for (const auto &applied : q.applied) {
        if (after_target) {
            auto tracked = files_in_patch(q, applied);
            if (std::ranges::find(tracked, file) != tracked.end()) {
                return applied;
            }
        }
        if (applied == patch) {
            after_target = true;
        }
    }
    return "";
}

static std::vector<std::string> reannotate_lines(std::span<const std::string> old_lines,
                                          std::span<const std::string> old_annotations,
                                          std::span<const std::string> new_lines,
                                          std::string_view annotation)
{
    const ptrdiff_t m = std::ssize(old_lines);
    const ptrdiff_t n = std::ssize(new_lines);
    std::vector<std::vector<int>> dp(checked_cast<size_t>(m + 1), std::vector<int>(checked_cast<size_t>(n + 1), 0));

    for (ptrdiff_t i = m; i-- > 0;) {
        for (ptrdiff_t j = n; j-- > 0;) {
            if (old_lines[checked_cast<size_t>(i)] == new_lines[checked_cast<size_t>(j)]) {
                dp[checked_cast<size_t>(i)][checked_cast<size_t>(j)] = dp[checked_cast<size_t>(i + 1)][checked_cast<size_t>(j + 1)] + 1;
            } else {
                dp[checked_cast<size_t>(i)][checked_cast<size_t>(j)] = std::max(dp[checked_cast<size_t>(i + 1)][checked_cast<size_t>(j)], dp[checked_cast<size_t>(i)][checked_cast<size_t>(j + 1)]);
            }
        }
    }

    std::vector<std::string> result;
    result.reserve(checked_cast<size_t>(n));
    ptrdiff_t i = 0;
    ptrdiff_t j = 0;
    while (i < m || j < n) {
        if (i < m && j < n && old_lines[checked_cast<size_t>(i)] == new_lines[checked_cast<size_t>(j)]) {
            result.push_back(i < std::ssize(old_annotations) ? old_annotations[checked_cast<size_t>(i)] : "");
            ++i;
            ++j;
        } else if (j < n && (i == m || dp[checked_cast<size_t>(i)][checked_cast<size_t>(j + 1)] >= dp[checked_cast<size_t>(i + 1)][checked_cast<size_t>(j)])) {
            result.emplace_back(annotation);
            ++j;
        } else {
            ++i;
        }
    }
    return result;
}

std::optional<AnnotateOptions> parse_options(const QuiltState &q, int argc, char **argv)
{
    AnnotateOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-P" && i + 1 < argc) {
            opts.patch = strip_patches_prefix(q, argv[i + 1]);
            ++i;
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            return std::nullopt;
        }
        if (!opts.file.empty()) {
            return std::nullopt;
        }
        opts.file = subdir_path(q, arg);
    }

    if (opts.file.empty()) {
        return std::nullopt;
    }
    return opts;
}

int no_applied_patches_error(const QuiltState &q)
{
    if (!q.series_file_exists) {
        err_line("No series file found");
    } else if (q.series.empty()) {
        err_line("No patches in series");
    } else {
        err_line("No patches applied");
    }
    return 1;
}

} // namespace

int cmd_annotate(QuiltState &q, int argc, char **argv)
{
    auto opts = parse_options(q, argc, argv);
    if (!opts.has_value()) {
        err_line("Usage: quilt annotate [-P patch] file");
        return 1;
    }

    if (q.applied.empty()) {
        return no_applied_patches_error(q);
    }

    std::string stop_patch = opts->patch.empty() ? q.applied.back() : opts->patch;
    if (!q.find_in_series(stop_patch).has_value()) {
        err_line("Patch " + stop_patch + " is not in series");
        return 1;
    }
    if (!q.is_applied(stop_patch)) {
        err_line("Patch " + stop_patch + " is not applied");
        return 1;
    }

    std::vector<std::string> patches;
    std::vector<std::string> files;
    std::string next_patch;

    for (const auto &patch : q.applied) {
        std::string old_file = path_join(pc_patch_dir(q, patch), opts->file);
        if (file_exists(old_file)) {
            patches.push_back(patch);
            files.push_back(old_file);
        }
        if (patch == stop_patch) {
            next_patch = next_patch_for_file(q, stop_patch, opts->file);
            break;
        }
    }

    if (next_patch.empty()) {
        files.push_back(path_join(q.work_dir, opts->file));
    } else {
        files.push_back(path_join(pc_patch_dir(q, next_patch), opts->file));
    }

    if (patches.empty()) {
        std::string target = files.back();
        if (!file_exists(target)) {
            err_line("File " + opts->file + " does not exist");
            return 1;
        }
        for (const auto &line : read_lines(target)) {
            out("\t" + line + "\n");
        }
        return 0;
    }

    std::vector<std::string> annotations(checked_cast<size_t>(std::ssize(read_lines(files.front()))), "");
    for (ptrdiff_t i = 0; i < std::ssize(patches); ++i) {
        annotations = reannotate_lines(read_lines(files[checked_cast<size_t>(i)]), annotations,
                                       read_lines(files[checked_cast<size_t>(i + 1)]),
                                       std::to_string(i + 1));
    }

    auto final_lines = read_lines(files.back());
    for (ptrdiff_t i = 0; i < std::ssize(annotations); ++i) {
        std::string line = i < std::ssize(final_lines) ? final_lines[checked_cast<size_t>(i)] : "";
        out(annotations[checked_cast<size_t>(i)] + "\t" + line + "\n");
    }

    out("\n");
    for (ptrdiff_t i = 0; i < std::ssize(patches); ++i) {
        out(std::to_string(i + 1) + "\t" + format_patch(q, patches[checked_cast<size_t>(i)]) + "\n");
    }
    return 0;
}

// === src/cmd_patch.cpp ===

// This is free and unencumbered software released into the public domain.

#include <cstring>
#include <cstdlib>
#include <set>

static std::string read_patch_header(std::string_view patch_path) {
    std::string content = read_file(patch_path);
    if (content.empty()) return "";

    std::string header;
    auto lines = split_lines(content);
    for (const auto &line : lines) {
        if (line.starts_with("Index:") ||
            line.starts_with("---") ||
            line.starts_with("diff ")) {
            break;
        }
        header += line;
        header += '\n';
    }
    return header;
}

int cmd_init(QuiltState &q, int argc, char **) {
    if (argc != 1) {
        err_line("Usage: quilt init");
        return 1;
    }

    q.work_dir = get_cwd();
    q.pc_dir = ".pc";
    q.patches_dir = "patches";
    std::string env_pc = get_env("QUILT_PC");
    if (!env_pc.empty()) {
        q.pc_dir = env_pc;
    }
    std::string env_patches = get_env("QUILT_PATCHES");
    if (!env_patches.empty()) {
        q.patches_dir = env_patches;
    }
    std::string series_name = get_env("QUILT_SERIES");
    if (series_name.empty()) {
        series_name = "series";
    }
    q.series_file = path_join(q.patches_dir, series_name);

    if (!ensure_pc_dir(q)) {
        return 1;
    }

    std::string patches_abs = path_join(q.work_dir, q.patches_dir);
    if (!is_directory(patches_abs)) {
        if (!make_dirs(patches_abs)) {
            err_line("Failed to create " + patches_abs);
            return 1;
        }
    }

    std::string series_abs = path_join(q.work_dir, q.series_file);
    if (!file_exists(series_abs)) {
        if (!write_series(series_abs, {}, {}, {})) {
            err_line("Failed to write series file.");
            return 1;
        }
    }

    std::string applied_abs = path_join(q.work_dir, q.pc_dir, "applied-patches");
    if (!file_exists(applied_abs)) {
        if (!write_applied(applied_abs, {})) {
            err_line("Failed to write applied-patches.");
            return 1;
        }
    }

    out_line("The quilt meta-data is now initialized.");
    return 0;
}

int cmd_new(QuiltState &q, int argc, char **argv) {
    // Parse options
    std::string patch_name;
    std::string p_value;
    int i = 1;  // skip argv[0] which is "new"
    while (i < argc) {
        std::string_view arg = argv[i];
        if (arg == "-p" && i + 1 < argc) {
            p_value = argv[i + 1];
            i += 2;
            continue;
        }
        if (arg.starts_with("-p") && std::ssize(arg) > 2) {
            p_value = std::string(arg.substr(2));
            i += 1;
            continue;
        }
        // First non-option argument is the patch name
        if (arg[0] != '-') {
            patch_name = std::string(arg);
            i += 1;
            break;
        }
        err("Unrecognized option: "); err_line(arg);
        return 1;
    }

    if (patch_name.empty()) {
        err_line("Usage: quilt new [-p n] patchname");
        return 1;
    }

    // Verify patch doesn't already exist in series
    if (q.find_in_series(patch_name).has_value()) {
        err("Patch "); err(patch_name); err_line(" already exists in series.");
        return 1;
    }

    // Ensure .pc/ directory exists
    if (!ensure_pc_dir(q)) return 1;

    // Ensure patches/ directory exists
    std::string patches_abs = path_join(q.work_dir, q.patches_dir);
    if (!is_directory(patches_abs)) {
        if (!make_dirs(patches_abs)) {
            err_line("Failed to create " + patches_abs);
            return 1;
        }
    }

    // Insert patch name into series (after current top, or at beginning)
    ptrdiff_t top_idx = q.top_index();
    if (!q.applied.empty() && top_idx < 0) {
        err_line("The series file no longer matches the applied patches. Please run 'quilt pop -a'.");
        return 1;
    }
    if (top_idx < 0) {
        // No applied patches — insert at beginning
        q.series.insert(q.series.begin(), patch_name);
    } else {
        // Insert after the current top
        q.series.insert(q.series.begin() + top_idx + 1, patch_name);
    }

    // Validate strip level
    if (!p_value.empty() && p_value != "0" && p_value != "1") {
        err_line("Cannot create patches with -p" + p_value +
                 ", please specify -p0 or -p1 instead");
        return 1;
    }

    // Store strip level
    if (!p_value.empty() && p_value != "1") {
        q.patch_strip_level[patch_name] = checked_cast<int>(parse_int(p_value));
    }

    // Write series file
    std::string series_abs = path_join(q.work_dir, q.series_file);
    if (!write_series(series_abs, q.series, q.patch_strip_level, q.patch_reversed)) {
        err_line("Failed to write series file.");
        return 1;
    }

    // Add to applied list and write applied-patches
    q.applied.push_back(patch_name);
    std::string applied_abs = path_join(q.work_dir, q.pc_dir, "applied-patches");
    if (!write_applied(applied_abs, q.applied)) {
        err_line("Failed to write applied-patches.");
        return 1;
    }

    // Create .pc/<patchname>/ directory
    std::string pc_dir = pc_patch_dir(q, patch_name);
    if (!is_directory(pc_dir)) {
        if (!make_dirs(pc_dir)) {
            err_line("Failed to create " + pc_dir);
            return 1;
        }
    }

    out_line("Patch " + patch_path_display(q, patch_name) + " is now on top");
    return 0;
}

int cmd_add(QuiltState &q, int argc, char **argv) {
    if (q.applied.empty()) {
        if (!q.series_file_exists) {
            err_line("No series file found");
            return 1;
        }
        err_line("No patches applied");
        return 1;
    }

    // Parse options
    std::string_view patch = q.applied.back();
    std::vector<std::string> files;
    int i = 1;
    while (i < argc) {
        std::string_view arg = argv[i];
        if (arg == "-P" && i + 1 < argc) {
            patch = strip_patches_prefix(q, argv[i + 1]);
            i += 2;
            continue;
        }
        if (arg[0] != '-') {
            files.push_back(subdir_path(q, arg));
        } else {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
        i += 1;
    }

    if (files.empty()) {
        err_line("Usage: quilt add [-P patch] file ...");
        return 1;
    }

    if (!q.is_applied(patch)) {
        err("Patch "); err(format_patch(q, patch)); err_line(" is not applied");
        return 1;
    }

    for (const auto &file : files) {
        // Check if file is already tracked by this patch
        std::string backup_path = path_join(pc_patch_dir(q, patch), file);
        if (file_exists(backup_path)) {
            err("File "); err(file); err(" is already in patch ");
            err_line(patch_path_display(q, patch));
            return 2;
        }

        // Check if file is modified by any patch applied after this one
        bool found_patch = false;
        for (const auto &ap : q.applied) {
            if (!found_patch) {
                if (ap == patch) found_patch = true;
                continue;
            }
            std::string later_backup = path_join(pc_patch_dir(q, ap), file);
            if (file_exists(later_backup)) {
                err("File "); err(file); err(" modified by patch ");
                err_line(patch_path_display(q, ap));
                return 1;
            }
        }

        // Backup the file
        if (!backup_file(q, patch, file)) {
            err("Failed to back up "); err_line(file);
            return 1;
        }

        out("File "); out(file); out(" added to patch ");
        out_line(patch_path_display(q, patch));
    }

    return 0;
}

int cmd_remove(QuiltState &q, int argc, char **argv) {
    if (q.applied.empty()) {
        err_line("No patches applied");
        return 1;
    }

    // Parse options
    std::string_view patch = q.applied.back();
    std::vector<std::string> files;
    int i = 1;
    while (i < argc) {
        std::string_view arg = argv[i];
        if (arg == "-P" && i + 1 < argc) {
            patch = strip_patches_prefix(q, argv[i + 1]);
            i += 2;
            continue;
        }
        if (arg[0] != '-') {
            files.push_back(subdir_path(q, arg));
        } else {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
        i += 1;
    }

    if (files.empty()) {
        err_line("Usage: quilt remove [-P patch] file ...");
        return 1;
    }

    if (!q.is_applied(patch)) {
        err("Patch "); err(format_patch(q, patch)); err_line(" is not applied");
        return 1;
    }

    for (const auto &file : files) {
        // Check if file is tracked by this patch
        std::string backup_path = path_join(pc_patch_dir(q, patch), file);
        if (!file_exists(backup_path)) {
            err("File "); err(file); err(" is not in patch ");
            err_line(patch_path_display(q, patch));
            return 1;
        }

        // Restore file from backup
        if (!restore_file(q, patch, file)) {
            err("Failed to restore "); err_line(file);
            return 1;
        }

        // Remove backup file
        delete_file(backup_path);

        out("File "); out(file); out(" removed from patch ");
        out_line(patch_path_display(q, patch));
    }

    return 0;
}

int cmd_edit(QuiltState &q, int argc, char **argv) {
    if (q.applied.empty()) {
        err_line("No patches applied");
        return 1;
    }

    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
        files.push_back(subdir_path(q, arg));
    }

    if (files.empty()) {
        err_line("Usage: quilt edit file ...");
        return 1;
    }

    std::string_view patch = q.applied.back();

    // Add each file to the top patch if not already tracked
    for (const auto &file : files) {
        std::string backup_path = path_join(pc_patch_dir(q, patch), file);
        if (!file_exists(backup_path)) {
            if (!backup_file(q, patch, file)) {
                err("Failed to back up "); err_line(file);
                return 1;
            }
            out("File "); out(file); out(" added to patch ");
            out_line(patch_path_display(q, patch));
        }
    }

    // Get editor from environment
    std::string editor = get_env("EDITOR");
    if (editor.empty()) {
        editor = "vi";
    }

    // Launch editor with all files as arguments
    std::vector<std::string> cmd_argv;
    cmd_argv.push_back(editor);
    for (const auto &file : files) {
        cmd_argv.push_back(path_join(q.work_dir, file));
    }

    return run_cmd_tty(cmd_argv);
}

// Convert unified diff output to context diff format.  This allows
// quilt to produce -c/-C output even when the external diff command
// (e.g. busybox) only supports unified format.
static std::string unified_to_context(std::string_view unified)
{
    auto lines = split_lines(unified);
    std::string result;
    ptrdiff_t n = std::ssize(lines);
    ptrdiff_t i = 0;

    // File headers: unified --- becomes context ***, unified +++ becomes context ---
    while (i < n && !lines[checked_cast<size_t>(i)].starts_with("--- ")) ++i;
    if (i < n) { result += "*** " + lines[checked_cast<size_t>(i)].substr(4) + "\n"; ++i; }
    if (i < n && lines[checked_cast<size_t>(i)].starts_with("+++ ")) {
        result += "--- " + lines[checked_cast<size_t>(i)].substr(4) + "\n"; ++i;
    }

    while (i < n) {
        if (!lines[checked_cast<size_t>(i)].starts_with("@@ ")) { ++i; continue; }

        // Parse @@ -os[,oc] +ns[,nc] @@
        int os = 0, oc = 1, ns = 0, nc = 1;
        {
            std::string_view hdr = lines[checked_cast<size_t>(i)];
            ptrdiff_t at1 = str_find(hdr, '-', 3);
            if (at1 >= 0) {
                ptrdiff_t p = at1 + 1;
                ptrdiff_t c = str_find(hdr, ',', p);
                ptrdiff_t pp = str_find(hdr, '+', p);
                if (pp >= 0) {
                    if (c >= 0 && c < pp) {
                        os = checked_cast<int>(parse_int(hdr.substr(checked_cast<size_t>(p), checked_cast<size_t>(c - p))));
                        oc = checked_cast<int>(parse_int(hdr.substr(checked_cast<size_t>(c + 1), checked_cast<size_t>(pp - c - 2))));
                    } else {
                        os = checked_cast<int>(parse_int(hdr.substr(checked_cast<size_t>(p), checked_cast<size_t>(pp - p - 1))));
                    }
                    p = pp + 1;
                    c = str_find(hdr, ',', p);
                    ptrdiff_t end = str_find(hdr, ' ', p);
                    if (end < 0) end = std::ssize(hdr);
                    if (c >= 0 && c < end) {
                        ns = checked_cast<int>(parse_int(hdr.substr(checked_cast<size_t>(p), checked_cast<size_t>(c - p))));
                        nc = checked_cast<int>(parse_int(hdr.substr(checked_cast<size_t>(c + 1), checked_cast<size_t>(end - c - 1))));
                    } else {
                        ns = checked_cast<int>(parse_int(hdr.substr(checked_cast<size_t>(p), checked_cast<size_t>(end - p))));
                    }
                }
            }
        }
        ++i;

        // Collect unified hunk body lines with their types
        struct UL { char type; std::string text; };
        std::vector<UL> body;
        while (i < n && !lines[checked_cast<size_t>(i)].starts_with("@@ ")) {
            std::string_view ln = lines[checked_cast<size_t>(i)];
            body.push_back({ln.empty() ? ' ' : ln[0],
                            ln.empty() ? std::string{} : std::string(ln.substr(1))});
            ++i;
        }

        // Build old-side and new-side lines with context-diff prefixes.
        // Adjacent -/+ runs form "change" blocks and get '!' prefix.
        std::vector<std::pair<char, std::string>> old_side, new_side;
        for (ptrdiff_t k = 0; k < std::ssize(body); ) {
            if (body[checked_cast<size_t>(k)].type == ' ') {
                old_side.push_back({' ', body[checked_cast<size_t>(k)].text});
                new_side.push_back({' ', body[checked_cast<size_t>(k)].text});
                ++k;
            } else if (body[checked_cast<size_t>(k)].type == '-') {
                ptrdiff_t ds = k;
                while (k < std::ssize(body) && body[checked_cast<size_t>(k)].type == '-') ++k;
                ptrdiff_t as = k;
                while (k < std::ssize(body) && body[checked_cast<size_t>(k)].type == '+') ++k;
                bool change = (as > ds && k > as);
                for (ptrdiff_t m = ds; m < as; ++m)
                    old_side.push_back({change ? '!' : '-', body[checked_cast<size_t>(m)].text});
                for (ptrdiff_t m = as; m < k; ++m)
                    new_side.push_back({change ? '!' : '+', body[checked_cast<size_t>(m)].text});
            } else if (body[checked_cast<size_t>(k)].type == '+') {
                new_side.push_back({'+', body[checked_cast<size_t>(k)].text});
                ++k;
            } else {
                ++k;
            }
        }

        int oe = oc == 0 ? os : os + oc - 1;
        int ne = nc == 0 ? ns : ns + nc - 1;

        result += "***************\n";
        result += std::format("*** {},{} ****\n", os, oe);
        bool has_old = false;
        for (auto &[p, t] : old_side) if (p != ' ') { has_old = true; break; }
        if (has_old)
            for (auto &[p, t] : old_side)
                result += std::string(1, p) + " " + t + "\n";

        result += std::format("--- {},{} ----\n", ns, ne);
        bool has_new = false;
        for (auto &[p, t] : new_side) if (p != ' ') { has_new = true; break; }
        if (has_new)
            for (auto &[p, t] : new_side)
                result += std::string(1, p) + " " + t + "\n";
    }

    return result;
}

static constexpr std::string_view SNAPSHOT_PATCH = ".snap";

static bool is_placeholder_copy(std::string_view path)
{
    return file_exists(path) && read_file(path).empty();
}

// Parse QUILT_DIFF_OPTS and extract context line count if present.
// Returns the context line count (-1 if not specified in opts).
static int parse_diff_opts_context(std::span<const std::string> opts)
{
    for (ptrdiff_t i = 0; i < std::ssize(opts); ++i) {
        const auto &o = opts[checked_cast<size_t>(i)];
        if (o.starts_with("-U") && std::ssize(o) > 2) {
            return checked_cast<int>(parse_int(std::string_view(o).substr(2)));
        }
        if (o == "-U" && i + 1 < std::ssize(opts)) {
            return checked_cast<int>(parse_int(opts[checked_cast<size_t>(i + 1)]));
        }
    }
    return -1;
}

// p_format: "ab" for a/b labels, "0" for bare filenames, "1" (default) for dir.orig/dir
// Format a file modification time as "YYYY-MM-DD HH:MM:SS.000000000 +HHMM".
static std::string format_file_timestamp(std::string_view path) {
    int64_t mt = file_mtime(path);
    if (mt <= 0) return "";
    DateTime dt = local_time(mt);
    int off_h = dt.utc_offset / 3600;
    int off_m = (std::abs(dt.utc_offset) % 3600) / 60;
    return std::format("\t{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.000000000 {:+03d}{:02d}",
                       dt.year, dt.month, dt.day,
                       dt.hour, dt.min, dt.sec, off_h, off_m);
}

static std::string generate_path_diff(const QuiltState &q,
                                      std::string_view file,
                                      std::string_view old_path,
                                      bool old_placeholder,
                                      std::string_view new_path,
                                      bool new_placeholder,
                                      std::string_view p_format = "1",
                                      bool reverse = false,
                                      std::span<const std::string> diff_cmd_base = {},
                                      int context_lines = 3,
                                      DiffFormat diff_format = DiffFormat::unified,
                                      bool no_timestamps = false,
                                      DiffAlgorithm diff_algorithm = DiffAlgorithm::myers) {
    bool old_missing = old_path.empty() || !file_exists(old_path) ||
        (old_placeholder && is_placeholder_copy(old_path));
    bool new_missing = new_path.empty() || !file_exists(new_path) ||
        (new_placeholder && is_placeholder_copy(new_path));

    // Detect binary files (null bytes in first 8 KB)
    auto is_binary = [](std::string_view path) {
        std::string data = read_file(path);
        size_t check_len = data.size() < 8192 ? data.size() : 8192;
        return data.find('\0', 0) < check_len;
    };
    if ((!old_missing && is_binary(old_path)) ||
        (!new_missing && is_binary(new_path))) {
        return "Binary files differ\n";
    }

    std::string old_arg = old_missing ? "/dev/null" : std::string(old_path);
    std::string new_arg = new_missing ? "/dev/null" : std::string(new_path);

    std::string old_label;
    std::string new_label;
    if (p_format == "ab") {
        old_label = "a/" + std::string(file);
        new_label = "b/" + std::string(file);
    } else if (p_format == "0") {
        old_label = std::string(file) + ".orig";
        new_label = std::string(file);
    } else {
        std::string work_base = basename(q.work_dir);
        old_label = work_base + ".orig/" + std::string(file);
        new_label = work_base + "/" + std::string(file);
    }

    if (old_missing) {
        old_label = "/dev/null";
    }
    if (new_missing) {
        new_label = "/dev/null";
    }

    // Append file timestamps unless suppressed
    if (!no_timestamps) {
        if (!old_missing)
            old_label += format_file_timestamp(old_path);
        if (!new_missing)
            new_label += format_file_timestamp(new_path);
    }

    if (reverse) {
        std::swap(old_arg, new_arg);
    }

    // Use built-in diff when no external diff utility is specified
    if (diff_cmd_base.empty()) {
        int ctx = context_lines;
        // QUILT_DIFF_OPTS may override context lines
        auto extra_diff_opts = shell_split(get_env("QUILT_DIFF_OPTS"));
        int opts_ctx = parse_diff_opts_context(extra_diff_opts);
        if (opts_ctx >= 0) ctx = opts_ctx;

        DiffResult result = builtin_diff(old_arg, new_arg, ctx,
                                          old_label, new_label, diff_format,
                                          diff_algorithm);
        return result.output;
    }

    // External diff utility path
    std::vector<std::string> cmd_argv(diff_cmd_base.begin(), diff_cmd_base.end());
    auto extra_diff_opts = shell_split(get_env("QUILT_DIFF_OPTS"));
    for (const auto &opt : extra_diff_opts) {
        cmd_argv.push_back(opt);
    }

    cmd_argv.push_back("--label");
    cmd_argv.push_back(old_label);
    cmd_argv.push_back("--label");
    cmd_argv.push_back(new_label);
    cmd_argv.push_back(old_arg);
    cmd_argv.push_back(new_arg);

    ProcessResult result = run_cmd(cmd_argv);
    if (result.exit_code == 2) {
        return {};
    }

    return result.out;
}

static std::string generate_file_diff(const QuiltState &q, std::string_view patch,
                                      std::string_view file,
                                      std::string_view p_format = "1",
                                      bool reverse = false,
                                      std::span<const std::string> diff_cmd_base = {},
                                      int context_lines = 3,
                                      DiffFormat diff_format = DiffFormat::unified,
                                      bool no_timestamps = false,
                                      DiffAlgorithm diff_algorithm = DiffAlgorithm::myers) {
    std::string backup_path = path_join(pc_patch_dir(q, patch), file);
    std::string working_path = path_join(q.work_dir, file);
    return generate_path_diff(q, file, backup_path, true, working_path, false,
                              p_format, reverse, diff_cmd_base,
                              context_lines, diff_format, no_timestamps,
                              diff_algorithm);
}

static std::map<std::string, std::string> split_patch_by_file(std::string_view content) {
    std::map<std::string, std::string> sections;
    auto lines = split_lines(content);
    std::string current_file;
    std::string current_section;

    auto flush = [&]() {
        if (!current_file.empty() && !current_section.empty()) {
            sections[current_file] = current_section;
        }
        current_file.clear();
        current_section.clear();
    };

    for (const auto &line : lines) {
        if (line.starts_with("Index:") || line.starts_with("diff ")) {
            flush();
            current_section += line + "\n";
        } else if (line.starts_with("+++ ")) {
            // Extract filename from +++ line
            std::string_view rest = std::string_view(line).substr(4);
            if (rest.starts_with("/dev/null")) {
                // File deletion: current_file already set from --- line
            } else {
                // Strip b/ prefix and trailing tab/timestamp
                if (rest.starts_with("b/")) rest = rest.substr(2);
                auto tab = str_find(rest, '\t');
                if (tab >= 0) rest = rest.substr(0, checked_cast<size_t>(tab));
                // Strip leading directory component (e.g., "dir.orig/")
                auto slash = str_find(rest, '/');
                if (slash >= 0) {
                    current_file = trim(rest.substr(checked_cast<size_t>(slash + 1)));
                } else {
                    current_file = trim(rest);
                }
            }
            current_section += line + "\n";
        } else if (line.starts_with("===")) {
            current_section += line + "\n";
        } else if (line.starts_with("--- ")) {
            // For file deletions (+++ /dev/null), we get the name from ---
            if (current_file.empty()) {
                std::string_view rest = std::string_view(line).substr(4);
                if (!rest.starts_with("/dev/null")) {
                    if (rest.starts_with("a/")) rest = rest.substr(2);
                    auto tab = str_find(rest, '\t');
                    if (tab >= 0) rest = rest.substr(0, checked_cast<size_t>(tab));
                    auto slash = str_find(rest, '/');
                    if (slash >= 0) {
                        current_file = trim(rest.substr(checked_cast<size_t>(slash + 1)));
                    } else {
                        current_file = trim(rest);
                    }
                }
            }
            current_section += line + "\n";
        } else {
            current_section += line + "\n";
        }
    }
    flush();
    return sections;
}

static void append_unique_files(std::vector<std::string> &dst,
                                std::set<std::string> &seen,
                                std::span<const std::string> src) {
    for (const auto &file : src) {
        if (seen.insert(file).second) {
            dst.push_back(file);
        }
    }
}

static std::vector<std::string> collect_files_for_patches(
    const QuiltState &q, std::span<const std::string> patches) {
    std::vector<std::string> files;
    std::set<std::string> seen;
    for (const auto &patch : patches) {
        append_unique_files(files, seen, files_in_patch(q, patch));
    }
    return files;
}

static std::vector<std::string> patch_range_for_diff(const QuiltState &q,
                                                     std::string_view last_patch) {
    if (last_patch.empty()) {
        return q.applied;
    }

    std::vector<std::string> patches;
    for (const auto &patch : q.applied) {
        patches.push_back(patch);
        if (patch == last_patch) {
            return patches;
        }
    }

    return {std::string(last_patch)};
}

static std::string first_patch_for_file(const QuiltState &q,
                                        std::span<const std::string> patches,
                                        std::string_view file) {
    for (const auto &patch : patches) {
        auto tracked = files_in_patch(q, patch);
        if (std::ranges::find(tracked, file) != tracked.end()) {
            return patch;
        }
    }
    return "";
}


static void apply_file_filter(std::vector<std::string> &tracked,
                              std::span<const std::string> file_filter) {
    if (file_filter.empty()) {
        return;
    }

    std::vector<std::string> filtered;
    for (const auto &tracked_file : tracked) {
        for (const auto &wanted_file : file_filter) {
            if (tracked_file == wanted_file) {
                filtered.push_back(tracked_file);
                break;
            }
        }
    }
    tracked = std::move(filtered);
}

int cmd_snapshot(QuiltState &q, int argc, char **argv) {
    bool remove_snapshot = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-d") {
            remove_snapshot = true;
            continue;
        }
        err_line("Usage: quilt snapshot [-d]");
        return 1;
    }

    std::string snap_dir = pc_patch_dir(q, SNAPSHOT_PATCH);
    if (is_directory(snap_dir) && !delete_dir_recursive(snap_dir)) {
        err_line("Failed to remove " + snap_dir);
        return 1;
    }

    if (remove_snapshot) {
        return 0;
    }

    if (!ensure_pc_dir(q)) {
        return 1;
    }
    if (!make_dirs(snap_dir)) {
        err_line("Failed to create " + snap_dir);
        return 1;
    }

    auto tracked = collect_files_for_patches(q, q.applied);
    for (const auto &file : tracked) {
        if (!backup_file(q, SNAPSHOT_PATCH, file)) {
            err_line("Failed to snapshot " + file);
            return 1;
        }
    }

    return 0;
}

// Built-in diffstat: parse unified diff, produce a summary matching
// the output format of the external diffstat(1) utility.
static std::string generate_diffstat(std::string_view diff)
{
    struct FileStat {
        std::string name;
        ptrdiff_t added   = 0;
        ptrdiff_t removed = 0;
    };

    std::vector<FileStat> stats;
    auto lines = split_lines(diff);

    for (ptrdiff_t i = 0; i < std::ssize(lines); ++i) {
        const auto &line = lines[checked_cast<size_t>(i)];

        // Detect file header: "--- a/file" followed by "+++ b/file"
        if (line.starts_with("--- ") &&
            i + 1 < std::ssize(lines) &&
            lines[checked_cast<size_t>(i + 1)].starts_with("+++ ")) {
            const auto &plus_line = lines[checked_cast<size_t>(i + 1)];

            // Extract filename from +++ line, strip "b/" prefix
            auto name = plus_line.substr(4);
            // Strip trailing timestamp (tab-separated)
            auto tab = str_find(name, '\t');
            if (tab >= 0) name = name.substr(0, checked_cast<size_t>(tab));
            // Strip one leading path component (a/ or b/ prefix)
            auto slash = str_find(name, '/');
            if (slash >= 0) name = name.substr(checked_cast<size_t>(slash + 1));
            // /dev/null means new or deleted file — use --- line instead
            if (name == "dev/null" || plus_line.substr(4).starts_with("/dev/null")) {
                name = lines[checked_cast<size_t>(i)].substr(4);
                tab = str_find(name, '\t');
                if (tab >= 0) name = name.substr(0, checked_cast<size_t>(tab));
                slash = str_find(name, '/');
                if (slash >= 0) name = name.substr(checked_cast<size_t>(slash + 1));
            }

            stats.push_back({std::string(name), 0, 0});
            i += 1;  // skip +++ line
            continue;
        }

        if (stats.empty()) continue;

        if (line.starts_with("+") && !line.starts_with("+++"))
            stats.back().added++;
        else if (line.starts_with("-") && !line.starts_with("---"))
            stats.back().removed++;
    }

    if (stats.empty()) return {};

    // Leading "---" separator (matches git format-patch / original quilt)
    std::string result = "---\n";

    // Find max filename width and max change count
    ptrdiff_t max_name = 0;
    ptrdiff_t max_changes = 0;
    for (const auto &s : stats) {
        max_name = std::max(max_name, std::ssize(s.name));
        max_changes = std::max(max_changes, s.added + s.removed);
    }

    // Format change count to find its width (minimum 4, matching diffstat)
    auto num_width = std::max(std::ssize(std::to_string(max_changes)),
                              static_cast<ptrdiff_t>(4));

    // Bar graph width: fit in ~72 columns after " name | num "
    //   1 (leading space) + max_name + 3 (" | ") + num_width + 1 (space)
    ptrdiff_t used = 1 + max_name + 3 + num_width + 1;
    ptrdiff_t bar_width = std::max(static_cast<ptrdiff_t>(1),
                                   static_cast<ptrdiff_t>(72) - used);

    // Scale factor for bar graph
    double scale = (max_changes > bar_width)
        ? static_cast<double>(bar_width) / static_cast<double>(max_changes)
        : 1.0;

    ptrdiff_t total_added = 0, total_removed = 0;
    ptrdiff_t total_files = std::ssize(stats);

    for (const auto &s : stats) {
        total_added += s.added;
        total_removed += s.removed;

        ptrdiff_t changes = s.added + s.removed;
        ptrdiff_t plus_bars = static_cast<ptrdiff_t>(
            static_cast<double>(s.added) * scale + 0.5);
        ptrdiff_t minus_bars = static_cast<ptrdiff_t>(
            static_cast<double>(s.removed) * scale + 0.5);

        // Ensure at least 1 bar for non-zero counts
        if (s.added > 0 && plus_bars == 0) plus_bars = 1;
        if (s.removed > 0 && minus_bars == 0) minus_bars = 1;

        // Cap total bars at scaled width
        ptrdiff_t total_bars = plus_bars + minus_bars;
        ptrdiff_t limit = static_cast<ptrdiff_t>(
            static_cast<double>(changes) * scale + 0.5);
        if (limit < 1 && changes > 0) limit = 1;
        if (total_bars > limit) {
            // Reduce the larger portion
            if (plus_bars > minus_bars)
                plus_bars = limit - minus_bars;
            else
                minus_bars = limit - plus_bars;
        }

        result += ' ';
        result += s.name;
        for (ptrdiff_t j = std::ssize(s.name); j < max_name; ++j)
            result += ' ';
        result += " | ";
        auto num_str = std::to_string(changes);
        for (ptrdiff_t j = std::ssize(num_str); j < num_width; ++j)
            result += ' ';
        result += num_str;
        result += ' ';
        for (ptrdiff_t j = 0; j < plus_bars; ++j) result += '+';
        for (ptrdiff_t j = 0; j < minus_bars; ++j) result += '-';
        result += '\n';
    }

    // Summary line
    result += ' ';
    result += std::to_string(total_files);
    result += (total_files == 1) ? " file changed" : " files changed";
    if (total_added > 0) {
        result += ", ";
        result += std::to_string(total_added);
        result += (total_added == 1) ? " insertion(+)" : " insertions(+)";
    }
    if (total_removed > 0) {
        result += ", ";
        result += std::to_string(total_removed);
        result += (total_removed == 1) ? " deletion(-)" : " deletions(-)";
    }
    result += '\n';

    return result;
}

// Remove an existing diffstat section from a patch header.
// Detects "---" separator followed by " file | N ++--" lines ending
// with a "N file(s) changed" summary line.
static std::string remove_diffstat_section(std::string_view header) {
    auto lines = split_lines(header);
    std::string result;
    for (ptrdiff_t i = 0; i < std::ssize(lines); ++i) {
        const auto &line = lines[checked_cast<size_t>(i)];

        // Detect "---" separator followed by diffstat, or bare diffstat
        ptrdiff_t ds_start = i;
        if (line == "---" && i + 1 < std::ssize(lines)) {
            ds_start = i + 1;
        }

        const auto &first = lines[checked_cast<size_t>(ds_start)];
        if (!first.empty() && first[0] == ' ' &&
            str_find(first, '|') >= 0) {
            // Look ahead to confirm this is a diffstat block
            bool found_summary = false;
            ptrdiff_t summary_end = -1;
            for (ptrdiff_t j = ds_start; j < std::ssize(lines); ++j) {
                const auto &l = lines[checked_cast<size_t>(j)];
                if (l.find("changed") != std::string::npos &&
                    l.find("file") != std::string::npos) {
                    found_summary = true;
                    summary_end = j;
                    break;
                }
                // If we hit an empty line or non-diffstat line, stop
                if (l.empty() || (l[0] != ' ' && str_find(l, '|') < 0))
                    break;
            }
            if (found_summary) {
                // Skip the entire diffstat block including summary
                i = summary_end;
                // Also skip a trailing blank line after diffstat
                if (i + 1 < std::ssize(lines) && lines[checked_cast<size_t>(i + 1)].empty())
                    i++;
                continue;
            }
        }
        result += line;
        result += '\n';
    }
    return result;
}

// Strip trailing whitespace from diff output lines.
// Returns the cleaned diff and emits warnings to stderr.

int cmd_refresh(QuiltState &q, int argc, char **argv) {
    if (q.applied.empty()) {
        err_line("No patches applied");
        return 1;
    }

    // Parse options
    std::string patch;
    std::string p_format;
    bool explicit_p = false;
    int i = 1;
    bool no_timestamps = !get_env("QUILT_NO_DIFF_TIMESTAMPS").empty();
    bool no_index = !get_env("QUILT_NO_DIFF_INDEX").empty();
    bool sort_files = true;
    bool force = false;
    std::string diff_type;
    std::string context_num;
    bool opt_fork = false;
    std::string fork_name;
    bool opt_diffstat = false;
    bool opt_backup = false;
    bool opt_strip_whitespace = false;
    DiffAlgorithm diff_algorithm = DiffAlgorithm::myers;
    {
        auto env_algo = get_env("QUILT_DIFF_ALGORITHM");
        if (!env_algo.empty()) {
            auto parsed = parse_diff_algorithm(env_algo);
            if (!parsed) {
                err("Unknown diff algorithm: "); err_line(env_algo);
                return 1;
            }
            diff_algorithm = *parsed;
        }
    }

    while (i < argc) {
        std::string_view arg = argv[i];
        if (arg == "-p" && i + 1 < argc) {
            p_format = std::string(argv[i + 1]);
            explicit_p = true;
            i += 2;
            continue;
        }
        if (arg.starts_with("-p") && std::ssize(arg) > 2) {
            p_format = std::string(arg.substr(2));
            explicit_p = true;
            i += 1;
            continue;
        }
        if (arg == "-f") {
            force = true;
            i += 1;
            continue;
        }
        if (arg == "-u") {
            diff_type = "u";
            context_num.clear();
            i += 1;
            continue;
        }
        if (arg.starts_with("-U")) {
            diff_type = "U";
            if (arg == "-U" && i + 1 < argc) {
                context_num = argv[i + 1];
                i += 2;
            } else {
                context_num = std::string(arg.substr(2));
                i += 1;
            }
            continue;
        }
        if (arg == "-c") {
            diff_type = "c";
            context_num.clear();
            i += 1;
            continue;
        }
        if (arg.starts_with("-C")) {
            diff_type = "C";
            if (arg == "-C" && i + 1 < argc) {
                context_num = argv[i + 1];
                i += 2;
            } else {
                context_num = std::string(arg.substr(2));
                i += 1;
            }
            continue;
        }
        if (arg.starts_with("-z")) {
            opt_fork = true;
            if (std::ssize(arg) > 2) {
                fork_name = strip_patches_prefix(q, arg.substr(2));
            }
            i += 1;
            continue;
        }
        if (arg == "--no-timestamps" || arg == "--no-timestamp") {
            no_timestamps = true;
            i += 1;
            continue;
        }
        if (arg == "--no-index") {
            no_index = true;
            i += 1;
            continue;
        }
        if (arg == "--sort") {
            sort_files = true;
            i += 1;
            continue;
        }
        if (arg == "--diffstat") {
            opt_diffstat = true;
            i += 1;
            continue;
        }
        if (arg == "--backup") {
            opt_backup = true;
            i += 1;
            continue;
        }
        if (arg == "--strip-trailing-whitespace") {
            opt_strip_whitespace = true;
            i += 1;
            continue;
        }
        if (arg.starts_with("--diff-algorithm=")) {
            auto name = arg.substr(17);
            auto algo = parse_diff_algorithm(name);
            if (!algo) {
                err("Unknown diff algorithm: "); err_line(name);
                return 1;
            }
            diff_algorithm = *algo;
            i += 1;
            continue;
        }
        if (arg == "--diff-algorithm" && i + 1 < argc) {
            std::string_view name = argv[i + 1];
            auto algo = parse_diff_algorithm(name);
            if (!algo) {
                err("Unknown diff algorithm: "); err_line(name);
                return 1;
            }
            diff_algorithm = *algo;
            i += 2;
            continue;
        }
        if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
        // Non-option: patch name
        if (patch.empty()) {
            patch = arg;
        }
        i += 1;
    }

    if (patch.empty()) {
        patch = q.applied.back();
    }

    if (!explicit_p) {
        p_format = q.get_p_format(patch);
    }

    // Compute diff format and context lines
    DiffFormat diff_format = DiffFormat::unified;
    int ctx_lines = 3;
    if (diff_type == "c") {
        diff_format = DiffFormat::context;
    } else if (diff_type == "C") {
        diff_format = DiffFormat::context;
        ctx_lines = checked_cast<int>(parse_int(context_num));
    } else if (diff_type == "U") {
        ctx_lines = checked_cast<int>(parse_int(context_num));
    }

    // Fork before refresh if -z was given
    // Unlike standalone "fork" (which replaces the original), refresh -z
    // inserts a new patch *after* the original and refreshes the fork.
    // The original patch keeps its content. The fork captures only the
    // delta between the original's refreshed state and the current working
    // tree.
    bool did_fork = false;
    if (opt_fork) {
        if (patch != q.applied.back()) {
            err_line("Can only use -z with the topmost applied patch");
            return 1;
        }
        std::string old_name(patch);

        // Generate fork name
        std::string new_name;
        if (!fork_name.empty()) {
            new_name = fork_name;
        } else {
            auto dot = str_rfind(old_name, '.');
            if (dot > 0) {
                new_name = old_name.substr(0, checked_cast<size_t>(dot)) + "-2" + old_name.substr(checked_cast<size_t>(dot));
            } else {
                new_name = old_name + "-2";
            }
        }

        if (q.find_in_series(new_name)) {
            err("Patch "); err(new_name); err_line(" already exists in series");
            return 1;
        }

        auto idx = q.find_in_series(old_name);
        if (!idx) {
            err("Patch "); err(old_name); err_line(" is not in series");
            return 1;
        }

        // Create the fork's .pc/ directory with backups representing the
        // file state *after* the original patch (i.e., the intermediate
        // state between the two patches).  Reconstruct this by applying
        // the original patch to the backup copies in an in-memory FS.
        std::string old_pc = pc_patch_dir(q, old_name);
        std::string new_pc = pc_patch_dir(q, new_name);
        make_dirs(new_pc);

        std::string orig_patch_path = path_join(q.work_dir, q.patches_dir, old_name);
        std::string orig_patch_content = read_file(orig_patch_path);
        int orig_strip = q.get_strip_level(old_name);
        auto orig_files = files_in_patch(q, old_name);

        // Build in-memory FS from backup copies, apply original patch
        std::map<std::string, std::string> memfs;
        for (const auto &file : orig_files) {
            std::string backup_path = path_join(old_pc, file);
            if (file_exists(backup_path) && !is_placeholder_copy(backup_path)) {
                memfs[file] = read_file(backup_path);
            }
        }
        if (!orig_patch_content.empty()) {
            PatchOptions popts;
            popts.strip_level = orig_strip;
            popts.quiet = true;
            popts.fs = &memfs;
            if (q.patch_reversed.contains(old_name)) popts.reverse = true;
            builtin_patch(orig_patch_content, popts);
        }

        // Write the intermediate states to the fork's .pc/ directory
        for (const auto &file : orig_files) {
            std::string fork_backup = path_join(new_pc, file);
            std::string fork_dir = dirname(fork_backup);
            if (!is_directory(fork_dir)) make_dirs(fork_dir);

            auto it = memfs.find(file);
            if (it != memfs.end()) {
                write_file(fork_backup, it->second);
            } else {
                // File was deleted by patch or not present — write empty placeholder
                write_file(fork_backup, "");
            }
        }

        // Write .timestamp for the fork
        write_file(path_join(new_pc, ".timestamp"), "");

        // Migrate per-patch metadata to fork
        auto sl_it = q.patch_strip_level.find(old_name);
        if (sl_it != q.patch_strip_level.end()) {
            q.patch_strip_level[new_name] = sl_it->second;
        }
        if (q.patch_reversed.contains(old_name)) {
            q.patch_reversed.insert(new_name);
        }

        // Insert new patch after original in series
        q.series.insert(q.series.begin() + *idx + 1, new_name);
        std::string series_abs = path_join(q.work_dir, q.series_file);
        if (!write_series(series_abs, q.series, q.patch_strip_level, q.patch_reversed)) {
            err_line("Failed to write series file.");
            return 1;
        }

        // Update applied: add fork after original
        q.applied.push_back(new_name);
        std::string applied_path = path_join(q.work_dir, q.pc_dir, "applied-patches");
        write_applied(applied_path, q.applied);

        out_line("Fork of patch " + patch_path_display(q, old_name) +
                 " created as " + patch_path_display(q, new_name));
        patch = new_name;
        // Suppress the "Refreshed patch" message; the fork message is sufficient
        did_fork = true;
    }

    // Compute shadowed files (files modified by patches above this one)
    // For each shadowed file, record the first patch above that tracks it
    // (needed to find its backup as the "new" side of the diff).
    std::set<std::string> shadowed;
    std::map<std::string, std::string> shadow_next_patch;
    if (patch != q.applied.back()) {
        bool above = false;
        for (const auto &a : q.applied) {
            if (above) {
                auto above_files = files_in_patch(q, a);
                for (const auto &f : above_files) {
                    if (!shadowed.contains(f)) {
                        shadow_next_patch[f] = a;
                    }
                    shadowed.insert(f);
                }
            }
            if (a == patch) above = true;
        }
    }

    if (!shadowed.empty() && !force) {
        err("More recent patches modify files in patch ");
        err(patch_path_display(q, patch)); err_line(". Enforce refresh with -f.");
        return 1;
    }

    // Get files tracked by this patch
    auto tracked = files_in_patch(q, patch);
    if (sort_files) {
        std::ranges::sort(tracked);
    }

    // Read existing patch file for header
    std::string patch_file = path_join(q.work_dir, q.patches_dir, patch);
    std::string old_content;
    std::string header;
    if (file_exists(patch_file)) {
        old_content = read_file(patch_file);
        header = read_patch_header(patch_file);
    }

    // Backup old patch file if requested
    if (opt_backup && file_exists(patch_file)) {
        copy_file(patch_file, patch_file + "~");
    }

    // Generate diffs
    std::string work_base = basename(q.work_dir);
    std::string patch_content = header;

    for (const auto &file : tracked) {
        if (shadowed.contains(file)) {
            // Diff this patch's backup against the next patch's backup
            auto it = shadow_next_patch.find(file);
            if (it != shadow_next_patch.end()) {
                std::string this_backup = path_join(pc_patch_dir(q, patch), file);
                std::string next_backup = path_join(pc_patch_dir(q, it->second), file);
                std::string diff_out = generate_path_diff(q, file,
                    this_backup, true, next_backup, true,
                    p_format, false, {}, ctx_lines, diff_format, no_timestamps,
                    diff_algorithm);
                if (!diff_out.empty()) {
                    if (!no_index) {
                        std::string idx_name;
                        if (p_format == "0") idx_name = file;
                        else if (p_format == "ab") idx_name = "b/" + file;
                        else idx_name = basename(q.work_dir) + "/" + file;
                        patch_content += "Index: " + idx_name + "\n";
                        patch_content += "===================================================================\n";
                    }
                    patch_content += diff_out;
                    if (!patch_content.empty() && patch_content.back() != '\n') {
                        patch_content += '\n';
                    }
                }
            }
            continue;
        }
        // Strip trailing whitespace from lines modified by this patch
        if (opt_strip_whitespace) {
            std::string working_path = path_join(q.work_dir, file);
            if (file_exists(working_path)) {
                // Find which lines are modified by diffing backup vs working
                std::set<int> modified_lines;
                std::string diff_check = generate_file_diff(
                    q, patch, file, "1", false, {}, 0,
                    DiffFormat::unified, true, diff_algorithm);
                if (!diff_check.empty()) {
                    auto dlines = split_lines(diff_check);
                    int new_lineno = 0;
                    for (const auto &dl : dlines) {
                        if (dl.starts_with("---") || dl.starts_with("+++")) {
                            continue;
                        }
                        if (dl.starts_with("@@")) {
                            // Parse @@ -X,Y +N,M @@
                            auto plus = str_find(dl, '+');
                            if (plus >= 0) {
                                auto comma = str_find(
                                    std::string_view(dl).substr(
                                        checked_cast<size_t>(plus)), ',');
                                std::string_view num_str;
                                if (comma >= 0) {
                                    num_str = std::string_view(dl).substr(
                                        checked_cast<size_t>(plus) + 1,
                                        checked_cast<size_t>(comma) - 1);
                                } else {
                                    auto sp = str_find(
                                        std::string_view(dl).substr(
                                            checked_cast<size_t>(plus)), ' ');
                                    if (sp >= 0) {
                                        num_str = std::string_view(dl).substr(
                                            checked_cast<size_t>(plus) + 1,
                                            checked_cast<size_t>(sp) - 1);
                                    }
                                }
                                if (!num_str.empty()) {
                                    int n = 0;
                                    std::from_chars(num_str.data(),
                                        num_str.data() + num_str.size(), n);
                                    new_lineno = n - 1;  // will be incremented
                                }
                            }
                        } else if (dl.starts_with("+")) {
                            new_lineno++;
                            modified_lines.insert(new_lineno);
                        } else if (!dl.starts_with("-")) {
                            new_lineno++;  // context line
                        }
                    }
                }

                std::string content = read_file(working_path);
                std::string stripped;
                auto lines = split_lines(content);
                int lineno = 0;
                for (const auto &line : lines) {
                    lineno++;
                    std::string_view l = line;
                    bool is_modified = modified_lines.contains(lineno);
                    auto end = l.find_last_not_of(" \t");
                    if (is_modified && end == std::string::npos) {
                        if (!l.empty()) {
                            out_line("Removing trailing whitespace from line "
                                     + std::to_string(lineno) + " of " + file);
                        }
                        stripped += '\n';
                    } else if (is_modified &&
                               static_cast<ptrdiff_t>(end) + 1 < std::ssize(l)) {
                        out_line("Removing trailing whitespace from line "
                                 + std::to_string(lineno) + " of " + file);
                        stripped += l.substr(0, end + 1);
                        stripped += '\n';
                    } else {
                        stripped += l;
                        stripped += '\n';
                    }
                }
                if (stripped != content) {
                    write_file(working_path, stripped);
                }
            }
        }

        std::string diff_out = generate_file_diff(q, patch, file, p_format,
                                                   false, {}, ctx_lines,
                                                   diff_format, no_timestamps,
                                                   diff_algorithm);
        if (diff_out.starts_with("Binary files ")) {
            err("Diff failed on file '"); err(file); err_line("', aborting");
            return 1;
        }
        if (!diff_out.empty()) {
            if (!no_index) {
                std::string idx_name;
                if (p_format == "0") idx_name = file;
                else if (p_format == "ab") idx_name = "b/" + file;
                else idx_name = work_base + "/" + file;
                patch_content += "Index: " + idx_name + "\n";
                patch_content += "===================================================================\n";
            }
            patch_content += diff_out;
            // Ensure trailing newline
            if (!patch_content.empty() && patch_content.back() != '\n') {
                patch_content += '\n';
            }
        }
    }

    // Add diffstat to header if requested
    if (opt_diffstat) {
        std::string diff_portion = patch_content.substr(checked_cast<size_t>(std::ssize(header)));
        if (!diff_portion.empty()) {
            std::string ds_out = generate_diffstat(diff_portion);
            if (!ds_out.empty()) {
                std::string clean_header = remove_diffstat_section(header);
                // Remove trailing blank lines from header
                while (std::ssize(clean_header) > 1 &&
                       clean_header[checked_cast<size_t>(std::ssize(clean_header) - 1)] == '\n' &&
                       clean_header[checked_cast<size_t>(std::ssize(clean_header) - 2)] == '\n') {
                    clean_header.pop_back();
                }
                patch_content = clean_header;
                if (!patch_content.empty() && patch_content.back() != '\n')
                    patch_content += '\n';
                patch_content += ds_out;
                if (!ds_out.empty() && ds_out.back() != '\n')
                    patch_content += '\n';
                patch_content += '\n';
                patch_content += diff_portion;
            }
        }
    }

    // Check if patch has no diff hunks
    bool has_diff = false;
    for (auto &line : split_lines(patch_content)) {
        if (line.starts_with("--- ") || line.starts_with("diff ")) {
            has_diff = true;
            break;
        }
    }

    // Check if patch content is unchanged (only skip write if file exists)
    if (patch_content == old_content && file_exists(patch_file)) {
        if (!has_diff) {
            out("Nothing in patch "); out_line(patch_path_display(q, patch));
        } else {
            out("Patch "); out(patch_path_display(q, patch));
            out_line(" is unchanged");
        }
        return 0;
    }

    // Ensure patches directory exists
    std::string patch_dir = dirname(patch_file);
    if (!is_directory(patch_dir)) {
        make_dirs(patch_dir);
    }

    // Write the patch file
    if (!write_file(patch_file, patch_content)) {
        err_line("Failed to write patch file " + patch_file);
        return 1;
    }

    // Update .timestamp
    write_file(path_join(pc_patch_dir(q, patch), ".timestamp"), "");

    // Clear .needs_refresh marker if present
    std::string nr = path_join(pc_patch_dir(q, patch), ".needs_refresh");
    if (file_exists(nr)) {
        delete_file(nr);
    }

    if (!did_fork) {
        if (!has_diff) {
            out("Nothing in patch "); out_line(patch_path_display(q, patch));
        } else {
            out("Refreshed patch "); out_line(patch_path_display(q, patch));
        }
    }
    return 0;
}

int cmd_diff(QuiltState &q, int argc, char **argv) {
    if (q.applied.empty()) {
        err_line("No patches applied");
        return 1;
    }

    // Parse options
    std::string_view patch;
    std::string p_format;
    bool explicit_p = false;
    std::vector<std::string> file_filter;
    bool no_timestamps = !get_env("QUILT_NO_DIFF_TIMESTAMPS").empty();
    bool no_index = !get_env("QUILT_NO_DIFF_INDEX").empty();
    bool since_refresh = false;
    bool against_snapshot = false;
    bool reverse = false;
    bool sort_files = true;
    std::string diff_utility;
    std::string combine_patch;
    std::string diff_type = "u";
    std::string context_num;
    DiffAlgorithm diff_algorithm = DiffAlgorithm::myers;
    {
        auto env_algo = get_env("QUILT_DIFF_ALGORITHM");
        if (!env_algo.empty()) {
            auto parsed = parse_diff_algorithm(env_algo);
            if (!parsed) {
                err("Unknown diff algorithm: "); err_line(env_algo);
                return 1;
            }
            diff_algorithm = *parsed;
        }
    }
    int i = 1;

    while (i < argc) {
        std::string_view arg = argv[i];
        if (arg == "-P" && i + 1 < argc) {
            patch = strip_patches_prefix(q, argv[i + 1]);
            i += 2;
            continue;
        }
        if (arg == "-p" && i + 1 < argc) {
            p_format = std::string(argv[i + 1]);
            explicit_p = true;
            i += 2;
            continue;
        }
        if (arg.starts_with("-p") && std::ssize(arg) > 2) {
            p_format = std::string(arg.substr(2));
            explicit_p = true;
            i += 1;
            continue;
        }
        if (arg == "-u") {
            diff_type = "u";
            context_num.clear();
            i += 1;
            continue;
        }
        if (arg == "-c") {
            diff_type = "c";
            context_num.clear();
            i += 1;
            continue;
        }
        if (arg.starts_with("-C")) {
            diff_type = "C";
            if (arg == "-C" && i + 1 < argc) {
                context_num = argv[i + 1];
                i += 2;
            } else {
                context_num = std::string(arg.substr(2));
                i += 1;
            }
            continue;
        }
        if (arg.starts_with("-U")) {
            diff_type = "U";
            if (arg == "-U" && i + 1 < argc) {
                context_num = argv[i + 1];
                i += 2;
            } else {
                context_num = std::string(arg.substr(2));
                i += 1;
            }
            continue;
        }
        if (arg == "-z") {
            since_refresh = true;
            i += 1;
            continue;
        }
        if (arg == "--snapshot") {
            against_snapshot = true;
            i += 1;
            continue;
        }
        if (arg == "-R") {
            reverse = true;
            i += 1;
            continue;
        }
        if (arg == "--no-timestamps" || arg == "--no-timestamp") {
            no_timestamps = true;
            i += 1;
            continue;
        }
        if (arg == "--no-index") {
            no_index = true;
            i += 1;
            continue;
        }
        if (arg == "--sort") {
            sort_files = true;
            i += 1;
            continue;
        }
        if (arg == "--combine" && i + 1 < argc) {
            combine_patch = argv[i + 1];
            i += 2;
            continue;
        }
        if (arg.starts_with("--combine=")) {
            combine_patch = std::string(arg.substr(10));
            i += 1;
            continue;
        }
        if (arg.starts_with("--diff=")) {
            diff_utility = std::string(arg.substr(7));
            i += 1;
            continue;
        }
        if (arg.starts_with("--diff-algorithm=")) {
            auto name = arg.substr(17);
            auto algo = parse_diff_algorithm(name);
            if (!algo) {
                err("Unknown diff algorithm: "); err_line(name);
                return 1;
            }
            diff_algorithm = *algo;
            i += 1;
            continue;
        }
        if (arg == "--diff-algorithm" && i + 1 < argc) {
            std::string_view name = argv[i + 1];
            auto algo = parse_diff_algorithm(name);
            if (!algo) {
                err("Unknown diff algorithm: "); err_line(name);
                return 1;
            }
            diff_algorithm = *algo;
            i += 2;
            continue;
        }
        if (arg == "--color" || arg.starts_with("--color=")) {
            if (arg.starts_with("--color=")) {
                auto val = arg.substr(8);
                if (val != "always" && val != "auto" && val != "never") {
                    err("Invalid --color value: "); err_line(val);
                    return 1;
                }
            }
            i += 1;
            continue;
        }
        if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
        // Non-option: file name or patch name
        if (arg[0] != '-') {
            file_filter.push_back(subdir_path(q, arg));
        }
        i += 1;
    }

    if (patch.empty()) {
        patch = q.applied.back();
    }

    if (!explicit_p) {
        p_format = q.get_p_format(patch);
    }

    if (since_refresh && against_snapshot) {
        err_line("Options `--snapshot' and `-z' cannot be combined.");
        return 1;
    }

    if (!combine_patch.empty() && since_refresh) {
        err_line("Options `--combine' and `-z' cannot be combined.");
        return 1;
    }

    if (!combine_patch.empty() && against_snapshot) {
        err_line("Options `--combine' and `--snapshot' cannot be combined.");
        return 1;
    }

    // Determine diff format and context lines for builtin diff
    DiffFormat diff_format = DiffFormat::unified;
    int ctx_lines = 3;
    if (diff_type == "c") {
        diff_format = DiffFormat::context;
    } else if (diff_type == "C") {
        diff_format = DiffFormat::context;
        ctx_lines = checked_cast<int>(parse_int(context_num));
    } else if (diff_type == "U") {
        ctx_lines = checked_cast<int>(parse_int(context_num));
    }

    // Build diff command base for external diff utility (empty = use builtin)
    bool convert_to_context = false;
    std::vector<std::string> diff_cmd_base;
    if (!diff_utility.empty()) {
        auto parts = split_on_whitespace(diff_utility);
        for (auto &p : parts) diff_cmd_base.push_back(std::move(p));
        // External diff: always request unified, convert to context in-process
        convert_to_context = (diff_type == "c" || diff_type == "C");
        if (diff_type == "U" || diff_type == "C") {
            diff_cmd_base.push_back("-U");
            diff_cmd_base.push_back(context_num);
        } else {
            diff_cmd_base.push_back("-u");
        }
    }

    auto emit_diff = [&](std::string_view d) {
        if (convert_to_context)
            out(unified_to_context(d));
        else
            out(d);
    };

    // Resolve --combine patch name
    std::string combine_start;
    if (!combine_patch.empty()) {
        if (combine_patch == "-") {
            combine_start = q.applied.front();
        } else {
            combine_start = strip_patches_prefix(q, combine_patch);
        }
    }

    auto patches = patch_range_for_diff(q, patch);
    std::vector<std::string> tracked;
    if (against_snapshot) {
        std::string snap_dir = pc_patch_dir(q, SNAPSHOT_PATCH);
        if (!is_directory(snap_dir)) {
            err_line("No snapshot to diff against");
            return 1;
        }

        std::set<std::string> seen;
        append_unique_files(tracked, seen, files_in_patch(q, SNAPSHOT_PATCH));
        append_unique_files(tracked, seen, collect_files_for_patches(q, patches));
    } else if (!combine_start.empty()) {
        // Collect files across the combine range
        std::vector<std::string> combine_range;
        bool in_range = false;
        for (const auto &a : q.applied) {
            if (a == combine_start) in_range = true;
            if (in_range) combine_range.push_back(a);
            if (a == patch) break;
        }
        tracked = collect_files_for_patches(q, combine_range);
    } else {
        tracked = files_in_patch(q, patch);
    }

    apply_file_filter(tracked, file_filter);

    if (sort_files) {
        std::ranges::sort(tracked);
    }

    std::string work_base = basename(q.work_dir);

    if (since_refresh) {
        // diff -z: show changes since last refresh.
        // For each tracked file, reconstruct the "refreshed state" by applying
        // the stored patch to the backup, then diff that against the working file.
        std::string patch_file = path_join(q.work_dir, q.patches_dir, patch);
        std::string stored_content;
        std::map<std::string, std::string> stored_sections;
        if (file_exists(patch_file)) {
            stored_content = read_file(patch_file);
            stored_sections = split_patch_by_file(stored_content);
        }

        // Create temp directory for reconstructing refreshed state
        std::string tmp_dir = make_temp_dir();
        if (tmp_dir.empty()) {
            err_line("Failed to create temp directory");
            return 1;
        }

        for (const auto &file : tracked) {
            std::string backup_path = path_join(pc_patch_dir(q, patch), file);
            std::string working_path = path_join(q.work_dir, file);
            std::string tmp_file = path_join(tmp_dir, file);

            // Ensure temp subdirectory exists
            std::string tmp_file_dir = dirname(tmp_file);
            if (!is_directory(tmp_file_dir)) {
                make_dirs(tmp_file_dir);
            }

            // Copy backup to temp (empty backup = file didn't exist)
            if (file_exists(backup_path)) {
                std::string backup_content = read_file(backup_path);
                write_file(tmp_file, backup_content);
            } else {
                write_file(tmp_file, "");
            }

            // Apply stored patch section to temp file
            auto it = stored_sections.find(file);
            if (it != stored_sections.end() && !it->second.empty()) {
                // Build a minimal patch with correct paths for -p0
                std::string mini_patch = "--- " + file + "\n+++ " + file + "\n";
                // Extract just the hunk lines from the stored section
                auto section_lines = split_lines(it->second);
                bool in_hunk = false;
                for (const auto &sl : section_lines) {
                    if (sl.starts_with("@@")) {
                        in_hunk = true;
                        mini_patch += sl + "\n";
                    } else if (in_hunk) {
                        mini_patch += sl + "\n";
                    }
                }
                if (in_hunk) {
                    std::string saved_cwd = get_cwd();
                    if (set_cwd(tmp_dir)) {
                        PatchOptions po;
                        po.strip_level = 0;
                        po.remove_empty = true;
                        po.quiet = true;
                        builtin_patch(mini_patch, po);
                        set_cwd(saved_cwd);
                    }
                }
            }

            // Now diff the reconstructed "refreshed" file against working file
            if (!file_exists(working_path) && !file_exists(tmp_file)) continue;

            std::string old_label, new_label;
            if (p_format == "ab") {
                old_label = "a/" + file;
                new_label = "b/" + file;
            } else if (p_format == "0") {
                old_label = file;
                new_label = file;
            } else {
                old_label = work_base + ".orig/" + file;
                new_label = work_base + "/" + file;
            }

            std::string old_f = tmp_file;
            std::string new_f = working_path;
            if (!file_exists(working_path)) new_f = "/dev/null";

            if (reverse) {
                std::swap(old_f, new_f);
            }

            std::string diff_out;
            if (diff_cmd_base.empty()) {
                // Use built-in diff
                int ctx = ctx_lines;
                auto extra_diff_opts = shell_split(get_env("QUILT_DIFF_OPTS"));
                int opts_ctx = parse_diff_opts_context(extra_diff_opts);
                if (opts_ctx >= 0) ctx = opts_ctx;

                DiffResult dr = builtin_diff(old_f, new_f, ctx,
                                             old_label, new_label, diff_format,
                                             diff_algorithm);
                diff_out = std::move(dr.output);
            } else {
                std::vector<std::string> diff_cmd = diff_cmd_base;
                auto extra_diff_opts = shell_split(get_env("QUILT_DIFF_OPTS"));
                for (const auto &opt : extra_diff_opts) diff_cmd.push_back(opt);
                diff_cmd.push_back("--label");
                diff_cmd.push_back(old_label);
                diff_cmd.push_back("--label");
                diff_cmd.push_back(new_label);
                diff_cmd.push_back(old_f);
                diff_cmd.push_back(new_f);

                ProcessResult result = run_cmd(diff_cmd);
                if (result.exit_code == 1) {
                    diff_out = std::move(result.out);
                }
            }
            if (!diff_out.empty()) {
                if (!no_index) {
                    out("Index: " + (p_format == "0" ? file : p_format == "ab" ? "b/" + file : work_base + "/" + file) + "\n");
                    out("===================================================================\n");
                }
                emit_diff(diff_out);
            }
        }

        delete_dir_recursive(tmp_dir);
    } else if (against_snapshot) {
        for (const auto &file : tracked) {
            std::string old_path = path_join(pc_patch_dir(q, SNAPSHOT_PATCH), file);
            bool old_placeholder = true;
            if (!file_exists(old_path)) {
                std::string first_patch = first_patch_for_file(q, patches, file);
                if (first_patch.empty()) {
                    continue;
                }
                old_path = path_join(pc_patch_dir(q, first_patch), file);
            }

            std::string new_path = path_join(q.work_dir, file);
            bool new_placeholder = false;
            std::string shadowing_patch = next_patch_for_file(q, patch, file);
            if (!shadowing_patch.empty()) {
                new_path = path_join(pc_patch_dir(q, shadowing_patch), file);
                new_placeholder = true;
            }

            std::string diff_out = generate_path_diff(
                q, file, old_path, old_placeholder, new_path, new_placeholder,
                p_format, reverse, diff_cmd_base, ctx_lines, diff_format,
                no_timestamps, diff_algorithm);
            if (!diff_out.empty()) {
                if (!no_index) {
                    out("Index: " + (p_format == "0" ? file : p_format == "ab" ? "b/" + file : work_base + "/" + file) + "\n");
                    out("===================================================================\n");
                }
                emit_diff(diff_out);
            }
        }
    } else if (!combine_start.empty()) {
        // --combine: diff backup from the earliest patch in range against working file
        for (const auto &file : tracked) {
            // Find the earliest patch in the combine range that tracks this file
            std::string earliest;
            bool in_range = false;
            for (const auto &a : q.applied) {
                if (a == combine_start) in_range = true;
                if (in_range) {
                    auto fip = files_in_patch(q, a);
                    if (std::ranges::find(fip, file) != fip.end()) {
                        earliest = a;
                        break;
                    }
                }
                if (a == patch) break;
            }
            if (earliest.empty()) continue;

            std::string old_path = path_join(pc_patch_dir(q, earliest), file);
            std::string new_path = path_join(q.work_dir, file);
            bool new_placeholder = false;

            // If a patch above the range shadows this file, use its backup
            std::string shadowing_patch = next_patch_for_file(q, patch, file);
            if (!shadowing_patch.empty()) {
                new_path = path_join(pc_patch_dir(q, shadowing_patch), file);
                new_placeholder = true;
            }

            std::string diff_out = generate_path_diff(
                q, file, old_path, true, new_path, new_placeholder,
                p_format, reverse, diff_cmd_base, ctx_lines, diff_format,
                no_timestamps, diff_algorithm);
            if (!diff_out.empty()) {
                if (!no_index) {
                    out("Index: " + (p_format == "0" ? file : p_format == "ab" ? "b/" + file : work_base + "/" + file) + "\n");
                    out("===================================================================\n");
                }
                emit_diff(diff_out);
            }
        }
    } else {
        // Warn if more recent patches modify files in this patch
        bool warned_shadowing = false;
        for (const auto &file : tracked) {
            std::string shadowing = next_patch_for_file(q, patch, file);
            if (!shadowing.empty() && !warned_shadowing) {
                err("Warning: more recent patches modify files in patch ");
                err_line(patch_path_display(q, patch));
                warned_shadowing = true;
            }

            std::string old_path = path_join(pc_patch_dir(q, patch), file);
            std::string new_path = path_join(q.work_dir, file);
            bool new_placeholder = false;

            // If a patch above this one shadows the file, use its backup
            if (!shadowing.empty()) {
                new_path = path_join(pc_patch_dir(q, shadowing), file);
                new_placeholder = true;
            }

            std::string diff_out = generate_path_diff(
                q, file, old_path, true, new_path, new_placeholder,
                p_format, reverse, diff_cmd_base, ctx_lines, diff_format,
                no_timestamps, diff_algorithm);
            if (!diff_out.empty()) {
                if (!no_index) {
                    out("Index: " + (p_format == "0" ? file : p_format == "ab" ? "b/" + file : work_base + "/" + file) + "\n");
                    out("===================================================================\n");
                }
                emit_diff(diff_out);
            }
        }
    }

    return 0;
}

int cmd_revert(QuiltState &q, int argc, char **argv) {
    if (q.applied.empty()) {
        err_line("No patches applied");
        return 1;
    }

    // Parse options
    std::string_view patch = q.applied.back();
    std::vector<std::string> files;
    int i = 1;
    while (i < argc) {
        std::string_view arg = argv[i];
        if (arg == "-P" && i + 1 < argc) {
            patch = strip_patches_prefix(q, argv[i + 1]);
            i += 2;
            continue;
        }
        if (arg[0] != '-') {
            files.push_back(subdir_path(q, arg));
        } else {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
        i += 1;
    }

    if (files.empty()) {
        err_line("Usage: quilt revert [-P patch] file ...");
        return 1;
    }

    if (!q.is_applied(patch)) {
        err("Patch "); err(format_patch(q, patch)); err_line(" is not applied");
        return 1;
    }

    // Check if any later applied patch also modifies these files
    bool found_patch = false;
    for (const auto &ap : q.applied) {
        if (!found_patch) {
            if (ap == patch) found_patch = true;
            continue;
        }
        // ap is a patch applied after 'patch'
        auto later_files = files_in_patch(q, ap);
        for (const auto &file : files) {
            for (const auto &lf : later_files) {
                if (lf == file) {
                    err("File "); err(file);
                    err(" modified by patch ");
                    err_line(patch_path_display(q, ap));
                    return 1;
                }
            }
        }
    }

    // Read the patch file to apply its hunks to backup content
    std::string patch_file = path_join(q.work_dir, q.patches_dir, patch);
    std::string patch_text = read_file(patch_file);
    int strip_level = q.patch_strip_level.count(std::string(patch))
        ? q.patch_strip_level.at(std::string(patch)) : 1;

    for (const auto &file : files) {
        // Check if file is tracked by the patch
        std::string backup_path = path_join(pc_patch_dir(q, patch), file);
        if (!file_exists(backup_path)) {
            err("File "); err(file); err(" is not in patch ");
            err_line(patch_path_display(q, patch));
            return 1;
        }

        // Build the clean post-patch state by applying patch to backup
        std::string backup_content = read_file(backup_path);
        std::map<std::string, std::string> memfs;
        memfs[file] = backup_content;
        PatchOptions opts;
        opts.strip_level = strip_level;
        opts.quiet = true;
        opts.fs = &memfs;
        builtin_patch(patch_text, opts);

        std::string clean_content = memfs.count(file) ? memfs[file] : "";

        // Check if current file matches clean state (unchanged)
        std::string target = path_join(q.work_dir, file);
        std::string current = file_exists(target) ? read_file(target) : "";
        if (current == clean_content) {
            out("File "); out(file);
            out_line(" is unchanged");
            continue;
        }

        // Write the clean post-patch state
        if (clean_content.empty()) {
            // Post-patch state is empty — either file was deleted by patch
            // or didn't exist.  Remove the working-tree copy.
            delete_file(target);
            out("Changes to "); out(file); out(" in patch ");
            out(patch_path_display(q, patch)); out_line(" reverted");
            continue;
        }

        std::string target_dir = dirname(target);
        if (!is_directory(target_dir)) {
            make_dirs(target_dir);
        }
        if (!write_file(target, clean_content)) {
            err("Failed to restore "); err_line(file);
            return 1;
        }

        out("Changes to "); out(file); out(" in patch ");
        out(patch_path_display(q, patch)); out_line(" reverted");
    }

    return 0;
}

// === src/cmd_manage.cpp ===

// This is free and unencumbered software released into the public domain.


static bool write_series_checked(const QuiltState &q,
                                 std::span<const std::string> series) {
    std::string series_abs = path_join(q.work_dir, q.series_file);
    if (!write_series(series_abs, series, q.patch_strip_level, q.patch_reversed)) {
        err_line("Failed to write series file.");
        return false;
    }
    return true;
}

static bool write_applied_checked(const QuiltState &q,
                                  std::span<const std::string> applied) {
    std::string applied_path = path_join(q.work_dir, q.pc_dir, "applied-patches");
    if (!write_applied(applied_path, applied)) {
        err_line("Failed to write applied-patches.");
        return false;
    }
    return true;
}


static std::string extract_header(std::string_view content) {
    std::string header;
    auto lines = split_lines(content);
    for (const auto &line : lines) {
        if (line.starts_with("Index:") ||
            line.starts_with("--- ") ||
            line.starts_with("diff ") ||
            line.starts_with("===")) {
            break;
        }
        header += line;
        header += '\n';
    }
    return header;
}

static std::string replace_header(std::string_view content, std::string_view new_header) {
    std::string result;
    auto lines = split_lines(content);
    bool in_diff = false;
    // Find where diffs start
    ptrdiff_t diff_start = 0;
    for (ptrdiff_t i = 0; i < std::ssize(lines); ++i) {
        if (lines[checked_cast<size_t>(i)].starts_with("Index:") ||
            lines[checked_cast<size_t>(i)].starts_with("--- ") ||
            lines[checked_cast<size_t>(i)].starts_with("diff ") ||
            lines[checked_cast<size_t>(i)].starts_with("===")) {
            diff_start = i;
            in_diff = true;
            break;
        }
    }

    result += std::string(new_header);
    // Ensure header ends with newline if non-empty
    if (!result.empty() && result.back() != '\n') {
        result += '\n';
    }

    if (in_diff) {
        for (ptrdiff_t i = diff_start; i < std::ssize(lines); ++i) {
            result += lines[checked_cast<size_t>(i)];
            result += '\n';
        }
    }
    return result;
}


int cmd_delete(QuiltState &q, int argc, char **argv) {
    bool opt_remove = false;
    bool opt_backup = false;
    bool opt_next = false;
    std::string_view patch_arg;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-r") {
            opt_remove = true;
        } else if (arg == "--backup") {
            opt_backup = true;
        } else if (arg == "-n") {
            opt_next = true;
        } else if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        } else {
            patch_arg = strip_patches_prefix(q, arg);
        }
    }

    std::string patch;
    if (!patch_arg.empty()) {
        patch = patch_arg;
    } else if (opt_next) {
        // Next unapplied patch
        ptrdiff_t top_idx = q.top_index();
        ptrdiff_t next_idx = top_idx + 1;
        if (next_idx >= std::ssize(q.series)) {
            err_line("No next patch");
            return 1;
        }
        patch = q.series[checked_cast<size_t>(next_idx)];
    } else {
        // Topmost applied patch
        if (q.applied.empty()) {
            err_line("No patches applied");
            return 1;
        }
        patch = q.applied.back();
    }

    // Verify patch is in series
    auto idx = q.find_in_series(patch);
    if (!idx) {
        err("Patch "); err(patch); err_line(" is not in series");
        return 1;
    }

    // If patch is applied, only allow deleting the topmost patch
    if (q.is_applied(patch)) {
        if (patch != q.applied.back()) {
            err("Patch "); err(patch_path_display(q, patch));
            err_line(" is currently applied");
            return 1;
        }
        // Pop the topmost patch silently (no per-file messages)
        auto tracked = files_in_patch(q, patch);
        out_line("Removing patch " + patch_path_display(q, patch));
        for (const auto &f : tracked) {
            restore_file(q, patch, f);
        }
        std::string pc_dir = pc_patch_dir(q, patch);
        if (is_directory(pc_dir)) delete_dir_recursive(pc_dir);
        q.applied.pop_back();
        if (!write_applied_checked(q, q.applied)) return 1;
        if (!q.applied.empty()) {
            out_line("Now at patch " +
                     patch_path_display(q, q.applied.back()));
        } else {
            out_line("No patches applied");
        }
    }

    // Remove from series
    auto new_series = q.series;
    new_series.erase(new_series.begin() + *idx);
    if (!write_series_checked(q, new_series)) {
        return 1;
    }
    q.series = std::move(new_series);

    // Optionally remove the patch file
    if (opt_remove) {
        std::string patch_file = path_join(q.work_dir, q.patches_dir, patch);
        if (opt_backup) {
            std::string backup = patch_file + "~";
            if (file_exists(patch_file) && !rename_path(patch_file, backup)) {
                err_line("Failed to rename " + patch_file + " to " + backup);
                return 1;
            }
        } else if (file_exists(patch_file) && !delete_file(patch_file)) {
            err_line("Failed to delete " + patch_file);
            return 1;
        }
    }

    out_line("Removed patch " + patch_path_display(q, patch));
    return 0;
}

int cmd_rename(QuiltState &q, int argc, char **argv) {
    std::string old_patch;
    std::string new_name;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-P" && i + 1 < argc) {
            old_patch = strip_patches_prefix(q, argv[++i]);
        } else if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        } else {
            new_name = strip_patches_prefix(q, arg);
        }
    }

    // Default to top patch
    if (old_patch.empty()) {
        if (q.applied.empty()) {
            err_line("No patches applied");
            return 1;
        }
        old_patch = q.applied.back();
    }

    if (new_name.empty()) {
        err_line("Usage: quilt rename [-P patch] new_name");
        return 1;
    }

    // Verify old patch exists in series
    auto idx = q.find_in_series(old_patch);
    if (!idx) {
        err("Patch "); err(old_patch); err_line(" is not in series");
        return 1;
    }

    // Verify new name doesn't exist in series
    auto new_idx = q.find_in_series(new_name);
    if (new_idx) {
        err("Patch "); err(patch_path_display(q, new_name));
        err_line(" exists already, please choose a different name");
        return 1;
    }

    // Rename patch file
    std::string old_file = path_join(q.work_dir, q.patches_dir, old_patch);
    std::string new_file = path_join(q.work_dir, q.patches_dir, new_name);
    bool renamed_patch_file = false;
    if (file_exists(old_file)) {
        // Ensure target directory exists
        std::string new_dir = dirname(new_file);
        if (!is_directory(new_dir)) {
            if (!make_dirs(new_dir)) {
                err_line("Failed to create " + new_dir);
                return 1;
            }
        }
        if (!rename_path(old_file, new_file)) {
            err_line("Failed to rename " + old_file + " to " + new_file);
            return 1;
        }
        renamed_patch_file = true;
    }

    // If patch is applied: rename in applied-patches and .pc/ dir
    auto new_applied = q.applied;
    bool renamed_pc_dir = false;
    if (q.is_applied(old_patch)) {
        for (auto &a : new_applied) {
            if (a == old_patch) {
                a = new_name;
                break;
            }
        }
        std::string old_pc = pc_patch_dir(q, old_patch);
        std::string new_pc = pc_patch_dir(q, new_name);
        if (is_directory(old_pc)) {
            if (!rename_path(old_pc, new_pc)) {
                if (renamed_patch_file) {
                    rename_path(new_file, old_file);
                }
                err_line("Failed to rename " + old_pc + " to " + new_pc);
                return 1;
            }
            renamed_pc_dir = true;
        }
    }

    // Migrate per-patch metadata before writing series
    std::string old_key(old_patch);
    auto sl_it = q.patch_strip_level.find(old_key);
    int saved_strip = -1;
    bool saved_reversed = false;
    if (sl_it != q.patch_strip_level.end()) {
        saved_strip = sl_it->second;
        q.patch_strip_level[new_name] = sl_it->second;
        q.patch_strip_level.erase(sl_it);
    }
    if (q.patch_reversed.erase(old_key)) {
        saved_reversed = true;
        q.patch_reversed.insert(new_name);
    }

    auto new_series = q.series;
    new_series[checked_cast<size_t>(*idx)] = new_name;
    if (!write_series_checked(q, new_series)) {
        // Undo metadata migration
        if (saved_strip >= 0) {
            q.patch_strip_level[old_key] = saved_strip;
            q.patch_strip_level.erase(new_name);
        }
        if (saved_reversed) {
            q.patch_reversed.erase(new_name);
            q.patch_reversed.insert(old_key);
        }
        if (renamed_pc_dir) {
            rename_path(pc_patch_dir(q, new_name), pc_patch_dir(q, old_patch));
        }
        if (renamed_patch_file) {
            rename_path(new_file, old_file);
        }
        return 1;
    }

    if (q.is_applied(old_patch) && !write_applied_checked(q, new_applied)) {
        write_series_checked(q, q.series);
        if (saved_strip >= 0) {
            q.patch_strip_level[old_key] = saved_strip;
            q.patch_strip_level.erase(new_name);
        }
        if (saved_reversed) {
            q.patch_reversed.erase(new_name);
            q.patch_reversed.insert(old_key);
        }
        if (renamed_pc_dir) {
            rename_path(pc_patch_dir(q, new_name), pc_patch_dir(q, old_patch));
        }
        if (renamed_patch_file) {
            rename_path(new_file, old_file);
        }
        return 1;
    }

    q.series = std::move(new_series);
    if (q.is_applied(old_patch)) {
        q.applied = std::move(new_applied);
    }

    out("Patch "); out(patch_path_display(q, old_patch));
    out(" renamed to "); out_line(patch_path_display(q, new_name));
    return 0;
}

int cmd_import(QuiltState &q, int argc, char **argv) {
    int strip_level = -1;
    std::string target_name;
    bool force = false;
    char dup_mode = 0;  // o=overwrite, a=append, n=next
    bool reversed = false;
    std::vector<std::string> patchfiles;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-p" && i + 1 < argc) {
            strip_level = checked_cast<int>(parse_int(argv[++i]));
        } else if (arg == "-R") {
            reversed = true;
        } else if (arg == "-P" && i + 1 < argc) {
            target_name = strip_patches_prefix(q, argv[++i]);
        } else if (arg == "-f") {
            force = true;
        } else if (arg == "-d" && i + 1 < argc) {
            dup_mode = argv[++i][0];
        } else if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        } else {
            patchfiles.emplace_back(arg);
        }
    }

    if (patchfiles.empty()) {
        err_line("Usage: quilt import [-p num] [-R] [-P patch] [-f] [-d {o|a|n}] patchfile ...");
        return 1;
    }

    if (!target_name.empty() && patchfiles.size() > 1) {
        err_line("Option `-P' can only be used when importing a single patch");
        return 1;
    }

    if (!ensure_pc_dir(q)) {
        return 1;
    }

    // Ensure patches dir exists
    std::string patches_abs = path_join(q.work_dir, q.patches_dir);
    if (!is_directory(patches_abs)) {
        if (!make_dirs(patches_abs)) {
            err_line("Failed to create " + patches_abs);
            return 1;
        }
    }

    for (const auto &patchfile : patchfiles) {
        // Determine target name
        std::string name;
        if (!target_name.empty()) {
            name = target_name;
        } else {
            name = basename(patchfile);
        }

        std::string dest = path_join(q.work_dir, q.patches_dir, name);

        // Check if target exists in series
        auto existing = q.find_in_series(name);
        if (existing && q.is_applied(name)) {
            err_line("Patch " + patch_path_display(q, name) +
                     " is applied");
            return 1;
        }
        if (existing && !force) {
            err_line("Patch " + patch_path_display(q, name) +
                     " exists. Replace with -f.");
            return 1;
        }

        // Ensure parent directory of dest exists (for subdir patch names)
        std::string dest_dir = dirname(dest);
        if (!is_directory(dest_dir)) {
            if (!make_dirs(dest_dir)) {
                err_line("Failed to create " + dest_dir);
                return 1;
            }
        }

        // Copy patchfile to patches/<name>, handling -d header mode
        if (existing && force && dup_mode && dup_mode != 'n') {
            // Merge headers based on -d mode
            std::string old_content = read_file(dest);
            std::string new_content = read_file(patchfile);
            std::string old_hdr = extract_header(old_content);
            std::string new_hdr = extract_header(new_content);
            std::string merged_header;
            if (dup_mode == 'o') {
                merged_header = old_hdr;
            } else if (dup_mode == 'a') {
                merged_header = old_hdr;
                if (!merged_header.empty() && merged_header.back() != '\n')
                    merged_header += '\n';
                merged_header += "---\n";
                merged_header += new_hdr;
            }
            std::string result = replace_header(new_content, merged_header);
            if (!write_file(dest, result)) {
                err_line("Failed to write " + dest);
                return 1;
            }
        } else if (existing && force && !dup_mode) {
            // Both patches exist and no -d flag: check if both have headers
            std::string old_content = read_file(dest);
            std::string new_content = read_file(patchfile);
            std::string old_hdr = extract_header(old_content);
            std::string new_hdr = extract_header(new_content);
            if (!old_hdr.empty() && !new_hdr.empty() && old_hdr != new_hdr) {
                err_line("Patch headers differ:");
                err_line("@@ -1 +1 @@");
                err_line("-" + old_hdr);
                err_line("+" + new_hdr);
                err_line("Please use -d {o|a|n} to specify which patch "
                         "header(s) to keep.");
                return 1;
            }
            if (!copy_file(patchfile, dest)) {
                err_line("Failed to copy " + patchfile + " to " + dest);
                return 1;
            }
        } else {
            if (!copy_file(patchfile, dest)) {
                err_line("Failed to copy " + patchfile + " to " + dest);
                return 1;
            }
        }

        // Update per-patch metadata
        if (strip_level >= 0 && strip_level != 1) {
            q.patch_strip_level[name] = strip_level;
        } else if (strip_level < 0) {
            q.patch_strip_level.erase(name);
        }
        if (reversed) {
            q.patch_reversed.insert(name);
        } else {
            q.patch_reversed.erase(name);
        }

        // Add to series if not already present
        if (!existing) {
            // Insert after top applied patch, or at end if none applied
            ptrdiff_t top_idx = q.top_index();
            auto new_series = q.series;
            if (top_idx >= 0 && top_idx + 1 < std::ssize(new_series)) {
                new_series.insert(new_series.begin() + top_idx + 1, name);
            } else {
                new_series.push_back(name);
            }
            if (!write_series_checked(q, new_series)) {
                delete_file(dest);
                return 1;
            }
            q.series = std::move(new_series);
        } else {
            // Overwriting existing patch — rewrite series for metadata update
            if (!write_series_checked(q, q.series)) {
                return 1;
            }
        }

        if (existing && force) {
            out_line("Replacing patch " + patch_path_display(q, name) +
                     " with new version");
        } else {
            out_line("Importing patch " + patchfile +
                     " (stored as " + patch_path_display(q, name) + ")");
        }
    }

    return 0;
}

// Remove an existing diffstat section from a header.
// Detects "---" separator followed by " file | N ++--" lines ending
// with a "N file(s) changed" summary line.
static std::string strip_diffstat(std::string_view header) {
    auto lines = split_lines(header);
    std::string result;
    for (ptrdiff_t i = 0; i < std::ssize(lines); ++i) {
        const auto &line = lines[checked_cast<size_t>(i)];

        // Detect "---" separator followed by diffstat, or bare diffstat
        ptrdiff_t ds_start = i;
        if (line == "---" && i + 1 < std::ssize(lines)) {
            ds_start = i + 1;
        }

        const auto &first = lines[checked_cast<size_t>(ds_start)];
        if (!first.empty() && first[0] == ' ' &&
            str_find(first, '|') >= 0) {
            bool found_summary = false;
            ptrdiff_t summary_end = -1;
            for (ptrdiff_t j = ds_start; j < std::ssize(lines); ++j) {
                const auto &l = lines[checked_cast<size_t>(j)];
                if (l.find("changed") != std::string::npos &&
                    l.find("file") != std::string::npos) {
                    found_summary = true;
                    summary_end = j;
                    break;
                }
                if (l.empty() || (l[0] != ' ' && str_find(l, '|') < 0))
                    break;
            }
            if (found_summary) {
                // Keep the "---" separator if the diffstat followed it
                if (ds_start != i) {
                    result += line;
                    result += '\n';
                }
                i = summary_end;
                if (i + 1 < std::ssize(lines) && lines[checked_cast<size_t>(i + 1)].empty())
                    i++;
                continue;
            }
        }
        result += line;
        result += '\n';
    }
    return result;
}

// Strip trailing whitespace from each line of a header.
static std::string strip_header_trailing_ws(std::string_view header) {
    std::string result;
    auto lines = split_lines(header);
    for (const auto &line : lines) {
        if (line.empty()) {
            result += '\n';
            continue;
        }
        // Strip \r from CRLF before checking for trailing whitespace
        std::string_view l = line;
        if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
        auto end = l.find_last_not_of(" \t");
        if (end == std::string::npos) {
            result += '\n';
        } else {
            result += l.substr(0, end + 1);
            result += '\n';
        }
    }
    return result;
}

static constexpr const char *dep3_template =
    "Description: <short summary>\n"
    " <long description that can span multiple lines>\n"
    "Author: \n"
    "Origin: <upstream|backport|vendor|other>, <URL>\n"
    "Bug: <URL to upstream bug report>\n"
    "Bug-Debian: https://bugs.debian.org/<bugnumber>\n"
    "Forwarded: <URL|no|not-needed>\n"
    "Applied-Upstream: <version|URL|commit>\n"
    "Last-Update: <YYYY-MM-DD>\n";

int cmd_header(QuiltState &q, int argc, char **argv) {
    enum Mode { PRINT, APPEND, REPLACE, EDIT };
    Mode mode = PRINT;
    bool opt_backup = false;
    bool opt_dep3 = false;
    bool opt_strip_ds = false;
    bool opt_strip_ws = false;
    std::string_view patch_arg;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-a") {
            mode = APPEND;
        } else if (arg == "-r") {
            mode = REPLACE;
        } else if (arg == "-e") {
            mode = EDIT;
        } else if (arg == "--backup") {
            opt_backup = true;
        } else if (arg == "--dep3") {
            opt_dep3 = true;
        } else if (arg == "--strip-diffstat") {
            opt_strip_ds = true;
        } else if (arg == "--strip-trailing-whitespace") {
            opt_strip_ws = true;
        } else if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        } else {
            patch_arg = strip_patches_prefix(q, arg);
        }
    }

    // Determine patch
    std::string_view patch;
    if (!patch_arg.empty()) {
        patch = patch_arg;
        // Verify patch is in series
        if (!q.find_in_series(patch)) {
            err("Patch "); err(patch);
            err_line(" is not in series");
            return 1;
        }
    } else if (!q.applied.empty()) {
        patch = q.applied.back();
    } else {
        err_line("No patches applied");
        return 1;
    }

    std::string patch_file = path_join(q.work_dir, q.patches_dir, patch);
    std::string content = read_file(patch_file);

    // Helper to apply --strip-diffstat and --strip-trailing-whitespace
    auto apply_strip = [&](std::string h) {
        if (opt_strip_ds) h = strip_diffstat(h);
        if (opt_strip_ws) h = strip_header_trailing_ws(h);
        return h;
    };

    if (mode == PRINT) {
        std::string header = apply_strip(extract_header(content));
        out(header);
        return 0;
    }

    if (mode == APPEND) {
        std::string stdin_data = read_stdin();
        std::string old_header = extract_header(content);
        std::string new_header = apply_strip(old_header + stdin_data);
        if (opt_backup) {
            copy_file(patch_file, patch_file + "~");
        }
        std::string new_content = replace_header(content, new_header);
        write_file(patch_file, new_content);
        out_line("Appended text to header of patch " +
                 patch_path_display(q, patch));
        return 0;
    }

    if (mode == REPLACE) {
        std::string stdin_data = read_stdin();
        std::string new_header = apply_strip(stdin_data);
        if (opt_backup) {
            copy_file(patch_file, patch_file + "~");
        }
        std::string new_content = replace_header(content, new_header);
        write_file(patch_file, new_content);
        out_line("Replaced header of patch " +
                 patch_path_display(q, patch));
        return 0;
    }

    if (mode == EDIT) {
        std::string editor = get_env("EDITOR");
        if (editor.empty()) editor = "vi";

        std::string header = extract_header(content);
        // Insert DEP-3 template if header is empty and --dep3 given
        if (opt_dep3 && trim(header).empty()) {
            header = dep3_template;
        }
        std::string tmp_file = path_join(q.work_dir, ".pc/.quilt_header_tmp");
        write_file(tmp_file, header);

        int rc = run_cmd_tty({editor, tmp_file});
        if (rc != 0) {
            delete_file(tmp_file);
            err_line("Editor exited with error");
            return 1;
        }

        std::string new_header = apply_strip(read_file(tmp_file));
        delete_file(tmp_file);

        if (opt_backup) {
            copy_file(patch_file, patch_file + "~");
        }
        std::string new_content = replace_header(content, new_header);
        write_file(patch_file, new_content);
        out_line("Replaced header of patch " + patch_path_display(q, patch));
        return 0;
    }

    return 0;
}

int cmd_files(QuiltState &q, int argc, char **argv) {
    bool opt_verbose = false;
    bool opt_all = false;
    bool opt_labels = false;
    std::string combine_patch;
    std::string_view patch_arg;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-v") {
            opt_verbose = true;
        } else if (arg == "-a") {
            opt_all = true;
        } else if (arg == "-l") {
            opt_labels = true;
        } else if (arg == "--combine" && i + 1 < argc) {
            combine_patch = argv[++i];
        } else if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        } else {
            patch_arg = strip_patches_prefix(q, arg);
        }
    }

    // Determine target patch (topmost or specified)
    std::string target_patch;
    if (!patch_arg.empty()) {
        target_patch = patch_arg;
    } else if (!q.applied.empty()) {
        target_patch = q.applied.back();
    } else if (!opt_all) {
        err_line("No patches applied");
        return 1;
    }

    // Build list of patches to show files for
    std::vector<std::string> patches_to_show;
    if (opt_all) {
        patches_to_show = q.applied;
    } else if (!combine_patch.empty()) {
        // Range from combine_patch through target_patch
        std::string start = combine_patch;
        if (start == "-") {
            if (q.applied.empty()) {
                err_line("No patches applied");
                return 1;
            }
            start = q.applied.front();
        } else {
            start = strip_patches_prefix(q, start);
        }
        bool in_range = false;
        for (const auto &a : q.applied) {
            if (a == start) in_range = true;
            if (in_range) patches_to_show.push_back(a);
            if (a == target_patch) break;
        }
        if (!in_range || patches_to_show.empty()) {
            err("Patch ");
            err(start);
            err_line(" not applied");
            return 1;
        }
    } else {
        patches_to_show.push_back(target_patch);
    }

    // With labels (-l): iterate patches, output per-patch file listings
    if (opt_labels) {
        for (const auto &patch : patches_to_show) {
            std::vector<std::string> file_list;
            if (q.is_applied(patch)) {
                file_list = files_in_patch(q, patch);
            } else {
                std::string patch_file = path_join(q.work_dir, q.patches_dir, patch);
                std::string content = read_file(patch_file);
                file_list = parse_patch_files(content);
            }
            std::ranges::sort(file_list);
            for (const auto &f : file_list) {
                out_line(patch + " " + f);
            }
        }
    } else {
        // Collect all files across patches
        std::vector<std::string> all_files;
        for (const auto &patch : patches_to_show) {
            std::vector<std::string> file_list;
            if (q.is_applied(patch)) {
                file_list = files_in_patch(q, patch);
            } else {
                std::string patch_file = path_join(q.work_dir, q.patches_dir, patch);
                std::string content = read_file(patch_file);
                file_list = parse_patch_files(content);
            }
            for (auto &f : file_list) {
                all_files.push_back(std::move(f));
            }
        }
        std::ranges::sort(all_files);
        for (const auto &f : all_files) {
            if (opt_verbose) {
                out_line("  " + f);
            } else {
                out_line(f);
            }
        }
    }

    return 0;
}

int cmd_patches(QuiltState &q, int argc, char **argv) {
    bool opt_verbose = false;
    std::vector<std::string> target_files;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-v") {
            opt_verbose = true;
        } else if (arg == "--color" || arg.starts_with("--color=")) {
            if (arg.starts_with("--color=")) {
                auto val = arg.substr(8);
                if (val != "always" && val != "auto" && val != "never") {
                    err("Invalid --color value: "); err_line(val);
                    return 1;
                }
            }
        } else if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        } else {
            target_files.push_back(subdir_path(q, arg));
        }
    }

    if (target_files.empty()) {
        err_line("Usage: quilt patches [-v] [--color] file [files...]");
        return 1;
    }

    for (const auto &patch : q.series) {
        bool touches = false;

        if (q.is_applied(patch)) {
            // Check .pc/<patch>/<file>
            std::string pc_dir = pc_patch_dir(q, patch);
            for (const auto &tf : target_files) {
                std::string check = path_join(pc_dir, tf);
                if (file_exists(check)) {
                    touches = true;
                    break;
                }
            }
        } else {
            // Parse patch file for references
            std::string patch_file = path_join(q.work_dir, q.patches_dir, patch);
            std::string content = read_file(patch_file);
            auto patched_files = parse_patch_files(content);
            for (const auto &tf : target_files) {
                for (const auto &pf : patched_files) {
                    if (pf == tf) {
                        touches = true;
                        break;
                    }
                }
                if (touches) break;
            }
        }

        if (touches) {
            std::string display = patch;
            if (opt_verbose) {
                // Show applied status: = for top, + for other applied, space for unapplied
                if (!q.applied.empty() && patch == q.applied.back()) {
                    out_line("= " + display);
                } else if (q.is_applied(patch)) {
                    out_line("+ " + display);
                } else {
                    out_line("  " + display);
                }
            } else {
                out_line(display);
            }
        }
    }

    return 0;
}

int cmd_fold(QuiltState &q, int argc, char **argv) {
    bool opt_reverse = false;
    bool opt_quiet = false;
    bool opt_force = false;
    int strip_level = 1;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-R") {
            opt_reverse = true;
        } else if (arg == "-q") {
            opt_quiet = true;
        } else if (arg == "-f") {
            opt_force = true;
        } else if (arg == "-p" && i + 1 < argc) {
            strip_level = checked_cast<int>(parse_int(argv[++i]));
        } else if (arg.starts_with("-p") && arg.size() > 2 &&
                   arg[2] >= '0' && arg[2] <= '9') {
            strip_level = checked_cast<int>(parse_int(arg.substr(2)));
        } else if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
    }

    if (q.applied.empty()) {
        err_line("No patches applied");
        return 1;
    }

    std::string top = q.applied.back();
    std::string stdin_data = read_stdin();

    if (stdin_data.empty()) {
        return 0;
    }

    // Parse the incoming patch to find affected files
    auto affected_files = parse_patch_files(stdin_data, strip_level);

    // Track new files in the current patch
    auto currently_tracked = files_in_patch(q, top);
    for (const auto &f : affected_files) {
        bool already_tracked = false;
        for (const auto &t : currently_tracked) {
            if (t == f) { already_tracked = true; break; }
        }
        if (!already_tracked) {
            backup_file(q, top, f);
        }
    }

    // Apply patch using built-in patch engine
    PatchOptions patch_opts;
    patch_opts.strip_level = strip_level;
    patch_opts.reverse = opt_reverse;
    patch_opts.force = opt_force;
    patch_opts.quiet = opt_quiet;
    auto extra_patch_opts = shell_split(get_env("QUILT_PATCH_OPTS"));
    for (const auto &opt : extra_patch_opts) {
        std::string_view o = opt;
        if (o == "-R") patch_opts.reverse = true;
        else if (o == "-f" || o == "--force") patch_opts.force = true;
        else if (o == "-s") patch_opts.quiet = true;
        else if (o == "-E") patch_opts.remove_empty = true;
        else if (o.starts_with("--fuzz=")) {
            patch_opts.fuzz = checked_cast<int>(parse_int(o.substr(7)));
        }
    }

    PatchResult r = builtin_patch(stdin_data, patch_opts);
    if (!opt_quiet && !r.out.empty()) {
        out(r.out);
    }
    if (!r.err.empty()) err(r.err);

    if (r.exit_code != 0 && !opt_force) {
        return 1;
    }

    return 0;
}

int cmd_fork(QuiltState &q, int argc, char **argv) {
    if (q.applied.empty()) {
        err_line("No patches applied");
        return 1;
    }

    std::string old_name = q.applied.back();
    std::string new_name;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
        new_name = strip_patches_prefix(q, arg);
        break;
    }

    // Generate default name if none given: increment "-N" suffix before extension
    if (new_name.empty()) {
        auto dot = str_rfind(old_name, '.');
        std::string base;
        std::string ext;
        if (dot > 0) {
            base = old_name.substr(0, checked_cast<size_t>(dot));
            ext = old_name.substr(checked_cast<size_t>(dot));
        } else {
            base = old_name;
        }
        // Check for existing -N suffix and increment it
        auto dash = str_rfind(base, '-');
        if (dash >= 0) {
            std::string_view suffix = std::string_view(base).substr(
                checked_cast<size_t>(dash) + 1);
            int n = 0;
            auto [ptr, ec] = std::from_chars(suffix.data(),
                suffix.data() + suffix.size(), n);
            if (ec == std::errc{} && ptr == suffix.data() + suffix.size()) {
                new_name = base.substr(0, checked_cast<size_t>(dash) + 1)
                    + std::to_string(n + 1) + ext;
            } else {
                new_name = base + "-2" + ext;
            }
        } else {
            new_name = base + "-2" + ext;
        }
    }

    // Check that the new name doesn't already exist in series
    if (q.find_in_series(new_name)) {
        err("Patch "); err(new_name); err_line(" already exists in series");
        return 1;
    }

    auto idx = q.find_in_series(old_name);
    if (!idx) {
        err("Patch "); err(old_name); err_line(" is not in series");
        return 1;
    }

    // Copy patch file
    std::string old_file = path_join(q.work_dir, q.patches_dir, old_name);
    std::string new_file = path_join(q.work_dir, q.patches_dir, new_name);
    bool copied_patch_file = false;
    if (file_exists(old_file)) {
        std::string new_dir = dirname(new_file);
        if (!is_directory(new_dir)) {
            if (!make_dirs(new_dir)) {
                err_line("Failed to create " + new_dir);
                return 1;
            }
        }
        if (!copy_file(old_file, new_file)) {
            err_line("Failed to copy " + old_file + " to " + new_file);
            return 1;
        }
        copied_patch_file = true;
    }

    // Rename .pc/ directory
    std::string old_pc = pc_patch_dir(q, old_name);
    std::string new_pc = pc_patch_dir(q, new_name);
    bool renamed_pc_dir = false;
    if (is_directory(old_pc)) {
        if (!rename_path(old_pc, new_pc)) {
            if (copied_patch_file) {
                delete_file(new_file);
            }
            err_line("Failed to rename " + old_pc + " to " + new_pc);
            return 1;
        }
        renamed_pc_dir = true;
    }

    // Migrate per-patch metadata before writing series
    auto sl_it = q.patch_strip_level.find(old_name);
    int saved_strip = -1;
    bool saved_reversed = false;
    if (sl_it != q.patch_strip_level.end()) {
        saved_strip = sl_it->second;
        q.patch_strip_level[new_name] = sl_it->second;
        q.patch_strip_level.erase(sl_it);
    }
    if (q.patch_reversed.erase(old_name)) {
        saved_reversed = true;
        q.patch_reversed.insert(new_name);
    }

    auto new_series = q.series;
    new_series[checked_cast<size_t>(*idx)] = new_name;
    if (!write_series_checked(q, new_series)) {
        if (saved_strip >= 0) {
            q.patch_strip_level[old_name] = saved_strip;
            q.patch_strip_level.erase(new_name);
        }
        if (saved_reversed) {
            q.patch_reversed.erase(new_name);
            q.patch_reversed.insert(old_name);
        }
        if (renamed_pc_dir) {
            rename_path(new_pc, old_pc);
        }
        if (copied_patch_file) {
            delete_file(new_file);
        }
        return 1;
    }

    auto new_applied = q.applied;
    for (auto &a : new_applied) {
        if (a == old_name) {
            a = new_name;
            break;
        }
    }
    if (!write_applied_checked(q, new_applied)) {
        write_series_checked(q, q.series);
        if (saved_strip >= 0) {
            q.patch_strip_level[old_name] = saved_strip;
            q.patch_strip_level.erase(new_name);
        }
        if (saved_reversed) {
            q.patch_reversed.erase(new_name);
            q.patch_reversed.insert(old_name);
        }
        if (renamed_pc_dir) {
            rename_path(new_pc, old_pc);
        }
        if (copied_patch_file) {
            delete_file(new_file);
        }
        return 1;
    }

    q.series = std::move(new_series);
    q.applied = std::move(new_applied);

    out_line("Fork of patch " + old_name +
             " created as " + new_name);
    return 0;
}

int cmd_upgrade(QuiltState &, int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            out_line("Usage: quilt upgrade");
            out_line("");
            out_line("Upgrade the metadata in the .pc/ directory from version 1 to");
            out_line("version 2. This command does nothing because quilt.cpp only");
            out_line("supports the version 2 format.");
            return 0;
        }
        if (arg[0] == '-') {
            err("Unrecognized option: "); err_line(arg);
            return 1;
        }
    }
    return 0;
}

// === src/cmd_graph.cpp ===

// This is free and unencumbered software released into the public domain.
#include <cmath>
#include <iomanip>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>

namespace {

struct LineRanges {
    bool computed = false;
    std::vector<int> left;
    std::vector<int> right;
};

struct GraphNode {
    int number = 0;
    std::string name;
    std::map<std::string, LineRanges> files;
    std::vector<std::string> attrs;
};

struct EdgeData {
    std::vector<std::string> names;
};

using EdgeKey = std::pair<int, int>;

static bool is_number(std::string_view value) {
    if (value.empty()) return false;
    for (char c : value) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

static bool is_zero_length_file(std::string_view path) {
    return file_exists(path) && read_file(path).empty();
}

static std::string dot_escape(std::string_view text) {
    std::string escaped;
    escaped.reserve(checked_cast<size_t>(std::ssize(text)));
    for (char c : text) {
        if (c == '\\' || c == '"') {
            escaped += '\\';
            escaped += c;
        } else if (c == '\n') {
            escaped += "\\n";
        } else {
            escaped += c;
        }
    }
    return escaped;
}

static int parse_hunk_count(const std::ssub_match &match) {
    if (!match.matched || match.str().empty()) {
        return 1;
    }
    return checked_cast<int>(parse_int(match.str()));
}

static LineRanges parse_ranges(std::string_view diff_text) {
    static const std::regex hunk_regex(
        R"(^@@ -([0-9]+)(?:,([0-9]+))? \+([0-9]+)(?:,([0-9]+))? @@)");

    LineRanges ranges;
    ranges.computed = true;
    for (const auto &line : split_lines(diff_text)) {
        std::smatch match;
        if (!std::regex_search(line, match, hunk_regex)) continue;

        int old_start = checked_cast<int>(parse_int(match[1].str()));
        int old_count = parse_hunk_count(match[2]);
        int new_start = checked_cast<int>(parse_int(match[3].str()));
        int new_count = parse_hunk_count(match[4]);

        ranges.left.push_back(new_start);
        ranges.left.push_back(new_start + new_count);
        ranges.right.push_back(old_start);
        ranges.right.push_back(old_start + old_count);
    }
    return ranges;
}

static std::optional<int> next_node_for_file(const std::vector<GraphNode> &nodes,
                                       int index,
                                       std::string_view file) {
    for (int i = index + 1; i < std::ssize(nodes); ++i) {
        if (nodes[checked_cast<size_t>(i)].files.contains(std::string(file))) {
            return i;
        }
    }
    return std::nullopt;
}

static void compute_ranges(const QuiltState &q,
                    std::vector<GraphNode> &nodes,
                    int index,
                    std::string_view file,
                    int context_lines) {
    auto it = nodes[checked_cast<size_t>(index)].files.find(std::string(file));
    if (it == nodes[checked_cast<size_t>(index)].files.end() || it->second.computed) return;

    LineRanges &ranges = it->second;
    ranges.computed = true;

    std::string old_path = path_join(pc_patch_dir(q, nodes[checked_cast<size_t>(index)].name), file);
    std::string new_path;
    auto next = next_node_for_file(nodes, index, file);
    if (next.has_value()) {
        new_path = path_join(pc_patch_dir(q, nodes[checked_cast<size_t>(*next)].name), file);
    } else {
        new_path = path_join(q.work_dir, file);
    }

    bool old_missing = is_zero_length_file(old_path);
    bool new_missing = is_zero_length_file(new_path);
    if (old_missing && new_missing) {
        return;
    }

    DiffResult diff = builtin_diff(
        old_missing ? "/dev/null" : std::string_view(old_path),
        new_missing ? "/dev/null" : std::string_view(new_path),
        context_lines);
    if (diff.exit_code == 0) {
        return;
    }

    ranges = parse_ranges(diff.output);
}

static bool is_conflict(const QuiltState &q,
                 std::vector<GraphNode> &nodes,
                 int from,
                 int to,
                 std::string_view file,
                 int context_lines) {
    compute_ranges(q, nodes, from, file, context_lines);
    compute_ranges(q, nodes, to, file, context_lines);

    const auto file_key = std::string(file);
    const auto &a = nodes[checked_cast<size_t>(from)].files[file_key].right;
    const auto &b = nodes[checked_cast<size_t>(to)].files[file_key].left;

    ptrdiff_t ia = 0;
    ptrdiff_t ib = 0;
    while (ia < std::ssize(a) && ib < std::ssize(b)) {
        ptrdiff_t rem_a = std::ssize(a) - ia;
        ptrdiff_t rem_b = std::ssize(b) - ib;
        if (a[checked_cast<size_t>(ia)] < b[checked_cast<size_t>(ib)]) {
            if ((rem_b % 2) == 1) return true;
            ++ia;
        } else if (a[checked_cast<size_t>(ia)] > b[checked_cast<size_t>(ib)]) {
            if ((rem_a % 2) == 1) return true;
            ++ib;
        } else {
            if ((rem_a % 2) == (rem_b % 2)) return true;
            ++ia;
            ++ib;
        }
    }
    return false;
}

static void add_edge(std::map<EdgeKey, EdgeData> &edges,
              int earlier,
              int later,
              std::string_view file) {
    auto &edge = edges[{earlier, later}];
    edge.names.emplace_back(file);
}

static std::set<int> collect_reachable(const std::map<EdgeKey, EdgeData> &edges,
                                int start,
                                bool forward) {
    std::map<int, std::vector<int>> adjacency;
    for (const auto &[key, value] : edges) {
        (void)value;
        int from = key.first;
        int to = key.second;
        if (forward) {
            adjacency[from].push_back(to);
        } else {
            adjacency[to].push_back(from);
        }
    }

    std::set<int> seen;
    std::vector<int> stack = {start};
    while (!stack.empty()) {
        int node = stack.back();
        stack.pop_back();
        if (!seen.insert(node).second) continue;
        auto it = adjacency.find(node);
        if (it == adjacency.end()) continue;
        for (int next : it->second) {
            stack.push_back(next);
        }
    }
    return seen;
}

static bool has_alternate_path(int from,
                        int to,
                        const std::map<int, std::vector<int>> &adjacency,
                        EdgeKey skip_edge) {
    std::vector<int> stack = {from};
    std::set<int> seen;
    while (!stack.empty()) {
        int node = stack.back();
        stack.pop_back();
        if (!seen.insert(node).second) continue;

        auto it = adjacency.find(node);
        if (it == adjacency.end()) continue;
        for (int next : it->second) {
            if (EdgeKey{node, next} == skip_edge) continue;
            if (next == to) return true;
            stack.push_back(next);
        }
    }
    return false;
}

static void reduce_edges(std::map<EdgeKey, EdgeData> &edges) {
    std::map<int, std::vector<int>> adjacency;
    for (const auto &[key, value] : edges) {
        (void)value;
        adjacency[key.first].push_back(key.second);
    }

    std::vector<EdgeKey> to_remove;
    for (const auto &[key, value] : edges) {
        (void)value;
        if (has_alternate_path(key.first, key.second, adjacency, key)) {
            to_remove.push_back(key);
        }
    }

    for (const auto &key : to_remove) {
        edges.erase(key);
    }
}

static std::string format_len_attr(int from, int to) {
    std::ostringstream value;
    value << std::fixed << std::setprecision(2)
          << std::log(static_cast<double>(std::abs(to - from) + 3));
    return "len=\"" + value.str() + "\"";
}

static std::string render_dot(const std::vector<GraphNode> &nodes,
                       std::map<EdgeKey, EdgeData> edges,
                       std::set<int> used_nodes,
                       bool reduce,
                       bool edge_labels) {
    if (reduce) {
        reduce_edges(edges);
        used_nodes.clear();
        for (const auto &[key, value] : edges) {
            (void)value;
            used_nodes.insert(key.first);
            used_nodes.insert(key.second);
        }
        // Preserve the selected node (style=bold) even if it has no edges
        for (const auto &node : nodes) {
            for (const auto &attr : node.attrs) {
                if (attr == "style=bold") {
                    used_nodes.insert(node.number);
                    break;
                }
            }
        }
    }

    std::string dot = "digraph dependencies {\n";
    for (const auto &node : nodes) {
        if (!used_nodes.contains(node.number)) continue;

        std::vector<std::string> attrs = node.attrs;
        attrs.push_back("label=\"" + dot_escape(node.name) + "\"");

        dot += "\tn" + std::to_string(node.number);
        if (!attrs.empty()) {
            dot += " [";
            for (ptrdiff_t i = 0; i < std::ssize(attrs); ++i) {
                if (i != 0) dot += ",";
                dot += attrs[checked_cast<size_t>(i)];
            }
            dot += "]";
        }
        dot += ";\n";
    }

    for (auto &[key, edge] : edges) {
        std::ranges::sort(edge.names);
        auto [first, last] = std::ranges::unique(edge.names);
        edge.names.erase(first, last);

        std::vector<std::string> attrs;
        if (edge_labels && !edge.names.empty()) {
            std::string label;
            for (ptrdiff_t i = 0; i < std::ssize(edge.names); ++i) {
                if (i != 0) label += "\\n";
                label += dot_escape(edge.names[checked_cast<size_t>(i)]);
            }
            attrs.push_back("label=\"" + label + "\"");
        }
        attrs.push_back(format_len_attr(key.first, key.second));

        dot += "\tn" + std::to_string(key.first) + " -> n" + std::to_string(key.second);
        dot += " [";
        for (ptrdiff_t i = 0; i < std::ssize(attrs); ++i) {
            if (i != 0) dot += ",";
            dot += attrs[checked_cast<size_t>(i)];
        }
        dot += "];\n";
    }

    dot += "}\n";
    return dot;
}

} // namespace

int cmd_graph(QuiltState &q, int argc, char **argv) {
    bool opt_all = false;
    bool opt_reduce = false;
    bool opt_edge_labels = false;
    std::optional<int> opt_lines;
    std::string_view patch_arg;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--all") {
            opt_all = true;
        } else if (arg == "--reduce") {
            opt_reduce = true;
        } else if (arg == "--lines") {
            opt_lines = 2;
            if (i + 1 < argc && is_number(argv[i + 1])) {
                opt_lines = checked_cast<int>(parse_int(argv[++i]));
            }
        } else if (arg.starts_with("--lines=")) {
            std::string value(arg.substr(8));
            if (!is_number(value)) {
                err_line("Usage: quilt graph [--all] [--reduce] [--lines[=num]] [--edge-labels=files] [-T ps] [patch]");
                return 1;
            }
            opt_lines = checked_cast<int>(parse_int(value));
        } else if (arg == "--edge-labels") {
            if (i + 1 >= argc || std::string_view(argv[i + 1]) != "files") {
                err_line("Usage: quilt graph [--all] [--reduce] [--lines[=num]] [--edge-labels=files] [-T ps] [patch]");
                return 1;
            }
            opt_edge_labels = true;
            ++i;
        } else if (arg == "--edge-labels=files") {
            opt_edge_labels = true;
        } else if (arg == "-T") {
            if (i + 1 >= argc || std::string_view(argv[i + 1]) != "ps") {
                err_line("Usage: quilt graph [--all] [--reduce] [--lines[=num]] [--edge-labels=files] [-T ps] [patch]");
                return 1;
            }
            ++i;
            err_line("quilt graph -T ps: not implemented");
            return 1;
        } else if (arg == "-Tps") {
            err_line("quilt graph -T ps: not implemented");
            return 1;
        } else if (!arg.empty() && arg[0] == '-') {
            err_line("Usage: quilt graph [--all] [--reduce] [--lines[=num]] [--edge-labels=files] [-T ps] [patch]");
            return 1;
        } else if (!patch_arg.empty()) {
            err_line("Usage: quilt graph [--all] [--reduce] [--lines[=num]] [--edge-labels=files] [-T ps] [patch]");
            return 1;
        } else {
            patch_arg = strip_patches_prefix(q, arg);
        }
    }

    if (!patch_arg.empty() && opt_all) {
        err_line("Usage: quilt graph [--all] [--reduce] [--lines[=num]] [--edge-labels=files] [-T ps] [patch]");
        return 1;
    }

    std::string_view selected_patch;
    if (!opt_all) {
        if (q.applied.empty()) {
            if (!q.series_file_exists) {
                err_line("No series file found");
            } else if (q.series.empty()) {
                err_line("No patches in series");
            } else {
                err_line("No patches applied");
            }
            return 1;
        }

        selected_patch = patch_arg.empty() ? std::string_view(q.applied.back()) : patch_arg;
        if (!q.find_in_series(selected_patch).has_value()) {
            err("Patch "); err(selected_patch); err_line(" is not in series");
            return 1;
        }
        if (!q.is_applied(selected_patch)) {
            err("Patch "); err(selected_patch); err_line(" is not applied");
            return 1;
        }
    } else if (q.applied.empty()) {
        err_line("No patches applied");
        return 1;
    }

    std::vector<GraphNode> nodes;
    nodes.reserve(checked_cast<size_t>(std::ssize(q.applied)));
    for (ptrdiff_t i = 0; i < std::ssize(q.applied); ++i) {
        const std::string &patch = q.applied[checked_cast<size_t>(i)];
        auto files = files_in_patch(q, patch);
        std::ranges::sort(files);

        GraphNode node;
        node.number = checked_cast<int>(i);
        node.name = patch;
        for (const auto &file : files) {
            node.files.emplace(file, LineRanges{});
        }
        nodes.push_back(std::move(node));
    }

    std::set<int> used_nodes;
    if (!selected_patch.empty()) {
        auto selected = std::ranges::find_if(nodes,
                                           [&](const GraphNode &node) {
                                               return node.name == selected_patch;
                                           });
        if (selected == nodes.end()) {
            err("Patch "); err(selected_patch); err_line(" is not applied");
            return 1;
        }

        selected->attrs.push_back("style=bold");
        selected->attrs.push_back("color=grey");

        std::set<std::string> selected_files;
        for (const auto &[file, ranges] : selected->files) {
            (void)ranges;
            selected_files.insert(file);
        }
        for (auto &node : nodes) {
            for (auto it = node.files.begin(); it != node.files.end(); ) {
                if (!selected_files.contains(it->first)) {
                    it = node.files.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    std::map<std::string, std::vector<int>> files_seen;
    std::map<EdgeKey, EdgeData> edges;
    for (auto &node : nodes) {
        for (const auto &[file, ranges] : node.files) {
            (void)ranges;
            auto seen_it = files_seen.find(file);
            if (seen_it != files_seen.end()) {
                std::optional<int> dependency;
                if (opt_lines.has_value()) {
                    for (auto prev = seen_it->second.rbegin();
                         prev != seen_it->second.rend();
                         ++prev) {
                        if (is_conflict(q, nodes, node.number, *prev, file, *opt_lines)) {
                            dependency = *prev;
                            break;
                        }
                    }
                } else {
                    dependency = seen_it->second.back();
                }

                if (dependency.has_value()) {
                    add_edge(edges, *dependency, node.number, file);
                    used_nodes.insert(*dependency);
                    used_nodes.insert(node.number);
                }
            }
            files_seen[file].push_back(node.number);
        }
    }

    if (!selected_patch.empty()) {
        int selected_index = -1;
        for (const auto &node : nodes) {
            if (node.name == selected_patch) {
                selected_index = node.number;
                break;
            }
        }

        std::set<int> reachable = collect_reachable(edges, selected_index, true);
        std::set<int> reverse = collect_reachable(edges, selected_index, false);
        reachable.insert(reverse.begin(), reverse.end());

        for (auto it = edges.begin(); it != edges.end(); ) {
            if (!reachable.contains(it->first.first) ||
                !reachable.contains(it->first.second)) {
                it = edges.erase(it);
            } else {
                ++it;
            }
        }
        used_nodes = std::move(reachable);
    }

    std::string dot = render_dot(nodes, edges, used_nodes, opt_reduce, opt_edge_labels);
    out(dot);
    return 0;
}

// === src/cmd_mail.cpp ===

// This is free and unencumbered software released into the public domain.

#include <cstdio>



static std::string extract_diff(std::string_view content) {
    auto lines = split_lines(content);
    std::string diff;
    bool in_diff = false;
    for (const auto &line : lines) {
        if (!in_diff) {
            if (line.starts_with("Index:") ||
                line.starts_with("--- ") ||
                line.starts_with("diff ") ||
                line.starts_with("===")) {
                in_diff = true;
            }
        }
        if (in_diff) {
            diff += line;
            diff += '\n';
        }
    }
    return diff;
}

static bool has_non_ascii(std::string_view s) {
    for (char ch : s) {
        if (static_cast<unsigned char>(ch) > 127) return true;
    }
    return false;
}

// RFC 2047 quoted-printable encoding for a header value
static std::string rfc2047_encode(std::string_view s) {
    // Encode as =?UTF-8?q?...?=
    // Characters that must be encoded: non-ASCII, =, ?, _, space
    std::string result = "=?UTF-8?q?";
    ptrdiff_t line_len = 10; // length of "=?UTF-8?q?"
    for (char ch : s) {
        auto c = static_cast<unsigned char>(ch);
        std::string encoded;
        if (c == '=' || c == '?' || c == '_' || c == ' ' || c > 127) {
            static constexpr char hex[] = "0123456789ABCDEF";
            encoded = {'=', hex[c >> 4], hex[c & 0xf]};
        } else {
            encoded = std::string(1, ch);
        }
        // Line wrap: if adding this would exceed ~75 chars, close and start new encoded word
        if (line_len + std::ssize(encoded) + 2 > 75) { // 2 for "?="
            result += "?=\n =?UTF-8?q?";
            line_len = 12; // " =?UTF-8?q?"
        }
        result += encoded;
        line_len += std::ssize(encoded);
    }
    result += "?=";
    return result;
}

// Format RFC 2822 date
static std::string format_rfc2822_date(int64_t t) {
    DateTime dt = local_time(t);

    int tz_hours = dt.utc_offset / 3600;
    int tz_mins = (dt.utc_offset % 3600) / 60;
    if (tz_mins < 0) tz_mins = -tz_mins;

    static constexpr const char *days[] =
        {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static constexpr const char *months[] =
        {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    return std::format("{}, {} {} {} {:02d}:{:02d}:{:02d} {:+03d}{:02d}",
                       days[dt.weekday],
                       dt.day,
                       months[dt.month - 1],
                       dt.year,
                       dt.hour, dt.min, dt.sec,
                       tz_hours, tz_mins);
}

// FNV-1a 64-bit hash
static uint64_t fnv1a_64(std::string_view data) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (char ch : data)
        h = (h ^ static_cast<uint64_t>(static_cast<unsigned char>(ch))) * 0x100000001b3ULL;
    return h;
}

// Generate a Message-ID
static std::string make_message_id(int64_t t, int seq,
                                   std::string_view from,
                                   std::string_view content) {
    // Extract domain from the from address
    std::string domain = "localhost";
    auto at = str_find(from, '@');
    if (at >= 0) {
        auto end = str_find(from, '>', at);
        if (end < 0) end = std::ssize(from);
        domain = std::string(from.substr(checked_cast<size_t>(at + 1), checked_cast<size_t>(end - at - 1)));
    }

    uint64_t h = fnv1a_64(content);
    return std::format("<{}.{}.{:016x}.{}@{}>", t, seq, h, current_time(), domain);
}

// Compute the width needed for zero-padded patch numbers
static int num_width(int n) {
    if (n < 10) return 1;
    if (n < 100) return 2;
    if (n < 1000) return 3;
    return 4;
}

int cmd_mail(QuiltState &q, int argc, char **argv) {
    std::string mbox_file;
    std::string from_addr;
    std::string sender_addr;
    std::string prefix = "PATCH";
    std::vector<std::string> to_addrs;
    std::vector<std::string> cc_addrs;
    std::vector<std::string> bcc_addrs;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--mbox" && i + 1 < argc) {
            mbox_file = argv[++i];
        } else if (arg == "--send") {
            err_line("quilt mail: send mode is not supported; use --mbox");
            return 1;
        } else if (arg == "--sender" && i + 1 < argc) {
            sender_addr = argv[++i];
        } else if (arg == "--from" && i + 1 < argc) {
            from_addr = argv[++i];
        } else if (arg == "--prefix" && i + 1 < argc) {
            prefix = argv[++i];
        } else if (arg == "--to" && i + 1 < argc) {
            to_addrs.emplace_back(argv[++i]);
        } else if (arg == "--cc" && i + 1 < argc) {
            cc_addrs.emplace_back(argv[++i]);
        } else if (arg == "--bcc" && i + 1 < argc) {
            bcc_addrs.emplace_back(argv[++i]);
        } else if (arg == "--subject" && i + 1 < argc) {
            ++i; // consume and ignore (cover letter not generated)
        } else if (arg == "-m" && i + 1 < argc) {
            ++i; // consume and ignore (cover letter not generated)
        } else if (arg == "-M" && i + 1 < argc) {
            ++i; // consume and ignore (cover letter not generated)
        } else if (arg == "--reply-to" && i + 1 < argc) {
            ++i; // consume and ignore (cover letter not generated)
        } else if (arg == "--charset" && i + 1 < argc) {
            ++i; // consume and ignore
        } else if (arg == "--signature" && i + 1 < argc) {
            ++i; // consume and ignore
        } else if (arg == "-h" || arg == "--help") {
            out_line("Usage: quilt mail {--mbox file} [--prefix prefix] "
                     "[--sender ...] [--from ...] [--to ...] [--cc ...] "
                     "[--bcc ...] [first_patch [last_patch]]");
            return 0;
        } else if (arg[0] != '-' || arg == "-") {
            positional.emplace_back(arg);
        } else {
            err("quilt mail: unknown option: ");
            err_line(arg);
            return 1;
        }
    }

    if (mbox_file.empty()) {
        err_line("quilt mail: --mbox is required");
        return 1;
    }

    // Determine From address
    std::string effective_from = from_addr;
    if (effective_from.empty()) {
        effective_from = sender_addr;
    }
    if (effective_from.empty()) {
        err_line("quilt mail: --from or --sender is required");
        return 1;
    }

    if (q.series.empty()) {
        err_line("No patches in series");
        return 1;
    }

    // Resolve patch range
    ptrdiff_t first_idx = 0;
    ptrdiff_t last_idx = std::ssize(q.series) - 1;

    if (std::ssize(positional) == 1) {
        // Single patch
        std::string name = positional[0];
        if (name == "-") {
            // "-" as single arg means all patches
        } else {
            auto idx = q.find_in_series(name);
            if (!idx) {
                err_line("Patch " + name + " is not in series");
                return 1;
            }
            first_idx = *idx;
            last_idx = *idx;
        }
    } else if (std::ssize(positional) == 2) {
        std::string first_name = positional[0];
        std::string last_name = positional[1];

        if (first_name == "-") {
            first_idx = 0;
        } else {
            auto idx = q.find_in_series(first_name);
            if (!idx) {
                err_line("Patch " + first_name + " is not in series");
                return 1;
            }
            first_idx = *idx;
        }

        if (last_name == "-") {
            last_idx = std::ssize(q.series) - 1;
        } else {
            auto idx = q.find_in_series(last_name);
            if (!idx) {
                err_line("Patch " + last_name + " is not in series");
                return 1;
            }
            last_idx = *idx;
        }

        if (first_idx > last_idx) {
            err_line("quilt mail: first patch must come before last patch in series");
            return 1;
        }
    } else if (std::ssize(positional) > 2) {
        err_line("Usage: quilt mail {--mbox file} [options] [first_patch [last_patch]]");
        return 1;
    }

    ptrdiff_t total = last_idx - first_idx + 1;
    int width = num_width(checked_cast<int>(total));

    std::string mbox;

    for (ptrdiff_t i = first_idx; i <= last_idx; ++i) {
        const std::string &patch = q.series[checked_cast<size_t>(i)];
        std::string patch_file = path_join(q.work_dir, q.patches_dir, patch);
        std::string content = read_file(patch_file);

        if (content.empty()) {
            err("Warning: patch ");
            err(patch);
            err_line(" is empty, skipping");
            continue;
        }

        // Extract header and diff
        std::string header = extract_header(content);
        std::string diff = extract_diff(content);

        // Split header into subject (first line) and body (rest)
        std::string subject_text;
        std::string body;
        if (!header.empty()) {
            auto hdr_lines = split_lines(header);
            // Find first non-empty line for subject
            ptrdiff_t subj_line = 0;
            while (subj_line < std::ssize(hdr_lines) && trim(hdr_lines[checked_cast<size_t>(subj_line)]).empty()) {
                subj_line++;
            }
            if (subj_line < std::ssize(hdr_lines)) {
                subject_text = trim(hdr_lines[checked_cast<size_t>(subj_line)]);
                // Remaining lines become body
                for (ptrdiff_t j = subj_line + 1; j < std::ssize(hdr_lines); ++j) {
                    body += hdr_lines[checked_cast<size_t>(j)];
                    body += '\n';
                }
            }
        }

        // If no header at all, use patch name as subject
        if (subject_text.empty()) {
            subject_text = patch;
        }

        // Build Subject with prefix
        std::string subject_prefix;
        if (total == 1) {
            subject_prefix = "[" + prefix + "]";
        } else {
            int seq = checked_cast<int>(i - first_idx + 1);
            subject_prefix = std::format("[{} {:0{}d}/{}]",
                                         prefix, seq, width, checked_cast<int>(total));
        }

        std::string full_subject = subject_prefix + " " + subject_text;

        // RFC 2047 encode subject if needed
        std::string subject_header;
        if (has_non_ascii(full_subject)) {
            subject_header = "Subject: " + rfc2047_encode(full_subject);
        } else {
            subject_header = "Subject: " + full_subject;
        }

        int64_t msg_time = file_mtime(patch_file);
        if (msg_time == -1) {
            msg_time = current_time();
        }
        int seq = checked_cast<int>(i - first_idx + 1);

        // Build message
        std::string msg;

        // Mbox separator
        msg += "From 0000000000000000000000000000000000000000 Mon Sep 17 00:00:00 2001\n";

        // From header
        msg += "From: " + effective_from + "\n";

        // Date header
        msg += "Date: " + format_rfc2822_date(msg_time) + "\n";

        // Subject header
        msg += subject_header + "\n";

        // Message-ID
        msg += "Message-ID: " + make_message_id(msg_time, seq, effective_from, content) + "\n";

        // MIME headers if non-ASCII in body
        if (has_non_ascii(header) || has_non_ascii(diff) || has_non_ascii(full_subject)) {
            msg += "MIME-Version: 1.0\n";
            msg += "Content-Type: text/plain; charset=UTF-8\n";
            msg += "Content-Transfer-Encoding: 8bit\n";
        }

        // Optional To/Cc/Bcc
        for (const auto &addr : to_addrs) {
            msg += "To: " + addr + "\n";
        }
        for (const auto &addr : cc_addrs) {
            msg += "Cc: " + addr + "\n";
        }
        for (const auto &addr : bcc_addrs) {
            msg += "Bcc: " + addr + "\n";
        }

        // Blank line separating headers from body
        msg += "\n";

        // Body (remaining header text)
        if (!body.empty()) {
            // Trim leading blank lines from body
            std::string_view bv = body;
            while (bv.starts_with("\n")) {
                bv = bv.substr(1);
            }
            if (!bv.empty()) {
                msg += bv;
                if (bv.back() != '\n') {
                    msg += '\n';
                }
            }
        }

        // Blank line between body and diff (when no diffstat separator)
        if (!msg.empty() && msg.back() == '\n' &&
            (msg.size() < 2 || msg[msg.size() - 2] != '\n')) {
            msg += '\n';
        }

        // Diff content
        if (!diff.empty()) {
            msg += diff;
            if (diff.back() != '\n') {
                msg += '\n';
            }
        }

        // Trailer (like git's "-- \n2.53.0\n")
        msg += "-- \nquilt\n\n";

        mbox += msg;
    }

    if (!write_file(mbox_file, mbox)) {
        err_line("Failed to write mbox file: " + mbox_file);
        return 1;
    }

    out("Wrote ");
    out(std::to_string(total));
    out(" patch");
    if (total != 1) out("es");
    out(" to ");
    out_line(mbox_file);

    return 0;
}

// === src/cmd_stubs.cpp ===

// This is free and unencumbered software released into the public domain.

static int not_implemented(const char *name)
{
    err("quilt ");
    err(name);
    err_line(": not implemented");
    return 1;
}

int cmd_grep(QuiltState &, int, char **)     { return not_implemented("grep"); }
int cmd_setup(QuiltState &, int, char **)    { return not_implemented("setup"); }
int cmd_shell(QuiltState &, int, char **)    { return not_implemented("shell"); }

// === src/platform_win32.cpp ===

// This is free and unencumbered software released into the public domain.
//
// Win32 platform implementation for quilt.
// Provides main entry point, UTF-16 <-> UTF-8 conversion at system
// boundaries, and Win32 implementations of the platform interface.
//
// This file is compiled only on Windows.  POSIX builds use
// platform_posix.cpp instead.  No #ifdef guards -- the build system
// selects exactly one platform source.


#ifndef _WIN32
#error "platform_win32.cpp must only be compiled on Windows"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

static std::wstring utf8_to_wide(std::string_view s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                  checked_cast<int>(std::ssize(s)), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), checked_cast<int>(std::ssize(s)),
                        out.data(), len);
    return out;
}

static std::string wide_to_utf8(const wchar_t *w, int wlen = -1)
{
    if (!w) return {};
    if (wlen < 0) wlen = checked_cast<int>(static_cast<ptrdiff_t>(wcslen(w)));
    if (wlen == 0) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, wlen,
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, wlen,
                        out.data(), len, nullptr, nullptr);
    return out;
}

static std::string read_handle(HANDLE h)
{
    std::string result;
    char buf[4096];
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(h, buf, sizeof(buf), &n, nullptr) || n == 0)
            break;
        result.append(buf, n);
    }
    return result;
}

static bool write_handle(HANDLE h, const void *data, size_t len)
{
    const char *p = static_cast<const char *>(data);
    while (len > 0) {
        DWORD written = 0;
        if (!WriteFile(h, p, (DWORD)len, &written, nullptr))
            return false;
        p   += written;
        len -= written;
    }
    return true;
}

// Write a UTF-8 string to a handle.  When the handle is a console,
// convert to UTF-16 and use WriteConsoleW so that non-ASCII text
// displays correctly.  For pipes/files, write raw UTF-8 bytes.
static bool write_console_or_file(HANDLE h, std::string_view s)
{
    DWORD mode;
    if (GetConsoleMode(h, &mode)) {
        std::wstring wide = utf8_to_wide(s);
        const wchar_t *p = wide.data();
        DWORD remaining = checked_cast<DWORD>(std::ssize(wide));
        while (remaining > 0) {
            DWORD written = 0;
            if (!WriteConsoleW(h, p, remaining, &written, nullptr))
                return false;
            p         += written;
            remaining -= written;
        }
        return true;
    }
    return write_handle(h, s.data(), s.size());
}

// Create an inheritable pipe.  read_end and write_end are set.
// inherit_which: 0 = read end inheritable, 1 = write end inheritable
static bool create_pipe(HANDLE &read_end, HANDLE &write_end,
                        int inherit_which)
{
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&read_end, &write_end, &sa, 0))
        return false;

    // Make the non-inherited end non-inheritable
    HANDLE &non_inherit = (inherit_which == 0) ? write_end : read_end;
    SetHandleInformation(non_inherit, HANDLE_FLAG_INHERIT, 0);
    return true;
}

static std::wstring build_cmdline(const std::vector<std::string> &argv)
{
    // Windows command-line quoting: wrap each arg in quotes, escape
    // internal quotes and backslashes before quotes.
    std::wstring cmdline;
    for (ptrdiff_t i = 0; i < std::ssize(argv); ++i) {
        if (i > 0) cmdline += L' ';

        std::wstring arg = utf8_to_wide(argv[checked_cast<size_t>(i)]);

        // Check if quoting is needed
        bool needs_quote = arg.empty();
        for (wchar_t c : arg) {
            if (c == L' ' || c == L'\t' || c == L'"') {
                needs_quote = true;
                break;
            }
        }

        if (!needs_quote) {
            cmdline += arg;
            continue;
        }

        cmdline += L'"';
        int num_backslashes = 0;
        for (wchar_t c : arg) {
            if (c == L'\\') {
                ++num_backslashes;
            } else if (c == L'"') {
                // Escape preceding backslashes and the quote
                for (int j = 0; j < num_backslashes; ++j)
                    cmdline += L'\\';
                cmdline += L'\\';
                cmdline += L'"';
                num_backslashes = 0;
            } else {
                num_backslashes = 0;
                cmdline += c;
            }
        }
        // Escape trailing backslashes before closing quote
        for (int j = 0; j < num_backslashes; ++j)
            cmdline += L'\\';
        cmdline += L'"';
    }
    return cmdline;
}

static ProcessResult run_cmd_impl(const std::vector<std::string> &argv,
                                  const char *stdin_data, size_t stdin_len)
{
    ProcessResult result{};

    if (argv.empty()) {
        result.exit_code = -1;
        return result;
    }

    // Create pipes for stdout, stderr, and optionally stdin
    HANDLE stdout_rd = INVALID_HANDLE_VALUE, stdout_wr = INVALID_HANDLE_VALUE;
    HANDLE stderr_rd = INVALID_HANDLE_VALUE, stderr_wr = INVALID_HANDLE_VALUE;
    HANDLE stdin_rd  = INVALID_HANDLE_VALUE, stdin_wr  = INVALID_HANDLE_VALUE;

    if (!create_pipe(stdout_rd, stdout_wr, 1)) {  // write end inheritable
        result.exit_code = -1;
        return result;
    }
    if (!create_pipe(stderr_rd, stderr_wr, 1)) {
        CloseHandle(stdout_rd); CloseHandle(stdout_wr);
        result.exit_code = -1;
        return result;
    }

    bool need_stdin = (stdin_data != nullptr);
    if (need_stdin) {
        if (!create_pipe(stdin_rd, stdin_wr, 0)) {  // read end inheritable
            CloseHandle(stdout_rd); CloseHandle(stdout_wr);
            CloseHandle(stderr_rd); CloseHandle(stderr_wr);
            result.exit_code = -1;
            return result;
        }
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdout_wr;
    si.hStdError  = stderr_wr;
    si.hStdInput  = need_stdin ? stdin_rd : GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};

    std::wstring cmdline = build_cmdline(argv);

    BOOL ok = CreateProcessW(
        nullptr,                               // lpApplicationName
        cmdline.data(),                        // lpCommandLine (mutable)
        nullptr, nullptr,                      // process/thread security
        TRUE,                                  // inherit handles
        CREATE_NO_WINDOW,                      // creation flags
        nullptr,                               // environment
        nullptr,                               // current directory
        &si, &pi
    );

    // Close child-side handles in parent
    CloseHandle(stdout_wr);
    CloseHandle(stderr_wr);
    if (need_stdin) CloseHandle(stdin_rd);

    if (!ok) {
        result.exit_code = -1;
        result.err = "CreateProcessW failed: " + std::to_string(GetLastError());
        CloseHandle(stdout_rd);
        CloseHandle(stderr_rd);
        if (need_stdin) CloseHandle(stdin_wr);
        return result;
    }

    // Write stdin data
    if (need_stdin) {
        write_handle(stdin_wr, stdin_data, stdin_len);
        CloseHandle(stdin_wr);
    }

    // Read stdout and stderr
    result.out = read_handle(stdout_rd);
    result.err = read_handle(stderr_rd);
    CloseHandle(stdout_rd);
    CloseHandle(stderr_rd);

    // Wait for process
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.exit_code = static_cast<int>(exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return result;
}

ProcessResult run_cmd(const std::vector<std::string> &argv)
{
    return run_cmd_impl(argv, nullptr, 0);
}

ProcessResult run_cmd_input(const std::vector<std::string> &argv,
                            std::string_view stdin_data)
{
    return run_cmd_impl(argv, stdin_data.data(), stdin_data.size());
}

int run_cmd_tty(const std::vector<std::string> &argv)
{
    if (argv.empty()) return -1;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    // No STARTF_USESTDHANDLES — child inherits console

    PROCESS_INFORMATION pi{};
    std::wstring cmdline = build_cmdline(argv);

    BOOL ok = CreateProcessW(
        nullptr, cmdline.data(),
        nullptr, nullptr,
        FALSE, 0,
        nullptr, nullptr,
        &si, &pi
    );
    if (!ok) return -1;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exit_code);
}

std::string read_file(std::string_view path)
{
    std::wstring wpath = utf8_to_wide(path);
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};

    std::string result = read_handle(h);
    CloseHandle(h);
    return result;
}

bool write_file(std::string_view path, std::string_view content)
{
    std::wstring wpath = utf8_to_wide(path);
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_WRITE, 0,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool ok = write_handle(h, content.data(), content.size());
    CloseHandle(h);
    return ok;
}

bool append_file(std::string_view path, std::string_view content)
{
    std::wstring wpath = utf8_to_wide(path);
    HANDLE h = CreateFileW(wpath.c_str(), FILE_APPEND_DATA, 0,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool ok = write_handle(h, content.data(), content.size());
    CloseHandle(h);
    return ok;
}

bool copy_file(std::string_view src, std::string_view dst)
{
    std::wstring wsrc = utf8_to_wide(src);
    std::wstring wdst = utf8_to_wide(dst);
    return CopyFileW(wsrc.c_str(), wdst.c_str(), FALSE) != 0;
}

bool rename_path(std::string_view old_path, std::string_view new_path)
{
    std::wstring wold = utf8_to_wide(old_path);
    std::wstring wnew = utf8_to_wide(new_path);
    return MoveFileExW(wold.c_str(), wnew.c_str(),
                       MOVEFILE_REPLACE_EXISTING) != 0;
}

bool delete_file(std::string_view path)
{
    std::wstring wpath = utf8_to_wide(path);
    return DeleteFileW(wpath.c_str()) != 0;
}

bool delete_dir_recursive(std::string_view path)
{
    std::wstring wpath = utf8_to_wide(path);
    std::wstring pattern = wpath + L"\\*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 ||
            wcscmp(fd.cFileName, L"..") == 0)
            continue;

        std::wstring child = wpath + L"\\" + fd.cFileName;
        std::string child_utf8 = wide_to_utf8(child.c_str(),
                                              checked_cast<int>(std::ssize(child)));

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!delete_dir_recursive(child_utf8)) ok = false;
        } else {
            if (!DeleteFileW(child.c_str())) ok = false;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (!RemoveDirectoryW(wpath.c_str())) ok = false;
    return ok;
}

bool make_dir(std::string_view path)
{
    std::wstring wpath = utf8_to_wide(path);
    if (CreateDirectoryW(wpath.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool make_dirs(std::string_view path)
{
    if (path.empty()) return false;

    std::string p(path);
    // Normalize separators
    for (char &c : p) {
        if (c == '/') c = '\\';
    }

    ptrdiff_t start = 1;
    if (std::ssize(p) >= 3 && std::isalpha(static_cast<unsigned char>(p[0])) &&
        p[1] == ':' && p[2] == '\\') {
        start = 3;
    } else if (std::ssize(p) >= 2 && p[0] == '\\' && p[1] == '\\') {
        // Leave UNC handling to the normal loop after the \\server\share prefix.
        ptrdiff_t slash_count = 0;
        start = 2;
        for (ptrdiff_t i = 2; i < std::ssize(p); ++i) {
            if (p[checked_cast<size_t>(i)] == '\\') {
                ++slash_count;
                if (slash_count == 2) {
                    start = i + 1;
                    break;
                }
            }
        }
    }

    // Walk through path components, creating each.
    for (ptrdiff_t i = start; i < std::ssize(p); ++i) {
        if (p[checked_cast<size_t>(i)] == '\\') {
            p[checked_cast<size_t>(i)] = '\0';
            std::wstring wp = utf8_to_wide(p.c_str());
            if (!CreateDirectoryW(wp.c_str(), nullptr)) {
                if (GetLastError() != ERROR_ALREADY_EXISTS)
                    return false;
            }
            p[checked_cast<size_t>(i)] = '\\';
        }
    }
    std::wstring wp = utf8_to_wide(p);
    if (!CreateDirectoryW(wp.c_str(), nullptr)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
    }
    return true;
}

bool file_exists(std::string_view path)
{
    std::wstring wpath = utf8_to_wide(path);
    DWORD attr = GetFileAttributesW(wpath.c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
}

bool is_directory(std::string_view path)
{
    std::wstring wpath = utf8_to_wide(path);
    DWORD attr = GetFileAttributesW(wpath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

int64_t file_mtime(std::string_view path)
{
    std::wstring wpath = utf8_to_wide(path);
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &data))
        return -1;
    // FILETIME: 100-nanosecond intervals since 1601-01-01
    uint64_t ft = (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32)
                | data.ftLastWriteTime.dwLowDateTime;
    return static_cast<int64_t>((ft - 116444736000000000ULL) / 10000000ULL);
}

std::vector<DirEntry> list_dir(std::string_view path)
{
    std::vector<DirEntry> entries;
    std::wstring wpath = utf8_to_wide(path);
    std::wstring pattern = wpath + L"\\*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return entries;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 ||
            wcscmp(fd.cFileName, L"..") == 0)
            continue;

        DirEntry e;
        e.name   = wide_to_utf8(fd.cFileName);
        e.is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entries.push_back(std::move(e));
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    return entries;
}

static void find_files_impl(const std::wstring &base,
                             const std::string &prefix,
                             std::vector<std::string> &out)
{
    std::wstring pattern = base + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 ||
            wcscmp(fd.cFileName, L"..") == 0)
            continue;

        std::string name = wide_to_utf8(fd.cFileName);
        std::wstring full = base + L"\\" + fd.cFileName;
        std::string rel = prefix.empty() ? name : prefix + "/" + name;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            find_files_impl(full, rel, out);
        } else {
            out.push_back(rel);
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

std::vector<std::string> find_files_recursive(std::string_view dir)
{
    std::vector<std::string> result;
    std::wstring wdir = utf8_to_wide(dir);
    find_files_impl(wdir, "", result);
    return result;
}

std::string make_temp_dir()
{
    wchar_t tmp_path[MAX_PATH + 1];
    if (!GetTempPathW(MAX_PATH + 1, tmp_path)) return {};

    DWORD pid = GetCurrentProcessId();
    static unsigned counter = 0;
    wchar_t tmp_dir[MAX_PATH + 1];
    for (int attempt = 0; attempt < 100; ++attempt) {
        unsigned n = counter++;
        swprintf(tmp_dir, MAX_PATH, L"%sqlt%lu_%u", tmp_path, (unsigned long)pid, n);
        if (CreateDirectoryW(tmp_dir, nullptr)) {
            std::string result = wide_to_utf8(tmp_dir);
            for (char &c : result)
                if (c == '\\') c = '/';
            return result;
        }
    }
    return {};
}

std::string get_env(std::string_view name)
{
    std::wstring wname = utf8_to_wide(name);
    // First call to get required buffer size
    DWORD len = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (len == 0) return {};
    std::wstring buf(len, L'\0');
    GetEnvironmentVariableW(wname.c_str(), buf.data(), len);
    // Remove trailing null
    if (!buf.empty() && buf.back() == L'\0') buf.pop_back();
    return wide_to_utf8(buf.c_str(), checked_cast<int>(std::ssize(buf)));
}

void set_env(std::string_view name, std::string_view value)
{
    std::wstring wname = utf8_to_wide(name);
    std::wstring wvalue = utf8_to_wide(value);
    SetEnvironmentVariableW(wname.c_str(), wvalue.c_str());
}

std::string get_home_dir()
{
    std::string home = get_env("HOME");
    if (!home.empty()) return home;
    std::string up = get_env("USERPROFILE");
    if (!up.empty()) return up;
    std::string hd = get_env("HOMEDRIVE");
    std::string hp = get_env("HOMEPATH");
    if (!hd.empty() && !hp.empty()) return hd + hp;
    return {};
}

std::string get_system_quiltrc()
{
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    std::wstring path(buf, len);
    // Strip exe filename
    auto pos = path.rfind(L'\\');
    if (pos == std::wstring::npos) return {};
    path.resize(pos);
    // Strip parent directory (e.g. bin/)
    pos = path.rfind(L'\\');
    if (pos == std::wstring::npos) return {};
    path.resize(pos);
    path += L"\\etc\\quilt.quiltrc";
    std::string result = wide_to_utf8(path.c_str(),
                                      checked_cast<int>(std::ssize(path)));
    for (char &c : result) {
        if (c == '\\') c = '/';
    }
    return result;
}

std::string get_cwd()
{
    DWORD len = GetCurrentDirectoryW(0, nullptr);
    if (len == 0) return {};
    std::wstring buf(len, L'\0');
    GetCurrentDirectoryW(len, buf.data());
    // Remove trailing null
    if (!buf.empty() && buf.back() == L'\0') buf.pop_back();
    std::string result = wide_to_utf8(buf.c_str(), checked_cast<int>(std::ssize(buf)));
    // Normalize backslashes to forward slashes for consistency
    for (char &c : result) {
        if (c == '\\') c = '/';
    }
    return result;
}

bool set_cwd(std::string_view path)
{
    std::wstring wpath = utf8_to_wide(path);
    return SetCurrentDirectoryW(wpath.c_str()) != 0;
}

void fd_write_stdout(std::string_view s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE)
        write_console_or_file(h, s);
}

void fd_write_stderr(std::string_view s)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h != INVALID_HANDLE_VALUE)
        write_console_or_file(h, s);
}

bool stdout_is_tty()
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    return h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_CHAR;
}

std::string read_stdin()
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return {};
    return read_handle(h);
}

int64_t current_time()
{
    return _time64(nullptr);
}

DateTime local_time(int64_t timestamp)
{
    __time64_t t = static_cast<__time64_t>(timestamp);
    struct tm local_tm, utc_tm;
    _localtime64_s(&local_tm, &t);
    _gmtime64_s(&utc_tm, &t);

    long local_sec = local_tm.tm_hour * 3600L + local_tm.tm_min * 60L + local_tm.tm_sec;
    long utc_sec = utc_tm.tm_hour * 3600L + utc_tm.tm_min * 60L + utc_tm.tm_sec;
    long diff = local_sec - utc_sec;
    int day_diff = local_tm.tm_yday - utc_tm.tm_yday;
    if (day_diff > 1) day_diff = -1;
    if (day_diff < -1) day_diff = 1;
    diff += day_diff * 86400L;

    return {
        local_tm.tm_year + 1900,
        local_tm.tm_mon + 1,
        local_tm.tm_mday,
        local_tm.tm_hour,
        local_tm.tm_min,
        local_tm.tm_sec,
        local_tm.tm_wday,
        static_cast<int>(diff),
    };
}

int main(int, char **)
{
    // Use GetCommandLineW + CommandLineToArgvW for reliable parsing
    int argc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv) return 1;

    // Convert to UTF-8
    std::vector<std::string> args_storage;
    args_storage.reserve(argc);
    for (int i = 0; i < argc; ++i)
        args_storage.push_back(wide_to_utf8(wargv[i]));
    LocalFree(wargv);

    std::vector<char *> argv_ptrs;
    argv_ptrs.reserve(argc + 1);
    for (auto &a : args_storage)
        argv_ptrs.push_back(a.data());
    argv_ptrs.push_back(nullptr);

    return quilt_main(argc, argv_ptrs.data());
}


