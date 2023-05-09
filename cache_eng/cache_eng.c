#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sched.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <immintrin.h>

#include <mpi.h>

#include "cache_eng.h"
#include "perfctr.h"

// -------------------------------

// Make sure to appropriately set NUM_SOCKETS and NUM_CORES_PER_SOCKET in perfctr.h
// Make sure to set PCI_PMON_bus depending on your system's PCI bus config

#define BARRIER_CUSTOM
// #define BARRIER_MPI

// -------------------------------

void perfctr_program_core(bool from_list, ctr_event_t *events, int n_events);
void perfctr_program_cha(bool from_list, ctr_event_t *events, int n_events);
void perfctr_program_m2m(bool from_list, ctr_event_t *events, int n_events);
void perfctr_program_imc(bool from_list, ctr_event_t *events, int n_events);

// -------------------------------

int ncache_lines = 1;
bool measure_times = true;
int time_exp = 6; // micro-seconds (10^-6)
bool sum_per_socket = true;
bool per_box_report = true;
bool enable_perfctr = true;
bool enable_prefetch = false;

bool active_cores[NUM_CORES] = {0};

// ---

int rank = -1;
int comm_size = 0;

// ---

int pvt_seq = 0;

root_ctrl_t *root_ctrl = NULL;
rank_ctrl_t *rank_ctrl = NULL;
volatile char *sh_data = NULL;
void *priv_data = NULL;

// ---

void (*pmon_box_perfctr_program_fn[PMON_BOX_COUNT])(bool, ctr_event_t *, int) = {
	[PMON_BOX_CORE] = perfctr_program_core,
	[PMON_BOX_CHA] = perfctr_program_cha,
	[PMON_BOX_M2M] = perfctr_program_m2m,
	[PMON_BOX_IMC] = perfctr_program_imc
};

static const char *ctr_event_names[PMON_BOX_COUNT][PMON_NUM_COUNTERS_MAX] = {NULL};

// -------------------------------

#define SET(v) ({ \
	_mm_mfence(); \
	rank_ctrl[rank].flag = (flag_offset + v); \
})

#define WAIT(rank, v) ({ \
	while(rank_ctrl[rank].flag < (flag_offset + v)) {} \
	__atomic_thread_fence(__ATOMIC_ACQUIRE); \
})

#define FLAG_RESET() ({flag_offset = rank_ctrl[rank].flag;})

#define READ_OFFSET(offset) ({ \
	* (volatile int *) (sh_data + offset); \
})

#define WRITE_OFFSET(offset) ({ \
	* (volatile int *) (sh_data + offset) = 0x45; \
})

/* We observed slight undercutting in some events without
 * the lfence (in BYPASS_CHA_IMC or IMC_READS_COUNT). */
#define READ_N_LINES(n) ({ \
	if(measure_times) \
		t1 = TS(); \
	\
	for(int i = 0; i < (n) * 64; i += 64) { \
		READ_OFFSET(i); \
		if(!enable_prefetch) \
			_mm_lfence(); \
	} \
	_mm_mfence(); \
	\
	if(measure_times) { \
		t2 = TS(); \
		msm_insert(msm_iter, MSM_MEMCPY, \
			ts_elapsed(start, t2), ts_elapsed(t1, t2)); \
	} \
})

#define WRITE_N_LINES(n) ({ \
	if(measure_times) \
		t1 = TS(); \
	\
	for(int i = 0; i < (n) * 64; i += 64) \
		WRITE_OFFSET(i); \
	_mm_mfence(); \
	\
	if(measure_times) { \
		t2 = TS(); \
		msm_insert(msm_iter, MSM_MEMCPY_W, \
			ts_elapsed(start, t2), ts_elapsed(t1, t2)); \
	} \
})

#define READ_LINES() READ_N_LINES(ncache_lines)
#define WRITE_LINES() WRITE_N_LINES(ncache_lines)

#define MEM_OP_OFFSET(offset, op) ({ \
	if(measure_times) \
		t1 = TS(); \
	(op)((void *) (sh_data + offset)); \
	if(measure_times) { \
		t2 = TS(); \
		msm_insert(msm_iter, MSM_MEMCPY_W, \
			ts_elapsed(start, t2), ts_elapsed(t1, t2)); \
	} \
})

#define CLWB_N_LINES(n) ({ \
	for(int i = 0; i < (n) * 64; i += 64) \
		MEM_OP_OFFSET(i, _mm_clwb); \
	_mm_mfence(); \
})

#define CLFLUSH_N_LINES(n) ({ \
	for(int i = 0; i < (n) * 64; i += 64) \
		MEM_OP_OFFSET(i, _mm_clflush); \
	_mm_mfence(); \
})

#define CLWB_LINES() CLWB_N_LINES(ncache_lines)
#define CLFLUSH_LINES() CLFLUSH_N_LINES(ncache_lines)

static inline void start_counters(void) {
	_mm_mfence();

	for(int c = 0; c < NUM_CORES; c++) {
		if(!active_cores[c]) continue;
		msr_write(c, CORE_GLOBAL_CTL_UNFREEZE,
			MSR_PMON_CORE_GLOBAL_CTL);
	}
	
	for(int s = 0; s < NUM_SOCKETS; s++)
		msr_write(SOCKET_CPU(s), 1L << GLOBAL_CTL_UNFRZ_ALL,
			MSR_PMON_GLOBAL_CTL);
}

static inline void stop_counters(void) {
	_mm_mfence();
	
	for(int s = 0; s < NUM_SOCKETS; s++)
		msr_write(SOCKET_CPU(s), 1L << GLOBAL_CTL_FRZ_ALL,
			MSR_PMON_GLOBAL_CTL);
	
	for(int c = 0; c < NUM_CORES; c++) {
		if(!active_cores[c]) continue;
		msr_write(c, CORE_GLOBAL_CTL_FREEZE,
			MSR_PMON_CORE_GLOBAL_CTL);
	}
}

