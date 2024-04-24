#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_BUFFER 1024 // Define the maximum buffer size for reading inputs

// Function to handle errors and exit the program
void handle_error(const char *error_message, int exit_code) {
    fprintf(stderr, "%s\n", error_message); // Print the error message to stderr
    exit(exit_code); // Exit the program with the provided exit code
}

// Function to safely read a line from a file descriptor
int read_line_safely(int fd, char *buffer, int max_length, int require_newline) {
    int pos = 0, bytes; // Position in buffer, and bytes read in each read call
    char ch; // Character read from the file
    int newline_found = 0; // Flag to check if newline is found

    // Loop until the buffer is full minus one character space for null terminator
    while (pos < max_length - 1) {
        bytes = read(fd, &ch, 1); // Read a single character
        if (bytes == 1) { // If read was successful
            if (ch == '\n') { // Check if the character is a newline
                newline_found = 1; // Set the newline found flag
                break; // Exit the loop
            }
            buffer[pos++] = ch; // Store the character in the buffer
        } else if (bytes == 0) { // If no more data to read
            break; // Break the loop
        } else { // If read returns an error
            handle_error("Operation Failed", 1); // Handle the error
        }
    }

    buffer[pos] = '\0'; // Null-terminate the string

    // Check if a newline was required but not found
    if (require_newline && !newline_found && pos > 0) {
        return -1; // Return an error code
    }

    return pos; // Return the number of characters read
}

// Placeholder function to decide when to print "OK"
int condition_to_print_ok() {
    return 1; // Always return 1 (true) for now.
}

// Main function entry point
int main(void) {
    char cmd[MAX_BUFFER], path[MAX_BUFFER]; // Buffers for command and path
    int file_desc, read_size; // File descriptor and read size variable
    char buffer[MAX_BUFFER], extra[MAX_BUFFER]; // Buffers for reading and extra input
    struct stat statbuf; // File status structure

    // Read a command safely
    if (read_line_safely(STDIN_FILENO, cmd, sizeof(cmd), 1) <= 0) {
        handle_error("Invalid Command", 1); // Handle invalid command
    }

    // Process "get" command
    if (!strcmp(cmd, "get")) {
        if (read_line_safely(STDIN_FILENO, path, sizeof(path), 1) <= 0) {
            handle_error("Invalid Command", 1);
        }

        // Check the status of the file
        if (stat(path, &statbuf) != 0) {
            handle_error("Invalid Command", 1);
        }

        // Check if the file is a regular file
        if (!S_ISREG(statbuf.st_mode)) {
            if (S_ISDIR(statbuf.st_mode)) {
                handle_error("Operation Failed", 1);
            } else {
                handle_error("Invalid Command", 1);
            }
        }

        // Read any extra input
        if (read_line_safely(STDIN_FILENO, extra, sizeof(extra), 0) > 0) {
            handle_error("Invalid Command", 1);
        }

        // Open the file for reading
        file_desc = open(path, O_RDONLY);
        if (file_desc == -1) {
            handle_error("Operation Failed", 1);
        }

        // Read and write the file content to stdout
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

    // Process "set" command
    else if (!strcmp(cmd, "set")) {
        if (read_line_safely(STDIN_FILENO, path, sizeof(path), 1) <= 0) {
            handle_error("Invalid Command", 1);
        }

        if (read_line_safely(STDIN_FILENO, buffer, sizeof(buffer), 1) <= 0) {
            handle_error("Invalid Command", 1);
        }

        int content_len = atoi(buffer), written_bytes = 0; // Content length and written bytes
        file_desc = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644); // Open file for writing

        if (file_desc == -1) {
            handle_error("Operation Failed", 1);
        }

        // Write to the file from stdin
        while (written_bytes < content_len) {
            int to_write = (MAX_BUFFER < content_len - written_bytes) ? MAX_BUFFER
                                                                      : content_len - written_bytes;
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
        handle_error("Invalid Command", 1); // Handle any other invalid commands
    }

    return 0; // Return 0 on successful completion
}
