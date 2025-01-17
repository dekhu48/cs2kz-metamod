#include "kz_timer.h"
#include "../mode/kz_mode.h"
#include "../style/kz_style.h"
#include "../noclip/kz_noclip.h"
#include "utils/utils.h"
#include "utils/simplecmds.h"

internal CUtlVector<KZTimerServiceEventListener *> eventListeners;

bool KZTimerService::RegisterEventListener(KZTimerServiceEventListener *eventListener)
{
	if (eventListeners.Find(eventListener) >= 0)
	{
		return false;
	}
	eventListeners.AddToTail(eventListener);
	return true;
}

bool KZTimerService::UnregisterEventListener(KZTimerServiceEventListener *eventListener)
{
	return eventListeners.FindAndRemove(eventListener);
}

bool KZTimerService::TimerStart(const char *courseName, bool playSound)
{
	if (!this->player->GetPawn()->IsAlive()
		|| this->JustStartedTimer()
		|| this->JustTeleported()
		|| this->player->noclipService->JustNoclipped()
		|| !this->HasValidMoveType()
		|| this->JustLanded()
		|| (this->GetTimerRunning() && !V_stricmp(courseName, this->currentCourse)))
	{
		return false;
	}
	if (V_strlen(this->player->modeService->GetModeName()) > KZ_MAX_MODE_NAME_LENGTH)
	{
		Warning("[KZ] Timer start failed: Mode name is too long!");
		return false;
	}

	bool allowStart = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowStart &= eventListeners[i]->OnTimerStart(this->player, courseName);
	}
	if (!allowStart)
	{
		return false;
	}

	this->currentTime = 0.0f;
	this->timerRunning = true;
	V_strncpy(this->currentCourse, courseName, KZ_MAX_COURSE_NAME_LENGTH);
	V_strncpy(this->lastStartMode, this->player->modeService->GetModeName(), KZ_MAX_MODE_NAME_LENGTH);
	validTime = true;
	if (playSound)
	{
		this->PlayTimerStartSound();
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerStartPost(this->player, courseName);
	}
	return true;
}

bool KZTimerService::TimerEnd(const char *courseName)
{
	if (!this->player->IsAlive())
	{
		return false;
	}

	if (!this->timerRunning || V_stricmp(this->currentCourse, courseName) != 0)
	{
		this->PlayTimerFalseEndSound();
		this->lastFalseEndTime = g_pKZUtils->GetServerGlobals()->curtime;
		return false;
	}

	f32 time = this->GetTime() + g_pKZUtils->GetServerGlobals()->frametime;
	u32 teleportsUsed = this->player->checkpointService->GetTeleportCount();

	bool allowEnd = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowEnd &= eventListeners[i]->OnTimerEnd(this->player, courseName, time, teleportsUsed);
	}
	if (!allowEnd)
	{
		return false;
	}

	this->timerRunning = false;
	this->lastEndTime = g_pKZUtils->GetServerGlobals()->curtime;
	this->PlayTimerEndSound();

	if (!this->player->GetPawn()->IsBot())
	{
		bool showMessage = true;
		FOR_EACH_VEC(eventListeners, i)
		{
			showMessage &= eventListeners[i]->OnTimerEndMessage(this->player, courseName, time, teleportsUsed);
		}
		if (showMessage)
		{
			this->PrintEndTimeString();
		}
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerEndPost(this->player, courseName, time, teleportsUsed);
	}

	return true;
}

bool KZTimerService::TimerStop(bool playSound)
{
	if (!this->timerRunning)
	{
		return false;
	}
	this->timerRunning = false;
	if (playSound)
	{
		this->PlayTimerStopSound();
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerStopped(this->player);
	}

	return true;
}

void KZTimerService::TimerStopAll(bool playSound)
{
	for (int i = 0; i < MAXPLAYERS + 1; i++)
	{
		KZPlayer *player = g_pKZPlayerManager->ToPlayer(i);
		if (!player || !player->timerService)
		{
			continue;
		}
		player->timerService->TimerStop(playSound);
	}
}

