#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <cpuid.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include "perfctr.h"

const char *pmon_box_str[PMON_BOX_COUNT] = {
	[PMON_BOX_CORE] = "CORE",
	[PMON_BOX_CHA] = "CHA",
	[PMON_BOX_M2M] = "M2M",
	[PMON_BOX_IMC] = "IMC"
};

int pmon_box_ev_cnt[PMON_BOX_COUNT] = {
	[PMON_BOX_CORE] = CORE_EVENT_COUNT,
	[PMON_BOX_CHA] = CHA_EVENT_COUNT,
	[PMON_BOX_M2M] = M2M_EVENT_COUNT,
	[PMON_BOX_IMC] = IMC_EVENT_COUNT
};

int pmon_box_num_counters[PMON_BOX_COUNT] = {
	[PMON_BOX_CORE] = NUM_CORE_COUNTERS,
	[PMON_BOX_CHA] = NUM_CHA_COUNTERS,
	[PMON_BOX_M2M] = NUM_M2M_COUNTERS,
	[PMON_BOX_IMC] = NUM_IMC_COUNTERS
};

// sed -r 's/(\w+)_EVENT_(\w+).*/[\1_EVENT_\2] = "\2",/'
const char *pmon_event_str[PMON_BOX_COUNT][PMON_EVENT_MAX] = {
	[PMON_BOX_CORE] = {
		[CORE_EVENT_CORE_SNOOP_RESPONSE] = "CORE_SNOOP_RESPONSE",
		[CORE_EVENT_LONGEST_LAT_CACHE] = "LONGEST_LAT_CACHE",
		
		[CORE_EVENT_L1D] = "L1D",
		[CORE_EVENT_L2_LINES_IN] = "L2_LINES_IN",
		[CORE_EVENT_L2_LINES_OUT] = "L2_LINES_OUT",
		[CORE_EVENT_L2_RQSTS] = "L2_RQSTS",
		[CORE_EVENT_L2_TRANS] = "L2_TRANS",
		
		[CORE_EVENT_MEM_INST_RETIRED] = "MEM_INST_RETIRED",
		[CORE_EVENT_MEM_LOAD_L3_HIT_RETIRED] = "MEM_LOAD_L3_HIT_RETIRED",
		[CORE_EVENT_MEM_LOAD_L3_MISS_RETIRED] = "MEM_LOAD_L3_MISS_RETIRED",
		[CORE_EVENT_MEM_LOAD_RETIRED] = "MEM_LOAD_RETIRED",
		
		[CORE_EVENT_OFFCORE_REQUESTS] = "OFFCORE_REQUESTS",
		
		[CORE_EVENT_IDI_MISC] = "_IDI_MISC"
	},
	
	[PMON_BOX_CHA] = {
		[CHA_EVENT_LLC_LOOKUP] = "LLC_LOOKUP",
		[CHA_EVENT_LLC_VICTIMS] = "LLC_VICTIMS",
		
		[CHA_EVENT_SF_EVICTION] = "SF_EVICTION",
		
		[CHA_EVENT_DIR_LOOKUP] = "DIR_LOOKUP",
		[CHA_EVENT_DIR_UPDATE] = "DIR_UPDATE",
		
		[CHA_EVENT_IMC_READS_COUNT] = "IMC_READS_COUNT",
		[CHA_EVENT_IMC_WRITES_COUNT] = "IMC_WRITES_COUNT",
		
		[CHA_EVENT_BYPASS_CHA_IMC] = "BYPASS_CHA_IMC",
		
		[CHA_EVENT_HITME_LOOKUP] = "HITME_LOOKUP",
		[CHA_EVENT_HITME_HIT] = "HITME_HIT",
		[CHA_EVENT_HITME_MISS] = "HITME_MISS",
		[CHA_EVENT_HITME_UPDATE] = "HITME_UPDATE",
		
		[CHA_EVENT_TOR_INSERTS] = "TOR_INSERTS",
		[CHA_EVENT_REQUESTS] = "REQUESTS",
		[CHA_EVENT_OSB] = "OSB",
		
		[CHA_EVENT_SNOOPS_SENT] = "SNOOPS_SENT",
		[CHA_EVENT_SNOOP_RESP] = "SNOOP_RESP",
		[CHA_EVENT_SNOOP_RESP_LOCAL] = "SNOOP_RESP_LOCAL",
		[CHA_EVENT_SNOOP_RSP_MISC] = "SNOOP_RSP_MISC",
		
		[CHA_EVENT_CORE_SNP] = "CORE_SNP",
		[CHA_EVENT_XSNP_RESP] = "_XSNP_RESP",
		
		[CHA_EVENT_WB_PUSH_MTOI] = "WB_PUSH_MTOI",
		[CHA_EVENT_MISC] = "MISC"
	},
	
	[PMON_BOX_M2M] = {
		[M2M_EVENT_BYPASS_M2M_INGRESS] = "BYPASS_M2M_INGRESS",
		[M2M_EVENT_BYPASS_M2M_EGRESS] = "BYPASS_M2M_EGRESS",
		
		[M2M_EVENT_DIRECTORY_HIT] = "DIRECTORY_HIT",
		[M2M_EVENT_DIRECTORY_MISS] = "DIRECTORY_MISS",
		[M2M_EVENT_DIRECTORY_LOOKUP] = "DIRECTORY_LOOKUP",
		[M2M_EVENT_DIRECTORY_UPDATE] = "_DIRECTORY_UPDATE",
		
		[M2M_EVENT_TAG_HIT] = "TAG_HIT",
		[M2M_EVENT_TAG_MISS] = "TAG_MISS",
		
		[M2M_EVENT_IMC_READS] = "IMC_READS",
		[M2M_EVENT_IMC_WRITES] = "IMC_WRITES",
		
		[M2M_EVENT_PKT_MATCH] = "_PKT_MATCH",
	},
	
	[PMON_BOX_IMC] = {
		[IMC_EVENT_CAS_COUNT] = "CAS_COUNT",
		[IMC_EVENT_RPQ_INSERTS] = "RPQ_INSERTS",
		[IMC_EVENT_WBQ_INSERTS] = "WBQ_INSERTS"
	}
};

