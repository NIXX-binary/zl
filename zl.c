#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <errno.h>

#define C_RESET   "\033[0m"
#define C_DIR     "\033[1;34m"
#define C_IMAGE   "\033[36m"
#define C_AUDIO   "\033[35m"
#define C_VIDEO   "\033[32m"
#define C_OTHER   "\033[38;5;208m"

static int show_hidden = 0;
static int raw_bytes = 0;   /* -b: show exact byte count instead of human-readable */

static const char *image_ext[] = {
    "jpg","jpeg","png","gif","bmp","svg","webp","tiff","tif","ico","heic","raw", NULL
};
static const char *audio_ext[] = {
    "mp3","wav","flac","ogg","m4a","aac","wma","opus","aiff", NULL
};
static const char *video_ext[] = {
    "mp4","mkv","avi","mov","wmv","flv","webm","m4v","mpg","mpeg", NULL
};

static int ext_in_list(const char *ext, const char **list) {
    for (int i = 0; list[i]; i++) {
        if (strcasecmp(ext, list[i]) == 0) return 1;
    }
    return 0;
}

static const char *color_for(const char *name, const struct stat *st) {
    if (S_ISDIR(st->st_mode)) return C_DIR;

    const char *dot = strrchr(name, '.');
    if (dot && dot != name && *(dot + 1) != '\0') {
        const char *ext = dot + 1;
        if (ext_in_list(ext, image_ext)) return C_IMAGE;
        if (ext_in_list(ext, audio_ext)) return C_AUDIO;
        if (ext_in_list(ext, video_ext)) return C_VIDEO;
    }
    return C_OTHER;
}

typedef struct {
    char *name;
    struct stat st;
} entry_t;

static int cmp_entries(const void *a, const void *b) {
    const entry_t *ea = a, *eb = b;
    return strcasecmp(ea->name, eb->name);
}

/* Formats a size into buf as plain text (no padding, no color).
 * raw_bytes (-b) -> exact byte count ("5242880b")
 * otherwise       -> human readable, lowercase units ("10gb", "2kb", "500b") */
static void format_size(off_t size, char *buf, size_t bufsz) {
    if (raw_bytes) {
        snprintf(buf, bufsz, "%lldb", (long long)size);
        return;
    }
    const char *units[] = {"b", "kb", "mb", "gb", "tb"};
    int i = 0;
    double d_size = (double)size;
    while (d_size >= 1024 && i < 4) {
        d_size /= 1024;
        i++;
    }
    if (i == 0) {
        snprintf(buf, bufsz, "%lldb", (long long)size);
    } else if (d_size == (long long)d_size) {
        snprintf(buf, bufsz, "%lld%s", (long long)d_size, units[i]);
    } else {
        snprintf(buf, bufsz, "%.1f%s", d_size, units[i]);
    }
}

static int term_width(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }
    return 80; /* fallback for non-tty / no ioctl support */
}

static int compute_cols(size_t cell_width) {
    int tw = term_width();
    int cols = tw / (int)(cell_width + 2); /* 2 spaces gutter between columns */
    if (cols < 1) cols = 1;
    if (!isatty(STDOUT_FILENO)) cols = 1; /* one per line when piped, like ls */
    return cols;
}

/* The true default view: "year/month/day              file1.txt --10gb"
 * cells, stacked in ls-style "down then across" grid columns. */
static void print_grid_sized(entry_t *entries, size_t count) {
    if (count == 0) return;

    char (*sizebuf)[32] = malloc(count * sizeof(*sizebuf));
    char (*timebuf)[16] = malloc(count * sizeof(*timebuf));
    if (!sizebuf || !timebuf) {
        perror("zl: memory allocation failed");
        free(sizebuf);
        free(timebuf);
        return;
    }

    size_t max_name = 0;
    for (size_t i = 0; i < count; i++) {
        format_size(entries[i].st.st_size, sizebuf[i], sizeof(sizebuf[i]));

        struct tm tmv;
        localtime_r(&entries[i].st.st_mtime, &tmv);
        strftime(timebuf[i], sizeof(timebuf[i]), "%Y/%m/%d", &tmv);

        size_t nlen = strlen(entries[i].name);
        if (nlen > max_name) max_name = nlen;
    }

    /* 10 chars for "year/month/day", 14 spaces for alignment separation gap */
    size_t time_and_gap_width = 10 + 14;
    size_t cell_width = time_and_gap_width + max_name + 2 /* "--" */ + 8 /* rough size fallback */;

    int cols = compute_cols(cell_width);
    size_t rows = (count + (size_t)cols - 1) / (size_t)cols;

    for (size_t r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            size_t idx = (size_t)c * rows + r;
            if (idx >= count) continue;

            const char *col = color_for(entries[idx].name, &entries[idx].st);
            size_t name_pad = max_name - strlen(entries[idx].name);

            /* print: year/month/day              [colored_name] */
            printf("%s              %s%s%s", timebuf[idx], col, entries[idx].name, C_RESET);
            for (size_t p = 0; p < name_pad; p++) putchar(' ');

            /* print: --size */
            printf("--%s", sizebuf[idx]);

            int is_last_col_in_row = (c == cols - 1) || (idx + rows >= count);
            if (!is_last_col_in_row) {
                /* standard 2 spaces separator padding between columns grid cells */
                printf("  ");
            }
        }
        putchar('\n');
    }

    free(sizebuf);
    free(timebuf);
}

static void list_dir(const char *path) {
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "zl: cannot access '%s': %s\n", path, strerror(errno));
        return;
    }

    entry_t *entries = NULL;
    size_t count = 0, cap = 0;
    struct dirent *de;

    size_t path_len = strlen(path);
    int needs_slash = (path_len > 0 && path[path_len - 1] != '/');

    while ((de = readdir(d)) != NULL) {
        if (!show_hidden && de->d_name[0] == '.') continue;

        char full[4096];
        int printed;
        if (needs_slash) {
            printed = snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        } else {
            printed = snprintf(full, sizeof(full), "%s%s", path, de->d_name);
        }

        if (printed >= (int)sizeof(full)) {
            fprintf(stderr, "zl: path truncation guard triggered\n");
            continue;
        }

        struct stat st;
        if (lstat(full, &st) != 0) continue;

        if (count == cap) {
            cap = cap ? cap * 2 : 32;
            entry_t *tmp = realloc(entries, cap * sizeof(entry_t));
            if (!tmp) {
                perror("zl: memory allocation failed");
                closedir(d);
                return;
            }
            entries = tmp;
        }

        entries[count].name = strdup(de->d_name);
        entries[count].st = st;
        count++;
    }
    closedir(d);

    qsort(entries, count, sizeof(entry_t), cmp_entries);

    // Forces the minimalist timestamp and size layout to run natively everywhere
    print_grid_sized(entries, count);

    for (size_t i = 0; i < count; i++) free(entries[i].name);
    free(entries);
}

int main(int argc, char **argv) {
    int npaths = 0;
    char *paths[256];

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (int j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'a') show_hidden = 1;
                else if (argv[i][j] == 'b') raw_bytes = 1; /* force exact byte counts */
                else {
                    fprintf(stderr, "zl: invalid option -- '%c'\n", argv[i][j]);
                    return 1;
                }
            }
        } else {
            if (npaths < 256) paths[npaths++] = argv[i];
        }
    }

    if (npaths == 0) {
        paths[0] = ".";
        npaths = 1;
    }

    for (int i = 0; i < npaths; i++) {
        if (npaths > 1) printf("%s:\n", paths[i]);
        list_dir(paths[i]);
        if (npaths > 1 && i != npaths - 1) printf("\n");
    }

    return 0;
}
