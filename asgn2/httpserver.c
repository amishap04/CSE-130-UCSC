#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "asgn2_helper_funcs.h"
#include <sys/time.h>

#define BUFF_SIZE 4096

int64_t currentTimeMillis() {
    struct timeval time;
    gettimeofday(&time, NULL);
    int64_t s1 = (int64_t) (time.tv_sec) * 1000;
    int64_t s2 = (time.tv_usec / 1000);
    return s1 + s2;
}

void handle_get_request(int client_fd, const char *uri);
void handle_put_request(int client_fd, const char *uri, int content_length);
int check_headers(const char *buffer);

int listener_init(Listener_Socket *sock, int port) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    printf("%ld start", currentTimeMillis());

    if ((sock->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }

    printf("%ld here", currentTimeMillis());

    if (bind(sock->fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        return -1;
    }

    printf("%ld here now", currentTimeMillis());

    if (listen(sock->fd, 128) < 0) {
        return -1;
    }

    printf("%ld final", currentTimeMillis());

    return 0;
}

int listener_accept(Listener_Socket *sock) {
    int connfd = accept(sock->fd, NULL, NULL);
    return connfd;
}

void handle_connection(int connfd) {
    char buf[BUFF_SIZE + 1];
    write_n_bytes(connfd, buf, BUFF_SIZE);
    buf[BUFF_SIZE] = '\0';
    printf("%s", buf);

    close(connfd);
    return;
}

ssize_t find_length(const char *path) {
    struct stat file;
    stat(path, &file);
    return file.st_size;
}

void handle_request_based_on_method(
    int client_fd, const char *method, const char *uri, const char *version, const char *buffer) {
    if (strcmp(version, "HTTP/1.1") != 0) {
        if (strcmp(version, "HTTP/1.10") == 0 || strcmp(version, "HTTP/1.0") == 0) {
            fprintf(stderr, "here loser 4\n");
            write_n_bytes(client_fd,
                "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n", 60);
        } else {
            write_n_bytes(client_fd,
                "HTTP/1.1 505 Version Not Supported\r\nContent-Length: 22\r\n\r\nVersion Not "
                "Supported\n",
                80);
        }
        return;
    }

    if (strcmp(method, "GET") == 0 || strcmp(method, "PUT") == 0) {
        if (strcmp(method, "GET") == 0) {
            handle_get_request(client_fd, uri);
        } else {
            char *content_length_str = strstr(buffer, "Content-Length: ");
            if (!content_length_str) {
                fprintf(stderr, "here loser 1\n");
                write_n_bytes(client_fd,
                    "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n", 60);
                return;
            }
            int content_length = 0;
            sscanf(content_length_str, "Content-Length: %d", &content_length);
            handle_put_request(client_fd, uri, content_length);
        }
    } else {
        if (strstr(method, "GET")) {
            write_n_bytes(client_fd,
                "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n", 60);
            return;
        }
        write_n_bytes(client_fd,
            "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n", 68);
    }
}

int check_headers(const char *buffer) {
    const char *ptr = buffer;
    while ((ptr = strstr(ptr, "\r\n")) != NULL) {
        ptr += 2;
        if (strncmp(ptr, "\r\n", 2) == 0)
            break;

        const char *colon = strchr(ptr, ':');
        fprintf(stderr, "%s", colon);
        if (colon == NULL) {
            return -1;
        }
    }
    return 0;
}

ssize_t my_read_until(int fd, char buf[], size_t n) {
    size_t bytesRead = 0;
    size_t totalRead = 0;
    char *bufPtr = buf;

    while (totalRead < n) {
        bytesRead = read(fd, bufPtr, 1);
        if (bytesRead == -1ul) {
            if (errno == EINTR) {
                bytesRead = 0;
            } else {
                return -1;
            }
        } else if (bytesRead == 0) {
            break;
        } else {
            totalRead += bytesRead;
            bufPtr += bytesRead;
            if (totalRead >= 2) {
                if ((memcmp(bufPtr - 2, "\n\n", 2) == 0) || (memcmp(bufPtr - 2, "\r\r", 2) == 0)) {
                    break;
                }
            }
            if (totalRead >= 4) {
                if (memcmp(bufPtr - 4, "\r\n\r\n", 4) == 0) {
                    break;
                }
            }
        }
    }
    if (totalRead < n) {
        *bufPtr = '\0';
    }

    return totalRead;
}

int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Usage: ./httpserver <port>\n");
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid Port\n");
        return 1;
    }

    Listener_Socket listener;
    if (listener_init(&listener, port) != 0) {
        fprintf(stderr, "Invalid Port\n");
        return 1;
    }

    while (1) {
        int client_fd = listener_accept(&listener);
        if (client_fd < 0) {
            perror("Failed to accept connection");
            continue;
        }

        char buffer[BUFF_SIZE];
        ssize_t bytes_read = my_read_until(client_fd, buffer, BUFF_SIZE);
        if (bytes_read <= 0) {
            perror("Failed to read from socket");
            close(client_fd);
            continue;
        }

        buffer[bytes_read]
            = '\0';

        if (check_headers(buffer) != 0) {
            write_n_bytes(client_fd,
                "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n", 60);
            close(client_fd);
            continue;
        }

        char method[10], uri[256], version[10];
        sscanf(buffer, "%s %s %s", method, uri, version);

        handle_request_based_on_method(client_fd, method, uri, version, buffer);

        close(client_fd);
    }

    return 0;
}

