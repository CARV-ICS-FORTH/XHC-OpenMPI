#!/bin/awk -f

function basename(file) {
	sub(".*/", "", file)
	return file
}

BEGIN {
	desc["sc_s_l"] = "local read"
	desc["sc_s_r"] = "remote read"
	desc["sc_s_lr"] = "remote read, after local read"
	desc["sc_s_rl"] = "local read, after remote read"
	
	for(d in desc)
		max_desc_len = (length(desc[d]) > max_desc_len ? length(desc[d]) : max_desc_len)
}

/([1-9][0-9]*): memcpy_w_time/{
	name = basename(FILENAME)
	
	if(match(name, ".*?_(sc_s_.*?)\\.txt", g))
		short_name = g[1]
	
	printf("%-*s: %.0f ns\n", max_desc_len+1,
		(short_name in desc ? desc[short_name] : name), $3)
}
