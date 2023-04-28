CFLAGS = -Wall -Wno-unused-variable -g -march=icelake-server

ifdef SC

SC_FILE=scenarios/$(SC).c
SC_FN := $(SC)

CFLAGS += -Wno-unused-function
CPPFLAGS += -D EXTERNAL_SCENARIO=\"$(SC_FILE)\"
CPPFLAGS += -D EXTERNAL_SCENARIO_FN=$(SC_FN)

endif

# -------------------------

ifndef NO_MSR

all: cache_eng
	@

cache_eng: cache_eng.c utils.c cache_eng.h perfctr.c perfctr.h $(SC_FILE) Makefile
	mpicc $(CFLAGS) $(CFLAGS_EXTRA) $(CPPFLAGS) -o $@ \
		$(filter %.c, $(filter-out $(SC_FILE), $^)) -lrt -lmpi
	sudo setcap cap_sys_rawio=ep "$@"

else

all: cache_eng
	@

cache_eng: cache_eng.c utils.c cache_eng.h perfctr.c perfctr.h $(SC_FILE) Makefile
	mpicc $(CFLAGS) $(CFLAGS_EXTRA) $(CPPFLAGS) -o $@ \
		$(filter %.c, $(filter-out $(SC_FILE), $^)) -lrt -lmpi

endif

# -------------------------

clean:
	rm -f cache_eng