// sed -r 's/.*IDI_(\w+Q)_(\w+).*/[IDI_\1_\2] = "\1.\2",/'
const char *idi_opcode_str[] = {
	[IDI_ISMQ_RSP_I] = "ISMQ.RSP_I",
	[IDI_ISMQ_RSP_S] = "ISMQ.RSP_S",
	[IDI_ISMQ_RSP_V] = "ISMQ.RSP_V",
	[IDI_ISMQ_RSP_DATA_M] = "ISMQ.RSP_DATA_M",
	[IDI_ISMQ_RSP_I_FWD_M] = "ISMQ.RSP_I_FWD_M",
	[IDI_ISMQ_RSP_I_FWD_M_PTL] = "ISMQ.RSP_I_FWD_M_PTL",
	[IDI_ISMQ_PULL_DATA] = "ISMQ.PULL_DATA",
	[IDI_ISMQ_PULL_DATA_PTL] = "ISMQ.PULL_DATA_PTL",
	[IDI_ISMQ_PULL_DATA_BOGUS] = "ISMQ.PULL_DATA_BOGUS",
	[IDI_ISMQ_CMP] = "ISMQ.CMP",
	[IDI_ISMQ_CMP_FWD_CODE] = "ISMQ.CMP_FWD_CODE",
	[IDI_ISMQ_CMP_FWD_INV_I_TO_E] = "ISMQ.CMP_FWD_INV_I_TO_E",
	[IDI_ISMQ_CMP_PULL_DATA] = "ISMQ.CMP_PULL_DATA",
	[IDI_ISMQ_CMP_FWD_INV_OWN] = "ISMQ.CMP_FWD_INV_OWN",
	[IDI_ISMQ_DATA_C_CMP] = "ISMQ.DATA_C_CMP",
	[IDI_ISMQ_VICTIM] = "ISMQ.VICTIM",
	[IDI_ISMQ_DATA_NC] = "ISMQ.DATA_NC",
	[IDI_ISMQ_DATA_C] = "ISMQ.DATA_C",
	[IDI_ISMQ_RSP_I_FWD_FE] = "ISMQ.RSP_I_FWD_FE",
	[IDI_ISMQ_RSP_S_FWD_FE] = "ISMQ.RSP_S_FWD_FE",
	[IDI_ISMQ_FWD_CN_FLT] = "ISMQ.FWD_CN_FLT",
	[IDI_ISMQ_LLC_VICTIM] = "ISMQ.LLC_VICTIM",
	
	[IDI_IRQ_RFO] = "IRQ.RFO",
	[IDI_IRQ_RFO_PREF] = "IRQ.RFO_PREF",
	[IDI_IRQ_CRD] = "IRQ.CRD",
	[IDI_IRQ_CRD_UC] = "IRQ.CRD_UC",
	[IDI_IRQ_CRD_PREF] = "IRQ.CRD_PREF",
	[IDI_IRQ_DRD] = "IRQ.DRD",
	[IDI_IRQ_DRD_PREF] = "IRQ.DRD_PREF",
	[IDI_IRQ_DRD_OPT] = "IRQ.DRD_OPT",
	[IDI_IRQ_DRD_OPT_PREF] = "IRQ.DRD_OPT_PREF",
	[IDI_IRQ_DRD_PTE] = "IRQ.DRD_PTE",
	[IDI_IRQ_P_RD] = "IRQ.P_RD",
	[IDI_IRQ_WC_I_LF] = "IRQ.WC_I_LF",
	[IDI_IRQ_WC_I_L] = "IRQ.WC_I_L",
	[IDI_IRQ_UC_RD_F] = "IRQ.UC_RD_F",
	[IDI_IRQ_W_I_L] = "IRQ.W_I_L",
	[IDI_IRQ_CLFLUSH] = "IRQ.CLFLUSH",
	[IDI_IRQ_CLFLUSH_OPT] = "IRQ.CLFLUSH_OPT",
	[IDI_IRQ_CLWB] = "IRQ.CLWB",
	[IDI_IRQ_PCI_RD_CUR] = "IRQ.PCI_RD_CUR",
	[IDI_IRQ_CL_CLEANSE] = "IRQ.CL_CLEANSE",
	[IDI_IRQ_WB_PUSH_HINT] = "IRQ.WB_PUSH_HINT",
	[IDI_IRQ_WB_M_TO_I] = "IRQ.WB_M_TO_I",
	[IDI_IRQ_WB_M_TO_E] = "IRQ.WB_M_TO_E",
	[IDI_IRQ_WB_EF_TO_I] = "IRQ.WB_EF_TO_I",
	[IDI_IRQ_WB_EF_TO_E] = "IRQ.WB_EF_TO_E",
	[IDI_IRQ_WB_S_TO_I] = "IRQ.WB_S_TO_I",
	[IDI_IRQ_I_TO_M] = "IRQ.I_TO_M",
	[IDI_IRQ_I_TO_M_CACHE_NEAR] = "IRQ.I_TO_M_CACHE_NEAR",
	[IDI_IRQ_SPEC_I_TO_M] = "IRQ.SPEC_I_TO_M",
	[IDI_IRQ_LLC_PREF_RFO] = "IRQ.LLC_PREF_RFO",
	[IDI_IRQ_LLC_PREF_CODE] = "IRQ.LLC_PREF_CODE",
	[IDI_IRQ_LLC_PREF_DATA] = "IRQ.LLC_PREF_DATA",
	[IDI_IRQ_INT_LOG] = "IRQ.INT_LOG",
	[IDI_IRQ_INT_PHY] = "IRQ.INT_PHY",
	[IDI_IRQ_INT_PRI_UP] = "IRQ.INT_PRI_UP",
	[IDI_IRQ_SPLIT_LOCK] = "IRQ.SPLIT_LOCK",
	[IDI_IRQ_LOCK] = "IRQ.LOCK",
	
	[IDI_WBQ_WB_M_TO_I] = "WBQ.WB_M_TO_I",
	[IDI_WBQ_WB_M_TO_S] = "WBQ.WB_M_TO_S",
	[IDI_WBQ_WB_M_TO_E] = "WBQ.WB_M_TO_E",
	[IDI_WBQ_NON_SNP_WR] = "WBQ.NON_SNP_WR",
	[IDI_WBQ_WB_M_TO_I_PTL] = "WBQ.WB_M_TO_I_PTL",
	[IDI_WBQ_WB_M_TO_E_PTL] = "WBQ.WB_M_TO_E_PTL",
	[IDI_WBQ_NON_SNP_WR_PTL] = "WBQ.NON_SNP_WR_PTL",
	[IDI_WBQ_WB_PUSH_M_TO_I] = "WBQ.WB_PUSH_M_TO_I",
	[IDI_WBQ_WB_FLUSH] = "WBQ.WB_FLUSH",
	[IDI_WBQ_EVCT_CLN] = "WBQ.EVCT_CLN",
	[IDI_WBQ_NON_SNP_RD] = "WBQ.NON_SNP_RD",
	
	[IDI_RRQ_RD_CUR] = "RRQ.RD_CUR",
	[IDI_RRQ_RD_CODE] = "RRQ.RD_CODE",
	[IDI_RRQ_RD_DATA] = "RRQ.RD_DATA",
	[IDI_RRQ_RD_DATA_MIG] = "RRQ.RD_DATA_MIG",
	[IDI_RRQ_INV_OWN] = "RRQ.INV_OWN",
	[IDI_RRQ_INV_X_TO_I] = "RRQ.INV_X_TO_I",
	[IDI_RRQ_INV_I_TO_E] = "RRQ.INV_I_TO_E",
	[IDI_RRQ_RD_INV] = "RRQ.RD_INV",
	[IDI_RRQ_INV_I_TO_M] = "RRQ.INV_I_TO_M",
	
	[IDI_IPQ_SNP_CUR] = "IPQ.SNP_CUR",
	[IDI_IPQ_SNP_CODE] = "IPQ.SNP_CODE",
	[IDI_IPQ_SNP_DATA] = "IPQ.SNP_DATA",
	[IDI_IPQ_SNP_DATA_MIG] = "IPQ.SNP_DATA_MIG",
	[IDI_IPQ_SNP_INV_OWN] = "IPQ.SNP_INV_OWN",
	[IDI_IPQ_SNP_INV_I_TO_E] = "IPQ.SNP_INV_I_TO_E"
};

