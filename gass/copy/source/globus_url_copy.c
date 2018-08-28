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

/******************************************************************************
                             Include header files
******************************************************************************/
#include "globus_common.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

#include "globus_url_copy.h"
#include "globus_gass_copy.h"
#include "globus_ftp_client_debug_plugin.h"
#include "globus_ftp_client_restart_plugin.h"
#include "globus_error_gssapi.h"
#include "globus_gsi_system_config.h"

/*
 *  use globus_io for netlogger stuff
 */
#include "globus_io.h"
#include "version.h"  /* provides local_version */
#if defined(_WIN32)
#include "openssl/applink.c"
#endif


#include "globus_preload.h"

#include <openssl/pem.h>
#include <openssl/conf.h>
#include <openssl/x509v3.h>

/* adapted from openssl 'mkcert.c' example */
int guc_mkcert(X509 **x509p, EVP_PKEY **pkeyp, int bits, int serial, int days)
{
    X509 *x;
    EVP_PKEY *pk;
    RSA *rsa;
    X509_NAME *name = NULL;

    if((pkeyp == NULL) || (*pkeyp == NULL))
    {
        if((pk=EVP_PKEY_new()) == NULL)
        {
            abort();
            return(0);
        }
    }
    else
    pk = *pkeyp;

    if((x509p == NULL) || (*x509p == NULL))
    {
        if((x=X509_new()) == NULL)
        goto err;
    }
    else
    x = *x509p;

    rsa = RSA_generate_key(bits,RSA_F4,NULL,NULL);
    if(!EVP_PKEY_assign_RSA(pk,rsa))
    {
        abort();
        goto err;
    }
    rsa = NULL;

    X509_set_version(x,2);
    ASN1_INTEGER_set(X509_get_serialNumber(x),serial);
    X509_gmtime_adj(X509_get_notBefore(x),0);
    X509_gmtime_adj(X509_get_notAfter(x),(long)60*60*24*days);
    X509_set_pubkey(x,pk);

    name = X509_get_subject_name(x);

    /* This function creates and adds the entry, working out the
    * correct string type and performing checks on its length.
    * Normally we'd check the return value for errors...
    */
    X509_NAME_add_entry_by_txt(name,"C",
    MBSTRING_ASC, (const unsigned char *) "US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name,"CN",
    MBSTRING_ASC, (const unsigned char *) "Globus DCSC Cert", -1, -1, 0);

    /* Its self signed so set the issuer name to be the same as the
    * subject.
    */
    X509_set_issuer_name(x,name);

    if (!X509_sign(x,pk,EVP_md5()))
        goto err;

    *x509p=x;
    *pkeyp=pk;
    return(1);
err:
    return(0);
}


int guc_gencert(gss_buffer_desc * buf)
{
    X509 *x509 = NULL;
    EVP_PKEY *pkey = NULL;
    BIO *bio;
    char *ptr;
    
    guc_mkcert(&x509, &pkey, 2048, 0, 100);

    bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, x509);
    PEM_write_bio_RSAPrivateKey(
        bio, EVP_PKEY_get1_RSA(pkey), NULL, NULL, 0, NULL, NULL);
    PEM_write_bio_X509(bio, x509);

    X509_free(x509);
    EVP_PKEY_free(pkey);
   
    buf->length = BIO_get_mem_data(bio, &ptr);
    buf->value = strdup(ptr);
    BIO_free(bio);

    return 0;
}
    

        
        
#define GUC_URL_ENC_CHAR "#;:=+ ,"

/******************************************************************************
                               Type definitions
******************************************************************************/
typedef struct
{
    globus_mutex_t                      mutex;
    globus_cond_t                       cond;
    globus_object_t *                   err;
    globus_bool_t                       use_err;
    globus_bool_t                       transfer_timeout;
    int                                 was_error;
    volatile globus_bool_t              done;
} globus_l_guc_monitor_t;

typedef struct 
{
    globus_gass_copy_glob_entry_t       type;
    char *                              url;
    int                                 mode;
    int                                 mdtm;
    globus_off_t                        size;
    char *                              checksum;
} globus_l_guc_url_info_t;

typedef struct 
{
    char *                              src_url;
    char *                              dst_url;
    globus_off_t                        offset;
    globus_off_t                        length;
    globus_l_guc_url_info_t *           src_info;
} globus_l_guc_src_dst_pair_t;

typedef struct globus_l_guc_handle_s
{
    globus_gass_copy_handle_t           gass_copy_handle;
    globus_gass_copy_handle_t           cksm_gass_copy_handle;

    globus_gass_copy_attr_t             source_gass_copy_attr;
    globus_gass_copy_attr_t             dest_gass_copy_attr;    
    
    globus_ftp_client_operationattr_t   source_ftp_attr;
    globus_ftp_client_operationattr_t   dest_ftp_attr;

    globus_gass_transfer_requestattr_t  source_gass_attr;
    globus_gass_transfer_requestattr_t  dest_gass_attr;
    
    void *                              guc_info;
    void *                              current_transfer;
    globus_l_guc_src_dst_pair_t *       pipeline_free_pair;
    unsigned int                        pipeline_id;
    int                                 id;
} globus_l_guc_handle_t;

typedef struct 
{
    globus_fifo_t                       user_url_list;
    globus_fifo_t                       expanded_url_list;
    globus_fifo_t                       dump_url_list;

    globus_hashtable_t                  recurse_hash;
    globus_hashtable_t                  dest_hash;
    globus_hashtable_t                  mkdir_hash;

    int                                 timeout_secs;

    char *                              dst_module_name;
    char *                              src_module_name;
    char *                              dst_module_args;
    char *                              src_module_args;

    char *                              source_subject;
    char *                              dest_subject;
    unsigned long                       options;
    globus_size_t                       block_size;
    globus_size_t                       tcp_buffer_size;
    int                                 num_streams;
    int                                 conc;
    globus_bool_t                       no_3pt;
    globus_bool_t                       no_dcau;
    globus_bool_t                       data_safe;
    globus_bool_t                       data_private;
    globus_bool_t                       cancelled;
    globus_bool_t                       recurse;
    int                                 restart_retries;
    int                                 restart_interval;
    int                                 restart_timeout;
    int                                 stall_timeout;
    globus_size_t                       stripe_bs;
    globus_bool_t			            striped;
    globus_bool_t			            rfc1738;
    globus_bool_t			            create_dest;
    globus_off_t			            partial_offset;
    globus_off_t			            partial_length;
    globus_bool_t                       list_uses_data_mode;
    globus_bool_t                       udt;
    globus_bool_t                       nl_bottleneck;
    int                                 nl_level;
    int                                 nl_interval;
    globus_bool_t                       ipv6;
    globus_bool_t                       gridftp2;
    globus_bool_t                       allo;
    globus_bool_t                       delayed_pasv;
    globus_bool_t                       pipeline;
    int                                 pipelineq;
    globus_bool_t                       perms;
    globus_bool_t                       sync;
    int                                 sync_level;
    FILE *                              dump_only_fp;
    char *                              dump_only_file;
    char *                              src_pipe_str;
    char *                              dst_pipe_str;
    char *                              src_net_stack_str;
    char *                              src_disk_stack_str;
    char *                              dst_net_stack_str;
    char *                              dst_disk_stack_str;
    char *                              src_authz_assert;
    char *                              dst_authz_assert;
    globus_bool_t                       cache_src_authz_assert;
    globus_bool_t                       cache_dst_authz_assert;
        
    gss_cred_id_t                       src_cred;
    gss_cred_id_t                       dst_cred;
    gss_cred_id_t                       data_cred;
    gss_buffer_desc                     auto_data;
    char *                              src_cred_subj;
    char *                              dst_cred_subj;
    char *                              data_cred_subj;

    char *                              mc_file;
    char *                              dumpfile;
    char *                              alias_file;

    char *                              list_url;
    int                                 conc_outstanding;
    globus_l_guc_handle_t **            handles;
    globus_bool_t                       comp_checksum;
    char *                              checksum_algo;
    
} globus_l_guc_info_t;

typedef struct globus_l_guc_transfer_s
{
    globus_l_guc_info_t *               guc_info;
    globus_l_guc_handle_t *             handle;
    globus_bool_t                       needs_mkdir;
    globus_l_guc_src_dst_pair_t *       urls;
    globus_fifo_t                       matched_url_list;
} globus_l_guc_transfer_t;

typedef struct
{
    char *                              name;
    int                                 entries;
    int                                 index;
    char *                              hostname[1];
} globus_l_guc_alias_t;

enum
{
    GLOBUS_L_GUC_EXISTS = 0,
    GLOBUS_L_GUC_SIZE = 1,
    GLOBUS_L_GUC_DATE = 2,
    GLOBUS_L_GUC_CKSM = 3
} globus_l_guc_sync_level_t;
            

static globus_hashtable_t               guc_l_alias_table;
static globus_bool_t                    guc_l_aliases = GLOBUS_FALSE;
static globus_l_guc_alias_t *           guc_l_src_alias_ent = NULL;
static globus_l_guc_alias_t *           guc_l_dst_alias_ent = NULL;
static globus_bool_t                    guc_l_newline_exit = GLOBUS_FALSE;
static gss_buffer_desc                  auto_data = GSS_C_EMPTY_BUFFER;

/*****************************************************************************
                          Module specific prototypes
*****************************************************************************/

static 
void
globus_l_url_copy_monitor_callback(void * callback_arg,
                                    globus_gass_copy_handle_t * handle,
                                    globus_object_t * result);

static
void
globus_l_gass_copy_performance_cb(
    void *                                          user_arg,
    globus_gass_copy_handle_t *                     handle,
    globus_off_t                                    total_bytes,
    float                                           instantaneous_throughput,
    float                                           avg_throughput);

static
void
globus_l_guc_entry_cb(
    const char *                         url,
    const globus_gass_copy_glob_stat_t * info_stat,
    void *                               user_arg);

static
void
globus_l_guc_dest_entry_cb(
    const char *                         url,
    const globus_gass_copy_glob_stat_t * info_stat,
    void *                               user_arg);
       
static
globus_result_t
globus_l_guc_file_to_string(
    char *                               filename,
    char **                              str);
 
static
int
globus_l_guc_parse_arguments(
    int                                             argc,
    char **                                         argv,
    globus_l_guc_info_t *                           guc_info);

static
globus_result_t
globus_l_guc_expand_urls(
    globus_l_guc_info_t *               guc_info,
    globus_l_guc_handle_t *             handle);

static
globus_result_t
globus_l_guc_expand_single_url(
    globus_l_guc_transfer_t *           transfer_info);

static
globus_result_t
globus_l_guc_transfer_files(
    globus_l_guc_info_t *                        guc_info);

static
globus_result_t
globus_l_guc_transfer(
    globus_l_guc_transfer_t *                    transfer_info);

static
int
globus_l_guc_init_gass_copy_handle(
    globus_gass_copy_handle_t *                     gass_copy_handle,
    globus_l_guc_info_t *                           guc_info, 
    int                                             id);

static
int
globus_l_guc_gass_attr_init(
    globus_gass_copy_attr_t *                       gass_copy_attr,
    globus_gass_transfer_requestattr_t *            gass_attr,
    globus_ftp_client_operationattr_t *             ftp_attr,
    globus_l_guc_info_t *                           guc_info,
    char *                                          url,
    globus_bool_t                                   src,
    globus_bool_t                                   twoparty);

static
globus_io_handle_t *
globus_l_guc_get_io_handle(
    char *                                          url,
    int                                             std_fileno);

static
void
globus_l_guc_info_destroy(
    globus_l_guc_info_t *                           guc_info);
    
static
void
globus_l_guc_destroy_url_list(
    globus_fifo_t *                     url_list);

static 
void
globus_l_guc_hashtable_element_free(
    void *                              datum);

static 
void
globus_l_guc_url_info_free(
    void *                              datum);

static
void
globus_l_guc_url_pair_free(
    globus_l_guc_src_dst_pair_t *       url_pair);

static
globus_result_t
globus_l_guc_create_dir(
    char *                              url,
    globus_l_guc_handle_t *             handle,
    globus_l_guc_info_t *               guc_info);

typedef struct globus_l_guc_plugin_op_s
{
    void *                              handle;
    globus_guc_plugin_funcs_t *         funcs;
    globus_l_guc_monitor_t              monitor;
} globus_l_guc_plugin_op_t;


globus_extension_registry_t             globus_guc_client_plugin_registry;
globus_extension_registry_t             globus_guc_plugin_registry;
static globus_list_t *                  g_client_lib_plugin_list = NULL;


static char *                           g_l_mc_fs_str = NULL;
/*****************************************************************************
                          Module specific variables
*****************************************************************************/

const char * oneline_usage =
"globus-url-copy [-help] [-vb] [-dbg] [-r] [-rst] [-s <subject>]\n"
"                        [-p <parallelism>] [-tcp-bs <size>] [-bs <size>]\n"
"                        -f <filename> | <sourceURL> <destURL>\n";

const char * long_usage =
"\nglobus-url-copy [options] <sourceURL> <destURL>\n"
"globus-url-copy [options] -f <filename>\n\n"

"<sourceURL> may contain wildcard characters * ? and [ ] character ranges\n"
"in the filename only.\n"
"Any url specifying a directory must end with a forward slash '/'\n\n"
"If <sourceURL> is a directory, all files within that directory will\n"
"be copied.\n"
"<destURL> must be a directory if multiple files are being copied.\n\n"
"Note:  If the ftp server from the source url does not support the MLSD\n"
"       command, this client will attempt to transfer subdirectories as\n"
"       files, resulting in an error.  Recursion is not possible in this\n"
"       case, but you can use the -c (continue on errors) option in order\n"
"       to transfer the regular files from the top level directory.\n"
"       **GridFTP servers prior to version 1.17 (Globus Toolkit 3.2)\n"
"         do not support MLSD.\n\n"
"OPTIONS\n"
"  -help | -usage\n"
"       Print help\n"
"  -version\n"
"       Print the version of this program\n"
"  -versions\n"
"       Print the versions of all modules that this program uses\n"
"  -c | -continue-on-error\n"
"       Do not die after any errors.  By default, program will exit after\n"
"       most errors.\n"
"  -a | -ascii\n"
"       Convert the file to/from ASCII format to/from local file format\n"
"  -b | -binary\n"
"       Do not apply any conversion to the files. *default*\n"
"  -f <filename>\n" 
"       Read a list of url pairs from filename.  Each line should contain\n"
"       <sourceURL> <destURL>\n"
"       Enclose URLs with spaces in double quotes (\").\n"
"       Blank lines and lines beginning with # will be ignored.\n"
"  -cd | -create-dest\n" 
"       Create destination directory if needed\n"
"  -r | -recurse\n" 
"       Copy files in subdirectories\n"
   
"  -fast\n" 
"       Recommended when using GridFTP servers. Use MODE E for all data\n"
"       transfers, including reusing data channels between list and transfer\n"
"       operations.\n"
"  -t <transfer time in seconds>\n"
"       Run the transfer for this number of seconds and then end.\n"
"       Useful for performance testing or forced restart loops.\n"   
"  -q | -quiet \n"
"       Suppress all output for successful operation\n"
"  -v | -verbose \n"
"       Display urls being transferred\n"
"  -vb | -verbose-perf \n"
"       During the transfer, display the number of bytes transferred\n"
"       and the transfer rate per second.  Show urls being transferred\n"
"  -dbg | -debugftp \n"
"       Debug ftp connections.  Prints control channel communication\n"
"       to stderr\n"

"  -rst | -restart \n"
"       Restart failed ftp operations.\n"
"  -rst-retries <retries>\n"
"       The maximum number of times to retry the operation before giving\n"
"       up on the transfer.  Use 0 for infinite.  Default is 5.\n"
"  -rst-interval <seconds>\n"
"       The interval in seconds to wait after a failure before retrying\n"
"       the transfer.  Use 0 for an exponential back off.  Default is 0.\n"
"  -rst-timeout <seconds>\n"
"       Maximum time after a failure to keep retrying.  Use 0 for no\n" 
"       timeout.  Default is 0.\n"
"  -stall-timeout | -st <seconds>\n"
"       How long before canceling/restarting a transfer with no data\n"
"       movement.  Set to 0 to disable.  Default is 600 seconds.\n"
"  -df <filename> | -dumpfile <filename>\n"
"       Path to file where untransferred urls will be saved for later\n"
"       restarting.  Resulting file is the same format as the -f input file.\n"
"       If file exists, it will be read and all other url input will be\n"
"       ignored.\n"
"  -do <filename> | -dump-only <filename>\n"
"       Perform no write operations on the destination.  Instead, all files\n"
"       that would be transferred are enumerated and dumped to the specified\n"
"       file.  Resulting file is the same format as the -f input file.\n"
"       Note: if you intend to use this file as input for a future transfer,\n"
"       the -create-dest option will be required if any destination\n"
"       directories do not already exist.\n"
"  -rp | -relative-paths\n"
"      The path portion of ftp urls will be interpreted as relative to the\n"
"      user's starting directory on the server.  By default, all paths are\n"
"      root-relative.  When this flag is set, the path portion of the ftp url\n"
"      must start with %%2F if it designates a root-relative path.\n"
   
"  -s  <subject> | -subject <subject>\n"
"       Use this subject to match with both the source and dest servers\n"
"  -ss <subject> | -source-subject <subject>\n"
"       Use this subject to match with the source server\n"
"  -ds <subject> | -dest-subject <subject>\n"
"       Use this subject to match with the destination server\n"
"  -tcp-bs <size> | -tcp-buffer-size <size>\n"
"       specify the size (in bytes) of the buffer to be used by the\n"
"       underlying ftp data channels\n"
"  -bs <block size> | -block-size <block size>\n"
"       specify the size (in bytes) of the buffer to be used by the\n"
"       underlying transfer methods\n"
"  -p <parallelism> | -parallel <parallelism>\n"
"       specify the number of parallel data connections should be used.\n"
   
"  -notpt | -no-third-party-transfers\n"
"       turn third-party transfers off (on by default)\n"
"  -nodcau | -no-data-channel-authentication\n"
"       turn off data channel authentication for ftp transfers\n"
"  -dcsafe | -data-channel-safe\n"
"       set data channel protection mode to SAFE\n"
"  -dcpriv | -data-channel-private\n"
"       set data channel protection mode to PRIVATE\n"
   
"  -off | -partial-offset\n"
"       offset for partial ftp file transfers, defaults to 0\n"
"  -len | -partial-length\n"
"       length for partial ftp file transfers, used only for the source url,\n"
"       defaults the full file.\n"
"  -list <url to list>\n"
"  -stripe\n"
"       enable striped transfers on supported servers\n"
"  -striped-block-size | -sbs\n"
"       set layout mode and block size for striped transfers\n"
"       If not set, server defaults will be used.\n"
"       If set to 0, Partitioned mode will be used.\n"
"       If set to >0, Blocked mode will be used, with this as the block size.\n"
"  -ipv6\n"
"       use ipv6 when available (EXPERIMENTAL)\n"
"  -udt\n"
"       Use UDT, a reliable udp based transport protocol, for data transfers\n" 
"  -no-g2 | -nogridftp2\n"
"       disable use of GridFTP v2 protocol enhancements\n"
"  -dp | -delayed-pasv\n"
"       enable delayed passive\n"
"  -mn | -module-name <gridftp storage module name>\n"
"      Set the backend storage module to use for both the source and\n"
"      destination in a GridFTP transfer\n"
"  -mp | -module-parameters <gridftp storage module parameters>\n"
"      Set the backend storage module arguments to use for both the source\n"
"      and destination in a GridFTP transfer\n"
"  -smn | -src-module-name <gridftp storage module name>\n"
"      Set the backend storage module to use for the source in a GridFTP\n"
"      transfer\n"
"  -smp | -src-module-parameters <gridftp storage module parameters>\n"
"      Set the backend storage module arguments to use for the source\n"
"      in a GridFTP transfer\n"
"  -dmn | -dst-module-name <gridftp storage module name>\n"
"      Set the backend storage module to use for the destination in a GridFTP\n"
"      transfer\n"
"  -dmp | -dst-module-parameters <gridftp storage module parameters>\n"
"      Set the backend storage module arguments to use for the destination\n"
"      in a GridFTP transfer\n"
"  -aa | -authz-assert <authorization assertion file>\n"
"      Use the assertions in this file to authorize the access with both\n"
"      source and dest servers\n" 
"  -saa | -src-authz-assert <authorization assertion file>\n"
"      Use the assertions in this file to authorize the access with source\n"
"      server\n" 
"  -daa | -dst-authz-assert <authorization assertion file>\n"
"      Use the assertions in this file to authorize the access with dest\n"
"      server\n" 
"  -cache-aa | -cache-authz-assert\n"
"      Cache the authz assertion for subsequent transfers\n"
"  -cache-saa | -cache-src-authz-assert\n"
"      Cache the src authz assertion for subsequent transfers\n"
"  -cache-daa | -cache-dst-authz-assert\n"
"      Cache the dst authz assertion for subsequent transfers\n"
"  -pipeline | -pp\n"
"      Enable pipelining support for multi-file ftp transfers.  Currently\n"
"      third-party transfers benefit from this. *EXPERIMENTAL*\n"
"  -concurrency | -cc\n"
"      Number of concurrent ftp connections to use for multiple transfers.\n"
"  -nl-bottleneck | -nlb\n"
"      Use NetLogger to estimate speeds of disk and network read/write\n"
"      system calls, and attempt to determine the bottleneck component\n"
"  -src-pipe | -SP <command line>\n"
"     This will use the popen xio driver on the server to run the given\n"
"     command, and the output (stdout) produced will be the data transferred.\n"
"     The path part of the source url is ignored and only the host/port is used.\n"
"     Requires a server configured to allow both the popen driver and the\n"
"     requested command.  Do not use with -src-fsstack.\n"
"  -dst-pipe | -DP <command line>\n"
"     This will use the popen xio driver on the server to run the given\n"
"     command, with the data transferred as its input (stdin).  The\n"
"     path part of the dest url is ignored and only the host/port is used.\n"
"     Requires a server configured to allow both the popen driver and the\n"
"     requested command.  Do not use with -dst-fsstack.\n"
"  -pipe <command line>\n"
"     sets both -src-pipe and -dst-pipe to the same thing\n"
"  -dcstack | -data-channel-stack\n"
"     Set the XIO driver stack for the network on both the source and\n"
"     and the destination.  Both must be gridftp servers. The stack should\n"
"     contain all network drivers to use, in the order specified from bottom\n"
"     to top (e.g. -dcstack tcp,gsi). If the gsi driver is not included in\n"
"     the stack and data channel authentication is enabled, it will be\n"
"     inserted above the transport driver in the stack.\n"
"  -fsstack | -file-system-stack\n"
"     Set the XIO driver stack for the disk on both the source and\n"
"     and the destination.  Both must be gridftp servers\n"
"  -src-dcstack | -source-data-channel-stack\n"
"     Set the XIO driver stack for the network on the source GridFTP server.\n"
"  -src-fsstack | -source-file-system-stack.\n"
"     Set the XIO driver stack for the disk on the source GridFTP server.\n"
"  -dst-dcstack | -dest-data-channel-stack\n"
"     Set the XIO driver stack for the network on the destination GridFTP server.\n"
"  -dst-fsstack | -dest-file-system-stack\n"
"     Set the XIO driver stack for the disk on the destination GridFTP server.\n"
"  -cred <path to credentials or proxy file>\n"
"  -src-cred | -sc <path to credentials or proxy file>\n"
"  -dst-cred | -dc <path to credentials or proxy file>\n"
"     Set the credentials to use for source, destination, \n"
"     or both ftp connections.\n"
"  -data-cred <path to credentials or proxy file>\n"
"     Set the credential to use for data connection.  A value of 'auto' will\n" 
"     generate a temporary self-signed credential.  This may be used with\n"
"     any authentication method, but the server must support the DCSC command.\n"
"  -af <filename> | -alias-file <filename>\n"
"       File with mapping of logical host aliases to lists of physical\n"
"       hosts.  When used with multiple concurrent connections, each\n"
"       connection uses the next host in the list.\n"
"       Each line should either be an alias, noted with the @\n"
"       symbol, or a hostname[:port].\n"
"       Currently, only the aliases @source and @destination are valid,\n"
"       and they are used for every source or destination url.\n"
"  -sync\n"
"       Only transfer files where the destination does not exist or differs\n"
"       from the source.  -sync-level controls how to determine if files\n"
"       differ.\n"
"  -sync-level <number>\n"
"       Choose criteria for determining if files differ when performing a\n"
"       sync transfer.  Level 0 will only transfer if the destination does\n"
"       not exist.  Level 1 will transfer if the size of the destination\n"
"       does not match the size of the source.  Level 2 will transfer if\n"
"       the timestamp of the destination is older than the timestamp of the\n"
"       source, or the sizes do not match.  Level 3 will perform a checksum of\n"
"       the source and destination and transfer if the checksums do not match,\n"
"       or the sizes do not match.  The default sync level is 2.\n"
"  -checksum-alg <checksum algorithm>\n"
"       Set the algorithm type to use for all checksum operations during the\n"
"       transfer.  The default algorithm is MD5.\n"
"  -verify-checksum\n"
"       Specify to perform a checksum on the source and destination after each\n"
"       file transfer and compare the two.  If they do not match, fail the\n"
"       transfer.\n"
"\n";

