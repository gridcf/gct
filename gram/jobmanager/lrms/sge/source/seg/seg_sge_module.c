/*
 * Sun Grid Engine Scheduler Event Generator implementation for GT4.
 *
 * See CREDITS file for attributions.
 * See LICENSE file for license terms.
 */

/* This #define is needed for the correct operation of the GLIBC strptime
 * function. */
#define _XOPEN_SOURCE 600

#include "globus_common.h"
#include "globus_scheduler_event_generator.h"
#include "version.h"

#include <stdlib.h>
#include <string.h>

#define SEG_SGE_DEBUG(level, message) \
    GlobusDebugPrintf(SEG_SGE, level, message)

/* This error code is used to represent the
 * "we want to skip a log entry" state. */
#define SEG_SGE_SKIP_LINE -10

/* this is for read control when finding the 1st timestamp in the logfile */
#define SEG_SGE_FOUND_FILE_TIMESTAMP -20

/**
 * Debug levels:
 * If the environment variable SEG_SGE_DEBUG is set to a bitwise or
 * of these values, then a corresponding log message will be generated.
 */
typedef enum
{
    /**
     * Information of function calls and exits
     */
    SEG_SGE_DEBUG_INFO = (1<<0),
    /**
     * Warnings of things which may be bad.
     */
    SEG_SGE_DEBUG_WARN = (1<<1),
    /**
     * Fatal errors.
     */
    SEG_SGE_DEBUG_ERROR = (1<<2),
    /**
     * Details of function executions.
     */
    SEG_SGE_DEBUG_TRACE = (1<<3)
}
globus_l_seg_sge_debug_level_t;

enum
{
    SEG_SGE_ERROR_UNKNOWN = 1,
    SEG_SGE_ERROR_OUT_OF_MEMORY,
    SEG_SGE_ERROR_BAD_PATH,
    SEG_SGE_ERROR_LOG_PERMISSIONS,
    SEG_SGE_ERROR_LOG_NOT_PRESENT
};

/**
 * State of the SGE log file parser.
 *
 * RJP  Jan.2008 added 4 fields to handle file rotation
 *
 *
 */
typedef struct
{
    /** Path of the current log file being parsed */
    char *                              path;
    /** Timestamp of when to start generating events from */
    struct tm                           start_timestamp;
    /** Stdio file handle of the log file */
    FILE *                              fp;
    /** Buffer of log file data */
    char *                              buffer;
    /** Callback for periodic file polling */
    globus_callback_handle_t            callback;
    /** Length of the buffer */
    size_t                              buffer_length;
    /** Starting offset of valid data in the buffer. */
    size_t                              buffer_point;
    /** Amount of valid data in the buffer */
    size_t                              buffer_valid;
    /** simple test whether all we're looking for is the timestamp; */
    globus_bool_t                       need_timestamp;
    /** First timestamp in log-file */
    time_t                              file_timestamp;
    /** file rotation number at 1st read - assumes N+1 old files labeled 0,1,2,3,4,5,6,...,N */
    int                                 file_number;
    /** file inode for quick test of file rotation */
    int                                 file_inode;
    /**
     * Flag indicating a Log close event indicating that the current
     * log was found in the log
     */
    globus_bool_t                       end_of_log;
    /**
     * Flag inidicating that this logfile isn't the one corresponding to
     * today, so and EOF on it should require us to close and open a newer
     * one
     */
    globus_bool_t                       old_log;
    /**
     * Path to the directory where the SGE server log files are located
     */
    char *                              log_file;
} globus_l_sge_logfile_state_t;

static globus_mutex_t                   globus_l_sge_mutex;
static globus_cond_t                    globus_l_sge_cond;
static globus_bool_t                    shutdown_called;
static int                              callback_count;


/* Function signature declarations. */
/*  rjp Jan.2008 added 3 routines for handling file rotation */

GlobusDebugDefine(SEG_SGE);

static
int
globus_l_sge_module_activate(void);

static
int
globus_l_sge_module_deactivate(void);

static
void
globus_l_sge_read_callback(
	void *                              user_arg);

static
int
globus_l_sge_parse_events(
	globus_l_sge_logfile_state_t *      state);

static
int
globus_l_sge_clean_buffer(
	globus_l_sge_logfile_state_t *      state);

static
int
globus_l_sge_increase_buffer(
	globus_l_sge_logfile_state_t *      state);

static
int
globus_l_sge_split_into_fields(
	globus_l_sge_logfile_state_t *      state,
	char ***                            fields,
	size_t *                            nfields);

static
int
globus_l_sge_find_logfile(
	globus_l_sge_logfile_state_t *      state);

static
int
globus_l_sge_set_logfile_name(
	 globus_l_sge_logfile_state_t *      state);

static
int
globus_l_sge_check_rotated(
        globus_l_sge_logfile_state_t * state);

static
int
globus_l_sge_get_file_timestamp(
        globus_l_sge_logfile_state_t * state);


/**** RJP 4.2 change -- replace above with this  *****/

GlobusExtensionDefineModule(globus_seg_sge) =
{
  "globus_seg_sge",
    globus_l_sge_module_activate,
    globus_l_sge_module_deactivate,
    NULL,
    NULL,
    &local_version

};

