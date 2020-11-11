//
//  pegasus_server.h
//  PegasusEar
//
//  Created by Kevin Colley on 11/5/2020.
//

#ifndef PEG_PEGASUS_SERVER_H
#define PEG_PEGASUS_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <dlfcn.h>
#include "pegasus_ear/plugin.h"

/*!
 * @brief Run the PEGASUS server using the provided plugin.
 * 
 * @param plugin_init Function pointer to the plugin initialization function
 * 
 * @return True on success, or false on failure
 */
bool PegasusServer_serveWithPlugin(PegPlugin_Init_func* plugin_init);


/*!
 * @brief Dynamically load libpegasus_server.so and call the PegasusServer_serveWithPlugin() function.
 * 
 * @param plugin_init Function pointer to the plugin initialization function
 * 
 * @return True on success, or false on failure
 */
static inline bool PegasusServer_dlopenAndServeWithPlugin(PegPlugin_Init_func* plugin_init) {
	void* srv_handle = dlopen("./libpegasus_server.so", RTLD_LAZY);
	if(srv_handle == NULL) {
		fprintf(stderr, "Unable to dlopen libpegasus_server.so: %s\n", dlerror());
		return false;
	}
	
	const char* sym = "PegasusServer_serveWithPlugin";
	bool (*fn_serve)(PegPlugin_Init_func* plugin_init) = (bool (*)(PegPlugin_Init_func*))dlsym(srv_handle, sym);
	if(fn_serve == NULL) {
		fprintf(stderr, "Missing required symbol \"%s\" in libpegasus_server.so!\n", sym);
		dlclose(srv_handle);
		return false;
	}
	
	bool ret = fn_serve(plugin_init);
	dlclose(srv_handle);
	return ret;
}

#endif /* PEG_PEGASUS_SERVER_H */