// --------------------------------------

// https://en.wikipedia.org/wiki/CPUID#EAX=1:_Processor_Info_and_Feature_Bits
unsigned int cpu_model(void) {
	unsigned int cpuid_data[4];
	
	int ret = __get_cpuid(1, &cpuid_data[0], &cpuid_data[1],
		&cpuid_data[2], &cpuid_data[3]);
	assert(ret);
	
	return (cpuid_data[0] & 0xFFF0FF0);
}

// --------------------------------------

int msr_fd[NUM_CORES];

void msr_init(void) {
	char filename[128];
	
	for(int c = 0; c < NUM_CORES; c++) {
		snprintf(filename, sizeof filename, "/dev/cpu/%d/msr", c);
		
		if((msr_fd[c] = open(filename, O_RDWR)) == -1) {
			fprintf(stderr, "ERROR %s when trying to open %s\n",
				strerror(errno), filename);
			abort();
		}
	}
}

void msr_deinit(void) {
	for(int i = 0; i < NUM_CORES; i++)
		close(msr_fd[i]);
}

uint64_t msr_read(int cpu, off_t offset) {
	ssize_t rc;
	uint64_t val;
	
	rc = pread(msr_fd[cpu], &val, sizeof(val), offset);
	
	if(rc != sizeof(val)) {
		fprintf(stderr, "ERROR: failed to read from fd %d at offset 0x%08lx (%s)\n",
			msr_fd[cpu], (ulong) offset, strerror(errno));
		abort();
	}
	
	return val;
}

