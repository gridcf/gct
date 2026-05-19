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
 oldgaa_utils.c:

Description:
	This file is used internally by the oldgaa routines
**********************************************************************/


/**********************************************************************
                             Include header files
**********************************************************************/
#include "globus_oldgaa.h"
#include "globus_gsi_cert_utils.h"
#include "oldgaa_utils.h"
#include "oldgaa_gl_internal_err.h"
#include "rfc1779.h"

#include <stdlib.h>     /* For malloc or free */
#include <math.h>       /* for pow()          */

#include <stdio.h>      /* File reading and writing */
#include <string.h>
#include <errno.h>	/* For errno */

#include <assert.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

/**********************************************************************
                       Define module specific variables
**********************************************************************/

int	       string_count = 0;
int	       string_max   = 0;
static char   *parse_error  = NULL;

/**********************************************************************
                       Declare Static Helper Functions
***********************************************************************/

static
char *
oldgaa_to_regex(const char * const glob_regex);


/******************************************************************************

Function:   oldgaa_oldgaa_handle_error
Description:
	Given and error message and a pointer to a pointer to be
	allocated handle the allocation and setting of the pointer.

Parameters:
	errstring, pointer to a pointer to be set to the allocated
	error message. May be NULL.

	message, the error message.

Returns:
	Nothing

******************************************************************************/

void
oldgaa_handle_error(char       **errstring,
                 const char *const message)
{
    /* If this fails we're hosed so don't bother checking */
    if (errstring)
      {
      if (*errstring == NULL) *errstring = strdup(message);
      else
        {
        *errstring = (char *)realloc(*errstring, strlen(message)+1);
        if (*errstring != NULL) strcpy(*errstring, message);
        }
      }

} /* oldgaa_handle_error() */

/**********************************************************************

Function: oldgaa_strings_match()

Description:
	Compare two strings.

Parameters:
	string1 and string2, pointers to two strings.

Returns:
	1 if strings match.
	0 if strings don't match.
       -1 on error, setting errno.

**********************************************************************/

int
oldgaa_strings_match(
  const char * const			string1,
  const char * const 			string2)
{
  /* Check arguments */
  if (!string1 || !string2)
  {
    errno = ERRNO_INVALID_ARGUMENT;
    return -1;
  }

  return (strcmp(string1, string2) == 0);
} /* strings_match() */


/*****************************************************************************
 * oldgaa_strcopy - copy a string allocating space if necessary
 *
 *     OLDGAA_STRCOPY takes a conventional string, S, as an argument, and a pointer to
 *     a second  string, R, which is to be replaced by S.  If R is long enough
 *     to hold S, S is copied.  Otherwise, new space is allocated, and R is
 *     freed.  S is then copied to the newly allocated space.  If S is
 *     NULL, then R is freed and NULL is returned.
 *
 *     In any event, OLDGAA_STRCOPY returns a pointer to the new copy of S,
 *     or a NULL pointer.
 *****************************************************************************/

char *
oldgaa_strcopy(const char *s, char *r)
{
    int	slen;

     if(!s && r) {
        free(r);
        return(NULL);
    }
    else if (!s) return(NULL);

    if(r) free(r);

    slen = strlen(s) + 1;

    r = (char *) malloc(slen);
    if (!r) out_of_memory();

    strcpy(r,s);
    return(r);

}

/**********************************************************************
  Compare elements
 **********************************************************************/

int
oldgaa_compare_principals(oldgaa_principals_ptr element,
                   oldgaa_principals_ptr new)
{
  /* Do the principal's names match? */

 if(oldgaa_strings_match(element->type,      new->type)     &&
    oldgaa_strings_match(element->authority, new->authority) &&
    !globus_i_gsi_cert_utils_dn_cmp(element->value, new->value) )
 return TRUE;
 else return FALSE;
}


/****************************************************************************-*/


int
oldgaa_compare_rights(oldgaa_rights_ptr element, oldgaa_rights_ptr new)
{
  if(oldgaa_strings_match(element->type,      new->type)     &&
     oldgaa_strings_match(element->authority, new->authority) &&
     oldgaa_strings_match(element->value,     new->value) )
  return TRUE;
  else return FALSE;
}

