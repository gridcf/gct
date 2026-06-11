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

/**********************************************************************
globus_utp_main.c

Public functions for the Unnamed Timing Package (UTP).
**********************************************************************/



#include "globus_common.h"

#ifndef _WIN32
#include <pwd.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>

#ifdef SOLHR
#include <netdb.h>
#endif


#include "globus_utp.h"
#include "globus_utp_private.h"

/******************************************************************************
                          Module activation structure
******************************************************************************/
static int
globus_l_utp_activate(void);

static int
globus_l_utp_deactivate(void);


globus_module_descriptor_t              globus_i_utp_module =
{
    "globus_utp",
    globus_l_utp_activate,
    globus_l_utp_deactivate,
    GLOBUS_NULL
};


/******************************************************************************
                   globus_utp module activation functions
******************************************************************************/

static int
globus_l_utp_activate(void)
{
    if (globus_module_activate(GLOBUS_COMMON_MODULE) != GLOBUS_SUCCESS)
    {
        return GLOBUS_FAILURE;
    }

    /* Initialize some global variables */
    globus_utp_attributes = GLOBUS_NULL;
    globus_utp_numTimers = 0;
    globus_utp_timers = GLOBUS_NULL;

    return GLOBUS_SUCCESS;
}


static int
globus_l_utp_deactivate(void)
{
    int rc;
    unsigned i;
    globus_utp_Attribute_t *curAttr;
    globus_utp_Attribute_t *tmpAttr;

    rc = GLOBUS_SUCCESS;

    /*
     * Free up any storage
     */
    curAttr = globus_utp_attributes;
    while (curAttr)
    {

        tmpAttr = curAttr;
        curAttr = curAttr->next;
        globus_free(tmpAttr);
    }
    globus_utp_attributes = GLOBUS_NULL;

    for (i = 0; i < globus_utp_numTimers; i++)
    {
        if (globus_utp_timers[i].name)
        {
            globus_free(globus_utp_timers[i].name);
        }
    }
    globus_free(globus_utp_timers);
    globus_utp_numTimers = 0;
    globus_utp_timers = GLOBUS_NULL;

    /*
     * Deactivate globus_common
     */
    if (globus_module_deactivate(GLOBUS_COMMON_MODULE) != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_FAILURE;
    }

    return rc;

}


/**********************************************************************
Public UTP functions.
**********************************************************************/

int globus_utp_init(unsigned numTimers, int mode)
{
        unsigned i;
        int sharingMode = mode & GLOBUS_UTP_MODE_SHARING_FIELD;
        char hostnameBuff[MAXHOSTNAMELEN];
        time_t theTime;
        const char *username;
        char theTimeStr[27];

        if (sharingMode == GLOBUS_UTP_MODE_PRIVATE)
        {
            globus_utp_warn("globus_utp_init: mode GLOBUS_UTP_MODE_PRIVATE not yet "
                            "implemented");
            return 1;
        } else if (sharingMode != GLOBUS_UTP_MODE_SHARED)
        {
            globus_utp_warn("globus_utp_init: invalid sharing mode %#X specified",
                            sharingMode);
            return 1;
        }

        if (!(globus_utp_timers = globus_malloc(numTimers * sizeof(globus_utp_Timer_t))))
        {
                globus_utp_warn("globus_utp_init: globus_malloc() failed for timers");
                globus_utp_numTimers = 0;
                return 1;
        }

        globus_utp_numTimers = numTimers;

                                /* This loop not only initializes every
                                   field; by "touching" everything, it also
                                   makes sure it is paged in. */
        for (i = 0; i < numTimers; i++) {
                globus_utp_timers[i].state = GLOBUS_UTP_TIMER_STOPPED;
                globus_utp_timeZero(&globus_utp_timers[i].startTime);
                globus_utp_timeZero(&globus_utp_timers[i].accumulatedTime);
                globus_utp_timers[i].numEvents = 0;
                                /* If name is still NULL when
                                   globus_utp_write_file() is called,
                                   globus_utp_DEFAULT_TIMER_NAME is printed. */
                globus_utp_timers[i].name = NULL;
        }

                                /* Make key/value linked list empty. */
        globus_utp_attributes = NULL;

                                /* Set attributes for "user", "hostname",
                                   and "timestamp". */

#       ifdef _WIN32
        {
            username = getenv("USERNAME");
            if (username == NULL)
            {
                username = "globus";
            }
        }
#       else
        {
            struct passwd *passwdEntry;
            passwdEntry = getpwuid(getuid());
            if (!passwdEntry) {
                return 1;
            }
            username = passwdEntry->pw_name;
        }
        globus_utp_set_attribute("%s", "user", username);
#endif

        if (globus_libc_gethostname(hostnameBuff, MAXHOSTNAMELEN)) {
            /* globus_utp_warn("globus_utp_init: gethostname() failed; system error is "
                         "\"%s\"", sys_errlist[errno]); */
                return 1;
        }
        globus_utp_set_attribute("%s", "hostname", hostnameBuff);

        if ((theTime = time(NULL)) == (time_t) -1) {
            /* globus_utp_warn("globus_utp_init: time(3) failed; system error is "
                         "\"%s\"", sys_errlist[errno]); */
                return 1;
        }
        strcpy(theTimeStr, ctime(&theTime));
                                /* Remove trailing '\n'. */
        theTimeStr[strlen(theTimeStr) - 1] = 0;
        globus_utp_set_attribute("%s", "timestamp", theTimeStr);

        return 0;
}

