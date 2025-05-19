#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "client.h"

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

    write(fd_c2s, username, strlen(username));
    write(fd_c2s, "\n", 1);

    char buf[256];
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
            char *version = strtok_r(NULL, "\n", &saveptr);
            char *doclen = strtok_r(NULL, "\n", &saveptr);
            char *document = saveptr;
            if (role && version && doclen && document)
            {
                // Print initial document info
                printf("Connected as: %s\n", username);
                printf("Role: %s\n", role);
                printf("Document version: %s\n", version);
                printf("Document (%s bytes):\n%s\n", doclen, document);

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

                    // Read response from server
                    n = read(fd_s2c, buf, sizeof(buf) - 1);
                    if (n > 0)
                    {
                        buf[n] = '\0';
                        // Process and display server response
                        printf("%s\n", buf);
                    }
                    else if (n <= 0)
                    {
                        printf("Server closed connection\n");
                        break;
                    }

                    printf("> ");
                }
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