void handle_get_request(int client_fd, const char *uri) {
    char filepath[256];
    strcpy(filepath, uri + 1);

    struct stat path_stat;
    if (stat(filepath, &path_stat) < 0) {
        write_n_bytes(
            client_fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n", 62);
        return;
    }

    if (S_ISDIR(path_stat.st_mode)) {
        write_n_bytes(
            client_fd, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n", 62);
        return;
    }

    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        write_n_bytes(
            client_fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n", 62);
        return;
    }

    ssize_t length = path_stat.st_size;
    char header[256];
    sprintf(header, "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", length);
    write_n_bytes(client_fd, header, strlen(header));
    pass_n_bytes(file_fd, client_fd, length);
    close(file_fd);
}

ssize_t my_pass_n_bytes(int src, int dst, size_t n) {
    char buffer[BUFF_SIZE];
    int remaining = n;

    while (remaining > 0) {
        int to_read = remaining < BUFF_SIZE ? remaining : BUFF_SIZE;
        int bytes_read = read(src, buffer, to_read);
        if (bytes_read <= 0) {
            return bytes_read;
        }
        remaining -= bytes_read;
        write_n_bytes(dst, buffer, bytes_read);
    }
    return n;
}

void handle_put_request(int client_fd, const char *uri, int content_length) {

    if (content_length == -1) {
        write_n_bytes(
            client_fd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n", 60);
        return;
    }

    char filepath[256];
    strcpy(filepath, uri + 1);

    struct stat path_stat;
    stat(filepath, &path_stat);

    if (S_ISDIR(path_stat.st_mode)) {
        write_n_bytes(
            client_fd, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n", 64);
        return;
    }

    int file_exists = 0;
    if (access(filepath, F_OK) == 0) {
        file_exists = 1;
    }
    errno = 0;
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    if (fd == -1) {
        if (fd == -1) {
            write_n_bytes(client_fd,
                "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server "
                "Error\n",
                86);
            return;
        }
    }
    int bytes_written = my_pass_n_bytes(client_fd, fd, content_length);
    if (bytes_written != content_length) {
        close(fd);
        write_n_bytes(client_fd,
            "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server "
            "Error\n",
            86);
        return;
    }

    if (file_exists == 1) {
        char err[] = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n";
        write_n_bytes(client_fd, err, strlen(err));
        close(fd);

    } else {
        char err[] = "HTTP/1.1 201 Created\r\nContent-Length: 7\r\n\r\nCreated\n";
        write_n_bytes(client_fd, err, strlen(err));
        close(fd);
    }
}

