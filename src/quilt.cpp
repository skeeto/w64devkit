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
    // Patch after the topmost applied one (the first patch if none is
    // applied), which new patches go in front of; empty at the series end.
    std::string patch_after_top() const;
    bool is_applied(std::string_view patch) const;
    std::optional<ptrdiff_t> find_in_series(std::string_view patch) const;
    int get_strip_level(std::string_view patch) const;  // returns 1 if not set
    std::string get_p_format(std::string_view patch) const;  // strip level as "-p" value
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

// A patch's description and its diff, split as upstream's patch_header and
// patch_body split them, and a description without its diffstat, as
// upstream's strip_diffstat. Like those awk scripts, these keep every byte,
// CRs included, but end a nonempty result with a newline.
std::string patch_header(std::string_view patch);
std::string patch_body(std::string_view patch);
std::string strip_diffstat(std::string_view header);
// A description with its diffstat replaced by (or, if it has none,
// followed by) a new one, as upstream's refresh --diffstat does it
std::string replace_diffstat(std::string_view header, std::string_view diffstat);

// Built-in patch engine.  Like GNU patch given the -f that quilt always
// passes, it never asks questions: it applies what it can and skips
// missing files the patch does not create.
struct PatchOptions {
    int strip_level = 1;       // -pN
    int fuzz = 2;              // --fuzz=N (default 2), see set_fuzz_option
    bool reverse = false;      // -R
    bool dry_run = false;      // --dry-run
    bool remove_empty = false; // -E
    bool quiet = false;        // -s
    bool merge = false;        // --merge
    std::string merge_style;   // "" or "diff3"
    // A bad option, such as a fuzz factor that is not a number, which like
    // GNU patch ends the patch before it touches any file
    std::string option_error;
    // In-memory filesystem for fuzz testing. When non-null, all file I/O
    // in builtin_patch uses this map instead of real syscalls.
    // Key present = file exists, value = content.
    std::map<std::string, std::string> *fs = nullptr;
};

struct PatchResult {
    int exit_code;             // 0=success, 1=rejects, 2=fatal
    // Like GNU patch, every message in order on stdout, and only a fatal
    // error, which ends the patch, on stderr
    std::string out;
    std::string err;
    // Files left alone that GNU patch would not have backed up: missing
    // files the patch does not create, or every file when a bad option
    // ends the patch before it starts
    std::vector<std::string> skipped;
};

PatchResult builtin_patch(std::string_view patch_text, const PatchOptions &opts);

// Set the fuzz factor from a --fuzz option's value, read as GNU patch
// reads it: an optional sign, then digits, and not negative. A factor too
// large for an int is clamped, since no hunk can use more fuzz than it has
// context. A bad value, the first one only, goes in opts.option_error.
void set_fuzz_option(PatchOptions &opts, std::string_view value);
// Set the strip level from a -p option's value, read the same way.
void set_strip_option(PatchOptions &opts, std::string_view value);

// Files builtin_patch would modify, without duplicates, in patch order.
// A deleted file (+++ /dev/null) is named by its --- line.
std::vector<std::string> patch_target_files(std::string_view patch_text,
                                            int strip_level, bool reverse = false);

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

// Default name for a fork of patch, like upstream's next_filename: a
// trailing "-N" ahead of any .diff, .dif, or .patch and compression suffix
// counts up, otherwise "-2" goes there (p.patch -> p-2.patch -> p-3.patch).
std::string next_filename(std::string_view patch);

// Patch lookups, like upstream's functions of the same names. Pass a
// patch argument as given: these strip the patches/ prefix themselves.
// Decide whether an argument was given before stripping, since a bare
// "patches/" is an argument that names no patch, not a request for the
// top patch. On failure, each prints the reason and returns nullopt.
//
// find_patch: the named patch must be in the series.
// find_top_patch: the topmost applied patch, which must be in the series.
// find_patch_in_series: find_patch, except an empty name means the top patch.
// find_applied_patch: find_patch_in_series, and the patch must be applied.
std::optional<std::string> find_patch(const QuiltState &q, std::string_view name);
std::optional<std::string> find_top_patch(const QuiltState &q);
std::optional<std::string> find_patch_in_series(const QuiltState &q, std::string_view name);
std::optional<std::string> find_applied_patch(const QuiltState &q, std::string_view name);

// Whether when, the value of a --color option, is valid. Like upstream, it
// may be empty, always, auto, tty, or never. Quilt.cpp never colors its
// output, so commands discard the option once it checks out.
bool valid_color_value(std::string_view when);

// Command-line options, parsed like the util-linux getopt(1) that upstream
// runs over each command's arguments, QUILT_<CMD>_ARGS first:
//
// - Short options may be grouped (-qa). A value goes attached (-p0) or in
//   the next word (-p 0), whatever that word is. An optional value, as in
//   "z::", only goes attached, and is empty when absent.
// - Long options take a value after "=" (--fuzz=2), or, when required, in
//   the next word (--fuzz 2). A unique prefix names an option (--leave).
//   When an upstream option and a quilt.cpp extension share the prefix,
//   the upstream option wins.
// - Options and operands mix in any order. "--" ends the options, and ""
//   and "-" are operands.
// - Every command takes --help as -h, a quilt.cpp extension.
enum class OptArg : unsigned char { none, required, optional };

struct LongOpt {
    std::string_view name;
    OptArg arg;
    int key;                 // a short option letter for an alias, else >= 256
    bool extension = false;  // quilt.cpp only, so upstream options win ties
};

struct ParsedOption {
    int key;                 // the short option letter or LongOpt::key
    std::string_view value;  // empty when absent
};

struct ParsedArgs {
    std::vector<ParsedOption> options;   // in command-line order
    std::vector<std::string_view> operands;
};

// Parse argv[1..argc), where argv[0] is the command's name. On a bad
// option, print what is wrong and the command's usage, as upstream does,
// and return nullopt, upon which the command exits with status 1.
std::optional<ParsedArgs> parse_options(int argc, char **argv,
                                        std::string_view shortopts,
                                        std::span<const LongOpt> longopts = {});

// Print the command's usage line on stderr, for wrong arguments, and
// return 1, upstream's exit status for them.
int usage_error(std::string_view command);
// Print the command's help on stdout, for -h, and return 0.
int command_help(std::string_view command);

// Resolve a user-provided file path relative to the current subdirectory.
inline std::string subdir_path(const QuiltState &q, std::string_view file) {
    if (q.subdir.empty()) return std::string(file);
    return q.subdir + "/" + std::string(file);
}

// Core helpers — defined in core.cpp
bool ensure_pc_dir(QuiltState &q);
std::string pc_patch_dir(const QuiltState &q, std::string_view patch);
std::vector<std::string> files_in_patch(const QuiltState &q, std::string_view patch);
std::vector<std::string> files_in_patch_ordered(const QuiltState &q, std::string_view patch);
bool backup_file(QuiltState &q, std::string_view patch, std::string_view file);
bool restore_file(QuiltState &q, std::string_view patch, std::string_view file);
std::vector<std::string> read_series(std::string_view path,
                                     std::map<std::string, int> *strip_levels,
                                     std::set<std::string> *reversed);
// Line-preserving series edits, like upstream's insert_in_series,
// remove_from_series, rename_in_series, and change_db_strip_level. Only the
// patch's own line changes, so comments, blank lines, and options on other
// lines survive. Each reloads q.series, q.patch_strip_level, and
// q.patch_reversed from the edited file.
//
// insert_in_series adds "patch opts" in front of before's line, or at the
// end when before is empty. set_series_strip_level records strip_level
// (omitted when 1) and drops -R on patch's line, keeping its other options.
bool insert_in_series(QuiltState &q, std::string_view patch,
                      std::string_view opts, std::string_view before);
bool remove_from_series(QuiltState &q, std::string_view patch);
bool rename_in_series(QuiltState &q, std::string_view from, std::string_view to);
bool set_series_strip_level(QuiltState &q, std::string_view patch,
                            int strip_level);
// The options on patch's series line, without any comment.
std::string series_patch_args(const QuiltState &q, std::string_view patch);
std::vector<std::string> read_applied(std::string_view path);
bool write_applied(std::string_view path, std::span<const std::string> patches);

// Command function type
using CmdFn = int (*)(QuiltState &q, int argc, char **argv);

