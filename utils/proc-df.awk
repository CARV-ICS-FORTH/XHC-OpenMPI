#!/bin/awk -f

{
	if(!match($1, "^[0-9]+$"))
		next
	
	sum[$1] += $2
	sum_sq[$1] += $2^2
	runs[$1]++
}

END {
	PROCINFO["sorted_in"]="@ind_num_asc"
	
	if(length(PREFIX) != 0)
		PRINT_PREFIX = PREFIX " "
	
	for(size in sum) {
		s = sum_sq[size]/runs[size] - (sum[size]/runs[size])^2
		stddev = (s < 0 && s > -1e-06 ? 0 : sqrt(s))
		avg = sum[size]/runs[size]
		
		printf("%s%d %.2f %.2f\n", PRINT_PREFIX, size, avg, stddev)
	}
}
