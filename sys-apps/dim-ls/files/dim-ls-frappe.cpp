#include <algorithm>
#include <cerrno>
#include <climits>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
#include <getopt.h>
#include <grp.h>
#include <iostream>
#include <pwd.h>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

enum class Format { one_per_line, many_per_line, long_format, with_commas, horizontal };
enum class SortBy { name, time, size, extension, none };
enum class TimeType { mtime, ctime, atime };
enum class IndicatorStyle { none, slash, file_type, classify };

static Format format = Format::many_per_line;
static SortBy sort_by = SortBy::name;
static TimeType time_type = TimeType::mtime;
static bool sort_reverse = false;
static bool show_all = false;
static bool show_almost_all = false;
static bool long_format = false;
static bool human_readable = false;
static bool print_inode = false;
static bool print_block_size = false;
static bool recursive = false;
static bool no_group = false;
static bool no_owner = false;
static bool numeric_ids = false;
static bool color_output = false;
static bool is_tty = false;
static bool show_git_status = false;
static bool tree_view = false;
static bool group_dirs_first = false;
static bool follow_symlinks = false;
static bool octal_permissions = false;
static IndicatorStyle indicator_style = IndicatorStyle::none;
static size_t line_length = 80;
static size_t tabsize = 8;

struct FileEntry {
    std::string name;
    std::string link_target;
    struct stat st;
    bool stat_ok = false;
};

static std::vector<FileEntry> files;
static std::unordered_map<std::string, std::string> git_status_map;

static size_t inode_width = 0;
static size_t blocks_width = 0;
static size_t nlink_width = 0;
static size_t owner_width = 0;
static size_t group_width = 0;
static size_t size_width = 0;

static int exit_status = 0;

// Catppuccin Mocha theme — accent: blue
static const char *color_reset = "\033[0m";

// -- Foreground colors --
static const char *color_dir   = "\033[1;38;2;140;170;238m";  // Bold Blue      #8caaee
static const char *color_link  = "\033[1;38;2;186;187;241m";  // Bold Lavender  #babbf1
static const char *color_exec  = "\033[1;38;2;166;209;137m";  // Bold Green     #a6d189
static const char *color_fifo  = "\033[38;2;229;200;144m";    // Yellow         #e5c890
static const char *color_sock  = "\033[1;38;2;202;158;230m";  // Bold Mauve     #ca9ee6
static const char *color_blk   = "\033[1;38;2;239;159;118m";  // Bold Peach     #ef9f76
static const char *color_chr   = "\033[1;38;2;239;159;118m";  // Bold Peach     #ef9f76

// -- Background combinations --
static const char *color_suid  = "\033[1;38;2;198;208;245;48;2;231;130;132m";  // Text on Red    fg:#c6d0f5 bg:#e78284
static const char *color_sgid  = "\033[1;38;2;48;52;70;48;2;229;200;144m";     // Base on Yellow fg:#303446 bg:#e5c890
static const char *color_sticky= "\033[1;38;2;198;208;245;48;2;140;170;238m";  // Text on Blue   fg:#c6d0f5 bg:#8caaee
static const char *color_ow    = "\033[1;38;2;140;170;238;48;2;166;209;137m";  // Blue on Green  fg:#8caaee bg:#a6d189
static const char *color_tw    = "\033[1;38;2;48;52;70;48;2;166;209;137m";     // Base on Green  fg:#303446 bg:#a6d189

static const char *color_git_staged  = "\033[1;38;2;166;209;137m";  // Green
static const char *color_git_modified = "\033[1;38;2;229;200;144m"; // Yellow
static const char *color_git_untracked = "\033[1;38;2;231;130;132m"; // Red

static std::string file_type_char(mode_t mode, bool stat_ok) {
    if (stat_ok) {
        if (S_ISDIR(mode)) return "/";
        if (S_ISLNK(mode)) return "@";
        if (S_ISFIFO(mode)) return "|";
        if (S_ISSOCK(mode)) return "=";
        if (S_ISBLK(mode)) return "#";
        if (S_ISCHR(mode)) return "%";
    }
    return "";
}

