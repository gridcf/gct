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

/**
 * @file globus_time.h
 * @brief Time Types and Macros
 */

#if !defined(GLOBUS_TIME_H)
#define      GLOBUS_TIME_H

#include "globus_types.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLOBUS_I_TIME_INFINITY_SEC   INT_MAX
#define GLOBUS_I_TIME_INFINITY_NSEC  INT_MAX
#define GLOBUS_I_TIME_INFINITY_USEC  INT_MAX

#if defined(_WIN32) && !defined(_POSIX)
typedef struct globus_abstime_s
{
   time_t  tv_sec;
   long    tv_nsec;
} globus_abstime_t;
#else
typedef struct timespec      globus_abstime_t;
#endif

typedef struct timeval  globus_reltime_t;

/**
 *  Set the abstime structure to the sec and usec parameter values.
 */
#define  GlobusTimeAbstimeSet(Abstime, Sec, USec)         \
{                                                         \
    GlobusTimeAbstimeGetCurrent(Abstime);                 \
    (Abstime).tv_nsec += (USec * 1000);                   \
    if((Abstime).tv_nsec >= 1000000000)                    \
    {                                                     \
        (Abstime).tv_sec += ((Abstime).tv_nsec / 1000000000);\
        (Abstime).tv_nsec = (Abstime).tv_nsec  % 1000000000; \
    }                                                     \
    (Abstime).tv_sec += Sec;                              \
}
/**
 *  Separates abstime structure into its components, sec and usec.
 */
#define  GlobusTimeAbstimeGet(Abstime, Sec, USec)         \
{                                                         \
    Sec = (Abstime).tv_sec;                               \
    USec = ((Abstime).tv_nsec / 1000);                    \
}

/**
 *  Set the reltime structure to the sec and usec parameter values.
 */
#define  GlobusTimeReltimeSet(Reltime, Sec, USec)         \
{                                                         \
    (Reltime).tv_usec = (USec);                           \
    (Reltime).tv_sec = Sec;                               \
    if((Reltime).tv_usec >= 1000000)                      \
    {                                                     \
        (Reltime).tv_sec += ((Reltime).tv_usec / 1000000);\
        (Reltime).tv_usec = (Reltime).tv_usec  % 1000000; \
    }                                                     \
}

#define  GlobusTimeReltimeGet(Reltime, Sec, USec)         \
{                                                         \
    (USec) = (Reltime).tv_usec;                           \
    (Sec) = (Reltime).tv_sec;                             \
}

#define  GlobusTimeAbstimePrintf(Abstime)                 \
{                                                         \
    printf("sec  -->%lu\n", (unsigned long) (Abstime).tv_sec);            \
    printf("nsec -->%lu\n", (unsigned long) (Abstime).tv_nsec);           \
}

#define  GlobusTimeReltimePrintf(Reltime)                 \
{                                                         \
    printf("sec  -->%lu\n", (unsigned long) (Reltime).tv_sec);            \
    printf("usec -->%lu\n", (unsigned long) (Reltime).tv_usec);           \
}

/**
 *  Find the difference between the 2 absolute times.
 */
#define  GlobusTimeAbstimeDiff(Reltime, T1, T2)           \
{                                                         \
    int __res = globus_abstime_cmp(&(T1), &(T2));         \
    if(__res < 0)                                         \
    {                                                     \
        (Reltime).tv_sec = (T2).tv_sec - (T1).tv_sec;     \
        (Reltime).tv_usec =                               \
                (((T2).tv_nsec - (T1).tv_nsec) / 1000);   \
        if((Reltime).tv_usec < 0)                         \
        {                                                 \
            (Reltime).tv_sec--;                           \
            (Reltime).tv_usec += 1000000;                 \
        }                                                 \
    }                                                     \
    else if(__res > 0)                                    \
    {                                                     \
        (Reltime).tv_sec = (T1).tv_sec - (T2).tv_sec;     \
        (Reltime).tv_usec =                               \
                (((T1).tv_nsec - (T2).tv_nsec) / 1000);   \
        if((Reltime).tv_usec < 0)                         \
        {                                                 \
            (Reltime).tv_sec--;                           \
            (Reltime).tv_usec += 1000000;                 \
        }                                                 \
    }                                                     \
    else                                                  \
    {                                                     \
        (Reltime).tv_sec = 0;                             \
        (Reltime).tv_usec = 0;                            \
    }                                                     \
}

#define  GlobusTimeReltimeDiff(Reltime, T1, T2)           \
{                                                         \
    int __res = globus_reltime_cmp(&(T1), &(T2));         \
    if(__res < 0)                                         \
    {                                                     \
        (Reltime).tv_sec = (T2).tv_sec - (T1).tv_sec;     \
        (Reltime).tv_usec =                               \
                ((T2).tv_usec - (T1).tv_usec);            \
        if((Reltime).tv_usec < 0)                         \
        {                                                 \
            (Reltime).tv_sec--;                           \
            (Reltime).tv_usec += 1000000;                 \
        }                                                 \
    }                                                     \
    else if(__res > 0)                                    \
    {                                                     \
        (Reltime).tv_sec = (T1).tv_sec - (T2).tv_sec;     \
        (Reltime).tv_usec =                               \
                ((T1).tv_usec - (T2).tv_usec);            \
        if((Reltime).tv_usec < 0)                         \
        {                                                 \
            (Reltime).tv_sec--;                           \
            (Reltime).tv_usec += 1000000;                 \
        }                                                 \
    }                                                     \
    else                                                  \
    {                                                     \
        (Reltime).tv_sec = 0;                             \
        (Reltime).tv_usec = 0;                            \
    }                                                     \
}

