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
 oldgaa_policy_evaluator.c:

Description:
	This file is used internally by the oldgaa routines
**********************************************************************/

/**********************************************************************
                             Include header files
**********************************************************************/
#include <stdio.h>	/* File reading and writing */
#include <errno.h>	/* For errno */
#include <stdlib.h>	/* For malloc() */
#include <string.h> 	/* strerror() and other string functions */

#include "globus_oldgaa.h"
#include "oldgaa_utils.h"
#include "globus_oldgaa_utils.h"
#include "oldgaa_policy_evaluator.h"
#include "oldgaa_gl_internal_err.h"

/**********************************************************************
                       Define module specific variables
**********************************************************************/

#define STRING_LENGTH			80


/**********************************************************************
             Helpers Static Function Declarations
 **********************************************************************/


static
oldgaa_error_code
evaluate_condition(oldgaa_sec_context_ptr sc,
                   oldgaa_conditions_ptr  condition,
                   oldgaa_options_ptr     options);

static
char *
get_day();

static
char *
get_hr_24();

/*  static */
/*  char * */
/*  get_hr_12() */

static
char *
get_minutes();

static
char *
get_seconds();

/*  static */
/*  char * */
/*  get_am_pm() */

static
int
day_to_val(char *str);

static
int
check_day(char *str1, char *str2, char *day);

static
char*
get_value(int *jj, const char *cond, const char delimiter);


/**********************************************************************

Function: oldgaa_find_matching_entry

Description:
	Finds policy corresponding to the given principal.

Parameters:
        minor_status, mechanism-specific error code.
	ptr, pointer to principal.
        policy, pointer to policy.

Returns:
	Pointer to a oldgaa_policy structure if successful

**********************************************************************/


oldgaa_policy_ptr
oldgaa_find_matching_entry(uint32             *minor_status,
                        oldgaa_principals_ptr  ptr,
                        oldgaa_policy_ptr      policy)
{
    oldgaa_policy_ptr entry = policy;

    /* Check arguments */
    if (!ptr)
    {
        errno = ERRNO_INVALID_ARGUMENT;
        *minor_status = -1;

        goto exit;
    }

    while(entry)
    {
        if(oldgaa_strings_match(entry->type, OLDGAA_ANYBODY)) return entry;

        /* do exact match */
        if(oldgaa_compare_principals(ptr, entry))  return entry;

#ifdef PRINCIPALS_REGEX_MATCH
        {
            char    **subject_regexes = NULL; /* NULL terminated regex list */
            int i;

#ifdef DEBUG
            fprintf(stderr, "%s %s\n", ptr->value, entry->value);
#endif /* DEBUG */

            subject_regexes = oldgaa_parse_regex(entry->value);

            if(subject_regexes)
            {
                if(oldgaa_check_reg_expr(ptr->value, subject_regexes))
                {
                    for (i=0; subject_regexes[i] != NULL; i++)
                        free(subject_regexes[i]);
                    free(subject_regexes);
                    return entry;
                }
                for (i=0; subject_regexes[i] != NULL; i++)
                    free(subject_regexes[i]);
                free(subject_regexes);
            }
        }
#endif /* #ifdef PRINCIPALS_REGEX_MATCH  */

        entry = entry->next;
    }

    exit:
    return NULL;
}

/**********************************************************************

Function: oldgaa_check_access_rights

Description:
         Checks if the requested access rights are granted. It checks
         conditions, if any.

Parameters:


Returns:
        OLDGAA atatus
	
**********************************************************************/


oldgaa_error_code
oldgaa_check_access_right(oldgaa_sec_context_ptr sc,
                    oldgaa_rights_ptr      requested_rights,
                    oldgaa_rights_ptr      rights,
                    oldgaa_answer_ptr      detailed_answer,
                    oldgaa_options_ptr     options)
{
 oldgaa_error_code oldgaa_status;
 int            was_no = FALSE, was_maybe = FALSE;

#ifdef DEBUG
fprintf(stderr, "\noldgaa_check_access_rights:\n");
#endif /* DEBUG */

  /* check if the requested rights match rights in the policy */

 if (!oldgaa_compare_rights(requested_rights, rights))
 return OLDGAA_NO; /* now we have just one type of rights: CA:sign */

#ifdef DEBUG
fprintf(stderr, "right is granted\n");
#endif /* DEBUG */

  detailed_answer->rights = rights;
  rights->reference_count++;

   if(rights->cond_bindings) /* operation is allowed and there are
                                          some conditions */

   {
#ifdef DEBUG
fprintf(stderr, "there are some conditions\n");
#endif /* DEBUG */

 oldgaa_status = oldgaa_evaluate_conditions(sc,
                                            rights->cond_bindings,
                                            options);

    if(oldgaa_status == OLDGAA_NO)    was_no    = TRUE;
    if(oldgaa_status == OLDGAA_MAYBE) was_maybe = TRUE;

   }

  if(was_no)    return OLDGAA_NO;
  if(was_maybe) return OLDGAA_MAYBE;

  return OLDGAA_YES;  /* operation is allowed and either there are NO any
                     or all conditions are met */
}

