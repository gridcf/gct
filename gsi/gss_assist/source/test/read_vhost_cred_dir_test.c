#include "globus_gss_assist.h"

int main(int argc, char *argv[])
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_cred_id_t                      *credentials_array = NULL;
    size_t                              credentials_array_size = 0;
    size_t                              credentials_count = 0;

    globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    major_status = globus_gss_assist_read_vhost_cred_dir(
            &minor_status,
            NULL,
            &credentials_array,
            &credentials_array_size);

    if (major_status != GSS_S_COMPLETE)
    {
        goto done;
    }
    credentials_count = credentials_array_size / sizeof(gss_cred_id_t);
    printf("count=%zd\n", credentials_count);

    for (size_t i = 0; i < credentials_count; i++)
    {
        gss_buffer_desc                 credential = { 0 };

        major_status = gss_export_cred(
                &minor_status,
                credentials_array[i],
                GSS_C_NO_OID,
                0,
                &credential);
        if (major_status != GSS_S_COMPLETE)
        {
            break;
        }
        printf("Entry %zd:\n%.*s\n", i,
            (int) credential.length, (char*) credential.value);
        gss_release_buffer(
                &minor_status,
                &credential);
    }

    for (size_t i = 0; i < credentials_count; i++)
    {
        gss_release_cred(
                &minor_status,
                &credentials_array[i]);
    }
    free(credentials_array);
    credentials_array = NULL;

done:
    if (major_status != GSS_S_COMPLETE)
    {
        gss_buffer_desc status_string = {0};

        gss_display_status(
                &minor_status,
                major_status,
                GSS_C_GSS_CODE,
                GSS_C_NO_OID,
                NULL,
                &status_string);

        fprintf(stderr, "Status: %.*s\n",
                (int) status_string.length,
                (char *) status_string.value);
        gss_release_buffer(&minor_status, &status_string);
    }
    globus_module_deactivate(GLOBUS_GSI_GSS_ASSIST_MODULE);

    return major_status != GSS_S_COMPLETE;
}
