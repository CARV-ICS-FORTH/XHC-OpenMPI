/*
 * Copyright (c) 2021-2022 Computer Architecture and VLSI Systems (CARV)
 *                         Laboratory, ICS Forth. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef MCA_COLL_XHC_EXPORT_H
#define MCA_COLL_XHC_EXPORT_H

#include "ompi_config.h"

#include <stdint.h>
#include <limits.h>

#include "mpi.h"

#include "ompi/mca/mca.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/communicator/communicator.h"
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/op/op.h"

#include "opal/mca/shmem/shmem.h"
#include "opal/mca/smsc/smsc.h"

#include "coll_xhc_atomic.h"

#define RETURN_WITH_ERROR(var, err, label) do {(var) = (err); goto label;} \
	while(0)

#define OBJ_RELEASE_IF_NOT_NULL(obj) do {if((obj) != NULL) OBJ_RELEASE(obj);} while(0)

#define REALLOC(p, s, t) do {void *tmp = realloc(p, (s)*sizeof(t)); \
	if(tmp) (p) = tmp;} while(0)

#define PROC_IS_LOCAL(proc, loc) (((proc)->super.proc_flags & (loc)) == (loc))
#define RANK_IS_LOCAL(comm, rank, loc) PROC_IS_LOCAL(ompi_comm_peer_lookup(comm, rank), loc)

// ---

#define OMPI_XHC_ACK_WIN 0

// Align to CPU cache line (portable way to obtain it?)
#define OMPI_XHC_CTRL_ALIGN 64

// Call opal_progress every this many ticks when busy-waiting
#define OMPI_XHC_OPAL_PROGRESS_CYCLE 10000

/* What should leaders do during Allreduce?
 * 0: Do not assist in reduction where leader, only propagate
 * 1: Assist in the reduction only if the leader on the top-level comm
 * 2: The top-level leader assists, other leaders assist only with the first chunk
 * 3: Leaders assist with the reduction as if normal members
 * 
 * Generally, we might not want leaders to be reducing, as that may lead to
 * load imbalance, since they will also have to reduce the comm's result(s) on
 * upper levels. Unless a leader is also one on all levels! (ie. he is the
 * top-level's leader) -- this specific leader should probably be assisting in
 * the reduction; otherwise, the *only* thing he will be doing is checking and
 * updating synchronization flags.
 * 
 * Regarding the load balancing problem, the leaders will actually not have
 * anything to do until the first chunk is reduced, so they might as well be
 * made to help the other members with this first chunk. Keep in mind though,
 * this might increase the memory load, and cause this first chunk to take
 * slightly more time to be produced.
 *
 * Brief experimental analysis declared option 1 or 2 to be the best ones
 * (pretty similar). Option 0 was next best, and option 3 was the worst. */
#define OMPI_XHC_ALLREDUCE_LEADER_REDUCE_POLICY 1

enum {
	OMPI_XHC_DYNAMIC_REDUCE_DISABLED,
	OMPI_XHC_DYNAMIC_REDUCE_NON_FLOAT,
	OMPI_XHC_DYNAMIC_REDUCE_ALL
};

#define OMPI_XHC_CICO_MAX (mca_coll_xhc_component.cico_max)

/* For other configuration options and default
 * values check coll_xhc_component.c */

// ---

BEGIN_C_DECLS

// ----------------------------------------

typedef opal_hwloc_locality_t xhc_loc_t;

typedef void * xhc_reg_t;

typedef struct mca_coll_xhc_component_t mca_coll_xhc_component_t;
typedef struct mca_coll_xhc_module_t mca_coll_xhc_module_t;
typedef struct mca_coll_xhc_module_t xhc_module_t;

typedef struct xhc_data_t xhc_data_t;
typedef struct xhc_comm_t xhc_comm_t;

typedef struct xhc_comm_ctrl_t xhc_comm_ctrl_t;
typedef struct xhc_member_ctrl_t xhc_member_ctrl_t;

typedef struct xhc_rank_info_t xhc_rank_info_t;
typedef struct xhc_member_info_t xhc_member_info_t;
typedef struct xhc_comm_info_t xhc_comm_info_t;

typedef struct xhc_reduce_queue_item_t xhc_rq_item_t;
typedef void xhc_copy_data_t;

typedef struct xhc_coll_fns_t xhc_coll_fns_t;

OMPI_MODULE_DECLSPEC extern mca_coll_xhc_component_t mca_coll_xhc_component;
OMPI_DECLSPEC OBJ_CLASS_DECLARATION(mca_coll_xhc_module_t);
OMPI_DECLSPEC OBJ_CLASS_DECLARATION(xhc_rq_item_t);

// ----------------------------------------

