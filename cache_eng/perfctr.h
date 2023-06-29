#ifndef PERFCTR_ENG_H
#define PERFCTR_ENG_H

#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>

/* Manual: 3rd Gen Intel® Xeon® Processor Scalable Family, Codename Ice Lake,
 * Uncore Performance Monitoring, Reference Num 639778, Rev 1.00, May 2021 */

// --------------------------------------
// SYSTEM
// --------------------------------------

#define NUM_SOCKETS 2
#define NUM_CORES_PER_SOCKET 24
// #define NUM_CPUS_PER_CORE 2

// ---

#define NUM_CORES (NUM_SOCKETS * NUM_CORES_PER_SOCKET)
// #define NUM_CPUS (NUM_CORES * NUM_CPUS_PER_CORE)

#define NUM_CORE_COUNTERS 4

#define NUM_CHA_BOXES (NUM_CORES_PER_SOCKET)
#define NUM_CHA_COUNTERS 4

#define NUM_M2M_BOXES 4
#define NUM_M2M_COUNTERS 4

#define NUM_IMC_BOXES 4
#define NUM_IMC_CHANNELS 3
#define NUM_IMC_COUNTERS 4

// Max number of counters that a PMON box might have
#define PMON_NUM_COUNTERS_MAX 4

// --------------------------------------
// /dev/mem
// --------------------------------------

// sudo grep MMCONFIG /proc/iomem
#define MMCONFIG_BASE 0x80000000
#define MMCONFIG_BOUND 0x8FFFFFFF
#define MMCONFIG_SIZE (MMCONFIG_BOUND - MMCONFIG_BASE + 1)

// --------------------------------------
// MISCELLANEOUS
// --------------------------------------

#define PCI_INTEL_VENDOR_ID 0x8086
#define PCI_PMON_M2M_DEVICE_ID 0x344A
#define PCI_PMON_IMC_DEVICE_ID 0x3451
#define ICELAKE_SERVER_SOCKETID_UBOX_DID 0x3450

#define CPU_MODEL_ICX 0x00606A0

// --------------------------------------
// PREFETCHERS
// --------------------------------------

/* Intel® 64 and IA-32 Architectures Software Developer’s Manual
 * Volume 4: Model-Specific Registers: https://www.intel.com/content
 *   /dam/develop/external/us/en/documents/335592-sdm-vol-4.pdf*/

/* @gkatev: Though, it's not entirely clear there that for ICX 0x1A4L is
 * the correct MSR. Put together using different resources, seems to work:
 * Likwid src/cpuFeatures.c (https://github.com/RRZE-HPC/likwid @ a4116cd)
 * "Disclosure of H/W prefetchers control on some Intel processors" (archived):
 *   https://radiable56.rssing.com/chan-25518398/article18.html
 *   https://web.archive.org/web/20200507141019/https://software.intel.com/content/www/us/en
 *     /develop/articles/disclosure-of-hw-prefetcher-control-on-some-intel-processors.html
 */

#define MSR_PREFETCH_CONTROL 0x1A4

#define MSR_PREFETCH_HW_DISABLE 0
#define MSR_PREFETCH_CL_DISABLE 1
#define MSR_PREFETCH_DCU_DISABLE 2
#define MSR_PREFETCH_IP_DISABLE 3

#define MSR_PREFETCH_ALL_DISABLE ( \
	1 << MSR_PREFETCH_HW_DISABLE | 1 << MSR_PREFETCH_CL_DISABLE | \
	1 << MSR_PREFETCH_DCU_DISABLE | 1 << MSR_PREFETCH_IP_DISABLE)

// --------------------------------------
// GLOBAL
// --------------------------------------

#define MSR_PMON_GLOBAL_CTL 0x700
#define GLOBAL_CTL_UNFRZ_ALL 61
#define GLOBAL_CTL_FRZ_ALL 63

