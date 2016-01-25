/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <engine/shared/config.h>
#include "entities/character.h"
#include "gamecontext.h"
#include "gamecontroller.h"
#include "player.h"


MACRO_ALLOC_POOL_ID_IMPL(CPlayer, MAX_CLIENTS)

IServer *CPlayer::Server() const { return m_pGameServer->Server(); }

CPlayer::CPlayer(CGameContext *pGameServer, int ClientID, bool Dummy)
{
	m_pGameServer = pGameServer;
	m_RespawnTick = Server()->Tick();
	m_DieTick = Server()->Tick();
	m_ScoreStartTick = Server()->Tick();
	m_pCharacter = 0;
	m_ClientID = ClientID;
	m_Team = GameServer()->m_pController->GetStartTeam();
	m_SpectatorID = SPEC_FREEVIEW;
	m_LastActionTick = Server()->Tick();
	m_TeamChangeTick = Server()->Tick();
	m_InactivityTickCounter = 0;
	m_Dummy = Dummy;
	m_IsReadyToPlay = !GameServer()->m_pController->IsPlayerReadyMode();
	m_RespawnDisabled = GameServer()->m_pController->GetStartRespawnState();
	m_DeadSpecMode = false;
	m_Spawning = 0;
}

CPlayer::~CPlayer()
{
	delete m_pCharacter;
	m_pCharacter = 0;
}

void CPlayer::Tick()
{
	if(!IsDummy() && !Server()->ClientIngame(m_ClientID))
		return;

	Server()->SetClientScore(m_ClientID, m_Score);

	// do latency stuff
	{
		IServer::CClientInfo Info;
		if(Server()->GetClientInfo(m_ClientID, &Info))
		{
			m_Latency.m_Accum += Info.m_Latency;
			m_Latency.m_AccumMax = max(m_Latency.m_AccumMax, Info.m_Latency);
			m_Latency.m_AccumMin = min(m_Latency.m_AccumMin, Info.m_Latency);
		}
		// each second
		if(Server()->Tick()%Server()->TickSpeed() == 0)
		{
			m_Latency.m_Avg = m_Latency.m_Accum/Server()->TickSpeed();
			m_Latency.m_Max = m_Latency.m_AccumMax;
			m_Latency.m_Min = m_Latency.m_AccumMin;
			m_Latency.m_Accum = 0;
			m_Latency.m_AccumMin = 1000;
			m_Latency.m_AccumMax = 0;
		}
	}

	if(m_pCharacter && !m_pCharacter->IsAlive())
	{
		delete m_pCharacter;
		m_pCharacter = 0;
	}

	if(!GameServer()->m_pController->IsGamePaused())
	{
		if(!m_pCharacter && m_Team == TEAM_SPECTATORS && m_SpectatorID == SPEC_FREEVIEW)
			m_ViewPos -= vec2(clamp(m_ViewPos.x-m_LatestActivity.m_TargetX, -500.0f, 500.0f), clamp(m_ViewPos.y-m_LatestActivity.m_TargetY, -400.0f, 400.0f));

		if(!m_pCharacter && m_DieTick+Server()->TickSpeed()*3 <= Server()->Tick() && !m_DeadSpecMode)
			Respawn();

		if(m_pCharacter)
		{
			if(m_pCharacter->IsAlive())
				m_ViewPos = m_pCharacter->GetPos();
		}
		else if(m_Spawning && m_RespawnTick <= Server()->Tick())
			TryRespawn();

		if(!m_DeadSpecMode && m_LastActionTick != Server()->Tick())
			++m_InactivityTickCounter;
	}
	else
	{
		++m_RespawnTick;
		++m_DieTick;
		++m_ScoreStartTick;
		++m_LastActionTick;
		++m_TeamChangeTick;
 	}
}

void CPlayer::PostTick()
{
	// update latency value
	if(m_PlayerFlags&PLAYERFLAG_SCOREBOARD)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				m_aActLatency[i] = GameServer()->m_apPlayers[i]->m_Latency.m_Min;
		}
	}

	// update view pos for spectators and dead players
	if((m_Team == TEAM_SPECTATORS || m_DeadSpecMode) && m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[m_SpectatorID])
		m_ViewPos = GameServer()->m_apPlayers[m_SpectatorID]->m_ViewPos;
}