/****************************************************************************-*/


int
oldgaa_compare_conditions(oldgaa_conditions_ptr element,
                   oldgaa_conditions_ptr new)
{
  if(oldgaa_strings_match(element->type,      new->type)     &&
     oldgaa_strings_match(element->authority, new->authority) &&
     oldgaa_strings_match(element->value,     new->value) )
  return TRUE;
  else return FALSE;
}

/****************************************************************************-*/

int
oldgaa_compare_sec_attrbs(oldgaa_sec_attrb_ptr element,
                       oldgaa_sec_attrb_ptr new)
{
  if(oldgaa_strings_match(element->type,      new->type)     &&
     oldgaa_strings_match(element->authority, new->authority) &&
     oldgaa_strings_match(element->value,     new->value) )
  return TRUE;
  else return FALSE;
}

/**********************************************************************
  Add new element to a list
 **********************************************************************/

oldgaa_principals_ptr
oldgaa_add_principal(oldgaa_policy_ptr   *list,
                  oldgaa_principals_ptr new)
{
  oldgaa_principals_ptr element;

  element = *list;

   if (oldgaa_compare_principals(element, new)) return element; /* found
      this principal in the list */

    while(element->next )
    {
     element = element->next;
     if (oldgaa_compare_principals(element, new)) return element; /* found
                                                   this principal in the list */
    }

    element->next = new; /* add new element to the end of the list */
    return new;
}


/*****************************************************************************/

oldgaa_rights_ptr
oldgaa_add_rights(oldgaa_rights_ptr *list,
           oldgaa_rights_ptr  new)
{
  oldgaa_rights_ptr element;

  element = *list;

  while(element->next!= NULL) element = element->next;
  element->next = new;

  return new;
}

/*****************************************************************************/
oldgaa_cond_bindings_ptr
oldgaa_add_cond_binding(oldgaa_cond_bindings_ptr *list,
                 oldgaa_cond_bindings_ptr  new)
{
  oldgaa_cond_bindings_ptr element;

  element = *list;

  while(element->next!= NULL) element = element->next;

  element->next = new;

  return new;
}


/*****************************************************************************/


oldgaa_conditions_ptr
oldgaa_add_condition(oldgaa_conditions_ptr *list,
                  oldgaa_conditions_ptr  new)
{
  oldgaa_conditions_ptr element;

  element = *list;

/*
 *DEE This code does not make sence. It will add new
 *to the end of the list, but will leave it hanging
 *if its the same as one already on the list.
 */

  if (oldgaa_compare_conditions(element, new))
	{
		return element; /* found this condition in the list */
	}
    while(element->next)
    {
      element = element->next;
      if (oldgaa_compare_conditions(element, new))
		{
			return element; /* found this condition in the list */
		}
    }
     element->next = new; /* add new element to the end of the list */

     return new;
}

/**********************************************************************
  Add new element to a list
 **********************************************************************/

oldgaa_sec_attrb_ptr
oldgaa_add_attribute(oldgaa_sec_attrb_ptr *list, oldgaa_sec_attrb_ptr new)
{
  oldgaa_sec_attrb_ptr element;

  element = *list;

   if (oldgaa_compare_sec_attrbs(element, new)) return element; /* found
      this attribute in the list */

    while(element->next)
    {
     element = element->next;
     if (oldgaa_compare_sec_attrbs(element, new)) return element; /* found
                                                   this attribute in the list */
    }

    element->next = new; /* add new element to the end of the list */
    return new;
}


/**********************************************************************
  Bindings
 **********************************************************************/

int
oldgaa_bind_rights_to_principals(oldgaa_principals_ptr start,
                          oldgaa_rights_ptr     rights)
{
 oldgaa_principals_ptr element = start;

 while(element != NULL)
  {
    element->rights = rights;
	rights->reference_count++;

#ifdef DEBUG
fprintf(stderr,"oldgaa_bind_rights_to_principals:Principal:%p->rights:%p\n",
		element, rights);
#endif
    element         = element->next;
  }

 return OLDGAA_SUCCESS;
}