#define START_COUNTERS() ({ \
	if(enable_perfctr && iter >= warmup) \
		start_counters(); \
})

#define STOP_COUNTERS() ({ \
	if(enable_perfctr && iter >= warmup) \
		stop_counters(); \
})

// -Wl,--export-dynamic
void cache_eng_start_counters(void) {
	start_counters();
}
void cache_eng_stop_counters(void) {
	stop_counters();
}

#define MARK_MAIN() ({ \
	msm_insert(msm_iter, MSM_MAIN, ts_elapsed(start, TS()), 0); \
})

// -------------------------------

void barrier_begin(void) {
#ifdef BARRIER_CUSTOM
	if(rank == 0) {
		for(int r = 1; r < comm_size; r++)
			while(rank_ctrl[r].seq != pvt_seq) {}
		root_ctrl->seq = pvt_seq;
	} else {
		rank_ctrl[rank].seq = pvt_seq;
		while(root_ctrl->seq != pvt_seq) {}
	}
#elif defined(BARRIER_MPI)
	MPI_Barrier(MPI_COMM_WORLD);
#endif
}

void barrier_end(void) {
#ifdef BARRIER_CUSTOM
	if(rank == 0) {
		for(int r = 1; r < comm_size; r++)
			while(rank_ctrl[r].ack != pvt_seq) {}
		root_ctrl->ack = pvt_seq;
	} else {
		rank_ctrl[rank].ack = pvt_seq;
		while(root_ctrl->ack != pvt_seq) {}
	}
#elif defined(BARRIER_MPI)
	MPI_Barrier(MPI_COMM_WORLD);
#endif
}

void perfctr_program_core(bool from_list, ctr_event_t *events, int n_events) {
	printf("Programming CORE counters\n");
	
	if(from_list) {
		for(int c = 0; c < NUM_CORES; c++) {
			for(int i = 0; i < NUM_CORE_COUNTERS; i++) {
				uint64_t val = 0;
				
				if(i < n_events) {
					val = CORE_PERFEVTSEL(events[i].event, events[i].umask);
					ctr_event_names[PMON_BOX_CORE][i] = events[i].desc;
				}
				
				msr_write(c, val, MSR_CORE_REG(PERFEVTSEL, i));
			}
		}
	} else {
		#define PROGRAM_CTR(event, umask, desc) ({ \
			assert(ctr_index < NUM_CORE_COUNTERS); \
			msr_write(c, CORE_PERFEVTSEL(event, umask), \
				MSR_CORE_REG(PERFEVTSEL, ctr_index)); \
			ctr_event_names[PMON_BOX_CORE][ctr_index++] = desc; \
		})
		
		for(int c = 0; c < NUM_CORES; c++) {
			int ctr_index = 0;
			
			// PROGRAM_CTR(CORE_EVENT_CORE_SNOOP_RESPONSE, 0x01, "CORE_SNOOP_RESPONSE.MISS");
			// PROGRAM_CTR(CORE_EVENT_CORE_SNOOP_RESPONSE, 0x02, "CORE_SNOOP_RESPONSE.I_HIT_FSE");
			// PROGRAM_CTR(CORE_EVENT_CORE_SNOOP_RESPONSE, 0x04, "CORE_SNOOP_RESPONSE.S_HIT_FSE");
			// PROGRAM_CTR(CORE_EVENT_CORE_SNOOP_RESPONSE, 0x08, "CORE_SNOOP_RESPONSE.S_FWD_M");
			// PROGRAM_CTR(CORE_EVENT_CORE_SNOOP_RESPONSE, 0x10, "CORE_SNOOP_RESPONSE.I_FWD_M");
			// PROGRAM_CTR(CORE_EVENT_CORE_SNOOP_RESPONSE, 0x20, "CORE_SNOOP_RESPONSE.I_FWD_FE");
			// PROGRAM_CTR(CORE_EVENT_CORE_SNOOP_RESPONSE, 0x40, "CORE_SNOOP_RESPONSE.S_FWD_FE");
			
			PROGRAM_CTR(CORE_EVENT_MEM_LOAD_L3_HIT_RETIRED, 0x04, "MEM_LOAD_L3_HIT_RETIRED.XSNP_FWD");
			PROGRAM_CTR(CORE_EVENT_MEM_LOAD_RETIRED, 0x10, "MEM_LOAD_RETIRED.L2_MISS");
			PROGRAM_CTR(CORE_EVENT_MEM_LOAD_RETIRED, 0x02, "MEM_LOAD_RETIRED.L2_HIT");
		}
		
		#undef PROGRAM_CTR
	}
}