/***********

this feature has not yet been implemented.

"\t Note: entering a dash \"-\" in the above arguments where <subject> is\n"
"\t       required will result in the subject being obtained from the users\n"
"\t       credentials\n"
"\n";
***********/

#define globus_url_copy_l_args_error(a) \
{ \
    globus_libc_fprintf(stderr, \
                        _GASCSL("\nERROR: " \
                        a \
                        "\n\nSyntax: %s\n" \
                        "\nUse -help to display full usage\n"), \
                        _GASCSL(oneline_usage)); \
    globus_module_deactivate_all(); \
    exit(1); \
}

#define globus_url_copy_l_args_error_fmt(fmt,arg) \
{ \
    globus_libc_fprintf(stderr, \
                        _GASCSL("\nERROR: " \
                        fmt \
                        "\n\nSyntax: %s\n" \
                        "\nUse -help to display full usage\n"), \
                        arg, _GASCSL(oneline_usage)); \
    globus_module_deactivate_all(); \
    exit(1); \
}

static 
int
test_integer( char *   value,
              void *   ignored,
              char **  errmsg )
{
    int  res = !(isdigit(*value) || *value == '-');
    if(res)
        *errmsg = strdup(_GASCSL("argument is not a positive integer"));
    return res;
}

enum 
{ 
    arg_a = 1, 
    arg_b, 
    arg_c, 
    arg_ext,
    arg_plugin,
    arg_mc,
    arg_dumpfile,
    arg_aliasfile,
    arg_modname,
    arg_modargs,
    arg_src_modname,
    arg_src_modargs,
    arg_dst_modname,
    arg_dst_modargs,
    arg_s, 
    arg_t, 
    arg_p, 
    arg_f, 
    arg_vb,
    arg_q, 
    arg_v, 
    arg_debugftp, 
    arg_restart,
    arg_rst_retries, 
    arg_rst_interval, 
    arg_rst_timeout, 
    arg_stall_timeout,
    arg_ss, 
    arg_ds, 
    arg_tcp_bs,
    arg_bs, 
    arg_conc,
    arg_notpt, 
    arg_nodcau,
    arg_data_safe,
    arg_data_private,
    arg_recurse,
    arg_partial_offset,
    arg_partial_length,
    arg_rfc1738,
    arg_create_dest,
    arg_fast,
    arg_list,
    arg_ipv6,
    arg_gridftp2,
    arg_gridftp2_off,
    arg_udt,
    arg_nl_bottleneck,
    arg_nl_interval,
    arg_src_pipe_str,
    arg_dst_pipe_str,
    arg_pipe_str,
    arg_net_stack_str,
    arg_disk_stack_str,
    arg_src_net_stack_str,
    arg_src_disk_stack_str,
    arg_dst_net_stack_str,
    arg_dst_disk_stack_str,
    arg_authz_assert,
    arg_src_authz_assert,
    arg_dst_authz_assert,
    arg_cache_authz_assert,
    arg_cache_src_authz_assert,
    arg_cache_dst_authz_assert,
    arg_cred,
    arg_src_cred,
    arg_dst_cred,
    arg_data_cred,
    arg_allo,
    arg_noallo,
    arg_delayed_pasv,
    arg_pipeline,
    arg_pipelineq,
    arg_sync,
    arg_sync_level,
    arg_dump_only,
    arg_preserve,
    arg_perms,
    arg_stripe_bs,
    arg_striped,
    arg_checksum,
    arg_checksum_algo,
    arg_num = arg_checksum_algo,
};

#define listname(x) x##_aliases
#define namedef(id,alias1,alias2) \
static char * listname(id)[] = { alias1, alias2, GLOBUS_NULL }

#define defname(x) x##_definition
#define flagdef(id,alias1,alias2) \
namedef(id,alias1,alias2); \
static globus_args_option_descriptor_t defname(id) = { id, listname(id), 0, \
						GLOBUS_NULL, GLOBUS_NULL }
#define funcname(x) x##_predicate_test
#define paramsname(x) x##_predicate_params

#define oneargdef(id,alias1,alias2,testfunc,testparams) \
namedef(id,alias1,alias2); \
static globus_args_valid_predicate_t funcname(id)[] = { testfunc }; \
static void* paramsname(id)[] = { (void *) testparams }; \
globus_args_option_descriptor_t defname(id) = \
    { (int) id, (char **) listname(id), 1, funcname(id), (void **) paramsname(id) }

#define twoargdef(id,alias1,alias2,testfunc,testparams)                     \
    namedef(id,alias1,alias2);                                              \
    static globus_args_valid_predicate_t funcname(id)[] = { testfunc };     \
    static void* paramsname(id)[] = { (void *) testparams };                \
    globus_args_option_descriptor_t defname(id) =                           \
    {                                                                       \
        (int) id, (char **) listname(id), 2, funcname(id),                  \
            (void **) paramsname(id)                                        \
    }                                                                       \

flagdef(arg_a, "-a", "-ascii");
flagdef(arg_b, "-b", "-binary");
flagdef(arg_c, "-c", "-continue-on-error");
flagdef(arg_q, "-q", "-quiet");
flagdef(arg_v, "-v", "-verbose");
flagdef(arg_vb, "-vb", "-verbose-perf");
flagdef(arg_debugftp, "-dbg", "-debugftp");
flagdef(arg_restart, "-rst", "-restart");
flagdef(arg_notpt, "-notpt", "-no-third-party-transfers");
flagdef(arg_nodcau, "-nodcau", "-no-data-channel-authentication");
flagdef(arg_data_safe, "-dcsafe", "-data-channel-safe");
flagdef(arg_data_private, "-dcpriv", "-data-channel-private");
flagdef(arg_recurse, "-r", "-recurse");
flagdef(arg_striped, "-stripe", "-striped");
flagdef(arg_rfc1738, "-rp", "-relative-paths");
flagdef(arg_create_dest, "-cd", "-create-dest");
flagdef(arg_fast, "-fast", "-fast-data-channels");
flagdef(arg_ipv6, "-ipv6","-IPv6");
flagdef(arg_gridftp2, "-g2","-gridftp2");
flagdef(arg_gridftp2_off, "-no-g2","-nogridftp2");
flagdef(arg_udt, "-u","-udt");
flagdef(arg_delayed_pasv, "-dp","-delayed-pasv");
flagdef(arg_pipeline, "-pp","-pipeline");
flagdef(arg_allo, "-allo","-allocate");
flagdef(arg_noallo, "-no-allo","-no-allocate");
flagdef(arg_cache_authz_assert, "-cache-aa","-cache-authz-assert");
flagdef(arg_cache_src_authz_assert, "-cache-saa","-cache-src-authz-assert");
flagdef(arg_cache_dst_authz_assert, "-cache-daa","-cache-dst-authz-assert");
flagdef(arg_nl_bottleneck, "-nlb","-nl-bottleneck");
flagdef(arg_sync, "-sync","-sync");
flagdef(arg_preserve, "-preserve", "-preserve");
flagdef(arg_perms, "-cp","-copy-perms");
flagdef(arg_checksum, "-verify-checksum", "-checksum");

oneargdef(arg_pipelineq, "-ppq","-pipelineq", NULL, NULL);
oneargdef(arg_list, "-list", "-list-url", NULL, NULL);
oneargdef(arg_nl_interval, "-nli","-nl-interval", NULL, NULL);
oneargdef(arg_ext, "-X", "-extentions", NULL, NULL);
oneargdef(arg_mc, "-MC", "-multicast", NULL, NULL);
oneargdef(arg_dumpfile, "-df", "-dumpfile", NULL, NULL);
oneargdef(arg_aliasfile, "-af", "-alias-file", NULL, NULL);
oneargdef(arg_plugin, "-CP", "-plugin", NULL, NULL);
oneargdef(arg_modname, "-mn", "-module-name", NULL, NULL);
oneargdef(arg_modargs, "-mp", "-module-parameters", NULL, NULL);
oneargdef(arg_src_modname, "-smn", "-src-module-name", NULL, NULL);
oneargdef(arg_src_modargs, "-smp", "-src-module-parameters", NULL, NULL);
oneargdef(arg_dst_modname, "-dmn", "-dst-module-name", NULL, NULL);
oneargdef(arg_dst_modargs, "-dmp", "-dst-module-parameters", NULL, NULL);
oneargdef(arg_f, "-f", "-filename", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_conc, "-cc", "-concurrency", test_integer, GLOBUS_NULL);
oneargdef(arg_stripe_bs, "-sbs", "-striped-block-size", test_integer, GLOBUS_NULL);
oneargdef(arg_bs, "-bs", "-block-size", test_integer, GLOBUS_NULL);
oneargdef(arg_tcp_bs, "-tcp-bs", "-tcp-buffer-size", test_integer, GLOBUS_NULL);
oneargdef(arg_p, "-p", "-parallel", test_integer, GLOBUS_NULL);
oneargdef(arg_t, "-t", "-transfer-time", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_s, "-s", "-subject", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_ss, "-ss", "-source-subject", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_ds, "-ds", "-dest-subject", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_rst_retries, "-rst-retries", "-restart-retries", test_integer, GLOBUS_NULL);
oneargdef(arg_rst_interval, "-rst-interval", "-restart-interval", test_integer, GLOBUS_NULL);
oneargdef(arg_rst_timeout, "-rst-timeout", "-restart-timeout", test_integer, GLOBUS_NULL);
oneargdef(arg_stall_timeout, "-st", "-stall-timeout", test_integer, GLOBUS_NULL);
oneargdef(arg_partial_offset, "-off", "-partial-offset", test_integer, GLOBUS_NULL);
oneargdef(arg_partial_length, "-len", "-partial-length", test_integer, GLOBUS_NULL);
oneargdef(arg_src_pipe_str, "-SP", "-src-pipe", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_dst_pipe_str, "-DP", "-dst-pipe", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_pipe_str, "-P", "-pipe", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_net_stack_str, "-dcstack", "-data-channel-stack", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_disk_stack_str, "-fsstack", "-file-system-stack", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_src_net_stack_str, "-src-dcstack", "-source-data-channel-stack", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_src_disk_stack_str, "-src-fsstack", "-source-file-system-stack", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_dst_net_stack_str, "-dst-dcstack", "-dest-data-channel-stack", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_dst_disk_stack_str, "-dst-fsstack", "-dest-file-system-stack", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_authz_assert, "-aa", "-authz-assert", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_src_authz_assert, "-saa", "-src-authz-assert", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_dst_authz_assert, "-daa", "-dst-authz-assert", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_cred, "-cred", "-cred", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_src_cred, "-sc", "-src-cred", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_dst_cred, "-dc", "-dst-cred", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_data_cred, "-data-cred", "-data-cred", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_sync_level, "-sync-level", "-sync-level", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_dump_only, "-do", "-dump-only", GLOBUS_NULL, GLOBUS_NULL);
oneargdef(arg_checksum_algo, "-checksum-alg", "-algo", GLOBUS_NULL, GLOBUS_NULL);


static globus_args_option_descriptor_t args_options[arg_num];

#define setupopt(id) args_options[id-1] = defname(id)

#define globus_url_copy_i_args_init()   \
    setupopt(arg_a);                    \
    setupopt(arg_f);                    \
    setupopt(arg_c);                    \
    setupopt(arg_ext);                  \
    setupopt(arg_mc);                   \
    setupopt(arg_dumpfile);             \
    setupopt(arg_aliasfile);            \
    setupopt(arg_plugin);               \
    setupopt(arg_modname);              \
    setupopt(arg_modargs);              \
    setupopt(arg_src_modname);          \
    setupopt(arg_src_modargs);          \
    setupopt(arg_dst_modname);          \
    setupopt(arg_dst_modargs);          \
    setupopt(arg_b);                    \
    setupopt(arg_s);                    \
    setupopt(arg_t);                    \
    setupopt(arg_q);                    \
    setupopt(arg_v);                    \
    setupopt(arg_vb);                   \
    setupopt(arg_debugftp);             \
    setupopt(arg_restart);              \
    setupopt(arg_rst_retries);          \
    setupopt(arg_rst_interval);         \
    setupopt(arg_rst_timeout);          \
    setupopt(arg_stall_timeout);        \
    setupopt(arg_ss);                   \
    setupopt(arg_ds);                   \
    setupopt(arg_tcp_bs);               \
    setupopt(arg_bs);                   \
    setupopt(arg_conc);                 \
    setupopt(arg_p);                    \
    setupopt(arg_notpt);                \
    setupopt(arg_nodcau);               \
    setupopt(arg_data_safe);            \
    setupopt(arg_data_private);         \
    setupopt(arg_recurse);		\
    setupopt(arg_partial_offset);	\
    setupopt(arg_partial_length);	\
    setupopt(arg_rfc1738);      	\
    setupopt(arg_create_dest);          \
    setupopt(arg_fast);	                \
    setupopt(arg_list);	                \
    setupopt(arg_udt);                  \
    setupopt(arg_nl_bottleneck);        \
    setupopt(arg_nl_interval);        \
    setupopt(arg_ipv6);         	\
    setupopt(arg_gridftp2_off);         \
    setupopt(arg_gridftp2);         	\
    setupopt(arg_src_pipe_str);        \
    setupopt(arg_dst_pipe_str);        \
    setupopt(arg_pipe_str);        \
    setupopt(arg_net_stack_str);        \
    setupopt(arg_disk_stack_str);        \
    setupopt(arg_src_net_stack_str);        \
    setupopt(arg_src_disk_stack_str);        \
    setupopt(arg_dst_net_stack_str);        \
    setupopt(arg_dst_disk_stack_str);        \
    setupopt(arg_authz_assert);         \
    setupopt(arg_src_authz_assert);     \
    setupopt(arg_dst_authz_assert);     \
    setupopt(arg_cache_authz_assert);         \
    setupopt(arg_cache_src_authz_assert);     \
    setupopt(arg_cache_dst_authz_assert);     \
    setupopt(arg_delayed_pasv);         \
    setupopt(arg_pipeline);             \
    setupopt(arg_pipelineq);             \
    setupopt(arg_sync);                 \
    setupopt(arg_sync_level);           \
    setupopt(arg_dump_only);            \
    setupopt(arg_preserve);             \
    setupopt(arg_perms);                \
    setupopt(arg_allo);         	\
    setupopt(arg_noallo);         	\
    setupopt(arg_cred);         	\
    setupopt(arg_src_cred);         	\
    setupopt(arg_dst_cred);         	\
    setupopt(arg_data_cred);         	\
    setupopt(arg_stripe_bs);         	\
    setupopt(arg_striped);              \
    setupopt(arg_checksum);             \
    setupopt(arg_checksum_algo);

static globus_bool_t globus_l_globus_url_copy_ctrlc = GLOBUS_FALSE;
static globus_bool_t globus_l_globus_url_copy_ctrlc_handled = GLOBUS_FALSE;
static globus_bool_t g_verbose_flag = GLOBUS_FALSE;
static globus_bool_t g_quiet_flag = GLOBUS_TRUE;
static globus_bool_t g_all_quiet = GLOBUS_FALSE;
static globus_bool_t g_ssh_print_connect = GLOBUS_FALSE;
static char *  g_ext = NULL;
static char ** g_ext_args = NULL;
static int      g_ext_arg_count = 0;
static int      g_transfer_timeout = 0;
static globus_bool_t g_use_debug = GLOBUS_FALSE;
static globus_bool_t g_use_restart = GLOBUS_FALSE;
static globus_bool_t g_continue = GLOBUS_FALSE;
static char *        g_err_msg;
static globus_l_guc_monitor_t           g_monitor = {};

/* for multicast stuff */
static globus_fifo_t                    guc_mc_url_q;

static
void
globus_l_guc_glob_list_cb(
    const char *                         url,
    const globus_gass_copy_glob_stat_t * info_stat,
    void *                               user_arg)
{
    int                                 end_ndx;
    char *                              tmp_str;
    globus_l_guc_info_t *               guc_info;
    globus_url_t                        url_info;
    char                                end_ch = ' ';

    memset(&url_info, '\0', sizeof(globus_url_t));
    globus_url_parse(url, &url_info);
    guc_info = (globus_l_guc_info_t *) user_arg;
    if(info_stat->type == GLOBUS_GASS_COPY_GLOB_ENTRY_DIR &&
        guc_info->recurse)
    {
        globus_fifo_enqueue(&guc_info->expanded_url_list, strdup(url));
    }

    if(url_info.url_path == NULL)
    {
        printf("    %s\n", url);
        return;
    }
    else
    {
        end_ndx = strlen(url_info.url_path) - 1;
        if(url_info.url_path[end_ndx] == '/' && end_ndx > 0)
        {
            url_info.url_path[end_ndx] = '\0';
            end_ch = '/';
        }

        tmp_str = strrchr(url_info.url_path, '/');
        *tmp_str = '\0';
        tmp_str++;
        printf("    %s%c\n", tmp_str, end_ch);
    }
    globus_url_destroy(&url_info);
}
    
static
void
globus_l_guc_ext_interrupt_handler(
    void *                              user_arg)
{
    globus_l_guc_plugin_op_t *          done_op;

    done_op = (globus_l_guc_plugin_op_t *) user_arg;
    done_op->funcs->cancel_func(done_op->handle);
}

static
void
globus_l_guc_ext_interrupt_unreg_sb(
    void *                              user_arg)
{
    globus_l_guc_plugin_op_t *          done_op;

    done_op = (globus_l_guc_plugin_op_t *) user_arg;

    globus_mutex_lock(&done_op->monitor.mutex);
    {
        done_op->monitor.done = GLOBUS_TRUE;
        globus_cond_signal(&done_op->monitor.cond);
    }
    globus_mutex_unlock(&done_op->monitor.mutex);
}

void
globus_guc_plugin_finished(
    globus_guc_plugin_op_t              done_op,
    globus_result_t                     result)
{
    globus_l_guc_plugin_op_t *          op;

    op = (globus_l_guc_plugin_op_t *) done_op;

    globus_mutex_lock(&op->monitor.mutex);
    {
        if(result != GLOBUS_SUCCESS)
        {
            op->monitor.err = globus_error_get(result);
        }
        op->monitor.done = GLOBUS_TRUE;
        globus_cond_signal(&op->monitor.cond);
    }
    globus_mutex_unlock(&op->monitor.mutex);
}

static
int
globus_l_guc_ext(
    globus_l_guc_info_t *               guc_info)
{
    globus_object_t *                   err;
    globus_result_t                     res;
    globus_guc_info_t                   ext_info;
    int                                 rc;
    globus_extension_handle_t           ext_handle;
    globus_l_guc_plugin_op_t            done_op;

    /* copy in ext info */
    ext_info.user_url_list = &guc_info->user_url_list;
    ext_info.source_subject = guc_info->source_subject;
    ext_info.dest_subject = guc_info->dest_subject;
    ext_info.options = guc_info->options;
    ext_info.block_size = guc_info->block_size;
    ext_info.tcp_buffer_size = guc_info->tcp_buffer_size;
    ext_info.num_streams = guc_info->num_streams;
    ext_info.conc = guc_info->conc;
    ext_info.no_3pt = guc_info->no_3pt;
    ext_info.no_dcau = guc_info->no_dcau;
    ext_info.data_safe = guc_info->data_safe;
    ext_info.data_private = guc_info->data_private;
    ext_info.cancelled = guc_info->cancelled;
    ext_info.recurse = guc_info->recurse;
    ext_info.restart_retries = guc_info->restart_retries;
    ext_info.restart_interval = guc_info->restart_interval;
    ext_info.restart_timeout = guc_info->restart_timeout;
    ext_info.stall_timeout = guc_info->stall_timeout;
    ext_info.stripe_bs = guc_info->stripe_bs;
    ext_info.striped = guc_info->striped;
    ext_info.rfc1738 = guc_info->rfc1738;
    ext_info.create_dest = guc_info->create_dest;
    ext_info.partial_offset = guc_info->partial_offset;
    ext_info.partial_length = guc_info->partial_length;
    ext_info.list_uses_data_mode = guc_info->list_uses_data_mode;
    ext_info.ipv6 = guc_info->ipv6;
    ext_info.gridftp2 = guc_info->gridftp2;
    ext_info.src_net_stack_str = guc_info->src_net_stack_str;
    ext_info.src_disk_stack_str = guc_info->src_disk_stack_str;
    ext_info.dst_net_stack_str = guc_info->dst_net_stack_str;
    ext_info.dst_disk_stack_str = guc_info->dst_disk_stack_str;
    ext_info.src_authz_assert = guc_info->src_authz_assert;
    ext_info.dst_authz_assert = guc_info->dst_authz_assert;
    ext_info.cache_src_authz_assert = guc_info->cache_src_authz_assert;
    ext_info.cache_dst_authz_assert = guc_info->cache_dst_authz_assert;
    ext_info.allo = guc_info->allo;
    ext_info.verbose = g_verbose_flag;
    ext_info.quiet = g_quiet_flag;
    ext_info.delayed_pasv = guc_info->delayed_pasv;
    ext_info.pipeline = guc_info->pipeline;
    ext_info.src_cred = guc_info->src_cred;
    ext_info.dst_cred = guc_info->dst_cred;
    ext_info.data_cred = guc_info->data_cred;

    rc = globus_extension_activate(g_ext);
    if(rc != 0)
    {
        fprintf(stderr, "Failed to load crft extension\n");
        return rc;
    }

    memset(&done_op, '\0', sizeof(globus_l_guc_plugin_op_t));
    done_op.funcs = (globus_guc_plugin_funcs_t *) globus_extension_lookup(
        &ext_handle, &globus_guc_plugin_registry, GUC_PLUGIN_FUNCS);
    if(done_op.funcs == NULL)
    {
        fprintf(stderr, "Failed to find crft extension structure.\n");
        return 1;
    }

    globus_mutex_init(&done_op.monitor.mutex, NULL);
    globus_cond_init(&done_op.monitor.cond, NULL);
    done_op.monitor.done = GLOBUS_FALSE;

    globus_mutex_lock(&done_op.monitor.mutex);
    {
        res = done_op.funcs->start_func(
            &done_op.handle, &ext_info, &done_op, g_ext_arg_count, g_ext_args);
        if(res != GLOBUS_SUCCESS)
        {
            err = globus_error_get(res);
            goto error;
        }
        globus_callback_register_signal_handler(
            GLOBUS_SIGNAL_INTERRUPT,
            GLOBUS_FALSE,
            globus_l_guc_ext_interrupt_handler,
            &done_op);

        while(!done_op.monitor.done)
        {
            globus_cond_wait(&done_op.monitor.cond, &done_op.monitor.mutex);
        }
        done_op.monitor.done = GLOBUS_FALSE;
        globus_callback_unregister_signal_handler(
            GLOBUS_SIGNAL_INTERRUPT,
            globus_l_guc_ext_interrupt_unreg_sb,
            &done_op);
        while(!done_op.monitor.done)
        {
            globus_cond_wait(&done_op.monitor.cond, &done_op.monitor.mutex);
        }
        done_op.funcs->cleanup_func(done_op.handle);

        if(done_op.monitor.err != NULL)
        {
            err = done_op.monitor.err;
            goto error;
        }
    }
    globus_mutex_unlock(&done_op.monitor.mutex);

    return 0;
error:

    fprintf(stderr, "%s\n", globus_error_print_friendly(err));
 
    return 1;
}
/* END CRFT STUFF */

/*
 *  also use this to end transfers for the timeout callback
 */
static
void
globus_l_guc_interrupt_handler(
    void *                              user_arg)
{
    globus_l_guc_monitor_t *            monitor;

    if(globus_l_globus_url_copy_ctrlc)
    {
        return;
    }

    monitor = (globus_l_guc_monitor_t *) user_arg;
    if(!monitor)
    {
        monitor = &g_monitor;
        monitor->transfer_timeout = GLOBUS_TRUE;
    }
    globus_mutex_lock(&monitor->mutex);
    {
        globus_l_globus_url_copy_ctrlc = GLOBUS_TRUE;
        globus_cond_signal(&monitor->cond);
    }
    globus_mutex_unlock(&monitor->mutex);
}


static
int
globus_l_guc_enqueue_pair(
    globus_fifo_t *                     q,
    globus_l_guc_src_dst_pair_t *       pair)
{
    return globus_fifo_enqueue(q, pair);
}

