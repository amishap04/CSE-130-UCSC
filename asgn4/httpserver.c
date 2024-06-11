#include <sys/socket.h>
#include <sys/stat.h>
#include <limits.h>
#include <sys/fcntl.h>
#include <stdbool.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include "debug.h"
#include "queue.h"
#include "rwlock.h"
#include "asgn2_helper_funcs.h"

#define BUFFER_SIZE 2048

typedef struct Request {
    char method[12];
} Request_t;

typedef struct Response {
    uint16_t code;
    char message[64];
} Response_t;

#define NUM_REQUESTS 3

const Request_t REQUEST_GET = { .method = "GET" };
const Request_t REQUEST_PUT = { .method = "PUT" };
const Request_t REQUEST_UNSUPPORTED = { .method = "UNSUPPORTED" };
const Request_t *requests[NUM_REQUESTS] = { &REQUEST_GET, &REQUEST_PUT, &REQUEST_UNSUPPORTED };
const Response_t RESPONSE_OK = { .code = 200, .message = "OK" };
const Response_t RESPONSE_CREATED = { .code = 201, .message = "Created" };
const Response_t RESPONSE_BAD_REQUEST = { .code = 400, .message = "Bad Request" };
const Response_t RESPONSE_FORBIDDEN = { .code = 403, .message = "Forbidden" };
const Response_t RESPONSE_NOT_FOUND = { .code = 404, .message = "Not Found" };
const Response_t RESPONSE_INTERNAL_SERVER_ERROR
    = { .code = 500, .message = "Internal Server Error" };
const Response_t RESPONSE_NOT_IMPLEMENTED = { .code = 501, .message = "Not Implemented" };
const Response_t RESPONSE_VERSION_NOT_SUPPORTED
    = { .code = 505, .message = "Version Not Supported" };

const char *request_get_str(const Request_t *request) {
    return request->method;
}

uint16_t response_get_code(const Response_t *response) {
    return response->code;
}

const char *response_get_message(const Response_t *response) {
    return response->message;
}

pthread_mutex_t rwlock_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct Conn conn_t;

conn_t *conn_new(int connfd);
void conn_delete(conn_t **conn);
const Response_t *conn_parse(conn_t *conn);
const Request_t *conn_get_request(conn_t *conn);
char *conn_get_uri(conn_t *conn);
char *conn_get_header(conn_t *conn, char *header);
const Response_t *conn_recv_file(conn_t *conn, int fd);
const Response_t *conn_send_file(conn_t *conn, int fd, uint64_t count);
const Response_t *conn_send_response(conn_t *conn, const Response_t *res);
char *conn_str(conn_t *conn);

typedef struct rwlockNodeObj *rwlockNode;
typedef struct rwlockNodeObj {
    char *uri;
    rwlock_t *rwlock;
    rwlockNode next;
} rwlockNodeObj;

typedef struct rwlockHTObj *rwlockHT;
typedef struct rwlockHTObj {
    rwlockNode head;
    int length;
} rwlockHTObj;

typedef struct ThreadObj *Thread;
typedef struct ThreadObj {
    pthread_t thread;
    int id;
    rwlockHT *rwlockHT;
    queue_t *queue;
} ThreadObj;

void handle_connection(int, rwlockHT);
void handle_get(conn_t *, rwlockHT);
void handle_put(conn_t *, rwlockHT);
void handle_unsupported(conn_t *);

rwlockNode create_rwlock_node(char *uri, rwlock_t *rwlock) {
    rwlockNode newNode;
    char *duplicateUri;
    rwlockNodeObj *rwlockObj;

    rwlockObj = malloc(sizeof(rwlockNodeObj));
    duplicateUri = strdup(uri);

    newNode = rwlockObj;
    newNode->uri = duplicateUri;
    newNode->rwlock = rwlock;
    newNode->next = NULL;

    return newNode;
}

void add_node_to_ht(rwlockHT table, char *uri, rwlock_t *lock) {
    if (table->length == 0) {
        rwlockNode firstNode = create_rwlock_node(uri, lock);
        table->head = firstNode;
        table->length++;
    } else {
        rwlockNode iterator = table->head;
        for (; iterator->next != NULL; iterator = iterator->next)
            ;
        rwlockNode newNode = create_rwlock_node(uri, lock);
        iterator->next = newNode;
        table->length++;
    }
}

rwlockHT create_rwlock_ht(void) {
    rwlockHT lockNode = malloc(sizeof(rwlockHTObj));
    lockNode->length = 0;
    lockNode->head = NULL;
    return lockNode;
}

void free_rwlock_ht(rwlockHT *ht) {
    if (ht == NULL || *ht == NULL) {
        return;
    }
    rwlockNode current = (*ht)->head;
    while (current) {
        rwlockNode temp = current;
        current = current->next;
        free(temp->uri);
        free(temp);
    }
    free(*ht);
    *ht = NULL;
}

