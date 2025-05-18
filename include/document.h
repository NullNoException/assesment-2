#ifndef DOCUMENT_H
#define DOCUMENT_H

typedef struct doc_node
{
    char c;
    struct doc_node *next;
} doc_node_t;

typedef struct
{
    doc_node_t *head;
    size_t length;
    unsigned long version;
} document_t;

document_t *document_create(void);
void document_free(document_t *doc);
int document_insert(document_t *doc, size_t pos, const char *text);
int document_delete(document_t *doc, size_t pos, size_t n);
void document_serialize(document_t *doc, char **out, size_t *len);

#endif