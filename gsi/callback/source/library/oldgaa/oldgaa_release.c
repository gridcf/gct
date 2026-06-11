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
    oldgaa_release.c:

Description:
        This file used internally by the oldgaa routines
**********************************************************************/
#include "globus_oldgaa.h"
#include "stdio.h" /* for fprintf() */

/**********************************************************************
Function:  oldgaa_release_buffer_contents

Description:
        Release the contents of a buffer

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_buffer_contents (uint32            *minor_status,
                                oldgaa_buffer_ptr  buffer)
{

#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_buffer_contents:\n");
#endif /* DEBUG */

     if (buffer == NULL || buffer == OLDGAA_NO_BUFFER) return OLDGAA_SUCCESS;

    if (buffer->value) free(buffer->value);
    buffer->length = 0;

    return OLDGAA_SUCCESS;

} /* oldgaa_release_buffer_contents */
/**********************************************************************
Function:  oldgaa_release_buffer

Description:
        Release the buffer

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_buffer(uint32            *minor_status,
                      oldgaa_buffer_ptr *buffer)
{

#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_buffer:\n");
#endif /* DEBUG */

    if (buffer == NULL || *buffer == NULL) return OLDGAA_SUCCESS;

    free(*buffer);
    *buffer = NULL;

    return OLDGAA_SUCCESS;

} /* oldgaa_release_buffer */

/**********************************************************************
Function:  oldgaa_release_options

Description:
        Release the contents of a buffer

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_options(uint32             *minor_status,
                       oldgaa_options_ptr  buffer)
{
#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_options:\n");
#endif /* DEBUG */

    if (buffer == NULL || buffer == OLDGAA_NO_OPTIONS) return OLDGAA_SUCCESS;

    if (buffer->value) free(buffer->value);
    buffer->length = 0;

    free(buffer);

    return OLDGAA_SUCCESS;

} /* oldgaa_release_options */

/**********************************************************************
Function:  oldgaa_release_data

Description:
        Release the contents of a buffer

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_data (uint32          *minor_status,
                     oldgaa_data_ptr  buffer)
{


#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_data:\n");
#endif /* DEBUG */

    if (buffer == NULL || buffer == OLDGAA_NO_DATA) return OLDGAA_SUCCESS;

    if (buffer->str)       free(buffer->str);
    if (buffer->error_str) free(buffer->error_str);

    free(buffer);

    return OLDGAA_SUCCESS;

} /* oldgaa_release_data */



/**********************************************************************

Function:   oldgaa_delete_sec_context()

Description:
        delete the security context

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_sec_context(uint32                 *minor_status,
                           oldgaa_sec_context_ptr *sec_context)
{

    oldgaa_sec_context_ptr  *context_handle = sec_context;
    uint32                   inv_minor_status = 0;


#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_sec_context:\n");
#endif /* DEBUG */

    if (*context_handle == NULL || *context_handle == OLDGAA_NO_SEC_CONTEXT)
        return OLDGAA_SUCCESS ;

    /* ignore errors to allow for incomplete context handles */

    if ((*context_handle)->identity_cred!= NULL)
        oldgaa_release_identity_cred(&inv_minor_status,
                              &((*context_handle)->identity_cred));


    if ((*context_handle)->authr_cred!= NULL)
        oldgaa_release_authr_cred(&inv_minor_status,
                              &((*context_handle)->authr_cred));


    if ((*context_handle)->group_membership!= NULL)
        oldgaa_release_identity_cred(&inv_minor_status,
                              &((*context_handle)->group_membership));


    if ((*context_handle)->group_non_membership!= NULL)
        oldgaa_release_identity_cred(&inv_minor_status,
                              &((*context_handle)->group_non_membership));


    if ((*context_handle)->attributes!= NULL)
        oldgaa_release_attributes(&inv_minor_status,
                              &((*context_handle)->attributes));


    if ((*context_handle)->unevl_cred!= NULL)
        oldgaa_release_uneval_cred(&inv_minor_status,
                              &((*context_handle)->unevl_cred));


    if ((*context_handle)->connection_state!= NULL)
    {
        oldgaa_release_buffer_contents(&inv_minor_status,
                              (*context_handle)->connection_state);

        oldgaa_release_buffer(&inv_minor_status,
                              &((*context_handle)->connection_state));
    }

    free(*context_handle);
    *context_handle = OLDGAA_NO_SEC_CONTEXT;

    return OLDGAA_SUCCESS;

} /* oldgaa_delete_sec_context */

