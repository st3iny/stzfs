#define FUSE_USE_VERSION 34
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>

#include "syscalls.h"
#include "types.h"
#include "vm.h"

int printf_filler(void* buffer, const char *name, const struct stat* stbuf, off_t offset,
                  enum fuse_fill_dir_flags flags) {
    printf("%s\n", name);
}

int main() {
    // definitions
    const blockptr_t blocks = 4 * 256 * 1024;
    const inodeptr_t inodes = (const inodeptr_t)pow(2, 20);

    // create vm hdd
    vm_config_create_file(VM_HDD_PATH, (long long)blocks * BLOCK_SIZE);
    off_t size = vm_config_set_file(VM_HDD_PATH);

    // create filesystem
    sys_makefs(inodes);

    // create some files
    struct fuse_file_info file_info;
    sys_create("/hello.world", 0, &file_info);

    sys_create("/foo.bar", 0, &file_info);

    sys_create("/some_file", 0, &file_info);

    // write data to a file
    char buffer[1024 * BLOCK_SIZE];
    size_t len;
    long offset = 0;
    int fd = open("/tmp/bigfile", O_RDONLY);
    while ((len = read(fd, buffer, sizeof(buffer))) != 0) {
        // printf("offset = %lu, len = %i\n", offset, len);
        sys_write("/some_file", buffer, len, offset, &file_info);
        offset += len;
    }
    close(fd);

    // list root contents
    sys_readdir("/", NULL, printf_filler, 0, NULL, 0);

    // rename some files
    sys_rename("/some_file", "/bigfile", 0);
    sys_rename("/foo.bar", "/hello.world", 0);

    // read data from file
    offset = 0;
    fd = open("/tmp/bigfile.comp", O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
    while ((len = sys_read("/bigfile", buffer, sizeof(buffer), offset, &file_info)) != 0) {
        // printf("offset = %lu, len = %i\n", offset, len);
        write(fd, buffer, len);
        offset += len;
    }
    close(fd);

    // unlink file
    sys_unlink("/bigfile");

    // create directories
    sys_mkdir("/home", 0);
    sys_mkdir("/home/user", 0);

    // create a file in new directory and write some data to it
    sys_create("/home/user/lore_ipsum.txt", 0, &file_info);
    char lorem[] = "Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua. At vero eos et accusam et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea takimata sanctus est Lorem ipsum dolor sit amet. Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua. At vero eos et accusam et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea takimata sanctus est Lorem ipsum dolor sit amet.";
    sys_write("/home/user/lore_ipsum.txt", lorem, sizeof(lorem), 0, &file_info);

    // ls the user directory
    sys_readdir("/home/user", NULL, printf_filler, 0, NULL, 0);

    // create another empty dir and unlink it
    sys_mkdir("/home/user/foo", 0);
    sys_unlink("/home/user/foo"); // should fail
    sys_create("/home/user/foo/bar", 0, &file_info);
    sys_readdir("/home/user", NULL, printf_filler, 0, NULL, 0);
    sys_readdir("/home/user/foo", NULL, printf_filler, 0, NULL, 0);
    sys_rmdir("/home/user/foo"); // should fail too
    sys_unlink("/home/user/foo/bar");
    sys_rmdir("/home/user/foo");
    sys_readdir("/home/user", NULL, printf_filler, 0, NULL, 0);

    return 0;
}
