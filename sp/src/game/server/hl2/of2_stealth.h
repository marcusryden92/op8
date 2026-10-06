//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: Stealth. What an NPC makes of the player before it has
//			positively identified them: a suspicion score fed by sounds and
//			glimpses, a detection meter that fills while the player is in
//			view, and the place the evidence points to.
//
//			Not an entity. An NPC class embeds one (DEFINE_EMBEDDED), calls the
//			hooks below from its senses and picks its schedules from State().
//			Once the player is a known enemy all of this stands back and the
//			stock combat AI runs untouched.
//
//=============================================================================//

#ifndef OF2_STEALTH_H
#define OF2_STEALTH_H
#ifdef _WIN32
#pragma once
#endif

class CAI_BaseNPC;
class CBasePlayer;
class CSound;
struct gamevcollisionevent_t;

enum OF2Awareness_t
{
	AWARE_UNAWARE = 0,	// nothing going on
	AWARE_SUSPICIOUS,	// something caught its attention: stop and look
	AWARE_SEARCHING,	// believes someone is there: go and look, then search around
	AWARE_COMBAT,		// the player is a known enemy; the stock AI is in charge
};

// What the evidence was. Later ones take the place of earlier ones.
enum OF2Stimulus_t
{
	STIM_NONE = 0,
	STIM_NOISE,			// an impact, a door, something landing
	STIM_FOOTSTEPS,		// the player moving
	STIM_GLIMPSE,		// saw something, not long enough to be sure
	STIM_GUNFIRE,		// shots, explosions, a squadmate hit
	STIM_REPORT,		// a squadmate called it in
	STIM_LOST_ENEMY,	// was fighting the player and lost them
};

#define OF2_STEALTH_HEARD_SOUNDS	6

// Schedules and tasks belong to the NPC class that defines them, so each class has
// its own copies and maps them to these. What they do is in here.
enum OF2StealthSched_t
{
	STEALTH_SCHED_NONE = 0,		// nothing to do: carry on as stock
	STEALTH_SCHED_LOOK,			// stop, turn to it, watch
	STEALTH_SCHED_INVESTIGATE,	// go to where it was, look left and right
	STEALTH_SCHED_SEARCH,		// walk to somewhere nearby, look about
	STEALTH_SCHED_RETURN,		// back to where it stood
};

enum OF2StealthTask_t
{
	STEALTH_TASK_FACE_STIMULUS = 0,		// run it with TASK_FACE_IDEAL
	STEALTH_TASK_LOOKED,
	STEALTH_TASK_GET_PATH_TO_STIMULUS,
	STEALTH_TASK_ARRIVED,
	STEALTH_TASK_GET_SEARCH_PATH,
	STEALTH_TASK_GET_PATH_HOME,
	STEALTH_TASK_FACE_HOME,				// run it with TASK_FACE_IDEAL
};

class COF2Awareness
{
	DECLARE_CLASS_NOBASE( COF2Awareness );
	DECLARE_SIMPLE_DATADESC();

public:
	COF2Awareness();

	// Not saved: call from the owner's constructor
	void	Init( CAI_BaseNPC *pOuter )				{ m_pOuter = pOuter; }

	// Is the system running for this NPC at all? (convar, per-NPC switch, and it
	// has to hate the player in the first place)
	bool	IsEnabled( void ) const;
	void	SetDisabled( bool bDisabled )			{ m_bDisabled = bDisabled; }
	bool	IsDisabled( void ) const				{ return m_bDisabled; }

	//-------------------------------------------------------------------------
	// Hooks
	//-------------------------------------------------------------------------
	// Once per think: decay, state changes, losing an enemy
	void	Update( void );

	// From OnListened(): turn what was heard into evidence
	void	HearSounds( void );

	// From QueryHearSound(): can it hear the player moving about?
	bool	CanHearPlayerSound( CSound *pSound );

	// From QuerySeeEntity(): the player is in range. False means "not sure yet",
	// and the NPC's senses then do not see them. flDarkRate: how fast it makes out
	// a player in complete darkness, against one in the light. 1 is night vision,
	// 0 cannot see them there at all.
	bool	SeePlayer( CBasePlayer *pPlayer, float flDarkRate = 1.0f );

	// For an NPC without night vision (flDarkRate 0): is the player in such
	// darkness that it cannot see them, known enemy or not? For its FVisible().
	bool	IsPlayerHidden( CBasePlayer *pPlayer, float flDarkRate );

	// From UpdateEnemyMemory(), second hand. True if it was taken as a report
	// (go and look there) instead of as knowing where the enemy is.
	bool	TakeEnemyReport( CBaseEntity *pEnemy, const Vector &vecPos, CBaseEntity *pInformer );

	// The squad has a new enemy. False if this NPC should not simply be handed it.
	bool	AcceptsSquadEnemy( CBaseEntity *pEnemy );

	// A squadmate is on to something at this place
	void	TakeAlert( const Vector &vecPos );

	// From OnTakeDamage_Alive(): whoever did that has its full attention
	void	TookDamage( CBaseEntity *pAttacker );

	// From UpdateEnemyMemory(): word of the player it is fighting, its own or a squadmate's
	void	EnemyUpdated( void )					{ m_flEnemyInfoTime = gpGlobals->curtime; }

	// Once per think, before Update(). flDarkRate as for SeePlayer(); bSpotLit: for an
	// NPC without night vision, is the place it last knew the player to be lit (by the
	// room or by its flashlight), so that it could see that nobody is there?
	void	SetVision( float flDarkRate, bool bSpotLit )	{ m_flDarkRate = flDarkRate; m_bSpotLit = bSpotLit; }