void KZTimerService::PlayTimerStartSound()
{
	if (g_pKZUtils->GetServerGlobals()->curtime - this->lastStartSoundTime > KZ_TIMER_SOUND_COOLDOWN)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), KZ_TIMER_SND_START);
		this->lastStartSoundTime = g_pKZUtils->GetServerGlobals()->curtime;
	}
}

void KZTimerService::InvalidateRun()
{
	if (!this->validTime)
	{
		return;
	}
	this->validTime = false;

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerInvalidated(this->player);
	}
}

// =====[ PRIVATE ]=====

bool KZTimerService::HasValidMoveType()
{
	return KZTimerService::IsValidMoveType(this->player->GetMoveType());
}

bool KZTimerService::JustTeleported()
{
	return g_pKZUtils->GetServerGlobals()->curtime - this->lastTeleportTime < KZ_TIMER_MIN_GROUND_TIME;
}

bool KZTimerService::JustEndedTimer()
{
	return g_pKZUtils->GetServerGlobals()->curtime - this->lastEndTime > 1.0f;
}

void KZTimerService::PlayTimerEndSound()
{
	utils::PlaySoundToClient(this->player->GetPlayerSlot(), KZ_TIMER_SND_END);
}

void KZTimerService::PlayTimerFalseEndSound()
{
	utils::PlaySoundToClient(this->player->GetPlayerSlot(), KZ_TIMER_SND_FALSE_END);
}

void KZTimerService::PlayTimerStopSound()
{
	utils::PlaySoundToClient(this->player->GetPlayerSlot(), KZ_TIMER_SND_STOP);
}


void KZTimerService::FormatTime(f64 time, char* output, u32 length, bool precise)
{
	int roundedTime = RoundFloatToInt(time * 1000); // Time rounded to number of ms

	int milliseconds = roundedTime % 1000;
	roundedTime = (roundedTime - milliseconds) / 1000;
	int seconds = roundedTime % 60;
	roundedTime = (roundedTime - seconds) / 60;
	int minutes = roundedTime % 60;
	roundedTime = (roundedTime - minutes) / 60;
	int hours = roundedTime;

	if (hours == 0)
	{
		if (precise)
		{
			snprintf(output, length, "%02i:%02i.%03i", minutes, seconds, milliseconds);
		}
		else
		{
			snprintf(output, length, "%i:%02i", minutes, seconds);
		}
	}
	else
	{
		if (precise)
		{
			snprintf(output, length, "%i:%02i:%02i.%03i", hours, minutes, seconds, milliseconds);
		}
		else
		{
			snprintf(output, length, "%i:%02i:%02i", hours, minutes, seconds);
		}
	}
}

void KZTimerService::PrintEndTimeString()
{
	CCSPlayerController *controller = this->player->GetController();
	char time[32];
	KZTimerService::FormatTime(this->GetTime(), time, sizeof(time));
	char tpCountStr[128] = "";
	u32 tpCount = this->player->checkpointService->GetTeleportCount();
	if (!tpCount)
	{
		snprintf(tpCountStr, sizeof(tpCountStr), "{purple}%s {grey}|{purple} %s{grey}", 
			this->player->modeService->GetModeShortName(), 
			this->player->styleService->GetStyleShortName());
	}
	else
	{
		snprintf(tpCountStr, sizeof(tpCountStr), "{purple}%s {grey}|{purple} %s {grey}|{purple} %i {grey}TPs",
			this->player->modeService->GetModeShortName(),
			this->player->styleService->GetStyleShortName(),
			tpCount);
	}
	char courseStr[KZ_MAX_COURSE_NAME_LENGTH + 16] = "";
	if (strlen(this->currentCourse) > 0)
	{
		snprintf(courseStr, sizeof(courseStr), " course {default}%s{grey} ", this->currentCourse);
	}
	utils::CPrintChatAll("%s {lime}%s {grey}finished %s with a%srun of {default}%s{grey}! [%s]",
		KZ_CHAT_PREFIX,
		this->player->GetController()->m_iszPlayerName(),
		courseStr,
		tpCount > 0 ? " " : " {blue}PRO{grey} ",
		time,
		tpCountStr);
}

