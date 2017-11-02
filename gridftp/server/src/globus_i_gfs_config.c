/*
 * Copyright 1999-2006 University of Chicago
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "globus_i_gridftp_server.h"
#include "globus_gsi_system_config.h"
#include "version.h"
#include "dirent.h"
#ifndef TARGET_ARCH_WIN32
#include <grp.h>
#endif

#ifdef TARGET_ARCH_WIN32
#define S_ISLNK(x) 0
#define lstat(x,y) stat(x,y)
#define mkdir(x,y) mkdir(x)
#define chown(x,y,z) -1
#define symlink(x,y) -1
#define readlink(x,y,z) 0
#define realpath(x,y) strcpy(y,x)
#define scandir(a,b,c,d) 0
#define alphasort(x,y) 0
#define setenv(x,y,z) SetEnvironmentVariable(x,y)
#endif

#ifdef TARGET_ARCH_WIN32

#define lstat(x,y) stat(x,y)
#define S_ISLNK(x) 0

#define getuid() 1
#define getpwuid(x) 0
#define initgroups(x,y) -1
#define getgroups(x,y) -1
#define setgroups(x,y) 0
#define setgid(x) 0
#define setuid(x) 0
#define sync() 0
#define fork() -1
#define setsid() -1
#define chroot(x) -1
#define globus_libc_getpwnam_r(a,b,c,d,e) -1
#define globus_libc_getpwuid_r(a,b,c,d,e) -1
#endif

#ifdef TARGET_ARCH_WIN32

#define getpwnam(x) 0

#define getgrgid(x) 0
#define getgrnam(x) 0

#define lstat(x,y) stat(x,y)
#define S_ISLNK(x) 0

#endif

#define GLOBUS_GFS_HELP_ROWS            60
#define GLOBUS_GFS_HELP_COLS            45
#define GLOBUS_GFS_HELP_WIDTH           80

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef enum
{
    GLOBUS_L_GFS_CONFIG_BOOL,
    GLOBUS_L_GFS_CONFIG_INT,
    GLOBUS_L_GFS_CONFIG_STRING,
    GLOBUS_L_GFS_CONFIG_LIST,
    GLOBUS_L_GFS_CONFIG_VOID
} globus_l_gfs_config_type_t;

typedef struct
{
    char *                              option_name;
    char *                              configfile_option;
    char *                              env_var_option;
    char *                              long_cmdline_option;
    char *                              short_cmdline_option;
    globus_l_gfs_config_type_t          type;
    int                                 int_value;
    void *                              value;
    char *                              usage;
    char *                              short_usage;
    char *                              expected_val;
    globus_bool_t                       public;
    globus_list_t *                     set_list;
} globus_l_gfs_config_option_t;

static globus_mutex_t                   globus_i_gfs_config_mutex;

static const globus_l_gfs_config_option_t option_list[] = 
{ 
{NULL, "Informational Options", NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL, GLOBUS_FALSE, NULL},
 {"help", "help", NULL, "help", "h", GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Show usage information and exit.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"longhelp", "longhelp", NULL, "longhelp", "hh", GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Show more usage information and exit.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"version", "version", NULL, "version", "v", GLOBUS_L_GFS_CONFIG_BOOL, 0, NULL,
    "Show version information for the server and exit.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"versions", "versions", NULL, "versions", "V", GLOBUS_L_GFS_CONFIG_BOOL, 0, NULL,
    "Show version information for all loaded globus libraries and exit.", NULL, NULL,GLOBUS_FALSE, NULL},
{NULL, "Modes of Operation", NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL,GLOBUS_FALSE, NULL},
 {"inetd", "inetd", NULL, "inetd", "i", GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Run under an inetd service.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"daemon", "daemon", NULL, "daemon", "s", GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_TRUE, NULL,
    "Run as a daemon.  All connections will fork off a new process and setuid if allowed.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"detach", "detach", NULL, "detach", "S", GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Run as a background daemon detached from any controlling terminals.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"ssh", "ssh", NULL, "ssh", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Run over a connected ssh session.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"exec", "exec", NULL, "exec", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "For statically compiled or non-GLOBUS_LOCATION standard binary locations, specify the full "
    "path of the server binary here.  Only needed when run in daemon mode.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"chdir", "chdir", NULL, "chdir", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_TRUE, NULL,
    "Change directory when the server starts. This will change directory to the dir specified "
    "by the chdir_to option.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"chdir_to", "chdir_to", NULL, "chdir-to", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Directory to chdir to after starting.  Will use / if not set.  Note that this is the "
    "directory of the process, not the client's home directory.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"threads", "threads", NULL, "threads", NULL, GLOBUS_L_GFS_CONFIG_INT, 0, NULL,
    "Enable threaded operation and set the number of threads.  The default is 0, which " 
    "is non-threaded.  When threading is required, a thread count of 1 or 2 should "
    "be sufficient.", NULL, NULL, GLOBUS_TRUE, NULL},
 {"fork", "fork", NULL, "fork", "f", GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_TRUE, NULL,
    "Server will fork for each new connection.  Disabling this option is only recommended "
    "when debugging. Note that non-forked servers running as 'root' will only "
    "accept a single connection, and then exit.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"fork_fallback", "fork_fallback", NULL, "fork-fallback", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    NULL /* attempt to run non-forked if fork fails */, NULL, NULL, GLOBUS_FALSE, NULL},
 {"single", "single", NULL, "single", "1", GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL, 
    "Exit after a single connection.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"chroot_path", "chroot_path", NULL, "chroot-path", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL, 
    "Path to become the new root after authentication.  This path must contain a valid "
    "certificate structure, /etc/passwd, and /etc/group.  The command "
    "globus-gridftp-server-setup-chroot can help create a suitable directory structure.", NULL, NULL,GLOBUS_FALSE, NULL},
{NULL, "Authentication, Authorization, and Security Options", NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL,GLOBUS_FALSE, NULL},
 {"auth_level", "auth_level", NULL, "auth-level", NULL, GLOBUS_L_GFS_CONFIG_INT, -1, NULL,
    "Add levels together to use more than one.\n    0 = Disables all authorization checks.\n    1 = Authorize identity. "
    "\n    2 = Authorize all file/resource accesses.\n    4 = Disable changing process uid to authenticated user (no setuid) -- DO NOT use this when process is started as root.\n"
    "If not set uses level 2 for front ends and level 1 for data nodes.  Note that levels 2 and 4 imply level 1 as well.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"process_user", "process_user", NULL, "process-user", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "User to setuid to upon login for all connections. Only applies when running as root.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"process_group", "process_group", NULL, "process-group", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Group to setgid to upon login for all connections. If unset, the default group "
    "of process_user will be used.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"ipc_allow_from", "ipc_allow_from", NULL, "ipc-allow-from", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Only allow connections from these source ip addresses.  Specify a comma "
    "separated list of ip address fragments.  A match is any ip address that "
    "starts with the specified fragment.  Example: '192.168.1.' will match and "
    "allow a connection from 192.168.1.45.  Note that if this option is used "
    "any address not specifically allowed will be denied.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"ipc_deny_from", "ipc_deny_from", NULL, "ipc-deny-from", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Deny connections from these source ip addresses. Specify a comma "
    "separated list of ip address fragments.  A match is any ip address that "
    "starts with the specified fragment.  Example: '192.168.2.' will match and "
    "deny a connection from 192.168.2.45.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"allow_from", "allow_from", NULL, "allow-from", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Only allow connections from these source ip addresses.  Specify a comma "
    "separated list of ip address fragments.  A match is any ip address that "
    "starts with the specified fragment.  Example: '192.168.1.' will match and "
    "allow a connection from 192.168.1.45.  Note that if this option is used "
    "any address not specifically allowed will be denied.", NULL, NULL, GLOBUS_FALSE, NULL},
 {"deny_from", "deny_from", NULL, "deny-from", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Deny connections from these source ip addresses. Specify a comma "
    "separated list of ip address fragments.  A match is any ip address that "
    "starts with the specified fragment.  Example: '192.168.2.' will match and "
    "deny a connection from 192.168.2.45.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"encrypt_data", "encrypt_data", NULL, "encrypt-data", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Require encrypted data channels.  This will cause an error and prevent all "
    "transfers in which the client does not request an authenticated and encrypted "
    "data channel.", NULL, NULL, GLOBUS_FALSE, NULL},
 {"secure_ipc", "secure_ipc", NULL, "secure-ipc", "si", GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_TRUE, NULL,
    "Use GSI security on ipc channel.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"ipc_auth_mode", "ipc_auth_mode", NULL, "ipc-auth-mode", "ia", GLOBUS_L_GFS_CONFIG_STRING, 0, "host",
    "Set GSI authorization mode for the ipc connection. Options are: none, host, self or subject:[subject].", NULL, NULL,GLOBUS_FALSE, NULL},
 {"ipc_user_name", "ipc_user_name", NULL, "ipc-user-name", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL /* User name for IPC connect back [not implemented] */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"ipc_subject", "ipc_subject", NULL, "ipc-subject", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL /* Expected DN for IPC connect back, for connect forward, find this subj in cred search path */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"ipc_credential", "ipc_credential", NULL, "ipc-credential", "ipc-cred", GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL /* cred file to load for connect forward. */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"ipc_cookie", "ipc_cookie", NULL, "ipc-cookie", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL /* [not implemented] */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"allow_anonymous", "allow_anonymous", NULL, "allow-anonymous", "aa", GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Allow clear text anonymous access. If server is running as root anonymous_user "
    "must also be set.  Disables ipc security.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"anonymous_names_allowed", "anonymous_names_allowed", NULL, "anonymous-names-allowed", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Comma separated list of names to treat as anonymous users when "
    "allowing anonymous access.  If not set, the default names of 'anonymous' "
    "and 'ftp' will be allowed.  Use '*' to allow any username.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"anonymous_user", "anonymous_user", NULL, "anonymous-user", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "User to setuid to for an anonymous connection. Only applies when running as root.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"anonymous_group", "anonymous_group", NULL, "anonymous-group", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Group to setgid to for an anonymous connection. If unset, the default group "
    "of anonymous_user will be used.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"sharing_dn", "sharing_dn", NULL, "sharing-dn", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Allow sharing when using the supplied DN.  A client connected with these credentials will "
    "be able to access any user for which sharing is enabled.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"sharing_state_dir", "sharing_state_dir", NULL, "sharing-state-dir", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Full path to a directory that will contain files used by GridFTP to control sharing "
    "access for individual local accounts. The special variables '$HOME' and '$USER' can "
    "be used to create a dynamic path that is unique to each local account.  This path"
    "must be writable by the associated account. "
    "The default path is '$HOME/.globus/sharing/'.  This must refer to a path on the filesystem, "
    "not a path that is only accessible via a DSI plugin.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"sharing_control", "sharing_control", NULL, "sharing-control", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_TRUE, NULL,
    "Allow a local user account to control its own sharing access via special "
    "GridFTP client commands.  The user account must have filesystem write access to "
    "the sharing state dir.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"sharing_rp", "sharing_rp", NULL, "sharing-rp", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Sharing specific path restrictions.  This completely replaces the normal path "
    "restrictions (-rp) when an account is being shared by a sharing-dn login."
    "Follows normal path restriction semantics.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"sharing_users_allow", "sharing_users_allow", NULL, "sharing-users-allow", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Comma separated list of usernames that are allowed to share unless matched "
    "in the user deny lists.  If this list is set, users that are not "
    "included will be denied unless matched in the group allow list."
    "", NULL, NULL,GLOBUS_FALSE, NULL},
 {"sharing_users_deny", "sharing_users_deny", NULL, "sharing-users-deny", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Comma separated list of usernames that are denied sharing even if matched "
    "in the user or group allow lists."
    "", NULL, NULL,GLOBUS_FALSE, NULL},
 {"sharing_groups_allow", "sharing_groups_allow", NULL, "sharing-groups-allow", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Comma separated list of groups whose members are allowed to share unless "
    "matched in the user or group deny lists.  If this list is set, groups that "
    "are not included will be denied unless matched in the user allow list."
    "", NULL, NULL,GLOBUS_FALSE, NULL},
 {"sharing_groups_deny", "sharing_groups_deny", NULL, "sharing-groups-deny", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Comma separated list of groups whose members will be denied sharing "
    "unless matched in the user allow list."
    "", NULL, NULL,GLOBUS_FALSE, NULL},
 {"allow_root", "allow_root", NULL, "allow-root", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Allow clients to be mapped to the root account.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"allow_disabled_login", "allow_disabled_login", NULL, "allow-disabled-login", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Do not check if a user's system account is disabled before allowing login.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"pw_file", "pw_file", NULL, "password-file", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Enable clear text access and authenticate users against this /etc/passwd formatted file.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"connections_max", "connections_max", NULL, "connections-max", NULL, GLOBUS_L_GFS_CONFIG_INT, -1, NULL,
    "Maximum concurrent connections allowed.  Only applies when running in daemon "
    "mode.  Unlimited if not set.", NULL, NULL,GLOBUS_TRUE, NULL},
 {"connections_disabled", "connections_disabled", NULL, "connections-disabled", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Disable all new connections.  For daemon mode, issue a SIGHUP to the server process after changing the config file "
    "in order to not affect ongoing connections.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"offline_msg", "offline_msg", NULL, "offline-msg", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Custom message to be displayed to clients when the server is offline via the "
    "connections_disabled or connections_max = 0 options.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"disable_command_list", "disable_command_list", NULL, "disable-command-list", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "A comma separated list of client commands that will be disabled.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"cas", "cas", NULL, "cas", "authz-callouts", GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_TRUE, NULL,
    "Enable the GSI authorization callout framework, for callouts such as CAS.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"use_home_dirs", "use_home_dirs", NULL, "use-home-dirs", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_TRUE, NULL,
    "Set the starting directory to the authenticated users home dir.  Disabling this is the "
    "same as setting '-home-dir /'.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"home_dir", "home_dir", NULL, "home-dir", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Set a path to override the system defined home/starting directory for authenticated "
    "users.  The special variable strings '$USER' and '$HOME' may be used.  The authenticated "
    "username will be substituted for $USER, and the user's real home dir will be substituted for "
    "$HOME.  Be sure to escape the $ character if using these on the command line.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"restrict_paths", "restrict_paths", NULL, "restrict-paths", "rp", GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "A comma separated list of full paths that clients may access.  Each path may be prefixed by R and/or W, denoting "
    "read or write access, otherwise full access is granted.  If a given path is a directory, "
    "all contents and subdirectories will be given the same access.  Order of paths does not matter -- "
    "the permissions on the longest matching path will apply.  The special character '~' will be "
    "replaced by the authenticated user's home directory, or the '-home-dir' option, if used. "
    "Note that if the home directory is not accessible, '~' will be set to '/'.  "
    "By default all paths are allowed, and access control is handled by the OS. "
    "In a striped or split process configuration, this should be set on both the frontend and data nodes.", 
    NULL, NULL,GLOBUS_FALSE, NULL},
 {"rp_follow_symlinks", "rp_follow_symlinks", NULL, "rp-follow-symlinks", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Do not verify that a symlink points to an allowed path before following.  By default, symlinks are "
    "followed only when they point to an allowed path.  By enabling this option, symlinks "
    "will be followed even if they point to a path that is otherwise restricted.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"acl", "acl", NULL, "acl", "em", GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "A comma separated list of ACL or event modules to load.",
    NULL, NULL,GLOBUS_FALSE, NULL}, 
{NULL, "Logging Options", NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL,GLOBUS_FALSE, NULL},
 {"log_level", "log_level", NULL, "log-level", "d", GLOBUS_L_GFS_CONFIG_STRING, 0, "ERROR",
    "Log level. A comma separated list of levels from: 'ERROR, WARN, INFO, TRANSFER, DUMP, ALL'. "
    "TRANSFER includes the same statistics that are sent to the separate transfer "
    "log when -log-transfer is used.  Example: error,warn,info. You may also specify a numeric "
    "level of 1-255.  The default level is ERROR.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"log_module", "log_module", NULL, "log-module", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "globus_logging module that will be loaded. If not set, the default 'stdio' module will "
    "be used, and the logfile options apply.  Built in modules are 'stdio' and 'syslog'.  Log module options "
    "may be set by specifying module:opt1=val1:opt2=val2.  Available options for the built in modules "
    "are 'interval' and 'buffer', for buffer flush interval and buffer size, respectively. "
    "The default options are a 64k buffer size and a 5 second flush interval.  A 0 second flush interval "
    "will disable periodic flushing, and the buffer will only flush when it is full.  A value of 0 for "
    "buffer will disable buffering and all messages will be written immediately.  "
    "Example: -log-module stdio:buffer=4096:interval=10", NULL, NULL,GLOBUS_FALSE, NULL},
 {"log_single", "log_single", NULL, "logfile", "l", GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Path of a single file to log all activity to.  If neither this option or log_unique is set, "
    "logs will be written to stderr unless the execution mode is detached or inetd, "
    "in which case logging will be disabled.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"log_unique", "log_unique", NULL, "logdir", "L", GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Partial path to which 'gridftp.(pid).log' will be appended to construct the log filename. "
    "Example: -L /var/log/gridftp/ will create a separate log ( /var/log/gridftp/gridftp.xxxx.log ) "
    "for each process (which is normally each new client session).  If neither this option or "
    "log_single is set, logs will be written to stderr unless the execution mode is detached or inetd, "
    "in which case logging will be disabled.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"log_transfer", "log_transfer", NULL, "log-transfer", "Z", GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Log netlogger style info for each transfer into this file.  You may also use the "
    "log-level of TRANSFER to include this info in the standard log.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"log_filemode", "log_filemode", NULL, "log-filemode", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "File access permissions of log files. Should be an octal number such as "
    "0644.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"disable_usage_stats", "disable_usage_stats", "GLOBUS_USAGE_OPTOUT", "disable-usage-stats", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Disable transmission of per-transfer usage statistics.  See the Usage Statistics "
    "section in the online documentation for more information.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"usage_stats_target", "usage_stats_target", NULL, "usage-stats-target", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Comma separated list of contact strings (host:port) for usage statistics receivers.  The usage stats sent to "
    "a particular receiver may be customized by configuring it with a taglist (host:port!taglist)  The taglist is a list "
    "of characters that each correspond to a usage stats tag.  When this option is unset, stats are reported to "
    "usage-stats.globus.org:4810.  If you set your own receiver, and wish to continue reporting to the Globus receiver, "
    "you will need to add it manually.  The list of available tags follow. Tags marked * are reported by default.\n\t\n"
    "  *(e) START - start time of transfer\n"
    "  *(E) END - end time of transfer\n"
    "  *(v) VER - version string of GridFTP server\n"
    "  *(b) BUFFER - tcp buffer size used for transfer\n"
    "  *(B) BLOCK - disk blocksize used for transfer\n"
    "  *(N) NBYTES - number of bytes transferred\n"
    "  *(s) STREAMS - number of parallel streams used\n"
    "  *(S) STRIPES - number of stripes used\n"
    "  *(t) TYPE - transfer command: RETR, STOR, LIST, etc\n"
    "  *(c) CODE - ftp result code (226 = success, 5xx = fail)\n"
    "  *(D) DSI - DSI module in use\n"
    "  *(A) EM - event modules in use\n"
    "  *(T) SCHEME - ftp, gsiftp, sshftp, etc. (client supplied)\n"
    "  *(a) APP - guc, rft, generic library app, etc. (client supplied)\n"
    "  *(V) APPVER - version string of above. (client supplied)\n"
    "  (f) FILE - name of file/data transferred\n"
    "  (i) CLIENTIP - ip address of host running client (control channel)\n"
    "  (I) DATAIP - ip address of source/dest host of data (data channel)\n"
    "  (u) USER - local user name the transfer was performed as\n"
    "  (d) USERDN - DN that was mapped to user id\n"
    "  (C) CONFID - ID defined by -usage-stats-id config option\n"
    "  (U) SESSID - unique id that can be used to match transfers in a session and\n"
    "      transfers across source/dest of a third party transfer. (client supplied)"
    , NULL, NULL,GLOBUS_FALSE, NULL},
 {"usage_stats_id", "usage_stats_id", NULL, "usage-stats-id", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Identifying tag to include in usage statistics data.  If this is set and usage-stats-target is unset, "
    "CONFID will be added to the default usage stats data.", NULL, NULL, GLOBUS_FALSE, NULL},
{NULL, "Single and Striped Remote Data Node Options", NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL,GLOBUS_FALSE, NULL},
 {"remote_nodes", "remote_nodes", NULL, "remote-nodes", "r", GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Comma separated list of remote node contact strings.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"hybrid", "hybrid", NULL, "hybrid", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "When a server is configured for striped operation with the 'remote_nodes' option, "
    "both a frontend and backend process are started even if the client does not request multiple "
    "stripes.  This option will start backend processes only when striped operation is requested "
    "by the client, while servicing non-striped requests with a single frontend process. "
    " ", NULL, NULL,GLOBUS_FALSE, NULL},
 {"data_node", "data_node", NULL, "data-node", "dn", GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "This server is a backend data node.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"stripe_blocksize", "stripe_blocksize", NULL, "stripe-blocksize", "sbs", GLOBUS_L_GFS_CONFIG_INT, (1024 * 1024), NULL,
    "Size in bytes of sequential data that each stripe will transfer.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"stripe_count", "stripe_count", NULL, "stripe-count", NULL, GLOBUS_L_GFS_CONFIG_INT, -1, NULL,
    "Number of number stripes to use per transfer when this server controls that number.  If remote nodes are statically "
    "configured (via -r or remote_nodes), this will be set to that number of nodes, otherwise the default is 1.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"brain", "brain", NULL, "brain", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL /* switch out the default remote brain [unsupported] */, NULL, NULL, GLOBUS_FALSE, NULL},
 {"stripe_layout", "stripe_layout", NULL, "stripe-layout", "sl", GLOBUS_L_GFS_CONFIG_INT, GLOBUS_GFS_LAYOUT_BLOCKED, NULL,
    "Stripe layout.\n    1 = Partitioned\n   2 = Blocked.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"stripe_blocksize_locked", "stripe_blocksize_locked", NULL, "stripe-blocksize-locked", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Do not allow client to override stripe blocksize with the OPTS RETR command", NULL, NULL,GLOBUS_FALSE, NULL},
 {"stripe_layout_locked", "stripe_layout_locked", NULL, "stripe-layout-locked", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Do not allow client to override stripe layout with the OPTS RETR command", NULL, NULL,GLOBUS_FALSE, NULL},
 {"stripe_mode", "stripe_mode", NULL, "stripe-mode", NULL, GLOBUS_L_GFS_CONFIG_INT, 1, NULL,
    NULL /* "Mode 1 is a 1-1 stripe configuration. Mode 2 is ALL-ALL."  */, NULL, NULL,GLOBUS_FALSE, NULL},
{NULL, "Disk Options", NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL,GLOBUS_FALSE, NULL},
 {"blocksize", "blocksize", NULL, "blocksize", "bs", GLOBUS_L_GFS_CONFIG_INT, (256 * 1024), NULL,
    "Size in bytes of data blocks to read from disk before posting to the network.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"sync_writes", "sync_writes", NULL, "sync-writes", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Flush disk writes before sending a restart marker.  This attempts to ensure that "
    "the range specified in the restart marker has actually been committed to disk. "
    "This option will probably impact performance, and may result in different behavior "
    "on different storage systems. See the manpage for sync() for more information.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"direct_io", "direct", NULL, "direct", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    NULL /* use O_DIRECT */, NULL, NULL, GLOBUS_FALSE, NULL},
 {"perms", "perms", NULL, "perms", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Set the default permissions for created files. Should be an octal number "
    "such as 0644.  The default is 0644.  Note: If umask is set it will affect "
    "this setting -- i.e. if the umask is 0002 and this setting is 0666, the "
    "resulting files will be created with permissions of 0664. ", NULL, NULL,GLOBUS_FALSE, NULL},
 {"file_timeout", "file_timeout", NULL, "file-timeout", NULL, GLOBUS_L_GFS_CONFIG_INT, 0, NULL,
    "Timeout in seconds for all disk accesses.  A value of 0 disables the timeout.", NULL, NULL,GLOBUS_FALSE, NULL},
{NULL, "Network Options", NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL,GLOBUS_FALSE, NULL},
 {"port", "port", NULL, "port", "p", GLOBUS_L_GFS_CONFIG_INT, 0, NULL,
    "Port on which a frontend will listen for client control channel connections, "
    "or on which a data node will listen for connections from a frontend.  If not set "
    "a random port will be chosen and printed via the logging mechanism.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"control_interface", "control_interface", NULL, "control-interface", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Hostname or IP address of the interface to listen for control connections "
    "on. If not set will listen on all interfaces.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"data_interface", "data_interface", NULL, "data-interface", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Hostname or IP address of the interface to use for data connections. If not "
    "set will use the current control interface.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"ipc_interface", "ipc_interface", NULL, "ipc-interface", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Hostname or IP address of the interface to use for ipc connections. If not "
    "set will listen on all interfaces.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"hostname", "hostname", NULL, "hostname", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Effectively sets the above control_interface, data_interface and ipc_interface options.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"ipc_port", "ipc_port", NULL, "ipc-port", NULL, GLOBUS_L_GFS_CONFIG_INT, 0, NULL,
    "Port on which the frontend will listen for data node connections.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"control_preauth_timeout", "control_preauth_timeout", NULL, "control-preauth-timeout", NULL, GLOBUS_L_GFS_CONFIG_INT, 120, NULL,
    "Time in seconds to allow a client to remain connected to the control "
    "channel without activity before authenticating.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"control_idle_timeout", "control_idle_timeout", NULL, "control-idle-timeout", NULL, GLOBUS_L_GFS_CONFIG_INT, 600, NULL,
    "Time in seconds to allow a client to remain connected to the control "
    "channel without activity.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"ipc_idle_timeout", "ipc_idle_timeout", NULL, "ipc-idle-timeout", NULL, GLOBUS_L_GFS_CONFIG_INT, 900, NULL,
    "Idle time in seconds before an unused ipc connection will close.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"ipc_connect_timeout", "ipc_connect_timeout", NULL, "ipc-connect-timeout", NULL, GLOBUS_L_GFS_CONFIG_INT, 60, NULL,
    "Time in seconds before canceling an attempted ipc connection.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"always_send_markers", "always_send_markers", NULL, "always-send-markers", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    NULL, NULL, NULL,GLOBUS_FALSE, NULL}, /* always send perf and restart markers, even in mode S */
 {"allow_udt", "allow_udt", NULL, "allow-udt", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Enable protocol support for UDT with NAT traversal if the udt driver is available.  Requires threads.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"port_range", "port_range", NULL, "port-range", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Port range to use for incoming connections. The format is \"startport,endport\". "
    "This, along with -data-interface, can be used to enable operation behind "
    "a firewall and/or when NAT is involved. "
    "This is the same as setting the environment variable GLOBUS_TCP_PORT_RANGE.", NULL, NULL, GLOBUS_FALSE, NULL},
{NULL, "User Messages", NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL,GLOBUS_FALSE, NULL},
 {"banner", "banner", NULL, "banner", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Message to display to the client before authentication.", NULL, NULL,GLOBUS_TRUE, NULL},
 {"banner_file", "banner_file", NULL, "banner-file", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "File to read banner message from.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"banner_terse", "banner_terse", NULL, "banner-terse", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "When this is set, the minimum allowed banner message will be displayed "
    "to unauthenticated clients.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"banner_append", "banner_append", NULL, "banner-append", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "When this is set, the message set in the 'banner' or 'banner_file' option "
    "will be appended to the default banner message rather than replacing it.", 
    NULL, NULL,GLOBUS_FALSE, NULL},
 {"version_tag", "version_tag", NULL, "version-tag", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Add an identifying string to the existing toolkit version.  This is displayed in the default "
    "banner message, the SITE VERSION command, and usage stats.", NULL, NULL,GLOBUS_TRUE, NULL},
 {"login_msg", "login_msg", NULL, "login-msg", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Message to display to the client after authentication.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"login_msg_file", "login_msg_file", NULL, "login-msg-file", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "File to read login message from.", NULL, NULL,GLOBUS_FALSE, NULL},
{NULL, "Module Options", NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL,GLOBUS_FALSE, NULL},
 {"load_dsi_module", "load_dsi_module", NULL, "dsi", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Data Storage Interface module to load. File and remote modules are defined by the server. "
    "If not set, the file module is loaded, unless the 'remote' option is specified, in which case the remote "
    "module is loaded.  An additional configuration string can be passed to the DSI using the format " 
    "[module name]:[configuration string] to this option.  The format of the configuration "
    "string is defined by the DSI being loaded.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"allowed_modules", "allowed_modules", NULL, "allowed-modules", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Comma separated list of ERET/ESTO modules to allow, and optionally specify an alias for. "
    "Example: module1,alias2:module2,module3 (module2 will be loaded when a client asks for alias2).", NULL, NULL,GLOBUS_FALSE, NULL}, 
 {"dc_whitelist", "dc_whitelist", NULL, "dc-whitelist", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "A comma separated list of drivers allowed on the network stack.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"fs_whitelist", "fs_whitelist", NULL, "fs-whitelist", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "A comma separated list of drivers allowed on the disk stack.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"popen_whitelist", "popen_whitelist", NULL, "popen-whitelist", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "A comma separated list of programs that the popen driver is allowed to "
    "execute, when used on the network or disk stack.  An alias may also be "
    "specified, so that a client does not need to specify the full path. "
    "Format is [alias:]prog,[alias:]prog. example: /bin/gzip,tar:/bin/tar", NULL, NULL, GLOBUS_FALSE, NULL},
 {"netmgr", "xnetmgr", NULL, "xnetmgr", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
   "An option string to pass to the XIO Network Manager Driver, which will "
   "then be loaded for all data channel connections.  "
   "This must be in the form \"manager=module;option1=value;option2=value;\".  "
   "See the Network Manager documentation for more info.", NULL, NULL, GLOBUS_FALSE, NULL},
 {"dc_default", "dc_default", NULL, "dc-default", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "A comma separated list of XIO drivers and options representing the default "
    "network stack. Format is of each driver entry is driver1[:opt1=val1;opt2=val2;...]. "
    "The bottom of the stack, the transport driver, is always first.", NULL, NULL, GLOBUS_FALSE, NULL},
 {"fs_default", "fs_default", NULL, "fs-default", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "A comma separated list of XIO drivers and options representing the default "
    "disk stack. Format is of each driver entry is driver1[:opt1=val1;opt2=val2;...]. "
    "The bottom of the stack, the transport driver, is always first.", NULL, NULL, GLOBUS_FALSE, NULL},
{NULL, "Other", NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL,GLOBUS_FALSE, NULL},
 {"configfile", NULL, NULL, "c", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
     "Path to main configuration file that should be loaded.  Otherwise will attempt "
     "to load $GLOBUS_LOCATION/etc/gridftp.conf and /etc/grid-security/gridftp.conf.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"config_dir", "config_dir", NULL, "C", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
     "Path to directory holding configuration files that should be loaded. Files "
     "will be loaded in alphabetical order, and in the event of duplicate parameters "
     "the last loaded file will take precedence.  Backup files and files created by "
     "package updates (e.g. file.rpmsave) will be ignored.  Note that the main "
     "configuration file, if one exists, will always be loaded last.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"config_base_path", "config_base_path", NULL, "config-base-path", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
     "Base path to use when config and log path options are not full paths. "
     "By default this is the current directory when the process is started.", NULL, NULL,GLOBUS_FALSE, NULL},
 {"debug", "debug", NULL, "debug", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    "Sets options that make server easier to debug.  Forces no-fork, no-chdir, "
    "and allows core dumps on bad signals instead of exiting cleanly. "
    "Not recommended for production servers.  Note that non-forked servers running "
    "as 'root' will only accept a single connection, and then exit.", NULL, NULL,GLOBUS_FALSE, NULL}, 
 {"pidfile", "pidfile", NULL, "pidfile", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    "Write PID of the GridFTP server to this path. May contain variable references to ${localstatedir}", NULL, NULL, GLOBUS_FALSE, NULL},

/* internal use */
 {"globus_location", "globus_location", "GLOBUS_LOCATION", "G", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL, NULL, NULL} /* "GLOBUS_LOCATION." */,
 {"ignore_bad_threads", "ignore_bad_threads", NULL, "ignore-bad-threads", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    NULL, NULL, NULL,GLOBUS_FALSE, NULL}, /* allow LinuxThreads */
 {"bad_signal_exit", "bad_signal_exit", NULL, "exit", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_TRUE, NULL,
    NULL, NULL, NULL,GLOBUS_FALSE, NULL}, /* exit cleanly on bad signals (no core dump) */
 {"test_acl", NULL, NULL, NULL, "testacl", GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL /* load and pass arguments to the test acl module. the string
        may include BLOCK, which will cause a failure in the callback,
        and any or all of ALL, init, or read, write, etc action to fail on */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"wsdl", NULL, NULL, "wsdl", NULL, GLOBUS_L_GFS_CONFIG_STRING, GLOBUS_FALSE, NULL,
    NULL /* generate wsdl */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"html", NULL, NULL, "html", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    NULL /* generate usage suitable for web docs */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"docbook", NULL, NULL, "docbook", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    NULL /* generate usage suitable for web docs */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"asciidoc", NULL, NULL, "asciidoc", NULL, GLOBUS_L_GFS_CONFIG_BOOL, GLOBUS_FALSE, NULL,
    NULL /* generate usage suitable for asciidoc docs */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"fqdn", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL /* hostname found by gethostname() */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"contact_string", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL /* contact string that server is listening on */, NULL, NULL,GLOBUS_TRUE, NULL},
 {"loaded_config", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
     NULL /* placeholder so configfile check doesn't fail */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"version_string", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
     NULL /* version string */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"module_list", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_LIST, 0, NULL,
    NULL /* used to store list of allowed modules */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"popen_list", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_LIST, 0, NULL,
    NULL /* used to store list of allowed popen execs */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"exec_name", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL /* full path of server used when fork/execing */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"dsi_options", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL /* options parsed from load_dsi_module config */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"ipc_cred", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_VOID, 0, NULL,
    NULL /* loaded cred to use for ipc connection */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"argv", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_VOID, 0, NULL,
    NULL /* original argv */, NULL, NULL,GLOBUS_FALSE, NULL},
 {"argc", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_INT, 0, NULL,
    NULL /* original argc */, NULL, NULL, GLOBUS_FALSE, NULL},

/* service container stuff */
 {"extension", "extension", NULL, "extension", NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL, NULL, NULL, GLOBUS_FALSE, NULL},
 {"extension_args", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL, NULL, NULL, GLOBUS_FALSE, NULL},
 {"approximate_load", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL,
    NULL, NULL, NULL, GLOBUS_TRUE, NULL},
 {"open_connections_count", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_INT, 0, NULL,
    NULL /* Current connections.  Applicable only to daemon mode. */, NULL, NULL, GLOBUS_TRUE, NULL},
 {"backend_pool", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_VOID, 0, NULL,
    NULL /* Number of backends registered. */, NULL, NULL, GLOBUS_TRUE, NULL},
 {"backends_registered", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_INT, 0, NULL,
    NULL /* Number of backends registered. */, NULL, NULL, GLOBUS_TRUE, NULL},
 {"data_connection_max", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_INT, 0, NULL,
    NULL /* Data node connection count. */, NULL, NULL, GLOBUS_TRUE, NULL},
 {"tcp_mem_limit", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_INT, 0, NULL,
    NULL /* TCP memory limit */, NULL, NULL, GLOBUS_TRUE, NULL},
 {"max_bw", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_INT, 0, NULL, 
    NULL, NULL, NULL, GLOBUS_TRUE, NULL},
 {"current_bw", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_INT, 0, NULL, 
    NULL, NULL, NULL, GLOBUS_TRUE, NULL},
 {"file_transfer_count", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_INT, 0, NULL,
    NULL, NULL, NULL, GLOBUS_TRUE, NULL},
 {"byte_transfer_count", NULL, NULL, NULL, NULL, GLOBUS_L_GFS_CONFIG_STRING, 0, NULL, 
    NULL, NULL, NULL, GLOBUS_TRUE, NULL},
{NULL, /* END */ NULL, NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL,GLOBUS_FALSE, NULL}
};

static int option_count = sizeof(option_list) / sizeof(globus_l_gfs_config_option_t);

static globus_hashtable_t               option_table;
static int                              globus_l_gfs_num_threads = -1;
static char *                           globus_l_gfs_port_range = NULL;
static globus_bool_t                    globus_l_gfs_common_loaded = GLOBUS_FALSE;
static globus_bool_t                    globus_l_gfs_is_worker = GLOBUS_FALSE;

/* for string options, setting with an int_val of 1 will free the old one */ 
static
int
globus_l_gfs_config_set(
    char *                              option_name,
    int                                 int_value,
    void *                              value)
{
    globus_l_gfs_config_option_t *      option;
    int                                 i;
    int                                 rc; 
    GlobusGFSName(globus_l_gfs_config_set);
    GlobusGFSDebugEnter();

    option = (globus_l_gfs_config_option_t *) globus_hashtable_remove(
            &option_table, option_name);   
    if(!option)
    {
        option = (globus_l_gfs_config_option_t *)
            globus_calloc(1, sizeof(globus_l_gfs_config_option_t));
        for(i = 0; 
            i < option_count && option_list[i].option_name &&
                strcmp(option_name, option_list[i].option_name); 
            i++);
        if(i == option_count)
        {
            goto error;
        }    
        memcpy(option, &option_list[i], sizeof(globus_l_gfs_config_option_t));
    }
    switch(option->type)
    {
      case GLOBUS_L_GFS_CONFIG_BOOL:
      case GLOBUS_L_GFS_CONFIG_INT:
        option->int_value = int_value;
        break;
      case GLOBUS_L_GFS_CONFIG_STRING:
        if(int_value && option->value != NULL)
        {
            globus_free(option->value);
        }
      case GLOBUS_L_GFS_CONFIG_LIST:
      case GLOBUS_L_GFS_CONFIG_VOID:
        option->value = value;
        break;
      default:
        option->value = value;
        break;
    }
    rc = globus_hashtable_insert(&option_table,
        option->option_name,
        option);
    if(rc)
    {
        goto error;
    }

    GlobusGFSDebugExit();
    return 0;

error:
    globus_free(option);
    GlobusGFSDebugExitWithError();
    return 1;             
}

#define GLOBUS_L_GFS_LINEBUFLEN 1024

static
globus_result_t
globus_l_gfs_config_load_config_file(
    char *                              filename)
{
    FILE *                              fptr;
    char *                              linebuf;
    char *                              optionbuf;
    char *                              valuebuf;
    int                                 linebuflen = GLOBUS_L_GFS_LINEBUFLEN;
    int                                 i;
    int                                 rc;
    globus_l_gfs_config_option_t *      option;
    int                                 line_num;
    int                                 optlen;
    char *                              p;
    globus_off_t                        tmp_off;
    globus_bool_t                       found;
    GlobusGFSName(globus_l_gfs_config_load_config_file);
    GlobusGFSDebugEnter();

    fptr = fopen(filename, "r");
    if(fptr == NULL)
    {
        GlobusGFSDebugExitWithError();
        return -2; /* XXX construct real error */
    }
    globus_l_gfs_config_set("loaded_config", 0, globus_libc_strdup(filename));  

    line_num = 0;
    linebuf = globus_malloc(linebuflen);
    if(!linebuf)
    {
        goto error_mem;
    }
    optionbuf = globus_malloc(linebuflen);
    if(!optionbuf)
    {
        goto error_mem;
    }
    valuebuf = globus_malloc(linebuflen);
    if(!valuebuf)
    {
        goto error_mem;
    }
    
    while(fgets(linebuf, linebuflen, fptr) != NULL)
    {
        p = linebuf;
        while(p && linebuf[strlen(linebuf) - 1] != '\n')
        {
            char                        part_line[GLOBUS_L_GFS_LINEBUFLEN];

            p = fgets(part_line, GLOBUS_L_GFS_LINEBUFLEN, fptr);
            if(p != NULL)
            {
                linebuflen += GLOBUS_L_GFS_LINEBUFLEN;
                linebuf = globus_realloc(linebuf, linebuflen);
                if(!linebuf)
                {
                    goto error_mem;
                }
                strncat(linebuf, part_line, linebuflen);
                
                optionbuf = globus_realloc(optionbuf, linebuflen);
                if(!optionbuf)
                {
                    goto error_mem;
                }
                valuebuf = globus_realloc(valuebuf, linebuflen);
                if(!valuebuf)
                {
                    goto error_mem;
                }
            }
        }
        line_num++;
        p = linebuf;
        optlen = 0;               
        while(*p && isspace(*p))
        {
            p++;
        }
        if(*p == '\0')
        {
            continue;
        }
        if(*p == '#')
        {
            continue;
        }        
        if(*p == '$')
        {
            continue;
        }        

        if(*p == '"')
        {
            rc = sscanf(p, "\"%[^\"]\"", optionbuf);
            optlen = 2;
        }
        else
        {
            rc = sscanf(p, "%s", optionbuf);
        }        
        if(rc != 1)
        {   
            goto error_parse;
        }
        optlen += strlen(optionbuf);
        p = p + optlen;
               
        optlen = 0;
        while(*p && isspace(*p))
        {
            p++;
        }
        if(*p == '"')
        {
            rc = sscanf(p, "\"%[^\"]\"", valuebuf);
            optlen = 2;
        }
        else
        {
            rc = sscanf(p, "%s", valuebuf);
        }        
        if(rc != 1)
        {   
            goto error_parse;
        }        
        optlen += strlen(valuebuf);
        p = p + optlen;        
        while(*p && isspace(*p))
        {
            p++;
        }
        if(*p && !isspace(*p))
        {
            goto error_parse;
        }
        
        found = GLOBUS_FALSE;
        for(i = 0; !found && i < option_count; i++)
        {
            if(option_list[i].option_name == NULL)
            {
                continue;
            }
            if(!option_list[i].configfile_option || 
                strcmp(optionbuf, option_list[i].configfile_option))
            {
                continue;
            }
            
            found = GLOBUS_TRUE;
            
            option = (globus_l_gfs_config_option_t *) globus_hashtable_remove(
                    &option_table, option_list[i].option_name);   
            if(!option)
            {
                option = (globus_l_gfs_config_option_t *)
                    globus_malloc(sizeof(globus_l_gfs_config_option_t));
                memcpy(option, &option_list[i], sizeof(globus_l_gfs_config_option_t));
            }
            switch(option->type)
            {
              case GLOBUS_L_GFS_CONFIG_BOOL:
                if(valuebuf[0] == '0' && valuebuf[1] == '\0')
                {
                    option->int_value = 0;
                }
                else if(valuebuf[0] == '1' && valuebuf[1] == '\0')
                {
                    option->int_value = 1;
                }
                else
                {
                    globus_gfs_log_exit_message("Value for %s must be 0 or 1.\n", 
                        option_list[i].option_name);
                    goto error_parse;
                }                    
                break;
              case GLOBUS_L_GFS_CONFIG_INT:
                rc = globus_args_bytestr_to_num(valuebuf, &tmp_off);
                if(rc != 0)
                {
                    globus_gfs_log_exit_message("Invalid value for %s\n", 
                        option_list[i].option_name);
                    goto error_parse;
                }                  
                option->int_value = (int) tmp_off;
                break;
              case GLOBUS_L_GFS_CONFIG_STRING:
                option->value = globus_libc_strdup(valuebuf);
                break;
              default:
                break;
            }
            rc = globus_hashtable_insert(&option_table,
                option->option_name,
                (void *) option);
            
            if(rc)
            {
                /* XXX error, log something */
            }
        }
        if(!found)
        {
            globus_gfs_log_exit_message("Problem parsing config file %s: line %d. "
                "Unknown option '%s'.\n", 
                filename, line_num, optionbuf);
            goto error_param;
        }
    }

    fclose(fptr);
    
    globus_free(linebuf);
    globus_free(valuebuf);
    globus_free(optionbuf);
    
    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error_parse:
    fclose(fptr);
    globus_gfs_log_exit_message("Problem parsing config file %s: line %d.\n", 
        filename, line_num);
error_param:
error_mem:

    GlobusGFSDebugExitWithError();
    return -1;

}

static
int
globus_l_gfs_config_load_envs_from_file(
    char *                              filename)
{
    FILE *                              fptr;
    char *                              linebuf;
    char *                              optionbuf;
    char *                              valuebuf;
    int                                 linebuflen = GLOBUS_L_GFS_LINEBUFLEN;
    int                                 rc;
    int                                 line_num;
    int                                 optlen;
    char *                              p;

    fptr = fopen(filename, "r");
    if(fptr == NULL)
    {
        return -2;
    }

    line_num = 0;

    linebuf = malloc(linebuflen);
    if(!linebuf)
    {
        goto error_mem;
    }
    optionbuf = malloc(linebuflen);
    if(!optionbuf)
    {
        goto error_mem;
    }
    valuebuf = malloc(linebuflen);
    if(!valuebuf)
    {
        goto error_mem;
    }

    while(fgets(linebuf, linebuflen, fptr) != NULL)
    {
        p = linebuf;
        while(p && linebuf[strlen(linebuf) - 1] != '\n')
        {
            char                        part_line[GLOBUS_L_GFS_LINEBUFLEN];

            p = fgets(part_line, GLOBUS_L_GFS_LINEBUFLEN, fptr);
            if(p != NULL)
            {
                linebuflen += GLOBUS_L_GFS_LINEBUFLEN;
                linebuf = realloc(linebuf, linebuflen);
                if(!linebuf)
                {
                    goto error_mem;
                }
                strncat(linebuf, part_line, linebuflen);
                
                optionbuf = realloc(optionbuf, linebuflen);
                if(!optionbuf)
                {
                    goto error_mem;
                }
                valuebuf = realloc(valuebuf, linebuflen);
                if(!valuebuf)
                {
                    goto error_mem;
                }
            }
        }
        line_num++;
        p = linebuf;
        optlen = 0;               
        while(*p && isspace(*p))
        {
            p++;
        }

        /* parse !daemon options */
        if((rc = sscanf(p, "%s", optionbuf)) == 1 && !globus_l_gfs_is_worker)
        {
            if(!strcmp(optionbuf, "inetd") || !strcmp(optionbuf, "debug") ||
                !strcmp(optionbuf, "ssh") || !strcmp(optionbuf, "fork") )
            {
                p = p + strlen(optionbuf);
                       
                while(*p && isspace(*p))
                {
                    p++;
                }
                if(*p == '"')
                {
                    rc = sscanf(p, "\"%[^\"]\"", valuebuf);
                }
                else
                {
                    rc = sscanf(p, "%s", valuebuf);
                }  
                if(rc == 1)
                {
                    globus_l_gfs_is_worker = atoi(valuebuf);
                    if(!strcmp(optionbuf, "fork"))
                    {
                        globus_l_gfs_is_worker = !globus_l_gfs_is_worker;
                    }
                }
            }
            continue;
        }
        
        /* parse threads option to apply it before common activates */
        if(*p == 't' && (rc = sscanf(p, "%s", optionbuf)) == 1 && 
            globus_l_gfs_num_threads == -1)
        {
            if(!strcmp(optionbuf, "threads"))
            {
                p = p + strlen(optionbuf);
                       
                while(*p && isspace(*p))
                {
                    p++;
                }
                if(*p == '"')
                {
                    rc = sscanf(p, "\"%[^\"]\"", valuebuf);
                }
                else
                {
                    rc = sscanf(p, "%s", valuebuf);
                }  
                if(rc == 1)
                {
                    globus_l_gfs_num_threads = atoi(valuebuf);
                }
            }
            continue;
        }
        /* parse port_range option to apply it before xio activates */
        if(!globus_l_gfs_port_range && *p == 'p' && 
            (rc = sscanf(p, "%s", optionbuf)) == 1)
        {
            if(!strcmp(optionbuf, "port_range"))
            {
                p = p + strlen(optionbuf);
                       
                while(*p && isspace(*p))
                {
                    p++;
                }
                if(*p == '"')
                {
                    rc = sscanf(p, "\"%[^\"]\"", valuebuf);
                }
                else
                {
                    rc = sscanf(p, "%s", valuebuf);
                }  
                if(rc == 1)
                {
                    setenv("GLOBUS_TCP_PORT_RANGE", valuebuf, 1);
                    setenv("GLOBUS_UDP_PORT_RANGE", valuebuf, 1);
                }
            }
            continue;
        }
                
        if(*p != '$')
        {
            continue;
        }
        p++;
        
        rc = sscanf(p, "%s", optionbuf);
        if(rc != 1)
        {   
            goto error_parse;
        }
        optlen += strlen(optionbuf);
        p = p + optlen;
               
        optlen = 0;
        while(*p && isspace(*p))
        {
            p++;
        }
        if(*p == '"')
        {
            rc = sscanf(p, "\"%[^\"]\"", valuebuf);
            optlen = 2;
        }
        else
        {
            rc = sscanf(p, "%s", valuebuf);
        }        
        if(rc != 1)
        {   
            goto error_parse;
        }        
        optlen += strlen(valuebuf);
        p = p + optlen;        
        while(*p && isspace(*p))
        {
            p++;
        }
        if(*p && !isspace(*p))
        {
            goto error_parse;
        }
        
        rc = globus_libc_setenv(optionbuf, valuebuf, 1);
        if(rc < 0)
        {
            char                        errstr[PATH_MAX];
            snprintf(
                errstr, PATH_MAX,
                "Problem loading environment from config file %s: line %d.\n", 
                filename, line_num);
            perror(errstr);
        }
    }

    fclose(fptr);

    free(linebuf);
    free(valuebuf);
    free(optionbuf);
   
    return 0;

error_parse:
    if(globus_l_gfs_common_loaded)
    {
        globus_gfs_log_exit_message("Problem parsing environment from config file %s: line %d.\n", 
            filename, line_num);
    }
    else
    {
        fprintf(
            stderr,
            "Problem parsing environment from config file %s: line %d. \n", 
            filename, line_num);
    }

error_mem:
    fclose(fptr);

    return -1;
}

static
globus_result_t
globus_l_gfs_config_load_config_dir(
    char *                              conf_dir,
    globus_bool_t                       envs_only)
{
    struct dirent **                    entries;
    int                                 count;
    int                                 i;
    int                                 rc = 0;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GlobusGFSName(globus_l_gfs_config_load_config_dir);
    GlobusGFSDebugEnter();

    count = scandir(conf_dir, &entries, 0, alphasort);
    if(count >= 0)
    {
        for(i = 0; i < count && result == GLOBUS_SUCCESS; i++)
        {
            char *                      full_path;
            
            /* skip hidden and . or ..
             and files possibly created by updates .rpmsave or .rpmnew */
            if(*entries[i]->d_name == '.' ||
                strstr(entries[i]->d_name, '.rpm') != NULL)
            {
                free(entries[i]);
                continue;
            }
            
            full_path = malloc(PATH_MAX);
            rc = snprintf(
                full_path, PATH_MAX, "%s/%s", conf_dir, entries[i]->d_name);

            if(!envs_only)
            {
                rc = globus_l_gfs_config_load_config_file(full_path);
                if(rc == -2)
                {
                    globus_gfs_log_exit_message("Problem parsing config file %s: "
                        "Unable to open file.\n", full_path);
                }
                if(rc < 0)
                {
                    result = GLOBUS_FAILURE;
                }
            }
            result = globus_l_gfs_config_load_envs_from_file(full_path);
            
            free(entries[i]);
            free(full_path);
        }
        free(entries);
    }
    else if(!envs_only)
    {
        globus_gfs_log_exit_message("Problem reading files from config dir %s.\n", conf_dir);
        result = GLOBUS_FAILURE;
    }

    GlobusGFSDebugExit();
    return result;
}

static
globus_result_t
globus_l_gfs_config_load_config_env()
{
    char *                              value;
    int                                 rc;
    int                                 i;
    globus_l_gfs_config_option_t *      option;
    globus_bool_t                       opt_is_new = GLOBUS_FALSE;
    globus_off_t                        tmp_off;
    GlobusGFSName(globus_l_gfs_config_load_config_env);
    GlobusGFSDebugEnter();
    

    for(i = 0; i < option_count; i++)
    {
        if(option_list[i].option_name == NULL)
        {
            continue;
        }
        if (!option_list[i].env_var_option || !*option_list[i].env_var_option)
        {
            continue;
        }

        value = getenv(option_list[i].env_var_option);
        
        if (!value)
        {
            continue;
        }
                            
        option = (globus_l_gfs_config_option_t *) globus_hashtable_remove(
                &option_table, option_list[i].option_name);   
        if(!option)
        {
            option = (globus_l_gfs_config_option_t *)
                globus_malloc(sizeof(globus_l_gfs_config_option_t));
            opt_is_new = GLOBUS_TRUE;
            memcpy(option, &option_list[i], sizeof(globus_l_gfs_config_option_t));
        }
        switch(option->type)
        {
          case GLOBUS_L_GFS_CONFIG_BOOL:
            if(value[0] == '0' && value[1] == '\0')
            {
                option->int_value = 0;
            }
            else if(value[0] == '1' && value[1] == '\0')
            {
                option->int_value = 1;
            }
            else
            {
                globus_gfs_log_exit_message("Value for %s must be 0 or 1.\n", 
                    option_list[i].option_name);
                return -1;
            }                    
            break;
          case GLOBUS_L_GFS_CONFIG_INT:
            rc = globus_args_bytestr_to_num(value, &tmp_off);
            if(rc != 0)
            {
                if(opt_is_new)
                {
                    free(option);
                }
                globus_gfs_log_exit_message("Invalid value for %s\n", 
                    option_list[i].option_name);
                return -1;
            }                  
            option->int_value = (int) tmp_off;
            break;
          case GLOBUS_L_GFS_CONFIG_STRING:
            option->value = globus_libc_strdup(value);
            break;
          default:
            break;
        }
        rc = globus_hashtable_insert(&option_table,
            option->option_name,
            (void *) option);
        
        if(rc)
        {
            /* XXX error, log something */
        }
    }       

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;
}


static
globus_result_t
globus_l_gfs_config_load_commandline(
    int                                 argc,
    char **                             argv)
{
    int                                 arg_num;
    char *                              argp;
    int                                 i;
    int                                 rc;
    int                                 len;
    globus_l_gfs_config_option_t *      option;
    globus_bool_t                       found;
    globus_bool_t                       negate;
    globus_bool_t                       opt_is_new = GLOBUS_FALSE;
    globus_off_t                        tmp_off;
    GlobusGFSName(globus_l_gfs_config_load_commandline);
    GlobusGFSDebugEnter();
    
    for(arg_num = 1; arg_num < argc; ++arg_num)
    {
        found = GLOBUS_FALSE;
        negate = GLOBUS_FALSE;

        argp = argv[arg_num];
        len = strlen(argp);
        
        if(len && *argp == '-')
        {
            argp++;
            len--;
        }
        if(len && *argp == '-')
        {
            argp++;
            len--;
        }
        if((len - 2) && strncasecmp(argp, "no-", 3) == 0)
        {
            argp += 3;
            len -= 3;
            negate = GLOBUS_TRUE;
        }
        else if(len && tolower(*argp) == 'n')
        {
            argp++;
            len--;
            negate = GLOBUS_TRUE;
        }
        
        for(i = 0; i < option_count && !found && len; i++)
        {
            if(option_list[i].option_name == NULL)
            {
                continue;
            }
            if((!option_list[i].short_cmdline_option || 
                strcmp(argp, option_list[i].short_cmdline_option)) && 
                (!option_list[i].long_cmdline_option || 
                strcmp(argp, option_list[i].long_cmdline_option)) )
            {
                continue;
            }
            
            found = GLOBUS_TRUE;
                       
            option = (globus_l_gfs_config_option_t *) globus_hashtable_remove(
                    &option_table, option_list[i].option_name);   
            if(!option)
            {
                opt_is_new = GLOBUS_TRUE;
                option = (globus_l_gfs_config_option_t *)
                    globus_malloc(sizeof(globus_l_gfs_config_option_t));
                memcpy(
                    option,
                    &option_list[i],
                    sizeof(globus_l_gfs_config_option_t));
            }

            switch(option->type)
            {
              case GLOBUS_L_GFS_CONFIG_BOOL:
                option->int_value = !negate;
                break;

              case GLOBUS_L_GFS_CONFIG_INT:
                if(++arg_num >= argc)
                {
                    if(opt_is_new)
                    {
                        free(option);
                    }
                    globus_gfs_log_exit_message("Option %s is missing a value\n", argp);
                    return -1;
                }
                rc = globus_args_bytestr_to_num(argv[arg_num], &tmp_off);
                if(rc != 0)
                {
                    if(opt_is_new)
                    {
                        free(option);
                    }
                    globus_gfs_log_exit_message("Invalid value for %s\n", argp);
                    return -1;
                }                  
                option->int_value = (int) tmp_off;
                break;
                
              case GLOBUS_L_GFS_CONFIG_STRING:
                if(++arg_num >= argc)
                {
                    if(opt_is_new)
                    {
                        free(option);
                    }
                    globus_gfs_log_exit_message("Option %s is missing a value\n", argp);
                    return -1;
                }
                option->value = globus_libc_strdup(argv[arg_num]);
                break;

              default:
                break;
             }

            rc = globus_hashtable_insert(&option_table,
                option->option_name,
                (void *) option);
            
            if(rc)
            {
                /* XXX error, log something */
            }
            
        }
        
        if(!found)
        {
            if(opt_is_new)
            {
                free(option);
            }
            globus_gfs_log_exit_message("Unknown option on command line: %s%s\n",
                negate ? "no-" : "", argp);
            return -1;
        }
    }
      
    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

}


static
globus_result_t
globus_l_gfs_config_load_defaults()
{
    int                                 rc;
    int                                 i;
    globus_l_gfs_config_option_t *      option;
    GlobusGFSName(globus_l_gfs_config_load_defaults);
    GlobusGFSDebugEnter();
    
    for(i = 0; i < option_count; i++)
    {        
        if(option_list[i].option_name == NULL)
        {
            continue;
        }
        option = (globus_l_gfs_config_option_t *)
            globus_malloc(sizeof(globus_l_gfs_config_option_t));
        memcpy(option, &option_list[i], sizeof(globus_l_gfs_config_option_t));
        
        rc = globus_hashtable_insert(&option_table, 
            option->option_name, 
            (void *) option);
        
        if(rc)
        {
            /* XXX error, log something */
        }
    }

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS; 
}

static
int
globus_l_config_loadfile(
    const char *                        filename,
    char **                             data_out)
{
    FILE *                              file;
    int                                 file_len;
    char *                              out_buf;
    GlobusGFSName(globus_l_config_loadfile);
    GlobusGFSDebugEnter();
     
    file = fopen(filename, "r");
    if(!file)
    {
        goto error;
    }
         
    fseek(file, 0L, SEEK_END);
    file_len = ftell(file);
    fseek(file, 0L, SEEK_SET);	

    out_buf = (char *) malloc((file_len + 1) * sizeof(char));	
    if(!out_buf)
    {
        fclose(file);
        goto error;
    }

    fread(out_buf, sizeof(char), file_len, file);
    fclose(file);
    out_buf[file_len] = '\0';

    *data_out = out_buf;
         
    GlobusGFSDebugExit();
    return 0;

error:
    GlobusGFSDebugExitWithError();
    return 1;
}

static
void
globus_l_gfs_config_display_html_usage()
{
    int                                 i;
    globus_l_gfs_config_option_t *      o;
    GlobusGFSName(globus_l_gfs_config_display_html_usage);
    GlobusGFSDebugEnter();
    
    printf("<!-- generated by globus-gridftp-server -help -html -->\n");
    printf("<p>\n"
        "The table below lists config file options, associated command line " 
        "options (if available) and descriptions. Note that any boolean "
        "option can be negated on the command line by preceding the specified " 
        "option with '-no-' or '-n'.  Example: -no-cas or -nf.\n"
        "</p>\n");

    printf("<ul>\n");
    for(i = 0; i < option_count; i++)
    {        
        o = (globus_l_gfs_config_option_t *) &option_list[i];
        if(o->option_name == NULL && o->configfile_option != NULL)
        {
            printf("  <li><a href=\"#gftpclass%d\">%s</a></li>\n",
                i, o->configfile_option);
        }
    }
    printf("</ul>\n");

    printf("\n");

    printf("<table border=\"1\" cellpadding=\"5\">\n");
    for(i = 0; i < option_count; i++)
    {        
        char *                          shortflag;
        char *                          longflag;
        char *                          value;
        char *                          defval;
        
        o = (globus_l_gfs_config_option_t *) &option_list[i];
        if(o->option_name == NULL && o->configfile_option != NULL)
        {
            printf(
                "  <tr>\n"
                "    <th colspan=\"2\" valign=\"top\"><a name=\"gftpclass%d\"></a>%s</th>\n"
                "  </tr>\n",
                i,
                o->configfile_option);
            continue;
        }
        if(o->usage == NULL)
        {
            continue;
        }

        switch(o->type)
        {
          case GLOBUS_L_GFS_CONFIG_BOOL:
            shortflag = "-";
            longflag = "-";
            value = " &lt;0|1&gt;"; 
            defval = o->int_value ? "TRUE" : "FALSE";
            break;
          case GLOBUS_L_GFS_CONFIG_INT:
            shortflag = "-";
            longflag = "-";
            value = " &lt;number&gt;"; 
            defval = o->int_value > 0 ? 
                globus_common_create_string("%d", o->int_value) : "not set";
            break;
          case GLOBUS_L_GFS_CONFIG_STRING:
            shortflag = "-";
            longflag = "-";
            value = " &lt;string&gt;"; 
            defval = o->value ? o->value : "not set";
            break;
          default:
            shortflag = "";
            longflag = "";
            value = ""; 
            defval = o->value ? o->value : "not set";
            break;
        }
        printf(
            "  <tr>\n"
            "    <td valign=\"top\">\n"
            "     <table border=\"0\" cellpadding=\"2\" width=\"100%%\">\n"
            "      <tr><td valign=\"top\"><pre>%s%s</pre></td></tr>\n"
            "      <tr><td valign=\"top\"><pre>%s%s%s%s%s%s%s</pre></td></tr>\n"
            "     </table>\n"
            "    </td>\n"
            "    <td valign=\"top\"><p>%s</p><p>Default value: %s</p></td>\n"
            "  </tr>\n", 
            o->configfile_option, 
            value,
            o->short_cmdline_option ? shortflag : "", 
            o->short_cmdline_option ? o->short_cmdline_option : "",
            o->type != GLOBUS_L_GFS_CONFIG_BOOL && 
                o->short_cmdline_option ? value : "",
            o->short_cmdline_option ? "\n" : "", 
            o->long_cmdline_option ? longflag : "",
            o->long_cmdline_option ? o->long_cmdline_option : "",
            o->type != GLOBUS_L_GFS_CONFIG_BOOL && 
                o->long_cmdline_option ? value : "",
            o->usage,
            defval);
    }
    printf("</table>\n");
    printf("<!-- end generated block -->\n");

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_config_display_wsdl()
{
    int                                 i;
    globus_l_gfs_config_option_t *      o;
    GlobusGFSName(globus_l_gfs_config_display_wsdl);
    char *                              wsdl_file;
    FILE *                              out;
    globus_bool_t                       close = GLOBUS_TRUE;
    GlobusGFSDebugEnter();

    wsdl_file = globus_common_create_string("%s.xsd",
        globus_i_gfs_config_string("wsdl"));
    out = fopen(wsdl_file, "w");
    if(out == NULL)
    {
        close = GLOBUS_FALSE;
        out = stdout;
    }
    for(i = 0; i < option_count; i++)
    {        
        o = (globus_l_gfs_config_option_t *) &option_list[i];
        if(o->option_name != NULL && o->public)
        {

            switch(o->type)
            {
                case GLOBUS_L_GFS_CONFIG_INT:
                case GLOBUS_L_GFS_CONFIG_BOOL:
                    fprintf(out, "    "
                        "<xsd:element name=\"%s\" type=\"xsd:int\"/>\n",
                        o->option_name);
                    break;

                case GLOBUS_L_GFS_CONFIG_STRING:
                    fprintf(out, "    "
                        "<xsd:element name=\"%s\" type=\"xsd:string\"/>\n",
                        o->option_name);
                    break;

                default:
                    break;
            }
        }
    }
    fprintf(out, "<xsd:element name=\"GridFTPInfo\">\n");
    fprintf(out, "    <xsd:complexType><xsd:sequence>\n");

    for(i = 0; i < option_count; i++)
    {        
        o = (globus_l_gfs_config_option_t *) &option_list[i];
        if(o->option_name != NULL && o->public)
        {
            switch(o->type)
            {
                case GLOBUS_L_GFS_CONFIG_STRING:
                case GLOBUS_L_GFS_CONFIG_INT:
                case GLOBUS_L_GFS_CONFIG_BOOL:
                    fprintf(out, "        "
                        "<xsd:element ref=\"tns:%s\"/>\n",
                        o->option_name);
                    break;

                default:
                    break;
            }
        }
    }
    fprintf(out, "    </xsd:sequence></xsd:complexType>\n");
    fprintf(out, "</xsd:element>\n");

    if(close)
    {
        fclose(out);
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_config_display_c_service()
{
    int                                 i;
    globus_l_gfs_config_option_t *      o;
    GlobusGFSName(globus_l_gfs_config_display_c_service);
    FILE *                              out;
    globus_bool_t                       close = GLOBUS_TRUE;
    char *                              c_file;
    GlobusGFSDebugEnter();
    
    c_file = globus_common_create_string("%s.c",
        globus_i_gfs_config_string("wsdl"));
    out = fopen(c_file, "w");
    if(out == NULL)
    {
        close = GLOBUS_FALSE;
        out = stdout;
    }
    for(i = 0; i < option_count; i++)
    {        
        o = (globus_l_gfs_config_option_t *) &option_list[i];
        if(o->option_name != NULL && o->public)
        {
            switch(o->type)
            {
                case GLOBUS_L_GFS_CONFIG_STRING:
                case GLOBUS_L_GFS_CONFIG_INT:
                case GLOBUS_L_GFS_CONFIG_BOOL:
                    fprintf(out, "#include <%s.h>\n", o->option_name);
                    break;

                default:
                    break;
            }
        }
    }
    fprintf(out, "\n");
    fprintf(out, "\n");

    fprintf(out, "globus_result_t\n");
    fprintf(out, "gridftpA_l_setup_resource(\n");
    fprintf(out, "    globus_resource_t                   resource)\n");
    fprintf(out, "{\n");
    fprintf(out, "    globus_result_t                     result;\n");
    fprintf(out, "    globus_i_gfs_config_option_cb_ent_t * cb_handle;\n");
    for(i = 0; i < option_count; i++)
    {        
        o = (globus_l_gfs_config_option_t *) &option_list[i];
        if(o->option_name != NULL && o->public)
        {
            switch(o->type)
            {
                case GLOBUS_L_GFS_CONFIG_INT:
                case GLOBUS_L_GFS_CONFIG_BOOL:
                    fprintf(out, "    "
                        "globus_gfs_config_add_cb(&cb_handle, \"%s\",\n",
                        o->option_name);
                    fprintf(out, "        gridftpA_l_int_change_cb,\n");
                    fprintf(out, "        \"%s\");\n", o->option_name);
                    fprintf(out, "\n");
                    fprintf(out, "    "
                        "result = globus_resource_create_property_callback(\n");
                    fprintf(out, "        resource,\n");
                    fprintf(out, "        &%s_qname,\n", o->option_name);
                    fprintf(out, "        &%s_info,\n", o->option_name);
                    fprintf(out, "        gridftpA_l_int_get_cb,\n");
                    fprintf(out, "        gridftpA_l_int_set_cb,\n");
                    fprintf(out, "        cb_handle);\n");
                    fprintf(out, "    if (result != GLOBUS_SUCCESS)\n");
                    fprintf(out, "    {\n");
                    fprintf(out, "        goto error;\n");
                    fprintf(out, "    }\n");
                    fprintf(out, "\n");
                    break;

                case GLOBUS_L_GFS_CONFIG_STRING:
                    fprintf(out, "    "
                        "globus_gfs_config_add_cb(&cb_handle, \"%s\",\n",
                        o->option_name);
                    fprintf(out, "        gridftpA_l_string_change_cb,\n");
                    fprintf(out, "        \"%s\");\n", o->option_name);
                    fprintf(out, "    "
                        "result = globus_resource_create_property_callback(\n");
                    fprintf(out, "        resource,\n");
                    fprintf(out, "        &%s_qname,\n", o->option_name);
                    fprintf(out, "        &%s_info,\n", o->option_name);
                    fprintf(out, "        gridftpA_l_string_get_cb,\n");
                    fprintf(out, "        gridftpA_l_string_set_cb,\n");
                    fprintf(out, "        \"%s\");\n", o->option_name);
                    fprintf(out, "    if (result != GLOBUS_SUCCESS)\n");
                    fprintf(out, "    {\n");
                    fprintf(out, "        goto error;\n");
                    fprintf(out, "    }\n");
                    fprintf(out, "\n");
                    break;

                default:
                    break;
            }
        }
    }
    fprintf(out, "\n");
    fprintf(out, "    return GLOBUS_SUCCESS;\n");
    fprintf(out, "error:\n");
    fprintf(out, "    return result;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");

    if(close)
    {
        fclose(out);
    }

    GlobusGFSDebugExit();
}


static
void
globus_l_gfs_config_display_docbook_usage()
{
    globus_bool_t                       first = GLOBUS_TRUE;
    int                                 i;
    globus_l_gfs_config_option_t *      o;
    GlobusGFSName(globus_l_gfs_config_display_docbook_usage);
    GlobusGFSDebugEnter();
    
    printf("<!-- generated by globus-gridftp-server -help -docbook -->\n");
    printf("<para>\n"
        "The list below contains the command-line options for the server, "
        "and also the name of the configuration file entry that implements "
        "that option. Note that any boolean option can be negated on the "
        "command line by preceding the specified option with '-no-' or '-n'.  "
        "example: -no-cas or -nf.\n"
        "</para>\n"
        "</refsect1>\n");

    for(i = 0; i < option_count; i++)
    {        
        char *                          shortflag;
        char *                          longflag;
        char *                          value;
        char *                          defval;
        
        o = (globus_l_gfs_config_option_t *) &option_list[i];
        if(o->option_name == NULL && o->configfile_option != NULL)
        {
            if(!first)
            {
                printf("</variablelist>\n");
                printf("</refsect1>\n");
            }
            first = GLOBUS_FALSE;

            printf(
                "<refsect1><title>%s</title>\n"
                "<variablelist>\n",
                o->configfile_option);
            continue;
        }
        if(o->usage == NULL)
        {
            continue;
        }

        switch(o->type)
        {
          case GLOBUS_L_GFS_CONFIG_BOOL:
            shortflag = "-";
            longflag = "-";
            value = NULL;
            defval = o->int_value ? "TRUE" : "FALSE";
            break;
          case GLOBUS_L_GFS_CONFIG_INT:
            shortflag = "-";
            longflag = "-";
            value = "number"; 
            defval = o->int_value > 0 ? 
                globus_common_create_string("%d", o->int_value) : NULL;
            break;
          case GLOBUS_L_GFS_CONFIG_STRING:
            shortflag = "-";
            longflag = "-";
            value = "string";
            defval = o->value ? o->value : NULL;
            break;
          default:
            shortflag = "";
            longflag = "";
            value = ""; 
            defval = o->value ? o->value : NULL;
            break;
        }
        
        printf("  <varlistentry>\n");
        if (o->short_cmdline_option)
        {
            printf("    <term><option>-%s%s%s</option></term>\n",
                    o->short_cmdline_option,
                    value ? " " : "",
                    value ? value : "");
        }
        if (o->long_cmdline_option)
        {
            printf("    <term><option>-%s%s%s</option></term>\n",
                    o->long_cmdline_option,
                    value ? " " : "",
                    value ? value : "");
        }
        printf("<listitem><simpara>");
        if (o->usage)
        {
            printf("%s", o->usage);
        }
        if (o->configfile_option)
        {
            printf("%sThis option can also be set in the configuration file as %s.",
            o->usage ? " " : "",
            o->configfile_option);
        }
        if (defval)
        {
            printf("%sThe default value of this option is <literal>%s</literal>.",
                (o->usage || o->configfile_option) ? " " : "",
                defval);
        }
        printf("</simpara></listitem>\n");
        printf("</varlistentry>\n");
    }
    if(!first)
    {
        printf("</variablelist>\n");
        printf("</refsect1>\n");
    }
    printf("<!-- end generated block -->\n");

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_config_display_asciidoc_usage()
{
    globus_bool_t                       first = GLOBUS_TRUE;
    int                                 i;
    globus_l_gfs_config_option_t *      o;
    GlobusGFSName(globus_l_gfs_config_display_docbook_usage);
    GlobusGFSDebugEnter();
    
    printf("////\ngenerated by globus-gridftp-server -help -asciidoc\n////\n");
    printf("The list below contains the command-line options for the server, "
        "and also the name of the configuration file entry that implements "
        "that option. Note that any boolean option can be negated on the "
        "command line by preceding the specified option with '-no-' or '-n'.  "
        "example: +-no-cas+ or +-nf+.\n"
        "\n");

    for(i = 0; i < option_count; i++)
    {        
        char *                          shortflag;
        char *                          longflag;
        char *                          value;
        char *                          defval;
        
        o = (globus_l_gfs_config_option_t *) &option_list[i];
        if(o->option_name == NULL && o->configfile_option != NULL)
        {
            size_t hyphens = strlen(o->configfile_option);

            if(!first)
            {
                printf("\n");
            }
            first = GLOBUS_FALSE;

            printf(
                "%s\n",
                o->configfile_option);
            while (hyphens-- > 0)
            {
                printf("~");
            }
            printf("\n");
            continue;
        }
        if(o->usage == NULL)
        {
            continue;
        }

        switch(o->type)
        {
          case GLOBUS_L_GFS_CONFIG_BOOL:
            shortflag = "-";
            longflag = "-";
            value = NULL;
            defval = o->int_value ? "TRUE" : "FALSE";
            break;
          case GLOBUS_L_GFS_CONFIG_INT:
            shortflag = "-";
            longflag = "-";
            value = "number"; 
            defval = o->int_value > 0 ? 
                globus_common_create_string("%d", o->int_value) : NULL;
            break;
          case GLOBUS_L_GFS_CONFIG_STRING:
            shortflag = "-";
            longflag = "-";
            value = "string";
            defval = o->value ? o->value : NULL;
            break;
          default:
            shortflag = "";
            longflag = "";
            value = ""; 
            defval = o->value ? o->value : NULL;
            break;
        }
        
        printf("*");
        if (o->short_cmdline_option)
        {
            printf("-%s%s%s",
                    o->short_cmdline_option,
                    value ? " " : "",
                    value ? value : "");
            if (o->long_cmdline_option)
            {
                printf(",");
            }

        }
        if (o->long_cmdline_option)
        {
            printf("-%s%s%s",
                    o->long_cmdline_option,
                    value ? " " : "",
                    value ? value : "");
        }
        printf("*::\n");
        if (o->usage)
        {
            char *c;

            puts("    ");
            for (c = o->usage; *c != '\0'; c++)
            {
                if (*c == '~')
                {
                    putchar('\\');
                }
                putchar(*c);
            }
            putchar('\n');
        }
        if (o->configfile_option)
        {
            printf("+\nThis option can also be set in the configuration file as +%s+.\n",
            o->configfile_option);
        }
        if (defval)
        {
            printf("    The default value of this option is +%s+.\n",
                defval);
        }
        printf("\n\n");
    }
    if(!first)
    {
        printf("\n");
    }
    printf("////\nend generated block\n////\n");

    GlobusGFSDebugExit();
}

void
globus_i_gfs_config_display_long_usage()
{
    int                                 i;
    globus_l_gfs_config_option_t *      o;
    GlobusGFSName(globus_i_gfs_config_display_long_usage);
    GlobusGFSDebugEnter();

    if(globus_i_gfs_config_bool("html"))
    {
        globus_l_gfs_config_display_html_usage();
    }
    else if(globus_i_gfs_config_bool("docbook"))
    {
        globus_l_gfs_config_display_docbook_usage();
    }
    else if(globus_i_gfs_config_bool("asciidoc"))
    {
        globus_l_gfs_config_display_asciidoc_usage();
    }
    else if(globus_i_gfs_config_string("wsdl") != NULL)
    {
        globus_l_gfs_config_display_wsdl();
        globus_l_gfs_config_display_c_service();
    }
    else
    {        
        for(i = 0; i < option_count; i++)
        {        
            o = (globus_l_gfs_config_option_t *) &option_list[i];
            if(o->usage == NULL)
            {
                continue;
            }
            
            printf("%-14s    %s\n%-14s    %sCommand line or ENV args:", 
                o->option_name, o->usage, "",
                o->type == GLOBUS_L_GFS_CONFIG_BOOL ? "(FLAG)  " : "");
            if(o->short_cmdline_option)
            {
                printf(" -%s,", o->short_cmdline_option);
            }
            if(o->long_cmdline_option)
            {
                printf(" -%s,", o->long_cmdline_option);
            }
            if(o->env_var_option)
            {
                printf(" $%s", o->env_var_option);
            }
            printf("\n");
        }
        printf("\nAny FLAG can be negated by prepending '-no-' or '-n' to the "
            "command line \noption or setting a value of 0 in the config file.\n\n");
        printf("Check the GridFTP section at http://www.globus.org/toolkit/docs/\n"
            "for more in-depth documentation.\n\n");
    }

    GlobusGFSDebugExit();
}

static
int
globus_l_gfs_config_format_line(
    const char *                        in_str,
    int                                 rows,
    int                                 columns,
    char *                              out_buffer)
{
    int                                 len;
    int                                 i;
    int                                 j;
    int                                 count;
    int                                 last;
    int                                 blanks;
                                   
    len = strlen(in_str);
    count = 0;
    last = 0;
    memset(out_buffer, 0, rows * columns);
    
    for(i = 0; i < rows && count < len; i++)
    {
        for(j = 0; j < columns - 1 && count < len; j++, count++) 
        {
            if(in_str[count] == ' ')
            {
                last = count;
            }
            if(in_str[count] == '\n')
            {
                last = count;
                break;
            }
            out_buffer[i * columns + j] = in_str[count];
        }
        if(count < len && in_str[count] != ' ')
        {
            blanks = count - last;
            if (blanks < columns)
            {
                count = last + 1;
                out_buffer[i * columns + j - blanks] = 0;
            }
        }
        while(count < len && in_str[count] == ' ')
        {
            count++;
        }  
    }

    return 0;
}
        
void
globus_i_gfs_config_display_usage()
{
    int                                 i;
    globus_l_gfs_config_option_t *      o;
    GlobusGFSName(globus_i_gfs_config_display_short_usage);
    GlobusGFSDebugEnter();
    
    if(globus_i_gfs_config_bool("html"))
    {
        globus_l_gfs_config_display_html_usage();
    }
    else if(globus_i_gfs_config_bool("docbook"))
    {
        globus_l_gfs_config_display_docbook_usage();
    }
    else if(globus_i_gfs_config_bool("asciidoc"))
    {
        globus_l_gfs_config_display_asciidoc_usage();
    }
    else if(globus_i_gfs_config_string("wsdl"))
    {
        globus_l_gfs_config_display_wsdl();
        globus_l_gfs_config_display_c_service();
    }
    else
    {        
        for(i = 0; i < option_count; i++)
        {
            char       linebuffer[GLOBUS_GFS_HELP_ROWS * GLOBUS_GFS_HELP_COLS];
            int                         count = 0;
            char *                      usage;
            char *                      value;
            int                         col;
            int                         row;
            int                         len;
            char                        defval[255];
            o = (globus_l_gfs_config_option_t *) &option_list[i];
            if(o->option_name == NULL && o->configfile_option != NULL)
            {
                count = 0;
                printf("\n");
                len = GLOBUS_GFS_HELP_WIDTH - 
                    (strlen(o->configfile_option) + 4);
                if(len > 0)
                {
                    len = len / 2;
                }
                else
                {
                    len = 0;
                }
                count += printf(" ");
                while(len--)
                {
                    count += printf("=");
                }
                count += printf("  %s  ", o->configfile_option);
                while(count < GLOBUS_GFS_HELP_WIDTH - 1)
                {
                    count += printf("=");
                }
                printf("\n\n");
                continue;
            }
            else if(o->usage == NULL && o->short_usage == NULL)
            {
                continue;
            }
           
            switch(o->type)
            {
                case GLOBUS_L_GFS_CONFIG_BOOL:
                    value = ""; 
                    sprintf(defval, "%s", o->int_value ? "TRUE" : "FALSE");
                    break;
                case GLOBUS_L_GFS_CONFIG_INT:
                    value = "<number>"; 
                    if(o->int_value > 0) 
                    {
                        sprintf(defval, "%d", o->int_value);
                    }
                    else
                    {
                        sprintf(defval, "%s", "not set");
                    }
                    break;
                case GLOBUS_L_GFS_CONFIG_STRING:
                    value = "<string>"; 
                    sprintf(defval, "%s", 
                        o->value ? (char *) o->value : "not set");
                    break;
                default:
                    value = ""; 
                    sprintf(defval, "%s", "not set");
                    break;
            }
            
            if(o->short_cmdline_option)
            {
                count += printf(" -%s", o->short_cmdline_option);
            }
            if(o->long_cmdline_option)
            {
                count += printf(" -%s", o->long_cmdline_option);
            }
            if(o->expected_val)
            {
                count += printf(" %s", o->expected_val);
            }
            if(value)
            {
                count += printf(" %s   ", value);
            }
            usage = o->short_usage ? o->short_usage : o->usage;
            globus_l_gfs_config_format_line(
                usage, GLOBUS_GFS_HELP_ROWS, GLOBUS_GFS_HELP_COLS, linebuffer);

            len = strlen(linebuffer);
            for(row = 0; row < GLOBUS_GFS_HELP_ROWS && 
                linebuffer[row * GLOBUS_GFS_HELP_COLS]; 
                row++)
            {
                if(row == 1)
                {
                    count += printf("  Default: %s ", defval);
                }
                for(col = count; 
                    col < GLOBUS_GFS_HELP_WIDTH - GLOBUS_GFS_HELP_COLS; 
                    col++)
                {
                    printf(" ");
                }
                count = 0;

                printf("%s", &linebuffer[row * GLOBUS_GFS_HELP_COLS]);
                printf("\n");
            }
            if(row == 1)
            {
                    count += printf("  Default: %s \n", defval);
            }
            printf("\n");
        }
        printf("\nAny FLAG can be negated by prepending '-no-' or '-n' to the "
            "command line option.\n\n");
        printf("Check the GridFTP section at http://www.globus.org/toolkit/docs/\n"
            "for more in-depth documentation.\n\n");
    }

    GlobusGFSDebugExit();
}

globus_result_t
globus_i_gfs_config_hostname_to_address_string(
    char *                              hostname,
    char *                              out_buf,
    int                                 out_buf_len)
{
    globus_addrinfo_t                   hints;
    globus_addrinfo_t *                 addrinfo;
    globus_result_t                     result;
    
    memset(&hints, 0, sizeof(globus_addrinfo_t));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    result = globus_libc_getaddrinfo(hostname, NULL, &hints, &addrinfo);
    if(result != GLOBUS_SUCCESS || addrinfo == NULL || 
        addrinfo->ai_addr == NULL)
    {
        goto error_exit;
    }
    result = globus_libc_getnameinfo(
        (const globus_sockaddr_t *) addrinfo->ai_addr,
        out_buf,
        out_buf_len,
        NULL,
        0,
        GLOBUS_NI_NUMERICHOST);
    if(result != GLOBUS_SUCCESS)
    {
        globus_libc_freeaddrinfo(addrinfo);
        goto error_exit;
    }       
    globus_libc_freeaddrinfo(addrinfo);    
    
    return GLOBUS_SUCCESS;
    
error_exit:
    return result;
}

static
void
globus_l_gfs_config_adjust_path(
    char *                              opt_name,
    globus_bool_t                       free_old)
{
    char *                              val;
    char *                              new_val;
    char *                              base_path;
    GlobusGFSName(globus_l_gfs_config_adjust_path);
    GlobusGFSDebugEnter();

#ifndef WIN32
    val = globus_i_gfs_config_string(opt_name);

    if(val && *val != '/' && *val != '$')
    {
        base_path = globus_i_gfs_config_string("config_base_path");
        new_val = globus_common_create_string("%s/%s", base_path, val);
        globus_l_gfs_config_set(opt_name, free_old, new_val);
    }
#endif

    GlobusGFSDebugExit();
}

static
globus_result_t
globus_l_gfs_config_misc()
{
    globus_list_t *                     module_list = NULL;
    globus_list_t *                     popen_list = NULL;
    char *                              module;
    char *                              ptr;
    int                                 rc;
    char *                              value;
    char *                              data;
    globus_result_t                     result;
    char                                ipaddr[256];
    char *                              default_banner;
    char *                              toolkit_version;
    GlobusGFSName(globus_l_gfs_config_misc);
    GlobusGFSDebugEnter();

    globus_l_gfs_config_adjust_path("chdir_to", 1);
    globus_l_gfs_config_adjust_path("chroot_path", 1);
    globus_l_gfs_config_adjust_path("pw_file", 1);
    globus_l_gfs_config_adjust_path("log_single", 1);
    globus_l_gfs_config_adjust_path("log_unique", 1);
    globus_l_gfs_config_adjust_path("log_transfer", 1);
    globus_l_gfs_config_adjust_path("banner_file", 1);
    globus_l_gfs_config_adjust_path("login_msg_file", 1);
    globus_l_gfs_config_adjust_path("pidfile", 1);
    globus_l_gfs_config_adjust_path("ipc_credential", 1);

#ifdef WIN32
    globus_l_gfs_config_set("fork", GLOBUS_FALSE, NULL);
#endif    

    if(globus_i_gfs_config_bool("detach") && 
        !globus_i_gfs_config_bool("daemon"))
    {
        globus_l_gfs_config_set("daemon", GLOBUS_TRUE, NULL);
    }
    if(!globus_i_gfs_config_bool("fork"))
    {
        globus_l_gfs_config_set("detach", GLOBUS_FALSE, NULL);
        globus_l_gfs_config_set("daemon", GLOBUS_FALSE, NULL);
        globus_l_gfs_config_set("chdir", GLOBUS_FALSE, NULL);
    }

    if(globus_i_gfs_config_bool("ssh"))
    {
        globus_l_gfs_config_set("inetd", GLOBUS_TRUE, NULL);
        globus_l_gfs_config_set("allow_anonymous", GLOBUS_TRUE, NULL);
        globus_l_gfs_config_set("anonymous_names_allowed", 0, "*");
    }

    if(globus_i_gfs_config_bool("inetd"))
    {
        globus_l_gfs_config_set("single", GLOBUS_TRUE, NULL);
        globus_l_gfs_config_set("daemon", GLOBUS_FALSE, NULL);
        globus_l_gfs_config_set("detach", GLOBUS_FALSE, NULL);
    }

    if(globus_i_gfs_config_bool("data_node"))
    {
        globus_l_gfs_config_set("hybrid", GLOBUS_FALSE, NULL);
    }

    if(globus_i_gfs_config_bool("debug"))
    {
        globus_l_gfs_config_set("daemon", GLOBUS_FALSE, NULL);
        globus_l_gfs_config_set("detach", GLOBUS_FALSE, NULL);
        globus_l_gfs_config_set("fork", GLOBUS_FALSE, NULL);
        globus_l_gfs_config_set("bad_signal_exit", GLOBUS_FALSE, NULL);
        globus_l_gfs_config_set("chdir", GLOBUS_FALSE, NULL);
        if((value = globus_i_gfs_config_string("log_module")) == NULL)
        {
            globus_l_gfs_config_set(
                "log_module", 0, globus_libc_strdup("stdio:buffer=0"));
        }
        else if(strchr(value, ':') == NULL)
        {
            globus_l_gfs_config_set(
                "log_module", 
                0, 
                globus_common_create_string("%s:buffer=0", value));
            globus_free(value);
        }
    }

    if(globus_i_gfs_config_bool("allow_anonymous"))
    {
        globus_l_gfs_config_set("secure_ipc", GLOBUS_FALSE, NULL);
    }
    if(globus_i_gfs_config_bool("longhelp"))
    {
        globus_l_gfs_config_set("help", GLOBUS_TRUE, NULL);
    }

    /* use ipc_interface and ipc_port if a listening data node */
    if(globus_i_gfs_config_string("remote_nodes") == NULL &&
        globus_i_gfs_config_bool("data_node"))
    {
        int                             port;
        char *                          iface;

        port = globus_i_gfs_config_int("ipc_port");
        if(port > 0)
        {
            globus_l_gfs_config_set("port", port, NULL);
        }

        iface = globus_i_gfs_config_string("ipc_interface");
        if(iface)
        {
            globus_l_gfs_config_set(
                "control_interface", 0, globus_libc_strdup(iface));
        }
    }
    
    if((value = 
        globus_i_gfs_config_string("control_interface")) != GLOBUS_NULL)
    {        
        memset(ipaddr, 0, sizeof(ipaddr));
        result = globus_i_gfs_config_hostname_to_address_string(
            value, ipaddr, sizeof(ipaddr));  
        if(result != GLOBUS_SUCCESS)
        {   
            goto error_exit;
        }
        globus_l_gfs_config_set(
            "control_interface", 1, globus_libc_strdup(ipaddr));
    }

    if((value = 
        globus_i_gfs_config_string("data_interface")) != GLOBUS_NULL)
    {        
        memset(ipaddr, 0, sizeof(ipaddr));
        result = globus_i_gfs_config_hostname_to_address_string(
            value, ipaddr, sizeof(ipaddr));  
        if(result != GLOBUS_SUCCESS)
        {   
            goto error_exit;
        }
        globus_l_gfs_config_set(
            "data_interface", 1, globus_libc_strdup(ipaddr));
    }

    if((value = globus_i_gfs_config_string("hostname")) != GLOBUS_NULL)
    {
        globus_l_gfs_config_set("fqdn", 0, globus_libc_strdup(value));
        
        memset(ipaddr, 0, sizeof(ipaddr));
        result = globus_i_gfs_config_hostname_to_address_string(
            value, ipaddr, sizeof(ipaddr));  
        if(result != GLOBUS_SUCCESS)
        {   
            goto error_exit;
        }
                      
        if(globus_i_gfs_config_string("control_interface") == NULL)
        {
            globus_l_gfs_config_set(
                "control_interface", 0, globus_libc_strdup(ipaddr));
        }
        if(globus_i_gfs_config_string("data_interface") == NULL)
        {
            globus_l_gfs_config_set(
                "data_interface", 0, globus_libc_strdup(ipaddr));
        }
    }
    else
    {
        char *                          hostname;
        hostname = globus_malloc(1024);
        globus_libc_gethostname(hostname, 1024);
        globus_l_gfs_config_set("fqdn", 0, globus_libc_strdup(hostname));
        globus_free(hostname);
    }            

    if((value = globus_i_gfs_config_string("version_tag")) != NULL)
    {
        toolkit_version = 
            globus_common_create_string("%s %s", toolkit_id, value);
    }
    else
    {
        toolkit_version = globus_libc_strdup(toolkit_id);
    }
        
    default_banner = globus_common_create_string(
        "%s GridFTP Server %d.%d (%s, %d-%d) [%s] ready.",
        globus_i_gfs_config_string("fqdn"),
        local_version.major,
        local_version.minor,
        build_flavor,
        local_version.timestamp,
        local_version.branch_id,
        toolkit_version);
        
    data = NULL;
    if(globus_i_gfs_config_bool("banner_terse"))
    {
        data = globus_libc_strdup("");                
    }
    else if((value = globus_i_gfs_config_string("banner_file")) != GLOBUS_NULL)
    {
        rc = globus_l_config_loadfile(value, &data);
    }
    else if((value = globus_i_gfs_config_string("banner")) != GLOBUS_NULL)
    {
        data = globus_libc_strdup(value);
    }
    
    if(data != NULL)
    {
        if(globus_i_gfs_config_bool("banner_append"))
        {
            char *                      banner;
            banner = 
                globus_common_create_string("%s %s", default_banner, data);
            globus_free(data);
            data = banner;
        }
        globus_free(default_banner);
    }
    else
    {
        data = default_banner;
    }
    globus_l_gfs_config_set("banner", 1, data);                
       
    data = globus_common_create_string(
            "%d.%d (%s, %d-%d) [%s]",
            local_version.major,
            local_version.minor,
            build_flavor,
            local_version.timestamp,
            local_version.branch_id,
            toolkit_version);
    globus_l_gfs_config_set("version_string", 0, data);
    globus_free(toolkit_version);

    if((value = globus_i_gfs_config_string("login_msg_file")) != GLOBUS_NULL)
    {
        rc = globus_l_config_loadfile(value, &data);
        globus_l_gfs_config_set("login_msg", 0, data);                
    }

    value = globus_i_gfs_config_string("load_dsi_module");
    if(value != NULL)
    {
        char *                          ptr;
        
        ptr = strchr(value, ':');
        if(ptr)
        {
            /* changing the value in the table directly here, but we'll set it
             * anyways to be clear that we are */
            *ptr = '\0';
            ptr++;
            globus_l_gfs_config_set(
                "load_dsi_module", 0, value);
            globus_l_gfs_config_set(
                "dsi_options", 0, globus_libc_strdup(ptr));
        }
    }

    value = globus_i_gfs_config_string("remote_nodes");
    {
        if(value)
        {
            if(globus_i_gfs_config_string("load_dsi_module") == NULL &&
                !globus_i_gfs_config_bool("data_node") && 
                !globus_i_gfs_config_bool("hybrid"))
            {
                globus_l_gfs_config_set(
                    "load_dsi_module", 0, globus_libc_strdup("remote"));    
            }
            
            /* if stripe_count wasn't set, set it to the number of 
             * nodes configured */
            if(globus_i_gfs_config_int("stripe_count") == -1)
            {
                int                         node_count = 1;
                char *                      ptr;
                
                ptr = value;
                while(ptr && *ptr && (ptr = strchr(ptr, ',')) != NULL)
                {
                    ptr++;
                    node_count++;
                }            
                globus_l_gfs_config_set("stripe_count", node_count, NULL);
            }
        }
        else
        {
            /* if no nodes configured and stripe_count not set, set it to 1 */
            if(globus_i_gfs_config_int("stripe_count") == -1)
            {
                globus_l_gfs_config_set("stripe_count", 1, NULL);
            }
        }
                       
    }
    
    value = globus_libc_strdup(globus_i_gfs_config_string("popen_whitelist"));
    if(value != NULL)
    {
        module = value;
        while((ptr = strchr(module, ',')) != NULL)
        {
            *ptr = '\0';
            globus_list_insert(&popen_list, globus_libc_strdup(module)); 
            module = ptr + 1;
        }
        if(ptr == NULL)
        {
            globus_list_insert(&popen_list, globus_libc_strdup(module)); 
        }               
        globus_free(value);             
    }
    globus_l_gfs_config_set("popen_list", 0, popen_list);   

    if(globus_i_gfs_config_string("load_dsi_module") == NULL)
    {
        globus_l_gfs_config_set("load_dsi_module", 0, globus_libc_strdup("file"));    
    } 

    value = globus_libc_strdup(globus_i_gfs_config_string("allowed_modules"));
    if(value != NULL)
    {
        module = value;
        while((ptr = strchr(module, ',')) != NULL)
        {
            *ptr = '\0';
            globus_list_insert(&module_list, globus_libc_strdup(module)); 
            module = ptr + 1;
        }
        if(ptr == NULL)
        {
            globus_list_insert(&module_list, globus_libc_strdup(module)); 
        }               
        globus_free(value);             
    }
    globus_l_gfs_config_set("module_list", 0, module_list);   
    
    /* if auth_level is -1 it means it has not yet been touched */
    switch(globus_i_gfs_config_int("auth_level"))
    {
        case -1:
            if(globus_i_gfs_config_bool("data_node"))
            {
                globus_l_gfs_config_set("auth_level", 1, NULL);
            }
            else
            {
                globus_l_gfs_config_set("auth_level", 3, NULL);
            }
            break;
        case 2:
            globus_l_gfs_config_set("auth_level", 3, NULL);
            break;
        case 4:
            globus_l_gfs_config_set("auth_level", 5, NULL);
            break;
        case 6:
            globus_l_gfs_config_set("auth_level", 7, NULL);
            break;
        default:
            break;
    }

    /* make sure root running process that does not fork can only run
        once */
    if(!globus_i_gfs_config_bool("daemon") && getuid() == 0)
    {
        globus_l_gfs_config_set("connections_max", 1, NULL);
        globus_l_gfs_config_set("single", 1, NULL);
    }

    if(globus_i_gfs_config_string("usage_stats_target") != NULL &&
        globus_i_gfs_config_bool("disable_usage_stats"))
    {
        globus_l_gfs_config_set("disable_usage_stats", GLOBUS_FALSE, NULL);        
    }
    
    if(globus_i_gfs_config_bool("data_node") &&
        !globus_i_gfs_config_bool("disable_usage_stats"))
    {
        globus_l_gfs_config_set("disable_usage_stats", GLOBUS_TRUE, NULL);        
    }

    if(globus_i_gfs_config_string("remote_nodes") != NULL &&
        globus_i_gfs_config_bool("data_node"))
    {
        char *                          str;

        /* XXX: not sure about this.  Perhaps it can connect back after
            forking, tho that how that would work is awkward, ie
            when would it fork */
        if(globus_i_gfs_config_bool("fork"))
        {
            /* should log an error */
            globus_l_gfs_config_set("fork", GLOBUS_FALSE, NULL);   
        }

        /* set the convenience conf opt */
        globus_l_gfs_config_set("data_node_client", GLOBUS_TRUE, NULL);

        /* if allow from not set for ipc specific, pull it from regular */
        str = globus_i_gfs_config_string("ipc_allow_from");
        if(str == NULL)
        {
            str = globus_i_gfs_config_string("allow_from");
            globus_l_gfs_config_set("ipc_allow_from", 0, str);
        }
        str = globus_i_gfs_config_string("ipc_deny_from");
        if(str == NULL)
        {
            str = globus_i_gfs_config_string("deny_from");
            globus_l_gfs_config_set("ipc_deny_from", 0, str);
        }
    }
    
    if(globus_i_gfs_config_string("remote_nodes") && 
        !globus_i_gfs_config_bool("data_node") && 
        globus_i_gfs_config_string("ipc_subject"))
    {
        char *                          ipc_dn;
        gss_buffer_desc                 buf;
        OM_uint32                       min_stat;
        OM_uint32                       maj_stat;
        gss_name_t                      cred_name;
        gss_cred_id_t                   cred;
            
        ipc_dn = globus_i_gfs_config_string("ipc_subject");

        if(strcasecmp(ipc_dn, "auto") == 0)
        {
            cred_name = GSS_C_NO_NAME;
        }
        else
        {
            buf.value = ipc_dn;
            buf.length = strlen(ipc_dn);
            maj_stat = gss_import_name(
                &min_stat,
                &buf,
                GSS_C_NT_USER_NAME,
                &cred_name);
            if(maj_stat != GSS_S_COMPLETE || cred_name == GSS_C_NO_NAME)
            {
                result = min_stat;
                goto error_exit;
            }
        }

        maj_stat = gss_acquire_cred(
            &min_stat,
            cred_name,
            0,
            GSS_C_NULL_OID_SET,
            GSS_C_INITIATE,
            &cred,
            NULL,
            NULL);
        if(maj_stat != GSS_S_COMPLETE)
        {
            result = min_stat;
            goto error_exit;
        }
        globus_l_gfs_config_set("ipc_cred", 0, cred);            
    }
    
    else if(globus_i_gfs_config_string("remote_nodes") && 
        !globus_i_gfs_config_bool("data_node") && 
        globus_i_gfs_config_string("ipc_credential"))
    {
        char *                          cred_file;
        gss_buffer_desc                 buf;
        OM_uint32                       min_stat;
        OM_uint32                       maj_stat;
        gss_cred_id_t                   cred;
        
        cred_file = globus_i_gfs_config_string("ipc_credential");

        buf.value = globus_common_create_string(
            "X509_USER_PROXY=%s", cred_file);
        buf.length = strlen(buf.value);
    
        maj_stat = gss_import_cred(
            &min_stat,
            &cred,
            GSS_C_NO_OID,
            1, /* GSS_IMPEXP_MECH_SPECIFIC */
            &buf,
            0,
            NULL);
        if(maj_stat != GSS_S_COMPLETE)
        {
            result = min_stat;
            goto error_exit;
        }
    
        globus_free(buf.value);
            
        globus_l_gfs_config_set("ipc_cred", 0, cred);            
    }
        
    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;
    
error_exit:
    GlobusGFSDebugExitWithError();
    return result;
}
    

/* logging is available in this func */
void
globus_i_gfs_config_post_init()
{
    GlobusGFSName(globus_l_gfs_config_misc);
    GlobusGFSDebugEnter();

    if(globus_i_gfs_config_bool("allow_udt") && globus_l_gfs_num_threads < 1)
    {
        globus_gfs_log_message(GLOBUS_GFS_LOG_WARN, 
            "Disabling UDT: threads must be enabled for UDT to function.\n");

        globus_l_gfs_config_set("allow_udt", GLOBUS_FALSE, NULL);
    }

    GlobusGFSDebugExit();
}

/**
 * load configuration.  read from defaults, file, env, and command line 
 * arguments. each overriding the other.
 * this function will log error messages and exit the server if any
 * errors occur.
 * XXX need to allow config errors to log to syslog, stderr, etc
 */
 
int
globus_i_gfs_config_init_envs(
    int                                 argc,
    char **                             argv)
{
    char *                              tmp_str;
    char *                              local_config_file;
    char *                              global_config_file;
    int                                 cmdline_config = 0;
    int                                 arg_num;
    char *                              argp;
    char **                             tmp_argv;
    int                                 rc;
    char *                              cwd_str;
    char *                              base_str = NULL;
    char *                              conf_dir = NULL;

    if(argv == NULL)
    {
        tmp_argv = malloc(2 * sizeof(char *));
        tmp_argv[0] = "globus-gridftp-server";
        tmp_argv[1] = NULL;
    }
    else
    {
        tmp_argv = argv;
    }
    
    GLOBUS_GSI_SYSCONFIG_GET_CURRENT_WORKING_DIR(&cwd_str); 

    global_config_file = "/etc/grid-security/gridftp.conf";
    local_config_file = NULL;
    conf_dir = NULL;

    for(arg_num = 0; arg_num < argc; arg_num++)
    {
        argp = tmp_argv[arg_num];
        while(*argp == '-')
        {
            argp++;
        }
        if(argp[0] == 'c' && argp[1] == '\0' 
            && tmp_argv[arg_num + 1])
        {
            local_config_file = strdup(tmp_argv[arg_num + 1]);
            arg_num++;
            cmdline_config = 1;
        }
        else if(argp[0] == 'C' && argp[1] == '\0' && 
            tmp_argv[arg_num + 1])
        {
            conf_dir = strdup(tmp_argv[arg_num + 1]);
            arg_num++;
        }
        else if(!strcmp(argp, "config-base-path") && tmp_argv[arg_num + 1])
        {
            base_str = strdup(tmp_argv[arg_num + 1]);
            arg_num++;
        }
        else if(!strcmp(argp, "threads") && tmp_argv[arg_num + 1])
        {            
            globus_l_gfs_num_threads = atoi(tmp_argv[arg_num + 1]);
            arg_num++;
        }
        else if(!strcmp(argp, "inetd") || !strcmp(argp, "debug") ||
                !strcmp(argp, "i") || !strcmp(argp, "ssh") || 
                !strcmp(argp, "no-fork"))
        {            
            globus_l_gfs_is_worker = GLOBUS_TRUE;
        }
        else if(!strcmp(argp, "port-range") && tmp_argv[arg_num + 1])
        {
            /* save arg and set after file is loaded */
            globus_l_gfs_port_range = tmp_argv[arg_num + 1];
            arg_num++;
        }
    }

    if(local_config_file == NULL)
    {
        char *                          tmp_gl;
        
        tmp_str = malloc(PATH_MAX);
        tmp_gl = getenv("GLOBUS_LOCATION");
        if(tmp_gl)
        {
            rc = snprintf(tmp_str, PATH_MAX, "%s/etc/gridftp.conf", tmp_gl);
            if(rc > 0)
            {
                local_config_file = tmp_str;
            }          
        }
    }
    
    if(base_str)
    {
        free(cwd_str);
        cwd_str = NULL;
    }
    else
    {
        base_str = cwd_str;
        cwd_str = NULL;
    }

    if(conf_dir != NULL)
    {
        if(*conf_dir != '/')
        {
            tmp_str = malloc(PATH_MAX);
            rc = snprintf(
                tmp_str, PATH_MAX, "%s/%s", base_str, conf_dir);
            globus_free(conf_dir);
            conf_dir = tmp_str;
        }
        rc = globus_l_gfs_config_load_config_dir(conf_dir, GLOBUS_TRUE);
        if(rc < 0)
        {
            goto error;
        }
    }

    if(local_config_file != NULL)
    {
        if(*local_config_file != '/')
        {
            tmp_str = malloc(PATH_MAX);
            rc = snprintf(
                tmp_str, PATH_MAX, "%s/%s", base_str, local_config_file);
            if(rc > 0)
            {
                free(local_config_file);
                local_config_file = tmp_str;
            }
        }
        rc = globus_l_gfs_config_load_envs_from_file(local_config_file);
        if(rc == -2 && !cmdline_config)
        {
            rc = globus_l_gfs_config_load_envs_from_file(global_config_file);
        }
        if(rc == -1)
        {
            goto error;
        }
    }
    else if(!cmdline_config)
    {
        rc = globus_l_gfs_config_load_envs_from_file(global_config_file);
    }
    
    if(globus_l_gfs_port_range)
    {
        setenv("GLOBUS_TCP_PORT_RANGE", globus_l_gfs_port_range, 1);
        setenv("GLOBUS_UDP_PORT_RANGE", globus_l_gfs_port_range, 1);
    }

    /* only enable threads for real process, not daemon */
    if(globus_l_gfs_num_threads > 0 && globus_l_gfs_is_worker)
    {
        char                            nthreads[8];
        snprintf(nthreads, sizeof(nthreads), "%d", globus_l_gfs_num_threads);
        setenv("GLOBUS_CALLBACK_POLLING_THREADS", nthreads, 1);
        globus_thread_set_model("pthread");
    }
    
    if(local_config_file != NULL)
    {
        free(local_config_file);
    }
    if(argv == NULL)
    {
        free(tmp_argv);
    }
    if(cwd_str == NULL)
    {
        free(base_str);
    }
    
    return 0;

error:
    return -1;
}

int
globus_i_gfs_config_init(
    int                                 argc,
    char **                             argv,
    globus_bool_t                       argv_only)
{
    char *                              tmp_str;
    char *                              exec_name;
    char *                              conf_dir;
    char *                              local_config_file;
    char *                              global_config_file;
    globus_bool_t                       cmdline_config = GLOBUS_FALSE;
    int                                 arg_num;
    char *                              argp;
    char **                             tmp_argv;
    int                                 rc;
    globus_result_t                     result;
    char *                              cwd_str;
    char *                              base_str = NULL;
    GlobusGFSName(globus_i_gfs_config_init);
    GlobusGFSDebugEnter();
    
    globus_l_gfs_common_loaded = GLOBUS_TRUE;
    
    globus_hashtable_init(
        &option_table,
        256,
        globus_hashtable_string_hash,
        globus_hashtable_string_keyeq);

    if(argv == NULL)
    {
        tmp_argv = globus_malloc(2 * sizeof(char *));
        tmp_argv[0] = "globus-gridftp-server";
        tmp_argv[1] = NULL;
    }
    else
    {
        tmp_argv = argv;
    }
   
    GLOBUS_GSI_SYSCONFIG_GET_CURRENT_WORKING_DIR(&cwd_str); 
    
    exec_name = tmp_argv[0];
    /* set default exe name */
    globus_location(&tmp_str);
    if(tmp_str)
    {
        exec_name = globus_common_create_string(
         "%s/sbin/globus-gridftp-server",
         tmp_str);
        free(tmp_str);
    }
    else if(exec_name[0] != '/' && strchr(exec_name, '/') != NULL)
    {
        exec_name = globus_common_create_string(
         "%s/%s", cwd_str, exec_name);
    }
    else
    {
        exec_name = globus_libc_strdup(tmp_argv[0]);
    }

    global_config_file = "/etc/grid-security/gridftp.conf";
    local_config_file = NULL;
    conf_dir = NULL;
    
    for(arg_num = 0; arg_num < argc; arg_num++)
    {
        argp = tmp_argv[arg_num];
        if(*argp != '-')
        {
            continue;
        }
        while(*argp == '-')
        {
            argp++;
        }
        
        if(argp[0] == 'c' && argp[1] == '\0' && tmp_argv[arg_num + 1])
        {
            local_config_file = globus_libc_strdup(tmp_argv[arg_num + 1]);
            cmdline_config = GLOBUS_TRUE;
            arg_num++;
            continue;
        }
        
        if(argp[0] == 'C' && argp[1] == '\0' && tmp_argv[arg_num + 1])
        {
            conf_dir = globus_libc_strdup(tmp_argv[arg_num + 1]);
            arg_num++;
            continue;
        }

        if(!strcmp(argp, "config-base-path") && tmp_argv[arg_num + 1])
        {
            base_str = globus_libc_strdup(tmp_argv[arg_num + 1]);
            continue;
        }
    }
    
    if(local_config_file == NULL && !argv_only)
    {
        globus_eval_path("${sysconfdir}/gridftp.conf", &local_config_file);
    }

    globus_l_gfs_config_load_defaults();
        
    if(base_str)
    {
        globus_free(cwd_str);
        cwd_str = NULL;
    }
    else
    {
        base_str = cwd_str;
        globus_l_gfs_config_set("config_base_path", 0, cwd_str);
    }

    if(conf_dir != NULL)
    {
        if(*conf_dir != '/')
        {
            tmp_str = globus_common_create_string(
                "%s/%s", base_str, conf_dir);
            globus_free(conf_dir);
            conf_dir = tmp_str;
        }
        rc = globus_l_gfs_config_load_config_dir(conf_dir, GLOBUS_FALSE);
        if(rc < 0)
        {
            goto error;
        }
    }
    
    if(local_config_file != NULL)
    {
        if(*local_config_file != '/')
        {
            tmp_str = globus_common_create_string(
                "%s/%s", base_str, local_config_file);
            globus_free(local_config_file);
            local_config_file = tmp_str;
        }
        rc = globus_l_gfs_config_load_config_file(local_config_file);
        if(rc == -2 && !cmdline_config)
        {
            rc = globus_l_gfs_config_load_config_file(global_config_file);
        }
        if(rc == -1)
        {
            goto error;
        }
    }
    else if(!cmdline_config)
    {
        rc = globus_l_gfs_config_load_envs_from_file(global_config_file);
    }
        
    if(!argv_only)
    {
        globus_l_gfs_config_load_config_env();
    }
    rc = globus_l_gfs_config_load_commandline(argc, tmp_argv);
    if(rc == -1)
    {
        goto error;
    }
    
    result = globus_l_gfs_config_misc();
    if(result != GLOBUS_SUCCESS)
    {
        globus_gfs_log_exit_message("Error in post config setup:\n %s", 
            globus_error_print_friendly(globus_error_peek(result)));
        goto error;
    }
    
    globus_l_gfs_config_set("exec_name", 0, exec_name);
    globus_l_gfs_config_set("argv", 0, tmp_argv);
    globus_l_gfs_config_set("argc", argc, NULL);

    if(local_config_file != NULL)
    {
        globus_free(local_config_file);
    }
    if(argv == NULL)
    {
        globus_free(tmp_argv);
    }
    if(cwd_str == NULL)
    {
        globus_free(base_str);
    }
    
    globus_mutex_init(&globus_i_gfs_config_mutex, NULL);

    GlobusGFSDebugExit();
    return 0;

error:
    return -1;
}


int
globus_i_gfs_config_int(
    const char *                        option_name)
{
    globus_l_gfs_config_option_t *      option;
    int                                 value = 0;    
    GlobusGFSName(globus_i_gfs_config_int);
    GlobusGFSDebugEnter();
    
    option = (globus_l_gfs_config_option_t *) 
        globus_hashtable_lookup(&option_table, (void *) option_name);
        
    if(option)
    {        
        value = option->int_value;
    }

    GlobusGFSDebugExit();
    return value;
}


void *
globus_i_gfs_config_get(
    const char *                        option_name)
{
    globus_l_gfs_config_option_t *      option;
    void *                              value = NULL;    
    GlobusGFSName(globus_i_gfs_config_get);
    GlobusGFSDebugEnter();
    
    option = (globus_l_gfs_config_option_t *) 
        globus_hashtable_lookup(&option_table, (void *) option_name);
        
    if(option && option->value)
    {        
        value = option->value;
    }

    GlobusGFSDebugExit();
    return value;
}

globus_bool_t
globus_i_gfs_config_is_anonymous(
    const char *                        userid)
{
    globus_bool_t                       valid = GLOBUS_FALSE;
    char *                              anonymous_names;
    GlobusGFSName(globus_i_gfs_config_is_anonymous);
    GlobusGFSDebugEnter();

    anonymous_names = globus_i_gfs_config_string("anonymous_names_allowed");
    if(anonymous_names)
    {
        if(*anonymous_names == '*' || strstr(anonymous_names, userid))
        {
            valid = GLOBUS_TRUE;
        }
    }
    else
    {
        if(strcmp(userid, "ftp") == 0 ||
            strcmp(userid, "anonymous") == 0 ||
            strcmp(userid, ":globus-mapping:") == 0)
        {
            valid = GLOBUS_TRUE;
        }
    }
    
    GlobusGFSDebugExit();
    return valid;
}

globus_bool_t
globus_i_gfs_config_allow_addr(
    const char *                        remote_addr,
    globus_bool_t                       ipc)
{
    char *                              allow_list;
    char *                              deny_list;
    globus_bool_t                       allowed = GLOBUS_FALSE;
    char *                              addr;
    char *                              ptr;
    GlobusGFSName(globus_i_gfs_config_allow_addr);
    GlobusGFSDebugEnter();

    if(ipc)
    {
        allow_list = globus_libc_strdup(
            globus_i_gfs_config_string("ipc_allow_from"));
        deny_list = globus_libc_strdup(
            globus_i_gfs_config_string("ipc_deny_from"));
    }
    else
    { 
        allow_list = globus_libc_strdup(
            globus_i_gfs_config_string("allow_from"));
        deny_list = globus_libc_strdup(
            globus_i_gfs_config_string("deny_from"));
    }

    if(allow_list == NULL)
    {
        allowed = GLOBUS_TRUE;
    }
    else
    {
        addr = allow_list;
        while((ptr = strchr(addr, ',')) != NULL && !allowed)
        {
            *ptr = '\0';
            if(strncmp(addr, remote_addr, strlen(addr)) == 0)
            {
                allowed = GLOBUS_TRUE;
            }
            addr = ptr + 1;
        }
        if(ptr == NULL && !allowed)
        {
           if(strncmp(addr, remote_addr, strlen(addr)) == 0)
            {
                allowed = GLOBUS_TRUE;
            }
        }
        globus_free(allow_list);
    }
    if(allowed && deny_list != NULL)
    {
        addr = deny_list;
        while((ptr = strchr(addr, ',')) != NULL && allowed)
        {
            *ptr = '\0';
            if(strncmp(addr, remote_addr, strlen(addr)) == 0)
            {
                allowed = GLOBUS_FALSE;
            }
            addr = ptr + 1;
        }
        if(ptr == NULL && allowed)
        {
           if(strncmp(addr, remote_addr, strlen(addr)) == 0)
            {
                allowed = GLOBUS_FALSE;
            }
        }
        globus_free(deny_list);
    }

    GlobusGFSDebugExit();
    return allowed;
}


const char *
globus_i_gfs_config_get_module_name(
    const char *                        client_alias)
{
    globus_list_t *                     module_list;
    globus_list_t *                     list;
    const char *                        module = NULL;
    const char *                        out_module = NULL;
    char *                              alias;
    globus_bool_t                       found = GLOBUS_FALSE;
    int                                 size;
    GlobusGFSName(globus_i_gfs_config_get_module_name);
    GlobusGFSDebugEnter();

    module_list = (globus_list_t *) globus_i_gfs_config_get("module_list");  
    for(list = module_list;
        !globus_list_empty(list) && !found;
        list = globus_list_rest(list))
    {
        /* parse out module name from <module> or <alias>:<module> */
        alias = (char *) globus_list_first(list);
        module = strchr(alias, ':');
        if(module != NULL)
        {
            size = module - alias;
            module++;
        }
        else
        {
            size = strlen(alias);
            module = alias;
        }
        if(strncasecmp(alias, client_alias, size) == 0)
        {
            found = GLOBUS_TRUE;
        }
    } 
    if(found)
    {
        out_module = module;
    }

    GlobusGFSDebugExit();
    return out_module;
}


/*
 *  public (ish) functions
 */
globus_bool_t
globus_gfs_config_get_bool(
    const char *                        option_name)
{
    globus_bool_t                       rc;

    globus_mutex_lock(&globus_i_gfs_config_mutex);
    rc = globus_i_gfs_config_bool(option_name);    
    globus_mutex_unlock(&globus_i_gfs_config_mutex);
    return rc;
}

char *
globus_gfs_config_get_string(
    const char *                        option_name)
{
    char *                              rc;

    globus_mutex_lock(&globus_i_gfs_config_mutex);
    rc = globus_i_gfs_config_string(option_name);    
    globus_mutex_unlock(&globus_i_gfs_config_mutex);
    return rc;
}

globus_list_t *
globus_gfs_config_get_list(
    const char *                        option_name)
{
    globus_list_t *                     rc;

    globus_mutex_lock(&globus_i_gfs_config_mutex);
    rc = globus_i_gfs_config_list(option_name);    
    globus_mutex_unlock(&globus_i_gfs_config_mutex);
    return rc;
}

void *
globus_gfs_config_get(
    const char *                        option_name)
{
    void *                              rc;

    globus_mutex_lock(&globus_i_gfs_config_mutex);
    rc = globus_i_gfs_config_get(option_name);    
    globus_mutex_unlock(&globus_i_gfs_config_mutex);
    return rc;
}

int
globus_gfs_config_get_int(
    const char *                        option_name)
{
    int                                 rc;

    globus_mutex_lock(&globus_i_gfs_config_mutex);
    rc = globus_i_gfs_config_int(option_name);    
    globus_mutex_unlock(&globus_i_gfs_config_mutex);
    return rc;
}

static
void
globus_l_gfs_config_publish_int(
    globus_list_t *                     list,
    char *                              option_name,
    int                                 int_val)
{
    globus_i_gfs_config_set_int_cb_t    cb;
    globus_i_gfs_config_option_cb_ent_t * cb_ent;

    while(!globus_list_empty(list))
    {
        cb_ent = (globus_i_gfs_config_option_cb_ent_t *)
            globus_list_first(list);

        list = globus_list_rest(list);
        if(cb_ent->enabled)
        {
            cb = (globus_i_gfs_config_set_int_cb_t)cb_ent->cb;
            cb_ent->enabled = GLOBUS_FALSE;
            cb(option_name, int_val, cb_ent->user_arg);
            cb_ent->enabled = GLOBUS_TRUE;
        }
    }
}

int
globus_gfs_config_set_int(
    char *                              option_name,
    int                                 int_val)
{
    int                                 rc;
    globus_list_t *                     list;
    globus_l_gfs_config_option_t *      option;

    globus_mutex_lock(&globus_i_gfs_config_mutex);
    {
        rc = globus_l_gfs_config_set(option_name, int_val, NULL);
        if(rc != 0)
        {
            goto error;
        }
        option = (globus_l_gfs_config_option_t *)
            globus_hashtable_lookup(&option_table, (void *) option_name);
        if(option == NULL)
        {
            goto error;
        }
        list = option->set_list;
        globus_l_gfs_config_publish_int(list, option_name, int_val);
    }
    globus_mutex_unlock(&globus_i_gfs_config_mutex);

    return 0;
error:
    globus_mutex_unlock(&globus_i_gfs_config_mutex);
    return rc;
}

int
globus_gfs_config_inc_int(
    char *                              option_name,
    int                                 inc_val)
{
    int                                 rc;
    int                                 tmp_i;
    globus_list_t *                     list;
    globus_l_gfs_config_option_t *      option;

    globus_mutex_lock(&globus_i_gfs_config_mutex);
    {
        tmp_i = globus_i_gfs_config_int(option_name);
        tmp_i += inc_val;
        rc = globus_l_gfs_config_set(option_name, tmp_i, NULL);
        if(rc != 0)
        {
            goto error;
        }
        option = (globus_l_gfs_config_option_t *)
            globus_hashtable_lookup(&option_table, (void *) option_name);
        if(option == NULL)
        {
            goto error;
        }
        list = option->set_list;
        globus_l_gfs_config_publish_int(list, option_name, tmp_i);
    }
    globus_mutex_unlock(&globus_i_gfs_config_mutex);

    return 0;
error:
    globus_mutex_unlock(&globus_i_gfs_config_mutex);
    return rc;
}

int
globus_gfs_config_set_bool(
    char *                              option_name,
    int                                 int_val)
{
    globus_list_t *                     list;
    int                                 rc;
    globus_l_gfs_config_option_t *      option;

    globus_mutex_lock(&globus_i_gfs_config_mutex);
    {
        rc = globus_l_gfs_config_set(option_name, int_val, NULL);
        if(rc != 0)
        {
            goto error;
        }
        option = (globus_l_gfs_config_option_t *)
            globus_hashtable_lookup(&option_table, (void *) option_name);
        if(option == NULL)
        {
            goto error;
        }
        list = option->set_list;
        globus_l_gfs_config_publish_int(list, option_name, int_val);
    }
    globus_mutex_unlock(&globus_i_gfs_config_mutex);

    return 0;
error:
    globus_mutex_unlock(&globus_i_gfs_config_mutex);
    return rc;
}

int
globus_gfs_config_set_ptr(
    char *                              option_name,
    void *                              ptr)
{
    globus_i_gfs_config_set_string_cb_t cb;
    globus_i_gfs_config_option_cb_ent_t * cb_ent;
    globus_list_t *                     list;
    int                                 rc;
    globus_l_gfs_config_option_t *      option;
    GlobusGFSName(globus_gfs_config_set_ptr);
    GlobusGFSDebugEnter();

    globus_mutex_lock(&globus_i_gfs_config_mutex);
    {
        rc = globus_l_gfs_config_set(option_name, 0, ptr);
        if(rc != 0)
        {
            goto error;
        }

        option = (globus_l_gfs_config_option_t *)
            globus_hashtable_lookup(&option_table, (void *) option_name);
        if(option == NULL)
        {
            goto error;
        }
        list = option->set_list;

        while(!globus_list_empty(list))
        {
            cb_ent = (globus_i_gfs_config_option_cb_ent_t *)
                globus_list_first(list);
            list = globus_list_rest(list);

            if(cb_ent->enabled)
            {
                cb = (globus_i_gfs_config_set_string_cb_t) cb_ent->cb;
                cb_ent->enabled = GLOBUS_FALSE;
                cb(option_name, ptr, cb_ent->user_arg);
                cb_ent->enabled = GLOBUS_TRUE;
            }
        }
    }
    globus_mutex_unlock(&globus_i_gfs_config_mutex);

    return 0;
error:
    globus_mutex_unlock(&globus_i_gfs_config_mutex);
    return rc;
}

void
globus_gfs_config_enable_cb(
    globus_i_gfs_config_option_cb_ent_t * cb_ent,
    globus_bool_t                       enabled)
{

    globus_mutex_lock(&globus_i_gfs_config_mutex);
    {
        cb_ent->enabled = enabled;
    }
    globus_mutex_unlock(&globus_i_gfs_config_mutex);
}

int
globus_gfs_config_add_cb(
    globus_i_gfs_config_option_cb_ent_t ** cb_handle,
    char *                              option_name,
    void *                              cb,
    void *                              user_arg)
{
    globus_i_gfs_config_option_cb_ent_t * cb_ent;
    globus_l_gfs_config_option_t *      option;
    int                                 rc;
    GlobusGFSName(globus_gfs_config_add_cb);
    GlobusGFSDebugEnter();

    globus_mutex_lock(&globus_i_gfs_config_mutex);
    {
        option = (globus_l_gfs_config_option_t *)
            globus_hashtable_lookup(&option_table, (void *) option_name);
        if(option == NULL)
        {
            rc = -1;
            goto error;
        }
        cb_ent = (globus_i_gfs_config_option_cb_ent_t *)
            globus_calloc(1, sizeof(globus_i_gfs_config_option_cb_ent_t));
        cb_ent->cb = cb;
        cb_ent->enabled = GLOBUS_TRUE;
        cb_ent->user_arg = user_arg;
        *cb_handle = cb_ent;
        globus_list_insert(&option->set_list, cb_ent);
    }
    globus_mutex_unlock(&globus_i_gfs_config_mutex);

    return 0;
error:
    globus_mutex_unlock(&globus_i_gfs_config_mutex);
    return rc;
}

