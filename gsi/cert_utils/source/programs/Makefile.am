man_MANS = grid-cert-info.1 \
        grid-cert-request.1 \
        grid-change-pass-phrase.1 \
        grid-default-ca.8 \
        globus-update-certificate-dir.8

bin_SCRIPTS = $(User_Scripts)
sbin_SCRIPTS = $(Admin_Scripts)

MANUALS_SOURCE = \
        grid-cert-info.txt \
        grid-cert-request.txt \
        grid-change-pass-phrase.txt \
        grid-default-ca.txt \
        globus-update-certificate-dir.txt

User_Scripts = \
	grid-cert-info \
	grid-change-pass-phrase \
	grid-cert-request
Admin_Scripts = \
	grid-default-ca \
	globus-update-certificate-dir

EXTRA_DIST = $(MANUALS_SOURCE) $(man_MANS)

if BUILD_MANPAGES
SUFFIXES = .txt .1 .8

.txt.8 .txt.1:
	$(A2X) -d manpage -f manpage $<
endif