void perfctr_program_m2m(bool from_list, ctr_event_t *events, int n_events) {
	printf("Programming M2M counters\n");
	
	if(from_list) {
		uint32_t opcode_mm = 0;
		int n_pkt_match = 0;
		
		for(int i = 0; i < n_events; i++) {
			if(events[i].event == M2M_EVENT_PKT_MATCH) {
				opcode_mm = events[i].xtra;
				events[i].xtra = 0;
				n_pkt_match++;
			}
		}
		assert(n_pkt_match <= 1);
		
		for(int s = 0; s < NUM_SOCKETS; s++) {
			for(int m = 0; m < NUM_M2M_BOXES; m++) {
				uint bus = PCI_PMON_bus[s];
				uint device = PCI_PMON_M2M_device[m];
				uint function = PCI_PMON_M2M_function[m];
				
				for(int i = 0; i < NUM_M2M_COUNTERS; i++) {
					uint64_t val = 1L << M2M_CTL_RESET;
					
					if(i < n_events) {
						val = M2M_CTL_EXT(events[i].event,
							events[i].umask, events[i].xtra);
						ctr_event_names[PMON_BOX_M2M][i] = events[i].desc;
					}
					
					pci_cfg_w64(bus, device, function, PCI_M2M_REG(CTL, i), val);
				}
				
				if(n_pkt_match > 0) {
					pci_cfg_w32(bus, device, function,
						PCI_PMON_M2M_OPCODE_MM, opcode_mm);
				}
			}
		}
	} else {
		#define PROGRAM_CTR_EXT(event, umask, xtra, desc) ({ \
			assert(ctr_index < NUM_M2M_COUNTERS); \
			pci_cfg_w64(bus, device, function, PCI_M2M_REG(CTL, ctr_index), \
				M2M_CTL_EXT(event, umask, xtra)); \
			ctr_event_names[PMON_BOX_M2M][ctr_index++] = desc; \
		})
		#define PROGRAM_CTR(event, umask, desc) PROGRAM_CTR_EXT(event, umask, 0L, desc)
		
		for(int s = 0; s < NUM_SOCKETS; s++) {
			for(int m = 0; m < NUM_M2M_BOXES; m++) {
				int ctr_index = 0;
				
				uint bus = PCI_PMON_bus[s];
				uint device = PCI_PMON_M2M_device[m];
				uint function = PCI_PMON_M2M_function[m];
				
				// PROGRAM_CTR(M2M_EVENT_DIRECTORY_LOOKUP, 0x01, "DIRECTORY_LOOKUP.ANY");
				// PROGRAM_CTR(M2M_EVENT_DIRECTORY_LOOKUP, 0x02, "DIRECTORY_LOOKUP.STATE_I");
				// PROGRAM_CTR(M2M_EVENT_DIRECTORY_LOOKUP, 0x04, "DIRECTORY_LOOKUP.STATE_S");
				// PROGRAM_CTR(M2M_EVENT_DIRECTORY_LOOKUP, 0x08, "DIRECTORY_LOOKUP.STATE_A");
				
				// PROGRAM_CTR(M2M_EVENT_DIRECTORY_HIT, 0xFF, "DIRECTORY_HIT.ALL");
				// PROGRAM_CTR(M2M_EVENT_DIRECTORY_MISS, 0xFF, "DIRECTORY_MISS.ALL");
				
				// PROGRAM_CTR(M2M_EVENT_DIRECTORY_UPDATE, 0x01, "_DIRECTORY_UPDATE");
				// PROGRAM_CTR(M2M_EVENT_DIRECTORY_UPDATE, 0x10, "_DIRECTORY_UPDATE._S_TO_A");
				
				// PROGRAM_CTR(M2M_EVENT_IMC_READS, 0x04, "IMC_READS");
				// PROGRAM_CTR(M2M_EVENT_IMC_WRITES, 0x10, "IMC_WRITES");
				
				// PROGRAM_CTR(M2M_EVENT_BYPASS_M2M_INGRESS, 0x01, "BYPASS_M2M_INGRESS.TAKEN");
				// PROGRAM_CTR(M2M_EVENT_BYPASS_M2M_EGRESS, 0x01, "BYPASS_M2M_EGRESS.TAKEN");
			}
		}
		
		#undef PROGRAM_CTR_EXT
		#undef PROGRAM_CTR
	}
}

void perfctr_program_imc(bool from_list, ctr_event_t *events, int n_events) {
	printf("Programming IMC counters\n");
	
	if(from_list) {
		for(int s = 0; s < NUM_SOCKETS; s++) {
			for(int imc = 0; imc < NUM_IMC_BOXES; imc++) {
				for(int chn = 0; chn < NUM_IMC_CHANNELS; chn++) {
					for(int i = 0; i < NUM_IMC_COUNTERS; i++) {
						uint64_t val = 1L << M2M_CTL_RESET;
						
						if(i < n_events) {
							val = IMC_CTL(events[i].event, events[i].umask);
							ctr_event_names[PMON_BOX_IMC][i] = events[i].desc;
						}
						
						pci_imc_w32(s, imc, PCI_IMC_REG_CTL(chn, i), val);
					}
				}
			}
		}
	} else {
		#define PROGRAM_CTR(event, umask, desc) ({ \
			assert(ctr_index < NUM_IMC_COUNTERS); \
			pci_imc_w32(s, imc, PCI_IMC_REG_CTL(chn, ctr_index), \
				IMC_CTL(event, umask)); \
			ctr_event_names[PMON_BOX_IMC][ctr_index++] = desc; \
		})
		
		for(int s = 0; s < NUM_SOCKETS; s++) {
			for(int imc = 0; imc < NUM_IMC_BOXES; imc++) {
				for(int chn = 0; chn < NUM_IMC_CHANNELS; chn++) {
					int ctr_index = 0;
					
					PROGRAM_CTR(IMC_EVENT_CAS_COUNT, 0x0F, "CAS_COUNT.RD");
					PROGRAM_CTR(IMC_EVENT_CAS_COUNT, 0x30, "CAS_COUNT.WR");
					// PROGRAM_CTR(IMC_EVENT_RPQ_INSERTS, 0xFF, "RPQ_INSERTS");
					// PROGRAM_CTR(IMC_EVENT_WBQ_INSERTS, 0xFF, "WBQ_INSERTS");
				}
			}
		}
		
		#undef PROGRAM_CTR_EXT
		#undef PROGRAM_CTR
	}
}