void CPlayer::Snap(int SnappingClient)
{
	if(!IsDummy() && !Server()->ClientIngame(m_ClientID))
		return;

	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, m_ClientID, sizeof(CNetObj_PlayerInfo)));
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_PlayerFlags = m_PlayerFlags&PLAYERFLAG_CHATTING;
	if(Server()->IsAuthed(m_ClientID))
		pPlayerInfo->m_PlayerFlags |= PLAYERFLAG_ADMIN;
	if(!GameServer()->m_pController->IsPlayerReadyMode() || m_IsReadyToPlay)
		pPlayerInfo->m_PlayerFlags |= PLAYERFLAG_READY;
	if(m_RespawnDisabled && (!GetCharacter() || !GetCharacter()->IsAlive()))
		pPlayerInfo->m_PlayerFlags |= PLAYERFLAG_DEAD;
	if(SnappingClient != -1 && (m_Team == TEAM_SPECTATORS || m_DeadSpecMode) && SnappingClient == m_SpectatorID)
		pPlayerInfo->m_PlayerFlags |= PLAYERFLAG_WATCHING;
	pPlayerInfo->m_Latency = SnappingClient == -1 ? m_Latency.m_Min : GameServer()->m_apPlayers[SnappingClient]->m_aActLatency[m_ClientID];
	pPlayerInfo->m_Score = m_Score;

	if(m_ClientID == SnappingClient && (m_Team == TEAM_SPECTATORS || m_DeadSpecMode))
	{
		CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, m_ClientID, sizeof(CNetObj_SpectatorInfo)));
		if(!pSpectatorInfo)
			return;

		pSpectatorInfo->m_SpectatorID = m_SpectatorID;
		pSpectatorInfo->m_X = m_ViewPos.x;
		pSpectatorInfo->m_Y = m_ViewPos.y;
	}

	// demo recording
	if(SnappingClient == -1)
	{
		CNetObj_De_ClientInfo *pClientInfo = static_cast<CNetObj_De_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_DE_CLIENTINFO, m_ClientID, sizeof(CNetObj_De_ClientInfo)));
		if(!pClientInfo)
			return;

		pClientInfo->m_Local = 0;
		pClientInfo->m_Team = m_Team;
		StrToInts(pClientInfo->m_aName, 4, Server()->ClientName(m_ClientID));
		StrToInts(pClientInfo->m_aClan, 3, Server()->ClientClan(m_ClientID));
		pClientInfo->m_Country = Server()->ClientCountry(m_ClientID);

		for(int p = 0; p < 6; p++)
		{
			StrToInts(pClientInfo->m_aaSkinPartNames[p], 6, m_TeeInfos.m_aaSkinPartNames[p]);
			pClientInfo->m_aUseCustomColors[p] = m_TeeInfos.m_aUseCustomColors[p];
			pClientInfo->m_aSkinPartColors[p] = m_TeeInfos.m_aSkinPartColors[p];
		}
	}
}

void CPlayer::OnDisconnect()
{
	KillCharacter();

	if(m_Team != TEAM_SPECTATORS)
	{
		// update spectator modes
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_SpectatorID == m_ClientID)
			{
				if(GameServer()->m_apPlayers[i]->m_DeadSpecMode)
					GameServer()->m_apPlayers[i]->UpdateDeadSpecMode();
				else
					GameServer()->m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
			}
		}
	}
}

void CPlayer::OnPredictedInput(CNetObj_PlayerInput *NewInput)
{
	// skip the input if chat is active
	if((m_PlayerFlags&PLAYERFLAG_CHATTING) && (NewInput->m_PlayerFlags&PLAYERFLAG_CHATTING))
		return;

	if(m_pCharacter)
		m_pCharacter->OnPredictedInput(NewInput);
}

void CPlayer::OnDirectInput(CNetObj_PlayerInput *NewInput)
{
	if(GameServer()->m_World.m_Paused)
	{
		m_PlayerFlags = NewInput->m_PlayerFlags;
		return;
	}

	if(NewInput->m_PlayerFlags&PLAYERFLAG_CHATTING)
	{
		// skip the input if chat is active
		if(m_PlayerFlags&PLAYERFLAG_CHATTING)
			return;

		// reset input
		if(m_pCharacter)
			m_pCharacter->ResetInput();

		m_PlayerFlags = NewInput->m_PlayerFlags;
 		return;
	}

	m_PlayerFlags = NewInput->m_PlayerFlags;

	if(m_pCharacter)
		m_pCharacter->OnDirectInput(NewInput);

	if(!m_pCharacter && m_Team != TEAM_SPECTATORS && (NewInput->m_Fire&1))
		Respawn();

	// check for activity
	if(NewInput->m_Direction || m_LatestActivity.m_TargetX != NewInput->m_TargetX ||
		m_LatestActivity.m_TargetY != NewInput->m_TargetY || NewInput->m_Jump ||
		NewInput->m_Fire&1 || NewInput->m_Hook)
	{
		m_LatestActivity.m_TargetX = NewInput->m_TargetX;
		m_LatestActivity.m_TargetY = NewInput->m_TargetY;
		m_LastActionTick = Server()->Tick();
		m_InactivityTickCounter = 0;
	}
}

CCharacter *CPlayer::GetCharacter()
{
	if(m_pCharacter && m_pCharacter->IsAlive())
		return m_pCharacter;
	return 0;
}

