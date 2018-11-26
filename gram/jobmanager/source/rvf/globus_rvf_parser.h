/*
 * Copyright 1999-2014 University of Chicago
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

#ifndef GLOBUS_RVF_PARSER_H
#define GLOBUS_RVF_PARSER_H 1

#include "globus_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @defgroup globus_gram_job_manager_rsl_validation RSL Validation
 * @brief RSL Validation
 * RSL Validation
 *
 * Validates that a request's RSL contains only valid parameters, and that
 * all required parameters are defined.
 *
 * RSL Validation operates on an RSL, and one or more validation files.
 * The format of the validation files is defined in the
 * @ref globus_gram_job_manager_rsl_validation_file
 * section of the manual.
 */


/**
 * RSL Validation Record
 * @ingroup globus_gram_job_manager_rsl_validation
 *
 * Contains Information parsed from the validation file about a single
 * RSL parameter.
 */
typedef struct
{
    /** The name of the RSL attribute this record refers to. */
    char *                              attribute;
    /**
     * A textual description of the RSL parameter. This is not
     * used other than for debugging the parser.
     */
    char *                              description;
    /**
     * Default value of the parameter to be inserted in the RSL
     * if the parameter is not present.
     */
    char *                              default_value;
    /**
     * String containing an enumeration of legal values for the
     * RSL parameter. For example, for the grammyjob parameter, this
     * would be "collective independent".
     */
    char *                              enumerated_values;

    /**
     * Bitwise or of values of the
     * globus_i_gram_job_manager_validation_when_t values, indicated
     * when, if ever, this RSL parameter is required.
     */
    int                                 required_when;

    /**
     * Bitwise or of values of the
     * globus_i_gram_job_manager_validation_when_t values, indicated
     * when, if ever, this RSL parameter's default value should be
     * inserted into the RSL.
     */
    int                                 default_when;
    /**
     * Bitwise or of values of the
     * globus_i_gram_job_manager_validation_when_t values, indicated
     * when, if ever, this RSL parameter is valid.
     */
    int                                 valid_when;

    /**
     * Boolean indicating whether this attribute should be published
     * in the MDS. 
     */
    globus_bool_t                       publishable;
}
globus_rvf_record_t;

extern int
globus_rvf_parse_file(
    char *                              rvf_path,
    globus_list_t **                    out_attr_list,
    char **                             error_string);

extern int
globus_rvf_parse_string(
    char *                              rvf_string,
    globus_list_t **                    out_attr_list,
    char **                             error_string);


#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */


#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_RVF_PARSER_H */