void perfctr_program_cha(bool from_list, ctr_event_t *events, int n_events) {
	printf("Programming CHA counters\n");
	
	if(from_list) {
		for(int s = 0; s < NUM_SOCKETS; s++) {
			for(int cha = 0; cha < NUM_CHA_BOXES; cha++) {
				for(int i = 0; i < NUM_CHA_COUNTERS; i++) {
					uint64_t val = 1L << CHA_CTL_RESET;
					
					if(i < n_events) {
						val = CHA_CTL_EXT(events[i].event,
							events[i].umask, events[i].xtra);
						ctr_event_names[PMON_BOX_CHA][i] = events[i].desc;
					}
					
					msr_write(SOCKET_CPU(s), val, MSR_CHA_REG(cha, CTL, i));
				}
			}
		}
	} else {
		#define PROGRAM_CTR_EXT(event, umask, xtra, desc) ({ \
			assert(ctr_index < NUM_CHA_COUNTERS); \
			msr_write(SOCKET_CPU(s), CHA_CTL_EXT(event, umask, xtra), \
				MSR_CHA_REG(cha, CTL, ctr_index)); \
			ctr_event_names[PMON_BOX_CHA][ctr_index++] = desc; \
		})
		#define PROGRAM_CTR(event, umask, desc) PROGRAM_CTR_EXT(event, umask, 0L, desc)
		
		for(int s = 0; s < NUM_SOCKETS; s++) {
			for(int cha = 0; cha < NUM_CHA_BOXES; cha++) {
				int ctr_index = 0;
				
				// PROGRAM_CTR(CHA_EVENT_DIR_UPDATE, 0x01L, "DIR_UPDATE.HA");
				// PROGRAM_CTR(CHA_EVENT_DIR_UPDATE, 0x02L, "DIR_UPDATE.TOR");
				
				// PROGRAM_CTR(CHA_EVENT_DIR_LOOKUP, 0x01L, "DIR_LOOKUP.SNP");
				// PROGRAM_CTR(CHA_EVENT_DIR_LOOKUP, 0x02L, "DIR_LOOKUP.NO_SNP");
				
				// PROGRAM_CTR(CHA_EVENT_OSB, 0x01L, "OSB.LOCAL_INVITOE");
				// PROGRAM_CTR(CHA_EVENT_OSB, 0x02L, "OSB.LOCAL_READ");
				// PROGRAM_CTR(CHA_EVENT_OSB, 0x04L, "OSB.REMOTE_READ");
				// PROGRAM_CTR(CHA_EVENT_OSB, 0x08L, "OSB.REMOTE_READ_INVITOE");
				// PROGRAM_CTR(CHA_EVENT_OSB, 0x10L, "OSB.RFO_HITS_SNP_BCAST");
				
				// PROGRAM_CTR(CHA_EVENT_OSB, 0x02L, "OSB.LOCAL_READ");
				
				// PROGRAM_CTR(CHA_EVENT_HITME_LOOKUP, 0x01L, "HITME_LOOKUP.READ");
				// PROGRAM_CTR(CHA_EVENT_HITME_LOOKUP, 0x02L, "HITME_LOOKUP.WRITE");
				
				PROGRAM_CTR_EXT(CHA_EVENT_LLC_LOOKUP, 0xFFL, 0x1FFFL, "LLC_LOOKUP.ALL");
				// PROGRAM_CTR_EXT(CHA_EVENT_LLC_LOOKUP, 0x01L, 0x0BD9L, "LLC_LOOKUP.READ_MISS_LOC_HOM");
				// PROGRAM_CTR_EXT(CHA_EVENT_LLC_LOOKUP, 0x01L, 0x1FE0L, "LLC_LOOKUP.MISS_ALL");
				
				// PROGRAM_CTR_EXT(CHA_EVENT_LLC_LOOKUP, 0xFFL, 0x1BC8L, "LLC_LOOKUP.RFO");
				// PROGRAM_CTR_EXT(CHA_EVENT_LLC_LOOKUP, 0x01L, 0x1BC8L, "LLC_LOOKUP.RFO_MISS");
				// PROGRAM_CTR_EXT(CHA_EVENT_LLC_LOOKUP, 0xFFL, 0x19C8L, "LLC_LOOKUP.RFO_LOCAL");
				// PROGRAM_CTR_EXT(CHA_EVENT_LLC_LOOKUP, 0xFFL, 0x1A08L, "LLC_LOOKUP.RFO_REMOTE");
				
				// PROGRAM_CTR_EXT(CHA_EVENT_TOR_INSERTS, 0x01L, 0xC807FE, "TOR_INSERTS.IA_MISS_RFO");
				// PROGRAM_CTR_EXT(CHA_EVENT_TOR_INSERTS, 0x01L, 0xC001FF, "TOR_INSERTS.ALL");
				// PROGRAM_CTR_EXT(CHA_EVENT_TOR_INSERTS, 0x01L, 0xC001FD, "TOR_INSERTS.IA_HIT");
				// PROGRAM_CTR_EXT(CHA_EVENT_TOR_INSERTS, 0x01L, 0xC001FE, "TOR_INSERTS.IA_MISS");
				
				// PROGRAM_CTR_EXT(CHA_EVENT_TOR_INSERTS, 0x01L, 0xC001FFL, "TOR_INSERTS.ALL_IRQ_IA");
				// PROGRAM_CTR_EXT(CHA_EVENT_TOR_INSERTS, 0x10L, 0xC001FFL, "TOR_INSERTS.ALL_IRQ_NON_IA");
				// PROGRAM_CTR_EXT(CHA_EVENT_TOR_INSERTS, 0x08L, 0xC001FFL, "TOR_INSERTS.ALL_IPQ");
				// PROGRAM_CTR_EXT(CHA_EVENT_TOR_INSERTS, 0x40L, 0xC001FFL, "TOR_INSERTS.ALL_RRQ");
				// PROGRAM_CTR_EXT(CHA_EVENT_TOR_INSERTS, 0x80L, 0xC001FFL, "TOR_INSERTS.ALL_WBQ");
				
				// PROGRAM_CTR_EXT(CHA_EVENT_TOR_INSERTS, 0x08L, 0xF823FFL, "TOR_INSERTS.IPQ.SNP_INV_OWN");
				
				// PROGRAM_CTR(CHA_EVENT_HITME_MISS, 0x20L, "HITME_MISS.SHARED_RDINVOWN");
				// PROGRAM_CTR(CHA_EVENT_HITME_MISS, 0x40L, "HITME_MISS.NOTSHARED_RDINVOWN");
				// PROGRAM_CTR(CHA_EVENT_HITME_MISS, 0x80L, "HITME_MISS.READ_OR_INV");
				// PROGRAM_CTR(CHA_EVENT_HITME_MISS, 0xFFL, "HITME_MISS.ALL");
				
				// PROGRAM_CTR(CHA_EVENT_HITME_LOOKUP, 0xFFL, "HITME_LOOKUP.ALL");
				// PROGRAM_CTR(CHA_EVENT_HITME_UPDATE, 0xFFL, "HITME_UPDATE.ALL");
				// PROGRAM_CTR(CHA_EVENT_HITME_MISS, 0xFFL, "HITME_MISS.ALL");
				// PROGRAM_CTR(CHA_EVENT_HITME_HIT, 0xFFL, "HITME_HIT.ALL");
				
				// PROGRAM_CTR(CHA_EVENT_DIR_LOOKUP, 0xFFL, "DIR_LOOKUP.ALL");
				
				// PROGRAM_CTR(CHA_EVENT_BYPASS_CHA_IMC, 0x01, "BYPASS_CHA_IMC.TAKEN");
				
				// PROGRAM_CTR(CHA_EVENT_CORE_SNP, 0x41, "CORE_SNP.CORE_ONE");
				
				// -----------------
			}
		}
		
		#undef PROGRAM_CTR_EXT
		#undef PROGRAM_CTR
	}
}

