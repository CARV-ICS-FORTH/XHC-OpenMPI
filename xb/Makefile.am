sources = \
	coll_xb.h \
	coll_xb_component.c \
	coll_xb_module.c \
	coll_xb_barrier.c

# Make the output library in this directory, and name it either
# mca_<type>_<name>.la (for DSO builds) or libmca_<type>_<name>.la
# (for static builds).

component_noinst =
component_install =
if MCA_BUILD_ompi_coll_xb_DSO
component_install += mca_coll_xb.la
else
component_noinst += libmca_coll_xb.la
endif

mcacomponentdir = $(ompilibdir)
mcacomponent_LTLIBRARIES = $(component_install)
mca_coll_xb_la_SOURCES = $(sources)
mca_coll_xb_la_LDFLAGS = -module -avoid-version
mca_coll_xb_la_LIBADD = $(top_builddir)/ompi/lib@OMPI_LIBMPI_NAME@.la

noinst_LTLIBRARIES = $(component_noinst)
libmca_coll_xb_la_SOURCES = $(sources)
libmca_coll_xb_la_LDFLAGS = -module -avoid-version