char *
globus_l_guc_url_replace_host(
    char *                              in_url,
    char *                              host)
{
    char *                              out;
    char *                              ptr;
    char *                              tmp;
    char *                              end;
    
    tmp = globus_libc_strdup(in_url);
    ptr = strstr(tmp, "ftp://");
    if(ptr == NULL)
    {
        goto skip;
    }
    ptr += 5;
    *ptr = '\0';
    ptr++;
    end = strchr(ptr, '/');
    if(end == NULL)
    {
        goto skip;
    }
    
    out = globus_common_create_string("%s/%s%s", tmp, host, end);
    globus_free(tmp);
    
    return out;
    
skip:
    globus_free(tmp);
    return NULL;    
}

static
globus_l_guc_src_dst_pair_t *
globus_l_guc_dequeue_pair(
    globus_fifo_t *                     q,
    int                                 handle_id)
{
    globus_l_guc_src_dst_pair_t *       pair;
    globus_l_guc_alias_t *              src_alias = NULL;
    globus_l_guc_alias_t *              dst_alias = NULL;
    char *                              new_src;
    char *                              new_dst;
    
    pair = (globus_l_guc_src_dst_pair_t *) globus_fifo_dequeue(q);
    
    if(guc_l_aliases)
    {
        /*
        src_alias = (globus_l_guc_alias_t *) 
            globus_hashtable_lookup(&guc_l_alias_table, (void *) "source");
        dst_alias = (globus_l_guc_alias_t *) 
            globus_hashtable_lookup(&guc_l_alias_table, (void *) "destination");
        */
        src_alias = guc_l_src_alias_ent;
        dst_alias = guc_l_dst_alias_ent;
        
        if(src_alias && src_alias->entries > 0)
        {
            new_src = globus_l_guc_url_replace_host(
                pair->src_url, src_alias->hostname[handle_id % src_alias->entries]);
            if(new_src != NULL)
            {
                globus_free(pair->src_url);
                pair->src_url = new_src;
            }
        }
    
        if(dst_alias && dst_alias->entries > 0)
        {
            new_dst = globus_l_guc_url_replace_host(
                pair->dst_url, dst_alias->hostname[handle_id % dst_alias->entries]);
            if(new_dst != NULL)
            {
                globus_free(pair->dst_url);
                pair->dst_url = new_dst;
            }
        }
    }
    
    if(!g_ssh_print_connect && !g_all_quiet &&
        strncmp(pair->src_url, "sshftp://", 9) == 0 &&
        strncmp(pair->dst_url, "sshftp://", 9) == 0)
    {
        g_ssh_print_connect = GLOBUS_TRUE;

        if(isatty(STDIN_FILENO))
        {
            globus_libc_setenv("GLOBUS_SSHFTP_PRINT_ON_CONNECT", "1", 1);
        }
    }

    return pair;

}

static
int
globus_l_guc_print_url_line(
    FILE *                              fp,
    char *                              src_url,
    char *                              dst_url,
    globus_off_t                        offset,
    globus_off_t                        length,
    globus_l_guc_url_info_t *           info,
    char *                              prefix)
{
    int                                 rc;
    char *                              pre;
    char *                              statstr = NULL;
    char *                              tmpstr = NULL;
        
    pre = prefix ? prefix : "";
    
    statstr = globus_libc_strdup("");

    if(info)
    {        
        if(info->size >= 0 && info->type == GLOBUS_GASS_COPY_GLOB_ENTRY_FILE)
        {
            tmpstr = globus_common_create_string(
                "%ssize=%"GLOBUS_OFF_T_FORMAT";", statstr, info->size);
            globus_free(statstr);
            statstr = tmpstr;
        }
        if(info->mdtm >= 0)
        {
            tmpstr = globus_common_create_string(
                "%smodify=%d;", statstr, info->mdtm);
            globus_free(statstr);
            statstr = tmpstr;
        }
        if(info->mode >= 0)
        {
            tmpstr = globus_common_create_string(
                "%smode=%04o;", statstr, info->mode);
            globus_free(statstr);
            statstr = tmpstr;
        }
        if(info->checksum && info->type == GLOBUS_GASS_COPY_GLOB_ENTRY_FILE)
        {
            tmpstr = globus_common_create_string(
                "%scksm=%s;", statstr, info->checksum);
            globus_free(statstr);
            statstr = tmpstr;
        }
    }
    
    if(length > -1)
    {
        rc = fprintf(
            fp, 
            "%s\"%s\" \"%s\" %"GLOBUS_OFF_T_FORMAT",%"GLOBUS_OFF_T_FORMAT" %s\n",
            pre, src_url, dst_url, offset, length, statstr);                
    }
    else if(offset > -1)
    {                
        rc = fprintf(
            fp, 
            "%s\"%s\" \"%s\" %"GLOBUS_OFF_T_FORMAT" %s\n", 
            pre, src_url, dst_url, offset, statstr);
    }
    else if(tmpstr)
    {
        rc = fprintf(fp, "%s\"%s\" \"%s\" 0,-1 %s\n", 
            pre, src_url, dst_url, statstr);
    }                
    else
    {
        rc = fprintf(fp, "%s\"%s\" \"%s\"\n", 
            pre, src_url, dst_url);
    }                

    globus_free(statstr);
    return rc;
}

