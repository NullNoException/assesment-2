#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include "client.h"

// Define real-time signals if not available
#ifndef SIGRTMIN
#define SIGRTMIN 34
#endif

// Structure to share data between threads
typedef struct
{
    int fd_s2c;            // Server to client file descriptor
    int fd_c2s;            // Client to server file descriptor
    int should_exit;       // Flag to indicate reader thread should exit
    char username[64];     // Username for this client
    char role[10];         // Role (read/write)
    char document[4096];   // Current document content
    unsigned long version; // Current document version
} client_data_t;

// Reader thread function declaration
void *reader_thread(void *arg);

#define MAX_CLIENTS 10

// Reader thread function that continuously checks for messages from the server
void *reader_thread(void *arg)
{
    client_data_t *data = (client_data_t *)arg;
    char buf[4096];

    // Set the file descriptor to non-blocking mode
    int flags = fcntl(data->fd_s2c, F_GETFL, 0);
    fcntl(data->fd_s2c, F_SETFL, flags | O_NONBLOCK);

    while (!data->should_exit)
    {
        ssize_t n = read(data->fd_s2c, buf, sizeof(buf) - 1);

        if (n > 0)
        {
            buf[n] = '\0';

            // Check if this is an auto update
            if (strstr(buf, "AUTO_UPDATE") != NULL)
            {
                // Parse version and document
                char *version_str = strstr(buf, "VERSION ");
                unsigned long new_version = 0;

                if (version_str)
                {
                    sscanf(version_str, "VERSION %lu", &new_version);

                    // Only process if this is a newer version
                    if (new_version > data->version)
                    {
                        data->version = new_version;

                        // Extract document
                        char *doc_start = strstr(buf, "\nEND\n");
                        if (doc_start)
                        {
                            // Skip past "END" to get to document content
                            doc_start += 5;

                            // Clear screen and show updated document
                            printf("\033[2J\033[H"); // Clear screen and move cursor to top-left
                            printf("--- Automatic update received (Version %lu) ---\n", new_version);
                            printf("%s\n", doc_start);
                            printf("> "); // Reprint prompt
                            fflush(stdout);
                        }
                    }
                }
            }
            else if (strstr(buf, "EDIT") != NULL)
            {
                // This is an edit broadcast
                char *version_str = strstr(buf, "VERSION ");
                unsigned long new_version = 0;

                if (version_str)
                {
                    sscanf(version_str, "VERSION %lu", &new_version);

                    // Extract edit info and document
                    char *edit_info = strstr(buf, "EDIT ");
                    char *doc_start = strstr(buf, "\nEND\n");

                    if (edit_info && doc_start && new_version > data->version)
                    {
                        data->version = new_version;

                        // Skip past "END" to get to document content
                        doc_start += 5;

                        // Calculate length of edit info
                        size_t edit_len = (doc_start - edit_info) - 5; // -5 for "\nEND\n"
                        char edit_details[256] = {0};
                        strncpy(edit_details, edit_info, edit_len > 255 ? 255 : edit_len);

                        // Clear screen and show updated document
                        printf("\033[2J\033[H"); // Clear screen and move cursor to top-left
                        printf("--- Document updated: %s ---\n", edit_details);
                        printf("%s\n", doc_start);
                        printf("> "); // Reprint prompt
                        fflush(stdout);
                    }
                }
            }
            else
            {
                // Regular response to a command
                printf("\n%s\n> ", buf);
                fflush(stdout);
            }
        }
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            // Real error occurred
            perror("Error reading from server");
            data->should_exit = 1;
        }

        // Sleep briefly to avoid busy waiting
        usleep(100000); // 100ms
    }

    return NULL;
}