/**************End 4.2 Change  ******************/


/* This function will be used by the SEG calling code to
 * initialize this module. */
static
int
globus_l_sge_module_activate(void)
{
    time_t                              timestamp_val;
    globus_l_sge_logfile_state_t *      logfile_state;
    int                                 rc;
    globus_reltime_t                    delay;
    char                               *globus_sge_conf= NULL;
    char                               *sge_config = NULL;
    char                               *sge_root = NULL, *sge_cell = NULL;
    globus_result_t                     result;

    rc = globus_module_activate(GLOBUS_COMMON_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
	goto error;
    }
    rc = globus_mutex_init(&globus_l_sge_mutex, NULL);

    if (rc != GLOBUS_SUCCESS)
    {
	goto deactivate_common_error;
    }
    rc = globus_cond_init(&globus_l_sge_cond, NULL);
    if (rc != GLOBUS_SUCCESS)
    {
	goto destroy_mutex_error;
    }
    shutdown_called = GLOBUS_FALSE;
    callback_count = 0;
    if (getenv("SEG_SGE_DEBUG") == NULL)
    {
        setenv("SEG_SGE_DEBUG", "SEG_SGE_DEBUG_ERROR", 1);
    }
    GlobusDebugInit(
	    SEG_SGE,
	    SEG_SGE_DEBUG_INFO
	    SEG_SGE_DEBUG_WARN
	    SEG_SGE_DEBUG_ERROR
	    SEG_SGE_DEBUG_TRACE);

    logfile_state = calloc(1, sizeof(globus_l_sge_logfile_state_t));

    if (logfile_state == NULL)
    {
        rc = SEG_SGE_ERROR_OUT_OF_MEMORY;
	goto destroy_cond_error;
    }

    rc = globus_l_sge_increase_buffer(logfile_state);
    if (rc != GLOBUS_SUCCESS)
    {
	goto free_logfile_state_error;
    }

    /* Configuration info */
    result = globus_scheduler_event_generator_get_timestamp(&timestamp_val);

    if (result != GLOBUS_SUCCESS)
    {
	goto free_logfile_state_buffer_error;
    }

    if (timestamp_val != 0)
    {
	if (globus_libc_localtime_r(&timestamp_val,
		    &logfile_state->start_timestamp) == NULL)
	{
	    goto free_logfile_state_buffer_error;
	}
    }

    result = globus_eval_path(
            "${sysconfdir}/globus/globus-sge.conf",
            &globus_sge_conf);

    if (result != GLOBUS_SUCCESS)
    {
        goto free_logfile_state_buffer_error;
    }

    result = globus_common_get_attribute_from_config_file(
	    "",
	    globus_sge_conf,
	    "log_path",
	    &logfile_state->log_file);

    /* Same algorithm for missing logfile finding as seg.pm:
     * 1. If sge_root and sge_cell are set in globus-sge.conf, use those
     * 2. If sge_config is set in globus-sge.conf, source it and echo out
     *    $SGE_ROOT and $SGE_CELL as needed
     * 3. If $SGE_ROOT or $SGE_CELL is set in environment, use them
     */
    if (result != GLOBUS_SUCCESS
        || logfile_state->log_file == NULL
        || logfile_state->log_file[0] == '\0')
    {
        globus_common_get_attribute_from_config_file(
            "",
	    globus_sge_conf,
            "sge_root",
            &sge_root);

        if (sge_root != NULL && strcmp(sge_root, "undefined") == 0)
        {
            free(sge_root);
            sge_root = NULL;
        }

        globus_common_get_attribute_from_config_file(
            "",
	    globus_sge_conf,
            "sge_cell",
            &sge_cell);
        if (sge_cell != NULL && strcmp(sge_cell, "undefined") == 0)
        {
            free(sge_cell);
            sge_cell = NULL;
        }

        globus_common_get_attribute_from_config_file(
            "",
	    globus_sge_conf,
            "sge_config",
            &sge_config);

        if (sge_root == NULL || sge_cell == NULL)
        {
            if (sge_config != NULL)
            {
                FILE *tf;
                int tfd;
                char *cmd;
                struct stat st;

                tf = tmpfile();
                if (tf)
                {
                    tfd = fileno(tf);
                    if (tfd > -1)
                    {
                        cmd = globus_common_create_string(
                            ". \"%s\" && printf \"$SGE_ROOT\\n$SGE_CELL\\n\" 1>&%d",
                            sge_config,
                            tfd);
                        system(cmd);
                        free(cmd);
                        fstat(tfd, &st);
                        rewind(tf);

                        if (sge_root == NULL)
                        {
                            sge_root = malloc((size_t) st.st_size);

                            if (sge_root)
                            {
                                fgets(sge_root, (int) st.st_size, tf);
                                sge_root[strlen(sge_root)-1] = 0;
                            }
                            else
                            {
                                fscanf(tf, "%*[^\n]\n");
                            }
                        }
                        else
                        {
                            fscanf(tf, "%*[^\n]\n");
                        }
                        if (sge_cell == NULL)
                        {
                            sge_cell = malloc((size_t) st.st_size);

                            if (sge_cell)
                            {
                                fgets(sge_cell, (int) st.st_size, tf);
                                sge_cell[strlen(sge_cell)-1] = 0;
                            }
                            else
                            {
                                fscanf(tf, "%*[^\n]\n");
                            }
                        }
                        else
                        {
                            fscanf(tf, "%*[^\n]\n");
                        }
                    }
                    fclose(tf);
                }
            }
            if (sge_root == NULL)
            {
                char * tmp = getenv("SGE_ROOT");

                if (tmp)
                {
                    sge_root = strdup(tmp);
                }
            }
            if (sge_cell == NULL)
            {
                char * tmp = getenv("SGE_CELL");

                if (tmp)
                {
                    sge_cell = strdup(tmp);
                }
            }
        }
        if (sge_root == NULL || sge_cell == NULL)
        {
            rc = SEG_SGE_ERROR_OUT_OF_MEMORY;
            goto free_sge_cell;
        }
        logfile_state->log_file = globus_common_create_string(
            "%s/%s/common/reporting",
            sge_root, sge_cell);

        if (logfile_state->log_file == NULL)
        {
            rc = SEG_SGE_ERROR_OUT_OF_MEMORY;
            goto free_sge_cell;
        }

        free(sge_root);
        sge_root = NULL;
        free(sge_cell);
        sge_cell = NULL;
    }
    if (logfile_state->log_file == NULL)
    {
	SEG_SGE_DEBUG(SEG_SGE_DEBUG_WARN,
		("unable to find log file in configuration\n"));
	goto free_logfile_state_buffer_error;
    }

    /* Locate our logfile.
     * Other DRMs need to know the current time to determine which
     * logfile to inspect.  SGE just keeps a single large 'reporting' log. */

    /* --- Above is true but we've implemented file rotation
     * within the finding logfile routine   rjp Jan.2008
     */

    rc = globus_l_sge_find_logfile(logfile_state);
    if (rc == GLOBUS_SUCCESS)
    {
	logfile_state->fp = fopen(logfile_state->path, "r");

	if (logfile_state->fp == NULL)
	{
	    rc = SEG_SGE_ERROR_OUT_OF_MEMORY;

	    goto free_logfile_state_path_error;
	}
	GlobusTimeReltimeSet(delay, 0, 0);
    }
    else if(rc == SEG_SGE_ERROR_LOG_NOT_PRESENT)
    {
	GlobusTimeReltimeSet(delay, 1, 0);
    }
    else
    {
	goto free_logfile_state_path_error;
    }

    /* Setup a callback so that our main read function will be
     * invoked at a later time.
     */
    result = globus_callback_register_oneshot(
	    NULL,
	    &delay,
	    globus_l_sge_read_callback,
	    logfile_state);
    if (result != GLOBUS_SUCCESS)
    {
	goto free_logfile_state_path_error;
    }
    callback_count++;

    return 0;

free_sge_cell:
    if (sge_cell != NULL)
    {
        free(sge_cell);
    }
    if (sge_root != NULL)
    {
        free(sge_root);
    }
free_logfile_state_path_error:
    if (logfile_state->path)
    {
	globus_libc_free(logfile_state->path);
    }
    if (logfile_state->log_file)
    {
	globus_libc_free(logfile_state->log_file);
    }
free_logfile_state_buffer_error:
    globus_libc_free(logfile_state->buffer);
    if (globus_sge_conf != NULL)
    {
        free(globus_sge_conf);
    }
free_logfile_state_error:
    globus_libc_free(logfile_state);
destroy_cond_error:
    globus_cond_destroy(&globus_l_sge_cond);
destroy_mutex_error:
    globus_mutex_destroy(&globus_l_sge_mutex);
deactivate_common_error:
    globus_module_deactivate(GLOBUS_COMMON_MODULE);
error:
    return 1;
}
/* globus_l_sge_module_activate() */

