#include <mpi.h>

void mpi_bcast(void) {
	static char env_osu_warm[] = {"OSU_WARM=_\0____"};
	static char env_osu_iter[] = {"OSU_ITER=_\0____"};
	static char env_osu_icnt[] = {"OSU_ICNT=_\0____"};
	
	if(iter == 0) {
		putenv(env_osu_warm);
		putenv(env_osu_iter);
		putenv(env_osu_icnt);
		
		// May god have mercy on my soul
		* (int *) (env_osu_warm+strlen("OSU_WARM=")+2) = warmup;
		* (int *) (env_osu_iter+strlen("OSU_ITER=")+2) = iterations;
	}
	
	* (int *) (env_osu_icnt+strlen("OSU_ICNT=")+2) = iter;
	
	if(rank == 0) {
		memset(priv_data, 0, ncache_lines * 64);
		START_COUNTERS();
		SET(1);
	} else
		WAIT(0, 1);
	
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Bcast((void *) priv_data, ncache_lines * 64, MPI_BYTE, 0, MPI_COMM_WORLD);
	MPI_Barrier(MPI_COMM_WORLD);
}
