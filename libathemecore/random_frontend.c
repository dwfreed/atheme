/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2019 Aaron M. D. Jones <aaronmdjones@gmail.com>
 *
 * Frontend routines for the random interface.
 */

#include <atheme.h>
#include "internal.h"

#ifndef MINIMUM
#  define MINIMUM(a, b) (((a) < (b)) ? (a) : (b))
#endif

#define ATHEME_LAC_RANDOM_FRONTEND_C    1
#define RANDSTR_ALPHABET_LEN            62U
#define RANDSTR_REQUEST_LEN             128U

#if (ATHEME_API_RANDOM_FRONTEND == ATHEME_API_RANDOM_FRONTEND_INTERNAL)
#  include "random_fe_internal.c"
#else
#  if (ATHEME_API_RANDOM_FRONTEND == ATHEME_API_RANDOM_FRONTEND_OPENBSD)
#    include "random_fe_openbsd.c"
#  else
#    if (ATHEME_API_RANDOM_FRONTEND == ATHEME_API_RANDOM_FRONTEND_SODIUM)
#      include "random_fe_sodium.c"
#    else
#      if (ATHEME_API_RANDOM_FRONTEND == ATHEME_API_RANDOM_FRONTEND_MBEDTLS)
#        include "random_fe_mbedtls.c"
#      else
#        if (ATHEME_API_RANDOM_FRONTEND == ATHEME_API_RANDOM_FRONTEND_OPENSSL)
#          include "random_fe_openssl.c"
#        else
#          error "No RNG API frontend was selected by the build system"
#        endif
#      endif
#    endif
#  endif
#endif

/* Note that this function generates a random printable string of length "len", and so it
 * actually requires a buffer of at least "len + 1" bytes, to write a terminating NULL
 * byte too. Thus, DO NOT use the size of the buffer as an argument to this function.    -- amdj
 */
void
atheme_random_str(char *const restrict buf, const size_t len)
{
	return_if_fail(buf != NULL);
	return_if_fail(len > 0);

	/* We request data from the RNG in small chunks, instead of all at once, because
	 * we may be asked to generate a random string longer than the maximum length of
	 * one request for random data, depending on the RNG frontend in use.    -- amdj
	 */
	for (size_t written = 0; written < len; /* No action */)
	{
		unsigned char tmp[RANDSTR_REQUEST_LEN];

		const size_t req = MINIMUM((len - written), RANDSTR_REQUEST_LEN);

		(void) atheme_random_buf(tmp, req);

		static const char alphabet[RANDSTR_ALPHABET_LEN] =
		    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

		for (size_t i = 0; i < req; /* No action */)
			buf[written++] = alphabet[tmp[i++] % RANDSTR_ALPHABET_LEN];
	}

	buf[len + 1] = 0x00;
}
