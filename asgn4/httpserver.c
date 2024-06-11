#include <sys/socket.h>
#include <sys/stat.h>
#include <limits.h>
#include <fcntl.h>
#include <stdbool.h>
#include <err.h>
#include <errno.h>
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

// Constants and type definitions
#define BUFFER_SIZE 2048
typedef struct Request {
    const char *name;
} Request_t;

#define NUM_REQUESTS 3
const Request_t REQUEST_GET = { "GET" };
const Request_t REQUEST_PUT = { "PUT" };
const Request_t REQUEST_UNSUPPORTED = { "UNSUPPORTED" };
const Request_t *requests[NUM_REQUESTS] = { &REQUEST_GET, &REQUEST_PUT, &REQUEST_UNSUPPORTED };

const char *request_get_str(const Request_t *request) {
    return request->name;
}

typedef struct Response {
    uint16_t code;
    const char *message;
} Response_t;

const Response_t RESPONSE_OK = { 200, "OK" };
const Response_t RESPONSE_CREATED = { 201, "Created" };
const Response_t RESPONSE_BAD_REQUEST = { 400, "Bad Request" };
const Response_t RESPONSE_FORBIDDEN = { 403, "Forbidden" };
const Response_t RESPONSE_NOT_FOUND = { 404, "Not Found" };
const Response_t RESPONSE_INTERNAL_SERVER_ERROR = { 500, "Internal Server Error" };
const Response_t RESPONSE_NOT_IMPLEMENTED = { 501, "Not Implemented" };
const Response_t RESPONSE_VERSION_NOT_SUPPORTED = { 505, "HTTP Version Not Supported" };

uint16_t response_get_code(const Response_t *response) {
    return response->code;
}

const char *response_get_message(const Response_t *response) {
    return response->message;
}

pthread_mutex_t mutex;

typedef struct Conn conn_t;

// Function prototypes
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

// Function to create a new rwlock node
rwlockNode create_rwlock_node(char *uri, rwlock_t *rwlock) {
    rwlockNode node = malloc(sizeof(rwlockNodeObj));
    node->uri = strdup(uri);
    node->rwlock = rwlock;
    node->next = NULL;
    return node;
}

// Function to append a node to the rwlock hash table
void append_rwlock_node(rwlockHT hashTable, char *uri, rwlock_t *lock) {
    rwlockNodeObj *newNode = create_rwlock_node(uri, lock);
    if (hashTable->length == 0) {
        hashTable->head = newNode;
    } else {
        rwlockNodeObj *currNode = hashTable->head;
        for (; currNode->next != NULL; currNode = currNode->next) {
        }
        currNode->next = newNode;
    }
    hashTable->length++;
}

// Function to create a new lock hash table
rwlockHT create_lock_hash_table(void) {
    rwlockHT lockNode = malloc(sizeof(rwlockHTObj));
    lockNode->length = 0;
    lockNode->head = NULL;
    return lockNode;
}

// Function to free all nodes in the hash table
void free_rwlock_nodes(rwlockHT *lockNode) {
    for (rwlockNode node = (*lockNode)->head; node != NULL;) {
        rwlockNode nextNode = node->next;
        free(node->uri);
        free(node);
        node = nextNode;
    }
    free(*lockNode);
}

// Function to lookup a rwlock in the hash table linked list
rwlock_t *lookup_rwlock(rwlockNode head, char *uri) {
    rwlockNode node = head;
    while (node != NULL) {
        if (strcmp(node->uri, uri) == 0) {
            return node->rwlock;
        }
        node = node->next;
    }
    return NULL;
}

// Function to verify the request method
int verify_request_method(const char *str) {
    switch (str[0]) {
    case 'G':
        if (strncmp(str, "GET ", 4) == 0) {
            return 1;
        }
        break;
    case 'P':
        if (strncmp(str, "PUT ", 4) == 0) {
            return 2;
        }
        break;
    }
    return 0;
}

// Function to verify the HTTP version
int verify_http_version(const char *str) {
    return (strncmp(str, "HTTP/", 5) == 0) ? 1 : 0;
}