rwlock_t *lookup_rwlock(rwlockNode head, char *uri) {
    if (!head) {
        return NULL;
    }

    rwlockNode current = head;
    for (rwlockNode iter = current; iter != NULL; iter = iter->next) {
        if (strcmp(iter->uri, uri) == 0) {
            return iter->rwlock;
        }
    }

    return NULL;
}

int verify_method(const char *method) {
    const char *get_method = "GET ";
    const char *put_method = "PUT ";

    for (int i = 0; i < 4; ++i) {
        if (method[i] != get_method[i]) {
            break;
        }
        if (i == 3) {
            return 1;
        }
    }

    for (int j = 0; j < 4; ++j) {
        if (method[j] != put_method[j]) {
            break;
        }
        if (j == 3) {
            return 2;
        }
    }

    return 0;
}

int verify_http_version(const char *str) {
    if (strncmp(str, "HTTP/", 5) == 0)
        return 1;
    else
        return 0;
}

int verify_version_number(const char *version_str) {
    const char *ptr = version_str;

    if (!((ptr[0] >= '0' && ptr[0] <= '9') && (ptr[2] >= '0' && ptr[2] <= '9'))) {
        return 0;
    }
    if (ptr[1] != '.') {
        return 0;
    }
    if (ptr[3] != '\r') {
        return 0;
    }
    if (ptr[4] != '\n') {
        return 0;
    }

    return 1;
}

bool check_alphabetic(const char *input_str) {
    const char *char_ptr = input_str;

    for (;;) {
        char current = *char_ptr;
        if (current == '\0') {
            break;
        }

        if ((current >= 'a' && current <= 'z') || (current >= 'A' && current <= 'Z')
            || current == ' ') {
            char_ptr++;
        } else {
            return false;
        }
    }

    return true;
}

bool check_alphanumeric(const char *input) {
    const char *ptr = input;

    for (; *ptr != '\0'; ++ptr) {
        char character = *ptr;
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9') || (character == '.' || character == '-')
            || (character == ' ') || (character == ':')) {
            continue;
        } else {
            return false;
        }
    }

    return true;
}

void *thread_worker(void *arg) {
    Thread thread_info = (Thread) arg;
    queue_t *task_queue = thread_info->queue;

    for (;;) {
        uintptr_t connection_fd = 0;
        queue_pop(task_queue, (void **) &connection_fd);

        handle_connection((int) connection_fd, *thread_info->rwlockHT);
        close((int) connection_fd);
    }

    return NULL;
}