oldgaa_error_code
oldgaa_check_access_rights(oldgaa_sec_context_ptr sc,
                    oldgaa_rights_ptr      requested_rights,
                    oldgaa_rights_ptr      rights,
                    oldgaa_answer_ptr      detailed_answer,
                    oldgaa_options_ptr     options)
{
    /*** this does not do the right thing for negative rights */
    oldgaa_rights_ptr right;
    oldgaa_error_code status = OLDGAA_NO;

    for (right = rights; right; right = right->next)
	if ((status = oldgaa_check_access_right(sc, requested_rights, right, detailed_answer, options)) == OLDGAA_YES)
	    break;
    return(status);
}
/*****************************************************************************/

oldgaa_error_code
oldgaa_get_authorized_principals(oldgaa_sec_attrb_ptr *attributes,
                              oldgaa_policy_ptr     policy,
                              oldgaa_principals_ptr principal,
                              oldgaa_rights_ptr     rights)

{
  oldgaa_policy_ptr    entry  = policy;
  int               was_anybody    = 0;
  int               number_of_entries = 1;
  oldgaa_sec_attrb_ptr attrb = NULL;
  oldgaa_error_code    oldgaa_status = OLDGAA_SUCCESS;

#ifdef DEBUG
fprintf(stderr, "\noldgaa_get_authorized_principals:\n");
#endif /* DEBUG */

/* Check arguments */
  if (!policy && !attributes)
  {
    errno = ERRNO_INVALID_ARGUMENT;
    return OLDGAA_FAILURE;
  }


   while(entry)
  {
    if(oldgaa_strings_match(entry->type, OLDGAA_ANYBODY) &&
       oldgaa_compare_rights(entry->rights, rights)) was_anybody = 1;


   if(oldgaa_strings_match(entry->type,      principal->type)   &&
      oldgaa_strings_match(entry->authority, principal->authority))
  {
    if(oldgaa_compare_rights(entry->rights, rights))
       {
         oldgaa_allocate_sec_attrb(&attrb);
         attrb->type      = oldgaa_strcopy(entry->type,      attrb->type);
         attrb->authority = oldgaa_strcopy(entry->authority, attrb->authority);
         attrb->value     = oldgaa_strcopy(entry->value,     attrb->value);

       if(*attributes == NULL) { *attributes = attrb; }

         oldgaa_add_attribute(attributes, attrb);
         number_of_entries++;
       }
  }

   entry = entry->next;

  } /* end of while */

    if(was_anybody && (number_of_entries == 1)) /* return ANYBODY only if it is the only entry of this type */
      {
         oldgaa_allocate_sec_attrb(&attrb);
         attrb->type      = oldgaa_strcopy(OLDGAA_ANYBODY, attrb->type);
         attrb->authority = oldgaa_strcopy(" ",         attrb->authority);
         attrb->value     = oldgaa_strcopy(" ",         attrb->value);

         if (*attributes == NULL) *attributes = attrb;
         else oldgaa_add_attribute(attributes, attrb);
       }

return  oldgaa_status;
}

/**********************************************************************

Function: oldgaa_evaluate_conditions()

Description:
	Walks throug condition list and evaluates each condition.

Parameters:
	security context, condition list and options

Returns:
	OLDGAA_YES   if all conditions are met.
	OLDGAA_NO    if at least one is not met.
	OLDGAA_MAYBE if some conditions are not evaluated.

**********************************************************************/

