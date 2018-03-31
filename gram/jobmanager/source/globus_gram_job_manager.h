/*
 * Copyright 1999-2009 University of Chicago
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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

#ifndef GLOBUS_GRAM_JOB_MANAGER_INCLUDE
#define GLOBUS_GRAM_JOB_MANAGER_INCLUDE

/* Includes */
#include "globus_common.h"
#include "globus_gram_protocol.h"
#include "globus_rsl.h"
#include "globus_gass_cache.h"
#include "globus_gsi_credential.h"

/* Defines */

#define GLOBUS_GRAM_AUTHZ_CALLOUT_TYPE "globus_gram_jobmanager_authz"

#ifdef __cplusplus
extern "C" {
#endif

#define GLOBUS_GRAM_JOB_MANAGER_EXPIRATION_ATTR "expiration"

/** Pointer to the current request to allow per-job logging to occur */
extern globus_thread_key_t globus_i_gram_request_key;

/* Type definitions */
typedef enum
{
    GLOBUS_GRAM_JOB_MANAGER_LOG_FATAL = 1 << 0,
    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR = 1 << 1,
    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN =  1 << 2,
    GLOBUS_GRAM_JOB_MANAGER_LOG_INFO =  1 << 3,
    GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG = 1 << 4,
    GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE = 1 << 5
}
globus_gram_job_manager_log_level_t;

typedef enum
{
    GLOBUS_GRAM_JOB_MANAGER_STATE_START,
    GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE,
    GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_COMMITTED,
    GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_IN,
    GLOBUS_GRAM_JOB_MANAGER_STATE_SUBMIT,
    GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1,
    GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2,
    GLOBUS_GRAM_JOB_MANAGER_STATE_CLOSE_OUTPUT,
    GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_OUT,
    GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END,
    GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END_COMMITTED,
    GLOBUS_GRAM_JOB_MANAGER_STATE_FILE_CLEAN_UP,
    GLOBUS_GRAM_JOB_MANAGER_STATE_SCRATCH_CLEAN_UP,
    GLOBUS_GRAM_JOB_MANAGER_STATE_CACHE_CLEAN_UP,
    GLOBUS_GRAM_JOB_MANAGER_STATE_DONE,
    GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED,
    GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CLOSE_OUTPUT,
    GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE,
    GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE_COMMITTED,
    GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_FILE_CLEAN_UP,
    GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_SCRATCH_CLEAN_UP,
    GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CACHE_CLEAN_UP,
    GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_DONE,
    GLOBUS_GRAM_JOB_MANAGER_STATE_STOP,
    GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY1,
    GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY2,
    /* GLOBUS_GRAM_JOB_MANAGER_STATE_PROXY_REFRESH  OBSOLETE STATE, */
    GLOBUS_GRAM_JOB_MANAGER_STATE_PRE_CLOSE_OUTPUT = GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY2+2,
    GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY1,
    GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY2
    /* GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_PROXY_REFRESH  OBSOLETE STATE */
}
globus_gram_jobmanager_state_t;

typedef enum
{
    GLOBUS_GRAM_JOB_MANAGER_STAGE_IN,
    GLOBUS_GRAM_JOB_MANAGER_STAGE_IN_SHARED,
    GLOBUS_GRAM_JOB_MANAGER_STAGE_OUT,
    GLOBUS_GRAM_JOB_MANAGER_STAGE_STREAMS
}
globus_gram_job_manager_staging_type_t;

typedef enum
{
    GLOBUS_GRAM_JOB_MANAGER_SIGNAL,
    GLOBUS_GRAM_JOB_MANAGER_CANCEL,
    GLOBUS_GRAM_JOB_MANAGER_PROXY_REFRESH
}
globus_gram_job_manager_query_type_t;

typedef enum 
{
    GLOBUS_GRAM_SCRIPT_PRIORITY_LEVEL_CANCEL,
    GLOBUS_GRAM_SCRIPT_PRIORITY_LEVEL_SIGNAL,
    GLOBUS_GRAM_SCRIPT_PRIORITY_LEVEL_SUBMIT,
    GLOBUS_GRAM_SCRIPT_PRIORITY_LEVEL_STAGE_OUT,
    GLOBUS_GRAM_SCRIPT_PRIORITY_LEVEL_STAGE_IN,
    GLOBUS_GRAM_SCRIPT_PRIORITY_LEVEL_POLL
}
globus_gram_script_priority_level_t;

typedef struct
{
    globus_gram_script_priority_level_t priority_level;
    uint64_t                            sequence;
}
globus_gram_script_priority_t;

typedef struct
{
    globus_gram_job_manager_staging_type_t
                                        type;
    globus_rsl_value_t *                from;
    char *                              evaled_from;
    globus_rsl_value_t *                to;
    char *                              evaled_to;
}
globus_gram_job_manager_staging_info_t;

typedef struct
{
    /**
     * Query type
     */
    globus_gram_job_manager_query_type_t
                                        type;
    /**
     * Connection handle
     *
     * Handle to send the script response to.
     */
    globus_gram_protocol_handle_t       handle;
    /**
     * Signal
     *
     * Type of signal to process.
     */
    globus_gram_protocol_job_signal_t   signal;

    /**
     * Signal-specific data
     *
     * If a priority change maybe something like high, medium, low. see
     * the documentation on signals in the globus_gram_protocol library.
     */
    char *                              signal_arg;
    /**
     * Query-specific RSL tree 
     *
     * If the query is a stdio update signal, then the arg is an RSL
     * string which may contain replacement values for
     * stdout, stderr, stdout_position, stderr_position, and remoteio_url.
     * This will be merged with the request's tree as the signal is handled.
     */
    globus_rsl_t *                      rsl;

    globus_gram_protocol_error_t        failure_code;

    /* When doing a proxy refresh, the newly delegated credential */
    gss_cred_id_t                       delegated_credential;
}
globus_gram_job_manager_query_t;

/**
 * Command-line configuration for a LRM instance. All of these items are
 * read from command-line or startup environment and do not change while
 * the job manager is running.
 */
typedef struct
{
    /*
     * -------------------------------------------------------------------
     * Values derived from command-line options and configuration file
     * -------------------------------------------------------------------
     */
    /** True if we are using kerberos for security instead of X.509
     * certificates.
     */
    globus_bool_t                       kerberos;
    /**
     * Path to globus installation.
     */
    char *                              globus_location;
    /**
     * GLOBUS_LOCATION which can be used on the target execution nodes.
     */
    char *                              target_globus_location;
    /**
     * Job Manager Type
     *
     * Identifies the scheduler which will be used to process this job
     * request. Possible values are fork, loadleveler, lsf, easymcs, pbs,
     * and others.
     */ 
    char *                              jobmanager_type;
    /**
     * Directory to store job state history information
     */
    char *                              job_history_dir;
    /**
     * Default cache path (may contain RSL substitutions)
     */
    char *                              cache_location;
    /**
     * Scratch directory root
     *
     * If the client requests a scratch directory with a relative path,
     * this base directory is prepended to it. It defaults to $(HOME),
     * but can be overridden on the job manager command line or configuration
     * file.
     */
    char *                              scratch_dir_base;
    /**
     * Condor Architecture
     *
     * Used only when type=condor.  Must match one of the archetecture values
     * as defined by condor
     */
    char *                              condor_arch;
    /**
     * Condor Operating System
     *
     * Used only when type=condor.  Must match one of the opsys values as
     * defined by condor
     */ 
    char *                              condor_os;
    /** gatekeeper host RSL substitution */
    char *                              globus_gatekeeper_host;
    /** gatekeeper port RSL substitution */
    char *                              globus_gatekeeper_port;
    /** gatekeeper subject RSL substitution */
    char *                              globus_gatekeeper_subject;
    /** host manufacturer RSL substitution */
    char *                              globus_host_manufacturer;
    /** host cputype RSL substitution */
    char *                              globus_host_cputype;
    /** host OS name RSL substitution */
    char *                              globus_host_osname;
    /** host OS version RSL substitution */
    char *                              globus_host_osversion;
    /**
     * Firewall-friendly range of TCP ports that will be used for network
     * traffic.
     */
    char *                              tcp_port_range;
    /**
     * Firewall-friendly range of TCP ports that will be used as sources for network
     * traffic.
     */
    char *                              tcp_source_range;
    /** Directory to store job_state files */
    char *                              job_state_file_dir;
    /**
     * Site-wide trusted certificate path.
     */
    char *                              x509_cert_dir;
    /**
     * List of char * which are variables to be added to the job environment.
     * They can be specified as just a name (in which case the value in the
     * job manager's environment will be used, or a NAME=VALUE string.
     */
    globus_list_t *                     extra_envvars;
    /**
     * SEG module to use instead of polling.
     */
    char *                              seg_module;
    /**
     * Path to job auditing directory.
     */
    char *                              auditing_dir;
    /** Grid Community Toolkit version */
    char *                              globus_version;
    /** Usage stats enabled by default */
    globus_bool_t                       usage_disabled;
    /** Usage stats target servers 
     * List of servers to report usage statistics to.  A null value
     * will result in the standard Globus listener getting the default set
     * of packets. 
     */
    char *                              usage_targets;
    /**
     * Streaming
     *
     * streaming_disabled is set from the config option -disable-streaming.
     * The default is false.
     * This is passed to the batch system script to decide whether to allow the
     * job. This lets admins disable streaming for most jobs, but allow it for
     * certain ones (e.g. the grid monitor).
     */
    globus_bool_t                       streaming_disabled;
    /**
     * Minimum proxy lifetime (in seconds) to allow. Once it is noticed that
     * the proxy will expire before that time, the job manager will go into the
     * STOP state.
     */
    int                                 proxy_timeout;
    /**
     * Events to record to syslog/log file. A bitwise or of the values from the
     * globus_gram_job_manager_log_level_t enumeration.
     */
    int                                 log_levels;
    /**
     * Flag indicating whether to use syslog for logging
     */
    globus_bool_t                       syslog_enabled;
    /**
     * Log file pattern. This may contain standard RSL substitutions.
     */
    const char *                        log_pattern;

    /*
     * -------------------------------------------------------------------
     * Values derived from job manager environment
     * -------------------------------------------------------------------
     */
    /** GSI Subject name */
    char *                              subject;
    /** User home directory */
    char *                              home;
    /** Username */
    char *                              logname;
    /** GRAM host */
    char *                              hostname;
    /** Short host name */
    char *                              short_hostname;
    /**
     * Service tag to differentiate job managers which are processing
     * jobs for the same LRM with different configurations
     */
    char *                              service_tag;
    /**
     * This is the prefix to directory which will contain host+lrm specific
     * state files. By default, it is $HOME/.globus/job, but can be 
     * modified by -globus-job-dir command-line option
     */
    char *                              job_dir_home;
    /**
     * Boolean flag indicating whether to enable GSI callouts
     * on GRAM operations or not. Default to no.
     */
    globus_bool_t                       enable_callout;
}
globus_gram_job_manager_config_t;

typedef struct 
{
    globus_callback_handle_t            session_timer_handle;
    char *                              jm_id;
    globus_abstime_t                    jm_start_time;
    
    int                                 count_total_done;
    int                                 count_total_failed;
    int                                 count_total_canceled;
    int                                 count_restarted;
    int                                 count_dryrun;
    int                                 count_peak_jobs;
    int                                 count_current_jobs;
} globus_i_gram_usage_tracker_t;

typedef struct globus_i_gram_usage_job_tracker_s
{
    globus_abstime_t                    unsubmitted_timestamp;
    globus_abstime_t                    file_stage_in_timestamp;
    globus_abstime_t                    pending_timestamp;
    globus_abstime_t                    active_timestamp;
    globus_abstime_t                    failed_timestamp;
    globus_abstime_t                    file_stage_out_timestamp;
    globus_abstime_t                    done_timestamp;
    int                                 restart_count;
    int                                 callback_count;
    int                                 status_count;
    int                                 register_count;
    int                                 unregister_count;
    int                                 signal_count;
    int                                 refresh_count;
    int                                 file_clean_up_count;
    int                                 file_stage_in_http_count;
    int                                 file_stage_in_https_count;
    int                                 file_stage_in_ftp_count;
    int                                 file_stage_in_gsiftp_count;
    int                                 file_stage_in_shared_http_count;
    int                                 file_stage_in_shared_https_count;
    int                                 file_stage_in_shared_ftp_count;
    int                                 file_stage_in_shared_gsiftp_count;
    int                                 file_stage_out_http_count;
    int                                 file_stage_out_https_count;
    int                                 file_stage_out_ftp_count;
    int                                 file_stage_out_gsiftp_count;
    char *                              client_address;
    char *                              user_dn;
} globus_i_gram_usage_job_tracker_t;

typedef struct
{
    /** Address of the client which submitted the job (hashtable key) */
    char *                              client_addr;
    /** Queue of script contexts ready to run */
    globus_priority_q_t                 script_queue;
    /** Number of script slots available for running scripts */
    int                                 script_slots_available;
    /** Fifo of available script handles */
    globus_fifo_t                       script_handles;
}
globus_gram_job_manager_scripts_t;

/**
 * Runtime state for a LRM instance. All of these items are
 * computed from the configuration state above and may change during the
 * lifetime of the job manager.
 */
typedef struct globus_gram_job_manager_s
{
    /** Link to the static job manager configuration */
    globus_gram_job_manager_config_t *  config;
    /**
     * set to GLOBUS_TRUE when the seg monitoring has begun
     */
    globus_bool_t                       seg_started;

    /**
     * Timestamp of the last SEG event we've completely processed. Initially
     * set to the time of the job submission.
     */
    time_t                              seg_last_timestamp;
    /**
     * Callback handle for fork SEG-like polling
     */
    globus_callback_handle_t            fork_callback_handle;
    /** LRM-specific set of validation records */
    globus_list_t *                     validation_records;
    /** Newest validation file timestamp */
    time_t                              validation_record_timestamp;
    /** Track when validation files are added or removed */
    globus_bool_t                       validation_file_exists[4];
    /** GRAM job manager listener contact string */
    char *                              url_base;
    /** Time when the job manager-wide proxy will expire */
    time_t                              cred_expiration_time;
    /** Timer tracking until the proxy expiration callback causes the job
     * manager to stop.
     */
    globus_callback_handle_t            proxy_expiration_timer;
    /** Hashtable mapping request->job_contact_path to request */
    globus_hashtable_t                  request_hash;
    /** Hashtable mapping job id->job_contact_path */
    globus_hashtable_t                  job_id_hash;
    /** Lock for thread-safety */
    globus_mutex_t                      mutex;
    /** Condition for noting when all jobs are done */
    globus_cond_t                       cond;
    /** Unix domain socket for receiving new job requests for other
     * job managers */
    int                                 socket_fd;
    /** XIO Handle for socket_fd so we can use XIO's select loop */
    globus_xio_handle_t                 active_job_manager_handle;
    /** Lock file related to the socket_fd */
    int                                 lock_fd;
    /** Socket file path */
    char *                              socket_path;
    /** Lock file path */
    char *                              lock_path;
    /** Pid file path */
    char *                              pid_path;
    /** OSG wants to have different clients connecting to the same job manager
     * to have separate script queues fto have scalability with nonresponsive
     * clients. We hash on client's address and have separate script queue and
     * available slots.
     */
    globus_list_t *                     scripts_per_client;
    /** Fifo of job state callback contexts to run */
    globus_fifo_t                       state_callback_fifo;
    /** Number of job state contact slots available */
    int                                 state_callback_slots;
    /** Path of job manager credential */
    char *                              cred_path;
    /** Grace period oneshot */
    globus_callback_handle_t            grace_period_timer;
    /** All jobs are done and grace period is complete */
    globus_bool_t                       done;
    globus_fifo_t                       seg_event_queue;
    int                                 seg_pause_count;
    /** All jobs are being stopped. Don't allow new ones in */
    globus_bool_t                       stop;
    /** List of jobs that still need to be restarted, but haven't yet */
    globus_list_t *                     pending_restarts;
    /** Periodic callback handle to process jobs in the pending_restarts list */
    globus_callback_handle_t            pending_restart_handle;
    /** Usage stats tracking data */
    globus_i_gram_usage_tracker_t *     usagetracker;
    /**
     * Error message extension for early misconfiguration-type errors that
     * occur before a request is read.
     */
    char *                              gt3_failure_message;
    globus_xio_attr_t                   script_attr;

    /**
     * Periodic callback handle to expire jobs which completed or failed
     * but didn't have two-phase end happen.
     */
    globus_callback_handle_t            expiration_handle;

    /**
     * Periodic callback handle to abort if something removes the lock file.
     */
    globus_callback_handle_t            lockcheck_handle;

    /**
     * Periodic callback handle to clse idle perl script xio handles
     */
    globus_callback_handle_t            idle_script_handle;
}
globus_gram_job_manager_t;

/**
 * Job Manager Request
 */
typedef struct globus_gram_job_manager_request_s
{
    /** Link to LRM-specific configuration */
    globus_gram_job_manager_config_t *  config;
    /** Link to LRM-specific runtime state */
    globus_gram_job_manager_t *         manager;
    /**
     * Job State
     *
     * The state of the job. This corresponds to the job state machine
     * described in the GRAM documentation.
     *
     * Use globus_gram_job_manager_request_set_status() to change.
     *
     */ 
    globus_gram_protocol_job_state_t    status;

    /**
     * Terminal state
     * 
     * The projected terminal state of the job. In the case of multiple
     * ids returned from the submit script, this will be set to failed if
     * any subjobs failed, or done otherwise.
     */
    globus_gram_protocol_job_state_t    expected_terminal_state;

    /**
     * Last time status was changed
     *
     * The time that the status member was last changed.
     * Automatically set by globus_gram_job_manager_request_set_status().
     */
    time_t                              status_update_time;

    /**
     * Job Failure Reason
     *
     * If the state is GLOBUS_GRAM_STATE_FAILED, then this
     * is an integer code that defines the failure. It is one of
     * GLOBUS_GRAM_PROTOCOL_ERROR_*.
     */
    int                                 failure_code;

    /**
     * Extended error message
     */
    char *                              gt3_failure_message;

    /**
     * Extended error type
     */
    char *                              gt3_failure_type;

    /**
     * Extended error information for staging errors (source url)
     */
    char *                              gt3_failure_source;

    /**
     * Extended error information for staging errors (destination url)
     */
    char *                              gt3_failure_destination;

    /**
     * Job Exit Code
     * 
     * If the state is GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE, then this
     * is an integer exit code from the job's executable.
     */
    int                                 exit_code;

    /** Stop Reason
     * 
     * If the job request is stopped either by an explicit signal or a proxy
     * timeout, this will be set to something besides 0, and that will be
     * sent as part of a fail message to satisfy condor
     */
    int                                 stop_reason;
    
    /**
     * Job identifier string
     *
     * String representation of the LRM job id. May be a comma-separated
     * string of separately-pollable ID values. This value is filled in when the
     * request is submitted. This version is modified as the subjobs complete. 
     */
    char *                              job_id_string;
    /**
     * Job identifier string
     *
     * String representation of the LRM job id. May be a comma-separated string
     * of separately-pollable ID values. This value is filled in when the
     * request is submitted.
     */
    char *                              original_job_id_string;
    
    /**
     * Poll Frequency
     *
     * How often should a check of the job status and output files be done.
     */
    unsigned int                        poll_frequency;

    /**
     * Dry Run
     *
     * If this is GLOBUS_TRUE, do not actually submit the job to the scheduler,
     * just verify the job parameters.
     */
    globus_bool_t                       dry_run;


    /**
     *
     * Two-phase commit.
     *
     * Non-zero if request should be confirmed via another signal.
     *
     * The value is how many seconds to wait before timing out.
     */
    int                                 two_phase_commit;

    /**
     * Value to extend the two-phase commit wait time by if a commit extend
     * signal arrives.
     */
    int                                 commit_extend;
    /** Time when job manager process is first begun */
    time_t                              creation_time;
    /** Time when job manager gets jobid from scheduler */
    time_t                              queued_time;
    /** Job-specific GASS cache tag. */
    char *                              cache_tag;
    /** RSL substitution symbol table */
    globus_symboltable_t                symbol_table;
    /** Parsed RSL values */
    globus_rsl_t *                      rsl;
    /** Canonical RSL document */
    char *                              rsl_spec;
    /**
     * Previous Job Manager Contact 
     *
     * If we're restarting from a terminated Job Manager, this will specify
     * the old job contact so we can locate the Job Manager state file.
     */
    char *                              jm_restart;
    /**
     * Unique job identifier
     *
     * Unique id for this job that will be consistent
     * across jobmanager restarts/recoveries.
     */
    char *                              uniq_id;
    /** Job contact string */
    char *                              job_contact;
    /** Unique job contact suffix */
    char *                              job_contact_path;
    /** Job-specific persistence file */
    char *                              job_state_file;
    /** Job-specific scratch directory after RSL evaluation */
    char *                              scratch_dir_base;
    /**
     * Job scratch directory.
     *
     * Scratch subdirectory created for this job. It will be removed
     * when the job completes. This is a subdirectory of scratch_dir_base.
     */
    char *                              scratchdir;
    /** remote_io_url value */
    char *                              remote_io_url;
    /** file to write remote_io_url to */
    char *                              remote_io_url_file;
    /** Job-specific proxy file */
    char *                              x509_user_proxy;
    /** Thread safety */
    globus_mutex_t                      mutex;
    /** Thread safety */
    globus_cond_t                       cond;
    /** Clients registered for job state changes */
    globus_list_t *                     client_contacts;
    /** List of file_stage_in values which haven't yet been processed */
    globus_list_t *                     stage_in_todo;
    /** List of file_stage_in_shared values which haven't yet been processed */
    globus_list_t *                     stage_in_shared_todo;
    /** List of file_stage_out values which haven't yet been processed */
    globus_list_t *                     stage_out_todo;
    /** List of file_stream_out values which haven't yet been processed */
    globus_list_t *                     stage_stream_todo;
    /** Current state machine state */
    globus_gram_jobmanager_state_t      jobmanager_state;
    /** State to resume from in the case of a restart */
    globus_gram_jobmanager_state_t      restart_state;
    /**
     * True if a job state change hasn't been sent to the callbacks registered
     * with it
     */
    globus_bool_t                       unsent_status_change;
    /** Timer tracking until the next job poll */
    globus_callback_handle_t            poll_timer;
    /**
     * Queue of job-specific operations (signals, cancel, etc) sent via the job
     * interface.
     */
    globus_fifo_t                       pending_queries;
    /** Directory for temporary job-specific files. */
    char *                              job_dir;
    /**
     * streaming_requested is set to true if there's at least one remote
     * destination for stdout or stderr. Otherwise, it's false.
     */
    globus_bool_t                       streaming_requested;
    /** Job-specific cache path after RSL evaluation */
    char *                              cache_location;
    /** Handle to add/remove files from the GASS cache */
    globus_gass_cache_t                 cache_handle;
    /** Path to job history file */
    char *                              job_history_file;
    /** Last job state stored in the job history file */
    int                                 job_history_status;
     /**
      * Value of the GLOBUS_CACHED_STDOUT RSL substitution
      */
    char *                              cached_stdout;
     /**
      * Value of the GLOBUS_CACHED_STDERR RSL substitution
      */
    char *                              cached_stderr;
    /** Security context used to submit job */
    gss_ctx_id_t                        response_context;
    /** Job Contact of this job when being handled by another process */
    char *                              old_job_contact;
    /**
     * Queue of pending SEG events
     */
    globus_fifo_t                       seg_event_queue;
    /**
     * Timestamp of the last SEG event we've completely processed. Initially
     * set to the time of the job submission.
     */
    time_t                              seg_last_timestamp;
    /**
     * TG Gateway user for auditing (from SAML assertion)
     */
    char *                              gateway_user;
    /**
     * Information to be tracked for usagestats
     */
    globus_i_gram_usage_job_tracker_t   job_stats;

    /**
     * Per-job log configuration, a bitwise or of values from the
     * globus_gram_job_manager_log_level_t enumeration.
     */
    int                                 job_log_level;
    /**
     * Per-job log filename pattern. If not set, the global log path will be used
     */
    char *                              log_pattern;
}
globus_gram_jobmanager_request_t;

typedef struct globus_gram_job_manager_ref_s
{
    /* Local copy of the unique hashtable key */
    char *                              key;
    /* Pointer to manager */
    globus_gram_job_manager_t *         manager;
    /* Pointer to the request, may be NULL if the request is swapped out of
     * memory because there is no active reference to it
     */
    globus_gram_jobmanager_request_t *  request;
    /* Count of callbacks, queries, etc that refer to this job.
     * When 0, the request is eligible for removal from memory.
     */
    int                                 reference_count;
    /* Timer to delay cleaning up unreferenced requests */
    globus_callback_handle_t            cleanup_timer;
    /* Current job state, for status updates without having to reload */
    globus_gram_protocol_job_state_t    job_state;
    /* Current job failure code, for status updates without having to reload */
    int                                 failure_code;
    /* Job exit code */
    int                                 exit_code;
    /* Number of times status query was processed while the job is swapped
     * out
     */
    int                                 status_count;
    /*
     * True if the job has been loaded from the state file at recovery 
     * time, but hasn't yet had its state machine started. In that case,
     * we have to go to the TWO_PHASE_COMMITTED state unless the job has
     * completed execution.
     */
    globus_bool_t                       loaded_only;

    /**
     * Timestamp of when to auto-destroy this job. Thsi will be 0 unless
     * the job has completed and failed to have it's two-phase commit end.
     * A periodic event will poll through the refs that have this attribute
     * set, and will reload them with a fake commit to get them cleaned up.
     */
    time_t                              expiration_time;

    /* The following are used for the internal fakeseg stuff for condor*/

    /**
     * Timestamp of the last SEG event we've completely processed. Initially
     * set to the time of the job submission.
     */
    time_t                              seg_last_timestamp;
    /**
     * Size of the Condor log file last time we polled.
     */
    globus_off_t                        seg_last_size;
}
globus_gram_job_manager_ref_t;

/* globus_gram_job_manager_config.c */
int
globus_gram_job_manager_config_init(
    globus_gram_job_manager_config_t *  config,
    int                                 argc,
    char **                             argv);

void
globus_gram_job_manager_config_destroy(
    globus_gram_job_manager_config_t *  config);

int
globus_i_gram_parse_log_levels(
    const char *                        unparsed_string,
    int *                               log_levels,
    char **                             error_string);

/* globus_gram_job_manager_request.c */
#ifdef DEBUG_THREADS
#define GlobusGramJobManagerRequestLock(request) \
    do { \
    globus_gram_job_manager_request_log( \
            request, \
            "JM: [tid=%ld] Locking request (%s:%d) %p\n", \
            (long) globus_thread_self(), \
            __FILE__, \
            __LINE__, \
            (request)); \
    globus_mutex_lock(&(request)->mutex); \
    } while (0)

#define GlobusGramJobManagerRequestUnlock(request) \
    do { \
    globus_gram_job_manager_request_log( \
            request, \
            "JM: [tid=%ld] Unlocking request (%s:%d) %p\n", \
            (long) globus_thread_self() \
            __FILE__, \
            __LINE__, \
            (request)); \
    globus_mutex_unlock(&(request)->mutex); \
    } while (0)
#else
#define GlobusGramJobManagerRequestLock(request) \
    globus_mutex_lock(&(request)->mutex)
#define GlobusGramJobManagerRequestUnlock(request) \
    globus_mutex_unlock(&(request)->mutex)
#endif

int
globus_gram_job_manager_request_init(
    globus_gram_jobmanager_request_t ** request,
    globus_gram_job_manager_t *         manager,
    char *                              rsl,
    gss_cred_id_t                       delegated_credential,
    gss_ctx_id_t                        response_ctx,
    const char *                        peer_address,
    globus_gsi_cred_handle_t            peer_cred,
    globus_bool_t                       reinit,
    char **                             old_job_contact,
    globus_gram_jobmanager_request_t ** old_job_request,
    char **                             gt3_failure_message);

void
globus_gram_job_manager_request_destroy(
    globus_gram_jobmanager_request_t *  request);

void
globus_gram_job_manager_request_free(
    globus_gram_jobmanager_request_t *  request);

int
globus_gram_job_manager_request_set_status(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_job_state_t    status);

int
globus_gram_job_manager_request_set_status_time(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_job_state_t    status,
        time_t valid_time);

void
globus_gram_job_manager_request_log(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_log_level_t level,
    const char *                        format,
    ...);

int
globus_gram_job_manager_request_acct(
    globus_gram_jobmanager_request_t *  request,
    const char *                        format,
    ...);

int
globus_gram_job_manager_symbol_table_populate(
    globus_gram_jobmanager_request_t *  request);

int
globus_gram_job_manager_history_file_set(
    globus_gram_jobmanager_request_t *  request);

int
globus_gram_job_manager_history_file_create(
    globus_gram_jobmanager_request_t *  request);

int
globus_gram_job_manager_request_load(
    globus_gram_job_manager_t *         manager,
    int                                 http_body_fd,
    int                                 context_fd,
    gss_cred_id_t                       credential,
    const char *                        peer_address,
    globus_gsi_cred_handle_t            peer_cred_handle,
    size_t                              content_length,
    globus_gram_jobmanager_request_t ** request,
    gss_ctx_id_t *                      context,
    char **                             contact,
    int *                               job_state_mask,
    char **                             old_job_contact,
    globus_gram_jobmanager_request_t ** old_job_request,
    globus_bool_t *                     version_only,
    char **                             gt3_failure_message);

int
globus_gram_job_manager_request_start(
    globus_gram_job_manager_t *         manager,
    globus_gram_jobmanager_request_t *  request,
    int                                 response_fd,
    const char *                        client_contact,
    int                                 job_state_mask);

void
globus_gram_job_manager_destroy_directory(
    globus_gram_jobmanager_request_t *  request,
    const char *                        directory);

int
globus_gram_job_manager_request_load_all(
    globus_gram_job_manager_t *         manager);

int
globus_i_gram_request_stdio_update(
    globus_gram_jobmanager_request_t *  request,
    globus_rsl_t *                      update_rsl);

int
globus_i_gram_symbol_table_populate(
    globus_gram_job_manager_config_t *  config,
    globus_symboltable_t *              symbol_table);

/* globus_gram_job_manager_validate.c */

/**
 * Select when an RSL parameter is valid or required.
 * @ingroup globus_gram_job_manager_rsl_validation 
 */
typedef enum
{
    GLOBUS_GRAM_VALIDATE_JOB_SUBMIT = 1,
    GLOBUS_GRAM_VALIDATE_JOB_MANAGER_RESTART = 2,
    GLOBUS_GRAM_VALIDATE_STDIO_UPDATE = 4
}
globus_gram_job_manager_validation_when_t;

extern
int
globus_gram_job_manager_validation_destroy(
    globus_list_t *                     validation_records);

extern
int
globus_gram_job_manager_validate_rsl(
    globus_gram_jobmanager_request_t *  request,
    globus_rsl_t *                      rsl,
    globus_gram_job_manager_validation_when_t
                                        when);

/* globus_gram_job_manager_contact.c */
int
globus_gram_job_manager_contact_add(
    globus_gram_jobmanager_request_t *  request,
    const char *                        contact,
    int                                 job_state_mask);

int
globus_gram_job_manager_contact_remove(
    globus_gram_jobmanager_request_t *  request,
    const char *                        contact);
int
globus_gram_job_manager_contact_list_free(
    globus_gram_jobmanager_request_t *  request);

void
globus_gram_job_manager_contact_state_callback(
    globus_gram_jobmanager_request_t *  request,
    globus_bool_t                       restart_state_machine);

int
globus_gram_job_manager_write_callback_contacts(
    globus_gram_jobmanager_request_t *  request,
    FILE *                              fp);

int
globus_gram_job_manager_read_callback_contacts(
    globus_gram_jobmanager_request_t *  request,
    FILE *                              fp);

/* globus_gram_job_manager_state.c */
extern
const char *                            globus_i_gram_job_manager_state_strings[];

int
globus_gram_job_manager_read_request(
    globus_gram_job_manager_t *         manager,
    int                                 fd,
    size_t                              content_length,
    char **                             rsl,
    char **                             client_contact,
    int *                               job_state_mask,
    globus_bool_t *                     version_only);

int
globus_gram_job_manager_reply(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_t *         manager,
    int                                 response_code,
    const char *                        job_contact,
    int                                 response_fd,
    gss_ctx_id_t                        response_context,
    const char *                        gt3_failure_message);

int
globus_gram_job_manager_validate_username(
    globus_gram_jobmanager_request_t *  request);

int
globus_gram_job_manager_state_machine_register(
    globus_gram_job_manager_t *         manager,
    globus_gram_jobmanager_request_t *  request,
    globus_reltime_t *                  delay);

int
globus_i_gram_remote_io_url_update(
    globus_gram_jobmanager_request_t *  request);

void
globus_gram_job_manager_state_machine_callback(
    void *                              user_arg);

/* globus_gram_job_manager_gsi.c */
int
globus_gram_job_manager_import_sec_context(
    globus_gram_job_manager_t *         manager,
    int                                 context_fd,
    gss_ctx_id_t *                      response_contextp);

globus_bool_t
globus_gram_job_manager_gsi_used(
    globus_gram_jobmanager_request_t *  request);

int
globus_gram_job_manager_gsi_register_proxy_timeout(
    globus_gram_job_manager_t *         manager,
    gss_cred_id_t                       cred,
    int                                 timeout,
    globus_callback_handle_t *          callback_handle);

int
globus_gram_job_manager_gsi_get_subject(
    char **                             subject_name);

int
globus_gram_job_manager_gsi_update_credential(
    globus_gram_job_manager_t *         manager,
    globus_gram_jobmanager_request_t *  request,
    gss_cred_id_t                       credential);

int
globus_gram_job_manager_gsi_write_credential(
    globus_gram_jobmanager_request_t *  request,
    gss_cred_id_t                       credential,
    const char *                        path);

int
globus_gram_job_manager_gsi_update_proxy_timeout(
    globus_gram_job_manager_t *         manager,
    gss_cred_id_t                       cred,
    int                                 timeout,
    globus_callback_handle_t *          callback_handle);

int
globus_gram_job_manager_gsi_relocate_proxy(
    globus_gram_jobmanager_request_t *  request,
    const char *                        new_proxy);

int
globus_gram_job_manager_call_authz_callout(
    globus_gram_job_manager_config_t *  config,
    gss_ctx_id_t                        request_context,
    gss_ctx_id_t                        authz_context,
    const char *                        uniq_id,
    const globus_rsl_t *                rsl,
    const char *                        auth_type);

int
globus_gram_job_manager_authz_query(
    globus_gram_job_manager_t *         manager,
    globus_gram_protocol_handle_t       handle,
    const char *                        uri,
    const char *                        auth_type);

int
globus_gram_gsi_get_dn_hash(
    gss_cred_id_t                       cred,
    unsigned long *                     hash);

/* globus_gram_job_manager_query.c */
void
globus_gram_job_manager_query_callback(
    void *                              arg,
    globus_gram_protocol_handle_t       handle,
    globus_byte_t *                     buf,
    globus_size_t                       nbytes,
    int                                 errorcode,
    char *                              uri);

void
globus_gram_job_manager_query_reply(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_query_t *   query);

/* globus_gram_job_manager_staging.c */
int
globus_gram_job_manager_staging_create_list(
    globus_gram_jobmanager_request_t *  request);

int
globus_gram_job_manager_streaming_list_replace(
    globus_gram_jobmanager_request_t *  request);
int
globus_gram_job_manager_staging_remove(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_staging_type_t
                                        type,
    char *                              from,
    char *                              to);

int
globus_gram_job_manager_staging_write_state(
    globus_gram_jobmanager_request_t *  request,
    FILE *                              fp);
int
globus_gram_job_manager_staging_read_state(
    globus_gram_jobmanager_request_t *  request,
    FILE *                              fp);

void
globus_gram_job_manager_staging_free_all(
    globus_gram_jobmanager_request_t *  request);

/* globus_gram_job_manager_rsl.c */
globus_rsl_t *
globus_gram_job_manager_rsl_merge(
    globus_rsl_t *                      base_rsl,
    globus_rsl_t *                      override_rsl);

globus_bool_t
globus_gram_job_manager_rsl_attribute_exists(
    globus_rsl_t *                      rsl,
    const char *                        attribute);

globus_bool_t
globus_gram_job_manager_rsl_need_stage_in(
    globus_gram_jobmanager_request_t *  request);

globus_bool_t
globus_gram_job_manager_rsl_need_stage_out(
    globus_gram_jobmanager_request_t *  request);

globus_bool_t
globus_gram_job_manager_rsl_need_file_cleanup(
    globus_gram_jobmanager_request_t *  request);

globus_bool_t
globus_gram_job_manager_rsl_need_scratchdir(
    globus_gram_jobmanager_request_t *  request);

globus_bool_t
globus_gram_job_manager_rsl_need_restart(
    globus_gram_jobmanager_request_t *  request);

int
globus_gram_job_manager_rsl_env_add(
    globus_rsl_t *                      ast_node,
    const char *                        var,
    const char *                        value);

int
globus_gram_job_manager_rsl_eval_one_attribute(
    globus_gram_jobmanager_request_t *  request,
    char *                              attribute,
    char **                             value);

int
globus_gram_job_manager_rsl_remove_attribute(
    globus_rsl_t *                      rsl,
    char *                              attribute);

globus_rsl_t *
globus_gram_job_manager_rsl_extract_relation(
    globus_rsl_t *                      rsl,
    const char *                        attribute);

int
globus_gram_job_manager_rsl_add_relation(
    globus_rsl_t *                      rsl,
    globus_rsl_t *                      relation);

int
globus_gram_job_manager_rsl_parse_value(
    char *                              value_string,
    globus_rsl_value_t **               rsl_value);

int
globus_gram_job_manager_rsl_evaluate_value(
    globus_symboltable_t *              symbol_table,
    globus_rsl_value_t *                value,
    char **                             value_string);

int
globus_gram_job_manager_rsl_eval_string(
    globus_symboltable_t *              symbol_table,
    const char *                        string,
    char **                             value_string);

int
globus_gram_job_manager_rsl_attribute_get_string_value(
    globus_rsl_t *                      rsl,
    const char *                        attribute,
    const char **                       value_ptr);

int
globus_gram_job_manager_rsl_attribute_get_boolean_value(
    globus_rsl_t *                      rsl,
    const char *                        attribute,
    globus_bool_t *                     value_ptr);

int
globus_gram_job_manager_rsl_attribute_get_int_value(
    globus_rsl_t *                      rsl,
    const char *                        attribute,
    int *                               value_ptr);

int
globus_gram_rsl_add_output(
    globus_gram_jobmanager_request_t *  request,
    globus_rsl_t *                      rsl,
    const char *                        attribute,
    const char *                        value);

int
globus_gram_rsl_add_stream_out(
    globus_gram_jobmanager_request_t *  request,
    globus_rsl_t *                      rsl,
    const char *                        source,
    const char *                        destination);

/* globus_gram_job_manager_state_file.c */
int
globus_gram_job_manager_state_file_set(
    globus_gram_jobmanager_request_t *  request,
    char **                             state_file);

int
globus_gram_job_manager_file_lock(
    int                                 fd);

int
globus_gram_job_manager_state_file_read(
    globus_gram_jobmanager_request_t *  request);
int
globus_gram_job_manager_state_file_write(
    globus_gram_jobmanager_request_t *  request);

int
globus_gram_job_manager_state_file_register_update(
    globus_gram_jobmanager_request_t *  request);

/* globus_gram_job_manager_script.c */
int 
globus_gram_job_manager_script_stage_in(
    globus_gram_jobmanager_request_t *  request);
int 
globus_gram_job_manager_script_stage_out(
    globus_gram_jobmanager_request_t *  request);
int 
globus_gram_job_manager_script_submit(
    globus_gram_jobmanager_request_t *  request);
int 
globus_gram_job_manager_script_poll(
    globus_gram_jobmanager_request_t *  request);
int
globus_gram_job_manager_script_signal(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_query_t *   query);
int
globus_gram_job_manager_script_cancel(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_query_t *   query);

globus_bool_t
globus_i_gram_job_manager_script_valid_state_change(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_job_state_t    new_state);

void
globus_gram_job_manager_script_close_all(
    globus_gram_job_manager_t *         manager);

void
globus_gram_script_close_idle(
    void *                              arg);

extern globus_xio_driver_t              globus_i_gram_job_manager_popen_driver;
extern globus_xio_stack_t               globus_i_gram_job_manager_popen_stack;

/* globus_gram_job_manager_seg.c */
globus_result_t
globus_gram_job_manager_init_seg(
    globus_gram_job_manager_t *         request);

globus_result_t
globus_gram_job_manager_shutdown_seg(
    globus_gram_job_manager_t *         manager);

void
globus_gram_job_manager_seg_handle_event(
    globus_gram_jobmanager_request_t *  request);

void
globus_gram_job_manager_seg_pause(
    globus_gram_job_manager_t *         manager);

void
globus_gram_job_manager_seg_resume(
    globus_gram_job_manager_t *         manager);

int
globus_gram_job_manager_seg_parse_condor_id(
    globus_gram_jobmanager_request_t *  request,
    char **                             condor_id);

/* globus_gram_job_manager_auditing.c */
int
globus_gram_job_manager_auditing_file_write(
    globus_gram_jobmanager_request_t *  request);

/* globus_gram_job_manager.c */
#ifdef DEBUG_THREADS
#define GlobusGramJobManagerLock(manager) \
    do { \
    globus_gram_job_manager_log( \
            manager, \
            "JM: [tid=%ld] Locking manager (%s:%d) %p\n", \
            (long) globus_thread_self() \
            __FILE__, \
            __LINE__, \
            (manager)); \
    globus_mutex_lock(&(manager)->mutex); \
    } while (0)

#define GlobusGramJobManagerUnlock(manager) \
    do { \
    globus_gram_job_manager_log( \
            manager, \
            "JM: [tid=%d] Unlocking manager (%s:%d) %p\n", \
            (long) globus_thread_self() \
            __FILE__, \
            __LINE__, \
            (manager)); \
    globus_mutex_unlock(&(manager)->mutex); \
    } while (0)
#define GlobusGramJobManagerWait(manager) \
    do { \
        globus_gram_job_manager_log( \
                manager, \
                GLOBUS_GRAM_LOG_TRACE, \
                "JM: [tid=%ld] Condition Wait: Unlocking manager (%s:%d) %p\n", \
                (long) globus_thread_self() \
                __FILE__, \
                __LINE__, \
                (manager)); \
        globus_cond_wait(&(manager)->cond, &(manager)->mutex); \
        globus_gram_job_manager_log( \
                manager, \
                GLOBUS_GRAM_LOG_TRACE, \
                "JM: [tid=%ld] Condition Wait Returns: Locking manager (%s:%d) %p\n", \
                (long) globus_thread_self() \
                __FILE__, \
                __LINE__, \
                (manager)); \
    } while (0)
#else
#define GlobusGramJobManagerLock(manager) \
        globus_mutex_lock(&(manager)->mutex)
#define GlobusGramJobManagerUnlock(manager) \
        globus_mutex_unlock(&(manager)->mutex)
#define GlobusGramJobManagerWait(manager) \
        globus_cond_wait(&(manager)->cond, &(manager)->mutex);
#endif
int
globus_gram_job_manager_init(
    globus_gram_job_manager_t *         manager,
    gss_cred_id_t                       cred,
    globus_gram_job_manager_config_t *  config);

void
globus_gram_job_manager_destroy(
    globus_gram_job_manager_t *         manager);

void
globus_gram_job_manager_log(
    globus_gram_job_manager_t *         manager,
    globus_gram_job_manager_log_level_t level,
    const char *                        format,
    ...);

int
globus_gram_job_manager_add_request(
    globus_gram_job_manager_t *         manager,
    const char *                        key,
    globus_gram_jobmanager_request_t *  request);

int
globus_gram_job_manager_add_reference(
    globus_gram_job_manager_t *         manager,
    const char *                        key,
    const char *                        reason,
    globus_gram_jobmanager_request_t ** request);

int
globus_gram_job_manager_remove_reference(
    globus_gram_job_manager_t *         manager,
    const char *                        reason,
    const char *                        key);

int
globus_gram_job_manager_register_job_id(
    globus_gram_job_manager_t *         manager,
    char *                              job_id,
    globus_gram_jobmanager_request_t *  request,
    globus_bool_t                       prelocked);

int
globus_gram_job_manager_unregister_job_id(
    globus_gram_job_manager_t *         manager,
    char *                              job_id);

int
globus_gram_job_manager_add_reference_by_jobid(
    globus_gram_job_manager_t *         manager,
    const char *                        jobid,
    const char *                        reason,
    globus_gram_jobmanager_request_t ** request);

int
globus_gram_job_manager_get_job_id_list(
    globus_gram_job_manager_t *         manager,
    globus_list_t **                    job_id_list);

globus_bool_t
globus_gram_job_manager_request_exists(
    globus_gram_job_manager_t *         manager,
    const char *                        key);

void
globus_gram_job_manager_set_grace_period_timer(
    globus_gram_job_manager_t *         manager);

void
globus_gram_job_manager_expire_old_jobs(
    void *                              arg);

int
globus_gram_job_manager_set_status(
    globus_gram_job_manager_t *         manager,
    const char *                        key,
    globus_gram_protocol_job_state_t    state,
    int                                 failure_code,
    int                                 exit_code);

int
globus_gram_job_manager_get_status(
    globus_gram_job_manager_t *         manager,
    const char *                        key,
    globus_gram_protocol_job_state_t *  state,
    int *                               failure_code,
    int *                               exit_code);

void
globus_gram_job_manager_stop_all_jobs(
    globus_gram_job_manager_t *         manager);

int
globus_gram_split_subjobs(
    const char *                        job_id,
    globus_list_t **                    subjobs);

int
globus_i_gram_mkdir(
    char *                              path);
/* globus_gram_job_manager_usagestats.c */

globus_result_t
globus_i_gram_usage_start_session_stats(
    globus_gram_job_manager_t *         manager);

globus_result_t
globus_i_gram_usage_end_session_stats(
    globus_gram_job_manager_t *         manager);
    
void
globus_i_gram_send_session_stats(
    globus_gram_job_manager_t *         manager);
    
void
globus_i_gram_send_job_stats(
    globus_gram_jobmanager_request_t *  request);
    
void
globus_i_gram_send_job_failure_stats(
    globus_gram_job_manager_t *         manager,
    int                                 rc);

globus_result_t
globus_i_gram_usage_stats_init(
    globus_gram_job_manager_t *         manager);

globus_result_t
globus_i_gram_usage_stats_destroy(
    globus_gram_job_manager_t *         manager);

/* startup_socket.c */
int
globus_gram_job_manager_startup_lock(
    globus_gram_job_manager_t *         manager,
    int *                               lock_fd);

int
globus_gram_job_manager_startup_socket_init(
    globus_gram_job_manager_t *         manager,
    globus_xio_handle_t *               handle,
    int *                               socket_fd);

int
globus_gram_job_manager_starter_send(
    globus_gram_job_manager_t *         manager,
    int                                 http_body_fd,
    int                                 context_fd,
    int                                 response_fd,
    gss_cred_id_t                       cred);

int
globus_gram_job_manager_starter_send_v2(
    globus_gram_job_manager_t *         manager,
    gss_cred_id_t                       cred);

extern globus_xio_driver_t              globus_i_gram_job_manager_file_driver;
extern globus_xio_stack_t               globus_i_gram_job_manager_file_stack;

/* tg_gateway.c */
int
globus_i_gram_get_tg_gateway_user(
    gss_ctx_id_t                        context,
    globus_gsi_cred_handle_t            peer_cred,
    char **                             gateway_user);


/* logging.c */
extern globus_logging_handle_t          globus_i_gram_job_manager_log_stdio;
extern globus_logging_handle_t          globus_i_gram_job_manager_log_sys;

extern
void
globus_i_job_manager_log_rotate(int sig);

extern
int
globus_gram_job_manager_logging_init(
    globus_gram_job_manager_config_t *  config);

extern
void
globus_gram_job_manager_logging_destroy(void);

extern
char *
globus_gram_prepare_log_string(
    const char *                        instr);

extern char globus_i_gram_default_rvf[];

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_GRAM_JOB_MANAGER_INCLUDE */
#endif /* ! GLOBUS_DONT_DOCUMENT_INTERNAL */
