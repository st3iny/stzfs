#ifndef STZFS_LOG_H
#define STZFS_LOG_H

#include <stdio.h>

#define LOG(args...) printf("%s: ", __func__); printf(args); printf("\n");

#endif // STZFS_LOG_H