void msr_write(int cpu, uint64_t val, off_t offset) {
	ssize_t rc;
	
	rc = pwrite(msr_fd[cpu], &val, sizeof(val), offset);
	
	if(rc != sizeof(val)) {
		fprintf(stderr, "ERROR: failed to write 0x%08lx to fd %d at offset 0x%08lx (%s)\n",
			val, msr_fd[cpu], (ulong) offset, strerror(errno));
		abort();
	}
}

// --------------------------------------

// ICX: sudo lspci -vnn | grep 8086:3450 -A 2
uint PCI_PMON_bus[NUM_SOCKETS] = {0x7E, 0xFE};

uint PCI_PMON_M2M_device[NUM_M2M_BOXES] = {0x0C, 0x0D, 0x0E, 0x0F};
uint PCI_PMON_M2M_function[NUM_M2M_BOXES] = {0x0, 0x0, 0x0, 0x0};

uint PCI_PMON_IMC_device = 0x00;
uint PCI_PMON_IMC_function = 0x1;

// --------------------------------------

void *pci_mm_cfg_base = NULL;

// https://wiki.osdev.org/PCI_Express
// https://github.com/jdmccalpin/periodic-performance-counters
volatile uint32_t *pci_cfg_ptr(uint bus, uint device, uint function, uint offset) {
	assert(bus < (1 << 8));
	assert(device < (1 << 5));
	assert(function < (1 << 3));
	assert(offset < (1 << 12));
	
	uintptr_t offset_in_area = (bus << 20)
		| (device << 15) | (function << 12) | offset;
	
	assert(offset_in_area + 4 <= MMCONFIG_SIZE);
	
	return (pci_mm_cfg_base + offset_in_area);
}