void CPlayer::KillCharacter(int Weapon)
{
	if(m_pCharacter)
	{
		m_pCharacter->Die(m_ClientID, Weapon);
		delete m_pCharacter;
		m_pCharacter = 0;
	}
}

void CPlayer::Respawn()
{
	if(m_RespawnDisabled)
	{
		// enable spectate mode for dead players
		m_DeadSpecMode = true;
		m_IsReadyToPlay = true;
		UpdateDeadSpecMode();
		return;
	}

	m_DeadSpecMode = false;

	if(m_Team != TEAM_SPECTATORS)
		m_Spawning = true;
}

bool CPlayer::SetSpectatorID(int SpectatorID)
{
	if(m_SpectatorID == SpectatorID || m_ClientID == SpectatorID)
		return false;

	if(m_Team == TEAM_SPECTATORS)
	{
		// check for freeview or if wanted player is playing
		if(SpectatorID == SPEC_FREEVIEW || (GameServer()->m_apPlayers[SpectatorID] && GameServer()->m_apPlayers[SpectatorID]->GetTeam() != TEAM_SPECTATORS))
		{
			m_SpectatorID = SpectatorID;
			return true;
		}
	}
	else if(m_DeadSpecMode)
	{
		// check if wanted player can be followed
		if(GameServer()->m_apPlayers[SpectatorID] && DeadCanFollow(GameServer()->m_apPlayers[SpectatorID]))
		{
			m_SpectatorID = SpectatorID;
			return true;
		}
	}

	return false;
}

bool CPlayer::DeadCanFollow(CPlayer *pPlayer) const
{
	// check if wanted player is in the same team and alive
	return (!pPlayer->m_RespawnDisabled || (pPlayer->GetCharacter() && pPlayer->GetCharacter()->IsAlive())) && pPlayer->GetTeam() == m_Team;
}

void CPlayer::UpdateDeadSpecMode()
{
	// check if actual spectator id is valid
	if(m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[m_SpectatorID] && DeadCanFollow(GameServer()->m_apPlayers[m_SpectatorID]))
		return;

	// find player to follow
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(GameServer()->m_apPlayers[i] && DeadCanFollow(GameServer()->m_apPlayers[i]))
		{
			m_SpectatorID = i;
			return;
		}
	}

	// no one available to follow -> turn spectator mode off
	m_DeadSpecMode = false;
}

void CPlayer::SetTeam(int Team, bool DoChatMsg)
{
	KillCharacter();

	m_Team = Team;
	m_LastActionTick = Server()->Tick();
	m_SpectatorID = SPEC_FREEVIEW;
	m_DeadSpecMode = false;
	
	// we got to wait 0.5 secs before respawning
	m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	
	if(Team == TEAM_SPECTATORS)
	{
		// update spectator modes
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_SpectatorID == m_ClientID)
			{
				if(GameServer()->m_apPlayers[i]->m_DeadSpecMode)
					GameServer()->m_apPlayers[i]->UpdateDeadSpecMode();
				else
					GameServer()->m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
			}
		}
	}
}

void CPlayer::TryRespawn()
{
	vec2 SpawnPos;

	if(!GameServer()->m_pController->CanSpawn(m_Team, &SpawnPos))
		return;

	m_Spawning = false;
	m_pCharacter = new(m_ClientID) CCharacter(&GameServer()->m_World);
	m_pCharacter->Spawn(this, SpawnPos);
	GameServer()->CreatePlayerSpawn(SpawnPos);
}

bool CPlayer::AfkTimer(int NewTargetX, int NewTargetY)
{
	/*
		afk timer (x, y = mouse coordinates)
		Since a player has to move the mouse to play, this is a better method than checking
		the player's position in the game world, because it can easily be bypassed by just locking a key.
		Frozen players could be kicked as well, because they can't move.
		It also works for spectators.
		returns true if kicked
	*/

	if(m_Authed)
		return false; // don't kick admins
	if(g_Config.m_SvMaxAfkTime == 0)
		return false; // 0 = disabled

	if(NewTargetX != m_LastTarget_x || NewTargetY != m_LastTarget_y)
	{
		m_LastPlaytime = time_get();
		m_LastTarget_x = NewTargetX;
		m_LastTarget_y = NewTargetY;
		m_Sent1stAfkWarning = 0; // afk timer's 1st warning after 50% of sv_max_afk_time
		m_Sent2ndAfkWarning = 0;

	}
	else
	{
		if(!m_Paused)
		{
			// not playing, check how long
			if(m_Sent1stAfkWarning == 0 && m_LastPlaytime < time_get()-time_freq()*(int)(g_Config.m_SvMaxAfkTime*0.5))
			{
				sprintf(
					m_pAfkMsg,
					"You have been afk for %d seconds now. Please note that you get kicked after not playing for %d seconds.",
					(int)(g_Config.m_SvMaxAfkTime*0.5),
					g_Config.m_SvMaxAfkTime
				);
				m_pGameServer->SendChatTarget(m_ClientID, m_pAfkMsg);
				m_Sent1stAfkWarning = 1;
			}
			else if(m_Sent2ndAfkWarning == 0 && m_LastPlaytime < time_get()-time_freq()*(int)(g_Config.m_SvMaxAfkTime*0.9))
			{
				sprintf(
					m_pAfkMsg,
					"You have been afk for %d seconds now. Please note that you get kicked after not playing for %d seconds.",
					(int)(g_Config.m_SvMaxAfkTime*0.9),
					g_Config.m_SvMaxAfkTime
				);
				m_pGameServer->SendChatTarget(m_ClientID, m_pAfkMsg);
				m_Sent2ndAfkWarning = 1;
			}
			else if(m_LastPlaytime < time_get()-time_freq()*g_Config.m_SvMaxAfkTime)
			{
				//CServer* serv =	(CServer*)m_pGameServer->Server(); // ...wtf?
				Server()->Kick(m_ClientID,"Away from keyboard");
				return true;
			}
		}
	}
	return false;
}

