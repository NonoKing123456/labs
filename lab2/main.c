#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>    
#include <limits.h>    
#include <inttypes.h>  

static int err_code;

/*
 * here are some function signatures and macros that may be helpful.
 */

void handle_error(char* fullname, char* action);
bool test_file(char* pathandname);
bool is_dir(char* pathandname);
const char* ftype_to_str(mode_t mode);
void list_file(char* pathandname, char* name, bool list_long);
void list_dir(char* dirname, bool list_long, bool list_all, bool recursive);

/*
 * You can use the NOT_YET_IMPLEMENTED macro to error out when you reach parts
 * of the code you have not yet finished implementing.
 */
#define NOT_YET_IMPLEMENTED(msg)                  \
    do {                                          \
        printf("Not yet implemented: " msg "\n"); \
        exit(255);                                \
    } while (0)

/*
 * PRINT_ERROR: This can be used to print the cause of an error returned by a
 * system call. It can help with debugging and reporting error causes to
 * the user. Example usage:
 *     if ( error_condition ) {
 *        PRINT_ERROR();
 *     }
 */
#define PRINT_ERROR(progname, what_happened, pathandname)               \
    do {                                                                \
        printf("%s: %s %s: %s\n", progname, what_happened, pathandname, \
               strerror(errno));                                        \
    } while (0)

/* PRINT_PERM_CHAR:
 *
 * This will be useful for -l permission printing.  It prints the given
 * 'ch' if the permission exists, or "-" otherwise.
 * Example usage:
 *     PRINT_PERM_CHAR(sb.st_mode, S_IRUSR, "r");
 */
#define PRINT_PERM_CHAR(mode, mask, ch) printf("%s", (mode & mask) ? ch : "-");

/*
 * Get username for uid. Return 1 on failure, 0 otherwise.
 */
static int uname_for_uid(uid_t uid, char* buf, size_t buflen) {
    struct passwd* p = getpwuid(uid);
    if (p == NULL) {
        return 1;
    }
    strncpy(buf, p->pw_name, buflen);
    return 0;
}

/*
 * Get group name for gid. Return 1 on failure, 0 otherwise.
 */
static int group_for_gid(gid_t gid, char* buf, size_t buflen) {
    struct group* g = getgrgid(gid);
    if (g == NULL) {
        return 1;
    }
    strncpy(buf, g->gr_name, buflen);
    return 0;
}

/*
 * Format the supplied `struct timespec` in `ts` (e.g., from `stat.st_mtime`) as a
 * string in `char *out`. Returns the length of the formatted string (see, `man
 * 3 strftime`).
 */
static size_t date_string(struct timespec* ts, char* out, size_t len) {
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    struct tm* t = localtime(&ts->tv_sec);
    if (now.tv_sec < ts->tv_sec) {
        // Future time, treat with care.
        return strftime(out, len, "%b %e %Y", t);
    } else {
        time_t difference = now.tv_sec - ts->tv_sec;
        if (difference < 31556952ull) {
            return strftime(out, len, "%b %e %H:%M", t);
        } else {
            return strftime(out, len, "%b %e %Y", t);
        }
    }
}

/*
 * Print help message and exit.
 */
static void help() {
    printf("ls: List files\n");
    printf("\t--help        Print this help\n");
    printf("\t-a            Include entries starting with '.'; also print '.' and '..'\n");
    printf("\t-l            Long listing format (mode, links, owner, group, size, mtime)\n");
    printf("\t-n            Print only a count of entries (takes precedence over -l)\n");
    printf("\t-R            Recursively list subdirectories\n");
    exit(0);
}

/* --------- helpers for path/name handling ---------- */

static bool is_dot_or_dotdot(const char* name);
static void join_path(char* out, size_t outsz, const char* dir, const char* name);
static const char* last_component(const char* path);

static bool is_dot_or_dotdot(const char* name) {
    return (strcmp(name, ".") == 0) || (strcmp(name, "..") == 0);
}

static void join_path(char* out, size_t outsz, const char* dir, const char* name) {
    size_t len = strlen(dir);
    if (len > 0 && dir[len - 1] == '/') {
        snprintf(out, outsz, "%s%s", dir, name);
    } else {
        snprintf(out, outsz, "%s/%s", dir, name);
    }
}

static const char* last_component(const char* path) {
    const char* p = strrchr(path, '/');
    if (!p) return path;
    // skip trailing slashes
    const char* end = path + strlen(path);
    while (end > path && *(end - 1) == '/') end--;
    // if path ends with '/', find previous '/'
    if (end != path && *(end - 1) != '/') {
        p = strrchr(path, '/');
    }
    if (!p) return path;
    return (*(p + 1)) ? (p + 1) : p; // if trailing '/', return that slash (won't be used for files)
}