void pci_cfg_w64(uint bus, uint device,
		uint function, uint offset, uint64_t val) {
	
	volatile uint32_t *ptr = pci_cfg_ptr(bus, device, function, offset);
	
	ptr[0] = val & 0xFFFFFFFF;
	ptr[1] = val >> 32;
}

void pci_cfg_w32(uint bus, uint device,
		uint function, uint offset, uint32_t val) {
	
	*pci_cfg_ptr(bus, device, function, offset) = val;
}

uint64_t pci_cfg_r64(uint bus, uint device,
		uint function, uint offset) {
	
	volatile uint32_t *ptr = pci_cfg_ptr(bus, device, function, offset);
	
	return ((uint64_t) ptr[1] << 32 | ptr[0]);
}

uint32_t pci_cfg_r32(uint bus, uint device,
		uint function, uint offset) {
	
	return *pci_cfg_ptr(bus, device, function, offset);
}

void pci_cfg_init(void) {
	uint32_t val;
	
	int mem_fd = open("/dev/mem", O_RDWR);
	
	if(mem_fd == -1) {
		fprintf(stderr, "ERROR when opening /dev/mem: %d (%s)\n",
			errno, strerror(errno));
		abort();
	}
	
	pci_mm_cfg_base = mmap(NULL, MMCONFIG_SIZE, PROT_READ | PROT_WRITE,
		MAP_SHARED, mem_fd, MMCONFIG_BASE);
	
	if(pci_mm_cfg_base == MAP_FAILED) {
		fprintf(stderr, "ERROR mapping PCI configuration space from /dev/mem "
			"@ address 0x%x %d (%s)\n", MMCONFIG_BASE, errno, strerror(errno));
		abort();
	}
	
	close(mem_fd);
	
	// http://linux-hardware.org/?view=search
	// 00:00.0: Intel Ice Lake Memory Map/VT-d - 8086:09a2
	// 00:00.1: Intel Ice Lake Mesh 2 PCIe - 8086:09a4
	
	val = *pci_cfg_ptr(0x00, 0x0, 0x0, 0);
	if(val != (0x09A2 << 16 | PCI_INTEL_VENDOR_ID)) {
		fprintf(stderr, "ERROR: PCI_cfg sanity check; "
			"At %02x:%02x.%01x[%d], expected 0x09a28086 got 0x%08x\n",
			0x00, 0x0, 0x0, 0, val);
		abort();
	}
	
	val = *pci_cfg_ptr(0x00, 0x0, 0x1, 0);
	if(val != (0x09A4 << 16 | PCI_INTEL_VENDOR_ID)) {
		fprintf(stderr, "ERROR: PCI_cfg sanity check; "
			"At %02x:%02x.%01x[%d], expected 0x09a28086 got 0x%08x\n",
			0x00, 0x0, 0x1, 0, val);
		abort();
	}
}

void pci_cfg_cleanup(void) {
	munmap(pci_mm_cfg_base, MMCONFIG_SIZE);
}

// ---

static void *pci_imc_base[NUM_SOCKETS][NUM_IMC_BOXES];

void pci_imc_w64(uint socket, uint imc, uint offset, uint64_t val) {
	volatile uint32_t *ptr = (uint32_t *)
		((char *) pci_imc_base[socket][imc] + offset);
	
	ptr[0] = val & 0xFFFFFFFF;
	ptr[1] = val >> 32;
}