static std::string classify_char(mode_t mode, bool stat_ok) {
    if (stat_ok && S_ISREG(mode) && (mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
        return "*";
    return file_type_char(mode, stat_ok);
}

static std::string indicator_str(bool stat_ok, mode_t mode) {
    switch (indicator_style) {
        case IndicatorStyle::slash:
            if (stat_ok && S_ISDIR(mode)) return "/";
            return "";
        case IndicatorStyle::file_type:
            return file_type_char(mode, stat_ok);
        case IndicatorStyle::classify:
            return classify_char(mode, stat_ok);
        default:
            return "";
    }
}

static std::string color_for(mode_t mode) {
    if (!color_output) return "";
    if (S_ISDIR(mode)) {
        if (mode & S_ISVTX) {
            if (mode & S_IWOTH) return color_tw;
            return color_sticky;
        }
        if (mode & S_IWOTH) return color_ow;
        return color_dir;
    }
    if (S_ISLNK(mode)) return color_link;
    if (S_ISFIFO(mode)) return color_fifo;
    if (S_ISSOCK(mode)) return color_sock;
    if (S_ISBLK(mode)) return color_blk;
    if (S_ISCHR(mode)) return color_chr;
    if (S_ISREG(mode)) {
        if (mode & S_ISUID) return color_suid;
        if (mode & S_ISGID) return color_sgid;
        if (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) return color_exec;
    }
    return "";
}

static const char *color_for_git_status(const std::string &status) {
    if (!color_output) return "";
    if (status.empty()) return "";
    // XY format: first char = index (staging area), second = work tree
    // Index changes
    if (status[0] == 'M' || status[0] == 'A' || status[0] == 'D' ||
        status[0] == 'R' || status[0] == 'C')
        return color_git_staged;
    // Work tree changes
    if (status[0] == '?' || status[0] == '!' || status[1] == 'M' || status[1] == 'D')
        return status[0] == '?' ? color_git_untracked : color_git_modified;
    return "";
}

static const char *icon_for(const std::string &name, mode_t mode = 0) {
    if (mode && S_ISDIR(mode)) return " ";
    if (mode && S_ISLNK(mode)) return " ";
    auto dot = name.rfind('.');
    if (dot == std::string::npos) {
        // Dotfiles without extension (name starts with .)
        if (name == ".gitignore")   return " ";
        if (name == ".gitattributes") return " ";
        if (name == ".gitmodules")  return " ";
        if (name == ".editorconfig") return " ";
        if (name == ".prettierrc")  return " ";
        if (name == ".eslintrc")    return " ";
        if (name == ".browserslistrc") return " ";
        if (name == ".npmrc")       return " ";
        if (name == ".nvmrc")       return " ";
        if (name == "Dockerfile")   return "󰡨 ";
        if (name == "Makefile")     return " ";
        if (name == "Makefile.am")  return " ";
        if (name == "Makefile.in")  return " ";
        if (name == "Gemfile")      return " ";
        if (name == "Rakefile")     return " ";
        if (name == "Cargo.toml")   return " ";
        if (name == "Cargo.lock")   return " ";
        if (name == "CMakeLists.txt") return " ";
        if (name == "Justfile")     return " ";
        if (name == "Containerfile")return "󰡨 ";
        if (name == "Vagrantfile")  return " ";
        if (name == "Procfile")     return " ";
        return "";
    }
    auto ext = std::string_view(name).substr(dot);
    if (ext == ".7z")        return " ";
    if (ext == ".ai")        return " ";
    if (ext == ".android")   return " ";
    if (ext == ".apk")       return " ";
    if (ext == ".asm")       return " ";
    if (ext == ".astro")     return " ";
    if (ext == ".avi")       return " ";
    if (ext == ".awk")       return " ";
    if (ext == ".azcli")     return " ";
    if (ext == ".bak")       return "󰁯 ";
    if (ext == ".bash")      return " ";
    if (ext == ".bat")       return " ";
    if (ext == ".bazel")     return " ";
    if (ext == ".bib")       return "󱉟 ";
    if (ext == ".bicep")     return " ";
    if (ext == ".blend")     return "󰂫 ";
    if (ext == ".bmp")       return " ";
    if (ext == ".bz2")       return " ";
    if (ext == ".c")         return " ";
    if (ext == ".cbl")       return " ";
    if (ext == ".cc")        return " ";
    if (ext == ".cfg")       return " ";
    if (ext == ".cjs")       return " ";
    if (ext == ".clj")       return " ";
    if (ext == ".cljs")      return " ";
    if (ext == ".cmake")     return " ";
    if (ext == ".cob")       return " ";
    if (ext == ".coffee")    return " ";
    if (ext == ".conda")     return " ";
    if (ext == ".conf")      return " ";
    if (ext == ".cpp")       return " ";
    if (ext == ".cr")        return " ";
    if (ext == ".cs")        return "󰌛 ";
    if (ext == ".csh")       return " ";
    if (ext == ".cson")      return " ";
    if (ext == ".css")       return " ";
    if (ext == ".csv")       return " ";
    if (ext == ".cts")       return " ";
    if (ext == ".cu")        return " ";
    if (ext == ".cue")       return "󰲹 ";
    if (ext == ".cxx")       return " ";
    if (ext == ".d")         return " ";
    if (ext == ".dart")      return " ";
    if (ext == ".db")        return " ";
    if (ext == ".desktop")   return " ";
    if (ext == ".diff")      return " ";
    if (ext == ".dll")       return " ";
    if (ext == ".doc")       return "󰈬 ";
    if (ext == ".dockerfile")return "󰡨 ";
    if (ext == ".docx")      return "󰈬 ";
    if (ext == ".dot")       return "󱁉 ";
    if (ext == ".download")  return " ";
    if (ext == ".dump")      return " ";
    if (ext == ".ebuild")    return " ";
    if (ext == ".ejs")       return " ";
    if (ext == ".el")        return " ";
    if (ext == ".elc")       return " ";
    if (ext == ".elm")       return " ";
    if (ext == ".env")       return " ";
    if (ext == ".eot")       return " ";
    if (ext == ".epub")      return " ";
    if (ext == ".erb")       return " ";
    if (ext == ".erl")       return " ";
    if (ext == ".ex")        return " ";
    if (ext == ".exe")       return " ";
    if (ext == ".exs")       return " ";
    if (ext == ".f90")       return "󱈚 ";
    if (ext == ".feature")   return " ";
    if (ext == ".fish")      return " ";
    if (ext == ".flac")      return " ";
    if (ext == ".fnl")       return " ";
    if (ext == ".fs")        return " ";
    if (ext == ".fsi")       return " ";
    if (ext == ".fsx")       return " ";
    if (ext == ".gd")        return " ";
    if (ext == ".gemfile")   return " ";
    if (ext == ".gemspec")   return " ";
    if (ext == ".gif")       return " ";
    if (ext == ".git")       return " ";
    if (ext == ".gitignore") return " ";
    if (ext == ".glb")       return " ";
    if (ext == ".gleam")     return " ";
    if (ext == ".glsl")      return " ";
    if (ext == ".go")        return " ";
    if (ext == ".godot")     return " ";
    if (ext == ".gql")       return " ";
    if (ext == ".gradle")    return " ";
    if (ext == ".graphql")   return " ";
    if (ext == ".gz")        return " ";
    if (ext == ".h")         return " ";
    if (ext == ".haml")      return " ";
    if (ext == ".hbs")       return " ";
    if (ext == ".heex")      return " ";
    if (ext == ".hex")       return " ";
    if (ext == ".hh")        return " ";
    if (ext == ".hpp")       return " ";
    if (ext == ".hrl")       return " ";
    if (ext == ".hs")        return " ";
    if (ext == ".htm")       return " ";
    if (ext == ".html")      return " ";
    if (ext == ".http")      return " ";
    if (ext == ".hx")        return " ";
    if (ext == ".hxx")       return " ";
    if (ext == ".ico")       return " ";
    if (ext == ".import")    return " ";
    if (ext == ".info")      return " ";
    if (ext == ".ini")       return " ";
    if (ext == ".ino")       return " ";
    if (ext == ".ipynb")     return " ";
    if (ext == ".iso")       return " ";
    if (ext == ".jar")       return " ";
    if (ext == ".java")      return " ";
    if (ext == ".jl")        return " ";
    if (ext == ".jinja")     return " ";
    if (ext == ".jpeg")      return " ";
    if (ext == ".jpg")       return " ";
    if (ext == ".js")        return " ";
    if (ext == ".json")      return " ";
    if (ext == ".json5")     return " ";
    if (ext == ".jsonc")     return " ";
    if (ext == ".jsx")       return " ";
    if (ext == ".jvm")       return " ";
    if (ext == ".kbx")       return "󰯄 ";
    if (ext == ".kdb")       return " ";
    if (ext == ".kdbx")      return " ";
    if (ext == ".ko")        return " ";
    if (ext == ".kt")        return " ";
    if (ext == ".kts")       return " ";
    if (ext == ".lck")       return " ";
    if (ext == ".less")      return " ";
    if (ext == ".lhs")       return " ";
    if (ext == ".lib")       return " ";
    if (ext == ".license")   return " ";
    if (ext == ".liquid")    return " ";
    if (ext == ".lock")      return " ";
    if (ext == ".log")       return "󰌱 ";
    if (ext == ".lrc")       return "󰨖 ";
    if (ext == ".lua")       return " ";
    if (ext == ".luac")      return " ";
    if (ext == ".luau")      return " ";
    if (ext == ".m")         return " ";
    if (ext == ".m3u")       return "󰲹 ";
    if (ext == ".m3u8")      return "󰲹 ";
    if (ext == ".m4a")       return " ";
    if (ext == ".m4v")       return " ";
    if (ext == ".magnet")    return " ";
    if (ext == ".makefile")  return " ";
    if (ext == ".markdown")  return " ";
    if (ext == ".material")  return " ";
    if (ext == ".md")        return " ";
    if (ext == ".md5")       return "󰕥 ";
    if (ext == ".mdx")       return " ";
    if (ext == ".mint")      return "󰌪 ";
    if (ext == ".mjs")       return " ";
    if (ext == ".mk")        return " ";
    if (ext == ".mkv")       return " ";
    if (ext == ".ml")        return " ";
    if (ext == ".mli")       return " ";
    if (ext == ".mm")        return " ";
    if (ext == ".mo")        return " ";
    if (ext == ".mobi")      return " ";
    if (ext == ".mojo")      return " ";
    if (ext == ".mov")       return " ";
    if (ext == ".mp3")       return " ";
    if (ext == ".mp4")       return " ";
    if (ext == ".mts")       return " ";
    if (ext == ".mustache")  return " ";
    if (ext == ".nfo")       return " ";
    if (ext == ".nim")       return " ";
    if (ext == ".nimble")    return " ";
    if (ext == ".nix")       return " ";
    if (ext == ".norg")      return " ";
    if (ext == ".nu")        return " ";
    if (ext == ".o")         return " ";
    if (ext == ".obj")       return "󰆧 ";
    if (ext == ".odin")      return "󰟢 ";
    if (ext == ".ogg")       return " ";
    if (ext == ".ogv")       return " ";
    if (ext == ".opus")      return " ";
    if (ext == ".org")       return " ";
    if (ext == ".otf")       return " ";
    if (ext == ".part")      return " ";
    if (ext == ".patch")     return " ";
    if (ext == ".pck")       return " ";
    if (ext == ".pdf")       return " ";
    if (ext == ".pem")       return "󰷖 ";
    if (ext == ".php")       return " ";
    if (ext == ".pl")        return " ";
    if (ext == ".plist")     return " ";
    if (ext == ".ply")       return "󰆧 ";
    if (ext == ".pm")        return " ";
    if (ext == ".png")       return " ";
    if (ext == ".po")        return " ";
    if (ext == ".pot")       return " ";
    if (ext == ".pp")        return " ";
    if (ext == ".ppt")       return "󰈧 ";
    if (ext == ".pptx")      return "󰈧 ";
    if (ext == ".prisma")    return " ";
    if (ext == ".pro")       return " ";
    if (ext == ".ps1")       return "󰨊 ";
    if (ext == ".psd")       return " ";
    if (ext == ".psd1")      return "󰨊 ";
    if (ext == ".psm1")      return "󰨊 ";
    if (ext == ".pub")       return "󰷖 ";
    if (ext == ".pug")       return " ";
    if (ext == ".py")        return " ";
    if (ext == ".pyc")       return " ";
    if (ext == ".pyd")       return " ";
    if (ext == ".pyi")       return " ";
    if (ext == ".pyo")       return " ";
    if (ext == ".pyw")       return " ";
    if (ext == ".pyx")       return " ";
    if (ext == ".qm")        return " ";
    if (ext == ".qml")       return " ";
    if (ext == ".qrc")       return " ";
    if (ext == ".qss")       return " ";
    if (ext == ".r")         return "󰟔 ";
    if (ext == ".rake")      return " ";
    if (ext == ".rar")       return " ";
    if (ext == ".rasi")      return " ";
    if (ext == ".razor")     return "󱦘 ";
    if (ext == ".rb")        return " ";
    if (ext == ".res")       return " ";
    if (ext == ".resi")      return " ";
    if (ext == ".rkt")       return "󰘧 ";
    if (ext == ".rlib")      return " ";
    if (ext == ".rmd")       return " ";
    if (ext == ".rproj")     return "󰗆 ";
    if (ext == ".rs")        return " ";
    if (ext == ".rss")       return " ";
    if (ext == ".rtf")       return " ";
    if (ext == ".s")         return " ";
    if (ext == ".sass")      return " ";
    if (ext == ".sbt")       return " ";
    if (ext == ".sc")        return " ";
    if (ext == ".scad")      return " ";
    if (ext == ".scala")     return " ";
    if (ext == ".scm")       return "󰘧 ";
    if (ext == ".scss")      return " ";
    if (ext == ".sh")        return " ";
    if (ext == ".sha1")      return "󰕥 ";
    if (ext == ".sha256")    return "󰕥 ";
    if (ext == ".sha512")    return "󰕥 ";
    if (ext == ".sig")       return " ";
    if (ext == ".slim")      return " ";
    if (ext == ".sln")       return " ";
    if (ext == ".smi")       return "󰨖 ";
    if (ext == ".so")        return " ";
    if (ext == ".sol")       return " ";
    if (ext == ".sql")       return " ";
    if (ext == ".sqlite")    return " ";
    if (ext == ".sqlite3")   return " ";
    if (ext == ".srt")       return "󰨖 ";
    if (ext == ".ssa")       return "󰨖 ";
    if (ext == ".stl")       return "󰆧 ";
    if (ext == ".styl")      return " ";
    if (ext == ".stylus")    return " ";
    if (ext == ".sub")       return "󰨖 ";
    if (ext == ".sublime")   return " ";
    if (ext == ".sv")        return "󰍛 ";
    if (ext == ".svelte")    return " ";
    if (ext == ".svg")       return "󰜡 ";
    if (ext == ".svh")       return "󰍛 ";
    if (ext == ".svx")       return " ";
    if (ext == ".swift")     return " ";
    if (ext == ".sym")       return " ";
    if (ext == ".t")         return " ";
    if (ext == ".tar")       return " ";
    if (ext == ".tcl")       return "󰛓 ";
    if (ext == ".templ")     return " ";
    if (ext == ".terminal")  return " ";
    if (ext == ".tex")       return " ";
    if (ext == ".tf")        return " ";
    if (ext == ".tfvars")    return " ";
    if (ext == ".tgz")       return " ";
    if (ext == ".tmpl")      return " ";
    if (ext == ".tmux")      return " ";
    if (ext == ".toml")      return " ";
    if (ext == ".torrent")   return " ";
    if (ext == ".ts")        return " ";
    if (ext == ".tsx")       return " ";
    if (ext == ".ttf")       return " ";
    if (ext == ".twig")      return " ";
    if (ext == ".txt")       return " ";
    if (ext == ".v")         return "󰍛 ";
    if (ext == ".vh")        return "󰍛 ";
    if (ext == ".vhd")       return "󰍛 ";
    if (ext == ".vhdl")      return "󰍛 ";
    if (ext == ".vim")       return " ";
    if (ext == ".vtt")       return "󰨖 ";
    if (ext == ".vue")       return "󰡄 ";
    if (ext == ".wav")       return " ";
    if (ext == ".wasm")      return " ";
    if (ext == ".webm")      return " ";
    if (ext == ".webp")      return " ";
    if (ext == ".whl")       return " ";
    if (ext == ".woff")      return " ";
    if (ext == ".woff2")     return " ";
    if (ext == ".xcf")       return " ";
    if (ext == ".xls")       return " ";
    if (ext == ".xlsx")      return " ";
    if (ext == ".xml")       return " ";
    if (ext == ".xul")       return " ";
    if (ext == ".xz")        return " ";
    if (ext == ".yaml")      return " ";
    if (ext == ".yarn")      return " ";
    if (ext == ".yml")       return " ";
    if (ext == ".zig")       return " ";
    if (ext == ".zip")       return " ";
    if (ext == ".zsh")       return " ";
    if (ext == ".zsh_theme") return " ";
    if (ext == ".zst")       return " ";
    return "";
}

static std::string mode_string(mode_t mode) {
    char buf[11];
    buf[0] = S_ISDIR(mode) ? 'd' : S_ISLNK(mode) ? 'l' : S_ISCHR(mode) ? 'c'
               : S_ISBLK(mode) ? 'b' : S_ISFIFO(mode) ? 'p' : S_ISSOCK(mode) ? 's'
               : S_ISREG(mode) ? '-' : '?';
    buf[1] = (mode & S_IRUSR) ? 'r' : '-';
    buf[2] = (mode & S_IWUSR) ? 'w' : '-';
    buf[3] = (mode & S_IXUSR) ? ((mode & S_ISUID) ? 's' : 'x') : ((mode & S_ISUID) ? 'S' : '-');
    buf[4] = (mode & S_IRGRP) ? 'r' : '-';
    buf[5] = (mode & S_IWGRP) ? 'w' : '-';
    buf[6] = (mode & S_IXGRP) ? ((mode & S_ISGID) ? 's' : 'x') : ((mode & S_ISGID) ? 'S' : '-');
    buf[7] = (mode & S_IROTH) ? 'r' : '-';
    buf[8] = (mode & S_IWOTH) ? 'w' : '-';
    buf[9] = (mode & S_IXOTH) ? ((mode & S_ISVTX) ? 't' : 'x') : ((mode & S_ISVTX) ? 'T' : '-');
    buf[10] = '\0';
    return buf;
}

static std::string octal_string(mode_t mode) {
    char buf[5];
    snprintf(buf, sizeof(buf), "%04o", mode & 07777);
    return buf;
}

static std::string user_name(uid_t uid) {
    if (numeric_ids) return std::to_string(uid);
    struct passwd *pw = getpwuid(uid);
    return pw ? pw->pw_name : std::to_string(uid);
}

static std::string group_name(gid_t gid) {
    if (numeric_ids) return std::to_string(gid);
    struct group *gr = getgrgid(gid);
    return gr ? gr->gr_name : std::to_string(gid);
}

static std::string time_string(time_t t, TimeType tt) {
    (void)tt;
    struct tm *tm = localtime(&t);
    if (!tm) return "??????????";
    char buf[64];
    time_t now = time(nullptr);
    struct tm *tm_now = localtime(&now);
    bool recent = tm_now && (tm->tm_year == tm_now->tm_year);
    if (recent)
        strftime(buf, sizeof(buf), "%b %e %H:%M", tm);
    else
        strftime(buf, sizeof(buf), "%b %e  %Y", tm);
    return buf;
}

static std::string human_size(off_t size) {
    if (!human_readable) return std::to_string(size);
    static const char *units[] = {"", "K", "M", "G", "T", "P"};
    double s = size;
    int i = 0;
    while (std::abs(s) >= 1024 && i < 5) { s /= 1024; i++; }
    char buf[32];
    if (i == 0)
        snprintf(buf, sizeof(buf), "%lld", (long long)size);
    else if (s < 10)
        snprintf(buf, sizeof(buf), "%.1f%s", s, units[i]);
    else
        snprintf(buf, sizeof(buf), "%.0f%s", s, units[i]);
    return buf;
}

static std::string block_string(blkcnt_t blocks) {
    auto s = human_size(blocks * 512);
    return s;
}

static int display_width(const std::string &s) {
    size_t wlen = mbstowcs(nullptr, s.c_str(), 0);
    if (wlen == (size_t)-1) return (int)s.size();
    std::wstring ws(wlen, L'\0');
    mbstowcs(&ws[0], s.c_str(), wlen + 1);
    int w = wcswidth(ws.c_str(), wlen);
    return w > 0 ? w : (int)s.size();
}

static void init_widths() {
    inode_width = 0;
    blocks_width = 0;
    nlink_width = 0;
    owner_width = 0;
    group_width = 0;
    size_width = 0;
    for (auto &f : files) {
        if (!f.stat_ok) continue;
        auto ino_str = std::to_string(f.st.st_ino);
        inode_width = std::max(inode_width, ino_str.size());
        auto blk_str = block_string(f.st.st_blocks);
        blocks_width = std::max(blocks_width, blk_str.size());
        auto nlink_str = std::to_string(f.st.st_nlink);
        nlink_width = std::max(nlink_width, nlink_str.size());
        auto own_str = user_name(f.st.st_uid);
        owner_width = std::max(owner_width, own_str.size());
        auto grp_str = group_name(f.st.st_gid);
        if (!no_group)
            group_width = std::max(group_width, grp_str.size());
        auto sz_str = human_size(f.st.st_size);
        size_width = std::max(size_width, sz_str.size());
    }
}

static bool should_ignore(const std::string &name) {
    if (name == "." || name == "..") {
        return !show_all && !show_almost_all;
    }
    if (name[0] == '.') return !show_all;
    return false;
}

static std::string git_status_for(const std::string &name) {
    auto it = git_status_map.find(name);
    if (it == git_status_map.end()) return "";
    return it->second;
}

static std::string shell_quote(const std::string &s) {
    return "'" + s + "'";
}

static std::string get_repo_prefix(const std::string &dirpath) {
    std::string safe = dirpath.empty() ? "." : dirpath;
    FILE *fp = popen(("git -C " + shell_quote(safe) + " rev-parse --show-toplevel 2>/dev/null").c_str(), "r");
    if (!fp) return "";
    char root_buf[4096];
    if (!fgets(root_buf, sizeof(root_buf), fp)) { pclose(fp); return ""; }
    pclose(fp);

    size_t rlen = strlen(root_buf);
    while (rlen > 0 && (root_buf[rlen-1] == '\n' || root_buf[rlen-1] == '\r'))
        root_buf[--rlen] = '\0';
    if (rlen == 0) return "";

    std::string repo_root(root_buf);
    if (safe == ".") {
        char cwd[4096];
        if (!getcwd(cwd, sizeof(cwd))) return "";
        std::string cur(cwd);
        if (cur.size() > repo_root.size() && cur.compare(0, repo_root.size(), repo_root) == 0) {
            std::string p = cur.substr(repo_root.size() + 1);
            return p.empty() ? "" : p + "/";
        }
        return "";
    }

    char real_buf[4096];
    if (!realpath(safe.c_str(), real_buf)) return "";
    std::string real_dir(real_buf);
    if (real_dir.size() > repo_root.size() && real_dir.compare(0, repo_root.size(), repo_root) == 0) {
        std::string p = real_dir.substr(repo_root.size() + 1);
        return p.empty() ? "" : p + "/";
    }
    return "";
}

static void load_git_status(const std::string &dirpath) {
    git_status_map.clear();
    std::string safe = dirpath.empty() ? "." : dirpath;

    FILE *fp = popen(("git -C " + shell_quote(safe) + " rev-parse --show-toplevel 2>/dev/null").c_str(), "r");
    if (!fp) return;
    char root_buf[4096];
    if (!fgets(root_buf, sizeof(root_buf), fp)) { pclose(fp); return; }
    pclose(fp);

    size_t rlen = strlen(root_buf);
    while (rlen > 0 && (root_buf[rlen-1] == '\n' || root_buf[rlen-1] == '\r'))
        root_buf[--rlen] = '\0';
    if (rlen == 0) return;

    std::string repo_root(root_buf);
    std::string prefix = get_repo_prefix(dirpath);

    fp = popen(("git -C " + shell_quote(repo_root) + " status --porcelain 2>/dev/null").c_str(), "r");
    if (!fp) return;

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len < 4) continue;
        std::string status(line, 2);
        std::string file(line + 3);
        while (!file.empty() && (file.back() == '\n' || file.back() == '\r'))
            file.pop_back();
        auto arrow = file.rfind(" -> ");
        if (arrow != std::string::npos)
            file = file.substr(arrow + 4);
        if (!prefix.empty() && file.compare(0, prefix.size(), prefix) == 0)
            file = file.substr(prefix.size());
        git_status_map[file] = status;
    }
    pclose(fp);
}