/* This function is called before we are shut down so that we can
 * clean up properly. */
static
int
globus_l_sge_module_deactivate(void)
{
    globus_mutex_lock(&globus_l_sge_mutex);
    shutdown_called = GLOBUS_TRUE;

    while (callback_count > 0)
    {
	globus_cond_wait(&globus_l_sge_cond, &globus_l_sge_mutex);
    }
    globus_mutex_unlock(&globus_l_sge_mutex);

    GlobusDebugDestroy(SEG_SGE);

    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    return 0;
}

/*
 * This is our master read function.  It will be called periodically
 * as a result of a previous globus_callback_register_oneshot() invocation.
 */
static
void
globus_l_sge_read_callback(
	void *                              user_arg)
{
    int                                 rc;
    struct stat                         s;
    globus_l_sge_logfile_state_t *      state = user_arg;
    size_t                              max_to_read;
    globus_bool_t                       eof_hit = GLOBUS_FALSE;
    globus_reltime_t                    delay;
    globus_result_t                     result;


    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO, ("globus_l_sge_read_callback() invoked.\n"));

    globus_mutex_lock(&globus_l_sge_mutex);
    if (shutdown_called)
    {
	SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO, ("polling while deactivating"));

	globus_mutex_unlock(&globus_l_sge_mutex);
	goto error;
    }
    globus_mutex_unlock(&globus_l_sge_mutex);

    /* file may not have existed earlier  rjp Jan.2008 */
    if(state->fp == NULL)
    {
        if( state->path == NULL )
	{
  	    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO, ("no file name available"));
            goto error;
        }
	else
	{
            rc = stat(state->path,&s);
	    if(rc == 0)
	      {
	        SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO, ("opening file in callback"));
                state->fp = fopen(state->path,"r");
                state->file_inode = s.st_ino;
	      }
	}
    }


    /* Provided that we have an open log filehandle.. */
    if (state->fp != NULL)
    {
        /* Calculate how much data will fit within the read-buffer. */
	max_to_read = state->buffer_length - state->buffer_valid
	    - state->buffer_point;

	SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
	    ("Reading a maximum of %u bytes from SGE reporting file = %s\n",
		max_to_read, state->path));

	/* Actually perform the read. */
	rc = fread(state->buffer + state->buffer_point +
		state->buffer_valid, 1, max_to_read, state->fp);

	SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
	    ("Read %d bytes\n", rc));

	/* If we haven't read the most we could, we have either: */
	if (rc < max_to_read)
	{
	    /* Reached the end of the file..*/
	    if (feof(state->fp))
	    {
	        SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE, ("Reached EOF\n"));
		eof_hit = GLOBUS_TRUE;
	  	clearerr(state->fp);
	    }
	    else
	    {
		/* Or something bad has happened.
		 * This error state is currently unhandled... */

		/* XXX: Read error */
	    }
	}

	/* Update our state to record that we've added more valid data
	 * to the buffer. */
	state->buffer_valid += rc;

	/* Parse data.  This function will also generate event
	 * notifications and send them to the main server. */
	SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE, ("Parsing events in buffer.\n"));
	rc = globus_l_sge_parse_events(state);

	/* Move any remaining log data to the start of the buffer,
	 * overwriting any old log data that we have already parsed. */
	SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
	    ("Cleaning buffer of parsed events.\n"));
	rc = globus_l_sge_clean_buffer(state);

    }


    if (eof_hit == GLOBUS_TRUE)
      {

        /* Here we hand log-rotation possibility - by resetting file_number
         *  1. check to see if log has been rotated and
         *  2. reset file_number so that next file opened will
         *  be correctly identified  rjp Jan.2008
         */
        rc = globus_l_sge_check_rotated(state);

        if(rc != 0)       /* file has been rotated */
	  {
	    state->file_number++;
            state->old_log = GLOBUS_TRUE;
	  }

        if(state->old_log)
	  {
           if(state->fp)
	     {
  	      fclose(state->fp);
              state->fp = NULL;
	     }

	   /* decrement file number.
	    * Note if file was rotated while open,
            * the above increment of file number
            * allows this to work   rjp Jan.2008
            */

           state->file_number--;
           rc = globus_l_sge_set_logfile_name(state);

           rc = stat(state->path,&s);
           if(rc == 0)
	     {
               state->fp = fopen(state->path,"r");
               state->file_inode = s.st_ino;
	     }

           if(state->fp != NULL)
	     {
	       /* we got a new file */
               eof_hit = GLOBUS_FALSE;
	     }
	  }
      }


      /* Determine if we have reached the EOF on the logfile.
       * If we have, set a moderately long delay.
       * If not, set  zero delay so we can read the rest! */

    if (eof_hit == GLOBUS_TRUE || state->fp == NULL)
    {
	GlobusTimeReltimeSet(delay, 2, 0);
    }
    else
    {
	GlobusTimeReltimeSet(delay, 0, 0);
    }


    /* Make the call to get ourselves invoked again. */
    /* rjp --> this used to include a pointer to the callback in the logfile_state struct.
     * as &state->callback, But this causes a memory leak. Removed and put to NULL */
    result = globus_callback_register_oneshot(
    	    NULL,
	    &delay,
	    globus_l_sge_read_callback,
	    state);

    if (result != GLOBUS_SUCCESS)
    {
	goto error;
    }

    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
	  ("globus_l_sge_read_callback() exited with/success \n"));

    return;
