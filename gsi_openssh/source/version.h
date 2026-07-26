/* $OpenBSD: version.h,v 1.108 2026/04/02 07:51:12 djm Exp $ */

#define SSH_VERSION	"OpenSSH_10.3"

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
#define SSH_HPN		"_hpn18.9.0"
#define SSH_RELEASE	SSH_VERSION SSH_PORTABLE GSI_PORTABLE SSH_HPN \
			GSI_VERSION KRB5_VERSION
