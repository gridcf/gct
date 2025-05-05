/*
 * Copyright 2021- Grid Community Forum
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

#ifndef FIPS_MODE_REPLACEMENT_H
#define FIPS_MODE_REPLACEMENT_H


#if OPENSSL_VERSION_NUMBER >= 0x30000000L
/*
 * OpenSSL versions 3.0 and up no longer have FIPS_mode(). To support both
 * OpenSSL 3.x and older versions for other OSes, we use the replacement
 * function as shipped by Fedora/RHEL/CentOS in their OpenSSL 3.x packages.
 */
# ifndef FIPS_mode
#  define FIPS_mode() EVP_default_properties_is_fips_enabled(NULL)
# endif /* FIPS_mode */
#elif /* openssl */
#include <openssl/fips.h>
#endif /* openssl */

#endif /* FIPS_MODE_REPLACEMENT_H */