static void add_entries(const std::string &dirpath) {
    DIR *dir = opendir(dirpath.empty() ? "." : dirpath.c_str());
    if (!dir) {
        std::cerr << "dim-ls: cannot open directory '" << dirpath << "': "
                  << strerror(errno) << std::endl;
        exit_status = 1;
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        std::string name = entry->d_name;
        if (should_ignore(name)) continue;

        std::string full = dirpath.empty() ? name : dirpath + "/" + name;
        FileEntry fe;
        fe.name = name;

        struct stat st;
        int stat_ret = follow_symlinks ? stat(full.c_str(), &st) : lstat(full.c_str(), &st);
        if (stat_ret == 0) {
            fe.st = st;
            fe.stat_ok = true;
            if (!follow_symlinks && S_ISLNK(st.st_mode)) {
                char linkbuf[4096];
                ssize_t len = readlink(full.c_str(), linkbuf, sizeof(linkbuf) - 1);
                if (len != -1) {
                    linkbuf[len] = '\0';
                    fe.link_target = linkbuf;
                }
            }
        }
        files.push_back(std::move(fe));
    }
    closedir(dir);
}

static bool name_cmp(const FileEntry &a, const FileEntry &b) {
    return a.name < b.name;
}

static bool time_cmp(const FileEntry &a, const FileEntry &b) {
    struct timespec ta{0, 0}, tb{0, 0};
    if (a.stat_ok) {
        switch (time_type) {
            case TimeType::mtime: ta = a.st.st_mtim; break;
            case TimeType::ctime: ta = a.st.st_ctim; break;
            case TimeType::atime: ta = a.st.st_atim; break;
        }
    }
    if (b.stat_ok) {
        switch (time_type) {
            case TimeType::mtime: tb = b.st.st_mtim; break;
            case TimeType::ctime: tb = b.st.st_ctim; break;
            case TimeType::atime: tb = b.st.st_atim; break;
        }
    }
    if (ta.tv_sec != tb.tv_sec) return ta.tv_sec > tb.tv_sec;
    if (ta.tv_nsec != tb.tv_nsec) return ta.tv_nsec > tb.tv_nsec;
    return a.name < b.name;
}

