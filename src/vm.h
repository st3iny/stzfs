#ifndef FILESYSTEM_VM_H
#define FILESYSTEM_VM_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define VM_HDD_PATH "/tmp/vm-hdd.dat"

// config
int vm_config_create_file(const char* path, off_t length);
off_t vm_config_set_file(const char* path);

// getters
int vm_get_fd(void);

// low level methods
int vm_write(off_t addr, const void* buffer, size_t length);
int vm_read(off_t addr, void* buffer, size_t length);
off_t vm_size(void);

// cleanup
void vm_destroy(void);

#endif // FILESYSTEM_VM_H