struct xhc_coll_fns_t {
	mca_coll_base_module_allreduce_fn_t coll_allreduce;
	mca_coll_base_module_t *coll_allreduce_module;
	
	mca_coll_base_module_barrier_fn_t coll_barrier;
	mca_coll_base_module_t *coll_barrier_module;
	
	mca_coll_base_module_bcast_fn_t coll_bcast;
	mca_coll_base_module_t *coll_bcast_module;
	
	mca_coll_base_module_reduce_fn_t coll_reduce;
	mca_coll_base_module_t *coll_reduce_module;
};

struct mca_coll_xhc_component_t {
	mca_coll_base_component_t super;
	
	int priority;
	bool print_info;
	bool set_hierarchy_envs;
	
	char *shmem_backing;
	
	bool dynamic_leader;
	int dynamic_reduce;
	bool force_reduce;
	
	bool uniform_chunks;
	size_t uniform_chunks_min;
	
	size_t cico_max;
	
	char *hierarchy_mca;
	char *chunk_size_mca;
};

struct mca_coll_xhc_module_t {
	mca_coll_base_module_t super;
	
	xhc_loc_t *hierarchy;
	int hierarchy_len;
	
	size_t *chunks;
	int chunks_len;
	
	xhc_data_t *data;
	bool initialized;
	
	xhc_coll_fns_t prev_colls;
};

struct xhc_data_t {
	xhc_comm_t *comms;
	int comm_count;
	
	void **cico_buffer_alloc;
	void *leader_cico_alloc;
	
	xhc_rank_info_t *rank_info;
	int rank_info_count;
	
	int ompi_rank;
	
	xf_sig_t pvt_coll_seq;
};

struct xhc_rank_info_t {
	ompi_proc_t *proc;
	mca_smsc_endpoint_t *smsc_ep;
	
	void *cico_buffer;
	opal_shmem_ds_t cico_ds;
};

struct xhc_comm_t {
	xhc_loc_t locality;
	size_t chunk_size;
	
	int size;
	int manager_rank;
	int member_id;
	
	// ---
	
	// Am I a leader in the current collective?
	bool is_coll_leader;
	
	// Have handshaked with all members in the current op? (useful to leader)
	bool all_joined;
	
	// Some collectives might override (lessen) the comm's chunk size
	int coll_chunk_elements;
	
	// How many of the members in the comm actually help with the (internal) reduction?
	int reduce_worker_count;
	
	struct xhc_member_info_t {
		xhc_reg_t *sbuf_reg;
		void *sbuf;
		
		xhc_reg_t *rbuf_reg;
		void *rbuf;
		
		bool init;
	} *member_info;
	
	opal_list_t *reduce_queue;
	
	// ---
	
	xhc_comm_ctrl_t *comm_ctrl;
	xhc_member_ctrl_t *member_ctrl;
	
	opal_shmem_ds_t ctrl_ds;
	
	// ---
	
	xhc_member_ctrl_t *my_member_ctrl; // = &member_ctrl[member_id]
	xhc_member_info_t *my_member_info; // = &member_info[member_id]
};

struct xhc_comm_ctrl_t {
	// We want leader_seq, coll_ack, coll_seq to all lie in their own cache lines
	
	volatile xf_sig_t leader_seq;
	
	volatile xf_sig_t coll_ack __attribute__((aligned(OMPI_XHC_CTRL_ALIGN)));
	
	volatile xf_sig_t coll_seq __attribute__((aligned(OMPI_XHC_CTRL_ALIGN)));
	
	/* - Reason *NOT* to keep below fields in the same cache line as coll_seq:
	 *   
	 *   While members busy-wait on leader's coll_seq, initializing the rest of
	 *   the fields will trigger cache-coherency-related "invalidate" and then
	 *   "read miss" messages, for each store.
	 * 
	 * - Reason to *DO* keep below fields in the same cache line as coll_seq:
	 *   
	 *   Members load from coll_seq, and implicitly fetch the entire cache
	 *   line, which also contains the values of the other fields, that will
	 *   also need to be loaded soon.
	 * 
	 * (not 100% sure of my description here)
	 * 
	 * Bcast seemed to perform better with the second option, so I went with
	 * that one. The best option might also be influenced by the ranks' order
	 * of entering in the operation.
	 */
	
	// "Guarded" by members' coll_seq
	volatile int leader_id;
	volatile int leader_rank;
	volatile int cico_id;
	
	void* volatile data_vaddr;
	volatile xf_size_t bytes_ready;
	
	char access_token[];
} __attribute__((aligned(OMPI_XHC_CTRL_ALIGN)));

struct xhc_member_ctrl_t {
	volatile xf_sig_t member_ack; // written by member
	