#define MSR_PMON_CORE_GLOBAL_CTL 0x38F
#define CORE_GLOBAL_CTL_FREEZE 0
#define CORE_GLOBAL_CTL_UNFREEZE ((1 << NUM_CORE_COUNTERS) - 1)

#define PMON_UNIT_CTL_ZSAFE 0x00030000

// --------------------------------------
// CORE
// --------------------------------------

#define MSR_PMON_CORE_PERFEVTSEL_BASE 0x186
#define MSR_PMON_CORE_PMC_BASE 0x0C1

#define MSR_CORE_REG(reg, n) \
	(MSR_PMON_CORE_##reg##_BASE + (n))

#define CORE_PERFEVTSEL_EVENT 0
#define CORE_PERFEVTSEL_UMASK 8
#define CORE_PERFEVTSEL_USR 16
#define CORE_PERFEVTSEL_ANY 21
#define CORE_PERFEVTSEL_ENABLE 22

#define CORE_PERFEVTSEL(event, umask) \
	((1L << CORE_PERFEVTSEL_ENABLE | 1L << CORE_PERFEVTSEL_ANY | \
	1L << CORE_PERFEVTSEL_USR | (umask) << CORE_PERFEVTSEL_UMASK | \
	(event) << CORE_PERFEVTSEL_EVENT))

// --------------------------------------
// OFF-CORE (but not UN-core)
// --------------------------------------

// #define MSR_PMON_OFFCORE_RSP_0 0x1A6
// #define MSR_PMON_OFFCORE_RSP_1 0x1A7

// --------------------------------------
// CHA
// --------------------------------------

static inline uint MSR_PMON_CHA_BASE(uint cha) {
	if(cha < 18)
		return 0x0E00 + cha * 0x0E;
	else if(cha < 34)
		return 0x0F0A + (cha - 18) * 0x0E;
	else
		return 0x0B60 + (cha - 34) * 0x0E;
}

#define MSR_PMON_CHA_UNIT_CTL_OFFSET 0x00
#define MSR_PMON_CHA_UNIT_STATUS_OFFSET 0x07
#define MSR_PMON_CHA_CTR_OFFSET 0x08
#define MSR_PMON_CHA_CTL_OFFSET 0x01
#define MSR_PMON_CHA_FILTER_OFFSET 0x05

#define MSR_CHA_REG(cha, reg, n) \
	(MSR_PMON_CHA_BASE(cha) + MSR_PMON_CHA_##reg##_OFFSET + (n))

#define CHA_CTL_EVENT 0
#define CHA_CTL_UMASK 8
#define CHA_CTL_RESET 17
#define CHA_CTL_ENABLE 22
#define CHA_CTL_UMASK_EXT 32

#define CHA_CTL_EXT(event, umask, umask_ext) \
	(((uint64_t) umask_ext) << CHA_CTL_UMASK_EXT | 1L << CHA_CTL_ENABLE \
	| (umask) << CHA_CTL_UMASK | (event) << CHA_CTL_EVENT)

#define CHA_CTL(event, umask) CHA_CTL_EXT(event, umask, 0L)

// --------------------------------------
// PCICFG Common Defs
// --------------------------------------

extern uint PCI_PMON_bus[NUM_SOCKETS];

// --------------------------------------
// M2M
// --------------------------------------

#define PCI_PMON_M2M_UNIT_CTL 0x438
#define PCI_PMON_M2M_UNIT_STATUS 0x4A8

#define PCI_PMON_M2M_CTR_BASE 0x440
#define PCI_PMON_M2M_CTL_BASE 0x468

#define PCI_M2M_REG(reg, n) \
	(PCI_PMON_M2M_##reg##_BASE + (n) * 0x8)

#define M2M_CTL_EVENT 0
#define M2M_CTL_UMASK 8
#define M2M_CTL_RESET 17
#define M2M_CTL_ENABLE 22
#define M2M_CTL_UMASK_EXT 32

#define M2M_CTL_EXT(event, umask, umask_ext) \
	(((uint64_t) umask_ext) << M2M_CTL_UMASK_EXT | 1L << M2M_CTL_ENABLE \
	| (umask) << M2M_CTL_UMASK | (event) << M2M_CTL_EVENT)

#define M2M_CTL(event, umask) M2M_CTL_EXT(event, umask, 0L)

/* Manual is faulty; has copy-paste bug from SKX one. These are
 * reverse-engineered -- may be wrong or not fully accurate. */
#define PCI_PMON_M2M_ADDRMATCH 0x490
#define PCI_PMON_M2M_ADDRMASK 0x498
#define PCI_PMON_M2M_OPCODE_MM 0x4A0

// This is according to the manual, but may well also be faulty in the manual
#define PCI_PMON_M2M_ADDR_FILTER_MASK 0x7FFFFFFFFFFF

extern uint PCI_PMON_M2M_device[NUM_M2M_BOXES];
extern uint PCI_PMON_M2M_function[NUM_M2M_BOXES];

// --------------------------------------
// IMC
// --------------------------------------

#define PCI_PMON_IMC_MMIO_CHN_SIZE 0x4000
#define PCI_PMON_IMC_MMIO_SIZE (PCI_PMON_IMC_UNIT_CTL(0) \
	+ NUM_IMC_CHANNELS * PCI_PMON_IMC_MMIO_CHN_SIZE)

// ---

#define PCI_PMON_IMC_UNIT_CTL(chn) (0x22800 + ((chn) * PCI_PMON_IMC_MMIO_CHN_SIZE))
#define PCI_PMON_IMC_UNIT_STATUS(chn) (0x2285C + ((chn) * PCI_PMON_IMC_MMIO_CHN_SIZE))
#define PCI_PMON_IMC_CTR_BASE(chn) (0x22808 + ((chn) * PCI_PMON_IMC_MMIO_CHN_SIZE))
#define PCI_PMON_IMC_CTL_BASE(chn) (0x22840 + ((chn) * PCI_PMON_IMC_MMIO_CHN_SIZE))

#define PCI_IMC_REG_CTL(chn, n) (PCI_PMON_IMC_CTL_BASE(chn) + (n) * 0x4)
#define PCI_IMC_REG_CTR(chn, n) (PCI_PMON_IMC_CTR_BASE(chn) + (n) * 0x8)

#define IMC_CTL_EVENT 0
#define IMC_CTL_UMASK 8
#define IMC_CTL_RESET 17
#define IMC_CTL_ENABLE 22

#define IMC_CTL(event, umask) \
	(1L << IMC_CTL_ENABLE | (umask) << IMC_CTL_UMASK | (event) << IMC_CTL_EVENT)

// ---

// Free running IMC counters
#define PCI_PMON_IMC_RD_DDR 0x2290
#define PCI_PMON_IMC_WR_DDR 0x2298
#define PCI_PMON_IMC_RD_PMM 0x22A0
#define PCI_PMON_IMC_WR_PMM 0x22A8
#define PCI_PMON_IMC_DCLK 0x22B0

extern uint PCI_PMON_IMC_device;
extern uint PCI_PMON_IMC_function;

// --------------------------------------

typedef enum pmon_box_enum_t {
	PMON_BOX_CORE,
	PMON_BOX_CHA,
	PMON_BOX_M2M,
	PMON_BOX_IMC,
	
	PMON_BOX_COUNT
} pmon_box_enum_t;

extern const char *pmon_box_str[PMON_BOX_COUNT];
extern int pmon_box_ev_cnt[PMON_BOX_COUNT];
extern int pmon_box_num_counters[PMON_BOX_COUNT];

// Max possible event code amongst all boxes
#define PMON_EVENT_MAX 0xFF

typedef enum core_events_enum_t {
	CORE_EVENT_CORE_SNOOP_RESPONSE = 0xEF,
	CORE_EVENT_LONGEST_LAT_CACHE = 0x2E,
	
	CORE_EVENT_L1D = 0x01,
	CORE_EVENT_L2_LINES_IN = 0xF1,
	CORE_EVENT_L2_LINES_OUT = 0xF2,
	CORE_EVENT_L2_RQSTS = 0x24,
	CORE_EVENT_L2_TRANS = 0xF0,
	
	CORE_EVENT_MEM_INST_RETIRED = 0xD0,
	CORE_EVENT_MEM_LOAD_L3_HIT_RETIRED = 0xD2,
	CORE_EVENT_MEM_LOAD_L3_MISS_RETIRED = 0xD3,
	CORE_EVENT_MEM_LOAD_RETIRED = 0xD1,
	
	CORE_EVENT_OFFCORE_REQUESTS = 0xB0,
	
	CORE_EVENT_IDI_MISC = 0xFE,
	
	// highest possible event code + 1
	CORE_EVENT_COUNT = 0xFF + 1
} core_events_enum_t;

typedef enum cha_events_enum_t {
	CHA_EVENT_LLC_LOOKUP = 0x34,
	CHA_EVENT_LLC_VICTIMS = 0x37,
	
	CHA_EVENT_SF_EVICTION = 0x3D,
	
	CHA_EVENT_DIR_LOOKUP = 0x53,
	CHA_EVENT_DIR_UPDATE = 0x54,
	
	CHA_EVENT_IMC_READS_COUNT = 0x59,
	CHA_EVENT_IMC_WRITES_COUNT = 0x5B,
	
	CHA_EVENT_BYPASS_CHA_IMC = 0x57,
	
	CHA_EVENT_HITME_LOOKUP = 0x5E,
	CHA_EVENT_HITME_HIT = 0x5F,
	CHA_EVENT_HITME_MISS = 0x60,
	CHA_EVENT_HITME_UPDATE = 0x61,
	
	CHA_EVENT_TOR_INSERTS = 0x35,
	CHA_EVENT_REQUESTS = 0x50,
	CHA_EVENT_OSB = 0x55,
	
	CHA_EVENT_SNOOPS_SENT = 0x51,
	CHA_EVENT_SNOOP_RESP = 0x5C,
	CHA_EVENT_SNOOP_RESP_LOCAL = 0x5D,
	CHA_EVENT_SNOOP_RSP_MISC = 0x6B,
	
	CHA_EVENT_CORE_SNP = 0x33,
	
	// experimental
	CHA_EVENT_XSNP_RESP = 0x32,
	
	CHA_EVENT_WB_PUSH_MTOI = 0x56,
	CHA_EVENT_MISC = 0x39,
	
	// highest possible event code + 1
	CHA_EVENT_COUNT = 0x70 + 1
} cha_events_enum_t;

typedef enum m2m_events_enum_t {
	M2M_EVENT_BYPASS_M2M_INGRESS = 0x21,
	M2M_EVENT_BYPASS_M2M_EGRESS = 0x22,
	
	M2M_EVENT_DIRECTORY_HIT = 0x2A,
	M2M_EVENT_DIRECTORY_MISS = 0x2B,
	M2M_EVENT_DIRECTORY_LOOKUP = 0x2D,
	M2M_EVENT_TAG_HIT = 0x2C,
	M2M_EVENT_TAG_MISS = 0x61,
	
	// experimental
	M2M_EVENT_DIRECTORY_UPDATE = 0x2E,
	
	M2M_EVENT_IMC_READS = 0x37,
	M2M_EVENT_IMC_WRITES = 0x38,
	
	// experimental
	M2M_EVENT_PKT_MATCH = 0x4C,
	
	// highest possible event code + 1
	M2M_EVENT_COUNT = 0xF2 + 1
} m2m_events_enum_t;

typedef enum imc_events_enum_t {
	IMC_EVENT_CAS_COUNT = 0x04,
	IMC_EVENT_RPQ_INSERTS = 0x10,
	IMC_EVENT_WBQ_INSERTS = 0x20,
	
	// highest possible event code + 1
	IMC_EVENT_COUNT = 0xEB + 1
} imc_events_enum_t;

extern const char *pmon_event_str[PMON_BOX_COUNT][PMON_EVENT_MAX];

// ---

typedef enum idi_opcode_enum_t {
	IDI_ISMQ_RSP_I = 0x00L,
	IDI_ISMQ_RSP_S = 0x01L,
	IDI_ISMQ_RSP_V = 0x26L,
	IDI_ISMQ_RSP_DATA_M = 0x02L,
	IDI_ISMQ_RSP_I_FWD_M = 0x03L,
	IDI_ISMQ_RSP_I_FWD_M_PTL = 0x33L,
	IDI_ISMQ_PULL_DATA = 0x04L,
	IDI_ISMQ_PULL_DATA_PTL = 0x34L,
	IDI_ISMQ_PULL_DATA_BOGUS = 0x05L,
	IDI_ISMQ_CMP = 0x06L,
	IDI_ISMQ_CMP_FWD_CODE = 0x07L,
	IDI_ISMQ_CMP_FWD_INV_I_TO_E = 0x08L,
	IDI_ISMQ_CMP_PULL_DATA = 0x09L,
	IDI_ISMQ_CMP_FWD_INV_OWN = 0x0BL,
	IDI_ISMQ_DATA_C_CMP = 0x0CL,
	IDI_ISMQ_VICTIM = 0x1BL,
	IDI_ISMQ_DATA_NC = 0x1EL,
	IDI_ISMQ_DATA_C = 0x20L,
	IDI_ISMQ_RSP_I_FWD_FE = 0x23L,
	IDI_ISMQ_RSP_S_FWD_FE = 0x24L,
	IDI_ISMQ_FWD_CN_FLT = 0x25L,
	IDI_ISMQ_LLC_VICTIM = 0x31L,
	
	IDI_IRQ_RFO = 0x100,
	IDI_IRQ_RFO_PREF = 0x110,
	IDI_IRQ_CRD = 0x101,
	IDI_IRQ_CRD_UC = 0x105,
	IDI_IRQ_CRD_PREF = 0x111,
	IDI_IRQ_DRD = 0x102,
	IDI_IRQ_DRD_PREF = 0x112,
	IDI_IRQ_DRD_OPT = 0x104,
	IDI_IRQ_DRD_OPT_PREF = 0x114,
	IDI_IRQ_DRD_PTE = 0x106,
	IDI_IRQ_P_RD = 0x107,
	IDI_IRQ_WC_I_LF = 0x10C,
	IDI_IRQ_WC_I_L = 0x10D,
	IDI_IRQ_UC_RD_F = 0x10E,
	IDI_IRQ_W_I_L = 0x10F,
	IDI_IRQ_CLFLUSH = 0x118,
	IDI_IRQ_CLFLUSH_OPT = 0x11A,
	IDI_IRQ_CLWB = 0x11C,
	IDI_IRQ_PCI_RD_CUR = 0x11E,
	IDI_IRQ_CL_CLEANSE = 0x13C,
	IDI_IRQ_WB_PUSH_HINT = 0x1A4,
	IDI_IRQ_WB_M_TO_I = 0x184,
	IDI_IRQ_WB_M_TO_E = 0x185,
	IDI_IRQ_WB_EF_TO_I = 0x186,
	IDI_IRQ_WB_EF_TO_E = 0x187,
	IDI_IRQ_WB_S_TO_I = 0x18C,
	IDI_IRQ_I_TO_M = 0x188,
	IDI_IRQ_I_TO_M_CACHE_NEAR = 0x1A8,
	IDI_IRQ_SPEC_I_TO_M = 0x18A,
	IDI_IRQ_LLC_PREF_RFO = 0x198,
	IDI_IRQ_LLC_PREF_CODE = 0x199,
	IDI_IRQ_LLC_PREF_DATA = 0x19A,
	IDI_IRQ_INT_LOG = 0x1D9,
	IDI_IRQ_INT_PHY = 0x1DA,
	IDI_IRQ_INT_PRI_UP = 0x1DB,
	IDI_IRQ_SPLIT_LOCK = 0x1DE,
	IDI_IRQ_LOCK = 0x1DF,
	
	IDI_WBQ_WB_M_TO_I = 0x400L,
	IDI_WBQ_WB_M_TO_S = 0x401L,
	IDI_WBQ_WB_M_TO_E = 0x402L,
	IDI_WBQ_NON_SNP_WR = 0x403L,
	IDI_WBQ_WB_M_TO_I_PTL = 0x404L,
	IDI_WBQ_WB_M_TO_E_PTL = 0x406L,
	IDI_WBQ_NON_SNP_WR_PTL = 0x407L,
	IDI_WBQ_WB_PUSH_M_TO_I = 0x408L,
	IDI_WBQ_WB_FLUSH = 0x40BL,
	IDI_WBQ_EVCT_CLN = 0x40CL,
	IDI_WBQ_NON_SNP_RD = 0x40DL,
	
	IDI_RRQ_RD_CUR = 0x500L,
	IDI_RRQ_RD_CODE = 0x501L,
	IDI_RRQ_RD_DATA = 0x502L,
	IDI_RRQ_RD_DATA_MIG = 0x503L,
	IDI_RRQ_INV_OWN = 0x504L,
	IDI_RRQ_INV_X_TO_I = 0x505L,
	IDI_RRQ_INV_I_TO_E = 0x087L,
	IDI_RRQ_RD_INV = 0x50CL,
	IDI_RRQ_INV_I_TO_M = 0x500L,
	
	IDI_IPQ_SNP_CUR = 0x700L,
	IDI_IPQ_SNP_CODE = 0x701L,
	IDI_IPQ_SNP_DATA = 0x702L,
	IDI_IPQ_SNP_DATA_MIG = 0x703L,
	IDI_IPQ_SNP_INV_OWN = 0x704L,
	IDI_IPQ_SNP_INV_I_TO_E = 0x705L,
	
	// highest possible op code + 1
	IDI_OPCODE_COUNT = 0x705 + 1
} idi_opcode_enum_t;

extern const char *idi_opcode_str[IDI_OPCODE_COUNT];

// typedef enum smi3_opcode_enum_t {
	
// } smi3_opcode_enum_t;

// --------------------------------------

unsigned int cpu_model(void);

int cpu_in_socket(int socket);

// --------------------------------------

void msr_init(void);
void msr_deinit(void);
uint64_t msr_read(int cpu, off_t offset);
void msr_write(int cpu, uint64_t val, off_t offset);

// --------------------------------------

void pci_cfg_init(void);
void pci_cfg_cleanup(void);

volatile uint32_t *pci_cfg_ptr(uint bus, uint device, uint function, uint offset);

void pci_cfg_w64(uint bus, uint device, uint function, uint offset, uint64_t val);
void pci_cfg_w32(uint bus, uint device, uint function, uint offset, uint32_t val);

uint64_t pci_cfg_r64(uint bus, uint device, uint function, uint offset);
uint32_t pci_cfg_r32(uint bus, uint device, uint function, uint offset);

// ---

void pci_imc_w64(uint socket, uint imc, uint offset, uint64_t val);
void pci_imc_w32(uint socket, uint imc, uint offset, uint32_t val);
uint64_t pci_imc_r64(uint socket, uint imc, uint offset);
uint32_t pci_imc_r32(uint socket, uint imc, uint offset);

// --------------------------------------

void perfctr_setup(bool want_prefetch);
void perfctr_cleanup(bool want_prefetch);

// --------------------------------------

#endif
