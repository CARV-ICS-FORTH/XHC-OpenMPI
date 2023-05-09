#ifndef CACHE_ENG_H
#define CACHE_ENG_H

#include <stdint.h>
#include <signal.h>
#include <stdbool.h>
#include <cpuid.h>
#include <time.h>

// #define TS_TIMESPEC
#define TS_TSC

#define RETURN_WITH_ERROR(var, err, label) ({(var) = (err); goto label;})

// -----------

extern int ncache_lines;
extern bool measure_times;
extern int time_exp;
extern bool sum_per_socket;
extern bool per_box_report;
extern bool enable_perfctr;
extern bool enable_prefetch;

// -----------

#ifdef TS_TIMESPEC
	typedef struct timespec ts_t;
#elif defined(TS_TSC)
	typedef uint64_t ts_t;
#endif

// -----------

typedef struct root_ctrl_t {
	volatile sig_atomic_t seq;
	volatile sig_atomic_t ack __attribute__((aligned(64)));
	
	volatile ts_t ts __attribute__((aligned(64)));
} root_ctrl_t;

typedef struct rank_ctrl_t {
	volatile sig_atomic_t seq;
	volatile sig_atomic_t ack __attribute__((aligned(64)));
	
	volatile sig_atomic_t flag __attribute__((aligned(64)));
} rank_ctrl_t;

typedef struct ctr_event_t {
	uint64_t event, umask, xtra;
	char *desc;
} ctr_event_t;

// -----------

typedef enum msm_kind_enum_t {
	MSM_ACK,
	MSM_JOIN,
	MSM_MAIN,
	MSM_MEMCPY,
	MSM_MEMCPY_W,
	MSM_COUNT
} msm_kind_enum_t;

extern const char *msm_kind_str[MSM_COUNT];

typedef struct msm_point_t {
	double ts;
	double time;
} msm_point_t;

typedef struct msm_line_t {
	msm_point_t *points;
	size_t size, len;
} msm_line_t;

void msm_init(msm_line_t *msm_lines, int nlines, int npoints_hint);
void msm_dealloc(msm_line_t *msm_lines, int nlines);
void msm_insert(msm_line_t *msm_lines, msm_kind_enum_t kind,
	double timestamp, double time_spent);
void msm_add(msm_line_t *msm_lines, msm_kind_enum_t kind, msm_point_t *point,
	size_t npoints, double ts_offset, double time_offset);
void msm_string(msm_line_t *msm_lines, msm_kind_enum_t kind,
	int iterations, char **ts_str, char **time_str);

// -----------

void perfctr_report_core(uint64_t initial_values[*][*],
	const char **event_names, int iterations);
void perfctr_report_cha(uint64_t initial_values[*][*][*],
	const char **event_names, int iterations);
void perfctr_report_m2m(uint64_t initial_values[*][*][*],
	const char **event_names, int iterations);
void perfctr_report_imc(uint64_t initial_values[*][*][*][*],
	const char **event_names, int iterations);

// -----------

#ifdef TS_TIMESPEC
	#define TS_ZINIT ((struct timespec) {0})
	#define TS_PRINT_MOD ".2lf"
	
	typedef struct timespec ts_t;
	
	static inline struct timespec TS(void) {
		struct timespec ts;
		
		#if defined(__x86_64) || defined(__x86_64__)
			__builtin_ia32_mfence();
		#elif defined(__arm__) || defined(__aarch64__)
			__asm__ __volatile__("dmb sy" : : : "memory");
		#else
			#error "Unknown arch, implement memory barrier"
		#endif
		
		clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
		return ts;
	}
	
	static inline void ts_init(void) {}
	
	static inline struct timespec ts_diff(struct timespec start, struct timespec end) {
		struct timespec result = (struct timespec) {
			.tv_sec = end.tv_sec - start.tv_sec,
			.tv_nsec = end.tv_nsec - start.tv_nsec
		};
		if (result.tv_nsec < 0) {
			result.tv_sec--;
			result.tv_nsec += 1000000000L;
		}
		return result;
	}
	
	static inline struct timespec ts_incr(struct timespec ts, struct timespec offset) {
		ts.tv_sec += offset.tv_sec;
		ts.tv_nsec += offset.tv_nsec;
		if(ts.tv_nsec >= 1000000000L) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000L;
		}
		return ts;
	}
	
	static inline struct timespec ts_ndiv(struct timespec ts, int n) {
		long double nsec = ((long double) ts.tv_sec * 1e09 + ts.tv_nsec)/n;
		return (struct timespec) {
			.tv_sec = (long long) nsec / 1000000000L,
			.tv_nsec = (long long) nsec % 1000000000L
		};
	}
	
	static inline bool ts_is_zero(struct timespec ts) {
		return (ts.tv_sec == 0 && ts.tv_nsec == 0);
	}
	
	static inline double ts_elapsed_ns(struct timespec start, struct timespec end) {
		return (double) ((end.tv_sec - start.tv_sec) * 1e09)
			+ (double) (end.tv_nsec - start.tv_nsec);
	}
	
	static inline double ts_elapsed_us(struct timespec start, struct timespec end) {
		return (double) ((end.tv_sec - start.tv_sec) * 1e06)
			+ (double) ((end.tv_nsec - start.tv_nsec) / 1e03);
	}
#elif defined(TS_TSC)
	#define TS_ZINIT 0
	#define TS_PRINT_MOD ".2lf"
	
	typedef uint64_t ts_t;
	
	static unsigned base_freq = 0;
	
	static unsigned int cpu_base_freq_mhz(void) {
		uint32_t eax, ebx, ecx, edx;
		int ret = __get_cpuid(0x16, &eax, &ebx, &ecx, &edx);
		return (ret ? eax : 0);
	}
	
	static inline __attribute__((always_inline)) uint64_t TS(void) {
		unsigned int p;
		__builtin_ia32_mfence();
		uint64_t ts = __builtin_ia32_rdtscp(&p);
		__builtin_ia32_lfence();
		return ts;
	}
	
	static inline void ts_init(void) {
		base_freq = cpu_base_freq_mhz();
		
		if(base_freq == 0) {
			fprintf(stderr, "Couldn't determine CPU base frequency, aborting\n");
			abort();
		}
	}
	
	static inline uint64_t ts_diff(uint64_t start, uint64_t end) {
		return end - start;
	}
	
	static inline uint64_t ts_incr(uint64_t ts, uint64_t offset) {
		return ts + offset;
	}
	
	static inline uint64_t ts_ndiv(uint64_t ts, int n) {
		return ts/n;
	}
	
	static inline bool ts_is_zero(uint64_t ts) {
		return (ts == 0);
	}
	
	static inline double ts_elapsed_ticks(uint64_t start, uint64_t end) {
		return (double) end - start;
	}
	
	static inline double ts_elapsed_us(uint64_t start, uint64_t end) {
		return ts_elapsed_ticks(start, end) / base_freq;
	}
	
	static inline double ts_elapsed_ns(uint64_t start, uint64_t end) {
		return ts_elapsed_ticks(start, end) * 1000 / base_freq;
	}
#endif

static inline double ts_elapsed(ts_t start, ts_t end) {
	switch(time_exp) {
		case 6: return ts_elapsed_us(start, end);
		case 9: return ts_elapsed_ns(start, end);
		default: return 0;
	}
}

#endif
