/* SPDX-License-Identifier: MIT */
#include "qk_file.h"

#ifdef __KERNEL__
#include <linux/fs.h>
#include <linux/vmalloc.h>

#include "qk_utils.h"

/* filp_open, filp_close, kernel_read and kernel_write are exported by the
 * kernel but are not in the GKI symbol list, so a production GKI kernel does
 * not offer them to a module. They are resolved by name instead; the module
 * still loads without them, and then has no fonts and no saved settings.
 */
static struct file *(*filp_open_fn)(const char *, int, umode_t);
static int (*filp_close_fn)(struct file *, fl_owner_t);
static ssize_t (*kernel_read_fn)(struct file *, void *, size_t, loff_t *);
static ssize_t (*kernel_write_fn)(struct file *, const void *, size_t, loff_t *);
static bool file_ready;

int qk_file_init(void)
{
    filp_open_fn = (typeof(filp_open_fn))qk_lookup_name("filp_open");
    filp_close_fn = (typeof(filp_close_fn))qk_lookup_name("filp_close");
    kernel_read_fn = (typeof(kernel_read_fn))qk_lookup_name("kernel_read");
    kernel_write_fn = (typeof(kernel_write_fn))qk_lookup_name("kernel_write");
    file_ready = filp_open_fn && filp_close_fn && kernel_read_fn && kernel_write_fn;
    if (!file_ready)
        LOGW("File access unavailable: no device font and no saved settings");
    return file_ready ? 0 : -ENOENT;
}

static struct file *NO_CFI qk_open(const char *path, int flags, umode_t mode)
{
    return filp_open_fn(path, flags, mode);
}

static void NO_CFI qk_close(struct file *file)
{
    filp_close_fn(file, NULL);
}

static ssize_t NO_CFI qk_read(struct file *file, void *buffer, size_t size,
                              loff_t *position)
{
    return kernel_read_fn(file, buffer, size, position);
}

static ssize_t NO_CFI qk_write(struct file *file, const void *buffer, size_t size,
                               loff_t *position)
{
    return kernel_write_fn(file, buffer, size, position);
}

long qk_file_size(const char *path)
{
    struct file *file;
    long size;

    if (!file_ready)
        return -ENOSYS;
    file = qk_open(path, O_RDONLY, 0);
    if (IS_ERR(file))
        return PTR_ERR(file);
    size = (long)i_size_read(file_inode(file));
    qk_close(file);
    return size;
}

long qk_file_read(const char *path, void *buffer, unsigned long size)
{
    struct file *file;
    loff_t position = 0;
    ssize_t got;

    if (!buffer || !size)
        return -EINVAL;
    if (!file_ready)
        return -ENOSYS;
    file = qk_open(path, O_RDONLY, 0);
    if (IS_ERR(file))
        return PTR_ERR(file);
    got = qk_read(file, buffer, size, &position);
    qk_close(file);
    return (long)got;
}

int qk_file_write(const char *path, const void *buffer, unsigned long size)
{
    struct file *file;
    loff_t position = 0;
    ssize_t written;

    if (!buffer)
        return -EINVAL;
    if (!file_ready)
        return -ENOSYS;
    file = qk_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (IS_ERR(file))
        return PTR_ERR(file);
    written = qk_write(file, buffer, size, &position);
    qk_close(file);
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

int qk_file_init(void)
{
    return 0;
}

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