// Function to verify the version number
int verify_version_number(const char *str) {
    return (str[0] >= '0' && str[0] <= '9' && str[2] >= '0' && str[2] <= '9' && str[1] == '.'
               && str[3] == '\r' && str[4] == '\n')
               ? 1
               : 0;
}

// Function to check if a string contains only alphabetic characters
bool is_alphabetic(const char *str) {
    while (*str) {
        if ((*str >= 'a' && *str <= 'z') || (*str >= 'A' && *str <= 'Z') || *str == ' ')
            str++;
        else
            return false;
    }
    return true;
}

// Function to check if a string contains alphanumeric characters and some symbols
bool is_alphanumeric_plus(const char *str) {
    while (*str) {
        char current_char = *str;
        if ((current_char >= 'a' && current_char <= 'z')
            || (current_char >= 'A' && current_char <= 'Z')
            || (current_char >= '0' && current_char <= '9')
            || (current_char == '.' || current_char == '-' || current_char == ' '
                || current_char == ':')) {
            str++;
        } else {
            return false;
        }
    }
    return true;
}

// Thread worker function
void *worker_thread(void *arg) {
    Thread thread = (Thread) arg;
    queue_t *queue = thread->queue;
    while (1) {
        uintptr_t connfd = 0;
        queue_pop(queue, (void **) &connfd);
        handle_connection((int) connfd, *thread->rwlockHT);
        close((int) connfd);
    }
    return NULL;
}

int main(int argc, char **argv) {
    char *endptr = NULL;
    long port = strtol(argv[1], &endptr, 10);
    int t = 4;
    int opt;
    int manualThread = 0;

    // Parsing command line options
    for (; (opt = getopt(argc, argv, "t:")) != -1;) {
        if (opt == 't') {
            t = atoi(optarg);
            manualThread = 1;
        }
    }

    // Checking for valid port number
    if (argc < 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (manualThread)
        port = strtol(argv[3], &endptr, 10);
    else
        port = strtol(argv[1], &endptr, 10);

    if (port < 1 || port > 65535 || (endptr && *endptr != '\0')) {
        fprintf(stderr, "Invalid Port\n");
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);
    Listener_Socket sock;
    listener_init(&sock, (int) port);

    Thread threads[t];
    rwlockHT rwlock_ht = create_lock_hash_table();
    queue_t *queue = queue_new(t);

    // Creating threads
    for (int i = 0; i < t; i++) {
        threads[i] = malloc(sizeof(ThreadObj));
        threads[i]->id = i;
        threads[i]->rwlockHT = &rwlock_ht;
        threads[i]->queue = queue;
        pthread_create(&threads[i]->thread, NULL, worker_thread, threads[i]);
    }

    // Dispatcher thread to accept connections
    while (1) {
        uintptr_t connfd = listener_accept(&sock);
        queue_push(queue, (void *) connfd);
    }

    return EXIT_SUCCESS;
}

// Function to handle an incoming connection
void handle_connection(int connfd, rwlockHT rwlock_HT) {
    conn_t *conn = conn_new(connfd);
    const Response_t *res = conn_parse(conn);

    if (res != NULL) {
        conn_send_response(conn, res);
    } else {
        debug("%s", conn_str(conn));
        const Request_t *req = conn_get_request(conn);
        if (req == &REQUEST_GET) {
            handle_get(conn, rwlock_HT);
        } else if (req == &REQUEST_PUT) {
            handle_put(conn, rwlock_HT);
        } else {
            handle_unsupported(conn);
        }
    }

    conn_delete(&conn);
}

// Function to handle a GET request
void handle_get(conn_t *conn, rwlockHT rwlock_HT) {
    char *uri = conn_get_uri(conn);
    const Response_t *res = NULL;

    pthread_mutex_lock(&mutex);
    rwlock_t *lock = lookup_rwlock(rwlock_HT->head, uri);
    if (lock == NULL) {
        lock = rwlock_new(N_WAY, 1);
        append_rwlock_node(rwlock_HT, uri, lock);
    }
    pthread_mutex_unlock(&mutex);
    reader_lock(lock);

    if (!is_alphanumeric_plus(uri)) {
        res = &RESPONSE_BAD_REQUEST;
        char *req = conn_get_header(conn, "Request-Id");
        if (req == NULL)
            req = "0";
        fprintf(stderr, "GET,/%s,400,%s\n", uri, req);
        goto out;
    }

    int fd = open(uri, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            res = &RESPONSE_NOT_FOUND;
            char *req = conn_get_header(conn, "Request-Id");
            if (req == NULL)
                req = "0";
            fprintf(stderr, "GET,/%s,404,%s\n", uri, req);
            goto out;
        } else if (errno == EACCES || errno == EISDIR) {
            res = &RESPONSE_FORBIDDEN;
            char *req = conn_get_header(conn, "Request-Id");
            if (req == NULL)
                req = "0";
            fprintf(stderr, "GET,/%s,403,%s\n", uri, req);
            goto out;
        } else {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            char *req = conn_get_header(conn, "Request-Id");
            if (req == NULL)
                req = "0";
            fprintf(stderr, "GET,/%s,500,%s\n", uri, req);
            goto out;
        }
    }

    struct stat fileStat;
    fstat(fd, &fileStat);

    if (S_ISDIR(fileStat.st_mode)) {
        res = &RESPONSE_FORBIDDEN;
        char *req = conn_get_header(conn, "Request-Id");
        if (req == NULL)
            req = "0";
        fprintf(stderr, "GET,/%s,403,%s\n", uri, req);
        goto out;
    }

    int fileSize = (int) fileStat.st_size;
    res = conn_send_file(conn, fd, fileSize);

    if (res == NULL) {
        res = &RESPONSE_OK;
        char *req = conn_get_header(conn, "Request-Id");
        if (req == NULL)
            req = "0";
        fprintf(stderr, "GET,/%s,200,%s\n", uri, req);
    }

    reader_unlock(lock);
    close(fd);
    return;

out:
    conn_send_response(conn, res);
    reader_unlock(lock);
}