oldgaa_error_code
oldgaa_evaluate_conditions(oldgaa_sec_context_ptr    sc,
                           oldgaa_cond_bindings_ptr  conditions,
                           oldgaa_options_ptr        options)
{
   oldgaa_error_code         oldgaa_status = OLDGAA_NO;
   oldgaa_cond_bindings_ptr  cond       = conditions;
   int                    was_no     = FALSE, was_maybe = FALSE;

#ifdef DEBUG
fprintf(stderr, "\noldgaa_evaluate_conditions:\n");
#endif /* DEBUG */

 while(cond)/* walk throug condition list */
   {
     oldgaa_status = OLDGAA_MAYBE;

     oldgaa_status =  evaluate_condition(sc, cond->condition, options);

     if(oldgaa_status == OLDGAA_NO)    was_no    = TRUE;
     if(oldgaa_status == OLDGAA_MAYBE) was_maybe = TRUE;

     cond = cond->next;
   }

 if(was_no)    return OLDGAA_NO;
  if(was_maybe) return OLDGAA_MAYBE;

  return OLDGAA_YES;

}



oldgaa_error_code
oldgaa_evaluate_day_cond(oldgaa_conditions_ptr condition,
                  oldgaa_options_ptr    options)

{
   int            retval, j=0;
   oldgaa_error_code oldgaa_status = OLDGAA_NO;

   char *day = NULL, *str1 = NULL, *str2 = NULL, cond[MAX_COND_LENGTH] = {NUL};
   char *current_day = NULL;
   char *value;

   strcpy(cond, condition->value);

     /* get current day */
     current_day = get_day();
     day = oldgaa_strcopy(current_day, day);
     free(current_day);

     /* get first day delimiter */
     str1 = oldgaa_strcopy(get_value(&j, cond, '-'), str1);

     /* get second day delimiter */
     value = get_value(&j, cond, NUL);
     str2 = oldgaa_strcopy(value, str2);
     free(value);

     retval = check_day(str1, str2, day);

     if(retval == -1) return OLDGAA_MAYBE; /* unsupported day format */

     if(retval == 1) oldgaa_status = OLDGAA_YES;

   return oldgaa_status;

}

/**********************************************************************

Function: oldgaa_evaluate_regex_cond()

Description:
	Walks throug condition list and evaluates each condition.

Parameters:
	security context, condition list and options

Returns:
	OLDGAA_YES   if all conditions are met.
	OLDGAA_NO    if at least one is not met.
	OLDGAA_MAYBE if some conditions are not evaluated.

**********************************************************************/

oldgaa_error_code
oldgaa_evaluate_regex_cond(oldgaa_conditions_ptr condition,
                        oldgaa_options_ptr    options)
{
  char          **subject_regexes = NULL; /* NULL terminated list of regexes */
  int i;
  oldgaa_error_code  oldgaa_status      = OLDGAA_NO;

#ifdef DEBUG
fprintf(stderr, "oldgaa_evaluate_rege_cond:\n");
#endif /* DEBUG */

   subject_regexes = oldgaa_parse_regex(condition->value);

   if(!subject_regexes) return OLDGAA_FAILURE;

     if(oldgaa_check_reg_expr(options->value, subject_regexes))
      oldgaa_status = OLDGAA_YES;

     for (i=0; subject_regexes[i] != NULL; i++)
       free(subject_regexes[i]);
     free(subject_regexes);

return oldgaa_status;

}

/*****************************************************************************/

oldgaa_error_code
oldgaa_evaluate_time_cond(oldgaa_conditions_ptr condition,
                       oldgaa_options_ptr    options)