static
void
globus_l_guc_dump_urls(
    void *                              user_arg)
{
    globus_l_guc_info_t *               guc_info;
    globus_fifo_t *                     tmp_fifo;
    globus_l_guc_src_dst_pair_t *       url_pair;
    FILE *                              dumpfile;
    char *                              dumptmp;
#ifdef WIN32
    char *                              dumptmp2;
#endif
    int                                 dumpfd;
    int                                 i;
    globus_l_guc_transfer_t *           transfer_info;

    guc_info = (globus_l_guc_info_t *) user_arg;
    
    globus_mutex_lock(&g_monitor.mutex);

    if(guc_info->dumpfile)
    {
        dumptmp = globus_common_create_string("%s.XXXXXX", guc_info->dumpfile);
#ifdef WIN32
        dumptmp2 = mktemp(dumptmp);
        dumpfd = open(dumptmp2, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
#else
        dumpfd = mkstemp(dumptmp);
#endif
        if(dumpfd < 0)
        {
            return;
        }
        dumpfile = fdopen(dumpfd, "w");
        if(!dumpfile)
        {
            close(dumpfd);
            unlink(dumptmp);
            return;
        }
            
        for(i = 0; i < guc_info->conc; i++)
        {
            transfer_info = (globus_l_guc_transfer_t *) 
                guc_info->handles[i]->current_transfer;
            if(transfer_info)
            {
                globus_ftp_client_handle_t              ftp_handle = NULL;
                globus_ftp_client_restart_marker_t      marker;
                globus_off_t                            start_offset = 0;
                globus_off_t                            end_offset = 0;
                globus_off_t                off = transfer_info->urls->offset;
                globus_off_t                len = transfer_info->urls->length;
                
                globus_gass_copy_get_ftp_handle(
                    &guc_info->handles[i]->gass_copy_handle, &ftp_handle);
                globus_ftp_client_handle_get_restart_marker(
                    &ftp_handle, &marker);
                globus_ftp_client_restart_marker_get_first_block(
                    &marker, &start_offset, &end_offset);
                globus_ftp_client_restart_marker_destroy(&marker);
                            
                if(transfer_info->urls->length > -1)
                {
                    off = transfer_info->urls->offset + end_offset;
                    len = transfer_info->urls->length - (end_offset - start_offset);               
                }
                else if(end_offset > 0 || transfer_info->urls->offset > -1)
                {                
                    off = end_offset > transfer_info->urls->offset ? 
                            end_offset : transfer_info->urls->offset;
                }

                globus_l_guc_print_url_line(
                    dumpfile,
                    transfer_info->urls->src_url,
                    transfer_info->urls->dst_url,
                    off,
                    len,
                    transfer_info->urls->src_info,
                    NULL);
            }            
        }
         
        if(!globus_fifo_empty(&guc_info->expanded_url_list))
        {
            tmp_fifo = globus_fifo_copy(&guc_info->expanded_url_list);
            
            while(!globus_fifo_empty(tmp_fifo))
            {
                url_pair = 
                    (globus_l_guc_src_dst_pair_t *) globus_fifo_dequeue(tmp_fifo);
                
                globus_l_guc_print_url_line(
                    dumpfile,
                    url_pair->src_url,
                    url_pair->dst_url,
                    url_pair->offset,
                    url_pair->length,
                    url_pair->src_info,
                    NULL);
            }
            globus_fifo_destroy(tmp_fifo);
        }
    
        if(!globus_fifo_empty(&guc_info->user_url_list))
        {
            tmp_fifo = globus_fifo_copy(&guc_info->user_url_list);
            
            while(!globus_fifo_empty(tmp_fifo))
            {
                url_pair = 
                    (globus_l_guc_src_dst_pair_t *) globus_fifo_dequeue(tmp_fifo);
            
                globus_l_guc_print_url_line(
                    dumpfile,
                    url_pair->src_url,
                    url_pair->dst_url,
                    url_pair->offset,
                    url_pair->length,
                    url_pair->src_info,
                    NULL);
            }
            globus_fifo_destroy(tmp_fifo);
        }
                
        fclose(dumpfile);
        if(rename(dumptmp, guc_info->dumpfile) < 0)
        {
            unlink(dumptmp);
        }
        globus_free(dumptmp);
    }
    
    if(guc_info->dump_only_file && guc_info->dump_only_fp == NULL)
    {
        dumptmp = globus_common_create_string("%s.XXXXXX", guc_info->dump_only_file);
#ifdef WIN32
        dumptmp2 = mktemp(dumptmp);
        dumpfd = open(dumptmp2, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
#else
        dumpfd = mkstemp(dumptmp);
#endif
        if(dumpfd < 0)
        {
            return;
        }
        dumpfile = fdopen(dumpfd, "w");
        if(!dumpfile)
        {
            close(dumpfd);
            unlink(dumptmp);
            return;
        }    
         
        if(!globus_fifo_empty(&guc_info->dump_url_list))
        {
            tmp_fifo = globus_fifo_copy(&guc_info->dump_url_list);
            
            while(!globus_fifo_empty(tmp_fifo))
            {
                char *                  is_dir;
                
                url_pair = 
                    (globus_l_guc_src_dst_pair_t *) globus_fifo_dequeue(tmp_fifo);
                
                is_dir = 
                    url_pair->src_url[strlen(url_pair->src_url) - 1] == '/' ?
                        "#" : NULL;

                globus_l_guc_print_url_line(
                    dumpfile,
                    url_pair->src_url,
                    url_pair->dst_url,
                    url_pair->offset,
                    url_pair->length,
                    url_pair->src_info,
                    is_dir);
            }
            globus_fifo_destroy(tmp_fifo);
        }

        fclose(dumpfile);
        if(rename(dumptmp, guc_info->dump_only_file) < 0)
        {
            unlink(dumptmp);
        }
        globus_free(dumptmp);
        
    }    

    globus_mutex_unlock(&g_monitor.mutex);

    return;
}


static
void
globus_l_guc_transfer_kickout(
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_l_guc_transfer_t *           transfer_info;
    globus_bool_t                       retry = GLOBUS_FALSE;
    globus_bool_t                       expanded = GLOBUS_FALSE;
    globus_bool_t                       expand = GLOBUS_FALSE;
    globus_object_t *                   err = NULL;
    
    transfer_info = (globus_l_guc_transfer_t * )  user_arg;

    globus_mutex_lock(&g_monitor.mutex);
    {        
        if(!g_monitor.done && !transfer_info->guc_info->cancelled &&
            !globus_fifo_empty(&transfer_info->guc_info->expanded_url_list))
        {   
            globus_l_guc_url_pair_free(transfer_info->urls);
            transfer_info->urls = globus_l_guc_dequeue_pair(
                &transfer_info->guc_info->expanded_url_list,
                transfer_info->handle->id);
            expanded = GLOBUS_TRUE;

            transfer_info->guc_info->conc_outstanding++;
            transfer_info->handle->current_transfer = transfer_info;
        }
        else if(!g_monitor.done && !transfer_info->guc_info->cancelled &&
            !globus_fifo_empty(&transfer_info->guc_info->user_url_list))
        {        
            globus_l_guc_url_pair_free(transfer_info->urls);
            transfer_info->urls = globus_l_guc_dequeue_pair(
                &transfer_info->guc_info->user_url_list,
                transfer_info->handle->id);
                        
            transfer_info->guc_info->conc_outstanding++;
            transfer_info->handle->current_transfer = transfer_info;
            if(transfer_info->guc_info->create_dest)
            {
                transfer_info->needs_mkdir = GLOBUS_TRUE;
            }
            
            expand = GLOBUS_TRUE;
        }
        else
        {
            globus_l_guc_url_pair_free(transfer_info->urls);
            transfer_info->urls = NULL;
            if(transfer_info->guc_info->conc_outstanding == 0)
            {
                g_monitor.done = GLOBUS_TRUE;
                globus_cond_signal(&g_monitor.cond);
            }
            else
            {
                retry = GLOBUS_TRUE;
            }
        }
        
    }
    globus_mutex_unlock(&g_monitor.mutex);
    
    if(expanded)
    {        
        result = globus_l_guc_transfer(transfer_info);
        if(result != GLOBUS_SUCCESS && !g_continue)
        {                        
            globus_mutex_lock(&g_monitor.mutex);
            {
                g_monitor.done = GLOBUS_TRUE;
                g_monitor.use_err = GLOBUS_TRUE;
                g_monitor.err = globus_error_peek(result);
                
                if(transfer_info->guc_info->conc_outstanding == 0)
                {
                    globus_cond_signal(&g_monitor.cond);
                }
            }
            globus_mutex_unlock(&g_monitor.mutex);
        }
    }
    else if(expand)
    {
            globus_l_guc_gass_attr_init(
                &transfer_info->handle->source_gass_copy_attr,
                &transfer_info->handle->source_gass_attr,
                &transfer_info->handle->source_ftp_attr,
                transfer_info->guc_info,
                transfer_info->urls->src_url,
                GLOBUS_TRUE,
                GLOBUS_TRUE);
            if(transfer_info->guc_info->sync)
            {
                globus_l_guc_gass_attr_init(
                    &transfer_info->handle->dest_gass_copy_attr,
                    &transfer_info->handle->dest_gass_attr,
                    &transfer_info->handle->dest_ftp_attr,
                    transfer_info->guc_info,
                    transfer_info->urls->dst_url,
                    GLOBUS_FALSE,
                    GLOBUS_TRUE);
            }
             
            result = globus_l_guc_expand_single_url(transfer_info);
            if(result != GLOBUS_SUCCESS)
            {
                err = globus_error_peek(result);
            }
            
            result = GLOBUS_SUCCESS;
            globus_l_url_copy_monitor_callback(
                transfer_info, &transfer_info->handle->gass_copy_handle, err);
    }
    else
    {
        if(retry)
        {
            globus_reltime_t                        delay;
            GlobusTimeReltimeSet(delay, 2, 0);

            globus_callback_register_oneshot(
                NULL,
                &delay,
                globus_l_guc_transfer_kickout,
                transfer_info);
        }
        else
        {
            globus_l_guc_url_pair_free(transfer_info->urls);
            globus_fifo_destroy_all(
                &transfer_info->matched_url_list, globus_l_guc_url_info_free);
            globus_free(transfer_info);
        }
    }
    return;
}

/******************************************************************************
Function: main()
Description:
Parameters:
Returns:
******************************************************************************/
int
main(int argc, char **argv)
{
    globus_bool_t                           ret_val = GLOBUS_FALSE;
    int                                     err;
    globus_result_t                         result = GLOBUS_SUCCESS;
    globus_l_guc_info_t                     guc_info;
    int                                     i;
    globus_callback_handle_t                dumpfile_handle;

    LTDL_SET_PRELOADED_SYMBOLS();
    err = globus_module_activate(GLOBUS_GASS_COPY_MODULE);
    if( err != GLOBUS_SUCCESS )
    {
        globus_libc_fprintf(stderr, 
            _GASCSL("Error %d, activating gass copy module\n"),
            err);
        return 1;
    }
    err = globus_module_activate(GLOBUS_FTP_CLIENT_DEBUG_PLUGIN_MODULE);
    if( err != GLOBUS_SUCCESS )
    {
        globus_libc_fprintf(stderr, 
            _GASCSL("Error %d, activating ftp debug plugin module\n"),
            err);
        return 1;
    }
    err = globus_module_activate(GLOBUS_FTP_CLIENT_RESTART_PLUGIN_MODULE);
    if( err != GLOBUS_SUCCESS )
    {
        globus_libc_fprintf(stderr, 
            _GASCSL("Error %d, activating ftp restart plugin module\n"),
            err);
        return 1;
    }
    err = globus_module_activate(GLOBUS_GSI_GSSAPI_MODULE);
    if( err != GLOBUS_SUCCESS )
    {
        globus_libc_fprintf(stderr, 
            _GASCSL("Error %d, activating ftp restart plugin module\n"),
            err);
        return 1;
    }

    memset(&guc_info, '\0', sizeof(globus_l_guc_info_t));
    globus_fifo_init(&guc_info.user_url_list);
    globus_fifo_init(&guc_info.expanded_url_list);
    globus_fifo_init(&guc_info.dump_url_list);

    /* parse user parms */
    if(globus_l_guc_parse_arguments(
           argc,
           argv,
           &guc_info) != 0)
    {
        return 1;
    }
    
    /* crft interception */
    if(g_ext != NULL)
    {
        return globus_l_guc_ext(&guc_info);
    }
    
    guc_info.handles = (globus_l_guc_handle_t **) 
        globus_calloc(guc_info.conc, sizeof(globus_l_guc_handle_t *));
    for(i = 0; i < guc_info.conc; i++)
    {
        guc_info.handles[i] = (globus_l_guc_handle_t *) 
            globus_calloc(1, sizeof(globus_l_guc_handle_t));
        
        guc_info.handles[i]->guc_info = &guc_info;
        
        globus_gass_copy_attr_init(
            &guc_info.handles[i]->source_gass_copy_attr);
        globus_gass_copy_attr_init(
            &guc_info.handles[i]->dest_gass_copy_attr);
        guc_info.handles[i]->id = i;
        /* initialize gass copy handle */
        if(globus_l_guc_init_gass_copy_handle(
               &guc_info.handles[i]->gass_copy_handle, &guc_info, i) != 0)
        {
            fprintf(stderr, "%s", _GASCSL("Failed to initialize handle.\n"));
            return 1;
        }
        if(globus_l_guc_init_gass_copy_handle(
               &guc_info.handles[i]->cksm_gass_copy_handle, &guc_info, i) != 0)
        {
            fprintf(stderr, "%s", _GASCSL("Failed to initialize handle.\n"));
            return 1;
        }
        /* associate cksm and copy handles for checksum verification */
        if(guc_info.comp_checksum)
        {
            globus_gass_copy_set_checksum_algo(
                &guc_info.handles[i]->gass_copy_handle,
                guc_info.checksum_algo,
                &guc_info.handles[i]->cksm_gass_copy_handle);
        }
    }
    
    globus_mutex_init(&g_monitor.mutex, NULL);
    globus_cond_init(&g_monitor.cond, NULL);
    g_monitor.was_error = 0;
    
    if(guc_info.list_url != NULL)
    {
        globus_fifo_enqueue(&guc_info.expanded_url_list,
            strdup(guc_info.list_url));

        while(!globus_fifo_empty(&guc_info.expanded_url_list))
        {
            char *                      current_url;

            current_url = (char *)
                globus_fifo_dequeue(&guc_info.expanded_url_list);
            fprintf(stdout, "%s\n", current_url);
            globus_l_guc_gass_attr_init(
                &guc_info.handles[0]->source_gass_copy_attr,
                &guc_info.handles[0]->source_gass_attr,
                &guc_info.handles[0]->source_ftp_attr,
                &guc_info,
                current_url,
                GLOBUS_TRUE,
                GLOBUS_TRUE);

            result = globus_gass_copy_glob_expand_url(
                    &guc_info.handles[0]->gass_copy_handle,
                    current_url,
                    &guc_info.handles[0]->source_gass_copy_attr,
                    globus_l_guc_glob_list_cb,
                    &guc_info);
            free(current_url);
            fprintf(stdout, "\n");
        }
    }
    else
    {
        globus_reltime_t                delay;
        if(g_transfer_timeout > 0)
        {
            GlobusTimeReltimeSet(delay, g_transfer_timeout, 0);
            globus_callback_register_oneshot(
                NULL,
                &delay,
                globus_l_guc_interrupt_handler,
                NULL);
        }
        
        if(guc_info.dumpfile || guc_info.dump_only_file)
        {
            GlobusTimeReltimeSet(delay, 60, 0);
            globus_callback_register_periodic(
                &dumpfile_handle,
                NULL,
                &delay,
                globus_l_guc_dump_urls,
                &guc_info);
        }
        /* expand globbed urls */
        result = globus_l_guc_expand_urls(
                 &guc_info,
                 guc_info.handles[0]);
    }
    if(result != GLOBUS_SUCCESS)
    {
        g_err_msg = globus_error_print_friendly(
            globus_error_peek(result));
        fprintf(stderr, _GASCSL("\nerror: %s"), g_err_msg);
        globus_free(g_err_msg);        
        ret_val = 1;
    }
    
    if(guc_info.dumpfile || guc_info.dump_only_file)
    {
        globus_callback_unregister(dumpfile_handle, NULL, NULL, NULL);
        if(!globus_l_globus_url_copy_ctrlc)
        {
            globus_l_guc_dump_urls(&guc_info);
        }
    }

    if(globus_l_globus_url_copy_ctrlc && !g_monitor.transfer_timeout)
    {
#ifdef SIGINT
        raise(SIGINT);
#endif
    }
    
    /* make sure the timer doesn't go off after a cancel.  setting this
        to true makes the handler ignore everything if it hasn't fired yet */
    globus_mutex_lock(&g_monitor.mutex);
    {
        globus_l_globus_url_copy_ctrlc = GLOBUS_TRUE;
    }
    globus_mutex_unlock(&g_monitor.mutex);

    globus_l_guc_destroy_url_list(&guc_info.user_url_list);
    globus_l_guc_destroy_url_list(&guc_info.expanded_url_list);
    globus_l_guc_destroy_url_list(&guc_info.dump_url_list);

    if(guc_l_newline_exit && !globus_l_globus_url_copy_ctrlc_handled)
    {
        globus_libc_fprintf(stdout, "\n");
    }
    
    if(guc_info.udt)
    {
        /* sidestep udt shutdown issues */
        return ret_val;
    }
    for(i = 0; i < guc_info.conc; i++)
    {
        globus_gass_copy_handle_destroy(
            &guc_info.handles[i]->gass_copy_handle);
        globus_gass_copy_handle_destroy(
            &guc_info.handles[i]->cksm_gass_copy_handle);
        
        globus_free(guc_info.handles[i]);
    }
    globus_free(guc_info.handles);
    guc_info.handles = NULL;
    
    globus_mutex_destroy(&g_monitor.mutex);
    globus_cond_destroy(&g_monitor.cond);

    globus_l_guc_info_destroy(&guc_info);
    
    globus_module_deactivate(GLOBUS_GSI_GSSAPI_MODULE);
    /* XXX fix hang globus_module_deactivate_all(); */

    return ret_val;

} /* main() */



/******************************************************************************
Function: globus_l_url_copy_monitor_callback()
Description:
Parameters:
Returns:
******************************************************************************/

static 
void
globus_l_url_copy_monitor_callback(
    void *                              callback_arg,
    globus_gass_copy_handle_t *         handle,
    globus_object_t *                   error)
{    
    globus_l_guc_transfer_t *           transfer_info;
    
    transfer_info = (globus_l_guc_transfer_t *)  callback_arg;
    
    globus_mutex_lock(&g_monitor.mutex);
    {
        if(error != NULL)
        {
            if(!g_continue)
            {
                globus_l_guc_enqueue_pair(
                    &transfer_info->guc_info->user_url_list, 
                    transfer_info->urls);
                    
                transfer_info->urls = NULL;
    
                g_monitor.done = GLOBUS_TRUE;
                g_monitor.use_err = GLOBUS_TRUE;
                g_monitor.err = globus_object_copy(error);
            }
            else
            {  
                char *                  msg;
                g_monitor.was_error++;
                msg = globus_error_print_friendly(error);
                fprintf(stderr, _GASCSL("\ncontinuing on error: %s\n"), msg);
                globus_free(msg);
            }

        }
        
        transfer_info->guc_info->conc_outstanding--;
        transfer_info->handle->current_transfer = NULL;
        globus_l_guc_url_pair_free(transfer_info->urls);
        transfer_info->urls = NULL;
        
        if(transfer_info->handle->source_ftp_attr)
        {
            globus_ftp_client_operationattr_destroy(
                &transfer_info->handle->source_ftp_attr);
            transfer_info->handle->source_ftp_attr = NULL;
        }
        if(transfer_info->handle->dest_ftp_attr)
        {
            globus_ftp_client_operationattr_destroy(
                &transfer_info->handle->dest_ftp_attr);
            transfer_info->handle->dest_ftp_attr = NULL;
        }   
    }
    globus_mutex_unlock(&g_monitor.mutex);
    
    globus_callback_register_oneshot(
        NULL,
        NULL,
        globus_l_guc_transfer_kickout,
        transfer_info);
        
    return;
} /* globus_l_url_copy_monitor_callback() */

/******************************************************************************
Function: globus_l_gass_copy_performance_cb()
Description:
Parameters:
Returns:
******************************************************************************/
static
void
globus_l_gass_copy_performance_cb(
    void *                                          user_arg,
    globus_gass_copy_handle_t *                     handle,
    globus_off_t                                    total_bytes,
    float                                           instantaneous_throughput,
    float                                           avg_throughput)
{
    globus_libc_fprintf(stdout,
        " %12" GLOBUS_OFF_T_FORMAT 
        " bytes %12.2f MB/sec avg %12.2f MB/sec inst\r",
        total_bytes,
        avg_throughput / (1024 * 1024),
        instantaneous_throughput / (1024 * 1024));
    fflush(stdout);
    guc_l_newline_exit = GLOBUS_TRUE;
}

void
globus_guc_copy_performance_update(
    globus_off_t                                    total_bytes,
    float                                           instantaneous_throughput,
    float                                           avg_throughput)
{
    globus_libc_fprintf(stdout,
        " %12" GLOBUS_OFF_T_FORMAT
        " bytes %12.2f MB/sec avg %12.2f MB/sec inst\r",
        total_bytes,
        avg_throughput / (1024 * 1024),
        instantaneous_throughput / (1024 * 1024));
    fflush(stdout);
    guc_l_newline_exit = GLOBUS_TRUE;
}

void
globus_guc_transfer_update(
    const char *                        src_url,
    const char *                        dst_url,
    const char *                        src_fname,
    const char *                        dst_fname)
{

    if(src_url != NULL && dst_url != NULL)
    {
        globus_libc_fprintf(stdout, _GASCSL("Source: %s\nDest:   %s\n"),
            src_url,
            dst_url);
    }

    if(src_fname != NULL && dst_fname != NULL)
    {
        if(!strcmp(src_fname, dst_fname))
        {
            globus_libc_fprintf(stdout, "  %s\n", src_fname);
        }
        else
        {
            globus_libc_fprintf(stdout, "  %s  ->  %s\n",
                src_fname, dst_fname);
        }
    }
}


static
void
globus_l_guc_entry_cb(
    const char *                         url,
    const globus_gass_copy_glob_stat_t * info_stat,
    void *                               user_arg)
{
    globus_l_guc_info_t *               guc_info;
    char *                              tmp_unique;
    globus_bool_t                       seen = GLOBUS_FALSE;
    int                                 retval;
    globus_l_guc_transfer_t *           transfer_info;                      
    globus_l_guc_url_info_t *           urlinfo;
    
    transfer_info = (globus_l_guc_transfer_t *) user_arg;
    guc_info = transfer_info->guc_info;

    globus_mutex_lock(&g_monitor.mutex);
    
    if(guc_info->recurse && info_stat &&
        info_stat->type == GLOBUS_GASS_COPY_GLOB_ENTRY_DIR &&
        info_stat->unique_id && *info_stat->unique_id)
    {
        tmp_unique = globus_libc_strdup(info_stat->unique_id);
        retval = globus_hashtable_insert(
            &guc_info->recurse_hash,
            tmp_unique,
            tmp_unique);
            
        if(retval != 0)
        {
            globus_free(tmp_unique);
            seen = GLOBUS_TRUE;
        }
    }
    
    if(!seen)
    {
        urlinfo = (globus_l_guc_url_info_t *) 
            globus_calloc(sizeof(globus_l_guc_url_info_t), 1);
            
        urlinfo->url = globus_libc_strdup(url);
        urlinfo->mode = info_stat->mode;
        urlinfo->mdtm = info_stat->mdtm;
        urlinfo->size = info_stat->size;
        urlinfo->type = info_stat->type;

        globus_fifo_enqueue(
            &transfer_info->matched_url_list,
           urlinfo);
    }
    
    globus_mutex_unlock(&g_monitor.mutex);
    
    return;   
}    

static
void
globus_l_guc_dest_entry_cb(
    const char *                         url,
    const globus_gass_copy_glob_stat_t * info_stat,
    void *                               user_arg)
{
    globus_l_guc_info_t *               guc_info;
    int                                 retval;
    globus_l_guc_transfer_t *           transfer_info;                      
    globus_l_guc_url_info_t *           urlinfo;
    
    transfer_info = (globus_l_guc_transfer_t *) user_arg;
    guc_info = transfer_info->guc_info;

    globus_mutex_lock(&g_monitor.mutex);
            
    if(info_stat &&
        info_stat->type != GLOBUS_GASS_COPY_GLOB_ENTRY_DIR)
    {
        urlinfo = (globus_l_guc_url_info_t *) 
            globus_calloc(sizeof(globus_l_guc_url_info_t), 1);
            
        urlinfo->url = globus_libc_strdup(url);
        urlinfo->mode = info_stat->mode;
        urlinfo->mdtm = info_stat->mdtm;
        urlinfo->size = info_stat->size;
        urlinfo->type = info_stat->type;
        
        retval = globus_hashtable_insert(
            &guc_info->dest_hash,
            urlinfo->url,
            urlinfo);
            
        if(retval != 0)
        {
            globus_free(urlinfo->url);
            globus_free(urlinfo);
        }
    }
        
    globus_mutex_unlock(&g_monitor.mutex);
    
    return;   
}    

static 
void
globus_l_guc_info_destroy(
    globus_l_guc_info_t *                    guc_info)
{
    OM_uint32                           min_stat;
    
    if(guc_info->source_subject)
    {
        globus_free(guc_info->source_subject);
    }
    if(guc_info->dest_subject)
    {
        globus_free(guc_info->dest_subject);
    }
    if(guc_info->src_net_stack_str)
    {
        globus_free(guc_info->src_net_stack_str);
    }
    if(guc_info->src_disk_stack_str)
    {
        globus_free(guc_info->src_disk_stack_str);
    }
    if(guc_info->dst_net_stack_str)
    {
        globus_free(guc_info->dst_net_stack_str);
    }
    if(guc_info->dst_disk_stack_str)
    {
        globus_free(guc_info->dst_disk_stack_str);
    }
    if(guc_info->src_cred != GSS_C_NO_CREDENTIAL)
    {
        gss_release_cred(&min_stat, &guc_info->src_cred);
    }
    if(guc_info->dst_cred != GSS_C_NO_CREDENTIAL)
    {
        gss_release_cred(&min_stat, &guc_info->dst_cred);
    }
    if(guc_info->data_cred != GSS_C_NO_CREDENTIAL)
    {
        gss_release_cred(&min_stat, &guc_info->data_cred);
    }
    if(guc_info->src_cred_subj)
    {
        globus_free(guc_info->src_cred_subj);
    }
    if(guc_info->dst_cred_subj)
    {
        globus_free(guc_info->dst_cred_subj);
    }
    if(guc_info->data_cred_subj)
    {
        globus_free(guc_info->data_cred_subj);
    }
    if(guc_info->alias_file)
    {
        globus_free(guc_info->alias_file);
    }
    if(guc_info->dumpfile)
    {
        globus_free(guc_info->dumpfile);
    }
    if(guc_info->dump_only_file)
    {
        globus_free(guc_info->dump_only_file);
    }
    if(guc_info->checksum_algo)
    {
        globus_free(guc_info->checksum_algo);
    }

    /* destroy the list */
}


static
int
globus_l_guc_parse_file(
    globus_l_guc_info_t *                           guc_info,
    char *                                          file_name, 
    globus_fifo_t *                                 user_url_list)
{
    FILE *                                          fptr;
    char                                            line[1024];
    char                                            src_url[512];
    char                                            dst_url[512];
    globus_l_guc_src_dst_pair_t *                   ent;
    char *                                          p;
    int                                             url_len;
    int                                             line_num = 0;
    int                                             rc;
    globus_bool_t                                   stdin_used;
    globus_off_t                                    offset;
    globus_off_t                                    length;

    stdin_used = (strcmp(file_name, "-") == 0);
    fptr = (stdin_used) ? stdin : fopen(file_name, "r");
    if(fptr == NULL)
    {
        return -1;
    }

    while(fgets(line, sizeof(line), fptr) != NULL)
    {
        line_num++;
        p = line;
        url_len = 0;
        offset = guc_info->partial_offset;
        length = guc_info->partial_length;
                
        while(*p && isspace(*p))
        {
            p++;
        }
        if(*p == '\0')
        {
            continue;
        }

        if(*p == '#')
        {
            continue;
        }
        
        if(*p == '"')
        {
            rc = sscanf(p, "\"%[^\"]\"", src_url);
            url_len = 2;
        }
        else
        {
            rc = sscanf(p, "%s", src_url);
        } 
        
        if(rc != 1)
        {   
            goto error_parse;
        }
        
        url_len += strlen(src_url);
        p = p + url_len;
       
        url_len = 0;
        while(*p && isspace(*p))
        {
            p++;
        }

        if(*p == '"')
        {
            rc = sscanf(p, "\"%[^\"]\"", dst_url);
            url_len = 2;
        }
        else
        {
            rc = sscanf(p, "%s", dst_url);
        }        

        if(rc != 1)
        {   
            goto error_parse;
        }
        
        url_len += strlen(dst_url);
        p = p + url_len;
        
        while(*p && isspace(*p))
        {
            p++;
        }
        if(*p)
        {
            rc = sscanf(p, "%"GLOBUS_OFF_T_FORMAT",%"GLOBUS_OFF_T_FORMAT,
                 &offset, &length);
            if(rc < 1 || offset < -1 || length < -1)
            {   
                goto error_parse;
            }
        }

        if(strcmp(src_url, "-") == 0 && strcmp(dst_url, "-") == 0)
        {
            fprintf(stderr, "%s", _GASCSL("stdin and stdout cannot be used together.\n"));
            goto error_parse;
        }
        if(strcmp(src_url, "-") == 0)
        {
            if(stdin_used)
            {
                fprintf(stderr, "%s", _GASCSL("stdin can only be used once.\n"));
                goto error_parse;
            }
            stdin_used = GLOBUS_TRUE;
        }

        ent = (globus_l_guc_src_dst_pair_t *)
                globus_malloc(sizeof(globus_l_guc_src_dst_pair_t));
        ent->src_url = globus_libc_strdup(src_url);
        ent->dst_url = globus_libc_strdup(dst_url);
        ent->offset = offset;
        ent->length = length;
        ent->src_info = NULL;
        globus_fifo_enqueue(user_url_list, ent);
    }

    if(fptr != stdin)
    {
        fclose(fptr);
    }

    return 0;
    
error_parse:
    if(fptr != stdin)
    {
        fclose(fptr);
    }
    fprintf(stderr, _GASCSL("Problem parsing url list: line %d\n"), line_num);
    return -2;
 
}

static
int
globus_l_guc_load_alias_file(
    char *                                          file_name)
{
    FILE *                                          fptr;
    char                                            line[1024];
    char                                            buf[256];
    globus_bool_t                                   newalias = GLOBUS_TRUE;
    int                                             line_num = 0;
    int                                             rc;
    char *                                          p;
    globus_l_guc_alias_t *                          alias_ent = NULL;

    globus_hashtable_init(
        &guc_l_alias_table,
        256,
        globus_hashtable_string_hash,
        globus_hashtable_string_keyeq);
    if(!file_name)
    {
        return -1;
    }
    fptr = fopen(file_name, "r");
    if(fptr == NULL)
    {
        return -1;
    }

    while(fgets(line, sizeof(line), fptr) != NULL)
    {
        line_num++;
        p = line;
                
        while(*p && isspace(*p))
        {
            p++;
        }
        if(*p == '\0' || *p == '#')
        {
            continue;
        }
        
        if(*p == '@')
        {
            newalias = GLOBUS_TRUE;
            rc = sscanf(p, "@%s", buf);
            if(rc != 1)
            {   
                goto error_parse;
            }
        }
        else
        {
            if(!alias_ent)
            {   
                goto error_parse;
            }
            rc = sscanf(p, "%s", buf);
            if(rc != 1)
            {   
                goto error_parse;
            }
        }           

        if(newalias)
        {
            alias_ent = (globus_l_guc_alias_t *)
                globus_malloc(sizeof(globus_l_guc_alias_t) + sizeof(char *) * 50);
            alias_ent->name = globus_libc_strdup(buf);
            alias_ent->entries = 0;
            alias_ent->index = 0;
            rc = globus_hashtable_insert(
                &guc_l_alias_table,
                alias_ent->name,
                (void *) alias_ent);
            if(rc != 0)
            {
                goto error_parse;
            }            
            newalias = GLOBUS_FALSE;
        }
        else
        {
            alias_ent->hostname[alias_ent->entries] = globus_libc_strdup(buf);
            alias_ent->entries++;
        }
    }

    fclose(fptr);
    
    guc_l_aliases = GLOBUS_TRUE;
    guc_l_src_alias_ent = (globus_l_guc_alias_t *) 
        globus_hashtable_lookup(&guc_l_alias_table, (void *) "source");
    guc_l_dst_alias_ent = (globus_l_guc_alias_t *) 
        globus_hashtable_lookup(&guc_l_alias_table, (void *) "destination");

    return 0;
    
error_parse:
    fclose(fptr);
    fprintf(stderr, 
        "Problem parsing alias file %s: line %d\n", file_name, line_num);
    return -2;
 
}


static
globus_result_t
globus_l_guc_transfer(
    globus_l_guc_transfer_t *                    transfer_info)
{
    globus_io_handle_t *                         source_io_handle = GLOBUS_NULL;
    globus_io_handle_t *                         dest_io_handle = GLOBUS_NULL;
    char *                                       src_filename;
    char *                                       dst_filename;
    char *                                       src_url_base = GLOBUS_NULL;
    char *                                       dst_url_base = GLOBUS_NULL;
    int                                          src_url_base_len = 0;
    int                                          dst_url_base_len = 0;
    globus_result_t                              result;
    globus_bool_t                                new_url;
    globus_bool_t                                dst_is_dir;
    globus_l_guc_info_t *                        guc_info;
    globus_l_guc_handle_t *                      handle;
    char *                                       src_url;
    char *                                       dst_url;
    globus_object_t *                            err = NULL;

    src_url = transfer_info->urls->src_url;
    dst_url = transfer_info->urls->dst_url;
    guc_info = transfer_info->guc_info;
    handle = transfer_info->handle;

    /* when creating the list the urls are check for validity */
    source_io_handle = globus_l_guc_get_io_handle(src_url, fileno(stdin));
    dest_io_handle = globus_l_guc_get_io_handle(dst_url, fileno(stdout));

    if(guc_info->comp_checksum) 
    {
        /*
         * Associate the source checksum with the transfer handle
         * so that the destination checksum can be verified after
         * the successful transfer.
         */   
        globus_gass_copy_set_checksum(&handle->gass_copy_handle, 
                                      transfer_info->urls->src_info->checksum);
    }

    /*
     *  we must setup attrs for every gass url.  if url is not
     *  gass handled the function will just return
     */

    if(source_io_handle == NULL)
    {
        globus_l_guc_gass_attr_init(
            &handle->source_gass_copy_attr,
            &handle->source_gass_attr,
            &handle->source_ftp_attr,
            guc_info,
            src_url,
            GLOBUS_TRUE,
            GLOBUS_FALSE);
    } 
    if(dest_io_handle == NULL)
    {
        globus_l_guc_gass_attr_init(
            &handle->dest_gass_copy_attr,
            &handle->dest_gass_attr,
            &handle->dest_ftp_attr,
            guc_info,
            dst_url,
            GLOBUS_FALSE,
            GLOBUS_FALSE);
    }

    /* setting offsets should have been an attr option in gass_copy but 
      since we only run one operation per handle this will be ok. */
    globus_gass_copy_set_partial_offsets(
        &handle->gass_copy_handle, 
        transfer_info->urls->offset,
        (transfer_info->urls->length == -1) ? 
            -1 : transfer_info->urls->offset + transfer_info->urls->length);

    if(source_io_handle)
    {
        result = globus_gass_copy_register_handle_to_url(
                     &handle->gass_copy_handle,
                     source_io_handle,
                     dst_url,
                     &handle->dest_gass_copy_attr,
                     globus_l_url_copy_monitor_callback,
                     transfer_info);
    }
    else if(dest_io_handle)
    {
        result = globus_gass_copy_register_url_to_handle(
                     &handle->gass_copy_handle,
                     src_url,
                     &handle->source_gass_copy_attr,
                     dest_io_handle,
                     globus_l_url_copy_monitor_callback,
                     transfer_info);
    }
    else
    {
        if(dst_url[strlen(dst_url) - 1] == '/')
        {
            dst_is_dir = GLOBUS_TRUE;   
        }
        else
        {
            dst_is_dir = GLOBUS_FALSE;
        }   
    
        if(!g_quiet_flag)
        {
            if(dst_is_dir)
            {
                src_filename = strrchr(src_url, '/');               
                while(src_filename > src_url && *src_filename == '/')
                {
                    src_filename--;
                }
                while(src_filename > src_url && *src_filename != '/')
                {
                    src_filename--;
                }
                src_filename++;
                
                dst_filename = strrchr(dst_url, '/');               
                while(dst_filename > dst_url && *dst_filename == '/')
                {
                    dst_filename--;
                }
                while(dst_filename > dst_url && *dst_filename != '/')
                {
                    dst_filename--;
                }
                dst_filename++;
            }
            else
            {
                src_filename = strrchr(src_url, '/');
                if(!src_filename++)
                {
                    src_filename = src_url;
                }
                
                dst_filename = strrchr(dst_url, '/');
                if(!dst_filename++)
                {
                    dst_filename = dst_url;
                }
            }
    
            
            if(src_url_base == GLOBUS_NULL || 
                src_filename - src_url != src_url_base_len ||
                strncmp(src_url, src_url_base, src_filename - src_url))
            {
                if(src_url_base != GLOBUS_NULL)
                {
                    globus_free(src_url_base);
                }
                src_url_base = globus_libc_strdup(src_url);
    
                src_url_base_len = src_filename - src_url;
                src_url_base[src_url_base_len] = '\0';
                new_url = GLOBUS_TRUE;
            }
            else
            {
                new_url = GLOBUS_FALSE;
            }
            
            if(dst_url_base == GLOBUS_NULL || 
                dst_filename - dst_url != dst_url_base_len ||
                strncmp(dst_url, dst_url_base, dst_filename - dst_url))
            {
                if(dst_url_base != GLOBUS_NULL)
                {
                    globus_free(dst_url_base);
                }
                dst_url_base = globus_libc_strdup(dst_url);
    
                dst_url_base_len = dst_filename - dst_url;
                dst_url_base[dst_url_base_len] = '\0';
                new_url = GLOBUS_TRUE;
            }
    
            if(new_url)
            {
                globus_guc_transfer_update(
                    src_url_base, dst_url_base, NULL, NULL);
            }
            globus_guc_transfer_update(
                NULL, NULL, src_filename, dst_filename);
        }
        
        if(dst_is_dir)
        {
            if(guc_info->dump_only_file)
            {
                if(!guc_info->dump_only_fp)
                {
                    globus_l_guc_src_dst_pair_t *   url_pair;
                    url_pair = (globus_l_guc_src_dst_pair_t *)
                            globus_malloc(sizeof(globus_l_guc_src_dst_pair_t));
                
                    url_pair->src_url = globus_libc_strdup(transfer_info->urls->src_url);
                    url_pair->dst_url = globus_libc_strdup(transfer_info->urls->dst_url);
                    url_pair->offset = transfer_info->urls->offset;
                    url_pair->length = transfer_info->urls->length;
                    url_pair->src_info = transfer_info->urls->src_info;
                    transfer_info->urls->src_info = NULL;
                    
                    globus_l_guc_enqueue_pair(
                        &guc_info->dump_url_list, 
                        url_pair);
                }
                result = GLOBUS_SUCCESS;
            }
            else
            {
                result = globus_l_guc_create_dir(
                    dst_url, handle, guc_info);
                if(result != GLOBUS_SUCCESS)
                {
                    result = GLOBUS_SUCCESS;
                }
            }
            globus_ftp_client_operationattr_destroy(&handle->source_ftp_attr);
            globus_l_guc_gass_attr_init(
                &handle->source_gass_copy_attr,
                &handle->source_gass_attr,
                &handle->source_ftp_attr,
                guc_info,
                src_url,
                GLOBUS_TRUE,
                GLOBUS_TRUE);
            if(guc_info->sync)
            {
                globus_ftp_client_operationattr_destroy(&handle->dest_ftp_attr);
                globus_l_guc_gass_attr_init(
                    &handle->dest_gass_copy_attr,
                    &handle->dest_gass_attr,
                    &handle->dest_ftp_attr,
                    guc_info,
                    dst_url,
                    GLOBUS_FALSE,
                    GLOBUS_TRUE);
            }
                
            result = globus_l_guc_expand_single_url(transfer_info);
            if(result != GLOBUS_SUCCESS)
            {   
                err = globus_error_peek(result);
            }
            result = GLOBUS_SUCCESS;
            globus_l_url_copy_monitor_callback(
                transfer_info, &handle->gass_copy_handle, err);
        }
        else
        {
            if(!guc_info->dump_only_file)
            {
                result = globus_gass_copy_register_url_to_url(
                     &handle->gass_copy_handle,
                     src_url,
                     &handle->source_gass_copy_attr,
                     dst_url,
                     &handle->dest_gass_copy_attr,
                     globus_l_url_copy_monitor_callback,
                     transfer_info);
            }
            else
            {
                if(!guc_info->dump_only_fp)
                {
                    globus_l_guc_src_dst_pair_t *   url_pair;
                    url_pair = (globus_l_guc_src_dst_pair_t *)
                            globus_malloc(sizeof(globus_l_guc_src_dst_pair_t));
                
                    url_pair->src_url = globus_libc_strdup(transfer_info->urls->src_url);
                    url_pair->dst_url = globus_libc_strdup(transfer_info->urls->dst_url);
                    url_pair->offset = transfer_info->urls->offset;
                    url_pair->length = transfer_info->urls->length;
                    url_pair->src_info = transfer_info->urls->src_info;
                    transfer_info->urls->src_info = NULL;                    
                    
                    globus_l_guc_enqueue_pair(
                        &guc_info->dump_url_list, 
                        url_pair);
                }
                result = GLOBUS_SUCCESS;
    
                globus_l_url_copy_monitor_callback(
                    transfer_info, &handle->gass_copy_handle, NULL);               
            }
        }
    }
    if(result != GLOBUS_SUCCESS)
    {
        err = globus_error_peek(result);
        globus_l_url_copy_monitor_callback(
            transfer_info, &handle->gass_copy_handle, err);
    }

    if(g_verbose_flag)
    {
        printf("\n");
    }

    if(src_url_base)
    {
        globus_free(src_url_base);
    }
    if(dst_url_base)
    {
        globus_free(dst_url_base);
    }
    
    return result;  

}
    
static
globus_result_t
globus_l_guc_transfer_files(
    globus_l_guc_info_t *                        guc_info)
{
    globus_io_handle_t *                         source_io_handle = GLOBUS_NULL;
    globus_io_handle_t *                         dest_io_handle = GLOBUS_NULL;
    globus_result_t                              result = GLOBUS_SUCCESS;
    globus_bool_t                                was_error = GLOBUS_FALSE;
    int                                          i;
    globus_l_guc_transfer_t *                    transfer_info;

    globus_callback_register_signal_handler(
        GLOBUS_SIGNAL_INTERRUPT,
        GLOBUS_FALSE,
        globus_l_guc_interrupt_handler,
        &g_monitor);

    g_monitor.done = GLOBUS_FALSE;
    g_monitor.use_err = GLOBUS_FALSE;
    guc_info->cancelled = GLOBUS_FALSE;
    guc_info->conc_outstanding = 0;
    
    for(i = 0; i < guc_info->conc; i++)
    {
        transfer_info = (globus_l_guc_transfer_t *)
            globus_malloc(sizeof(globus_l_guc_transfer_t));

        transfer_info->urls = NULL;
        transfer_info->handle = guc_info->handles[i];
        transfer_info->guc_info = guc_info;
        transfer_info->needs_mkdir = GLOBUS_FALSE;
        globus_fifo_init(&transfer_info->matched_url_list);
        
        globus_l_guc_transfer_kickout(transfer_info);
    }
        
    globus_mutex_lock(&g_monitor.mutex);

    while(!g_monitor.done || guc_info->conc_outstanding > 0)
    {
        globus_cond_wait(&g_monitor.cond, &g_monitor.mutex);

        if(globus_l_globus_url_copy_ctrlc &&
           !globus_l_globus_url_copy_ctrlc_handled)
        {
            fprintf(stderr, "%s", _GASCSL("\nCancelling copy...\n"));
            guc_info->cancelled = GLOBUS_TRUE;
            
            if(guc_info->dumpfile || guc_info->dump_only_file)
            {
                globus_l_guc_dump_urls(guc_info);
            }

            for(i = 0; i < guc_info->conc; i++)
            {        
                globus_gass_copy_cancel(
                    &guc_info->handles[i]->gass_copy_handle, 
                    GLOBUS_NULL, 
                    GLOBUS_NULL);
            }
            globus_l_globus_url_copy_ctrlc_handled = GLOBUS_TRUE;
            
            globus_callback_unregister_signal_handler(
                GLOBUS_SIGNAL_INTERRUPT, GLOBUS_NULL, GLOBUS_NULL);
        }
    }
    globus_mutex_unlock(&g_monitor.mutex);
	

    if(g_monitor.use_err)
    {
        result = globus_error_put(g_monitor.err);
        was_error = GLOBUS_TRUE;
        if(!g_continue || globus_l_globus_url_copy_ctrlc_handled)
        {
            goto error_transfer;
        }
        else
        {
            g_monitor.done = GLOBUS_TRUE;
            g_err_msg = globus_error_print_friendly(
                globus_error_peek(result));
            fprintf(stderr, _GASCSL("\nerror transferring:\n%s"),
                 g_err_msg);
            globus_free(g_err_msg);
        }
    }
    else if(globus_l_globus_url_copy_ctrlc_handled)
    {
        goto error_transfer;
    }
    
    if(!globus_l_globus_url_copy_ctrlc_handled)
    {
        globus_callback_unregister_signal_handler(
            GLOBUS_SIGNAL_INTERRUPT, GLOBUS_NULL, GLOBUS_NULL);
    }

    if(was_error)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_NULL,
                GLOBUS_NULL,
                _GASCSL("There was an error with one or more transfers.\n")));
    }

    if(g_monitor.was_error)
    {
        if(g_monitor.was_error > 1)
        {
            result = globus_error_put(
                globus_error_construct_string(
                    GLOBUS_NULL,
                    GLOBUS_NULL,
                    _GASCSL("There were errors with %d transfers.\n"), 
                    g_monitor.was_error));
        }
        else
        {
            result = globus_error_put(
                globus_error_construct_string(
                    GLOBUS_NULL,
                    GLOBUS_NULL,
                    _GASCSL("There was an error with 1 transfer.\n")));
        }
            
    }        
    return result;

