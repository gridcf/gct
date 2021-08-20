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

#ifndef GLOBUS_FTP_CLIENT_THROUGHPUT_NL_PLUGIN_H
#define GLOBUS_FTP_CLIENT_THROUGHPUT_NL_PLUGIN_H

/**
 * @file globus_ftp_client_throughput_nl_plugin.h
 * @brief GridFTP Netlogger Throughput Plugin
 */

/**
 * @defgroup globus_ftp_client_throughput_nl_plugin Netlogger Throughput Plugin
 * @ingroup globus_ftp_client_plugins
 * @brief Netlogger Throughput Plugin
 *
 * This plugin allows a user to easily use the throughput plugin to log
 * performance data vi Netlogger.
 *
 * The plugin will log the following Event Types with its coressponding info
 *
 * TransferPerfTotal : This event type will be sent every time a throughput
 *      plugin total callback is received.
 *
 * - URL.SOURCE   &lt;string&gt;  Source url of transfer
 * - URL.DEST     &lt;string&gt;  Dest url of transfer
 * - BYTES        &lt;int&gt;     Total bytes transferred thus far
 * - BW.CURRENT   &lt;float&gt;   Current (instantaneous) bandwidth
 * - BW.AVG       &lt;float&gt;   Average (instantaneous) bandwidth
 *
 * TransferPerfStripe  : This event type will be sent every time a throughput
 *      plugin stripe callback is received.
 *
 * - URL.SOURCE   &lt;string&gt;  Source url of transfer
 * - URL.DEST     &lt;string&gt;  Dest url of transfer
 * - INDEX        &lt;int&gt;     The stripe index the event applies to
 * - BYTES        &lt;int&gt;     Total bytes transferred thus far on this stripe
 * - BW.CURRENT   &lt;float&gt;   Current (instantaneous) bandwidth on this stripe
 * - BW.AVG       &lt;float&gt;   Average (instantaneous) bandwidth on this stripe
 *
 * TransferBegin  : This event type will be sent every time a throughput
 *      plugin begin callback is received.
 *
 * - URL.SOURCE   &lt;string&gt;  Source url of transfer
 * - URL.DEST     &lt;string&gt;  Dest url of transfer
 *
 * TransferEnd  : This event type will be sent every time a throughput
 *      plugin complete callback is received.
 *
 * - SUCCESS      &lt;bool&gt;    Completion status
 *
 */



#include "globus_ftp_client.h"
#include "globus_ftp_client_plugin.h"
#include "globus_ftp_client_throughput_plugin.h"
#include "NetLogger.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Module descriptor
 * @ingroup globus_ftp_client_throughput_nl_plugin
 */
#define GLOBUS_FTP_CLIENT_THROUGHPUT_NL_PLUGIN_MODULE (&globus_i_ftp_client_throughput_nl_plugin_module)

extern
globus_module_descriptor_t globus_i_ftp_client_throughput_nl_plugin_module;

globus_result_t
globus_ftp_client_throughput_nl_plugin_init(
    globus_ftp_client_plugin_t *			plugin,
    const char *                                        nl_url,
    const char *                                        prog_name,
    const char *                                        opaque_string);

globus_result_t
globus_ftp_client_throughput_nl_plugin_init_with_handle(
    globus_ftp_client_plugin_t *			plugin,
    NLhandle *                                          nl_handle,
    const char *                                        opaque_string);

globus_result_t
globus_ftp_client_throughput_nl_plugin_destroy(
    globus_ftp_client_plugin_t *			plugin);

globus_result_t
globus_ftp_client_throughput_nl_plugin_set_callbacks(
    globus_ftp_client_plugin_t *                        plugin,
    globus_ftp_client_throughput_plugin_begin_cb_t      begin_cb,
    globus_ftp_client_throughput_plugin_stripe_cb_t     per_stripe_cb,
    globus_ftp_client_throughput_plugin_total_cb_t      total_cb,
    globus_ftp_client_throughput_plugin_complete_cb_t   complete_cb,
    void *                                              user_specific);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_FTP_CLIENT_THROUGHPUT_NL_PLUGIN_H */
