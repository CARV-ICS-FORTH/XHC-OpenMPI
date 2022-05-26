AC_DEFUN([MCA_ompi_coll_xbrc_CONFIG],[
	AC_CONFIG_FILES([ompi/mca/coll/xbrc/Makefile])
	
	OPAL_CHECK_XPMEM([coll_xbrc], [$1], [$2])
	
	AC_SUBST([coll_xbrc_CPPFLAGS])
	AC_SUBST([coll_xbrc_LDFLAGS])
	AC_SUBST([coll_xbrc_LIBS])
])dnl
