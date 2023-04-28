#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "cache_eng.h"
#include "perfctr.h"

const char *msm_kind_str[MSM_COUNT] = {
	[MSM_ACK] = "ack",
	[MSM_JOIN] = "join",
	[MSM_MAIN] = "main",
	[MSM_MEMCPY] = "memcpy",
	[MSM_MEMCPY_W] = "memcpy_w"
};

void msm_init(msm_line_t *msm_lines, int nlines, int npoints_hint) {
	for(int i = 0; i < nlines; i++) {
		msm_line_t *msm = &msm_lines[i];
		
		msm->points = calloc((msm->size = npoints_hint), sizeof(msm_point_t));
		assert(msm->points);
		
		msm->len = 0;
	}
}

void msm_dealloc(msm_line_t *msm_lines, int nlines) {
	for(int i = 0; i < nlines; i++)
		free(msm_lines[i].points);
}

void msm_insert(msm_line_t *msm_lines, msm_kind_enum_t kind,
		double timestamp, double time_spent) {
	
	msm_line_t *msm = &msm_lines[kind];
	
	if(msm->len == msm->size) {
		msm->points = realloc(msm->points, (msm->size *= 2) * sizeof(msm_point_t));
		assert(msm->points);
	}
	
	msm->points[msm->len++] = (msm_point_t) {
		.ts = timestamp,
		.time = time_spent
	};
}

void msm_add(msm_line_t *msm_lines, msm_kind_enum_t kind, msm_point_t *points,
		size_t npoints, double ts_offset, double time_offset) {
	
	msm_line_t *msm = &msm_lines[kind];
	
	if(msm->size < npoints) {
		msm->points = realloc(msm->points, (msm->size *= 2) * sizeof(msm_point_t));
		assert(msm->points);
	}
	
	for(int i = 0; i < npoints; i++) {
		if(i >= msm->len)
			msm->points[i] = (msm_point_t) {0};
		
		msm->points[i].ts += points[i].ts + ts_offset;
		msm->points[i].time += points[i].time + time_offset;
	}
	
	if(npoints > msm->len)
		msm->len = npoints;
}

void msm_string(msm_line_t *msm_lines, msm_kind_enum_t kind,
		int iterations, char **ts_str, char **time_str) {
	
	msm_line_t *msm = &msm_lines[kind];
	
	size_t pbuf_size = 16384;
	size_t pbuf_idx = 0;
	char *pbuf;
	
	pbuf = calloc(pbuf_size, sizeof(char));
	
	pbuf_idx += snprintf(pbuf+pbuf_idx,
		pbuf_size-pbuf_idx, "%s_ts ", msm_kind_str[kind]);
	
	for(int i = 0; i < msm->len; i++) {
		pbuf_idx += snprintf(pbuf+pbuf_idx, pbuf_size-pbuf_idx,
			"%" TS_PRINT_MOD " ", msm->points[i].ts/iterations);
	}
	
	*ts_str = pbuf;
	
	pbuf = calloc(pbuf_size, sizeof(char));
	pbuf_idx = 0;
	
	pbuf_idx += snprintf(pbuf+pbuf_idx,
		pbuf_size-pbuf_idx, "%s_time ", msm_kind_str[kind]);
	
	for(int i = 0; i < msm->len; i++) {
		pbuf_idx += snprintf(pbuf+pbuf_idx, pbuf_size-pbuf_idx,
			"%" TS_PRINT_MOD " ", msm->points[i].time/iterations);
	}
	
	*time_str = pbuf;
}

// --------------------------------------

