#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define INPUT_BUFFER_SIZE  4096
#define OUTPUT_BUFFER_SIZE 512

// Function to print error messages and exit
void print_error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// Function to flush the write buffer and reset its index
void flushOutputBuffer(char *buffer, ssize_t *bufferLength) {
    if (*bufferLength > 0) {
        if (write(STDOUT_FILENO, buffer, *bufferLength) != *bufferLength) {
            print_error("write error");
        }
        *bufferLength = 0; // Reset buffer length after flushing
    }
}

// Function to process each file or stdin based on the delimiter
void processInputFile(int fileDescriptor, char delimiterChar) {
    char readBuffer[INPUT_BUFFER_SIZE];
    char outputBuffer[OUTPUT_BUFFER_SIZE];
    ssize_t bytesRead, outputBufferLength = 0;

    while ((bytesRead = read(fileDescriptor, readBuffer, INPUT_BUFFER_SIZE)) > 0) {
        for (ssize_t i = 0; i < bytesRead; ++i) {
            if (outputBufferLength == OUTPUT_BUFFER_SIZE) {
                flushOutputBuffer(outputBuffer, &outputBufferLength);
            }
            outputBuffer[outputBufferLength++]
                = (readBuffer[i] == delimiterChar) ? '\n' : readBuffer[i];
        }
    }
    flushOutputBuffer(outputBuffer, &outputBufferLength);

    if (bytesRead == -1) {
        print_error("read error");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <delimiter> <file1> [file2 ...]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char delimiterChar = argv[1][0];
    if (strlen(argv[1]) != 1) {
        fprintf(stderr, "Delimiter must be a single character.\n");
        return 1;
    }

    struct stat fileInfo;
    int operationStatus = 0;

    for (int argIndex = 2; argIndex < argc; ++argIndex) {
        if (strcmp(argv[argIndex], "-") == 0) {
            processInputFile(STDIN_FILENO, delimiterChar);
        } else {
            if (stat(argv[argIndex], &fileInfo) != 0 || !S_ISREG(fileInfo.st_mode)) {
                fprintf(
                    stderr, "Error: %s is not a regular file or does not exist\n", argv[argIndex]);
                operationStatus = 1;
                continue;
            }

            int fileDescriptor = open(argv[argIndex], O_RDONLY);

            if (fileDescriptor == -1) {
                print_error("open error");
            }

            processInputFile(fileDescriptor, delimiterChar);
            close(fileDescriptor);
        }
    }

    return operationStatus;
}

