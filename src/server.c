#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include "server.h"
#include "document.h"
#include "protocol.h"
#include <stdbool.h>

// Define real-time signals if not available
#ifndef SIGRTMIN
#define SIGRTMIN 34
#endif

#define MAX_CLIENTS 10

// Structure to hold client information
typedef struct
{
    int pid;
    int fd_s2c; // Server to client file descriptor
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
static int time_interval = 30; // Default interval in seconds

// Cursor position tracking
typedef struct
{
    int client_index;
    int cursor_pos;
} cursor_position_t;
static cursor_position_t cursor_positions[MAX_CLIENTS];

// Function to adjust cursors after a document edit
void adjust_cursors(int edit_pos, int len_change)
{
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].connected)
        {
            // If cursor is after edit position, adjust it
            if (cursor_positions[i].cursor_pos > edit_pos)
            {
                cursor_positions[i].cursor_pos += len_change;
                if (cursor_positions[i].cursor_pos < 0)
                    cursor_positions[i].cursor_pos = 0;
            }
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

// Forward declarations
void broadcast_document_update(const char *username, const char *command, const char *response);
void timed_broadcast(int signum);
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

    if (client_index == -1)
    {
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

        // Remove trailing newline if present
        size_t cmd_len = strlen(cmd);
        if (cmd_len > 0 && cmd[cmd_len - 1] == '\n')
        {
            cmd[cmd_len - 1] = '\0';
        }

        printf("Received command from %s: %s\n", username, cmd);

        // Create response buffer
        char response[512] = {0};

        // Execute the command if permissions allow
        if (strncmp(cmd, "i ", 2) == 0 || strncmp(cmd, "d ", 2) == 0 ||
            strncmp(cmd, "BOLD", 4) == 0 || strncmp(cmd, "ITALIC", 6) == 0 ||
            strncmp(cmd, "HEADING", 7) == 0 || strncmp(cmd, "LIST", 4) == 0)
        {
            // These commands require write permission
            if (strcmp(role, "write") == 0)
            {
                pthread_mutex_lock(&doc_mutex);
                bool success = process_command(cmd, username, role, response, sizeof(response));
                if (success)
                {
                    // Increase version only for successful write operations
                    version++;
                }
                pthread_mutex_unlock(&doc_mutex);

                // Broadcast to all clients
                broadcast_document_update(username, cmd, success ? "SUCCESS" : response);
            }
            else
            {
                // Read-only user tried to modify document
                snprintf(response, sizeof(response),
                         "Reject UNAUTHORISED %c write read\n", cmd[0]);
                write(fd_s2c, response, strlen(response));
            }
        }
        else
        {
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

// Timer signal handler for broadcasting document updates at regular intervals
void timed_broadcast(int signum)
{
    (void)signum; // Suppress unused parameter warning

    pthread_mutex_lock(&doc_mutex);
    // Increment the version to signal a new document state
    version++;

    // Broadcast the updated document to all clients
    char *docstr;
    size_t doclen;
    document_serialize(doc, &docstr, &doclen);

    // Create update message for periodic broadcast
    char header[128];
    snprintf(header, sizeof(header), "VERSION %lu\nAUTO_UPDATE\nEND\n", version);

    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].connected)
        {
            // Send timed update to each connected client
            write(clients[i].fd_s2c, header, strlen(header));
            send_document(clients[i].fd_s2c, clients[i].role, version, docstr, doclen);
        }
    }
    pthread_mutex_unlock(&client_mutex);

    free(docstr);
    pthread_mutex_unlock(&doc_mutex);
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
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].connected)
        {
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
    if (strncmp(cmd, "i ", 2) == 0)
    {
        if (!has_write_permission(role))
        {
            snprintf(response, resp_size, "Reject UNAUTHORISED INSERT write read");
            return false;
        }

        int position;
        char text[256];
        if (sscanf(cmd, "i %d %[^\n]", &position, text) == 2)
        {
            if (position < 0 || position > (int)doc->length)
            {
                snprintf(response, resp_size, "Reject INVALID_POSITION");
                return false;
            }

            document_insert(doc, position, text);
            adjust_cursors(position, strlen(text));
            return true;
        }
    }

    // Check for delete command: d <position> <count>
    else if (strncmp(cmd, "d ", 2) == 0)
    {
        if (!has_write_permission(role))
        {
            snprintf(response, resp_size, "Reject UNAUTHORISED DELETE write read");
            return false;
        }

        int position, count;
        if (sscanf(cmd, "d %d %d", &position, &count) == 2)
        {
            if (position < 0 || position >= (int)doc->length)
            {
                snprintf(response, resp_size, "Reject INVALID_POSITION");
                return false;
            }

            document_delete(doc, position, count);
            adjust_cursors(position, -count);
            return true;
        }
    }

    // Check for BOLD command
    else if (strncmp(cmd, "BOLD ", 5) == 0)
    {
        if (!has_write_permission(role))
        {
            snprintf(response, resp_size, "Reject UNAUTHORISED BOLD write read");
            return false;
        }

        int position, length;
        if (sscanf(cmd, "BOLD %d %d", &position, &length) == 2)
        {
            if (position < 0 || position >= (int)doc->length ||
                position + length > (int)doc->length)
            {
                snprintf(response, resp_size, "Reject INVALID_POSITION");
                return false;
            }

            // Get the text to be made bold
            char *docstr;
            size_t doclen;
            document_serialize(doc, &docstr, &doclen);

            // Extract the text to be made bold
            char text_to_bold[256];
            strncpy(text_to_bold, docstr + position, length);
            text_to_bold[length] = '\0';

            // Delete the original text
            document_delete(doc, position, length);

            // Create bold text with ** markers
            char bold_text[512];
            snprintf(bold_text, sizeof(bold_text), "**%s**", text_to_bold);

            // Insert the bold text
            document_insert(doc, position, bold_text);

            free(docstr);
            return true;
        }
    }

    // Check for ITALIC command
    else if (strncmp(cmd, "ITALIC ", 7) == 0)
    {
        if (!has_write_permission(role))
        {
            snprintf(response, resp_size, "Reject UNAUTHORISED ITALIC write read");
            return false;
        }

        int position, length;
        if (sscanf(cmd, "ITALIC %d %d", &position, &length) == 2)
        {
            if (position < 0 || position >= (int)doc->length ||
                position + length > (int)doc->length)
            {
                snprintf(response, resp_size, "Reject INVALID_POSITION");
                return false;
            }

            // Get the text to be made italic
            char *docstr;
            size_t doclen;
            document_serialize(doc, &docstr, &doclen);

            // Extract the text to be made italic
            char text_to_italic[256];
            strncpy(text_to_italic, docstr + position, length);
            text_to_italic[length] = '\0';

            // Delete the original text
            document_delete(doc, position, length);

            // Create italic text with * markers
            char italic_text[512];
            snprintf(italic_text, sizeof(italic_text), "*%s*", text_to_italic);

            // Insert the italic text
            document_insert(doc, position, italic_text);

            free(docstr);
            return true;
        }
    }

    // Check for HEADING command
    else if (strncmp(cmd, "HEADING ", 8) == 0)
    {
        if (!has_write_permission(role))
        {
            snprintf(response, resp_size, "Reject UNAUTHORISED HEADING write read");
            return false;
        }

        int level, position, length;
        if (sscanf(cmd, "HEADING %d %d %d", &level, &position, &length) == 3)
        {
            if (level < 1 || level > 6)
            {
                snprintf(response, resp_size, "Reject INVALID_HEADING_LEVEL");
                return false;
            }

            if (position < 0 || position >= (int)doc->length ||
                position + length > (int)doc->length)
            {
                snprintf(response, resp_size, "Reject INVALID_POSITION");
                return false;
            }

            // Get the text to be made heading
            char *docstr;
            size_t doclen;
            document_serialize(doc, &docstr, &doclen);

            // Extract the text to be made heading
            char text_to_heading[256];
            strncpy(text_to_heading, docstr + position, length);
            text_to_heading[length] = '\0';

            // Delete the original text
            document_delete(doc, position, length);

            // Create heading text with # markers
            char heading_text[512];
            char hashes[7] = "######"; // Max 6 #'s
            hashes[level] = '\0';      // Truncate to required level

            snprintf(heading_text, sizeof(heading_text), "%s %s", hashes, text_to_heading);

            // Insert the heading text
            document_insert(doc, position, heading_text);

            free(docstr);
            return true;
        }
    }

    // Check for LIST command (ordered or unordered)
    else if (strncmp(cmd, "LIST ", 5) == 0)
    {
        if (!has_write_permission(role))
        {
            snprintf(response, resp_size, "Reject UNAUTHORISED LIST write read");
            return false;
        }

        char type;
        int position, count;
        if (sscanf(cmd, "LIST %c %d %d", &type, &position, &count) == 3)
        {
            if (position < 0 || position >= (int)doc->length)
            {
                snprintf(response, resp_size, "Reject INVALID_POSITION");
                return false;
            }

            // Get the document text
            char *docstr;
            size_t doclen;
            document_serialize(doc, &docstr, &doclen);

            // Find line starts based on position
            int line_starts[100]; // Assuming max 100 lines for simplicity
            int line_count = 0;

            // Find start of current line
            int curr_pos = position;
            while (curr_pos > 0 && docstr[curr_pos - 1] != '\n')
                curr_pos--;

            line_starts[line_count++] = curr_pos;

            // Find next 'count-1' line starts
            curr_pos = position;
            while (line_count < count && curr_pos < (int)doclen)
            {
                if (docstr[curr_pos] == '\n')
                    line_starts[line_count++] = curr_pos + 1;
                curr_pos++;
            }

            // Apply list formatting to each line
            for (int i = 0; i < line_count; i++)
            {
                int line_pos = line_starts[i];

                // Create list marker
                char marker[10];
                if (type == 'O' || type == 'o') // Ordered list
                    snprintf(marker, sizeof(marker), "%d. ", i + 1);
                else // Unordered list
                    strcpy(marker, "- ");

                // Insert marker at line start
                document_insert(doc, line_pos, marker);

                // Update other line positions after insertion
                for (int j = i + 1; j < line_count; j++)
                    line_starts[j] += strlen(marker);
            }

            free(docstr);
            return true;
        }
    }

    // Check for DOC? command
    else if (strcmp(cmd, "DOC?\n") == 0 || strcmp(cmd, "DOC?") == 0)
    {
        char *docstr;
        size_t doclen;
        document_serialize(doc, &docstr, &doclen);
        snprintf(response, resp_size, "VERSION %lu\nDOCUMENT (%zu bytes):\n%s",
                 version, doclen, docstr);
        free(docstr);
        return true;
    }

    // Check for PERM? command
    else if (strcmp(cmd, "PERM?\n") == 0 || strcmp(cmd, "PERM?") == 0)
    {
        snprintf(response, resp_size, "PERMISSIONS %s: %s", username, role);
        return true;
    }

    // Check for LOG? command
    else if (strcmp(cmd, "LOG?\n") == 0 || strcmp(cmd, "LOG?") == 0)
    {
        snprintf(response, resp_size, "Connected clients:\n");

        pthread_mutex_lock(&client_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].connected)
            {
                char client_info[128];
                snprintf(client_info, sizeof(client_info),
                         "- Client %d: %s (%s)\n",
                         i, clients[i].username, clients[i].role);
                strncat(response, client_info, resp_size - strlen(response) - 1);
            }
        }
        pthread_mutex_unlock(&client_mutex);

        return true;
    }

    // Check for QUIT command - save document and exit
    else if (strcmp(cmd, "QUIT\n") == 0 || strcmp(cmd, "QUIT") == 0)
    {
        if (!has_write_permission(role))
        {
            snprintf(response, resp_size, "Reject UNAUTHORISED QUIT write read");
            return false;
        }

        // Serialize the document
        char *docstr;
        size_t doclen;
        document_serialize(doc, &docstr, &doclen);

        // Save to doc.md
        FILE *f = fopen("doc.md", "w");
        if (f)
        {
            fwrite(docstr, 1, doclen, f);
            fclose(f);
            snprintf(response, resp_size, "Document saved to doc.md. Server shutting down.");

            // Schedule server shutdown
            // We'll exit after sending the response
            pthread_t shutdown_thread;
            pthread_create(&shutdown_thread, NULL, (void *(*)(void *))exit, (void *)0);

            free(docstr);
            return true;
        }
        else
        {
            snprintf(response, resp_size, "Failed to save document");
            free(docstr);
            return false;
        }
    }

    // Check for empty command or just whitespace
    else if (cmd[0] == '\0' || strspn(cmd, " \t\n\r") == strlen(cmd))
    {
        // Just return a simple OK for empty commands
        snprintf(response, resp_size, "OK");
        return true;
    }

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

    // Parse time interval from argument
    time_interval = atoi(argv[1]);
    if (time_interval <= 0)
    {
        fprintf(stderr, "TIME_INTERVAL must be a positive integer\n");
        exit(1);
    }

    printf("Server PID: %d\n", getpid());
    doc = document_create();

    // Set up signal handler for client connections
    struct sigaction sa = {0};
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sigrtmin_handler;
    sigaction(SIGRTMIN, &sa, NULL);

    // Set up timer for periodic broadcasts
    struct sigaction sa_timer = {0};
    sa_timer.sa_handler = timed_broadcast;
    sigaction(SIGALRM, &sa_timer, NULL);

    // Configure timer interval
    struct itimerval timer;
    timer.it_value.tv_sec = time_interval;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = time_interval;
    timer.it_interval.tv_usec = 0;

    // Start the timer
    setitimer(ITIMER_REAL, &timer, NULL);

    while (1)
        pause();

    document_free(doc);
    return 0;
}