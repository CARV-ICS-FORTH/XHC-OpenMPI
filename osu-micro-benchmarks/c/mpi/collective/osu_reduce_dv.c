#define BENCHMARK "OSU MPI%s Reduce Latency Test w/ data-varying"
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
	int i, j, numprocs, rank, size;
	double latency = 0.0, t_start = 0.0, t_stop = 0.0;
	double timer=0.0;
	double avg_time = 0.0, max_time = 0.0, min_time = 0.0;
	float *sendbuf, *recvbuf;
	int po_ret;
	int errors = 0, local_errors = 0;
	size_t bufsize;
	omb_graph_options_t omb_graph_options;
	omb_graph_data_t *omb_graph_data = NULL;
	int papi_eventset = OMB_PAPI_NULL;
	
	set_header(HEADER);
	set_benchmark_name("osu_reduce");
	
	options.bench = COLLECTIVE;
	options.subtype = REDUCE;
	
	po_ret = process_options(argc, argv);
	
	if (PO_OKAY == po_ret && NONE != options.accel) {
		if (init_accel()) {
			fprintf(stderr, "Error initializing device\n");
			exit(EXIT_FAILURE);
		}
	}
	
	// Extra options
	// -------------

	char *env;
	int root = 0;
	long step = -1;

	if((env = getenv("OSU_MB_ROOT")) != NULL)
		root = atoi(env);

	if((env = getenv("OSU_MB_STEP")) != NULL)
		step = atol(env);

	bool lat_all = (getenv("OSU_MB_LAT_ALL") != NULL);
	bool do_barriers = (getenv("OSU_MB_NO_BARRIER") == NULL);

	/* Hacky interface for benchmark to
	 * collectives library communication. */

	char env_active[] = {"OSU_MB_ACTIVE_ITER=_\0____"};
	char env_last[] = {"OSU_MB_LAST_ITER=_\0____"};

	putenv(env_active);
	putenv(env_last);

	int32_t *env_active_int = (int32_t *) (env_active + strlen(env_active) + 1);
	int32_t *env_last_int = (int32_t *) (env_last + strlen(env_last) + 1);

	*env_active_int = 0;
	*env_last_int = 0;

	// ---

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
	
	options.min_message_size /= sizeof(float);
	if (options.min_message_size < MIN_MESSAGE_SIZE) {
		options.min_message_size = MIN_MESSAGE_SIZE;
	}
	
	bufsize = sizeof(float)*(options.max_message_size/sizeof(float));
	if (allocate_memory_coll((void**)&sendbuf, bufsize, options.accel)) {
		fprintf(stderr, "Could Not Allocate Memory [rank %d]\n", rank);
		MPI_CHECK(MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE));
	}
	set_buffer(sendbuf, options.accel, 1, bufsize);
	
	bufsize = sizeof(float)*(options.max_message_size/sizeof(float));
	if (allocate_memory_coll((void**)&recvbuf, bufsize, options.accel)) {
		fprintf(stderr, "Could Not Allocate Memory [rank %d]\n", rank);
		MPI_CHECK(MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE));
	}
	set_buffer(recvbuf, options.accel, 0, bufsize);
	
	// ---
	
	print_preamble(rank);
	omb_papi_init(&papi_eventset);
	
	for (size = options.min_message_size;
		size * sizeof(float) <= options.max_message_size;
		size = (step > 0 ? (size + step / sizeof(float)) : (size * 2))) {
		
		if(size > LARGE_MESSAGE_SIZE) {
			options.skip = options.skip_large;
			options.iterations = options.iterations_large;
		}
		
		omb_graph_allocate_and_get_data_buffer(&omb_graph_data,
				&omb_graph_options, size * sizeof(float), options.iterations);
		
		*env_active_int = 0;
		*env_last_int = 0;
		
		timer=0.0;
		for(i=0; i < options.iterations + options.skip; i++) {
			if (i == options.skip)
				omb_papi_start(&papi_eventset);
			
			for(j = 0; j < size; j++)
				sendbuf[j] = (float) (uint8_t) (i + j);
			
			if(do_barriers)
				MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
			
			if(i >= options.skip)
				*env_active_int = 1;
			
			if(i == options.iterations + options.skip - 1)
				*env_last_int = 1;
			
			t_start = MPI_Wtime();
			MPI_CHECK(MPI_Reduce(sendbuf, recvbuf, size,
				MPI_FLOAT, MPI_SUM, root, MPI_COMM_WORLD));
			t_stop = MPI_Wtime();
			
			*env_active_int = 0;
			
			if(do_barriers)
				MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
			
			volatile float x = 0;
			for(j = 0; j < size; j++)
				x += recvbuf[j];
			
			if(i >= options.skip) {
				timer+=t_stop-t_start;
				
				if(options.graph && 0 == rank)
					omb_graph_data->data[i - options.skip] =
						(t_stop - t_start) * 1e6;
			}
		}
		
		// -------
		
		omb_papi_stop_and_print(&papi_eventset, size);
		
		latency = (timer * 1e6) / options.iterations;
		
		if(lat_all) {
			double lats[numprocs];
			
			MPI_CHECK(MPI_Gather(&latency, 1, MPI_DOUBLE,
				lats, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD));
			
			if(rank == 0) {
				for(int r = 0; r < numprocs; r++) {
					char rank_str[] = "000";
					
					if(r >= 100) rank_str[0] = '0' + (r / 100);
					if(r >= 10) rank_str[1] = '0' + (r / 10 % 10);
					rank_str[2] = '0' + (r % 10);
					
					printf("%s: %zu %.*f\n", rank_str, size * sizeof(float),
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
		print_stats(rank, size * sizeof(float), avg_time, min_time, max_time);
		
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
	
	free_buffer(sendbuf, options.accel);
	free_buffer(recvbuf, options.accel);
	
	MPI_CHECK(MPI_Finalize());
	
	if (NONE != options.accel) {
		if (cleanup_accel()) {
			fprintf(stderr, "Error cleaning up device\n");
			exit(EXIT_FAILURE);
		}
	}
	
	return EXIT_SUCCESS;
}
