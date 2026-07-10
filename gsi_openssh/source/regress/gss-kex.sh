tid="GSS-API Key Exchange"

# Skip the test if GSSAPI support is not configured
if ! grep -E '^#define GSSAPI' "$BUILDDIR/config.h" >/dev/null 2>&1; then
    skip "GSSAPI not enabled"
fi

# We test with MIT Kerberos KDC, skip if not installed
if ! which krb5kdc >/dev/null 2>&1; then
    skip "MIT Kerberos KDC not installed"
fi

# The test needs nss_wrapper to emulate gethostname() and /etc/hosts,
# we skip if the shared library is not installed
nss_wrapper="libnss_wrapper.so"
if ! ldconfig -p | grep "$nss_wrapper" >/dev/null 2>&1; then
    skip "$nss_wrapper not installed"
fi

# Set up the username of the SSH client
client="$LOGNAME"
if [ "x$client" = "x" ]; then
	client="$(whoami)"
fi

# Set up SSHD and KDC hostnames and resolve both to localhost
sshd_hostname="sshd.example.org"
kdc_hostname="kdc.example.org"
kdc_port=2088
hosts="$OBJ/hosts"
echo "127.0.0.1 $sshd_hostname $kdc_hostname" > "$hosts"

# Set up a directory to store Kerberos data
gssdir="$OBJ/gss"
mkdir -p "$gssdir"
export KRB5CCNAME="$gssdir/cc"
export KRB5_CONFIG="$gssdir/krb5.conf"
export KRB5_KDC_PROFILE="$gssdir/kdc.conf"
export KRB5_KTNAME="$gssdir/ssh.keytab"
export KRB5RCACHETYPE="none"
kdc_pidfile="$gssdir/pid"

# Configure Kerberos
cat<<EOF > "$KRB5_KDC_PROFILE"
[realms]
    EXAMPLE.ORG = {
        database_name = $gssdir/principal
        key_stash_file = $gssdir/stash
        kdc_listen = $kdc_hostname:$kdc_port
        kdc_tcp_listen = $kdc_hostname:$kdc_port
    }
[logging]
    kdc = FILE:$gssdir/kdc.log
    debug = true
EOF

cat<<EOF > "$KRB5_CONFIG"
[libdefaults]
    default_realm = EXAMPLE.ORG
[realms]
    EXAMPLE.ORG = {
        kdc = $kdc_hostname:$kdc_port
    }
EOF

# Back up the default sshd_config
cp "$OBJ/sshd_config" "$OBJ/sshd_config.orig"

setup_sshd() {
    kex_alg="$1"

    cp "$OBJ/sshd_config.orig" "$OBJ/sshd_config"

    cat<<EOF >> "$OBJ/sshd_config"
PubkeyAuthentication no
PasswordAuthentication no
GSSAPIAuthentication yes
GSSAPIKeyExchange yes
GSSAPIKexAlgorithms $kex_alg
GSSAPIStrictAcceptorCheck no
EOF

    test_ssh_sshd_env_backup="$TEST_SSH_SSHD_ENV"
    TEST_SSH_SSHD_ENV="$TEST_SSH_SSHD_ENV                  \
                       LD_PRELOAD=$nss_wrapper             \
                       NSS_WRAPPER_HOSTS=$hosts            \
                       NSS_WRAPPER_HOSTNAME=$sshd_hostname \
                       KRB5_CONFIG=$KRB5_CONFIG            \
                       KRB5_KDC_PROFILE=$KRB5_KDC_PROFILE  \
                       KRB5CCNAME=$KRB5CCNAME              \
                       KRB5_KTNAME=$KRB5_KTNAME            \
                       KRB5RCACHETYPE=$KRB5RCACHETYPE"
    start_sshd
}

teardown_sshd() {
    TEST_SSH_SSHD_ENV="$test_ssh_sshd_env_backup"
    stop_sshd
}