error_transfer:
    if(source_io_handle)
    {
        globus_free(source_io_handle);
    }

    if(dest_io_handle)
    {
        globus_free(dest_io_handle);
    }
        
    if(!globus_l_globus_url_copy_ctrlc_handled)
    {
        globus_callback_unregister_signal_handler(
            GLOBUS_SIGNAL_INTERRUPT, GLOBUS_NULL, GLOBUS_NULL);
    }
    
    return result;
}

static
globus_result_t
globus_l_guc_file_to_string(
    char *                                          filename,
    char **                                         str)
{
    FILE *                                          fp;
    int                                             numbytes;
    globus_result_t                                 result = GLOBUS_SUCCESS;

    fp = fopen(filename, "r");
    if(fp == NULL)
    {
        goto error_open;
    }
    fseek(fp, 0L, SEEK_END);
    numbytes = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    *str = (char*)calloc(numbytes+1, sizeof(char));
    if(*str == NULL)
    {
        goto error_memory;
    }
    fread(*str, sizeof(char), numbytes, fp);
    fclose(fp);
    return result;

error_memory:
    result = globus_error_put(
	globus_error_construct_string(
	    GLOBUS_NULL,
	    GLOBUS_NULL,
	    _GASCSL("Could not open file: %s\n"),
            filename));      
    fclose(fp);
error_open:
    if(result == GLOBUS_SUCCESS)
    { 
	result = globus_error_put(
	    globus_error_construct_string(
		GLOBUS_NULL,
		GLOBUS_NULL,
		_GASCSL("Could not allocate memory.\n")));      
    }
    return result;
} 

#if 0
static
int
guc_l_gmc_create_dir(
    globus_l_guc_info_t *               guc_info)
{
    int                                 i;
    globus_gass_copy_handle_t           gass_copy_handle;
    globus_gass_copy_handleattr_t       gass_copy_handleattr;
    globus_ftp_client_handleattr_t      ftp_handleattr;
    char *                              dst_url;
    globus_url_t                        url_info;
    char *                              tmp_ptr;
    globus_result_t                     result;
    globus_ftp_client_operationattr_t   ftp_op_attr;
    globus_gass_copy_attr_t             gass_copy_attr;
    char *                              sbj = NULL;

    globus_gass_copy_handleattr_init(&gass_copy_handleattr);
    globus_ftp_client_operationattr_init(&ftp_op_attr);
    result = globus_ftp_client_handleattr_init(&ftp_handleattr);
    if(result != GLOBUS_SUCCESS)
    {
        fprintf(stderr, _GASCSL("Error: Unable to init ftp handle attr %s\n"),
            globus_error_print_friendly(globus_error_peek(result)));

        return -1;
    }

    if(guc_info->rfc1738)
    {
        result = globus_ftp_client_handleattr_set_rfc1738_url(
            &ftp_handleattr, GLOBUS_TRUE);
        if(result != GLOBUS_SUCCESS)
        {
            fprintf(stderr, _GASCSL("Error: Unable to set rfc1738 support %s\n"),
                globus_error_print_friendly(globus_error_peek(result)));

            return -1;
        }
    }
    globus_gass_copy_handleattr_set_ftp_attr(
        &gass_copy_handleattr, &ftp_handleattr);
    globus_gass_copy_handle_init(&gass_copy_handle, &gass_copy_handleattr);
    globus_gass_copy_attr_init(&gass_copy_attr);
    for(i = 0; i < globus_fifo_size(&guc_mc_url_q); i++)
    {
        dst_url = globus_fifo_dequeue(&guc_mc_url_q);
        globus_fifo_enqueue(&guc_mc_url_q, dst_url);

        if(strstr(dst_url, "local_write=n") != NULL)
        {
            dst_url = globus_libc_strdup(dst_url);

            sbj = guc_info->dest_subject;
            tmp_ptr = strstr(dst_url, "?");
            if(tmp_ptr != NULL)
            {
                *tmp_ptr = '\0';

                tmp_ptr++;
                tmp_ptr = strstr(tmp_ptr, "subject=");
                if(tmp_ptr != NULL)
                {
                    tmp_ptr += sizeof("subject=");
                    sbj = tmp_ptr;
                    tmp_ptr = strstr(sbj, ";");
                    if(tmp_ptr != NULL)
                    {
                        *tmp_ptr = '\0';
                    }
                }
            }
            globus_url_parse(dst_url, &url_info);

            if(sbj || url_info.user || url_info.password)
            {
                globus_ftp_client_operationattr_set_authorization(
                    &ftp_op_attr,
                    GSS_C_NO_CREDENTIAL,
                    url_info.user,
                    url_info.password,
                    NULL,
                    sbj);
            }
            globus_gass_copy_attr_set_ftp(&gass_copy_attr, &ftp_op_attr);

            result = globus_gass_copy_mkdir(
                &gass_copy_handle,
                dst_url,
                &gass_copy_attr);
            if(result != GLOBUS_SUCCESS)
            {
                /* XXX log the error */
            }
        }
    }

    globus_gass_copy_handleattr_destroy(&gass_copy_handleattr);
    globus_ftp_client_handleattr_destroy(&ftp_handleattr);

    return 0;
}
#endif

static
char *
guc_build_mc_str(
    char *                              fname,
    char **                             out_first,
    char *                              sbj)
{
    char *                              qm_add = "?";
    int                                 count = 0;
    char *                              ptr;
    char *                              url_str;
    char *                              url_opts;
    char *                              url_tack_on;
    char *                              tmp_url_str;
    FILE *                              fptr;
    char                                url_line[512];
    globus_url_t                        url_info;
    int                                 rc;
    char **                             l_out_first = out_first;

    fptr = fopen(fname, "r");
    if(fptr == NULL)
    {
        /* XXX log error? */
        return NULL;
    }

    url_tack_on = "urls=";
    url_str = strdup("");
    ptr = fgets(url_line, 512, fptr);
    while(ptr != NULL)
    {
        ptr = strchr(url_line, '\n');
        if(ptr != NULL)
        {
            *ptr = '\0';
        }
        if(url_line[0] == '\0')
        {
            continue;
        }
        ptr = strchr(url_line, '?');
        if(ptr != NULL)
        {
            *ptr = '\0';
        }
        rc = globus_url_parse(url_line, &url_info);
        if(rc != 0)
        {
            goto error;
        }
        /* put the question mark back if there was one */

        if(ptr != NULL)
        {
            url_opts = strdup(ptr+1);
            qm_add = ";";
        }
        else
        {
            url_opts = strdup("");
            qm_add = "";
        }
        globus_fifo_enqueue(&guc_mc_url_q, globus_libc_strdup(url_line));

        /* subject test.  if no subject is there, but there is one
            specified in the cmd args, add it */
        ptr = strstr(url_opts, "subject=");
        if(ptr == NULL && sbj != NULL)
        {
            ptr = globus_common_create_string("%s%ssubject=%s",
                url_opts, qm_add, sbj);
            globus_free(url_opts);
            url_opts = ptr;
        }

        if(l_out_first != NULL)
        {
            *l_out_first = strdup(url_line);
            l_out_first = NULL;

            tmp_url_str = globus_common_create_string("%s%s",
                url_str, url_opts);
            globus_free(url_str);
            url_str = tmp_url_str;
            url_tack_on = ";urls=";
        }
        else
        {
            char * tmp_enc;
            char * enc;

            tmp_enc = globus_common_create_string("%s?%s",
                url_line, url_opts);

            enc = globus_url_string_hex_encode(tmp_enc, GUC_URL_ENC_CHAR);
            globus_free(tmp_enc);

            tmp_url_str = globus_common_create_string(
                "%s%s#%s", url_str, url_tack_on, enc);
            globus_free(enc);
            globus_free(url_str);
            url_str = tmp_url_str;

            url_tack_on = "";
        }
        globus_free(url_opts);
        count++;

        memset(url_line, 0, sizeof(url_line));
        ptr = fgets(url_line, 512, fptr);
    }

    if(count == 0)
    {
        goto error;
    }
    fclose(fptr);

    tmp_url_str = globus_url_string_hex_encode(url_str, GUC_URL_ENC_CHAR);
    free(url_str);
    url_str = globus_common_create_string("gridftp_multicast:%s", tmp_url_str);
    free(tmp_url_str);

    return url_str;

error:
    fclose(fptr);
    free(url_str);

    return NULL;
}

static char *
guc_l_convert_file_url(
    char *                              in_url)
{
    char *                              tmp_ptr;
    char *                              tmp_path;
    globus_result_t                     result;
    char *                              dir_ptr = "";


    /* do we already have a url or - */
    tmp_ptr = strstr(in_url, ":/");
    if(tmp_ptr != NULL || strcmp(in_url, "-") == 0)
    {
        return globus_libc_strdup(in_url);
    }

    result = GLOBUS_GSI_SYSCONFIG_MAKE_ABSOLUTE_PATH_FOR_FILENAME(in_url, &tmp_path);
    if(result != GLOBUS_SUCCESS)
    {
        /* just punt if the system call fails */
        return globus_libc_strdup(in_url);
    }
    dir_ptr = globus_url_string_hex_encode(tmp_path, "");
    
    /* In windows, absolute paths don't begin with / */
    tmp_ptr = globus_common_create_string("file://%s%s",
        dir_ptr[0] == '/' ? "" : "/", dir_ptr);
    
    globus_free(dir_ptr);
    globus_free(tmp_path);
    
    return tmp_ptr;
}

static
char *
guc_l_pipe_to_stack_str(
    char *                              in_str)
{
    globus_list_t *                     list;
    globus_list_t *                     r_list = NULL;
    char *                              pipe_str;
    char *                              del_choices = "#$^*!|%&()'{}";
    char *                              del;
    char *                              word;
    char *                              tmp_s;
    globus_bool_t                       found;

    del = del_choices;
    found = GLOBUS_FALSE;
    while(!found)
    {
        if(strchr(in_str, *del) == NULL)
        {
            found = GLOBUS_TRUE;
        }
        else
        {
            del++;
            if(del == '\0')
            {
                fprintf(stderr, "The pipe string most contain at least one of the following characters: %s", del_choices);
                return NULL;
            }
        }
    }
    list = globus_list_from_string(in_str, ' ', " \t\r\n");

    /* need to reverse the list */
    while(!globus_list_empty(list))
    {
        word = (char *) globus_list_remove(&list, list);
        if(*word != '\0')
        {
            globus_list_insert(&r_list, word);
        }
    }

    if(globus_list_size(r_list) <= 0)
    {
        return NULL;
    }

    pipe_str = globus_common_create_string("popen:argv=");
    while(!globus_list_empty(r_list))
    {
        word = (char *) globus_list_remove(&r_list, r_list);
        tmp_s = globus_common_create_string("%s%c%s", pipe_str, *del, word);
        free(pipe_str);
        pipe_str = tmp_s;
    }
    return pipe_str;
}

static
globus_result_t
globus_l_guc_load_cred(
    char *                              path,
    gss_cred_id_t *                     out_cred,
    char **                             out_subject)
{
    OM_uint32                           maj_stat;
    OM_uint32                           min_stat;
    gss_cred_id_t                       cred;
    gss_buffer_desc                     buf;
    gss_name_t                          name;
    globus_result_t                     result = GLOBUS_SUCCESS;

    if(path)
    {
        if(strcasecmp(path, "auto") == 0)
        {
            guc_gencert(&buf);
            auto_data.length = buf.length;
            auto_data.value = buf.value;
            return result;       
        }
        else
        {            
            buf.value = globus_common_create_string("X509_USER_PROXY=%s", path);
            buf.length = strlen(buf.value);
        
            maj_stat = gss_import_cred(
                &min_stat,
                &cred,
                GSS_C_NO_OID,
                1, /* GSS_IMPEXP_MECH_SPECIFIC */
                &buf,
                0,
                NULL);
            if(maj_stat != GSS_S_COMPLETE)
            {
                goto error;
            }
        
            globus_free(buf.value);
        }
    }
    else
    {
        maj_stat = gss_acquire_cred(
            &min_stat,
            GSS_C_NO_NAME,
            0,
            GSS_C_NULL_OID_SET,
            GSS_C_ACCEPT,
            &cred,
            NULL,
            NULL);
        if(maj_stat != GSS_S_COMPLETE)
        {
            goto error;
        }
    }

    if(out_subject)
    {
        maj_stat = gss_inquire_cred(
            &min_stat, cred, &name, NULL, NULL, NULL);
        if(maj_stat != GSS_S_COMPLETE)
        {
            goto error;
        }
    
        maj_stat = gss_display_name(&min_stat, name, &buf, NULL);
        if(maj_stat != GSS_S_COMPLETE)
        {
            goto error;
        }
    
        *out_subject = buf.value;
        
        gss_release_name(&min_stat, &name);
    }
    
    if(out_cred)
    {
        *out_cred = cred;
    }
    else
    {
        gss_release_cred(&min_stat, &cred);
    }

    return result;
    
error:
        result = globus_error_put(globus_error_construct_gssapi_error(
            NULL, NULL, maj_stat, min_stat));
    return result;
}

