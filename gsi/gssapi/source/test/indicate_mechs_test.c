/*
 * Copyright 1999-2008 University of Chicago
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

#include "gssapi.h"
#include "globus_gsi_system_config.h"
#include "globus_gsi_credential.h"
#include "gssapi_test_utils.h"
#include "openssl/x509.h"
#include <strings.h>

int
indicate_mechs_bad_params_test(void)
{
    gss_OID_set                         oids;
    OM_uint32                           major_status = 0;
    OM_uint32                           minor_status = 0;

    major_status = gss_indicate_mechs(NULL, &oids);
    if(!GSS_ERROR(major_status))
    {
        fprintf(stderr, "gss_indicate_mechs with NULL minor_status didn't fail\n");
        return 1;
    }


    major_status = gss_indicate_mechs(&minor_status, NULL);
    if(!GSS_ERROR(major_status))
    {
        fprintf(stderr, "gss_indicate_mechs with NULL oid_set didn't fail\n");

        return 2;
    }

    return 0;
}
/* indicate_mechs_bad_params_test() */

int
indicate_mechs_test(void)
{
    gss_OID_set                         oids;
    OM_uint32                           major_status = 0;
    OM_uint32                           minor_status = 0;
    int                                 i;

    if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    {
        return 77;
    }
    major_status = gss_indicate_mechs(&minor_status, &oids);
    if(GSS_ERROR(major_status))
    {
        globus_gsi_gssapi_test_print_error(stderr, major_status, minor_status);
        return 1;
    }

    for (i = 0; i < oids->count; i++)
    {
    static gss_OID_desc                 gssapi_mech_gsi =
            {9, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01"};
        if (oids->elements[i].length == gssapi_mech_gsi.length &&
            memcmp(oids->elements[i].elements,
                    gssapi_mech_gsi.elements,
                    gssapi_mech_gsi.length) == 0)
        {
            break;
        }
    }

    if (i == oids->count)
    {
        fprintf(stderr, "Didn't find GSI mech in OID set\n");
        return 2;
    }

    major_status = gss_release_oid_set(&minor_status, &oids);

    if (GSS_ERROR(major_status))
    {
        globus_gsi_gssapi_test_print_error(stderr, major_status, minor_status);
        return 3;
    }

    return 0;
}
/* indicate_mechs_test() */

int
indicate_mechs_v2_test(void)
{
    gss_OID_set                         oids;
    OM_uint32                           major_status = 0;
    OM_uint32                           minor_status = 0;
    int                                 i;
    static gss_OID_desc                 gssapi_mech_gsi =
        {10, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x01"};

    if (OPENSSL_VERSION_NUMBER < 0x10001000L)
    {
        return 77;
    }
    major_status = gss_indicate_mechs(&minor_status, &oids);
    if(GSS_ERROR(major_status))
    {
        globus_gsi_gssapi_test_print_error(stderr, major_status, minor_status);
        return 1;
    }

    for (i = 0; i < oids->count; i++)
    {
        if (oids->elements[i].length == gssapi_mech_gsi.length &&
            memcmp(oids->elements[i].elements,
                    gssapi_mech_gsi.elements,
                    gssapi_mech_gsi.length) == 0)
        {
            break;
        }
    }

    if (i == oids->count)
    {
        fprintf(stderr, "Didn't find GSI mech in OID set\n");
        return 2;
    }

    major_status = gss_release_oid_set(&minor_status, &oids);

    if (GSS_ERROR(major_status))
    {
        globus_gsi_gssapi_test_print_error(stderr, major_status, minor_status);
        return 3;
    }

    return 0;
}
/* indicate_mechs_v2_test() */

int main()
{
    int                                 i, rc = 0, failed = 0;
    globus_module_descriptor_t         *modules[] =
    {
        GLOBUS_COMMON_MODULE,
        GLOBUS_GSI_GSSAPI_MODULE,
        NULL
    }, *failed_module = NULL;

    test_case_t                         tests[] =
    {
        TEST_CASE(indicate_mechs_bad_params_test),
        TEST_CASE(indicate_mechs_test),
        TEST_CASE(indicate_mechs_v2_test)
    };

    rc = globus_module_activate_array(modules, &failed_module);
    if (rc != 0)
    {
        exit(1);
    }

    printf("1..%d\n", (int) SIZEOF_ARRAY(tests));

    for (i = 0; i < SIZEOF_ARRAY(tests); i++)
    {
        const char *format = NULL;

        rc = (*(tests[i].func))();

        if (rc == 0)
        {
            format = "ok %d - %s\n";
        }
        else if (rc == 77)
        {
            format = "ok %d - %s # SKIP unsupported mech\n";
        }
        else
        {
            failed++;
            format = "not ok %d - %s\n";
        }

        printf(format, i + 1, tests[i].name);
    }

    return 0;
}
