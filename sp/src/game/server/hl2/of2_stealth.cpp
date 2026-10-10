
	// Picked to go along: nobody looks into things alone
	if ( s_bAlertRecruits )
	{
		m_flDetection = MAX( m_flDetection, 0.3f );
		AddEvidence( 1.0f, vecPos, STIM_REPORT );
		return;
	}
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
#include "ai_behavior_follow.h"
#include "soundent.h"
#include "player.h"
#include "physics.h"
#include "ndebugoverlay.h"
#include "Sprite.h"
#include "spotlightend.h"
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
static ConVar of2_stealth_hear_footsteps( "of2_stealth_hear_footsteps", "0.75", FCVAR_NONE, "Stealth: how far the player's movement carries when slow (crouched, sneaking), as a multiple of their speed in units. Stock is 1." );
static ConVar of2_stealth_hear_running( "of2_stealth_hear_running", "2.4", FCVAR_NONE, "Stealth: how far the player's movement carries at a run and faster, as a multiple of their speed in units: 2.4 is about 11 m at a run (190) and 19 m sprinting. Between sneaking and running it goes from the one to the other." );
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

static ConVar of2_stealth_search_partners( "of2_stealth_search_partners", "1", FCVAR_NONE, "Stealth: an NPC that goes to look into something by itself takes this many of the nearest squadmates along." );
// Bodies
static ConVar of2_stealth_bodies( "of2_stealth_bodies", "1", FCVAR_NONE, "Stealth: Combine who come across one of their own dead know an enemy is about and search, and a squad only hears of a death one of them saw or that happens while they are alerted (0 = stock: bodies are ignored, every death is called out)." );
static ConVar of2_stealth_body_dist( "of2_stealth_body_dist", "800", FCVAR_NONE, "Stealth: how far off an NPC notices a body in its view. Without night vision it also has to be lit: by the room or by its flashlight." );

#define STEALTH_NODE_MAX_DZ			128.0f	// search points this far above or below the stimulus are another floor
#define STEALTH_NODE_MIN_DIST		96.0f	// not worth walking to
#define STEALTH_NODE_TRACES			12		// sight checks per pick
#define STEALTH_REACT_MOVE			150.0f	// a stimulus has to move this far to make a searching NPC change course
#define STEALTH_REACT_INTERVAL		1.5f	// and it does that no more often than this
#define STEALTH_MAP_SETTLE_TIME		3.0f	// props dropping into place at map start are not noise
#define STEALTH_LKP_HEIGHT			40.0f	// chest height over a last known position
#define STEALTH_GUNFIRE_VOLUME		500		// SOUNDENT_VOLUME_EMPTY: the player's combat sounds louder than a dry click are shots
#define STEALTH_GOAL_TOLERANCE		48.0f	// near enough to a place it was drawn to
#define STEALTH_BODY_LOOK_INTERVAL	0.5f	// how often an NPC looks about for bodies
#define STEALTH_BODY_HEIGHT			16.0f	// a body lies about this far above where the NPC stood
#define STEALTH_SEE_SLACK			32.0f	// a line of sight that ends this near a place has reached it

// Inside AlertSquad(): the squadmate being told is to come along, not just look that way
static bool s_bAlertRecruits = false;

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
	DEFINE_FIELD( m_bFollowing,			FIELD_BOOLEAN ),
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
	m_pLight = NULL;
	m_flNextBodyLook = 0.0f;
	m_bFoundBody = false;
	m_flNextSearchLook = 0.0f;
	m_bSayClear = false;
	m_bSearchFace = false;
	m_vecSearchFace.Init();
	m_bFollowing = false;
	m_bSearchCrouch = false;
	m_flStandTime = 0.0f;
	m_iCrouchWant = 0;
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

	// Down to look at a sweep spot: up again when that is done, or anything comes up
	if ( m_flStandTime != 0.0f && ( flNow > m_flStandTime || flNow < m_flStandTime - 30.0f || m_pOuter->GetEnemy() || m_pOuter->IsMoving() || m_iState != AWARE_SEARCHING ) )
	{
		m_flStandTime = 0.0f;
		m_iCrouchWant = -1;
	}
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

	LookForBodies();

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

	if ( m_iState == AWARE_SEARCHING && m_bVisited )
	{
		SearchLook();
	}

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
// Purpose: How far the player's movement is heard, from how far stock has it
//			(as many units as their speed). For CBasePlayer::UpdatePlayerSound:
//			it has to be in the sound itself, since no NPC is asked about a
//			sound further off than its volume. Sneaking is quieter than stock,
//			so that a crouched player still gets within reach of a knife;
//			running is a good deal louder (the user: about 10 m behind a cop).
//-----------------------------------------------------------------------------
float OF2_PlayerMovementNoise( float flSpeed )
{
	if ( !of2_stealth.GetBool() )
		return flSpeed;

	return flSpeed * RemapValClamped( flSpeed, 80.0f, 190.0f, of2_stealth_hear_footsteps.GetFloat(), of2_stealth_hear_running.GetFloat() );
}

//-----------------------------------------------------------------------------
// Purpose: Is a SOUND_PLAYER sound loud enough here? Anything it cannot see
//			the source of carries less far
//			(stock: an idle NPC does not hear those at all, doors included).
//-----------------------------------------------------------------------------
bool COF2Awareness::CanHearPlayerSound( CSound *pSound )
{
	float flVolume = pSound->Volume() * m_pOuter->HearingSensitivity();

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
			// Prop impacts: something nudged makes it look up, something thrown
			// (a bottle is 550 or so) is worth going to see
			flAmount = clamp( flVolume / 700.0f, 0.3f, 1.0f );
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

	// The nearest few come along (the user: groups of two at least); the rest stop and
	// look that way
	CAI_BaseNPC *pPartners[4] = { NULL, NULL, NULL, NULL };
	int nPartners = clamp( of2_stealth_search_partners.GetInt(), 0, 4 );
	for ( int i = 0; i < nPartners; i++ )
	{
		float flNearest = flRadiusSqr;
		AISquadIter_t iterNear;
		for ( CAI_BaseNPC *pMember = pSquad->GetFirstMember( &iterNear ); pMember; pMember = pSquad->GetNextMember( &iterNear ) )
		{
			float flDistSqr = pMember->GetAbsOrigin().DistToSqr( m_pOuter->GetAbsOrigin() );
			if ( pMember == m_pOuter || !pMember->IsAlive() || pMember->GetEnemy() || flDistSqr >= flNearest )
				continue;

			if ( pMember == pPartners[0] || pMember == pPartners[1] || pMember == pPartners[2] || pMember == pPartners[3] )
				continue;

			flNearest = flDistSqr;
			pPartners[i] = pMember;
		}
	}

	AISquadIter_t iter;
	for ( CAI_BaseNPC *pMember = pSquad->GetFirstMember( &iter ); pMember; pMember = pSquad->GetNextMember( &iter ) )
	{
		if ( pMember == m_pOuter || pMember->GetAbsOrigin().DistToSqr( m_pOuter->GetAbsOrigin() ) > flRadiusSqr )
			continue;

		s_bAlertRecruits = ( pMember == pPartners[0] || pMember == pPartners[1] || pMember == pPartners[2] || pMember == pPartners[3] );
		pMember->OF2_StealthAlert( m_vecStimulus );
		s_bAlertRecruits = false;
	}
}

//-----------------------------------------------------------------------------
// Bodies. The client keeps g_ragdoll_maxcount ragdolls and fades the oldest,
// so that many places are remembered.
//-----------------------------------------------------------------------------
struct OF2Body_t
{
	EHANDLE	hVictim;		// until it is removed, a moment after dying
	EHANDLE	hRagdoll;		// the server's ragdoll, if there is one
	bool	bHadRagdoll;
	Vector	vecPos;
	CUtlVector<EHANDLE> seenBy;
};

static CUtlVector<OF2Body_t *> s_Bodies;

class COF2BodyList : public CAutoGameSystem
{
public:
	COF2BodyList() : CAutoGameSystem( "COF2BodyList" ) {}
	virtual void LevelShutdownPostEntity( void )	{ s_Bodies.PurgeAndDeleteElements(); }
};
static COF2BodyList s_BodyList;

void OF2_BodyAdd( CBaseEntity *pVictim, const CTakeDamageInfo &info )
{
	if ( !pVictim || !of2_stealth_bodies.GetBool() )
		return;

	// Nothing is left of one that was dissolved
	if ( info.GetDamageType() & DMG_DISSOLVE )
		return;

	extern ConVar g_ragdoll_maxcount;
	int nMax = MAX( g_ragdoll_maxcount.GetInt(), 1 );
	while ( s_Bodies.Count() >= nMax )
	{
		delete s_Bodies[0];
		s_Bodies.Remove( 0 );
	}

	OF2Body_t *pBody = new OF2Body_t;
	pBody->hVictim = pVictim;
	pBody->bHadRagdoll = false;
	pBody->vecPos = pVictim->GetAbsOrigin() + Vector( 0, 0, STEALTH_BODY_HEIGHT );
	s_Bodies.AddToTail( pBody );
}

static CHandle<CBasePlayer> s_hKillLeadsTo;

void OF2_KillLeadsTo( CBasePlayer *pKiller )
{
	s_hKillLeadsTo = pKiller;
}

