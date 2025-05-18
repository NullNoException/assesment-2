#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "protocol.h"

int send_document(int fd, const char *role, unsigned long version, const char *doc, size_t len)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "%s\n%lu\n%zu\n", role, version, len);
    if (write(fd, buf, n) != n)
        return -1;
    if (write(fd, doc, len) != (ssize_t)len)
        return -1;
    return 0;
}