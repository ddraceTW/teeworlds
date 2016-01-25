#ifndef GAME_SERVER_DDRACECOMMANDS_H
#define GAME_SERVER_DDRACECOMMANDS_H
#undef GAME_SERVER_DDRACECOMMANDS_H
#ifndef CHAT_COMMAND
#define CHAT_COMMAND(name, params, flags, callback, userdata, help)
#endif

CHAT_COMMAND("info", "", CFGFLAG_SERVERCHAT, ChatConInfo, this, "Display some information about this modification")

#undef CHAT_COMMAND
#endif