	// written by member, at beginning of operation
	volatile xf_sig_t member_seq __attribute__((aligned(OMPI_XHC_CTRL_ALIGN)));
	volatile int rank;
	
	void* volatile sbuf_vaddr;
	void* volatile rbuf_vaddr;
	volatile int cico_id;
	
	// reduction progress counters, written by member
	volatile xf_int_t reduce_ready;
	volatile xf_int_t reduce_done;
} __attribute__((aligned(OMPI_XHC_CTRL_ALIGN)));

struct xhc_comm_info_t {
	int manager_rank;
	opal_shmem_ds_t ctrl_ds;
};

struct xhc_reduce_queue_item_t {
	opal_list_item_t super;
	int member;
	int count;
};

// ----------------------------------------

// coll_xhc_component.c
// --------------------

int mca_coll_xhc_component_init_query(bool enable_progress_threads,
	bool enable_mpi_threads);

int xhc_component_parse_hierarchy(const char *val_str,
	xhc_loc_t **vals_dst, int *len_dst);
int xhc_component_parse_chunk_sizes(const char *val_str,
	size_t **vals_dst, int *len_dst);

// coll_xhc_module.c
// -----------------

mca_coll_base_module_t *mca_coll_xhc_module_comm_query(
	ompi_communicator_t *comm, int *priority);

int mca_coll_xhc_module_enable(mca_coll_base_module_t *module,
	ompi_communicator_t *comm);

xhc_coll_fns_t xhc_module_set_fns(ompi_communicator_t *comm,
	xhc_coll_fns_t new);

int xhc_module_prepare_hierarchy(mca_coll_xhc_module_t *module,
	ompi_communicator_t *comm);

// coll_xhc.c
// ----------

int xhc_lazy_init(mca_coll_base_module_t *module, ompi_communicator_t *comm);
void xhc_destroy_data(mca_coll_xhc_module_t *module);

void *xhc_get_cico(xhc_rank_info_t *rank_info, int rank);

int xhc_copy_expose_region(void *base, size_t len, xhc_copy_data_t **region_data);
void xhc_copy_region_post(void *dst, xhc_copy_data_t *region_data);
int xhc_copy_from(xhc_rank_info_t *rank_info, void *dst,
	void *src, size_t size, void *access_token);
void xhc_copy_close_region(xhc_copy_data_t *region_data);

void *xhc_get_registration(xhc_rank_info_t *rank_info,
	void *peer_vaddr, size_t size, xhc_reg_t **reg);
void xhc_return_registration(xhc_reg_t *reg);

// Primitives (respective file)
// ----------------------------

int mca_coll_xhc_bcast(void *buf, int count, ompi_datatype_t *datatype,
	int root, ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_xhc_barrier(ompi_communicator_t *ompi_comm,
	mca_coll_base_module_t *module);

int mca_coll_xhc_reduce(const void *sbuf, void *rbuf,
	int count, ompi_datatype_t *datatype, ompi_op_t *op, int root,
	ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_xhc_allreduce(const void *sbuf, void *rbuf,
	int count, ompi_datatype_t *datatype, ompi_op_t *op,
	ompi_communicator_t *comm, mca_coll_base_module_t *module);

// Miscellaneous
// -------------

int mca_coll_xhc_allreduce_internal(const void *sbuf, void *rbuf, int count,
	ompi_datatype_t *datatype, ompi_op_t *op, ompi_communicator_t *ompi_comm,
	mca_coll_base_module_t *module, bool require_bcast);

// ----------------------------------------

// Rollover-safe check that flag has reached/exceeded thresh, with max deviation
static inline bool CHECK_FLAG(volatile xf_sig_t *flag,
		xf_sig_t thresh, xf_sig_t win) {
	
	// This is okay because xf_sig_t is unsigned. Take care.
	// The cast's necessity is dependent on the size of xf_sig_t
	return ((xf_sig_t) (*flag - thresh) <= win);
}

static inline void WAIT_FLAG(volatile xf_sig_t *flag,
		xf_sig_t thresh, xf_sig_t win) {
	bool ready = false;
	
	do {
		for(int i = 0; i < OMPI_XHC_OPAL_PROGRESS_CYCLE; i++) {
			if(CHECK_FLAG(flag, thresh, win)) {
				ready = true;
				break;
			}
			
			/* xf_sig_t f = *flag;
			if(CHECK_FLAG(&f, thresh, win)) {
				ready = true;
				break;
			} else if(CHECK_FLAG(&f, thresh, 1000))
				printf("Debug: Flag check with window %d failed, "
					"but succeeded with window 1000. flag = %d, "
					"thresh = %d\n", win, f, thresh); */
		}
		
		if(!ready)
			opal_progress();
	} while(!ready);
}

// ----------------------------------------

END_C_DECLS

#endif
