void init_modified(void) {
	if(rank == 0) {
		WRITE_LINES();
		SET(1);
	} else
		WAIT(0, 1);
}

// SC_L_1: init = root write, action = local read
void sc_l_1(void) {
	init_modified();
	
	if(rank == 1) {
		START_COUNTERS();
		READ_LINES();
		SET(1);
	}
}

// SC_L_2: init = root write, local read, action = 2nd local read
void sc_l_2(void) {
	init_modified();
	
	if(rank == 1) {
		READ_LINES();
		SET(1);
	} else if(rank == 2) {
		WAIT(1, 1);
		START_COUNTERS();
		READ_LINES();
		SET(1);
	}
}

// SC_L_3: init = root write, local read, 2nd local read, action = 3rd local read
void sc_l_3(void) {
	init_modified();
	
	if(rank == 1) {
		READ_LINES();
		SET(1);
	} else if(rank == 2) {
		WAIT(1, 1);
		READ_LINES();
		SET(1);
	} else if(rank == 3) {
		WAIT(2, 1);
		START_COUNTERS();
		READ_LINES();
		SET(1);
	}
}
