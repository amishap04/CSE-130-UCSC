#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_BUFFER 1024

void handle_error(const char *error_message, int exit_code) {
    fprintf(stderr, "%s\n", error_message);
    exit(exit_code);
}

int read_line_safely(int fd, char *buffer, int max_length, int require_newline) {
    int pos = 0, bytes;
    char ch;
    int newline_found = 0;

    while (pos < max_length - 1) {
        bytes = read(fd, &ch, 1);
        if (bytes == 1) {
            if (ch == '\n') {
                newline_found = 1;
                break;
            }
            buffer[pos++] = ch;
        } 
	else if (bytes == 0) {
            break;
        } 
	else {
            handle_error("Operation Failed", 1);
        }
    }
    buffer[pos] = '\0';

    if (require_newline && !newline_found && pos > 0) {
        return -1;
    }
    return pos;
}

int condition_to_print_ok() {
    return 1; // Always return 1 (true) for now.
}

int main(void) {
    char cmd[MAX_BUFFER], path[MAX_BUFFER];
    int file_desc, read_size;
    char buffer[MAX_BUFFER], extra[MAX_BUFFER];
    struct stat statbuf;

    if (read_line_safely(STDIN_FILENO, cmd, sizeof(cmd), 1) <= 0) {
        handle_error("Invalid Command", 1);
    }

    if (!strcmp(cmd, "get")) {
        if (read_line_safely(STDIN_FILENO, path, sizeof(path), 1) <= 0) {
            handle_error("Invalid Command", 1);
        }

        if (stat(path, &statbuf) != 0) {
            handle_error("Invalid Command", 1);
        }
        if (!S_ISREG(statbuf.st_mode)) {
            if (S_ISDIR(statbuf.st_mode)) {
                handle_error("Operation Failed", 1);
            } 
	    else {
                handle_error("Invalid Command", 1);
            }
        }

        if (read_line_safely(STDIN_FILENO, extra, sizeof(extra), 0) > 0) {
            handle_error("Invalid Command", 1);
        }

        file_desc = open(path, O_RDONLY);
        if (file_desc == -1) {
            handle_error("Operation Failed", 1);
        }

        while ((read_size = read(file_desc, buffer, MAX_BUFFER)) > 0) {
            if (write(STDOUT_FILENO, buffer, read_size) != read_size) {
                close(file_desc);
                handle_error("Write Error", 1);
            }
        }
        if (read_size < 0) {
            close(file_desc);
            handle_error("Operation Failed", 1);
        }
        close(file_desc);
    }

    else if (!strcmp(cmd, "set")) {
        if (read_line_safely(STDIN_FILENO, path, sizeof(path), 1) <= 0) {
            handle_error("Invalid Command", 1);
        }

        if (read_line_safely(STDIN_FILENO, buffer, sizeof(buffer), 1) <= 0) {
            handle_error("Invalid Command", 1);
        }

        int content_len = atoi(buffer), written_bytes = 0;
        file_desc = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        
	if (file_desc == -1) {
            handle_error("Operation Failed", 1);
        }

        while (written_bytes < content_len) {
            int to_write = (MAX_BUFFER < content_len - written_bytes) ? MAX_BUFFER : content_len - written_bytes;
            read_size = read(STDIN_FILENO, buffer, to_write);
            if (read_size < 0) {
                close(file_desc);
                handle_error("Operation Failed", 1);
            }
            if (write(file_desc, buffer, read_size) != read_size) {
                close(file_desc);
                handle_error("Operation Failed", 1);
            }

            written_bytes += read_size;
        }
        close(file_desc);
        if (condition_to_print_ok()) {
            printf("OK\n");
        }
    }

    else {
        handle_error("Invalid Command", 1);
    }

    return 0;
}

