#define BENCHMARK "OSU MPI_Send (data-varying)"
/*
 * Copyright (C) 2002-2021 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University. 
 *
 * Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level OMB directory.
 */
#include <osu_util_mpi.h>

int main (int argc, char *argv[]) {
	int rank, numprocs, i, k;
	int size;
	MPI_Status reqstat;
	char *s_buf, *r_buf;
	double t_start, t_end, t_total;
	int po_ret = 0;
	
	options.bench = PT2PT;
	options.subtype = LAT;
	
	set_header(HEADER);
	set_benchmark_name("osu_latency");
	
	po_ret = process_options(argc, argv);
	
	if (PO_OKAY == po_ret && NONE != options.accel) {
		if (init_accel()) {
			fprintf(stderr, "Error initializing device\n");
			exit(EXIT_FAILURE);
		}
	}
	
	MPI_CHECK(MPI_Init(&argc, &argv));
	MPI_CHECK(MPI_Comm_size(MPI_COMM_WORLD, &numprocs));
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
				break;
			case PO_HELP_MESSAGE:
				print_help_message(rank);
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
	
	if (numprocs != 2) {
		if (rank == 0) {
			fprintf(stderr, "This test requires exactly two processes\n");
		}
		
		MPI_CHECK(MPI_Finalize());
		exit(EXIT_FAILURE);
	}
	
	if (options.buf_num == SINGLE) {
		if (allocate_memory_pt2pt(&s_buf, &r_buf, rank)) {
			/* Error allocating memory */
			MPI_CHECK(MPI_Finalize());
			exit(EXIT_FAILURE);
		}
	}
	
	print_header(rank, LAT);
	
	/* MPI_Send test */
	for(size = options.min_message_size; size <= options.max_message_size;
			size = (size != 0 ? size * 2 : 1)) {
		
		if(options.buf_num == MULTIPLE) {
			if(allocate_memory_pt2pt_size(&s_buf, &r_buf, rank, size)) {
				/* Error allocating memory */
				MPI_CHECK(MPI_Finalize());
				exit(EXIT_FAILURE);
			}
		}
		
		if(size > LARGE_MESSAGE_SIZE) {
			options.skip = options.skip_large;
			options.iterations = options.iterations_large;
		}
		
		t_total = 0;
		
		for(i = 0; i < options.iterations + options.skip + 1; i++) {
			for(k = 0; k < size; k++)
				s_buf[k] = (char) (uint8_t) (i + k + rank);
			
			MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
			
			if(rank == 0) {
				t_start = MPI_Wtime();
				MPI_CHECK(MPI_Send(s_buf, size, MPI_CHAR, 1, 1, MPI_COMM_WORLD));
				t_end = MPI_Wtime();
				
				if(i >= options.skip && i != options.iterations + options.skip)
					t_total += t_end - t_start;
			} else if(rank == 1)
				MPI_CHECK(MPI_Recv(r_buf, size, MPI_CHAR, 0, 1, MPI_COMM_WORLD, &reqstat));
			
			MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
		}
		
		if(rank == 0) {
			double latency = (t_total * 1e6) / options.iterations;
			printf("%-*d%*.*f\n", 10, size, FIELD_WIDTH, FLOAT_PRECISION, latency);
		}
		
		if(options.buf_num == MULTIPLE)
			free_memory(s_buf, r_buf, rank);
	}
	
	if(options.buf_num == SINGLE)
		free_memory(s_buf, r_buf, rank);
	
	MPI_CHECK(MPI_Finalize());
	
	if(options.accel != NONE) {
		if(cleanup_accel()) {
			fprintf(stderr, "Error cleaning up device\n");
			exit(EXIT_FAILURE);
		}
	}
	
	return EXIT_SUCCESS;
}