/*
 * call this when there's been an error.
 * The function should:
 * - print a suitable error message (this is already implemented)
 * - set appropriate bits in err_code
 */
void handle_error(char* what_happened, char* fullname) {
    PRINT_ERROR("ls", what_happened, fullname);

    // Set "some error" bit (bit 6)
    err_code |= 0x40;

    // Classify per spec:
    // bit 3 (0x08) -> not found
    // bit 4 (0x10) -> access denied
    // bit 5 (0x20) -> other error
    if (errno == ENOENT) {
        err_code |= 0x08;
    } else if (errno == EACCES || errno == EPERM) {
        err_code |= 0x10;
    } else {
        err_code |= 0x20;
    }
    return;
}

/*
 * test_file():
 * test whether stat() returns successfully and if not, handle error.
 * Use this to test for whether a file or dir exists
 */
bool test_file(char* pathandname) {
    struct stat sb;
    if (stat(pathandname, &sb)) {
        handle_error("cannot access", pathandname);
        return false;
    }
    return true;
}

/*
 * is_dir(): tests whether the argument refers to a directory.
 * precondition: test_file() returns true. that is, call this function
 * only if test_file(pathandname) returned true.
 */
bool is_dir(char* pathandname) {
    struct stat sb;
    if (stat(pathandname, &sb) != 0) {
        return false;
    }
    return S_ISDIR(sb.st_mode);
}

/* convert the mode field in a struct stat to a file type, for -l printing */
const char* ftype_to_str(mode_t mode) {
    if (S_ISDIR(mode)) return "d";
    if (S_ISREG(mode)) return "-";
    return "?";
}

int lab2_test = 0;

/* -------- global flags for -n counting ---------- */
static bool g_count_only = false;
static unsigned long long g_count = 0;

/* list_file():
 * implement the logic for listing a single file.
 * This function takes:
 *   - pathandname: the directory name plus the file name.
 *   - name: just the name "component".
 *   - list_long: a flag indicated whether the printout should be in
 *   long mode.
 */
void list_file(char* pathandname, char* name, bool list_long) {
    struct stat sb;
    if (stat(pathandname, &sb) != 0) {
        handle_error("cannot access", pathandname);
        return;
    }

    if (g_count_only) {
        // Count everything passed to us (caller enforces -a filtering for dir entries)
        g_count++;
        return;
    }

    bool is_directory = S_ISDIR(sb.st_mode);
    bool pseudo = is_dot_or_dotdot(name);

    if (list_long) {
        // type
        printf("%s", ftype_to_str(sb.st_mode));

        // user perms
        PRINT_PERM_CHAR(sb.st_mode, S_IRUSR, "r");
        PRINT_PERM_CHAR(sb.st_mode, S_IWUSR, "w");
        PRINT_PERM_CHAR(sb.st_mode, S_IXUSR, "x");
        // group perms
        PRINT_PERM_CHAR(sb.st_mode, S_IRGRP, "r");
        PRINT_PERM_CHAR(sb.st_mode, S_IWGRP, "w");
        PRINT_PERM_CHAR(sb.st_mode, S_IXGRP, "x");
        // other perms
        PRINT_PERM_CHAR(sb.st_mode, S_IROTH, "r");
        PRINT_PERM_CHAR(sb.st_mode, S_IWOTH, "w");
        PRINT_PERM_CHAR(sb.st_mode, S_IXOTH, "x");
        printf(" ");

        // links
        printf("%ju ", (uintmax_t)sb.st_nlink);

        // user
        char ubuf[64], gbuf[64];
        int uerr = uname_for_uid(sb.st_uid, ubuf, sizeof ubuf);
        int gerr = group_for_gid(sb.st_gid, gbuf, sizeof gbuf);
        if (uerr) {
            // per spec: print numeric uid, set error bits, no error message
            printf("%u ", (unsigned)sb.st_uid);
            err_code |= 0x40; // some error
            err_code |= 0x20; // other error
        } else {
            printf("%s ", ubuf);
        }
        if (gerr) {
            printf("%u ", (unsigned)sb.st_gid);
            err_code |= 0x40;
            err_code |= 0x20;
        } else {
            printf("%s ", gbuf);
        }

        // size
        printf("%jd ", (intmax_t)sb.st_size);

        // date
        char tbuf[64];
        struct timespec ts;
#ifdef st_mtim
        ts = sb.st_mtim;
#else
        ts.tv_sec = sb.st_mtime;
        ts.tv_nsec = 0;
#endif
        date_string(&ts, tbuf, sizeof tbuf);
        printf("%s ", tbuf);

        // name (append '/' for non-pseudo directories)
        if (is_directory && !pseudo)
            printf("%s/\n", name);
        else
            printf("%s\n", name);
    } else {
        if (is_directory && !pseudo)
            printf("%s/\n", name);
        else
            printf("%s\n", name);
    }
}