static bool size_cmp(const FileEntry &a, const FileEntry &b) {
    off_t sa = a.stat_ok ? a.st.st_size : 0;
    off_t sb = b.stat_ok ? b.st.st_size : 0;
    if (sa != sb) return sa > sb;
    return a.name < b.name;
}

static bool ext_cmp(const FileEntry &a, const FileEntry &b) {
    auto ext = [](const std::string &s) -> std::string_view {
        auto dot = s.rfind('.');
        return dot == std::string::npos ? "" : std::string_view(s).substr(dot);
    };
    auto ea = ext(a.name), eb = ext(b.name);
    if (ea != eb) return ea < eb;
    return a.name < b.name;
}

static void sort_files() {
    if (files.empty()) return;
    switch (sort_by) {
        case SortBy::name: std::sort(files.begin(), files.end(), name_cmp); break;
        case SortBy::time: std::sort(files.begin(), files.end(), time_cmp); break;
        case SortBy::size: std::sort(files.begin(), files.end(), size_cmp); break;
        case SortBy::extension: std::sort(files.begin(), files.end(), ext_cmp); break;
        case SortBy::none: break;
    }
    if (sort_reverse && sort_by != SortBy::none)
        std::reverse(files.begin(), files.end());
    if (group_dirs_first) {
        std::stable_partition(files.begin(), files.end(), [](const FileEntry &f) {
            return f.stat_ok && S_ISDIR(f.st.st_mode);
        });
    }
}