void OF2_BodyRagdoll( CBaseEntity *pVictim, CBaseEntity *pRagdoll )
{
	for ( int i = 0; i < s_Bodies.Count(); i++ )
	{
		if ( pVictim && pRagdoll && s_Bodies[i]->hVictim.Get() == pVictim )
		{
			s_Bodies[i]->hRagdoll = pRagdoll;
			s_Bodies[i]->bHadRagdoll = true;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: In view, nothing in the way, and light enough to see by
//-----------------------------------------------------------------------------
bool COF2Awareness::SeesPlace( const Vector &vecPos, float flMaxDist, bool bNeedLight )
{
	Vector vecEyes = m_pOuter->EyePosition();
	if ( vecPos.DistToSqr( vecEyes ) > Square( flMaxDist ) || !m_pOuter->FInViewCone( vecPos ) )
		return false;

	// No night vision: only what the room or its own flashlight lights
	if ( bNeedLight && m_flDarkRate <= 0.0f && m_pLight && !m_pLight->Lights( vecPos ) )
		return false;

	// (Not FVisible: what lies at the place, a ragdoll, would block that)
	trace_t tr;
	UTIL_TraceLine( vecEyes, vecPos, MASK_BLOCKLOS, m_pOuter, COLLISION_GROUP_NONE, &tr );
	return tr.fraction == 1.0f || tr.endpos.DistToSqr( vecPos ) < Square( STEALTH_SEE_SLACK );
}

//-----------------------------------------------------------------------------
// Purpose: Every so often: is there a body in view that it has not seen yet?
//			A dead comrade means an armed enemy, so it reacts as to gunfire:
//			it goes there, and it does not stand down afterwards.
//-----------------------------------------------------------------------------
void COF2Awareness::LookForBodies( void )
{
	if ( gpGlobals->curtime < m_flNextBodyLook )
		return;

	m_flNextBodyLook = gpGlobals->curtime + STEALTH_BODY_LOOK_INTERVAL + random->RandomFloat( 0.0f, 0.1f );

	if ( !of2_stealth_bodies.GetBool() )
		return;

	float flDist = of2_stealth_body_dist.GetFloat();

	for ( int i = s_Bodies.Count() - 1; i >= 0; i-- )
	{
		OF2Body_t *pBody = s_Bodies[i];

		if ( pBody->bHadRagdoll )
		{
			// Carried off, eaten, cleaned up: no body
			CBaseEntity *pRagdoll = pBody->hRagdoll.Get();
			if ( !pRagdoll )
			{
				delete pBody;
				s_Bodies.Remove( i );
				continue;
			}
			pBody->vecPos = pRagdoll->WorldSpaceCenter();
		}

		if ( of2_stealth_debug.GetBool() )
		{
			NDebugOverlay::Cross3D( pBody->vecPos, 10.0f, 200, 0, 255, true, STEALTH_BODY_LOOK_INTERVAL + 0.15f );
		}

		if ( pBody->seenBy.Find( m_pOuter ) != pBody->seenBy.InvalidIndex() )
			continue;

		if ( !SeesPlace( pBody->vecPos, flDist ) )
			continue;

		pBody->seenBy.AddToTail( m_pOuter );

		// (Evidence is not taken in a scene, or by one that is no enemy of the player)
		float flBefore = m_flLastEvidenceTime;
		AddEvidence( 1.0f, pBody->vecPos, STIM_BODY );
		if ( m_flLastEvidenceTime != flBefore )
		{
			m_bFoundBody = true;
		}
		return;
	}
}

//-----------------------------------------------------------------------------
// Purpose: A squadmate has died. Stock, the whole squad calls it out wherever
//			they are, which gives a quiet kill away. Now only those say it who
//			can know: whoever saw it, and anyone already alerted (Overwatch
//			tells them). To the rest it is a body to come across.
//-----------------------------------------------------------------------------
bool COF2Awareness::WitnessDeath( CBaseEntity *pFriend )
{
	if ( !IsEnabled() || !of2_stealth_bodies.GetBool() || !pFriend )
		return true;

	OF2Body_t *pBody = NULL;
	for ( int i = 0; i < s_Bodies.Count(); i++ )
	{
		if ( s_Bodies[i]->hVictim.Get() == pFriend )
		{
			pBody = s_Bodies[i];
		}
	}

	bool bKnows = ( m_iState >= AWARE_SEARCHING ) || m_pOuter->GetEnemy() != NULL;
	bool bSaw = SeesPlace( pFriend->WorldSpaceCenter(), of2_stealth_body_dist.GetFloat() );

	if ( bSaw )
	{
		// Killed on the end of a Barnacle's tongue: that leads straight back to the player
		// (the user asked for this). As with a shot it heard, it knows where they are.
		CBasePlayer *pKiller = s_hKillLeadsTo.Get();
		if ( pKiller )
		{
			m_bEnemyAbout = true;
			m_flDetection = 1.0f;
			m_flLastSeenTime = gpGlobals->curtime;

			m_bHeardShot = true;
			m_pOuter->UpdateEnemyMemory( pKiller, pKiller->GetAbsOrigin(), pKiller );
			m_bHeardShot = false;
		}

		AddEvidence( 1.0f, pFriend->GetAbsOrigin(), STIM_BODY );
	}

	// Either way this body is no news to it later
	if ( pBody && ( bKnows || bSaw ) && pBody->seenBy.Find( m_pOuter ) == pBody->seenBy.InvalidIndex() )
	{
		pBody->seenBy.AddToTail( m_pOuter );
	}

	return bKnows || bSaw;
}

//=============================================================================
// OF2: Searching together (the user's design). Everyone searching round the
// same place is one search. Its members are split into groups of at least
// of2_stealth_search_group_size, each with a slice of the ground round the
// place, and work outwards through it: the first of a group to ask picks the
// nearest spot of its slice nobody has looked at, the others go to spots beside
// it. A spot is every ground node in the search's radius, and every
// info_of2_sweepspot (a place the mapper marked as somewhere to hide: rafters,
// a ledge), which is looked at from the nearest node with a line to it. A spot
// is done once any member has had it in view. When every spot of a room is
// done, the one who saw the last calls it clear; a room is an area as the map's
// areaportals divide it, or the group's slice where the search is all in one
// area. (Vis portals and leaves cannot be asked about here in any useful way:
// leaves can, but hold a node or two each.) Not saved: a search starts over
// after a load.
//=============================================================================
static ConVar of2_stealth_search_groups( "of2_stealth_search_groups", "1", FCVAR_NONE, "Stealth: NPCs searching the same place split into groups, each with its own slice of the ground, and look at every node once (0 = each wanders to random nodes by itself)." );
static ConVar of2_stealth_search_group_size( "of2_stealth_search_group_size", "2", FCVAR_NONE, "Stealth: the fewest NPCs in a search group. Those left over join the groups there are." );
static ConVar of2_stealth_search_view_dist( "of2_stealth_search_view_dist", "700", FCVAR_NONE, "Stealth: a search spot counts as looked at when a searcher has it in view within this distance (and, without night vision, lit)." );
static ConVar of2_stealth_search_resweep( "of2_stealth_search_resweep", "20", FCVAR_NONE, "Stealth: seconds after a search that does not stand down has looked at everything before it starts over." );
static ConVar of2_stealth_search_formation( "of2_stealth_search_formation", "5", FCVAR_NONE, "Stealth: the formation a search group keeps round its leader, as the follow behavior numbers them: 0 simple, 1 wide, 3 commander (citizens round the player), 4 tight, 5 medium, 6 sidekick. -1: no formation, each walks to a node beside the leader's." );
static ConVar of2_stealth_search_crouch_time( "of2_stealth_search_crouch_time", "2.5", FCVAR_NONE, "Stealth: seconds a searcher stays down to look at a sweep spot marked for crouching." );

#define SEARCH_MATCH_DIST		300.0f	// stimuli this close together are the same search
#define SEARCH_IDLE_TIME		15.0f	// a search nobody has asked about for this long is over
#define SEARCH_MEMBER_TIME		12.0f	// so is a member's part in it
#define SEARCH_CLAIM_TIME		25.0f	// a group's spot is the group's for this long
#define SEARCH_WAIT_TIME		14.0f	// how long a leader waits at a spot for the rest of its group
#define SEARCH_BESIDE_DIST		250.0f	// the others of a group go to spots this near the leader's
#define SEARCH_ARRIVED_DIST		64.0f	// standing on a spot is having looked at it
#define SEARCH_LOOKS_PER_THINK	5		// sight lines tried per look
#define SEARCH_LOOK_INTERVAL	0.3f
#define SEARCH_BUCKETS			64
#define SEARCH_CHECK_HEIGHT		512.0f	// check points up to this far above or below the place belong to its search
#define SEARCH_CHECK_STAND_DIST	600.0f	// how far from a check point the node to look at it from may be
#define SEARCH_CHECK_LIT_DIST	160.0f	// a player this near a check point being looked at is in the beam
#define SEARCH_CROUCH_EYES		28.0f	// how far off the floor a crouching searcher looks from
#define SEARCH_STAND_EYES		60.0f

//-----------------------------------------------------------------------------
// A place the mapper wants looked at in a search
//-----------------------------------------------------------------------------
class COF2CheckPoint : public CPointEntity
{
	DECLARE_CLASS( COF2CheckPoint, CPointEntity );

public:
	DECLARE_DATADESC();

	COF2CheckPoint()	{ m_bDisabled = false; m_bCrouch = false; s_List.AddToTail( this ); }
	~COF2CheckPoint()	{ s_List.FindAndRemove( this ); }

	void	InputEnable( inputdata_t &inputdata )	{ m_bDisabled = false; }
	void	InputDisable( inputdata_t &inputdata )	{ m_bDisabled = true; }

	bool	m_bDisabled;
	bool	m_bCrouch;		// looked at from a crouch: a tunnel, under a truck

	static CUtlVector<COF2CheckPoint *> s_List;
};

CUtlVector<COF2CheckPoint *> COF2CheckPoint::s_List;

LINK_ENTITY_TO_CLASS( info_of2_sweepspot, COF2CheckPoint );

BEGIN_DATADESC( COF2CheckPoint )
	DEFINE_KEYFIELD( m_bDisabled, FIELD_BOOLEAN, "StartDisabled" ),
	DEFINE_KEYFIELD( m_bCrouch, FIELD_BOOLEAN, "crouch" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
END_DATADESC()

//-----------------------------------------------------------------------------
struct OF2SearchSpot_t
{
	Vector	vecPos;			// what has to be seen
	Vector	vecGoal;		// where to go for it (a check point: once worked out)
	int		iNode;			// ground node, or -1 for a check point
	EHANDLE	hCheck;
	bool	bGoal;			// vecGoal is known
	bool	bCrouch;		// a check point to look at from a crouch
	float	flBearing;		// from the middle of the search
	float	flDist;
	int		iArea;
	int		iBucket;		// the room it is part of
	bool	bViewed;
	bool	bSkip;			// cannot be got to: left for whoever happens to see it
};

struct OF2SearchGroup_t
{
	int		iTarget;		// the spot it is working on, or -1
	float	flTargetTime;
	CHandle<CAI_BaseNPC> hLeader;
};

struct OF2SearchMember_t
{
	CHandle<CAI_BaseNPC> hNPC;
	int		iGroup;
	float	flBearing;
	float	flLastTime;
};

struct OF2Search_t
{
	Vector	vecCenter;
	float	flRadius;
	float	flLastTime;
	float	flDoneTime;		// when the last spot was looked at; 0: not yet
	bool	bByArea;		// rooms are the map's areas (else the groups' slices)
	float	flBaseBearing;	// where the first slice starts
	int		iLookNext;
	bool	bClearSaid[SEARCH_BUCKETS];
	CUtlVector<OF2SearchSpot_t>		spots;
	CUtlVector<OF2SearchGroup_t>	groups;
	CUtlVector<OF2SearchMember_t>	members;
};

static CUtlVector<OF2Search_t *> s_Searches;

class COF2SearchList : public CAutoGameSystem
{
public:
	COF2SearchList() : CAutoGameSystem( "COF2SearchList" ) {}
	virtual void LevelShutdownPostEntity( void )	{ s_Searches.PurgeAndDeleteElements(); }
};
static COF2SearchList s_SearchList;

struct OF2SearchPick_t
{
	Vector	vecGoal;
	bool	bFace;
	Vector	vecFace;
	bool	bCrouch;
};

//-----------------------------------------------------------------------------
static OF2Search_t *Search_Find( const Vector &vecCenter )
{
	for ( int i = s_Searches.Count() - 1; i >= 0; i-- )
	{
		OF2Search_t *pSearch = s_Searches[i];

		// (The second test: a time from before a load)
		if ( gpGlobals->curtime - pSearch->flLastTime > SEARCH_IDLE_TIME || pSearch->flLastTime > gpGlobals->curtime )
		{
			delete pSearch;
			s_Searches.Remove( i );
			continue;
		}

		if ( ( pSearch->vecCenter - vecCenter ).Length2D() <= SEARCH_MATCH_DIST && fabs( pSearch->vecCenter.z - vecCenter.z ) <= STEALTH_NODE_MAX_DZ )
			return pSearch;
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Which slice of the ground a spot is in. -1: at the middle, anyone's.
//-----------------------------------------------------------------------------
static int Search_SliceOf( OF2Search_t *pSearch, const OF2SearchSpot_t &spot )
{
	int nGroups = pSearch->groups.Count();
	if ( nGroups <= 1 )
		return 0;

	if ( spot.flDist < 96.0f )
		return -1;

	int iSlice = (int)( AngleNormalizePositive( spot.flBearing - pSearch->flBaseBearing ) / ( 360.0f / nGroups ) );
	return clamp( iSlice, 0, nGroups - 1 );
}

static bool Search_BucketDone( OF2Search_t *pSearch, int iBucket )
{
	for ( int i = 0; i < pSearch->spots.Count(); i++ )
	{
		const OF2SearchSpot_t &spot = pSearch->spots[i];
		if ( spot.iBucket == iBucket && !spot.bViewed && !spot.bSkip )
			return false;
	}
	return true;
}

static void Search_SetBuckets( OF2Search_t *pSearch )
{
	for ( int i = 0; i < pSearch->spots.Count(); i++ )
	{
		OF2SearchSpot_t &spot = pSearch->spots[i];
		spot.iBucket = pSearch->bByArea ? ( spot.iArea & ( SEARCH_BUCKETS - 1 ) ) : MAX( Search_SliceOf( pSearch, spot ), 0 );
	}

	// Nobody calls out a room that is done already
	for ( int i = 0; i < SEARCH_BUCKETS; i++ )
	{
		pSearch->bClearSaid[i] = Search_BucketDone( pSearch, i );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The spots of a search: the ground nodes and check points in its
//			radius. Called again when the radius has grown; what is there stays.
//-----------------------------------------------------------------------------
static void Search_AddSpot( OF2Search_t *pSearch, const Vector &vecPos, const Vector &vecGoal, int iNode, CBaseEntity *pCheck )
{
	OF2SearchSpot_t spot;
	spot.vecPos = vecPos;
	spot.vecGoal = vecGoal;
	spot.iNode = iNode;
	spot.hCheck = pCheck;
	spot.bGoal = ( pCheck == NULL );
	COF2CheckPoint *pCheckPoint = dynamic_cast<COF2CheckPoint *>( pCheck );
	spot.bCrouch = pCheckPoint && pCheckPoint->m_bCrouch;
	Vector vecFromCenter = vecPos - pSearch->vecCenter;
	spot.flBearing = UTIL_VecToYaw( vecFromCenter );
	spot.flDist = vecFromCenter.Length2D();
	spot.iArea = engine->GetArea( vecPos );
	spot.iBucket = 0;
	spot.bViewed = false;
	spot.bSkip = false;
	pSearch->spots.AddToTail( spot );
}

static void Search_Build( OF2Search_t *pSearch, float flRadius )
{
	pSearch->flRadius = flRadius;

	int nNodes = g_pBigAINet ? g_pBigAINet->NumNodes() : 0;
	for ( int iNode = 0; iNode < nNodes; iNode++ )
	{
		CAI_Node *pNode = g_pBigAINet->GetNode( iNode, false );
		if ( !pNode || pNode->GetType() != NODE_GROUND )
			continue;

		Vector vecNode = pNode->GetPosition( HULL_HUMAN );
		if ( fabs( vecNode.z - pSearch->vecCenter.z ) > STEALTH_NODE_MAX_DZ || ( vecNode - pSearch->vecCenter ).Length2D() > flRadius )
			continue;

		bool bHave = false;
		for ( int i = 0; i < pSearch->spots.Count() && !bHave; i++ )
		{
			bHave = ( pSearch->spots[i].iNode == iNode );
		}

		if ( !bHave )
		{
			Search_AddSpot( pSearch, vecNode + Vector( 0, 0, 40 ), vecNode, iNode, NULL );
		}
	}

	for ( int iCheck = 0; iCheck < COF2CheckPoint::s_List.Count(); iCheck++ )
	{
		COF2CheckPoint *pCheck = COF2CheckPoint::s_List[iCheck];
		Vector vecCheck = pCheck->GetAbsOrigin();
		if ( pCheck->m_bDisabled || fabs( vecCheck.z - pSearch->vecCenter.z ) > SEARCH_CHECK_HEIGHT || ( vecCheck - pSearch->vecCenter ).Length2D() > flRadius )
			continue;

		bool bHave = false;
		for ( int i = 0; i < pSearch->spots.Count() && !bHave; i++ )
		{
			bHave = ( pSearch->spots[i].hCheck.Get() == pCheck );
		}

		if ( !bHave )
		{
			Search_AddSpot( pSearch, vecCheck, vecCheck, -1, pCheck );
		}
	}

	// More than one area in it: those are its rooms
	pSearch->bByArea = false;
	for ( int i = 1; i < pSearch->spots.Count() && !pSearch->bByArea; i++ )
	{
		pSearch->bByArea = ( pSearch->spots[i].iArea != pSearch->spots[0].iArea );
	}

	pSearch->flDoneTime = 0.0f;
	Search_SetBuckets( pSearch );
}

//-----------------------------------------------------------------------------
// Purpose: Who is in which group. Members are taken in the order they stand
//			round the middle, so a group is NPCs that are near each other, and
//			the slices are handed out the same way round.
//-----------------------------------------------------------------------------
static int __cdecl Search_MemberSort( const OF2SearchMember_t *pA, const OF2SearchMember_t *pB )
{
	return ( pA->flBearing < pB->flBearing ) ? -1 : ( pA->flBearing > pB->flBearing ) ? 1 : 0;
}

static void Search_Regroup( OF2Search_t *pSearch )
{
	for ( int i = 0; i < pSearch->members.Count(); i++ )
	{
		OF2SearchMember_t &member = pSearch->members[i];
		CAI_BaseNPC *pNPC = member.hNPC.Get();
		member.flBearing = pNPC ? UTIL_VecToYaw( pNPC->GetAbsOrigin() - pSearch->vecCenter ) : 0.0f;
	}
	pSearch->members.Sort( Search_MemberSort );

	int nMembers = pSearch->members.Count();
	int nGroups = MAX( 1, nMembers / MAX( of2_stealth_search_group_size.GetInt(), 1 ) );

	pSearch->groups.SetCount( nGroups );
	for ( int i = 0; i < nGroups; i++ )
	{
		pSearch->groups[i].iTarget = -1;
		pSearch->groups[i].flTargetTime = gpGlobals->curtime;
		pSearch->groups[i].hLeader = NULL;
	}

	for ( int i = 0; i < nMembers; i++ )
	{
		pSearch->members[i].iGroup = i * nGroups / nMembers;

		// The same one leads a group however often the groups are made again
		OF2SearchGroup_t &group = pSearch->groups[pSearch->members[i].iGroup];
		CAI_BaseNPC *pNPC = pSearch->members[i].hNPC.Get();
		CAI_BaseNPC *pLeader = group.hLeader.Get();
		if ( pNPC && ( !pLeader || pNPC->entindex() < pLeader->entindex() ) )
		{
			group.hLeader = pNPC;
		}
	}

	pSearch->flBaseBearing = nMembers ? pSearch->members[0].flBearing : 0.0f;

	if ( !pSearch->bByArea )
	{
		Search_SetBuckets( pSearch );
	}
}

//-----------------------------------------------------------------------------
// Purpose: This NPC's place in the search, coming and going looked after
//-----------------------------------------------------------------------------
static OF2SearchMember_t *Search_Member( OF2Search_t *pSearch, CAI_BaseNPC *pNPC )
{
	bool bChanged = false;
	int iMember = -1;

	for ( int i = pSearch->members.Count() - 1; i >= 0; i-- )
	{
		CAI_BaseNPC *pOther = pSearch->members[i].hNPC.Get();
		if ( pOther == pNPC )
			continue;

		if ( !pOther || !pOther->IsAlive() || gpGlobals->curtime - pSearch->members[i].flLastTime > SEARCH_MEMBER_TIME )
		{
			pSearch->members.Remove( i );
			bChanged = true;
		}
	}

	for ( int i = 0; i < pSearch->members.Count(); i++ )
	{
		if ( pSearch->members[i].hNPC.Get() == pNPC )
		{
			iMember = i;
		}
	}

	if ( iMember < 0 )
	{
		// In one search at a time
		for ( int i = 0; i < s_Searches.Count(); i++ )
		{
			OF2Search_t *pOther = s_Searches[i];
			for ( int j = pOther->members.Count() - 1; j >= 0 && pOther != pSearch; j-- )
			{
				if ( pOther->members[j].hNPC.Get() == pNPC )
				{
					pOther->members.Remove( j );
					Search_Regroup( pOther );
				}
			}
		}

		OF2SearchMember_t member;
		member.hNPC = pNPC;
		member.iGroup = 0;
		member.flBearing = 0.0f;
		member.flLastTime = gpGlobals->curtime;
		pSearch->members.AddToTail( member );
		bChanged = true;
	}

	if ( bChanged )
	{
		Search_Regroup( pSearch );

		for ( int i = 0; i < pSearch->members.Count(); i++ )
		{
			if ( pSearch->members[i].hNPC.Get() == pNPC )
			{
				iMember = i;
			}
		}
	}

	pSearch->members[iMember].flLastTime = gpGlobals->curtime;
	return &pSearch->members[iMember];
}

//-----------------------------------------------------------------------------
// Purpose: Where to stand to look at a check point: the nearest node of the
//			search with a line to it, not right underneath.
//-----------------------------------------------------------------------------
static bool Search_FindCheckGoal( OF2Search_t *pSearch, OF2SearchSpot_t &check )
{
	if ( check.bGoal )
		return true;

	CTraceFilterWorldOnly filter;
	float flBest = FLT_MAX;

	for ( int i = 0; i < pSearch->spots.Count(); i++ )
	{
		const OF2SearchSpot_t &spot = pSearch->spots[i];
		if ( spot.iNode < 0 || spot.bSkip )
			continue;

		float flDist = ( spot.vecGoal - check.vecPos ).Length2D();
		if ( flDist < 64.0f || flDist > SEARCH_CHECK_STAND_DIST || flDist >= flBest )
			continue;

		trace_t tr;
		UTIL_TraceLine( spot.vecGoal + Vector( 0, 0, check.bCrouch ? SEARCH_CROUCH_EYES : SEARCH_STAND_EYES ), check.vecPos, MASK_BLOCKLOS, &filter, &tr );
		if ( tr.fraction < 1.0f )
			continue;

		flBest = flDist;
		check.vecGoal = spot.vecGoal;
		check.bGoal = true;
	}

	return check.bGoal;
}

//-----------------------------------------------------------------------------
// Purpose: Where this NPC goes next. False: nothing left to look at (or no
//			nodes), and the caller does as it did before all this.
//-----------------------------------------------------------------------------
static bool OF2Search_Pick( CAI_BaseNPC *pNPC, const Vector &vecCenter, float flRadius, OF2SearchPick_t *pPick )
{
	pPick->bCrouch = false;
	OF2Search_t *pSearch = Search_Find( vecCenter );
	if ( !pSearch )
	{
		pSearch = new OF2Search_t;
		pSearch->vecCenter = vecCenter;
		pSearch->flBaseBearing = 0.0f;
		pSearch->iLookNext = 0;
		s_Searches.AddToTail( pSearch );
		Search_Build( pSearch, flRadius );
	}
	else if ( flRadius > pSearch->flRadius + 100.0f )
	{
		// It has been a while: they may have got further
		Search_Build( pSearch, flRadius );
	}

	pSearch->flLastTime = gpGlobals->curtime;

	OF2SearchMember_t *pMember = Search_Member( pSearch, pNPC );
	OF2SearchGroup_t &group = pSearch->groups[pMember->iGroup];
	Vector vecOrigin = pNPC->GetAbsOrigin();

	// A search that goes on for good looks at everything again after a while
	if ( pSearch->flDoneTime > 0.0f && gpGlobals->curtime - pSearch->flDoneTime > of2_stealth_search_resweep.GetFloat() )
	{
		for ( int i = 0; i < pSearch->spots.Count(); i++ )
		{
			pSearch->spots[i].bViewed = false;
		}
		pSearch->flDoneTime = 0.0f;
		Search_SetBuckets( pSearch );
	}

	// A group moves as one: whoever leads it picks where it goes next, and the others
	// go to the nodes beside that, one each, whether or not the place has been looked
	// at meanwhile (it usually has, from a distance, long before anyone gets there:
	// tied to that, everyone ended up picking for themselves and nobody walked together).
	bool bHasTarget = pSearch->spots.IsValidIndex( group.iTarget );
	CAI_BaseNPC *pLeader = group.hLeader.Get();
	bool bLeaderHolds = bHasTarget && pLeader && pLeader->IsAlive() && gpGlobals->curtime - group.flTargetTime < SEARCH_CLAIM_TIME;

	if ( bLeaderHolds && pLeader != pNPC )
	{
		const OF2SearchSpot_t &target = pSearch->spots[group.iTarget];

		// Which of the others in the group it is
		int nRank = 0;
		for ( int i = 0; i < pSearch->members.Count(); i++ )
		{
			CAI_BaseNPC *pOther = pSearch->members[i].hNPC.Get();
			if ( pOther == pNPC )
				break;

			if ( pSearch->members[i].iGroup == pMember->iGroup && pOther != pLeader )
			{
				nRank++;
			}
		}

		// The nearest node to the leader's for the first, the next nearest for the second...
		int iBeside = -1;
		float flLast = 24.0f;
		for ( int n = 0; n <= nRank; n++ )
		{
			int iNext = -1;
			float flBest = SEARCH_BESIDE_DIST;
			for ( int i = 0; i < pSearch->spots.Count(); i++ )
			{
				const OF2SearchSpot_t &spot = pSearch->spots[i];
				if ( i == group.iTarget || spot.iNode < 0 || spot.bSkip )
					continue;

				float flDist = spot.vecGoal.DistTo( target.vecGoal );
				if ( flDist > flLast && flDist < flBest )
				{
					flBest = flDist;
					iNext = i;
				}
			}

			if ( iNext < 0 )
				break;

			iBeside = iNext;
			flLast = flBest;
		}

		pPick->vecGoal = ( iBeside >= 0 ) ? pSearch->spots[iBeside].vecGoal : target.vecGoal;
		pPick->bFace = ( target.iNode < 0 );
		pPick->vecFace = target.vecPos;
		return true;
	}

	if ( bLeaderHolds && pLeader == pNPC )
	{
		OF2SearchSpot_t &old = pSearch->spots[group.iTarget];
		bool bArrived = old.vecGoal.DistTo( vecOrigin ) < 150.0f;

		if ( !bArrived )
		{
			// (Something broke in on the way: carry on to it)
			pPick->vecGoal = old.vecGoal;
			pPick->bFace = ( old.iNode < 0 );
			pPick->bCrouch = old.bCrouch;
			pPick->vecFace = old.vecPos;
			return true;
		}

		// There, and it still has not been looked at: it cannot be
		if ( !old.bViewed )
		{
			old.bSkip = true;
		}

		// It does not go on without the others: wait here until they have caught up
		// (for a while: one of them may be stuck)
		if ( gpGlobals->curtime - group.flTargetTime < SEARCH_WAIT_TIME )
		{
			for ( int i = 0; i < pSearch->members.Count(); i++ )
			{
				CAI_BaseNPC *pOther = pSearch->members[i].hNPC.Get();
				if ( pOther && pOther != pNPC && pSearch->members[i].iGroup == pMember->iGroup &&
					pOther->GetAbsOrigin().DistTo( vecOrigin ) > SEARCH_BESIDE_DIST + 100.0f )
				{
					pPick->vecGoal = vecOrigin;
					pPick->bFace = false;
					pPick->vecFace = vecOrigin;
					return true;
				}
			}
		}
	}
	else if ( bHasTarget && pLeader == pNPC && !pSearch->spots[group.iTarget].bViewed )
	{
		// Did not get there in all that time
		pSearch->spots[group.iTarget].bSkip = true;
	}

	// Not the one who leads its group, and the leader has nowhere in mind yet: to the leader
	if ( pLeader && pLeader != pNPC && pLeader->IsAlive() && !pLeader->GetEnemy() &&
		gpGlobals->curtime - group.flTargetTime <= SEARCH_CLAIM_TIME + SEARCH_WAIT_TIME )
	{
		pPick->vecGoal = pLeader->GetAbsOrigin();
		pPick->bFace = false;
		pPick->vecFace = pPick->vecGoal;
		return true;
	}

	for ( int iTry = 0; iTry < 4; iTry++ )
	{
		int iBest = -1;
		float flBest = FLT_MAX;

		for ( int iPass = 0; iPass < 2 && iBest < 0; iPass++ )
		{
			for ( int i = 0; i < pSearch->spots.Count(); i++ )
			{
				OF2SearchSpot_t &spot = pSearch->spots[i];
				if ( spot.bViewed || spot.bSkip )
					continue;

				// Its own slice first, then anyone's
				int iSlice = Search_SliceOf( pSearch, spot );
				if ( iPass == 0 && iSlice >= 0 && iSlice != pMember->iGroup )
					continue;

				// Another group is on its way there
				bool bTaken = false;
				for ( int g = 0; g < pSearch->groups.Count() && !bTaken; g++ )
				{
					bTaken = ( g != pMember->iGroup && pSearch->groups[g].iTarget == i && gpGlobals->curtime - pSearch->groups[g].flTargetTime < SEARCH_CLAIM_TIME );
				}
				if ( bTaken )
					continue;

				// Outwards from the middle, without crossing the slice for it. The
				// places the mapper marked come before the ground round them.
				float flScore = spot.flDist + 0.3f * spot.vecGoal.DistTo( vecOrigin ) - ( ( spot.iNode < 0 ) ? 200.0f : 0.0f );
				if ( flScore < flBest )
				{
					flBest = flScore;
					iBest = i;
				}
			}
		}

		if ( iBest < 0 )
			break;

		OF2SearchSpot_t &best = pSearch->spots[iBest];

		if ( best.iNode < 0 && !Search_FindCheckGoal( pSearch, best ) )
		{
			best.bSkip = true;
			continue;
		}

		if ( best.iNode >= 0 && best.vecGoal.DistTo( vecOrigin ) < SEARCH_ARRIVED_DIST )
		{
			best.bViewed = true;
			continue;
		}

		group.iTarget = iBest;
		group.flTargetTime = gpGlobals->curtime;
		group.hLeader = pNPC;

		pPick->vecGoal = best.vecGoal;
		pPick->bFace = ( best.iNode < 0 );
		pPick->bCrouch = best.bCrouch;
		pPick->vecFace = best.vecPos;
		return true;
	}

	group.iTarget = -1;
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: A searcher looks about: which spots does it have in view? *pbClear:
//			that was the last of a room. *pbDone: of the whole search.
//-----------------------------------------------------------------------------
static void OF2Search_Look( COF2Awareness *pAware, CAI_BaseNPC *pNPC, const Vector &vecCenter, bool *pbClear, bool *pbDone )
{
	*pbClear = *pbDone = false;

	OF2Search_t *pSearch = Search_Find( vecCenter );
	if ( !pSearch || !pSearch->spots.Count() )
		return;

	pSearch->flLastTime = gpGlobals->curtime;

	Vector vecOrigin = pNPC->GetAbsOrigin();
	float flViewDist = of2_stealth_search_view_dist.GetFloat();
	bool bDebug = of2_stealth_debug.GetBool();
	int nSpots = pSearch->spots.Count();
	int nLooks = 0;
	int nLeft = 0;

	// The check point its group is at: look up at it
	int iTarget = -1;
	for ( int i = 0; i < pSearch->members.Count(); i++ )
	{
		if ( pSearch->members[i].hNPC.Get() == pNPC && pSearch->groups.IsValidIndex( pSearch->members[i].iGroup ) )
		{
			iTarget = pSearch->groups[pSearch->members[i].iGroup].iTarget;
		}
	}

	if ( pSearch->spots.IsValidIndex( iTarget ) && pSearch->spots[iTarget].iNode < 0 && !pSearch->spots[iTarget].bViewed &&
		pSearch->spots[iTarget].vecPos.DistTo( pNPC->EyePosition() ) <= flViewDist )
	{
		pNPC->AddLookTarget( pSearch->spots[iTarget].vecPos, 1.0f, SEARCH_LOOK_INTERVAL + 0.3f );
	}

	int iStart = pSearch->iLookNext;

	for ( int n = 0; n < nSpots; n++ )
	{
		int i = ( iStart + n ) % nSpots;
		OF2SearchSpot_t &spot = pSearch->spots[i];

		if ( bDebug )
		{
			int r = spot.bViewed ? 0 : 255, g = spot.bViewed ? 255 : ( spot.bSkip ? 128 : 0 );
			NDebugOverlay::Cross3D( spot.vecPos, ( spot.iNode < 0 ) ? 12.0f : 4.0f, r, g, 0, true, SEARCH_LOOK_INTERVAL + 0.15f );
		}

		if ( spot.bViewed )
			continue;

		bool bSeen = false;
		if ( spot.iNode >= 0 && spot.vecGoal.DistTo( vecOrigin ) < SEARCH_ARRIVED_DIST )
		{
			bSeen = true;
		}
		else if ( nLooks < SEARCH_LOOKS_PER_THINK && spot.vecPos.DistToSqr( pNPC->EyePosition() ) <= Square( flViewDist ) && pNPC->FInViewCone( spot.vecPos ) )
		{
			nLooks++;
			pSearch->iLookNext = ( i + 1 ) % nSpots;

			// A check point is looked at with the head, and the light on it: whoever
			// is hiding right there is in the beam
			if ( spot.iNode < 0 )
			{
				Vector vecLookFrom = pNPC->EyePosition();
				if ( spot.bCrouch )
				{
					// Only from down there, and by the one who went down for it
					vecLookFrom = vecOrigin + Vector( 0, 0, SEARCH_CROUCH_EYES );

					trace_t trSpot;
					UTIL_TraceLine( vecLookFrom, spot.vecPos, MASK_BLOCKLOS, pNPC, COLLISION_GROUP_NONE, &trSpot );
					bSeen = ( i == iTarget ) && pNPC->IsCrouching() &&
						( trSpot.fraction == 1.0f || trSpot.endpos.DistToSqr( spot.vecPos ) < Square( STEALTH_SEE_SLACK ) );
				}
				else
				{
					bSeen = ( i == iTarget ) && pAware->SeesPlace( spot.vecPos, flViewDist, false );
				}

				CBasePlayer *pPlayer = AI_GetSinglePlayer();
				trace_t trPlayer;
				if ( bSeen && pPlayer && pPlayer->WorldSpaceCenter().DistTo( spot.vecPos ) <= SEARCH_CHECK_LIT_DIST )
				{
					UTIL_TraceLine( vecLookFrom, pPlayer->WorldSpaceCenter(), MASK_BLOCKLOS, pNPC, COLLISION_GROUP_NONE, &trPlayer );
				}
				if ( bSeen && pPlayer && pPlayer->WorldSpaceCenter().DistTo( spot.vecPos ) <= SEARCH_CHECK_LIT_DIST && ( trPlayer.fraction == 1.0f || trPlayer.m_pEnt == pPlayer ) )
				{
					OF2_PlayerLit( 1.0f );
				}
			}
			else
			{
				bSeen = pAware->SeesPlace( spot.vecPos, flViewDist );
			}
		}

		if ( !bSeen )
		{
			if ( !spot.bSkip )
			{
				nLeft++;
			}
			continue;
		}

		spot.bViewed = true;

		int iBucket = spot.iBucket;
		if ( !pSearch->bClearSaid[iBucket] && Search_BucketDone( pSearch, iBucket ) )
		{
			pSearch->bClearSaid[iBucket] = true;
			*pbClear = true;
		}
	}

	if ( nLeft == 0 && pSearch->flDoneTime == 0.0f )
	{
		pSearch->flDoneTime = gpGlobals->curtime;
		*pbDone = true;
	}

	if ( bDebug )
	{
		for ( int i = 0; i < pSearch->members.Count(); i++ )
		{
			if ( pSearch->members[i].hNPC.Get() == pNPC )
			{
				char szText[48];
				Q_snprintf( szText, sizeof( szText ), "group %d of %d, %d spots left", pSearch->members[i].iGroup + 1, pSearch->groups.Count(), nLeft );
				NDebugOverlay::Text( pNPC->EyePosition() + Vector( 0, 0, 30 ), szText, false, SEARCH_LOOK_INTERVAL + 0.15f );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Joins the search round this place (starting it if need be) and says
//			who leads this NPC's group. NULL: it does, or nobody fit to.
//-----------------------------------------------------------------------------
static CAI_BaseNPC *OF2Search_Join( CAI_BaseNPC *pNPC, const Vector &vecCenter, float flRadius )
{
	OF2Search_t *pSearch = Search_Find( vecCenter );
	if ( !pSearch )
	{
		pSearch = new OF2Search_t;
		pSearch->vecCenter = vecCenter;
		pSearch->flBaseBearing = 0.0f;
		pSearch->iLookNext = 0;
		s_Searches.AddToTail( pSearch );
		Search_Build( pSearch, flRadius );
	}

	pSearch->flLastTime = gpGlobals->curtime;

	OF2SearchMember_t *pMember = Search_Member( pSearch, pNPC );
	OF2SearchGroup_t &group = pSearch->groups[pMember->iGroup];
	CAI_BaseNPC *pLeader = pSearch->groups[pMember->iGroup].hLeader.Get();

	// A leader that has not led anywhere in a long while is busy with something else
	if ( pLeader && pLeader != pNPC && gpGlobals->curtime - group.flTargetTime > SEARCH_CLAIM_TIME + SEARCH_WAIT_TIME )
	{
		group.hLeader = pNPC;
		group.flTargetTime = gpGlobals->curtime;
		return NULL;
	}
	if ( !pLeader || pLeader == pNPC || !pLeader->IsAlive() || pLeader->GetEnemy() )
		return NULL;

	return pLeader;
}

static bool OF2Search_IsDone( const Vector &vecCenter )
{
	OF2Search_t *pSearch = Search_Find( vecCenter );
	return pSearch && pSearch->spots.Count() && pSearch->flDoneTime > 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: How far out this NPC searches by now
//-----------------------------------------------------------------------------
float COF2Awareness::SearchRadius( void )
{
	float flRadius = of2_stealth_search_radius.GetFloat();
	if ( m_bEnemyAbout && of2_stealth_search_forever.GetBool() )
	{
		// The longer since the last word, the further they may have got
		flRadius += ( gpGlobals->curtime - m_flStimulusTime ) * of2_stealth_search_spread.GetFloat();
		flRadius = MIN( flRadius, MAX( of2_stealth_search_radius_max.GetFloat(), of2_stealth_search_radius.GetFloat() ) );
	}
	return flRadius;
}

//-----------------------------------------------------------------------------
// Purpose: Walking with the leader of its search group
//-----------------------------------------------------------------------------
void COF2Awareness::UpdateFormation( CAI_FollowBehavior &follow )
{
	CAI_BaseNPC *pLeader = NULL;

	if ( IsEnabled() && of2_stealth_search_groups.GetBool() && of2_stealth_search_formation.GetInt() >= 0 &&
		m_iState == AWARE_SEARCHING && m_bVisited && !m_pOuter->GetEnemy() &&
		!m_pOuter->IsInAScript() && m_pOuter->GetState() != NPC_STATE_SCRIPT )
	{
		pLeader = OF2Search_Join( m_pOuter, m_vecStimulus, SearchRadius() );
	}

	if ( pLeader )
	{
		// (Following someone the map told it to: not ours to change)
		if ( !m_bFollowing && follow.GetFollowTarget() )
			return;

		if ( follow.GetFollowTarget() != pLeader )
		{
			AI_FollowParams_t params( (AI_Formations_t)clamp( of2_stealth_search_formation.GetInt(), (int)AIF_SIMPLE, (int)AIF_VORTIGAUNT ) );
			follow.SetParameters( params );
			follow.SetFollowTarget( pLeader );
		}
		m_bFollowing = true;
	}
	else if ( m_bFollowing )
	{
		follow.SetFollowTarget( NULL );
		m_bFollowing = false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: While searching: what of the search it has in view counts as looked at
//-----------------------------------------------------------------------------
void COF2Awareness::SearchLook( void )
{
	if ( gpGlobals->curtime < m_flNextSearchLook || !of2_stealth_search_groups.GetBool() )
		return;

	m_flNextSearchLook = gpGlobals->curtime + SEARCH_LOOK_INTERVAL;

	bool bClear, bDone;
	OF2Search_Look( this, m_pOuter, m_vecStimulus, &bClear, &bDone );

	if ( bClear || bDone )
	{
		m_bSayClear = true;
	}

	// Everywhere looked at and nobody found: a search that can end, ends
	if ( bDone && !( m_bEnemyAbout && of2_stealth_search_forever.GetBool() ) )
	{
		m_flSearchEndTime = MIN( m_flSearchEndTime, gpGlobals->curtime + 2.0f );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Somewhere around the stimulus to walk to and look. Nodes it cannot
//			see from here come first: behind things is where people hide.
//-----------------------------------------------------------------------------
bool COF2Awareness::PickSearchPoint( Vector *pResult )
{
	m_bSearchFace = false;
	m_bSearchCrouch = false;

	// With the others, if there is anything left to look at
	if ( of2_stealth_search_groups.GetBool() )
	{
		OF2SearchPick_t pick;
		if ( OF2Search_Pick( m_pOuter, m_vecStimulus, SearchRadius(), &pick ) )
		{
			*pResult = pick.vecGoal;
			m_bSearchFace = pick.bFace;
			m_vecSearchFace = pick.vecFace;
			m_bSearchCrouch = pick.bCrouch;
			return true;
		}

		// Everything has been looked at. A search that can end, ends (for the ones who
		// did not see the last spot themselves too).
		if ( OF2Search_IsDone( m_vecStimulus ) && !( m_bEnemyAbout && of2_stealth_search_forever.GetBool() ) )
		{
			m_flSearchEndTime = MIN( m_flSearchEndTime, gpGlobals->curtime + 2.0f );
		}
	}

	CAI_Network *pNetwork = m_pOuter->GetNavigator()->GetNetwork();
	int nNodes = pNetwork ? pNetwork->NumNodes() : 0;
	if ( nNodes <= 0 )
		return false;

	float flRadius = SearchRadius();
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

	case STEALTH_TASK_FACE_SEARCH:
		// Came here to look at somewhere the mapper marked: towards it
		if ( m_bSearchFace )
		{
			// Down to look into it (the idle that follows is then the crouched one)
			if ( m_bSearchCrouch )
			{
				m_iCrouchWant = 1;
				m_flStandTime = gpGlobals->curtime + of2_stealth_search_crouch_time.GetFloat();
			}
			m_pOuter->GetMotor()->SetIdealYawToTarget( m_vecSearchFace );
			return true;
		}
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
	static const char *pszStimuli[] = { "", "noise", "footsteps", "glimpse", "gunfire", "body", "report", "lost enemy" };

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
		flRadius = pBodySound->Volume();
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

	// Heavier is louder, but not in proportion: a bottle at throwing speed (300 and
	// up) has to carry a little further than the player running does (the user's
	// measure; at 0.6 here they were barely noticed)
	float flMass = MIN( pEvent->pObjects[index]->GetMass(), 100.0f );
	float flRadius = flSpeed * ( 1.6f + 0.25f * sqrt( flMass ) ) * of2_stealth_impact_scale.GetFloat();
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

//=============================================================================
// OF2: The head flashlight (COF2Flashlight). It was the metrocop's own; ordinary
// soldiers wear it too now that only elites have night vision, so the convars
// keep their of2_police_ names.
//=============================================================================
ConVar of2_police_flashlight( "of2_police_flashlight", "1", FCVAR_NONE, "Metrocops and ordinary soldiers have a head flashlight. What it shines on, they (and everyone) can see. Per NPC: the of2_flashlight keyvalue, 0 never, 1 where it is dark, 2 always." );
ConVar of2_police_flashlight_range( "of2_police_flashlight_range", "600", FCVAR_NONE, "Reach of a head flashlight: how far the beam goes and how far it lets them see." );
ConVar of2_police_flashlight_fov( "of2_police_flashlight_fov", "50", FCVAR_NONE, "Cone of a head flashlight in degrees: what it lets them see, and how wide the light lands." );
ConVar of2_police_flashlight_beam( "of2_police_flashlight_beam", "10", FCVAR_NONE, "How visible the cone of light from a head flashlight is in the air (0-255; 0 for none)." );
ConVar of2_police_flashlight_beam_length( "of2_police_flashlight_beam_length", "320", FCVAR_NONE, "How far from the head that cone reaches before it has faded out. It widens at the light's own angle (of2_police_flashlight_fov)." );
ConVar of2_police_flashlight_budget( "of2_police_flashlight_budget", "4", FCVAR_NONE, "How many head flashlights may be projected lights at once: the lit ones nearest the player. The others have the pool of light. -1: no limit." );
ConVar of2_police_flashlight_projected_dist( "of2_police_flashlight_projected_dist", "1500", FCVAR_NONE, "Further from the player than this, a head flashlight has the pool of light instead of the projected one." );
ConVar of2_police_flashlight_projected( "of2_police_flashlight_projected", "1", FCVAR_NONE, "1: a head flashlight is a projected texture, which casts shadows, instead of a pool of light. Shines backwards too on any surface not drawn by the mod's own world shader. Can be switched while one is on." );
ConVar of2_police_flashlight_dark( "of2_police_flashlight_dark", "0.2", FCVAR_NONE, "A head flashlight is on where the brightness is below this (0-1, the 'light' figure of of2_stealth_light_debug)." );
ConVar of2_police_flashlight_hold( "of2_police_flashlight_hold", "6", FCVAR_NONE, "Until the game knows how dark it is where the NPC stands: seconds its flashlight stays on after it has calmed down." );
ConVar of2_police_flashlight_glow_offset( "of2_police_flashlight_glow_offset", "-3 -4.5 1", FCVAR_NONE, "Where the glow sprite sits from between the eyes: forward, left, up. Read when the light is first switched on." );
ConVar of2_police_flashlight_glow_scale( "of2_police_flashlight_glow_scale", "0.1", FCVAR_NONE, "Size of the glow sprite. Read when the light is first switched on." );

#define OF2_FLASHLIGHT_GLOW		"sprites/light_glow03.vmt"
#define OF2_FLASHLIGHT_LIT_TIME	0.5f	// the player stays lit this long after the beam was on them (a think or two)

//-----------------------------------------------------------------------------
// Purpose: The light a flashlight makes in the air. Nothing here but its place
//			(it rides on the eyes) and a few numbers; the client draws it
//			(client\hl2\c_of2_stealth.cpp).
//-----------------------------------------------------------------------------
class COF2LightCone : public CBaseEntity
{
	DECLARE_CLASS( COF2LightCone, CBaseEntity );

public:
	DECLARE_SERVERCLASS();

	COF2LightCone()
	{
		m_flConeFOV = 50.0f;
		m_flConeLength = 320.0f;
		m_flConeBrightness = 0.04f;
		m_flConePool = 0.0f;
	}

	void	Precache( void ) { PrecacheMaterial( "effects/of2_lightcone" ); PrecacheMaterial( "effects/of2_lightcone_inside" ); }
	void	Spawn( void )
	{
		Precache();
		SetSolid( SOLID_NONE );
		SetMoveType( MOVETYPE_NONE );
	}

	// Sent whenever the one wearing it is
	int		UpdateTransmitState( void ) { return SetTransmitState( FL_EDICT_FULLCHECK ); }
	int		ShouldTransmit( const CCheckTransmitInfo *pInfo )
	{
		CBaseEntity *pParent = GetMoveParent();
		return pParent ? pParent->ShouldTransmit( pInfo ) : FL_EDICT_DONTSEND;
	}

	// Its owner makes it again
	int		ObjectCaps( void ) { return ( BaseClass::ObjectCaps() & ~FCAP_ACROSS_TRANSITION ) | FCAP_DONT_SAVE; }

	CNetworkVar( float, m_flConeFOV );			// degrees, edge to edge
	CNetworkVar( float, m_flConeLength );
	CNetworkVar( float, m_flConeBrightness );	// 0-1
	CNetworkVar( float, m_flConePool );			// reach of the pool of light where it lands; 0: none (a projected light does that)
};

LINK_ENTITY_TO_CLASS( of2_lightcone, COF2LightCone );

IMPLEMENT_SERVERCLASS_ST( COF2LightCone, DT_OF2LightCone )
	SendPropFloat( SENDINFO( m_flConeFOV ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flConeLength ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flConeBrightness ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flConePool ), 0, SPROP_NOSCALE ),
END_SEND_TABLE()

BEGIN_SIMPLE_DATADESC( COF2Flashlight )
	DEFINE_FIELD( m_bOn,			FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flAmbientLight,	FIELD_FLOAT ),
	DEFINE_FIELD( m_flOffTime,		FIELD_TIME ),
	DEFINE_FIELD( m_hCone,			FIELD_EHANDLE ),
	DEFINE_FIELD( m_hEnd,			FIELD_EHANDLE ),
	DEFINE_FIELD( m_hProjected,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_hGlow,			FIELD_EHANDLE ),
END_DATADESC()

// Every flashlight there is, for sharing out the projected lights
static CUtlVector<COF2Flashlight *> s_Flashlights;

COF2Flashlight::COF2Flashlight()
{
	m_pOuter = NULL;
	m_bOn = false;
	m_flAmbientLight = -1.0f;
	m_flOffTime = 0.0f;
	m_bOnSpot = false;

	s_Flashlights.AddToTail( this );
}

COF2Flashlight::~COF2Flashlight()
{
	s_Flashlights.FindAndRemove( this );
}

void COF2Flashlight::Precache( void )
{
	CBaseEntity::PrecacheModel( OF2_FLASHLIGHT_GLOW );
	UTIL_PrecacheOther( "of2_lightcone" );
	UTIL_PrecacheOther( "spotlight_end" );
	CBaseEntity::PrecacheScriptSound( "HL2Player.FlashLightOn" );
	CBaseEntity::PrecacheScriptSound( "HL2Player.FlashLightOff" );
}

void COF2Flashlight::Remove( void )
{
	UTIL_Remove( m_hCone );
	UTIL_Remove( m_hEnd );
	UTIL_Remove( m_hProjected );
	UTIL_Remove( m_hGlow );
	m_hCone = NULL;
	m_hEnd = NULL;
	m_hProjected = NULL;
	m_hGlow = NULL;
	m_bOn = false;
}

bool COF2Flashlight::IsSpotLit( void ) const
{
	return m_bOnSpot || m_flAmbientLight >= of2_police_flashlight_dark.GetFloat() + 0.08f;
}

bool COF2Flashlight::Lights( const Vector &vecPos )
{
	// (Not heard from the client: taken as lit, as the player is)
	if ( m_flAmbientLight < 0.0f || m_flAmbientLight >= of2_police_flashlight_dark.GetFloat() + 0.08f )
		return true;

	if ( !m_bOn || !m_pOuter )
		return false;

	Vector vecOrigin, vecForward;
	GetRay( &vecOrigin, &vecForward );

	Vector vecTo = vecPos - vecOrigin;
	float flDist = VectorNormalize( vecTo );
	return flDist <= of2_police_flashlight_range.GetFloat() &&
		DotProduct( vecTo, vecForward ) >= cos( DEG2RAD( of2_police_flashlight_fov.GetFloat() * 0.5f ) );
}

//-----------------------------------------------------------------------------
// Purpose: Where the light is and which way it points: the eyes.
//-----------------------------------------------------------------------------
bool COF2Flashlight::GetRay( Vector *pOrigin, Vector *pForward )
{
	int iAttachment = m_pOuter->LookupAttachment( "eyes" );
	QAngle angEyes;
	if ( iAttachment > 0 && m_pOuter->GetAttachment( iAttachment, *pOrigin, angEyes ) )
	{
		AngleVectors( angEyes, pForward );
		return true;
	}

	// No eyes on this model: from the head, the way the body faces
	*pOrigin = m_pOuter->EyePosition();
	*pForward = m_pOuter->BodyDirection3D();
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: A projected light costs a drawing of everything it lands on and, for
//			the first few, of the scene from the lamp for its shadows; past those
//			few it has no shadows and shines through walls. So only the
//			of2_police_flashlight_budget lit ones nearest the player are
//			projected, and the rest make do with the pool of light.
//-----------------------------------------------------------------------------
float COF2Flashlight::Rank( CBasePlayer *pPlayer )
{
	// Whoever has one keeps it a little longer, so two at the same distance do not
	// hand it back and forth
	float flDist = ( m_pOuter->GetAbsOrigin() - pPlayer->GetAbsOrigin() ).Length();
	return ( m_hProjected != NULL ) ? flDist * 0.85f : flDist;
}

bool COF2Flashlight::WantsProjected( void )
{
	if ( !of2_police_flashlight_projected.GetBool() )
		return false;

	int nBudget = of2_police_flashlight_budget.GetInt();
	if ( nBudget < 0 )
		return true;

	CBasePlayer *pPlayer = AI_GetSinglePlayer();
	if ( !pPlayer || nBudget == 0 )
		return false;

	float flRank = Rank( pPlayer );
	if ( flRank > of2_police_flashlight_projected_dist.GetFloat() )
		return false;

	int nNearer = 0;
	for ( int i = 0; i < s_Flashlights.Count(); i++ )
	{
		COF2Flashlight *pOther = s_Flashlights[i];
		if ( pOther == this || !pOther->m_pOuter || !pOther->m_bOn )
			continue;

		float flOther = pOther->Rank( pPlayer );
		if ( flOther < flRank || ( flOther == flRank && pOther->m_pOuter->entindex() < m_pOuter->entindex() ) )
		{
			if ( ++nNearer >= nBudget )
				return false;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: On or off, and (on) whatever of it is missing is made: just loaded,
//			or the kind of light has changed.
//-----------------------------------------------------------------------------
void COF2Flashlight::Set( bool bOn )
{
	bool bWasOn = m_bOn;
	m_bOn = bOn;

	if ( !bOn )
	{
		UTIL_Remove( m_hCone );
		UTIL_Remove( m_hEnd );
		UTIL_Remove( m_hProjected );
		m_hCone = NULL;
		m_hEnd = NULL;
		m_hProjected = NULL;
	}
	else
	{
		Vector vecOrigin, vecForward;
		bool bHasEyes = GetRay( &vecOrigin, &vecForward );
		int iAttachment = bHasEyes ? m_pOuter->LookupAttachment( "eyes" ) : 0;

		// A projected texture, with shadows, for the few nearest the player
		bool bProjected = WantsProjected();
		if ( bProjected )
		{
			UTIL_Remove( m_hEnd );
			m_hEnd = NULL;

			if ( !m_hProjected )
			{
				CBaseEntity *pLight = CreateEntityByName( "env_projectedtexture" );
				if ( pLight )
				{
					pLight->KeyValue( "lightfov", of2_police_flashlight_fov.GetString() );
					pLight->KeyValue( "nearz", "12" );		// past its own face
					pLight->KeyValue( "farz", of2_police_flashlight_range.GetString() );
					pLight->KeyValue( "enableshadows", "1" );
					pLight->KeyValue( "brightnessscale", "1.5" );
					pLight->KeyValue( "texturename", "effects/flashlight001" );
					pLight->KeyValue( "spawnflags", "3" );	// on, and follows its parent every frame
					pLight->SetAbsOrigin( vecOrigin );
					pLight->SetAbsAngles( m_pOuter->GetAbsAngles() );
					DispatchSpawn( pLight );
					pLight->Activate();

					if ( iAttachment > 0 )
					{
						pLight->SetParent( m_pOuter, iAttachment );
						pLight->SetLocalOrigin( vec3_origin );
					}
					else
					{
						pLight->SetParent( m_pOuter );
						pLight->SetLocalOrigin( Vector( 8, 0, 64 ) );
					}
					pLight->SetLocalAngles( vec3_angle );

					m_hProjected = pLight;
				}
			}
		}
		else
		{
			UTIL_Remove( m_hProjected );
			m_hProjected = NULL;
		}

		// Otherwise a pool of light, which the client makes every frame from the cone
		// (m_flConePool). This only marks the kind of light.
		if ( !bProjected && !m_hEnd )
		{
			CSpotlightEnd *pEnd = (CSpotlightEnd *)CreateEntityByName( "spotlight_end" );
			if ( pEnd )
			{
				pEnd->Spawn();
				pEnd->SetAbsOrigin( vecOrigin );
				pEnd->SetOwnerEntity( m_pOuter );
				pEnd->m_flLightScale = 0.0f;
				pEnd->m_Radius = of2_police_flashlight_range.GetFloat();
				m_hEnd = pEnd;
			}
		}

		// The light in the air: a cone on the eyes, at the light's own angle, which the
		// client draws. (Not saved; this is also where it comes back after a load.)
		if ( !m_hCone )
		{
			COF2LightCone *pCone = (COF2LightCone *)CreateEntityByName( "of2_lightcone" );
			if ( pCone )
			{
				pCone->SetAbsOrigin( vecOrigin );
				DispatchSpawn( pCone );

				if ( iAttachment > 0 )
				{
					pCone->SetParent( m_pOuter, iAttachment );
					pCone->SetLocalOrigin( vec3_origin );
				}
				else
				{
					pCone->SetParent( m_pOuter );
					pCone->SetLocalOrigin( Vector( 8, 0, 64 ) );
				}
				pCone->SetLocalAngles( vec3_angle );

				m_hCone = pCone;
			}
		}

		if ( !m_hGlow )
		{
			CSprite *pGlow = CSprite::SpriteCreate( OF2_FLASHLIGHT_GLOW, m_pOuter->GetAbsOrigin(), false );
			if ( pGlow )
			{
				Vector vecOffset( -3, -4.5, 1 );
				UTIL_StringToVector( vecOffset.Base(), of2_police_flashlight_glow_offset.GetString() );

				if ( iAttachment > 0 )
				{
					pGlow->SetParent( m_pOuter, iAttachment );
					pGlow->SetLocalOrigin( vecOffset );
				}
				else
				{
					pGlow->SetParent( m_pOuter );
					pGlow->SetLocalOrigin( Vector( 0, 0, 66 ) + vecOffset );
				}

				pGlow->SetTransparency( kRenderGlow, 255, 250, 235, 255, kRenderFxNoDissipation );
				pGlow->SetScale( of2_police_flashlight_glow_scale.GetFloat() );
				pGlow->SetGlowProxySize( 2.0f );

				m_hGlow = pGlow;
			}
		}
	}

	CSprite *pGlow = dynamic_cast<CSprite *>( m_hGlow.Get() );
	if ( pGlow )
	{
		if ( bOn )
		{
			pGlow->TurnOn();
		}
		else
		{
			pGlow->TurnOff();
		}
	}

	if ( bOn != bWasOn )
	{
		m_pOuter->EmitSound( bOn ? "HL2Player.FlashLightOn" : "HL2Player.FlashLightOff" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Once per think. Switches the light; while it is on, tells the stealth
//			code when the player is in the beam.
//-----------------------------------------------------------------------------
bool COF2Flashlight::Update( int iMode, bool bAlerted )
{
	if ( !m_pOuter )
		return false;

	bool bWant = false;
	bool bAllowed = of2_police_flashlight.GetBool() && m_pOuter->IsAlive();

	// Only the client knows how dark it is here; this is what asks it to say
	bool bAuto = bAllowed && iMode == 1;

	if ( bAllowed )
	{
		if ( iMode >= 2 )
		{
			bWant = true;
		}
		else if ( iMode == 1 && m_flAmbientLight >= 0.0f )
		{
			// On wherever it is dark, calm or not (the user: a cop standing in the pitch
			// black with its light off makes no sense). A little apart so it does not flicker.
			float flDark = of2_police_flashlight_dark.GetFloat();
			bWant = m_flAmbientLight < ( m_bOn ? flDark + 0.08f : flDark );
		}
		else if ( iMode == 1 )
		{
			// Not heard from the client yet (it only reports on NPCs it can see):
			// on while looking for someone or fighting
			if ( bAlerted )
			{
				m_flOffTime = gpGlobals->curtime + of2_police_flashlight_hold.GetFloat();
			}
			bWant = ( gpGlobals->curtime < m_flOffTime );
		}
	}

	// (On but without its light: just loaded, or the kind of light is to change)
	bool bProjected = bWant && WantsProjected();
	bool bHasLight = m_hCone != NULL && ( bProjected ? ( m_hProjected != NULL && !m_hEnd ) : ( m_hEnd != NULL && !m_hProjected ) );
	if ( bWant != m_bOn || ( bWant && !bHasLight ) )
	{
		Set( bWant );
	}

	m_bOnSpot = false;

	if ( !m_bOn )
		return bAuto;

	Vector vecOrigin, vecForward;
	GetRay( &vecOrigin, &vecForward );

	float flRange = MAX( of2_police_flashlight_range.GetFloat(), 64.0f );
	float flMinDot = cos( DEG2RAD( of2_police_flashlight_fov.GetFloat() * 0.5f ) );

	COF2LightCone *pCone = dynamic_cast<COF2LightCone *>( m_hCone.Get() );
	if ( pCone )
	{
		pCone->m_flConeFOV = clamp( of2_police_flashlight_fov.GetFloat(), 1.0f, 170.0f );
		pCone->m_flConeLength = clamp( of2_police_flashlight_beam_length.GetFloat(), 16.0f, flRange );
		pCone->m_flConeBrightness = clamp( of2_police_flashlight_beam.GetFloat() / 255.0f, 0.0f, 1.0f );
		pCone->m_flConePool = ( m_hEnd != NULL ) ? flRange : 0.0f;
	}

	// For the stealth code: is the place it last knew the player to be in the beam?
	if ( m_pOuter->GetEnemy() )
	{
		Vector vecToSpot = ( m_pOuter->GetEnemyLKP() + Vector( 0, 0, 40 ) ) - vecOrigin;
		float flSpotDist = VectorNormalize( vecToSpot );
		m_bOnSpot = ( flSpotDist <= flRange && DotProduct( vecToSpot, vecForward ) >= flMinDot );
	}

	// Chest or head in the beam, with nothing in between: lit, for everyone
	CBasePlayer *pPlayer = AI_GetSinglePlayer();
	if ( !pPlayer )
		return bAuto;

	for ( int i = 0; i < 2; i++ )
	{
		Vector vecSpot = i ? pPlayer->EyePosition() : pPlayer->WorldSpaceCenter();
		Vector vecTo = vecSpot - vecOrigin;
		float flDist = VectorNormalize( vecTo );
		if ( flDist > flRange || DotProduct( vecTo, vecForward ) < flMinDot )
			continue;

		trace_t trPlayer;
		UTIL_TraceLine( vecOrigin, vecSpot, MASK_BLOCKLOS, m_pOuter, COLLISION_GROUP_NONE, &trPlayer );
		if ( trPlayer.fraction == 1.0f || trPlayer.m_pEnt == pPlayer )
		{
			OF2_PlayerLit( OF2_FLASHLIGHT_LIT_TIME );
			break;
		}
	}

	return bAuto;
}