/**********************************************************************
Function:   oldgaa_release_identity_cred()

Description:
        delete the security context

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_identity_cred (uint32                   *minor_status,
                              oldgaa_identity_cred_ptr *identity_cred)

{
    oldgaa_identity_cred_ptr  *cred = identity_cred;
    uint32                     inv_minor_status = 0;

#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_identity_cred:\n");
#endif /* DEBUG */


    if (*cred == NULL || *cred == OLDGAA_NO_IDENTITY_CRED)
        return OLDGAA_SUCCESS;

    /* ignore errors to allow for incomplete context handles */

    if ((*cred)->principal!= NULL)
        oldgaa_release_principals(&inv_minor_status, &((*cred)->principal));

    if ((*cred)->conditions!= NULL)
        oldgaa_release_conditions(&inv_minor_status, &((*cred)->conditions));

    if ((*cred)->mech_spec_cred!= NULL)
    {
        oldgaa_release_buffer_contents(&inv_minor_status,
                                              (*cred)->mech_spec_cred);
        oldgaa_release_buffer(&inv_minor_status, &((*cred)->mech_spec_cred));
    }

    if ((*cred)->next!= NULL)
        oldgaa_release_identity_cred(&inv_minor_status, &((*cred)->next));
    free(*cred);

    return OLDGAA_SUCCESS;

}

/**********************************************************************
Function:   oldgaa_release_authr_cred()

Description:
        delete the security context

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_authr_cred(uint32                *minor_status,
                          oldgaa_authr_cred_ptr *authr_cred)

{
    oldgaa_authr_cred_ptr   *cred = authr_cred;
    uint32                   inv_minor_status = 0;

#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_authr_cred:\n");
#endif /* DEBUG */

    if (*cred == NULL || *cred == OLDGAA_NO_AUTHORIZATION_CRED)
        return OLDGAA_SUCCESS;

    /* ignore errors to allow for incomplete context handles */

    if ((*cred)->grantor != NULL)
        oldgaa_release_principals(&inv_minor_status, &((*cred)->grantor));


    if ((*cred)->grantee != NULL)
        oldgaa_release_principals(&inv_minor_status, &((*cred)->grantee));


    /*
     *DEE what about release of objects?
     * Since There are is no oldgaa_allocate_authr_cred
     * that can be fixed when this routine is actually used.
     */

    if ((*cred)->access_rights!= NULL)
        oldgaa_release_rights(&inv_minor_status, &((*cred)->access_rights));

    if ((*cred)->mech_spec_cred!= NULL)
    {
        oldgaa_release_buffer_contents(&inv_minor_status,
                                             (*cred)->mech_spec_cred);
        oldgaa_release_buffer(&inv_minor_status, &((*cred)->mech_spec_cred));
    }

    if ((*cred)->next!= NULL)
        oldgaa_release_authr_cred(&inv_minor_status, &((*cred)->next));

    free(*cred);

    return OLDGAA_SUCCESS;

} /* oldgaa_release_authr_cred */

/**********************************************************************
Function:   oldgaa_release_attributes()

Description:
        delete the security context

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_attributes(uint32                *minor_status,
                          oldgaa_attributes_ptr *attributes)
{

    oldgaa_attributes_ptr  *cred = attributes;
    uint32                  inv_minor_status = 0;


#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_attributes:\n");
#endif /* DEBUG */

    if (*cred == NULL || *cred == OLDGAA_NO_ATTRIBUTES)
        return OLDGAA_SUCCESS;

    /* ignore errors to allow for incomplete context handles */

    if ((*cred)->mech_type != NULL) free((*cred)->mech_type);
    if ((*cred)->type      != NULL) free((*cred)->type);
    if ((*cred)->value     != NULL) free((*cred)->value);


    if ((*cred)->conditions!= NULL)
        oldgaa_release_cond_bindings(&inv_minor_status,
                              &((*cred)->conditions));

    if ((*cred)->mech_spec_cred!= NULL)
    {
        oldgaa_release_buffer_contents(&inv_minor_status,
                                   (*cred)->mech_spec_cred);
        oldgaa_release_buffer(&inv_minor_status, &((*cred)->mech_spec_cred));
    }

    if ((*cred)->next!= NULL)
        oldgaa_release_attributes(&inv_minor_status, &((*cred)->next));

    free(*cred);

    return OLDGAA_SUCCESS;

} /* oldgaa_rlease_attributes */