void pci_imc_w32(uint socket, uint imc, uint offset, uint32_t val) {
	volatile uint32_t *ptr = (uint32_t *)
		((char *) pci_imc_base[socket][imc] + offset);
	
	*ptr = val;
}

uint64_t pci_imc_r64(uint socket, uint imc, uint offset) {
	volatile uint32_t *ptr = (uint32_t *)
		((char *) pci_imc_base[socket][imc] + offset);
	
	return ((uint64_t) ptr[1] << 32 | ptr[0]);
}

uint32_t pci_imc_r32(uint socket, uint imc, uint offset) {
	volatile uint32_t *ptr = (uint32_t *)
		((char *) pci_imc_base[socket][imc] + offset);
	
	return *ptr;
}

// --------------------------------------

static void perfctr_setup_core(void) {
	// Disable counters
	for(int c = 0; c < NUM_CORES; c++) {
		for(int ctr = 0; ctr < NUM_CORE_COUNTERS; ctr++)
			msr_write(c, 0, MSR_CORE_REG(PERFEVTSEL, ctr));
	}
}

static void perfctr_cleanup_core(void) {
	// Disable counters
	for(int c = 0; c < NUM_CORES; c++) {
		for(int ctr = 0; ctr < NUM_CORE_COUNTERS; ctr++)
			msr_write(c, 0, MSR_CORE_REG(PERFEVTSEL, ctr));
	}
}

// ---

static void perfctr_setup_cha(void) {
	for(int s = 0; s < NUM_SOCKETS; s++) {
		for(int cha = 0; cha < NUM_CHA_BOXES; cha++) {
			
			// Unfreeze at CHA box level (we'll handle freezing globally)
			msr_write(SOCKET_CPU(s), PMON_UNIT_CTL_ZSAFE, MSR_CHA_REG(cha, UNIT_CTL, 0));
			
			// Disable and zero counters
			for(int ctr = 0; ctr < NUM_CHA_COUNTERS; ctr++) {
				msr_write(SOCKET_CPU(s), 1L << CHA_CTL_RESET,
					MSR_CHA_REG(cha, CTL, ctr));
			}
		}
	}
}

static void perfctr_cleanup_cha(void) {
	// Disable and zero counters
	for(int s = 0; s < NUM_SOCKETS; s++) {
		for(int cha = 0; cha < NUM_CHA_BOXES; cha++) {
			for(int ctr = 0; ctr < NUM_CHA_COUNTERS; ctr++) {
				msr_write(SOCKET_CPU(s), 1L << CHA_CTL_RESET, MSR_CHA_REG(cha, CTL, ctr));
			}
		}
	}
}

// ---

static void perfctr_setup_m2m(void) {
	volatile uint32_t *pci_mm_ptr;
	
	// Sanity check
	for(int s = 0; s < NUM_SOCKETS; s++) {
		uint32_t val, expected;
		
		expected = (ICELAKE_SERVER_SOCKETID_UBOX_DID << 16 | PCI_INTEL_VENDOR_ID);
		val = *pci_cfg_ptr(PCI_PMON_bus[s], 0x0, 0x0, 0);
		
		if(val != expected) {
			fprintf(stderr, "ERROR: PCI_cfg PMON bus sanity check; "
				"At %02x:%02x.%01x[%d], expected 0x%08x got 0x%08x\n",
				PCI_PMON_bus[s], 0x0, 0x1, 0, expected, val);
			abort();
		}
		
		expected = (PCI_PMON_M2M_DEVICE_ID << 16 | PCI_INTEL_VENDOR_ID);
		for(int m = 0; m < NUM_M2M_BOXES; m++) {
			val = *pci_cfg_ptr(PCI_PMON_bus[s],
				PCI_PMON_M2M_device[m], PCI_PMON_M2M_function[m], 0);
			
			if(val != expected) {
				fprintf(stderr, "ERROR: PCI_cfg PMON M2M box sanity check; "
					"At %02x:%02x.%01x[%d], expected 0x%08x got 0x%08x\n",
					PCI_PMON_bus[s], 0x0, 0x1, 0, expected, val);
				abort();
			}
		}
	}
	
	for(int s = 0; s < NUM_SOCKETS; s++) {
		for(int m = 0; m < NUM_M2M_BOXES; m++) {
			uint bus = PCI_PMON_bus[s];
			uint device = PCI_PMON_M2M_device[m];
			uint function = PCI_PMON_M2M_function[m];
			
			// Unfreeze at M2M-box level (we'll handle freezing globally)
			pci_mm_ptr = pci_cfg_ptr(bus, device,
				function, PCI_PMON_M2M_UNIT_CTL);
			*pci_mm_ptr = PMON_UNIT_CTL_ZSAFE;
			
			// Disable and zero counters
			for(int ctr = 0; ctr < NUM_M2M_COUNTERS; ctr++) {
				pci_mm_ptr = pci_cfg_ptr(bus, device,
					function, PCI_M2M_REG(CTL, ctr));
				*pci_mm_ptr = 1L << M2M_CTL_RESET;
			}
			
			/* We don't alter these anywhere, so set them here once.
			 * We only alter OPCODE_MM for PKT_MATCH events */
			pci_cfg_w64(bus, device, function, PCI_PMON_M2M_ADDRMATCH, 0);
			pci_cfg_w64(bus, device, function, PCI_PMON_M2M_ADDRMASK,
				PCI_PMON_M2M_ADDR_FILTER_MASK);
		}
	}
}

