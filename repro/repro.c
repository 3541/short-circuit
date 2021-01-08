#define _GNU_SOURCE
#include <fcntl.h>
#include <liburing.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define URING_ENTRIES 2048
#define FILE_SIZE     20480

#define DATA_SPLICE_IN  1
#define DATA_SPLICE_OUT 2

static int in;
static int out;
static int pipes[URING_ENTRIES / 2][2];

static void splice_submit(struct io_uring* uring, size_t i) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(uring);
    if (!sqe) {
        fprintf(stderr, "Unable to get sqe.\n");
        return;
    }

    io_uring_prep_splice(sqe, in, 0, pipes[i][1], (uint64_t)-1, FILE_SIZE,
                         SPLICE_F_MOVE);
    io_uring_sqe_set_data(sqe, (void*)DATA_SPLICE_IN);
    io_uring_sqe_set_flags(sqe, IOSQE_IO_LINK);

    sqe = io_uring_get_sqe(uring);
    if (!sqe) {
        fprintf(stderr, "Unable to get sqe.\n");
        return;
    }

    io_uring_prep_splice(sqe, pipes[i][0], (uint64_t)-1, out, (uint64_t)-1,
                         FILE_SIZE, SPLICE_F_MOVE);
    io_uring_sqe_set_data(sqe, (void*)DATA_SPLICE_OUT);
}

int main(void) {
    struct io_uring uring;

    in  = open("20k", O_RDONLY);
    out = open("/dev/null", O_WRONLY);

    if (in < 0 || out < 0) {
        fprintf(stderr, "Failed to open files.\n");
        return -1;
    }

    for (size_t i = 0; i < URING_ENTRIES / 2; i++) {
        pipe(pipes[i]);
        if (pipes[i][0] < 0 || pipes[i][1] < 0) {
            fprintf(stderr, "Failed to open pipe.\n");
            return -1;
        }
    }

    int ret;
    if ((ret = io_uring_queue_init(URING_ENTRIES, &uring, 0))) {
        fprintf(stderr, "Failed to open queue (%s).\n", strerror(-ret));
        return -1;
    }

    for (size_t i = 0; i < 100; i++) {
        for (size_t j = 0; j < URING_ENTRIES / 2; j++)
            splice_submit(&uring, j);

        io_uring_submit(&uring);

        struct io_uring_cqe* cqe;
        for (int rc = io_uring_wait_cqe(&uring, &cqe); cqe;
             rc     = io_uring_peek_cqe(&uring, &cqe)) {
            if (rc < 0)
                fprintf(stderr, "Failed to get completion (%s).\n",
                        strerror(rc));

            if (cqe->res < FILE_SIZE) {
                if (cqe->user_data == DATA_SPLICE_IN)
                    fprintf(stderr, "Splice file to pipe: ");
                else
                    fprintf(stderr, "Splice pipe to file: ");

                if (cqe->res < 0)
                    fprintf(stderr, "got error %s.\n", strerror(-cqe->res));
                else
                    fprintf(stderr, "got short read/write %d.\n", cqe->res);
            }

            io_uring_cqe_seen(&uring, cqe);
        }
    }

    return 0;
}
