// Test case for OP_SPLICE short read issue. Most systems may require the open
// file limit to be raised (4096, which is probably still within the hard limit,
// should be enough).
//
// This tries to read from a file called '20k', which I generated with the
// following:
// `dd if=/dev/random of=20k bs=1k count=20`

#define _GNU_SOURCE
#include <fcntl.h>
#include <liburing.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define URING_ENTRIES 2048
#define FILE_SIZE     20480

#define DATA_SPLICE_IN  (1ULL)
#define DATA_SPLICE_OUT (2ULL)

static int in;
static int out;
static int pipes[URING_ENTRIES / 2][2];

static void splice_submit(struct io_uring* uring, size_t i) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(uring);
    if (!sqe) {
        fprintf(stderr, "Unable to get sqe.\n");
        return;
    }

    int nbytes;
    ioctl(pipes[i][0], FIONREAD, &nbytes);
    if (nbytes)
        fprintf(stderr, "Pipe not empty. Has %d bytes.\n", nbytes);

    io_uring_prep_splice(sqe, in, 0, pipes[i][1], (uint64_t)-1, FILE_SIZE,
                         SPLICE_F_MOVE);
    sqe->user_data = (uintptr_t)&pipes[i][1] | DATA_SPLICE_IN;
    io_uring_sqe_set_flags(sqe, IOSQE_IO_LINK);

    sqe = io_uring_get_sqe(uring);
    if (!sqe) {
        fprintf(stderr, "Unable to get sqe.\n");
        return;
    }

    io_uring_prep_splice(sqe, pipes[i][0], (uint64_t)-1, out, (uint64_t)-1,
                         FILE_SIZE, SPLICE_F_MOVE);
    sqe->user_data = (uintptr_t)&pipes[i][0] | DATA_SPLICE_OUT;
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
        if (pipe(pipes[i]) < 0) {
            fprintf(stderr, "Failed to open pipe (%s).\n", strerror(errno));
            return -1;
        }
    }

    int ret;
    if ((ret = io_uring_queue_init(URING_ENTRIES, &uring, 0))) {
        fprintf(stderr, "Failed to open queue (%s).\n", strerror(-ret));
        return -1;
    }

    for (size_t i = 0; i < 10000; i++) {
        for (size_t j = 0; j < URING_ENTRIES / 2; j++)
            splice_submit(&uring, j);

        if ((ret = io_uring_submit(&uring)) < 0) {
            fprintf(stderr, "Failed to submit. %s\n", strerror(-ret));
            return -1;
        }

        struct io_uring_cqe* cqe;
        for (int rc = io_uring_wait_cqe(&uring, &cqe); cqe;
             rc     = io_uring_peek_cqe(&uring, &cqe)) {
            if (rc < 0)
                fprintf(stderr, "Failed to get completion (%s).\n",
                        strerror(rc));

            if (cqe->res < FILE_SIZE) {
                if (cqe->user_data & DATA_SPLICE_IN)
                    fprintf(stderr, "Splice file to pipe: ");
                else
                    fprintf(stderr, "Splice pipe to file: ");

                if (cqe->res < 0)
                    fprintf(stderr, "got error %s.\n", strerror(-cqe->res));
                else {
                    fprintf(stderr, "got short read/write %d. ", cqe->res);
                    int pipe = *((int*)(cqe->user_data &
                                        ~(DATA_SPLICE_IN | DATA_SPLICE_OUT)));
                    int nbytes;
                    if (ioctl(pipe, FIONREAD, &nbytes) < 0)
                        perror("ioctl");
                    fprintf(stderr, "%d bytes in pipe.\n", nbytes);
                }
            }

            io_uring_cqe_seen(&uring, cqe);
        }
    }

    return 0;
}
