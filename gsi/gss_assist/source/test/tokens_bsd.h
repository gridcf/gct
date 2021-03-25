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

/**
 * @file tokens_bsd.h
 * @author Sam Lang, Sam Meder
 */

#include "globus_common.h"
#include "globus_i_gss_assist.h"

int
token_bsd_get(
    void *                              arg,
    void **                             bufp,
    size_t *                            sizep);

int
token_bsd_send(
    void *                              arg,
    void *                              buf,
    size_t                              size);


int
token_bsd_send_ex(
    void *                              exp,
    void *                              buf,
    size_t                              size);
