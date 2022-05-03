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

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
/*
 * OpenSSL version 3.0 and up no longer has FIPS_mode().
 * Making a replacement function is not feasible since FIPS would need to be
 * initialized differently in any case.
 * See https://www.openssl.org/docs/manmaster/man7/fips_module.html for details
 */
# define FIPS_mode() 0
#endif