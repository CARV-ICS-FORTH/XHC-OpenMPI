#!/bin/awk -f

function basename(file) {
	sub(".*/", "", file)
	return file
}

BEGIN {
	desc["titan_sc_s_l.txt"] = "local read"
	desc["titan_sc_s_r.txt"] = "remote read"
	desc["titan_sc_s_lr.txt"] = "remote read, after local read"
	desc["titan_sc_s_rl.txt"] = "local read, after remote read"
	
	for(d in desc)
		max_desc_len = (length(desc[d]) > max_desc_len ? length(desc[d]) : max_desc_len)
}

/([1-9][0-9]*): memcpy_w_time/{
	name = basename(FILENAME)
	
	printf("%-*s: %.0f ns\n", max_desc_len+1, (name in desc ? desc[name] : name), $3)
}
