#ifndef _CERVER_VERSION_H_
#define _CERVER_VERSION_H_

#include "cerver/config.h"

#define CERVER_VERSION                  "1.6.3b-2"
#define CERVER_VERSION_NAME             "Beta 1.6.3b-2"
#define CERVER_VERSION_DATE			    "02/12/2020"
#define CERVER_VERSION_TIME			    "07:08 CST"
#define CERVER_VERSION_AUTHOR			"Erick Salas"

// print full cerver version information 
CERVER_EXPORT void cerver_version_print_full (void);

// print the version id
CERVER_EXPORT void cerver_version_print_version_id (void);

// print the version name
CERVER_EXPORT void cerver_version_print_version_name (void);

#endif