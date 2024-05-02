#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFF_SIZE 4096

typedef struct {
    int fd;
} Listener_Socket;

int listener_init(Listener_Socket *sock, int port) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    sock->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock->fd < 0) {
        perror("Socket creation failed");
        return -1;
    }

    int opt = 1;
    setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Socket bind failed");
        close(sock->fd);
        return -1;
    }

    if (listen(sock->fd, 128) < 0) {
        perror("Socket listen failed");
        close(sock->fd);
        return -1;
    }

    return 0;
}

int listener_accept(Listener_Socket *sock) {
    return accept(sock->fd, NULL, NULL);
}

void handle_get_request(int client_fd, const char *uri) {
    char filepath[256];
    strcpy(filepath, uri + 1);

    struct stat path_stat;
    if (stat(filepath, &path_stat) < 0) {
        write(client_fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n", 62);
        return;
    }

    if (S_ISDIR(path_stat.st_mode)) {
        write(client_fd, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n", 62);
        return;
    }

    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        write(client_fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n", 62);
        return;
    }

    ssize_t length = path_stat.st_size;
    char header[256];
    sprintf(header, "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", length);
    write(client_fd, header, strlen(header));
    char buffer[BUFF_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(file_fd, buffer, BUFF_SIZE)) > 0) {
        write(client_fd, buffer, bytes_read);
    }
    close(file_fd);
}

void handle_put_request(int client_fd, const char *uri, int content_length) {
    char filepath[256];
    strcpy(filepath, uri + 1);
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) {
        write(client_fd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server Error\n", 86);
        return;
    }

    char buffer[BUFF_SIZE];
    int remaining = content_length;
    while (remaining > 0) {
        int to_read = remaining < BUFF_SIZE ? remaining : BUFF_SIZE;
        int bytes_read = read(client_fd, buffer, to_read);
        if (bytes_read <= 0) {
            write(client_fd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n", 60);
            close(fd);
            unlink(filepath);  // Remove the partially written file
            return;
        }
        if (write(fd, buffer, bytes_read) != bytes_read) {
            write(client_fd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server Error\n", 86);
            close(fd);
            unlink(filepath);
            return;
        }
        remaining -= bytes_read;
    }

    close(fd);
    struct stat path_stat;
    if (stat(filepath, &path_stat) == 0 && path_stat.st_size == content_length) {
        write(client_fd, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK\n", 47);
    } else {
        write(client_fd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server Error\n", 86);
    }
}

void handle_connection(int client_fd) {
    char buffer[BUFF_SIZE];
    ssize_t bytes_read = read(client_fd, buffer, BUFF_SIZE);
    if (bytes_read <= 0) {
        perror("Failed to read from socket");
        close(client_fd);
        return;
    }

    buffer[bytes_read] = '\0';  // Null-terminate the buffer to make it a valid string for parsing

    char method[10], uri[256], version[10];
    sscanf(buffer, "%s %s %s", method, uri, version);  // Parse the request line

    if (strcmp(version, "HTTP/1.1") != 0) {
        write(client_fd, "HTTP/1.1 505 Version Not Supported\r\nContent-Length: 22\r\n\r\nVersion Not Supported\n", 80);
    } else if (strcmp(method, "GET") == 0) {
        handle_get_request(client_fd, uri);
    } else if (strcmp(method, "PUT") == 0) {
        char *content_length_str = strstr(buffer, "Content-Length: ");
        if (!content_length_str) {
            write(client_fd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n", 60);
        } else {
            int content_length = 0;
            sscanf(content_length_str, "Content-Length: %d", &content_length);
            handle_put_request(client_fd, uri, content_length);
        }
    } else {
        write(client_fd, "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n", 68);
    }
    close(client_fd);
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
        fprintf(stderr, "Listener failed to initialize\n");
        return 1;
    }

    while (1) {
        int client_fd = listener_accept(&listener);
        if (client_fd < 0) {
            perror("Failed to accept connection");
            continue;
        }
        handle_connection(client_fd);
    }

    return 0;
}