{
   int   j = 0;
   int   hr, min, sec;
   int   cond_hr, cond_min, cond_sec;
   char  cond[MAX_COND_LENGTH] = {NUL};

   strcpy(cond, condition->value);

  if(oldgaa_strings_match(condition->authority, HOUR_SCALE_24))
   {
     char *hr_str;
     char *min_str;
     char *sec_str;
     char *value;

     /* current hour    */
     hr_str = get_hr_24();
     hr  = atoi(hr_str);
     free(hr_str);

     /* current minutes */
     min_str = get_minutes();
     min = atoi(min_str);
     free(min_str);

     /* current seconds */
     sec_str = get_seconds();
     sec = atoi(sec_str);
     free(sec_str);

     /* get hours from condition value */
     value = get_value(&j, cond, ':');
     cond_hr = atoi(value);
     free(value);

     if (hr < cond_hr) return OLDGAA_NO;

     /* get minutes from condition value */
     value = get_value(&j, cond, ':');
     cond_min = atoi(value);
     free(value);

     /* get seconds from condition value */
     value = get_value(&j, cond, '-');
     cond_sec = atoi(value);
     free(value);


    if (cond_hr == hr) /* if hours are equal, check minutes */
    {
      if (min < cond_min) return OLDGAA_NO;

      if (cond_min == min) /* if minutes are equal, check seconds */
	{
          if (sec < cond_sec) return OLDGAA_NO;
          else goto success;
	}
    }

      /* hours are greater, check second time value */

     /* get hours from condition value */
     value = get_value(&j, cond, ':');
     cond_hr = atoi(value);
     free(value);

     if  (cond_hr < hr) return OLDGAA_NO;

    /* get minutes from condition value */
     value = get_value(&j, cond, ':');
     cond_min = atoi(value);
     free(value);

    /* get seconds from condition value */
     value = get_value(&j, cond, ':');
     cond_sec = atoi(value);
     free(value);


    if (cond_hr == hr) /* if hours are equal, check minutes */
    {
      if (cond_min < min) return OLDGAA_NO;

      if (cond_min == min) /* if minutes are equal, check seconds */
	{
          if (cond_sec <  sec) return OLDGAA_NO;
          else  goto success;
	}
    }


 success:

   return OLDGAA_YES;
   }

 return OLDGAA_MAYBE; /* unsupported time format */

}

/*****************************************************************************/


oldgaa_error_code
oldgaa_evaluate_sech_mech_cond(oldgaa_principals_ptr  principal,
                        oldgaa_conditions_ptr  condition,
                        oldgaa_options_ptr     options)

{
   oldgaa_error_code oldgaa_status = OLDGAA_NO;

   if (oldgaa_strings_match(condition->value, principal->authority))
   oldgaa_status = OLDGAA_YES;

   return oldgaa_status;

}



/**********************************************************************
             Helpers Static Functions
 **********************************************************************/


/**********************************************************************

Function: evaluate_condition()

Description:
	Invokes apropriate evaluation function for each condition.

Parameters:
	security context, condition list and options

Returns:
	OLDGAA_YES   if condition is met.
	OLDGAA_NO    if condition is not met.
	OLDGAA_MAYBE if evaluation function was not found.

**********************************************************************/

static
oldgaa_error_code
evaluate_condition(oldgaa_sec_context_ptr sc,
                   oldgaa_conditions_ptr  condition,
                   oldgaa_options_ptr     options)
{
  oldgaa_error_code oldgaa_status = OLDGAA_MAYBE;

#ifdef DEBUG
fprintf(stderr, "evaluate_condition: %s %s %s\n",
        condition->type,
        condition->authority,
        condition->value);
#endif /* DEBUG */


  if(!strcmp(condition->type,      COND_SUBJECTS) &&
     !strcmp(condition->authority, AUTH_GLOBUS))
     oldgaa_status = oldgaa_evaluate_regex_cond(condition, options);

  if(!strcmp(condition->type,      COND_BANNED_SUBJECTS) &&
     !strcmp(condition->authority, AUTH_GLOBUS))
    {
     oldgaa_status = oldgaa_evaluate_regex_cond(condition, options);
     if(oldgaa_status == OLDGAA_YES) oldgaa_status = OLDGAA_NO;
    }

#ifdef OLDGAA_COND_DAY
     if(!strcmp(condition->type, COND_DAY))
     oldgaa_status = oldgaa_evaluate_day_cond(condition, options);
#endif

#ifdef OLDGAA_COND_TIME
     if(!strcmp(condition->type, COND_TIME))
     oldgaa_status = oldgaa_evaluate_time_cond(condition, options);
#endif

#ifdef OLDGAA_COND_SEC_MECH
     if(!strcmp(condition->type, COND_SEC_MECH))
     oldgaa_status = oldgaa_evaluate_sech_mech_cond(sc->identity_cred->principal,
                                              condition, options);
#endif

   /* check if condition evaluation function for upcall was passed in the security context */
  if(sc->condition_evaluation)
    sc->condition_evaluation(sc, options, condition, &oldgaa_status);

  if(oldgaa_status != OLDGAA_MAYBE)
  condition->status |= COND_FLG_EVALUATED; /* evaluated */

  if(oldgaa_status == OLDGAA_YES)
  condition->status |= COND_FLG_MET;       /* met */

return oldgaa_status;

}

/*****************************************************************************/
static
char *
get_day()
{
  time_t     tt;
  struct tm *t;
  char      *str;

  str = malloc(STRING_LENGTH + 1 /* for NUL */);
  if (!str)
      out_of_memory();

  time(&tt);
  t = localtime(&tt);
  strftime(str,STRING_LENGTH,"%A",t);

  return str;
}


