#ifndef PROTOCOL_H
#define PROTOCOL_H

int send_document(int fd, const char *role, unsigned long version, const char *doc, size_t len);

#endif