void perfctr_report_core(uint64_t initial_values[NUM_CORES][NUM_CORE_COUNTERS],
		const char **event_names, int iterations) {
	
	uint64_t system_sum[NUM_CORE_COUNTERS] = {0};
	uint64_t socket_sum[NUM_SOCKETS][NUM_CORE_COUNTERS] = {{0}};
	
	for(int ctr = 0; ctr < NUM_CORE_COUNTERS; ctr++) {
		for(int c = 0; c < NUM_CORES; c++) {
			uint64_t ctr_now = msr_read(c, MSR_CORE_REG(PMC, ctr));
			uint64_t ctr_avg = (ctr_now - initial_values[c][ctr])/iterations;
				
			if(ctr_avg == 0) continue;
				
			if(per_box_report) {
				if(event_names[ctr])
					printf("CORE %d %s %lu\n",
						c, event_names[ctr], ctr_avg);
				else
					printf("CORE %d CTR_%d %lu\n",
						c, ctr, ctr_avg);
			}
			
			system_sum[ctr] += ctr_avg;
			socket_sum[c / NUM_CORES_PER_SOCKET][ctr] += ctr_avg;
		}
	}
	
	if(per_box_report)
		printf("====================\n");
	
	if(sum_per_socket) {
		for(int s = 0; s < NUM_SOCKETS; s++) {
			for(int ctr = 0; ctr < NUM_CORE_COUNTERS; ctr++) {
				if(socket_sum[s][ctr] == 0) continue;
				
				if(event_names[ctr])
					printf("SOCKET %d ALL CORE %s %lu\n",
						s, event_names[ctr], socket_sum[s][ctr]);
				else
					printf("SOCKET %d ALL CORE CTR_%d %lu\n",
						s, ctr, socket_sum[s][ctr]);
			}
		}
	} else {
		for(int ctr = 0; ctr < NUM_CORE_COUNTERS; ctr++) {
			if(system_sum[ctr] == 0) continue;
			
			if(event_names[ctr])
				printf("ALL CORE %s %lu\n",
					event_names[ctr], system_sum[ctr]);
			else
				printf("ALL CORE CTR_%d %lu\n",
					ctr, system_sum[ctr]);
		}
	}
}

void perfctr_report_cha(uint64_t initial_values[NUM_SOCKETS][NUM_CHA_BOXES][NUM_CHA_COUNTERS],
		const char **event_names, int iterations) {
	
	uint64_t system_cha_sum[NUM_CHA_COUNTERS] = {0};
	uint64_t socket_cha_sum[NUM_SOCKETS][NUM_CHA_COUNTERS] = {{0}};
	
	for(int ctr = 0; ctr < NUM_CHA_COUNTERS; ctr++) {
		for(int s = 0; s < NUM_SOCKETS; s++) {
			for(int cha = 0; cha < NUM_CHA_BOXES; cha++) {
				uint64_t ctr_now = msr_read(SOCKET_CPU(s),
					MSR_CHA_REG(cha, CTR, ctr));
				
				uint64_t ctr_avg = (ctr_now - initial_values[s][cha][ctr])/iterations;
				
				if(ctr_avg == 0) continue;
				
				if(per_box_report) {
					if(event_names[ctr]) {
						printf("CHA %d:%02d %s %lu\n",
							s, cha, event_names[ctr], ctr_avg);
					} else {
						printf("CHA %d:%02d CTR_%d %lu\n",
								s, cha, ctr, ctr_avg);
					}
				}
				
				system_cha_sum[ctr] += ctr_avg;
				socket_cha_sum[s][ctr] += ctr_avg;
			}
		}
	}
	
	if(per_box_report)
		printf("====================\n");
	
	if(sum_per_socket) {
		for(int s = 0; s < NUM_SOCKETS; s++) {
			for(int ctr = 0; ctr < NUM_CHA_COUNTERS; ctr++) {
				if(socket_cha_sum[s][ctr] == 0) continue;
				
				if(event_names[ctr])
					printf("SOCKET %d ALL CHA %s %lu\n",
						s, event_names[ctr], socket_cha_sum[s][ctr]);
				else
					printf("SOCKET %d ALL CHA CTR_%d %lu\n",
						s, ctr, socket_cha_sum[s][ctr]);
			}
		}
	} else {
		for(int ctr = 0; ctr < NUM_CHA_COUNTERS; ctr++) {
			if(system_cha_sum[ctr] == 0) continue;
			
			if(event_names[ctr])
				printf("ALL CHA %s %lu\n",
					event_names[ctr], system_cha_sum[ctr]);
			else
				printf("ALL CHA CTR_%d %lu\n",
					ctr, system_cha_sum[ctr]);
		}
	}
}

