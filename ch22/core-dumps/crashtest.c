/* crashtest.c – deliberately crash to produce a core file
   Build: clang -g -Wall -o crashtest crashtest.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(void)
{
    /* Ensure the core dump isn’t size-capped */
    struct rlimit rl = { RLIM_INFINITY, RLIM_INFINITY };
    if (setrlimit(RLIMIT_CORE, &rl) == -1)
        die("setrlimit(RLIMIT_CORE)");

    printf("PID %ld about to segfault – expect a core file here\n",
           (long)getpid());
    fflush(stdout);

    /* Crash on purpose */
    raise(SIGSEGV);          /* or: *(volatile int *)0 = 0; */

    return 0;                /* never reached */
}


