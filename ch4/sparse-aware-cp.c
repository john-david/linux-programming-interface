
#define _XOPEN_SOURCE 700
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define BUF_SIZE 65536    /* 64 KB buffer */

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <source> <dest>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* 1. Open source for reading and stat it */
    int infd = open(argv[1], O_RDONLY);
    if (infd < 0) die("open source");

    struct stat st;
    if (fstat(infd, &st) < 0) die("fstat source");
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "Error: source is not a regular file\n");
        exit(EXIT_FAILURE);
    }

    /* 2. Open/create dest with same permissions, truncate existing */
    int outfd = open(argv[2],
                     O_WRONLY | O_CREAT | O_TRUNC,
                     st.st_mode & 0777);
    if (outfd < 0) die("open dest");

    /* 3. Copy, preserving holes */
    unsigned char *buf = malloc(BUF_SIZE);
    if (!buf) die("malloc");

    ssize_t nread;
    while ((nread = read(infd, buf, BUF_SIZE)) > 0) {
        unsigned char *p     = buf;
        size_t        togo  = (size_t)nread;

        while (togo > 0) {
            /* If this byte is zero, scan for a zero-run */
            if (*p == 0) {
                size_t z = 1;
                while (z < togo && p[z] == 0) z++;
                /* seek forward in output, creating a hole */
                if (lseek(outfd, (off_t)z, SEEK_CUR) < 0)
                    die("lseek dest");
                p   += z;
                togo -= z;
            }
            else {
                /* Non-zero run: scan and write it */
                size_t nz = 1;
                while (nz < togo && p[nz] != 0) nz++;
                ssize_t written = write(outfd, p, nz);
                if (written < 0 || (size_t)written != nz)
                    die("write dest");
                p   += nz;
                togo -= nz;
            }
        }
    }
    if (nread < 0) die("read source");

    /* 4. Ensure trailing hole (if source ended in a hole) is created */
    if (ftruncate(outfd, st.st_size) < 0)
        die("ftruncate dest");

    /* 5. Clean up */
    free(buf);
    close(infd);
    close(outfd);
    return EXIT_SUCCESS;
}