int main(int argc, char **argv) {
    char *end_pointer = NULL;
    long port_number = strtol(argv[1], &end_pointer, 10);
    int thread_count = 4;
    int option;
    int custom_thread_count = 0;

    while ((option = getopt(argc, argv, "t:")) != -1) {
        switch (option) {
        case 't':
            thread_count = atoi(optarg);
            custom_thread_count = 1;
            break;
        default: break;
        }
    }

    if (argc < 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (custom_thread_count)
        port_number = strtol(argv[3], &end_pointer, 10);
    else
        port_number = strtol(argv[1], &end_pointer, 10);

    if (port_number < 1 || port_number > 65535 || (end_pointer && *end_pointer != '\0')) {
        fprintf(stderr, "Invalid Port\n");
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);
    Listener_Socket listener_socket;
    listener_init(&listener_socket, (int) port_number);

    Thread worker_threads[thread_count];
    rwlockHT rwlock_hash_table = create_rwlock_ht();
    queue_t *request_queue = queue_new(thread_count);

    for (int thread_index = 0; thread_index < thread_count; thread_index++) {
        worker_threads[thread_index] = malloc(sizeof(ThreadObj));
        worker_threads[thread_index]->id = thread_index;
        worker_threads[thread_index]->rwlockHT = &rwlock_hash_table;
        worker_threads[thread_index]->queue = request_queue;
        pthread_create(&worker_threads[thread_index]->thread, NULL, thread_worker,
            worker_threads[thread_index]);
    }

    while (1) {
        uintptr_t connection_fd = listener_accept(&listener_socket);
        queue_push(request_queue, (void *) connection_fd);
    }

    return EXIT_SUCCESS;
}

void handle_connection(int client_fd, rwlockHT rwlock_HT) {
    conn_t *connection = conn_new(client_fd);

    const Response_t *response = conn_parse(connection);
    if (response != NULL) {
        conn_send_response(connection, response);
    } else {
        const Request_t *request = conn_get_request(connection);
        if (strcmp(request->method, "GET") == 0) {
            handle_get(connection, rwlock_HT);
        } else if (strcmp(request->method, "PUT") == 0) {
            handle_put(connection, rwlock_HT);
        } else {
            handle_unsupported(connection);
        }
    }

    conn_delete(&connection);
}

void handle_get(conn_t *connection, rwlockHT hashtable) {
    char *resource_uri = conn_get_uri(connection);
    const Response_t *response = NULL;
    pthread_mutex_lock(&rwlock_mutex);
    rwlock_t *rw_lock = lookup_rwlock(hashtable->head, resource_uri);
    if (rw_lock == NULL) {
        rw_lock = rwlock_new(N_WAY, 1);
        add_node_to_ht(hashtable, resource_uri, rw_lock);
    }
    pthread_mutex_unlock(&rwlock_mutex);
    reader_lock(rw_lock);

    if (!check_alphanumeric(resource_uri)) {
        response = &RESPONSE_BAD_REQUEST;
        char *request_id = conn_get_header(connection, "Request-Id");
        if (request_id == NULL) {
            request_id = "0";
        }
        fprintf(stderr, "GET,/%s,400,%s\n", resource_uri, request_id);
        goto finalize;
    }

    int file_desc = open(resource_uri, O_RDONLY);
    if (file_desc < 0) {
        char *request_id = conn_get_header(connection, "Request-Id");
        if (request_id == NULL) {
            request_id = "0";
        }

        switch (errno) {
        case ENOENT:
            response = &RESPONSE_NOT_FOUND;
            fprintf(stderr, "GET,/%s,404,%s\n", resource_uri, request_id);
            break;
        case EACCES:
        case EISDIR:
            response = &RESPONSE_FORBIDDEN;
            fprintf(stderr, "GET,/%s,403,%s\n", resource_uri, request_id);
            break;
        default:
            response = &RESPONSE_INTERNAL_SERVER_ERROR;
            fprintf(stderr, "GET,/%s,500,%s\n", resource_uri, request_id);
            break;
        }
        goto finalize;
    }

    struct stat file_info;
    fstat(file_desc, &file_info);
    if (S_ISDIR(file_info.st_mode)) {
        response = &RESPONSE_FORBIDDEN;
        char *request_id = conn_get_header(connection, "Request-Id");
        if (request_id == NULL) {
            request_id = "0";
        }
        fprintf(stderr, "GET,/%s,403,%s\n", resource_uri, request_id);
        goto finalize;
    }

    int file_size = (int) file_info.st_size;
    response = conn_send_file(connection, file_desc, file_size);
    if (response == NULL) {
        response = &RESPONSE_OK;
        char *request_id = conn_get_header(connection, "Request-Id");
        if (request_id == NULL) {
            request_id = "0";
        }
        fprintf(stderr, "GET,/%s,200,%s\n", resource_uri, request_id);
    }

    reader_unlock(rw_lock);
    close(file_desc);
    return;

finalize:
    conn_send_response(connection, response);
    reader_unlock(rw_lock);
}

void handle_put(conn_t *connection, rwlockHT hashTable) {
    char *resource_uri = conn_get_uri(connection);
    const Response_t *response = NULL;
    bool resource_exists = access(resource_uri, F_OK) == 0;
    pthread_mutex_lock(&rwlock_mutex);
    rwlock_t *rw_lock = lookup_rwlock(hashTable->head, resource_uri);
    if (rw_lock == NULL) {
        rw_lock = rwlock_new(N_WAY, 1);
        add_node_to_ht(hashTable, resource_uri, rw_lock);
    }
    pthread_mutex_unlock(&rwlock_mutex);
    writer_lock(rw_lock);

    int file_descriptor = open(resource_uri, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (file_descriptor < 0) {
        int error_code = errno;
        char *request_id = conn_get_header(connection, "Request-Id");
        if (request_id == NULL) {
            request_id = "0";
        }

        switch (error_code) {
        case EACCES:
        case EISDIR:
        case ENOENT:
            response = &RESPONSE_FORBIDDEN;
            fprintf(stderr, "PUT,/%s,403,%s\n", resource_uri, request_id);
            goto cleanup;
        default:
            response = &RESPONSE_INTERNAL_SERVER_ERROR;
            fprintf(stderr, "PUT,/%s,500,%s\n", resource_uri, request_id);
            goto cleanup;
        }
    }

    response = conn_recv_file(connection, file_descriptor);
    if (response == NULL) {
        char *request_id = conn_get_header(connection, "Request-Id");
        if (request_id == NULL) {
            request_id = "0";
        }

        if (resource_exists) {
            response = &RESPONSE_OK;
            fprintf(stderr, "PUT,/%s,200,%s\n", resource_uri, request_id);
        } else {
            response = &RESPONSE_CREATED;
            fprintf(stderr, "PUT,/%s,201,%s\n", resource_uri, request_id);
        }
    }

    writer_unlock(rw_lock);
    close(file_descriptor);

cleanup:
    conn_send_response(connection, response);
}

void handle_unsupported(conn_t *conn) {
    conn_send_response(conn, &RESPONSE_NOT_IMPLEMENTED);
}
