/*
 * Copyright (c) 2021-2022 Computer Architecture and VLSI Systems (CARV)
 *                         Laboratory, ICS Forth. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "mpi.h"

#include "ompi/communicator/communicator.h"
#include "ompi/constants.h"
#include "ompi/op/op.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/coll_tags.h"
#include "ompi/mca/pml/pml.h"

#include "opal/util/bit_ops.h"
#include "opal/sys/atomic.h"

#include "coll_smhc.h"

// ----------------------------------------



typedef mca_coll_smhc_flag_t sh_flag_t;

// ----------------------------------------

static void smhc_store_32(uint32_t *dst, uint32_t v) {
	opal_atomic_wmb();
	*dst = v;
}

static uint32_t smhc_load_32(uint32_t *src) {
	uint32_t v = *src;
	opal_atomic_rmb();
	return v;
}

static int smhc_open(char *file_name_buf, size_t size) {
	int fd = -1;
	
	opal_jobid_t jobid = ompi_proc_local()->super.proc_name.jobid;
	
	for(int i = 0; i < 128; i++) {
		snprintf(file_name_buf, size, "/ompi_smhc.%08x-%d", jobid, i);
		
		errno = 0;
		fd = shm_open(file_name_buf, O_CREAT | O_EXCL | O_RDWR,
			S_IRUSR | S_IWUSR);
		
		if(errno != EEXIST)
			break;
	}
	
	return fd;
}

static int smhc_lazy_init(struct ompi_communicator_t *comm,
		mca_coll_base_module_t *module) {
	
	int rank = ompi_comm_rank(comm);
	int comm_size = ompi_comm_size(comm);
	
	mca_coll_smhc_module_t *smhc_module = (mca_coll_smhc_module_t *) module;
	mca_coll_smhc_flat_data_t *data;
	
	char shm_file_name[SMHC_SHM_FILE_MAXLEN] = {0};
	
	int return_code = 0;
	int fd = -1;
	
	data = malloc(sizeof(mca_coll_smhc_flat_data_t));
	if(data == NULL) return -1;
	
	data->shm_size = (2 * comm_size + 1) * sizeof(sh_flag_t)
		+ (SMHC_CHUNK * SMHC_PIPELINE);
	
	/* Rank 0 (regardless of the root of the collective that triggered the
	 * init) creates the identifier of the shm region, sizes it up accordingly,
	 * (rank times 32-bit flags x2 + bcast buffer, no reduce atm). The flags
	 * are also first initialized here. The gather flags will also be taken
	 * advantage of the initialization process later on. */
	if(rank == 0) {
		if((fd = smhc_open(shm_file_name, SMHC_SHM_FILE_MAXLEN - 1)) == -1) {
			return_code = -11;
			goto end;
		}
		
		if(ftruncate(fd, data->shm_size) == -1) {
			return_code = -12;
			goto end;
		}
		
		data->shm_base = mmap(NULL, data->shm_size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
		
		if(data->shm_base == MAP_FAILED) {
			return_code = -13;
			goto end;
		}
		
		memset(data->shm_base, 0, data->shm_size);
	}
	
	/* Leverage a simple existing broadcast implementation to share the
	 * name of the shm region's handle with the other nodes. */
	ompi_coll_base_bcast_intra_basic_linear(shm_file_name,
		sizeof(shm_file_name), MPI_CHAR, 0, comm, module);
	
	/* Child ranks attach to the shm region. The shm file handle is no
	 * longer necessary. */
	if(rank != 0) {
		if((fd = shm_open(shm_file_name, O_RDWR, 0)) == -1) {
			return_code = -21;
			goto end;
		}
		
		data->shm_base = mmap(NULL, data->shm_size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
		
		if(data->shm_base == MAP_FAILED) {
			return_code = -23;
			goto end;
		}
		
		close(fd);
	}
	
	// Wait for children to attach
	ompi_coll_base_barrier_intra_basic_linear(comm, module);
	
	if(rank == 0) {
		close(fd);
		fd = -1;
		
		if(shm_unlink(shm_file_name) == -1) {
			opal_output_verbose(10, ompi_coll_base_framework.framework_output,
				"coll:smhc:smhc_lazy_init unlink of shm file '%s' failed",
				shm_file_name);
		}
	}
	
	// Definitely watch out for the pointer arithmetic here!
	data->sh_release      = ((sh_flag_t *) data->shm_base) + 0;
	data->sh_gather       = ((sh_flag_t *) data->shm_base) + comm_size;
	data->sh_root_gather  = ((sh_flag_t *) data->shm_base) + 2 * comm_size;
	data->sh_bcast_buffer = ((sh_flag_t *) data->shm_base) + 2 * comm_size + 1;
	
	data->pvt_release_state = 0;
	data->pvt_gather_state = 0;
	
	smhc_module->flat_data = data;
	smhc_module->flat_initialized = true;
	
	end:
	
	if(return_code != 0) {
		opal_output_verbose(10, ompi_coll_base_framework.framework_output,
			"coll:smhc: ERROR %d at smhc_lazy_init [%s]",
			return_code, strerror(errno));
		
		if(fd != -1)
			close(fd);
		
		if(fd != -1 && rank == 0)
			shm_unlink(shm_file_name);
	}
	
	return return_code;
}

// ----------------------------------------

int mca_coll_smhc_bcast_flat(void *buf, int count,
		struct ompi_datatype_t *datatype, int root,
		struct ompi_communicator_t *comm,
		mca_coll_base_module_t *module) {
	
	int rank = ompi_comm_rank(comm);
	int comm_size = ompi_comm_size(comm);
	
	if(((mca_coll_smhc_module_t*) module)->flat_initialized == false) {
		int ret = smhc_lazy_init(comm, module);
		if(ret != 0) return OMPI_ERROR;
	}
	
	mca_coll_smhc_flat_data_t *data =
		((mca_coll_smhc_module_t*) module)->flat_data;
	
	size_t dtype_size;
	ompi_datatype_type_size(datatype, &dtype_size);
	
	size_t msg_bytes = count * dtype_size;
	size_t chunk_size = SMHC_CHUNK;
	
	/* Remember the initial state, so that we may use the delta
	 * to determine the chunks IDs. We need this because the state
	 * may/will be left in a non SMHC_PIPELINE-bit aligned status. */
	uint32_t initial_pvt_state = data->pvt_release_state;
	
	/* Wait until the previous operation is over, before touching
	 * the bcast buffer or the communication flags (keep in mind
	 * this op's root might be different than last one's). */
	if(rank == root)
		while(smhc_load_32(&data->sh_root_gather->flag) != initial_pvt_state) {}
	
	for(size_t b = 0; b < msg_bytes; b += chunk_size) {
		data->pvt_release_state++;
		
		/* We know where inside the bcast buffer to place the chunk from
		 * the release state. We place chunks in a round-robin fashion,
		 * so a simple modulo gives us the inter-pipeline stage. */
		
		int chunk_id = (data->pvt_release_state - initial_pvt_state - 1)
			% SMHC_PIPELINE;
		
		size_t sh_buf_offset = chunk_id * chunk_size;
		
		void *user_buf_p = (void *) ((uintptr_t) buf + b);
		void *sh_buf_p = (void *) ((uintptr_t) data->sh_bcast_buffer + sh_buf_offset);
		
		size_t to_copy = (b + chunk_size < msg_bytes ? chunk_size : (msg_bytes - b));
		
		if(rank == root) {
			memcpy(sh_buf_p, user_buf_p, to_copy);
			
			// Release step for this chunk
			for(int r = 0; r < comm_size; r++) {
				if(r == rank) continue;
				
				smhc_store_32(&data->sh_release[r].flag, data->pvt_release_state);
			}
			
			// We don't perform a gather step until the pipeline is full
			if(b >= chunk_size * (SMHC_PIPELINE - 1)) {
				
				/* Gather step for some previous chunk (probably not the
				 * one we just released, because there is pipelining).*/
				
				data->pvt_gather_state++;
				
				for(int r = 0; r < comm_size; r++) {
					if(r == rank) continue;
					
					uint32_t sh_g, pvt_g;
					
					/* TODO Should we poll different children
					 * instead of busy-waiting on each one? */
					
					do {
						sh_g = smhc_load_32(&data->sh_gather[r].flag);
						pvt_g = data->pvt_gather_state;
					} while((uint32_t) (sh_g - pvt_g) >= SMHC_PIPELINE);
				}
				
				/* Gather step for this rank's parent. Not yet relevant as
				 * trees are not yet supported. But let's leave it as a
				 * reminder. */
				smhc_store_32(&data->sh_gather[rank].flag,
						data->pvt_gather_state);
			}
		} else {
			uint32_t sh_r, pvt_r;
			
			// Wrap-around safe comparison
			do {
				sh_r = smhc_load_32(&data->sh_release[rank].flag);
				pvt_r = data->pvt_release_state;
			} while((uint32_t) (sh_r - pvt_r) >= SMHC_PIPELINE);
			
			memcpy(user_buf_p, sh_buf_p, to_copy);
			
			data->pvt_gather_state++;
			
			smhc_store_32(&data->sh_gather[rank].flag, data->pvt_gather_state);
		}
	}
	
	/* The main loop, for the root, is release-centric. Meaning at each
	 * iteration it releases a chunk, but does not start gathering until
	 * the pipeline is full. So after there are no more chunks to copy,
	 * there might exist a release-gather state mismatch. */
	if(data->pvt_release_state != data->pvt_gather_state) {
		assert(rank == root);
		
		data->pvt_gather_state = data->pvt_release_state;
		
		for(int r = 0; r < comm_size; r++) {
			if(r == rank) continue;
			
			while(smhc_load_32(&data->sh_gather[r].flag)
					!= data->pvt_gather_state) {}
		}
		
		smhc_store_32(&data->sh_gather[rank].flag, data->pvt_gather_state);
	}
	
	// Operations up until <gather_state> are officially over!
	if(rank == root)
		smhc_store_32(&data->sh_root_gather->flag, data->pvt_gather_state);
	
	assert(data->pvt_release_state == data->pvt_gather_state);
	
	return OMPI_SUCCESS;
}
