#define REMOTE(x) (comm_size/2 + (x))

void init_shared(void) {
	for(int r = 0; r < comm_size; r++) {
		if(rank == r) {
			if(r > 0) WAIT(r-1, 1);
			READ_LINES();
			SET(1);
		}
	}
	WAIT(comm_size-1, 1);
	FLAG_RESET();
}

void init_modified(void) {
	if(rank == 0) {
		WRITE_LINES();
		SET(1);
	} else
		WAIT(0, 1);
}

/* Init_shared is required before init_modified, for accurate results
 * in this scenario. For example, if only root writes the line and a
 * remote reads it, an optimization is triggered and the behaviour
 * changes. This actually emulated the behaviour of a broadcast more
 * closely. In the previous iteration, all readers will have read the
 * line (init_shared). Then the root will write the line before or at
 * the beginning of the new iteration (init_modified). Then, finally,
 * the readers begin fetching the line. */

// SC_R_1: init = all shared, root write, action = remote read
void sc_r_1(void) {
	init_shared();
	init_modified();
	
	if(rank == REMOTE(0)) {
		START_COUNTERS();
		READ_LINES();
		SET(1);
	}
}

// SC_R_2: init = all shared, root write, local read, action = remote read
void sc_r_2(void) {
	init_shared();
	init_modified();
	
	if(rank == 1) {
		READ_LINES();
		SET(1);
	} else if(rank == REMOTE(0)) {
		WAIT(1, 1);
		START_COUNTERS();
		READ_LINES();
		SET(1);
	}
}

// SC_R_3: init = all shared, root write, remote read, action = local read
void sc_r_3(void) {
	init_shared();
	init_modified();
	
	if(rank == REMOTE(0)) {
		READ_LINES();
		SET(1);
	} else if(rank == 1) {
		WAIT(REMOTE(0), 1);
		START_COUNTERS();
		READ_LINES();
		SET(1);
	}
}