int connect_to_server(pid_t server_pid, const char *username)
{
    printf("Client PID from client app: %d\n", getpid());
    kill(server_pid, SIGRTMIN);
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGRTMIN + 1);
    sigprocmask(SIG_BLOCK, &set, NULL); // Block SIGRTMIN+1 before sigwait
    int sig;
    sigwait(&set, &sig);

    char fifo_c2s[64], fifo_s2c[64];
    snprintf(fifo_c2s, sizeof(fifo_c2s), "FIFO_C2S_%d", getpid());
    snprintf(fifo_s2c, sizeof(fifo_s2c), "FIFO_S2C_%d", getpid());

    printf("Client PID: %d\n", getpid());

    // Give the server some time to create the FIFOs
    // Add retry mechanism with timeout to open the FIFOs
    int retry = 0;
    const int max_retries = 5;
    int fd_s2c = -1;

    while (retry < max_retries && fd_s2c < 0)
    {
        fd_s2c = open(fifo_s2c, O_RDONLY); // Open for reading first
        if (fd_s2c < 0)
        {
            if (retry == max_retries - 1)
            {
                perror("Failed to open server-to-client FIFO");
                return -1;
            }
            printf("Retrying to open server-to-client FIFO (attempt %d/%d)...\n",
                   retry + 1, max_retries);
            sleep(1); // Wait 1 second before retrying
            retry++;
        }
    }

    int fd_c2s = open(fifo_c2s, O_WRONLY); // Open for writing second
    if (fd_c2s < 0)
    {
        perror("Failed to open client-to-server FIFO");
        close(fd_s2c);
        return -1;
    }

    // Initialize client data structure
    client_data_t client_data = {0};
    client_data.fd_s2c = fd_s2c;
    client_data.fd_c2s = fd_c2s;
    client_data.should_exit = 0;
    client_data.version = 0;
    strncpy(client_data.username, username, sizeof(client_data.username) - 1);

    write(fd_c2s, username, strlen(username));
    write(fd_c2s, "\n", 1);

    char buf[4096];
    ssize_t n = read(fd_s2c, buf, sizeof(buf) - 1);
    if (n > 0)
    {
        buf[n] = '\0';
        // Parse the server response
        if (strncmp(buf, "Reject", 6) == 0)
        {
            printf("Server response: %s", buf);
        }
        else
        {
            // Expecting: role\nversion\ndoclen\ndocument
            char *saveptr;
            char *role = strtok_r(buf, "\n", &saveptr);
            char *version_str = strtok_r(NULL, "\n", &saveptr);
            char *doclen = strtok_r(NULL, "\n", &saveptr);
            char *document = saveptr;
            if (role && version_str && doclen && document)
            {
                // Store initial document info
                strncpy(client_data.role, role, sizeof(client_data.role) - 1);
                client_data.version = strtol(version_str, NULL, 10);
                strncpy(client_data.document, document, sizeof(client_data.document) - 1);

                // Print initial document info
                printf("Connected as: %s\n", username);
                printf("Role: %s\n", role);
                printf("Document version: %s\n", version_str);
                printf("Document (%s bytes):\n%s\n", doclen, document);

                // Start reader thread to handle automatic updates
                pthread_t reader_tid;
                if (pthread_create(&reader_tid, NULL, reader_thread, &client_data) != 0)
                {
                    perror("Failed to create reader thread");
                    close(fd_c2s);
                    close(fd_s2c);
                    return -1;
                }

                // Start command processing loop
                char cmd[256];
                printf("\nEnter commands (q to quit):\n> ");
                while (fgets(cmd, sizeof(cmd), stdin))
                {
                    // Check for quit command
                    if (cmd[0] == 'q' && (cmd[1] == '\n' || cmd[1] == '\0'))
                        break;

                    // Send command to server
                    write(fd_c2s, cmd, strlen(cmd));

                    // Brief pause to let the reader thread receive the response
                    usleep(100000); // 100ms

                    printf("> ");
                }

                // Signal reader thread to exit and wait for it
                client_data.should_exit = 1;
                pthread_join(reader_tid, NULL);
            }
            else
            {
                // Fallback: print raw
                printf("%s", buf);
            }
        }
    }
    else
    {
        printf("Failed to read from server.\n");
    }

    // Close connection
    close(fd_c2s);
    close(fd_s2c);
    unlink(fifo_c2s);
    unlink(fifo_s2c);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <server_pid> <username>\n", argv[0]);
        exit(1);
    }
    connect_to_server(atoi(argv[1]), argv[2]);
    return 0;
}