#ifndef SLGW_PROCESS_LOCK_H
#define SLGW_PROCESS_LOCK_H

#include <stdbool.h>

#include "slgw_config.h"

typedef struct {
    int fd;
    bool acquired;
    char path[SLGW_PATH_SIZE];
} slgw_process_lock;

int slgw_process_lock_init(slgw_process_lock *lock, const slgw_config *cfg);
int slgw_process_lock_acquire(slgw_process_lock *lock);
void slgw_process_lock_release(slgw_process_lock *lock);

#endif
