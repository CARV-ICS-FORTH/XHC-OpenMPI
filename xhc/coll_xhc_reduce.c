/*
 * Copyright (c) 2021-2023 Computer Architecture and VLSI Systems (CARV)
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
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/communicator/communicator.h"
#include "ompi/op/op.h"

#include "opal/mca/rcache/base/base.h"
#include "opal/util/show_help.h"
#include "opal/util/minmax.h"

#include "coll_xhc.h"

int mca_coll_xhc_reduce(const void *sbuf, void *rbuf,
		int count, ompi_datatype_t *datatype, ompi_op_t *op, int root,
		ompi_communicator_t *ompi_comm, mca_coll_base_module_t *ompi_module) {
	
	xhc_module_t *module = (xhc_module_t *) ompi_module;
	
	/* XHC does not yet support MPI_Reduce as it is described in the MPI
	 * spec. However, an implementation of it as a sub-case of Allreduce
	 * does exist.
	 * 
	 * The problem lies is in the rbuf parameter. In Allreduce, all ranks
	 * (must) provide an rbuf, and XHC's implementation leverages this for
	 * a temporary reduction buffer on each hierarchical level.
	 * 
	 * If the caller would guarantee that a fitting rbuf is provided by
	 * all ranks to MPI_Reduce, like in MPI_Allreduce, Reduce *can* be
	 * implemented as sub-case of Allreduce.
	 * 
	 * If this condition is met, one can call MPI_Reduce with root=-1,
	 * and XHC's special Reduce will be invoked. Otherwise, the previous
	 * module's Reduce will be invoked as normal. Currently, XHC's Reduce
	 * only supports root=0, and the special '-1' parameter will have that
	 * result.
	 *
	 * If the "force_reduce" MCA is set, the "special" Reduce will be called
	 * for all calls of MPI_Reduce (granted that they do have root = 0...)
	 * 
	 * The prime use-case for the existence of this "special" Reduce, is
	 * HAN's Allreduce, which is implemented as Reduce+Reduce+Bcast+Bcast,
	 * but an rbuf is available for all ranks. */
	
	if(root == -1 || (mca_coll_xhc_component.force_reduce && root == 0)) {
		return mca_coll_xhc_allreduce_internal(sbuf, rbuf, count,
			datatype, op, ompi_comm, ompi_module, false);
	} else {
		xhc_coll_fns_t fallback = module->prev_colls;
		
		return fallback.coll_reduce(sbuf, rbuf, count, datatype,
			op, root, ompi_comm, fallback.coll_reduce_module);
	}
}
