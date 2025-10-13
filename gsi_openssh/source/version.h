/* $OpenBSD: version.h,v 1.103 2024/09/19 22:17:44 djm Exp $ */

#define SSH_VERSION	"OpenSSH_9.9"

#ifdef GSI
#define GSI_VERSION	" GSI"
#else
#define GSI_VERSION	""
#endif

#ifdef KRB5
#define KRB5_VERSION	" KRB5"
#else
#define KRB5_VERSION	""
#endif

#define SSH_PORTABLE	"p1"
#define GSI_PORTABLE	"c-GSI"
#define SSH_HPN		"_hpn18.6.0"
#define SSH_RELEASE	SSH_VERSION SSH_PORTABLE GSI_PORTABLE SSH_HPN \
			GSI_VERSION KRB5_VERSION
