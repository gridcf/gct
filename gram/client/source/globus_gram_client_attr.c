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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_gram_client_attr.c
 * @brief Attribute Functions
 */
#endif

#include "globus_i_gram_client.h"

/**
 * @brief Initialize a GRAM client attribute
 * @ingroup globus_gram_client_attr
 *
 * @details
 * The globus_gram_client_attr_init() function creates a new opaque
 * structure that can be used to specify custom attributes for performing
 * GRAM client operations.
 * 
 * @param attr
 *     An output parameter which will be set to the newly initialized 
 *     attribute.
 *
 * @return
 *     Upon success, globus_gram_client_attr_init() modifies the @a attr
 *     parameter to point to a new GRAM client attribute and returns
 *     @a GLOBUS_SUCCESS. If an error occurs, globus_gram_client_attr_init()
 *     returns an integer error code and value of @a attr is undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR
 *     Invalid attribute
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 *
 * @see globus_gram_client_attr_destroy()
 */
int
globus_gram_client_attr_init(
    globus_gram_client_attr_t *     attr)
{
    globus_i_gram_client_attr_t *   iattr;

    if (attr == NULL)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR;
    }
    iattr = globus_libc_calloc(1, sizeof(globus_i_gram_client_attr_t));

    if(iattr == NULL)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }
    iattr->delegation_mode = GLOBUS_IO_SECURE_DELEGATION_MODE_LIMITED_PROXY;

    *attr = (void*) iattr;

    return GLOBUS_SUCCESS;
}

/**
 * @brief Destroy a GRAM client attribute
 * @ingroup globus_gram_client_attr
 *
 * @details
 * The globus_gram_client_attr_destroy() function destroys and frees
 * a GRAM client attribute. After this function returns, the value pointed
 * to by @a attr is no longer valid and must not be used.
 *
 * @param attr
 *     A pointer to the attribute to destroy. All data associated with
 *     the attribute will be freed and it will be an invalid attribute.
 *
 * @return
 *     Upon success, globus_gram_client_attr_destroy() destroys the 
 *     attribute pointed to by the @a attr parameter and sets it to an invalid
 *     state.  If an error occurs, globus_gram_client_attr_destroy()
 *     returns an integer error code and value of @a attr is unchanged.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR
 *     Invalid attribute
 *
 * @see globus_gram_client_attr_init()
 */
int
globus_gram_client_attr_destroy(
    globus_gram_client_attr_t *     attr)
{
    int                             rc = GLOBUS_SUCCESS;
    globus_i_gram_client_attr_t *   iattr;

    if (attr == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR;
        goto out;
    }

    iattr = (globus_i_gram_client_attr_t *) *attr;
    if (iattr == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR;
        goto out;
    }

    globus_libc_free(iattr);
    *attr = NULL;

out:
    return rc;
}
/* globus_gram_client_attr_destroy() */

/**
 * @brief Set a GRAM client attribute's security credential
 * @ingroup globus_gram_client_attr
 * 
 * @details
 * The globus_gram_client_attr_set_credential() function sets the 
 * value of the @b credential in an attribute to the GSSAPI credential
 * named by the @a credential parameter. This is done as a shallow copy, so 
 * the value of @a credential must not be freed until the attribute will
 * no longer be used.
 *
 * @param attr
 *     The attribute set to modify to use the credential named by the
 *     @a credential parameter.
 * @param credential
 *     The GSSAPI credential to use with the attribute named by the @a attr
 *     parameter. This may be @a GSS_C_NO_CREDENTIAL to set the attribute
 *     to use the default security credential.
 *
 * @return
 *     Upon success, globus_gram_client_attr_set_credential() modifies the
 *     the attribute pointed to by the @a attr parameter to use the credential
 *     specified by the @a credential parameter and returns @a GLOBUS_SUCCESS.
 *     If an error occurs, globus_gram_client_attr_set_credential()
 *     returns an integer error code and the attribute named by @a attr is
 *     unchanged.
 * 
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR
 *     An invalid attribute set was passed to this function.
 *
 * @see globus_gram_client_attr_get_credential()
 */
int
globus_gram_client_attr_set_credential(
    globus_gram_client_attr_t           attr,
    gss_cred_id_t                       credential)
{
    int                                 rc = GLOBUS_SUCCESS;
    globus_i_gram_client_attr_t *       iattr;

    if (attr == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR;
        goto out;
    }
    iattr = (globus_i_gram_client_attr_t *) attr;
    iattr->credential = credential;
out:
    return rc;
}
/* globus_gram_client_attr_set_credential() */

