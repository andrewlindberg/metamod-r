#pragma once

#include "meta_api.h"			// META_RES, etc
#include "mlist.h"			// MPluginList, etc
#include "mreg.h"			// MRegCmdList, etc
#include "conf_meta.h"			// MConfig
#include "osdep.h"			// NAME_MAX, etc
#include "types_meta.h"			// bool
#include "mplayer.h"                    // MPlayerList
#include "meta_eiface.h"		// HL_enginefuncs_t, meta_enginefuncs_t
#include "engine_t.h"          		// engine_t, Engine

// file that lists plugins to load at startup
#define PLUGINS_INI			"addons/metamod/plugins.ini"
#define OLD_PLUGINS_INI			"metamod.ini"

// file that contains commands to metamod plugins at startup
#define EXEC_CFG			"addons/metamod/exec.cfg"
#define OLD_EXEC_CFG			"metaexec.cfg"

// previously, file that contained path for an override-gamedll
#define OLD_GAMEDLL_TXT			"metagame.ini"

// generic config file
#define CONFIG_INI			"addons/metamod/config.ini"

// cvar to contain version
extern cvar_t meta_version;

// Info about the game dll/mod.
struct gamedll_t
{
	char name[NAME_MAX];		// ie "cstrike" (from gamedir)
	const char *desc;		// ie "Counter-Strike"
	char gamedir[PATH_MAX];		// ie "/home/willday/half-life/cstrike"
	char pathname[PATH_MAX];	// ie "/home/willday/half-life/cstrike/dlls/cs_i386.so"
	char const *file;		// ie "cs_i386.so"
	char real_pathname[PATH_MAX];	// in case pathname overridden by bot, etc
	DLHANDLE handle;
	gamedll_funcs_t funcs;		// dllapi_table, newapi_table
};

extern gamedll_t GameDLL;

// SDK variables for storing engine funcs and globals.
extern HL_enginefuncs_t g_engfuncs;
extern globalvars_t *gpGlobals;

// Our modified version of the engine funcs, to give to plugins.
extern meta_enginefuncs_t g_plugin_engfuncs;

// g_config structure.
extern MConfig *g_config;

// List of plugins loaded/opened/running.
extern MPluginList *g_plugins;

// List of command functions registered by plugins.
extern MRegCmdList *g_regCmds;

// List of cvar structures registered by plugins.
extern MRegCvarList *g_regCvars;

// List of user messages registered by gamedll.
extern MRegMsgList *g_regMsgs;

#ifdef UNFINISHED
// List of event/logline hooks requested by plugins.
extern MHookList *Hooks;
#endif

// Data provided to plugins.
// Separate copies to prevent plugins from modifying "readable" parts.
// See meta_api.h for meta_globals_t structure.
extern meta_globals_t g_metaGlobals;

// hook function tables
extern DLL_FUNCTIONS *pHookedDllFunctions;
extern NEW_DLL_FUNCTIONS *pHookedNewDllFunctions;

// (patch by hullu)
// Safety check for metamod-bot-plugin bugfix.
//  engine_api->pfnRunPlayerMove calls dllapi-functions before it returns.
//  This causes problems with bots running as metamod plugins, because
//  metamod assumed that g_metaGlobals is free to be used.
//  With call_count we can fix this by backuping up g_metaGlobals if
//  it's already being used.
extern unsigned int CALL_API_count;

// stores previous requestid counter
extern int requestid_counter;

// (patch by BAILOPAN)
// Holds cached player info, right now only things for querying cvars
// Max players is always 32, small enough that we can use a static array
extern MPlayerList g_players;

void metamod_startup();

bool meta_init_gamedll();
bool meta_load_gamedll();
void meta_rebuild_callbacks();

// lotsa macros...

// These are the meat of the metamod processing, and are as ugly as (or
// uglier) than they look.  This is done via macros, because of the varying
// parameter types (int, void, edict_t*, etc) as well as varying
// function-pointer types and different api tables (dllapi, newapi,
// engine), which just can't be passed to a function.  And, since the
// operation is similar for each api call, I didn't want to keep
// duplicating code all over the place.  Thus the ugly macros.
//
// The basic operation is, for each api call:
//  - iterate through list of plugins
//  - for each plugin, if it provides this api call, then call the
//    function in the plugin
//  - call the "real" function (in the game dll, or from the engine)
//  - for each plugin, check for a "post" version of the function, and call
//    if present
//
//
// Also, for any api call, each plugin has the opportunity to replace the
// real routine, in two ways:
//  - prevent the real routine from being called ("supercede")
//  - allow the real routine to be called, but change the value that's
//    returned ("override")
//
// Thus after each plugin is called, its META_RETURN flag is checked, and
// action taken appropriately.  Note that supercede/override only affects
// the _real_ routine; other plugins will still be called.
//
// In addition to the SUPERCEDE and OVERRIDE flags, there are two
// additional flags a plugin can return:
//  - HANDLED ("I did something here")
//  - IGNORED ("I didn't really do anything")
//
// These aren't used by metamod itself, but could be used by plugins to
// get an idea if a previous plugin did anything.
//
//
// The 5 basic macros are:
//   SETUP
//   CALL_PLUGIN
//   CALL_GAME  and  CALL_ENGINE
//   RETURN
//
// These 5 are actually used to build second level macros for each api type
// (dllapi, newapi, engine), with the CALL_PLUGIN macro being used twice
// (before and after).  Altogether, they end up expanding to approx 150
// lines of code for _each_ api call.  Ack, ugly indeed.
//
// However, due to some functions returning 'void', and others returning an
// actual value, I had to have separate macros for the two, since I
// couldn't seem to generalize the two into a form that the compiler would
// accept.  Thus there are "_void" versions of the 5 macros; these are
// listed first.