void KZTimerService::Pause()
{
	if (!this->CanPause(true))
	{
		return;
	}

	bool allowPause = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowPause &= eventListeners[i]->OnPause(this->player);
	}
	if (!allowPause)
	{
		utils::CPrintChat(this->player->GetController(), "%s %s", KZ_CHAT_PREFIX, "{grey}Can't pause right now.");
		// TODO: Play error sound to client
		return;
	}

	this->paused = true;
	this->pausedOnLadder = this->player->GetMoveType() == MOVETYPE_LADDER;
	this->lastDuckValue = this->player->GetMoveServices()->m_flDuckAmount;
	this->lastStaminaValue = this->player->GetMoveServices()->m_flStamina;
	this->player->SetVelocity(vec3_origin);
	this->player->GetPawn()->SetMoveType(MOVETYPE_NONE);

	if (this->GetTimerRunning())
	{
		this->hasPausedInThisRun = true;
		this->lastPauseTime = g_pKZUtils->GetServerGlobals()->curtime;
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnPausePost(this->player);
	}
}

bool KZTimerService::CanPause(bool showError)
{
	if (this->paused)
	{
		return false;
	}

	Vector velocity;
	this->player->GetVelocity(&velocity);

	if (this->GetTimerRunning())
	{
		if (this->hasResumedInThisRun && g_pKZUtils->GetServerGlobals()->curtime - this->lastResumeTime < KZ_PAUSE_COOLDOWN)
		{
			if (showError)
			{
				utils::CPrintChat(this->player->GetController(), "%s %s", KZ_CHAT_PREFIX, "{grey}Can't pause, just resumed.");
				this->player->PlayErrorSound();
			}
			return false;
		}
		else if (!this->player->GetPawn()->m_fFlags & FL_ONGROUND
				&& !(velocity.Length2D() == 0.0f && velocity.z == 0.0f))
		{
			if (showError)
			{
				utils::CPrintChat(this->player->GetController(), "%s %s", KZ_CHAT_PREFIX, "{grey}Can't pause, just resumed.");
				this->player->PlayErrorSound();
			}
			return false;
		}
		// TODO: Bhop/Antipause detection
	}
	return true;
}

void KZTimerService::Resume(bool force)
{
	if (!this->paused)
	{
		return;
	}
	if (!force && !this->CanResume(true))
	{
		return;
	}

	bool allowResume = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowResume &= eventListeners[i]->OnResume(this->player);
	}
	if (!allowResume)
	{
		utils::CPrintChat(this->player->GetController(), "%s %s", KZ_CHAT_PREFIX, "Can't resume right now.");
		this->player->PlayErrorSound();
		return;
	}

	if (this->pausedOnLadder)
	{
		this->player->GetPawn()->SetMoveType(MOVETYPE_LADDER);
	}
	else
	{
		this->player->GetPawn()->SetMoveType(MOVETYPE_WALK);
	}

	// GOKZ: prevent noclip exploit
	this->player->GetPawn()->m_Collision().m_CollisionGroup() = KZ_COLLISION_GROUP_STANDARD;

	this->paused = false;
	if (this->GetTimerRunning())
	{
		this->hasResumedInThisRun = true;
		this->lastResumeTime = g_pKZUtils->GetServerGlobals()->curtime;
	}
	this->player->GetMoveServices()->m_flDuckAmount = this->lastDuckValue;
	this->player->GetMoveServices()->m_flStamina = this->lastStaminaValue;

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnResumePost(this->player);
	}
}

bool KZTimerService::CanResume(bool showError)
{
	if (this->GetTimerRunning() && this->hasPausedInThisRun
		&& g_pKZUtils->GetServerGlobals()->curtime - this->lastPauseTime < KZ_PAUSE_COOLDOWN)
	{
		if (showError)
		{
			utils::CPrintChat(this->player->GetController(), "%s %s", KZ_CHAT_PREFIX, "{grey}Can't resume, just paused.");
			this->player->PlayErrorSound();
		}
		return false;
	}
	return true;
}