static void print_entry_long(const FileEntry &f) {
    if (show_git_status) {
        auto gs = git_status_for(f.name);
        auto gcol = color_for_git_status(gs);
        if (gcol) printf("%s", gcol);
        if (gs.empty()) printf("  ");
        else printf("%s", gs.c_str());
        if (gcol) printf("%s", color_reset);
        printf(" ");
    }
    if (octal_permissions) {
        printf("%s ", f.stat_ok ? octal_string(f.st.st_mode).c_str() : "????");
    }
    if (print_inode) {
        printf("%*s ", (int)inode_width, f.stat_ok ? std::to_string(f.st.st_ino).c_str() : "?");
    }
    if (print_block_size) {
        printf("%*s ", (int)blocks_width,
               f.stat_ok ? block_string(f.st.st_blocks).c_str() : "?");
    }
    if (f.stat_ok) {
        printf("%s ", mode_string(f.st.st_mode).c_str());
        printf("%*ld ", (int)nlink_width, (long)f.st.st_nlink);
        if (!no_owner)
            printf("%-*s ", (int)owner_width, user_name(f.st.st_uid).c_str());
        if (!no_group)
            printf("%-*s ", (int)group_width, group_name(f.st.st_gid).c_str());
        printf("%*s ", (int)size_width, human_size(f.st.st_size).c_str());
        printf("%s ", time_string(f.st.st_mtime, TimeType::mtime).c_str());
    } else {
        printf("?---------- ? ? %-*s ? %-*s ? %*s ? %s ",
               (int)owner_width, "?", (int)group_width, "?",
               (int)size_width, "?", "??????????");
    }
    auto col = color_for(f.st.st_mode);
    if (!col.empty()) printf("%s", col.c_str());
    auto icon = icon_for(f.name, f.stat_ok ? f.st.st_mode : 0);
    if (*icon) printf("%s", icon);
    printf("%s", f.name.c_str());
    if (!col.empty()) printf("%s", color_reset);
    printf("%s", indicator_str(f.stat_ok, f.st.st_mode).c_str());
    if (f.stat_ok && S_ISLNK(f.st.st_mode) && !f.link_target.empty())
        printf(" -> %s", f.link_target.c_str());
    printf("\n");
}

