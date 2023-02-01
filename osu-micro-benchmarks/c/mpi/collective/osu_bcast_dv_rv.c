#define BENCHMARK "OSU MPI_Bcast (data-varying + root-varying)"
/*
* Copyright (C) 2002-2022 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University.
 *
 * Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level OMB directory.
 */
#include <osu_util_mpi.h>

#include <stdlib.h>
#include <stdbool.h>

int main(int argc, char *argv[])
{
    int i = 0, j, rank, size;
    int numprocs;
    double avg_time = 0.0, max_time = 0.0, min_time = 0.0;
    double latency = 0.0, t_start = 0.0, t_stop = 0.0;
    double timer=0.0;
    char *buffer=NULL;
    int po_ret;
    int errors = 0, local_errors = 0;
    omb_graph_options_t omb_graph_options;
    omb_graph_data_t *omb_graph_data = NULL;
    int papi_eventset = OMB_PAPI_NULL;

    options.bench = COLLECTIVE;
    options.subtype = BCAST;

    set_header(HEADER);
    set_benchmark_name("osu_bcast");
    po_ret = process_options(argc, argv);

    if (PO_OKAY == po_ret && NONE != options.accel) {
        if (init_accel()) {
            fprintf(stderr, "Error initializing device\n");
            exit(EXIT_FAILURE);
        }
    }

    MPI_CHECK(MPI_Init(&argc, &argv));
    MPI_CHECK(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
    MPI_CHECK(MPI_Comm_size(MPI_COMM_WORLD, &numprocs));

    omb_graph_options_init(&omb_graph_options);
    switch (po_ret) {
        case PO_BAD_USAGE:
            print_bad_usage_message(rank);
            MPI_CHECK(MPI_Finalize());
            exit(EXIT_FAILURE);
        case PO_HELP_MESSAGE:
            print_help_message(rank);
            MPI_CHECK(MPI_Finalize());
            exit(EXIT_SUCCESS);
        case PO_VERSION_MESSAGE:
            print_version_message(rank);
            MPI_CHECK(MPI_Finalize());
            exit(EXIT_SUCCESS);
        case PO_OKAY:
            break;
    }

    if(numprocs < 2) {
        if (rank == 0) {
            fprintf(stderr, "This test requires at least two processes\n");
        }

        MPI_CHECK(MPI_Finalize());
        exit(EXIT_FAILURE);
    }

    check_mem_limit(numprocs);

    if (allocate_memory_coll((void**)&buffer, options.max_message_size, options.accel)) {
        fprintf(stderr, "Could Not Allocate Memory [rank %d]\n", rank);
        MPI_CHECK(MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE));
    }
    set_buffer(buffer, options.accel, 1, options.max_message_size);
    
    char env_warm[] = {"OSU_WARM=_\0____"};
    char env_iter[] = {"OSU_ITER=_\0____"};
    char env_icnt[] = {"OSU_ICNT=_\0____"};

    putenv(env_warm);
    putenv(env_iter);
    putenv(env_icnt);

    char *warmdown_env;
    int warmdown = 0;

    if((warmdown_env = getenv("OSU_WARMDOWN")) != NULL)
        warmdown = atoi(warmdown_env);

    print_preamble(rank);

    omb_papi_init(&papi_eventset);

    for (size = options.min_message_size; size <= options.max_message_size;
            size *= 2) {
        if(size > LARGE_MESSAGE_SIZE) {
            options.skip = options.skip_large;
            options.iterations = options.iterations_large;
        }

        // May god have mercy on my soul
        * (int *) (env_warm+strlen("OSU_WARM=")+2) = options.skip;
        * (int *) (env_iter+strlen("OSU_ITER=")+2) = options.iterations + warmdown;
        
        omb_graph_allocate_and_get_data_buffer(&omb_graph_data,
                &omb_graph_options, size, options.iterations);

        int root = 0;

        timer=0.0;
        for(i=0; i < options.iterations + options.skip + warmdown; i++) {
            if (i == options.skip)
                omb_papi_start(&papi_eventset);

            * (int *) (env_icnt+strlen("OSU_ICNT=")+2) = i;

            if(rank == root) {
                for(j = 0; j < size; j++)
                    buffer[j] = (char) (uint8_t) (i + j);
            }
            
            MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
            
            t_start = MPI_Wtime();
            MPI_CHECK(MPI_Bcast(buffer, size, MPI_CHAR, root, MPI_COMM_WORLD));
            t_stop = MPI_Wtime();
            
            MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
            
            if(i >= options.skip && i < options.iterations + options.skip) {
                timer+=t_stop-t_start;

                if(options.graph && 0 == rank)
                    omb_graph_data->data[i - options.skip] =
                        (t_stop - t_start) * 1e6;
            }
            
            if (i == options.iterations + options.skip)
                omb_papi_stop_and_print(&papi_eventset, size);
            
            root = (root + 1) % numprocs;
        }
        
        // -------
        
        latency = (timer * 1e6) / options.iterations;
        
        if(getenv("LAT_ALL") != NULL) {
            double lats[numprocs];
            
            MPI_CHECK(MPI_Gather(&latency, 1, MPI_DOUBLE,
                lats, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD));
            
            if(rank == 0) {
                for(int r = 0; r < numprocs; r++) {
                    char rank_str[] = "000";
                    
                    if(r >= 100) rank_str[0] = '0' + (r / 100);
                    if(r >= 10) rank_str[1] = '0' + (r / 10 % 10);
                    rank_str[2] = '0' + (r % 10);
                    
                    printf("%s: %d %.*f\n", rank_str, size,
                        FLOAT_PRECISION, lats[r]);
                }
            }
        }

        MPI_CHECK(MPI_Reduce(&latency, &min_time, 1,
            MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD));
        MPI_CHECK(MPI_Reduce(&latency, &max_time, 1,
            MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD));
        MPI_CHECK(MPI_Reduce(&latency, &avg_time, 1,
            MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD));

        avg_time = avg_time/numprocs;
        print_stats(rank, size, avg_time, min_time, max_time);

        if (options.graph && 0 == rank)
            omb_graph_data->avg = avg_time;

        MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
    }

    if (0 == rank && options.graph) {
        omb_graph_plot(&omb_graph_options, benchmark_name);
    }
    omb_graph_combined_plot(&omb_graph_options, benchmark_name);
    omb_graph_free_data_buffers(&omb_graph_options);
    omb_papi_free(&papi_eventset);

    free_buffer(buffer, options.accel);

    MPI_CHECK(MPI_Finalize());

    if (NONE != options.accel) {
        if (cleanup_accel()) {
            fprintf(stderr, "Error cleaning up device\n");
            exit(EXIT_FAILURE);
        }
    }

    return EXIT_SUCCESS;
}

/* vi: set sw=4 sts=4 tw=80: */