/**
 * @brief Get a GRAM client attribute's security credential
 * @ingroup globus_gram_client_attr
 *
 * @details
 * The globus_gram_client_attr_get_credential() function gets the 
 * value of the @b credential in an attribute and modifies the @a credential
 * parameter to point to it. This is a shallow copy.
 *
 * @param attr
 *     The attribute set to query for its @a credential.
 * @param credential
 *     An output parameter that will be initialized to point to the GSSAPI
 *     credential which the @a attr is currently using.
 *
 * @return
 *     Upon success, globus_gram_client_attr_get_credential() modifies the
 *     the value pointed to by the @a credential parameter to be the same
 *     credential as that being used by the attribute named by the @a attr
 *     parameter and returns @a GLOBUS_SUCCESS.
 *     If an error occurs, globus_gram_client_attr_get_credential()
 *     returns an integer error code and the value pointed to by the
 *     @a credential parameter is undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR
 *     Invalid attribute
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *     Null parameter
 *
 * @see globus_gram_client_attr_set_credential()
 */
int
globus_gram_client_attr_get_credential(
    globus_gram_client_attr_t           attr,
    gss_cred_id_t *                     credential)
{
    int                                 rc = GLOBUS_SUCCESS;
    globus_i_gram_client_attr_t *       iattr;

    iattr = (globus_i_gram_client_attr_t *) attr;

    if (iattr == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR;

        goto out;
    }
    if (credential == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto out;
    }
    *credential = iattr->credential;

out:
    return rc;
}

/**
 * @brief Set a GRAM client attribute's delegation mode
 * @ingroup globus_gram_client_attr
 *
 * @details
 * The globus_gram_client_attr_set_delegation_mode() function sets the 
 * value of the @b delegation_mode in an attribute to the delegation mode
 * in the @a mode parameter.
 *
 * The GRAM client supports the following delegation modes:
 * - @b GLOBUS_IO_SECURE_DELEGATION_MODE_LIMITED_PROXY
 * - @b GLOBUS_IO_SECURE_DELEGATION_MODE_FULL_PROXY
 *
 * @param attr
 *     The attribute set to modify to use the delegation mode in the
 *     @a mode parameter.
 * @param mode
 *     The new value of the delegation mode.
 *
 * @return
 *     Upon success, globus_gram_client_attr_set_delegation_mode() modifies
 *     the the attribute named by the @a attr parameter to use the delegation
 *     mode in the @a mode parameter and returns GLOBUS_SUCCESS.
 *     If an error occurs, globus_gram_client_attr_set_delegation_mode()
 *     returns an integer error code and the @a delegation_mode attribute 
 *     value is unchanged.
 * 
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR
 *     Invalid attribute
 *
 * @see globus_gram_client_attr_get_delegation_mode()
 */
int
globus_gram_client_attr_set_delegation_mode(
    globus_gram_client_attr_t           attr,
    globus_io_secure_delegation_mode_t  mode)
{
    int                                 rc = GLOBUS_SUCCESS;
    globus_i_gram_client_attr_t *       iattr;

    if (attr == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR;
        goto out;
    }
    if (mode != GLOBUS_IO_SECURE_DELEGATION_MODE_LIMITED_PROXY &&
        mode != GLOBUS_IO_SECURE_DELEGATION_MODE_FULL_PROXY)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR;
        goto out;
    }
    iattr = (globus_i_gram_client_attr_t *) attr;
    iattr->delegation_mode = mode;
out:
    return rc;
}

/**
 * @brief Get a GRAM client attribute's security credential
 * @ingroup globus_gram_client_attr
 * 
 * @details
 * The globus_gram_client_attr_get_delegation_mode() function gets the 
 * value of the @b delegation_mode in an attribute and modifies the
 * @a mode parameter to point to its value.
 *
 * @param attr
 *     The attribute set to query for its @a delegation_mode.
 * @param mode
 *     An output parameter that will be set to point to the delegation mode
 *     which the @a attr is currently using.
 *
 * @return
 *     Upon success, globus_gram_client_attr_get_delegation_mode() modifies
 *     the the value pointed to by the @a mode parameter as described above
 *     and returns @a GLOBUS_SUCCESS.
 *     If an error occurs, globus_gram_client_attr_get_delegation_mode()
 *     returns an integer error code and the value pointed to by the
 *     @a mode parameter is undefined.
 *
 *
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR
 *     Invalid attribute
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *     Null parameter
 *
 * @see globus_gram_client_attr_get_delegation_mode()
 */
int
globus_gram_client_attr_get_delegation_mode(
    globus_gram_client_attr_t           attr,
    globus_io_secure_delegation_mode_t *mode)
{
    int                                 rc = GLOBUS_SUCCESS;
    globus_i_gram_client_attr_t *       iattr;

    iattr = (globus_i_gram_client_attr_t *) attr;

    if (iattr == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR;

        goto out;
    }
    if (mode == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto out;
    }
    *mode = iattr->delegation_mode;

out:
    return rc;
}
/* globus_gram_client_attr_get_delegation_mode() */
