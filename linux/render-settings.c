/* DS3 profile dimensions; file handling and parser adapted from ds3gearmass.c. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
/* Exact DS3 AppData VFS path, not ME1's Documents/Config nor the game mount. */
static int system_dir(int parent) {
    static const char* const dirs[] = {"drive_c", "Users", "Shepard", "AppData", "Local", "EA Games", "Dead Space 3"};
    int fd = dup(parent);
    for (size_t i = 0; fd >= 0 && i < sizeof dirs / sizeof *dirs; ++i) {
        int next = openat(fd, dirs[i], O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
        close(fd);
        fd = next;
    }
    return fd;
}

static int read_file(int dir, const char* name, char* data, size_t cap, struct stat* info) {
    int fd = openat(dir, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size >= (off_t)cap) { close(fd); return -1; }
    size_t used = 0;
    ssize_t n;
    do {
        n = read(fd, data + used, cap - used);
        if (n > 0) used += (size_t)n;
    } while (used < cap && (n > 0 || (n < 0 && errno == EINTR)));
    close(fd);
    if (n < 0 || used == cap || memchr(data, 0, used)) return -1;
    data[used] = 0;
    if (info) *info = st;
    return 1;
}

/* Commit complete files via same-directory rename; a preexisting temp is never followed. */
static int replace_file(int dir, const char* name, const char* data, mode_t mode) {
    char temporary[96];
    snprintf(temporary, sizeof temporary, ".%s.tmp.%ld", name, (long)getpid());
    int fd = openat(dir, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode & 0777);
    if (fd < 0) return 0;
    size_t left = strlen(data);
    while (left) {
        ssize_t n = write(fd, data, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        data += n; left -= (size_t)n;
    }
    int ok = !left && !fsync(fd);
    if (close(fd)) ok = 0;
    if (ok && !renameat(dir, temporary, dir, name)) return 1;
    unlinkat(dir, temporary, 0);
    return 0;
}

/* Same-aspect subset of init_modes() in hle_d3d9.c; never invent arbitrary sizes. */
static const struct { int w, h; const char* text; } modes[] = {
    {1280, 720, "1280x720"}, {1600, 900, "1600x900"}, {1920, 1080, "1920x1080"},
};
typedef struct { const char *first, *end; int value; } Span;
static int dimensions(const char* file, Span size[2]) {
    const char* line = file;
    while (*line) {
        const char *end = strchr(line, '\n'), *key = line;
        if (!end) end = line + strlen(line);
        key += strspn(key, " \t");
        const char* keys[] = {"Window.Width", "Window.Height"};
        for (int i = 0; i < 2; ++i) {
            size_t len = strlen(keys[i]);
            if (strncmp(key, keys[i], len)) continue;
            const char* value = key + len;
            if (*value != '=' && *value != ' ' && *value != '\t') continue;
            value += strspn(value, " \t");
            if (*value++ != '=' || size[i].first) return -1;
            value += strspn(value, " \t");
            size_t digits = strspn(value, "0123456789");
            if (!digits || digits > 5 || value + digits > end) return -1;
            const char* tail = value + digits;
            if (tail + strspn(tail, " \t\r") != end) return -1;
            size[i] = (Span){value, tail, atoi(value)};
        }
        line = *end ? end + 1 : end;
    }
    for (size_t i = 0; i < sizeof modes / sizeof *modes; ++i)
        if (size[0].value == modes[i].w && size[1].value == modes[i].h) return (int)i;
    return -1;
}

int main(int argc,char **argv) {
    if (argc!=3) return 2;
    int mode=-1;
    for (unsigned i=0;i<sizeof modes/sizeof *modes;++i) if (!strcmp(argv[2],modes[i].text)) mode=i;
    if (mode<0) return 2;
    int root=open(argv[1],O_RDONLY|O_CLOEXEC|O_DIRECTORY|O_NOFOLLOW);
    if (root<0) return 2;
    int dir=system_dir(root), ok=0;
    close(root);
    if (dir<0) goto done;
    char file[8192], output[8192];
    Span size[2]={{0}};
    struct stat original, now;
    const char* name="system.txt";
    if (read_file(dir,name,file,sizeof file,&original)!=1) goto done;
    dimensions(file,size);
    if (!size[0].first || !size[1].first) goto done;
    /* Reparse to distinguish an unsupported size from malformed/duplicate keys. */
    if (dimensions(file,(Span[2]){{0}})<0) goto done;
    int a=size[0].first<size[1].first?0:1, b=1-a;
    int values[]={modes[mode].w,modes[mode].h};
    int n=snprintf(output,sizeof output,"%.*s%d%.*s%d%s",(int)(size[a].first-file),file,values[a],
                   (int)(size[b].first-size[a].end),size[a].end,values[b],size[b].end);
    if (n<0 || (size_t)n>=sizeof output || fstatat(dir,name,&now,AT_SYMLINK_NOFOLLOW) ||
        original.st_ino!=now.st_ino || original.st_dev!=now.st_dev || original.st_size!=now.st_size ||
        original.st_mtim.tv_sec!=now.st_mtim.tv_sec || original.st_mtim.tv_nsec!=now.st_mtim.tv_nsec) goto done;
    ok=!strcmp(file,output) || replace_file(dir,name,output,original.st_mode);
done:
    if (dir>=0) close(dir);
    if (!ok) fprintf(stderr,"[ds3-launcher] cannot update profile system.txt; use current settings or initialise the profile in game\n");
    return ok?0:2;
}