static
int
globus_l_guc_parse_arguments(
    int                                             argc,
    char **                                         argv,
    globus_l_guc_info_t *                           guc_info)
{
    int                                             sc;
    char *                                          program;
    globus_list_t *                                 options_found = NULL;
    char *                                          subject = NULL;
    char *                                          file_name = NULL;
    globus_args_option_instance_t *                 instance = NULL;
    globus_list_t *                                 list = NULL;
    globus_l_guc_src_dst_pair_t *                   ent;
    int                                             rc;
    char *                                          tmp_str;
    int                                             ext_arg_size;
    globus_off_t                                    tmp_off;
    char *                                          authz_assert = NULL;
    char *                                          cred_path = NULL;
    globus_result_t                                 result;

    guc_info->list_url = NULL;
    guc_info->no_3pt = GLOBUS_FALSE;
    guc_info->no_dcau = GLOBUS_FALSE;
    guc_info->data_safe = GLOBUS_FALSE;
    guc_info->data_private = GLOBUS_FALSE;
    guc_info->recurse = GLOBUS_FALSE;
    guc_info->num_streams = 0;
    guc_info->conc = 1;
    guc_info->tcp_buffer_size = 0;
    guc_info->block_size = 0;
    guc_info->options = 0UL;
    guc_info->source_subject = NULL;
    guc_info->dest_subject = NULL;
    guc_info->restart_retries = 5;
    guc_info->restart_interval = 0;
    guc_info->restart_timeout = 0;
    guc_info->stall_timeout = 600;
    guc_info->stripe_bs = -1;
    guc_info->striped = GLOBUS_FALSE;
    guc_info->partial_offset = -1;
    guc_info->partial_length = -1;
    guc_info->rfc1738 = GLOBUS_FALSE;
    guc_info->list_uses_data_mode = GLOBUS_FALSE;
    guc_info->udt = GLOBUS_FALSE;
    guc_info->nl_bottleneck = GLOBUS_FALSE;
    guc_info->nl_level = 0;
    guc_info->nl_interval = 0;
    guc_info->ipv6 = GLOBUS_FALSE;
    guc_info->gridftp2 = GLOBUS_TRUE;
    guc_info->allo = GLOBUS_TRUE;
    guc_info->delayed_pasv = GLOBUS_FALSE;
    guc_info->pipeline = GLOBUS_FALSE;
    guc_info->pipelineq = 0;
    guc_info->create_dest = GLOBUS_FALSE;
    guc_info->dst_module_name = GLOBUS_NULL;
    guc_info->src_module_name = GLOBUS_NULL;
    guc_info->dst_module_args = GLOBUS_NULL;
    guc_info->src_module_args = GLOBUS_NULL;
    guc_info->src_net_stack_str = GLOBUS_NULL;
    guc_info->src_disk_stack_str = GLOBUS_NULL;
    guc_info->dst_net_stack_str = GLOBUS_NULL;
    guc_info->dst_disk_stack_str = GLOBUS_NULL;
    guc_info->src_authz_assert = GLOBUS_NULL;
    guc_info->dst_authz_assert = GLOBUS_NULL;
    guc_info->cache_src_authz_assert = GLOBUS_FALSE;
    guc_info->cache_dst_authz_assert = GLOBUS_FALSE;
    guc_info->src_cred = GSS_C_NO_CREDENTIAL;
    guc_info->dst_cred = GSS_C_NO_CREDENTIAL;
    guc_info->data_cred = GSS_C_NO_CREDENTIAL;
    guc_info->src_cred_subj = NULL;
    guc_info->dst_cred_subj = NULL;
    guc_info->data_cred_subj = NULL;
    guc_info->dumpfile = NULL;
    guc_info->dump_only_file = NULL;
    guc_info->dump_only_fp = NULL;
    guc_info->sync_level = 2;
    guc_info->sync = GLOBUS_FALSE;
    guc_info->perms = GLOBUS_FALSE;
    guc_info->comp_checksum = GLOBUS_FALSE;
    guc_info->checksum_algo = GLOBUS_NULL;


    /* determine the program name */
    
    program = strrchr(argv[0],'/');
    if(!program)
    {
        program = argv[0];
    }
    else
    {
	program++;
    }

    globus_url_copy_i_args_init();

    if(globus_args_scan(
            &argc,
            &argv,
            arg_num,
            args_options,
            program,
            &local_version,
            oneline_usage,
            long_usage,
            &options_found,
            NULL) < 0)  /* error on argument line */
    {
        return -1;
    }
    
    for(list = options_found;
        !globus_list_empty(list);
        list = globus_list_rest(list))
    {
        instance = globus_list_first(list);

        switch(instance->id_number)
        {
        case arg_a:
            guc_info->options |= GLOBUS_URL_COPY_ARG_ASCII;
            break;
        case arg_b:
            guc_info->options |= GLOBUS_URL_COPY_ARG_BINARY;
            break;
        case arg_c:
            g_continue = GLOBUS_TRUE;
            break;
        case arg_ext:
            g_ext = globus_libc_strdup(instance->values[0]);
            ext_arg_size = 8;
            g_ext_args = (char **)calloc(ext_arg_size, sizeof(char *));
            tmp_str = strchr(g_ext, ' ');
            g_ext_args[0] = g_ext;
            g_ext_arg_count = 1;
            while(tmp_str != NULL)
            {
                *tmp_str = '\0';
                tmp_str++;
                if(tmp_str == '\0')
                {
                    tmp_str = NULL;
                }
                else
                {
                    g_ext_args[g_ext_arg_count] = tmp_str;
                    tmp_str = strchr(tmp_str, ' ');
                }
                g_ext_arg_count++;
                if(g_ext_arg_count >= ext_arg_size)
                {
                    ext_arg_size *= 2;
                    g_ext_args = (char **)
                        realloc(g_ext_args, ext_arg_size*sizeof(char *));
                }
            }
            break;
        case arg_mc:
            guc_info->mc_file = globus_libc_strdup(instance->values[0]);
            break;
        case arg_dumpfile:
            guc_info->dumpfile = globus_libc_strdup(instance->values[0]);
            break;
        case arg_aliasfile:
            guc_info->alias_file = globus_libc_strdup(instance->values[0]);
            break;
        case arg_plugin:
            g_client_lib_plugin_list = globus_list_from_string(
                instance->values[0], ',', NULL);
            break;
        case arg_modname:
            guc_info->src_module_name = globus_libc_strdup(instance->values[0]);
            guc_info->dst_module_name = globus_libc_strdup(instance->values[0]);
            break;
        case arg_modargs:
            guc_info->src_module_args = globus_libc_strdup(instance->values[0]);
            guc_info->dst_module_args = globus_libc_strdup(instance->values[0]);
            break;
        case arg_src_modname:
            guc_info->src_module_name = globus_libc_strdup(instance->values[0]);
            break;
        case arg_src_modargs:
            guc_info->src_module_args = globus_libc_strdup(instance->values[0]);
            break;
        case arg_dst_modname:
            guc_info->dst_module_name = globus_libc_strdup(instance->values[0]);
            break;
        case arg_dst_modargs:
            guc_info->dst_module_args = globus_libc_strdup(instance->values[0]);
            break;
        case arg_q:
            g_quiet_flag = GLOBUS_TRUE;
            g_all_quiet = GLOBUS_TRUE;
            break;
        case arg_v:
            g_quiet_flag = GLOBUS_FALSE;
            break;
        case arg_vb:
            g_quiet_flag = GLOBUS_FALSE;
            g_verbose_flag = GLOBUS_TRUE;
            break;
        case arg_bs:
            rc = globus_args_bytestr_to_num(instance->values[0], &tmp_off);
            if(rc != 0)
            {
                globus_url_copy_l_args_error(
                    "invalid value for block size");
                return -1;
            }                  
            guc_info->block_size = (globus_size_t) tmp_off;
            break;
        case arg_f:
            file_name = globus_libc_strdup(instance->values[0]);
            break;
        case arg_tcp_bs:
            rc = globus_args_bytestr_to_num(instance->values[0], &tmp_off);
            if(rc != 0)
            {
                globus_url_copy_l_args_error(
                    "invalid value for tcp buffer size");
                return -1;
            }                  
            guc_info->tcp_buffer_size = (globus_size_t) tmp_off;
            break;
        case arg_s:
            subject = globus_libc_strdup(instance->values[0]);
            break;
        case arg_t:
            rc = sscanf(instance->values[0], "%d", &g_transfer_timeout);
            if(rc != 1)
            {
                globus_url_copy_l_args_error(
                    "Invalid value to -t.  Must be an integer");
                return -1;
            }                  
            break;
        case arg_ss:
            guc_info->source_subject = globus_libc_strdup(instance->values[0]);
            break;
        case arg_ds:
            guc_info->dest_subject = globus_libc_strdup(instance->values[0]);
            break;
        case arg_p:
            guc_info->num_streams = atoi(instance->values[0]);
            if(guc_info->list_uses_data_mode && guc_info->num_streams < 1)
            {
                guc_info->num_streams = 1;
            }
            break;
        case arg_conc:
            guc_info->conc = atoi(instance->values[0]);
            break;
        case arg_notpt:
            guc_info->no_3pt = GLOBUS_TRUE;
            break;
        case arg_nodcau:
            guc_info->no_dcau = GLOBUS_TRUE;
            break;
        case arg_data_safe:
            guc_info->data_safe = GLOBUS_TRUE;
            break;
        case arg_data_private:
            guc_info->data_private = GLOBUS_TRUE;
            break;
        case arg_debugftp:
            g_use_debug = GLOBUS_TRUE;
            break;
        case arg_restart:
            g_use_restart = GLOBUS_TRUE;
            break;
        case arg_rst_retries:
            guc_info->restart_retries = atoi(instance->values[0]);
            break;
        case arg_rst_interval:
            guc_info->restart_interval = atoi(instance->values[0]);
            break;
        case arg_rst_timeout:
            guc_info->restart_timeout = atoi(instance->values[0]);
            break;
        case arg_stall_timeout:
            guc_info->stall_timeout = atoi(instance->values[0]);
            break;
        case arg_recurse:
            guc_info->recurse = GLOBUS_TRUE;
            break;
        case arg_rfc1738:
            guc_info->rfc1738 = GLOBUS_TRUE;
            break;
        case arg_create_dest:
            guc_info->create_dest = GLOBUS_TRUE;
            break;	
        case arg_stripe_bs:
            rc = globus_args_bytestr_to_num(instance->values[0], &tmp_off);
            if(rc != 0)
            {
                globus_url_copy_l_args_error(
                    "invalid value for stripe blocksize");
                return -1;
            }                  
            guc_info->stripe_bs = (globus_size_t) tmp_off;
	    break;
	case arg_striped:
	    guc_info->striped = GLOBUS_TRUE;
	    break;
	case arg_udt:
	    guc_info->udt = GLOBUS_TRUE;
	    break;
	case arg_nl_bottleneck:
	    guc_info->nl_bottleneck = GLOBUS_TRUE;
        guc_info->nl_level = 15;
	    break;

    case arg_nl_interval:
        guc_info->nl_bottleneck = GLOBUS_TRUE;
        guc_info->nl_level = 15;
        sc = sscanf(instance->values[0], "%d", &guc_info->nl_interval);
        if(sc != 1)
        {
            fprintf(stderr, "%s",
               _GASCSL("Error: Argument to nl-bottleneck must be a 4 bit mask"
                "\n"));
            return -1;
        } 
        break;


	case arg_ipv6:
	    guc_info->ipv6 = GLOBUS_TRUE;
	    break;
	case arg_gridftp2:
	    guc_info->gridftp2 = GLOBUS_TRUE;
	    break;
	case arg_gridftp2_off:
	    guc_info->gridftp2 = GLOBUS_FALSE;
	    break;

        case arg_src_pipe_str:
            guc_info->src_pipe_str =
                guc_l_pipe_to_stack_str(instance->values[0]);
            break;
        case arg_dst_pipe_str:
            guc_info->dst_pipe_str =
                guc_l_pipe_to_stack_str(instance->values[0]);
            break;
        case arg_pipe_str:
            guc_info->dst_pipe_str =
                guc_l_pipe_to_stack_str(instance->values[0]);
            guc_info->src_pipe_str =
                guc_l_pipe_to_stack_str(instance->values[0]);
            break;

        case arg_src_net_stack_str:
            guc_info->src_net_stack_str = 
                globus_libc_strdup(instance->values[0]);
            break;
        case arg_src_disk_stack_str:
            guc_info->src_disk_stack_str = 
                globus_libc_strdup(instance->values[0]);
            break;
        case arg_dst_net_stack_str:
            guc_info->dst_net_stack_str = 
                globus_libc_strdup(instance->values[0]);
            break;
        case arg_dst_disk_stack_str:
            guc_info->dst_disk_stack_str = 
                globus_libc_strdup(instance->values[0]);
            break;
        case arg_net_stack_str:
            guc_info->src_net_stack_str = 
                globus_libc_strdup(instance->values[0]);
            guc_info->dst_net_stack_str = 
                globus_libc_strdup(instance->values[0]);
            break;
        case arg_disk_stack_str:
            guc_info->src_disk_stack_str = 
                globus_libc_strdup(instance->values[0]);
            guc_info->dst_disk_stack_str = 
                globus_libc_strdup(instance->values[0]);
            break;
        case arg_authz_assert:
            result = globus_l_guc_file_to_string(
                               instance->values[0], &authz_assert);
            if(result != GLOBUS_SUCCESS)
            {
		fprintf(stderr, 
                    _GASCSL("Error: Unable to read authz assertion %s\n"),
		    globus_error_print_friendly(globus_error_peek(result)));
                return -1;
            }
            break;
        case arg_src_authz_assert:
            result = globus_l_guc_file_to_string(
		       instance->values[0], &guc_info->src_authz_assert);
            if(result != GLOBUS_SUCCESS)
            {
		fprintf(stderr, 
                    _GASCSL("Error: Unable to read src authz assertion %s\n"),
		    globus_error_print_friendly(globus_error_peek(result)));
                return -1;
            }
            break;
        case arg_dst_authz_assert:
            result = globus_l_guc_file_to_string(
		       instance->values[0], &guc_info->dst_authz_assert);
            if(result != GLOBUS_SUCCESS)
            {
		fprintf(stderr, 
                    _GASCSL("Error: Unable to read dst authz assertion %s\n"),
		    globus_error_print_friendly(globus_error_peek(result)));
                return -1;
            }
            break;
        case arg_cache_authz_assert:
            guc_info->cache_src_authz_assert = GLOBUS_TRUE;
            guc_info->cache_dst_authz_assert = GLOBUS_TRUE;
            break;
        case arg_cache_src_authz_assert:
            guc_info->cache_src_authz_assert = GLOBUS_TRUE;
            break;
        case arg_cache_dst_authz_assert:
            guc_info->cache_dst_authz_assert = GLOBUS_TRUE;
            break;
        case arg_cred:
            cred_path = globus_libc_strdup(instance->values[0]);
            break;
        case arg_src_cred:
            result = globus_l_guc_load_cred(
                instance->values[0], 
                &guc_info->src_cred, 
                &guc_info->src_cred_subj);
            if(result != GLOBUS_SUCCESS)
            {
                fprintf(stderr,
                    "Error loading source credential: %s\n",
                    globus_error_print_friendly(globus_error_peek(result)));
                    return -1;
            }
            break;
        case arg_dst_cred:
            result = globus_l_guc_load_cred(
                instance->values[0], 
                &guc_info->dst_cred,
                &guc_info->dst_cred_subj);
            if(result != GLOBUS_SUCCESS)
            {
                fprintf(stderr,
                    "Error loading destination credential: %s\n",
                    globus_error_print_friendly(globus_error_peek(result)));
                    return -1;
            }
            break;
        case arg_data_cred:
            result = globus_l_guc_load_cred(
                instance->values[0], 
                &guc_info->data_cred,
                &guc_info->data_cred_subj);
            if(result != GLOBUS_SUCCESS)
            {
                fprintf(stderr,
                    "Error loading data channel credential: %s\n",
                    globus_error_print_friendly(globus_error_peek(result)));
                    return -1;
            }
            break;
	case arg_delayed_pasv:
	    guc_info->delayed_pasv = GLOBUS_TRUE;
	    break;
	case arg_pipeline:
	    guc_info->pipeline = GLOBUS_TRUE;
	    break;
	case arg_pipelineq:
	    guc_info->pipeline = GLOBUS_TRUE;
	    guc_info->pipelineq = atoi(instance->values[0]);
	    break;
	case arg_sync:
	    guc_info->sync = GLOBUS_TRUE;
	    break;
	case arg_sync_level:
	    guc_info->sync_level = atoi(instance->values[0]);
	    break;
	case arg_checksum:
	    guc_info->comp_checksum = GLOBUS_TRUE;
	    break;
	case arg_checksum_algo:
	    guc_info->checksum_algo = globus_libc_strdup(instance->values[0]);
	    break;
	case arg_dump_only:
	    guc_info->dump_only_file = globus_libc_strdup(instance->values[0]);
	    break;
	case arg_preserve:
	case arg_perms:
	    guc_info->perms = GLOBUS_TRUE;
	    break;
	case arg_allo:
	    guc_info->allo = GLOBUS_TRUE;
	    break;
	case arg_noallo:
	    guc_info->allo = GLOBUS_FALSE;
	    break;
	case arg_partial_offset:
            rc = globus_args_bytestr_to_num(instance->values[0], &tmp_off);
            if(rc != 0)
            {
                globus_url_copy_l_args_error(
                    "invalid value for offset");
                return -1;
            }                  
            guc_info->partial_offset = tmp_off;
	    break;
	case arg_partial_length:
            rc = globus_args_bytestr_to_num(instance->values[0], &tmp_off);
            if(rc != 0)
            {
                globus_url_copy_l_args_error(
                    "invalid value for length");
                return -1;
            }                  
            guc_info->partial_length = tmp_off;
            if(guc_info->partial_offset == -1)
            {
                guc_info->partial_offset = 0;
            }
	    break;
	case arg_fast:
	    guc_info->list_uses_data_mode = GLOBUS_TRUE;
	    if(guc_info->num_streams < 1)
	    {
                guc_info->num_streams = 1;
            }

	    break;

	    case arg_list:
            guc_info->list_url = globus_libc_strdup(instance->values[0]);
            break;

        default:
            globus_url_copy_l_args_error_fmt("parse panic, arg id = %d",
                                       instance->id_number);
            break;
        }
    }

    globus_args_option_instance_list_free(&options_found);

    if(guc_info->checksum_algo == GLOBUS_NULL)
    {
       guc_info->checksum_algo = globus_libc_strdup("MD5");
    }

    if(guc_info->src_pipe_str != NULL)
    {
        if(guc_info->src_disk_stack_str)
        {
            tmp_str = globus_common_create_string("%s,%s", 
                guc_info->src_pipe_str, guc_info->src_disk_stack_str);
            free(guc_info->src_disk_stack_str);
            guc_info->src_disk_stack_str = tmp_str;
        }
        else
        {
            guc_info->src_disk_stack_str = strdup(guc_info->src_pipe_str);
        }
    }
    if(guc_info->dst_pipe_str != NULL)
    {
        if(guc_info->dst_disk_stack_str)
        {
            tmp_str = globus_common_create_string("%s,%s", 
                guc_info->dst_pipe_str, guc_info->dst_disk_stack_str);
            free(guc_info->dst_disk_stack_str);
            guc_info->dst_disk_stack_str = tmp_str;
        }
        else
        {
            guc_info->dst_disk_stack_str = globus_common_create_string(
                "%s,ordering", guc_info->dst_pipe_str);
        }
    }
    /* if we are doing multicast allow no dest option by adding the first 
        url in the file */
    if(guc_info->mc_file != NULL)
    {
        char **                         first_dst_ptr = NULL;
        char *                          first_dst = NULL;
        char *                          sbj;

        if(file_name != NULL)
        {
            /* echo error */
            globus_url_copy_l_args_error("iCannot use -mc and -f");
            return -1;
        }

        if(argc == 2)
        {
            first_dst_ptr = &first_dst;
        }

        globus_fifo_init(&guc_mc_url_q);

        sbj = subject;
        if(guc_info->dest_subject != NULL)
        {
            sbj = guc_info->dest_subject;
        }
        g_l_mc_fs_str = guc_build_mc_str(
            guc_info->mc_file, first_dst_ptr, sbj);
        if(g_l_mc_fs_str == NULL)
        {
            globus_url_copy_l_args_error("There is no destination set");
            return -1;
        }

        /* lie to the rest of the code b tacking on a new first dest */
        if(first_dst_ptr != NULL)
        {
            argc++;
            argv[argc-1] = first_dst;
        }
    }

    rc = -1;
    if(guc_info->dump_only_file && guc_info->dump_only_file[0] == '-' && 
        guc_info->dump_only_file[1] == '\0')
    {
        guc_info->dump_only_fp = stdout;
    }
    
    if(guc_info->dumpfile)
    {
        if(guc_info->dump_only_file && !guc_info->dump_only_fp)
        {
            rc = globus_l_guc_parse_file(
                guc_info, guc_info->dump_only_file, &guc_info->dump_url_list);
        }
        else if(rc == -2)
        {
            return -1;
        }

        rc = globus_l_guc_parse_file(
            guc_info, guc_info->dumpfile, &guc_info->user_url_list);
        
        if(rc == 0 && globus_fifo_empty(&guc_info->user_url_list))
        {
            printf("Dumpfile exists and is empty.  Nothing to do.");
            exit(2);
        }
        else if(rc == -2)
        {
            return -1;
        }

    }
    
    if(rc == 0)
    {
        if(g_verbose_flag)
        {
            printf("Reading URLs from dumpfile\n");
        }
    }    
    else if(file_name != NULL)
    {
        /* get source dest pairs */
        if(argc > 1)
        {
            globus_url_copy_l_args_error(
                "No urls are provided on the command line when using "
                "the -f option.\n");
            globus_free(file_name);
            return -1;
        }

        if(globus_l_guc_parse_file(
            guc_info, file_name, &guc_info->user_url_list) != 0)
        {
            globus_free(file_name);
            return -1;
        }

        globus_free(file_name);
    }
    else if(guc_info->list_url == NULL) /* only if we are not listing */
    {
        /* there must be 2 additional unflagged arguments:
         *     the source and destination URL's 
         */    
        if(argc > 3)
        {
            globus_url_copy_l_args_error("too many url strings specified");
            return -1;
        }
        if(argc < 3)
        {
            globus_url_copy_l_args_error(
                "source and dest url strings are required");
            return -1;
        }

        if(strcmp(argv[1], "-") == 0 && strcmp(argv[2], "-") == 0)
        {
            globus_url_copy_l_args_error(
                "Cannot have stdin as source and stdout as destination.\n");
            return -1;
        }
        else
        {   
            ent = (globus_l_guc_src_dst_pair_t *)
                    globus_malloc(sizeof(globus_l_guc_src_dst_pair_t));

            ent->src_url = guc_l_convert_file_url(argv[1]);
            ent->dst_url = guc_l_convert_file_url(argv[2]);
            ent->offset = guc_info->partial_offset;
            ent->length = guc_info->partial_length;
            ent->src_info = NULL;
 
            globus_fifo_enqueue(&guc_info->user_url_list, ent);
        }
        
    }

    globus_l_guc_load_alias_file(guc_info->alias_file);

    if(cred_path)
    {
        if(guc_info->src_cred == GSS_C_NO_CREDENTIAL)
        {
            result = globus_l_guc_load_cred(
                cred_path, 
                &guc_info->src_cred,
                &guc_info->src_cred_subj);
            if(result != GLOBUS_SUCCESS)
            {
                fprintf(stderr,
                    "Error loading source credential: %s\n",
                    globus_error_print_friendly(globus_error_peek(result)));
                    return -1;
            }
        }
        if(guc_info->dst_cred == GSS_C_NO_CREDENTIAL)
        {
            result = globus_l_guc_load_cred(
                cred_path, 
                &guc_info->dst_cred,
                &guc_info->dst_cred_subj);
            if(result != GLOBUS_SUCCESS)
            {
                fprintf(stderr,
                    "Error loading dest credential: %s\n",
                    globus_error_print_friendly(globus_error_peek(result)));
                    return -1;
            }
        }
        
        globus_free(cred_path);
    }
    
    if(guc_info->src_cred != GSS_C_NO_CREDENTIAL && 
        guc_info->dst_cred == GSS_C_NO_CREDENTIAL)
    {
        result = globus_l_guc_load_cred(
            NULL, 
            &guc_info->dst_cred,
            &guc_info->dst_cred_subj);
        if(result != GLOBUS_SUCCESS)
        {
            fprintf(stderr,
                "Error loading dest credential: %s\n",
                globus_error_print_friendly(globus_error_peek(result)));
                return -1;
        }
    }
    if(guc_info->src_cred == GSS_C_NO_CREDENTIAL && 
        guc_info->dst_cred != GSS_C_NO_CREDENTIAL)
    {
        result = globus_l_guc_load_cred(
            NULL, 
            &guc_info->src_cred,
            &guc_info->src_cred_subj);
        if(result != GLOBUS_SUCCESS)
        {
            fprintf(stderr,
                "Error loading dest credential: %s\n",
                globus_error_print_friendly(globus_error_peek(result)));
                return -1;
        }
    }

    if(subject && !guc_info->source_subject)
    {
        guc_info->source_subject = globus_libc_strdup(subject);
    }
    if(subject && !guc_info->dest_subject)
    {
        guc_info->dest_subject = globus_libc_strdup(subject);
    }

    if(subject) globus_free(subject);

    if(authz_assert && !guc_info->src_authz_assert)
    {
        guc_info->src_authz_assert = globus_libc_strdup(authz_assert);
    }
    if(authz_assert && !guc_info->dst_authz_assert)
    {
        guc_info->dst_authz_assert = globus_libc_strdup(authz_assert);
    }

    if(authz_assert) globus_free(authz_assert);

    if(guc_info->pipeline && guc_info->num_streams < 1)
    {
        guc_info->num_streams = 1;
    }

    if(guc_info->udt)
    {
        /* need to verify nothing else was set */
        guc_info->src_net_stack_str = globus_libc_strdup("udt");
        guc_info->dst_net_stack_str = globus_libc_strdup("udt");
    }

    if(guc_info->nl_bottleneck)
    {
        globus_uuid_t                   uuid;
        char *                          net_netlog_str;
        char *                          disk_netlog_str;
        char *                          disk_stack_str;
        char *                          net_stack_str;


        ent = (globus_l_guc_src_dst_pair_t *)
            globus_fifo_peek(&guc_info->user_url_list);

        /* we must verify that we are doing a 3pt.  this means that
            both src and dst url must be ftp:// or gsiftp:// 
            obviously this doesnt work for the case where there is
            a file full or url transfers and the first is 3pt fpt transfers
            but the remainder are not.  that case is a PITA.  For now
            we nicely error out here. */
        if(ent == NULL)
        {
            globus_url_copy_l_args_error(
                "-nl-bottle options requires third party transfers between "
                "ftp:// or gsiftp:// urls.");
            return -1;
        }
        if(strncmp(ent->src_url, "ftp://", 6) != 0 &&
            strncmp(ent->src_url, "gsiftp://", 9) != 0)
        {
            globus_url_copy_l_args_error(
                "-nl-bottle options requires third party transfers between "
                "ftp:// or gsiftp:// urls.");
            return -1;
        }
        if(strncmp(ent->dst_url, "ftp://", 6) != 0 &&
            strncmp(ent->dst_url, "gsiftp://", 9) != 0)
        {
            globus_url_copy_l_args_error(
                "-nl-bottle options requires third party transfers between "
                "ftp:// or gsiftp:// urls.");
            return -1;
        }

        /* stick in plugin */
        globus_list_insert(
            &g_client_lib_plugin_list, "client_netlogger_plugin");

        globus_uuid_create(&uuid);

        net_netlog_str = globus_common_create_string(
            "netlogger:uuid=%s;mask=255;io_type=net;interval=%d;level=%d",
            uuid.text, guc_info->nl_interval, guc_info->nl_level);
        disk_netlog_str = globus_common_create_string(
            "netlogger:uuid=%s;mask=255;io_type=disk;interval=%d;level=%d",
            uuid.text, guc_info->nl_interval, guc_info->nl_level);

        if(guc_info->src_net_stack_str != NULL)
        {
            net_stack_str = globus_common_create_string(
                "%s,%s", guc_info->src_net_stack_str, net_netlog_str);
            globus_free(guc_info->src_net_stack_str);

            guc_info->src_net_stack_str = net_stack_str;
        }
        else
        {
            guc_info->src_net_stack_str = globus_common_create_string(
                "tcp,%s", net_netlog_str);
        }
        if(guc_info->dst_net_stack_str != NULL)
        {
            net_stack_str = globus_common_create_string(
                "%s,%s", guc_info->dst_net_stack_str, net_netlog_str);
            globus_free(guc_info->dst_net_stack_str);

            guc_info->dst_net_stack_str = net_stack_str;
        }
        else
        {
            guc_info->dst_net_stack_str = globus_common_create_string(
                "tcp,%s", net_netlog_str);
        }

        if(guc_info->dst_disk_stack_str != NULL)
        {
            disk_stack_str = globus_common_create_string(
                "%s,%s", guc_info->src_disk_stack_str, disk_netlog_str);
            globus_free(guc_info->src_disk_stack_str);

            guc_info->src_disk_stack_str = disk_stack_str;
        }
        else
        {
            guc_info->src_disk_stack_str = globus_common_create_string(
                "file,%s", disk_netlog_str);
        }
        if(guc_info->dst_disk_stack_str != NULL)
        {
            disk_stack_str = globus_common_create_string(
                "%s,%s", guc_info->dst_disk_stack_str, disk_netlog_str);
            globus_free(guc_info->dst_disk_stack_str);

            guc_info->dst_disk_stack_str = disk_stack_str;
        }
        else
        {
            guc_info->dst_disk_stack_str = globus_common_create_string(
                "file,%s", disk_netlog_str);
        }

        globus_free(net_netlog_str);
        globus_free(disk_netlog_str);
    }
    
    /* check arguemnt validity */
    if((guc_info->options & GLOBUS_URL_COPY_ARG_ASCII) &&
       (guc_info->options & GLOBUS_URL_COPY_ARG_BINARY) )
    {
        globus_url_copy_l_args_error(
            "option -ascii and -binary are mutually exclusive");
        return -1;
    }
    if(guc_info->data_safe && guc_info->data_private)
    {
        globus_url_copy_l_args_error(
            "option -data-channel-safe and -data-channel-private "
            "are mutually exclusive");
        return -1;
    }
    return 0;
    
}


static
globus_result_t
globus_l_guc_create_dir(
    char *                              url,
    globus_l_guc_handle_t *             handle,
    globus_l_guc_info_t *               guc_info)
{
    globus_result_t                     result;
    globus_bool_t                       first_attempt = GLOBUS_TRUE;
    globus_bool_t                       done_create_dest = GLOBUS_FALSE;
    char *                              dst_mkurl;
    int                                 rc;
    globus_url_t                        parsed_url;
    globus_list_t *                     mkdir_urls = GLOBUS_NULL;
    char *                              dst_filename;
    
    rc = globus_url_parse(url, &parsed_url);
    if(rc != 0)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "Couldn't create destination: error parsing url."));
        goto error;
    }
    if(parsed_url.url_path == GLOBUS_NULL)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "Couldn't create destination: empty url path."));
        goto error;
    }
    dst_mkurl = globus_libc_strdup(url);
    while(rc == 0 && parsed_url.url_path != GLOBUS_NULL &&
        strrchr(parsed_url.url_path, '/') != parsed_url.url_path)
    {
        dst_filename = strrchr(dst_mkurl, '/');
        if(dst_filename)
        {
            char *                      save_url;
            save_url = globus_libc_strdup(dst_mkurl);

            save_url[dst_filename - dst_mkurl + 1] = '\0';
            globus_list_insert(&mkdir_urls, save_url);
            
            *dst_filename = '\0';                       
        }
        
        globus_url_destroy(&parsed_url);
        rc = globus_url_parse(dst_mkurl, &parsed_url);
    }
    if(rc == 0)
    {
        globus_url_destroy(&parsed_url);
    }
    globus_free(dst_mkurl);
    
    dst_mkurl = globus_libc_strdup(url);
    dst_filename = strrchr(dst_mkurl, '/');
    if(dst_filename && *(++dst_filename))
    {
        *dst_filename = '\0';
    }
    globus_list_insert(&mkdir_urls, dst_mkurl);

    while(!done_create_dest && !globus_list_empty(mkdir_urls))
    {        
        globus_bool_t                   inserted = GLOBUS_FALSE;
        char *                          already_exists = NULL;
        
        dst_mkurl = globus_list_remove(&mkdir_urls, mkdir_urls);
        
        already_exists = globus_hashtable_lookup(
            &guc_info->mkdir_hash, dst_mkurl);

        if(!already_exists)
        {
            if(handle->dest_ftp_attr)
            {
                globus_ftp_client_operationattr_destroy(
                    &handle->dest_ftp_attr);
            }

            globus_l_guc_gass_attr_init(
                &handle->dest_gass_copy_attr,
                &handle->dest_gass_attr,
                &handle->dest_ftp_attr,
                guc_info,
                url,
                GLOBUS_FALSE,
                GLOBUS_FALSE);

            result = globus_gass_copy_mkdir(
                &handle->gass_copy_handle,
                dst_mkurl,
                &handle->dest_gass_copy_attr);
            if(result == GLOBUS_SUCCESS && first_attempt)
            {
                done_create_dest = GLOBUS_TRUE;
            }
            
            if(result == GLOBUS_SUCCESS || !first_attempt)
            {
                globus_mutex_lock(&g_monitor.mutex);
                    rc = globus_hashtable_insert(
                        &guc_info->mkdir_hash,
                        dst_mkurl,
                        dst_mkurl);
                globus_mutex_unlock(&g_monitor.mutex);

                if(rc == 0)
                {
                    inserted = GLOBUS_TRUE;
                }
            }

            globus_ftp_client_operationattr_destroy(
                &handle->dest_ftp_attr);
            handle->dest_ftp_attr = NULL;
        }
        
        first_attempt = GLOBUS_FALSE;
        if(!inserted)
        {
            globus_free(dst_mkurl);
        }
    }
    
    while(!globus_list_empty(mkdir_urls))
    {
        dst_mkurl = globus_list_remove(&mkdir_urls, mkdir_urls);
        globus_free(dst_mkurl);
    }
    
    return GLOBUS_SUCCESS;
    
