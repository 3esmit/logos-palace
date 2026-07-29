#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef SYS_pidfd_open
#error "pidfd_open syscall number is unavailable"
#endif

#ifndef SYS_pidfd_send_signal
#error "pidfd_send_signal syscall number is unavailable"
#endif

static int parse_positive(const char *encoded, unsigned long long *value)
{
    if (encoded == NULL || encoded[0] < '1' || encoded[0] > '9') {
        return -1;
    }
    for (const char *cursor = encoded + 1; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return -1;
        }
    }
    char *end = NULL;
    errno = 0;
    const unsigned long long parsed = strtoull(encoded, &end, 10);
    if (errno != 0 || end == encoded || *end != '\0' || parsed == 0) {
        return -1;
    }
    *value = parsed;
    return 0;
}

static int read_start_time(pid_t pid, unsigned long long *start_time)
{
    char path[64];
    const int length = snprintf(path, sizeof(path), "/proc/%ld/stat",
        (long)pid);
    if (length <= 0 || (size_t)length >= sizeof(path)) {
        return -1;
    }
    const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return -1;
    }
    char buffer[4097];
    ssize_t size;
    do {
        size = read(fd, buffer, sizeof(buffer) - 1);
    } while (size < 0 && errno == EINTR);
    const int read_error = errno;
    if (close(fd) != 0 && size >= 0) {
        return -1;
    }
    errno = read_error;
    if (size <= 0 || (size_t)size >= sizeof(buffer) - 1) {
        return -1;
    }
    buffer[size] = '\0';
    char *cursor = strrchr(buffer, ')');
    if (cursor == NULL || cursor[1] != ' ') {
        return -1;
    }
    cursor += 2;
    for (size_t field = 0; field < 19; ++field) {
        cursor = strchr(cursor, ' ');
        if (cursor == NULL) {
            return -1;
        }
        ++cursor;
    }
    char *end = strchr(cursor, ' ');
    if (end == NULL) {
        end = strchr(cursor, '\n');
    }
    if (end == NULL || end == cursor) {
        return -1;
    }
    const char saved = *end;
    *end = '\0';
    const int result = parse_positive(cursor, start_time);
    *end = saved;
    return result;
}

static int signal_number(const char *name)
{
    if (strcmp(name, "SIGTERM") == 0) {
        return SIGTERM;
    }
    if (strcmp(name, "SIGKILL") == 0) {
        return SIGKILL;
    }
    return -1;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fputs("usage: palace-pidfd-signal <pid> <start-time-ticks> "
            "<SIGTERM|SIGKILL>\n", stderr);
        return 2;
    }
    unsigned long long encoded_pid;
    unsigned long long expected_start;
    const int signal = signal_number(argv[3]);
    if (parse_positive(argv[1], &encoded_pid) != 0
        || encoded_pid > (unsigned long long)INT_MAX
        || parse_positive(argv[2], &expected_start) != 0
        || signal < 0) {
        fputs("pidfd signal arguments are invalid\n", stderr);
        return 2;
    }
    const pid_t pid = (pid_t)encoded_pid;
    const int pidfd = (int)syscall(SYS_pidfd_open, pid, 0);
    if (pidfd < 0) {
        const int status = errno == ESRCH ? 3 : 1;
        perror("pidfd_open");
        return status;
    }
    unsigned long long observed_start;
    if (read_start_time(pid, &observed_start) != 0) {
        const int saved = errno;
        close(pidfd);
        errno = saved;
        perror("read target identity");
        return 3;
    }
    if (observed_start != expected_start) {
        close(pidfd);
        fputs("pidfd target identity mismatch\n", stderr);
        return 4;
    }
    if (syscall(SYS_pidfd_send_signal, pidfd, signal, NULL, 0) != 0) {
        const int saved = errno;
        const int status = saved == ESRCH ? 3 : 1;
        close(pidfd);
        errno = saved;
        perror("pidfd_send_signal");
        return status;
    }
    if (close(pidfd) != 0) {
        perror("close pidfd");
        return 1;
    }
    return 0;
}
