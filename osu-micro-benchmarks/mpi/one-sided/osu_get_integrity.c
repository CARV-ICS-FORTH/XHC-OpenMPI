#define BENCHMARK "OSU MPI_Get%s Integrity Test"
/*
 * Copyright (C) 2003-2021 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University.            
 *
 * Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level OMB directory.
 */

#include <osu_util_mpi.h>
#include <assert.h>

double  t_start = 0.0, t_end = 0.0;
char    *rbuf=NULL, *win_base=NULL;

void run_get_with_fence (int, enum WINDOW);
void run_get_with_pscw (int, enum WINDOW);

int main (int argc, char *argv[])
{
    int         rank,nprocs;
    int         po_ret = PO_OKAY;
#if MPI_VERSION >= 3
    options.win = WIN_ALLOCATE;
    // options.sync = FLUSH;
#else
    options.win = WIN_CREATE;
    // options.sync = LOCK;
#endif

    options.sync = FENCE;

    options.bench = ONE_SIDED;
    options.subtype = LAT;
    options.synctype = ALL_SYNC;

    set_header(HEADER);
    set_benchmark_name("osu_get_latency");

    po_ret = process_options(argc, argv);

    if (PO_OKAY == po_ret && NONE != options.accel) {
        if (init_accel()) {
           fprintf(stderr, "Error initializing device\n");
            exit(EXIT_FAILURE);
        }
    }

    MPI_CHECK(MPI_Init(&argc, &argv));
    MPI_CHECK(MPI_Comm_size(MPI_COMM_WORLD, &nprocs));
    MPI_CHECK(MPI_Comm_rank(MPI_COMM_WORLD, &rank));

    if (0 == rank) {
        switch (po_ret) {
            case PO_CUDA_NOT_AVAIL:
                fprintf(stderr, "CUDA support not enabled.  Please recompile "
                        "benchmark with CUDA support.\n");
                break;
            case PO_OPENACC_NOT_AVAIL:
                fprintf(stderr, "OPENACC support not enabled.  Please "
                        "recompile benchmark with OPENACC support.\n");
                break;
            case PO_BAD_USAGE:
                print_bad_usage_message(rank);
            case PO_HELP_MESSAGE:
                usage_one_sided("osu_get_latency");
                break;
            case PO_VERSION_MESSAGE:
                print_version_message(rank);
                MPI_CHECK(MPI_Finalize());
                exit(EXIT_SUCCESS);
            case PO_OKAY:
                break;
        }
    }

    switch (po_ret) {
        case PO_CUDA_NOT_AVAIL:
        case PO_OPENACC_NOT_AVAIL:
        case PO_BAD_USAGE:
            MPI_CHECK(MPI_Finalize());
            exit(EXIT_FAILURE);
        case PO_HELP_MESSAGE:
        case PO_VERSION_MESSAGE:
            MPI_CHECK(MPI_Finalize());
            exit(EXIT_SUCCESS);
        case PO_OKAY:
            break;
    }

    if (nprocs != 2) {
        if (rank == 0) {
            fprintf(stderr, "This test requires exactly two processes\n");
        }

        MPI_CHECK(MPI_Finalize());

        return EXIT_FAILURE;
    }

    print_header_one_sided(rank, options.win, options.sync);

    switch (options.sync) {
        case LOCK:
        case FLUSH_LOCAL:
        case LOCK_ALL:
        case FLUSH:
            fprintf(stderr, "This sync method is disabled for this test\n");
            MPI_CHECK(MPI_Finalize());
            return EXIT_FAILURE;

        case PSCW:
            run_get_with_pscw(rank, options.win);
            break;

        case FENCE:
        default:
            run_get_with_fence(rank, options.win);
            break;
    }

    MPI_CHECK(MPI_Finalize());

    if (NONE != options.accel) {
        if (cleanup_accel()) {
            fprintf(stderr, "Error cleaning up device\n");
            exit(EXIT_FAILURE);
        }
    }

    return EXIT_SUCCESS;
}