error:
    globus_mutex_lock(&globus_l_sge_mutex);
    if (shutdown_called)
    {
	callback_count--;

	if (callback_count == 0)
	{
	    globus_cond_signal(&globus_l_sge_cond);
	}
    }
    else
    {
        fprintf(stderr,
                "FATAL: Unable to register callback. SGE SEG exiting\n");
        exit(EXIT_FAILURE);
    }
    globus_mutex_unlock(&globus_l_sge_mutex);

    SEG_SGE_DEBUG(SEG_SGE_DEBUG_WARN,
	    ("globus_l_sge_read_callback() exited with/error\n"));
    return;
}
/* globus_l_sge_read_callback() */

/**
 * Determine the SGE log file name.
 * This is actually really easy for SGE, because the filename doesn't change --
 * it'll always be called 'reporting' and we'll already have the
 * exact path to use.
 *
 * above is now modified for simple reporting file rotation: rjp Jan.2008
 *
 * @param state
 *     SGE log state structure. The path field of the structure may be
 *     modified by this function.
 *
 * @retval GLOBUS_SUCCESS
 *     Name of an log file name has been found and the file exists.
 * @retval 1
 *     Something bad occurred.
 */
static
int
globus_l_sge_find_logfile(
	globus_l_sge_logfile_state_t *      state)
{
    struct stat                         s;
    int                                 rc;
    time_t                              stamp;
    globus_bool_t                       file_found = GLOBUS_FALSE;

    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO, ("globus_l_sge_find_logfile()\n"));

    if (state->path == NULL)
    {
	SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE, ("allocating path\n"));
	state->path = malloc(strlen(state->log_file) + 10);
	if (state->path == NULL)
	{
	    rc = SEG_SGE_ERROR_OUT_OF_MEMORY;
	    goto error;
	}
    }


    /* log_file contains string of base file name including path */
    /* Simply copy the path string from log_file to path. */

    stamp = mktime(&state->start_timestamp);
    SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE, ("input timestamp = %d\n",stamp));

    state->file_number=-1;
    while(!file_found)
      {
         SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
                      ("find file loop with file_number = %d\n",
                       state->file_number));

        rc = globus_l_sge_set_logfile_name(state);
        if (rc < 0)
	  {
	    SEG_SGE_DEBUG(SEG_SGE_DEBUG_WARN, ("couldn't format string\n"));
	    rc = SEG_SGE_ERROR_OUT_OF_MEMORY;
	    goto error;
	  }

        rc = stat(state->path, &s);
	if(rc == 0)
	  {
            if ((s.st_mode & S_IFREG) == 0)
            {
                SEG_SGE_DEBUG(SEG_SGE_DEBUG_ERROR,
                    ("SEG looks for SGE log file but finds "
                     "non-regular file at %s\n",
                    state->path));
                exit(EXIT_FAILURE);
            }
            rc = globus_l_sge_get_file_timestamp(state);
	  } else {
            SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
                         ("file = %s not found\n",state->path));
            if(state->file_number >= 0)
              {
                /* rjp Jan.2008
                   here we assume input timestamp is earlier than
                   the chain of rotated log files. Back it up 1.
                */
	        state->file_number--;
                rc = globus_l_sge_set_logfile_name(state);
                file_found = GLOBUS_TRUE;

              } else {
                /* it's possible the direct file (file_number = -1)
                   doesn't exist yet so set to skip over (see next if/else) */
                state->file_timestamp = 0;
	      }
	  }

        if( state->file_timestamp > 0 && state->file_timestamp < stamp )
	  {
            SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
                         ("found our file = %s with Timestamp %d \n",
                          state->path,state->file_timestamp));
            file_found=GLOBUS_TRUE;
	  }
	else
	  {
            SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
                         ("Not file to use = %s with Timestamp %d \n",
                          state->path,state->file_timestamp));

	    /* next file */
            state->file_number++;
	
	    /** now it;s possible under quick file rotations that no
             *  timestamp is put in the file, thus state->file_timestamp = 0.
             *  In this case, as written above, we'll appropriately skip
             *  that file:   rjp Jan.2008
             */
	  }
      }

    rc = stat(state->path, &s);
    state->file_inode = s.st_ino;

    if (rc < 0)
    {
	switch (errno)
	{
	    case ENOENT:
		/* Doesn't exist,
		*/
		SEG_SGE_DEBUG(SEG_SGE_DEBUG_ERROR,
			("file %s doesn't exist\n", state->path));

		break;

	    case EACCES:
		SEG_SGE_DEBUG(SEG_SGE_DEBUG_ERROR,
			("permissions needed to access logfile %s\n",
			 state->path));
		/* Permission problem (fatal) */
		rc = SEG_SGE_ERROR_LOG_PERMISSIONS;
		goto error;

	    case ENOTDIR:
	    case ELOOP:
	    case ENAMETOOLONG:
		/* broken path (fatal) */
		SEG_SGE_DEBUG(SEG_SGE_DEBUG_ERROR,
			("broken path to logfile %s\n",
			 state->path));
		rc = SEG_SGE_ERROR_BAD_PATH;
		goto error;

	    case EFAULT:
		SEG_SGE_DEBUG(SEG_SGE_DEBUG_ERROR,
			("bad pointer\n"));
		globus_assert(errno != EFAULT);

	    case EINTR:
	    case ENOMEM:

	    default:
		SEG_SGE_DEBUG(SEG_SGE_DEBUG_ERROR,
			("unexpected errno\n"));
		rc = SEG_SGE_ERROR_UNKNOWN;
		goto error;
	}
    }

    if (rc != 0)
    {
        state->file_inode = 0;
	goto error;
    }

    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
	    ("globus_l_sge_find_logfile() exits w/out error\n"));
    return 0;

