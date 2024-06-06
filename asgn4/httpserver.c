#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "queue.h"
#include "rwlock.h"
#include "protocol.h"
#include "debug.h"

#define DEFAULT_THREADS 4
#define BUFFER_SIZE 1024

typedef struct {
    int client_socket;
    int request_id;
} client_request_t;

queue_t *request_queue;
rwlock_t *audit_log_lock;
int request_count = 0;

void *handle_request(void *arg);
void process_request(int client_socket, int request_id);
void log_audit(const char *method, const char *uri, int status_code, int request_id);

void *dispatcher_thread(void *arg) {
    int server_socket = *(int *)arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_socket;

    while ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len)) >= 0) {
        client_request_t *request = malloc(sizeof(client_request_t));
        request->client_socket = client_socket;
        request->request_id = __sync_add_and_fetch(&request_count, 1);
        queue_push(request_queue, request);
    }

    close(server_socket);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = 0;
    int threads = DEFAULT_THREADS;
    int opt;

    while ((opt = getopt(argc, argv, "t:")) != -1) {
        switch (opt) {
            case 't':
                threads = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-t threads] <port>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Expected port number after options\n");
        exit(EXIT_FAILURE);
    }

    port = atoi(argv[optind]);

    // Create server socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) < 0) {
        perror("listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Initialize queue and locks
    request_queue = queue_new(100);
    audit_log_lock = rwlock_new(READERS, 0);

    pthread_t dispatcher;
    pthread_t *workers = malloc(threads * sizeof(pthread_t));

    // Create dispatcher thread
    pthread_create(&dispatcher, NULL, dispatcher_thread, &server_socket);

    // Create worker threads
    for (int i = 0; i < threads; i++) {
        pthread_create(&workers[i], NULL, handle_request, NULL);
    }

    // Wait for dispatcher thread to finish
    pthread_join(dispatcher, NULL);

    // Cleanup
    for (int i = 0; i < threads; i++) {
        pthread_join(workers[i], NULL);
    }

    queue_delete(&request_queue);
    rwlock_delete(&audit_log_lock);
    free(workers);

    return 0;
}

void *handle_request(void *arg) {
    (void)arg;  // Mark parameter as unused to avoid warning

    client_request_t *request;

    while (queue_pop(request_queue, (void **)&request)) {
        process_request(request->client_socket, request->request_id);
        close(request->client_socket);
        free(request);
    }

    return NULL;
}

void process_request(int client_socket, int request_id) {
    char buffer[BUFFER_SIZE];
    int bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        perror("read");
        return;
    }

    buffer[bytes_read] = '\0';

    // Parse the request (for simplicity, we assume it is a valid HTTP request)
    char method[8], uri[64], version[16];
    sscanf(buffer, "%s %s %s", method, uri, version);

    // Process the request and generate the response
    char response[BUFFER_SIZE];
    int status_code = 200;

    if (strcmp(method, "GET") == 0) {
        snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
    } else if (strcmp(method, "PUT") == 0) {
        snprintf(response, sizeof(response), "HTTP/1.1 201 Created\r\nContent-Length: 7\r\n\r\nCreated");
    } else {
        status_code = 400;
        snprintf(response, sizeof(response), "HTTP/1.1 400 Bad Request\r\nContent-Length: 11\r\n\r\nBad Request");
    }

    // Write the response
    write(client_socket, response, strlen(response));

    // Log the request
    log_audit(method, uri, status_code, request_id);
}

void log_audit(const char *method, const char *uri, int status_code, int request_id) {
    writer_lock(audit_log_lock);
    fprintf(stderr, "%s,%s,%d,%d\n", method, uri, status_code, request_id);
    writer_unlock(audit_log_lock);
}

