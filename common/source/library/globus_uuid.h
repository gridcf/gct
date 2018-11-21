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
 * @file globus_uuid.h
 * @brief Globus UUID Generator
 */

/**
 * @defgroup globus_uuid Globus UUID Generator
 * @ingroup globus_common
 * @brief UUID Generator
 */
#ifndef GLOBUS_UUID_H
#define GLOBUS_UUID_H

#include "globus_common_include.h"
#include "globus_libc.h"


#ifdef __cplusplus
extern "C" {
#endif

#define GLOBUS_UUID_TEXTLEN 36

#define GLOBUS_UUID_VERSION(uuid) ((uuid).binary.bytes[6] >> 4)
#define GLOBUS_UUID_VERSION_TIME 1
#define GLOBUS_UUID_VERSION_DCE 2
#define GLOBUS_UUID_VERSION_NAME 3
#define GLOBUS_UUID_VERSION_RANDOM 4

#define GLOBUS_UUID_MATCH(u1, u2)                                           \
    (memcmp((u1).binary.bytes, (u2).binary.bytes, 16) == 0)

typedef struct
{
    uint32_t                    time_low;
    uint16_t                    time_mid;
    uint16_t                    time_hi_and_version;
    uint8_t                     clock_seq_hi_and_reserved;
    uint8_t                     clock_seq_low;
    uint8_t                     node[6];
} globus_uuid_fields_t;

typedef struct
{
    union
    {
        uint8_t                         bytes[16];
        /* all multibyte fields in network byte order */
        globus_uuid_fields_t            fields;
    } binary;
    
    char                                text[GLOBUS_UUID_TEXTLEN + 1];
} globus_uuid_t;

/**
 * @brief Create UUID
 * @ingroup globus_uuid
 * Creates a time based, Leach-Salz variant uuid, using the mac address when
 * available.
 */
int
globus_uuid_create(
    globus_uuid_t *                     uuid);

/**
 * @brief Import a UUID
 * @ingroup globus_uuid
 * Import a string in the text UUID format into a globus_uuid_t value.
 * @param uuid
 *    UUID to modify
 * @param str
 *     A string at least GLOBUS_UUID_TEXTLEN long and be in the 
 *     format like 1b4e28ba-2fa1-11d2-883f-b9a761bde3fb
 */
int
globus_uuid_import(
    globus_uuid_t *                     uuid,
    const char *                        str);

/**
 * @brief Extract fields from UUID
 * @ingroup globus_uuid
 * Copy the fields from uuid to uuid_fields in host byte order
 * @param uuid
 *     Pointer to a uuid.
 * @param uuid_fields
 *     Pointer to uuid fields.
 */
int
globus_uuid_fields(
    globus_uuid_t *                     uuid,
    globus_uuid_fields_t *              uuid_fields);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_UUID_H */