error:
    SEG_SGE_DEBUG(SEG_SGE_DEBUG_WARN,
	    ("globus_l_sge_find_logfile() exits w/error\n"));
    return rc;
}
/* globus_l_sge_find_logfile() */


/**
 *  rjp Jan.2008
 *  routine to set the file name based on the file rotation model.
 *  Here simply all rotated files have '.file_number' extension. If other
 *  models are defined, change this routine accordingly
 *
 **/
static
int
globus_l_sge_set_logfile_name(
	globus_l_sge_logfile_state_t *      state)
{

    int                                 rc;

    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
                 ("globus_l_sge_set_logfile_name()\n"));
    if( state->file_number < 0)
      {
         SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
                      ("non-rotated file number \n"));
         rc = sprintf(state->path,"%s",state->log_file);
         state->old_log = GLOBUS_FALSE;
      }
    else
      {
 	 SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
                      ("rotated file file_number >= 0\n"));
         rc = sprintf(state->path,"%s%s%d",state->log_file,".",state->file_number);
         state->old_log = GLOBUS_TRUE;
      }

    return rc;
}
/* globus_l_sge_set_logfile_name */

/**
 * Move any data in the state buffer to the beginning, to enable reusing
 * buffer space which has already been parsed.
 */
static
int
globus_l_sge_clean_buffer(
	globus_l_sge_logfile_state_t *      state)
{
    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
	    ("globus_l_sge_clean_buffer() called\n"));

    /* move data to head of buffer */
    if (state->buffer != NULL)
    {
	if(state->buffer_point > 0)
	{
	    if (state->buffer_valid > 0)
	    {
		memmove(state->buffer,
			state->buffer+state->buffer_point,
			state->buffer_valid);
	    }
	    state->buffer_point = 0;
	}
    }
    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
	    ("globus_l_sge_clean_buffer() exits\n"));
    return 0;
}
/* globus_l_sge_clean_buffer() */