/*Run Get with Fence */
void run_get_with_fence(int rank, enum WINDOW type)
{
    int size, i, k;
    MPI_Aint disp = 0;
    MPI_Win     win;

    for (size = options.min_message_size; size <= options.max_message_size; size = (size ? size * 2 : size + 1)) {
        allocate_memory_one_sided(rank, &rbuf, &win_base, size, type, &win);

#if MPI_VERSION >= 3
        if (type == WIN_DYNAMIC) {
            disp = disp_remote;
        }
#endif

        if (size > LARGE_MESSAGE_SIZE) {
            options.iterations = options.iterations_large;
            options.skip = options.skip_large;
        }

        MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

        for (i = 0; i < options.skip + options.iterations; i++) {
            for(k = 0; k < size; k++)
                win_base[k] = (char) (i + k);

            MPI_CHECK(MPI_Win_fence(0, win));

            if(rank == 0) {
                if (i == options.skip)
                    t_start = MPI_Wtime();

                MPI_CHECK(MPI_Get(rbuf, size, MPI_CHAR, 1, disp, size, MPI_CHAR, win));
            }

            MPI_CHECK(MPI_Win_fence(0, win));

            if(rank == 0)
                assert(memcmp(win_base, rbuf, size) == 0);
        }

        if (rank == 0)
            t_end = MPI_Wtime();

        MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

        if (rank == 0) {
            fprintf(stdout, "%-*d%*.*f\n", 10, size, FIELD_WIDTH,
                FLOAT_PRECISION, (t_end - t_start) * 1.0e6 / options.iterations);
        }

        free_memory_one_sided (rbuf, win_base, type, win, rank);
    }
}

/*Run GET with Post/Start/Complete/Wait */
void run_get_with_pscw(int rank, enum WINDOW type)
{
    int destrank, size, i, k;
    MPI_Aint disp = 0;
    MPI_Win     win;
    MPI_Group   comm_group, group;

    MPI_CHECK(MPI_Comm_group(MPI_COMM_WORLD, &comm_group));

    for (size = options.min_message_size; size <= options.max_message_size; size = (size ? size * 2 : 1)) {
        allocate_memory_one_sided(rank, &rbuf, &win_base, size, type, &win);

#if MPI_VERSION >= 3
        if (type == WIN_DYNAMIC) {
            disp = disp_remote;
        }
#endif

        if (size > LARGE_MESSAGE_SIZE) {
            options.iterations = options.iterations_large;
            options.skip = options.skip_large;
        }

        destrank = (rank == 0 ? 1 : 0);
        MPI_CHECK(MPI_Group_incl(comm_group, 1, &destrank, &group));

        MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

        for (i = 0; i < options.skip + options.iterations; i++) {
            for(k = 0; k < size; k++)
                win_base[k] = (char) (i + k);

            if(rank == 0) {
                if(i == options.skip)
                    t_start = MPI_Wtime();

                MPI_CHECK(MPI_Win_start (group, 0, win));
                MPI_CHECK(MPI_Get(rbuf, size, MPI_CHAR, 1, disp, size, MPI_CHAR, win));
                MPI_CHECK(MPI_Win_complete(win));

                assert(memcmp(win_base, rbuf, size) == 0);
            }

            if(rank == 1) {
                MPI_CHECK(MPI_Win_post(group, 0, win));
                MPI_CHECK(MPI_Win_wait(win));
            }
        }

        if(rank == 0)
            t_end = MPI_Wtime();

        MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

        if(rank == 0) {
            printf("%-*d%*.*f\n", 10, size, FIELD_WIDTH,
                FLOAT_PRECISION, (t_end - t_start) * 1.0e6 / options.iterations);
        }

        MPI_CHECK(MPI_Group_free(&group));

        free_memory_one_sided (rbuf, win_base, type, win, rank);
    }

    MPI_CHECK(MPI_Group_free(&comm_group));
}
