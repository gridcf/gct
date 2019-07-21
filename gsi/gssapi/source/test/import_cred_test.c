#include "globus_common.h"
#include "gssapi_test_utils.h"
#include <gssapi.h>

int main(int argc, char *argv[])
{
    gss_buffer_desc                     input_buffer = { .value = NULL };
    OM_uint32                           option_req = 0;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GSS_S_COMPLETE;
    int                                 rc = 0;
    gss_cred_id_t                       cred = GSS_C_NO_CREDENTIAL;

    while ((rc = getopt(argc, argv, "o:i:")) != -1)
    {
        switch (rc)
        {
        case 'o':
            option_req = atoi(optarg);
            break;
        case 'i':
            input_buffer.value = optarg;
            input_buffer.length = strlen(optarg);
            break;
        }
    }

    major_status = gss_import_cred(
        &minor_status,
        &cred,
        NULL,
        option_req,
        &input_buffer,
        0,
        NULL);

    if (major_status != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, major_status, minor_status);
    }
    return major_status != GSS_S_COMPLETE;
}
