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

#define BUFFER_SIZE 4096

// Function prototypes
void handleGETRequest(int clientSocket, const char *resourceURI);
void handlePUTRequest(int clientSocket, const char *resourceURI, int contentLength);
int validateHeaders(const char *headerBuffer);
ssize_t readUntilDelimiter(int fileDescriptor, char buffer[], size_t maxBytes);
ssize_t my_pass_n_bytes(int sourceFd, int destinationFd, size_t bytesToPass);
int isValidHTTPMethod(const char *method);

int64_t getCurrentTimeMillis() {
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);
    return (int64_t) (currentTime.tv_sec) * 1000 + (currentTime.tv_usec / 1000);
}

int initializeListener(Listener_Socket *listenerSocket, int listeningPort) {
    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(listeningPort);
    printf("%ld start", getCurrentTimeMillis());

    if ((listenerSocket->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }

    printf("%ld here", getCurrentTimeMillis());

    if (bind(listenerSocket->fd, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) < 0) {
        return -1;
    }

    printf("%ld here now", getCurrentTimeMillis());

    if (listen(listenerSocket->fd, 128) < 0) {
        return -1;
    }

    printf("%ld final", getCurrentTimeMillis());

    return 0;
}

int acceptConnection(Listener_Socket *listenerSocket) {
    return accept(listenerSocket->fd, NULL, NULL);
}

ssize_t getFileLength(const char *filePath) {
    struct stat fileInfo;
    stat(filePath, &fileInfo);
    return fileInfo.st_size;
}

void processHTTPRequest(int clientSocket, const char *httpMethod, const char *uri,
    const char *httpVersion, const char *requestBuffer) {
    if (!isValidHTTPMethod(httpMethod)) {
        write_n_bytes(clientSocket,
            "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n", 68);
        return;
    }

    if (strcmp(httpVersion, "HTTP/1.1") != 0) {
        write_n_bytes(clientSocket,
            "HTTP/1.1 505 Version Not Supported\r\nContent-Length: 22\r\n\r\nVersion Not "
            "Supported\n",
            80);
        return;
    }

    if (strcmp(httpMethod, "GET") == 0) {
        handleGETRequest(clientSocket, uri);
    } else if (strcmp(httpMethod, "PUT") == 0) {
        char *contentLengthStr = strstr(requestBuffer, "Content-Length: ");
        if (!contentLengthStr) {
            write_n_bytes(clientSocket,
                "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n", 60);
            return;
        }
        int contentLength = 0;
        sscanf(contentLengthStr, "Content-Length: %d", &contentLength);
        handlePUTRequest(clientSocket, uri, contentLength);
    } else {
        write_n_bytes(clientSocket,
            "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n", 68);
    }
}

int validateHeaders(const char *buffer) {
    const char *pointer = buffer;
    while ((pointer = strstr(pointer, "\r\n")) != NULL) {
        pointer += 2;
        if (strncmp(pointer, "\r\n", 2) == 0)
            break;

        const char *colonPosition = strchr(pointer, ':');
        if (colonPosition == NULL) {
            return -1;
        }
    }
    return 0;
}

ssize_t readUntilDelimiter(int fileDescriptor, char buffer[], size_t maxBytes) {
    size_t bytesRead = 0;
    size_t totalBytesRead = 0;
    char *bufferPointer = buffer;

    while (totalBytesRead < maxBytes) {
        bytesRead = read(fileDescriptor, bufferPointer, 1);
        if (bytesRead == -1ul) {
            if (errno == EINTR) {
                bytesRead = 0;
            } else {
                return -1;
            }
        } else if (bytesRead == 0) {
            break;
        } else {
            totalBytesRead += bytesRead;
            bufferPointer += bytesRead;
            if (totalBytesRead >= 2) {
                if ((memcmp(bufferPointer - 2, "\n\n", 2) == 0)
                    || (memcmp(bufferPointer - 2, "\r\r", 2) == 0)) {
                    break;
                }
            }
            if (totalBytesRead >= 4) {
                if (memcmp(bufferPointer - 4, "\r\n\r\n", 4) == 0) {
                    break;
                }
            }
        }
    }
    if (totalBytesRead < maxBytes) {
        *bufferPointer = '\0';
    }

    return totalBytesRead;
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
    if (initializeListener(&listener, port) != 0) {
        fprintf(stderr, "Failed to initialize listener\n");
        return 1;
    }

    while (1) {
        int clientSocket = acceptConnection(&listener);
        if (clientSocket < 0) {
            perror("Failed to accept connection");
            continue;
        }

        char requestBuffer[BUFFER_SIZE];
        ssize_t bytesRead = readUntilDelimiter(clientSocket, requestBuffer, BUFFER_SIZE);
        if (bytesRead <= 0) {
            perror("Failed to read from socket");
            close(clientSocket);
            continue;
        }

        requestBuffer[bytesRead] = '\0';

        if (validateHeaders(requestBuffer) != 0) {
            write_n_bytes(clientSocket,
                "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n", 60);
            close(clientSocket);
            continue;
        }

        char httpMethod[10], uri[256], httpVersion[10];
        sscanf(requestBuffer, "%s %s %s", httpMethod, uri, httpVersion);

        processHTTPRequest(clientSocket, httpMethod, uri, httpVersion, requestBuffer);

        close(clientSocket);
    }

    return 0;
}

int isValidHTTPMethod(const char *method) {
    const char *validMethods[] = { "GET", "PUT", "POST", "DELETE", "HEAD", "OPTIONS" };
    size_t numMethods = sizeof(validMethods) / sizeof(validMethods[0]);

    for (size_t i = 0; i < numMethods; i++) {
        if (strcmp(method, validMethods[i]) == 0) {
            return 1; // Valid method found
        }
    }
    return 0; // No valid method found
}

void handleGETRequest(int clientSocket, const char *resourceURI) {
    char filePath[256];
    strcpy(filePath, resourceURI + 1);

    struct stat fileStats;
    if (stat(filePath, &fileStats) < 0) {
        write_n_bytes(
            clientSocket, "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n", 62);
        return;
    }

    if (S_ISDIR(fileStats.st_mode)) {
        write_n_bytes(
            clientSocket, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n", 62);
        return;
    }

    int fileFd = open(filePath, O_RDONLY);
    if (fileFd < 0) {
        write_n_bytes(
            clientSocket, "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n", 62);
        return;
    }

    ssize_t contentLength = fileStats.st_size;
    char responseHeader[256];
    sprintf(responseHeader, "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", contentLength);
    write_n_bytes(clientSocket, responseHeader, strlen(responseHeader));
    my_pass_n_bytes(fileFd, clientSocket, contentLength);
    close(fileFd);
}

ssize_t my_pass_n_bytes(int sourceFd, int destinationFd, size_t bytesToPass) {
    char transferBuffer[BUFFER_SIZE];
    ssize_t remainingBytes = bytesToPass;

    while (remainingBytes > 0) {
        int readAmount = remainingBytes < BUFFER_SIZE ? remainingBytes : BUFFER_SIZE;
        ssize_t bytesRead = read(sourceFd, transferBuffer, readAmount);
        if (bytesRead <= 0) {
            return bytesRead; // Error or EOF
        }
        remainingBytes -= bytesRead;
        write_n_bytes(destinationFd, transferBuffer, bytesRead);
    }
    return bytesToPass;
}

void handlePUTRequest(int clientSocket, const char *resourceURI, int contentLength) {
    if (contentLength == -1) {
        write_n_bytes(clientSocket,
            "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n", 60);
        return;
    }

    char filePath[256];
    strcpy(filePath, resourceURI + 1);

    struct stat fileStats;
    stat(filePath, &fileStats);

    if (S_ISDIR(fileStats.st_mode)) {
        write_n_bytes(
            clientSocket, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n", 64);
        return;
    }

    int fileExists = 0;
    if (access(filePath, F_OK) == 0) {
        fileExists = 1;
    }
    errno = 0;
    int fileFd = open(filePath, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    if (fileFd == -1) {
        write_n_bytes(clientSocket,
            "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server "
            "Error\n",
            86);
        return;
    }
    int bytesWritten = my_pass_n_bytes(clientSocket, fileFd, contentLength);
    if (bytesWritten != contentLength) {
        close(fileFd);
        write_n_bytes(clientSocket,
            "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server "
            "Error\n",
            86);
        return;
    }

    if (fileExists == 1) {
        char successMessage[] = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n";
        write_n_bytes(clientSocket, successMessage, strlen(successMessage));
        close(fileFd);

    } else {
        char createdMessage[] = "HTTP/1.1 201 Created\r\nContent-Length: 7\r\n\r\nCreated\n";
        write_n_bytes(clientSocket, createdMessage, strlen(createdMessage));
        close(fileFd);
    }
}