void
globus_utp_write_file(const char *outFilename)
{
    FILE *outFile;
    unsigned i;
    char buff[100];
    globus_utp_Attribute_t *curAttr;

    if (!(outFile = fopen(outFilename, "w")))
    {
        /* globus_utp_warn("globus_utp_write_file: fopen() on file \"%s\" failed",
                        outFilename);
        globus_utp_warn("globus_utp_write_file: system error message is \"%s\"",
                        sys_errlist[errno]); */
        return;
    }

    curAttr = globus_utp_attributes;
    while (curAttr)
    {
        globus_utp_Attribute_t *tmp = curAttr;
        fprintf(outFile, "attribute\t\"%s\"\t\"%s\"\n",
                curAttr->key, curAttr->value);
        curAttr = curAttr->next;
        globus_free(tmp->key);
        globus_free(tmp->value);
        globus_free(tmp);
    }
    globus_utp_attributes = NULL;

    for (i = 0; i < globus_utp_numTimers; i++)
    {
        const char *name = globus_utp_timers[i].name;

        if (globus_utp_timers[i].state == GLOBUS_UTP_TIMER_RUNNING)
        {
            globus_utp_warn("globus_utp_write_file: timer %d still running",
                            i);
            globus_utp_stop_timer(i);
        }

        globus_utp_timeToString(buff,
                                &globus_utp_timers[i].accumulatedTime);

        fprintf(outFile, "timer\t%u\t%s\t%lu\t\"%s\"\n",
                (unsigned) i,
                buff,
                globus_utp_timers[i].numEvents,
                name ? name : GLOBUS_UTP_DEFAULT_TIMER_NAME);
    }

    if (fclose(outFile))
    {
        /* globus_utp_warn("globus_utp_write_file: fclose() on file \"%s\" failed",
                        outFilename);
        globus_utp_warn("globus_utp_write_file: system error message is \"%s\"",
                        sys_errlist[errno]); */
        return;
    }
}

void
globus_utp_set_attribute(const char *keyStr, const char *keyArg,
                  const char *valueStr, ...)
{
        globus_utp_Attribute_t *newAttr;        /* New key/value pair. */
        globus_utp_Attribute_t **lastNext;      /* Final "next" pointer in list. */
        char *newKey = globus_malloc(sizeof(char) * GLOBUS_UTP_MAX_NAME_LENGTH);
        char *newValue = globus_malloc(sizeof(char) * GLOBUS_UTP_MAX_NAME_LENGTH);
        va_list ap;

        if (!newKey || !newValue) {
                globus_utp_warn("globus_utp_set_attribute: globus_malloc() failed");
                return;
        }
        sprintf(newKey, keyStr, keyArg);        /* Compute new key. */
        va_start(ap, valueStr);                 /* Compute new value. */
        vsprintf(newValue, valueStr, ap);

        newAttr = globus_utp_attributes;
        lastNext = &globus_utp_attributes;
                                        /* Search for same key in list. */
        while (newAttr && strcmp(newAttr->key, newKey) != 0) {
                lastNext = &newAttr->next;
                newAttr = newAttr->next;
        }

        if (newAttr) {                  /* Key already exists in list. */
                                        /* Just replace the old value. */
                strcpy(newAttr->value, newValue);
                globus_free(newKey);
                globus_free(newValue);
                return;
        }

                                /* Add new key/value pair to list. */
        *lastNext = newAttr = globus_malloc(sizeof(globus_utp_Attribute_t));
        if (!newAttr) {
                globus_utp_warn("globus_utp_set_attribute: globus_malloc() failed");
                return;
        }
        newAttr->key = newKey;
        newAttr->value = newValue;
        newAttr->next = NULL;
}