void CPlayer::AfkVoteTimer(CNetObj_PlayerInput *NewTarget)
{
	if(g_Config.m_SvMaxAfkVoteTime == 0)
		return;

	if(mem_comp(NewTarget, &m_LastTarget, sizeof(CNetObj_PlayerInput)) != 0)
	{
		m_LastPlaytime = time_get();
		mem_copy(&m_LastTarget, NewTarget, sizeof(CNetObj_PlayerInput));
	}
	else if(m_LastPlaytime < time_get()-time_freq()*g_Config.m_SvMaxAfkVoteTime)
	{
		m_Afk = true;
		return;
	}

	m_Afk = false;
}

void CPlayer::ProcessPause()
{
	if(!m_pCharacter)
		return;

	char aBuf[128];
	if(m_Paused >= PAUSED_PAUSED)
	{
		if(!m_pCharacter->IsPaused())
		{
			m_pCharacter->Pause(true);
			if(g_Config.m_SvPauseMessages)
			{
				str_format(aBuf, sizeof(aBuf), (m_Paused == PAUSED_PAUSED) ? "'%s' paused" : "'%s' was force-paused for %ds", Server()->ClientName(m_ClientID), m_ForcePauseTime/Server()->TickSpeed());
				GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
			}
			GameServer()->CreateDeath(m_pCharacter->m_Pos, m_ClientID, m_pCharacter->Teams()->TeamMask(m_pCharacter->Team(), -1, m_ClientID));
			GameServer()->CreateSound(m_pCharacter->m_Pos, SOUND_PLAYER_DIE, m_pCharacter->Teams()->TeamMask(m_pCharacter->Team(), -1, m_ClientID));
			m_NextPauseTick = Server()->Tick() + g_Config.m_SvPauseFrequency * Server()->TickSpeed();
		}
	}
	else
	{
		if(m_pCharacter->IsPaused())
		{
			m_pCharacter->Pause(false);
			if(g_Config.m_SvPauseMessages)
			{
				str_format(aBuf, sizeof(aBuf), "'%s' resumed", Server()->ClientName(m_ClientID));
				GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
			}
			GameServer()->CreatePlayerSpawn(m_pCharacter->m_Pos, m_pCharacter->Teams()->TeamMask(m_pCharacter->Team(), -1, m_ClientID));
			m_NextPauseTick = Server()->Tick() + g_Config.m_SvPauseFrequency * Server()->TickSpeed();
		}
	}
}

bool CPlayer::IsPlaying()
{
	if(m_pCharacter && m_pCharacter->IsAlive())
		return true;
	return false;
}

/*void CPlayer::FindDuplicateSkins()
{
	if (m_TeeInfos.m_UseCustomColor == 0 && !m_StolenSkin) return;
	m_StolenSkin = 0;
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		if (i == m_ClientID) continue;
		if(GameServer()->m_apPlayers[i])
		{
			if (GameServer()->m_apPlayers[i]->m_StolenSkin) continue;
			if ((GameServer()->m_apPlayers[i]->m_TeeInfos.m_UseCustomColor == m_TeeInfos.m_UseCustomColor) &&
			(GameServer()->m_apPlayers[i]->m_TeeInfos.m_ColorFeet == m_TeeInfos.m_ColorFeet) &&
			(GameServer()->m_apPlayers[i]->m_TeeInfos.m_ColorBody == m_TeeInfos.m_ColorBody) &&
			!str_comp(GameServer()->m_apPlayers[i]->m_TeeInfos.m_SkinName, m_TeeInfos.m_SkinName))
			{
				m_StolenSkin = 1;
				return;
			}
		}
	}
}*/
