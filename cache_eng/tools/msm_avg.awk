#!/bin/awk -f

BEGIN {
	split("ack_ts ack_time join_ts memcpy_ts memcpy_time \
		memcpy_w_ts memcpy_w_time main_ts", keys_assoc)
	
	for(i in keys_assoc)
		keys[keys_assoc[i]] = 1
}

{
	if($2 in keys) {
		rank = substr($1, 0, length($1)-1)
		
		for(i=3; i<=NF; i++) {
			sum[rank][$2][i-3] += $i
			cnt[rank][$2][i-3]++
		}
	}
}

END {
	PROCINFO["sorted_in"]="@ind_num_asc"
	
	round = ("ROUND" in ENVIRON ? ENVIRON["ROUND"] : 2)
	
	for(rank in sum) {
		for(key in sum[rank]) {
			printf("%s: %s ", rank, key)
			
			for(n in sum[rank][key]) {
				avg = sum[rank][key][n]/cnt[rank][key][n]
				printf("%.*f ", round, avg)
			}
			
			printf("\n")
		}
	}
}
