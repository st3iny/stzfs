add_executable(filesystem main.c stzfs.c disk.c block.c inode.c blockptr.c inodeptr.c direntry.c bitmap.c find.c helpers.c bitmap_cache.c super_block_cache.c)
target_link_libraries(filesystem fuse3)

add_executable(stzfs fuse_cli.c stzfs.c disk.c block.c inode.c blockptr.c inodeptr.c direntry.c bitmap.c find.c helpers.c bitmap_cache.c super_block_cache.c)
target_link_libraries(stzfs fuse3)

add_executable(utils utils.c stzfs.c disk.c block.c inode.c blockptr.c inodeptr.c direntry.c bitmap.c find.c helpers.c bitmap_cache.c super_block_cache.c)
target_link_libraries(utils fuse3)

add_executable(mkfs.stzfs mkfs.c stzfs.c disk.c block.c inode.c blockptr.c inodeptr.c direntry.c bitmap.c find.c helpers.c bitmap_cache.c super_block_cache.c)
target_link_libraries(mkfs.stzfs fuse3)