void perfctr_report_m2m(uint64_t initial_values[NUM_SOCKETS][NUM_M2M_BOXES][NUM_M2M_COUNTERS],
		const char **event_names, int iterations) {
	
	uint64_t system_sum[NUM_M2M_COUNTERS] = {0};
	uint64_t socket_sum[NUM_SOCKETS][NUM_M2M_COUNTERS] = {{0}};
	
	for(int ctr = 0; ctr < NUM_M2M_COUNTERS; ctr++) {
		for(int s = 0; s < NUM_SOCKETS; s++) {
			for(int m = 0; m < NUM_M2M_BOXES; m++) {
				uint bus = PCI_PMON_bus[s];
				uint device = PCI_PMON_M2M_device[m];
				uint function = PCI_PMON_M2M_function[m];
				
				uint64_t ctr_now = pci_cfg_r64(bus, device,
					function, PCI_M2M_REG(CTR, ctr));
				
				uint64_t ctr_avg = (ctr_now - initial_values[s][m][ctr])/iterations;
				
				if(ctr_avg == 0) continue;
				
				if(per_box_report) {
					if(event_names[ctr]) {
						printf("M2M %d:%d %s %lu\n",
							s, m, event_names[ctr], ctr_avg);
					} else {
						printf("M2M %d:%d CTR_%d %lu\n",
								s, m, ctr, ctr_avg);
					}
				}
				
				system_sum[ctr] += ctr_avg;
				socket_sum[s][ctr] += ctr_avg;
			}
		}
	}
	
	if(per_box_report)
		printf("====================\n");
	
	if(sum_per_socket) {
		for(int s = 0; s < NUM_SOCKETS; s++) {
			for(int ctr = 0; ctr < NUM_M2M_COUNTERS; ctr++) {
				if(socket_sum[s][ctr] == 0) continue;
				
				if(event_names[ctr])
					printf("SOCKET %d ALL M2M %s %lu\n",
						s, event_names[ctr], socket_sum[s][ctr]);
				else
					printf("SOCKET %d ALL M2M CTR_%d %lu\n",
						s, ctr, socket_sum[s][ctr]);
			}
		}
	} else {
		for(int ctr = 0; ctr < NUM_M2M_COUNTERS; ctr++) {
			if(system_sum[ctr] == 0) continue;
			
			if(event_names[ctr])
				printf("ALL M2M %s %lu\n",
					event_names[ctr], system_sum[ctr]);
			else
				printf("ALL M2M CTR_%d %lu\n",
					ctr, system_sum[ctr]);
		}
	}
}

void perfctr_report_imc(uint64_t initial_values[NUM_SOCKETS][NUM_IMC_BOXES][NUM_IMC_CHANNELS][NUM_IMC_COUNTERS],
		const char **event_names, int iterations) {
	
	uint64_t system_sum[NUM_IMC_COUNTERS] = {0};
	uint64_t socket_sum[NUM_SOCKETS][NUM_IMC_COUNTERS] = {{0}};
	
	for(int ctr = 0; ctr < NUM_IMC_COUNTERS; ctr++) {
		for(int s = 0; s < NUM_SOCKETS; s++) {
			for(int imc = 0; imc < NUM_IMC_BOXES; imc++) {
				for(int chn = 0; chn < NUM_IMC_CHANNELS; chn++) {
					uint64_t ctr_now = pci_imc_r64(s, imc,
						PCI_IMC_REG_CTR(chn, ctr));
					
					uint64_t ctr_avg = (ctr_now -
						initial_values[s][imc][chn][ctr])/iterations;
					
					if(ctr_avg == 0) continue;
					
					if(per_box_report) {
						if(event_names[ctr]) {
							printf("IMC %d:%d:%d %s %lu\n",
								s, imc, chn, event_names[ctr], ctr_avg);
						} else {
							printf("IMC %d:%d:%d CTR_%d %lu\n",
									s, imc, chn, ctr, ctr_avg);
						}
					}
					
					system_sum[ctr] += ctr_avg;
					socket_sum[s][ctr] += ctr_avg;
				}
			}
		}
	}
	
	if(per_box_report)
		printf("====================\n");
	
	if(sum_per_socket) {
		for(int s = 0; s < NUM_SOCKETS; s++) {
			for(int ctr = 0; ctr < NUM_IMC_COUNTERS; ctr++) {
				if(socket_sum[s][ctr] == 0) continue;
				
				if(event_names[ctr])
					printf("SOCKET %d ALL IMC %s %lu\n",
						s, event_names[ctr], socket_sum[s][ctr]);
				else
					printf("SOCKET %d ALL IMC CTR_%d %lu\n",
						s, ctr, socket_sum[s][ctr]);
			}
		}
	} else {
		for(int ctr = 0; ctr < NUM_IMC_COUNTERS; ctr++) {
			if(system_sum[ctr] == 0) continue;
			
			if(event_names[ctr])
				printf("ALL IMC %s %lu\n",
					event_names[ctr], system_sum[ctr]);
			else
				printf("ALL IMC CTR_%d %lu\n",
					ctr, system_sum[ctr]);
		}
	}
}
