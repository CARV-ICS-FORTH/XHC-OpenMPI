AM_CPPFLAGS = $(coll_xbrc_CPPFLAGS)

sources = \
	coll_xbrc.h \
	coll_xbrc.c \
	coll_xbrc_component.c \
	coll_xbrc_module.c \
	coll_xbrc_allreduce.c

# Make the output library in this directory, and name it either
# mca_<type>_<name>.la (for DSO builds) or libmca_<type>_<name>.la
# (for static builds).

component_noinst =
component_install =
if MCA_BUILD_ompi_coll_xbrc_DSO
component_install += mca_coll_xbrc.la
else
component_noinst += libmca_coll_xbrc.la
endif

mcacomponentdir = $(ompilibdir)
mcacomponent_LTLIBRARIES = $(component_install)
mca_coll_xbrc_la_SOURCES = $(sources)
mca_coll_xbrc_la_LDFLAGS = -module -avoid-version $(coll_xbrc_LDFLAGS)
mca_coll_xbrc_la_LIBADD = $(top_builddir)/ompi/lib@OMPI_LIBMPI_NAME@.la $(coll_xbrc_LIBS)

noinst_LTLIBRARIES = $(component_noinst)
libmca_coll_xbrc_la_SOURCES = $(sources)
libmca_coll_xbrc_la_LIBADD = $(coll_xbrc_LIBS)
libmca_coll_xbrc_la_LDFLAGS = -module -avoid-version $(coll_xbrc_LDFLAGS)