setup_kdc() {
    kdb5_util create -P "foo" -s
    krb5kdc -w 1 -P "$kdc_pidfile"
    i=0;
    while [ ! -f "$kdc_pidfile" -a $i -lt 10 ]; do
        i=$((i + 1))
        sleep 1
    done
    test -f "$kdc_pidfile" || fatal "KDC failed to start"
}

teardown_kdc() {
    kill "$(cat "$kdc_pidfile")"
    kdestroy
    rm -f "$KRB5_KTNAME" "$kdc_pidfile"
    kdb5_util destroy -f
}

setup_nss_emulation() {
    export LD_PRELOAD="$nss_wrapper"
    export NSS_WRAPPER_HOSTS="$hosts"
}

teardown_nss_emulation() {
    unset LD_PRELOAD
    unset NSS_WRAPPER_HOSTS
}

# GSS kex family prefixes to test.
# GSSAPIKexAlgorithms accepts these prefixes; the full algorithm names
# (prefix + Base64(MD5(OID))) are constructed during negotiation.
gss_kex_families="
    gss-group14-sha256-
    gss-group16-sha512-
    gss-nistp256-sha256-
    gss-curve25519-sha256-
    gss-group14-sha1-
    gss-gex-sha1-
"

# Positive tests: each GSS kex algorithm connects successfully
for kex_family in $gss_kex_families; do
    # Check if the algorithm family is recognized by ssh
    if ! ${SSH} -G -F "$OBJ/ssh_config" \
        -o "GSSAPIKeyExchange yes" \
        -o "GSSAPIKexAlgorithms $kex_family" \
        "$client@$sshd_hostname" >/dev/null 2>&1; then
        verbose "gss kex $kex_family not supported, skipping"
        continue
    fi

    verbose "gss kex $kex_family"

    setup_nss_emulation
    setup_kdc

    kadmin.local add_principal -randkey "host/$sshd_hostname"
    kadmin.local ktadd "host/$sshd_hostname"
    kadmin.local add_principal -pw "foo" "$client"
    echo "foo" | kinit "$client"

    setup_sshd "$kex_family"

    ${SSH} -F "$OBJ/ssh_config" \
        -o "GSSAPIAuthentication yes" \
        -o "GSSAPIKeyExchange yes" \
        -o "GSSAPIKexAlgorithms $kex_family" \
        -o "GSSAPIDelegateCredentials no" \
        "$client@$sshd_hostname" true
    status=$?

    teardown_sshd
    teardown_kdc
    teardown_nss_emulation

    if [ $status -ne 0 ]; then
        fail "gss kex $kex_family failed"
    fi
done

# Negative test: connection must fail when keytab is missing
verbose "gss kex negative test: no keytab"
kex_neg="gss-group14-sha256-"

if ${SSH} -G -F "$OBJ/ssh_config" \
    -o "GSSAPIKeyExchange yes" \
    -o "GSSAPIKexAlgorithms $kex_neg" \
    "$client@$sshd_hostname" >/dev/null 2>&1; then

    setup_nss_emulation
    setup_kdc

    # Create host principal but do NOT export to keytab
    kadmin.local add_principal -randkey "host/$sshd_hostname"
    kadmin.local add_principal -pw "foo" "$client"
    echo "foo" | kinit "$client"

    setup_sshd "$kex_neg"

    ${SSH} -F "$OBJ/ssh_config" \
        -o "GSSAPIAuthentication yes" \
        -o "GSSAPIKeyExchange yes" \
        -o "GSSAPIKexAlgorithms $kex_neg" \
        -o "GSSAPIDelegateCredentials no" \
        "$client@$sshd_hostname" true
    status=$?

    teardown_sshd
    teardown_kdc
    teardown_nss_emulation

    if [ $status -eq 0 ]; then
        fail "gss kex succeeded without keytab"
    fi
fi

unset KRB5CCNAME
unset KRB5_CONFIG
unset KRB5_KDC_PROFILE
unset KRB5_KTNAME
unset KRB5RCACHETYPE
rm -rf "$gssdir"
