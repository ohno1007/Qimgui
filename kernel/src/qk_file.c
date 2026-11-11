/* SPDX-License-Identifier: MIT */
#include "qk_file.h"

#ifdef __KERNEL__
#include <linux/fs.h>
#include <linux/vmalloc.h>

long qk_file_size(const char *path)
{
    struct file *file;
    long size;

    file = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(file))
        return PTR_ERR(file);
    size = (long)i_size_read(file_inode(file));
    filp_close(file, NULL);
    return size;
}

long qk_file_read(const char *path, void *buffer, unsigned long size)
{
    struct file *file;
    loff_t position = 0;
    ssize_t got;

    if (!buffer || !size)
        return -EINVAL;
    file = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(file))
        return PTR_ERR(file);
    got = kernel_read(file, buffer, size, &position);
    filp_close(file, NULL);
    return (long)got;
}

int qk_file_write(const char *path, const void *buffer, unsigned long size)
{
    struct file *file;
    loff_t position = 0;
    ssize_t written;

    if (!buffer)
        return -EINVAL;
    file = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (IS_ERR(file))
        return PTR_ERR(file);
    written = kernel_write(file, buffer, size, &position);
    filp_close(file, NULL);
    if (written < 0)
        return (int)written;
    return (unsigned long)written == size ? 0 : -EIO;
}

long qk_file_load(const char *path, void **buffer, unsigned long limit)
{
    long size, got;
    void *area;

    if (!buffer)
        return -EINVAL;
    *buffer = NULL;
    size = qk_file_size(path);
    if (size < 0)
        return size;
    if (!size || (unsigned long)size > limit)
        return -EFBIG;
    /* Fonts run to tens of megabytes: vmalloc, never kmalloc. */
    area = vmalloc((unsigned long)size);
    if (!area)
        return -ENOMEM;
    got = qk_file_read(path, area, (unsigned long)size);
    if (got != size) {
        vfree(area);
        return got < 0 ? got : -EIO;
    }
    *buffer = area;
    return size;
}

void qk_file_release(void *buffer)
{
    vfree(buffer);
}

#else /* Host build: the same contract over stdio, for the tests. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

long qk_file_size(const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;

    if (!file)
        return -ENOENT;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fclose(file);
    return size;
}

long qk_file_read(const char *path, void *buffer, unsigned long size)
{
    FILE *file = fopen(path, "rb");
    size_t got;

    if (!buffer || !size)
        return -EINVAL;
    if (!file)
        return -ENOENT;
    got = fread(buffer, 1, size, file);
    fclose(file);
    return (long)got;
}

int qk_file_write(const char *path, const void *buffer, unsigned long size)
{
    FILE *file = fopen(path, "wb");
    size_t written;

    if (!buffer)
        return -EINVAL;
    if (!file)
        return -EACCES;
    written = fwrite(buffer, 1, size, file);
    fclose(file);
    return written == size ? 0 : -EIO;
}

long qk_file_load(const char *path, void **buffer, unsigned long limit)
{
    long size, got;
    void *area;

    if (!buffer)
        return -EINVAL;
    *buffer = NULL;
    size = qk_file_size(path);
    if (size < 0)
        return size;
    if (!size || (unsigned long)size > limit)
        return -EFBIG;
    area = malloc((unsigned long)size);
    if (!area)
        return -ENOMEM;
    got = qk_file_read(path, area, (unsigned long)size);
    if (got != size) {
        free(area);
        return got < 0 ? got : -EIO;
    }
    *buffer = area;
    return size;
}

void qk_file_release(void *buffer)
{
    free(buffer);
}
#endif