/**********************************************************************
Timer state transition diagram.  If timer is in "Current State", and
"State Change Function" is called, result is either the indicated new state
or "Invalid" (in which case globus_utp_warn() is called).

                                State Change Function
Current
State           start()    stop()     reset()    disable()   enable()
-------         -------    ------     -------    ---------   ---------
STOPPED         RUNNING    Invalid    STOPPED    DISABLED    STOPPED

RUNNING         Invalid    STOPPED    STOPPED    Invalid     RUNNING

DISABLED        DISABLED   DISABLED   DISABLED   DISABLED    STOPPED
**********************************************************************/

void globus_utp_start_timer(unsigned timerNumber)
{
        globus_utp_Timer_t *theTimer;

                /* Don't compile run-time checks into fully-optimized
                   version of the library. */
#ifndef NDEBUG
        if (timerNumber >= globus_utp_numTimers) {
                globus_utp_warn("globus_utp_start_timer: illegal timer %d",
                         (int) timerNumber);
                return;
        }
#endif /* #ifndef NDEBUG */

        theTimer = &globus_utp_timers[timerNumber];
        if (theTimer->state == GLOBUS_UTP_TIMER_DISABLED)
                return;

#ifndef NDEBUG
        if (theTimer->state == GLOBUS_UTP_TIMER_RUNNING) {
                globus_utp_warn("globus_utp_start_timer: timer %d already running",
                         (int) timerNumber);
                return;
        }
#endif /* #ifndef NDEBUG */

        theTimer->state = GLOBUS_UTP_TIMER_RUNNING;
        globus_utp_readTime(&theTimer->startTime);
}

void globus_utp_stop_timer(unsigned timerNumber)
{
        globus_utp_Timer_t *theTimer;
        globus_utp_TimeValue_t stopTime, elapsedTime;

#ifndef NDEBUG
        if (timerNumber >= globus_utp_numTimers) {
                globus_utp_warn("globus_utp_stop_timer: illegal timer %d",
                         (int) timerNumber);
                return;
        }
#endif /* #ifndef NDEBUG */

        theTimer = &globus_utp_timers[timerNumber];
        if (theTimer->state == GLOBUS_UTP_TIMER_DISABLED)
                return;

#ifndef NDEBUG
        if (theTimer->state == GLOBUS_UTP_TIMER_STOPPED) {
                globus_utp_warn("globus_utp_stop_timer: timer %d not running",
                         (int) timerNumber);
                return;
        }
#endif /* #ifndef NDEBUG */

        theTimer->state = GLOBUS_UTP_TIMER_STOPPED;
        globus_utp_readTime(&stopTime);
        globus_utp_timeDifference(&elapsedTime, &theTimer->startTime, &stopTime);
        globus_utp_timeAdd(&theTimer->accumulatedTime, &elapsedTime);
        theTimer->numEvents++;
}

void globus_utp_reset_timer(unsigned timerNumber)
{
        globus_utp_Timer_t *theTimer;

#ifndef NDEBUG
        if (timerNumber >= globus_utp_numTimers) {
                globus_utp_warn("globus_utp_reset_timer: illegal timer %d",
                         (int) timerNumber);
                return;
        }
#endif /* #ifndef NDEBUG */

        theTimer = &globus_utp_timers[timerNumber];
        if (theTimer->state == GLOBUS_UTP_TIMER_DISABLED)
                return;

        theTimer->state = GLOBUS_UTP_TIMER_STOPPED;
        globus_utp_timeZero(&theTimer->startTime);
        globus_utp_timeZero(&theTimer->accumulatedTime);
        theTimer->numEvents = 0;
}

void globus_utp_disable_timer(unsigned timerNumber)
{
        globus_utp_Timer_t *theTimer;

#ifndef NDEBUG
        if (timerNumber >= globus_utp_numTimers) {
                globus_utp_warn("globus_utp_disable_timer: illegal timer %d",
                         (int) timerNumber);
                return;
        }
#endif /* #ifndef NDEBUG */

        theTimer = &globus_utp_timers[timerNumber];

#ifndef NDEBUG
        if (theTimer->state == GLOBUS_UTP_TIMER_RUNNING) {
                globus_utp_warn("globus_utp_disable_timer: timer %d is running",
                         (int) timerNumber);
                return;
        }
#endif /* #ifndef NDEBUG */

        theTimer->state = GLOBUS_UTP_TIMER_DISABLED;
}