/**
 * Reduce unused space in the log buffer, increasing the size of the buffer
 * if it is full.
 *
 * @param state
 *     SGE log state structure. The buffer-related fields of the structure
 *     may be modified by this function.
 */
static
int
globus_l_sge_increase_buffer(
	globus_l_sge_logfile_state_t *      state)
{
    char *                              save = state->buffer;
    const size_t                        GLOBUS_SGE_READ_BUFFER_SIZE = 4096;
    int                                 rc;

    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
	    ("globus_l_sge_increase_buffer() called\n"));

    /* If the buffer is full of valid data, enlarge it! */
    if (state->buffer_valid == state->buffer_length)
    {
	state->buffer = globus_libc_realloc(state->buffer,
		state->buffer_length + GLOBUS_SGE_READ_BUFFER_SIZE);
	if (state->buffer == NULL)
	{
	    SEG_SGE_DEBUG(SEG_SGE_DEBUG_ERROR, ("realloc() failed\n"));

	    rc = SEG_SGE_ERROR_OUT_OF_MEMORY;
	    goto error;
	}
    }

    state->buffer_length += GLOBUS_SGE_READ_BUFFER_SIZE;

    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
	    ("globus_l_sge_increase_buffer() exits w/success\n"));
    return 0;

error:
    SEG_SGE_DEBUG(SEG_SGE_DEBUG_WARN,
	    ("globus_l_sge_increase_buffer() exits w/failure\n"));
    state->buffer = save;
    return rc;
}
/* globus_l_sge_increase_buffer() */


/**
 *
 *  Simple routine to check inode number to see if it has changed.
 *  If so we assume file has been rotated
 *
 **/

static
int
globus_l_sge_check_rotated(globus_l_sge_logfile_state_t * state)
{

  int                            rc;
  struct stat s;

  SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO, ("globus_l_sge_check_rotated() invoked.\n"));

  rc = stat(state->path,&s);
  if(s.st_ino != state->file_inode)
     {
        SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO, ("file has been rotated().\n"));
        return 1;
     }

  SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO, ("globus_l_sge_check_rotated() exit.\n"));
  return 0;
}
/* globus_l_sge_check_rotated */


/* This function's job is to parse any whole events from our read buffer,
 * generate state update messages and deliver them to the main process.
 *
 * It's now also used to grab the 1st timestamped entry in the reporting file
 * when file rotation is activated . rjp Jan.2008
 *
 * The format of the reporting file is indicated in the SGE documentation. */