/**
 *  Convert a relitive time into a long in usec units
 */
#define  GlobusTimeReltimeToUSec(SlpInt, Reltime)         \
{                                                         \
    SlpInt = ((Reltime).tv_sec * 1000000) +               \
                                     ((Reltime).tv_usec); \
}

/**
 *  Convert a relative time into a long in millisec units
 */
#define  GlobusTimeReltimeToMilliSec( Milliseconds, Reltime)  \
{                                                         \
    Milliseconds = ((Reltime).tv_sec * 1000) +            \
                              ((Reltime).tv_usec)/ 1000;   \
}

/**
 *  Add reltime to abstime
 */
#define  GlobusTimeAbstimeInc(Abstime, Reltime)           \
{                                                         \
    (Abstime).tv_nsec += ((Reltime).tv_usec * 1000);      \
    if((Abstime).tv_nsec >= 1000000000)                    \
    {                                                     \
        (Abstime).tv_sec++;                               \
        (Abstime).tv_nsec -= 1000000000;                  \
    }                                                     \
    (Abstime).tv_sec += (Reltime).tv_sec;                 \
}

#define  GlobusTimeAbstimeDec(Abstime, Reltime)           \
{                                                         \
    (Abstime).tv_nsec -= ((Reltime).tv_usec * 1000);      \
    if((Abstime).tv_nsec < 0)                             \
    {                                                     \
        (Abstime).tv_sec--;                               \
        (Abstime).tv_nsec += 1000000000;                  \
    }                                                     \
    (Abstime).tv_sec -= (Reltime).tv_sec;                 \
}


/**
 *  Get the current time
 */
#if defined(_WIN32)
#   define GlobusTimeAbstimeGetCurrent(Abstime)           \
    {                                                     \
        struct _timeb timebuffer;                      \
                                                          \
        _ftime(&timebuffer);                            \
        (Abstime).tv_sec = timebuffer.time;               \
        (Abstime).tv_nsec = (timebuffer.millitm * 1000000);  \
    }
#else
#   define  GlobusTimeAbstimeGetCurrent(Abstime)          \
    {                                                     \
        struct timeval __time;                            \
                                                          \
        gettimeofday(&__time, GLOBUS_NULL);               \
        (Abstime).tv_sec = __time.tv_sec;                 \
        (Abstime).tv_nsec = (__time.tv_usec * 1000);      \
    }
#endif

/**
 *  Copy the absolute time
 */
#define  GlobusTimeAbstimeCopy(Dest, Src)                 \
{                                                         \
   (Dest).tv_sec = (Src).tv_sec;                          \
   (Dest).tv_nsec = (Src).tv_nsec;                        \
}

/**
 *  Copy the relative time
 */
#define  GlobusTimeReltimeCopy(Dest, Src)                 \
{                                                         \
   (Dest).tv_sec = (Src).tv_sec;                          \
   (Dest).tv_usec = (Src).tv_usec;                        \
}

/**
 *  Multiple the reltime by factor
 */
#define  GlobusTimeReltimeMultiply(Reltime, Factor)       \
{                                                         \
   (Reltime).tv_usec *= Factor;                           \
   (Reltime).tv_sec *= Factor;                            \
                                                          \
    if((Reltime).tv_usec >= 1000000)                      \
    {                                                     \
        (Reltime).tv_sec += ((Reltime).tv_usec / 1000000);\
        (Reltime).tv_usec = (Reltime).tv_usec  % 1000000; \
    }                                                     \
}

/**
 *  divide the reltime by factor
 */
#define  GlobusTimeReltimeDivide(Reltime, Factor)         \
{                                                         \
   (Reltime).tv_usec /= Factor;                           \
   (Reltime).tv_sec /= Factor;                            \
}

extern const globus_abstime_t         globus_i_abstime_infinity;
extern const globus_abstime_t         globus_i_abstime_zero;
extern const globus_reltime_t         globus_i_reltime_infinity;
extern const globus_reltime_t         globus_i_reltime_zero;

/**
 *  Has abstime expired
 *
 *  Returns a boolean that reflects whether or not abstime is less than the
 *  current time.
 */
globus_bool_t
globus_time_has_expired(
    const globus_abstime_t *                     abstime);

/**
 *  Returns a boolean that reflects whether or not abstime is infinity.
 */
globus_bool_t
globus_time_abstime_is_infinity(
    const globus_abstime_t *                     abstime);

/**
 *  Returns a boolean that reflects whether or not reltime is infinity.
 */
globus_bool_t
globus_time_reltime_is_infinity(
    const globus_reltime_t *                     reltime);

/**
 *  Compare two absolute times.
 *
 *  This function returns an integer that reflects the comparison of two 
 *  abstimes in the following way.
 *
 *  0  :  values are the same.
 *  -1 :  the first value is less than the second.
 *  1  :  the first value is greater than the second.
 */
int
globus_abstime_cmp(
    const globus_abstime_t *                     abstime_1,
    const globus_abstime_t *                     abstime_2);

/**
 *  Compare two absolute times.
 *
 *  This function returns an integer that reflects the comparison of two 
 *  reltimes in the following way.
 *
 *  0  :  values are the same.
 *  -1 :  the first value is less than the second.
 *  1  :  the first value is greater than the second.
 */
int
globus_reltime_cmp(
    const globus_reltime_t *                     reltime_1,
    const globus_reltime_t *                     reltime_2);


#ifdef __cplusplus
}
#endif
#endif /* GLOBUS_TIME_H */