void perform_test(int iter, int warmup, int iterations) {
	msm_line_t msm_iter[MSM_COUNT];
	static msm_line_t msm_sum[MSM_COUNT];
	
	static uint64_t perf_core_initial[NUM_CORES][NUM_CHA_COUNTERS] = {{0}};
	static uint64_t perf_cha_initial[NUM_SOCKETS][NUM_CHA_BOXES][NUM_CHA_COUNTERS] = {{{0}}};
	static uint64_t perf_m2m_initial[NUM_SOCKETS][NUM_M2M_BOXES][NUM_M2M_COUNTERS] = {{{0}}};
	static uint64_t perf_imc_initial[NUM_SOCKETS][NUM_IMC_BOXES][NUM_IMC_CHANNELS][NUM_IMC_COUNTERS] = {{{{0}}}};
	
	int flag_offset = 0;
	
	ts_t t1, t2;
	
	double dt_ts;
	ts_t dt_ts_t, start;
	
	if(iter == 0) {
		msm_init(msm_sum, MSM_COUNT, ncache_lines);
		
		if(rank == 0 && enable_perfctr) {
			// CORE
			for(int c = 0; c < NUM_CORES; c++) {
				for(int ctr = 0; ctr < NUM_CORE_COUNTERS; ctr++)
					perf_core_initial[c][ctr] = msr_read(c, MSR_CORE_REG(PMC, ctr));
			}
			
			for(int s = 0; s < NUM_SOCKETS; s++) {
				// CHA
				for(int cha = 0; cha < NUM_CHA_BOXES; cha++) {
					for(int ctr = 0; ctr < NUM_CHA_COUNTERS; ctr++) {
						perf_cha_initial[s][cha][ctr] = msr_read(SOCKET_CPU(s),
							MSR_CHA_REG(cha, CTR, ctr));
					}
				}
				
				// M2M
				for(int m = 0; m < NUM_M2M_BOXES; m++) {
					for(int ctr = 0; ctr < NUM_M2M_COUNTERS; ctr++) {
						uint bus = PCI_PMON_bus[s];
						uint device = PCI_PMON_M2M_device[m];
						uint function = PCI_PMON_M2M_function[m];
						
						perf_m2m_initial[s][m][ctr] = pci_cfg_r64(bus,
							device, function, PCI_M2M_REG(CTR, ctr));
					}
				}
				
				// IMC
				for(int imc = 0; imc < NUM_IMC_BOXES; imc++) {
					for(int chn = 0; chn < NUM_IMC_CHANNELS; chn++) {
						for(int ctr = 0; ctr < NUM_IMC_COUNTERS; ctr++) {
							perf_imc_initial[s][imc][chn][ctr] = pci_imc_r64(s,
								imc, PCI_IMC_REG_CTR(chn, ctr));
						}
					}
				}
			}
		}
	}
	
	if(measure_times)
		msm_init(msm_iter, MSM_COUNT, ncache_lines);
	
	// ----
	
	if(measure_times) {
		t1 = TS();
		t2 = TS();
		
		dt_ts = ts_elapsed(t1, t2);
		dt_ts_t = ts_diff(t1, t2);
		start = TS_ZINIT;
	}
	
	pvt_seq++;
	rank_ctrl[rank].flag = 0;
	
	// ----
	
	barrier_begin();
	
	if(measure_times)
		start = ts_incr(TS(), ts_ndiv(dt_ts_t, 2));
	
#ifdef EXTERNAL_SCENARIO
	#include EXTERNAL_SCENARIO
	EXTERNAL_SCENARIO_FN();
#else
	
	/* {
		for(int r = 0; r < comm_size; r++) {
			if(rank == r) {
				if(r > 0) WAIT(r-1, 1);
				READ_LINES();
				SET(1);
			}
		}
		WAIT(comm_size-1, 1);
		FLAG_RESET();
	} */
	
	if(rank == 0) {
		WRITE_LINES();
		
		// START_COUNTERS();
		
		// CLWB_LINES();
		// CLFLUSH_LINES();
		
		// START_COUNTERS();
		
		SET(1);
	}
	
	if(rank == 1) {
		WAIT(0, 1);
		// WAIT(2, 1);
		
		// START_COUNTERS();
		
		READ_LINES();
		// WRITE_LINES();
		
		SET(1);
	}
	
	if(rank == comm_size/2) {
		// WAIT(0, 1);
		WAIT(1, 1);
		
		START_COUNTERS();
		
		READ_LINES();
		// WRITE_LINES();
		
		// CLWB_LINES();
		// CLFLUSH_LINES();
		
		SET(1);
	}
	
#endif
	
	if(measure_times) {
		msm_insert(msm_iter, MSM_ACK, ts_elapsed(start, TS()), 0);
		
		if(rank == 0) root_ctrl->ts = start;
	}
	
	barrier_end();
	
	if(rank == 0)
		STOP_COUNTERS();
	
	// ----
	
	if(measure_times && iter >= warmup) {
		double root_adjust = ts_elapsed(root_ctrl->ts, start);
		
		msm_insert(msm_iter, MSM_JOIN, 0, 0);
		
		for(int m = 0; m < MSM_COUNT; m++) {
			double ts_offset = root_adjust;
			double time_offset = -dt_ts;
			
			if(m == MSM_ACK || m == MSM_JOIN || m == MSM_MAIN)
				time_offset = 0;
			
			msm_add(msm_sum, m, msm_iter[m].points,
				msm_iter[m].len, ts_offset, time_offset);
		}
		
		msm_dealloc(msm_iter, MSM_COUNT);
	}
	
	if(measure_times && iter == warmup + iterations - 1) {
		for(int r = 0; r < rank; r++)
			MPI_Barrier(MPI_COMM_WORLD);
		
		for(int m = 0; m < MSM_COUNT; m++) {
			char *ts_str, *time_str;
			
			msm_string(msm_sum, m, iterations, &ts_str, &time_str);
			printf("%s\n%s\n", ts_str, time_str);
			free(ts_str); free(time_str);
		}
		
		for(int r = rank; r < comm_size; r++)
			MPI_Barrier(MPI_COMM_WORLD);
		
		msm_dealloc(msm_sum, MSM_COUNT);
	}
	
	if(rank == 0 && enable_perfctr && iter == warmup + iterations - 1) {
		if(measure_times)
			printf("\n");
		
		perfctr_report_core(perf_core_initial,
			ctr_event_names[PMON_BOX_CORE], iterations);
		
		printf("\n");
		
		perfctr_report_cha(perf_cha_initial,
			ctr_event_names[PMON_BOX_CHA], iterations);
		
		printf("\n");
		
		perfctr_report_m2m(perf_m2m_initial,
			ctr_event_names[PMON_BOX_M2M], iterations);
		
		printf("\n");
		
		perfctr_report_imc(perf_imc_initial,
			ctr_event_names[PMON_BOX_IMC], iterations);
	}
}

