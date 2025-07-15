#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(const char *progname) {
    fprintf(stderr, "Usage: %s [-a] file\n", progname);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int append_mode = 0;
    int opt;

    /* Parse options */
    while ((opt = getopt(argc, argv, "a")) != -1) {
        switch (opt) {
        case 'a':
            append_mode = 1;
            break;
        default:
            usage(argv[0]);
        }
    }

    /* Must have exactly one non-option argument: the filename */
    if (optind + 1 != argc) {
        usage(argv[0]);
    }
    const char *filename = argv[optind];

    /* Open the file for writing (create if needed), with appropriate flags */
    int flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
    int fd = open(filename, flags, 0644);
    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    /* Read from stdin and write to both stdout and the file */
    char buf[4096];
    ssize_t nread;
    while ((nread = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        /* Write to stdout */
        ssize_t nw = write(STDOUT_FILENO, buf, nread);
        if (nw != nread) {
            perror("write to stdout");
            close(fd);
            exit(EXIT_FAILURE);
        }
        /* Write to file */
        nw = write(fd, buf, nread);
        if (nw != nread) {
            perror("write to file");
            close(fd);
            exit(EXIT_FAILURE);
        }
    }
    if (nread < 0) {
        perror("read");
        close(fd);
        exit(EXIT_FAILURE);
    }

    /* Clean up */
    if (close(fd) < 0) {
        perror("close");
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

