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
 * @file globus_release.h
 * @brief Release Information
 */

#ifndef GLOBUS_RELEASE_H
#define GLOBUS_RELEASE_H

/* I have made these routines as macros                    */
/* since you have to include the header file, anyways.     */
/* The only reason why they should be functions is if      */
/* you expect the linker to fill in the appropriate values */
/* but then the user needs to include the prototypes.      */
#define  globus_release_major()    GLOBUS_RELEASE_MAJOR
#define  globus_release_minor()    GLOBUS_RELEASE_MINOR
#define  globus_release_patch()    GLOBUS_RELEASE_PATCH
#define  globus_release_beta()     GLOBUS_RELEASE_BETA

#ifdef GLOBUS_RELEASE_STRING
#define  globus_release_string()   GLOBUS_RELEASE_STRING
#else
#define  globus_release_string()   ""
#endif
#endif /* GLOBUS_DEBUG_H */