static
int
globus_l_sge_parse_events(
	globus_l_sge_logfile_state_t *      state)
{
    char *                              eol;
    char *                              rp;
    struct tm                           tm;
    time_t                              stamp;
    char **                             fields = NULL;
    size_t                              nfields;
    time_t                              when;
    int                                 rc;
    int                                 exit_status;
    int                                 status;
    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
	    ("globus_l_sge_parse_events() called\n"));

    status = 0;

    /* Find the next newline */
    while ( (status != SEG_SGE_FOUND_FILE_TIMESTAMP) &&
            (eol = memchr(state->buffer + state->buffer_point,
		    '\n',
		    state->buffer_valid)) != NULL)
    {
	/* Replace the EOL character with a NULL terminator. */
	*eol = '\0';

	SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
		("parsing line %s\n", state->buffer + state->buffer_point));

	rc = globus_l_sge_split_into_fields(state, &fields, &nfields);

	/* If split_into_fields fails, ignore the line.*/
	if (rc != GLOBUS_SUCCESS)
	{
	    SEG_SGE_DEBUG(SEG_SGE_DEBUG_WARN,
		    ("Failed to parse line %s\n",
		     state->buffer + state->buffer_point));
	    goto free_fields;
	}

	/* If the first character is a '#', ignore the line. */
	if (strstr(fields[0], "#") == fields[0]) {
	    SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
		    ("Line '%s' is a comment, skipping.\n",
		     state->buffer + state->buffer_point));
	    goto free_fields;
 	}

	/* If the number of fields is < 14, ignore the line. */
	/* This is a safety check -- we will quite happily access fields[13]
	 * after this point. */
	if (nfields < 14)
	{
	    SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
		    ("too few fields, freeing and getting next line\n"));
	    goto free_fields;
	}

	/* Extract the timestamp from the first field.
         * Originally this was seconds since the epoch. Univa changed it
         * in UGE 8.2.0 to milliseconds since the epoch
         *
         * If the string is longer than 10 digits, we'll truncate it---
         * that'll work for > one hundred years.
         */
        if (strlen(fields[0]) > 10)
        {
            fields[0][10] = '\0';
        }
        /* (rp is a pointer to the char immediately following the timestamp.) */
	rp = strptime(fields[0],"%s", &tm);

	if (rp == NULL || (*rp) != '\0')
	{
	    SEG_SGE_DEBUG(SEG_SGE_DEBUG_WARN,
		    ("Unable to extract timestamp from first field in line '%s'\n",
		     state->buffer + state->buffer_point));
	    goto free_fields;
	}
	stamp = mktime(&tm);
	if (stamp == -1)
	{
	    SEG_SGE_DEBUG(SEG_SGE_DEBUG_WARN,
		    ("mktime generated invalid timestamp\n"));
	    goto free_fields;
	}

	/* for getting file-timestamp only  rjp Jan.2008 */
        if(state->file_timestamp == 0)
	  {
	    state->file_timestamp = stamp;
	    SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
		    ("  Setting the file timestamp to %d\n",state->file_timestamp));
            if(state->need_timestamp)
	      {
                status = SEG_SGE_FOUND_FILE_TIMESTAMP; /* will kick out of loop */
	        goto free_fields;
	      }
	  }

	when = mktime(&state->start_timestamp);

	if (stamp < when)
	{
	    /* Skip messages which are before our start timestamp */
	    SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
		    ("Skipping entry as timestamp %d is before checkpoint %d\n",
		     stamp, when));
	    status = SEG_SGE_SKIP_LINE;
	    goto free_fields;
	}

	/* Batch accounting: resources consumed by the job  */
	/* 
	 * UGE reporting file notes that an "extra" acct record is sent to the reporting file at
	 * midnight for long running jobs. These will be uniquely identified with 
	 * exit_status (fields[14]) = -1 and should be skipped.  rjporter 05/2013
	 *
	 */
	if ((strstr(fields[1], "acct") == fields[1]) && !(strstr(fields[14], "-1") == fields[14]))
	{
            char * job_id;
	    int failed;
	    /* From the SGE 'reporting' man page:
	     *
	     * failed:
	     * Indicates the problem which occurred in case a job could not  be
	     * started on the execution host (e.g. because the owner of the job
	     * did not have a valid account on that machine).  If  Grid  Engine
	     * tries  to  start a job multiple times, this may lead to multiple
	     * entries in the accounting file corresponding to the same job ID.
	     *
	     * exit status:
	     * Exit status of the job script (or Grid Engine specific status in
	     * case of certain error conditions)
	     */

	    /* Lookup the exit status of the job. */
	    rc = sscanf(fields[13], "%d", &failed);
	    rc = sscanf(fields[14], "%d", &exit_status);

            job_id = globus_common_create_string(
                    "%s.%s",
                    fields[7], nfields > 37 ? fields[37] : "0");

	    /* Return a job failure event if the exit status is non-zero. */
	    if ( failed != 0)
	    {
		SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
			("New event: job %s has failed with exit status %d.\n",
			 job_id, exit_status));
		rc = globus_scheduler_event_failed(stamp, job_id, failed);
	    }
	    else
	    {
		SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
			("New event: job %s has done with exit status %d.\n",
			 job_id, exit_status));
		rc = globus_scheduler_event_done(stamp, job_id, exit_status);
	    }
            free(job_id);
	}
	else if (strstr(fields[1], "job_log") == fields[1])
	{
            char * job_id;

	    /* Job state change. */
	    if (strstr(fields[3], "pending") == fields[3])
	    {
                job_id = globus_common_create_string(
                        "%s.%s", fields[4], fields[5]);

		SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
			("New event: job %s now pending at t=%d\n",
                            job_id, stamp));
		rc = globus_scheduler_event_pending(stamp, job_id);
                free(job_id);
	    }
	    else if (strstr(fields[3], "delivered") == fields[3])
	    {
                job_id = globus_common_create_string(
                        "%s.%s", fields[4], fields[5]);
		SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
			("New event: job %s now active at t=%d\n",
                            job_id, stamp));
		rc = globus_scheduler_event_active(stamp, job_id);
                free(job_id);
	    }
	    else if (strstr(fields[3], "deleted") == fields[3])
	    {
                job_id = globus_common_create_string(
                        "%s.%s", fields[4], fields[5]);
		SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
			("New event: job %s now completed at t=%d\n",
                        job_id,
                        stamp));
		rc = globus_scheduler_event_done(stamp, job_id, 0);
                free(job_id);
	    }
	}