static void print_entry_short(const FileEntry &f, size_t col_width) {
    if (show_git_status) {
        auto gs = git_status_for(f.name);
        auto gcol = color_for_git_status(gs);
        if (gcol) printf("%s", gcol);
        if (gs.empty()) printf("  ");
        else printf("%s", gs.c_str());
        if (gcol) printf("%s", color_reset);
        printf(" ");
    }
    auto col = color_for(f.st.st_mode);
    if (!col.empty()) printf("%s", col.c_str());

    if (print_inode)
        printf("%*s ", (int)inode_width,
               f.stat_ok ? std::to_string(f.st.st_ino).c_str() : "?");
    if (print_block_size)
        printf("%*s ", (int)blocks_width,
               f.stat_ok ? block_string(f.st.st_blocks).c_str() : "?");

    auto icon = icon_for(f.name, f.stat_ok ? f.st.st_mode : 0);
    if (*icon) printf("%s", icon);
    printf("%s", f.name.c_str());
    if (!col.empty()) printf("%s", color_reset);
    auto ind = indicator_str(f.stat_ok, f.st.st_mode);
    printf("%s", ind.c_str());

    size_t printed = (size_t)display_width(f.name) + ind.size() + (*icon ? display_width(icon) : 0);
    if (show_git_status) printed += 3;
    if (col_width > printed)
        for (size_t i = printed; i < col_width; i++)
            putchar(' ');
}

static void print_files() {
    if (files.empty()) return;

    if (format == Format::long_format) {
        blkcnt_t total = 0;
        for (auto &f : files)
            if (f.stat_ok) total += f.st.st_blocks;
        printf("total %s\n", block_string(total).c_str());
        for (auto &f : files)
            print_entry_long(f);
        return;
    }

    size_t max_name = 0;
    for (auto &f : files) {
        auto len = (size_t)display_width(f.name) + (indicator_style != IndicatorStyle::none ? 1 : 0);
        auto icon = icon_for(f.name, f.stat_ok ? f.st.st_mode : 0);
        if (*icon) len += display_width(icon);
        if (print_inode && f.stat_ok)
            len += inode_width + 1;
        if (print_block_size && f.stat_ok)
            len += blocks_width + 1;
        if (show_git_status) len += 3;
        max_name = std::max(max_name, len);
    }

    if (format == Format::one_per_line) {
        for (auto &f : files) {
            print_entry_short(f, 0);
            putchar('\n');
        }
        return;
    }

    auto entry_width = [](const FileEntry &f) -> size_t {
        auto len = (size_t)display_width(f.name) + (indicator_style != IndicatorStyle::none ? 1 : 0);
        auto icon = icon_for(f.name, f.stat_ok ? f.st.st_mode : 0);
        if (*icon) len += display_width(icon);
        if (print_inode && f.stat_ok)
            len += inode_width + 1;
        if (print_block_size && f.stat_ok)
            len += blocks_width + 1;
        if (show_git_status) len += 3;
        return len;
    };

    size_t ncols = 1;
    std::vector<size_t> col_widths;
    size_t max_possible = std::min(files.size(), std::max(line_length / 2, (size_t)1));

    for (size_t try_cols = max_possible; try_cols >= 1; try_cols--) {
        size_t nrows = (files.size() + try_cols - 1) / try_cols;
        if (nrows == 0) nrows = 1;
        std::vector<size_t> maxw(try_cols, 0);
        for (size_t c = 0; c < try_cols; c++) {
            for (size_t r = 0; r < nrows; r++) {
                size_t idx = c * nrows + r;
                if (idx >= files.size()) break;
                maxw[c] = std::max(maxw[c], entry_width(files[idx]));
            }
        }
        size_t total = maxw[0];
        for (size_t c = 1; c < try_cols; c++)
            total += 2 + maxw[c];
        if (total <= line_length) {
            ncols = try_cols;
            col_widths.resize(try_cols);
            for (size_t c = 0; c < try_cols; c++)
                col_widths[c] = maxw[c] + 2;
            break;
        }
    }

    size_t nrows = (files.size() + ncols - 1) / ncols;

    if (format == Format::many_per_line) {
        for (size_t r = 0; r < nrows; r++) {
            for (size_t c = 0; c < ncols; c++) {
                size_t idx = c * nrows + r;
                if (idx >= files.size()) continue;
                print_entry_short(files[idx], col_widths[c]);
            }
            putchar('\n');
        }
    } else if (format == Format::horizontal) {
        size_t h_width = max_name + 2;
        for (size_t i = 0; i < files.size(); i++) {
            if (i > 0) {
                putchar(' ');
                putchar(' ');
            }
            print_entry_short(files[i], h_width);
        }
        putchar('\n');
    } else if (format == Format::with_commas) {
        for (size_t i = 0; i < files.size(); i++) {
            if (i > 0) {
                size_t guess = 2;
                if (guess + entry_width(files[i]) + 2 > line_length)
                    printf(",\n");
                else
                    printf(", ");
            }
            print_entry_short(files[i], 0);
        }
        putchar('\n');
    }
}

static void list_tree(const std::string &dirpath, const std::string &prefix, bool root, int depth = 0) {
    if (depth > 20) return;

    DIR *dir = opendir(dirpath.empty() ? "." : dirpath.c_str());
    if (!dir) {
        std::cerr << "dim-ls: cannot open directory '" << dirpath << "': "
                  << strerror(errno) << std::endl;
        exit_status = 1;
        return;
    }

    std::vector<FileEntry> local_files;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        std::string name = entry->d_name;
        if (should_ignore(name)) continue;
        std::string full = dirpath.empty() ? name : dirpath + "/" + name;
        FileEntry fe;
        fe.name = name;
        struct stat st;
        int stat_ret = follow_symlinks ? stat(full.c_str(), &st) : lstat(full.c_str(), &st);
        if (stat_ret == 0) {
            fe.st = st;
            fe.stat_ok = true;
            if (!follow_symlinks && S_ISLNK(st.st_mode)) {
                char linkbuf[4096];
                ssize_t len = readlink(full.c_str(), linkbuf, sizeof(linkbuf) - 1);
                if (len != -1) {
                    linkbuf[len] = '\0';
                    fe.link_target = linkbuf;
                }
            }
        }
        local_files.push_back(std::move(fe));
    }
    closedir(dir);

    if (local_files.empty()) return;

    switch (sort_by) {
        case SortBy::name: std::sort(local_files.begin(), local_files.end(), name_cmp); break;
        case SortBy::time: std::sort(local_files.begin(), local_files.end(), time_cmp); break;
        case SortBy::size: std::sort(local_files.begin(), local_files.end(), size_cmp); break;
        case SortBy::extension: std::sort(local_files.begin(), local_files.end(), ext_cmp); break;
        case SortBy::none: break;
    }
    if (sort_reverse)
        std::reverse(local_files.begin(), local_files.end());
    if (group_dirs_first) {
        std::stable_partition(local_files.begin(), local_files.end(), [](const FileEntry &f) {
            return f.stat_ok && S_ISDIR(f.st.st_mode);
        });
    }

    for (size_t i = 0; i < local_files.size(); i++) {
        bool last = (i == local_files.size() - 1);
        FileEntry &f = local_files[i];

        if (!root) {
            printf("%s", prefix.c_str());
            printf(last ? "└── " : "├── ");
        }

        auto col = color_for(f.st.st_mode);
        if (!col.empty()) printf("%s", col.c_str());
        auto icon = icon_for(f.name, f.stat_ok ? f.st.st_mode : 0);
        if (*icon) printf("%s", icon);
        printf("%s", f.name.c_str());
        if (!col.empty()) printf("%s", color_reset);
        printf("%s", indicator_str(f.stat_ok, f.st.st_mode).c_str());

        if (f.stat_ok && S_ISLNK(f.st.st_mode) && !f.link_target.empty())
            printf(" -> %s", f.link_target.c_str());

        printf("\n");

        if (f.stat_ok && S_ISDIR(f.st.st_mode)) {
            std::string new_prefix = prefix + (last ? "    " : "│   ");
            std::string subdir = dirpath.empty() ? f.name : dirpath + "/" + f.name;
            list_tree(subdir, new_prefix, false, depth + 1);
        }
    }
}