/**********************************************************************
Function:   oldgaa_rlease_uneval_cred()

Description:
        delete the security context

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_uneval_cred(uint32                 *minor_status,
                           oldgaa_uneval_cred_ptr *uneval_cred)

{
    oldgaa_uneval_cred_ptr  *cred = uneval_cred;
    uint32                   inv_minor_status = 0;

#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_uneval_cred:\n");
#endif /* DEBUG */


    if (*cred == NULL || *cred == OLDGAA_NO_UNEVAL_CRED)
        return OLDGAA_SUCCESS;

    /* ignore errors to allow for incomplete context handles */


    if ((*cred)->grantor != NULL)
        oldgaa_release_principals(&inv_minor_status, &((*cred)->grantor));


    if ((*cred)->grantee != NULL)
        oldgaa_release_principals(&inv_minor_status, &((*cred)->grantee));


    if ((*cred)->mech_spec_cred!= NULL)
    {
        oldgaa_release_buffer_contents(&inv_minor_status,
                                             (*cred)->mech_spec_cred);
        oldgaa_release_buffer(&inv_minor_status, &((*cred)->mech_spec_cred));
    }

    if ((*cred)->next!= NULL)
        oldgaa_release_uneval_cred(&inv_minor_status, &((*cred)->next));

    free(*cred);

    return OLDGAA_SUCCESS;

} /* oldgaa_rlease_unevaluated_cred */

/**********************************************************************
Function:   oldgaa_rlease_principals()

Description:
        delete the security context

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_principals(uint32                *minor_status,
                          oldgaa_principals_ptr *principals)
{

    oldgaa_principals_ptr  *cred = principals;
    uint32                  inv_minor_status = 0;

#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_principals:\n");
#endif /* DEBUG */

    if (*cred == NULL || *cred == OLDGAA_NO_PRINCIPALS)
        return OLDGAA_SUCCESS;

    /* ignore errors to allow for incomplete context handles */


    if ((*cred)->rights != NULL)
        oldgaa_release_rights(&inv_minor_status, &((*cred)->rights));


    if ((*cred)->next != NULL)
        oldgaa_release_principals(&inv_minor_status, &((*cred)->next));

    if ((*cred)->type      != NULL) free((*cred)->type);
    if ((*cred)->authority != NULL) free((*cred)->authority);
    if ((*cred)->value     != NULL) free((*cred)->value);

    free(*cred);

    return OLDGAA_SUCCESS;

} /* oldgaa_rlease_principals */

/**********************************************************************
Function:   oldgaa_rlease_rights()

Description:
        delete the security context

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_rights(uint32            *minor_status,
                      oldgaa_rights_ptr *rights)
{

    oldgaa_rights_ptr  *cred = rights;
    uint32              inv_minor_status = 0;

#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_rights:\n");
#endif /* DEBUG */


    if (*cred == NULL || *cred == OLDGAA_NO_RIGHTS)
        return OLDGAA_SUCCESS;

#ifdef DEBUG
    fprintf(stderr, "rights:%p:ref:%d\n",*cred,(*cred)->reference_count);
#endif /* DEBUG */

    (*cred)->reference_count--;
    if ((*cred)->reference_count > 0) {
        *rights = NULL;
        return OLDGAA_SUCCESS;
    }

    /* ignore errors to allow for incomplete context handles */


    if ((*cred)->cond_bindings != NULL)
        oldgaa_release_cond_bindings(&inv_minor_status,
                              &((*cred)->cond_bindings));


    if ((*cred)->next != NULL)
        oldgaa_release_rights(&inv_minor_status, &((*cred)->next));

    if ((*cred)->type      != NULL) free((*cred)->type);
    if ((*cred)->authority != NULL) free((*cred)->authority);
    if ((*cred)->value     != NULL) free((*cred)->value);


    free(*cred);
    *rights = NULL;

    return OLDGAA_SUCCESS;

} /* oldgaa_rlease_rights */