/*****************************************************************************/
static
char *
get_hr_24()
{
  time_t     tt;
  struct tm *t;
  char      *str;

  str = malloc(STRING_LENGTH + 1 /* for NUL */);
  if (!str)
      out_of_memory();

  time(&tt);
  t = localtime(&tt);
  strftime(str,STRING_LENGTH,"%H",t);

  return str;
}
/*****************************************************************************/
/* commented out by SLANG - not currently used */
/*  static */
/*  char * */
/*  get_hr_12() */
/*  { */
/*    time_t     tt; */
/*    struct tm *t; */
/*    char      *str; */

/*    str = malloc(STRING_LENGTH + 1); */
/*    if (!str) */
/*        out_of_memory(); */

/*    time(&tt);  */
/*    t = localtime(&tt);  */
/*    strftime(str,STRING_LENGTH,"%I",t); */

/*    return str;  */
/*  } */

/*****************************************************************************/
static
char *
get_minutes()
{
  time_t     tt;
  struct tm *t;
  char      *str;

  str = malloc(STRING_LENGTH + 1 /* for NUL */);
  if (!str)
      out_of_memory();


  time(&tt);
  t = localtime(&tt);
  strftime(str,STRING_LENGTH,"%M",t);

  return str;
}
/*****************************************************************************/
static
char *
get_seconds()
{
  time_t     tt;
  struct tm *t;
  char      *str;

  str = malloc(STRING_LENGTH + 1 /* for NUL */);
  if (!str)
      out_of_memory();

  time(&tt);
  t = localtime(&tt);
  strftime(str,STRING_LENGTH,"%S",t);

  return str;
}

/*****************************************************************************/
/* commented out by SLANG - not currently used */
/*  static */
/*  char * */
/*  get_am_pm() */
/*  { */
/*    time_t     tt; */
/*    struct tm *t; */
/*    char      *str; */

/*    str = malloc(STRING_LENGTH + 1); */
/*    if (!str) */
/*        out_of_memory(); */

/*    time(&tt);  */
/*    t = localtime(&tt);  */
/*    strftime(str,STRING_LENGTH,"%p",t); */

/*    return str;  */
/*  }  */
/*****************************************************************************/
static
int
day_to_val(char *str)
{

 if (oldgaa_regex_matches_string(str, "Su") ||
     oldgaa_regex_matches_string(str, "su")) return 1;

 if (oldgaa_regex_matches_string(str, "Mo") ||
     oldgaa_regex_matches_string(str, "mo")) return 2;

 if (oldgaa_regex_matches_string(str, "Tu") ||
     oldgaa_regex_matches_string(str, "tu")) return 3;

 if (oldgaa_regex_matches_string(str, "We") ||
     oldgaa_regex_matches_string(str, "we")) return 4;

 if (oldgaa_regex_matches_string(str, "Th") ||
     oldgaa_regex_matches_string(str, "th")) return 5;

 if (oldgaa_regex_matches_string(str, "Fr") ||
     oldgaa_regex_matches_string(str, "fr")) return 6;

 if (oldgaa_regex_matches_string(str, "Sa") ||
     oldgaa_regex_matches_string(str, "sa")) return 7;

 return 0;
}


/*****************************************************************************/
static
int
check_day(char *str1, char *str2, char *day)

{
 int val, val1, val2;

 val  = day_to_val(day);  /* current day          */
 val1 = day_to_val(str1); /* first day delimiter  */
 val2 = day_to_val(str2); /* second day delimiter */

 if (!val1) return -1;

 if((val == val1) ||
    (val == val2) ||
    ((val1 < val2) && (val > val1) && (val < val2)) ||
    ((val1 > val2) && val2 && ((val > val1) || (val < val2)))
   ) return 1;

 return 0;

}


/*****************************************************************************/

static
char*
get_value(int *jj, const char *cond, const char delimiter)
 {
  int  i,j  = *jj, length = strlen(cond);
  char *str = NULL;

  str = malloc(length + 1 /* for NUL */);
  if(!str) out_of_memory();

  for(i=0; j <= length; i++)
	{
          str[i] = cond[j];
          j++;
          if((cond[j] == delimiter)) { j++; /* omit delimiter */ break; }
	}

 str[i+1] = NUL; /* terminate the string */
 *jj = j;
 return str;
  }

/*****************************************************************************/



