#define _POSIX_C_SOURCE 200809L
#include "slgw_process_lock.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slgw_text.h"

static int slgw_process_lock_prepare_parent(const char *path) {
    char local[SLGW_PATH_SIZE];
    char *last_sep;
    if (slgw_copy_text(local, sizeof(local), path) != 0) {
        return -1;
    }
    last_sep = strrchr(local, '/');
    if (last_sep == NULL) {
        return 0;
    }
    *last_sep = '\0';
    if (local[0] == '\0') {
        return 0;
    }
    if (mkdir(local, 0775) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

int slgw_process_lock_init(slgw_process_lock *lock, const slgw_config *cfg) {
    memset(lock, 0, sizeof(*lock));
    lock->fd = -1;
    if (slgw_copy_text(lock->path, sizeof(lock->path), cfg->process_lock_file) != 0) {
        return -1;
    }
    return 0;
}

int slgw_process_lock_acquire(slgw_process_lock *lock) {
    char payload[128];
    ssize_t ignored;
    if (lock == NULL || lock->path[0] == '\0') {
        return -1;
    }
    if (slgw_process_lock_prepare_parent(lock->path) != 0) {
        return -1;
    }
    lock->fd = open(lock->path, O_CREAT | O_RDWR, 0664);
    if (lock->fd < 0) {
        return -1;
    }
    if (flock(lock->fd, LOCK_EX | LOCK_NB) != 0) {
        close(lock->fd);
        lock->fd = -1;
        return -1;
    }
    if (ftruncate(lock->fd, 0) != 0) {
        close(lock->fd);
        lock->fd = -1;
        return -1;
    }
    if (slgw_format(payload, sizeof(payload), "{\"pid\":%ld,\"path\":\"%s\"}\n", (long)getpid(), lock->path) != 0) {
        close(lock->fd);
        lock->fd = -1;
        return -1;
    }
    ignored = write(lock->fd, payload, strlen(payload));
    (void)ignored;
    lock->acquired = true;
    return 0;
}

void slgw_process_lock_release(slgw_process_lock *lock) {
    if (lock == NULL || lock->fd < 0) {
        return;
    }
    flock(lock->fd, LOCK_UN);
    close(lock->fd);
    lock->fd = -1;
    lock->acquired = false;
}