/* list_dir():
 * implement the logic for listing a directory.
 * This function takes:
 *    - dirname: the name of the directory
 *    - list_long: should the directory be listed in long mode?
 *    - list_all: are we in "-a" mode?
 *    - recursive: are we supposed to list sub-directories?
 */
void list_dir(char* dirname, bool list_long, bool list_all, bool recursive) {
    DIR* d = opendir(dirname);
    if (!d) {
        handle_error("cannot open directory", dirname);
        return;
    }

    struct dirent* de;

    // First pass: list entries
    errno = 0;
    while ((de = readdir(d)) != NULL) {
        const char* name = de->d_name;

        if (!list_all && name[0] == '.')
            continue;

        char path[PATH_MAX];
        join_path(path, sizeof path, dirname, name);

        // For -a: include '.' and '..' but do NOT add trailing slash on them.
        list_file(path, (char*)name, list_long);
    }
    if (errno != 0) {
        // readdir error during iteration
        handle_error("error reading directory", dirname);
    }

    if (!recursive) {
        closedir(d);
        return;
    }

    // Second pass: recurse into subdirectories (respect -a; skip . and ..)
    rewinddir(d);
    errno = 0;
    while ((de = readdir(d)) != NULL) {
        const char* name = de->d_name;

        if (is_dot_or_dotdot(name))
            continue;
        if (!list_all && name[0] == '.')
            continue;

        char path[PATH_MAX];
        join_path(path, sizeof path, dirname, name);

        if (is_dir(path)) {
            if (!g_count_only) {
                printf("\n%s:\n", path);
            }
            list_dir(path, list_long, list_all, true);
        }
    }
    if (errno != 0) {
        handle_error("error reading directory", dirname);
    }

    closedir(d);
}

int main(int argc, char* argv[]) {
    // This needs to be int since C does not specify whether char is signed or
    // unsigned.
    int opt;
    err_code = 0;
    bool list_long = false, list_all = false;
    bool recursive = false;
    g_count_only = false;
    g_count = 0;

    struct option opts[] = {
        {.name = "help", .has_arg = 0, .flag = NULL, .val = '\a'},
        {0, 0, 0, 0}};

    // Parse flags
    while ((opt = getopt_long(argc, argv, "1alnR", opts, NULL)) != -1) {
        switch (opt) {
            case '\a':
                help();
                break;
            case '1':
                // default behavior; ignore
                break;
            case 'a':
                list_all = true;
                break;
            case 'l':
                list_long = true;
                break;
            case 'n':
                g_count_only = true;
                break;
            case 'R':
                recursive = true;
                break;
            default:
                printf("Unimplemented flag %d\n", opt);
                break;
        }
    }

    // If no paths provided, use "."
    int n_args = argc - optind;
    if (n_args <= 0) {
        char* dot = ".";
        if (g_count_only) {
            // count entries in "."
            list_dir(dot, false, list_all, recursive);
            printf("%llu\n", g_count);
            exit(err_code);
        } else {
            list_dir(dot, list_long, list_all, recursive);
            exit(err_code);
        }
    }

    // Separate pass for files vs directories.
    // First: files
    bool printed_any_file = false;
    for (int i = optind; i < argc; i++) {
        char* path = argv[i];
        if (!test_file(path)) {
            continue; // error already handled, keep going
        }
        if (!is_dir(path)) {
            const char* name = last_component(path);
            list_file(path, (char*)name, list_long);
            printed_any_file = true;
        }
    }

    // If files printed and there are dirs too, add a blank line between sections
    bool have_dir = false;
    for (int i = optind; i < argc; i++) {
        if (test_file(argv[i]) && is_dir(argv[i])) {
            have_dir = true;
            break;
        }
    }
    if (!g_count_only && printed_any_file && have_dir) {
        printf("\n");
    }

    // Second: directories
    bool multiple_targets = (n_args > 1);
    bool first_dir_printed = false;
    for (int i = optind; i < argc; i++) {
        char* path = argv[i];
        if (!test_file(path)) {
            continue;
        }
        if (is_dir(path)) {
            if (!g_count_only && (multiple_targets || recursive)) {
                // header per dir when multiple args, or when recursing from top arguments
                if (first_dir_printed || printed_any_file) {
                    printf("\n");
                }
                printf("%s:\n", path);
                first_dir_printed = true;
            }
            list_dir(path, list_long, list_all, recursive);
        }
    }

    if (g_count_only) {
        printf("%llu\n", g_count);
    }

    exit(err_code);
}
