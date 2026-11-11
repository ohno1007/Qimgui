// SPDX-License-Identifier: MIT
// Loads a kernel module and says why it did not load.
//
//   qkload qimgui.ko                 # try to load, then report
//   qkload -f qimgui.ko              # ignore version magic (see below)
//   qkload -n qimgui.ko              # inspect the file, load nothing
//
// insmod reports errno and nothing else, and ENOEXEC covers a dozen different
// refusals. The kernel writes the actual reason to its log, so this calls the
// syscall directly and then reads the log itself, which also works where the
// dmesg command is restricted. It checks the file first: a module that was
// renamed, truncated or rewritten in transit fails exactly the same way.
//
// -f sets MODULE_INIT_IGNORE_VERMAGIC. That is safe only when the module was
// built against the same kernel interface as the running kernel -- the symbol
// CRCs are still checked, and they are what keeps it honest. It never sets
// MODULE_INIT_IGNORE_MODVERSIONS: loading a module whose idea of a structure
// disagrees with the kernel's is how machines get corrupted.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

#define MODULE_INIT_IGNORE_MODVERSIONS 1
#define MODULE_INIT_IGNORE_VERMAGIC    2

static const char *elf_class(unsigned char value)
{
    return value == 1 ? "32-bit" : value == 2 ? "64-bit" : "unknown";
}

static const char *elf_type(unsigned short value)
{
    switch (value) {
    case 1: return "ET_REL (a kernel module is this)";
    case 2: return "ET_EXEC (an executable, not a module)";
    case 3: return "ET_DYN (a shared object, not a module)";
    default: return "not an object file";
    }
}

static const char *elf_machine(unsigned short value)
{
    switch (value) {
    case 183: return "aarch64";
    case 62: return "x86-64";
    case 40: return "arm (32-bit)";
    default: return "other";
    }
}

/* Reports what the file is, and the version magic it was built with. */
static int inspect(const char *path)
{
    unsigned char header[64];
    struct stat info;
    char *buffer;
    FILE *file;
    size_t size, i;
    int found = 0;

    if (stat(path, &info)) {
        printf("file      : %s -- cannot stat: %s\n", path, strerror(errno));
        return -1;
    }
    printf("file      : %s\n", path);
    printf("size      : %lld bytes\n", (long long)info.st_size);

    file = fopen(path, "rb");
    if (!file) {
        printf("            cannot open: %s\n", strerror(errno));
        return -1;
    }
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        printf("            too short to be an object file\n");
        fclose(file);
        return -1;
    }
    if (memcmp(header, "\177ELF", 4)) {
        printf("elf       : NOT an ELF file -- the first bytes are %02x %02x %02x %02x\n",
               header[0], header[1], header[2], header[3]);
        printf("            Whatever reached the device is not the module.\n");
        fclose(file);
        return -1;
    }
    printf("elf       : %s, %s, type %s\n", elf_class(header[4]),
           elf_machine(*(unsigned short *)(header + 18)),
           elf_type(*(unsigned short *)(header + 16)));

    /* The version magic sits in .modinfo as a plain string. */
    size = (size_t)info.st_size;
    buffer = malloc(size + 1);
    if (buffer) {
        rewind(file);
        if (fread(buffer, 1, size, file) == size) {
            buffer[size] = 0;
            for (i = 0; i + 9 < size; i++) {
                if (!memcmp(buffer + i, "vermagic=", 9)) {
                    printf("vermagic  : %s\n", buffer + i + 9);
                    found = 1;
                    break;
                }
            }
            for (i = 0; i + 11 < size; i++) {
                if (!memcmp(buffer + i, "__versions", 10)) {
                    printf("modversions: yes (symbol CRCs are present)\n");
                    break;
                }
            }
        }
        free(buffer);
    }
    if (!found)
        printf("vermagic  : none found -- not a kernel module?\n");
    fclose(file);
    return 0;
}

static void environment(void)
{
    struct utsname host;
    char line[256];
    FILE *file;

    if (!uname(&host))
        printf("kernel    : %s %s\n", host.release, host.machine);
    printf("page size : %ld bytes\n", sysconf(_SC_PAGESIZE));

    file = fopen("/proc/sys/kernel/modules_disabled", "r");
    if (file) {
        if (fgets(line, sizeof(line), file))
            printf("modules   : modules_disabled=%s", line);
        fclose(file);
    }
    file = fopen("/sys/module/module/parameters/sig_enforce", "r");
    if (file) {
        if (fgets(line, sizeof(line), file))
            printf("signatures: sig_enforce=%s", line);
        fclose(file);
    }
}

/* The kernel's own log, read through the syscall: the dmesg command is often
 * restricted on a device, and the reason for a refused module is only there.
 */
static void kernel_log(void)
{
    static char buffer[256 * 1024];
    const char *interesting[] = {"module", "qimgui", "symbol", "magic", "Unknown",
                                 "disagrees", "truncated", "corrupt", "section"};
    char *line, *next;
    long length;
    int shown = 0;
    unsigned i;

    /* SYSLOG_ACTION_READ_ALL */
    length = syscall(__NR_syslog, 3, buffer, (long)sizeof(buffer) - 1);
    if (length < 0) {
        printf("\nkernel log: cannot read it (%s). Run this as root.\n",
               strerror(errno));
        return;
    }
    buffer[length] = 0;
    printf("\nkernel log (matching lines, newest last):\n");
    for (line = buffer; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        for (i = 0; i < sizeof(interesting) / sizeof(*interesting); i++) {
            if (strstr(line, interesting[i])) {
                printf("  %s\n", line);
                shown++;
                break;
            }
        }
    }
    if (!shown)
        printf("  (nothing about modules in the log)\n");
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int flags = 0, inspect_only = 0, fd, ret, i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f"))
            flags |= MODULE_INIT_IGNORE_VERMAGIC;
        else if (!strcmp(argv[i], "-n"))
            inspect_only = 1;
        else
            path = argv[i];
    }
    if (!path) {
        printf("usage: %s [-f] [-n] <module.ko> \n", argv[0]);
        printf("  -f  ignore version magic (symbol CRCs are still checked)\n");
        printf("  -n  inspect the file and the system, load nothing\n");
        return 2;
    }

    environment();
    if (inspect(path) || inspect_only)
        return inspect_only ? 0 : 1;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        printf("\nopen failed: %s\n", strerror(errno));
        return 1;
    }
    printf("\nloading%s ...\n", flags ? " (ignoring version magic)" : "");
    ret = syscall(__NR_finit_module, fd, "", flags);
    if (ret == 0) {
        printf("loaded. Check /proc/qimgui for what it did next.\n");
        close(fd);
        kernel_log();
        return 0;
    }
    printf("finit_module failed: errno %d (%s)\n", errno, strerror(errno));
    switch (errno) {
    case ENOEXEC:
        printf("  The kernel parsed the file and refused it. The log below says\n"
               "  why: version magic, a symbol CRC, or a malformed section.\n");
        break;
    case ENOENT:
        printf("  A symbol this module needs is not exported by this kernel.\n");
        break;
    case EKEYREJECTED:
    case ENOKEY:
        printf("  This kernel requires signed modules.\n");
        break;
    case EPERM:
        printf("  Not permitted: run as root, and check modules_disabled above.\n");
        break;
    case EEXIST:
        printf("  A module of this name is already loaded.\n");
        break;
    default:
        break;
    }
    close(fd);
    kernel_log();
    return 1;
}
