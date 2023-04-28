#!/bin/awk -f

{
	if(!match($1, "^[0-9]+$"))
		next
	
	lat[$1][length(lat[$1]) + 1] = $2
}

END {
	PROCINFO["sorted_in"]="@ind_num_asc"
	
	if(length(PREFIX) != 0)
		PRINT_PREFIX = PREFIX " "
	
	for(size in lat) {
		for(idx in lat[size])
			printf("%s%d %.2f\n", PRINT_PREFIX, size, lat[size][idx])
	}
}
