#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
AC_PREREQ([2.60])
AC_INIT([gridftp_hdfs], [2.0])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])

AC_CONFIG_SRCDIR([src/gridftp_hdfs.c])
#AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_LIBTOOL

PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.0],
    [OPENSSL_PKGCONFIG="openssl >= 1.0"])

AC_ARG_WITH(logdir, 
    AC_HELP_STRING([--with-logdir=LOG], [Log directory location]),
    [logdir=${withval}], [logdir=/var/log])

AS_IF([ test "x${JAVA_HOME}" != x ], , [JAVA_HOME=/usr/java/latest])

AC_ARG_WITH([java],
    AC_HELP_STRING([--with-java=JAVA_HOME], [Location of java]),
    [JAVA_HOME=${withval}])
AS_IF([ test -e ${JAVA_HOME}/include ], [JNIHDIR=${JAVA_HOME}/include], )
AC_CHECK_FILE(${JNIHDIR}/jni.h, ,[AC_MSG_ERROR(Can't find jni.h in ${JNIHDIR})])
dnl '
AS_IF([ test -e ${JAVA_HOME}/jre/lib/amd64/server ], LDFLAGS=["$LDFLAGS -L${JAVA_HOME}/jre/lib/amd64 -L${JAVA_HOME}/jre/lib/amd64/server -Wl,-rpath,${JAVA_HOME}/jre/lib/amd64/server"])
AS_IF([ test -e ${JAVA_HOME}/jre/lib/amd64/default ], LDFLAGS=["$LDFLAGS -L${JAVA_HOME}/jre/lib/amd64 -L${JAVA_HOME}/jre/lib/amd64/default -Wl,-rpath,${JAVA_HOME}/jre/lib/amd64/default"])
AS_IF([ test -e ${JAVA_HOME}/jre/lib/i386/server ], LDFLAGS=["$LDFLAGS -L${JAVA_HOME}/jre/lib/i386 -L${JAVA_HOME}/jre/lib/i386/server -Wl,-rpath,${JAVA_HOME}/jre/lib/i386/server"])
AS_IF([ test -e ${JAVA_HOME}/jre/lib/i386/default ], LDFLAGS=["$LDFLAGS -L${JAVA_HOME}/jre/lib/i386 -L${JAVA_HOME}/jre/lib/i386/default -Wl,-rpath,${JAVA_HOME}/jre/lib/i386/default"])
AS_IF([ test -e ${JAVA_HOME}/jre/lib/server ], LDFLAGS=["$LDFLAGS -L${JAVA_HOME}/jre/lib/server -Wl,-rpath,${JAVA_HOME}/jre/lib/server"])
AS_IF([ test -e ${JAVA_HOME}/jre/lib/default ], LDFLAGS=["$LDFLAGS -L${JAVA_HOME}/jre/lib/default -Wl,-rpath,${JAVA_HOME}/jre/lib/default"])


AC_ARG_WITH([hadoop],
    AC_HELP_STRING([--with-hadoop=HADOOP_HOME], [Location of $HADOOP_HOME]),
        [HADOOPHOME=${withval}], [HADOOPHOME=$HADOOP_HOME])
AC_CHECK_FILE(${HADOOPHOME}/src/c++/libhdfs/hdfs.h, INCLUDE=["$INCLUDE -I${HADOOPHOME}/src/c++/libhdfs"],)
AC_CHECK_FILE(${HADOOPHOME}/build/libhdfs/libhdfs.so, LDFLAGS=["$LDFLAGS -L${HADOOPHOME}/build/libhdfs"],)

# EPEL5 defaults
PKG_CHECK_MODULES([GLOBUS_GRIDFTP_SERVER], [globus-gridftp-server >= 11])
PKG_CHECK_MODULES([GLOBUS_COMMON], [globus-common])

# Checks for libraries.
AC_CHECK_LIB([jvm], [JNI_CreateJavaVM], , [AC_MSG_ERROR(Could not find libjvm)])
AC_CHECK_LIB([hdfs], [hdfsRead], , [AC_MSG_ERROR(Could not find libhdfs)])

# Checks for header files.
AC_CHECK_HEADERS([unistd.h], ,[AC_MSG_ERROR(Could not find unistd.h header)])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_MODE_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_HEADER_STDC
AC_FUNC_MMAP
AC_FUNC_STRTOD
AC_CHECK_FUNCS([ftruncate gethostname memset munmap strdup strrchr])

if test "x${prefix}" == "xNONE" ; then
    prefix_resolved=${ac_default_prefix}
else
    eval "prefix_resolved=\"${prefix}\""
fi

if test "x${exec_prefix}" == "xNONE" ; then
    exec_prefix_resolved=${ac_default_prefix}
else
    eval "exec_prefix_resolved=\"${exec_prefix}\""
fi

if test "x${sysconfdir}" == "xNONE/etc" ; then
    sysconfdir_resolved=${ac_default_prefix}/etc
else
    eval "sysconfdir_resolved=\"${sysconfdir}\""
fi

if test "x${sbindir}" == "xNONE" ; then
    sbindir_resolved=${ac_default_prefix}/sbin
else
    eval "sbindir_resolved=\"${sbindir}\""
fi

if test "x${localstatedir}" == "xNONE" ; then
    localstatedir_resolved=${ac_default_prefix}/var
else
    eval "localstatedir_resolved=\"${localstatedir}\""
fi

if test "x${datadir}" == "xNONE" ; then
    datadir_resolved=${ac_default_prefix}/share
else
    eval "datadir_resolved=\"${datadir}\""
fi

AC_SUBST(HADOOPHOME)
AC_SUBST(JNIHDIR)
AC_SUBST(INCLUDE)
AC_SUBST(GLOBUSLOCATION)
AC_SUBST(logdir)
AC_SUBST(exec_prefix_resolved)
AC_SUBST(prefix_resolved)
AC_SUBST(sysconfdir_resolved)
AC_SUBST(datadir_resolved)
AC_SUBST(localstatedir_resolved)
AC_SUBST(sbindir_resolved)

AC_CONFIG_FILES([Makefile src/Makefile conf/Makefile scripts/Makefile conf/gridftp-inetd.conf scripts/xinetd/gridftp-hdfs scripts/xinetd/gridftp-hdfs-inetd scripts/gridftp-hdfs-standalone scripts/init/gridftp-hdfs])
AC_OUTPUT

