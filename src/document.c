#include <stdlib.h>
#include <string.h>
#include "document.h"

document_t *document_create(void)
{
    document_t *doc = calloc(1, sizeof(document_t));
    return doc;
}

void document_free(document_t *doc)
{
    doc_node_t *cur = doc->head;
    while (cur)
    {
        doc_node_t *tmp = cur;
        cur = cur->next;
        free(tmp);
    }
    free(doc);
}

int document_insert(document_t *doc, size_t pos, const char *text)
{
    if (!doc || !text)
        return -1;
    size_t len = strlen(text);
    if (pos > doc->length)
        pos = doc->length;
    doc_node_t **pp = &doc->head;
    for (size_t i = 0; i < pos; ++i)
        pp = &(*pp)->next;
    for (size_t i = 0; i < len; ++i)
    {
        doc_node_t *n = malloc(sizeof(doc_node_t));
        n->c = text[i];
        n->next = *pp;
        *pp = n;
        pp = &n->next;
        doc->length++;
    }
    return 0;
}

int document_delete(document_t *doc, size_t pos, size_t n)
{
    if (!doc || pos >= doc->length)
        return -1;
    doc_node_t **pp = &doc->head;
    for (size_t i = 0; i < pos; ++i)
        pp = &(*pp)->next;
    for (size_t i = 0; i < n && *pp; ++i)
    {
        doc_node_t *tmp = *pp;
        *pp = tmp->next;
        free(tmp);
        doc->length--;
    }
    return 0;
}

void document_serialize(document_t *doc, char **out, size_t *len)
{
    *len = doc->length;
    *out = malloc(doc->length + 1);
    doc_node_t *cur = doc->head;
    for (size_t i = 0; i < doc->length; ++i)
    {
        (*out)[i] = cur->c;
        cur = cur->next;
    }
    (*out)[doc->length] = 0;
}