/*****************************************************************************/

void
oldgaa_bind_rights_to_conditions(oldgaa_rights_ptr        start,
                              oldgaa_cond_bindings_ptr cond_bind)
{
 oldgaa_rights_ptr element = start;

/*DEE - Looks like all the rights will point to this cond_bind*/
/* With Globus we only have 1, so should not be a problem */
 while(element)
  {
    element->cond_bindings = cond_bind;
	cond_bind->reference_count++;
#ifdef DEBUG
fprintf(stderr,"oldgaa_bind_rights_to_conditions:rights:%p->cond_bind:%p\n",
		element, cond_bind);
#endif
    element                = element->next;
  }
}

/**********************************************************************
  Regex Handling Functions
 **********************************************************************/

/**********************************************************************

Function: oldgaa_check_reg_expr()

Description:
	Goes throug the list of reg expressions and looks for a match
        to the given rex expression.

Parameters:
	reg_expr pointer to a reg expression and reg_expr_list, pointers to
        a reg expression list

Returns:
	1 if regex match.
	0 if regex don't match.
	-1 on error, setting errno.

**********************************************************************/
int
oldgaa_check_reg_expr(char  *reg_expr,
                   char **reg_expr_list)
{
 char **pregex;

#ifdef DEBUG
fprintf(stderr, "\noldgaa_check_reg_expr:\n");
#endif /* DEBUG */

/* Check arguments */
  if (!reg_expr || !reg_expr_list)
  {
    errno = ERRNO_INVALID_ARGUMENT;
    return -1;
  }

/* walk through the regexes and see if we match any */
      for (pregex = reg_expr_list;
	  *pregex != NULL;
	   pregex++)
      {
#ifdef DEBUG
fprintf(stderr, "reg_exp  %s\n*pregex %s\n\n", reg_expr, *pregex);
#endif /* DEBUG */

       if (oldgaa_regex_matches_string(reg_expr, *pregex) == 1)
       return 1; /* We have a match */		
      }

  return 0;
}



/**********************************************************************

Function: oldgaa_regex_matches_string()

Description:
	Determine if a regex matches a given string.

Parameters:
	string, the string to check

	regex, the regex to compare

Returns:
	1 if match.
	0 if don't match.
       -1 on error, setting errno.

**********************************************************************/

int
oldgaa_regex_matches_string(
    const char * const                  string,
    const char * const                  regex)
{
  /* Our result (1 == match) */
  int					                result = 0;
  const char *                          star;

  /* Check arguments */
  if (!string || !regex)
  {
    errno = ERRNO_INVALID_ARGUMENT;
    return -1;
  }


  if(!globus_i_gsi_cert_utils_dn_cmp(string,regex))
  {
      result = 1;
  }
  else
  {
      char * tmp_str;
      tmp_str = malloc(strlen(string) + strlen(regex));
      if(tmp_str)
      {
          star = strrchr(regex,'*');
          if(star)
          {
              int index;
              int after_star;

              index = star - regex;
              /* Number of characters after * in regex */
              after_star = strlen(regex) - index - 1;

              strcpy(tmp_str, string);
              if(strlen(tmp_str) > (index + after_star))
              {
                  /*
                   * Build new string formed of string made of up
                   * string up to where the star is and then end of
                   * string with length of number of characters after star
                   * in regex.
                   */
                  tmp_str[index] = '*';
                  strcpy(tmp_str + index + 1,
                         string + strlen(string) - after_star);
                  if(!globus_i_gsi_cert_utils_dn_cmp(regex, tmp_str))
                  {
                      result = 1;
                  }
              }
          }
          free(tmp_str);
      }
  }

  return result;

} /* oldgaa_regex_matches_string() */



/**********************************************************************

Function: oldgaa_parse_regex()

Description:
	Walks throug condition list and evaluates each condition.

Parameters:
        condition, pointer to oldgaa_conditions structure

Returns:
       list of regex

**********************************************************************/

char **
oldgaa_parse_regex(char * str)

