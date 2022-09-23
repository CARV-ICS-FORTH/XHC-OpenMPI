/*
 * Copyright (c) 2021-2022 Computer Architecture and VLSI Systems (CARV)
 *                         Laboratory, ICS Forth. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef MCA_COLL_XBRC_EXPORT_H
#define MCA_COLL_XBRC_EXPORT_H

#include "ompi_config.h"

#include <stdint.h>
#include <limits.h>
#include <xpmem.h>

#include "mpi.h"

#include "ompi/mca/mca.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/communicator/communicator.h"
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/op/op.h"

#include "opal/mca/rcache/base/base.h"

#define RETURN_WITH_ERROR(var, err, label) do {(var) = (err); goto label;} while(0)

// Rollover-safe busy-wait 'till flag >= thresh
#define CHECK_FLAG(flag, thresh, win) ((uint) ((flag) - (thresh)) <= (win))

#define WAIT_FLAG(flag, thresh, win) do {              \
	bool ready = false;                                \
	                                                   \
	do {                                               \
		for(int __i = 0; __i < 100000; __i++) {        \
			if(CHECK_FLAG(flag, thresh, win)) {        \
				ready = true;                          \
				break;                                 \
			}                                          \
		}                                              \
		                                               \
		if(!ready)                                     \
			opal_progress();                           \
	} while(!ready);                                   \
} while(0)

// This is 2MB on btl/vader, for a reason that is unknown to me
#define OMPI_XBRC_XPMEM_ALIGN (sysconf(_SC_PAGE_SIZE))

// Align to CPU cache line
#define OMPI_XBRC_CTRL_ALIGN 64

// Allreduce design specifies L2-cache-Line-size granularity
#define OMPI_XBRC_CHUNK_ALIGN 64

BEGIN_C_DECLS

// ----------------------------------------

typedef struct xpmem_addr xpmem_addr_t;
typedef sig_atomic_t pf_sig_t;

typedef struct mca_coll_xbrc_component_t mca_coll_xbrc_component_t;
typedef struct mca_coll_xbrc_module_t mca_coll_xbrc_module_t;
typedef struct mca_coll_xbrc_module_t xbrc_module_t;

typedef struct xbrc_data_t xbrc_data_t;

typedef struct xbrc_rank_info_t xbrc_rank_info_t;
typedef struct xbrc_reduce_info_t xbrc_reduce_info_t;

typedef struct xbrc_rank_ctrl_t xbrc_rank_ctrl_t;

typedef struct xbrc_reg_t xbrc_reg_t;

OMPI_MODULE_DECLSPEC extern mca_coll_xbrc_component_t mca_coll_xbrc_component;
OMPI_DECLSPEC OBJ_CLASS_DECLARATION(mca_coll_xbrc_module_t);

// ----------------------------------------

struct mca_coll_xbrc_component_t {
	mca_coll_base_component_t super;
	
	int priority;
	
	size_t xpmem_align;
};

struct mca_coll_xbrc_module_t {
	mca_coll_base_module_t super;
	
	xbrc_data_t *data;
	bool initialized;
};

struct xbrc_rank_info_t {
	xpmem_segid_t seg_id;
	uintptr_t max_address;
	
	xpmem_apid_t apid;
	
	mca_rcache_base_module_t *rcache;
	
	struct xbrc_reduce_info_t {
		xbrc_reg_t *sbuf_reg;
		void *sbuf;
		
		xbrc_reg_t *rbuf_reg;
		void *rbuf;
	} reduce_info;
};

struct xbrc_rank_ctrl_t {
	volatile pf_sig_t coll_seq;
	
	void* volatile sbuf_vaddr;
	void* volatile rbuf_vaddr;
	
	// Used as bool
	volatile pf_sig_t reduction_complete;
	volatile pf_sig_t copy_complete;
} __attribute__((aligned(OMPI_XBRC_CTRL_ALIGN)));

struct xbrc_data_t {
	pf_sig_t pvt_coll_seq;
	
	int ompi_rank;
	int ompi_size;
	
	xbrc_rank_info_t *rank_info;
	
	xbrc_rank_ctrl_t *rank_ctrl;
	volatile pf_sig_t *ack_num;
};

struct xbrc_reg_t {
	mca_rcache_base_registration_t super;
	void *attach_base;
};

// ----------------------------------------

int mca_coll_xbrc_component_init_query(bool enable_progress_threads,
	bool enable_mpi_threads);


mca_coll_base_module_t *mca_coll_xbrc_module_comm_query(
	ompi_communicator_t *comm, int *priority);

int mca_coll_xbrc_module_enable(mca_coll_base_module_t *module,
	ompi_communicator_t *comm);


int mca_coll_xbrc_lazy_init(mca_coll_base_module_t *module,
	ompi_communicator_t *comm);

void mca_coll_xbrc_destroy_data(mca_coll_xbrc_module_t *module);

void *mca_coll_xbrc_get_registration(mca_rcache_base_module_t *rcache,
	void *peer_vaddr, size_t size, xbrc_reg_t **reg);

void mca_coll_xbrc_return_registration(xbrc_reg_t *reg);

// ---

int mca_coll_xbrc_allreduce(const void *sbuf, void *rbuf,
	int count, ompi_datatype_t *datatype, ompi_op_t *op,
	ompi_communicator_t *comm, mca_coll_base_module_t *module);

// ---

END_C_DECLS

#endif
