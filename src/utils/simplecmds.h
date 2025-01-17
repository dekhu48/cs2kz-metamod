/* date = October 12th 2023 8:02 pm */

#ifndef SIMPLECMDS_H
#define SIMPLECMDS_H

class CCSPlayerController;

#define SCMD_CALLBACK(name) META_RES name(CCSPlayerController *controller, const CCommand *args)

#define SCMD_CONSOLE_PREFIX "kz_"
#define SCMD_CHAT_SILENT_TRIGGER '/'
#define SCMD_CHAT_TRIGGER '!'
#define SCMD_MAX_CMDS 512

namespace scmd
{
	typedef SCMD_CALLBACK(Callback_t);
	bool RegisterCmd(const char *name, Callback_t *callback, const char *description = nullptr, bool hidden = false);
	bool UnregisterCmd(const char *name);

	META_RES OnClientCommand(CPlayerSlot &slot, const CCommand &args);
	META_RES OnDispatchConCommand(ConCommandHandle cmd, const CCommandContext &ctx, const CCommand &args);
}


#endif //SIMPLECMDS_H