{
    char **subject_regexes = NULL;
    int    num_regexes     = 0;         /* Number of subject regexes parsed */
    char  *new_str;                     /* Pointer to the string*/
    int    i      = 0,                  /* Pointer to our location in str */
           j,                           /* Pointer to our location in new_str */
           length = strlen(str);			
    int    end = FALSE;			
    int    fail = 0;

#ifdef DEBUG
    fprintf(stderr, "\noldgaa_parse_regex:\n");
#endif /* DEBUG */


    /* Now read and parse all the subject regexes */
    subject_regexes = calloc(num_regexes + 1 /* for NULL */, sizeof(char *));

    if (!subject_regexes) out_of_memory();
    subject_regexes[0] = NULL;

    new_str = malloc(strlen(str)+1);
    if (!new_str) out_of_memory();

    if (QUOTING != str[i]) strcpy(new_str, str);

    while(1)
    {
        char  *uncnv_regex;  /* Pointer to unconverted regex */
        char  *cnv_regex;    /* Pointer to converted regex */
        char **tmp_regexes;  /* Temporary holder for list of regexes */

        if(!end)
        {
            while ((str[i] == WHITESPACE) ||
                   (str[i] == TAB) ||
                   (str[i] == QUOTING))
            {
                i++;
            }

            j=0;

            while(1)
            {
                if (str[i] == QUOTING)
                {
                    if (i == length-1) 
                    {
                        end = TRUE;
                    }
                    break;
                }

                if (i > length-1)
                {
                    end = TRUE;
                    break;
                }

                new_str[j]=str[i];
                i++; j++;
            }

            new_str[j]= NUL;

        } /* end of if(compound) */

        if (oldgaa_rfc1779_name_parse(new_str, &uncnv_regex, NULL) != 0)
        {
            oldgaa_handle_error(&parse_error,
                    "oldgaa_globus_parse_conditions: error parsing rfc1779 name");
            fail = 1;

            goto error;
        }

        cnv_regex = oldgaa_to_regex(uncnv_regex);

        free(uncnv_regex);
        uncnv_regex = NULL;

        if (cnv_regex == NULL)
        {
            oldgaa_handle_error(&parse_error,
                      "oldgaa_globus_parse_conditions: error parsing regular expression");
            fail = 1;

            goto error;
        }

        num_regexes++;
        tmp_regexes = realloc(subject_regexes,
                              (num_regexes + 1) * sizeof(char *));

        if (tmp_regexes == NULL)
        {
            oldgaa_handle_error(&parse_error,
                    "oldgaa_globus_parse_conditions: out of memory");

            free(cnv_regex);
            num_regexes--;

            fail = 1;

            goto error;
        }

        subject_regexes = tmp_regexes;

        subject_regexes[num_regexes - 1] = cnv_regex;
        subject_regexes[num_regexes] = NULL;

        if (end) 
        {
            break;
        }
    }

    if (num_regexes == 0)
    {
        /* No subject regexes were found */
        oldgaa_handle_error(&parse_error,
                    "oldgaa_globus_parse_conditions: no subject regexes found");
        fail = 1;
    }

error:
    if (fail)
    {
        for (i = 0; i < num_regexes; i++)
        {
            if (subject_regexes[i])
            {
                free(subject_regexes[i]);
            }
        }
        free(subject_regexes);

        subject_regexes = NULL;
    }

    free(new_str);
    return subject_regexes;
}


/**********************************************************************

Function:	oldgaa_to_regex()

Description:
	Convert a shell-style regex to a regex suitable
	to feed into the posix regex commands.

	Specifically:

	'*' is converted to '.*'

	'?' is converted to '.'

	'.', '^', '\, and '$' are escaped by preceding them
	with a backslash
			
	'^' is prepended to the string and '$' is appended so that the
	resulting regex will force a complete match.

Parameters:
	glob_regex, a pointer to the glob-style regex string.

Returns:
	a pointer to allocated regex string
	NULL on error (errno is set).
		
**********************************************************************/

static
char *
oldgaa_to_regex(const char * const glob_regex)
{
    /* don't do the conversion */
    /* we're no longer doing regex matching -Sam */
    return strdup(glob_regex);
} /* oldgaa_to_regex() */


/**********************************************************************/
