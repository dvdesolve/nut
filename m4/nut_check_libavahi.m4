dnl Check for LIBAVAHI compiler flags. On success, set nut_have_avahi="yes"
dnl and set LIBAVAHI_CFLAGS and LIBAVAHI_LIBS. On failure, set
dnl nut_have_avahi="no". This macro can be run multiple times, but will
dnl do the checking only once.

AC_DEFUN([NUT_CHECK_LIBAVAHI],
[
if test -z "${nut_have_avahi_seen}"; then
	nut_have_avahi_seen=yes
	AC_REQUIRE([NUT_CHECK_PKGCONFIG])

	dnl save CFLAGS and LIBS
	CFLAGS_ORIG="${CFLAGS}"
	LIBS_ORIG="${LIBS}"
	CFLAGS=""
	LIBS=""
	depCFLAGS=""
	depLIBS=""

	AS_IF([test x"$have_PKG_CONFIG" = xyes],
		[dnl See which version of the avahi library (if any) is installed
		 AC_MSG_CHECKING(for avahi-core version via pkg-config (0.6.30 minimum required))
		 AVAHI_CORE_VERSION="`$PKG_CONFIG --silence-errors --modversion avahi-core 2>/dev/null`"
		 if test "$?" != "0" -o -z "${AVAHI_CORE_VERSION}"; then
		    AVAHI_CORE_VERSION="none"
		 fi
		 AC_MSG_RESULT(${AVAHI_CORE_VERSION} found)

		 AC_MSG_CHECKING(for avahi-client version via pkg-config (0.6.30 minimum required))
		 AVAHI_CLIENT_VERSION="`$PKG_CONFIG --silence-errors --modversion avahi-client 2>/dev/null`"
		 if test "$?" != "0" -o -z "${AVAHI_CLIENT_VERSION}"; then
		    AVAHI_CLIENT_VERSION="none"
		 fi
		 AC_MSG_RESULT(${AVAHI_CLIENT_VERSION} found)
		], [AC_MSG_NOTICE([can not check avahi settings via pkg-config])]
	)

	AC_MSG_CHECKING(for avahi cflags)
	AC_ARG_WITH(avahi-includes,
		AS_HELP_STRING([@<:@--with-avahi-includes=CFLAGS@:>@], [include flags for the avahi library]),
	[
		case "${withval}" in
		yes|no)
			AC_MSG_ERROR(invalid option --with(out)-avahi-includes - see docs/configure.txt)
			;;
		*)
			depCFLAGS="${withval}"
			;;
		esac
	], [
		AS_IF([test x"$have_PKG_CONFIG" = xyes],
			[depCFLAGS="`$PKG_CONFIG --silence-errors --cflags avahi-core avahi-client 2>/dev/null`" \
			 || depCFLAGS="-I/usr/local/include -I/usr/include -L/usr/local/lib -L/usr/lib"],
			[depCFLAGS="-I/usr/local/include -I/usr/include -L/usr/local/lib -L/usr/lib"]
		)]
	)
	AC_MSG_RESULT([${depCFLAGS}])

	AC_MSG_CHECKING(for avahi ldflags)
	AC_ARG_WITH(avahi-libs,
		AS_HELP_STRING([@<:@--with-avahi-libs=LIBS@:>@], [linker flags for the avahi library]),
	[
		case "${withval}" in
		yes|no)
			AC_MSG_ERROR(invalid option --with(out)-avahi-libs - see docs/configure.txt)
			;;
		*)
			depLIBS="${withval}"
			;;
		esac
	], [
		AS_IF([test x"$have_PKG_CONFIG" = xyes],
			[depLIBS="`$PKG_CONFIG --silence-errors --libs avahi-core avahi-client 2>/dev/null`" \
			 || depLIBS="-lavahi-core -lavahi-client"],
			[depLIBS="-lavahi-core -lavahi-client"]
		)]
	)
	AC_MSG_RESULT([${depLIBS}])

	dnl check if avahi-core is usable
	CFLAGS="${CFLAGS_ORIG} ${depCFLAGS}"
	LIBS="${LIBS_ORIG} ${depLIBS}"
	AC_CHECK_HEADERS(avahi-common/malloc.h, [nut_have_avahi=yes], [nut_have_avahi=no], [AC_INCLUDES_DEFAULT])
	AC_CHECK_FUNCS(avahi_free, [], [nut_have_avahi=no])

	if test "${nut_have_avahi}" = "yes"; then
		dnl check if avahi-client is usable
		AC_CHECK_HEADERS(avahi-client/client.h, [nut_have_avahi=yes], [nut_have_avahi=no], [AC_INCLUDES_DEFAULT])
		AC_CHECK_FUNCS(avahi_client_new, [], [nut_have_avahi=no])
		if test "${nut_have_avahi}" = "yes"; then
			LIBAVAHI_CFLAGS="${depCFLAGS}"
			LIBAVAHI_LIBS="${depLIBS}"
		fi

		dnl Help ltdl if we can (nut-scanner etc.)
		for TOKEN in $depLIBS ; do
			AS_CASE(["${TOKEN}"],
				[-l*avahi*client*], [
					AX_REALPATH_LIB([${TOKEN}], [SOPATH_LIBAVAHI], [])
					AS_IF([test -n "${SOPATH_LIBAVAHI}" && test -s "${SOPATH_LIBAVAHI}"], [
						AC_DEFINE_UNQUOTED([SOPATH_LIBAVAHI],["${SOPATH_LIBAVAHI}"],[Path to dynamic library on build system])
						SOFILE_LIBAVAHI="`basename "$SOPATH_LIBAVAHI"`"
						AC_DEFINE_UNQUOTED([SOFILE_LIBAVAHI],["${SOFILE_LIBAVAHI}"],[Base file name of dynamic library on build system])
						break
					])
				]
			)
		done
		unset TOKEN
	fi

	unset depCFLAGS
	unset depLIBS

	dnl restore original CFLAGS and LIBS
	CFLAGS="${CFLAGS_ORIG}"
	LIBS="${LIBS_ORIG}"
fi
])