static void list_dir(const std::string &path, bool top_level) {
    if (tree_view) {
        std::string display = path.empty() ? "." : path;
        // Strip trailing slash
        if (display.size() > 1 && display.back() == '/') display.pop_back();
        auto col = color_output ? color_dir : "";
        if (*col) printf("%s", col);
        printf("%s", display.c_str());
        if (*col) printf("%s", color_reset);
        printf("\n");
        list_tree(path, "", false);
        return;
    }
    files.clear();
    add_entries(path);
    sort_files();
    init_widths();

    if (!top_level || recursive) {
        printf("%s:\n", path.empty() ? "." : path.c_str());
    }
    print_files();
}

struct option long_options[] = {
    {"all", no_argument, nullptr, 'a'},
    {"almost-all", no_argument, nullptr, 'A'},
    {"author", no_argument, nullptr, 256},
    {"block-size", required_argument, nullptr, 257},
    {"classify", no_argument, nullptr, 'F'},
    {"color", optional_argument, nullptr, 258},
    {"dereference", no_argument, nullptr, 'L'},
    {"directory", no_argument, nullptr, 'd'},
    {"dired", no_argument, nullptr, 'D'},
    {"escape", no_argument, nullptr, 'b'},
    {"file-type", no_argument, nullptr, 259},
    {"format", required_argument, nullptr, 260},
    {"full-time", no_argument, nullptr, 261},
    {"git", no_argument, nullptr, 273},
    {"group-directories-first", no_argument, nullptr, 262},
    {"help", no_argument, nullptr, 263},
    {"hide", required_argument, nullptr, 264},
    {"hide-control-chars", no_argument, nullptr, 'q'},
    {"human-readable", no_argument, nullptr, 'h'},
    {"ignore", required_argument, nullptr, 'I'},
    {"ignore-backups", no_argument, nullptr, 'B'},
    {"indicator-style", required_argument, nullptr, 265},
    {"inode", no_argument, nullptr, 'i'},
    {"literal", no_argument, nullptr, 'N'},
    {"no-group", no_argument, nullptr, 'G'},
    {"numeric-uid-gid", no_argument, nullptr, 'n'},
    {"octal-permissions", no_argument, nullptr, 'Z'},
    {"quote-name", no_argument, nullptr, 'Q'},
    {"quoting-style", required_argument, nullptr, 266},
    {"recursive", no_argument, nullptr, 'R'},
    {"reverse", no_argument, nullptr, 'r'},
    {"show-control-chars", no_argument, nullptr, 267},
    {"si", no_argument, nullptr, 268},
    {"size", no_argument, nullptr, 's'},
    {"sort", required_argument, nullptr, 269},
    {"tabsize", required_argument, nullptr, 'T'},
    {"time", required_argument, nullptr, 270},
    {"time-style", required_argument, nullptr, 271},
    {"tree", no_argument, nullptr, 274},
    {"version", no_argument, nullptr, 272},
    {"width", required_argument, nullptr, 'w'},
    {nullptr, 0, nullptr, 0}
};

static void print_usage() {
    std::cout << "Usage: dim-ls [OPTION]... [FILE]...\n"
              << "List information about the FILEs (the current directory by default).\n\n"
              << "  -a, --all                  do not ignore entries starting with .\n"
              << "  -A, --almost-all           do not list implied . and ..\n"
              << "  -B, --ignore-backups       do not list implied entries ending with ~\n"
              << "  -c                         with -lt: sort by, and show, ctime\n"
              << "                             with -l: show ctime and sort by name\n"
              << "                             otherwise: sort by ctime\n"
              << "  -C                         list entries by columns\n"
              << "      --color[=WHEN]         colorize the output; WHEN is 'always' (default),\n"
              << "                             'auto', or 'never'\n"
              << "  -d, --directory            list directory entries instead of contents\n"
              << "  -D, --dired                generate output designed for Emacs' dired mode\n"
              << "  -e, --tree                 list directories in a tree-like format\n"
              << "  -F, --classify             append indicator (one of */=>@|) to entries\n"
              << "      --file-type            likewise, except do not append '*'\n"
              << "      --git                  show git status for each file\n"
              << "  -G, --no-group             in a long listing, don't print group names\n"
              << "      --group-directories-first\n"
              << "                             group directories before files\n"
              << "  -h, --human-readable       with -l, print sizes in human readable format\n"
              << "  -H, --dereference-command-line\n"
              << "                             follow symbolic links listed on the command line\n"
              << "  -i, --inode                print the index number of each file\n"
              << "  -l                         use a long listing format\n"
              << "  -L, --dereference          follow symbolic links\n"
              << "  -m                         fill width with a comma separated list of entries\n"
              << "  -n, --numeric-uid-gid      like -l, but list numeric user and group IDs\n"
              << "  -N, --literal              print raw entry names\n"
              << "  -o                         like -l, but do not list group information\n"
              << "  -p, --indicator-style=slash\n"
              << "                             append / indicator to directories\n"
              << "  -q, --hide-control-chars   print ? instead of non graphic characters\n"
              << "  -Q, --quote-name           enclose entry names in double quotes\n"
              << "  -r, --reverse              reverse order while sorting\n"
              << "  -R, --recursive            list subdirectories recursively\n"
              << "  -s, --size                 print the allocated size of each file, in blocks\n"
              << "  -S                         sort by file size\n"
              << "      --sort=WORD            sort by WORD instead of name: none, extension,\n"
              << "                             size, time, version\n"
              << "      --time=WORD            with -l, show time as WORD instead of modification\n"
              << "                             time: atime, access, use, ctime, status\n"
              << "  -t                         sort by modification time\n"
              << "  -T, --tabsize=COLS         assume tab stops at each COLS instead of 8\n"
              << "  -u                         with -lt: sort by, and show, access time\n"
              << "                             with -l: show access time and sort by name\n"
              << "                             otherwise: sort by access time\n"
              << "  -U                         do not sort; list entries in directory order\n"
              << "  -v, --version              output version information and exit\n"
              << "  -w, --width=COLS           assume screen width instead of current value\n"
              << "  -x                         list entries by lines instead of by columns\n"
              << "  -X                         sort alphabetically by entry extension\n"
              << "  -Z, --octal-permissions    show file permissions in octal format\n"
              << "  -1                         list one file per line\n"
              << "      --help                 display this help and exit\n";
}

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");
    is_tty = isatty(STDOUT_FILENO);
    if (is_tty) {
        format = Format::many_per_line;
    } else {
        format = Format::one_per_line;
    }

    if (auto *p = getenv("COLUMNS"); p && *p)
        line_length = std::stoul(p);
