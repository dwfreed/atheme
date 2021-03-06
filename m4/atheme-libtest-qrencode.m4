AC_DEFUN([ATHEME_LIBTEST_QRENCODE], [

	LIBQRENCODE="No"

	AC_ARG_WITH([qrencode],
		[AS_HELP_STRING([--without-qrencode], [Do not attempt to detect libqrencode (for generating QR codes)])],
		[], [with_qrencode="auto"])

	case "x${with_qrencode}" in
		xno | xyes | xauto)
			;;
		*)
			AC_MSG_ERROR([invalid option for --with-qrencode])
			;;
	esac

	CPPFLAGS_SAVED="${CPPFLAGS}"
	LIBS_SAVED="${LIBS}"

	AS_IF([test "${with_qrencode}" != "no"], [
		PKG_CHECK_MODULES([LIBQRENCODE], [libqrencode], [
			CPPFLAGS="${LIBQRENCODE_CFLAGS} ${CPPFLAGS}"
			LIBS="${LIBQRENCODE_LIBS} ${LIBS}"
			AC_MSG_CHECKING([if libqrencode appears to be usable])
			AC_LINK_IFELSE([
				AC_LANG_PROGRAM([[
					#ifdef HAVE_STDDEF_H
					#  include <stddef.h>
					#endif
					#include <qrencode.h>
				]], [[
					(void) QRcode_encodeData(0, NULL, 0, (QRecLevel) 0);
					(void) QRcode_free(NULL);
				]])
			], [
				AC_MSG_RESULT([yes])
				LIBQRENCODE="Yes"
				AC_DEFINE([HAVE_LIBQRENCODE], [1], [Define to 1 if libqrencode appears to be usable])
				ATHEME_COND_QRCODE_ENABLE
			], [
				AC_MSG_RESULT([no])
				LIBQRENCODE="No"
				AS_IF([test "${with_qrencode}" = "yes"], [
					AC_MSG_FAILURE([--with-qrencode was given but libqrencode does not appear to be usable])
				])
			])
		], [
			LIBQRENCODE="No"
			AS_IF([test "${with_qrencode}" = "yes"], [
				AC_MSG_ERROR([--with-qrencode was given but libqrencode could not be found])
			])
		])
	], [
		LIBQRENCODE="No"
	])

	CPPFLAGS="${CPPFLAGS_SAVED}"
	LIBS="${LIBS_SAVED}"

	AS_IF([test "${LIBQRENCODE}" = "No"], [
		LIBQRENCODE_CFLAGS=""
		LIBQRENCODE_LIBS=""
	])

	AC_SUBST([LIBQRENCODE_CFLAGS])
	AC_SUBST([LIBQRENCODE_LIBS])
])
