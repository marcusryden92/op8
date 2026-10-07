//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: Stealth. See of2_stealth.h.
//
//			Everything here is evidence: sounds the NPC already hears through
//			CSoundEnt and the player being in its view. Evidence adds up to a
//			suspicion score, the score sets the state, and the state is all the
//			owner needs to pick a schedule. Nothing in here ever looks up where
//			the player is; a stimulus is where a sound came from or where the
//			player was when seen.
//
//=============================================================================//

#include "cbase.h"
#include "ai_basenpc.h"
#include "ai_senses.h"
#include "ai_memory.h"
#include "ai_squad.h"
#include "ai_network.h"
#include "ai_node.h"
#include "ai_navigator.h"
#include "ai_motor.h"
#include "soundent.h"
#include "player.h"
#include "physics.h"
#include "ndebugoverlay.h"
#include "of2_stealth.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar of2_stealth( "of2_stealth", "1", FCVAR_NONE, "Stealth: Combine have to notice the player before they fight (0 = stock: seen at once)." );
ConVar of2_stealth_debug( "of2_stealth_debug", "0", FCVAR_NONE, "Stealth: draw each NPC's state, suspicion and detection, where it thinks something is, the noise props make, and print how visible the player is." );

// States
static ConVar of2_stealth_suspicious( "of2_stealth_suspicious", "0.25", FCVAR_NONE, "Stealth: suspicion (0-1) at which an NPC stops and looks." );
static ConVar of2_stealth_search( "of2_stealth_search", "0.6", FCVAR_NONE, "Stealth: suspicion (0-1) at which an NPC goes to look." );
static ConVar of2_stealth_decay( "of2_stealth_decay", "0.05", FCVAR_NONE, "Stealth: suspicion lost per second once nothing new has turned up." );
static ConVar of2_stealth_decay_delay( "of2_stealth_decay_delay", "4", FCVAR_NONE, "Stealth: seconds after the last evidence before suspicion starts to fade." );
static ConVar of2_stealth_search_time( "of2_stealth_search_time", "30", FCVAR_NONE, "Stealth: seconds an NPC keeps searching after a noise or a glimpse. New evidence extends it." );
static ConVar of2_stealth_search_time_lost( "of2_stealth_search_time_lost", "45", FCVAR_NONE, "Stealth: seconds an NPC keeps searching after losing an enemy it was fighting." );
static ConVar of2_stealth_search_radius( "of2_stealth_search_radius", "400", FCVAR_NONE, "Stealth: how far from the place it was drawn to an NPC searches." );
static ConVar of2_stealth_lose_time( "of2_stealth_lose_time", "20", FCVAR_NONE, "Stealth: seconds without anyone in the squad seeing the player before an NPC stops knowing where they are and searches instead (0 = stock, about a minute)." );

// Once it knows there is an armed enemy
static ConVar of2_stealth_search_forever( "of2_stealth_search_forever", "1", FCVAR_NONE, "Stealth: an NPC that has fought the player, been shot at or heard gunfire never stands down: it goes on searching, further and further out (0 = it gives up after of2_stealth_search_time_lost)." );
static ConVar of2_stealth_search_spread( "of2_stealth_search_spread", "15", FCVAR_NONE, "Stealth: units per second the search of such an NPC widens from the last place it had word of the player." );
static ConVar of2_stealth_search_radius_max( "of2_stealth_search_radius_max", "1500", FCVAR_NONE, "Stealth: the widest that search gets." );
static ConVar of2_stealth_lkp_see_dist( "of2_stealth_lkp_see_dist", "800", FCVAR_NONE, "Stealth: from this close, an NPC looking at the place it last knew the player to be can tell they are not there." );
static ConVar of2_stealth_lkp_empty_time( "of2_stealth_lkp_empty_time", "1.5", FCVAR_NONE, "Stealth: seconds of looking at that place before it concludes the player has gone, stops firing at it and searches." );
static ConVar of2_stealth_suppress( "of2_stealth_suppress", "1", FCVAR_NONE, "Stealth: NPCs fire at the place they last knew the player to be, until they see them elsewhere or see it empty." );
static ConVar of2_stealth_suppress_time( "of2_stealth_suppress_time", "8", FCVAR_NONE, "Stealth: seconds after the last word of the player that an NPC keeps firing at that place." );
static ConVar of2_stealth_suppress_range( "of2_stealth_suppress_range", "2000", FCVAR_NONE, "Stealth: furthest an NPC fires at a place it only knows the player was." );
static ConVar of2_stealth_suppress_near( "of2_stealth_suppress_near", "160", FCVAR_NONE, "Stealth: it fires if its shots would land within this of that place (the cover the player went behind), not only with a clear line." );
static ConVar of2_stealth_gunfire_error( "of2_stealth_gunfire_error", "0.08", FCVAR_NONE, "Stealth: how far off an NPC places a gunshot it heard, as a fraction of the distance to it." );

// Sight
static ConVar of2_stealth_see_time( "of2_stealth_see_time", "0.5", FCVAR_NONE, "Stealth: seconds to identify a player running in full light, close and straight ahead. Everything below stretches this." );
static ConVar of2_stealth_see_instant( "of2_stealth_see_instant", "100", FCVAR_NONE, "Stealth: within this distance, in view, the player is identified at once whatever the light." );
static ConVar of2_stealth_see_near( "of2_stealth_see_near", "250", FCVAR_NONE, "Stealth: up to this distance, distance does not slow detection." );
static ConVar of2_stealth_see_far( "of2_stealth_see_far", "2000", FCVAR_NONE, "Stealth: distance at which detection is slowest." );
static ConVar of2_stealth_see_far_factor( "of2_stealth_see_far_factor", "0.06", FCVAR_NONE, "Stealth: detection speed at of2_stealth_see_far and beyond, as a fraction." );
static ConVar of2_stealth_see_crouch( "of2_stealth_see_crouch", "0.6", FCVAR_NONE, "Stealth: detection speed against a crouching player." );
static ConVar of2_stealth_see_still( "of2_stealth_see_still", "0.6", FCVAR_NONE, "Stealth: detection speed against a player standing still." );
static ConVar of2_stealth_see_sprint( "of2_stealth_see_sprint", "1.5", FCVAR_NONE, "Stealth: detection speed against a sprinting player." );
static ConVar of2_stealth_see_edge( "of2_stealth_see_edge", "0.4", FCVAR_NONE, "Stealth: detection speed at the edge of an NPC's view (1 straight ahead)." );
static ConVar of2_stealth_see_alert( "of2_stealth_see_alert", "2.0", FCVAR_NONE, "Stealth: detection speed of an NPC that is searching. A suspicious one is halfway." );
static ConVar of2_stealth_see_decay( "of2_stealth_see_decay", "0.2", FCVAR_NONE, "Stealth: detection lost per second while the player is out of view." );
static ConVar of2_stealth_blind( "of2_stealth_blind", "0.05", FCVAR_NONE, "Stealth: player visibility (0-1) at and below which an NPC without night vision cannot see them at all." );

