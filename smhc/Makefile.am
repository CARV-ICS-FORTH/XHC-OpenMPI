dist_opaldata_DATA = help-coll-smhc.txt

sources = coll_smhc.h coll_smhc_module.c coll_smhc_component.c \
	coll_smhc_bcast_flat.c coll_smhc_bcast_tree.c

# Make the output library in this directory, and name it either
# mca_<type>_<name>.la (for DSO builds) or libmca_<type>_<name>.la
# (for static builds).

component_noinst =
component_install =
if MCA_BUILD_ompi_coll_smhc_DSO
component_install += mca_coll_smhc.la
else
component_noinst += libmca_coll_smhc.la
endif

mcacomponentdir = $(ompilibdir)
mcacomponent_LTLIBRARIES = $(component_install)
mca_coll_smhc_la_SOURCES = $(sources)
mca_coll_smhc_la_LDFLAGS = -module -avoid-version
mca_coll_smhc_la_LIBADD = $(top_builddir)/ompi/lib@OMPI_LIBMPI_NAME@.la

noinst_LTLIBRARIES = $(component_noinst)
libmca_coll_smhc_la_SOURCES =$(sources)
libmca_coll_smhc_la_LDFLAGS = -module -avoid-version