// Function to handle unsupported requests
void handle_unsupported(conn_t *conn) {
    debug("handling unsupported request");
    conn_send_response(conn, &RESPONSE_NOT_IMPLEMENTED);
}

// Function to handle a PUT request
void handle_put(conn_t *conn, rwlockHT rwlock_HT) {
    char *uri = conn_get_uri(conn);
    const Response_t *res = NULL;
    debug("handling put request for %s", uri);

    bool existed = access(uri, F_OK) == 0;
    debug("%s existed? %d", uri, existed);

    pthread_mutex_lock(&mutex);
    rwlock_t *lock = lookup_rwlock(rwlock_HT->head, uri);
    if (lock == NULL) {
        lock = rwlock_new(N_WAY, 1);
        append_rwlock_node(rwlock_HT, uri, lock);
    }
    pthread_mutex_unlock(&mutex);
    writer_lock(lock);

    int fd = open(uri, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) {
        debug("%s: %d", uri, errno);
        if (errno == EACCES || errno == EISDIR || errno == ENOENT) {
            res = &RESPONSE_FORBIDDEN;
            char *req = conn_get_header(conn, "Request-Id");
            if (req == NULL)
                req = "0";
            fprintf(stderr, "PUT,/%s,403,%s\n", uri, req);
            goto out;
        } else {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            char *req = conn_get_header(conn, "Request-Id");
            if (req == NULL)
                req = "0";
            fprintf(stderr, "PUT,/%s,500,%s\n", uri, req);
            goto out;
        }
    }

    res = conn_recv_file(conn, fd);

    if (res == NULL && existed) {
        res = &RESPONSE_OK;
        char *req = conn_get_header(conn, "Request-Id");
        if (req == NULL)
            req = "0";
        fprintf(stderr, "PUT,/%s,200,%s\n", uri, req);
    } else if (res == NULL && !existed) {
        res = &RESPONSE_CREATED;
        char *req = conn_get_header(conn, "Request-Id");
        if (req == NULL)
            req = "0";
        fprintf(stderr, "PUT,/%s,201,%s\n", uri, req);
    }

    writer_unlock(lock);
    close(fd);

out:
    conn_send_response(conn, res);
}