	// Fighting a player it cannot see: fire at where it last knew them to be?
	bool	ShouldSuppressLKP( void );

	// Has fought the player, been shot at or heard shots: it does not stand down again
	bool	KnowsEnemyAbout( void ) const			{ return m_bEnemyAbout; }

	void	AddEvidence( float flAmount, const Vector &vecPos, OF2Stimulus_t kind );

	//-------------------------------------------------------------------------
	// For schedule selection
	//-------------------------------------------------------------------------
	OF2Awareness_t	State( void ) const				{ return (OF2Awareness_t)m_iState; }
	float	Suspicion( void ) const					{ return m_flSuspicion; }
	float	Detection( void ) const					{ return m_flDetection; }

	const Vector &StimulusPos( void ) const			{ return m_vecStimulus; }
	OF2Stimulus_t StimulusKind( void ) const		{ return (OF2Stimulus_t)m_iStimulus; }
	// Worth running for?
	bool	IsUrgent( void ) const					{ return m_iStimulus >= STIM_GUNFIRE; }

	// Something new to react to since the last TakeChanged(). The owner turns
	// this into a condition that breaks its idle schedules.
	bool	HasChanged( void ) const				{ return m_bChanged; }
	void	TakeChanged( void );

	// Suspicious: still owes the stimulus a look
	bool	WantsLook( void ) const					{ return m_bWantsLook; }
	void	Looked( void )							{ m_bWantsLook = false; }

	// Searching: has it been to where the stimulus was yet?
	bool	HasVisited( void ) const				{ return m_bVisited; }
	void	Visited( void )							{ m_bVisited = true; }

	// A place near the stimulus to go and look at. Prefers ones it cannot see
	// from where it stands. False if the node graph has nothing.
	bool	PickSearchPoint( Vector *pResult );

	// What to run now. bHasDuties: it has somewhere to be (a patrol, a path), so
	// it does not walk back to where it stood afterwards.
	OF2StealthSched_t SelectSchedule( bool bHasDuties );
	// True: now chain TASK_FACE_IDEAL (the two FACE tasks)
	bool	StartTask( OF2StealthTask_t task );

	// A way to a place something was heard or seen, which need not be anywhere
	// an NPC can stand
	bool	GetPathTo( const Vector &vecGoal, bool bRun );

	// Calmed down again: where it stood before all this
	bool	WantsReturn( void ) const				{ return m_bWantsReturn; }
	void	ClearReturn( void )						{ m_bWantsReturn = false; }
	const Vector &HomePos( void ) const				{ return m_vecHome; }
	float	HomeYaw( void ) const					{ return m_flHomeYaw; }

	void	DrawDebug( void );

private:
	void	SetState( OF2Awareness_t state );
	bool	KnowsPlayer( CBasePlayer *pPlayer ) const;
	bool	AlreadyHeard( CSound *pSound );
	void	AlertSquad( void );

	CAI_BaseNPC	*m_pOuter;

	int		m_iState;
	bool	m_bDisabled;

	float	m_flSuspicion;			// 0..1, from everything
	float	m_flDetection;			// 0..1, from seeing the player; 1 is a positive identification
	float	m_flLastEvidenceTime;
	float	m_flLastSeenTime;		// last time the player was in view
	float	m_flLastLookTime;		// last SeePlayer()
	float	m_flLastListenTime;		// last HearSounds()
	float	m_flLastUpdateTime;
	float	m_flSearchEndTime;
	float	m_flNextReportTime;
	float	m_flEnemyInfoTime;		// last word of where the player it is fighting is
	bool	m_bEnemyAbout;
	float	m_flSpotEmptyTime;		// how long it has looked at the last known position without seeing the player
	Vector	m_vecLastLKP;
	float	m_flDarkRate;			// not saved: set every think
	bool	m_bSpotLit;
	bool	m_bHeardShot;			// inside its own UpdateEnemyMemory() call

	Vector	m_vecStimulus;
	int		m_iStimulus;
	float	m_flStimulusTime;
	Vector	m_vecActedOn;			// where the stimulus was when the owner last reacted
	float	m_flChangedTime;

	bool	m_bChanged;
	bool	m_bWantsLook;
	bool	m_bVisited;
	bool	m_bWantsReturn;

	Vector	m_vecHome;
	float	m_flHomeYaw;

	// Sounds last longer than a think. Not saved.
	CSound	*m_pHeard[OF2_STEALTH_HEARD_SOUNDS];
	float	m_flHeardExpire[OF2_STEALTH_HEARD_SOUNDS];
	int		m_iHeardNext;
	EHANDLE	m_hLastNoiseOwner;
	float	m_flLastNoiseTime;
};

//-----------------------------------------------------------------------------
// Player visibility: 0 in the dark, 1 in the light. The client works it out from
// the lighting where the player stands and tells the server (the server has no
// lighting to ask). 1 until it has.
//-----------------------------------------------------------------------------
float	OF2_GetPlayerVisibility( CBasePlayer *pPlayer );
void	OF2_SetPlayerVisibility( CBasePlayer *pPlayer, float flVisibility );

// Something has a light on the player (a metrocop's flashlight): fully visible
// to everyone for this long
void	OF2_PlayerLit( float flDuration );

//-----------------------------------------------------------------------------
// A physics prop hit something: make a noise NPCs can hear (SOUND_WORLD), as
// loud as the hit was hard. flNextTime is the prop's own, to keep a rattling
// prop from filling the sound list.
//-----------------------------------------------------------------------------
void	OF2_PropImpactNoise( CBaseEntity *pProp, int index, gamevcollisionevent_t *pEvent, float &flNextTime );

#endif // OF2_STEALTH_H