// Hearing
static ConVar of2_stealth_hear_footsteps( "of2_stealth_hear_footsteps", "0.75", FCVAR_NONE, "Stealth: how far the player's movement carries, against stock (stock: as many units as the player's speed)." );
static ConVar of2_stealth_hear_walls( "of2_stealth_hear_walls", "0.5", FCVAR_NONE, "Stealth: how far the player's sounds carry to an NPC that cannot see where they come from, as a fraction (stock: not at all while idle)." );
static ConVar of2_stealth_footstep_rate( "of2_stealth_footstep_rate", "0.5", FCVAR_NONE, "Stealth: suspicion per second from hearing the player move close by." );
static ConVar of2_stealth_noise_scale( "of2_stealth_noise_scale", "1.0", FCVAR_NONE, "Stealth: scales the suspicion from every single noise (impacts, doors, shots)." );

// Noise from props
static ConVar of2_stealth_impact( "of2_stealth_impact", "1", FCVAR_NONE, "Stealth: physics props make a noise NPCs hear when they hit something." );
static ConVar of2_stealth_impact_min_speed( "of2_stealth_impact_min_speed", "90", FCVAR_NONE, "Stealth: a prop hitting something slower than this is silent." );
static ConVar of2_stealth_impact_scale( "of2_stealth_impact_scale", "1.0", FCVAR_NONE, "Stealth: scales how far prop impacts carry." );
static ConVar of2_stealth_impact_max( "of2_stealth_impact_max", "1200", FCVAR_NONE, "Stealth: the furthest a prop impact carries." );

// Squad
static ConVar of2_stealth_squad( "of2_stealth_squad", "1", FCVAR_NONE, "Stealth: squadmates are told what one of them is on to. An enemy one of them sees is a place to go and look, not knowledge of where the player is (0 = stock: the whole squad knows)." );
static ConVar of2_stealth_squad_radius( "of2_stealth_squad_radius", "1500", FCVAR_NONE, "Stealth: how far an NPC that goes to investigate something alerts its squad." );
static ConVar of2_stealth_report_interval( "of2_stealth_report_interval", "3", FCVAR_NONE, "Stealth: seconds between the positions a squadmate who sees the player passes on." );

#define STEALTH_NODE_MAX_DZ			128.0f	// search points this far above or below the stimulus are another floor
#define STEALTH_NODE_MIN_DIST		96.0f	// not worth walking to
#define STEALTH_NODE_TRACES			12		// sight checks per pick
#define STEALTH_REACT_MOVE			150.0f	// a stimulus has to move this far to make a searching NPC change course
#define STEALTH_REACT_INTERVAL		1.5f	// and it does that no more often than this
#define STEALTH_MAP_SETTLE_TIME		3.0f	// props dropping into place at map start are not noise
#define STEALTH_LKP_HEIGHT			40.0f	// chest height over a last known position
#define STEALTH_GUNFIRE_VOLUME		500		// SOUNDENT_VOLUME_EMPTY: the player's combat sounds louder than a dry click are shots
#define STEALTH_GOAL_TOLERANCE		48.0f	// near enough to a place it was drawn to

BEGIN_SIMPLE_DATADESC( COF2Awareness )
	DEFINE_FIELD( m_iState,				FIELD_INTEGER ),
	DEFINE_FIELD( m_bDisabled,			FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flSuspicion,		FIELD_FLOAT ),
	DEFINE_FIELD( m_flDetection,		FIELD_FLOAT ),
	DEFINE_FIELD( m_flLastEvidenceTime,	FIELD_TIME ),
	DEFINE_FIELD( m_flLastSeenTime,		FIELD_TIME ),
	DEFINE_FIELD( m_flLastLookTime,		FIELD_TIME ),
	DEFINE_FIELD( m_flLastListenTime,	FIELD_TIME ),
	DEFINE_FIELD( m_flLastUpdateTime,	FIELD_TIME ),
	DEFINE_FIELD( m_flSearchEndTime,	FIELD_TIME ),
	DEFINE_FIELD( m_flNextReportTime,	FIELD_TIME ),
	DEFINE_FIELD( m_flEnemyInfoTime,	FIELD_TIME ),
	DEFINE_FIELD( m_bEnemyAbout,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flSpotEmptyTime,	FIELD_FLOAT ),
	DEFINE_FIELD( m_vecLastLKP,			FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecStimulus,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_iStimulus,			FIELD_INTEGER ),
	DEFINE_FIELD( m_flStimulusTime,		FIELD_TIME ),
	DEFINE_FIELD( m_vecActedOn,			FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_flChangedTime,		FIELD_TIME ),
	DEFINE_FIELD( m_bChanged,			FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bWantsLook,			FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bVisited,			FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bWantsReturn,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vecHome,			FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_flHomeYaw,			FIELD_FLOAT ),
	// m_pOuter: set by the owner
	// m_pHeard etc: sounds do not outlive a save
