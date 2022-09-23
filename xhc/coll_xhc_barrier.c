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
#include "mpi.h"

#include "ompi/constants.h"
#include "ompi/communicator/communicator.h"

#include "coll_xhc.h"

static void xhc_barrier_leader(xhc_comm_t *comms,
		int comm_count, xf_sig_t seq) {
	
	// Non-leader by default
	for(int i = 0; i < comm_count; i++)
		comms[i].is_coll_leader = false;
	
	for(int i = 0; i < comm_count; i++) {
		
		// In barrier, the comm manager is the leader
		if(comms[i].member_id != 0)
			break;
		
		comms[i].comm_ctrl->leader_seq = seq;
		comms[i].is_coll_leader = true;
	}
}

/* Hierarchical Barrier with seq/ack flags
 * ---------------------------------------
 * 1. Ranks write their coll_seq field to signal they have joined
 *    the collective. Leaders propagate this information towards
 *    the top-most comm's leader using the same method.
 *
 * 2. The top-most comm's leader (root) sets the comm's coll_ack
 *    field to signal, that all ranks have joined the barrier.
 *
 * 3. Leaders propagate the info towards the bottom-most comm, using
 *    the same method. Ranks wait on thei coll_ack flag, set their
 *    own ack, and exit the collective.
 * --------------------------------------- */
int mca_coll_xhc_barrier(ompi_communicator_t *ompi_comm,
		mca_coll_base_module_t *module) {
	
	if(((mca_coll_xhc_module_t *) module)->initialized == false) {
		int ret = xhc_lazy_init(module, ompi_comm);
		if(ret != 0) return OMPI_ERROR;
	}
	
	xhc_data_t *data = ((xhc_module_t *) module)->data;
	xf_sig_t pvt_seq = ++data->pvt_coll_seq;
	
	xhc_barrier_leader(data->comms, data->comm_count, pvt_seq);
	
	// 1. Upwards SEQ Wave
	for(int i = 0; i < data->comm_count; i++) {
		xhc_comm_t *xc = &data->comms[i];
		
		xc->my_member_ctrl->member_seq = pvt_seq;
		
		if(!xc->is_coll_leader)
			break;
		
		for(int m = 0; m < xc->size; m++) {
			if(m == xc->member_id)
				continue;
			
			/* Poll comm members and wait for them to join the barrier.
			 * No need for windowed comparison here; Ranks won't exit the
			 * barrier before the leader has set the coll_seq flag. */
			WAIT_FLAG(&xc->member_ctrl[m].member_seq, pvt_seq, 0);
		}
	}
	
	// 2. Wait for ACK (root won't wait!)
	for(int i = 0; i < data->comm_count; i++) {
		xhc_comm_t *xc = &data->comms[i];
		
		if(xc->is_coll_leader == false) {
			WAIT_FLAG(&xc->comm_ctrl->coll_ack, pvt_seq, 0);
			break;
		}
	}
	
	// 3. Trigger ACK Wave
	for(int i = 0; i < data->comm_count; i++) {
		xhc_comm_t *xc = &data->comms[i];
		
		/* Not actually necessary for the barrier operation, but
		 * good for consistency between all seq/ack numbers */
		xc->my_member_ctrl->member_ack = pvt_seq;
		
		if(!xc->is_coll_leader)
			break;
		
		xc->comm_ctrl->coll_ack = pvt_seq;
	}
	
	return OMPI_SUCCESS;
}