free_fields:
	if (fields != NULL)
	{
	    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
		    ("freeing fields\n"));
	    globus_libc_free(fields);
	    fields = NULL;
	}

	state->buffer_valid -= eol + 1 - state->buffer - state->buffer_point;
	state->buffer_point = eol + 1 - state->buffer;

    }

    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
	    ("globus_l_sge_parse_events() exits\n"));
    return status;
}
/* globus_l_sge_parse_events() */

/**
 *
 *  Routine added for handling file rotation. rjp Jan.2008
 *
 */
static
int
globus_l_sge_get_file_timestamp(globus_l_sge_logfile_state_t* state)
{

    globus_bool_t    eof_hit = GLOBUS_FALSE;
    int              max_to_read;
    int              rc;

    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO, ("globus_l_sge_get_file_timestamp() invoked.\n"));

    if(state->fp != NULL)
      {
        fclose(state->fp);
        state->fp = NULL;
      }

    state->fp = fopen(state->path,"r");

    if(state->fp == NULL)
      {
        SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
                     ("   unable to open file name = %s\n",state->path));
        goto error;
      }

    /* start with an empty buffer */
    state->buffer_point = 0;
    state->buffer_valid = 0;
    state->need_timestamp = GLOBUS_TRUE;
    state->file_timestamp = 0;

    while ( state->file_timestamp == 0  && !eof_hit )
      {
         /* Calculate how much data will fit within the read-buffer. */
         max_to_read = state->buffer_length - state->buffer_valid
                   - state->buffer_point;

         SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
                   ("Reading a maximum of %u bytes from SGE reporting file\n",
                     max_to_read));

        /* Actually perform the read. */
         rc = fread(state->buffer + state->buffer_point +
          	  state->buffer_valid, 1, max_to_read, state->fp);

        SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
                   ("Read %d bytes\n", rc));
	/* If we haven't read the most we could, we have either: */

	if (rc < max_to_read)
	   {
	     /* Reached the end of the file or some other problem - either way assume EOF */
	     eof_hit = GLOBUS_TRUE;
	   }

        state->buffer_valid += rc;
        /* try to find the file timestamp inside the buffer */
        rc = globus_l_sge_parse_events(state);

        SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
                   ("     Cleaning buffer of parsed events.\n"));
        rc = globus_l_sge_clean_buffer(state);
      }


    if(state->fp != NULL)
       {
         fclose(state->fp);
         state->fp = NULL;
       }

    /* End with an empty buffer */
    state->buffer_point = 0;
    state->buffer_valid = 0;
    state->need_timestamp = GLOBUS_FALSE;

    if(state->file_timestamp == 0 )
      {
	SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
                     (" Could not get timestamp from file "));
        return -1;
      }

    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
                 ("globus_l_sge_get_file_timestamp() exit.\n"));
    return  0;

 error:
    SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE,
                 ("Get Timestamp Problem opening file %s\n",state->path));
    return -1;
}
/* globus_l_sge_get_file_timestamp */


/**
 * @param state
 *     Log state structure. The string pointed to by
 *     state-\>buffer + state-\>buffer_point is modified
 * @param fields
 *     Modified to point to a newly allocated array of char * pointers which
 *     point to the start of each field within the state buffer block.
 * @param nfields
 *     Modified value pointed to by this will contain the number of fields in
 *     the @a fields array after completion.
 */
static
int
globus_l_sge_split_into_fields(
	globus_l_sge_logfile_state_t *      state,
	char ***                            fields,
	size_t *                            nfields)
{
    size_t                              i = 0;
    size_t                              cnt = 1;
    char *                              tmp;
    int                                 rc;

    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO, ("globus_l_sge_split_into_fields()\n"));

    *fields = NULL;
    *nfields = 0;

    tmp = state->buffer + state->buffer_point;

    SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE, ("splitting %s\n", tmp));

    while (*tmp != '\0')
    {
	if (*tmp == ':')
	{
	    cnt++;
	}
	tmp++;
    }
    SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE, ("%u fields\n", cnt));

    *fields = globus_libc_calloc(cnt, sizeof(char **));

    if (*fields == NULL)
    {
	rc = SEG_SGE_ERROR_OUT_OF_MEMORY;
	goto error;
    }
    *nfields = cnt;

    tmp = state->buffer + state->buffer_point;

    (*fields)[i++] = tmp;

    while (*tmp != '\0' && i < cnt)
    {
	if (*tmp == ':')
	{
	    (*fields)[i++] = tmp+1;
	    *tmp = '\0';
	}
	tmp++;
    }

#   if BUILD_DEBUG
    {
	for (i = 0; i < cnt; i++)
	{
	    SEG_SGE_DEBUG(SEG_SGE_DEBUG_TRACE, ("field[%u]=%s\n",
			i, (*fields)[i]));
	}
    }
#   endif

    SEG_SGE_DEBUG(SEG_SGE_DEBUG_INFO,
	    ("globus_l_sge_split_into_fields(): exit success\n"));

    return 0;

error:
    SEG_SGE_DEBUG(SEG_SGE_DEBUG_WARN,
	    ("globus_l_sge_split_into_fields(): exit failure: %d\n", rc));
    return rc;;
}
/* globus_l_sge_split_into_fields() */

