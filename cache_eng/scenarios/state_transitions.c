#define REMOTE(x) (comm_size/2 + (x))

void init_modified(void) {
	if(rank == 0) {
		WRITE_LINES();
		SET(1);
	} else
		WAIT(0, 1);
}

// SC_S_1: local read
void sc_s_l(void) {
	init_modified();
	
	if(rank == 1) {
		READ_LINES();
		WRITE_LINES();
	}
}

// SC_S_2: remote read
void sc_s_r(void) {
	init_modified();
	
	if(rank == REMOTE(0)) {
		READ_LINES();
		WRITE_LINES();
	}
}

// SC_S_3: remote read, after local read
void sc_s_lr(void) {
	init_modified();
	
	if(rank == 1) {
		READ_LINES();
		SET(1);
	} else if(rank == REMOTE(0)) {
		WAIT(1, 1);
		READ_LINES();
		WRITE_LINES();
	}
}

// SC_S_4: local read, after remote read
void sc_s_rl(void) {
	init_modified();
	
	if(rank == REMOTE(0)) {
		READ_LINES();
		SET(1);
	} else if(rank == 1) {
		WAIT(REMOTE(0), 1);
		READ_LINES();
		WRITE_LINES();
	}
}