static void perfctr_cleanup_m2m(void) {
	volatile uint32_t *pci_mm_ptr;
	
	for(int s = 0; s < NUM_SOCKETS; s++) {
		for(int m = 0; m < NUM_M2M_BOXES; m++) {
			uint bus = PCI_PMON_bus[s];
			uint device = PCI_PMON_M2M_device[m];
			uint function = PCI_PMON_M2M_function[m];
			
			// Disable and zero counters
			for(int ctr = 0; ctr < NUM_M2M_COUNTERS; ctr++) {
				pci_mm_ptr = pci_cfg_ptr(bus, device,
					function, PCI_M2M_REG(CTL, ctr));
				*pci_mm_ptr = 1L << M2M_CTL_RESET;
			}
		}
	}
}

// ---

// To determine the starting area in /dev/mem
#define PCI_PMON_IMC_MMIO_BASE_OFFSET 0xd0
#define PCI_PMON_IMC_MMIO_BASE_MASK 0x1FFFFFFF
#define PCI_PMON_IMC_MMIO_BASE_SHIFT 23

// To determine the starting area for each IMC in /dev/mem (along with MMIO_BASE)
#define PCI_PMON_IMC_MEM_BAR_OFFSET(imc) (0xd8 + (imc) * 0x04)
#define PCI_PMON_IMC_MEM_BAR_MASK 0x7FF
#define PCI_PMON_IMC_MEM_BAR_SHIFT 12