// -------------------------------

int bind_hwthread(void);
int init_shared_area(const char *shm_name, size_t size, void **ptr);
int parse_event_file(char *file_path, ctr_event_t **events_dst, int *n_events_dst);
void M2M_scatter_PTK_MATCH(ctr_event_t *evs, int n_evs);

int main(int argc, char **argv) {
	int warmup = 100;
	int iterations = 1000;
	
	char *input_file = NULL;
	ctr_event_t *ctr_events[PMON_BOX_COUNT] = {NULL};
	int n_ctr_events[PMON_BOX_COUNT] = {0};
	
	int return_code = 0;
	
	// ----
	
	int opt;
	int option_index = 0;
	
	struct option long_options[] = {
		{"sum-socket", no_argument, NULL, 0},
		{"sum-system", no_argument, NULL, 0},
		{"per-box", no_argument, NULL, 0},
		{"no-per-box", no_argument, NULL, 0},
		{"no-perfctr", no_argument, NULL, 0},
		{"no-times", no_argument, NULL, 0},
		{"prefetch", no_argument, NULL, 0},
		{"ns", no_argument, NULL, 0},
		{NULL, 0, NULL, 0}
	};
	
	while((opt = getopt_long(argc, argv, "x:i:n:sf:", long_options, &option_index)) != -1) {
		switch(opt) {
			case 0:
				switch(option_index) {
					case 0: sum_per_socket = true; break;
					case 1: sum_per_socket = false; break;
					case 2: per_box_report = true; break;
					case 3: per_box_report = false; break;
					case 4: enable_perfctr = false; break;
					case 5: measure_times = false; break;
					case 6: enable_prefetch = true; break;
					case 7: time_exp = 9; break;
				}
				
				break;
			case 'x':
				warmup = atoi(optarg);
				break;
			case 'i':
				iterations = atoi(optarg);
				break;
			case 'n':
				ncache_lines = atoi(optarg);
				break;
			case 's':
				measure_times = false;
				break;
			case 'f':
				input_file = optarg;
				break;
			case '?':
				if(optopt == 'c')
					printf("Option -%c requires an argument\n", optopt);
				else if(isprint(optopt))
					printf("Unkown option -%c\n", optopt);
				else
					printf("Unkown option character \\x%x\n", optopt);
				
				RETURN_WITH_ERROR(return_code, -1, end);
				break;
			default:
				abort();
		}
	}
	
	// ----
	
	MPI_Init(NULL, NULL);
	
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
	
	init_shared_area("root_ctrl", sizeof(root_ctrl_t),
		(void **) &root_ctrl);
	
	init_shared_area("rank_ctrl", comm_size * sizeof(rank_ctrl_t),
		(void **) &rank_ctrl);
	
	init_shared_area("data", ncache_lines * 64, (void **) &sh_data);
	
	priv_data = malloc(ncache_lines * 64);
	
	if(!root_ctrl || !rank_ctrl || !sh_data || !priv_data)
		RETURN_WITH_ERROR(return_code, -2, end);
	
	// ----

	if(enable_perfctr) {
		int my_cpu = bind_hwthread();
		assert(my_cpu >= 0 && my_cpu < NUM_CORES);
		
		active_cores[my_cpu] = true;
		MPI_Allreduce(MPI_IN_PLACE, active_cores,
			NUM_CORES, MPI_C_BOOL, MPI_LOR, MPI_COMM_WORLD);
		
		if(rank == 0) {
			printf("Active CPUs: ");
			for(int i = 0; i < NUM_CORES; i++) {
				if(active_cores[i])
					printf("%d ", i);
			}
			printf("\n");
		}
	}
	
	// ----
	
	ts_init();
	
	// ----
	
	int total_file_events = 0;
	
	if(enable_perfctr && input_file)
		total_file_events = parse_event_file(input_file, ctr_events, n_ctr_events);
	
	if(enable_perfctr) {
		msr_init();
		
		if(rank == 0) {
			pci_cfg_init();
			perfctr_setup(enable_prefetch);
		}
	}
	
	if(total_file_events > 0) {
		int n_done_ev[PMON_BOX_COUNT] = {0};
		
		M2M_scatter_PTK_MATCH(ctr_events[PMON_BOX_M2M], n_ctr_events[PMON_BOX_M2M]);
		
		for(int handled_events = 0; handled_events < total_file_events;) {
			ctr_event_t *next_ev_ptr[PMON_BOX_COUNT] = {NULL};
			int n_next_ev[PMON_BOX_COUNT] = {0};
			
			for(int b = 0; b < PMON_BOX_COUNT; b++) {
				int n_ev = n_ctr_events[b] - n_done_ev[b];
				
				if(n_ev > pmon_box_num_counters[b])
					n_ev = pmon_box_num_counters[b];
				
				next_ev_ptr[b] = &ctr_events[b][n_done_ev[b]];
				
				// Can only handle one PKT_MATCH at once
				int pkt_match_count = 0;
				for(int i = 0; i < n_ev; i++) {
					if(next_ev_ptr[b][i].event == M2M_EVENT_PKT_MATCH) {
						if(++pkt_match_count == 2) {
							n_ev = i;
							break;
						}
					}
				}
				
				n_next_ev[b] = n_ev;
				
				if(rank == 0)
					pmon_box_perfctr_program_fn[b](true,
						next_ev_ptr[b], n_next_ev[b]);
				
				n_done_ev[b] += n_ev;
				handled_events += n_ev;
			}
			
			for(int i = 0; i < warmup + iterations; i++)
				perform_test(i, warmup, iterations);
			
			if(rank == 0)
				printf("\n");
			
			MPI_Barrier(MPI_COMM_WORLD);
		}
	} else {
		if(rank == 0 && enable_perfctr) {
			for(int b = 0; b < PMON_BOX_COUNT; b++)
				pmon_box_perfctr_program_fn[b](false, NULL, 0);
		}
		
		for(int i = 0; i < warmup + iterations; i++)
			perform_test(i, warmup, iterations);
	}
	
	if(enable_perfctr) {
		if(rank == 0) {
			perfctr_cleanup(enable_prefetch);
			pci_cfg_cleanup();
		}
		
		msr_deinit();
	}
	
	// ----
	
	end:
	
	for(int b = 0; b < PMON_BOX_COUNT; b++)
		free(ctr_events[b]);
	
	if(root_ctrl) munmap((void *) root_ctrl, sizeof(root_ctrl));
	if(rank_ctrl) munmap((void *) rank_ctrl, comm_size * sizeof(root_ctrl));
	if(sh_data) munmap((void *) sh_data, sizeof(*sh_data));
	
	MPI_Finalize();
	return return_code;
}