void globus_utp_enable_timer(unsigned timerNumber)
{
        globus_utp_Timer_t *theTimer;

#ifndef NDEBUG
        if (timerNumber >= globus_utp_numTimers) {
                globus_utp_warn("globus_utp_disable_timer: illegal timer %d",
                         (int) timerNumber);
                return;
        }
#endif /* #ifndef NDEBUG */

        theTimer = &globus_utp_timers[timerNumber];

                        /* If STOPPED or RUNNING, already enabled.
                           Otherwise, can only get to DISABLED from
                           STOPPED, so go there. */
        if (theTimer->state == GLOBUS_UTP_TIMER_DISABLED)
                theTimer->state = GLOBUS_UTP_TIMER_STOPPED;
}

void globus_utp_disable_all_timers(void)
{
        unsigned i;

        for (i = 0; i < globus_utp_numTimers; i++)
                globus_utp_disable_timer(i);
}

void globus_utp_enable_all_timers(void)
{
        unsigned i;

        for (i = 0; i < globus_utp_numTimers; i++)
                globus_utp_enable_timer(i);
}

const char *
globus_utp_name_timer(unsigned timerNumber, const char *nameStr, ...)
{
        char *name;
        va_list ap;

#ifndef NDEBUG
        if (timerNumber >= globus_utp_numTimers) {
                globus_utp_warn("globus_utp_name_timer: illegal timer %d",
                         (int) timerNumber);
                return NULL;
        }
#endif /* #ifndef NDEBUG */
        if (!(name = globus_malloc(sizeof(char) * GLOBUS_UTP_MAX_NAME_LENGTH))) {
                globus_utp_warn("globus_utp_name_timer: globus_malloc() failed");
                return NULL;
        }

        va_start(ap, nameStr);
        vsprintf(name, nameStr, ap);
        globus_utp_timers[timerNumber].name = name;

        return name;
}

void
globus_utp_get_accum_time(unsigned timerNumber, double *time, int *precision)
{
        globus_utp_Timer_t *theTimer;

#ifndef NDEBUG
        if (timerNumber >= globus_utp_numTimers) {
                globus_utp_warn("globus_utp_get_accum_time: illegal timer %d",
                         (int) timerNumber);
                return;
        }
#endif /* #ifndef NDEBUG */

        theTimer = &globus_utp_timers[timerNumber];

        globus_utp_timeToDouble(time, precision, &theTimer->accumulatedTime);
}


/**********************************************************************
Private UTP functions.
**********************************************************************/

                        /* Use external assembler routine for RS6000 by
                           default, unless RS6000_GETTIMER is defined. */
#if !defined(RS6000) || (defined(RS6000) && defined(RS6000_GETTIMER))

void
globus_utp_readTime(globus_utp_TimeValue_t *tv)
{
#ifdef UNIX
#ifdef DEBUG
        int flag;

        flag = gettimeofday(tv, NULL);

        if (flag) {
                globus_utp_warn("globus_utp_readTime: gettimeofday() failed");
                globus_utp_warn("globus_utp_readTime: system error message is \"%s\"",
                         sys_errlist[errno]);
        }
#else
        gettimeofday(tv, NULL);
#endif /* #ifdef DEBUG */
#endif /* #ifdef UNIX */


#ifdef SOLHR
        *tv = gethrtime();
#endif

#ifdef RS6000

/*
The AIX 3.2.5 on-line "info" about gettimer() presents a totally wrong
calling interface for gettimer().  The code below is based on the example
on p. 59 of the "Optimizing and Tuning Guide for FORTRAN, C, and C++", and
seems to work just fine.
*/

#ifdef DEBUG
        int flag;

        flag = gettimer(TIMEOFDAY, tv);

        if (flag) {
                globus_utp_warn("globus_utp_readTime: gettimer() failed");
                globus_utp_warn("globus_utp_readTime: system error message is \"%s\"",
                         sys_errlist[errno]);
        }
#else
        gettimer(TIMEOFDAY, tv);
#endif /* #ifdef DEBUG */

#endif /* #ifdef RS6000 */

#ifdef PARAGON
        *tv = dclock();
#endif  /* #ifdef PARAGON */
}

#endif  /* #if !defined(RS6000) || .... */


