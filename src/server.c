#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "server.h"
#include "document.h"
#include "protocol.h"
#include <stdbool.h>

#define MAX_CLIENTS 10

// Structure to hold client information
typedef struct {
    int pid;
    int fd_s2c;  // Server to client file descriptor
    bool connected;
    char username[64];
    char role[10]; // "read" or "write"
} client_t;

// Global variables
static document_t *doc;
static unsigned long version = 0;
static client_t clients[MAX_CLIENTS];
static int client_count = 0;
static pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t doc_mutex = PTHREAD_MUTEX_INITIALIZER;

// Forward declarations
void broadcast_document_update(const char *username, const char *command, const char *response);
bool has_write_permission(const char *role);
bool process_command(const char *cmd, const char *username, const char *role, char *response, size_t resp_size);

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

    // Register client in the global array
    int client_index = -1;
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!clients[i].connected)
        {
            client_index = i;
            clients[i].pid = client_pid;
            clients[i].fd_s2c = fd_s2c;
            clients[i].connected = true;
            strncpy(clients[i].username, username, sizeof(clients[i].username) - 1);
            strncpy(clients[i].role, role, sizeof(clients[i].role) - 1);
            client_count++;
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);

    if (client_index == -1) {
        write(fd_s2c, "Reject SERVER_FULL\n", 19);
        close(fd_c2s);
        close(fd_s2c);
        unlink(fifo_c2s);
        unlink(fifo_s2c);
        return NULL;
    }
    
    // Send role and document
    pthread_mutex_lock(&doc_mutex);
    char *docstr;
    size_t doclen;
    document_serialize(doc, &docstr, &doclen);
    send_document(fd_s2c, role, version, docstr, doclen);
    free(docstr);
    pthread_mutex_unlock(&doc_mutex);

    // Command loop
    char cmd[256];
    while (read(fd_c2s, cmd, sizeof(cmd) - 1) > 0)
    {
        // Process the command
        cmd[sizeof(cmd) - 1] = '\0';
        printf("Received command from %s: %s", username, cmd);

        // Create response buffer
        char response[512] = {0};
        
        // Execute the command if permissions allow
        if (strncmp(cmd, "i ", 2) == 0 || strncmp(cmd, "d ", 2) == 0) {
            // These commands require write permission
            if (strcmp(role, "write") == 0) {
                pthread_mutex_lock(&doc_mutex);
                bool success = process_command(cmd, username, role, response, sizeof(response));
                if (success) {
                    // Increase version only for successful write operations
                    version++;
                }
                pthread_mutex_unlock(&doc_mutex);
                
                // Broadcast to all clients
                broadcast_document_update(username, cmd, success ? "SUCCESS" : response);
            } else {
                // Read-only user tried to modify document
                snprintf(response, sizeof(response), 
                         "Reject UNAUTHORISED %c write read\n", cmd[0]);
                write(fd_s2c, response, strlen(response));
            }
        } else {
            // Other commands (read operations, etc.)
            pthread_mutex_lock(&doc_mutex);
            bool success = process_command(cmd, username, role, response, sizeof(response));
            pthread_mutex_unlock(&doc_mutex);
            
            // For queries like DOC?, just send response to this client
            write(fd_s2c, response, strlen(response));
        }
    }

    // Client disconnected, clean up
    pthread_mutex_lock(&client_mutex);
    clients[client_index].connected = false;
    client_count--;
    pthread_mutex_unlock(&client_mutex);
    
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

// Broadcast document update to all connected clients
void broadcast_document_update(const char *username, const char *command, const char *response)
{
    char *docstr;
    size_t doclen;
    document_serialize(doc, &docstr, &doclen);
    
    // Create update message
    char header[256];
    snprintf(header, sizeof(header), "VERSION %lu\nEDIT %s %s %s\nEND\n", 
             version, username, command, response);
    
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].connected) {
            // Send update to each connected client
            write(clients[i].fd_s2c, header, strlen(header));
            send_document(clients[i].fd_s2c, clients[i].role, version, docstr, doclen);
        }
    }
    pthread_mutex_unlock(&client_mutex);
    
    free(docstr);
}

// Check if user has write permission
bool has_write_permission(const char *role)
{
    return (role != NULL && strcmp(role, "write") == 0);
}

// Process a client command
bool process_command(const char *cmd, const char *username, const char *role, char *response, size_t resp_size)
{
    // Check for insert command: i <position> <text>
    if (strncmp(cmd, "i ", 2) == 0) {
        if (!has_write_permission(role)) {
            snprintf(response, resp_size, "Reject UNAUTHORISED INSERT write read");
            return false;
        }
        
        int position;
        char text[256];
        if (sscanf(cmd, "i %d %[^\n]", &position, text) == 2) {
            if (position < 0 || position > (int)doc->length) {
                snprintf(response, resp_size, "Reject INVALID_POSITION");
                return false;
            }
            
            document_insert(doc, position, text);
            return true;
        }
    }
    
    // Check for delete command: d <position> <count>
    else if (strncmp(cmd, "d ", 2) == 0) {
        if (!has_write_permission(role)) {
            snprintf(response, resp_size, "Reject UNAUTHORISED DELETE write read");
            return false;
        }
        
        int position, count;
        if (sscanf(cmd, "d %d %d", &position, &count) == 2) {
            if (position < 0 || position >= (int)doc->length) {
                snprintf(response, resp_size, "Reject INVALID_POSITION");
                return false;
            }
            
            document_delete(doc, position, count);
            return true;
        }
    }
    
    // Check for DOC? command
    else if (strcmp(cmd, "DOC?\n") == 0 || strcmp(cmd, "DOC?") == 0) {
        char *docstr;
        size_t doclen;
        document_serialize(doc, &docstr, &doclen);
        snprintf(response, resp_size, "VERSION %lu\nDOCUMENT (%zu bytes):\n%s", 
                 version, doclen, docstr);
        free(docstr);
        return true;
    }
    
    // Handle version query or other commands here
    
    // Unknown command
    snprintf(response, resp_size, "Reject UNKNOWN_COMMAND");
    return false;
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