/**********************************************************************
Function:   oldgaa_rlease_cond_bindings()

Description:
        delete the security context

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_cond_bindings(uint32                    *minor_status,
                             oldgaa_cond_bindings_ptr  *cond_bind)
{
    oldgaa_cond_bindings_ptr  *cred = cond_bind;
    uint32                     inv_minor_status = 0;

#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_cond_bindings:\n");
#endif /* DEBUG */


    if (*cred == NULL || *cred == OLDGAA_NO_COND_BINDINGS)
        return OLDGAA_SUCCESS;

#ifdef DEBUG
    fprintf(stderr, "cond:%p:ref:%d\n",*cred,(*cred)->reference_count);
#endif /* DEBUG */

    (*cred)->reference_count--;
    if ((*cred)->reference_count > 0) {
        *cond_bind = NULL;
        return OLDGAA_SUCCESS;
    }

    /* ignore errors to allow for incomplete context handles */

    if ((*cred)->condition != NULL)
        oldgaa_release_conditions(&inv_minor_status, &((*cred)->condition));

    if ((*cred)->next != NULL)
        oldgaa_release_cond_bindings(&inv_minor_status, &((*cred)->next));

    free(*cred);
    *cond_bind = NULL;

    return OLDGAA_SUCCESS;

}

/**********************************************************************
Function:   oldgaa_rlease_conditions()

Description:
        delete the security context

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_conditions(uint32                *minor_status,
                          oldgaa_conditions_ptr *cond)
{
    oldgaa_conditions_ptr  *cred = cond;
    uint32                  inv_minor_status = 0;

#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_conditions:\n");
#endif /* DEBUG */

    if (*cred == NULL || *cred == OLDGAA_NO_CONDITIONS)
        return OLDGAA_SUCCESS;

#ifdef DEBUG
    fprintf(stderr, "conditions:%p:ref:%d\n",*cred,(*cred)->reference_count);
#endif /* DEBUG */

    (*cred)->reference_count--;
    if ((*cred)->reference_count > 0) {
        *cond = NULL;
        return OLDGAA_SUCCESS;
    }
    /* ignore errors to allow for incomplete context handles */

    if ((*cred)->next != NULL)
        oldgaa_release_conditions(&inv_minor_status, &((*cred)->next));

    if ((*cred)->type      != NULL) free((*cred)->type);
    if ((*cred)->authority != NULL) free((*cred)->authority);
    if ((*cred)->value     != NULL) free((*cred)->value);

    free(*cred);

    return OLDGAA_SUCCESS;

}
/**********************************************************************
Function:   oldgaa_rlease_answer()

Description:
        delete the security context

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_answer(uint32            *minor_status,
                      oldgaa_answer_ptr *answer)
{
    oldgaa_answer_ptr  *                cred = answer;
    uint32                              inv_minor_status = 0;


#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_answer:\n");
#endif /* DEBUG */

    if (*cred == NULL || *cred == OLDGAA_NO_ANSWER)
    {
        return OLDGAA_SUCCESS;
    }

    if ((*cred)->rights!= NULL)
    {
        /* ignore errors to allow for incomplete context handles */
        oldgaa_release_rights(&inv_minor_status, &((*cred)->rights));
    }

    if ((*cred)->valid_time != NULL)
    {
        free((*cred)->valid_time);
    }

    free(*cred);

    return OLDGAA_SUCCESS;
} /* oldgaa_rlease_answer */


/**********************************************************************
Function:   oldgaa_rlease_principals()

Description:
        delete the security context

Parameters:

Returns:
**********************************************************************/

oldgaa_error_code
oldgaa_release_sec_attrb(uint32               *minor_status,
                         oldgaa_sec_attrb_ptr *attributes)
{
    oldgaa_sec_attrb_ptr  *             cred = attributes;
    uint32                              inv_minor_status = 0;

#ifdef DEBUG
    fprintf(stderr, "\noldgaa_release_sec_attrb:\n");
#endif /* DEBUG */

    if (*cred == NULL || *cred == OLDGAA_NO_SEC_ATTRB)
    {
        return OLDGAA_SUCCESS;
    }

    if ((*cred)->next != NULL)
    {
        /* ignore errors to allow for incomplete context handles */
        oldgaa_release_sec_attrb(&inv_minor_status, &((*cred)->next));
    }

    if ((*cred)->type      != NULL) free((*cred)->type);
    if ((*cred)->authority != NULL) free((*cred)->authority);
    if ((*cred)->value     != NULL) free((*cred)->value);

    free(*cred);

    return OLDGAA_SUCCESS;
}

/**********************************************************************/