END_DATADESC()

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
COF2Awareness::COF2Awareness()
{
	m_pOuter = NULL;
	m_iState = AWARE_UNAWARE;
	m_bDisabled = false;
	m_flSuspicion = 0.0f;
	m_flDetection = 0.0f;
	m_flLastEvidenceTime = 0.0f;
	m_flLastSeenTime = 0.0f;
	m_flLastLookTime = 0.0f;
	m_flLastListenTime = 0.0f;
	m_flLastUpdateTime = 0.0f;
	m_flSearchEndTime = 0.0f;
	m_flNextReportTime = 0.0f;
	m_flEnemyInfoTime = 0.0f;
	m_bEnemyAbout = false;
	m_flSpotEmptyTime = 0.0f;
	m_vecLastLKP.Init();
	m_flDarkRate = 1.0f;
	m_bSpotLit = true;
	m_bHeardShot = false;
	m_vecStimulus.Init();
	m_iStimulus = STIM_NONE;
	m_flStimulusTime = 0.0f;
	m_vecActedOn.Init();
	m_flChangedTime = 0.0f;
	m_bChanged = false;
	m_bWantsLook = false;
	m_bVisited = false;
	m_bWantsReturn = false;
	m_vecHome.Init();
	m_flHomeYaw = 0.0f;

	for ( int i = 0; i < OF2_STEALTH_HEARD_SOUNDS; i++ )
	{
		m_pHeard[i] = NULL;
		m_flHeardExpire[i] = 0.0f;
	}
	m_iHeardNext = 0;
	m_flLastNoiseTime = 0.0f;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool COF2Awareness::IsEnabled( void ) const
{
	return m_pOuter && !m_bDisabled && of2_stealth.GetBool();
}

//-----------------------------------------------------------------------------
// Purpose: Is the player an enemy it knows the whereabouts of? Then none of
//			this applies.
//-----------------------------------------------------------------------------
bool COF2Awareness::KnowsPlayer( CBasePlayer *pPlayer ) const
{
	AI_EnemyInfo_t *pMemory = m_pOuter->GetEnemies()->Find( pPlayer );
	return pMemory && !pMemory->bEludedMe;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void COF2Awareness::SetState( OF2Awareness_t state )
{
	if ( state == m_iState )
		return;

	int iOld = m_iState;
	m_iState = state;

	if ( iOld == AWARE_UNAWARE )
	{
		// Where to go back to when this is over
		m_vecHome = m_pOuter->GetAbsOrigin();
		m_flHomeYaw = m_pOuter->GetAbsAngles().y;
		m_bWantsReturn = false;
	}

	if ( state > iOld && state != AWARE_COMBAT )
	{
		m_bChanged = true;
		m_flChangedTime = gpGlobals->curtime;
	}

	switch ( state )
	{
	case AWARE_UNAWARE:
		m_flSuspicion = 0.0f;
		m_iStimulus = STIM_NONE;
		m_bWantsLook = false;
		m_bWantsReturn = true;
		break;

	case AWARE_SUSPICIOUS:
		m_bWantsLook = ( iOld < state );
		break;

	case AWARE_SEARCHING:
		m_bVisited = false;
		m_bWantsLook = false;
		m_flSearchEndTime = MAX( m_flSearchEndTime, gpGlobals->curtime + of2_stealth_search_time.GetFloat() );

		// Found this out for itself: tell the others
		if ( iOld < state && m_iStimulus != STIM_REPORT )
		{
			AlertSquad();
		}
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: The owner has reacted to the stimulus as it is now
//-----------------------------------------------------------------------------
void COF2Awareness::TakeChanged( void )
{
	m_bChanged = false;
	m_vecActedOn = m_vecStimulus;
}

//-----------------------------------------------------------------------------
// Purpose: Once per think
//-----------------------------------------------------------------------------
void COF2Awareness::Update( void )
{
	float flNow = gpGlobals->curtime;
	float dt = clamp( flNow - m_flLastUpdateTime, 0.0f, 0.5f );
	m_flLastUpdateTime = flNow;

	if ( !IsEnabled() )
	{
		if ( m_iState != AWARE_UNAWARE || m_flSuspicion > 0.0f )
		{
			m_iState = AWARE_UNAWARE;
			m_flSuspicion = 0.0f;
			m_flDetection = 0.0f;
			m_iStimulus = STIM_NONE;
			m_bChanged = m_bWantsLook = m_bWantsReturn = false;
		}
		return;
	}

	CBaseEntity *pEnemy = m_pOuter->GetEnemy();
	if ( pEnemy && pEnemy->IsPlayer() )
	{
		// Fighting the player: the stock AI's business. All that is kept is
		// where to start looking if it loses them.
		if ( m_iState != AWARE_COMBAT )
		{
			m_flEnemyInfoTime = flNow;
		}
		SetState( AWARE_COMBAT );
		m_bEnemyAbout = true;
		m_flSuspicion = 1.0f;
		m_flDetection = 1.0f;
		m_iStimulus = STIM_LOST_ENEMY;
		m_flStimulusTime = flNow;
		m_flLastEvidenceTime = flNow;

		// (An enemy set by a script can come without a memory)
		if ( m_pOuter->GetEnemies()->HasMemory( pEnemy ) )
		{
			m_vecStimulus = m_pOuter->GetEnemyLKP();

			// Looking at the place it last knew them to be, close enough to tell,
			// and they are not there. (If they were, it would see them; and where
			// it cannot see for darkness, it cannot see that the place is empty.)
			if ( m_vecStimulus.DistToSqr( m_vecLastLKP ) > Square( 32.0f ) )
			{
				m_vecLastLKP = m_vecStimulus;
				m_flSpotEmptyTime = 0.0f;
			}

			Vector vecSpot = m_vecStimulus + Vector( 0, 0, STEALTH_LKP_HEIGHT );
			bool bSeesSpot = !m_pOuter->HasCondition( COND_SEE_ENEMY ) && ( m_flDarkRate > 0.0f || m_bSpotLit ) &&
				vecSpot.DistTo( m_pOuter->EyePosition() ) <= of2_stealth_lkp_see_dist.GetFloat() &&
				m_pOuter->FInViewCone( vecSpot ) && m_pOuter->FVisible( vecSpot );

			m_flSpotEmptyTime = bSeesSpot ? m_flSpotEmptyTime + dt : 0.0f;
			if ( m_flSpotEmptyTime >= of2_stealth_lkp_empty_time.GetFloat() )
			{
				m_pOuter->GetEnemies()->MarkAsEluded( pEnemy );
			}

			// Stock soldiers know where an enemy they cannot see was for a minute.
			// Give up on that sooner and search. Not while squadmates still
			// call out where the player is: that is a fight going on.
			float flLoseTime = of2_stealth_lose_time.GetFloat();
			float flLastWord = MAX( m_flEnemyInfoTime, m_pOuter->GetEnemyLastTimeSeen() );
			if ( flLoseTime > 0.0f && !m_pOuter->HasCondition( COND_SEE_ENEMY ) && flNow - flLastWord > flLoseTime )
			{
				m_pOuter->GetEnemies()->MarkAsEluded( pEnemy );
			}
		}
		else
		{
			m_vecStimulus = m_pOuter->GetAbsOrigin();
		}

		DrawDebug();
		return;
	}

	if ( pEnemy )
	{
		// Busy with something else. Nothing fades meanwhile.
		DrawDebug();
		return;
	}

	if ( m_iState == AWARE_COMBAT )
	{
		// Lost them. It knows someone is about, so it searches long and is
		// quick to recognise them again.
		m_iState = AWARE_SEARCHING;
		m_flSuspicion = 1.0f;
		m_flDetection = 0.5f;
		m_bVisited = false;
		m_bWantsLook = false;
		m_bChanged = false;
		m_flSearchEndTime = flNow + of2_stealth_search_time_lost.GetFloat();
	}

	float flSuspicious = of2_stealth_suspicious.GetFloat();
	float flSearch = MAX( of2_stealth_search.GetFloat(), flSuspicious );

	if ( m_iState == AWARE_SEARCHING )
	{
		// A search runs its time, however long ago the evidence was
		m_flSuspicion = MAX( m_flSuspicion, flSearch );

		// ...unless it knows there is someone with a gun about. Then it does not
		// go back to its post (the user: giving up made no sense).
		if ( flNow > m_flSearchEndTime && !( m_bEnemyAbout && of2_stealth_search_forever.GetBool() ) )
		{
			m_flSuspicion = flSearch * 0.8f;
			SetState( AWARE_SUSPICIOUS );
		}
	}
	else if ( flNow - m_flLastEvidenceTime > of2_stealth_decay_delay.GetFloat() )
	{
		m_flSuspicion = MAX( 0.0f, m_flSuspicion - of2_stealth_decay.GetFloat() * dt );
	}

	if ( flNow - m_flLastSeenTime > 1.0f )
	{
		m_flDetection = MAX( 0.0f, m_flDetection - of2_stealth_see_decay.GetFloat() * dt );
	}

	// Rising is done where the evidence comes in; here it only comes down
	if ( m_iState == AWARE_SUSPICIOUS && m_flSuspicion < flSuspicious * 0.4f )
	{
		SetState( AWARE_UNAWARE );
	}

	DrawDebug();
}

//-----------------------------------------------------------------------------
// Purpose: Something points to someone being at vecPos. flAmount of 0 only
//			keeps what there is from fading.
//-----------------------------------------------------------------------------
void COF2Awareness::AddEvidence( float flAmount, const Vector &vecPos, OF2Stimulus_t kind )
{
	if ( flAmount < 0.0f || !IsEnabled() )
		return;

	// Not while fighting, and not in the middle of a scene
	if ( m_iState == AWARE_COMBAT || m_pOuter->GetEnemy() )
		return;

	if ( m_pOuter->IsInAScript() || m_pOuter->GetState() == NPC_STATE_SCRIPT )
		return;

	// Only of someone it would fight (a metrocop before the player is wanted is not on the lookout)
	CBasePlayer *pPlayer = AI_GetSinglePlayer();
	if ( pPlayer )
	{
		Disposition_t disposition = m_pOuter->IRelationType( pPlayer );
		if ( disposition != D_HT && disposition != D_FR )
			return;
	}

	float flNow = gpGlobals->curtime;

	m_flSuspicion = MIN( 1.0f, m_flSuspicion + flAmount );
	m_flLastEvidenceTime = flNow;

	if ( kind >= STIM_GUNFIRE && flAmount > 0.0f )
	{
		m_bEnemyAbout = true;
	}

	// Does this move where it thinks someone is? Not if what it has is better
	// and fresh, and it has not been there yet.
	bool bTake = ( m_iStimulus == STIM_NONE ) || ( kind >= m_iStimulus ) || m_bVisited || ( flNow - m_flStimulusTime > 5.0f );
	if ( bTake )
	{
		m_vecStimulus = vecPos;
		m_iStimulus = kind;
		m_flStimulusTime = flNow;

		// Already reacting to something: only change course for somewhere else,
		// and not on every footstep
		bool bElsewhere = ( vecPos.DistTo( m_vecActedOn ) > STEALTH_REACT_MOVE ) && ( flNow - m_flChangedTime > STEALTH_REACT_INTERVAL );

		if ( m_iState == AWARE_SEARCHING )
		{
			m_flSearchEndTime = MAX( m_flSearchEndTime, flNow + of2_stealth_search_time.GetFloat() * 0.5f );

			if ( bElsewhere )
			{
				m_bVisited = false;
				m_bChanged = true;
				m_flChangedTime = flNow;
			}
		}
		else if ( m_iState == AWARE_SUSPICIOUS && bElsewhere )
		{
			m_bWantsLook = true;
			m_bChanged = true;
			m_flChangedTime = flNow;
		}
	}

	// Raise the state here rather than a think later
	float flSuspicious = of2_stealth_suspicious.GetFloat();
	float flSearch = MAX( of2_stealth_search.GetFloat(), flSuspicious );

	if ( m_iState < AWARE_SEARCHING && m_flSuspicion >= flSearch )
	{
		SetState( AWARE_SEARCHING );
	}
	else if ( m_iState < AWARE_SUSPICIOUS && m_flSuspicion >= flSuspicious )
	{
		SetState( AWARE_SUSPICIOUS );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Sounds stay in the list for a while and are heard every think.
//			True if this one has been counted.
//-----------------------------------------------------------------------------
bool COF2Awareness::AlreadyHeard( CSound *pSound )
{
	float flExpire = pSound->SoundExpirationTime();

	for ( int i = 0; i < OF2_STEALTH_HEARD_SOUNDS; i++ )
	{
		if ( m_pHeard[i] == pSound && m_flHeardExpire[i] == flExpire )
			return true;
	}

	m_pHeard[m_iHeardNext] = pSound;
	m_flHeardExpire[m_iHeardNext] = flExpire;
	m_iHeardNext = ( m_iHeardNext + 1 ) % OF2_STEALTH_HEARD_SOUNDS;
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: The player's reserved sound: how fast they are moving, as a radius
//-----------------------------------------------------------------------------
static CSound *PlayerBodySound( void )
{
	CBasePlayer *pPlayer = AI_GetSinglePlayer();
	if ( !pPlayer )
		return NULL;

	return CSoundEnt::SoundPointerForIndex( CSoundEnt::ClientSoundIndex( pPlayer->edict() ) );
}

//-----------------------------------------------------------------------------
// Purpose: Is a SOUND_PLAYER sound loud enough here? Movement carries less far
//			than stock, and anything it cannot see the source of less again
//			(stock: an idle NPC does not hear those at all, doors included).
//-----------------------------------------------------------------------------
bool COF2Awareness::CanHearPlayerSound( CSound *pSound )
{
	float flVolume = pSound->Volume() * m_pOuter->HearingSensitivity();

	if ( pSound == PlayerBodySound() )
	{
		flVolume *= of2_stealth_hear_footsteps.GetFloat();
	}

	float flDist = pSound->GetSoundOrigin().DistTo( m_pOuter->EarPosition() );
	if ( flDist > flVolume )
		return false;

	if ( flDist > flVolume * of2_stealth_hear_walls.GetFloat() && !m_pOuter->FVisible( pSound->GetSoundReactOrigin() ) )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: After the NPC has listened: what was that worth?
//-----------------------------------------------------------------------------
void COF2Awareness::HearSounds( void )
{
	float flNow = gpGlobals->curtime;
	float dt = clamp( flNow - m_flLastListenTime, 0.0f, 0.3f );
	m_flLastListenTime = flNow;

	if ( !IsEnabled() )
		return;

	// The player's gunfire says where they are, near enough, to anyone who hears
	// it: a last known position, whether or not it can see them (in the dark, a
	// metrocop then fires at where the shot came from). In a fight too, unless it
	// has them in sight. Handed on as second-hand word, so that the engine's
	// moment of knowing exactly where a just-seen enemy is does not apply.
	CBasePlayer *pPlayer = AI_GetSinglePlayer();
	if ( pPlayer && !m_pOuter->IsInAScript() && m_pOuter->GetState() != NPC_STATE_SCRIPT && m_pOuter->IRelationType( pPlayer ) == D_HT &&
		!( m_pOuter->GetEnemy() == pPlayer && m_pOuter->HasCondition( COND_SEE_ENEMY ) ) )
	{
		AISoundIter_t iterShots;
		for ( CSound *pSound = m_pOuter->GetSenses()->GetFirstHeardSound( &iterShots ); pSound; pSound = m_pOuter->GetSenses()->GetNextHeardSound( &iterShots ) )
		{
			if ( pSound->SoundTypeNoContext() != SOUND_COMBAT || ( pSound->SoundContext() & SOUND_CONTEXT_EXPLOSION ) )
				continue;

			if ( pSound->m_hOwner.Get() != pPlayer || pSound->Volume() <= STEALTH_GUNFIRE_VOLUME || AlreadyHeard( pSound ) )
				continue;

			// Further away, less sure of the spot
			Vector vecShot = pSound->GetSoundOrigin();
			float flError = MIN( vecShot.DistTo( m_pOuter->EarPosition() ) * of2_stealth_gunfire_error.GetFloat(), 300.0f );
			vecShot.x += random->RandomFloat( -flError, flError );
			vecShot.y += random->RandomFloat( -flError, flError );

			m_bEnemyAbout = true;
			m_bHeardShot = true;
			m_pOuter->UpdateEnemyMemory( pPlayer, vecShot, pPlayer );
			m_bHeardShot = false;
		}
	}

	if ( m_iState == AWARE_COMBAT || m_pOuter->GetEnemy() )
		return;

	CSound *pBodySound = PlayerBodySound();
	Vector vecEar = m_pOuter->EarPosition();
	float flScale = of2_stealth_noise_scale.GetFloat();

	AISoundIter_t iter;
	for ( CSound *pSound = m_pOuter->GetSenses()->GetFirstHeardSound( &iter ); pSound; pSound = m_pOuter->GetSenses()->GetNextHeardSound( &iter ) )
	{
		if ( !pSound->FIsSound() )
			continue;

		// Louder where it is nearer
		float flVolume = MAX( 1.0f, pSound->Volume() * m_pOuter->HearingSensitivity() );
		float flNear = 1.0f - clamp( pSound->GetSoundOrigin().DistTo( vecEar ) / flVolume, 0.0f, 1.0f );
		float flFactor = 0.4f + 0.6f * flNear;

		if ( pSound == pBodySound )
		{
			// Goes on for as long as the player moves
			AddEvidence( of2_stealth_footstep_rate.GetFloat() * dt * flFactor, pSound->GetSoundOrigin(), STIM_FOOTSTEPS );
			continue;
		}

		// Its own side's shooting and walking about is the stock AI's to deal
		// with. One of them getting hurt is news.
		CBaseEntity *pOwner = pSound->m_hOwner;
		CAI_BaseNPC *pOwnerNPC = pOwner ? pOwner->MyNPCPointer() : NULL;
		bool bInjury = ( pSound->SoundChannel() == SOUNDENT_CHANNEL_INJURY );
		if ( pOwnerNPC && m_pOuter->IRelationType( pOwnerNPC ) == D_LI && !bInjury )
			continue;

		float flAmount;
		OF2Stimulus_t kind = STIM_NOISE;

		switch ( pSound->SoundTypeNoContext() )
		{
		case SOUND_WORLD:
			// Prop impacts: a can is a fifth of this, a thrown barrel all of it
			flAmount = clamp( flVolume / 1000.0f, 0.15f, 1.0f );
			break;

		case SOUND_PLAYER:
		case SOUND_PLAYER_VEHICLE:
			// Doors, landing from a jump
			flAmount = clamp( flVolume / 600.0f, 0.25f, 0.8f );
			break;

		case SOUND_BULLET_IMPACT:
			flAmount = 0.7f;
			break;

		case SOUND_PHYSICS_DANGER:
			flAmount = 0.5f;
			break;

		case SOUND_COMBAT:
			if ( pSound->SoundChannel() == SOUNDENT_CHANNEL_NPC_FOOTSTEP || pSound->SoundChannel() == SOUNDENT_CHANNEL_SPOOKY_NOISE )
				continue;

			// Shots and explosions leave no doubt
			flAmount = bInjury ? 0.8f : 1.5f;
			kind = STIM_GUNFIRE;
			break;

		default:
			continue;
		}

		if ( AlreadyHeard( pSound ) )
			continue;

		// The same thing still bouncing
		if ( pOwner && pOwner == m_hLastNoiseOwner.Get() && flNow - m_flLastNoiseTime < 2.0f )
		{
			flAmount *= 0.5f;
		}
		m_hLastNoiseOwner = pOwner;
		m_flLastNoiseTime = flNow;

		AddEvidence( flAmount * flFactor * flScale, pSound->GetSoundReactOrigin(), kind );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The player is within sight range. Fills the detection meter while
//			they are actually in view. True once the NPC is sure.
//-----------------------------------------------------------------------------
bool COF2Awareness::SeePlayer( CBasePlayer *pPlayer, float flDarkRate )
{
	if ( !IsEnabled() || KnowsPlayer( pPlayer ) )
		return true;

	Disposition_t disposition = m_pOuter->IRelationType( pPlayer );
	if ( disposition != D_HT && disposition != D_FR )
		return true;

	if ( m_pOuter->IsInAScript() || m_pOuter->GetState() == NPC_STATE_SCRIPT )
		return true;

	// The senses look for players several times a second. After a gap, count
	// one look's worth, so a glimpse stays a glimpse.
	float flNow = gpGlobals->curtime;
	float dt = flNow - m_flLastLookTime;
	if ( dt > 0.3f )
	{
		dt = 0.1f;
	}
	m_flLastLookTime = flNow;

	if ( IsPlayerHidden( pPlayer, flDarkRate ) )
		return false;

	if ( !m_pOuter->FInViewCone( pPlayer ) || !m_pOuter->FVisible( pPlayer ) )
		return false;

	m_flLastSeenTime = flNow;

	Vector vecTo = pPlayer->WorldSpaceCenter() - m_pOuter->EyePosition();
	float flDist = vecTo.Length();
	if ( flDist <= of2_stealth_see_instant.GetFloat() )
	{
		m_flDetection = 1.0f;
		return true;
	}

	// Distance
	float flNear = of2_stealth_see_near.GetFloat();
	float flFar = MAX( of2_stealth_see_far.GetFloat(), flNear + 1.0f );
	float flFrac = 1.0f - clamp( ( flDist - flNear ) / ( flFar - flNear ), 0.0f, 1.0f );
	float flRate = MAX( of2_stealth_see_far_factor.GetFloat(), flFrac * flFrac );

	// Light. With night vision (1) it makes no difference; without, the rate
	// falls with it, to nothing where IsPlayerHidden() takes over.
	flRate *= Lerp( OF2_GetPlayerVisibility( pPlayer ), clamp( flDarkRate, 0.0f, 1.0f ), 1.0f );

	// Movement and posture
	float flSpeed = pPlayer->GetAbsVelocity().Length();
	if ( flSpeed < 20.0f )
	{
		flRate *= of2_stealth_see_still.GetFloat();
	}
	else if ( flSpeed > 250.0f )
	{
		flRate *= of2_stealth_see_sprint.GetFloat();
	}

	if ( pPlayer->GetFlags() & FL_DUCKING )
	{
		flRate *= of2_stealth_see_crouch.GetFloat();
	}

	// Corner of the eye
	Vector2D vecTo2D = vecTo.AsVector2D();
	Vector2DNormalize( vecTo2D );
	float flDot = DotProduct2D( vecTo2D, m_pOuter->EyeDirection2D().AsVector2D() );
	flRate *= RemapValClamped( flDot, -0.2f, 0.5f, of2_stealth_see_edge.GetFloat(), 1.0f );

	// Looking for trouble
	if ( m_iState == AWARE_SEARCHING )
	{
		flRate *= of2_stealth_see_alert.GetFloat();
	}
	else if ( m_iState == AWARE_SUSPICIOUS )
	{
		flRate *= ( 1.0f + of2_stealth_see_alert.GetFloat() ) * 0.5f;
	}

	m_flDetection += flRate * dt / MAX( of2_stealth_see_time.GetFloat(), 0.01f );
	if ( m_flDetection >= 1.0f )
	{
		m_flDetection = 1.0f;
		return true;
	}

	// Not sure yet: suspicion keeps pace with what it has seen, and where it
	// saw it is where it will look
	AddEvidence( MAX( 0.0f, m_flDetection - m_flSuspicion ), pPlayer->GetAbsOrigin(), STIM_GLIMPSE );
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: A squadmate passes on where it sees an enemy. Stock, that makes this
//			NPC know it too. One that is not in the fight yet takes it as
//			somewhere to go and look.
//-----------------------------------------------------------------------------
bool COF2Awareness::TakeEnemyReport( CBaseEntity *pEnemy, const Vector &vecPos, CBaseEntity *pInformer )
{
	if ( !pEnemy || !pEnemy->IsPlayer() || !pInformer || pInformer == m_pOuter )
		return false;

	// Its own ears (HearSounds)
	if ( m_bHeardShot )
		return false;

	if ( !IsEnabled() || !of2_stealth_squad.GetBool() )
		return false;

	if ( KnowsPlayer( static_cast<CBasePlayer *>( pEnemy ) ) )
		return false;

	if ( m_pOuter->IsInAScript() || m_pOuter->GetState() == NPC_STATE_SCRIPT )
		return false;

	// The squadmate keeps seeing them. It does not get a running commentary.
	if ( gpGlobals->curtime < m_flNextReportTime )
		return true;

	m_flNextReportTime = gpGlobals->curtime + of2_stealth_report_interval.GetFloat();

	m_flDetection = MAX( m_flDetection, 0.5f );
	AddEvidence( 1.0f, vecPos, STIM_REPORT );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Stock, a squad's new enemy is set on every member that has none,
//			memory or not. Same rule as above: not for those not in the fight.
//-----------------------------------------------------------------------------
bool COF2Awareness::AcceptsSquadEnemy( CBaseEntity *pEnemy )
{
	if ( !pEnemy || !pEnemy->IsPlayer() || !IsEnabled() || !of2_stealth_squad.GetBool() )
		return true;

	return KnowsPlayer( static_cast<CBasePlayer *>( pEnemy ) );
}

//-----------------------------------------------------------------------------
// Purpose: Hurt by the player. Stock, an NPC hit by someone it does not see
//			only learns that it was hit. It still has to see who did it, but
//			it will know them the moment it does, and it knows which way.
//-----------------------------------------------------------------------------
void COF2Awareness::TookDamage( CBaseEntity *pAttacker )
{
	if ( !pAttacker || !pAttacker->IsPlayer() || !IsEnabled() )
		return;

	if ( KnowsPlayer( static_cast<CBasePlayer *>( pAttacker ) ) )
		return;

	m_flDetection = 1.0f;
	m_flLastSeenTime = gpGlobals->curtime;	// holds the meter for a moment

	// g_vecAttackDir: towards where it came from, as CAI_BaseNPC::OnTakeDamage_Alive uses it
	AddEvidence( 1.0f, m_pOuter->GetAbsOrigin() + g_vecAttackDir * 200.0f, STIM_GUNFIRE );
}

//-----------------------------------------------------------------------------
// Purpose: A squadmate is going to look at something. Enough to make this one
//			stop and watch that way, not to send it along.
//-----------------------------------------------------------------------------
void COF2Awareness::TakeAlert( const Vector &vecPos )
{
	if ( !IsEnabled() || m_iState >= AWARE_SEARCHING )
		return;

	float flWanted = of2_stealth_suspicious.GetFloat() + 0.1f;
	AddEvidence( MAX( 0.0f, flWanted - m_flSuspicion ), vecPos, STIM_NOISE );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void COF2Awareness::AlertSquad( void )
{
	CAI_Squad *pSquad = m_pOuter->GetSquad();
	if ( !pSquad || !of2_stealth_squad.GetBool() )
		return;

	float flRadiusSqr = Square( of2_stealth_squad_radius.GetFloat() );

	AISquadIter_t iter;
	for ( CAI_BaseNPC *pMember = pSquad->GetFirstMember( &iter ); pMember; pMember = pSquad->GetNextMember( &iter ) )
	{
		if ( pMember == m_pOuter || pMember->GetAbsOrigin().DistToSqr( m_pOuter->GetAbsOrigin() ) > flRadiusSqr )
			continue;

		pMember->OF2_StealthAlert( m_vecStimulus );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Somewhere around the stimulus to walk to and look. Nodes it cannot
//			see from here come first: behind things is where people hide.
//-----------------------------------------------------------------------------
bool COF2Awareness::PickSearchPoint( Vector *pResult )
{
	CAI_Network *pNetwork = m_pOuter->GetNavigator()->GetNetwork();
	int nNodes = pNetwork ? pNetwork->NumNodes() : 0;
	if ( nNodes <= 0 )
		return false;

	float flRadius = of2_stealth_search_radius.GetFloat();
	if ( m_bEnemyAbout && of2_stealth_search_forever.GetBool() )
	{
		// The longer since the last word, the further they may have got
		flRadius += ( gpGlobals->curtime - m_flStimulusTime ) * of2_stealth_search_spread.GetFloat();
		flRadius = MIN( flRadius, MAX( of2_stealth_search_radius_max.GetFloat(), of2_stealth_search_radius.GetFloat() ) );
	}
	Vector vecOrigin = m_pOuter->GetAbsOrigin();

	bool bFound = false;
	float flBest = 0.0f;
	int nTraces = 0;
	int iStart = random->RandomInt( 0, nNodes - 1 );

	for ( int i = 0; i < nNodes; i++ )
	{
		CAI_Node *pNode = pNetwork->GetNode( ( iStart + i ) % nNodes, false );
		if ( !pNode || pNode->GetType() != NODE_GROUND )
			continue;

		Vector vecNode = pNode->GetPosition( m_pOuter->GetHullType() );
		if ( fabs( vecNode.z - m_vecStimulus.z ) > STEALTH_NODE_MAX_DZ )
			continue;

		if ( ( vecNode - m_vecStimulus ).Length2D() > flRadius || vecNode.DistTo( vecOrigin ) < STEALTH_NODE_MIN_DIST )
			continue;

		float flScore = random->RandomFloat( 0.0f, 1.0f );
		if ( nTraces < STEALTH_NODE_TRACES )
		{
			nTraces++;
			if ( !m_pOuter->FVisible( vecNode + Vector( 0, 0, 32 ) ) )
			{
				flScore += 1.0f;
			}
		}

		if ( !bFound || flScore > flBest )
		{
			bFound = true;
			flBest = flScore;
			*pResult = vecNode;
		}
	}

	return bFound;
}

//-----------------------------------------------------------------------------
// Purpose: Can an NPC without night vision not see the player for darkness?
//			This holds for an enemy it is already fighting too: step into the
//			dark and it loses sight of you. A light on the player (its own
//			flashlight, anyone's) counts as light.
//-----------------------------------------------------------------------------
bool COF2Awareness::IsPlayerHidden( CBasePlayer *pPlayer, float flDarkRate )
{
	if ( flDarkRate > 0.0f || !IsEnabled() )
		return false;

	if ( m_pOuter->IsInAScript() || m_pOuter->GetState() == NPC_STATE_SCRIPT )
		return false;

	return OF2_GetPlayerVisibility( pPlayer ) <= of2_stealth_blind.GetFloat();
}

//-----------------------------------------------------------------------------
// Purpose: It is fighting the player, does not see them, and has a place it
//			last knew them to be: is that worth shooting at? The stock aim is at
//			the last known position already; this says when to pull the trigger.
//			Yes while the word is fresh and the shot gets there, or as good as
//			(the crate they ducked behind). Not once it has seen the place
//			empty: Update() drops the enemy then.
//-----------------------------------------------------------------------------
bool COF2Awareness::ShouldSuppressLKP( void )
{
	if ( !IsEnabled() || !of2_stealth_suppress.GetBool() )
		return false;

	CBaseEntity *pEnemy = m_pOuter->GetEnemy();
	if ( !pEnemy || !pEnemy->IsPlayer() || !m_pOuter->GetEnemies()->HasMemory( pEnemy ) )
		return false;

	if ( m_pOuter->HasCondition( COND_SEE_ENEMY ) || !m_pOuter->GetActiveWeapon() || !( m_pOuter->CapabilitiesGet() & bits_CAP_WEAPON_RANGE_ATTACK1 ) )
		return false;

	float flLastWord = MAX( m_flEnemyInfoTime, m_pOuter->GetEnemyLastTimeSeen() );
	if ( gpGlobals->curtime - flLastWord > of2_stealth_suppress_time.GetFloat() )
		return false;

	Vector vecSpot = m_pOuter->GetEnemyLKP() + Vector( 0, 0, STEALTH_LKP_HEIGHT );
	Vector vecSrc = m_pOuter->Weapon_ShootPosition();
	float flDist = vecSrc.DistTo( vecSpot );
	if ( flDist < 64.0f || flDist > of2_stealth_suppress_range.GetFloat() )
		return false;

	trace_t tr;
	UTIL_TraceLine( vecSrc, vecSpot, MASK_SHOT, m_pOuter, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction < 1.0f && tr.m_pEnt != pEnemy )
	{
		// Not through its own side
		CAI_BaseNPC *pHit = tr.m_pEnt ? tr.m_pEnt->MyNPCPointer() : NULL;
		if ( pHit && m_pOuter->IRelationType( pHit ) == D_LI )
			return false;

		if ( tr.endpos.DistTo( vecSpot ) > of2_stealth_suppress_near.GetFloat() )
			return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: What to do about evidence that is not an enemy yet
//-----------------------------------------------------------------------------
OF2StealthSched_t COF2Awareness::SelectSchedule( bool bHasDuties )
{
	if ( !IsEnabled() || m_pOuter->GetEnemy() )
		return STEALTH_SCHED_NONE;

	if ( m_pOuter->GetState() != NPC_STATE_IDLE && m_pOuter->GetState() != NPC_STATE_ALERT )
		return STEALTH_SCHED_NONE;

	TakeChanged();

	switch ( m_iState )
	{
	case AWARE_SUSPICIOUS:
		if ( m_bWantsLook )
			return STEALTH_SCHED_LOOK;
		break;

	case AWARE_SEARCHING:
		if ( m_pOuter->GetState() == NPC_STATE_IDLE )
		{
			m_pOuter->SetIdealState( NPC_STATE_ALERT );
		}
		return m_bVisited ? STEALTH_SCHED_SEARCH : STEALTH_SCHED_INVESTIGATE;

	case AWARE_UNAWARE:
		if ( m_bWantsReturn )
		{
			// Back to where it stood, unless it has somewhere to be
			if ( bHasDuties || m_pOuter->GetGoalEnt() || ( m_pOuter->GetAbsOrigin() - m_vecHome ).Length2D() < STEALTH_GOAL_TOLERANCE )
			{
				m_bWantsReturn = false;
				break;
			}
			return STEALTH_SCHED_RETURN;
		}
		break;
	}

	return STEALTH_SCHED_NONE;
}

//-----------------------------------------------------------------------------
// Purpose: The tasks of the stealth schedules, for whichever class runs them
//			True: the NPC has been told which way to face, and the owner starts
//			TASK_FACE_IDEAL (ChainStartTask is the owner's to call).
//-----------------------------------------------------------------------------
bool COF2Awareness::StartTask( OF2StealthTask_t task )
{
	bool bPath = false;

	switch ( task )
	{
	case STEALTH_TASK_FACE_STIMULUS:
		m_pOuter->GetMotor()->SetIdealYawToTarget( m_vecStimulus );
		return true;

	case STEALTH_TASK_LOOKED:
		m_bWantsLook = false;
		m_pOuter->TaskComplete();
		return false;

	case STEALTH_TASK_ARRIVED:
		m_bVisited = true;
		m_pOuter->TaskComplete();
		return false;

	case STEALTH_TASK_FACE_HOME:
		// Home or not, this is as far as it goes
		m_bWantsReturn = false;
		if ( m_pOuter->GetState() == NPC_STATE_ALERT )
		{
			m_pOuter->SetIdealState( NPC_STATE_IDLE );
		}
		m_pOuter->GetMotor()->SetIdealYaw( m_flHomeYaw );
		return true;

	case STEALTH_TASK_GET_PATH_TO_STIMULUS:
		bPath = GetPathTo( m_vecStimulus, IsUrgent() );
		break;

	case STEALTH_TASK_GET_SEARCH_PATH:
		{
			Vector vecPoint;
			bPath = PickSearchPoint( &vecPoint ) && GetPathTo( vecPoint, false );
		}
		break;

	case STEALTH_TASK_GET_PATH_HOME:
		bPath = GetPathTo( m_vecHome, false );
		break;
	}

	if ( bPath )
	{
		m_pOuter->TaskComplete();
	}
	else
	{
		m_pOuter->TaskFail( FAIL_NO_ROUTE );
	}

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool COF2Awareness::GetPathTo( const Vector &vecGoal, bool bRun )
{
	Activity activity = bRun ? ACT_RUN : ACT_WALK;
	CAI_Navigator *pNavigator = m_pOuter->GetNavigator();

	// Things land on tables and ledges: start from the floor under it
	trace_t tr;
	UTIL_TraceLine( vecGoal + Vector( 0, 0, 8 ), vecGoal - Vector( 0, 0, 256 ), MASK_NPCSOLID_BRUSHONLY, m_pOuter, COLLISION_GROUP_NONE, &tr );
	Vector vecFloor = ( tr.startsolid || tr.fraction == 1.0f ) ? vecGoal : tr.endpos;

	if ( pNavigator->SetGoal( AI_NavGoal_t( vecFloor, activity, STEALTH_GOAL_TOLERANCE ), AIN_NO_PATH_TASK_FAIL ) )
		return true;

	// No way there: as near as the node graph gets
	CAI_Network *pNetwork = pNavigator->GetNetwork();
	int iNode = pNetwork ? pNetwork->NearestNodeToPoint( m_pOuter, vecFloor, false ) : NO_NODE;
	CAI_Node *pNode = ( iNode != NO_NODE ) ? pNetwork->GetNode( iNode, false ) : NULL;
	if ( !pNode )
		return false;

	return pNavigator->SetGoal( AI_NavGoal_t( pNode->GetPosition( m_pOuter->GetHullType() ), activity, STEALTH_GOAL_TOLERANCE ), AIN_NO_PATH_TASK_FAIL );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void COF2Awareness::DrawDebug( void )
{
	if ( !of2_stealth_debug.GetBool() )
		return;

	static const char *pszStates[] = { "unaware", "suspicious", "searching", "combat" };
	static const char *pszStimuli[] = { "", "noise", "footsteps", "glimpse", "gunfire", "report", "lost enemy" };

	int r = 255, g = 255, b = 255;
	switch ( m_iState )
	{
	case AWARE_SUSPICIOUS:	g = 220; b = 0; break;
	case AWARE_SEARCHING:	g = 130; b = 0; break;
	case AWARE_COMBAT:		g = 30; b = 30; break;
	}

	char szText[128];
	Q_snprintf( szText, sizeof( szText ), "%s  susp %.2f  seen %.2f  %s", pszStates[m_iState], m_flSuspicion, m_flDetection, pszStimuli[m_iStimulus] );
	NDebugOverlay::Text( m_pOuter->EyePosition() + Vector( 0, 0, 20 ), szText, false, 0.15f );

	if ( m_iState == AWARE_SUSPICIOUS || m_iState == AWARE_SEARCHING )
	{
		NDebugOverlay::Cross3D( m_vecStimulus, 12.0f, r, g, b, true, 0.15f );
		NDebugOverlay::Line( m_pOuter->EyePosition(), m_vecStimulus, r, g, b, true, 0.15f );
	}
}

//-----------------------------------------------------------------------------
// Player visibility. One player, so one value.
//-----------------------------------------------------------------------------
static float s_flPlayerVisibility = 1.0f;
static float s_flPlayerVisibilityTime = -1.0f;
static float s_flPlayerLitUntil = -1.0f;

void OF2_PlayerLit( float flDuration )
{
	s_flPlayerLitUntil = gpGlobals->curtime + flDuration;
}

void OF2_SetPlayerVisibility( CBasePlayer *pPlayer, float flVisibility )
{
	s_flPlayerVisibility = clamp( flVisibility, 0.0f, 1.0f );
	s_flPlayerVisibilityTime = gpGlobals->curtime;

	if ( of2_stealth_debug.GetBool() )
	{
		engine->Con_NPrintf( 20, "visibility %.2f", s_flPlayerVisibility );
	}
}

float OF2_GetPlayerVisibility( CBasePlayer *pPlayer )
{
	// In someone's flashlight (the second test: a time from before a load)
	if ( gpGlobals->curtime < s_flPlayerLitUntil && s_flPlayerLitUntil - gpGlobals->curtime < 5.0f )
		return 1.0f;

	// Nothing from the client lately (or a value from before a load): in plain view
	if ( s_flPlayerVisibilityTime < 0.0f || fabs( gpGlobals->curtime - s_flPlayerVisibilityTime ) > 2.0f )
		return 1.0f;

	return s_flPlayerVisibility;
}

//-----------------------------------------------------------------------------
// Purpose: The loudest thing the player is doing, for the HUD. The same sounds
//			NPCs hear: the reserved one for moving about (as far as it carries
//			to stealth NPCs) and anything else of the player's in the sound
//			list (landing, the knife, gunfire). On a log scale, since footsteps
//			carry tens of units and a shot thousands. A thrown prop's noise is
//			the prop's own and is not in this.
//-----------------------------------------------------------------------------
#define STEALTH_NOISE_METER_MIN		30.0f	// radius that shows as nothing
#define STEALTH_NOISE_METER_MAX		3000.0f	// radius that fills the meter
#define STEALTH_NOISE_METER_DECAY	0.9f	// how fast a peak falls, in meters a second

float OF2_GetPlayerNoise( CBasePlayer *pPlayer )
{
	static float s_flNoise = 0.0f;
	static float s_flNoiseTime = 0.0f;

	float flRadius = 0.0f;

	CSound *pBodySound = PlayerBodySound();
	if ( pBodySound )
	{
		flRadius = pBodySound->Volume() * of2_stealth_hear_footsteps.GetFloat();
	}

	for ( int iSound = CSoundEnt::ActiveList(); iSound != SOUNDLIST_EMPTY; )
	{
		CSound *pSound = CSoundEnt::SoundPointerForIndex( iSound );
		if ( !pSound )
			break;

		if ( pSound != pBodySound && pSound->FIsSound() && pSound->m_hOwner.Get() == pPlayer )
		{
			flRadius = MAX( flRadius, (float)pSound->Volume() );
		}

		iSound = pSound->NextSound();
	}

	float flLevel = 0.0f;
	if ( flRadius > STEALTH_NOISE_METER_MIN )
	{
		flLevel = clamp( log( flRadius / STEALTH_NOISE_METER_MIN ) / log( STEALTH_NOISE_METER_MAX / STEALTH_NOISE_METER_MIN ), 0.0f, 1.0f );
	}

	// (The clock goes back on a load)
	float dt = clamp( gpGlobals->curtime - s_flNoiseTime, 0.0f, 0.5f );
	s_flNoiseTime = gpGlobals->curtime;
	s_flNoise = MAX( flLevel, s_flNoise - STEALTH_NOISE_METER_DECAY * dt );

	return s_flNoise;
}

//-----------------------------------------------------------------------------
// Purpose: Noise from a prop hitting something
//-----------------------------------------------------------------------------
void OF2_PropImpactNoise( CBaseEntity *pProp, int index, gamevcollisionevent_t *pEvent, float &flNextTime )
{
	if ( !of2_stealth_impact.GetBool() )
		return;

	float flNow = gpGlobals->curtime;
	if ( flNow < flNextTime || flNow < STEALTH_MAP_SETTLE_TIME )
		return;

	float flSpeed = pEvent->collisionSpeed;
	if ( flSpeed < of2_stealth_impact_min_speed.GetFloat() )
		return;

	// An NPC wading through cans should not send its squad looking
	CBaseEntity *pOther = pEvent->pEntities[!index];
	if ( pOther && pOther->IsNPC() )
		return;

	// Heavier is louder, but not in proportion: a can at throwing speed has to
	// carry across a room
	float flMass = MIN( pEvent->pObjects[index]->GetMass(), 100.0f );
	float flRadius = flSpeed * ( 0.6f + 0.25f * sqrt( flMass ) ) * of2_stealth_impact_scale.GetFloat();
	flRadius = MIN( flRadius, of2_stealth_impact_max.GetFloat() );
	if ( flRadius < 1.0f )
		return;

	Vector vecPos;
	pEvent->pInternalData->GetContactPoint( vecPos );

	CSoundEnt::InsertSound( SOUND_WORLD, vecPos, (int)flRadius, 0.3f, pProp );
	flNextTime = flNow + 0.25f;

	if ( of2_stealth_debug.GetBool() )
	{
		char szText[32];
		Q_snprintf( szText, sizeof( szText ), "noise %d", (int)flRadius );
		NDebugOverlay::Text( vecPos, szText, false, 2.0f );
		NDebugOverlay::Cross3D( vecPos, 6.0f, 255, 255, 255, true, 2.0f );
	}
}
