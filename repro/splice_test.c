#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define FILE_SIZE 1048576

int main(void) {
    int in = open("1m", O_RDONLY);
    int out = open("/dev/null", O_WRONLY);
    if (in < 0 || out < 0) {
        perror("open");
        return -1;
    }

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return -1;
    }

    ssize_t count = splice(in, NULL, pipefd[1], NULL, FILE_SIZE, SPLICE_F_MOVE);
    printf("file -> pipe: %ld\n", count);

    count = splice(pipefd[0], NULL, out, NULL, FILE_SIZE, SPLICE_F_MOVE);
    printf("pipe -> file: %ld\n", count);

    close(in);
    close(out);
    close(pipefd[0]);
    close(pipefd[1]);

    return 0;
}