#ifdef TIOCGWINSZ
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1 && ws.ws_col > 0)
        line_length = ws.ws_col;
#endif
    if (auto *p = getenv("TABSIZE"); p)
        tabsize = std::stoul(p);

    int opt;
    while ((opt = getopt_long(argc, argv, "1AaBbCcdDeFfGgHhiI:kLlmnNopqQrRSstT:uUvxXw:Z",
                              long_options, nullptr)) != -1)
    {
        switch (opt) {
            case '1': if (format != Format::long_format) format = Format::one_per_line; break;
            case 'a': show_all = true; break;
            case 'A': show_almost_all = true; break;
            case 'B': break;
            case 'b': break;
            case 'C': format = Format::many_per_line; break;
            case 'c': time_type = TimeType::ctime; break;
            case 'd': break;
            case 'D': break;
            case 'e': tree_view = true; break;
            case 'F': indicator_style = IndicatorStyle::classify; break;
            case 'f': show_all = true; sort_by = SortBy::none; break;
            case 'G': no_group = true; break;
            case 'g': long_format = true; no_owner = true; format = Format::long_format; break;
            case 'H': break;
            case 'h': human_readable = true; break;
            case 'I': break;
            case 'i': print_inode = true; break;
            case 'k': break;
            case 'L': follow_symlinks = true; break;
            case 'l': format = Format::long_format; long_format = true; break;
            case 'm': format = Format::with_commas; break;
            case 'N': break;
            case 'n': numeric_ids = true; format = Format::long_format; long_format = true; break;
            case 'o': no_group = true; format = Format::long_format; long_format = true; break;
            case 'p': indicator_style = IndicatorStyle::slash; break;
            case 'q': break;
            case 'Q': break;
            case 'r': sort_reverse = true; break;
            case 'R': recursive = true; break;
            case 'S': sort_by = SortBy::size; break;
            case 's': print_block_size = true; break;
            case 'T':
                tabsize = std::stoul(optarg);
                break;
            case 't': sort_by = SortBy::time; break;
            case 'U': sort_by = SortBy::none; break;
            case 'u': time_type = TimeType::atime; break;
            case 'v':
                std::cout << "dim-ls 0.1.3\n";
                return 0;
            case 'w':
                line_length = std::stoul(optarg);
                break;
            case 'x': format = Format::horizontal; break;
            case 'X': sort_by = SortBy::extension; break;
            case 'Z': octal_permissions = true; break;
            case 258:
                color_output = true;
                break;
            case 259: indicator_style = IndicatorStyle::file_type; break;
            case 260:
                if (strcmp(optarg, "long") == 0 || strcmp(optarg, "verbose") == 0)
                    format = Format::long_format;
                else if (strcmp(optarg, "single-column") == 0)
                    format = Format::one_per_line;
                else if (strcmp(optarg, "vertical") == 0 || strcmp(optarg, "C") == 0)
                    format = Format::many_per_line;
                else if (strcmp(optarg, "horizontal") == 0 || strcmp(optarg, "across") == 0)
                    format = Format::horizontal;
                else if (strcmp(optarg, "commas") == 0)
                    format = Format::with_commas;
                break;
            case 262: group_dirs_first = true; break;
            case 263: print_usage(); return 0;
            case 265:
                if (strcmp(optarg, "none") == 0) indicator_style = IndicatorStyle::none;
                else if (strcmp(optarg, "slash") == 0) indicator_style = IndicatorStyle::slash;
                else if (strcmp(optarg, "file-type") == 0) indicator_style = IndicatorStyle::file_type;
                else if (strcmp(optarg, "classify") == 0) indicator_style = IndicatorStyle::classify;
                break;
            case 269:
                if (strcmp(optarg, "none") == 0) sort_by = SortBy::none;
                else if (strcmp(optarg, "name") == 0) sort_by = SortBy::name;
                else if (strcmp(optarg, "size") == 0) sort_by = SortBy::size;
                else if (strcmp(optarg, "time") == 0) sort_by = SortBy::time;
                else if (strcmp(optarg, "extension") == 0) sort_by = SortBy::extension;
                else if (strcmp(optarg, "version") == 0) sort_by = SortBy::name;
                break;
            case 270:
                if (strcmp(optarg, "atime") == 0 || strcmp(optarg, "access") == 0 || strcmp(optarg, "use") == 0)
                    time_type = TimeType::atime;
                else if (strcmp(optarg, "ctime") == 0 || strcmp(optarg, "status") == 0)
                    time_type = TimeType::ctime;
                break;
            case 273: show_git_status = true; break;
            case 274: tree_view = true; break;
            case 272:
                std::cout << "dim-ls 0.1.3\n";
                return 0;
            default:
                std::cerr << "Try 'dim-ls --help' for more information.\n";
                return 2;
        }
    }

    if (format == Format::long_format || print_block_size) {
    }

    std::vector<std::string> paths;
    if (optind >= argc) {
        paths.push_back(".");
    } else {
        for (int i = optind; i < argc; i++)
            paths.push_back(argv[i]);
    }

    bool first = true;
    for (auto &p : paths) {
        struct stat st;
        int stat_ret = follow_symlinks ? stat(p.c_str(), &st) : lstat(p.c_str(), &st);
        if (stat_ret == 0 && S_ISDIR(st.st_mode)) {
            if (!first && !tree_view) printf("\n");
            first = false;
            if (show_git_status)
                load_git_status(p);
            list_dir(p, paths.size() <= 1);
        } else {
            files.clear();
            FileEntry fe;
            fe.name = p;
            if (stat(p.c_str(), &st) == 0) {
                fe.st = st;
                fe.stat_ok = true;
            }
            files.push_back(std::move(fe));
            sort_files();
            init_widths();
            if (format == Format::long_format)
                print_entry_long(files[0]);
            else
                print_entry_short(files[0], 0), printf("\n");
        }
    }

    return exit_status;
}
