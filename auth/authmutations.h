/* CPP definitions for MiG processing of auth.defs for auth server.  */

#define AUTH_INTRAN authhandle_t auth_port_to_handle (auth_t)
#define AUTH_DESTRUCTOR ports_port_deref (authhandle_t)

#define AUTH_IMPORTS import "auth_mig.h";