static void perfctr_setup_imc(void) {
	int mem_fd;
	uint32_t val;
	
	if((mem_fd = open("/dev/mem", O_RDWR)) == -1) {
		fprintf(stderr, "ERROR when opening /dev/mem: %d (%s)\n",
			errno, strerror(errno));
		abort();
	}
	
	for(int s = 0; s < NUM_SOCKETS; s++) {
		uint32_t expected = (PCI_PMON_IMC_DEVICE_ID << 16 | 0x8086);
		val = pci_cfg_r32(PCI_PMON_bus[s], PCI_PMON_IMC_device,
			PCI_PMON_IMC_function, 0);
		
		if(val != expected) {
			fprintf(stderr, "ERROR: PCI_cfg PMON IMC box sanity check; "
				"At %02x:%02x.%01x[%d], expected 0x%08x got 0x%08x\n",
				PCI_PMON_bus[s], PCI_PMON_IMC_device, PCI_PMON_IMC_function,
				0, expected, val);
			abort();
		}
		
		// ---
		
		val = pci_cfg_r32(PCI_PMON_bus[s], PCI_PMON_IMC_device,
			PCI_PMON_IMC_function, PCI_PMON_IMC_MMIO_BASE_OFFSET);
		
		uint64_t MMIO_BASE = (val & PCI_PMON_IMC_MMIO_BASE_MASK)
			<< PCI_PMON_IMC_MMIO_BASE_SHIFT;
		
		for(int imc = 0; imc < NUM_IMC_BOXES; imc++) {
			uint32_t BAR_raw = pci_cfg_r32(PCI_PMON_bus[s], PCI_PMON_IMC_device,
				PCI_PMON_IMC_function, PCI_PMON_IMC_MEM_BAR_OFFSET(imc));
			
			uint64_t MEM_base = (BAR_raw & PCI_PMON_IMC_MEM_BAR_MASK)
				<< PCI_PMON_IMC_MEM_BAR_SHIFT;
			
			uint64_t IMC_MMIO_base = MMIO_BASE + MEM_base;
			
			// Attach to area of IMC, as determined through MEMx_BAR
			pci_imc_base[s][imc] = mmap(NULL, PCI_PMON_IMC_MMIO_SIZE,
				PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, IMC_MMIO_base);
			assert(pci_imc_base[s][imc] != MAP_FAILED);
			
			for(int chn = 0; chn < NUM_IMC_CHANNELS; chn++) {
				// Unfreeze at IMC-box level (we'll handle freezing globally)
				pci_imc_w32(s, imc, PCI_PMON_IMC_UNIT_CTL(chn), PMON_UNIT_CTL_ZSAFE);
				
				// Disable and zero counters
				for(int ctr = 0; ctr < NUM_IMC_COUNTERS; ctr++)
					pci_imc_w32(s, imc, PCI_IMC_REG_CTL(chn, ctr), 1 << IMC_CTL_RESET);
			}
		}
	}
	
	close(mem_fd);
}

static void perfctr_cleanup_imc(void) {
	for(int s = 0; s < NUM_SOCKETS; s++) {
		for(int imc = 0; imc < NUM_IMC_BOXES; imc++) {
			for(int chn = 0; chn < NUM_IMC_CHANNELS; chn++) {
				// Disable and zero counters
				for(int ctr = 0; ctr < NUM_IMC_COUNTERS; ctr++) {
					pci_imc_w32(s, imc, PCI_IMC_REG_CTL(chn, ctr),
						1 << IMC_CTL_RESET);
				}
			}
			
			munmap(pci_imc_base[s][imc], PCI_PMON_IMC_MMIO_SIZE);
		}
	}
}

// ---

void perfctr_setup(bool want_prefetch) {
	printf("Setting up MSR\n");
	
	unsigned int model = cpu_model();
	
	/* Abort if not on ICX, all this is only tested on ICX (CARV's Titan). The
	 * MSR addresses and bit indices may be different on different processors.
	 * Faulty MSR manipulation could lead to damage (?) or unforeseen effects. */
	switch(model) {
		case CPU_MODEL_ICX: break;
		default:
			fprintf(stderr, "Woah!! I don't know this CPU! (0x%08x)\n", model);
			abort();
	}
	
	// Freeze uncore counters globally
	for(int s = 0; s < NUM_SOCKETS; s++)
		msr_write(SOCKET_CPU(s), 1L << GLOBAL_CTL_FRZ_ALL, MSR_PMON_GLOBAL_CTL);
	
	// Freeze all cores' counters
	for(int c = 0; c < NUM_CORES; c++)
		msr_write(c, CORE_GLOBAL_CTL_FREEZE, MSR_PMON_CORE_GLOBAL_CTL);
	
	perfctr_setup_core();
	perfctr_setup_cha();
	perfctr_setup_m2m();
	perfctr_setup_imc();
	
	if(!want_prefetch) {
		/* Disable prefetchers. Appears that disabling
		 * for the core affects both of the HW threads */
		for(int c = 0; c < NUM_CORES; c++)
			msr_write(c, MSR_PREFETCH_ALL_DISABLE, MSR_PREFETCH_CONTROL);
	}
}

void perfctr_cleanup(bool want_prefetch) {
	printf("Cleaning up MSR\n");
	
	perfctr_cleanup_core();
	perfctr_cleanup_cha();
	perfctr_cleanup_m2m();
	perfctr_cleanup_imc();
	
	
	if(!want_prefetch) {
		// Re-enable prefetchers
		for(int c = 0; c < NUM_CORES; c++)
			msr_write(c, 0, MSR_PREFETCH_CONTROL);
	}
}
