#!/bin/awk -f

BEGIN {
	if(length(PREFIX) != 0)
		PRINT_PREFIX = PREFIX " "
}

{
	if(match($1, "([0-9]+):", g))
		printf("%s%d %d %.2f\n", PRINT_PREFIX, int(g[1]), $2, $3)
}