void KZTimerService::Reset()
{
	this->timerRunning = {};
	this->currentTime = {};
	this->currentCourse[0] = 0;
	this->lastEndTime = {};
	this->lastFalseEndTime = {};
	this->lastStartSoundTime = {};
	this->lastStartMode[0] = 0;
	this->validTime = {};
	this->lastTeleportTime = {};
	this->paused = {};
	this->pausedOnLadder = {};
	this->lastPauseTime = {};
	this->hasPausedInThisRun = {};
	this->lastResumeTime = {};
	this->hasResumedInThisRun = {};
	this->lastDuckValue = {};
	this->lastStaminaValue = {};
}

void KZTimerService::OnPhysicsSimulatePost()
{
	if (this->player->IsAlive() && this->GetTimerRunning() && !this->GetPaused())
	{
		this->currentTime += ENGINE_FIXED_TICK_INTERVAL;
	}
}

void KZTimerService::OnChangeMoveType(MoveType_t oldMoveType)
{
	// Check if player has escaped MOVETYPE_NONE
	if (!this->paused || this->player->GetMoveType() == MOVETYPE_NONE)
	{
		return;
	}

	this->paused = false;
	if (this->GetTimerRunning())
	{
		this->hasResumedInThisRun = true;
		this->lastResumeTime = g_pKZUtils->GetServerGlobals()->curtime;
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnResumePost(this->player);
	}
}

void KZTimerService::OnTeleportToStart()
{
	this->TimerStop();
}

void KZTimerService::OnClientDisconnect()
{
	this->TimerStop();
}

void KZTimerService::OnPlayerSpawn()
{
	if (!this->paused)
	{
		return;
	}

	// Player has left paused state by spawning in, so resume
	this->paused = false;
	if (this->GetTimerRunning())
	{
		this->hasResumedInThisRun = true;
		this->lastResumeTime = g_pKZUtils->GetServerGlobals()->curtime;
	}
	this->player->GetMoveServices()->m_flDuckAmount = this->lastDuckValue;
	this->player->GetMoveServices()->m_flStamina = this->lastStaminaValue;

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnResumePost(this->player);
	}
}

void KZTimerService::OnPlayerJoinTeam(i32 team)
{
	if (team == CS_TEAM_SPECTATOR)
	{
		this->paused = true;
		if (this->GetTimerRunning())
		{
			this->hasPausedInThisRun = true;
			this->lastPauseTime = g_pKZUtils->GetServerGlobals()->curtime;
		}

		FOR_EACH_VEC(eventListeners, i)
		{
			eventListeners[i]->OnPausePost(this->player);
		}
	}
}

void KZTimerService::OnPlayerDeath()
{
	this->TimerStop();
}

void KZTimerService::OnOptionsChanged()
{
	// TODO
}

void KZTimerService::OnRoundStart()
{
	KZTimerService::TimerStopAll();
}

void KZTimerService::OnTeleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity)
{
	this->lastTeleportTime = g_pKZUtils->GetServerGlobals()->curtime;
}

internal SCMD_CALLBACK(Command_KzStopTimer)
{
	KZPlayer *player = g_pKZPlayerManager->ToPlayer(controller);
	if (player->timerService->GetTimerRunning())
	{
		player->timerService->TimerStop();
	}
	return MRES_SUPERCEDE;
}

internal SCMD_CALLBACK(Command_KzPauseTimer)
{
	KZPlayer *player = g_pKZPlayerManager->ToPlayer(controller);
	player->timerService->TogglePause();
	return MRES_SUPERCEDE;
}
void KZTimerService::RegisterCommands()
{
	scmd::RegisterCmd("kz_stop", Command_KzStopTimer, "Stop timer.");
	scmd::RegisterCmd("kz_pause", Command_KzPauseTimer, "Toggle timer pause.");

}