// -------------------------------

int bind_hwthread(void) {
	int ret;
	cpu_set_t set;
	
	ret = sched_getaffinity(0, sizeof(set), &set);
	assert(ret == 0);
	
	int ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	
	for(int cpu = 0; cpu < ncpus; cpu++) {
		if(CPU_ISSET(cpu, &set) == 1) {
			CPU_ZERO(&set);
			CPU_SET(cpu, &set);
			
			ret = sched_setaffinity(0, sizeof(set), &set);
			assert(ret == 0);
			
			return cpu;
		}
	}
	
	return -1;
}

int parse_event_file(char *file_path, ctr_event_t *events[PMON_BOX_COUNT],
		int n_events[PMON_BOX_COUNT]) {
	
	int total_events = 0;
	
	size_t events_size[PMON_BOX_COUNT];
	
	for(int b = 0; b < PMON_BOX_COUNT; b++) {
		events[b] = malloc((events_size[b] = 4) * sizeof(ctr_event_t));
		assert(events[b]);
		n_events[b] = 0;
	}
	
	FILE *file = fopen(file_path, "r");
	assert(file != NULL);
	
	char *line = NULL;
	size_t line_size = 0;
	ssize_t nread;
	
	errno = 0;
	
	while((nread = getline(&line, &line_size, file)) != -1) {
		char *ev = NULL, *ev_ext = NULL;
		uint64_t umask = 0, xtra = 0;
		
		char *box_str = NULL;
		
		if(nread == 1)
			continue;
		
		bool is_comment = false;
		for(char *c = line; *c; c++) {
			if(*c == '#')
				is_comment = true;
			if(*c != ' ' && *c != '\t')
				break;
		}
		if(is_comment)
			continue;
		
		int ntokens = 1;
		
		for(int i = 0; i < nread; i++) {
			if(line[i] == ' ')
				ntokens++;
		}
		
		if(ntokens == 3) {
			sscanf(line, "%ms %ms %lx", &box_str, &ev, &umask);
		} else if(ntokens == 4) {
			sscanf(line, "%ms %ms %lx %ms", &box_str, &ev, &umask, &ev_ext);
		} else if(ntokens == 5) {
			sscanf(line, "%ms %ms %lx %lx %ms", &box_str, &ev, &umask, &xtra, &ev_ext);
		} else
			abort();
		
		pmon_box_enum_t box = PMON_BOX_COUNT;
		uint64_t event = -1;
		
		for(int b = 0; b < PMON_BOX_COUNT; b++) {
			if(strcmp(box_str, pmon_box_str[b]) == 0) {
				box = b;
				break;
			}
		}
		
		assert(box != PMON_BOX_COUNT);
		
		for(int i = 0; i < pmon_box_ev_cnt[box]; i++) {
			if(pmon_event_str[box][i]
					&& strcmp(ev, pmon_event_str[box][i]) == 0) {
				event = i;
				break;
			}
		}
		
		assert(event != (uint64_t) -1);
		
		char *desc;
		assert(asprintf(&desc, "%s%s%s", ev,
			(ev_ext ? "." : ""), (ev_ext ? ev_ext : "")) >= 0);
		
		if(n_events[box] == events_size[box]) {
			events[box] = realloc(events[box],
				(events_size[box] *= 2) * sizeof(ctr_event_t));
			assert(events[box]);
		}
		
		events[box][n_events[box]++] = (ctr_event_t) {
			.event = event,
			.umask = umask,
			.xtra = xtra,
			.desc = desc,
		};
		
		total_events++;
		
		free(ev);
		free(ev_ext);
		free(box_str);
	}
	
	assert(errno == 0);
	free(line);
	
	return total_events;
}

