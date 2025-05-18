#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h> // Add this to fix the errno errors
#include "server.h"
#include "document.h"
#include "protocol.h"

static document_t *doc;
static unsigned long version = 0;

void *handle_client(void *arg)
{

    int client_pid = *(int *)arg;
    free(arg);
    char fifo_c2s[64], fifo_s2c[64];
    printf("Client PID: %d\n", client_pid);
    snprintf(fifo_c2s, sizeof(fifo_c2s), "FIFO_C2S_%d", client_pid);
    snprintf(fifo_s2c, sizeof(fifo_s2c), "FIFO_S2C_%d", client_pid);
    if (mkfifo(fifo_c2s, 0666) == -1 && errno != EEXIST)
    {
        perror("Failed to create client-to-server FIFO");
        return NULL;
    }
    if (mkfifo(fifo_s2c, 0666) == -1 && errno != EEXIST)
    {
        perror("Failed to create server-to-client FIFO");
        unlink(fifo_c2s);
        return NULL;
    }

    int fd_s2c = open(fifo_s2c, O_WRONLY); // Open for writing first
    if (fd_s2c < 0)
    {
        perror("Failed to open server-to-client FIFO");
        unlink(fifo_c2s);
        unlink(fifo_s2c);
        return NULL;
    }

    int fd_c2s = open(fifo_c2s, O_RDONLY); // Open for reading second
    if (fd_c2s < 0)
    {
        perror("Failed to open client-to-server FIFO");
        close(fd_s2c);
        unlink(fifo_c2s);
        unlink(fifo_s2c);
        return NULL;
    }

    char username[64] = {0};
    read(fd_c2s, username, sizeof(username) - 1);
    username[strcspn(username, "\n")] = 0;

    // Only "bob" and "ryan" are write, "eve" is read
    const char *role = (!strcmp(username, "bob") || !strcmp(username, "ryan")) ? "write" : (!strcmp(username, "eve") ? "read" : NULL);
    if (!role)
    {
        write(fd_s2c, "Reject UNAUTHORISED\n", 20);
        sleep(1);
        close(fd_c2s);
        close(fd_s2c);
        unlink(fifo_c2s);
        unlink(fifo_s2c);
        return NULL;
    }
    // Send role and document
    char *docstr;
    size_t doclen;
    document_serialize(doc, &docstr, &doclen);
    send_document(fd_s2c, role, version, docstr, doclen);
    free(docstr);

    // Command loop (not fully implemented)
    char cmd[256];
    while (read(fd_c2s, cmd, sizeof(cmd) - 1) > 0)
    {
        // Process the command here
        cmd[sizeof(cmd) - 1] = '\0';
        printf("Received command: %s", cmd);

        // Echo back to client for now
        write(fd_s2c, cmd, strlen(cmd));

        // Eventually implement real command processing here
        // TODO: Parse and apply commands, update doc/version, broadcast
    }
    close(fd_c2s);
    close(fd_s2c);
    unlink(fifo_c2s);
    unlink(fifo_s2c);
    return NULL;
}

void sigrtmin_handler(int sig, siginfo_t *si, void *unused)
{
    // Suppress warnings for unused parameters
    (void)sig;
    (void)unused;

    int *pid = malloc(sizeof(int));
    *pid = si->si_pid;
    pthread_t tid;
    pthread_create(&tid, NULL, handle_client, pid);
    kill(*pid, SIGRTMIN + 1);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <TIME_INTERVAL>\n", argv[0]);
        exit(1);
    }
    printf("Server PID: %d\n", getpid());
    doc = document_create();

    struct sigaction sa = {0};
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sigrtmin_handler;
    sigaction(SIGRTMIN, &sa, NULL);

    while (1)
        pause();
    document_free(doc);
    return 0;
}