void
globus_utp_timeDifference(globus_utp_TimeValue_t *diff, const globus_utp_TimeValue_t *start,
                   const globus_utp_TimeValue_t *end)
{
#ifdef UNIX
        diff->tv_sec = end->tv_sec - start->tv_sec;

        if (end->tv_usec < start->tv_usec) {
                diff->tv_sec--;         /* "Borrow" from seconds. */
                diff->tv_usec = 1000000 + end->tv_usec - start->tv_usec;
        } else
                diff->tv_usec = end->tv_usec - start->tv_usec;
#endif /* #ifdef UNIX */

#ifdef RS6000
        diff->tv_sec = end->tv_sec - start->tv_sec;

        if (end->tv_nsec < start->tv_nsec) {
                diff->tv_sec--;         /* "Borrow" from seconds. */
                diff->tv_nsec = 1000000000 + end->tv_nsec - start->tv_nsec;
        } else
                diff->tv_nsec = end->tv_nsec - start->tv_nsec;
#endif /* #ifdef RS6000 */

#ifdef SOLHR
        *diff = *end - *start;
#endif

#ifdef PARAGON
        *diff = *end - *start;
#endif  /* #ifdef PARAGON */
}


void
globus_utp_timeAdd(globus_utp_TimeValue_t *oldElapsed,
            const globus_utp_TimeValue_t *newElapsed)
{
#ifdef UNIX
        oldElapsed->tv_usec += newElapsed->tv_usec;

        if (oldElapsed->tv_usec >= 1000000) {
                oldElapsed->tv_sec++;   /* "Carry" into seconds. */
                oldElapsed->tv_usec -= 1000000;
        }

        oldElapsed->tv_sec += newElapsed->tv_sec;
#endif /* #ifdef UNIX */

#ifdef RS6000
        oldElapsed->tv_nsec += newElapsed->tv_nsec;

        if (oldElapsed->tv_nsec >= 1000000000) {
                oldElapsed->tv_sec++;   /* "Carry" into seconds. */
                oldElapsed->tv_nsec -= 1000000000;
        }

        oldElapsed->tv_sec += newElapsed->tv_sec;
#endif /* #ifdef RS6000 */

#ifdef SOLHR
        *oldElapsed += *newElapsed;
#endif

#ifdef PARAGON
        *oldElapsed += *newElapsed;
#endif  /* #ifdef PARAGON */
}


void
globus_utp_timeZero(globus_utp_TimeValue_t *tv)
{
#ifdef UNIX
        tv->tv_sec = 0;
        tv->tv_usec = 0;
#endif /* #ifdef UNIX */

#ifdef RS6000
        tv->tv_sec = 0;
        tv->tv_nsec = 0;
#endif /* #ifdef RS6000 */

#ifdef SOLHR
        *tv = 0;
#endif

#ifdef PARAGON
        *tv = 0.0;
#endif  /* #ifdef PARAGON */
}


void
globus_utp_timeToDouble(double *time, int *precision, const globus_utp_TimeValue_t *tv)
{
#ifdef UNIX
        *time = (double) tv->tv_sec + ((double) tv->tv_usec) / 1000000.0;
        *precision = 6;
#endif /* #ifdef UNIX */

#ifdef RS6000
        *time = (double) tv->tv_sec + ((double) tv->tv_nsec) /
                1000000000.0;
        *precision = 7;
#endif /* #ifdef RS6000 */

#ifdef SOLHR
        *time = ((double)*tv) / 1000000000.0;
        *precision = 6;
#endif

#ifdef PARAGON
        *time = *tv;
        *precision = 7;
#endif  /* #ifdef PARAGON */
}


void
globus_utp_timeToString(char timeString[], const globus_utp_TimeValue_t *tv)
{
        double tvAsDouble;
        int precision;

        globus_utp_timeToDouble(&tvAsDouble, &precision, tv);

        sprintf(timeString, "%.*f", precision, tvAsDouble);
}


void
globus_utp_warn(const char *messageStr, ...)
{
#   if defined(DEBUG)
    {
        va_list ap;

        va_start(ap, messageStr);

        vfprintf(stderr, messageStr, ap);
        fprintf(stderr, "\n");
        fflush(stderr);         /* Just to be safe. */
    }
#   endif
}


/**********************************************************************
Private UTP globals.
**********************************************************************/

unsigned globus_utp_numTimers = 0;
globus_utp_Timer_t *globus_utp_timers = NULL;
globus_utp_Attribute_t *globus_utp_attributes;