/* Since we can only have on PKT_MATCH per round, make an
 * effort to scatter them around in the M2M event list. */
void M2M_scatter_PTK_MATCH(ctr_event_t *evs, int n_evs) {
	int last_pm = -NUM_M2M_COUNTERS;
	
	for(int i = 0; i < n_evs;) {
		bool moved = false;
		
		if(evs[i].event == M2M_EVENT_PKT_MATCH) {
			if(i - last_pm < NUM_M2M_COUNTERS) {
				for(int k = i+1, lpm = last_pm; k < n_evs; k++) {
					if(evs[k].event == M2M_EVENT_PKT_MATCH)
						lpm = k;
					
					if(k - lpm >= NUM_M2M_COUNTERS - 1) {
						ctr_event_t tmp = evs[i];
						
						for(int l = i; l < k; l++)
							evs[l] = evs[l+1];
						
						evs[k] = tmp;
						moved = true;
						break;
					}
				}
				
			}
			
			if(!moved)
				last_pm = i;
		}
		
		if(!moved)
			i++;
	}
}

static int open_shm(const char *salt, char *dst, size_t dst_size) {
	int fd = -1;
	
	for(int i = 0; fd == -1 && i < 128; i++) {
		int ret = snprintf(dst, dst_size, "/cache_eng.%d.%s", i, salt);
		
		if(ret < 0)
			break;
		
		errno = 0;
		fd = shm_open(dst, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
		
		if(fd == -1 && errno != EEXIST)
			break;
	}
	
	return fd;
}

int init_shared_area(const char *area_name, size_t size, void **ptr) {
	char shm_file_name[32] = {0};
	int fd = -1;
	
	void *shm_base;
	
	int return_code = 0;
	
	errno = 0;
	
	if(rank == 0) {
		fd = open_shm(area_name, shm_file_name, sizeof(shm_file_name));
		if(fd == -1) RETURN_WITH_ERROR(return_code, -1, end);
		
		if(ftruncate(fd, size) == -1)
			RETURN_WITH_ERROR(return_code, -2, end);
		
		shm_base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if(shm_base == MAP_FAILED) RETURN_WITH_ERROR(return_code, -3, end);
		
		memset(shm_base, 0, size);
	}
	
	MPI_Bcast(shm_file_name, sizeof(shm_file_name), MPI_CHAR, 0, MPI_COMM_WORLD);
	
	if(rank != 0) {
		fd = shm_open(shm_file_name, O_RDWR, 0);
		if(fd == -1) RETURN_WITH_ERROR(return_code, -4, end);
		
		shm_base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if(shm_base == MAP_FAILED) RETURN_WITH_ERROR(return_code, -5, end);
		
		close(fd);
	}
	
	MPI_Barrier(MPI_COMM_WORLD);
	
	if(rank == 0) {
		close(fd); fd = -1;
		shm_unlink(shm_file_name);
	}
	
	*ptr = shm_base;
	
	end:
	
	if(return_code != 0) {
		fprintf(stderr, "Shared area '%s' (%zu bytes) init failed, code %d (%s (%d))\n",
			area_name, size, return_code, strerror(errno), errno);
		
		if(fd != -1)
			close(fd);
		
		if(fd != -1 && rank == 0)
			shm_unlink(shm_file_name);
	}
	
	return return_code;
}