error:
    return result;
}

static
globus_result_t
globus_l_guc_expand_urls(
    globus_l_guc_info_t *               guc_info,
    globus_l_guc_handle_t *             handle)
{
    char *                              src_url;
    char *                              dst_url;
    globus_l_guc_src_dst_pair_t *       user_url_pair;
    globus_l_guc_src_dst_pair_t *       expanded_url_pair;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_bool_t                       no_matches = GLOBUS_TRUE;
    globus_bool_t                       was_error = GLOBUS_FALSE;
    globus_bool_t                       no_expand = GLOBUS_FALSE;

    globus_hashtable_init(
        &guc_info->recurse_hash,
        256,
        globus_hashtable_string_hash,
        globus_hashtable_string_keyeq);
            
    globus_hashtable_init(
        &guc_info->dest_hash,
        256,
        globus_hashtable_string_hash,
        globus_hashtable_string_keyeq);

    globus_hashtable_init(
        &guc_info->mkdir_hash,
        256,
        globus_hashtable_string_hash,
        globus_hashtable_string_keyeq);

    if(!globus_fifo_empty(&guc_info->user_url_list))
    {
        user_url_pair = globus_l_guc_dequeue_pair(
            &guc_info->user_url_list, handle->id);

        src_url = user_url_pair->src_url;
        dst_url = user_url_pair->dst_url;
        
        no_expand = GLOBUS_FALSE;
        if(strcmp("-", src_url) == 0)
        {
            expanded_url_pair = (globus_l_guc_src_dst_pair_t *)
                    globus_malloc(sizeof(globus_l_guc_src_dst_pair_t));
        
            expanded_url_pair->src_url = globus_libc_strdup(src_url);
            expanded_url_pair->dst_url = globus_libc_strdup(dst_url);
            expanded_url_pair->offset = user_url_pair->offset;
            expanded_url_pair->length = user_url_pair->length;
            expanded_url_pair->src_info = NULL;
            
            if(guc_info->create_dest && !guc_info->dump_only_file)
            {
                result = globus_l_guc_create_dir(
                    expanded_url_pair->dst_url, handle, guc_info);
                if(result != GLOBUS_SUCCESS)
                {
                    goto error_transfer;
                }
            }

            globus_fifo_enqueue(
                &guc_info->expanded_url_list, 
                expanded_url_pair);
            no_expand = GLOBUS_TRUE;
        }
                
        globus_l_guc_gass_attr_init(
            &handle->source_gass_copy_attr,
            &handle->source_gass_attr,
            &handle->source_ftp_attr,
            guc_info,
            src_url,
            GLOBUS_TRUE,
            GLOBUS_TRUE);
        if(guc_info->sync)
        {
            globus_l_guc_gass_attr_init(
                &handle->dest_gass_copy_attr,
                &handle->dest_gass_attr,
                &handle->dest_ftp_attr,
                guc_info,
                dst_url,
                GLOBUS_FALSE,
                GLOBUS_TRUE);
        }

        if(!no_expand)
        { 
            globus_l_guc_transfer_t    transfer_info;
            transfer_info.urls = user_url_pair;
            globus_fifo_init(&transfer_info.matched_url_list);
            transfer_info.needs_mkdir = GLOBUS_TRUE;
            transfer_info.handle = handle;
            transfer_info.guc_info = guc_info;
            handle->current_transfer = &transfer_info;
            transfer_info.guc_info->conc_outstanding++;

            result = globus_l_guc_expand_single_url(&transfer_info);

            handle->current_transfer = NULL;
            transfer_info.guc_info->conc_outstanding--;
            globus_fifo_destroy_all(
                &transfer_info.matched_url_list, globus_l_guc_url_info_free);

            if(result != GLOBUS_SUCCESS)
            {
                if(!g_continue)
                {                    
                    globus_fifo_enqueue(
                        &transfer_info.guc_info->user_url_list, 
                        transfer_info.urls);

                    transfer_info.urls = NULL;
                }
                
                globus_l_guc_url_pair_free(transfer_info.urls);
                goto error_expand;
                
            }
        }
        globus_ftp_client_operationattr_destroy(&handle->source_ftp_attr);
        handle->source_ftp_attr = NULL;
        if(guc_info->sync)
        {
            globus_ftp_client_operationattr_destroy(&handle->dest_ftp_attr);
            handle->dest_ftp_attr = NULL;
        }
                
        globus_l_guc_url_pair_free(user_url_pair);
    }

    if(!globus_fifo_empty(&guc_info->expanded_url_list) || 
        guc_info->sync || guc_info->dump_only_fp)
    {
        no_matches = GLOBUS_FALSE;
    }

    result = globus_l_guc_transfer_files(guc_info);
    
    if(globus_l_globus_url_copy_ctrlc_handled)
    {
        goto error_transfer;
    }
    if(result != GLOBUS_SUCCESS)
    {
        was_error = GLOBUS_TRUE;
        if(!g_continue)
        {
            goto error_transfer;
        }
    }
    
    if(no_matches && result == GLOBUS_SUCCESS)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_NULL,
                GLOBUS_NULL,
                _GASCSL("No files matched the source url.\n")));                    
    }
    if(was_error && result == GLOBUS_SUCCESS)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_NULL,
                GLOBUS_NULL,
                _GASCSL("There was an error with one or more transfers.\n")));
    }

    globus_hashtable_destroy_all(&guc_info->recurse_hash,
        globus_l_guc_hashtable_element_free);
    globus_hashtable_destroy_all(&guc_info->dest_hash,
        globus_l_guc_url_info_free);
    globus_hashtable_destroy_all(&guc_info->mkdir_hash,
        globus_l_guc_hashtable_element_free);

    return result;

error_expand:
    globus_ftp_client_operationattr_destroy(&handle->source_ftp_attr);
    handle->source_ftp_attr = NULL;
    if(guc_info->sync)
    {
        globus_ftp_client_operationattr_destroy(&handle->dest_ftp_attr);
        handle->dest_ftp_attr = NULL;
    }

error_transfer:            
    globus_hashtable_destroy_all(&guc_info->recurse_hash,
        globus_l_guc_hashtable_element_free);
    globus_hashtable_destroy_all(&guc_info->dest_hash,
        globus_l_guc_url_info_free);
    globus_hashtable_destroy_all(&guc_info->mkdir_hash,
        globus_l_guc_hashtable_element_free);
     
    return result;                
}

typedef struct
{
    globus_l_guc_handle_t *             handle;
    globus_bool_t                       source;
    globus_l_guc_url_info_t *           urlinfo;
    globus_bool_t                       done;
    globus_object_t *                   error;
    globus_l_guc_monitor_t *            monitor;
} globus_l_guc_cksm_info_t;


static 
void
globus_l_guc_cksm_cb(
    void *                              callback_arg,
    globus_gass_copy_handle_t *         handle,
    globus_object_t *                   error)
{    
    globus_l_guc_cksm_info_t *          cksm_info;

    cksm_info = (globus_l_guc_cksm_info_t *) callback_arg;
    
    if(error)
    {
        globus_free(cksm_info->urlinfo->checksum);
        cksm_info->urlinfo->checksum = NULL;
        cksm_info->error = globus_object_copy(error);
    }
    globus_mutex_lock(&cksm_info->monitor->mutex);
    {
        cksm_info->done = GLOBUS_TRUE;
        globus_cond_signal(&cksm_info->monitor->cond);
    }
    globus_mutex_unlock(&cksm_info->monitor->mutex);   
}


static
globus_result_t
globus_l_guc_check_sync(
    globus_l_guc_info_t *               guc_info,
    globus_l_guc_handle_t *             handle,
    globus_l_guc_url_info_t *           src_urlinfo,
    char *                              dst_url,
    globus_bool_t *                     mismatch)
{
    globus_bool_t                       retval = GLOBUS_TRUE;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_guc_url_info_t *           dest_urlinfo;
    
    globus_mutex_lock(&g_monitor.mutex);
    dest_urlinfo = globus_hashtable_remove(&guc_info->dest_hash, dst_url);
    globus_mutex_unlock(&g_monitor.mutex);

    if(dest_urlinfo)
    {
        switch(guc_info->sync_level)
        {
            case GLOBUS_L_GUC_CKSM:
                if(dest_urlinfo->size == src_urlinfo->size)
                {
                    globus_l_guc_cksm_info_t    src_cksm_info;
                    globus_l_guc_cksm_info_t    dst_cksm_info;
                    globus_l_guc_monitor_t      cksm_monitor;

                    globus_mutex_init(&cksm_monitor.mutex, NULL);
                    globus_cond_init(&cksm_monitor.cond, NULL);
                    
                    src_urlinfo->checksum = malloc(CKSM_SIZE);
                    src_cksm_info.urlinfo = src_urlinfo;
                    src_cksm_info.done = GLOBUS_FALSE;
                    src_cksm_info.monitor = &cksm_monitor;
                    src_cksm_info.error = NULL;
                    
                    result = globus_gass_copy_cksm_async(
                        &handle->gass_copy_handle,
                        src_urlinfo->url,
                        &handle->source_gass_copy_attr,
                        0,
                        -1,
                        guc_info->checksum_algo,
                        src_urlinfo->checksum,
                        globus_l_guc_cksm_cb,
                        &src_cksm_info);
                    if(result != GLOBUS_SUCCESS)
                    {
                        globus_free(src_urlinfo->checksum);
                        src_urlinfo->checksum = NULL;
                        goto error;
                    }
                    globus_ftp_client_handle_t      real_ftp_handle;
                    globus_ftp_client_handle_t      cksm_ftp_handle;
                    
                    dest_urlinfo->checksum = malloc(CKSM_SIZE);
                    dst_cksm_info.urlinfo = dest_urlinfo;
                    dst_cksm_info.done = GLOBUS_FALSE;
                    dst_cksm_info.monitor = &cksm_monitor;
                    dst_cksm_info.error = NULL;
                    
                    globus_gass_copy_get_ftp_handle(
                        &handle->gass_copy_handle, &real_ftp_handle);
                    globus_gass_copy_get_ftp_handle(
                        &handle->cksm_gass_copy_handle, &cksm_ftp_handle);
                    globus_ftp_client_handle_borrow_connection(
                        &real_ftp_handle, GLOBUS_FALSE, &cksm_ftp_handle, GLOBUS_TRUE);
                    
                    result = globus_gass_copy_cksm_async(
                        &handle->cksm_gass_copy_handle,
                        dest_urlinfo->url,
                        &handle->dest_gass_copy_attr,
                        0,
                        -1,
                        guc_info->checksum_algo,
                        dest_urlinfo->checksum,
                        globus_l_guc_cksm_cb,
                        &dst_cksm_info);
                    if(result != GLOBUS_SUCCESS)
                    {
                        globus_free(dest_urlinfo->checksum);
                        dest_urlinfo->checksum = NULL;
                        globus_ftp_client_handle_borrow_connection(
                            &cksm_ftp_handle, GLOBUS_TRUE, &real_ftp_handle, GLOBUS_FALSE);

                        goto error;
                    }
                        
                    globus_mutex_lock(&cksm_monitor.mutex);
                    while(!src_cksm_info.done || !dst_cksm_info.done)
                    {
                        globus_cond_wait(
                            &cksm_monitor.cond, &cksm_monitor.mutex);
                    }
                    globus_mutex_unlock(&cksm_monitor.mutex);
                    
                    if(src_cksm_info.error)
                    {
                        result = globus_error_put(src_cksm_info.error);
                        globus_ftp_client_handle_borrow_connection(
                            &cksm_ftp_handle, GLOBUS_TRUE, &real_ftp_handle, GLOBUS_FALSE);

                        goto error;
                    }
                    if(dst_cksm_info.error)
                    {
                        result = globus_error_put(dst_cksm_info.error);
                        globus_ftp_client_handle_borrow_connection(
                            &cksm_ftp_handle, GLOBUS_TRUE, &real_ftp_handle, GLOBUS_FALSE);

                        goto error;
                    }
                    
                    if(src_urlinfo->checksum && dest_urlinfo->checksum &&
                        strcmp(src_urlinfo->checksum, dest_urlinfo->checksum) == 0)
                    {
                        retval = GLOBUS_FALSE;
                    }
                    globus_ftp_client_handle_borrow_connection(
                        &cksm_ftp_handle, GLOBUS_TRUE, &real_ftp_handle, GLOBUS_FALSE);

                    globus_cond_destroy(&cksm_monitor.cond);
                    globus_mutex_destroy(&cksm_monitor.mutex);
                }
                break;

            case GLOBUS_L_GUC_DATE:
                if(dest_urlinfo->mdtm >= src_urlinfo->mdtm && 
                    src_urlinfo->mdtm != -1 &&
                    dest_urlinfo->size == src_urlinfo->size &&
                    src_urlinfo->size != -1)
                {
                    retval = GLOBUS_FALSE;
                }
                break;
                
            case GLOBUS_L_GUC_SIZE:
                if(dest_urlinfo->size == src_urlinfo->size &&
                    src_urlinfo->size != -1)
                {
                    retval = GLOBUS_FALSE;
                }
                break;

            case GLOBUS_L_GUC_EXISTS:
                if(dest_urlinfo->size != -1)
                {
                    retval = GLOBUS_FALSE;
                }
                break;
             
            default:
                break;
        }

        globus_l_guc_url_info_free(dest_urlinfo);
    }
    
    *mismatch = retval;
    return GLOBUS_SUCCESS;

error:
    *mismatch = GLOBUS_TRUE;
    return result;

}

static
globus_result_t
globus_l_guc_expand_single_url(
    globus_l_guc_transfer_t *           transfer_info)
{
    char *                              src_url;
    char *                              dst_url;
    int                                 url_len;
    globus_l_guc_src_dst_pair_t *       expanded_url_pair;
    globus_l_guc_url_info_t *           matched_src_urlinfo;
    char *                              matched_file;
    int                                 base_url_len;
    char *                              matched_dest_url;
    globus_result_t                     result;
    globus_bool_t                       dst_is_file;
    globus_bool_t                       matched_is_dir;
    int				        files_matched;  
    globus_l_guc_info_t *               guc_info;
    globus_l_guc_handle_t *             handle;
    globus_bool_t                       do_transfer = GLOBUS_TRUE;
    int                                 rc; 
    
    src_url = transfer_info->urls->src_url;
    dst_url = transfer_info->urls->dst_url;
    handle = transfer_info->handle;
    guc_info = transfer_info->guc_info;
        
    result = globus_gass_copy_glob_expand_url(
                &handle->gass_copy_handle,
                src_url,
                &handle->source_gass_copy_attr,
                globus_l_guc_entry_cb,
                transfer_info);
    
    if(result != GLOBUS_SUCCESS)
    {
        result = globus_error_put(
            globus_error_construct_error( 
                GLOBUS_NULL,              
                globus_error_peek(result),   
                0,     
                __FILE__,                     
                GLOBUS_NULL,                    
                __LINE__,                     
                "Unable to list url %s:",                 
                src_url));
        goto error_expand;  
    }
    
    url_len = strlen(dst_url);
    
    if(dst_url[url_len - 1] == '/')
    {
        dst_is_file = GLOBUS_FALSE;
    }
    else
    {
        dst_is_file = GLOBUS_TRUE;
    }

    if(guc_info->sync)
    {
        globus_gass_copy_glob_stat_t    stat_info;
        
        result = globus_gass_copy_stat(
            &handle->gass_copy_handle,
            dst_url,
            &handle->dest_gass_copy_attr,
            &stat_info);
        if(result == GLOBUS_SUCCESS)
        {
            if(stat_info.type == GLOBUS_GASS_COPY_GLOB_ENTRY_DIR)
            {
                char *                  tmp_dst;

                tmp_dst = globus_libc_strdup(dst_url);
                
                globus_mutex_lock(&g_monitor.mutex);
                rc = globus_hashtable_insert(
                    &guc_info->mkdir_hash,
                    tmp_dst,
                    tmp_dst);
                globus_mutex_unlock(&g_monitor.mutex);
                if(rc != 0)
                {
                    globus_free(tmp_dst);
                }

                result = globus_gass_copy_glob_expand_url(
                    &handle->gass_copy_handle,
                    dst_url,
                    &handle->dest_gass_copy_attr,
                    globus_l_guc_dest_entry_cb,
                    transfer_info);
                if(result != GLOBUS_SUCCESS && !g_continue)
                {
                    /* we know it exists, so we can fail on this error */
                    result = globus_error_put(
                        globus_error_construct_error(
                            GLOBUS_NULL,
                            globus_error_peek(result),
                            0,
                            __FILE__,
                            GLOBUS_NULL,
                            __LINE__,
                            "Unable to list destination directory for sync: %s",
                            dst_url));
                    goto error_expand;
                }
            }
            else
            {
                globus_l_guc_dest_entry_cb(
                    dst_url, &stat_info, transfer_info);
            }
            
            if(stat_info.symlink_target)
            {
                globus_free(stat_info.symlink_target);
            }
            if(stat_info.unique_id)
            {
                globus_free(stat_info.unique_id);
            }
        }
        else if(!g_continue)
        {
            char *                      tmp_errstr;
            globus_object_t *           tmp_err;
            char *                      tmp_ptr;
            
            tmp_err = globus_error_peek(result);
            tmp_errstr = globus_error_print_friendly(tmp_err);
            for(tmp_ptr = tmp_errstr; *tmp_ptr != '\0'; tmp_ptr++)
            {
                *tmp_ptr = tolower(*tmp_ptr);
            }

            rc = globus_error_ftp_error_get_code(tmp_err);
            /* if error code is not 5xx, or is 5xx and the error
             * is not "No such file or directory" or "File not found", fail.
             * ugly, but we can't rely on error codes. */
            if((rc > 0 && rc / 100 != 5) ||
                (strstr(tmp_errstr, "no such file") == NULL &&
                strstr(tmp_errstr, "file not found") == NULL))
            {
                globus_free(tmp_errstr);
                result = globus_error_put(
                    globus_error_construct_error(
                        GLOBUS_NULL,
                        tmp_err,
                        0,
                        __FILE__,
                        GLOBUS_NULL,
                        __LINE__,
                        "Unable to check destination url for sync: %s",
                        dst_url));
                goto error_expand;
            }
            globus_free(tmp_errstr);
        }
    }
    
    files_matched = 0;
    base_url_len = strrchr(src_url, '/') - src_url + 1;
                                 
    while(!globus_fifo_empty(&transfer_info->matched_url_list))
    {
        matched_src_urlinfo = (globus_l_guc_url_info_t *) 
            globus_fifo_dequeue(&transfer_info->matched_url_list);
    
        matched_file = matched_src_urlinfo->url + base_url_len;
    
        matched_is_dir = GLOBUS_FALSE;
        if(matched_src_urlinfo->url[strlen(matched_src_urlinfo->url) - 1] == '/')
        {
            matched_is_dir = GLOBUS_TRUE;
            if(!guc_info->recurse)
            {
                globus_l_guc_url_info_free(matched_src_urlinfo);
                continue;
            }
            else if(dst_is_file)
            {
                globus_l_guc_url_info_free(matched_src_urlinfo);
                goto error_too_many_matches;
            }
        }
            
        if(dst_is_file && ++files_matched > 1)
        {
            globus_l_guc_url_info_free(matched_src_urlinfo);
            goto error_too_many_matches;
        }

        if(dst_is_file)
        {
            matched_dest_url = globus_libc_strdup(dst_url);
        }
        else 
        {        
            matched_dest_url = (char *) globus_malloc(
                (url_len + strlen(matched_file) + 1) * sizeof(char));
            
            sprintf(matched_dest_url, "%s%s", dst_url, matched_file);
        }
        if(guc_info->sync)
        {
            result = globus_l_guc_check_sync(
                guc_info, handle, matched_src_urlinfo, matched_dest_url, &do_transfer);
            if(result != GLOBUS_SUCCESS && !g_continue)
            {
                goto error_expand;
            }
        }
        
        if(do_transfer)
        {

            /*
             * See if a post transfer checksum comparison has been requested
             */   

            if(guc_info->comp_checksum) 
            {
                if(matched_src_urlinfo->checksum == GLOBUS_NULL)
                {
                    /*
                     * If a checksum hasn't been taken on the source yet, take it now.
                     */
    
                    globus_l_guc_cksm_info_t    src_cksm_info;
                    globus_l_guc_monitor_t      cksm_monitor;
        
                    globus_mutex_init(&cksm_monitor.mutex, NULL);
                    globus_cond_init(&cksm_monitor.cond, NULL);
        
                    matched_src_urlinfo->checksum = malloc(CKSM_SIZE);
                    src_cksm_info.urlinfo = matched_src_urlinfo;
                    src_cksm_info.done = GLOBUS_FALSE;
                    src_cksm_info.monitor = &cksm_monitor;
                    src_cksm_info.error = NULL;
        
                    result = globus_gass_copy_cksm_async(
                                            &handle->gass_copy_handle,
                                            matched_src_urlinfo->url,
                                            &handle->source_gass_copy_attr,
                                            0,
                                            -1,
                                            guc_info->checksum_algo,
                                            matched_src_urlinfo->checksum,
                                            globus_l_guc_cksm_cb,
                                            &src_cksm_info);
                    if(result != GLOBUS_SUCCESS)
                    {
                        globus_free(matched_src_urlinfo->checksum);
                        matched_src_urlinfo->checksum = GLOBUS_NULL;
                        goto error_checksum;
                    }
                    globus_mutex_lock(&cksm_monitor.mutex);
                    while(!src_cksm_info.done)
                    {
                        globus_cond_wait(
                            &cksm_monitor.cond, &cksm_monitor.mutex);
                    }
                    globus_mutex_unlock(&cksm_monitor.mutex);
                    
                    if(src_cksm_info.error)
                    {
                        result = globus_error_put(src_cksm_info.error);
                        goto error_checksum;
                    }
                }
            }
    
            if(guc_info->create_dest && !guc_info->dump_only_file)
            {
                result = globus_l_guc_create_dir(
                    matched_dest_url, handle, guc_info);
                if(result != GLOBUS_SUCCESS)
                {
                    goto error_mkdir;
                }
            }
            
            if(guc_info->dump_only_fp)
            {
                globus_l_guc_print_url_line(
                    guc_info->dump_only_fp,
                    matched_src_urlinfo->url,
                    matched_dest_url,
                    transfer_info->urls->offset,
                    transfer_info->urls->length,
                    matched_src_urlinfo,
                    matched_is_dir ? "#" : NULL);
            }
            
            expanded_url_pair = (globus_l_guc_src_dst_pair_t *)
                    globus_malloc(sizeof(globus_l_guc_src_dst_pair_t));

            expanded_url_pair->src_url = matched_src_urlinfo->url;
            expanded_url_pair->dst_url = matched_dest_url;
            expanded_url_pair->offset = transfer_info->urls->offset;
            expanded_url_pair->length = transfer_info->urls->length;
            expanded_url_pair->src_info = matched_src_urlinfo;
            
            matched_src_urlinfo->url = NULL;
                
            if(!guc_info->dump_only_fp || matched_is_dir)
            {
                globus_l_guc_enqueue_pair(
                    &guc_info->expanded_url_list, 
                    expanded_url_pair);
            }
            else
            {
                globus_l_guc_url_pair_free(expanded_url_pair);
            }
        }
        else
        {
            globus_l_guc_url_info_free(matched_src_urlinfo);
            globus_free(matched_dest_url);
        }
    }
    
    return GLOBUS_SUCCESS;
    
error_too_many_matches:
    result = globus_error_put(
        globus_error_construct_string(
            GLOBUS_NULL,
            GLOBUS_NULL,
            _GASCSL("Multiple source urls must be transferred to a directory "
            "destination url:\n%s\n"),
            dst_url));                    
error_mkdir:
error_expand:
error_checksum:
    return result;                
}