struct Command {
    const char *name;
    CmdFn       fn;
    const char *synopsis;     // usage line for wrong arguments, as upstream's
    const char *usage;        // full help, for -h
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
// Copies keep the source's modification time, like cp -p
bool copy_file(std::string_view src, std::string_view dst);
bool rename_path(std::string_view old_path, std::string_view new_path);
bool delete_file(std::string_view path);
bool delete_dir(std::string_view path);  // only if empty
bool delete_dir_recursive(std::string_view path);
bool make_dir(std::string_view path);
bool make_dirs(std::string_view path);
bool file_exists(std::string_view path);
bool is_directory(std::string_view path);
// Seconds since the epoch, -1 on failure; nsec gets the fraction, if wanted
int64_t file_mtime(std::string_view path, int32_t *nsec = nullptr);

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

std::string QuiltState::patch_after_top() const {
    ptrdiff_t next = 0;
    if (!applied.empty()) {
        ptrdiff_t top = top_index();
        if (top < 0) return {};
        next = top + 1;
    }
    if (next >= std::ssize(series)) return {};
    return series[checked_cast<size_t>(next)];
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
    return std::to_string(get_strip_level(patch));
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

// The patches directory as named from where quilt was run, like
// upstream's $SUBDIR_DOWN$QUILT_PATCHES/: one ../ per subdirectory level.
// Upstream prepends the ../ to an absolute directory too, naming nothing,
// so here an absolute directory is used as it is.
static std::string patches_prefix(const QuiltState &q) {
    std::string prefix;
    if (!q.subdir.empty() && !is_absolute_path(q.patches_dir)) {
        prefix = "../";
        for (char c : q.subdir) {
            if (c == '/') prefix += "../";
        }
    }
    return prefix + q.patches_dir + "/";
}

std::string format_patch(const QuiltState &q, std::string_view name) {
    if (!get_env("QUILT_PATCHES_PREFIX").empty()) {
        return patches_prefix(q) + std::string(name);
    }
    return std::string(name);
}

std::string next_filename(std::string_view patch) {
    // Set aside one compression suffix, then one patch suffix
    std::string_view base = patch;
    for (std::string_view ext : {".gz", ".bz2", ".xz", ".lzma", ".lz", ".zst"}) {
        if (base.ends_with(ext)) {
            base.remove_suffix(ext.size());
            break;
        }
    }
    for (std::string_view ext : {".diff", ".dif", ".patch"}) {
        if (base.ends_with(ext)) {
            base.remove_suffix(ext.size());
            break;
        }
    }
    std::string_view ext = patch.substr(base.size());

    // Take a trailing "-N" as decimal even with leading zeros, which
    // upstream's shell arithmetic reads as octal
    std::string_view stem = base;
    while (!stem.empty() && stem.back() >= '0' && stem.back() <= '9')
        stem.remove_suffix(1);
    std::string_view digits = base.substr(stem.size());
    std::string num = "1";
    if (!digits.empty() && stem.ends_with('-')) {
        while (std::ssize(digits) > 1 && digits.front() == '0')
            digits.remove_prefix(1);
        num = digits;
        stem.remove_suffix(1);
    } else {
        stem = base;
    }

    // Count up in the string itself, so that no N is too long
    auto it = num.rbegin();
    for (; it != num.rend() && *it == '9'; ++it)
        *it = '0';
    if (it == num.rend()) num.insert(num.begin(), '1');
    else ++*it;

    return std::string(stem) + "-" + num + std::string(ext);
}

std::optional<std::string> find_patch(const QuiltState &q, std::string_view name) {
    // Like upstream, strip the patches directory as format_patch names it,
    // so from a subdirectory only ../patches/ is stripped. A bare
    // "patches/" strips to nothing, which names no patch.
    std::string prefix = patches_prefix(q);
    std::string_view patch = name;
    if (patch.starts_with(prefix)) patch.remove_prefix(prefix.size());
    if (!patch.empty() && q.find_in_series(patch)) {
        return std::string(patch);
    }
    if (!q.series_file_exists) {
        err_line("No series file found");
    } else if (q.series.empty()) {
        err_line("No patches in series");
    } else {
        // Upstream echoes the name as given here, but not below
        err("Patch "); err(name); err_line(" is not in series");
    }
    return std::nullopt;
}

std::optional<std::string> find_top_patch(const QuiltState &q) {
    // Upstream checks for the series file, and that it still matches the
    // applied patches, before running any command that looks up a patch
    if (!q.series_file_exists) {
        err_line("No series file found");
        return std::nullopt;
    }
    if (!q.applied.empty()) {
        if (!q.find_in_series(q.applied.back())) {
            err_line("The series file no longer matches the applied patches. "
                     "Please run 'quilt pop -a'.");
            return std::nullopt;
        }
        return q.applied.back();
    }
    if (q.series.empty()) {
        err_line("No patches in series");
    } else {
        err_line("No patches applied");
    }
    return std::nullopt;
}

std::optional<std::string> find_patch_in_series(const QuiltState &q, std::string_view name) {
    return name.empty() ? find_top_patch(q) : find_patch(q, name);
}

std::optional<std::string> find_applied_patch(const QuiltState &q, std::string_view name) {
    if (name.empty()) return find_top_patch(q);
    auto patch = find_patch(q, name);
    if (patch && !q.is_applied(*patch)) {
        err("Patch "); err(format_patch(q, *patch)); err_line(" is not applied");
        return std::nullopt;
    }
    return patch;
}

bool valid_color_value(std::string_view when) {
    return when.empty() || when == "always" || when == "auto" ||
           when == "tty" || when == "never";
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

// /^<tag>[ \t][^ \t]/ for a tag of "***", "---" or "+++"
static bool is_file_label(std::string_view line, std::string_view tag) {
    return std::ssize(line) >= 5 && line.starts_with(tag) &&
           (line[3] == ' ' || line[3] == '\t') &&
           line[4] != ' ' && line[4] != '\t';
}

// /^Index:[ \t][^ \t]|^diff -/
static bool is_diff_start(std::string_view line) {
    return line.starts_with("diff -") ||
           (std::ssize(line) >= 8 && line.starts_with("Index:") &&
            (line[6] == ' ' || line[6] == '\t') &&
            line[7] != ' ' && line[7] != '\t');
}

struct PatchSplit {
    ptrdiff_t header_end;  // the header is [0, header_end)
    ptrdiff_t body_start;  // the body is [body_start, end)
};

// The state machine shared by upstream's patch_header and patch_body awk
// scripts. A "*** x" or "--- x" line starts the body only when the next
// line is "--- y" or "+++ y" respectively. When it is not, the held line
// goes to the header, and the next line cannot be held in turn, though it
// may still be an "Index: x" or "diff -" line. A line still held at the
// end of input is in neither part.
static PatchSplit split_patch(std::string_view s) {
    std::string_view confirm;  // the label that confirms the held line
    ptrdiff_t held = 0;
    ptrdiff_t n = std::ssize(s);
    for (ptrdiff_t pos = 0, next = 0; pos < n; pos = next) {
        ptrdiff_t nl = str_find(s, '\n', pos);
        ptrdiff_t end = nl < 0 ? n : nl;
        next = nl < 0 ? n : nl + 1;
        std::string_view line = s.substr(checked_cast<size_t>(pos),
                                         checked_cast<size_t>(end - pos));
        if (confirm.empty()) {
            if (is_file_label(line, "***")) confirm = "---";
            else if (is_file_label(line, "---")) confirm = "+++";
            if (!confirm.empty()) {
                held = pos;
                continue;
            }
        } else if (is_file_label(line, confirm)) {
            return {held, held};
        } else {
            confirm = {};
        }
        if (is_diff_start(line)) return {pos, pos};
    }
    return {confirm.empty() ? n : held, n};
}

// Like awk's print, end the last line with a newline
static std::string awk_lines(std::string_view s) {
    std::string r(s);
    if (!r.empty() && r.back() != '\n') r += '\n';
    return r;
}

std::string patch_header(std::string_view patch) {
    auto split = split_patch(patch);
    return awk_lines(patch.substr(0, checked_cast<size_t>(split.header_end)));
}

std::string patch_body(std::string_view patch) {
    auto split = split_patch(patch);
    return awk_lines(patch.substr(checked_cast<size_t>(split.body_start)));
}

// Remove the first line from s and return it without its '\n'
static std::string_view take_line(std::string_view &s) {
    ptrdiff_t nl = str_find(s, '\n');
    ptrdiff_t len = nl < 0 ? std::ssize(s) : nl;
    std::string_view line = s.substr(0, checked_cast<size_t>(len));
    s.remove_prefix(checked_cast<size_t>(nl < 0 ? len : len + 1));
    return line;
}

// /^#? .* files? changed/, how upstream's diffstat summary patterns start
static bool is_diffstat_summary(std::string_view line) {
    ptrdiff_t p = line.starts_with('#') ? 1 : 0;
    return std::ssize(line) > p && line[checked_cast<size_t>(p)] == ' ' &&
           (str_find(line, " file changed", p + 1) >= 0 ||
            str_find(line, " files changed", p + 1) >= 0);
}

// Like upstream: lines matching /#? .* \| / are held until a line matching
// /^#? .* files? changed/ drops them and itself, or any other line puts
// them back. The awk script has no END rule, so lines still held at the
// end are lost.
std::string strip_diffstat(std::string_view header) {
    std::string result;
    std::string held;
    while (!header.empty()) {
        std::string_view line = take_line(header);
        ptrdiff_t space = str_find(line, ' ');
        if (space >= 0 && str_find(line, " | ", space + 1) >= 0) {
            held += line;
            held += '\n';
            continue;
        }
        if (is_diffstat_summary(line)) {
            held.clear();
            continue;
        }
        result += held;
        result += line;
        result += '\n';
        held.clear();
    }
    return result;
}

// /^#? .* \|  *[1-9][0-9]* /, upstream refresh's diffstat file line
static bool is_diffstat_file_line(std::string_view line) {
    ptrdiff_t n = std::ssize(line);
    ptrdiff_t p = line.starts_with('#') ? 1 : 0;
    if (n <= p || line[checked_cast<size_t>(p)] != ' ') return false;
    auto at = [&](ptrdiff_t i) { return line[checked_cast<size_t>(i)]; };
    for (ptrdiff_t bar = str_find(line, " |", p + 1); bar >= 0;
         bar = str_find(line, " |", bar + 1)) {
        ptrdiff_t i = bar + 2;
        if (i >= n || at(i) != ' ') continue;
        while (i < n && at(i) == ' ') ++i;
        if (i >= n || at(i) < '1' || at(i) > '9') continue;
        while (i < n && at(i) >= '0' && at(i) <= '9') ++i;
        if (i < n && at(i) == ' ') return true;
    }
    return false;
}

// Like upstream refresh --diffstat's awk script: diffstat file lines are
// held. A summary line drops them and itself for the new diffstat, with
// each line prefixed by "#" when the summary line starts with one. Any
// other line puts the held lines back. With no summary line, the held
// lines stay, and "---", the new diffstat and a line holding just the last
// line's "#" prefix (if any) are added at the end, so an empty header
// becomes "---", the diffstat and a blank line. Every other byte, CRs and
// blank lines included, is kept.
std::string replace_diffstat(std::string_view header, std::string_view diffstat) {
    std::string result;
    std::string held;
    std::string_view prefix;
    bool replaced = false;
    auto put_diffstat = [&] {
        for (std::string_view s = diffstat; !s.empty();) {
            ptrdiff_t nl = str_find(s, '\n');
            ptrdiff_t len = nl < 0 ? std::ssize(s) : nl + 1;
            result += prefix;
            result += s.substr(0, checked_cast<size_t>(len));
            s.remove_prefix(checked_cast<size_t>(len));
        }
    };
    while (!header.empty()) {
        std::string_view line = take_line(header);
        prefix = line.starts_with('#') ? "#" : "";
        if (is_diffstat_file_line(line)) {
            held += line;
            held += '\n';
            continue;
        }
        if (is_diffstat_summary(line)) {
            put_diffstat();
            replaced = true;
            held.clear();
            continue;
        }
        result += held;
        result += line;
        result += '\n';
        held.clear();
    }
    result += held;
    if (!replaced) {
        result += "---\n";
        put_diffstat();
        result += prefix;
        result += '\n';
    }
    return result;
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

static std::vector<std::string> parse_series(std::string_view content,
                                             std::map<std::string, int> *strip_levels,
                                             std::set<std::string> *reversed) {
    std::vector<std::string> patches;
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

std::vector<std::string> read_series(std::string_view path,
                                     std::map<std::string, int> *strip_levels,
                                     std::set<std::string> *reversed) {
    return parse_series(read_file(path), strip_levels, reversed);
}

// Split series file content into lines, each keeping its line ending, so
// that edits reproduce untouched lines byte for byte.
static std::vector<std::string_view> series_lines(std::string_view content) {
    std::vector<std::string_view> lines;
    while (!content.empty()) {
        auto nl = str_find(content, '\n');
        auto len = nl < 0 ? std::ssize(content) : nl + 1;
        lines.push_back(content.substr(0, checked_cast<size_t>(len)));
        content.remove_prefix(checked_cast<size_t>(len));
    }
    return lines;
}

// The patch a series line names, as read_series sees it: the first word of
// the trimmed line, unless the line is blank or a comment, in which case it
// is empty.
static std::string_view series_line_patch(std::string_view line) {
    auto is_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (!line.empty() && is_space(line.front())) line.remove_prefix(1);
    while (!line.empty() && is_space(line.back())) line.remove_suffix(1);
    ptrdiff_t len = 0;
    while (len < std::ssize(line) && line[checked_cast<size_t>(len)] != ' ' &&
           line[checked_cast<size_t>(len)] != '\t') {
        ++len;
    }
    std::string_view name = line.substr(0, checked_cast<size_t>(len));
    return name.starts_with('#') ? std::string_view{} : name;
}

// Write an edited series file, then reload the in-memory series from it.
static bool store_series(QuiltState &q, std::string_view path,
                         std::string_view content) {
    if (!write_file(path, content)) return false;
    q.series_file_exists = true;
    q.patch_strip_level.clear();
    q.patch_reversed.clear();
    q.series = parse_series(content, &q.patch_strip_level, &q.patch_reversed);
    return true;
}

bool insert_in_series(QuiltState &q, std::string_view patch,
                      std::string_view opts, std::string_view before) {
    std::string path = path_join(q.work_dir, q.series_file);
    std::string content = read_file(path);
    auto lines = series_lines(content);

    // The new line follows the file's line endings
    std::string_view eol = !lines.empty() && lines[0].ends_with("\r\n") ? "\r\n" : "\n";
    std::string entry(patch);
    if (!opts.empty()) {
        entry += ' ';
        entry += opts;
    }
    entry += eol;

    std::string result;
    bool inserted = false;
    for (auto line : lines) {
        if (!inserted && !before.empty() && series_line_patch(line) == before) {
            result += entry;
            inserted = true;
        }
        result += line;
    }
    if (!inserted) {
        if (!result.empty() && !result.ends_with('\n')) result += eol;
        result += entry;
    }
    return store_series(q, path, result);
}

bool remove_from_series(QuiltState &q, std::string_view patch) {
    std::string path = path_join(q.work_dir, q.series_file);
    std::string content = read_file(path);
    std::string result;
    for (auto line : series_lines(content)) {
        auto name = series_line_patch(line);
        if (name.empty() || name != patch) result += line;
    }
    return store_series(q, path, result);
}

bool rename_in_series(QuiltState &q, std::string_view from, std::string_view to) {
    std::string path = path_join(q.work_dir, q.series_file);
    std::string content = read_file(path);
    std::string result;
    for (auto line : series_lines(content)) {
        auto name = series_line_patch(line);
        if (name.empty() || name != from) {
            result += line;
            continue;
        }
        // Keep the indentation, options, and comment around the name
        auto at = name.data() - line.data();
        result += line.substr(0, checked_cast<size_t>(at));
        result += to;
        result += line.substr(checked_cast<size_t>(at + std::ssize(name)));
    }
    return store_series(q, path, result);
}

std::string series_patch_args(const QuiltState &q, std::string_view patch) {
    std::string content = read_file(path_join(q.work_dir, q.series_file));
    for (auto line : series_lines(content)) {
        auto name = series_line_patch(line);
        if (name.empty() || name != patch) continue;
        auto end = name.data() - line.data() + std::ssize(name);
        std::string args;
        for (const auto &tok : split_on_whitespace(trim(line.substr(checked_cast<size_t>(end))))) {
            if (tok.starts_with('#')) break;
            if (!args.empty()) args += ' ';
            args += tok;
        }
        return args;
    }
    return {};
}

bool set_series_strip_level(QuiltState &q, std::string_view patch,
                            int strip_level) {
    std::string path = path_join(q.work_dir, q.series_file);
    std::string content = read_file(path);
    std::string result;
    bool changed = false;
    for (auto line : series_lines(content)) {
        auto name = series_line_patch(line);
        if (name.empty() || name != patch) {
            result += line;
            continue;
        }

        // Split off the line ending and inline comment as read_series does
        std::string_view body = line;
        if (body.ends_with('\n')) body.remove_suffix(1);
        if (body.ends_with('\r')) body.remove_suffix(1);
        std::string_view eol = line.substr(body.size());
        std::string_view comment;
        auto hash = str_find(body, " #");
        if (hash >= 0) {
            comment = body.substr(checked_cast<size_t>(hash + 1));
            body = body.substr(0, checked_cast<size_t>(hash));
        }
        auto tokens = split_on_whitespace(body);

        // Drop -R and any -p option ("-pN" or "-p N"), then put the new
        // level where the old one was, or right after the name.
        std::vector<std::string> opts;
        ptrdiff_t level_at = -1;
        for (ptrdiff_t i = 1; i < std::ssize(tokens); ++i) {
            const auto &tok = tokens[checked_cast<size_t>(i)];
            if (tok == "-R") continue;
            if (tok.starts_with("-p")) {
                if (tok == "-p" && i + 1 < std::ssize(tokens)) ++i;
                if (level_at < 0) level_at = std::ssize(opts);
                continue;
            }
            opts.push_back(tok);
        }
        if (strip_level != 1) {
            opts.insert(opts.begin() + std::max(level_at, ptrdiff_t{0}),
                        "-p" + std::to_string(strip_level));
        }
        if (std::equal(opts.begin(), opts.end(), tokens.begin() + 1, tokens.end())) {
            result += line;
            continue;
        }

        result += tokens[0];
        for (const auto &opt : opts) {
            result += ' ';
            result += opt;
        }
        if (!comment.empty()) {
            result += ' ';
            result += comment;
        }
        result += eol;
        changed = true;
    }
    return !changed || store_series(q, path, result);
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
        // Skip quilt metadata, which lives only at the top level of
        // .pc/<patch>/. Other dotfiles (.gitignore, sub/.hidden) are
        // tracked files.
        if (f == ".timestamp" || f == ".needs_refresh") continue;
        result.push_back(std::move(f));
    }
    return result;
}

// File names in the patch file, in order, like upstream's
// filenames_in_patch: a loose scan of ---, +++, and *** lines anywhere in
// the file, with the series strip level applied.
static std::vector<std::string> filenames_in_patch(const QuiltState &q,
                                                   std::string_view patch) {
    std::vector<std::string> names;
    std::string path = path_join(q.work_dir, q.patches_dir, patch);
    if (!file_exists(path)) return names;
    std::string content = read_file(path);
    int strip = q.get_strip_level(patch);
    std::set<std::string, std::less<>> seen;

    std::string_view rest = content;
    while (!rest.empty()) {
        ptrdiff_t nl = str_find(rest, '\n');
        std::string_view line = nl < 0 ? rest : rest.substr(0, checked_cast<size_t>(nl));
        rest.remove_prefix(nl < 0 ? rest.size() : checked_cast<size_t>(nl + 1));

        // The first and third whitespace-separated fields, like awk's
        std::string_view fields[3];
        std::string_view scan = line;
        for (auto &field : fields) {
            while (!scan.empty() && (scan[0] == ' ' || scan[0] == '\t')) scan.remove_prefix(1);
            ptrdiff_t end = 0;
            while (end < std::ssize(scan) && scan[checked_cast<size_t>(end)] != ' ' &&
                   scan[checked_cast<size_t>(end)] != '\t') ++end;
            field = scan.substr(0, checked_cast<size_t>(end));
            scan.remove_prefix(checked_cast<size_t>(end));
        }
        if (!(fields[0] == "+++" || (fields[0] == "---" && fields[2] != "----") ||
              (fields[0] == "***" && fields[2] != "****"))) continue;
        if (std::ssize(line) < 4 || line[3] != ' ') continue;
        std::string_view name = line.substr(4);

        if (name.starts_with('"')) {
            // Up to the first quote that ends the line or precedes a tab
            name.remove_prefix(1);
            for (ptrdiff_t i = 0; i < std::ssize(name); ++i) {
                if (name[checked_cast<size_t>(i)] == '"' &&
                    (i + 1 == std::ssize(name) || name[checked_cast<size_t>(i + 1)] == '\t')) {
                    name = name.substr(0, checked_cast<size_t>(i));
                    break;
                }
            }
        } else {
            ptrdiff_t tab = str_find(name, '\t');
            if (tab >= 0) name = name.substr(0, checked_cast<size_t>(tab));
        }
        if (name.empty() || name == "/dev/null") continue;

        for (int n = 0; n < strip; ++n) {
            ptrdiff_t slash = str_find(name, '/');
            if (slash > 0) name.remove_prefix(checked_cast<size_t>(slash + 1));
        }
        if (seen.insert(std::string(name)).second) names.emplace_back(name);
    }
    return names;
}

std::vector<std::string> files_in_patch_ordered(const QuiltState &q, std::string_view patch) {
    // Like upstream: the patch's files in the order its patch file names
    // them, then any others in sorted order
    auto files = files_in_patch(q, patch);
    std::ranges::sort(files);
    std::set<std::string, std::less<>> tracked(files.begin(), files.end());
    std::vector<std::string> result;
    std::set<std::string, std::less<>> placed;
    for (auto &name : filenames_in_patch(q, patch)) {
        if (tracked.contains(name) && placed.insert(name).second) result.push_back(name);
    }
    for (auto &file : files) {
        if (!placed.contains(file)) result.push_back(file);
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
     "Usage: quilt new [-p n] {patchname}",
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
     "Usage: quilt add [-P patch] {file} ...",
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
     "Usage: quilt push [-afqvm] [--fuzz=N] [--merge[=merge|diff3]] "
     "[--leave-rejects] [--color[=always|auto|never]] [--refresh] [num|patch]",
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
     "Usage: quilt pop [-afRqv] [--refresh] [num|patch]",
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
     "Usage: quilt refresh [-p n|-p ab] [-u|-U num|-c|-C num] [-z[new_name]] "
     "[-f] [--no-timestamps] [--no-index] [--diffstat] [--sort] [--backup] "
     "[--strip-trailing-whitespace] [patch]",
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
     "Usage: quilt diff [-p n|-p ab] [-u|-U num|-c|-C num] "
     "[--combine patch|-z] [-R] [-P patch] [--snapshot] [--diff=utility] "
     "[--no-timestamps] [--no-index] [--sort] [--color[=always|auto|never]] "
     "[file ...]",
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
     "Usage: quilt series [--color[=always|auto|never]] [-v]",
     "Usage: quilt series [-v]\n"
     "\n"
     "List all patches in the series file, both applied and unapplied.\n"
     "\n"
     "Options:\n"
     "  -v          Mark applied patches with + and the top with =.\n",
     "List all patches in the series"},

    {"applied", cmd_applied,
     "Usage: quilt applied [patch]",
     "Usage: quilt applied [patch]\n"
     "\n"
     "List the currently applied patches in stack order. With a patch\n"
     "name, lists all applied patches up to and including it.\n",
     "List applied patches"},

    {"unapplied", cmd_unapplied,
     "Usage: quilt unapplied [patch]",
     "Usage: quilt unapplied [patch]\n"
     "\n"
     "List the patches that have not been applied yet. With a patch\n"
     "name, lists all patches after the named one in the series.\n",
     "List patches not yet applied"},

    {"top", cmd_top,
     "Usage: quilt top",
     "Usage: quilt top\n"
     "\n"
     "Print the name of the topmost applied patch.\n",
     "Show the topmost applied patch"},

    {"next", cmd_next,
     "Usage: quilt next [patch]",
     "Usage: quilt next [patch]\n"
     "\n"
     "Print the patch after the topmost applied patch, or after the\n"
     "named patch in the series.\n",
     "Show the next patch after the top or a given patch"},

    {"previous", cmd_previous,
     "Usage: quilt previous [patch]",
     "Usage: quilt previous [patch]\n"
     "\n"
     "Print the patch before the topmost applied patch, or before the\n"
     "named patch in the series.\n",
     "Show the patch before the top or a given patch"},

    {"delete", cmd_delete,
     "Usage: quilt delete [-r] [--backup] [patch|-n]",
     "Usage: quilt delete [-r] [--backup] [patch|-n]\n"
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
     "Usage: quilt rename [-P patch] new_name",
     "Usage: quilt rename [-P patch] new_name\n"
     "\n"
     "Rename the topmost or named patch. Updates the series file and\n"
     "renames the patch file in the patches directory.\n"
     "\n"
     "Options:\n"
     "  -P patch    Rename the named patch instead of the top.\n",
     "Rename a patch"},

    {"import", cmd_import,
     "Usage: quilt import [-p num] [-R] [-P patch] [-f] [-d "
     "{o|a|n}] patchfile ...",
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
     "Usage: quilt header [-a|-r|-e] [--backup] [--strip-diffstat] "
     "[--strip-trailing-whitespace] [patch]",
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
     "Usage: quilt files [-v] [-a] [-l] [--combine patch] [patch]",
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
     "Usage: quilt patches [-v] [--color[=always|auto|never]] {file} "
     "[files...]",
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
     "Usage: quilt edit file ...",
     "Usage: quilt edit file ...\n"
     "\n"
     "Add files to the topmost patch and open them in $EDITOR. This is\n"
     "a shortcut for quilt add followed by $EDITOR, and is the safest\n"
     "way to modify tracked files.\n",
     "Add files to the topmost patch and open an editor"},

    {"revert", cmd_revert,
     "Usage: quilt revert [-P patch] {file} ...",
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
     "Usage: quilt remove [-P patch] {file} ...",
     "Usage: quilt remove [-P patch] file ...\n"
     "\n"
     "Remove files from the topmost or named patch and restore them\n"
     "from backup. The opposite of quilt add.\n"
     "\n"
     "Options:\n"
     "  -P patch    Remove files from the named patch instead of the top.\n",
     "Remove files from the topmost patch"},

    {"fold", cmd_fold,
     "Usage: quilt fold [-R] [-q] [-f] [-p strip-level]",
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
     "Usage: quilt fork [new_name]",
     "Usage: quilt fork [new_name]\n"
     "\n"
     "Copy the topmost patch to a new name. The series is updated to\n"
     "reference the copy; the original file is kept but removed from\n"
     "the series. If no name is given, -2 goes ahead of any .diff or\n"
     ".patch suffix, or a -N already there counts up (patch.diff,\n"
     "patch-2.diff, patch-3.diff).\n",
     "Create a copy of the topmost patch under a new name"},

    // Implemented analysis commands
    {"annotate", cmd_annotate,
     "Usage: quilt annotate [-P patch] {file}",
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
     "Usage: quilt graph [--all] [--reduce] [--lines[=num]] "
     "[--edge-labels=files] [-T ps] [patch]",
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
     "Usage: quilt mail {--mbox file} [--prefix prefix] [--sender ...] "
     "[--from ...] [--to ...] [--cc ...] [--bcc ...] "
     "[first_patch [last_patch]]",
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
     "Usage: quilt grep [-h|options] {pattern}",
     "Usage: quilt grep [-h|options] pattern\n"
     "\n"
     "Search source files, skipping patches/ and .pc/ directories.\n"
     "Not yet implemented.\n",
     "Search source files (not implemented)"},

    {"setup", cmd_setup,
     "Usage: quilt setup [-d path-prefix] [-v] [--sourcedir dir] [--fuzz=N] "
     "[--spec-filter FILTER] [--slow|--fast] {specfile|seriesfile}",
     "Usage: quilt setup [-d path] series\n"
     "\n"
     "Initialize a source tree from a series file or RPM spec.\n"
     "Not yet implemented.\n",
     "Set up a source tree from a series file (not implemented)"},

    {"shell", cmd_shell,
     "Usage: quilt shell [command]",
     "Usage: quilt shell [command]\n"
     "\n"
     "Open a shell or run a command in the quilt environment.\n"
     "Not yet implemented.\n",
     "Open a subshell (not implemented)"},

    {"snapshot", cmd_snapshot,
     "Usage: quilt snapshot [-d]",
     "Usage: quilt snapshot [-d]\n"
     "\n"
     "Save a copy of the current working tree state for later\n"
     "comparison with quilt diff --snapshot.\n"
     "\n"
     "Options:\n"
     "  -d          Remove the current snapshot instead of creating one.\n",
     "Save a snapshot of the working tree for later diff"},

    {"upgrade", cmd_upgrade,
     "Usage: quilt upgrade",
     "Usage: quilt upgrade\n"
     "\n"
     "Upgrade quilt metadata in .pc/ to the current format. This is\n"
     "a no-op because only the version 2 format is supported.\n",
     "Upgrade quilt metadata to the current format"},

    {"init", cmd_init,
     "Usage: quilt init",
     "Usage: quilt init\n"
     "\n"
     "Initialize quilt metadata in the current directory. This is\n"
     "optional since any quilt command creates .pc/ and patches/ as\n"
     "needed, but it lets you establish the project root before\n"
     "working from a subdirectory.\n",
     "Initialize quilt metadata in the current directory"},
};

static constexpr int num_commands = sizeof(commands) / sizeof(commands[0]);

static const Command *find_command(std::string_view name) {
    for (const auto &c : commands) {
        if (name == c.name) return &c;
    }
    return nullptr;
}

int usage_error(std::string_view command) {
    if (const Command *c = find_command(command)) err_line(c->synopsis);
    return 1;
}

int command_help(std::string_view command) {
    if (const Command *c = find_command(command)) out_line(c->usage);
    return 0;
}

// The option that --name names: an exact match, or else the only option
// that name abbreviates, where upstream's options beat quilt.cpp's
// extensions. Without one, return the candidates, which may be none.
static const LongOpt *match_long_option(std::span<const LongOpt> longopts,
                                        std::string_view name,
                                        std::vector<const LongOpt *> &candidates)
{
    for (const auto &opt : longopts) {
        if (opt.name == name) return &opt;
        if (!name.empty() && opt.name.starts_with(name)) candidates.push_back(&opt);
    }
    if (std::ranges::any_of(candidates, [](auto *c) { return !c->extension; })) {
        std::erase_if(candidates, [](auto *c) { return c->extension; });
    }
    if (candidates.empty()) return nullptr;
    // Like getopt_long, names for the same option are no ambiguity
    const LongOpt *first = candidates.front();
    bool same = std::ranges::all_of(candidates, [&](auto *c) {
        return c->key == first->key && c->arg == first->arg;
    });
    return same ? first : nullptr;
}

std::optional<ParsedArgs> parse_options(int argc, char **argv,
                                        std::string_view shortopts,
                                        std::span<const LongOpt> longopts)
{
    std::string_view command = argv[0];
    std::vector<LongOpt> all_longopts(longopts.begin(), longopts.end());
    all_longopts.push_back({"help", OptArg::none, 'h', true});

    // Like getopt(1), report every bad option before giving up
    bool ok = true;
    auto complain = [&](std::string_view what) {
        err("quilt "); err(command); err(": "); err_line(what);
        ok = false;
    };

    ParsedArgs parsed;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--") {
            while (++i < argc) parsed.operands.emplace_back(argv[i]);
            break;
        }
        if (std::ssize(arg) < 2 || arg[0] != '-') {
            parsed.operands.push_back(arg);
            continue;
        }

        if (arg[1] == '-') {
            std::string_view body = arg.substr(2);
            ptrdiff_t eq = str_find(body, '=');
            std::string_view name = body.substr(0, eq < 0 ? body.size() : checked_cast<size_t>(eq));
            std::vector<const LongOpt *> candidates;
            const LongOpt *opt = match_long_option(all_longopts, name, candidates);
            if (!opt) {
                std::string what;
                if (candidates.empty()) {
                    what = "unrecognized option '" + std::string(arg) + "'";
                } else {
                    what = "option '" + std::string(arg) + "' is ambiguous; possibilities:";
                    for (auto *c : candidates) what += " '--" + std::string(c->name) + "'";
                }
                complain(what);
                continue;
            }
            std::string_view value;
            if (eq >= 0) {
                if (opt->arg == OptArg::none) {
                    complain("option '--" + std::string(opt->name) + "' doesn't allow an argument");
                    continue;
                }
                value = body.substr(checked_cast<size_t>(eq + 1));
            } else if (opt->arg == OptArg::required) {
                if (i + 1 >= argc) {
                    complain("option '--" + std::string(opt->name) + "' requires an argument");
                    continue;
                }
                value = argv[++i];
            }
            parsed.options.push_back({opt->key, value});
            continue;
        }

        for (ptrdiff_t j = 1; j < std::ssize(arg); ++j) {
            char letter = arg[checked_cast<size_t>(j)];
            ptrdiff_t at = letter == ':' ? -1 : str_find(shortopts, letter);
            if (at < 0) {
                complain(std::string("invalid option -- '") + letter + "'");
                continue;
            }
            int key = static_cast<unsigned char>(letter);
            auto colon_at = [&](ptrdiff_t k) {
                return k < std::ssize(shortopts) && shortopts[checked_cast<size_t>(k)] == ':';
            };
            if (!colon_at(at + 1)) {
                parsed.options.push_back({key, {}});
                continue;
            }
            // The rest of the word is the value, or, for a required one,
            // the next word, whatever it is
            std::string_view value = arg.substr(checked_cast<size_t>(j + 1));
            if (value.empty() && !colon_at(at + 2)) {
                if (i + 1 >= argc) {
                    complain(std::string("option requires an argument -- '") + letter + "'");
                    break;
                }
                value = argv[++i];
            }
            parsed.options.push_back({key, value});
            break;
        }
    }

    if (!ok) {
        usage_error(command);
        return std::nullopt;
    }
    return parsed;
}

static std::string to_upper(std::string_view s) {
    std::string result(s);
    for (char &c : result) {
        if (c >= 'a' && c <= 'z') c -= 32;
    }
    return result;
}

int quilt_main(int argc, char **argv) {
    // --- Phase 1: Scan the arguments like upstream's bin/quilt ---
    // The first argument not starting with "-" names the command, and the
    // others go to it in order, so "quilt -a push" runs "push -a".
    // --quiltrc and --trace are taken out wherever they appear.
    std::string quiltrc_path;   // empty = default search, "-" = disabled
    bool quiltrc_set = false;
    std::optional<std::string> command;
    std::vector<std::string> command_args;
    bool bad_trace = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (!a.empty() && a[0] != '-') {
            if (!command) command = std::string(a);
            else command_args.emplace_back(a);
        } else if (a.starts_with("--quiltrc=")) {
            quiltrc_path = std::string(a.substr(10));
            quiltrc_set = true;
        } else if (a == "--quiltrc") {
            // Without a value, upstream reads no configuration file
            quiltrc_path = i + 1 < argc ? argv[++i] : "-";
            quiltrc_set = true;
        } else if (a.starts_with("--trace")) {
            // Accepted but ignored; any other form prints the usage
            if (a != "--trace" && a != "--trace=verbose") {
                bad_trace = true;
                break;
            }
        } else {
            command_args.emplace_back(a);
        }
    }

    auto usage = [] {
        err_line("Usage: quilt [--quiltrc file] <command> [options] [args]");
        err_line("Use \"quilt --help\" for a list of commands.");
        return 1;
    };
    if (bad_trace) return usage();

    if (!command) {
        if (std::ssize(command_args) == 1 && command_args[0] == "--version") {
            out_line(QUILT_VERSION);
            return 0;
        }
        if (std::ranges::find(command_args, "--help") == command_args.end() &&
            std::ranges::find(command_args, "-h") == command_args.end()) {
            return usage();
        }
    }

    // Handle --help
    if (!command || *command == "help") {
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
    std::string cmd_name(*command);

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
    // Like upstream, the command parses the variable's words and the command
    // line in one pass, so a "--" in the variable ends the options for both.
    std::vector<std::string> final_argv_storage;
    std::vector<char *> final_argv;

    final_argv_storage.push_back(std::string(found->name));
    for (auto &ea : extra_args) {
        final_argv_storage.push_back(ea);
    }
    for (auto &arg : command_args) {
        final_argv_storage.push_back(arg);
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

// Split content into lines.  Each element keeps its terminating '\n', so
// an incomplete last line compares equal only to the other file's
// incomplete last line with the same text, never to a complete line.
// GNU diff treats it the same way in unified and context output.
static std::vector<std::string_view> split_file_lines(std::string_view content)
{
    std::vector<std::string_view> lines;
    ptrdiff_t start = 0;
    ptrdiff_t len = std::ssize(content);
    for (ptrdiff_t i = 0; i < len; ++i) {
        if (content[checked_cast<size_t>(i)] == '\n') {
            lines.push_back(content.substr(checked_cast<size_t>(start),
                                           checked_cast<size_t>(i + 1 - start)));
            start = i + 1;
        }
    }
    // If there's content after the last newline (no trailing newline)
    if (start < len) {
        lines.push_back(content.substr(checked_cast<size_t>(start),
                                       checked_cast<size_t>(len - start)));
    }
    return lines;
}

// Append one line of a hunk body after its prefix, followed by a
// "No newline" marker if it is an incomplete last line.
static void append_hunk_line(std::string &out, std::string_view prefix,
                             std::string_view line)
{
    out += prefix;
    out += line;
    if (!line.ends_with('\n')) {
        out += "\n\\ No newline at end of file\n";
    }
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
        // Like GNU diff, merge when no more than twice the context
        // lines separate the changes, so the context windows overlap or
        // are adjacent
        ptrdiff_t gap = changes[checked_cast<size_t>(i)].first - merged.back().last - 1;
        if (gap <= 2 * context_lines) {
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
                append_hunk_line(result, " ", old_lines[checked_cast<size_t>(op.old_idx)]);
            } else if (op.type == 'D') {
                append_hunk_line(result, "-", old_lines[checked_cast<size_t>(op.old_idx)]);
            } else { // 'I'
                append_hunk_line(result, "+", new_lines[checked_cast<size_t>(op.new_idx)]);
            }
        }
    }

    return result;
}

// A context diff range: "start,end", or like GNU diff a single number for
// one line, or for an empty range the line before it.
static std::string context_range(ptrdiff_t start, ptrdiff_t count)
{
    if (count > 1)
        return std::format("{},{}", start, start + count - 1);
    return std::format("{}", start);
}

// Format context diff output
static std::string format_context(
    std::span<const std::string_view> old_lines,
    std::span<const std::string_view> new_lines,
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

    for (const auto &hunk : hunks) {
        result += "***************\n";

        // Classify each edit group: adjacent D and I runs form "changes" (! prefix)
        // We need to build old-side and new-side lines with proper prefixes.
        struct SideLine { std::string_view prefix; std::string_view text; };
        std::vector<SideLine> old_side, new_side;

        ptrdiff_t num_ops = std::ssize(hunk.ops);
        for (ptrdiff_t k = 0; k < num_ops; ) {
            const auto &op = hunk.ops[checked_cast<size_t>(k)];
            if (op.type == 'E') {
                old_side.push_back({"  ", old_lines[checked_cast<size_t>(op.old_idx)]});
                new_side.push_back({"  ", new_lines[checked_cast<size_t>(op.new_idx)]});
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
                    old_side.push_back({is_change ? "! " : "- ",
                                       old_lines[checked_cast<size_t>(dop.old_idx)]});
                }
                for (ptrdiff_t j = de; j < ie; ++j) {
                    auto &iop = hunk.ops[checked_cast<size_t>(j)];
                    new_side.push_back({is_change ? "! " : "+ ",
                                       new_lines[checked_cast<size_t>(iop.new_idx)]});
                }
            }
        }

        // Old range header
        result += std::format("*** {} ****\n",
                              context_range(hunk.old_start, hunk.old_count));

        // Print old-side lines only if there are changes (not just context)
        bool has_old_changes = false;
        for (const auto &sl : old_side) {
            if (sl.prefix != "  ") { has_old_changes = true; break; }
        }
        if (has_old_changes) {
            for (const auto &sl : old_side) {
                append_hunk_line(result, sl.prefix, sl.text);
            }
        }

        // New range header
        result += std::format("--- {} ----\n",
                              context_range(hunk.new_start, hunk.new_count));

        // Print new-side lines only if there are changes
        bool has_new_changes = false;
        for (const auto &sl : new_side) {
            if (sl.prefix != "  ") { has_new_changes = true; break; }
        }
        if (has_new_changes) {
            for (const auto &sl : new_side) {
                append_hunk_line(result, sl.prefix, sl.text);
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
    auto old_lines = split_file_lines(old_content);
    auto new_lines = split_file_lines(new_content);

    // Run diff algorithm
    std::vector<EditOp> ops;
    if (algorithm == DiffAlgorithm::patience)
        ops = patience_diff(old_lines, new_lines);
    else if (algorithm == DiffAlgorithm::histogram)
        ops = histogram_diff(old_lines, new_lines);
    else
        ops = myers_diff(old_lines, new_lines, algorithm);

    // Check if there are any differences
    bool has_diff = false;
    for (const auto &op : ops) {
        if (op.type != 'E') { has_diff = true; break; }
    }

    if (!has_diff) {
        return {0, ""};
    }

    // Use labels or default to paths
    std::string old_lbl = old_label.empty() ? std::string(old_path) : std::string(old_label);
    std::string new_lbl = new_label.empty() ? std::string(new_path) : std::string(new_label);

    // Build hunks
    auto hunks = build_hunks(ops, context_lines);

    std::string output;
    if (format == DiffFormat::context) {
        output = format_context(old_lines, new_lines, hunks, old_lbl, new_lbl);
    } else {
        output = format_unified(old_lines, new_lines, hunks, old_lbl, new_lbl);
    }

    return {1, std::move(output)};
}

// === src/patch.cpp ===

// This is free and unencumbered software released into the public domain.
//
// Built-in patch engine for applying unified and context diffs.
// Implements spiral search with offset tracking, fuzz matching,
// reverse application, merge conflict markers, and reject files.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>

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
    // What follows the ranges in the header, from the space before it,
    // usually a function name, which GNU patch copies into the rejects
    std::string function;
    // In a context diff, the old and new sections as given, or as filled
    // in when diff omitted one, each line with its marker (' ', '-', '+'
    // or '!'), which GNU patch writes back into the rejects
    std::vector<std::string> old_section;
    std::vector<std::string> new_section;
};

struct PatchFile {
    // The names and timestamps of the file headers, as GNU patch writes
    // them in the rejects
    std::string old_label;
    std::string new_label;
    bool context = false;      // a context diff rather than a unified one
    std::string target_path;   // after strip-level
    // How surely the patch says the file is absent before (old) and after
    // (new) it, as GNU patch judges: 0 not at all, 1 when the first hunk's
    // range on that side starts at line 0, 2 when the header also names
    // /dev/null or gives the epoch as the file's timestamp, as diff -N does
    int old_absent = 0;
    int new_absent = 0;
    // 1-based line where the text leading up to the first hunk begins,
    // after the previous file's last hunk, as GNU patch quotes it
    ptrdiff_t text_line = 0;
    ptrdiff_t hunk_line = 0;   // 1-based line of the first hunk header
    // 1-based line whose CRLF ending has GNU patch strip the CRs from the
    // hunks: the "+++ " line of a unified diff, or the first hunk's
    // "*** N ****" line of a context diff
    ptrdiff_t crlf_line = 0;
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

// A file header as GNU patch writes it in the rejects: the name stripped of
// strip leading components, then the rest of the line, the timestamp.  The
// name /dev/null, or one with fewer slashes than it must strip, stands
// alone as /dev/null.
static std::string reject_label(std::string_view header, int strip)
{
    std::string name = extract_path(header);
    ptrdiff_t slashes = 0;  // runs of them, as strip_path counts them
    for (ptrdiff_t k = 0; k < std::ssize(name); ++k) {
        if (name[checked_cast<size_t>(k)] == '/' &&
            (k + 1 == std::ssize(name) || name[checked_cast<size_t>(k + 1)] != '/')) {
            ++slashes;
        }
    }
    if (name == "/dev/null" || slashes < strip) return "/dev/null";
    return strip_path(name, strip) + std::string(header.substr(name.size()));
}

// ── Diff parser ────────────────────────────────────────────────────────

// Remove prefix from the front of s, returning whether it was there.
static bool take(std::string_view &s, std::string_view prefix)
{
    if (!s.starts_with(prefix)) return false;
    s.remove_prefix(prefix.size());
    return true;
}

// Remove a decimal number from the front of s.
static bool take_number(std::string_view &s, ptrdiff_t &n)
{
    if (s.empty() || s[0] < '0' || s[0] > '9') return false;
    auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (ec != std::errc{}) return false;
    s.remove_prefix(checked_cast<size_t>(end - s.data()));
    return true;
}

// Whether a diff header's timestamp is the epoch, which diff -N gives a
// missing file.  Like GNU patch, match any time within the range of local
// time offsets of it, -25:00 to +26:00.  Reads the forms diff writes for -u,
// "1970-01-01 00:00:00.000000000 +0000", and -c, "Thu Jan  1 00:00:00 1970".
static bool is_epoch_timestamp(std::string_view s)
{
    ptrdiff_t year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    auto skip_spaces = [&] { while (take(s, " ")) {} };
    auto take_time = [&] {
        return take_number(s, hour) && take(s, ":") && take_number(s, minute) &&
               take(s, ":") && take_number(s, second);
    };

    skip_spaces();
    if (take_number(s, year)) {
        if (!take(s, "-") || !take_number(s, month) || !take(s, "-") ||
            !take_number(s, day) || !take(s, " ") || !take_time()) {
            return false;
        }
        if (take(s, ".")) {
            while (!s.empty() && s[0] >= '0' && s[0] <= '9') s.remove_prefix(1);
        }
    } else {
        // Skip the weekday
        ptrdiff_t space = str_find(s, ' ');
        if (space < 0) return false;
        s.remove_prefix(checked_cast<size_t>(space));
        skip_spaces();
        static constexpr std::string_view months = "JanFebMarAprMayJunJulAugSepOctNovDec";
        if (std::ssize(s) < 3) return false;
        ptrdiff_t m = str_find(months, s.substr(0, 3));
        if (m < 0 || m % 3 != 0) return false;
        month = m / 3 + 1;
        s.remove_prefix(3);
        skip_spaces();
        if (!take_number(s, day) || !take(s, " ") || !take_time()) return false;
        skip_spaces();
        if (!take_number(s, year)) return false;
    }

    ptrdiff_t zone = 0;
    skip_spaces();
    bool west = take(s, "-");
    if (west || take(s, "+")) {
        ptrdiff_t hhmm = 0;
        if (!take_number(s, hhmm) || hhmm > 2459) return false;
        zone = (hhmm / 100 * 60 + hhmm % 100) * 60 * (west ? -1 : 1);
    }

    if (day > 31 || hour > 23 || minute > 59 || second > 60) return false;
    ptrdiff_t days;
    if (year == 1969 && month == 12) days = day - 32;
    else if (year == 1970 && month == 1) days = day - 1;
    else return false;  // too far from the epoch in any time zone
    ptrdiff_t t = ((days * 24 + hour) * 60 + minute) * 60 + second - zone;
    return -25 * 60 * 60 < t && t < 26 * 60 * 60;
}

// How surely a file header and the start of the first hunk's range on the
// same side say that the file is absent; see PatchFile.  A /dev/null name
// counts even without hunks, so such a header still creates or deletes.
static int absence(std::string_view header, ptrdiff_t first_start)
{
    if (extract_path(header) == "/dev/null") return 2;
    if (first_start != 0) return 0;
    ptrdiff_t tab = str_find(header, '\t');
    if (tab >= 0 && is_epoch_timestamp(header.substr(checked_cast<size_t>(tab) + 1))) {
        return 2;
    }
    return 1;
}

static bool is_no_newline_marker(std::string_view line)
{
    return line.starts_with("\\ No newline at end of file") ||
           line.starts_with("\\ no newline at end of file");
}

// GNU patch's message for a line it cannot parse.  It prints the line with
// its newline, so the message ends in a blank line.
static std::string malformed(ptrdiff_t lineno, std::string_view line)
{
    return std::format("malformed patch at line {}: {}\n", lineno, line);
}

static std::string malformed(std::span<const std::string> lines, ptrdiff_t i)
{
    return malformed(i + 1, lines[checked_cast<size_t>(i)]);
}

// Parse a unified hunk header "@@ -start[,count] +start[,count] @@" as
// leniently as GNU patch: the spaces are optional, and anything may follow
// the first '@' of the closing "@@".  Only what follows "@@ " counts as the
// function, though.
static bool parse_unified_range(std::string_view s, PatchHunk &hunk)
{
    hunk.old_count = hunk.new_count = 1;
    if (!take(s, "@@ -") || !take_number(s, hunk.old_start)) return false;
    if (take(s, ",") && !take_number(s, hunk.old_count)) return false;
    take(s, " ");
    if (!take(s, "+") || !take_number(s, hunk.new_start)) return false;
    if (take(s, ",") && !take_number(s, hunk.new_count)) return false;
    take(s, " ");
    if (!s.starts_with("@")) return false;
    if (s.starts_with("@@ ")) hunk.function = s.substr(2);
    return true;
}

// Parse the unified hunk whose header is lines[i], advancing i past it.
// Like GNU patch, read exactly the lines that the header's counts call for,
// so a hunk that ends early, at a line that does not fit or at the end of
// the patch, is malformed.
static bool parse_unified_hunk(std::span<const std::string> lines, ptrdiff_t &i,
                               PatchHunk &hunk, std::string &error)
{
    if (!parse_unified_range(lines[checked_cast<size_t>(i)], hunk)) {
        error = malformed(lines, i);
        return false;
    }
    ++i;

    ptrdiff_t n = std::ssize(lines);
    ptrdiff_t old_left = hunk.old_count, new_left = hunk.new_count;
    while (old_left > 0 || new_left > 0) {
        // When the patch ends with at most three new lines missing, GNU
        // patch assumes that blank context lines were chopped off, and
        // blames the patch's last line if they do not fit
        std::string_view ln = " ";
        ptrdiff_t lineno = n;
        if (i < n) {
            lineno = i + 1;
            ln = lines[checked_cast<size_t>(i++)];
        } else if (new_left > 3) {
            error = "unexpected end of file in patch";
            return false;
        }

        if (ln.starts_with('#')) continue;  // GNU patch skips comments

        // A blank line, or one that starts with a tab, is a context line
        // whose leading space was lost.  GNU patch also takes '=' for ' '.
        std::string line;
        if (ln.empty() || ln[0] == '\t') {
            line = " " + std::string(ln);
        } else if (ln[0] == '=') {
            line = " " + std::string(ln.substr(1));
        } else {
            line = std::string(ln);
        }
        char mark = line[0];
        bool fits = mark == '-' ? old_left > 0
                  : mark == '+' ? new_left > 0
                  : mark == ' ' && old_left > 0 && new_left > 0;
        if (!fits) {
            error = malformed(lineno, ln);
            return false;
        }
        if (mark != '+') old_left--;
        if (mark != '-') new_left--;
        hunk.lines.push_back(std::move(line));

        // "\ No newline at end of file" applies to the line before it.  GNU
        // patch accepts it only after the last line of a side, but the
        // built-in diff has written it after earlier lines too.
        while (i < n && is_no_newline_marker(lines[checked_cast<size_t>(i)])) {
            if (mark != '+') hunk.old_no_newline = true;
            if (mark != '-') hunk.new_no_newline = true;
            ++i;
        }
    }
    return true;
}

// Parse a context hunk range "*** start[,end] ****" or "--- start[,end] ----",
// where mark is "***" or "---".  A lone number names one line, or none if
// it is 0.
static bool parse_context_range(std::string_view s, std::string_view mark,
                                ptrdiff_t &start, ptrdiff_t &count)
{
    if (!take(s, mark) || !take(s, " ") || !take_number(s, start)) return false;
    count = start ? 1 : 0;
    if (take(s, ",")) {
        ptrdiff_t end = 0;
        if (!take_number(s, end) || end < start) return false;
        count = end - start + 1;
    }
    return take(s, " ") && s.starts_with(mark);
}

// One section of a context hunk: lines with their markers (' ', '-', '+'
// or '!'), and whether the last line lacks a newline.
struct ContextLine {
    char mark;
    std::string_view text;
};

struct ContextSection {
    std::vector<ContextLine> lines;
    bool no_newline = false;
};

// Split a context hunk line "m text" whose marker m is one of marks.  diff -T
// puts a tab after the marker instead of a space, and a blank line can lose
// its trailing whitespace, leaving just the marker or nothing at all.
static bool split_context_line(std::string_view ln, std::string_view marks,
                               ContextLine &cl)
{
    if (ln.empty()) {
        cl = {' ', {}};
        return true;
    }
    if (str_find(marks, ln[0]) < 0) return false;
    if (std::ssize(ln) > 1 && ln[1] != ' ' && ln[1] != '\t') return false;
    cl = {ln[0], ln.substr(std::ssize(ln) > 1 ? 2 : 1)};
    return true;
}

// Parse the context hunk whose "***************" line is lines[i], advancing
// i past it, and convert it to unified form.  diff omits a section that has
// no changes, in which case the section is the other one's context lines.
static bool parse_context_hunk(std::span<const std::string> lines, ptrdiff_t &i,
                               PatchHunk &hunk, std::string &error)
{
    ptrdiff_t n = std::ssize(lines);
    auto at = [&](ptrdiff_t k) -> std::string_view {
        return lines[checked_cast<size_t>(k)];
    };

    // Like GNU patch, take a space after the stars to begin the function
    std::string_view stars = at(i);
    while (take(stars, "*")) {}
    if (stars.starts_with(' ')) hunk.function = stars;

    ++i;
    ptrdiff_t range_line = i;
    if (i >= n) {
        error = "unexpected end of file in patch";
        return false;
    }
    if (!parse_context_range(at(i), "***", hunk.old_start, hunk.old_count)) {
        error = malformed(lines, i);
        return false;
    }

    ContextSection old_sec, new_sec;
    for (++i; i < n && !at(i).starts_with("--- "); ++i) {
        ContextLine cl;
        if (is_no_newline_marker(at(i))) {
            old_sec.no_newline = true;
        } else if (split_context_line(at(i), " -!", cl)) {
            old_sec.lines.push_back(cl);
        } else {
            error = malformed(lines, i);
            return false;
        }
    }
    if (i >= n) {
        error = "unexpected end of file in patch";
        return false;
    }
    if (!parse_context_range(at(i), "---", hunk.new_start, hunk.new_count)) {
        error = malformed(lines, i);
        return false;
    }

    // Unlike the old section, the new section has no terminator, so stop
    // after the lines its range names.  A first line that is not part of
    // it means the section was omitted.  That includes a blank line, unless
    // a change ('!') in the old section requires a new section.
    bool may_omit = std::ranges::none_of(old_sec.lines, [](const ContextLine &cl) {
        return cl.mark == '!';
    });
    for (++i; i < n && std::ssize(new_sec.lines) < hunk.new_count; ++i) {
        ContextLine cl;
        if (is_no_newline_marker(at(i))) {
            new_sec.no_newline = true;
            continue;
        }
        if (new_sec.lines.empty() && at(i).empty() && may_omit) break;
        if (!split_context_line(at(i), " +!", cl)) {
            if (new_sec.lines.empty()) break;
            error = malformed(lines, i);
            return false;
        }
        new_sec.lines.push_back(cl);
    }
    if (i < n && is_no_newline_marker(at(i))) {
        new_sec.no_newline = true;
        ++i;
    }

    auto context_of = [](const ContextSection &sec) {
        ContextSection ctx;
        for (const auto &cl : sec.lines) {
            if (cl.mark == ' ') ctx.lines.push_back(cl);
        }
        ctx.no_newline = sec.no_newline && !sec.lines.empty() &&
                         sec.lines.back().mark == ' ';
        return ctx;
    };
    if (old_sec.lines.empty()) {
        old_sec = context_of(new_sec);
    } else if (new_sec.lines.empty()) {
        new_sec = context_of(old_sec);
    }

    // A lone line number with no lines names the empty range after that
    // line, as diff -C0 writes for a pure insertion or deletion.  The other
    // section has the change then, or the hunk ended before its lines.
    auto fits = [](ptrdiff_t &count, ptrdiff_t actual, ptrdiff_t other) {
        if (actual == 0 && count == 1 && other > 0) count = 0;
        return actual == count;
    };
    if (!fits(hunk.old_count, std::ssize(old_sec.lines), std::ssize(new_sec.lines)) ||
        !fits(hunk.new_count, std::ssize(new_sec.lines), std::ssize(old_sec.lines))) {
        error = std::format("replacement text or line numbers mangled in hunk at line {}",
                            range_line + 1);
        return false;
    }

    // Interleave the sections, pairing up their context lines
    ptrdiff_t old_n = std::ssize(old_sec.lines);
    ptrdiff_t new_n = std::ssize(new_sec.lines);
    ptrdiff_t o = 0, w = 0;
    for (;;) {
        for (; o < old_n && old_sec.lines[checked_cast<size_t>(o)].mark != ' '; ++o) {
            hunk.lines.push_back("-" + std::string(old_sec.lines[checked_cast<size_t>(o)].text));
        }
        for (; w < new_n && new_sec.lines[checked_cast<size_t>(w)].mark != ' '; ++w) {
            hunk.lines.push_back("+" + std::string(new_sec.lines[checked_cast<size_t>(w)].text));
        }
        if (o == old_n && w == new_n) break;
        if (o == old_n || w == new_n ||
            old_sec.lines[checked_cast<size_t>(o)].text !=
                new_sec.lines[checked_cast<size_t>(w)].text) {
            error = std::format("context mangled in hunk at line {}", range_line + 1);
            return false;
        }
        hunk.lines.push_back(" " + std::string(old_sec.lines[checked_cast<size_t>(o)].text));
        ++o;
        ++w;
    }
    hunk.old_no_newline = old_sec.no_newline;
    hunk.new_no_newline = new_sec.no_newline;
    for (const auto &cl : old_sec.lines) hunk.old_section.push_back(cl.mark + std::string(cl.text));
    for (const auto &cl : new_sec.lines) hunk.new_section.push_back(cl.mark + std::string(cl.text));
    return true;
}

// Swap a hunk's old and new sides, as patch -R does.
static void reverse_hunk(PatchHunk &hunk)
{
    std::swap(hunk.old_start, hunk.new_start);
    std::swap(hunk.old_count, hunk.new_count);
    std::swap(hunk.old_no_newline, hunk.new_no_newline);
    std::swap(hunk.old_section, hunk.new_section);
    for (auto *lines : {&hunk.lines, &hunk.old_section, &hunk.new_section}) {
        for (auto &line : *lines) {
            if (line[0] == '-') line[0] = '+';
            else if (line[0] == '+') line[0] = '-';
        }
    }
}

// Parse the lines of a complete unified or context diff into a list of
// per-file patch descriptions.  A hunk that does not parse ends parsing,
// with GNU patch's message for it in error.
static std::vector<PatchFile> parse_patch(std::span<const std::string> lines,
                                           int strip_level, bool reverse,
                                           std::string &error)
{
    std::vector<PatchFile> files;
    ptrdiff_t n = std::ssize(lines);
    auto at = [&](ptrdiff_t k) -> std::string_view {
        return lines[checked_cast<size_t>(k)];
    };

    ptrdiff_t i = 0;
    ptrdiff_t text_line = 1;
    while (i < n) {
        // A unified diff names the files on "--- " and "+++ " lines, and a
        // context diff on "*** " and "--- " lines before its first hunk
        bool unified = at(i).starts_with("--- ") &&
                       i + 1 < n && at(i + 1).starts_with("+++ ");
        bool context = at(i).starts_with("*** ") &&
                       i + 2 < n && at(i + 1).starts_with("--- ") &&
                       at(i + 2).starts_with("***************");
        if (!unified && !context) {
            ++i;
            continue;
        }

        PatchFile pf;
        std::string_view old_header = at(i).substr(4);
        std::string_view new_header = at(i + 1).substr(4);

        if (reverse) {
            std::swap(old_header, new_header);
        }

        std::string raw_old = extract_path(old_header);
        std::string raw_new = extract_path(new_header);
        pf.old_label = reject_label(old_header, strip_level);
        pf.new_label = reject_label(new_header, strip_level);
        pf.context = context;

        // Determine target path
        // Prefer new path like GNU patch does for the common -p0 case
        // where old has a .orig suffix (e.g., "--- f.txt.orig" / "+++ f.txt")
        if (raw_old == "/dev/null") {
            pf.target_path = strip_path(raw_new, strip_level);
        } else if (raw_new == "/dev/null") {
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

        i += 2;  // skip the file header lines
        pf.text_line = text_line;
        pf.hunk_line = i + 1;
        pf.crlf_line = unified ? i : i + 2;

        std::string_view hunk_start = unified ? "@@ " : "***************";
        while (i < n && at(i).starts_with(hunk_start)) {
            PatchHunk hunk;
            bool ok = unified ? parse_unified_hunk(lines, i, hunk, error)
                              : parse_context_hunk(lines, i, hunk, error);
            if (!ok) return files;
            if (reverse) reverse_hunk(hunk);
            pf.hunks.push_back(std::move(hunk));
        }

        // GNU patch judges from the first hunk alone
        const PatchHunk *first = pf.hunks.empty() ? nullptr : &pf.hunks[0];
        pf.old_absent = absence(old_header, first ? first->old_start : -1);
        pf.new_absent = absence(new_header, first ? first->new_start : -1);

        // Like GNU patch, ignore file headers with no hunk after them, so
        // the text leading up to the next file includes them
        if (!pf.hunks.empty()) {
            files.push_back(std::move(pf));
            text_line = i + 1;
        }
    }

    return files;
}

std::vector<std::string> patch_target_files(std::string_view patch_text,
                                            int strip_level, bool reverse)
{
    std::vector<std::string> result;
    std::string error;  // a patch that does not parse will not apply anyway
    auto lines = split_lines(patch_text);
    for (auto &pf : parse_patch(lines, strip_level, reverse, error)) {
        if (pf.target_path.empty()) continue;
        if (std::ranges::find(result, pf.target_path) != result.end()) continue;
        result.push_back(std::move(pf.target_path));
    }
    return result;
}

// Remove directories left empty by deleting path, like GNU patch.
static void remove_empty_parents(std::string_view path)
{
    for (std::string dir = dirname(path); dir != "." && dir != "/";
         dir = dirname(dir)) {
        if (!delete_dir(dir)) break;
    }
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
// position `pos` (0-based), skipping prefix_fuzz lines at its top and
// suffix_fuzz at its bottom.  Returns true if the pattern matches.
static bool try_match(std::span<const std::string> file_lines,
                      ptrdiff_t pos,
                      const std::vector<PatternLine> &pattern,
                      ptrdiff_t prefix_fuzz,
                      ptrdiff_t suffix_fuzz)
{
    ptrdiff_t pat_len = std::ssize(pattern);
    if (pat_len == 0) return true;

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

// 0-based file position of the hunk's old range.  An empty range names the
// line it follows, as in "@@ -5,0 +6 @@" or "*** 5 ****" from diff -U0/-C0.
static ptrdiff_t old_range_pos(const PatchHunk &hunk)
{
    if (hunk.old_count == 0) return hunk.old_start;
    return std::max(hunk.old_start, ptrdiff_t{1}) - 1;
}

// Spiral search: find where a hunk matches in the file, as GNU patch does,
// at each fuzz level up to max_fuzz in turn.  Returns the 0-based file
// position, with fuzz_used set to the fuzz it matched with, or -1 if not
// found.
//
// Lines before last_frozen_line are frozen: the hunks before have copied
// them to the output or deleted them.  Search like GNU patch, which may
// find a hunk among the frozen lines: return the guess for an empty
// pattern, and for a guess among the frozen lines, try first as far before
// the guess as the frozen lines reach past it, then the first line not
// frozen, then each line up from the first.  The caller fails a hunk found
// where it would change a frozen line.
static ptrdiff_t locate_hunk(std::span<const std::string> file_lines,
                              const PatchHunk &hunk,
                              const std::vector<PatternLine> &pattern,
                              ptrdiff_t last_frozen_line,
                              ptrdiff_t cumulative_offset,
                              int max_fuzz,
                              ptrdiff_t &fuzz_used)
{
    ptrdiff_t file_len = std::ssize(file_lines);
    ptrdiff_t pat_old_count = std::ssize(pattern);

    // Get real prefix/suffix context from full hunk (not just old-side pattern)
    auto ctx = get_hunk_context(hunk);

    // First guess: the position the hunk header names, plus the offset
    ptrdiff_t first_guess = old_range_pos(hunk) + cumulative_offset;

    if (pat_old_count == 0) {
        return first_guess < 0 ? -1 : first_guess;
    }

    // Like GNU patch, fuzz no more than the hunk has context, and skip
    // context at the end of the hunk with more of it first.  Until fuzz
    // reaches the end with less, a hunk with less context at its start
    // must start the file when its header puts it on line 1, and a hunk
    // with less at its end must end the file.
    ptrdiff_t context = std::max(ctx.prefix, ctx.suffix);
    ptrdiff_t top_fuzz = std::min(ptrdiff_t{max_fuzz}, context);
    for (ptrdiff_t fuzz = 0; fuzz <= top_fuzz; ++fuzz) {
        fuzz_used = fuzz;
        ptrdiff_t prefix_fuzz = fuzz + ctx.prefix - context;
        ptrdiff_t suffix_fuzz = fuzz + ctx.suffix - context;
        if (prefix_fuzz < 0 && old_range_pos(hunk) == 0) {
            if (last_frozen_line <= ctx.prefix &&
                try_match(file_lines, 0, pattern, 0, suffix_fuzz)) {
                return 0;
            }
            continue;
        }
        prefix_fuzz = std::max(prefix_fuzz, ptrdiff_t{0});
        if (suffix_fuzz < 0) {
            ptrdiff_t pos = file_len - pat_old_count;
            if (pos >= 0 && pos >= last_frozen_line &&
                try_match(file_lines, pos, pattern, prefix_fuzz, 0)) {
                return pos;
            }
            continue;
        }

        // The last position where the lines to match fit, which counts the
        // fuzzed prefix, as GNU patch does
        ptrdiff_t max_search = file_len - (pat_old_count - suffix_fuzz);

        if (first_guess < last_frozen_line && first_guess <= max_search) {
            // A first guess of 0 or less reaches no line before it
            ptrdiff_t lowest = first_guess > 0 ? 2 * first_guess - last_frozen_line : -1;
            if (lowest >= 0 &&
                try_match(file_lines, lowest, pattern, prefix_fuzz, suffix_fuzz)) {
                return lowest;
            }
            if (try_match(file_lines, last_frozen_line, pattern, prefix_fuzz, suffix_fuzz)) {
                return last_frozen_line;
            }
            for (ptrdiff_t pos = std::max(lowest + 1, ptrdiff_t{0}); pos <= max_search; ++pos) {
                if (try_match(file_lines, pos, pattern, prefix_fuzz, suffix_fuzz)) {
                    return pos;
                }
            }
            continue;
        }

        // Try exact position first
        if (first_guess >= 0 && first_guess <= max_search &&
            first_guess > last_frozen_line - 1) {
            if (try_match(file_lines, first_guess, pattern, prefix_fuzz, suffix_fuzz)) {
                return first_guess;
            }
        }

        // Spiral outward.  Start at the first offset that reaches a
        // position the checks below accept, so a guess far past the end
        // of the file, from a hunk header with a huge line number, doesn't
        // step through every line in between.
        ptrdiff_t max_offset_forward = max_search - first_guess;
        ptrdiff_t max_offset_backward = first_guess - last_frozen_line;
        ptrdiff_t max_range = std::max(max_offset_forward, max_offset_backward);
        if (max_range < 0) max_range = 0;
        ptrdiff_t min_range = std::max({ptrdiff_t{1},
                                        last_frozen_line - first_guess,
                                        first_guess - max_search});

        for (ptrdiff_t delta = min_range; delta <= max_range; ++delta) {
            // Each direction is bounded before its position is computed,
            // so a guess from a huge line number cannot overflow.

            // Try forward
            if (delta <= max_offset_forward) {
                ptrdiff_t pos = first_guess + delta;
                if (pos >= 0 && pos <= max_search && pos > last_frozen_line - 1) {
                    if (try_match(file_lines, pos, pattern, prefix_fuzz, suffix_fuzz)) {
                        return pos;
                    }
                }
            }

            // Try backward
            if (delta <= max_offset_backward) {
                ptrdiff_t pos = first_guess - delta;
                if (pos >= 0 && pos <= max_search && pos > last_frozen_line - 1) {
                    if (try_match(file_lines, pos, pattern, prefix_fuzz, suffix_fuzz)) {
                        return pos;
                    }
                }
            }
        }
    }

    return -1;  // no match found
}

// ── Hunk application ───────────────────────────────────────────────────

// Build the output file content after applying all successfully matched hunks.
// hunks_positions[i] = 0-based file position where hunk i matched, or -1 if rejected.
// Like GNU patch, copy the file up to each change and write only the added
// lines from the patch, so context, matched or fuzzed, comes from the file,
// and a hunk may start among the trailing context of the hunk before.
static std::string build_output(std::span<const std::string> file_lines,
                                 bool has_trailing_newline,
                                 const PatchFile &pf,
                                 const std::vector<ptrdiff_t> &hunk_positions)
{
    std::string output;
    ptrdiff_t file_len = std::ssize(file_lines);
    ptrdiff_t copied = 0;       // file lines copied or deleted so far
    bool after_newline = true;  // whether output ends at a line's end

    // Like GNU patch, end a line left incomplete before writing another
    auto put = [&](std::string_view line, bool newline) {
        if (!after_newline) output += '\n';
        output += line;
        if (newline) output += '\n';
        after_newline = newline;
    };
    auto copy_till = [&](ptrdiff_t end) {
        for (; copied < std::min(end, file_len); ++copied) {
            put(file_lines[checked_cast<size_t>(copied)],
                copied < file_len - 1 || has_trailing_newline);
        }
    };

    for (ptrdiff_t h = 0; h < std::ssize(pf.hunks); ++h) {
        ptrdiff_t pos = hunk_positions[checked_cast<size_t>(h)];
        if (pos < 0) continue;  // rejected hunk, skip

        const auto &hunk = pf.hunks[checked_cast<size_t>(h)];
        ptrdiff_t nlines = std::ssize(hunk.lines);
        ptrdiff_t last_new = nlines - 1;  // last new-side line, if any
        while (last_new >= 0 && hunk.lines[checked_cast<size_t>(last_new)][0] == '-') {
            --last_new;
        }

        ptrdiff_t old = pos;  // file line of the next old-side line
        for (ptrdiff_t j = 0; j < nlines; ++j) {
            std::string_view line = hunk.lines[checked_cast<size_t>(j)];
            if (line[0] == ' ') {
                ++old;
            } else if (line[0] == '-') {
                copy_till(old);
                copied = ++old;
            } else {
                copy_till(old);
                put(line.substr(1), !(j == last_new && hunk.new_no_newline));
            }
        }
    }

    copy_till(file_len);
    return output;
}

// ── Merging ────────────────────────────────────────────────────────────
//
// Merge mode follows GNU patch's merge.c, with the bestmatch.h and the
// gnulib diffseq.h it builds on.  A hunk that matches exactly, without
// fuzz, applies there.  Any other goes where its old lines best match the
// file, a diff lines the two up, and each change in the hunk merges when
// the file has its old lines, is left alone when the file already has its
// new lines, and otherwise becomes a conflict.  Like the rest of the
// engine, lines compare without their line endings.

// A line on one side of a hunk: its mark, ' ' or '-' on the old side and
// ' ' or '+' on the new, and its text.  Each side ends with a sentinel
// marked '=' on the old side and '^' on the new.
struct MergeLine {
    char mark;
    std::string_view text;
};

// Whether file line k, from 0, exists and is text
static bool file_line_is(std::span<const std::string> file, ptrdiff_t k,
                         std::string_view text)
{
    return k >= 0 && k < std::ssize(file) && file[checked_cast<size_t>(k)] == text;
}

// GNU patch's bestmatch(): the fewest changes, at most max, that turn old
// lines [xoff, xlim) into file lines [yoff, *py) while matching at least
// min lines, with *py as far as those changes reach, or max + 1 if none
// do.  Lines count from 1, as in GNU patch: its check of min compares
// against xoff - yoff where xoff + yoff belongs, so it depends on them.
static ptrdiff_t bestmatch(std::span<const MergeLine> old,
                           std::span<const std::string> file,
                           ptrdiff_t xoff, ptrdiff_t xlim,
                           ptrdiff_t yoff, ptrdiff_t ylim,
                           ptrdiff_t min, ptrdiff_t max, ptrdiff_t *py)
{
    auto equal = [&](ptrdiff_t x, ptrdiff_t y) {
        return file_line_is(file, y - 1, old[checked_cast<size_t>(x - 1)].text);
    };
    const ptrdiff_t dmin = xoff - ylim;  // minimum valid diagonal
    const ptrdiff_t dmax = xlim - yoff;  // maximum valid diagonal
    const ptrdiff_t fmid = xoff - yoff;  // center diagonal
    ptrdiff_t fmin = fmid;
    ptrdiff_t fmax = fmid;
    ptrdiff_t ymax = -1;

    // How far along each diagonal the search has reached, in x, for the
    // diagonals that max changes can reach
    std::vector<ptrdiff_t> fdiag(checked_cast<size_t>(2 * max + 3), -1);
    auto fd = [&](ptrdiff_t d) -> ptrdiff_t & {
        return fdiag[checked_cast<size_t>(d - fmid + max + 1)];
    };

    ptrdiff_t fmid_plus_2_min = 0;
    if (min) {
        fmid_plus_2_min = fmid + 2 * min;
        min += yoff;
        if (min > ylim) return max + 1;
    }

    // Handle the exact match
    while (xoff < xlim && yoff < ylim && equal(xoff, yoff)) {
        xoff++;
        yoff++;
    }
    if (xoff == xlim && yoff >= min && xoff + yoff >= fmid_plus_2_min) {
        *py = yoff;
        return 0;
    }

    fd(fmid) = xoff;
    for (ptrdiff_t c = 1; c <= max; c++) {
        if (fmin > dmin) fd(--fmin - 1) = -1;
        else ++fmin;
        if (fmax < dmax) fd(++fmax + 1) = -1;
        else --fmax;
        for (ptrdiff_t d = fmax; d >= fmin; d -= 2) {
            ptrdiff_t x = fd(d - 1) < fd(d + 1) ? fd(d + 1) : fd(d - 1) + 1;
            ptrdiff_t y = x - d;
            while (x < xlim && y < ylim && equal(x, y)) {
                x++;
                y++;
            }
            fd(d) = x;
            if (x == xlim && y >= min && x + y - c >= fmid_plus_2_min) {
                ymax = std::max(ymax, y);
                if (y == ylim) break;
            }
        }
        if (ymax != -1) {
            *py = ymax;
            return c;
        }
    }
    return max + 1;
}

// GNU patch's locate_merge(): the line, from 0, where a hunk that does not
// match exactly best matches the file, and in matched how many file lines
// from there its old lines match, 0 when none match well enough.  Like
// GNU patch, it prefers the longest match closest to where the hunk
// should be, and holds a hunk with less context after its changes than
// before to the end of the file.
static ptrdiff_t locate_merge(std::span<const std::string> file,
                              const PatchHunk &hunk,
                              std::span<const MergeLine> old,
                              ptrdiff_t last_frozen_line,
                              ptrdiff_t cumulative_offset,
                              ptrdiff_t &matched)
{
    ptrdiff_t input_lines = std::ssize(file);
    ptrdiff_t pch_first = old_range_pos(hunk) + 1;
    ptrdiff_t first_guess = pch_first + cumulative_offset;
    ptrdiff_t pat_lines = std::ssize(old);
    ptrdiff_t context_lines = std::ranges::count(old, ' ', &MergeLine::mark);
    ptrdiff_t min_where = last_frozen_line + 1;
    ptrdiff_t max_pos_offset = input_lines - pat_lines + context_lines + 1 - first_guess;
    ptrdiff_t max_neg_offset = first_guess - min_where;
    ptrdiff_t max_offset = std::max(max_pos_offset, max_neg_offset);
    ptrdiff_t where = first_guess;
    matched = 0;

    if (context_lines > 0) {
        // Allow at most context_lines lines to be replaced, and require
        // the remaining lines to match
        ptrdiff_t max = 2 * context_lines;
        ptrdiff_t min = pat_lines - context_lines;

        // Hunks from the start or end of the file have less context, so
        // anchor them there
        auto ctx = get_hunk_context(hunk);
        if (ctx.suffix > ctx.prefix && pch_first <= 1) max_pos_offset = 0;
        bool match_until_eof = ctx.suffix < ctx.prefix;

        // Do not try lines before the first
        if (first_guess <= max_neg_offset) max_neg_offset = first_guess - 1;

        // Whether guess matches exactly, after keeping it if it matches
        // more lines than any match so far
        auto try_guess = [&](ptrdiff_t guess) {
            ptrdiff_t last = 0;
            ptrdiff_t changes = bestmatch(
                old, file, 1, pat_lines + 1, guess, input_lines + 1,
                match_until_eof ? input_lines - guess + 1 : min, max, &last);
            if (changes > max || last - guess <= matched) return false;
            matched = last - guess;
            where = guess;
            min = matched;
            max = changes - 1;
            return changes == 0;
        };
        // Start at the first offset whose guess could win, so that a hunk
        // header with a huge line number does not step through every line
        // in between.  A match from bestmatch() ends at most pat_lines past
        // the end of the file, and one from a guess before line 1 - max
        // matches no line of the file, which it allows only from line -1.
        ptrdiff_t lowest = std::min(1 - max, ptrdiff_t{-1});
        ptrdiff_t highest = input_lines + pat_lines;
        ptrdiff_t start = PTRDIFF_MAX;
        if (first_guess <= highest) start = std::max(ptrdiff_t{0}, lowest - first_guess);
        if (first_guess >= lowest) {
            start = std::min(start, std::max(ptrdiff_t{1}, first_guess - highest));
        }

        for (ptrdiff_t offset = start; offset <= max_offset; offset++) {
            if (offset <= max_pos_offset && try_guess(first_guess + offset)) break;
            if (offset > 0 && offset <= max_neg_offset && try_guess(first_guess - offset)) break;
        }
    }

    return std::max(where, min_where) - 1;
}

// The diff of gnulib's diffseq, which GNU patch's merge uses to line up a
// hunk's old lines with the file lines they matched: it marks with '-'
// each old line the file lines lack, and with '+' each file line the old
// lines lack.  GNU patch sets no limit on the cost of the search, so the
// diff is minimal, except past a cost of 200, where diffseq turns to a
// heuristic that this leaves out, as a hunk would need hundreds of
// context lines to get there.
struct MergeDiff {
    std::span<const MergeLine> old;
    std::span<const std::string> file;
    ptrdiff_t base;               // file line where the in lines start
    std::vector<char> old_marks;  // per old line, then '='
    std::vector<char> in_marks;   // per matched file line, then '^'
    std::vector<ptrdiff_t> fdiag; // furthest x per diagonal, top down
    std::vector<ptrdiff_t> bdiag; // furthest x per diagonal, bottom up
    ptrdiff_t doff;               // index of diagonal 0

    MergeDiff(std::span<const MergeLine> old_lines,
              std::span<const std::string> file_lines,
              ptrdiff_t where, ptrdiff_t matched)
        : old(old_lines), file(file_lines), base(where),
          old_marks(checked_cast<size_t>(std::ssize(old_lines) + 1), ' '),
          in_marks(checked_cast<size_t>(matched + 1), ' '),
          fdiag(checked_cast<size_t>(std::ssize(old_lines) + matched + 3)),
          bdiag(fdiag.size()),
          doff(matched + 1)
    {
        old_marks.back() = '=';
        in_marks.back() = '^';
    }

    bool equal(ptrdiff_t x, ptrdiff_t y) const
    {
        return file_line_is(file, base + y, old[checked_cast<size_t>(x)].text);
    }
    ptrdiff_t &fd(ptrdiff_t d) { return fdiag[checked_cast<size_t>(d + doff)]; }
    ptrdiff_t &bd(ptrdiff_t d) { return bdiag[checked_cast<size_t>(d + doff)]; }

    // diffseq's diag(): the midpoint of the shortest edit script for old
    // lines [xoff, xlim) and in lines [yoff, ylim), searching from both
    // ends at once until the searches meet
    void diag(ptrdiff_t xoff, ptrdiff_t xlim, ptrdiff_t yoff, ptrdiff_t ylim,
              ptrdiff_t &xmid, ptrdiff_t &ymid)
    {
        const ptrdiff_t dmin = xoff - ylim;  // minimum valid diagonal
        const ptrdiff_t dmax = xlim - yoff;  // maximum valid diagonal
        const ptrdiff_t fmid = xoff - yoff;  // center diagonal, top down
        const ptrdiff_t bmid = xlim - ylim;  // center diagonal, bottom up
        ptrdiff_t fmin = fmid;
        ptrdiff_t fmax = fmid;
        ptrdiff_t bmin = bmid;
        ptrdiff_t bmax = bmid;
        bool odd = ((fmid - bmid) & 1) != 0;

        fd(fmid) = xoff;
        bd(bmid) = xlim;
        for (;;) {
            // Extend the top-down search by an edit step in each diagonal
            if (fmin > dmin) fd(--fmin - 1) = -1;
            else ++fmin;
            if (fmax < dmax) fd(++fmax + 1) = -1;
            else --fmax;
            for (ptrdiff_t d = fmax; d >= fmin; d -= 2) {
                ptrdiff_t tlo = fd(d - 1);
                ptrdiff_t thi = fd(d + 1);
                ptrdiff_t x = tlo < thi ? thi : tlo + 1;
                ptrdiff_t y = x - d;
                while (x < xlim && y < ylim && equal(x, y)) {
                    x++;
                    y++;
                }
                fd(d) = x;
                if (odd && bmin <= d && d <= bmax && bd(d) <= x) {
                    xmid = x;
                    ymid = y;
                    return;
                }
            }

            // Likewise extend the bottom-up search
            if (bmin > dmin) bd(--bmin - 1) = PTRDIFF_MAX;
            else ++bmin;
            if (bmax < dmax) bd(++bmax + 1) = PTRDIFF_MAX;
            else --bmax;
            for (ptrdiff_t d = bmax; d >= bmin; d -= 2) {
                ptrdiff_t tlo = bd(d - 1);
                ptrdiff_t thi = bd(d + 1);
                ptrdiff_t x = tlo < thi ? tlo : thi - 1;
                ptrdiff_t y = x - d;
                while (xoff < x && yoff < y && equal(x - 1, y - 1)) {
                    x--;
                    y--;
                }
                bd(d) = x;
                if (!odd && fmin <= d && d <= fmax && x <= fd(d)) {
                    xmid = x;
                    ymid = y;
                    return;
                }
            }
        }
    }

    // diffseq's compareseq(): mark the differences between old lines
    // [xoff, xlim) and in lines [yoff, ylim)
    void compareseq(ptrdiff_t xoff, ptrdiff_t xlim, ptrdiff_t yoff, ptrdiff_t ylim)
    {
        for (;;) {
            // Slide down the bottom initial diagonal, and up the top one
            while (xoff < xlim && yoff < ylim && equal(xoff, yoff)) {
                xoff++;
                yoff++;
            }
            while (xoff < xlim && yoff < ylim && equal(xlim - 1, ylim - 1)) {
                xlim--;
                ylim--;
            }

            if (xoff == xlim) {
                for (; yoff < ylim; yoff++) in_marks[checked_cast<size_t>(yoff)] = '+';
                return;
            }
            if (yoff == ylim) {
                for (; xoff < xlim; xoff++) old_marks[checked_cast<size_t>(xoff)] = '-';
                return;
            }

            // Split at the midpoint, recursing into the smaller half
            ptrdiff_t xmid, ymid;
            diag(xoff, xlim, yoff, ylim, xmid, ymid);
            if ((xlim + ylim) - (xmid + ymid) < (xmid + ymid) - (xoff + yoff)) {
                compareseq(xmid, xlim, ymid, ylim);
                xlim = xmid;
                ylim = ymid;
            } else {
                compareseq(xoff, xmid, yoff, ymid);
                xoff = xmid;
                yoff = ymid;
            }
        }
    }
};

// GNU patch's merge_result(): report how a change in a hunk merged, the
// first time as "Hunk #N <what> at <lines>", and after that on the same
// line.  Like GNU patch, list a result of the same kind as the first with
// just a comma, even after results of other kinds.
struct MergeReport {
    std::string &out;
    ptrdiff_t hunk;
    std::string_view first_what = {};

    void add(std::string_view what, ptrdiff_t from, ptrdiff_t to)
    {
        if (first_what.empty()) {
            out += std::format("Hunk #{} {} at ", hunk, what);
            first_what = what;
        } else if (what == first_what) {
            out += ',';
        } else {
            out += std::format(", {} at ", what);
        }
        out += to <= from ? std::format("{}", from) : std::format("{}-{}", from, to);
    }

    void finish()
    {
        if (!first_what.empty()) out += ".\n";
    }
};

// Merge each hunk of pf into the file's lines, like GNU patch --merge,
// reporting what did not apply as it is.  Returns the merged text, with
// conflicts set when some change did not merge.  Like GNU patch, reject
// a hunk found where it would change a line already merged, and set
// rejected[h] for it, with offsets[h] the lines to move it by.
static std::string merge_hunks(const FileContent &fc, const PatchFile &pf,
                               const PatchOptions &opts, std::string &out,
                               bool &conflicts, std::vector<bool> &rejected,
                               std::vector<ptrdiff_t> &offsets)
{
    std::span<const std::string> file = fc.lines;
    ptrdiff_t file_len = std::ssize(file);
    bool diff3 = opts.merge_style == "diff3";

    std::string text;
    bool after_newline = true;       // whether text ends with a newline
    ptrdiff_t last_frozen_line = 0;  // file lines copied to text so far
    ptrdiff_t cumulative_offset = 0;
    // Like GNU patch, move the lines reported by the lines that hunks and
    // conflicts added, but not by those that hunks deleted
    ptrdiff_t out_offset = 0;

    // Copy the file's lines before line n, from 0, to the text, first
    // ending a last line written without a newline
    auto copy_till = [&](ptrdiff_t n) {
        for (; last_frozen_line < std::min(n, file_len); ++last_frozen_line) {
            if (!after_newline) text += '\n';
            text += file[checked_cast<size_t>(last_frozen_line)];
            after_newline = last_frozen_line < file_len - 1 || fc.has_trailing_newline;
            if (after_newline) text += '\n';
        }
        last_frozen_line = std::max(last_frozen_line, n);
    };
    auto write_line = [&](std::string_view line, bool newline) {
        text += line;
        if (newline) text += '\n';
        after_newline = newline;
    };
    auto write_marker = [&](std::string_view marker) {
        if (!after_newline) text += '\n';
        text += marker;
        text += '\n';
        after_newline = true;
    };

    for (ptrdiff_t h = 0; h < std::ssize(pf.hunks); ++h) {
        const auto &hunk = pf.hunks[checked_cast<size_t>(h)];

        // Split the hunk into its sides
        std::vector<MergeLine> old_lines, new_lines;
        for (const auto &line : hunk.lines) {
            std::string_view body = std::string_view(line).substr(1);
            if (line[0] != '+') old_lines.push_back({line[0], body});
            if (line[0] != '-') new_lines.push_back({line[0], body});
        }
        ptrdiff_t old_len = std::ssize(old_lines);
        ptrdiff_t new_len = std::ssize(new_lines);
        old_lines.push_back({'=', {}});
        new_lines.push_back({'^', {}});
        auto old_side = std::span<const MergeLine>(old_lines).first(checked_cast<size_t>(old_len));
        auto old_char = [&](ptrdiff_t k) { return old_lines[checked_cast<size_t>(k)].mark; };
        auto new_char = [&](ptrdiff_t k) { return new_lines[checked_cast<size_t>(k)].mark; };
        auto write_old = [&](ptrdiff_t k) {
            write_line(old_lines[checked_cast<size_t>(k)].text,
                       !(hunk.old_no_newline && k == old_len - 1));
        };
        auto write_new = [&](ptrdiff_t k) {
            write_line(new_lines[checked_cast<size_t>(k)].text,
                       !(hunk.new_no_newline && k == new_len - 1));
        };

        // Apply the hunk where it matches exactly, or else merge it where
        // it matches best
        ptrdiff_t matched = old_len;
        ptrdiff_t no_fuzz = 0;
        ptrdiff_t where = locate_hunk(file, hunk, get_old_pattern(hunk),
                                      last_frozen_line, cumulative_offset, 0, no_fuzz);
        bool applies_cleanly = where >= 0;
        if (applies_cleanly) {
            cumulative_offset = where - old_range_pos(hunk);

            // Like GNU patch, which finds a hunk among the lines merged
            // already as it does outside merge mode, fail one whose changes
            // start among them.  GNU patch notices only once it has copied
            // the hunk's leading context, and with none, fails an assertion.
            auto ctx = get_hunk_context(hunk);
            if (ctx.prefix < std::ssize(hunk.lines) && where + ctx.prefix < last_frozen_line) {
                out += "misordered hunks! output would be garbled\n";
                if (!opts.quiet) {
                    out += std::format("Hunk #{} FAILED at {}.\n", h + 1, where + 1 + out_offset);
                }
                rejected[checked_cast<size_t>(h)] = true;
                offsets[checked_cast<size_t>(h)] = out_offset;
                continue;
            }
        } else {
            where = locate_merge(file, hunk, old_side, last_frozen_line,
                                 cumulative_offset, matched);
        }

        // Line up the old lines with the file lines from where
        MergeDiff md(old_side, file, where, matched);
        md.compareseq(0, old_len, 0, matched);
        auto old_diff = [&](ptrdiff_t k) { return md.old_marks[checked_cast<size_t>(k)]; };
        auto in_diff = [&](ptrdiff_t k) { return md.in_marks[checked_cast<size_t>(k)]; };

        // Walk the old lines, the new lines, and the file lines ("in") in
        // step, a run of lines at a time
        MergeReport report{out, h + 1};
        copy_till(where);
        ptrdiff_t old = 0, neu = 0, in = 0;
        for (;;) {
            ptrdiff_t first_old = old, first_new = neu, first_in = in;
            bool conflict = false;

            if (old_char(old) == '-' || new_char(neu) == '+') {
                // A change merges when the file has its old lines, and no
                // lines among them
                while (!conflict && old_char(old) == '-') {
                    if (old_diff(old) == '-' || in_diff(in) == '+') {
                        conflict = true;
                    } else {
                        ++in;
                        ++old;
                    }
                }
                conflict = conflict || old_diff(old) == '-' || in_diff(in) == '+';
                if (!conflict) {
                    while (new_char(neu) == '+') ++neu;
                    ptrdiff_t lines = neu - first_new;
                    if (!opts.quiet && !applies_cleanly) {
                        report.add("merged", where + 1 + out_offset, where + lines + out_offset);
                    }
                    last_frozen_line += old - first_old;
                    where += old - first_old;
                    out_offset += lines;
                    for (ptrdiff_t k = first_new; k < neu; ++k) write_new(k);
                    continue;
                }
            } else if (old_char(old) == ' ') {
                if (old_diff(old) == '-') {
                    // Context the file lacks drops out, unless a change
                    // comes next
                    while (old_char(old) == ' ' && old_diff(old) == '-') {
                        if (new_char(neu) == '+') {
                            conflict = true;
                            break;
                        }
                        ++old;
                        ++neu;
                    }
                    conflict = conflict || old_char(old) == '-' || new_char(neu) == '+';
                    if (!conflict) continue;
                } else if (in_diff(in) == '+') {
                    // File lines the hunk lacks stay
                    while (in_diff(in) == '+') ++in;
                    where += in - first_in;
                    copy_till(where);
                    continue;
                } else {
                    // Context the file has stays
                    while (old_char(old) == ' ' && old_diff(old) == ' ' &&
                           new_char(neu) == ' ' && in_diff(in) == ' ') {
                        ++old;
                        ++neu;
                        ++in;
                    }
                    where += in - first_in;
                    copy_till(where);
                    continue;
                }
            } else {
                break;  // both sides are done
            }

            // Find the end of the conflict
            for (;;) {
                if (old_char(old) == '-') {
                    while (in_diff(in) == '+') ++in;
                    if (old_diff(old) == ' ') ++in;
                    ++old;
                } else if (old_diff(old) == '-') {
                    while (new_char(neu) == '+') ++neu;
                    ++neu;  // the context line
                    ++old;
                } else if (new_char(neu) == '+') {
                    while (new_char(neu) == '+') ++neu;
                } else if (in_diff(in) == '+') {
                    while (in_diff(in) == '+') ++in;
                } else {
                    break;
                }
            }

            // Keep the new lines that the file has at the start of the
            // conflict.  When it has all of them, the change is already
            // applied.
            ptrdiff_t last = where;
            while (first_in < in && first_new < neu &&
                   file_line_is(file, last, new_lines[checked_cast<size_t>(first_new)].text)) {
                ++first_in;
                ++first_new;
                ++last;
            }
            bool applied = first_in == in && first_new == neu;
            if (applied) {
                report.add("already applied", where + 1 + out_offset, last + out_offset);
            } else if (diff3) {
                // diff3 conflicts keep those lines on both sides.  GNU
                // patch does this for a change already applied too, but
                // then loses its place in the file, and merges the hunk's
                // later changes into the wrong lines.
                ptrdiff_t common_prefix = last - where;
                first_in -= common_prefix;
                first_new -= common_prefix;
                last = where;
            }
            where = last;
            copy_till(where);
            if (applied) continue;

            // Other conflicts set aside the new lines that the file has
            // at the end
            ptrdiff_t common_suffix = 0;
            if (!diff3) {
                for (last = where + (in - first_in);
                     first_in < in && first_new < neu &&
                     file_line_is(file, last - 1, new_lines[checked_cast<size_t>(neu - 1)].text);
                     --in, --neu, --last) {
                    ++common_suffix;
                }
            }

            ptrdiff_t lines = 3 + (in - first_in) + (neu - first_new);
            if (diff3) lines += 1 + (old - first_old);
            report.add("NOT MERGED", where + 1 + out_offset, where + lines + out_offset);
            out_offset += lines - (in - first_in);

            write_marker("<<<<<<<");
            where += in - first_in;
            copy_till(where);
            if (diff3) {
                write_marker("|||||||");
                for (ptrdiff_t k = first_old; k < old; ++k) write_old(k);
            }
            write_marker("=======");
            for (ptrdiff_t k = first_new; k < neu; ++k) write_new(k);
            write_marker(">>>>>>>");

            where += common_suffix;
            copy_till(where);
            in += common_suffix;
            neu += common_suffix;
            conflicts = true;
        }
        report.finish();
    }

    copy_till(file_len);
    return text;
}

// ── Reject file generation ─────────────────────────────────────────────

// A unified diff range as GNU patch writes it, with no count for one line.
static std::string reject_unified_range(ptrdiff_t start, ptrdiff_t count)
{
    if (count == 1) return std::format("{}", start);
    return std::format("{},{}", start, count);
}

// A context diff range as GNU patch writes it, from first to last line,
// either alone for one line, or 0 for none.
static std::string reject_context_range(ptrdiff_t start, ptrdiff_t count)
{
    if (count == 0) return "0";
    if (count == 1) return std::format("{}", start);
    return std::format("{},{}", start, start + count - 1);
}

// Format a file's rejected hunks for its .rej file like GNU patch does: as
// a diff of the same form, with each hunk moved by the lines that the hunks
// applied before it added, offsets[h].  GNU patch runs a line that lacks a
// newline into the next one, though, where this marks it as diff does.
static std::string format_rejects(const PatchFile &pf,
                                  const std::vector<bool> &rejected,
                                  const std::vector<ptrdiff_t> &offsets)
{
    static constexpr std::string_view no_newline = "\\ No newline at end of file\n";
    std::string result = pf.context
        ? "*** " + pf.old_label + "\n--- " + pf.new_label + "\n"
        : "--- " + pf.old_label + "\n+++ " + pf.new_label + "\n";

    for (ptrdiff_t h = 0; h < std::ssize(pf.hunks); ++h) {
        if (!rejected[checked_cast<size_t>(h)]) continue;
        const auto &hunk = pf.hunks[checked_cast<size_t>(h)];
        ptrdiff_t offset = offsets[checked_cast<size_t>(h)];

        if (pf.context) {
            auto add_section = [&](std::span<const std::string> section, bool no_nl) {
                for (const auto &line : section) {
                    result += line[0];
                    result += ' ';
                    result += std::string_view(line).substr(1);
                    result += '\n';
                }
                if (no_nl && !section.empty()) result += no_newline;
            };
            result += "***************" + hunk.function + "\n";
            result += "*** " + reject_context_range(hunk.old_start + offset, hunk.old_count) + " ****\n";
            add_section(hunk.old_section, hunk.old_no_newline);
            result += "--- " + reject_context_range(hunk.new_start + offset, hunk.new_count) + " ----\n";
            add_section(hunk.new_section, hunk.new_no_newline);
            continue;
        }

        result += std::format("@@ -{} +{} @@{}\n",
                              reject_unified_range(hunk.old_start + offset, hunk.old_count),
                              reject_unified_range(hunk.new_start + offset, hunk.new_count),
                              hunk.function);

        // The last line on each side, which alone may lack a newline
        ptrdiff_t n = std::ssize(hunk.lines);
        ptrdiff_t last_old = -1, last_new = -1;
        for (ptrdiff_t k = 0; k < n; ++k) {
            char mark = hunk.lines[checked_cast<size_t>(k)][0];
            if (mark != '+') last_old = k;
            if (mark != '-') last_new = k;
        }
        auto add_line = [&](ptrdiff_t k) {
            result += hunk.lines[checked_cast<size_t>(k)];
            result += '\n';
            if ((k == last_old && hunk.old_no_newline) ||
                (k == last_new && hunk.new_no_newline)) {
                result += no_newline;
            }
        };

        // Like GNU patch, list each change's deletions before its additions
        for (ptrdiff_t k = 0; k < n; ++k) {
            ptrdiff_t end = k;
            while (end < n && hunk.lines[checked_cast<size_t>(end)][0] != ' ') ++end;
            for (char mark : {'-', '+'}) {
                for (ptrdiff_t j = k; j < end; ++j) {
                    if (hunk.lines[checked_cast<size_t>(j)][0] == mark) add_line(j);
                }
            }
            if (end < n) add_line(end);
            k = end;
        }
    }

    return result;
}

// ── Main patch engine ──────────────────────────────────────────────────

// Line k (from 1) of text as given, with its line ending, or empty
static std::string_view raw_line(std::string_view text, ptrdiff_t k)
{
    if (k < 1) return {};
    for (; k > 1; --k) {
        ptrdiff_t nl = str_find(text, '\n');
        if (nl < 0) return {};
        text.remove_prefix(checked_cast<size_t>(nl + 1));
    }
    ptrdiff_t nl = str_find(text, '\n');
    return nl < 0 ? text : text.substr(0, checked_cast<size_t>(nl + 1));
}

// A file name as GNU patch shows it, quoted as gnulib's quotearg does in
// its default "shell" style, in the C locale: as is unless the shell would
// take it otherwise, and then in single quotes, each single quote written
// as '\''.  A name with a single quote and otherwise only printable ASCII
// that needs no escaping in C goes in double quotes instead.
static std::string quote_name(std::string_view name)
{
    bool quote = name.empty();
    bool apostrophe = false;
    bool plain = true;  // fit for double quotes
    for (ptrdiff_t k = 0; k < std::ssize(name); ++k) {
        unsigned char c = static_cast<unsigned char>(name[checked_cast<size_t>(k)]);
        switch (c) {
        case '{': case '}':  // special alone
            if (std::ssize(name) == 1) {
                quote = true;
            } else {
                plain = false;
            }
            break;
        case '#': case '~':  // special at the start
            if (k == 0) {
                quote = true;
            } else {
                plain = false;
            }
            break;
        case '\'':
            apostrophe = true;
            quote = true;
            break;
        case ' ':
            quote = true;
            break;
        case '\t': case '\n': case '\r': case '\\': case '?':
        case '!': case '"': case '$': case '&': case '(': case ')': case '*':
        case ';': case '<': case '=': case '>': case '[': case '^': case '`':
        case '|':
            quote = true;
            plain = false;
            break;
        default:
            // Any other byte goes as it is, though only printable ASCII
            // suits double quotes
            if (c < 0x20 || c > 0x7e) plain = false;
        }
    }

    if (!quote) return std::string(name);
    if (apostrophe && plain) return "\"" + std::string(name) + "\"";
    std::string result = "'";
    for (char c : name) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += '\'';
    return result;
}

// Read value as GNU patch reads a number for an option: an optional sign,
// then digits, and not negative. A value too large for an int is clamped.
// A bad value, the first one only, goes in opts.option_error, named by what.
static void set_number_option(PatchOptions &opts, int &number, std::string_view what,
                              std::string_view value)
{
    std::string_view digits = value;
    bool negative = digits.starts_with('-');
    if (negative || digits.starts_with('+')) digits.remove_prefix(1);

    std::string_view problem;
    if (digits.empty() ||
        !std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; })) {
        problem = "is not a number";
    } else if (negative && digits.find_first_not_of('0') != std::string_view::npos) {
        problem = "is negative";
    }
    if (!problem.empty()) {
        if (opts.option_error.empty()) {
            opts.option_error = std::string(what) + " " + quote_name(value) + " " +
                                std::string(problem);
        }
        return;
    }

    auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (ec == std::errc::result_out_of_range) {
        number = std::numeric_limits<int>::max();
    }
}

void set_fuzz_option(PatchOptions &opts, std::string_view value)
{
    set_number_option(opts, opts.fuzz, "fuzz factor", value);
}

void set_strip_option(PatchOptions &opts, std::string_view value)
{
    set_number_option(opts, opts.strip_level, "strip count", value);
}

PatchResult builtin_patch(std::string_view patch_text, const PatchOptions &opts)
{
    PatchResult result;
    result.exit_code = 0;

    // Like GNU patch, a bad option ends the patch before it touches, or
    // backs up, any file
    if (!opts.option_error.empty()) {
        result.exit_code = 2;
        result.err = "patch: **** " + opts.option_error + "\n";
        result.skipped = patch_target_files(patch_text, opts.strip_level, opts.reverse);
        return result;
    }

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

    std::string parse_error;
    auto lines = split_lines(patch_text);
    auto files = parse_patch(lines, opts.strip_level, opts.reverse, parse_error);

    // A hunk that does not parse is fatal, so apply none of the patch
    if (!parse_error.empty()) {
        result.exit_code = 2;
        result.err = "patch: **** " + parse_error + "\n";
        return result;
    }

    // Like GNU patch, empty input applies nothing, but input with no hunk
    // at all is fatal, even with -f or -s
    if (files.empty()) {
        if (!patch_text.empty()) {
            result.exit_code = 2;
            result.err = "patch: **** Only garbage was found in the patch input.\n";
        }
        return result;
    }

    bool had_rejects = false;
    std::vector<std::string> patched;  // targets of the sections not skipped

    for (const auto &pf : files) {
        if (pf.target_path.empty()) continue;

        bool file_existed = fs_exists(pf.target_path);
        std::string original = file_existed ? fs_read(pf.target_path) : std::string{};
        bool is_empty = original.empty();

        // Like GNU patch given -f, as quilt always does, warn about a
        // patch that creates a file with contents, or deletes or empties
        // one that is missing or empty, then apply it anyway
        bool looks_reversed = is_empty ? pf.new_absent > 0 : pf.old_absent == 2;
        if (looks_reversed && !opts.quiet) {
            result.out += std::format(
                "The next patch{} would {} the file {},\nwhich {}!  Applying it anyway.\n",
                opts.reverse ? ", when reversed," : "",
                !file_existed ? "delete" : is_empty ? "empty out" : "create",
                quote_name(pf.target_path),
                !file_existed ? "does not exist" : is_empty ? "is already empty"
                                                            : "already exists");
        }

        // Like GNU patch, note a CRLF ending on the line it goes by.  The
        // patch is read with its CRs stripped in any case.
        if (!opts.quiet && raw_line(patch_text, pf.crlf_line).ends_with("\r\n")) {
            result.out += "(Stripping trailing CRs from patch; use --binary to disable.)\n";
        }

        // A missing file is patched as empty when the patch creates it, or
        // when GNU patch would have warned above.  Otherwise, like GNU patch
        // given -f, as quilt always does, quote the text leading up to the
        // first hunk and skip the file without writing rejects.  As with -s,
        // quiet drops only the first two lines.
        if (!file_existed && !pf.old_absent && !looks_reversed) {
            if (!opts.quiet) {
                result.out += std::format(
                    "can't find file to patch at input line {}\n"
                    "Perhaps you used the wrong -p or --strip option?\n",
                    pf.hunk_line);
            }
            result.out += "The text leading up to this was:\n"
                          "--------------------------\n";
            // Quote the lines as given, carriage returns and all
            std::string_view text = patch_text;
            for (ptrdiff_t k = 1; k < pf.hunk_line; ++k) {
                // Every line before a hunk header ends with a newline
                ptrdiff_t len = str_find(text, '\n') + 1;
                if (k >= pf.text_line) {
                    result.out += '|';
                    result.out += text.substr(0, checked_cast<size_t>(len));
                }
                text.remove_prefix(checked_cast<size_t>(len));
            }
            ptrdiff_t nhunks = std::ssize(pf.hunks);
            result.out += std::format(
                "--------------------------\n"
                "No file to patch.  Skipping patch.\n"
                "{} out of {} {} ignored\n",
                nhunks, nhunks, nhunks == 1 ? "hunk" : "hunks");
            result.exit_code = 1;
            if (std::ranges::find(result.skipped, pf.target_path) == result.skipped.end()) {
                result.skipped.push_back(pf.target_path);
            }
            continue;
        }
        patched.push_back(pf.target_path);

        if (!opts.quiet) {
            result.out += "patching file " + quote_name(pf.target_path) + "\n";
        }
        FileContent fc = load_file_lines(original);

        std::vector<ptrdiff_t> hunk_positions(checked_cast<size_t>(std::ssize(pf.hunks)), -1);
        std::vector<bool> rejected(checked_cast<size_t>(std::ssize(pf.hunks)), false);
        ptrdiff_t cumulative_offset = 0;
        // Lines that the hunks applied so far added, which GNU patch adds to
        // the line numbers it reports, and offset_before[h] before hunk h
        ptrdiff_t out_offset = 0;
        std::vector<ptrdiff_t> offset_before(checked_cast<size_t>(std::ssize(pf.hunks)));
        ptrdiff_t last_frozen_line = 0;  // 0-based, exclusive: lines before this are frozen
        bool conflicts = false;
        std::string merged;

        if (opts.merge) {
            // Like GNU patch, merge every hunk, conflicts and all
            merged = merge_hunks(fc, pf, opts, result.out, conflicts,
                                 rejected, offset_before);
        } else {
            // Try to match each hunk
            for (ptrdiff_t h = 0; h < std::ssize(pf.hunks); ++h) {
                const auto &hunk = pf.hunks[checked_cast<size_t>(h)];
                auto pattern = get_old_pattern(hunk);
                offset_before[checked_cast<size_t>(h)] = out_offset;

                ptrdiff_t fuzz_used = 0;
                ptrdiff_t pos = locate_hunk(fc.lines, hunk, pattern,
                                             last_frozen_line, cumulative_offset,
                                             opts.fuzz, fuzz_used);
                auto ctx = get_hunk_context(hunk);
                bool changes = ctx.prefix < std::ssize(hunk.lines);

                // Like GNU patch outside merge mode, refuse a hunk at the top
                // of a file with contents when the patch surely creates it
                bool refused = pos == 0 && pf.old_absent == 2 && !is_empty;

                // Like GNU patch, fail a hunk found among the frozen lines that
                // would change one, saying so even with -s, though the hunks
                // after it still start from where it was found
                bool misordered = !refused && pos >= 0 && changes &&
                                  pos + ctx.prefix < last_frozen_line;
                if (misordered) {
                    result.out += "misordered hunks! output would be garbled\n";
                    cumulative_offset = pos - old_range_pos(hunk);
                }

                if (pos >= 0 && !refused && !misordered) {
                    hunk_positions[checked_cast<size_t>(h)] = pos;
                    ptrdiff_t pat_len = std::ssize(pattern);
                    ptrdiff_t actual_offset = pos - old_range_pos(hunk);

                    // Like GNU patch, report the hunk's line in the patched
                    // file and its whole offset from the line it names, even
                    // when the hunk before had the same offset
                    if ((actual_offset != 0 || fuzz_used > 0) && !opts.quiet) {
                        result.out += std::format("Hunk #{} succeeded at {}",
                                                  h + 1, pos + 1 + out_offset);
                        if (fuzz_used > 0) {
                            result.out += std::format(" with fuzz {}", fuzz_used);
                        }
                        if (actual_offset != 0) {
                            // Like GNU patch, only +1 is singular; -1 stays
                            // "lines"
                            result.out += std::format(" (offset {} line{})", actual_offset,
                                                      actual_offset == 1 ? "" : "s");
                        }
                        result.out += ".\n";
                    }

                    // Update offset and frozen line.  Like GNU patch outside
                    // merge mode, freeze only the lines through the hunk's last
                    // change, as the output takes context from the file.
                    cumulative_offset = actual_offset;
                    if (changes) last_frozen_line = pos + pat_len - ctx.suffix;
                    out_offset += hunk.new_count - hunk.old_count;
                } else {
                    // Hunk failed
                    rejected[checked_cast<size_t>(h)] = true;
                    if (!opts.quiet) {
                        // Like GNU patch, the line a hunk was found at, or else
                        // the line it names, the one after for an empty range
                        ptrdiff_t line = refused || misordered ? pos + 1
                                       : hunk.old_count == 0 ? hunk.old_start + 1
                                       : hunk.old_start;
                        result.out += std::format("Hunk #{} FAILED at {}.\n",
                                                  h + 1, line + out_offset);
                    }
                }
            }
        }

        bool file_has_rejects = std::ranges::find(rejected, true) != rejected.end();
        if (file_has_rejects || conflicts) {
            had_rejects = true;
            result.exit_code = 1;
        }

        // Apply changes
        if (!opts.dry_run) {
            // Like GNU patch, merge mode always writes the file
            bool any_applied = opts.merge;
            for (ptrdiff_t h = 0; h < std::ssize(pf.hunks); ++h) {
                if (hunk_positions[checked_cast<size_t>(h)] >= 0) { any_applied = true; break; }
            }

            bool creating = !file_existed && pf.old_absent;
            bool changed = any_applied || creating;
            std::string new_content;
            if (changed) {
                if (opts.merge) {
                    new_content = std::move(merged);
                } else {
                    new_content = build_output(fc.lines, fc.has_trailing_newline,
                                               pf, hunk_positions);
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
            }

            // GNU patch writes out the file even when no hunk changed it, so
            // it judges what is left of the file either way
            bool left_empty = changed ? new_content.empty() : is_empty;

            // Remove a file left empty when -E is given or the patch
            // surely deletes it, like GNU patch outside POSIX mode
            if ((opts.remove_empty || pf.new_absent == 2) && left_empty && !pf.old_absent) {
                if (file_existed && fs_delete(pf.target_path) && !opts.fs) {
                    remove_empty_parents(pf.target_path);
                }
            } else {
                // Like GNU patch, which in merge mode stays quiet about it
                // once any hunk has failed
                if (pf.new_absent == 2 && !left_empty &&
                    !(opts.merge && result.exit_code != 0)) {
                    result.exit_code = 1;
                    if (!opts.quiet) {
                        result.out += "Not deleting file " + quote_name(pf.target_path) +
                                      " as content differs from patch\n";
                    }
                }
                if (changed) fs_write(pf.target_path, new_content);
            }

            // Write the reject file, which merge mode needs only for a hunk
            // found among lines it merged already
            if (file_has_rejects) {
                fs_write(pf.target_path + ".rej", format_rejects(pf, rejected, offset_before));
                // Like GNU patch, even with -s
                ptrdiff_t rej_count = 0;
                for (bool r : rejected) if (r) ++rej_count;
                result.out += std::format(
                    "{} out of {} {} FAILED -- saving rejects to file {}\n",
                    rej_count, std::ssize(pf.hunks),
                    std::ssize(pf.hunks) == 1 ? "hunk" : "hunks",
                    quote_name(pf.target_path + ".rej"));
            }
        }
    }

    if (had_rejects) {
        result.exit_code = 1;
    }

    // Another section may have patched a skipped file, say by creating it
    std::erase_if(result.skipped, [&](const std::string &file) {
        return std::ranges::find(patched, file) != patched.end();
    });

    return result;
}

// === src/cmd_stack.cpp ===

// This is free and unencumbered software released into the public domain.
#include <cstdlib>


static void write_applied_patches(QuiltState &q) {
    std::string path = path_join(q.work_dir, q.pc_dir, "applied-patches");
    write_applied(path, q.applied);
    // Like the original quilt, remove the file once the stack is empty.
    // Truncating first means a failed removal leaves no stale entries.
    if (q.applied.empty()) delete_file(path);
}

// Apply the patch options from QUILT_PATCH_OPTS that the builtin engine
// understands.
static void apply_quilt_patch_opts(PatchOptions &opts, std::span<const std::string> extra)
{
    for (const auto &opt : extra) {
        std::string_view o = opt;
        if (o == "-R") opts.reverse = true;
        else if (o == "-s") opts.quiet = true;
        else if (o == "-E") opts.remove_empty = true;
        else if (o.starts_with("--fuzz=")) set_fuzz_option(opts, o.substr(7));
    }
}

// Rewrite the " -- saving rejects to file X" ending of patch's messages for
// a reject file that push removes, as upstream push's cleanup_patch_output
// does: name the file with the rejects, from the last "patching file" line,
// or with -q, which hides those lines, drop the ending.
static std::string cleanup_patch_output(std::string_view text, bool quiet)
{
    std::string result;
    std::string_view file;
    for (;;) {
        ptrdiff_t nl = str_find(text, '\n');
        std::string_view line = nl < 0 ? text : text.substr(0, checked_cast<size_t>(nl));
        if (line.starts_with("patching file ")) {
            file = line.substr(14);
        }
        ptrdiff_t at = str_find(line, " -- saving rejects to ");
        if (at < 0) {
            result += line;
        } else {
            result += line.substr(0, checked_cast<size_t>(at));
            if (!quiet) {
                result += " -- rejects in file ";
                result += file;
            }
        }
        if (nl < 0) return result;
        result += '\n';
        text.remove_prefix(checked_cast<size_t>(nl + 1));
    }
}

// Check that the patch file accounts for every change to the patch's files,
// like upstream's check_for_pending_changes: apply the patch to the backups
// in memory and compare each result with the working tree.
static bool removes_cleanly(const QuiltState &q, std::string_view name,
                            std::span<const std::string> extra_patch_opts)
{
    std::string pc_dir = pc_patch_dir(q, name);
    auto files = files_in_patch(q, name);

    std::map<std::string, std::string> memfs;
    for (const auto &file : files) {
        // An empty backup means the file did not exist before the patch
        std::string backup = read_file(path_join(pc_dir, file));
        if (!backup.empty()) memfs[file] = std::move(backup);
    }

    std::string patch_content = read_file(path_join(q.work_dir, q.patches_dir, name));
    if (!patch_content.empty()) {
        PatchOptions opts;
        opts.strip_level = q.get_strip_level(name);
        if (q.patch_reversed.contains(std::string(name))) opts.reverse = true;
        apply_quilt_patch_opts(opts, extra_patch_opts);
        // The engine keeps whatever applies, so a force-applied patch
        // matches the partial result that push left behind
        opts.quiet = true;
        opts.fs = &memfs;
        builtin_patch(patch_content, opts);
    }

    for (const auto &file : files) {
        // A missing file compares as empty, like diff against /dev/null
        auto it = memfs.find(file);
        std::string_view expected = it != memfs.end() ? std::string_view(it->second) : "";
        if (read_file(path_join(q.work_dir, file)) != expected) return false;
    }
    return true;
}

// Like upstream push, check whether a patch that does not apply is applied
// already by applying it in reverse, here to copies of its files.  Set files
// to those the reverse patch would have backed up.
static bool reverse_applies(const QuiltState &q, std::string_view name,
                            std::string_view patch_content, PatchOptions opts,
                            std::span<const std::string> extra_patch_opts,
                            std::vector<std::string> &files)
{
    // Flip the series' direction, though as with patch given -R twice, a
    // -R in QUILT_PATCH_OPTS keeps the patch reversed
    opts.reverse = !q.patch_reversed.contains(std::string(name));
    apply_quilt_patch_opts(opts, extra_patch_opts);
    opts.quiet = true;

    files = patch_target_files(patch_content, opts.strip_level, opts.reverse);
    std::map<std::string, std::string> memfs;
    for (const auto &file : files) {
        std::string path = path_join(q.work_dir, file);
        if (file_exists(path)) memfs[file] = read_file(path);
    }
    opts.fs = &memfs;
    PatchResult result = builtin_patch(patch_content, opts);

    std::erase_if(files, [&](const std::string &file) {
        return std::ranges::find(result.skipped, file) != result.skipped.end();
    });
    return result.exit_code == 0;
}

// List the files that rolling back a patch restored from their backups, as
// upstream push -v does through backup-files: first those it removed, whose
// empty backups mean they were missing or empty, then the rest.
static void show_rollback(const QuiltState &q, std::span<const std::string> files)
{
    std::vector<std::string_view> restored;
    for (const auto &file : files) {
        if (read_file(path_join(q.work_dir, file)).empty()) {
            out_line("Removing " + file);
        } else {
            restored.push_back(file);
        }
    }
    for (auto file : restored) {
        out("Restoring ");
        out_line(file);
    }
}

int cmd_series(QuiltState &q, int argc, char **argv) {
    enum { COLOR = 256 };
    static constexpr LongOpt longopts[] = {
        {"color", OptArg::optional, COLOR},
    };
    auto args = parse_options(argc, argv, "vh", longopts);
    if (!args) return 1;
    bool verbose = false;
    for (const auto &opt : args->options) {
        switch (opt.key) {
        case 'v': verbose = true; break;
        case COLOR:
            if (!valid_color_value(opt.value)) return usage_error(argv[0]);
            break;
        case 'h': return command_help(argv[0]);
        }
    }
    if (!args->operands.empty()) return usage_error(argv[0]);

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
        out_line(format_patch(q, patch));
    }
    return 0;
}

int cmd_applied(QuiltState &q, int argc, char **argv) {
    // Upstream declares -n but never handles it, and loops forever on it,
    // so -n does nothing here
    auto args = parse_options(argc, argv, "nh");
    if (!args) return 1;
    for (const auto &opt : args->options) {
        if (opt.key == 'h') return command_help(argv[0]);
    }
    if (std::ssize(args->operands) > 1) return usage_error(argv[0]);
    std::string_view target;
    if (!args->operands.empty()) target = args->operands[0];

    if (!target.empty()) {
        // Print all applied patches up to and including target
        auto found = find_applied_patch(q, target);
        if (!found) return 1;
        for (const auto &a : q.applied) {
            out_line(format_patch(q, a));
            if (a == *found) break;
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
    auto args = parse_options(argc, argv, "h");
    if (!args) return 1;
    if (!args->options.empty()) return command_help(argv[0]);
    if (std::ssize(args->operands) > 1) return usage_error(argv[0]);
    std::optional<std::string_view> target;
    if (!args->operands.empty()) target = args->operands[0];

    if (q.series.empty()) {
        if (q.series_file_exists) {
            err_line("No patches in series");
        } else {
            err_line("No series file found");
        }
        return 1;
    }

    ptrdiff_t start_idx;
    if (target) {
        // Like upstream, a given name is looked up even when empty, which
        // means the top patch
        auto found = find_patch_in_series(q, *target);
        if (!found) return 1;
        start_idx = q.find_in_series(*found).value() + 1;
    } else {
        ptrdiff_t top = q.top_index();
        start_idx = top + 1;
    }

    if (start_idx >= std::ssize(q.series)) {
        // With an explicit target patch, having no patches after it is not
        // an error — just print nothing.
        if (target) {
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
    auto args = parse_options(argc, argv, "h");
    if (!args) return 1;
    if (!args->options.empty()) return command_help(argv[0]);
    if (!args->operands.empty()) return usage_error(argv[0]);
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
    auto args = parse_options(argc, argv, "h");
    if (!args) return 1;
    if (!args->options.empty()) return command_help(argv[0]);
    if (std::ssize(args->operands) > 1) return usage_error(argv[0]);
    std::string_view target;
    if (!args->operands.empty()) target = args->operands[0];

    if (!target.empty()) {
        auto found = find_patch(q, target);
        if (!found) return 1;
        // Original quilt: if the named patch is applied, error
        if (q.is_applied(*found)) {
            err("Patch "); err(format_patch(q, *found)); err_line(" is currently applied");
            return 2;
        }
        // If unapplied, return the patch itself (it's the "next" to be pushed)
        out_line(format_patch(q, *found));
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

    ptrdiff_t after_idx = q.top_index() + 1;

    if (after_idx >= std::ssize(q.series)) {
        std::string_view top_name = q.applied.empty() ? std::string_view("??") : std::string_view(q.applied.back());
        err("File series fully applied, ends at patch "); err_line(format_patch(q, top_name));
        return 2;
    }

    out_line(format_patch(q, q.series[checked_cast<size_t>(after_idx)]));
    return 0;
}

int cmd_previous(QuiltState &q, int argc, char **argv) {
    auto args = parse_options(argc, argv, "h");
    if (!args) return 1;
    if (!args->options.empty()) return command_help(argv[0]);
    if (std::ssize(args->operands) > 1) return usage_error(argv[0]);
    std::string_view target;
    if (!args->operands.empty()) target = args->operands[0];

    if (!target.empty()) {
        auto found = find_patch(q, target);
        if (!found) return 1;
        auto idx = q.find_in_series(*found);
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
    bool verbose = false;  // lists the files of each rollback, like upstream
    std::string_view fuzz;  // like upstream, an empty value means none
    bool merge = false;
    std::string merge_style;
    bool leave_rejects = false;
    bool do_refresh = false;
    int push_count = -1;
    std::string_view target;

    enum { FUZZ = 256, LEAVE_REJECTS, COLOR, REFRESH };
    static constexpr LongOpt longopts[] = {
        {"fuzz", OptArg::required, FUZZ},
        {"merge", OptArg::optional, 'm'},
        {"leave-rejects", OptArg::none, LEAVE_REJECTS},
        {"color", OptArg::optional, COLOR},
        {"refresh", OptArg::none, REFRESH},
        {"quiet", OptArg::none, 'q', true},
        {"verbose", OptArg::none, 'v', true},
    };
    auto args = parse_options(argc, argv, "fqvam::h", longopts);
    if (!args) return 1;
    for (const auto &opt : args->options) {
        switch (opt.key) {
        case 'f': force = true; break;
        case 'q': quiet = true; break;
        case 'v': verbose = true; break;
        case 'a': push_all = true; break;
        case 'h': return command_help(argv[0]);
        case FUZZ: fuzz = opt.value; break;
        case 'm':
            if (!opt.value.empty() && opt.value != "merge" && opt.value != "diff3") {
                return usage_error(argv[0]);
            }
            merge = true;
            merge_style = opt.value == "diff3" ? "diff3" : "";
            break;
        case LEAVE_REJECTS: leave_rejects = true; break;
        case COLOR:
            if (!valid_color_value(opt.value)) return usage_error(argv[0]);
            break;
        case REFRESH: do_refresh = true; break;
        }
    }
    auto &operands = args->operands;
    if (std::ssize(operands) > 1 || (push_all && !operands.empty())) {
        return usage_error(argv[0]);
    }
    if (!operands.empty()) {
        // Try as number first
        std::string_view arg = operands[0];
        int val = 0;
        auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), val);
        if (ec == std::errc{} && ptr == arg.data() + arg.size() && val > 0) {
            push_count = val;
        } else {
            target = arg;
        }
    }

    ptrdiff_t top = q.top_index();
    ptrdiff_t start_idx = top + 1;

    // Like upstream's find_unapplied_patch, find the named target, or else
    // the next patch, before checking anything else
    ptrdiff_t target_idx = -1;
    if (!push_all && !target.empty()) {
        auto found = find_patch(q, target);
        if (!found) return 1;
        target_idx = q.find_in_series(*found).value();
        if (target_idx < start_idx) {
            err("Patch "); err(format_patch(q, *found)); err_line(" is currently applied");
            return 2;
        }
    } else if (!q.series_file_exists) {
        err_line("No series file found");
        return 1;
    } else if (q.series.empty()) {
        err_line("No patches in series");
        return 2;
    } else if (start_idx >= std::ssize(q.series)) {
        err_line("File series fully applied, ends at patch " +
                 patch_path_display(q, q.applied.back()));
        return 2;
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

    ptrdiff_t end_idx;  // inclusive
    if (push_all) {
        end_idx = std::ssize(q.series) - 1;
    } else if (target_idx >= 0) {
        end_idx = target_idx;
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

        if (i > start_idx && !quiet) {
            out_line("");
        }
        out_line("Applying patch " + display);

        // Read patch file. A missing patch applies as an empty one.
        std::string patch_path = path_join(q.work_dir, q.patches_dir, name);
        bool patch_exists = file_exists(patch_path);
        std::string patch_content = patch_exists ? read_file(patch_path) : "";

        // Apply the patch using built-in patch engine
        PatchOptions patch_opts;
        patch_opts.strip_level = q.get_strip_level(name);
        if (q.patch_reversed.contains(name)) patch_opts.reverse = true;
        if (!fuzz.empty()) set_fuzz_option(patch_opts, fuzz);
        if (merge) {
            patch_opts.merge = true;
            patch_opts.merge_style = merge_style;
        }
        patch_opts.quiet = quiet;
        apply_quilt_patch_opts(patch_opts, extra_patch_opts);

        // Back up every file the patch will modify, including deletions
        auto affected = patch_target_files(patch_content, patch_opts.strip_level,
                                           patch_opts.reverse);
        std::string pc_dir = pc_patch_dir(q, name);
        if (!is_directory(pc_dir)) {
            make_dirs(pc_dir);
        }

        for (const auto &file : affected) {
            backup_file(q, name, file);
        }

        // Like upstream, run patch only for a patch file with something in
        // it, so that a bad option fails no empty patch
        PatchResult result = patch_content.empty()
            ? PatchResult{} : builtin_patch(patch_content, patch_opts);

        // GNU patch backs up only the files it patches, so forget the
        // missing files it skipped
        for (const auto &file : result.skipped) {
            delete_file(path_join(pc_dir, file));
            std::erase(affected, file);
        }

        // Like upstream, which runs patch with 2>&1, show all of patch's
        // output on stdout
        if (!force && !leave_rejects) {
            result.out = cleanup_patch_output(result.out, quiet);
        }
        out(result.out);
        out(result.err);

        bool failed = result.exit_code != 0;
        if (failed) {
            if (!force) {
                // Not forced: restore files from backups and clean up
                for (const auto &file : affected) {
                    restore_file(q, name, file);
                }
                if (verbose) show_rollback(q, affected);

                std::vector<std::string> reversed_files;
                if (reverse_applies(q, name, patch_content, patch_opts,
                                    extra_patch_opts, reversed_files)) {
                    out_line("Patch " + display + " can be reverse-applied");
                } else {
                    out_line("Patch " + display + " does not apply (enforce with -f)");
                }
                // Upstream rolls back its trial too
                if (verbose) show_rollback(q, reversed_files);

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

        // Record as applied; a forced patch is marked as needing refresh
        q.applied.push_back(name);
        write_applied_patches(q);
        write_file(path_join(pc_dir, ".timestamp"), "");
        if (failed) {
            write_file(path_join(pc_dir, ".needs_refresh"), "");
        }

        // Like upstream, these print even with -q
        if (!patch_exists) {
            out_line("Patch " + display + " does not exist; applied empty patch");
        } else if (affected.empty()) {
            out_line("Patch " + display + " appears to be empty; applied");
        } else if (failed) {
            out_line("Applied patch " + display + " (forced; needs refresh)");
        }
        if (failed) return 1;

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
    std::optional<std::string_view> target;

    enum { REFRESH = 256 };
    static constexpr LongOpt longopts[] = {
        {"refresh", OptArg::none, REFRESH},
        {"quiet", OptArg::none, 'q', true},
        {"verbose", OptArg::none, 'v', true},
    };
    auto args = parse_options(argc, argv, "fRqvah", longopts);
    if (!args) return 1;
    for (const auto &opt : args->options) {
        switch (opt.key) {
        case 'f': force = true; break;
        // -R (verify removal) is always done unless forced, so it only
        // cancels an earlier -f, as in the original quilt.
        case 'R': force = false; break;
        case 'q': quiet = true; break;
        case 'v': verbose = true; break;
        case 'a': pop_all = true; break;
        case 'h': return command_help(argv[0]);
        case REFRESH: auto_refresh = true; break;
        }
    }
    auto &operands = args->operands;
    if (std::ssize(operands) > 1 || (pop_all && !operands.empty())) {
        return usage_error(argv[0]);
    }
    if (!operands.empty()) {
        std::string_view arg = operands[0];
        if (!arg.empty() &&
            std::ranges::all_of(arg, [](char c) { return c >= '0' && c <= '9'; })) {
            // Any run of digits is a count, as in the original quilt
            auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), pop_count);
            if (ec == std::errc::result_out_of_range) pop_all = true;
        } else {
            target = arg;
        }
    }

    if (q.applied.empty() && !q.series_file_exists) {
        err_line("No series file found");
        return 1;
    }

    if (force && auto_refresh) {
        err_line("Options -f and --refresh are mutually exclusive");
        return 1;
    }

    ptrdiff_t stop_idx;  // index in applied to stop BEFORE (exclusive); pop down to this
    if (pop_all) {
        stop_idx = 0;
    } else if (target) {
        // Like upstream, an empty name means the top patch, so nothing
        // is popped. Upstream does not check that the series still
        // matches the applied patches for pop, so the top patch need not
        // be in the series.
        std::optional<std::string> found;
        if (target->empty() && q.series_file_exists && !q.applied.empty()) {
            found = q.applied.back();
        } else {
            found = find_applied_patch(q, *target);
        }
        if (!found) return 1;
        ptrdiff_t found_idx = std::ranges::find(q.applied, *found) - q.applied.begin();
        // Pop down to (but not including) the target patch
        stop_idx = found_idx + 1;
    } else if (pop_count >= 0) {
        stop_idx = std::ssize(q.applied) - pop_count;
        if (stop_idx < 0) stop_idx = 0;
    } else {
        // Pop just the top patch
        stop_idx = std::ssize(q.applied) - 1;
    }

    // Refuse to pop a force-applied top patch unless forced, even with
    // --refresh. Like the original quilt, this comes after the target
    // patch has been resolved.
    if (!force && !q.applied.empty()) {
        std::string top_nr = path_join(pc_patch_dir(q, q.applied.back()),
                                       ".needs_refresh");
        if (file_exists(top_nr)) {
            err_line("Patch " + patch_path_display(q, q.applied.back()) +
                     " needs to be refreshed first.");
            return 1;
        }
    }

    if (q.applied.empty() || stop_idx >= std::ssize(q.applied)) {
        err_line("No patch removed");
        return 2;
    }

    auto extra_patch_opts = shell_split(get_env("QUILT_PATCH_OPTS"));

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

        std::string pc_dir = pc_patch_dir(q, name);

        // Refuse to discard changes that are not in the patch file
        if (!force && !removes_cleanly(q, name, extra_patch_opts)) {
            err_line("Patch " + display +
                     " does not remove cleanly (refresh it or enforce with -f)");
            err_line("Hint: `quilt diff -z' will show the pending changes.");
            return 1;
        }

        // Restore backed-up files
        auto files = files_in_patch(q, name);

        if (!first_pop && !quiet) {
            out_line("");
        }
        if (files.empty()) {
            out_line("Patch " + display + " appears to be empty, removing");
        } else {
            out_line("Removing patch " + display);
        }
        first_pop = false;

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

} // namespace

int cmd_annotate(QuiltState &q, int argc, char **argv)
{
    auto args = parse_options(argc, argv, "P:h");
    if (!args) return 1;
    AnnotateOptions opts;
    for (const auto &opt : args->options) {
        if (opt.key == 'h') return command_help(argv[0]);
        opts.patch = opt.value;
    }
    if (std::ssize(args->operands) != 1) return usage_error(argv[0]);
    opts.file = subdir_path(q, args->operands[0]);
    if (opts.file.empty()) return usage_error(argv[0]);

    // No -P, or an empty one, means the top patch
    auto found = find_applied_patch(q, opts.patch);
    if (!found) return 1;
    std::string stop_patch = *found;

    std::vector<std::string> patches;
    std::vector<std::string> files;
    std::string next_patch;

    for (const auto &patch : q.applied) {
        std::string old_file = path_join(pc_patch_dir(q, patch), opts.file);
        if (file_exists(old_file)) {
            patches.push_back(patch);
            files.push_back(old_file);
        }
        if (patch == stop_patch) {
            next_patch = next_patch_for_file(q, stop_patch, opts.file);
            break;
        }
    }

    if (next_patch.empty()) {
        files.push_back(path_join(q.work_dir, opts.file));
    } else {
        files.push_back(path_join(pc_patch_dir(q, next_patch), opts.file));
    }

    if (patches.empty()) {
        std::string target = files.back();
        if (!file_exists(target)) {
            err_line("File " + opts.file + " does not exist");
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

int cmd_init(QuiltState &q, int argc, char **argv) {
    auto args = parse_options(argc, argv, "h");
    if (!args) return 1;
    if (!args->options.empty()) return command_help(argv[0]);
    if (!args->operands.empty()) return usage_error(argv[0]);

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
        if (!write_file(series_abs, "")) {
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
    auto args = parse_options(argc, argv, "p:h");
    if (!args) return 1;
    std::string p_value;
    for (const auto &opt : args->options) {
        if (opt.key == 'h') return command_help(argv[0]);
        p_value = opt.value;
    }

    // Like upstream, check the strip level before the arguments
    if (!p_value.empty() && p_value != "0" && p_value != "1") {
        err_line("Cannot create patches with -p" + p_value +
                 ", please specify -p0 or -p1 instead");
        return 1;
    }

    if (std::ssize(args->operands) != 1 || args->operands[0].empty()) {
        return usage_error(argv[0]);
    }
    std::string patch_name(strip_patches_prefix(q, args->operands[0]));

    if (q.find_in_series(patch_name).has_value()) {
        err_line("Patch " + patch_path_display(q, patch_name) + " exists already");
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

    if (!q.applied.empty() && q.top_index() < 0) {
        err_line("The series file no longer matches the applied patches. Please run 'quilt pop -a'.");
        return 1;
    }

    // Insert into the series after the current top, recording only -p0
    if (!insert_in_series(q, patch_name, p_value == "0" ? "-p0" : "",
                          q.patch_after_top())) {
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

// Parse the options of add, remove, and revert, which take "P:h" and at
// least one file. Return nullopt, with the exit status in status, for -h
// or wrong arguments.
struct PatchFileArgs {
    std::string_view patch;  // -P, empty for the top patch
    std::vector<std::string> files;
};

static std::optional<PatchFileArgs> parse_patch_file_args(const QuiltState &q, int argc,
                                                          char **argv, int &status)
{
    auto args = parse_options(argc, argv, "P:h");
    status = 1;
    if (!args) return std::nullopt;
    PatchFileArgs result;
    for (const auto &opt : args->options) {
        if (opt.key == 'h') {
            status = command_help(argv[0]);
            return std::nullopt;
        }
        result.patch = opt.value;
    }
    if (args->operands.empty()) {
        status = usage_error(argv[0]);
        return std::nullopt;
    }
    for (auto file : args->operands) result.files.push_back(subdir_path(q, file));
    return result;
}

int cmd_add(QuiltState &q, int argc, char **argv) {
    int rc;
    auto args = parse_patch_file_args(q, argc, argv, rc);
    if (!args) return rc;
    std::string_view patch_arg = args->patch;
    const auto &files = args->files;

    // No -P, or an empty one, means the top patch
    auto found = find_applied_patch(q, patch_arg);
    if (!found) return 1;
    std::string_view patch = *found;

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
    int rc;
    auto args = parse_patch_file_args(q, argc, argv, rc);
    if (!args) return rc;
    std::string_view patch_arg = args->patch;
    const auto &files = args->files;

    // No -P, or an empty one, means the top patch
    auto found = find_applied_patch(q, patch_arg);
    if (!found) return 1;
    std::string_view patch = *found;

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
    auto args = parse_options(argc, argv, "h");
    if (!args) return 1;
    if (!args->options.empty()) return command_help(argv[0]);
    if (args->operands.empty()) return usage_error(argv[0]);

    if (q.applied.empty()) {
        err_line("No patches applied");
        return 1;
    }

    std::vector<std::string> files;
    for (auto file : args->operands) files.push_back(subdir_path(q, file));

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

static constexpr std::string_view SNAPSHOT_PATCH = ".snap";

static bool is_placeholder_copy(std::string_view path)
{
    return file_exists(path) && read_file(path).empty();
}

// Detect binary content (null bytes in the first 8 KB)
static bool is_binary_data(std::string_view data)
{
    return str_find(data.substr(0, 8192), '\0') >= 0;
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
// Format a file modification time as "YYYY-MM-DD HH:MM:SS.NNNNNNNNN +HHMM".
static std::string format_file_timestamp(std::string_view path) {
    int32_t nsec = 0;
    int64_t mt = file_mtime(path, &nsec);
    if (mt <= 0) return "";
    DateTime dt = local_time(mt);
    int off_h = dt.utc_offset / 3600;
    int off_m = (std::abs(dt.utc_offset) % 3600) / 60;
    return std::format("\t{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:09d} {:+03d}{:02d}",
                       dt.year, dt.month, dt.day,
                       dt.hour, dt.min, dt.sec, nsec, off_h, off_m);
}

static std::string generate_path_diff(const QuiltState &q,
                                      std::string_view file,
                                      std::string_view old_path,
                                      bool old_placeholder,
                                      std::string_view new_path,
                                      bool new_placeholder,
                                      std::string_view p_format = "1",
                                      bool reverse = false,
                                      int context_lines = 3,
                                      DiffFormat diff_format = DiffFormat::unified,
                                      bool no_timestamps = false,
                                      DiffAlgorithm diff_algorithm = DiffAlgorithm::myers) {
    bool old_missing = old_path.empty() || !file_exists(old_path) ||
        (old_placeholder && is_placeholder_copy(old_path));
    bool new_missing = new_path.empty() || !file_exists(new_path) ||
        (new_placeholder && is_placeholder_copy(new_path));

    // Identical files never differ, binary or not. Only a changed binary
    // file is reported, which callers treat as a failed diff.
    std::string old_data = old_missing ? std::string() : read_file(old_path);
    std::string new_data = new_missing ? std::string() : read_file(new_path);
    if (old_data == new_data) {
        return {};
    }
    if (is_binary_data(old_data) || is_binary_data(new_data)) {
        return "Binary files differ\n";
    }

    // As in the original quilt, labels follow the files after the swap
    if (reverse) {
        std::swap(old_path, new_path);
        std::swap(old_missing, new_missing);
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
        // A -p0 deletion names the file itself, so it can be applied
        if (p_format == "0") old_label = new_label;
        new_label = "/dev/null";
    }

    // Append file timestamps unless suppressed
    if (!no_timestamps) {
        if (!old_missing)
            old_label += format_file_timestamp(old_path);
        if (!new_missing)
            new_label += format_file_timestamp(new_path);
    }

    // QUILT_DIFF_OPTS may override context lines
    int ctx = context_lines;
    auto extra_diff_opts = shell_split(get_env("QUILT_DIFF_OPTS"));
    int opts_ctx = parse_diff_opts_context(extra_diff_opts);
    if (opts_ctx >= 0) ctx = opts_ctx;

    DiffResult result = builtin_diff(old_arg, new_arg, ctx,
                                      old_label, new_label, diff_format,
                                      diff_algorithm);
    return result.output;
}

static std::string generate_file_diff(const QuiltState &q, std::string_view patch,
                                      std::string_view file,
                                      std::string_view p_format = "1",
                                      bool reverse = false,
                                      int context_lines = 3,
                                      DiffFormat diff_format = DiffFormat::unified,
                                      bool no_timestamps = false,
                                      DiffAlgorithm diff_algorithm = DiffAlgorithm::myers) {
    std::string backup_path = path_join(pc_patch_dir(q, patch), file);
    std::string working_path = path_join(q.work_dir, file);
    return generate_path_diff(q, file, backup_path, true, working_path, false,
                              p_format, reverse,
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
        append_unique_files(files, seen, files_in_patch_ordered(q, patch));
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
    auto args = parse_options(argc, argv, "dh");
    if (!args) return 1;
    bool remove_snapshot = false;
    for (const auto &opt : args->options) {
        if (opt.key == 'h') return command_help(argv[0]);
        remove_snapshot = true;
    }
    if (!args->operands.empty()) return usage_error(argv[0]);

    if (!q.series_file_exists) {
        err_line("No series file found");
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

// A context diff hunk's "*** N[,M] ****" or "--- N[,M] ----" line, for
// a mark of '*' or '-'
static bool is_context_range(std::string_view line, char mark)
{
    std::string head = std::string(3, mark) + ' ';
    std::string tail = ' ' + std::string(4, mark);
    if (std::ssize(line) <= std::ssize(head) + std::ssize(tail) ||
        !line.starts_with(head) || !line.ends_with(tail)) {
        return false;
    }
    line.remove_prefix(head.size());
    line.remove_suffix(tail.size());
    return std::ranges::all_of(line, [](char c) {
        return (c >= '0' && c <= '9') || c == ',';
    });
}

// The name diffstat(1) lists a file under, given the labels of its old
// and new versions in the diff's file header: the new label, or the old
// one for a deleted file, without its timestamp and first path component
static std::string diffstat_name(std::string_view old_label,
                                 std::string_view new_label)
{
    auto strip = [](std::string_view label) {
        ptrdiff_t tab = str_find(label, '\t');
        if (tab >= 0) label = label.substr(0, checked_cast<size_t>(tab));
        ptrdiff_t slash = str_find(label, '/');
        if (slash >= 0) label.remove_prefix(checked_cast<size_t>(slash + 1));
        return std::string(label);
    };
    std::string name = strip(new_label);
    if (name == "dev/null" || new_label.starts_with("/dev/null")) {
        name = strip(old_label);
    }
    return name;
}

// Built-in diffstat: parse a unified or context diff, produce a summary
// matching the output of the external diffstat(1) utility with its
// default options and 80 columns.
static std::string generate_diffstat(std::string_view diff)
{
    struct FileStat {
        std::string name;
        ptrdiff_t added    = 0;
        ptrdiff_t removed  = 0;
        ptrdiff_t modified = 0;  // "! " lines of a context diff, both sides
    };

    // diffstat(1) lists files in byte order by name, the order refresh
    // already lists them in
    std::vector<FileStat> stats;
    auto lines = split_lines(diff);

    for (ptrdiff_t i = 0; i < std::ssize(lines); ++i) {
        const std::string &line = lines[checked_cast<size_t>(i)];
        std::string_view next;
        if (i + 1 < std::ssize(lines)) next = lines[checked_cast<size_t>(i + 1)];

        // A file header: "--- old" then "+++ new" in a unified diff, or
        // "*** old" then "--- new" in a context diff, unlike a context
        // hunk's "*** N,M ****" and "--- N,M ----" lines
        bool unified = line.starts_with("--- ") && next.starts_with("+++ ");
        bool context = line.starts_with("*** ") && next.starts_with("--- ") &&
                       !is_context_range(line, '*') && !is_context_range(next, '-');
        if (unified || context) {
            stats.push_back({diffstat_name(std::string_view(line).substr(4),
                                           next.substr(4))});
            i += 1;  // skip the new version's label
            continue;
        }

        if (stats.empty()) continue;

        if (line.starts_with("+") && !line.starts_with("+++"))
            stats.back().added++;
        else if (line.starts_with("-") && !line.starts_with("---"))
            stats.back().removed++;
        else if (line.starts_with("!"))
            stats.back().modified++;
    }

    // Like diffstat(1), an empty diff gives just the summary
    if (stats.empty()) return " 0 files changed\n";

    // Each line is " name |", the name padded one past the longest, then
    // the file's total in at least five columns and a histogram that is
    // scaled down when the largest total does not fit in the rest
    ptrdiff_t name_width = 0;
    ptrdiff_t plot_scale = 0;
    for (const auto &s : stats) {
        name_width = std::max(name_width, std::ssize(s.name) + 1);
        plot_scale = std::max(plot_scale, s.added + s.removed + s.modified);
    }
    ptrdiff_t plot_width = std::max(80 - name_width - 8, ptrdiff_t{10});
    plot_scale = std::max(plot_scale, plot_width);

    std::string result;
    ptrdiff_t total_added = 0, total_removed = 0, total_modified = 0;
    ptrdiff_t total_files = std::ssize(stats);

    for (const auto &s : stats) {
        total_added += s.added;
        total_removed += s.removed;
        total_modified += s.modified;

        result += ' ';
        result += s.name;
        result.append(checked_cast<size_t>(name_width - std::ssize(s.name)), ' ');
        result += '|';
        std::string total = std::to_string(s.added + s.removed + s.modified);
        result.append(checked_cast<size_t>(std::max(5 - std::ssize(total), ptrdiff_t{0})), ' ');
        result += total;
        result += ' ';

        // diffstat(1)'s plot_num, including how it carries the remainder
        // from one mark to the next
        ptrdiff_t extra = 0;
        for (auto [count, mark] : {std::pair{s.added, '+'}, std::pair{s.removed, '-'},
                                   std::pair{s.modified, '!'}}) {
            if (count == 0) continue;
            ptrdiff_t product = plot_width * count;
            ptrdiff_t bars = (product + extra) / plot_scale;
            extra = product - bars * plot_scale - extra;
            if (bars > 0) result.append(checked_cast<size_t>(bars), mark);
        }
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
    if (total_modified > 0) {
        result += ", ";
        result += std::to_string(total_modified);
        result += (total_modified == 1) ? " modification(!)" : " modifications(!)";
    }
    result += '\n';

    return result;
}

// Split text into lines, each keeping its '\n' terminator (the last line
// may have none).
static std::vector<std::string_view> split_lines_keep_eol(std::string_view s)
{
    std::vector<std::string_view> lines;
    while (!s.empty()) {
        ptrdiff_t nl = str_find(s, '\n');
        ptrdiff_t len = nl < 0 ? std::ssize(s) : nl + 1;
        lines.push_back(s.substr(0, checked_cast<size_t>(len)));
        s.remove_prefix(checked_cast<size_t>(len));
    }
    return lines;
}

// Length of the run of spaces and tabs ending a line, just before its '\n'
// (if any), not counting into the first `keep` bytes. As in upstream, '\r'
// is not whitespace here, so a CRLF line is left alone.
static ptrdiff_t trailing_ws_len(std::string_view line, ptrdiff_t keep)
{
    if (line.ends_with('\n')) line.remove_suffix(1);
    keep = std::min(keep, std::ssize(line));
    std::string_view tail = line.substr(checked_cast<size_t>(keep));
    auto last = tail.find_last_not_of(" \t");
    if (last == std::string_view::npos) return std::ssize(tail);
    return std::ssize(tail) - checked_cast<ptrdiff_t>(last) - 1;
}

// Append a line less the `n` bytes just before its '\n' (if any).
static void append_without_trailing_ws(std::string &out,
                                       std::string_view line, ptrdiff_t n)
{
    bool eol = line.ends_with('\n');
    if (eol) line.remove_suffix(1);
    out += line.substr(0, checked_cast<size_t>(std::ssize(line) - n));
    if (eol) out += '\n';
}

// Parse a decimal number at s[pos], advancing pos past it.
static ptrdiff_t parse_diff_num(std::string_view s, ptrdiff_t &pos)
{
    ptrdiff_t n = 0;
    auto first = s.data() + pos;
    auto [ptr, ec] = std::from_chars(first, s.data() + s.size(), n);
    pos += ptr - first;
    return n;
}

static bool char_at_is(std::string_view s, ptrdiff_t pos, char c)
{
    return pos < std::ssize(s) && s[checked_cast<size_t>(pos)] == c;
}

static bool digit_at(std::string_view s, ptrdiff_t pos)
{
    return pos < std::ssize(s) && s[checked_cast<size_t>(pos)] >= '0' &&
           s[checked_cast<size_t>(pos)] <= '9';
}

// Strip trailing whitespace from the lines a single-file diff (unified or
// context format) adds, like upstream's remove-trailing-ws script. Returns
// the line numbers, in the new file, of the lines that were stripped.
static std::vector<ptrdiff_t> strip_diff_trailing_ws(std::string &diff)
{
    std::vector<ptrdiff_t> stripped_lines;
    auto lines = split_lines_keep_eol(diff);
    std::string result;
    ptrdiff_t n = std::ssize(lines);
    ptrdiff_t i = 0;
    auto line_at = [&](ptrdiff_t k) { return lines[checked_cast<size_t>(k)]; };

    // Strip a line that adds content after a `keep`-byte prefix
    auto take_added = [&](std::string_view line, ptrdiff_t keep,
                          ptrdiff_t line_number) {
        ptrdiff_t ws = trailing_ws_len(line, keep);
        if (ws > 0) stripped_lines.push_back(line_number);
        append_without_trailing_ws(result, line, ws);
    };

    bool context = false;
    for (; i < n; ++i) {
        result += line_at(i);
        if (line_at(i).starts_with("--- ")) { ++i; break; }
        if (line_at(i).starts_with("*** ")) { context = true; ++i; break; }
    }

    while (i < n) {
        std::string_view line = line_at(i++);
        result += line;
        if (!context && line.starts_with("@@ -") && digit_at(line, 4)) {
            // @@ -a[,b] +c[,d] @@
            ptrdiff_t pos = 4;
            parse_diff_num(line, pos);
            ptrdiff_t removed = 1, added = 1;
            if (char_at_is(line, pos, ',')) removed = parse_diff_num(line, ++pos);
            if (!char_at_is(line, pos, ' ') || !char_at_is(line, pos + 1, '+') ||
                !digit_at(line, pos + 2)) {
                continue;
            }
            pos += 2;
            ptrdiff_t line_number = parse_diff_num(line, pos);
            if (char_at_is(line, pos, ',')) added = parse_diff_num(line, ++pos);
            while ((removed > 0 || added > 0) && i < n) {
                std::string_view hl = line_at(i++);
                if (hl.starts_with('+')) {
                    take_added(hl, 1, line_number);
                    added--;
                    line_number++;
                    continue;
                }
                if (hl.starts_with('-')) {
                    removed--;
                } else if (hl.starts_with(' ') || hl == "\n") {
                    removed--;
                    added--;
                    line_number++;
                }
                result += hl;
            }
        } else if (context && line.starts_with("--- ") && digit_at(line, 4) &&
                   line.ends_with(" ----\n")) {
            // --- c[,d] ----
            ptrdiff_t pos = 4;
            ptrdiff_t line_number = parse_diff_num(line, pos);
            ptrdiff_t last_line = line_number;
            if (char_at_is(line, pos, ',')) last_line = parse_diff_num(line, ++pos);
            for (; line_number <= last_line && i < n; ++line_number) {
                std::string_view hl = line_at(i++);
                if (hl.starts_with("+ ") || hl.starts_with("! ")) {
                    take_added(hl, 2, line_number);
                } else {
                    result += hl;
                }
                if (hl.starts_with("****") || hl.starts_with("*** ")) break;
            }
        }
    }

    diff = std::move(result);
    return stripped_lines;
}

// Remove trailing spaces and tabs from the given (ascending, 1-based) lines
// of a file, leaving all other bytes alone. The file is rewritten only if
// something changed.
static bool strip_file_trailing_ws(const std::string &path,
                                   std::span<const ptrdiff_t> line_numbers)
{
    std::string content = read_file(path);
    std::string result;
    ptrdiff_t lineno = 0;
    auto next = line_numbers.begin();
    for (auto line : split_lines_keep_eol(content)) {
        ++lineno;
        while (next != line_numbers.end() && *next < lineno) ++next;
        ptrdiff_t ws = 0;
        if (next != line_numbers.end() && *next == lineno) {
            ws = trailing_ws_len(line, 0);
        }
        append_without_trailing_ws(result, line, ws);
    }
    if (result == content) return true;
    return write_file(path, result);
}

// Like upstream change_db_strip_level, record in the series the strip level
// a refreshed patch was written with. Refresh always writes forward, so -R
// is dropped too.
static bool record_strip_level(QuiltState &q, const std::string &patch,
                               int strip_level) {
    if (!set_series_strip_level(q, patch, strip_level)) {
        err_line("Failed to write series file.");
        return false;
    }
    return true;
}

int cmd_refresh(QuiltState &q, int argc, char **argv) {
    enum { NO_TIMESTAMPS = 256, DIFFSTAT, BACKUP, SORT, NO_INDEX,
           STRIP_TRAILING_WHITESPACE, DIFF_ALGORITHM };
    static constexpr LongOpt longopts[] = {
        {"no-timestamps", OptArg::none, NO_TIMESTAMPS},
        {"diffstat", OptArg::none, DIFFSTAT},
        {"backup", OptArg::none, BACKUP},
        {"sort", OptArg::none, SORT},
        {"no-index", OptArg::none, NO_INDEX},
        {"strip-trailing-whitespace", OptArg::none, STRIP_TRAILING_WHITESPACE},
        {"diff-algorithm", OptArg::required, DIFF_ALGORITHM, true},
    };
    auto args = parse_options(argc, argv, "p:uU:cC:fz::h", longopts);
    if (!args) return 1;

    std::string p_format;
    bool no_timestamps = !get_env("QUILT_NO_DIFF_TIMESTAMPS").empty();
    bool no_index = !get_env("QUILT_NO_DIFF_INDEX").empty();
    bool sort_files = false;
    bool force = false;
    std::string diff_type;
    std::string context_num;
    bool opt_fork = false;
    std::optional<std::string> fork_name;
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

    for (const auto &opt : args->options) {
        switch (opt.key) {
        case 'p': p_format = opt.value; break;
        case 'f': force = true; break;
        case 'u':
        case 'c':
            diff_type = static_cast<char>(opt.key);
            context_num.clear();
            break;
        case 'U':
        case 'C':
            diff_type = static_cast<char>(opt.key);
            context_num = opt.value;
            break;
        case 'z':
            opt_fork = true;
            // An empty value means no name was given; a name that strips
            // to nothing still names the patches directory, as upstream
            if (!opt.value.empty()) fork_name = strip_patches_prefix(q, opt.value);
            break;
        case 'h': return command_help(argv[0]);
        case NO_TIMESTAMPS: no_timestamps = true; break;
        case NO_INDEX: no_index = true; break;
        case DIFFSTAT: opt_diffstat = true; break;
        case BACKUP: opt_backup = true; break;
        case SORT: sort_files = true; break;
        case STRIP_TRAILING_WHITESPACE: opt_strip_whitespace = true; break;
        case DIFF_ALGORITHM: {
            auto algo = parse_diff_algorithm(opt.value);
            if (!algo) {
                err("Unknown diff algorithm: "); err_line(opt.value);
                return 1;
            }
            diff_algorithm = *algo;
            break;
        }
        }
    }

    // Like upstream, any argument names the patch, even an empty one
    if (std::ssize(args->operands) > 1) return usage_error(argv[0]);
    std::optional<std::string_view> patch_arg;
    if (!args->operands.empty()) patch_arg = args->operands[0];

    if (!q.series_file_exists) {
        err_line("No series file found");
        return 1;
    }

    // Like upstream, look up the patch first, so that an unknown name is
    // reported as such. No argument, or an empty one, means the top patch.
    auto found = find_applied_patch(q, patch_arg.value_or(""));
    if (!found) return 1;
    std::string patch = *found;

    // Like upstream, -z forks only the top patch, and only when no patch
    // is named, even if the name is the top patch's or empty
    if (opt_fork && patch_arg) {
        err_line("Can only refresh the topmost patch with -z currently");
        return 1;
    }

    // Like the original quilt, validate the effective strip level, which
    // may come from the series file.
    if (p_format.empty()) {
        p_format = q.get_p_format(patch);
    }
    if (p_format != "0" && p_format != "1" && p_format != "ab") {
        err("Cannot refresh patches with -p"); err(p_format);
        err_line(", please specify -p0, -p1, or -pab instead");
        return 1;
    }
    int strip_level = p_format == "0" ? 0 : 1;

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
    // tree. Like upstream, the fork joins the series only once its patch is
    // written, so a fork with nothing in it leaves everything as it was.
    std::string fork_of;
    if (opt_fork) {
        std::string old_name(patch);

        std::string new_name = fork_name ? *fork_name : next_filename(old_name);

        if (file_exists(path_join(q.work_dir, q.patches_dir, new_name))) {
            err("Patch "); err(patch_path_display(q, new_name));
            err_line(" exists already");
            return 1;
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
        if (is_directory(new_pc)) delete_dir_recursive(new_pc);
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

        patch = new_name;
        fork_of = old_name;
    }

    // An unfinished fork leaves nothing behind
    auto fail = [&] {
        if (!fork_of.empty()) delete_dir_recursive(pc_patch_dir(q, patch));
        return 1;
    };

    // Get files tracked by this patch
    // Like upstream, the patch's own order unless --sort is given
    std::vector<std::string> tracked;
    if (sort_files) {
        tracked = files_in_patch(q, patch);
        std::ranges::sort(tracked);
    } else {
        tracked = files_in_patch_ordered(q, patch);
    }

    // Read existing patch file for header
    std::string patch_file = path_join(q.work_dir, q.patches_dir, patch);
    std::string old_content;
    std::string header;
    if (file_exists(patch_file)) {
        old_content = read_file(patch_file);
        header = patch_header(old_content);
    }

    // Generate diffs
    std::string work_base = basename(q.work_dir);
    std::string patch_content = header;
    // The patch with trailing whitespace stripped from added lines, which
    // --strip-trailing-whitespace writes if it strips the files too
    std::string stripped_content = header;
    // Added lines (per file) with trailing whitespace, reported once every
    // diff has succeeded. Like upstream, which checks the whole generated
    // patch, this includes files shadowed by later patches.
    std::map<std::string, std::vector<ptrdiff_t>> ws_lines;
    bool files_were_shadowed = false;

    auto append_diff = [&](std::string &content, const std::string &file,
                           const std::string &diff) {
        if (diff.empty()) return;
        if (!no_index) {
            std::string idx_name;
            if (p_format == "0") idx_name = file;
            else if (p_format == "ab") idx_name = "b/" + file;
            else idx_name = work_base + "/" + file;
            content += "Index: " + idx_name + "\n";
            content += "===================================================================\n";
        }
        content += diff;
        // Ensure trailing newline
        if (content.back() != '\n') content += '\n';
    };

    for (const auto &file : tracked) {
        std::string diff_out;
        // Like upstream, a file that a later patch also changes is diffed
        // against that patch's backup
        std::string next = fork_of.empty() ? next_patch_for_file(q, patch, file) : "";
        if (!next.empty()) {
            std::string this_backup = path_join(pc_patch_dir(q, patch), file);
            std::string next_backup = path_join(pc_patch_dir(q, next), file);
            diff_out = generate_path_diff(q, file,
                this_backup, true, next_backup, true,
                p_format, false, ctx_lines, diff_format, no_timestamps,
                diff_algorithm);
            files_were_shadowed = true;
        } else {
            diff_out = generate_file_diff(q, patch, file, p_format,
                                          false, ctx_lines,
                                          diff_format, no_timestamps,
                                          diff_algorithm);
        }
        if (diff_out.starts_with("Binary files ")) {
            err("Diff failed on file '"); err(file); err_line("', aborting");
            return fail();
        }
        if (files_were_shadowed && !force) {
            err("More recent patches modify files in patch ");
            err(patch_path_display(q, patch)); err_line(". Enforce refresh with -f.");
            return fail();
        }
        // Like upstream, complain for this and every later file once one is
        // shadowed, but strip them all anyway
        if (files_were_shadowed && opt_strip_whitespace) {
            err_line("Cannot use --strip-trailing-whitespace on a patch that has shadowed files.");
        }
        std::string stripped = diff_out;
        auto lines = strip_diff_trailing_ws(stripped);
        if (!lines.empty()) ws_lines[file] = std::move(lines);
        append_diff(patch_content, file, diff_out);
        if (opt_strip_whitespace) append_diff(stripped_content, file, stripped);
    }

    // Like upstream, there is something in the patch when the diff is not
    // empty. The header may hold lines that look like a diff.
    bool has_diff = std::ssize(patch_content) != std::ssize(header);

    if (!fork_of.empty() && !has_diff) {
        err("Nothing in patch "); err_line(patch_path_display(q, patch));
        return fail();
    }

    // Like upstream's remove-trailing-ws, report (or strip) the files in name
    // order. A file that cannot be opened stops the stripping, and the patch
    // is then written unstripped.
    bool strip_patch = opt_strip_whitespace;
    for (const auto &[file, lines] : ws_lines) {
        std::string list;
        for (auto n : lines) {
            if (!list.empty()) list += ',';
            list += std::to_string(n);
        }
        if (!opt_strip_whitespace) {
            err(std::ssize(lines) == 1 ? "Warning: trailing whitespace in line "
                                       : "Warning: trailing whitespace in lines ");
            err(list); err(" of "); err_line(file);
            continue;
        }
        err(std::ssize(lines) == 1 ? "Removing trailing whitespace from line "
                                   : "Removing trailing whitespace from lines ");
        err(list); err(" of "); err_line(file);
        // A shadowed file's line numbers are those of the next patch's
        // backup, but like upstream (which has a FIXME for this), they are
        // stripped in the working file, which may be a later patch's line.
        // A file that a later patch deleted is missing.
        std::string path = path_join(q.work_dir, file);
        if (!file_exists(path)) {
            err(file); err_line(": No such file or directory");
            strip_patch = false;
            break;
        }
        if (!strip_file_trailing_ws(path, lines)) {
            err("Failed to write "); err_line(file);
            return fail();
        }
    }
    if (strip_patch) patch_content = std::move(stripped_content);

    // Like upstream, --diffstat replaces the diffstat in the old header,
    // or adds one at its end, even when the diff is empty
    if (opt_diffstat) {
        std::string diff_portion =
            patch_content.substr(checked_cast<size_t>(std::ssize(header)));
        patch_content = replace_diffstat(header, generate_diffstat(diff_portion)) +
                        diff_portion;
    }

    // Like upstream, refreshing clears the .needs_refresh marker left by a
    // forced push, even when the patch file does not change
    std::string nr = path_join(pc_patch_dir(q, patch), ".needs_refresh");

    // Leave an existing patch file alone if its content is unchanged, even
    // when there is nothing in it. Like upstream, "Nothing in patch" is only
    // for a patch file that gets written.
    if (patch_content == old_content && file_exists(patch_file)) {
        if (file_exists(nr)) {
            delete_file(nr);
        }
        out("Patch "); out(patch_path_display(q, patch));
        out_line(" is unchanged");
        return record_strip_level(q, patch, strip_level) ? 0 : 1;
    }

    // Ensure patches directory exists
    std::string patch_dir = dirname(patch_file);
    if (!is_directory(patch_dir)) {
        make_dirs(patch_dir);
    }

    // Like upstream, back up the old patch file only when replacing it
    if (opt_backup && file_exists(patch_file)) {
        copy_file(patch_file, patch_file + "~");
    }

    // Write the patch file
    if (!write_file(patch_file, patch_content)) {
        err_line("Failed to write patch file " + patch_file);
        return fail();
    }

    if (!fork_of.empty()) {
        // Like upstream, insert the fork after the original (the top) with
        // the original's options. Refresh then records the strip level the
        // fork is written with, which defaults to the original's, and drops -R.
        if (!insert_in_series(q, patch, series_patch_args(q, fork_of),
                              q.patch_after_top())) {
            err_line("Failed to write series file.");
            delete_file(patch_file);
            return fail();
        }

        // Update applied: add fork after original
        q.applied.push_back(patch);
        std::string applied_path = path_join(q.work_dir, q.pc_dir, "applied-patches");
        write_applied(applied_path, q.applied);

        // The fork message replaces the "Refreshed patch" message
        out_line("Fork of patch " + patch_path_display(q, fork_of) +
                 " created as " + patch_path_display(q, patch));
    }

    // Update .timestamp
    write_file(path_join(pc_patch_dir(q, patch), ".timestamp"), "");

    // Clear .needs_refresh marker if present
    if (file_exists(nr)) {
        delete_file(nr);
    }

    if (fork_of.empty()) {
        if (!has_diff) {
            out("Nothing in patch "); out_line(patch_path_display(q, patch));
        } else {
            out("Refreshed patch "); out_line(patch_path_display(q, patch));
        }
    }
    return record_strip_level(q, patch, strip_level) ? 0 : 1;
}

int cmd_diff(QuiltState &q, int argc, char **argv) {
    enum { DIFF = 256, SNAPSHOT, NO_TIMESTAMPS, NO_INDEX, COMBINE, COLOR, SORT,
           DIFF_ALGORITHM };
    static constexpr LongOpt longopts[] = {
        {"diff", OptArg::required, DIFF},
        {"snapshot", OptArg::none, SNAPSHOT},
        {"no-timestamps", OptArg::none, NO_TIMESTAMPS},
        {"no-index", OptArg::none, NO_INDEX},
        {"combine", OptArg::required, COMBINE},
        {"color", OptArg::optional, COLOR},
        {"sort", OptArg::none, SORT},
        {"diff-algorithm", OptArg::required, DIFF_ALGORITHM, true},
    };
    auto args = parse_options(argc, argv, "p:P:RuU:cC:zh", longopts);
    if (!args) return 1;

    std::string_view patch_arg;
    std::string p_format;
    std::vector<std::string> file_filter;
    bool no_timestamps = !get_env("QUILT_NO_DIFF_TIMESTAMPS").empty();
    bool no_index = !get_env("QUILT_NO_DIFF_INDEX").empty();
    bool since_refresh = false;
    bool against_snapshot = false;
    bool reverse = false;
    bool sort_files = false;
    std::string diff_utility;
    std::optional<std::string_view> combine_arg;
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

    for (const auto &opt : args->options) {
        switch (opt.key) {
        case 'p': p_format = opt.value; break;
        case 'P': patch_arg = opt.value; break;
        case COMBINE: combine_arg = opt.value; break;
        case 'R': reverse = true; break;
        case 'z': since_refresh = true; break;
        case 'u':
        case 'c':
            diff_type = static_cast<char>(opt.key);
            context_num.clear();
            break;
        case 'U':
        case 'C':
            diff_type = static_cast<char>(opt.key);
            context_num = opt.value;
            break;
        case 'h': return command_help(argv[0]);
        case SNAPSHOT: against_snapshot = true; break;
        case DIFF: diff_utility = opt.value; break;
        case NO_TIMESTAMPS: no_timestamps = true; break;
        case NO_INDEX: no_index = true; break;
        case SORT: sort_files = true; break;
        case COLOR:
            if (!valid_color_value(opt.value)) return usage_error(argv[0]);
            break;
        case DIFF_ALGORITHM: {
            auto algo = parse_diff_algorithm(opt.value);
            if (!algo) {
                err("Unknown diff algorithm: "); err_line(opt.value);
                return 1;
            }
            diff_algorithm = *algo;
            break;
        }
        }
    }

    // Like upstream, an empty name names no file, unless the subdirectory
    // prefix makes it one
    for (auto arg : args->operands) {
        std::string file = subdir_path(q, arg);
        if (!file.empty()) file_filter.push_back(std::move(file));
    }

    if (!q.series_file_exists) {
        err_line("No series file found");
        return 1;
    }

    // Resolve --combine before -P, in the same order as upstream. Neither
    // "-" nor an empty name is looked up: "-" means the first applied
    // patch, and an empty name matches no patch in the range below.
    std::string combine_start;
    if (combine_arg && !combine_arg->empty() && *combine_arg != "-") {
        auto found = find_applied_patch(q, *combine_arg);
        if (!found) return 1;
        combine_start = *found;
    }

    if (since_refresh && against_snapshot) {
        err_line("Options `--snapshot' and `-z' cannot be combined.");
        return 1;
    }

    if (combine_arg && since_refresh) {
        err_line("Options `--combine' and `-z' cannot be combined.");
        return 1;
    }

    if (combine_arg && against_snapshot) {
        err_line("Options `--combine' and `--snapshot' cannot be combined.");
        return 1;
    }

    // No -P, or an empty one, means the top patch
    auto found = find_applied_patch(q, patch_arg);
    if (!found) return 1;
    std::string patch = *found;

    if (combine_arg) {
        if (*combine_arg == "-") {
            combine_start = q.applied.front();
        }
        if (combine_start.empty() ||
            std::ranges::find(q.applied, combine_start) > std::ranges::find(q.applied, patch)) {
            err("Patch "); err(format_patch(q, combine_start));
            err(" not applied before patch "); err_line(format_patch(q, patch));
            return 1;
        }
    }

    // Like the original quilt, validate the effective strip level, which
    // may come from the series file.
    if (p_format.empty()) {
        p_format = q.get_p_format(patch);
    }
    if (p_format != "0" && p_format != "1" && p_format != "ab") {
        err("Cannot diff patches with -p"); err(p_format);
        err_line(", please specify -p0, -p1, or -pab instead");
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

    // Like upstream's do_diff, a --diff utility gets just the two files,
    // as upstream names them, with an empty or missing one as /dev/null.
    // It runs only for files that differ, its output goes straight
    // through, without an Index line, and its exit status is ignored.
    auto run_diff_utility = [&](std::string old_f, std::string new_f) {
        if (reverse) std::swap(old_f, new_f);
        std::string base = q.work_dir + "/";
        std::string old_data, new_data;
        for (auto [path, data] : {std::pair{&old_f, &old_data}, std::pair{&new_f, &new_data}}) {
            if (path->starts_with(base)) path->erase(0, base.size());
            if (file_exists(*path)) *data = read_file(*path);
            if (data->empty()) *path = "/dev/null";
        }
        if (old_f == new_f || old_data == new_data) return;
        std::vector<std::string> cmd;
        for (auto &part : split_on_whitespace(diff_utility)) cmd.push_back(std::move(part));
        cmd.push_back(old_f);
        cmd.push_back(new_f);
        run_cmd_tty(cmd);
    };

    // A changed binary file is reported as "Binary files differ", and like
    // the original quilt, a failed diff aborts the whole command
    auto abort_on_binary = [&](std::string_view diff_out, std::string_view file) {
        if (diff_out.starts_with("Binary files ")) {
            err("Diff failed on file '"); err(file); err_line("', aborting");
            return true;
        }
        return false;
    };

    auto patches = patch_range_for_diff(q, patch);
    std::vector<std::string> tracked;
    if (against_snapshot) {
        std::string snap_dir = pc_patch_dir(q, SNAPSHOT_PATCH);
        if (!is_directory(snap_dir)) {
            err_line("No snapshot to diff against");
            return 1;
        }

        std::set<std::string> seen;
        auto snapshot_files = files_in_patch(q, SNAPSHOT_PATCH);
        std::ranges::sort(snapshot_files);
        append_unique_files(tracked, seen, snapshot_files);
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
        tracked = files_in_patch_ordered(q, patch);
    }

    apply_file_filter(tracked, file_filter);

    // Like upstream, files go in the order first seen unless --sort is given
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

        bool files_were_shadowed = false;

        for (const auto &file : tracked) {
            std::string backup_path = path_join(pc_patch_dir(q, patch), file);
            std::string working_path = path_join(q.work_dir, file);
            std::string tmp_file = path_join(tmp_dir, file);

            // Ensure temp subdirectory exists
            std::string tmp_file_dir = dirname(tmp_file);
            if (!is_directory(tmp_file_dir)) {
                make_dirs(tmp_file_dir);
            }

            // Rebuild the refreshed state as the original quilt does: start
            // from the backup (an empty backup means the file did not
            // exist), then apply this file's section of the stored patch.
            // The result is removed only if the section deletes the file;
            // a file the patch merely empties stays as an empty file.
            if (file_exists(backup_path) && !is_placeholder_copy(backup_path)) {
                write_file(tmp_file, read_file(backup_path));
            }

            // Apply stored patch section to temp file
            auto it = stored_sections.find(file);
            if (it != stored_sections.end() && !it->second.empty()) {
                // Build a minimal patch with correct paths for -p0
                std::string mini_patch = "--- " + file + "\n+++ " + file + "\n";
                // Extract just the hunk lines from the stored section
                auto section_lines = split_lines(it->second);
                bool in_hunk = false;
                bool deletes_file = false;
                for (const auto &sl : section_lines) {
                    if (sl.starts_with("@@")) {
                        in_hunk = true;
                        mini_patch += sl + "\n";
                    } else if (in_hunk) {
                        mini_patch += sl + "\n";
                    } else if (sl.starts_with("+++ /dev/null")) {
                        deletes_file = true;
                    }
                }
                if (in_hunk) {
                    if (!file_exists(tmp_file)) write_file(tmp_file, "");
                    std::string saved_cwd = get_cwd();
                    if (set_cwd(tmp_dir)) {
                        PatchOptions po;
                        po.strip_level = 0;
                        po.quiet = true;
                        builtin_patch(mini_patch, po);
                        set_cwd(saved_cwd);
                    }
                    if (deletes_file) delete_file(tmp_file);
                }
            }

            // Diff the reconstructed "refreshed" file against the working
            // file or, if a later applied patch modifies this file, against
            // that patch's backup. The original quilt warns about shadowed
            // files after the loop.
            std::string new_src = working_path;
            std::string shadowing_patch = next_patch_for_file(q, patch, file);
            if (!shadowing_patch.empty()) {
                files_were_shadowed = true;
                new_src = path_join(pc_patch_dir(q, shadowing_patch), file);
            }
            if (!file_exists(new_src) && !file_exists(tmp_file)) continue;
            if (!diff_utility.empty()) {
                run_diff_utility(tmp_file, new_src);
                continue;
            }

            std::string old_label, new_label;
            if (p_format == "ab") {
                old_label = "a/" + file;
                new_label = "b/" + file;
            } else if (p_format == "0") {
                old_label = file + ".orig";
                new_label = file;
            } else {
                old_label = work_base + ".orig/" + file;
                new_label = work_base + "/" + file;
            }

            std::string old_f = tmp_file;
            std::string new_f = new_src;
            if (reverse) {
                std::swap(old_f, new_f);
            }
            // As in the original quilt, a missing or empty old file and a
            // missing new file are diffed as /dev/null.
            if (!file_exists(old_f) || read_file(old_f).empty()) {
                old_f = "/dev/null";
                old_label = "/dev/null";
            }
            if (!file_exists(new_f)) {
                if (p_format == "0") old_label = new_label;
                new_f = "/dev/null";
                new_label = "/dev/null";
            }

            std::string diff_out;
            std::string old_data = old_f == "/dev/null" ? std::string() : read_file(old_f);
            std::string new_data = new_f == "/dev/null" ? std::string() : read_file(new_f);
            if (old_data != new_data &&
                (is_binary_data(old_data) || is_binary_data(new_data))) {
                diff_out = "Binary files differ\n";
            } else {
                int ctx = ctx_lines;
                auto extra_diff_opts = shell_split(get_env("QUILT_DIFF_OPTS"));
                int opts_ctx = parse_diff_opts_context(extra_diff_opts);
                if (opts_ctx >= 0) ctx = opts_ctx;

                DiffResult dr = builtin_diff(old_f, new_f, ctx,
                                             old_label, new_label, diff_format,
                                             diff_algorithm);
                diff_out = std::move(dr.output);
            }
            if (abort_on_binary(diff_out, file)) {
                delete_dir_recursive(tmp_dir);
                return 1;
            }
            if (!diff_out.empty()) {
                if (!no_index) {
                    out("Index: " + (p_format == "0" ? file : p_format == "ab" ? "b/" + file : work_base + "/" + file) + "\n");
                    out("===================================================================\n");
                }
                out(diff_out);
            }
        }

        if (files_were_shadowed) {
            err("Warning: more recent patches modify files in patch ");
            err_line(patch_path_display(q, patch));
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

            if (!diff_utility.empty()) {

                run_diff_utility(old_path, new_path);

                continue;

            }

            std::string diff_out = generate_path_diff(
                q, file, old_path, old_placeholder, new_path, new_placeholder,
                p_format, reverse, ctx_lines, diff_format,
                no_timestamps, diff_algorithm);
            if (abort_on_binary(diff_out, file)) return 1;
            if (!diff_out.empty()) {
                if (!no_index) {
                    out("Index: " + (p_format == "0" ? file : p_format == "ab" ? "b/" + file : work_base + "/" + file) + "\n");
                    out("===================================================================\n");
                }
                out(diff_out);
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

            if (!diff_utility.empty()) {

                run_diff_utility(old_path, new_path);

                continue;

            }

            std::string diff_out = generate_path_diff(
                q, file, old_path, true, new_path, new_placeholder,
                p_format, reverse, ctx_lines, diff_format,
                no_timestamps, diff_algorithm);
            if (abort_on_binary(diff_out, file)) return 1;
            if (!diff_out.empty()) {
                if (!no_index) {
                    out("Index: " + (p_format == "0" ? file : p_format == "ab" ? "b/" + file : work_base + "/" + file) + "\n");
                    out("===================================================================\n");
                }
                out(diff_out);
            }
        }
    } else {
        // Like upstream, warn after the diffs if more recent patches
        // modify files in this patch
        bool files_were_shadowed = false;
        for (const auto &file : tracked) {
            std::string shadowing = next_patch_for_file(q, patch, file);
            if (!shadowing.empty()) files_were_shadowed = true;

            std::string old_path = path_join(pc_patch_dir(q, patch), file);
            std::string new_path = path_join(q.work_dir, file);
            bool new_placeholder = false;

            // If a patch above this one shadows the file, use its backup
            if (!shadowing.empty()) {
                new_path = path_join(pc_patch_dir(q, shadowing), file);
                new_placeholder = true;
            }

            if (!diff_utility.empty()) {

                run_diff_utility(old_path, new_path);

                continue;

            }

            std::string diff_out = generate_path_diff(
                q, file, old_path, true, new_path, new_placeholder,
                p_format, reverse, ctx_lines, diff_format,
                no_timestamps, diff_algorithm);
            if (abort_on_binary(diff_out, file)) return 1;
            if (!diff_out.empty()) {
                if (!no_index) {
                    out("Index: " + (p_format == "0" ? file : p_format == "ab" ? "b/" + file : work_base + "/" + file) + "\n");
                    out("===================================================================\n");
                }
                out(diff_out);
            }
        }
        if (files_were_shadowed) {
            err("Warning: more recent patches modify files in patch ");
            err_line(patch_path_display(q, patch));
        }
    }

    return 0;
}

// The backup of a file named as the user typed it. Unlike path_join,
// concatenation keeps an absolute name inside the .pc directory, as
// upstream's "$QUILT_PC/$patch/$file" does.
static std::string revert_backup_path(const QuiltState &q, std::string_view patch,
                                      std::string_view file) {
    return pc_patch_dir(q, patch) + "/" + std::string(file);
}

// Like upstream's file_in_patch: the backup must be a regular file, and
// it is looked up through the filesystem, so "./f" and "d/../f" find the
// backup of "f".
static bool revert_file_in_patch(const QuiltState &q, std::string_view patch,
                                 std::string_view file) {
    std::string path = revert_backup_path(q, patch, file);
    return file_exists(path) && !is_directory(path);
}

// Lexically normalize a relative path ("./f", "d//f", "d/../f" become
// "f"), so that two names for the same file compare equal.
static std::string normalize_relative_path(std::string_view path) {
    std::vector<std::string_view> parts;
    while (!path.empty()) {
        ptrdiff_t slash = str_find(path, '/');
        std::string_view part = path;
        if (slash < 0) {
            path = {};
        } else {
            part = path.substr(0, checked_cast<size_t>(slash));
            path.remove_prefix(checked_cast<size_t>(slash + 1));
        }
        if (part.empty() || part == ".") continue;
        if (part == ".." && !parts.empty() && parts.back() != "..") {
            parts.pop_back();
        } else {
            parts.push_back(part);
        }
    }
    std::string result;
    for (auto part : parts) {
        if (!result.empty()) result += '/';
        result += part;
    }
    return result;
}

int cmd_revert(QuiltState &q, int argc, char **argv) {
    int rc;
    auto args = parse_patch_file_args(q, argc, argv, rc);
    if (!args) return rc;
    std::string_view patch_arg = args->patch;
    const auto &files = args->files;

    // No -P, or an empty one, means the top patch
    auto found = find_applied_patch(q, patch_arg);
    if (!found) return 1;
    std::string patch = *found;

    // Check every file before changing any, reporting each problem
    int status = 0;
    for (const auto &file : files) {
        if (!revert_file_in_patch(q, patch, file)) {
            err("File "); err(file); err(" is not in patch ");
            err_line(patch_path_display(q, patch));
            status = 1;
            continue;
        }
        auto later = std::ranges::find(q.applied, patch);
        for (++later; later != q.applied.end(); ++later) {
            if (revert_file_in_patch(q, *later, file)) {
                out("File "); out(file); out(" modified by patch ");
                out_line(patch_path_display(q, *later));
                status = 1;
                break;
            }
        }
    }
    if (status != 0) return status;

    // Read the patch file to apply its hunks to backup content
    std::string patch_file = path_join(q.work_dir, q.patches_dir, patch);
    std::string patch_text = read_file(patch_file);
    int strip_level = q.patch_strip_level.count(patch)
        ? q.patch_strip_level.at(patch) : 1;
    bool reverse = q.patch_reversed.contains(patch);
    auto targets = patch_target_files(patch_text, strip_level, reverse);

    for (const auto &file : files) {
        // Build the clean post-patch state by applying patch to backup,
        // under the name the patch uses for the file. builtin_patch keys
        // files by their names in the patch headers, which may be spelled
        // differently from the name given ("./f" for "f").
        std::string backup_content = read_file(revert_backup_path(q, patch, file));
        std::string name = normalize_relative_path(file);
        for (const auto &target : targets) {
            if (normalize_relative_path(target) == name) {
                name = target;
                break;
            }
        }
        std::map<std::string, std::string> memfs;
        memfs[name] = backup_content;
        PatchOptions opts;
        opts.strip_level = strip_level;
        opts.reverse = reverse;
        opts.quiet = true;
        opts.fs = &memfs;
        builtin_patch(patch_text, opts);

        std::string clean_content = memfs.count(name) ? memfs[name] : "";

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


static bool write_applied_checked(const QuiltState &q,
                                  std::span<const std::string> applied) {
    std::string applied_path = path_join(q.work_dir, q.pc_dir, "applied-patches");
    if (!write_applied(applied_path, applied)) {
        err_line("Failed to write applied-patches.");
        return false;
    }
    // Like the original quilt, remove the file once the stack is empty.
    if (applied.empty()) delete_file(applied_path);
    return true;
}

// Files an unapplied patch would modify, per its series options
static std::vector<std::string> unapplied_patch_files(const QuiltState &q,
                                                      std::string_view patch) {
    std::string content = read_file(path_join(q.work_dir, q.patches_dir, patch));
    return patch_target_files(content, q.get_strip_level(patch),
                              q.patch_reversed.contains(std::string(patch)));
}

// Like upstream's merge_patches, build the new contents of a patch that
// import -f replaces, keeping the old (o), all (a) or new (n) header. The
// headers are compared, and the old one kept, without their diffstats.
// Without a mode, keep whichever header is not empty, or show how they
// differ and fail. Unlike upstream, matching headers take the new version
// as it is (upstream writes the header twice), and the mode chosen here is
// not kept for the next patch.
static std::optional<std::string> merge_patches(std::string_view old_patch,
                                                std::string_view new_patch,
                                                char mode) {
    std::string old_desc = strip_diffstat(patch_header(old_patch));
    std::string new_desc = strip_diffstat(patch_header(new_patch));

    if (!mode) {
        if (old_desc.empty() || old_desc == new_desc) {
            mode = 'n';
        } else if (new_desc.empty()) {
            mode = 'o';
        } else {
            std::map<std::string, std::string> fs = {{"a", old_desc},
                                                     {"b", new_desc}};
            std::string diff = builtin_diff("a", "b", 3, {}, {},
                                            DiffFormat::unified,
                                            DiffAlgorithm::myers, &fs).output;
            // Like sed -e '1,2d', drop the --- and +++ lines
            for (int i = 0; i < 2; ++i) {
                diff.erase(0, checked_cast<size_t>(str_find(diff, '\n') + 1));
            }
            err_line("Patch headers differ:");
            err(diff);
            err_line("Please use -d {o|a|n} to specify which patch "
                     "header(s) to keep.");
            return std::nullopt;
        }
    }

    std::string merged;
    if (mode != 'n') merged = old_desc;
    if (mode == 'a') merged += "---\n";
    if (mode == 'o') {
        merged += patch_body(new_patch);
    } else {
        merged += new_patch;
    }
    return merged;
}


int cmd_delete(QuiltState &q, int argc, char **argv) {
    enum { BACKUP = 256 };
    static constexpr LongOpt longopts[] = {
        {"backup", OptArg::none, BACKUP},
    };
    auto args = parse_options(argc, argv, "nrh", longopts);
    if (!args) return 1;
    bool opt_remove = false;
    bool opt_backup = false;
    bool opt_next = false;
    for (const auto &opt : args->options) {
        switch (opt.key) {
        case 'n': opt_next = true; break;
        case 'r': opt_remove = true; break;
        case 'h': return command_help(argv[0]);
        case BACKUP: opt_backup = true; break;
        }
    }
    const auto &operands = args->operands;
    if (std::ssize(operands) > 1 || (opt_next && !operands.empty())) {
        return usage_error(argv[0]);
    }
    std::string_view patch_arg;
    if (!operands.empty()) patch_arg = operands[0];

    std::string patch;
    if (opt_next) {
        // Next unapplied patch
        ptrdiff_t top_idx = q.top_index();
        ptrdiff_t next_idx = top_idx + 1;
        if (next_idx >= std::ssize(q.series)) {
            err_line("No next patch");
            return 1;
        }
        patch = q.series[checked_cast<size_t>(next_idx)];
    } else {
        // No argument, or an empty one, means the top patch
        auto found = find_patch_in_series(q, patch_arg);
        if (!found) return 1;
        patch = *found;
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
        if (tracked.empty()) {
            out_line("Patch " + patch_path_display(q, patch) +
                     " appears to be empty, removing");
        } else {
            out_line("Removing patch " + patch_path_display(q, patch));
        }
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

    if (!remove_from_series(q, patch)) {
        err_line("Failed to write series file.");
        return 1;
    }

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
    auto args = parse_options(argc, argv, "P:h");
    if (!args) return 1;
    std::string_view old_arg;
    for (const auto &opt : args->options) {
        if (opt.key == 'h') return command_help(argv[0]);
        old_arg = opt.value;
    }
    if (std::ssize(args->operands) != 1) return usage_error(argv[0]);
    std::string new_name(strip_patches_prefix(q, args->operands[0]));

    // No -P, or an empty one, means the top patch
    auto found = find_patch_in_series(q, old_arg);
    if (!found) return 1;
    std::string old_patch = *found;

    // Like upstream, refuse a name in any use, so nothing is overwritten.
    // An empty name (from "" or "patches/") names the directories
    // themselves, so it is always in use.
    if (new_name.empty() || q.find_in_series(new_name) ||
        is_directory(pc_patch_dir(q, new_name)) ||
        file_exists(path_join(q.work_dir, q.patches_dir, new_name))) {
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

    if (!rename_in_series(q, old_patch, new_name)) {
        err_line("Failed to write series file.");
        if (renamed_pc_dir) {
            rename_path(pc_patch_dir(q, new_name), pc_patch_dir(q, old_patch));
        }
        if (renamed_patch_file) {
            rename_path(new_file, old_file);
        }
        return 1;
    }

    if (q.is_applied(old_patch) && !write_applied_checked(q, new_applied)) {
        rename_in_series(q, new_name, old_patch);
        if (renamed_pc_dir) {
            rename_path(pc_patch_dir(q, new_name), pc_patch_dir(q, old_patch));
        }
        if (renamed_patch_file) {
            rename_path(new_file, old_file);
        }
        return 1;
    }

    if (q.is_applied(old_patch)) {
        q.applied = std::move(new_applied);
    }

    out("Patch "); out(patch_path_display(q, old_patch));
    out(" renamed to "); out_line(patch_path_display(q, new_name));
    return 0;
}

int cmd_import(QuiltState &q, int argc, char **argv) {
    std::string strip_arg;  // -p value, recorded verbatim in the series
    std::string target_name;
    bool force = false;
    char dup_mode = 0;  // -d: keep the o(ld), a(ll) or n(ew) header
    bool reversed = false;
    std::vector<std::string> patchfiles;

    auto args = parse_options(argc, argv, "P:d:fp:Rh");
    if (!args) return 1;
    for (const auto &opt : args->options) {
        switch (opt.key) {
        case 'P': target_name = strip_patches_prefix(q, opt.value); break;
        case 'p': strip_arg = opt.value; break;
        case 'R': reversed = true; break;
        case 'd':
            if (opt.value != "o" && opt.value != "a" && opt.value != "n") {
                return usage_error(argv[0]);
            }
            dup_mode = opt.value[0];
            break;
        case 'f': force = true; break;
        case 'h': return command_help(argv[0]);
        }
    }
    for (auto file : args->operands) patchfiles.emplace_back(file);

    // Like upstream, importing no patches does nothing
    if (patchfiles.empty()) return 0;

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

    // Like the original quilt, record -p as given (even -p1, or a value that
    // is not a number), and insert every patch in front of the same one,
    // keeping their order.
    std::string patch_args;
    if (!strip_arg.empty()) patch_args = "-p" + strip_arg;
    if (reversed) patch_args += patch_args.empty() ? "-R" : " -R";
    std::string before = q.patch_after_top();

    for (const auto &patchfile : patchfiles) {
        if (!file_exists(patchfile)) {
            err_line("Patch " + patchfile + " does not exist");
            return 1;
        }

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

        // Copy patchfile to patches/<name>, merging the headers of a patch
        // it replaces unless -d n
        std::optional<std::string> merged;
        if (existing && dup_mode != 'n') {
            merged = merge_patches(read_file(dest), read_file(patchfile),
                                   dup_mode);
            if (!merged) return 1;
        }
        if (existing) {
            err_line("Replacing patch " + patch_path_display(q, name) +
                     " with new version");
        }
        if (merged ? !write_file(dest, *merged) : !copy_file(patchfile, dest)) {
            err_line("Failed to import patch " + patch_path_display(q, name));
            return 1;
        }

        // When replacing an existing patch the original quilt leaves the
        // series entry (and thus its -p/-R args) untouched.
        if (!existing && !insert_in_series(q, name, patch_args, before)) {
            err_line("Failed to write series file.");
            delete_file(dest);
            return 1;
        }

        if (!existing) {
            out_line("Importing patch " + patchfile +
                     " (stored as " + patch_path_display(q, name) + ")");
        }
    }

    return 0;
}

// Strip trailing spaces and tabs from each line of a header. Like upstream's
// sed -e 's:[ \t]*$::', this leaves a CR, and a missing final newline, alone.
static std::string strip_header_trailing_ws(std::string_view header) {
    std::string result;
    while (!header.empty()) {
        ptrdiff_t nl = str_find(header, '\n');
        ptrdiff_t len = nl < 0 ? std::ssize(header) : nl;
        std::string_view line = header.substr(0, checked_cast<size_t>(len));
        header.remove_prefix(checked_cast<size_t>(nl < 0 ? len : len + 1));
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
            line.remove_suffix(1);
        result += line;
        if (nl >= 0) result += '\n';
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
    bool mode_conflict = false;
    // Repeating a mode is fine; combining different modes is not.
    auto set_mode = [&](Mode m) {
        if (mode != PRINT && mode != m) mode_conflict = true;
        mode = m;
    };

    enum { BACKUP = 256, STRIP_TRAILING_WHITESPACE, STRIP_DIFFSTAT, DEP3 };
    static constexpr LongOpt longopts[] = {
        {"backup", OptArg::none, BACKUP},
        {"strip-trailing-whitespace", OptArg::none, STRIP_TRAILING_WHITESPACE},
        {"strip-diffstat", OptArg::none, STRIP_DIFFSTAT},
        {"dep3", OptArg::none, DEP3, true},
    };
    auto args = parse_options(argc, argv, "areh", longopts);
    if (!args) return 1;
    for (const auto &opt : args->options) {
        switch (opt.key) {
        case 'a': set_mode(APPEND); break;
        case 'r': set_mode(REPLACE); break;
        case 'e': set_mode(EDIT); break;
        case BACKUP: opt_backup = true; break;
        case STRIP_DIFFSTAT: opt_strip_ds = true; break;
        case STRIP_TRAILING_WHITESPACE: opt_strip_ws = true; break;
        case DEP3: opt_dep3 = true; break;
        case 'h': return command_help(argv[0]);
        }
    }

    if (mode_conflict || std::ssize(args->operands) > 1) return usage_error(argv[0]);
    std::string_view patch_arg;
    if (!args->operands.empty()) patch_arg = args->operands[0];

    // No argument, or an empty one, means the top patch
    auto found = find_patch_in_series(q, patch_arg);
    if (!found) return 1;
    std::string patch = *found;

    std::string patch_file = path_join(q.work_dir, q.patches_dir, patch);
    std::string content = read_file(patch_file);

    // Helper to apply --strip-diffstat and --strip-trailing-whitespace
    auto apply_strip = [&](std::string h) {
        if (opt_strip_ds) h = strip_diffstat(h);
        if (opt_strip_ws) h = strip_header_trailing_ws(h);
        return h;
    };

    // Like upstream, end the new header with a newline (unless it ends with
    // a CR) before the strip options apply, then append the patch body.
    auto write_header = [&](std::string h) {
        if (!h.empty() && h.back() != '\n' && h.back() != '\r') h += '\n';
        if (opt_backup) {
            copy_file(patch_file, patch_file + "~");
        }
        write_file(patch_file, apply_strip(std::move(h)) + patch_body(content));
    };

    if (mode == PRINT) {
        std::string header = apply_strip(patch_header(content));
        out(header);
        return 0;
    }

    if (mode == APPEND) {
        std::string stdin_data = read_stdin();
        write_header(patch_header(content) + stdin_data);
        out_line("Appended text to header of patch " +
                 patch_path_display(q, patch));
        return 0;
    }

    if (mode == REPLACE) {
        write_header(read_stdin());
        out_line("Replaced header of patch " +
                 patch_path_display(q, patch));
        return 0;
    }

    if (mode == EDIT) {
        std::string editor = get_env("EDITOR");
        if (editor.empty()) editor = "vi";

        std::string header = patch_header(content);
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

        std::string new_header = read_file(tmp_file);
        delete_file(tmp_file);

        write_header(std::move(new_header));
        out_line("Replaced header of patch " + patch_path_display(q, patch));
        return 0;
    }

    return 0;
}

int cmd_files(QuiltState &q, int argc, char **argv) {
    bool opt_verbose = false;
    bool opt_all = false;
    bool opt_labels = false;
    std::optional<std::string_view> combine_arg;

    enum { COMBINE = 256 };
    static constexpr LongOpt longopts[] = {
        {"combine", OptArg::required, COMBINE},
    };
    auto args = parse_options(argc, argv, "vhal", longopts);
    if (!args) return 1;
    for (const auto &opt : args->options) {
        switch (opt.key) {
        case 'v': opt_verbose = true; break;
        case 'a': opt_all = true; break;
        case 'l': opt_labels = true; break;
        case 'h': return command_help(argv[0]);
        case COMBINE: combine_arg = opt.value; break;
        }
    }
    if (std::ssize(args->operands) > 1) return usage_error(argv[0]);
    std::string_view patch_arg;
    if (!args->operands.empty()) patch_arg = args->operands[0];

    // Like upstream, resolve --combine first. Both "-" and an empty name
    // stand for the first applied patch, resolved below.
    std::string combine_start;
    if (combine_arg && !combine_arg->empty() && *combine_arg != "-") {
        auto found = find_patch(q, *combine_arg);
        if (!found) return 1;
        combine_start = *found;
    }

    // No argument, or an empty one, means the top patch
    auto found = find_patch_in_series(q, patch_arg);
    if (!found) return 1;
    std::string target_patch = *found;

    // Like upstream, --combine implies -a, and -a lists every patch in the
    // series from the first applied (or the --combine patch) through the
    // target, each on its own
    if (combine_arg) opt_all = true;
    std::vector<std::string> patches_to_show;
    if (opt_all) {
        std::string first = combine_start;
        if (first.empty() && !q.applied.empty()) first = q.applied.front();
        auto last = q.find_in_series(target_patch);
        ptrdiff_t start = -1;
        for (ptrdiff_t i = 0; last && i <= *last; ++i) {
            if (q.series[checked_cast<size_t>(i)] == first) { start = i; break; }
        }
        if (start < 0) {
            err_line("Patch " + format_patch(q, first) + " not applied before patch " +
                     format_patch(q, target_patch));
            return 1;
        }
        for (ptrdiff_t i = start; i <= *last; ++i) {
            patches_to_show.push_back(q.series[checked_cast<size_t>(i)]);
        }
    } else {
        patches_to_show.push_back(target_patch);
    }

    auto nonempty = [](const std::string &path) {
        return file_exists(path) && !read_file(path).empty();
    };
    bool use_status = opt_verbose && !opt_labels;
    for (const auto &patch : patches_to_show) {
        if (opt_all && use_status) out_line(patch);
        std::vector<std::string> file_list = q.is_applied(patch)
            ? files_in_patch(q, patch)
            : unapplied_patch_files(q, patch);
        std::ranges::sort(file_list);
        for (const auto &f : file_list) {
            std::string line;
            if (opt_labels) line = opt_verbose ? "[" + patch + "] " : patch + " ";
            if (use_status) {
                // Like upstream: - for a file the patch removes, + for one
                // it adds, judged by which side is empty or missing
                char status = ' ';
                bool backup = nonempty(path_join(pc_patch_dir(q, patch), f));
                bool current = nonempty(path_join(q.work_dir, f));
                if (backup && !current) status = '-';
                else if (!backup && current) status = '+';
                line += status;
                line += ' ';
            }
            out_line(line + f);
        }
    }

    return 0;
}

int cmd_patches(QuiltState &q, int argc, char **argv) {
    bool opt_verbose = false;
    enum { COLOR = 256 };
    static constexpr LongOpt longopts[] = {
        {"color", OptArg::optional, COLOR},
    };
    auto args = parse_options(argc, argv, "vh", longopts);
    if (!args) return 1;
    for (const auto &opt : args->options) {
        switch (opt.key) {
        case 'v': opt_verbose = true; break;
        case COLOR:
            if (!valid_color_value(opt.value)) return usage_error(argv[0]);
            break;
        case 'h': return command_help(argv[0]);
        }
    }
    if (args->operands.empty()) return usage_error(argv[0]);
    std::vector<std::string> target_files;
    for (auto file : args->operands) target_files.push_back(subdir_path(q, file));

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
            auto patched_files = unapplied_patch_files(q, patch);
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
    std::string_view strip_arg;

    auto args = parse_options(argc, argv, "Rp:qfh");
    if (!args) return 1;
    for (const auto &opt : args->options) {
        switch (opt.key) {
        case 'R': opt_reverse = true; break;
        case 'f': opt_force = true; break;
        case 'p': strip_arg = opt.value; break;
        case 'q': opt_quiet = true; break;
        case 'h': return command_help(argv[0]);
        }
    }
    if (!args->operands.empty()) return usage_error(argv[0]);

    if (q.applied.empty()) {
        err_line("No patches applied");
        return 1;
    }

    std::string top = q.applied.back();
    std::string stdin_data = read_stdin();

    if (stdin_data.empty()) {
        return 0;
    }

    // Apply patch using built-in patch engine
    PatchOptions patch_opts;
    // Like upstream, an empty -p means the default, 1, and patch checks the
    // rest, refusing a strip level that is not a number
    if (!strip_arg.empty()) set_strip_option(patch_opts, strip_arg);
    patch_opts.reverse = opt_reverse;
    patch_opts.quiet = opt_quiet;
    auto extra_patch_opts = shell_split(get_env("QUILT_PATCH_OPTS"));
    for (const auto &opt : extra_patch_opts) {
        std::string_view o = opt;
        if (o == "-R") patch_opts.reverse = true;
        else if (o == "-s") patch_opts.quiet = true;
        else if (o == "-E") patch_opts.remove_empty = true;
        else if (o.starts_with("--fuzz=")) set_fuzz_option(patch_opts, o.substr(7));
    }

    // Like upstream's "patch -d $SUBDIR", file names in the patch are
    // relative to the subdirectory quilt was run from
    std::string patch_dir = path_join(q.work_dir, q.subdir);
    if (!set_cwd(patch_dir)) {
        err_line("Cannot change into directory " + patch_dir);
        return 1;
    }

    // Track new files in the current patch, including deletions. Snapshot
    // every target file so that a failed fold can be undone: backups of
    // files the top patch already tracks predate the top patch, not the fold.
    struct Snapshot {
        std::string file;
        bool existed;
        std::string content;
        bool added;  // backed up into the top patch by this fold
    };
    std::vector<Snapshot> snapshots;
    auto affected_files = patch_target_files(stdin_data, patch_opts.strip_level,
                                             patch_opts.reverse);
    auto currently_tracked = files_in_patch(q, top);
    auto is_tracked = [&](const std::string &f) {
        return std::ranges::find(currently_tracked, f) != currently_tracked.end();
    };
    for (auto &f : affected_files) {
        f = subdir_path(q, f);
        std::string path = path_join(q.work_dir, f);
        Snapshot s{f, file_exists(path), {}, !is_tracked(f)};
        if (s.existed) s.content = read_file(path);
        if (s.added) backup_file(q, top, f);
        snapshots.push_back(std::move(s));
    }

    PatchResult r = builtin_patch(stdin_data, patch_opts);
    set_cwd(q.work_dir);

    // GNU patch backs up only the files it patches, so leave the missing
    // files it skipped out of the patch
    for (const auto &skipped : r.skipped) {
        std::string f = subdir_path(q, skipped);
        if (!is_tracked(f)) {
            delete_file(path_join(pc_patch_dir(q, top), f));
        }
    }
    out(r.out);
    err(r.err);

    if (r.exit_code != 0 && !opt_force) {
        // Like upstream, restore the pre-fold state and drop the backups
        // this fold added. Reject files stay behind.
        std::string pc_dir = pc_patch_dir(q, top);
        for (const auto &s : snapshots) {
            std::string path = path_join(q.work_dir, s.file);
            bool exists = file_exists(path);
            if (exists != s.existed || (exists && read_file(path) != s.content)) {
                bool ok;
                if (s.existed) {
                    std::string dir = dirname(path);
                    ok = (is_directory(dir) || make_dirs(dir)) &&
                         write_file(path, s.content);
                } else {
                    ok = delete_file(path);
                }
                if (!ok) {
                    err("File "); err(s.file); err_line(" may be corrupted");
                }
            }
            if (s.added) {
                std::string backup = path_join(pc_dir, s.file);
                delete_file(backup);
                for (std::string dir = dirname(backup);
                     std::ssize(dir) > std::ssize(pc_dir); dir = dirname(dir)) {
                    if (!delete_dir(dir)) break;
                }
            }
        }
        return 1;
    }

    return 0;
}

int cmd_fork(QuiltState &q, int argc, char **argv) {
    auto args = parse_options(argc, argv, "h");
    if (!args) return 1;
    if (!args->options.empty()) return command_help(argv[0]);
    if (std::ssize(args->operands) > 1) return usage_error(argv[0]);
    std::optional<std::string> given_name;
    if (!args->operands.empty()) given_name = strip_patches_prefix(q, args->operands[0]);

    auto top = find_top_patch(q);
    if (!top) return 1;
    std::string old_name = *top;

    // An empty name, given as "" or as "patches/", is refused below since
    // .pc/ itself exists, as upstream does
    std::string new_name = given_name ? *given_name : next_filename(old_name);

    // Like upstream, refuse a name in any use, so nothing is overwritten
    if (q.find_in_series(new_name) || is_directory(pc_patch_dir(q, new_name)) ||
        file_exists(path_join(q.work_dir, q.patches_dir, new_name))) {
        err("Patch "); err(patch_path_display(q, new_name));
        err_line(" exists already, please choose a new name");
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

    if (!rename_in_series(q, old_name, new_name)) {
        err_line("Failed to write series file.");
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
        rename_in_series(q, new_name, old_name);
        if (renamed_pc_dir) {
            rename_path(new_pc, old_pc);
        }
        if (copied_patch_file) {
            delete_file(new_file);
        }
        return 1;
    }

    q.applied = std::move(new_applied);

    out_line("Fork of patch " + patch_path_display(q, old_name) +
             " created as " + patch_path_display(q, new_name));
    return 0;
}

int cmd_upgrade(QuiltState &, int argc, char **argv)
{
    // Like upstream, take one argument, which means nothing
    auto args = parse_options(argc, argv, "h");
    if (!args) return 1;
    if (!args->options.empty()) return command_help(argv[0]);
    if (std::ssize(args->operands) > 1) return usage_error(argv[0]);
    return 0;
}

// === src/cmd_graph.cpp ===

// This is free and unencumbered software released into the public domain.
#include <cmath>
#include <iomanip>
#include <limits>
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

    // Nodes without any edge are de-emphasized, matching the original
    // dependency-graph script's close_node_style.
    std::set<int> connected;
    for (const auto &[key, value] : edges) {
        (void)value;
        connected.insert(key.first);
        connected.insert(key.second);
    }

    for (const auto &node : nodes) {
        if (!used_nodes.contains(node.number)) continue;

        std::vector<std::string> attrs = node.attrs;
        if (!connected.contains(node.number)) {
            attrs.push_back("color=grey");
        }
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
    bool opt_postscript = false;

    enum { ALL = 256, REDUCE, LINES, EDGE_LABELS };
    static constexpr LongOpt longopts[] = {
        {"all", OptArg::none, ALL},
        {"reduce", OptArg::none, REDUCE},
        {"lines", OptArg::optional, LINES},
        {"edge-labels", OptArg::required, EDGE_LABELS},
    };
    auto args = parse_options(argc, argv, "T:h", longopts);
    if (!args) return 1;
    for (const auto &opt : args->options) {
        switch (opt.key) {
        case 'T':
            if (opt.value != "ps") return usage_error(argv[0]);
            opt_postscript = true;
            break;
        case ALL: opt_all = true; break;
        case REDUCE: opt_reduce = true; break;
        case LINES:
            // Like upstream, --lines alone means 2, and the number only
            // goes after "=", so "--lines 3" names patch 3
            if (opt.value.empty()) {
                opt_lines = 2;
            } else if (is_number(opt.value)) {
                // Saturate, since no patch has more lines than that
                int lines = 0;
                auto [ptr, ec] = std::from_chars(opt.value.data(),
                                                 opt.value.data() + opt.value.size(), lines);
                if (ec == std::errc::result_out_of_range) {
                    lines = std::numeric_limits<int>::max();
                }
                opt_lines = lines;
            } else {
                return usage_error(argv[0]);
            }
            break;
        case EDGE_LABELS:
            if (opt.value != "files") return usage_error(argv[0]);
            opt_edge_labels = true;
            break;
        case 'h': return command_help(argv[0]);
        }
    }
    const auto &operands = args->operands;
    if (std::ssize(operands) > 1 || (opt_all && !operands.empty())) {
        return usage_error(argv[0]);
    }
    if (opt_postscript) {
        err_line("quilt graph -T ps: not implemented");
        return 1;
    }
    std::string_view patch_arg;
    if (!operands.empty()) patch_arg = operands[0];

    std::string selected_patch;
    if (!opt_all) {
        // No argument, or an empty one, means the top patch
        auto found = find_applied_patch(q, patch_arg);
        if (!found) return 1;
        selected_patch = *found;
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

    enum { FROM = 256, TO, CC, BCC, SUBJECT, SEND, MBOX, CHARSET, SENDER, PREFIX,
           REPLY_TO, SIGNATURE };
    static constexpr LongOpt longopts[] = {
        {"from", OptArg::required, FROM},
        {"to", OptArg::required, TO},
        {"cc", OptArg::required, CC},
        {"bcc", OptArg::required, BCC},
        {"subject", OptArg::required, SUBJECT},
        {"send", OptArg::none, SEND},
        {"mbox", OptArg::required, MBOX},
        {"charset", OptArg::required, CHARSET},
        {"sender", OptArg::required, SENDER},
        {"prefix", OptArg::required, PREFIX},
        {"reply-to", OptArg::required, REPLY_TO},
        {"signature", OptArg::required, SIGNATURE},
    };
    auto args = parse_options(argc, argv, "m:M:h", longopts);
    if (!args) return 1;
    for (const auto &opt : args->options) {
        switch (opt.key) {
        case MBOX: mbox_file = opt.value; break;
        case SEND:
            err_line("quilt mail: send mode is not supported; use --mbox");
            return 1;
        case SENDER: sender_addr = opt.value; break;
        case FROM: from_addr = opt.value; break;
        case PREFIX: prefix = opt.value; break;
        case TO: to_addrs.emplace_back(opt.value); break;
        case CC: cc_addrs.emplace_back(opt.value); break;
        case BCC: bcc_addrs.emplace_back(opt.value); break;
        // No cover letter, so its options do nothing
        case 'm': case 'M': case SUBJECT: case REPLY_TO: break;
        case CHARSET: case SIGNATURE: break;
        case 'h': return command_help(argv[0]);
        }
    }
    const auto &positional = args->operands;
    if (std::ssize(positional) > 2) return usage_error(argv[0]);

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
        std::string name(positional[0]);
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
        std::string first_name(positional[0]);
        std::string last_name(positional[1]);

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
        std::string header = patch_header(content);
        std::string diff = patch_body(content);

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

// Print the help for -h or --help ahead of any "--", else refuse to run
static int not_implemented(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--") break;
        if (arg == "-h" || arg == "--help") return command_help(argv[0]);
    }
    err("quilt ");
    err(argv[0]);
    err_line(": not implemented");
    return 1;
}

int cmd_grep(QuiltState &, int argc, char **argv)  { return not_implemented(argc, argv); }
int cmd_setup(QuiltState &, int argc, char **argv) { return not_implemented(argc, argv); }
int cmd_shell(QuiltState &, int argc, char **argv) { return not_implemented(argc, argv); }

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

bool delete_dir(std::string_view path)
{
    std::wstring wpath = utf8_to_wide(path);
    return RemoveDirectoryW(wpath.c_str()) != 0;
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

int64_t file_mtime(std::string_view path, int32_t *nsec)
{
    std::wstring wpath = utf8_to_wide(path);
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &data))
        return -1;
    // FILETIME: 100-nanosecond intervals since 1601-01-01
    uint64_t ft = (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32)
                | data.ftLastWriteTime.dwLowDateTime;
    ft -= 116444736000000000ULL;
    if (nsec) *nsec = static_cast<int32_t>(ft % 10000000ULL * 100);
    return static_cast<int64_t>(ft / 10000000ULL);
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


