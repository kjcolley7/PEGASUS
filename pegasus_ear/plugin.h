//
//  plugin.h
//  PegasusEar
//
//  Created by Kevin Colley on 11/5/20.
//

#ifndef PEG_PLUGIN_H
#define PEG_PLUGIN_H

#include "ear.h"
#include "loader.h"

typedef struct PegPlugin PegPlugin;
typedef struct PegVar PegVar;

typedef PegPlugin* PegPlugin_Init_func(EAR* ear, PegasusLoader* pegload, int var_count, PegVar* vars);
#define PEG_PLUGIN_INIT_SYMBOL "PegPlugin_Init"

typedef void PegPlugin_Destroy_func(PegPlugin* pegmod);
typedef bool PegPlugin_OnLoaded_func(PegPlugin* pegmod);

struct PegPlugin {
	PegPlugin_Destroy_func* fn_destroy;
	PegPlugin_OnLoaded_func* fn_onLoaded;
};

struct PegVar {
	char* name;
	char* value;
};

#endif /* PEG_PLUGIN_H */
