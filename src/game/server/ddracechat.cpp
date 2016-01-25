#include "gamecontext.h"
#include <engine/shared/config.h>

void CGameContext::ChatConInfo(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;

	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "| DDRace for teeworlds 0.7");
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "| The sun goes down");
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "| The stars come out");
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "| And all that counts");
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "| Is here and now");
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "| My universe will never be the same");
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "| I'm glad you came");
}