static
void 
globus_l_guc_pipeline(
    globus_ftp_client_handle_t *                ftp_handle,
    char **                                     source_url,
    char **                                     dest_url,
    void *                                      user_arg)
{
    globus_l_guc_handle_t *                     handle;
    globus_l_guc_info_t *                       guc_info;
    globus_l_guc_src_dst_pair_t *               pair;
    globus_bool_t                               none = GLOBUS_FALSE;
    
    handle = (globus_l_guc_handle_t *) user_arg;
    guc_info = handle->guc_info;
    
    *source_url = NULL;
    *dest_url = NULL;
    
    globus_mutex_lock(&g_monitor.mutex);

    globus_l_guc_url_pair_free(handle->pipeline_free_pair);        
    handle->pipeline_free_pair = NULL;
    
    if(!g_monitor.done && !globus_fifo_empty(&guc_info->expanded_url_list))
    {
        pair = (globus_l_guc_src_dst_pair_t *)
            globus_fifo_peek(&guc_info->expanded_url_list);
        
        if(pair->dst_url[strlen(pair->dst_url) - 1] == '/')
        {
            none = GLOBUS_TRUE;
        }
        
        if(strncmp(pair->src_url, "file:/", 5) == 0 || 
            strncmp(pair->src_url, "http", 4) == 0 ||
            strncmp(pair->dst_url, "file:/", 5) == 0 || 
            strncmp(pair->dst_url, "http", 4) == 0)       
        {
            none = GLOBUS_TRUE;
        }
        
        if(!none)
        {
            char                        idstr[16];
            
            sprintf(idstr, "%u", handle->pipeline_id++);
            *source_url = pair->src_url;
            *dest_url = pair->dst_url;
            
            handle->pipeline_free_pair = globus_fifo_dequeue(
                &guc_info->expanded_url_list);
                
            if(!g_quiet_flag)
            {
                globus_libc_fprintf(stdout,
                    "Pipelining:\n  %s\n  %s\n", *source_url, *dest_url);
            }
        }
    }

    globus_mutex_unlock(&g_monitor.mutex);

        
}




static
int
globus_l_guc_init_gass_copy_handle(
    globus_gass_copy_handle_t *                     gass_copy_handle,
    globus_l_guc_info_t *                           guc_info,
    int                                             id)
{
    globus_ftp_client_plugin_t                      plugin;
    globus_list_t *                                 list;
    int                                             rc;
    globus_extension_handle_t                       ext_handle;
    globus_guc_client_plugin_funcs_t *              funcs;
    char *                                          plugin_name;
    char *                                          plugin_arg;
    char *                                          tmp_s;
    char *                                          func_name;
    globus_ftp_client_handleattr_t                  ftp_handleattr;
    globus_result_t                                 result;
    globus_ftp_client_plugin_t                      debug_plugin;
    globus_ftp_client_plugin_t                      restart_plugin;
    globus_reltime_t                                interval;
    globus_abstime_t                                timeout;
    globus_abstime_t *                              timeout_p = GLOBUS_NULL;
    globus_gass_copy_handleattr_t                   gass_copy_handleattr;
    char *                                          ver_str;


    globus_gass_copy_handleattr_init(&gass_copy_handleattr);

    result = globus_ftp_client_handleattr_init(&ftp_handleattr);
    if(result != GLOBUS_SUCCESS)
    {
        fprintf(stderr, _GASCSL("Error: Unable to init ftp handle attr %s\n"),
            globus_error_print_friendly(globus_error_peek(result)));

        return -1;
    }
    
    ver_str = globus_common_create_string(
        "%d.%d (%s, %d-%d) [%s]",
        local_version.major,
        local_version.minor,
        build_flavor,
        local_version.timestamp,
        local_version.branch_id,
        toolkit_id);
    globus_ftp_client_handleattr_set_clientinfo(
        &ftp_handleattr, "globus-url-copy", ver_str, NULL);
    if(ver_str)
    {
        globus_free(ver_str);
    }

    globus_ftp_client_handleattr_set_cache_all(&ftp_handleattr, GLOBUS_TRUE);

    memset(&ext_handle, '\0', sizeof(globus_extension_handle_t));
    for(list = g_client_lib_plugin_list;
        !globus_list_empty(list);
        list = globus_list_rest(list))
    {
        plugin_name = globus_list_first(list);
        plugin_name = globus_libc_strdup(plugin_name);
        plugin_arg = NULL;
        tmp_s = strchr(plugin_name, ':');
        if(tmp_s != NULL)
        {
            *tmp_s = '\0';
            plugin_arg = tmp_s + 1;
        }
        rc = globus_extension_activate(plugin_name);
        if(rc != 0)
        {
            fprintf(stderr, "Failed to load extension %s\n", plugin_name);
        }
    
        func_name = globus_common_create_string("%s_funcs", plugin_name);

        funcs = (globus_guc_client_plugin_funcs_t *) globus_extension_lookup(
            &ext_handle, &globus_guc_client_plugin_registry, func_name);
        if(funcs == NULL)
        {
            fprintf(stderr, "Failed to find extension structure %s.\n",
                func_name);
        }
        else
        {
            result = funcs->init_func(&plugin, plugin_arg);
            if(result != GLOBUS_SUCCESS)
            {
                fprintf(stderr,
                    _GASCSL("Error: Unable to init extension plugin %s\n"),
                    globus_error_print_friendly(globus_error_peek(result)));
            }

            result = globus_ftp_client_handleattr_add_plugin(
                &ftp_handleattr,
                &plugin);
            if(result != GLOBUS_SUCCESS)
            {
                fprintf(stderr,
                    _GASCSL("Error: Unable to register extension plugin %s\n"),
                    globus_error_print_friendly(globus_error_peek(result)));
            }
        }
        globus_free(plugin_name);
    }

    if(g_use_debug)
    {
        result = globus_ftp_client_debug_plugin_init(
            &debug_plugin,
            stderr,
            "debug");
        if(result != GLOBUS_SUCCESS)
        {
            fprintf(stderr, _GASCSL("Error: Unable to init debug plugin %s\n"),
                globus_error_print_friendly(globus_error_peek(result)));

            return -1;
        }

        result = globus_ftp_client_handleattr_add_plugin(
            &ftp_handleattr,
            &debug_plugin);
        if(result != GLOBUS_SUCCESS)
        {
            fprintf(stderr, _GASCSL("Error: Unable to register debug plugin %s\n"),
                globus_error_print_friendly(globus_error_peek(result)));

            return -1;
        }
    }

    if(g_use_restart)
    {
        GlobusTimeReltimeSet(interval, guc_info->restart_interval, 0);
        
        if(guc_info->restart_timeout)
        {
            GlobusTimeAbstimeSet(timeout, guc_info->restart_timeout, 0);
            timeout_p = &timeout;
        }
            
        result = globus_ftp_client_restart_plugin_init(
            &restart_plugin,
            guc_info->restart_retries, /* retry times 0=forever */
            &interval, /* time between tries 0=exponential backoff */
            timeout_p); /* absolute timeout NULL=inifinte */
        if(result != GLOBUS_SUCCESS)
        {
            fprintf(stderr, _GASCSL("Error: Unable to init debug plugin %s\n"),
                globus_error_print_friendly(globus_error_peek(result)));

            return -1;
        }
        
        if(guc_info->stall_timeout)
        {
            result = globus_ftp_client_restart_plugin_set_stall_timeout(
                &restart_plugin, guc_info->stall_timeout);
            if(result != GLOBUS_SUCCESS)
            {
                fprintf(stderr, _GASCSL("Error: Unable to init debug plugin %s\n"),
                    globus_error_print_friendly(globus_error_peek(result)));
    
                return -1;
            }
        }

        result = globus_ftp_client_handleattr_add_plugin(
            &ftp_handleattr,
            &restart_plugin);
        if(result != GLOBUS_SUCCESS)
        {
            fprintf(stderr, _GASCSL("Error: Unable to register restart plugin %s\n"),
                globus_error_print_friendly(globus_error_peek(result)));

            return -1;
        }
    }
    else if(guc_info->stall_timeout)
    {        
        GlobusTimeAbstimeSet(timeout, 0, 1);
        timeout_p = &timeout;

        result = globus_ftp_client_restart_plugin_init(
            &restart_plugin, 0, NULL, timeout_p); 
        if(result != GLOBUS_SUCCESS)
        {
            fprintf(stderr, _GASCSL("Error: Unable to init debug plugin %s\n"),
                globus_error_print_friendly(globus_error_peek(result)));

            return -1;
        }
        
        result = globus_ftp_client_restart_plugin_set_stall_timeout(
            &restart_plugin, guc_info->stall_timeout);
        if(result != GLOBUS_SUCCESS)
        {
            fprintf(stderr, _GASCSL("Error: Unable to init debug plugin %s\n"),
                globus_error_print_friendly(globus_error_peek(result)));

            return -1;
        }

        result = globus_ftp_client_handleattr_add_plugin(
            &ftp_handleattr,
            &restart_plugin);
        if(result != GLOBUS_SUCCESS)
        {
            fprintf(stderr, _GASCSL("Error: Unable to register restart plugin %s\n"),
                globus_error_print_friendly(globus_error_peek(result)));

            return -1;
        }
    }        

    if(guc_info->rfc1738)
    {
        result = globus_ftp_client_handleattr_set_rfc1738_url(
            &ftp_handleattr, GLOBUS_TRUE);
        if(result != GLOBUS_SUCCESS)
        {
            fprintf(stderr, _GASCSL("Error: Unable to set rfc1738 support %s\n"),
                globus_error_print_friendly(globus_error_peek(result)));

            return -1;
        }
    }        

    result = globus_ftp_client_handleattr_set_gridftp2(
        &ftp_handleattr, guc_info->gridftp2);
    if(result != GLOBUS_SUCCESS)
    {
        fprintf(stderr, _GASCSL("Error: Unable to enable gridftp2 support %s\n"),
            globus_error_print_friendly(globus_error_peek(result)));

        return -1;
    }

    if(guc_info->pipeline)
    {
        char                            idstr[16];
        sprintf(idstr, "%02d", id);
        result = globus_ftp_client_handleattr_set_pipeline(
            &ftp_handleattr, guc_info->pipelineq, globus_l_guc_pipeline, guc_info->handles[id]);
        if(result != GLOBUS_SUCCESS)
        {
            fprintf(stderr, _GASCSL("Error: Unable to enable pipeline %s\n"),
                globus_error_print_friendly(globus_error_peek(result)));

            return -1;
        }
    }        
    
    globus_gass_copy_handleattr_set_ftp_attr(
        &gass_copy_handleattr, &ftp_handleattr);
    globus_gass_copy_handle_init(gass_copy_handle, &gass_copy_handleattr);

    if(guc_info->block_size > 0)
    {
        globus_gass_copy_set_buffer_length(gass_copy_handle, 
            guc_info->block_size);
    }

    if(guc_info->no_3pt)
    {
        globus_gass_copy_set_no_third_party_transfers(gass_copy_handle,
            GLOBUS_TRUE);
    }
    
    if(!guc_info->pipeline)
    {
        globus_gass_copy_set_allocate(gass_copy_handle, guc_info->allo);
    }
    
    if(guc_info->sync)
    {
        globus_gass_copy_set_stat_on_expand(gass_copy_handle, GLOBUS_TRUE);
    }
    
    if(g_verbose_flag)
    {
        result = globus_gass_copy_register_performance_cb(
            gass_copy_handle,
            globus_l_gass_copy_performance_cb,
            (void *) (intptr_t) id);

        if(result != GLOBUS_SUCCESS)
        {
            fprintf(stderr, 
                _GASCSL("Error: Unable to register performance handler %s\n"),
                globus_error_print_friendly(globus_error_peek(result)));

            fprintf(stderr, "%s", _GASCSL("Continuing without performance info\n"));
        }
    }


    if(g_use_restart || guc_info->stall_timeout)
    {
        globus_ftp_client_handleattr_remove_plugin(
            &ftp_handleattr,
            &restart_plugin);
        globus_ftp_client_restart_plugin_destroy(&restart_plugin);
    }
    if(g_use_debug)
    {
        globus_ftp_client_handleattr_remove_plugin(
            &ftp_handleattr,
            &debug_plugin);
        globus_ftp_client_debug_plugin_destroy(&debug_plugin);
    }
    globus_gass_copy_handleattr_destroy(&gass_copy_handleattr);
    globus_ftp_client_handleattr_destroy(&ftp_handleattr);

    return 0;
}



/*
 *  since i can't seem to find away to get a list of schemes that
 *  gass supports this will need to be called for each url
 */

static
int
globus_l_guc_gass_attr_init(
    globus_gass_copy_attr_t *               gass_copy_attr,
    globus_gass_transfer_requestattr_t *    gass_t_attr,
    globus_ftp_client_operationattr_t *     ftp_attr,
    globus_l_guc_info_t *                   guc_info,
    char *                                  url,
    globus_bool_t                           src,
    globus_bool_t                           twoparty)
{
    globus_url_t                        url_info;
    globus_gass_copy_url_mode_t         url_mode;
    globus_ftp_control_tcpbuffer_t      tcp_buffer;
    globus_ftp_control_parallelism_t    parallelism;
    globus_ftp_control_dcau_t           dcau;
    globus_ftp_control_layout_t         layout;
    const char *                        gsi_driver_str = NULL;
    char *                              subject;
    char *                              module_name;
    char *                              module_args;
    char *                              authz_assert;
    char *                              disk_str = NULL;
    globus_bool_t                       cache_authz_assert;
    char *                              tmp_net_str = NULL;
    char *                              tmp_disk_str = NULL;
    gss_cred_id_t                       cred = GSS_C_NO_CREDENTIAL;
    char *                              dcau_subj = NULL;

    if(src)
    {                  
        subject = guc_info->source_subject;
        module_name = guc_info->src_module_name;
        module_args = guc_info->src_module_args;
        authz_assert = guc_info->src_authz_assert,
        cache_authz_assert = guc_info->cache_src_authz_assert;
        cred = guc_info->src_cred;
        if(guc_info->src_cred_subj && guc_info->dst_cred_subj && 
            strcmp(guc_info->src_cred_subj, guc_info->dst_cred_subj))
        {
            dcau_subj = guc_info->dst_cred_subj;
        }
    }
    else
    {
        subject = guc_info->dest_subject;
        module_name = guc_info->dst_module_name;
        module_args = guc_info->dst_module_args;
        authz_assert = guc_info->dst_authz_assert,
        cache_authz_assert = guc_info->cache_dst_authz_assert;
        cred = guc_info->dst_cred;
        if(guc_info->src_cred_subj && guc_info->dst_cred_subj && 
            strcmp(guc_info->src_cred_subj, guc_info->dst_cred_subj))
        {
            dcau_subj = guc_info->src_cred_subj;
        }
    }
    
    globus_url_parse(url, &url_info);
    globus_gass_copy_get_url_mode(url, &url_mode);
    /*
     *  setup the ftp attr
     */
    if(url_mode == GLOBUS_GASS_COPY_URL_MODE_FTP)
    {     
        globus_ftp_client_operationattr_init(ftp_attr);

        if(guc_info->tcp_buffer_size > 0)
        {
            tcp_buffer.mode = GLOBUS_FTP_CONTROL_TCPBUFFER_FIXED;
            tcp_buffer.fixed.size = guc_info->tcp_buffer_size;
            globus_ftp_client_operationattr_set_tcp_buffer(
                ftp_attr,
                &tcp_buffer);
        }

        if(guc_info->num_streams >= 1)
        {
            globus_ftp_client_operationattr_set_mode(
                ftp_attr,
                GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK);

            parallelism.mode = GLOBUS_FTP_CONTROL_PARALLELISM_FIXED;
            parallelism.fixed.size = guc_info->num_streams;
            globus_ftp_client_operationattr_set_parallelism(
                ftp_attr,
                &parallelism); 
            
            globus_ftp_client_operationattr_set_list_uses_data_mode(
                ftp_attr,
                guc_info->list_uses_data_mode);
	}

	if(guc_info->striped)
	{
	    memset(&layout, '\0', sizeof(globus_ftp_control_layout_t));
            switch(guc_info->stripe_bs)
            {
                case -1:
                    layout.mode = 
                        GLOBUS_FTP_CONTROL_STRIPING_NONE;
                    break;
                case 0:
                    layout.mode = 
                        GLOBUS_FTP_CONTROL_STRIPING_PARTITIONED;
                    break;
                default:
                    layout.mode = 
                        GLOBUS_FTP_CONTROL_STRIPING_BLOCKED_ROUND_ROBIN;
                    layout.round_robin.block_size = guc_info->stripe_bs;
            }
            globus_ftp_client_operationattr_set_mode(
                        ftp_attr,
                        GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK);
            globus_ftp_client_operationattr_set_striped(ftp_attr, GLOBUS_TRUE);    
            globus_ftp_client_operationattr_set_layout(ftp_attr, &layout);
	}

	if(guc_info->delayed_pasv)
	{
            globus_ftp_client_operationattr_set_delayed_pasv(
                ftp_attr, GLOBUS_TRUE);    
	}

	if(guc_info->ipv6)
	{
            globus_ftp_client_operationattr_set_allow_ipv6(
                ftp_attr, GLOBUS_TRUE);    
	}

        if(module_name != NULL)
        {
            globus_ftp_client_operationattr_set_storage_module(
                ftp_attr, module_name, module_args);
        }
        if(authz_assert != NULL)
        {
            globus_ftp_client_operationattr_set_authz_assert(
                ftp_attr, authz_assert, cache_authz_assert);
        }

        if(url_info.scheme_type == GLOBUS_URL_SCHEME_GSIFTP &&
            (subject  ||
            url_info.user ||
            url_info.password ||
            cred != GSS_C_NO_CREDENTIAL))
        {
            globus_ftp_client_operationattr_set_authorization(
                ftp_attr,
                cred,
                url_info.user,
                url_info.password,
                NULL,
                subject);
        }
        else if((url_info.scheme_type == GLOBUS_URL_SCHEME_FTP ||
            url_info.scheme_type == GLOBUS_URL_SCHEME_SSHFTP) &&
            url_info.user)
        {
            globus_ftp_client_operationattr_set_authorization(
                ftp_attr,
                GSS_C_NO_CREDENTIAL,
                url_info.user,
                url_info.password,
                NULL,
                NULL);
        }

        if(guc_info->data_cred != GSS_C_NO_CREDENTIAL)
        {
            globus_ftp_client_operationattr_set_data_security(
                ftp_attr, 'P', guc_info->data_cred);
        }
        else if(auto_data.length > 0)
        {
            globus_ftp_client_operationattr_set_data_security(
                ftp_attr, 'p', &auto_data);            
        }
        else if(url_info.scheme_type == GLOBUS_URL_SCHEME_SSHFTP && 
            (guc_info->data_private || guc_info->data_safe))
        {
            fprintf(stderr, "\nERROR: Data protection with sshftp requires a credential. Try '-data-cred auto'.\n\n");
            exit(EXIT_FAILURE);
        }
            
        if(guc_info->no_dcau)
        {
            dcau.mode = GLOBUS_FTP_CONTROL_DCAU_NONE;
            globus_ftp_client_operationattr_set_dcau(
                ftp_attr,
                &dcau);
        }
        else
        {
            if(!twoparty && dcau_subj)
            {
                dcau.mode = GLOBUS_FTP_CONTROL_DCAU_SUBJECT;
                dcau.subject.subject = dcau_subj;
                globus_ftp_client_operationattr_set_dcau(
                    ftp_attr,
                    &dcau);
            }
            
            if(url_info.scheme_type == GLOBUS_URL_SCHEME_GSIFTP)
            {
                gsi_driver_str = "gsi";
            }
        }
        
        if(guc_info->data_private)
        {
            globus_ftp_client_operationattr_set_data_protection(
                ftp_attr,
                GLOBUS_FTP_CONTROL_PROTECTION_PRIVATE);
        }
        else if(guc_info->data_safe)
        {
            globus_ftp_client_operationattr_set_data_protection(
                ftp_attr,
                GLOBUS_FTP_CONTROL_PROTECTION_SAFE);
        }
                
        if(src)
        {
            tmp_net_str = guc_info->src_net_stack_str;
            tmp_disk_str = guc_info->src_disk_stack_str;
        }
        else
        {
            tmp_net_str = guc_info->dst_net_stack_str;
            tmp_disk_str = guc_info->dst_disk_stack_str;
        }
        if(tmp_net_str != NULL && gsi_driver_str != NULL)
        {
            char *  tmp_stack;
            char *  second_driver;
            char * gsi_ptr;

            if ((strstr(tmp_net_str, "gsi,") != tmp_net_str) &&
                (strstr(tmp_net_str, ",gsi,") == NULL) &&
                (((gsi_ptr = strstr(tmp_net_str, ",gsi")) == NULL) ||
                    (strlen(gsi_ptr) != 4)))
            {
                tmp_stack = malloc(strlen(tmp_net_str) + 5);
                if (tmp_stack == NULL)
                {
                    fprintf(stderr, "Malloc fail\n");
                    exit(EXIT_FAILURE);
                }
                second_driver = strchr(tmp_net_str, ',');
                if (second_driver)
                {
                    sprintf(tmp_stack, "%s", tmp_net_str);
                    strcpy(tmp_stack + (second_driver - tmp_net_str)+1,
                           gsi_driver_str);
                    strcat(tmp_stack, second_driver);
                }
                else
                {
                    sprintf(tmp_stack, "%s,%s", tmp_net_str, gsi_driver_str);
                }
            }
            else
            {
                tmp_stack = globus_common_create_string("%s", tmp_net_str);
            }
            globus_ftp_client_operationattr_set_net_stack(
                ftp_attr,
                tmp_stack);
            free(tmp_stack);
        }

        disk_str = globus_libc_strdup(tmp_disk_str);
        /* if we need to take on the multicast string */
        if(g_l_mc_fs_str != NULL && !src)
        {
            char *                      tmp_str;
            if(disk_str != NULL)
            {
                tmp_str = disk_str;

                disk_str = globus_common_create_string(
                    "%s,%s", g_l_mc_fs_str, tmp_str);

                globus_free(tmp_str);
            }
            else
            {
                disk_str = globus_common_create_string(
                    "file,%s", g_l_mc_fs_str);
            }
        }

        if(disk_str)
        {
            globus_ftp_client_operationattr_set_disk_stack(
                ftp_attr,
                disk_str);
            globus_free(disk_str);
        }

        globus_gass_copy_attr_set_ftp(gass_copy_attr, ftp_attr);
        
    }
    /*
     *  setup the gass copy attr
     */
    else if(url_mode == GLOBUS_GASS_COPY_URL_MODE_GASS)
    {
        globus_gass_transfer_requestattr_init(gass_t_attr, url_info.scheme);

        if(guc_info->options & GLOBUS_URL_COPY_ARG_ASCII)
        {
             globus_gass_transfer_requestattr_set_file_mode(
                  gass_t_attr,
                  GLOBUS_GASS_TRANSFER_FILE_MODE_TEXT);
        }
        else if(guc_info->options & GLOBUS_URL_COPY_ARG_BINARY)
        {
             globus_gass_transfer_requestattr_set_file_mode(
                gass_t_attr,
                GLOBUS_GASS_TRANSFER_FILE_MODE_BINARY);
        }

        if(subject)
        {
            globus_gass_transfer_secure_requestattr_set_authorization(
                gass_t_attr,
                GLOBUS_GASS_TRANSFER_AUTHORIZE_SUBJECT,
                subject);
        }

        globus_gass_copy_attr_set_gass(gass_copy_attr, gass_t_attr);
    }

    globus_url_destroy(&url_info);    
	
    return 0;
}



static
globus_io_handle_t *
globus_l_guc_get_io_handle(
    char *                                          url,
    int                                             std_fileno)
{
    globus_io_handle_t *                            io_handle;

    /*
     *  if not stdio
     */
    if(strcmp(url, "-") != 0)
    {
        return NULL;
    }
    
    io_handle =(globus_io_handle_t *)
        globus_libc_malloc(sizeof(globus_io_handle_t));

    /* convert stdin to be a globus_io_handle */
    globus_io_file_posix_convert(std_fileno, GLOBUS_NULL, io_handle);

    return io_handle;
}


static
void
globus_l_guc_url_pair_free(
    globus_l_guc_src_dst_pair_t *       url_pair)
{
    if(url_pair)
    {
        if(url_pair->src_url)
        {
            globus_free(url_pair->src_url);
        }
        if(url_pair->dst_url)
        {
            globus_free(url_pair->dst_url);
        }
        globus_l_guc_url_info_free(url_pair->src_info);
        globus_free(url_pair);
    }
}   

static
void
globus_l_guc_destroy_url_list(
    globus_fifo_t *                     url_list)
{
    globus_l_guc_src_dst_pair_t *       url_pair;

    while(!globus_fifo_empty(url_list))
    {
        url_pair = (globus_l_guc_src_dst_pair_t *)
                        globus_fifo_dequeue(url_list);
        globus_l_guc_url_pair_free(url_pair);
    }      
    globus_fifo_destroy(url_list);
}   

static 
void
globus_l_guc_hashtable_element_free(
    void *                              datum)
{
    char *                              string;
    
    string = (char *) datum;

    if(string != GLOBUS_NULL)
    { 
        globus_free(string);
    }
    
    return;
}

static 
void
globus_l_guc_url_info_free(
    void *                              datum)
{
    globus_l_guc_url_info_t *           info;
    
    info = (globus_l_guc_url_info_t *) datum;

    if(info != NULL)
    { 
        if(info->url)
        {
            globus_free(info->url);
        }
        if(info->checksum)
        {
            globus_free(info->checksum);
        }
        globus_free(info);
    }
    
    return;
}
