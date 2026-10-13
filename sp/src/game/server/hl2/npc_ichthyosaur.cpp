//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Ichthyosaur - buh bum...  buh bum...
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "ai_basenpc.h"
#include "ai_task.h"
#include "ai_default.h"
#include "ai_schedule.h"
#include "ai_hull.h"
#include "ai_interactions.h"
#include "ai_navigator.h"
#include "ai_motor.h"
#include "activitylist.h"
#include "game.h"
#include "npcevent.h"
#include "player.h"
#include "entitylist.h"
#include "soundenvelope.h"
#include "shake.h"
#include "ndebugoverlay.h"
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"
#include "movevars_shared.h"
#include "igamesystem.h"		// OF2
#include "collisionutils.h"		// OF2
#include "hl2_player.h"			// OF2
#include "te_effect_dispatch.h"	// OF2
#include "physics_prop_ragdoll.h"	// OF2
#include "of2_stealth.h"			// OF2

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar	sk_ichthyosaur_health( "sk_ichthyosaur_health", "0" );
ConVar	sk_ichthyosaur_melee_dmg( "sk_ichthyosaur_melee_dmg", "0" );

// OF2: the grab Valve wrote and never switched on (EnsnareVictim was commented out of Bite)
ConVar	of2_ichthyosaur_grab( "of2_ichthyosaur_grab", "1", FCVAR_NONE, "1: an ichthyosaur's bite can catch the player and drag them under. 0: it only bites, as in stock HL2." );
ConVar	of2_ichthyosaur_grab_chance( "of2_ichthyosaur_grab_chance", "0.33", FCVAR_NONE, "The chance that an ichthyosaur's bite catches the player, whatever their health (1: every bite, 0: never by chance). One out of the water is always caught." );
ConVar	of2_ichthyosaur_hold_max( "of2_ichthyosaur_hold_max", "8", FCVAR_NONE, "Longest an ichthyosaur holds the player, in seconds. It otherwise lets go 2 seconds before their air runs out." );
ConVar	of2_ichthyosaur_wander( "of2_ichthyosaur_wander", "1", FCVAR_NONE, "1: an ichthyosaur with nothing to do swims about slowly. 0: it hangs still, as in stock HL2." );
ConVar	of2_ichthyosaur_depth( "of2_ichthyosaur_depth", "40", FCVAR_NONE, "How far under the surface an ichthyosaur keeps its origin (the middle of its body), in units. Stock was 8, with its back out of the water." );
ConVar	of2_ichthyosaur_avoid_dist( "of2_ichthyosaur_avoid_dist", "300", FCVAR_NONE, "How far ahead an ichthyosaur looks for something to steer round, in units, when a second of its speed is less than that. Stock looked one second ahead and no further (150 units at a slow swim)." );
ConVar	of2_ichthyosaur_clearance( "of2_ichthyosaur_clearance", "130", FCVAR_NONE, "The room an ichthyosaur keeps to its sides and under it, measured from its middle and from the middle of its tail, in units. 0: none, as in stock." );
ConVar	of2_ichthyosaur_bend( "of2_ichthyosaur_bend", "3", FCVAR_NONE, "How far an ichthyosaur's body curves into a turn: degrees of bend per degree a second of turning (up to 90; at 3, a turn of 30 degrees a second is the full bend). Negative curves it the other way, 0 not at all." );
// OF2: the leap out of the water at a player hanging over it (the user's idea)
ConVar	of2_ichthyosaur_leap( "of2_ichthyosaur_leap", "1", FCVAR_NONE, "1: an ichthyosaur jumps out of the water at a player hanging above it. 0: it cannot see anyone out of the water, as before." );
ConVar	of2_ichthyosaur_leap_grab( "of2_ichthyosaur_leap_grab", "400", FCVAR_NONE, "How high above the water a player's feet may be for an ichthyosaur's jump to reach them, in units." );
ConVar	of2_ichthyosaur_leap_try( "of2_ichthyosaur_leap_try", "600", FCVAR_NONE, "Up to this height above the water an ichthyosaur still jumps at the player and falls short. Higher than this it takes no notice of them." );
ConVar	of2_ichthyosaur_leap_delay( "of2_ichthyosaur_leap_delay", "6", FCVAR_NONE, "Seconds between an ichthyosaur's jumps." );
ConVar	of2_ichthyosaur_leap_depth( "of2_ichthyosaur_leap_depth", "120", FCVAR_NONE, "The least depth of water under the player for an ichthyosaur to jump from, in units. It dives up to 300 for its run-up." );
ConVar	of2_ichthyosaur_leap_accel( "of2_ichthyosaur_leap_accel", "2500", FCVAR_NONE, "How fast an ichthyosaur gathers speed on its way up to a jump, in units a second per second." );
// OF2: the lunge onto a shore, a ramp or a low walkway at whoever stands by the water (the user's idea)
ConVar	of2_ichthyosaur_lunge( "of2_ichthyosaur_lunge", "1", FCVAR_NONE, "1: an ichthyosaur comes up out of the water after someone standing beside it. 0: it cannot see anyone on land, as before." );
ConVar	of2_ichthyosaur_lunge_range( "of2_ichthyosaur_lunge_range", "350", FCVAR_NONE, "How far from water deep enough to swim in someone may stand and still be lunged at, in units along the ground." );
ConVar	of2_ichthyosaur_lunge_height( "of2_ichthyosaur_lunge_height", "80", FCVAR_NONE, "How high above the water their feet may be, in units." );
ConVar	of2_ichthyosaur_lunge_clip( "of2_ichthyosaur_lunge_clip", "20", FCVAR_NONE, "How far into the ground and walls its box may go on the way up out of the water, in units (0 = not at all, at most 32). The user: slight clipping is fine." );
ConVar	of2_ichthyosaur_lunge_delay( "of2_ichthyosaur_lunge_delay", "8", FCVAR_NONE, "Seconds between an ichthyosaur's lunges." );
ConVar	of2_ichthyosaur_lunge_speed( "of2_ichthyosaur_lunge_speed", "650", FCVAR_NONE, "How fast an ichthyosaur comes up at them, in units a second." );
ConVar	of2_ichthyosaur_lunge_back( "of2_ichthyosaur_lunge_back", "300", FCVAR_NONE, "How fast it slides back into the water afterwards, in units a second." );
ConVar	of2_ichthyosaur_lunge_guard( "of2_ichthyosaur_lunge_guard", "1", FCVAR_NONE, "1: an antlion guard by the water is taken as well." );
ConVar	of2_ichthyosaur_prey_time( "of2_ichthyosaur_prey_time", "6", FCVAR_NONE, "How long an ichthyosaur keeps the body of an NPC it took in its mouth, in seconds." );
ConVar	of2_ichthyosaur_prey_dist( "of2_ichthyosaur_prey_dist", "0.5", FCVAR_NONE, "How far ahead of an ichthyosaur the body in its mouth is held, as a share of the way to the tip of its jaws (1). An antlion guard is always held at 1." );
ConVar	of2_ichthyosaur_swallow( "of2_ichthyosaur_swallow", "2.5", FCVAR_NONE, "Seconds after taking an NPC that an ichthyosaur swallows it: the body goes down its throat and is gone. 0: it lets the body go after of2_ichthyosaur_prey_time instead. An antlion guard is never swallowed." );
ConVar	of2_ichthyosaur_blood( "of2_ichthyosaur_blood", "1", FCVAR_NONE, "How much blood there is when an ichthyosaur takes an NPC: 0 none, 1 a lot, 2 and up that many times more." );
ConVar	of2_ichthyosaur_debug( "of2_ichthyosaur_debug", "0", FCVAR_NONE, "1: every ichthyosaur prints what it is doing twice a second and draws a line to where it is swimming." );
ConVar	of2_ichthyosaur_hold_dist( "of2_ichthyosaur_hold_dist", "105", FCVAR_NONE, "How far in front of an ichthyosaur's origin the middle of the player it holds is kept, in units." );

// OF2: its body is a server ragdoll (COF2IchthyosaurCorpses)
ConVar	of2_ichthyosaur_corpse_solid( "of2_ichthyosaur_corpse_solid", "1", FCVAR_NONE, "1: a dead ichthyosaur is solid to the player, from the moment they are clear of it: it blocks them and they push it by moving against it. 0: they pass through it, as through every other body." );
ConVar	of2_ichthyosaur_corpse_push( "of2_ichthyosaur_corpse_push", "400", FCVAR_NONE, "How hard a dead ichthyosaur is pushed away from a player who is inside it (always so with of2_ichthyosaur_corpse_solid 0, otherwise only until they are clear of it): the speed its parts gain a second, in units, on top of the player's own speed. 0: not pushed." );

#define	ICHTHYOSAUR_MODEL	"models/ichthyosaur.mdl"

#define	ICH_HEIGHT_PREFERENCE	16.0f
#define	ICH_DEPTH_PREFERENCE	8.0f

#define	ICH_WAYPOINT_DISTANCE	64.0f

#define	ICH_AE_BITE				11
#define	ICH_AE_BITE_START		12

#define	ICH_SWIM_SPEED_WALK		150
#define	ICH_SWIM_SPEED_RUN		500

#define	ICH_MIN_TURN_SPEED		4.0f
#define	ICH_MAX_TURN_SPEED		30.0f

// OF2: a victim this far from the mouth has got away (blocked by the world, teleported)
#define	ICH_HOLD_LOST_DIST		200.0f
// OF2: steepest it points up or down, degrees
#define	ICH_MAX_PITCH			60.0f

// OF2: the size of the model against Valve's: SCALE in the model's compile.bat. Keep the two the same.
#define	ICH_MODEL_SCALE			1.5f
// OF2: how fast a caught victim is drawn in to the mouth, units a second
#define	ICH_HOLD_PULL_SPEED		400.0f

#define	ENVELOPE_CONTROLLER		(CSoundEnvelopeController::GetController())

#define	FEELER_COLLISION			0
#define	FEELER_COLLISION_VISUALIZE	(FEELER_COLLISION&&0)

enum IchthyosaurMoveType_t
{
	ICH_MOVETYPE_SEEK = 0,	// Fly through the target without stopping.
	ICH_MOVETYPE_ARRIVE		// Slow down and stop at target.
};

//
// CNPC_Ichthyosaur
//

class CNPC_Ichthyosaur : public CAI_BaseNPC, public IMotionEvent	// OF2: IMotionEvent holds the body it carries
{
public:
	DECLARE_CLASS( CNPC_Ichthyosaur, CAI_BaseNPC );
	DECLARE_DATADESC();

	CNPC_Ichthyosaur( void ) {}

	int		SelectSchedule( void );
	int		MeleeAttack1Conditions( float flDot, float flDist );
	int		OnTakeDamage_Alive( const CTakeDamageInfo &info );
	int		TranslateSchedule( int type );

	void	Precache( void );
	void	Spawn( void );
	void	MoveFlyExecute( CBaseEntity *pTargetEnt, const Vector & vecDir, float flDistance, float flInterval );
	void	HandleAnimEvent( animevent_t *pEvent );
	void	PrescheduleThink( void );
	bool	OverrideMove( float flInterval );
	void	StartTask( const Task_t *pTask );
	void	RunTask( const Task_t *pTask );
	void	TranslateNavGoal( CBaseEntity *pEnemy, Vector &chasePosition );
	float	GetDefaultNavGoalTolerance();

	float	MaxYawSpeed( void );

	Class_T Classify( void )	{	return CLASS_ICHTHYOSAUR;	}	// OF2: it was CLASS_ANTLION, for want of a class for wildlife

	// OF2
	void	Event_Killed( const CTakeDamageInfo &info );
	void	UpdateOnRemove( void );
	void	StopLoopingSounds( void );

	bool	FVisible( CBaseEntity *pEntity, int traceMask = MASK_BLOCKLOS, CBaseEntity **ppBlocker = NULL );

private:

	bool	SteerAvoidObstacles( Vector &Steer, const Vector &Velocity, const Vector &Forward, const Vector &Right, const Vector &Up );
	bool	Beached( void );

	void	DoMovement( float flInterval, const Vector &MoveTarget, int eMoveType );
	void	SteerArrive( Vector &Steer, const Vector &Target );
	void	SteerSeek( Vector &Steer, const Vector &Target );
	void	ClampSteer( Vector &SteerAbs, Vector &SteerRel, Vector &forward, Vector &right, Vector &up );
	void	AddSwimNoise( Vector *velocity );

	void	Bite( void );
	void	EnsnareVictim( CBaseEntity *pVictim );
	void	ReleaseVictim( void );
	void	DragVictim( float flInterval );

	void	SetPoses( Vector moveRel, float speed );

	//void	IchTouch( CBaseEntity *pOther );

	float	GetGroundSpeed( void );

#if FEELER_COLLISION
	Vector	DoProbe( const Vector &Probe );
	Vector	m_LastSteer;
#endif

	CBaseEntity	*m_pVictim;

	static const Vector	m_vecAccelerationMax;
	static const Vector	m_vecAccelerationMin;

	Vector	m_vecLastMoveTarget;
		
	float	m_flNextBiteTime;
	float	m_flHoldTime;
	float	m_flSwimSpeed;
	float	m_flTailYaw;
	float	m_flTailPitch;

	float	m_flNextPingTime;
	float	m_flNextGrowlTime;

	bool	m_bHasMoveTarget;
	bool	m_bIgnoreSurface;

	//CSoundPatch	*m_pSwimSound;
	//CSoundPatch	*m_pVoiceSound;
	CSoundPatch	*m_pBreathSound;	// OF2
	Vector		m_vecVictimFree;	// OF2: the last place the victim fitted
	float		m_flBlockedTime;	// OF2: when we last made headway along a path
	bool		m_bPitchBlocked;	// OF2: the last move could not go up or down as it wanted

	Vector		m_vecBlockedPos;	// OF2: where we were then
	float		m_flChargeTime;		// OF2: when TASK_ICH_CHARGE last set the move target

	bool	SetWanderGoal( float flDist );	// OF2

	// OF2: the leap. The motion is OverrideMove's (LeapMove) and goes on to the end whatever the schedule does
	// once it is in the air; TASK_ICH_LEAP only starts it and waits.
	enum
	{
		ICH_LEAP_NONE = 0,
		ICH_LEAP_DIVE,		// getting under them, down for the run-up
		ICH_LEAP_RISE,		// up through the water
		ICH_LEAP_AIR,		// out of it, falling
	};

	bool	IsOverWater( CBaseEntity *pEntity, float *pflHeight, float *pflSurfaceZ );
	bool	StartLeap( void );
	void	EndLeap( void );
	bool	LeapMove( float flInterval );
	void	LeapSplash( void );

	int			m_iLeapPhase;
	float		m_flLeapPhaseTime;	// when this phase began
	float		m_flLeapTaskTime;	// when TASK_ICH_LEAP last ran
	float		m_flNextLeapTime;
	float		m_flLeapSurfaceZ;
	float		m_flLeapDepth;		// how far under the surface the run-up starts
	float		m_flLeapExitSpeed;	// upward speed to leave the water with
	bool		m_bLeapSnapped;		// the jaws have shut on this jump, on someone or on nothing

	// OF2: the lunge. A curve worked out beforehand, from water it can swim in up to its prey and back the same
	// way; LungeMove puts it along that by hand. TASK_ICH_LUNGE only starts it and waits.
	enum
	{
		ICH_LUNGE_NONE = 0,
		ICH_LUNGE_APPROACH,	// swimming to where the curve starts
		ICH_LUNGE_STRIKE,	// up the curve
		ICH_LUNGE_BITE,		// at the top of it, jaws shut
		ICH_LUNGE_RETURN,	// back down it, tail first
	};

	bool	IsLungePrey( CBaseEntity *pEntity );
	bool	IsOnShore( CBaseEntity *pEntity );
	float	GetSurfaceZ( void );
	bool	FindLungeStart( CBaseEntity *pTarget, Vector *pvecStart );
	bool	SetLungeCurve( CBaseEntity *pTarget );
	void	LungeBox( Vector *pvecMins, Vector *pvecMaxs );
	Vector	LungePoint( float t, Vector *pvecTangent = NULL );
	bool	StartLunge( void );
	void	EndLunge( void );
	bool	LungeMove( float flInterval );
	void	CatchPrey( CAI_BaseNPC *pNPC );
	void	UpdatePrey( void );
	void	ReleasePrey( void );
	virtual simresult_e	Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular );

	IPhysicsMotionController	*m_pPreyController;	// not saved
	Vector		m_vecPreyMouthWas;	// where the mouth was a think ago, and is now
	Vector		m_vecPreyMouth;
	float		m_flPreyMouthTime;
	float		m_flPreyDist;		// how far ahead of us the body is held
	int			m_iPreyBlood;		// its blood, and until when it runs
	float		m_flPreyBleedTime;
	float		m_flSwallowTime;	// when it starts down the throat (0: it is not swallowed)
	float		m_flPreyDistHeld;	// m_flPreyDist before that
	void	PreyBlood( const Vector &vecAt, bool bBurst );

	int			m_iLungePhase;
	float		m_flLungePhaseTime;
	float		m_flLungeTaskTime;
	float		m_flNextLungeTime;
	Vector		m_vecLungeStart;	// the curve: from here in the water,
	Vector		m_vecLungeMid;		// drawn up towards this,
	Vector		m_vecLungeEnd;		// to here, with the mouth on the prey
	float		m_flLungeLength;
	float		m_flLungeAlong;		// how far along it we are, in units
	EHANDLE		m_hLungeTarget;
	EHANDLE		m_hPrey;			// the body of an NPC in our mouth
	float		m_flPreyTime;		// when we let it go
	float		m_flSurfaceZ;		// the surface over us when last asked
	float		m_flSurfaceTime;
	void	SteerKeepDepth( Vector &Steer );	// OF2
	void	SteerKeepClear( Vector &Steer, const Vector &forward, const Vector &right, const Vector &up );	// OF2
	float		m_flAvoidReach;		// OF2: how far it is to where we are swimming; nothing beyond that is steered round
	float		m_flPoseYaw;		// OF2: heading and pitch at the last SetPoses, for the turn the body curves into
	float		m_flPosePitch;
	float		m_flPoseTime;

	DEFINE_CUSTOM_AI;
};

//Acceleration definitions
const Vector CNPC_Ichthyosaur::m_vecAccelerationMax	= Vector(  256,  1024,  512 );
const Vector CNPC_Ichthyosaur::m_vecAccelerationMin	= Vector( -256, -1024, -512 );

//Data description
BEGIN_DATADESC( CNPC_Ichthyosaur )

// Silence classcheck
//	DEFINE_FIELD( m_LastSteer, FIELD_VECTOR ),

	DEFINE_FIELD( m_pVictim,				FIELD_CLASSPTR ),
	DEFINE_FIELD( m_vecLastMoveTarget,	FIELD_VECTOR ),
	DEFINE_FIELD( m_flNextBiteTime,		FIELD_FLOAT ),
	DEFINE_FIELD( m_flHoldTime,			FIELD_FLOAT ),
	DEFINE_FIELD( m_flSwimSpeed,			FIELD_FLOAT ),
	DEFINE_FIELD( m_flTailYaw,			FIELD_FLOAT ),
	DEFINE_FIELD( m_flTailPitch,			FIELD_FLOAT ),
	DEFINE_FIELD( m_flAvoidReach,		FIELD_FLOAT ),
	DEFINE_FIELD( m_iLeapPhase,			FIELD_INTEGER ),
	DEFINE_FIELD( m_flLeapPhaseTime,		FIELD_TIME ),
	DEFINE_FIELD( m_flLeapTaskTime,		FIELD_TIME ),
	DEFINE_FIELD( m_flNextLeapTime,		FIELD_TIME ),
	DEFINE_FIELD( m_flLeapSurfaceZ,		FIELD_FLOAT ),
	DEFINE_FIELD( m_flLeapDepth,		FIELD_FLOAT ),
	DEFINE_FIELD( m_flLeapExitSpeed,		FIELD_FLOAT ),
	DEFINE_FIELD( m_bLeapSnapped,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_iLungePhase,		FIELD_INTEGER ),
	DEFINE_FIELD( m_flLungePhaseTime,	FIELD_TIME ),
	DEFINE_FIELD( m_flLungeTaskTime,	FIELD_TIME ),
	DEFINE_FIELD( m_flNextLungeTime,	FIELD_TIME ),
	DEFINE_FIELD( m_vecLungeStart,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecLungeMid,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecLungeEnd,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_flLungeLength,		FIELD_FLOAT ),
	DEFINE_FIELD( m_flLungeAlong,		FIELD_FLOAT ),
	DEFINE_FIELD( m_hLungeTarget,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_hPrey,				FIELD_EHANDLE ),
	DEFINE_FIELD( m_flPreyTime,			FIELD_TIME ),
	DEFINE_FIELD( m_flSurfaceZ,			FIELD_FLOAT ),
	DEFINE_FIELD( m_flSurfaceTime,		FIELD_TIME ),
	DEFINE_FIELD( m_flPoseYaw,			FIELD_FLOAT ),
	DEFINE_FIELD( m_flPosePitch,			FIELD_FLOAT ),
	DEFINE_FIELD( m_flPoseTime,			FIELD_TIME ),
	DEFINE_FIELD( m_flNextPingTime,		FIELD_FLOAT ),
	DEFINE_FIELD( m_flNextGrowlTime,		FIELD_FLOAT ),
	DEFINE_FIELD( m_bHasMoveTarget,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bIgnoreSurface,		FIELD_BOOLEAN ),
	DEFINE_SOUNDPATCH( m_pBreathSound ),
	DEFINE_FIELD( m_vecVictimFree,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_flBlockedTime,		FIELD_TIME ),
	DEFINE_FIELD( m_bPitchBlocked,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vecBlockedPos,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_flChargeTime,		FIELD_TIME ),

	//DEFINE_FUNCTION( IchTouch ),

END_DATADESC()

//Schedules
enum IchSchedules
{
	SCHED_ICH_CHASE_ENEMY = LAST_SHARED_SCHEDULE,
	SCHED_ICH_PATROL_RUN,
	SCHED_ICH_PATROL_WALK,
	SCHED_ICH_DROWN_VICTIM,
	SCHED_ICH_MELEE_ATTACK1,
	SCHED_ICH_THRASH,
	SCHED_ICH_CHARGE,	// OF2
	SCHED_ICH_LEAP,		// OF2
	SCHED_ICH_LUNGE,	// OF2
};

//Tasks
enum IchTasks
{
	TASK_ICH_GET_PATH_TO_RANDOM_NODE = LAST_SHARED_TASK,
	TASK_ICH_GET_PATH_TO_DROWN_NODE,
	TASK_ICH_THRASH_PATH,
	TASK_ICH_CHARGE,	// OF2
	TASK_ICH_LEAP,		// OF2
	TASK_ICH_LUNGE,		// OF2
};

//Activities
int	ACT_ICH_THRASH;
int	ACT_ICH_BITE_HIT;
int	ACT_ICH_BITE_MISS;

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::InitCustomSchedules( void ) 
{
	INIT_CUSTOM_AI( CNPC_Ichthyosaur );

	//Interaction	REGISTER_INTERACTION( g_interactionAntlionAttacked );

	//Schedules
	ADD_CUSTOM_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_CHASE_ENEMY );
	ADD_CUSTOM_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_PATROL_RUN );
	ADD_CUSTOM_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_PATROL_WALK );
	ADD_CUSTOM_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_DROWN_VICTIM );
	ADD_CUSTOM_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_MELEE_ATTACK1 );
	ADD_CUSTOM_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_THRASH );
	ADD_CUSTOM_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_CHARGE );
	ADD_CUSTOM_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_LEAP );
	ADD_CUSTOM_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_LUNGE );
	
	//Tasks
	ADD_CUSTOM_TASK( CNPC_Ichthyosaur,		TASK_ICH_GET_PATH_TO_RANDOM_NODE );
	ADD_CUSTOM_TASK( CNPC_Ichthyosaur,		TASK_ICH_GET_PATH_TO_DROWN_NODE );
	ADD_CUSTOM_TASK( CNPC_Ichthyosaur,		TASK_ICH_THRASH_PATH );
	ADD_CUSTOM_TASK( CNPC_Ichthyosaur,		TASK_ICH_CHARGE );
	ADD_CUSTOM_TASK( CNPC_Ichthyosaur,		TASK_ICH_LEAP );
	ADD_CUSTOM_TASK( CNPC_Ichthyosaur,		TASK_ICH_LUNGE );

	//Conditions	ADD_CUSTOM_CONDITION( CNPC_CombineGuard,	COND_ANTLIONGRUB_HEARD_SQUEAL );

	//Activities
	ADD_CUSTOM_ACTIVITY( CNPC_Ichthyosaur,	ACT_ICH_THRASH );
	ADD_CUSTOM_ACTIVITY( CNPC_Ichthyosaur,	ACT_ICH_BITE_HIT );
	ADD_CUSTOM_ACTIVITY( CNPC_Ichthyosaur,	ACT_ICH_BITE_MISS );

	AI_LOAD_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_CHASE_ENEMY );
	AI_LOAD_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_PATROL_RUN );
	AI_LOAD_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_PATROL_WALK );
	AI_LOAD_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_DROWN_VICTIM );
	AI_LOAD_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_MELEE_ATTACK1 );
	AI_LOAD_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_THRASH );
	AI_LOAD_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_CHARGE );
	AI_LOAD_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_LEAP );
	AI_LOAD_SCHEDULE( CNPC_Ichthyosaur,	SCHED_ICH_LUNGE );
}

LINK_ENTITY_TO_CLASS( npc_ichthyosaur, CNPC_Ichthyosaur );
IMPLEMENT_CUSTOM_AI( npc_ichthyosaur, CNPC_Ichthyosaur );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::Precache( void )
{
	PrecacheModel( ICHTHYOSAUR_MODEL );

	PrecacheScriptSound( "NPC_Ichthyosaur.Bite" );
	PrecacheScriptSound( "NPC_Ichthyosaur.BiteMiss" );
	PrecacheScriptSound( "NPC_Ichthyosaur.AttackGrowl" );

	// OF2: in the mod's game_sounds_of2.txt
	PrecacheScriptSound( "NPC_Ichthyosaur.OF2Breath" );
	PrecacheScriptSound( "NPC_Ichthyosaur.OF2Growl" );
	PrecacheScriptSound( "Physics.WaterSplash" );

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::Spawn( void )
{
	Precache();

	SetModel( ICHTHYOSAUR_MODEL );

	SetHullType(HULL_LARGE_CENTERED);
	SetHullSizeNormal();
	SetDefaultEyeOffset();
	
	SetNavType( NAV_FLY );
	m_NPCState				= NPC_STATE_NONE;
	SetBloodColor( BLOOD_COLOR_RED );
	m_iHealth				= sk_ichthyosaur_health.GetFloat();
	m_iMaxHealth			= m_iHealth;
	m_flFieldOfView			= -0.707;	// 270 degrees
	SetDistLook( 1024 );

	SetSolid( SOLID_BBOX );
	AddSolidFlags( FSOLID_NOT_STANDABLE );
	SetMoveType( MOVETYPE_STEP );
	AddFlag( FL_FLY | FL_STEPMOVEMENT );

	m_flGroundSpeed			= ICH_SWIM_SPEED_RUN;

	m_bIgnoreSurface		= false;

	m_flSwimSpeed			= 0.0f;
	m_flTailYaw				= 0.0f;
	m_flTailPitch			= 0.0f;

	m_flNextBiteTime		= gpGlobals->curtime;
	m_flHoldTime			= gpGlobals->curtime;
	m_flNextPingTime		= gpGlobals->curtime;
	m_flNextGrowlTime		= gpGlobals->curtime;

	m_pVictim				= NULL;
	m_pBreathSound			= NULL;
	m_flBlockedTime			= gpGlobals->curtime;
	m_bHasMoveTarget		= false;
	m_bPitchBlocked			= false;
	m_vecBlockedPos			= GetAbsOrigin();
	m_flChargeTime			= 0.0f;
	m_flAvoidReach			= 0.0f;
	m_iLeapPhase			= ICH_LEAP_NONE;
	m_flLeapPhaseTime		= gpGlobals->curtime;
	m_flLeapTaskTime		= 0.0f;
	m_flNextLeapTime		= gpGlobals->curtime;
	m_flLeapSurfaceZ		= 0.0f;
	m_flLeapDepth			= 0.0f;
	m_flLeapExitSpeed		= 0.0f;
	m_bLeapSnapped			= false;
	m_iLungePhase			= ICH_LUNGE_NONE;
	m_flLungePhaseTime		= gpGlobals->curtime;
	m_flLungeTaskTime		= 0.0f;
	m_flNextLungeTime		= gpGlobals->curtime;
	m_vecLungeStart			= vec3_origin;
	m_vecLungeMid			= vec3_origin;
	m_vecLungeEnd			= vec3_origin;
	m_flLungeLength			= 0.0f;
	m_flLungeAlong			= 0.0f;
	m_hLungeTarget			= NULL;
	m_hPrey					= NULL;
	m_pPreyController		= NULL;
	m_vecPreyMouthWas		= vec3_origin;
	m_vecPreyMouth			= vec3_origin;
	m_flPreyMouthTime		= 0.0f;
	m_flPreyDist			= 0.0f;
	m_iPreyBlood			= DONT_BLEED;
	m_flPreyBleedTime		= 0.0f;
	m_flSwallowTime			= 0.0f;
	m_flPreyDistHeld		= 0.0f;
	m_flPreyTime			= 0.0f;
	m_flSurfaceZ			= 0.0f;
	m_flSurfaceTime			= -1.0f;
	m_flPoseYaw				= GetAbsAngles().y;
	m_flPosePitch			= GetAbsAngles().x;
	m_flPoseTime			= gpGlobals->curtime;

#if FEELER_COLLISION

	Vector	forward;

	GetVectors( &forward, NULL, NULL );

	m_vecCurrentVelocity	= forward * m_flGroundSpeed;

#endif

	//SetTouch( IchTouch );

	CapabilitiesClear();
	CapabilitiesAdd( bits_CAP_MOVE_FLY | bits_CAP_INNATE_MELEE_ATTACK1 );

	NPCInit();

	//m_pSwimSound	= ENVELOPE_CONTROLLER.SoundCreate( edict(), CHAN_BODY,	"xxxCONVERTTOGAMESOUNDS!!!npc/ichthyosaur/ich_amb1wav", ATTN_NORM );
	//m_pVoiceSound	= ENVELOPE_CONTROLLER.SoundCreate( edict(), CHAN_VOICE,	"xxxCONVERTTOGAMESOUNDS!!!npc/ichthyosaur/water_breathwav", ATTN_IDLE );

	//ENVELOPE_CONTROLLER.Play( m_pSwimSound,	1.0f, 100 );
	//ENVELOPE_CONTROLLER.Play( m_pVoiceSound,1.0f, 100 );

	BaseClass::Spawn();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pOther - 
//-----------------------------------------------------------------------------
/*
void CNPC_Ichthyosaur::IchTouch( CBaseEntity *pOther )
{
}
*/

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CNPC_Ichthyosaur::SelectSchedule( void )
{
	if ( m_NPCState == NPC_STATE_COMBAT )
	{
		if ( m_flHoldTime > gpGlobals->curtime )
			return SCHED_ICH_DROWN_VICTIM;

		// OF2: someone beside the water: up onto the land after them, and about in front of it in between
		if ( of2_ichthyosaur_lunge.GetBool() && IsOnShore( GetEnemy() ) )
		{
			if ( m_flNextLungeTime <= gpGlobals->curtime )
				return SCHED_ICH_LUNGE;

			return SCHED_PATROL_RUN;
		}

		// OF2: someone hanging over the water: jump at them, and swim about underneath between jumps. Under 48
		// units up the ordinary bite reaches them.
		float	flHeight;

		if ( of2_ichthyosaur_leap.GetBool() && IsOverWater( GetEnemy(), &flHeight, NULL ) && ( flHeight >= 48.0f ) )
		{
			if ( m_flNextLeapTime <= gpGlobals->curtime )
				return SCHED_ICH_LEAP;

			return SCHED_PATROL_RUN;
		}

		if ( m_flNextBiteTime > gpGlobals->curtime )
			return	SCHED_PATROL_RUN;

		if ( HasCondition( COND_CAN_MELEE_ATTACK1 ) )
			return	SCHED_MELEE_ATTACK1;

		return SCHED_CHASE_ENEMY;
	}

	return BaseClass::SelectSchedule();
}

//-----------------------------------------------------------------------------
// Purpose: Handles movement towards the last move target.
// Input  : flInterval - 
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::OverrideMove( float flInterval )
{
	m_flGroundSpeed = GetGroundSpeed();

	// OF2: a jump has the movement to itself from the run up to the splash
	if ( ( m_iLeapPhase != ICH_LEAP_NONE ) && LeapMove( flInterval ) )
		return true;

	// OF2: and so has a lunge
	if ( ( m_iLungePhase != ICH_LUNGE_NONE ) && LungeMove( flInterval ) )
		return true;

	// OF2: the steering was fed from MoveFlyExecute, which CAI_BaseNPC once called and no longer has: nothing called
	// it, there was never a move target, and the fish only moved while biting. Returning true here also keeps the
	// navigator from moving us, so its path is followed here, the way the flying NPCs do it.
	if ( GetNavigator()->IsGoalActive() )
	{
		AI_ProgressFlyPathParams_t params( MASK_NPCSOLID, 32.0f, 0.0f, 100.0f, ICH_WAYPOINT_DISTANCE + 32.0f );
		params.SetCurrent( GetEnemy() );

		if ( GetNavigator()->ProgressFlyPath( params ) == AINPP_COMPLETE )
		{
			TaskMovementComplete();
		}
		else
		{
			MoveFlyExecute( GetEnemy(), vec3_origin, 0.0f, flInterval );

			// Up against something with nowhere to go: call it arrived, and whatever sent us picks again.
			// By ground covered, not by speed: pressed against the world it keeps a speed it cannot use.
			if ( ( GetAbsOrigin() - m_vecBlockedPos ).LengthSqr() > Square( 48.0f ) )
			{
				m_flBlockedTime = gpGlobals->curtime;
				m_vecBlockedPos = GetAbsOrigin();
			}
			else if ( gpGlobals->curtime - m_flBlockedTime > 2.0f )
			{
				m_flBlockedTime = gpGlobals->curtime;
				GetNavigator()->ClearGoal();
				TaskMovementComplete();
			}

			return true;
		}
	}

	m_flBlockedTime = gpGlobals->curtime;
	m_vecBlockedPos = GetAbsOrigin();

	if ( m_bHasMoveTarget )
	{
		// OF2: straight at it without slowing while TASK_ICH_CHARGE keeps the target fresh
		bool bCharging = ( gpGlobals->curtime - m_flChargeTime ) < 0.3f;

		DoMovement( flInterval, m_vecLastMoveTarget, bCharging ? ICH_MOVETYPE_SEEK : ICH_MOVETYPE_ARRIVE );
	}
	else
	{
		DoMovement( flInterval, GetLocalOrigin(), ICH_MOVETYPE_ARRIVE );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &probe - 
// Output : Vector
//-----------------------------------------------------------------------------
#if FEELER_COLLISION

Vector CNPC_Ichthyosaur::DoProbe( const Vector &probe )
{
	trace_t	tr;
	float	fraction = 1.0f;
	bool	collided = false;
	Vector	normal	 = Vector( 0, 0, -1 );

	float	waterLevel = UTIL_WaterLevel( GetAbsOrigin(), GetAbsOrigin().z, GetAbsOrigin().z+150 );

	waterLevel -= GetAbsOrigin().z;
	waterLevel /= 150;

	if ( waterLevel < 1.0f )
	{
		collided = true;
		fraction = waterLevel;
	}

	AI_TraceHull( GetAbsOrigin(), probe, GetHullMins(), GetHullMaxs(), MASK_NPCSOLID, this, COLLISION_GROUP_NONE, &tr );
	
	if ( ( collided == false ) || ( tr.fraction < fraction ) )
	{
		fraction	= tr.fraction;
		normal		= tr.plane.normal;
	}

	if ( ( fraction < 1.0f ) && ( GetEnemy() == NULL || tr.u.ent != GetEnemy()->pev ) )
	{
#if FEELER_COLLISION_VISUALIZE
		NDebugOverlay::Line( GetLocalOrigin(), probe, 255, 0, 0, false, 0.1f );
#endif
		
		Vector	probeDir = probe - GetLocalOrigin();

		Vector	normalToProbeAndWallNormal = probeDir.Cross( normal );
		Vector	steeringVector = normalToProbeAndWallNormal.Cross( probeDir );

		Vector	velDir = GetAbsVelocity();
		VectorNormalize( velDir );

		float	steeringForce = m_flGroundSpeed * ( 1.0f - fraction ) * normal.Dot( velDir );

		if ( steeringForce < 0.0f )
		{
			steeringForce = -steeringForce;
		}

		velDir = steeringVector;
		VectorNormalize( velDir );

		steeringVector = steeringForce * velDir;
		
		return steeringVector;
	}

#if FEELER_COLLISION_VISUALIZE
	NDebugOverlay::Line( GetLocalOrigin(), probe, 0, 255, 0, false, 0.1f );
#endif

	return Vector( 0.0f, 0.0f, 0.0f );
}

#endif

//-----------------------------------------------------------------------------
// Purpose: Move the victim of a drag along with us
// Input  : moveDist - our amount of travel
//-----------------------------------------------------------------------------

void CNPC_Ichthyosaur::DragVictim( float flInterval )
{
	// OF2: Valve's version put the victim at a new place every think (ten jumps a second on screen), swept the
	// ichthyosaur's own box to get there and let go if that touched anything, which next to any wall or floor is
	// at once. The victim is parented to us instead (EnsnareVictim), so the client moves it with us every frame,
	// and this only sees to where in front of us it sits: drawn in to the mouth, and kept out of the world.
	Vector	forward;
	GetVectors( &forward, NULL, NULL );

	Vector	vecMiddle = m_pVictim->WorldSpaceCenter() - m_pVictim->GetAbsOrigin();
	Vector	vecHold = GetAbsOrigin() + ( forward * of2_ichthyosaur_hold_dist.GetFloat() ) - vecMiddle;

	trace_t	tr;
	CTraceFilterSkipTwoEntities filter( m_pVictim, this, COLLISION_GROUP_NONE );

	// We may have carried it into something since the last think: go on from the last place it fitted
	Vector	vecFrom = m_pVictim->GetAbsOrigin();
	UTIL_TraceHull( vecFrom, vecFrom, m_pVictim->WorldAlignMins(), m_pVictim->WorldAlignMaxs(), MASK_PLAYERSOLID, &filter, &tr );

	if ( tr.startsolid )
	{
		vecFrom = m_vecVictimFree;
	}

	Vector	vecTo = vecHold;
	Vector	vecStep = vecTo - vecFrom;
	float	flStep = vecStep.Length();
	float	flMaxStep = ICH_HOLD_PULL_SPEED * flInterval;

	if ( flStep > flMaxStep )
	{
		vecTo = vecFrom + ( vecStep * ( flMaxStep / flStep ) );
	}

	UTIL_TraceHull( vecFrom, vecTo, m_pVictim->WorldAlignMins(), m_pVictim->WorldAlignMaxs(), MASK_PLAYERSOLID, &filter, &tr );

	if ( tr.startsolid || ( ( vecHold - tr.endpos ).LengthSqr() > Square( ICH_HOLD_LOST_DIST ) ) )
	{
		ReleaseVictim();
		return;
	}

	m_pVictim->SetAbsOrigin( tr.endpos );
	m_vecVictimFree = tr.endpos;
}

//-----------------------------------------------------------------------------
// Purpose: Determines the pose parameters for the bending of the body and tail speed
// Input  : moveRel - the dot products for the deviation off of each direction (f,r,u)
//			speed - speed of the fish
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::SetPoses( Vector moveRel, float speed )
{
	float	movePerc, moveBase;

	//Find out how fast we're moving in our animations boundaries
	if ( GetIdealActivity() == ACT_WALK )
	{
		moveBase = 0.5f;
		movePerc = moveBase * ( speed / ICH_SWIM_SPEED_WALK );
	}
	else
	{
		moveBase = 1.0f;
		movePerc = moveBase * ( speed / ICH_SWIM_SPEED_RUN );
	}
	
	Vector	tailPosition;
	float	flSwimSpeed = movePerc;

	//Forward deviation
	if ( moveRel.x > 0 )
	{
		flSwimSpeed *= moveBase + (( moveRel.x / m_vecAccelerationMax.x )*moveBase);
	}
	else if ( moveRel.x < 0 )
	{
		flSwimSpeed *= moveBase - (( moveRel.x / m_vecAccelerationMin.x )*moveBase);
	}

	//Vertical deviation
	if ( moveRel.z > 0 )
	{
		tailPosition[PITCH]	= -90.0f * ( moveRel.z / m_vecAccelerationMax.z );
	}
	else if ( moveRel.z < 0 )
	{
		tailPosition[PITCH]	= 90.0f * ( moveRel.z / m_vecAccelerationMin.z );
	}
	else
	{
		tailPosition[PITCH]	= 0.0f;
	}

	//Lateral deviation
	if ( moveRel.y > 0 )
	{
		tailPosition[ROLL]	= 25 * moveRel.y / m_vecAccelerationMax.y;
		tailPosition[YAW]	= -1.0f * moveRel.y / m_vecAccelerationMax.y;
	}
	else if ( moveRel.y < 0 )
	{
		tailPosition[ROLL]	= -25 * moveRel.y / m_vecAccelerationMin.y;
		tailPosition[YAW]	= moveRel.y / m_vecAccelerationMin.y;
	}
	else
	{
		tailPosition[ROLL]	= 0.0f;
		tailPosition[YAW]	= 0.0f;
	}
	
	// OF2: the body's curve comes from how fast it is really turning, not from what the steering asks for.
	// Valve's sideways value above is between -1 and 1 on a dial that runs from -90 to 90 (the up and down one
	// had its 90), so the model's side to side bend never showed; and the steering is at its limit most of the
	// time, which would have slammed the bend from one end to the other. sidetoside: -90 is the bend to the
	// right, 90 to the left. upanddown: -90 up, 90 down.
	float	flPoseStep = gpGlobals->curtime - m_flPoseTime;

	if ( ( flPoseStep > 0.0f ) && ( flPoseStep < 1.0f ) )
	{
		float	flBend = of2_ichthyosaur_bend.GetFloat();

		tailPosition[YAW]	= flBend * ( AngleDiff( GetAbsAngles().y, m_flPoseYaw ) / flPoseStep );
		tailPosition[PITCH]	= flBend * ( AngleDiff( GetAbsAngles().x, m_flPosePitch ) / flPoseStep );
	}
	else
	{
		tailPosition[YAW]	= 0.0f;
		tailPosition[PITCH]	= 0.0f;
	}

	m_flPoseYaw		= GetAbsAngles().y;
	m_flPosePitch	= GetAbsAngles().x;
	m_flPoseTime	= gpGlobals->curtime;

	//Clamp
	flSwimSpeed			= clamp( flSwimSpeed, 0.25f, 1.0f );
	tailPosition[YAW]	= clamp( tailPosition[YAW], -90.0f, 90.0f );
	tailPosition[PITCH]	= clamp( tailPosition[PITCH], -90.0f, 90.0f );

	//Blend
	m_flTailYaw		= ( m_flTailYaw * 0.8f ) + ( tailPosition[YAW] * 0.2f );
	m_flTailPitch	= ( m_flTailPitch * 0.8f ) + ( tailPosition[PITCH] * 0.2f );
	m_flSwimSpeed	= ( m_flSwimSpeed * 0.8f ) + ( flSwimSpeed * 0.2f );

	//Pose the body
	SetPoseParameter( 0, m_flSwimSpeed );
	SetPoseParameter( 1, m_flTailYaw );
	SetPoseParameter( 2, m_flTailPitch );
	
	//FIXME: Until the sequence info is reset properly after SetPoseParameter
	if ( ( GetActivity() == ACT_RUN ) || ( GetActivity() == ACT_WALK ) )
	{
		ResetSequenceInfo();
	}

	//Face our current velocity
	GetMotor()->SetIdealYawAndUpdate( UTIL_AngleMod( CalcIdealYaw( GetAbsOrigin() + GetAbsVelocity() ) ), AI_KEEP_YAW_SPEED );

	float	pitch = 0.0f;

	// OF2: not from speed it could not use on the last move (it faced straight down at the bottom), and no steeper
	// than ICH_MAX_PITCH
	if ( ( speed != 0.0f ) && ( m_bPitchBlocked == false ) )
	{
		pitch = -RAD2DEG( asin( clamp( GetAbsVelocity().z / speed, -1.0f, 1.0f ) ) );
		pitch = clamp( pitch, -ICH_MAX_PITCH, ICH_MAX_PITCH );
	}

	m_bPitchBlocked = false;

	//FIXME: Framerate dependant
	QAngle angles = GetLocalAngles();

	angles.x = (angles.x * 0.8f) + (pitch * 0.2f);
	angles.z = (angles.z * 0.9f) + (tailPosition[ROLL] * 0.1f);

	SetLocalAngles( angles );
}

#define	LATERAL_NOISE_MAX	2.0f
#define	LATERAL_NOISE_FREQ	1.0f

#define	VERTICAL_NOISE_MAX	2.0f
#define	VERTICAL_NOISE_FREQ	1.0f

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : velocity - 
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::AddSwimNoise( Vector *velocity )
{
	Vector	right, up;

	GetVectors( NULL, &right, &up );

	float	lNoise, vNoise;

	lNoise = LATERAL_NOISE_MAX * sin( gpGlobals->curtime * LATERAL_NOISE_FREQ );
	vNoise = VERTICAL_NOISE_MAX * sin( gpGlobals->curtime * VERTICAL_NOISE_FREQ );

	(*velocity) += ( right * lNoise ) + ( up * vNoise );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flInterval - 
//			&m_LastMoveTarget - 
//			eMoveType - 
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::DoMovement( float flInterval, const Vector &MoveTarget, int eMoveType )
{
	// dvs: something is setting this bit, causing us to stop moving and get stuck that way
	Forget( bits_MEMORY_TURNING );

	Vector Steer, SteerAvoid, SteerRel;
	Vector forward, right, up;

	//Get our orientation vectors.
	GetVectors( &forward, &right, &up);

	if ( ( GetActivity() == ACT_MELEE_ATTACK1 ) && ( GetEnemy() != NULL ) )
	{
		SteerSeek( Steer, GetEnemy()->GetAbsOrigin() );
	}
	else
	{
		//If we are approaching our goal, use an arrival steering mechanism.
		if ( eMoveType == ICH_MOVETYPE_ARRIVE )
		{
			SteerArrive( Steer, MoveTarget );
		}
		else
		{
			//Otherwise use a seek steering mechanism.
			SteerSeek( Steer, MoveTarget );
		}
	}
	
#if FEELER_COLLISION

	Vector f, u, l, r, d;

	float	probeLength = GetAbsVelocity().Length();

	if ( probeLength < 150 )
		probeLength = 150;

	if ( probeLength > 500 )
		probeLength = 500;

	f = DoProbe( GetLocalOrigin() + (probeLength * forward) );
	r = DoProbe( GetLocalOrigin() + (probeLength/3 * (forward+right)) );
	l = DoProbe( GetLocalOrigin() + (probeLength/3 * (forward-right)) );
	u = DoProbe( GetLocalOrigin() + (probeLength/3 * (forward+up)) );
	d = DoProbe( GetLocalOrigin() + (probeLength/3 * (forward-up)) );

	SteerAvoid = f+r+l+u+d;
	
	//NDebugOverlay::Line( GetLocalOrigin(), GetLocalOrigin()+SteerAvoid, 255, 255, 0, false, 0.1f );	

	if ( SteerAvoid.LengthSqr() )
	{
		Steer = (SteerAvoid*0.5f);
	}

	m_vecVelocity = m_vecVelocity + (Steer*0.5f);

	VectorNormalize( m_vecVelocity );

	SteerRel.x = forward.Dot( m_vecVelocity );
	SteerRel.y = right.Dot( m_vecVelocity );
	SteerRel.z = up.Dot( m_vecVelocity );

	m_vecVelocity *= m_flGroundSpeed;

#else

	// OF2: how far it is to where we are going, for SteerAvoidObstacles
	if ( ( GetActivity() == ACT_MELEE_ATTACK1 ) && ( GetEnemy() != NULL ) )
	{
		m_flAvoidReach = ( GetEnemy()->GetAbsOrigin() - GetAbsOrigin() ).Length();
	}
	else
	{
		m_flAvoidReach = ( MoveTarget - GetAbsOrigin() ).Length();
	}

	//See if we need to avoid any obstacles.
	if ( SteerAvoidObstacles( SteerAvoid, GetAbsVelocity(), forward, right, up ) )
	{
		//Take the avoidance vector
		Steer = SteerAvoid;
	}

	SteerKeepDepth( Steer );	// OF2
	SteerKeepClear( Steer, forward, right, up );	// OF2

	//Clamp our ideal steering vector to within our physical limitations.
	ClampSteer( Steer, SteerRel, forward, right, up );

	ApplyAbsVelocityImpulse( Steer * flInterval );
	
#endif

	Vector vecNewVelocity = GetAbsVelocity();
	float flLength = vecNewVelocity.Length();

	//Clamp our final speed
	if ( flLength > m_flGroundSpeed )
	{
		vecNewVelocity *= ( m_flGroundSpeed / flLength );
		flLength = m_flGroundSpeed;
	}

	Vector	workVelocity = vecNewVelocity;

	AddSwimNoise( &workVelocity );

	// Pose the fish properly
	SetPoses( SteerRel, flLength );

	//Drag our victim before moving
	if ( m_pVictim != NULL )
	{
		DragVictim( flInterval );
	}

	//Move along the current velocity vector
	if ( WalkMove( workVelocity * flInterval, MASK_NPCSOLID ) == false )
	{
		//Attempt a half-step
		if ( WalkMove( (workVelocity*0.5f) * flInterval,  MASK_NPCSOLID) == false )
		{
			// OF2: slide along what is in the way instead of pressing into it. Pressed against the bottom or held
			// under the surface in shallow water, the speed it could not use was all downward and stood it on its nose.
			Vector	vecFlat( workVelocity.x, workVelocity.y, 0.0f );
			Vector	vecRise( 0.0f, 0.0f, workVelocity.z );

			if ( WalkMove( vecFlat * flInterval, MASK_NPCSOLID ) )
			{
				vecNewVelocity.z = 0.0f;
				m_bPitchBlocked = true;
			}
			else if ( WalkMove( vecRise * flInterval, MASK_NPCSOLID ) )
			{
				vecNewVelocity.x = 0.0f;
				vecNewVelocity.y = 0.0f;
			}
			else
			{
				//Restart the velocity
				//VectorNormalize( m_vecVelocity );
				vecNewVelocity *= 0.5f;
				m_bPitchBlocked = true;
			}
		}
		else
		{
			//Cut our velocity in half
			vecNewVelocity *= 0.5f;
		}
	}

	SetAbsVelocity( vecNewVelocity );

}

//-----------------------------------------------------------------------------
// Purpose: Gets a steering vector to arrive at a target location with a
//			relatively small velocity.
// Input  : Steer - Receives the ideal steering vector.
//			Target - Target position at which to arrive.
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::SteerArrive(Vector &Steer, const Vector &Target)
{
	Vector Offset = Target - GetLocalOrigin();
	float fTargetDistance = Offset.Length();

	float fIdealSpeed = m_flGroundSpeed * (fTargetDistance / ICH_WAYPOINT_DISTANCE);
	float fClippedSpeed = MIN( fIdealSpeed, m_flGroundSpeed );
	
	Vector DesiredVelocity( 0, 0, 0 );

	if ( fTargetDistance > ICH_WAYPOINT_DISTANCE )
	{
		DesiredVelocity = (fClippedSpeed / fTargetDistance) * Offset;
	}

	Steer = DesiredVelocity - GetAbsVelocity();
}


//-----------------------------------------------------------------------------
// Purpose: Gets a steering vector to move towards a target position as quickly
//			as possible.
// Input  : Steer - Receives the ideal steering vector.
//			Target - Target position to seek.
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::SteerSeek( Vector &Steer, const Vector &Target )
{
	Vector offset = Target - GetLocalOrigin();
	
	VectorNormalize( offset );
	
	Vector DesiredVelocity = m_flGroundSpeed * offset;
	
	Steer = DesiredVelocity - GetAbsVelocity();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &Steer - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::SteerAvoidObstacles(Vector &Steer, const Vector &Velocity, const Vector &Forward, const Vector &Right, const Vector &Up)
{
	trace_t	tr;

	bool	collided = false;
	Vector	dir = Velocity;
	float	speed = VectorNormalize( dir );

	//Look ahead one second and avoid whatever is in our way.
	// OF2: or of2_ichthyosaur_avoid_dist if that is further (at a slow swim a second is 150 units, and it turned
	// with its nose on the wall), but not past where it is going: a wall behind its goal is not in the way
	float	flProbe = MAX( speed, MIN( of2_ichthyosaur_avoid_dist.GetFloat(), m_flAvoidReach ) );

	if ( speed == 0.0f )
		return false;

	AI_TraceHull( GetAbsOrigin(), GetAbsOrigin() + (dir*flProbe), GetHullMins(), GetHullMaxs(), MASK_NPCSOLID, this, COLLISION_GROUP_NONE, &tr );

	Vector	forward;

	GetVectors( &forward, NULL, NULL );

	//If we're hitting our enemy, just continue on
	if ( ( GetEnemy() != NULL ) && ( tr.m_pEnt == GetEnemy() ) )
		return false;

	if ( tr.fraction < 1.0f )
	{
		CBaseEntity *pBlocker = tr.m_pEnt;
		
		if ( ( pBlocker != NULL ) && ( pBlocker->MyNPCPointer() != NULL ) )
		{
			DevMsg( 2, "Avoiding an NPC\n" );

			Vector HitOffset = tr.endpos - GetAbsOrigin();

			Vector SteerUp = CrossProduct( HitOffset, Velocity );
			Steer = CrossProduct(  SteerUp, Velocity  );
			VectorNormalize( Steer );

			/*Vector probeDir = tr.endpos - GetAbsOrigin();
			Vector normalToProbeAndWallNormal = probeDir.Cross( tr.plane.normal );
			
			Steer = normalToProbeAndWallNormal.Cross( probeDir );
			VectorNormalize( Steer );*/

			if ( tr.fraction > 0 )
			{
				Steer = (Steer * Velocity.Length()) / tr.fraction;
				//NDebugOverlay::Line( GetLocalOrigin(), GetLocalOrigin()+Steer, 255, 0, 0, false, 0.1f );
			}
			else
			{
				Steer = (Steer * 1000 * Velocity.Length());
				//NDebugOverlay::Line( GetLocalOrigin(), GetLocalOrigin()+Steer, 255, 0, 0, false, 0.1f );
			}
		}
		else
		{
			if ( ( pBlocker != NULL ) && ( pBlocker == GetEnemy() ) )
			{
				DevMsg( "Avoided collision\n" );
				return false;
			}

			DevMsg( 2, "Avoiding the world\n" );
			
			Vector	steeringVector = tr.plane.normal;

			if ( tr.fraction == 0.0f )
				return false;

			Steer = steeringVector * ( Velocity.Length() / tr.fraction );
			
			//NDebugOverlay::Line( GetLocalOrigin(), GetLocalOrigin()+Steer, 255, 0, 0, false, 0.1f );
		}

		//return true;
		collided = true;
	}

	return collided;
}

//-----------------------------------------------------------------------------
// Purpose: OF2: off the bottom and under the surface. These two were the end of SteerAvoidObstacles, where
//			they counted as a collision, and a collision's steering replaces the steering towards the target:
//			near the surface it only steered down and coasted on past whoever it was after (seen in the
//			user's log: closing to 250 units and then swimming by). They are added to the steering now.
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::SteerKeepDepth( Vector &Steer )
{
	trace_t	tr;

	//Try to remain 8 feet above the ground.
	AI_TraceLine( GetAbsOrigin(), GetAbsOrigin() + Vector(0, 0, -ICH_HEIGHT_PREFERENCE), MASK_NPCSOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );

	if ( tr.fraction < 1.0f )
	{
		Steer += Vector( 0, 0, m_vecAccelerationMax.z / MAX( tr.fraction, 0.1f ) );	// was a division by zero when touching
	}

	//Stay under the surface
	if ( m_bIgnoreSurface == false )
	{
		// It kept its origin ICH_DEPTH_PREFERENCE (8) under, which left its back out of the water; and at the
		// surface this divided by zero
		float flDepth = MAX( of2_ichthyosaur_depth.GetFloat(), 1.0f );
		float waterLevel = ( UTIL_WaterLevel( GetAbsOrigin(), GetAbsOrigin().z, GetAbsOrigin().z+flDepth ) - GetAbsOrigin().z ) / flDepth;

		if ( waterLevel < 1.0f )
		{
			Steer += -Vector( 0, 0, m_vecAccelerationMax.z / MAX( waterLevel, 0.1f ) );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: OF2: room to the sides and underneath. Its box is an 80 unit cube round its shoulders, and the body is
//			wider than that and three times as long, all of it behind: swimming along a wall, or turning away from
//			one, the tail and the fins went through it (the user: "clipping a lot"). Feelers out to
//			of2_ichthyosaur_clearance from the middle of the box and from the middle of the tail push it away
//			from what they touch, harder the nearer it is. Added to the steering, like SteerKeepDepth.
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::SteerKeepClear( Vector &Steer, const Vector &forward, const Vector &right, const Vector &up )
{
	float	flClear = of2_ichthyosaur_clearance.GetFloat();

	if ( flClear <= 0.0f )
		return;

	// Not when closing on its prey or holding it: someone against a wall could not be reached at all
	if ( ( m_pVictim != NULL ) || ( ( GetEnemy() != NULL ) && ( ( GetEnemy()->GetAbsOrigin() - GetAbsOrigin() ).LengthSqr() < Square( 250.0f ) ) ) )
		return;

	// The tail's middle: the model reaches about 210 units back from the origin at Valve's size
	const float flTailBack = 105.0f * ICH_MODEL_SCALE;

	Vector	vecFrom[2];
	vecFrom[0] = GetAbsOrigin();
	vecFrom[1] = GetAbsOrigin() - ( forward * flTailBack );

	// Flat sideways, so that a fish pitched up or rolled does not feel for the surface with its fins
	Vector	vecSide = right;
	vecSide.z = 0.0f;

	if ( VectorNormalize( vecSide ) == 0.0f )
		return;

	Vector	vecDirs[3];
	vecDirs[0] = vecSide;
	vecDirs[1] = -vecSide;
	vecDirs[2] = Vector( 0, 0, -1 );

	trace_t	tr;

	for ( int i = 0; i < 2; i++ )
	{
		// The tail's feelers start from the tail, which may itself be in a wall: only what can be seen from the
		// middle of the box counts as where the tail is
		if ( i == 1 )
		{
			AI_TraceLine( vecFrom[0], vecFrom[1], MASK_NPCSOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );

			if ( tr.fraction < 1.0f )
			{
				// The tail is in something: away from it
				Steer += tr.plane.normal * m_vecAccelerationMax.y;
				continue;
			}
		}

		for ( int j = 0; j < 3; j++ )
		{
			AI_TraceLine( vecFrom[i], vecFrom[i] + ( vecDirs[j] * flClear ), MASK_NPCSOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );

			if ( tr.fraction < 1.0f )
			{
				float	flPush = ( j == 2 ) ? m_vecAccelerationMax.z : m_vecAccelerationMax.y;

				Steer -= vecDirs[j] * ( flPush * ( 1.0f - tr.fraction ) * 2.0f );

				if ( of2_ichthyosaur_debug.GetBool() )
				{
					NDebugOverlay::Line( vecFrom[i], tr.endpos, 255, 128, 0, true, 0.15f );
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Clamps the desired steering vector based on the limitations of this
//			vehicle.
// Input  : SteerAbs - The vector indicating our ideal steering vector. Receives
//				the clamped steering vector in absolute (x,y,z) coordinates.
//			SteerRel - Receives the clamped steering vector in relative (forward, right, up)
//				coordinates.
//			forward - Our current forward vector.
//			right - Our current right vector.
//			up - Our current up vector.
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::ClampSteer(Vector &SteerAbs, Vector &SteerRel, Vector &forward, Vector &right, Vector &up)
{
	float fForwardSteer = DotProduct(SteerAbs, forward);
	float fRightSteer = DotProduct(SteerAbs, right);
	float fUpSteer = DotProduct(SteerAbs, up);

	if (fForwardSteer > 0)
	{
		fForwardSteer = MIN(fForwardSteer, m_vecAccelerationMax.x);
	}
	else
	{
		fForwardSteer = MAX(fForwardSteer, m_vecAccelerationMin.x);
	}

	if (fRightSteer > 0)
	{
		fRightSteer = MIN(fRightSteer, m_vecAccelerationMax.y);
	}
	else
	{
		fRightSteer = MAX(fRightSteer, m_vecAccelerationMin.y);
	}

	if (fUpSteer > 0)
	{
		fUpSteer = MIN(fUpSteer, m_vecAccelerationMax.z);
	}
	else
	{
		fUpSteer = MAX(fUpSteer, m_vecAccelerationMin.z);
	}

	SteerAbs = (fForwardSteer*forward) + (fRightSteer*right) + (fUpSteer*up);

	SteerRel.x = fForwardSteer;
	SteerRel.y = fRightSteer;
	SteerRel.z = fUpSteer;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pTargetEnt - 
//			vecDir - 
//			flDistance - 
//			flInterval - 
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::MoveFlyExecute( CBaseEntity *pTargetEnt, const Vector &vecDir, float flDistance, float flInterval )
{
	IchthyosaurMoveType_t eMoveType = ( GetNavigator()->CurWaypointIsGoal() ) ? ICH_MOVETYPE_ARRIVE : ICH_MOVETYPE_SEEK;

	m_flGroundSpeed = GetGroundSpeed();

	Vector	moveGoal = GetNavigator()->GetCurWaypointPos();

	//See if we can move directly to our goal
	if ( ( GetEnemy() != NULL ) && ( GetNavigator()->GetGoalTarget() == (CBaseEntity *) GetEnemy() ) )
	{
		trace_t	tr;
		Vector	goalPos = GetEnemy()->GetAbsOrigin() + ( GetEnemy()->GetSmoothedVelocity() * 0.5f );

		AI_TraceHull( GetAbsOrigin(), goalPos, GetHullMins(), GetHullMaxs(), MASK_NPCSOLID, GetEnemy(), COLLISION_GROUP_NONE, &tr );

		if ( tr.fraction == 1.0f )
		{
			moveGoal = tr.endpos;
		}
	}

	//Move
	DoMovement( flInterval, moveGoal, eMoveType );

	//Save the info from that run
	m_vecLastMoveTarget	= moveGoal;
	m_bHasMoveTarget	= true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::FVisible( CBaseEntity *pEntity, int traceMask, CBaseEntity **ppBlocker )
{
	// don't look through water
	// OF2: this asked for the same water level as ours, so a player swimming at the surface or wading (level 1
	// or 2) could not be seen from below (3). In the water at all is enough.
	if ( ( GetWaterLevel() > 0 ) != ( pEntity->GetWaterLevel() > 0 ) )
	{
		// OF2: except a player over the water, low enough to jump at
		// (or anyone it preys on beside the water, near enough to lunge at)
		if ( GetWaterLevel() == 0 )
			return false;

		bool	bLeap = of2_ichthyosaur_leap.GetBool() && IsOverWater( pEntity, NULL, NULL );
		bool	bLunge = ( bLeap == false ) && of2_ichthyosaur_lunge.GetBool() && IsOnShore( pEntity );

		if ( ( bLeap == false ) && ( bLunge == false ) )
			return false;
	}

	return BaseClass::FVisible( pEntity, traceMask, ppBlocker );
}

//-----------------------------------------------------------------------------
// OF2: THE LEAP (the user's idea, 2026-10-13): out of the water at a player hanging over it.
//
// It sees such a player (FVisible) while their feet are no more than of2_ichthyosaur_leap_try above the surface with
// nothing but air in between. SelectSchedule then picks SCHED_ICH_LEAP every of2_ichthyosaur_leap_delay seconds:
// swim to a place under them and as deep as there is room (DIVE), straight up at them gathering speed (RISE), and
// out (AIR), where only gravity moves it. How high it gets follows from the speed it leaves the water with, and that
// is set for the player's height but for no more than of2_ichthyosaur_leap_grab: higher than that it comes up
// short and its jaws shut on nothing. A player the mouth passes is caught as by a bite, and comes down with it.
//-----------------------------------------------------------------------------

// The mouth, ahead of the origin along the body, and how near to it the middle of the player has to pass
#define	ICH_LEAP_MOUTH		( 40.0f * ICH_MODEL_SCALE )
#define	ICH_LEAP_CATCH		48.0f
#define	ICH_LEAP_RUNUP		300.0f

//-----------------------------------------------------------------------------
// Purpose: Is this a player with only air between their feet and water, no higher than we would jump at?
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::IsOverWater( CBaseEntity *pEntity, float *pflHeight, float *pflSurfaceZ )
{
	if ( ( pEntity == NULL ) || ( pEntity->IsPlayer() == false ) || ( pEntity->IsAlive() == false ) || ( pEntity->GetWaterLevel() != 0 ) )
		return false;

	trace_t	tr;
	Vector	vecFrom = pEntity->GetAbsOrigin();

	UTIL_TraceLine( vecFrom, vecFrom - Vector( 0, 0, of2_ichthyosaur_leap_try.GetFloat() + 1.0f ), MASK_SOLID | MASK_WATER, pEntity, COLLISION_GROUP_NONE, &tr );

	if ( ( tr.fraction == 1.0f ) || tr.startsolid )
		return false;

	// Water, and not a floor or a boat
	if ( ( UTIL_PointContents( tr.endpos - Vector( 0, 0, 4 ) ) & MASK_WATER ) == 0 )
		return false;

	if ( pflHeight != NULL )
	{
		*pflHeight = vecFrom.z - tr.endpos.z;
	}

	if ( pflSurfaceZ != NULL )
	{
		*pflSurfaceZ = tr.endpos.z;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Begin a jump at our enemy, if there is water enough under them for the run-up
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::StartLeap( void )
{
	CBaseEntity	*pEnemy = GetEnemy();
	float		flHeight, flSurfaceZ;

	if ( ( m_pVictim != NULL ) || ( GetWaterLevel() != 3 ) || ( IsOverWater( pEnemy, &flHeight, &flSurfaceZ ) == false ) )
		return false;

	// Our box just under the surface below them, and how far down it can go from there
	float	flTop = GetHullMaxs().z + 8.0f;
	Vector	vecTop = pEnemy->WorldSpaceCenter();
	vecTop.z = flSurfaceZ - flTop;

	trace_t	tr;
	AI_TraceHull( vecTop, vecTop - Vector( 0, 0, ICH_LEAP_RUNUP ), GetHullMins(), GetHullMaxs(), MASK_NPCSOLID, this, COLLISION_GROUP_NONE, &tr );

	float	flDepth = flTop + ( ICH_LEAP_RUNUP * tr.fraction );

	if ( tr.startsolid || ( flDepth < of2_ichthyosaur_leap_depth.GetFloat() ) )
	{
		if ( of2_ichthyosaur_debug.GetBool() )
		{
			Msg( "ich %d: no leap, water under them is %.0f deep (needs %.0f)\n", entindex(), tr.startsolid ? 0.0f : flDepth, of2_ichthyosaur_leap_depth.GetFloat() );
		}

		return false;
	}

	GetNavigator()->ClearGoal();

	m_iLeapPhase		= ICH_LEAP_DIVE;
	m_flLeapPhaseTime	= gpGlobals->curtime;
	m_flLeapTaskTime	= gpGlobals->curtime;
	m_flLeapSurfaceZ	= flSurfaceZ;
	m_flLeapDepth		= MAX( flDepth - 16.0f, flTop );
	m_bLeapSnapped		= false;

	if ( of2_ichthyosaur_debug.GetBool() )
	{
		Msg( "ich %d: leap begins, player %.0f over the water, run-up from %.0f under\n", entindex(), flHeight, m_flLeapDepth );
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::EndLeap( void )
{
	if ( of2_ichthyosaur_debug.GetBool() )
	{
		Msg( "ich %d: leap over in phase %d, %s\n", entindex(), m_iLeapPhase, ( m_pVictim != NULL ) ? "holding the player" : "nobody caught" );
	}

	m_iLeapPhase		= ICH_LEAP_NONE;
	m_flNextLeapTime	= gpGlobals->curtime + of2_ichthyosaur_leap_delay.GetFloat();
	m_bHasMoveTarget	= false;
}

//-----------------------------------------------------------------------------
// Purpose: Where we leave or meet the water
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::LeapSplash( void )
{
	CEffectData	data;
	data.m_vOrigin = GetAbsOrigin();
	data.m_vOrigin.z = m_flLeapSurfaceZ;
	data.m_vNormal = Vector( 0, 0, 1 );
	data.m_flScale = random->RandomFloat( 22.0f, 28.0f );

	DispatchEffect( "watersplash", data );
	EmitSound( "Physics.WaterSplash" );
}

//-----------------------------------------------------------------------------
// Purpose: The movement of a jump. False: swim as usual this time (the way to the run-up, or the jump is off).
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::LeapMove( float flInterval )
{
	CBaseEntity	*pEnemy = GetEnemy();
	float		flPhaseTime = gpGlobals->curtime - m_flLeapPhaseTime;
	float		flGravity = GetCurrentGravity();

	// Until it is in the air the jump can be called off: the schedule was taken from us, or they are no
	// longer hanging there
	if ( m_iLeapPhase != ICH_LEAP_AIR )
	{
		if ( ( ( gpGlobals->curtime - m_flLeapTaskTime ) > 0.35f ) || ( IsOverWater( pEnemy, NULL, NULL ) == false ) )
		{
			EndLeap();
			return false;
		}
	}

	if ( m_iLeapPhase == ICH_LEAP_DIVE )
	{
		Vector	vecStart = pEnemy->WorldSpaceCenter();
		vecStart.z = m_flLeapSurfaceZ - m_flLeapDepth;

		Vector	vecOff = vecStart - GetAbsOrigin();
		float	flOff = vecOff.Length2D();

		bool	bThere = ( flOff < 80.0f ) && ( fabs( vecOff.z ) < 90.0f );

		if ( ( bThere == false ) && ( flPhaseTime > 4.0f ) )
		{
			// Could not get to the place: from here if it is near, else not this time
			if ( flOff > 200.0f )
			{
				EndLeap();
				return false;
			}

			bThere = true;
		}

		if ( bThere == false )
		{
			m_vecLastMoveTarget	= vecStart;
			m_bHasMoveTarget	= true;
			return false;
		}

		// Up. The speed to leave the water with: enough to bring the mouth to their middle, or to as high as we
		// can reach if they are higher than that.
		float	flMiddle = pEnemy->WorldSpaceCenter().z - m_flLeapSurfaceZ;
		float	flReach = of2_ichthyosaur_leap_grab.GetFloat() + ( pEnemy->WorldSpaceCenter().z - pEnemy->GetAbsOrigin().z );
		float	flApex = MAX( MIN( flMiddle - ICH_LEAP_MOUTH, flReach - ICH_LEAP_MOUTH - ICH_LEAP_CATCH ), 32.0f );

		m_flLeapExitSpeed	= sqrt( 2.0f * flGravity * flApex );
		m_iLeapPhase		= ICH_LEAP_RISE;
		m_flLeapPhaseTime	= gpGlobals->curtime;
		flPhaseTime			= 0.0f;

		EmitSound( "NPC_Ichthyosaur.AttackGrowl" );

		if ( of2_ichthyosaur_debug.GetBool() )
		{
			Msg( "ich %d: leap up, for %.0f over the water at %.0f a second (their middle is at %.0f)\n", entindex(), flApex, m_flLeapExitSpeed, flMiddle );
		}
	}

	Vector	vecVelocity = GetAbsVelocity();
	Vector	vecMouthWas;
	{
		Vector	vecAhead;
		GetVectors( &vecAhead, NULL, NULL );
		vecMouthWas = GetAbsOrigin() + ( vecAhead * ICH_LEAP_MOUTH );
	}

	if ( m_iLeapPhase == ICH_LEAP_RISE )
	{
		// At where they will be, and never flatter than a steep climb
		Vector	vecDir = ( pEnemy->WorldSpaceCenter() + ( pEnemy->GetSmoothedVelocity() * 0.3f ) ) - GetAbsOrigin();
		VectorNormalize( vecDir );

		if ( vecDir.z < 0.6f )
		{
			Vector	vecFlat( vecDir.x, vecDir.y, 0.0f );
			VectorNormalize( vecFlat );
			vecDir = ( vecFlat * 0.8f ) + Vector( 0, 0, 0.6f );
		}

		float	flWant = m_flLeapExitSpeed / vecDir.z;
		float	flSpeed = MAX( DotProduct( vecVelocity, vecDir ), 0.0f ) + ( of2_ichthyosaur_leap_accel.GetFloat() * flInterval );

		vecVelocity = vecDir * MIN( flSpeed, flWant );

		if ( ( WalkMove( vecVelocity * flInterval, MASK_NPCSOLID ) == false ) || ( flPhaseTime > 2.5f ) )
		{
			// Something in the way, or no way up
			SetAbsVelocity( vecVelocity * 0.25f );
			EndLeap();
			return true;
		}

		if ( GetAbsOrigin().z > m_flLeapSurfaceZ )
		{
			// Out. From here it only falls, so the speed across is set now: what brings it under them at the top.
			m_iLeapPhase		= ICH_LEAP_AIR;
			m_flLeapPhaseTime	= gpGlobals->curtime;

			float	flTop = MAX( vecVelocity.z / flGravity, 0.1f );
			Vector	vecAcross = ( pEnemy->WorldSpaceCenter() + ( pEnemy->GetSmoothedVelocity() * flTop ) ) - GetAbsOrigin();
			vecAcross.z = 0.0f;
			vecAcross /= flTop;

			float	flAcross = vecAcross.Length();

			if ( flAcross > 250.0f )
			{
				vecAcross *= ( 250.0f / flAcross );
			}

			vecVelocity.x = vecAcross.x;
			vecVelocity.y = vecAcross.y;

			LeapSplash();
			SetIdealActivity( ACT_MELEE_ATTACK1 );
		}
	}
	else if ( m_iLeapPhase == ICH_LEAP_AIR )
	{
		vecVelocity.z -= flGravity * flInterval;

		if ( WalkMove( vecVelocity * flInterval, MASK_NPCSOLID ) == false )
		{
			if ( vecVelocity.z > 0.0f )
			{
				// Head against something: no higher
				vecVelocity.z = 0.0f;
				vecVelocity.x *= 0.25f;
				vecVelocity.y *= 0.25f;
			}
			else if ( WalkMove( Vector( 0, 0, vecVelocity.z * flInterval ), MASK_NPCSOLID ) == false )
			{
				// Come down on something that is not water
				SetAbsVelocity( vec3_origin );
				EndLeap();
				return true;
			}
			else
			{
				vecVelocity.x = 0.0f;
				vecVelocity.y = 0.0f;
			}
		}

		if ( ( vecVelocity.z < 0.0f ) && ( GetAbsOrigin().z < ( m_flLeapSurfaceZ - 24.0f ) ) )
		{
			// Back in
			LeapSplash();
			SetAbsVelocity( vecVelocity * 0.4f );
			EndLeap();
			return true;
		}

		if ( flPhaseTime > 6.0f )
		{
			EndLeap();
			return true;
		}
	}

	SetAbsVelocity( vecVelocity );

	// Nose up on the way up, and over forwards once it falls. Not from the real speed across, which is next to
	// nothing straight under someone: it would stand on its tail and snap over in one think.
	if ( pEnemy != NULL )
	{
		Vector	vecTo = pEnemy->GetAbsOrigin() - GetAbsOrigin();

		if ( vecTo.Length2D() > 24.0f )
		{
			GetMotor()->SetIdealYawAndUpdate( UTIL_VecToYaw( vecTo ), AI_KEEP_YAW_SPEED );
		}
	}

	float	flPitch = clamp( -RAD2DEG( atan2( vecVelocity.z + 150.0f, 80.0f ) ), -85.0f, 85.0f );
	QAngle	angles = GetLocalAngles();

	angles.x = ( angles.x * 0.5f ) + ( flPitch * 0.5f );
	angles.z *= 0.5f;

	SetLocalAngles( angles );

	if ( m_pVictim != NULL )
	{
		DragVictim( flInterval );
	}
	else if ( ( m_iLeapPhase == ICH_LEAP_AIR ) && ( m_bLeapSnapped == false ) )
	{
		// Has the mouth passed their middle since the last think?
		Vector	vecAhead;
		GetVectors( &vecAhead, NULL, NULL );

		Vector	vecMouth = GetAbsOrigin() + ( vecAhead * ICH_LEAP_MOUTH );

		if ( of2_ichthyosaur_debug.GetBool() )
		{
			NDebugOverlay::Line( vecMouthWas, vecMouth, 255, 0, 255, true, 3.0f );
			NDebugOverlay::Cross3D( vecMouth, ICH_LEAP_CATCH, 255, 0, 255, true, 3.0f );
		}

		CBasePlayer	*pPlayer = ToBasePlayer( pEnemy );

		if ( ( pPlayer != NULL ) && pPlayer->IsAlive() && of2_ichthyosaur_grab.GetBool() &&
			 ( CalcDistanceToLineSegment( pPlayer->WorldSpaceCenter(), vecMouthWas, vecMouth ) < ICH_LEAP_CATCH ) )
		{
			m_bLeapSnapped = true;

			EnsnareVictim( pPlayer );

			CTakeDamageInfo info( this, this, sk_ichthyosaur_melee_dmg.GetInt(), DMG_CLUB );
			CalculateMeleeDamageForce( &info, vecVelocity, pPlayer->GetAbsOrigin() );
			pPlayer->TakeDamage( info );

			color32 red = {64, 0, 0, 255};
			UTIL_ScreenFade( pPlayer, red, 0.5, 0, FFADE_IN );

			SetActivity( (Activity) ACT_ICH_BITE_HIT );
		}
		else if ( vecVelocity.z <= 0.0f )
		{
			// Over the top with nobody in its mouth
			m_bLeapSnapped = true;
			SetActivity( (Activity) ACT_ICH_BITE_MISS );
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// OF2: THE LUNGE (the user's idea, 2026-10-13): up out of the water after someone standing beside it, on a beach, a
// ramp or a low walkway, and back in with them.
//
// The user's way of doing it: work the move out first and then put the fish along it by hand with an animation
// playing, instead of leaving it to the swimming. So: a curve from a place in the water where its box fits
// (m_vecLungeStart) to where its mouth is on the prey (m_vecLungeEnd), lifted in the middle to clear a bank or
// the edge of a walkway (m_vecLungeMid), checked for room before it commits. LungeMove swims it to the start
// (APPROACH), moves it up the curve at of2_ichthyosaur_lunge_speed with the bite animation started so that the jaws
// shut as it arrives (STRIKE, BITE), and takes it back down the same curve tail first (RETURN), which is known to
// be clear and to end in water.
//
// The player is caught as by a bite. An NPC is killed and its body carried off in the mouth (CatchPrey).
//-----------------------------------------------------------------------------

#define	ICH_LUNGE_CATCH		68.0f	// how near the mouth their middle has to be when the jaws shut
#define	ICH_LUNGE_SAMPLES	10
#define	ICH_LUNGE_GLIDE		350.0f	// from this far it is taken to the foot of the curve by hand

// Swallowing: how long the body takes to go down, and where it ends up, behind the origin, inside the body
#define	ICH_SWALLOW_TIME	0.5f
#define	ICH_SWALLOW_DIST	( -30.0f * ICH_MODEL_SCALE )

//-----------------------------------------------------------------------------
// Purpose: The kinds of thing it takes off the land: the player, and NPCs about the size of a person
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::IsLungePrey( CBaseEntity *pEntity )
{
	if ( ( pEntity == NULL ) || ( pEntity->IsAlive() == false ) || ( pEntity == m_pVictim ) )
		return false;

	if ( pEntity->IsPlayer() )
		return true;

	CAI_BaseNPC	*pNPC = pEntity->MyNPCPointer();

	if ( ( pNPC == NULL ) || ( pNPC->GetMoveType() != MOVETYPE_STEP ) || ( pNPC->GetFlags() & FL_FLY ) )
		return false;

	switch ( pNPC->GetHullType() )
	{
	case HULL_HUMAN:
	case HULL_WIDE_HUMAN:
	case HULL_MEDIUM:
	case HULL_WIDE_SHORT:
		return true;

	case HULL_LARGE:
		return of2_ichthyosaur_lunge_guard.GetBool() && FClassnameIs( pNPC, "npc_antlionguard" );
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: The height of the surface of the water we are in. Asked for several times a think.
//-----------------------------------------------------------------------------
float CNPC_Ichthyosaur::GetSurfaceZ( void )
{
	if ( ( m_flSurfaceTime < 0.0f ) || ( ( gpGlobals->curtime - m_flSurfaceTime ) > 0.5f ) || ( gpGlobals->curtime < m_flSurfaceTime ) )
	{
		m_flSurfaceZ = UTIL_WaterLevel( GetAbsOrigin(), GetAbsOrigin().z, GetAbsOrigin().z + 2048.0f );
		m_flSurfaceTime = gpGlobals->curtime;
	}

	return m_flSurfaceZ;
}

//-----------------------------------------------------------------------------
// Purpose: Is this prey of ours standing on something beside our water, low and near enough to lunge at?
//			The quick test, for what it can see; whether there is a way up to them is StartLunge's business.
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::IsOnShore( CBaseEntity *pEntity )
{
	if ( ( GetWaterLevel() != 3 ) && ( m_iLungePhase == ICH_LUNGE_NONE ) )
		return false;

	if ( ( IsLungePrey( pEntity ) == false ) || ( pEntity->GetWaterLevel() != 0 ) || ( pEntity->GetGroundEntity() == NULL ) )
		return false;

	float	flSurfaceZ = GetSurfaceZ();
	float	flHeight = pEntity->GetAbsOrigin().z - flSurfaceZ;

	if ( ( flHeight < -24.0f ) || ( flHeight > of2_ichthyosaur_lunge_height.GetFloat() ) )
		return false;

	Vector	vecDir = GetAbsOrigin() - pEntity->GetAbsOrigin();
	vecDir.z = 0.0f;

	float	flAway = VectorNormalize( vecDir );

	if ( flAway > 1200.0f )
		return false;

	// Water on the way from them to us, within reach
	float	flReach = MIN( of2_ichthyosaur_lunge_range.GetFloat(), flAway );

	for ( float flStep = 32.0f; flStep <= flReach; flStep += 32.0f )
	{
		Vector	vecPoint = pEntity->GetAbsOrigin() + ( vecDir * flStep );
		vecPoint.z = flSurfaceZ - 24.0f;

		if ( UTIL_PointContents( vecPoint ) & MASK_WATER )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: The nearest place to the target where our box fits under the surface: the foot of the curve
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::FindLungeStart( CBaseEntity *pTarget, Vector *pvecStart )
{
	float	flSurfaceZ = GetSurfaceZ();
	float	flBoxZ = flSurfaceZ - ( GetHullMaxs().z + 8.0f );
	float	flRange = of2_ichthyosaur_lunge_range.GetFloat();

	// Towards us first, then all round
	Vector	vecToUs = GetAbsOrigin() - pTarget->GetAbsOrigin();
	vecToUs.z = 0.0f;
	VectorNormalize( vecToUs );

	float	flBest = FLT_MAX;

	for ( int i = 0; i < 9; i++ )
	{
		Vector	vecDir = vecToUs;

		if ( i > 0 )
		{
			AngleVectors( QAngle( 0.0f, i * 45.0f, 0.0f ), &vecDir );
		}

		for ( float flStep = 48.0f; flStep <= flRange; flStep += 24.0f )
		{
			// The way straight towards us is taken if it is not much longer than the shortest: less swimming round
			if ( flStep >= ( ( i == 0 ) ? flBest : ( flBest - 96.0f ) ) )
				break;

			// Right under the surface if it fits there, else with its back out of the water: the nearer the
			// shore it comes up, the better (the user: it came out ten metres off and flew the rest)
			bool	bFits = false;
			Vector	vecPoint = pTarget->GetAbsOrigin() + ( vecDir * flStep );

			for ( int iRise = 0; ( iRise < 3 ) && ( bFits == false ); iRise++ )
			{
				vecPoint.z = flBoxZ + ( iRise * 16.0f );

				if ( ( UTIL_PointContents( vecPoint ) & MASK_WATER ) == 0 )
					continue;

				trace_t	tr;
				AI_TraceHull( vecPoint, vecPoint, GetHullMins(), GetHullMaxs(), MASK_NPCSOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );

				bFits = ( tr.startsolid == false ) && ( tr.fraction == 1.0f );
			}

			if ( bFits == false )
				continue;

			flBest = flStep;
			*pvecStart = vecPoint;
			break;
		}
	}

	return ( flBest != FLT_MAX );
}

//-----------------------------------------------------------------------------
// Purpose: A point on the curve (0: the start in the water, 1: at the prey), and which way it runs there
//-----------------------------------------------------------------------------
Vector CNPC_Ichthyosaur::LungePoint( float t, Vector *pvecTangent )
{
	t = clamp( t, 0.0f, 1.0f );

	float	s = 1.0f - t;

	if ( pvecTangent != NULL )
	{
		*pvecTangent = ( ( m_vecLungeMid - m_vecLungeStart ) * ( 2.0f * s ) ) + ( ( m_vecLungeEnd - m_vecLungeMid ) * ( 2.0f * t ) );
		VectorNormalize( *pvecTangent );
	}

	return ( m_vecLungeStart * ( s * s ) ) + ( m_vecLungeMid * ( 2.0f * s * t ) ) + ( m_vecLungeEnd * ( t * t ) );
}

//-----------------------------------------------------------------------------
// Purpose: Our box as the way out of the water is checked with: smaller all round by what it may clip
//			(of2_ichthyosaur_lunge_clip), since a way that fits the whole 80 unit cube is rare on a real shore
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::LungeBox( Vector *pvecMins, Vector *pvecMaxs )
{
	float	flClip = clamp( of2_ichthyosaur_lunge_clip.GetFloat(), 0.0f, 32.0f );

	*pvecMins = GetHullMins() + Vector( flClip, flClip, flClip );
	*pvecMaxs = GetHullMaxs() - Vector( flClip, flClip, flClip );
}

//-----------------------------------------------------------------------------
// Purpose: Lay the curve from m_vecLungeStart to the target as they stand now, and see that our box can go
//			along it. Lifted further in the middle until it can, a few times.
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::SetLungeCurve( CBaseEntity *pTarget )
{
	Vector	vecOut = pTarget->GetAbsOrigin() - m_vecLungeStart;
	vecOut.z = 0.0f;

	float	flFlat = VectorNormalize( vecOut );

	if ( flFlat < 1.0f )
		return false;

	// Our origin with the mouth on their middle, and our box clear of what they stand on
	Vector	vecEnd = pTarget->WorldSpaceCenter() - ( vecOut * ICH_LEAP_MOUTH );
	vecEnd.z = MAX( vecEnd.z, pTarget->GetAbsOrigin().z + GetHullMaxs().z + 4.0f );

	// The box the way is checked with: ours, less what it may clip
	Vector	vecBoxMins, vecBoxMaxs;
	LungeBox( &vecBoxMins, &vecBoxMaxs );

	CTraceFilterSkipTwoEntities	filter( this, pTarget, COLLISION_GROUP_NONE );
	trace_t	tr;

	for ( int iLift = 0; iLift < 5; iLift++ )
	{
		UTIL_TraceHull( vecEnd, vecEnd, vecBoxMins, vecBoxMaxs, MASK_NPCSOLID_BRUSHONLY, &filter, &tr );

		if ( ( tr.startsolid == false ) && ( tr.fraction == 1.0f ) )
			break;

		if ( iLift == 4 )
		{
			if ( of2_ichthyosaur_debug.GetBool() )
			{
				Msg( "ich %d: no room for its box at the prey\n", entindex() );
			}

			return false;
		}

		vecEnd.z += 10.0f;
	}

	m_vecLungeEnd = vecEnd;

	// The curve leaves its middle point for its end along the line between the two, so that point sets how it
	// arrives: at the end's height, and it comes in level, its body lying flat with the tail low. (The user's
	// rule: level or still just rising. Lifted above the end, as it was at first, it came down onto its prey with
	// its tail 45 degrees in the air.) The climb is all in the first part; if that does not clear the bank, the
	// point is moved back towards the start, which makes the climb steeper. Only when none of those is clear
	// (a rise between the water and the prey) is it lifted, and then it does come down on them.
	// NOT UNDER THE END'S HEIGHT: that was tried for "still just rising", and on a level bank the stretch before
	// the end is then in the ground, so no lunge was ever clear (the user's log: "no way up to them" every time).
	static const float	flBacks[6] = { 0.35f, 0.2f, 0.05f, 0.35f, 0.2f, 0.05f };
	static const float	flLifts[6] = { 0.0f, 0.0f, 0.0f, 40.0f, 70.0f, 100.0f };

	for ( int iTry = 0; iTry < 6; iTry++ )
	{
		float	flBack = flBacks[iTry];

		m_vecLungeMid = m_vecLungeStart + ( ( m_vecLungeEnd - m_vecLungeStart ) * flBack );
		m_vecLungeMid.z = m_vecLungeEnd.z + flLifts[iTry];

		bool	bClear = true;
		Vector	vecFrom = m_vecLungeStart;

		m_flLungeLength = 0.0f;

		for ( int i = 1; i <= ICH_LUNGE_SAMPLES; i++ )
		{
			Vector	vecTo = LungePoint( (float)i / ICH_LUNGE_SAMPLES );

			UTIL_TraceHull( vecFrom, vecTo, vecBoxMins, vecBoxMaxs, MASK_NPCSOLID_BRUSHONLY, &filter, &tr );

			if ( tr.startsolid || ( tr.fraction < 1.0f ) )
			{
				bClear = false;
				break;
			}

			m_flLungeLength += ( vecTo - vecFrom ).Length();
			vecFrom = vecTo;
		}

		if ( bClear )
			return true;

		if ( of2_ichthyosaur_debug.GetBool() )
		{
			NDebugOverlay::Line( vecFrom, tr.endpos, 255, 0, 0, true, 4.0f );
			NDebugOverlay::Box( tr.endpos, vecBoxMins, vecBoxMaxs, 255, 0, 0, 8, 4.0f );
		}
	}

	if ( of2_ichthyosaur_debug.GetBool() )
	{
		Msg( "ich %d: no curve is clear, from %.0f %.0f %.0f to %.0f %.0f %.0f (%.0f along the ground)\n", entindex(),
			m_vecLungeStart.x, m_vecLungeStart.y, m_vecLungeStart.z, m_vecLungeEnd.x, m_vecLungeEnd.y, m_vecLungeEnd.z, flFlat );
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::StartLunge( void )
{
	CBaseEntity	*pEnemy = GetEnemy();

	if ( ( m_pVictim != NULL ) || ( m_hPrey != NULL ) || ( m_iLeapPhase != ICH_LEAP_NONE ) || ( GetWaterLevel() != 3 ) || ( IsOnShore( pEnemy ) == false ) )
		return false;

	bool	bFoot = FindLungeStart( pEnemy, &m_vecLungeStart );

	if ( ( bFoot == false ) || ( SetLungeCurve( pEnemy ) == false ) )
	{
		if ( of2_ichthyosaur_debug.GetBool() )
		{
			Msg( "ich %d: no lunge at %s, %s\n", entindex(), pEnemy->GetClassname(),
				bFoot ? "no way up to them" : "no water it fits in within reach of them" );
		}

		return false;
	}

	GetNavigator()->ClearGoal();

	m_iLungePhase		= ICH_LUNGE_APPROACH;
	m_flLungePhaseTime	= gpGlobals->curtime;
	m_flLungeTaskTime	= gpGlobals->curtime;
	m_flLungeAlong		= 0.0f;
	m_hLungeTarget		= pEnemy;
	m_flLeapSurfaceZ	= GetSurfaceZ();	// for LeapSplash

	if ( of2_ichthyosaur_debug.GetBool() )
	{
		Msg( "ich %d: lunge at %s begins, %.0f units of curve\n", entindex(), pEnemy->GetClassname(), m_flLungeLength );
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::EndLunge( void )
{
	if ( of2_ichthyosaur_debug.GetBool() )
	{
		Msg( "ich %d: lunge over in phase %d, %s\n", entindex(), m_iLungePhase,
			( m_pVictim != NULL ) ? "holding the player" : ( ( m_hPrey != NULL ) ? "with a body" : "nobody caught" ) );
	}

	m_iLungePhase		= ICH_LUNGE_NONE;
	m_flNextLungeTime	= gpGlobals->curtime + of2_ichthyosaur_lunge_delay.GetFloat();
	m_hLungeTarget		= NULL;
	m_bHasMoveTarget	= false;
}

//-----------------------------------------------------------------------------
// Purpose: The movement of a lunge. False: swim as usual this time (the way to its start, or the lunge is off).
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::LungeMove( float flInterval )
{
	CBaseEntity	*pTarget = m_hLungeTarget;
	float		flPhaseTime = gpGlobals->curtime - m_flLungePhaseTime;

	if ( m_iLungePhase == ICH_LUNGE_APPROACH )
	{
		// Can still be called off: the schedule was taken from us, or they have left the water's edge
		if ( ( ( gpGlobals->curtime - m_flLungeTaskTime ) > 0.35f ) || ( pTarget == NULL ) || ( IsOnShore( pTarget ) == false ) || ( flPhaseTime > 6.0f ) )
		{
			EndLunge();
			return false;
		}

		// To the foot of the curve, which is as near the shore as its box fits. The swimming takes it most of the
		// way; the last stretch is done by hand, in a straight line under the water, because the steering keeps
		// it off the bottom and out of the shallows and it never got there. (It then struck from wherever it had
		// got to, which the user saw as coming out of the water ten metres off and flying the rest.)
		Vector	vecToFoot = m_vecLungeStart - GetAbsOrigin();
		float	flToFoot = VectorNormalize( vecToFoot );

		if ( flToFoot > 16.0f )
		{
			if ( flToFoot > ICH_LUNGE_GLIDE )
			{
				m_vecLastMoveTarget	= m_vecLungeStart;
				m_bHasMoveTarget	= true;
				return false;
			}

			float	flStep = MIN( flToFoot, ICH_SWIM_SPEED_RUN * flInterval );
			Vector	vecTo = GetAbsOrigin() + ( vecToFoot * flStep );

			trace_t	tr;
			AI_TraceHull( GetAbsOrigin(), vecTo, GetHullMins(), GetHullMaxs(), MASK_NPCSOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );

			if ( tr.startsolid || ( tr.fraction < 1.0f ) )
			{
				// No straight way to it from here
				EndLunge();
				return false;
			}

			SetAbsOrigin( vecTo );
			SetAbsVelocity( vecToFoot * ICH_SWIM_SPEED_RUN );
			PhysicsTouchTriggers();

			QAngle	angles = GetLocalAngles();
			float	flPitch = clamp( -RAD2DEG( asin( clamp( vecToFoot.z, -1.0f, 1.0f ) ) ), -50.0f, 50.0f );

			angles.x = ( angles.x * 0.5f ) + ( flPitch * 0.5f );
			angles.y = ApproachAngle( UTIL_VecToYaw( vecToFoot ), angles.y, 300.0f * flInterval );
			angles.z *= 0.5f;

			SetLocalAngles( angles );
			GetMotor()->SetIdealYaw( angles.y );

			return true;
		}

		// There. The curve from here to where they stand now.
		m_vecLungeStart = GetAbsOrigin();

		if ( SetLungeCurve( pTarget ) == false )
		{
			EndLunge();
			return false;
		}

		m_iLungePhase		= ICH_LUNGE_STRIKE;
		m_flLungePhaseTime	= gpGlobals->curtime;
		m_flLungeAlong		= 0.0f;

		// The bite, begun part of the way through if the way up is short, so that the jaws shut on arriving.
		// They shut at frame 28 of 29.
		SetIdealActivity( ACT_MELEE_ATTACK1 );
		SetActivity( ACT_MELEE_ATTACK1 );
		m_flPlaybackRate = 1.0f;

		float	flSequence = SequenceDuration();
		float	flShut = flSequence * ( 28.0f / 29.0f );
		float	flWay = m_flLungeLength / MAX( of2_ichthyosaur_lunge_speed.GetFloat(), 100.0f );

		if ( ( flSequence > 0.0f ) && ( flWay < flShut ) )
		{
			SetCycle( ( flShut - flWay ) / flSequence );
		}

		EmitSound( "NPC_Ichthyosaur.AttackGrowl" );

		if ( of2_ichthyosaur_debug.GetBool() )
		{
			Msg( "ich %d: lunge up, %.0f units in %.2f s\n", entindex(), m_flLungeLength, flWay );

			Vector	vecFrom = m_vecLungeStart;

			for ( int i = 1; i <= ICH_LUNGE_SAMPLES; i++ )
			{
				Vector	vecTo = LungePoint( (float)i / ICH_LUNGE_SAMPLES );
				NDebugOverlay::Line( vecFrom, vecTo, 0, 255, 255, true, 6.0f );
				vecFrom = vecTo;
			}

			NDebugOverlay::Box( m_vecLungeEnd, GetHullMins(), GetHullMaxs(), 0, 255, 255, 16, 6.0f );
		}
	}

	Vector	vecWas = GetAbsOrigin();
	bool	bWasWet = ( vecWas.z < m_flLeapSurfaceZ );

	if ( m_iLungePhase == ICH_LUNGE_STRIKE )
	{
		m_flLungeAlong += of2_ichthyosaur_lunge_speed.GetFloat() * flInterval;

		Vector	vecTo = LungePoint( m_flLungeAlong / m_flLungeLength );

		// Something has come into the way since it was looked at: no further
		CTraceFilterSkipTwoEntities	filter( this, pTarget, COLLISION_GROUP_NONE );
		Vector	vecBoxMins, vecBoxMaxs;
		LungeBox( &vecBoxMins, &vecBoxMaxs );

		trace_t	tr;
		UTIL_TraceHull( vecWas, vecTo, vecBoxMins, vecBoxMaxs, MASK_NPCSOLID_BRUSHONLY, &filter, &tr );

		if ( tr.startsolid || ( tr.fraction < 1.0f ) )
		{
			m_flLungeAlong		= MAX( m_flLungeAlong - ( of2_ichthyosaur_lunge_speed.GetFloat() * flInterval ), 0.0f );
			m_iLungePhase		= ICH_LUNGE_RETURN;
			m_flLungePhaseTime	= gpGlobals->curtime;

			SetIdealActivity( (Activity) ACT_ICH_BITE_MISS );
			SetActivity( (Activity) ACT_ICH_BITE_MISS );
		}
		else
		{
			SetAbsOrigin( vecTo );

			if ( m_flLungeAlong >= m_flLungeLength )
			{
				// Arrived, jaws shut. On whom?
				m_flLungeAlong		= m_flLungeLength;
				m_iLungePhase		= ICH_LUNGE_BITE;
				m_flLungePhaseTime	= gpGlobals->curtime;

				Vector	vecAhead;
				GetVectors( &vecAhead, NULL, NULL );

				Vector	vecMouth = GetAbsOrigin() + ( vecAhead * ICH_LEAP_MOUTH );
				bool	bCaught = false;

				if ( ( pTarget != NULL ) && pTarget->IsAlive() && ( ( pTarget->WorldSpaceCenter() - vecMouth ).Length() < ICH_LUNGE_CATCH ) )
				{
					if ( pTarget->IsPlayer() )
					{
						if ( of2_ichthyosaur_grab.GetBool() )
						{
							EnsnareVictim( pTarget );
						}

						CTakeDamageInfo info( this, this, sk_ichthyosaur_melee_dmg.GetInt(), DMG_CLUB );
						CalculateMeleeDamageForce( &info, vecAhead, pTarget->GetAbsOrigin() );
						pTarget->TakeDamage( info );

						color32 red = {64, 0, 0, 255};
						UTIL_ScreenFade( pTarget, red, 0.5, 0, FFADE_IN );

						bCaught = true;
					}
					else if ( pTarget->MyNPCPointer() != NULL )
					{
						CatchPrey( pTarget->MyNPCPointer() );
						bCaught = true;
					}
				}

				Activity	eBite = (Activity)( bCaught ? ACT_ICH_BITE_HIT : ACT_ICH_BITE_MISS );
				SetIdealActivity( eBite );
				SetActivity( eBite );

				if ( of2_ichthyosaur_debug.GetBool() )
				{
					Msg( "ich %d: lunge arrived, %s\n", entindex(), bCaught ? "caught them" : "missed" );
					NDebugOverlay::Cross3D( vecMouth, ICH_LUNGE_CATCH, 255, 0, 255, true, 6.0f );
				}
			}
		}
	}
	else if ( m_iLungePhase == ICH_LUNGE_BITE )
	{
		// A moment up there with its jaws shut
		if ( flPhaseTime > 0.25f )
		{
			m_iLungePhase		= ICH_LUNGE_RETURN;
			m_flLungePhaseTime	= gpGlobals->curtime;
		}
	}
	else if ( m_iLungePhase == ICH_LUNGE_RETURN )
	{
		// Back the way it came, which was clear and ends in the water. Nothing is asked on the way: it has to
		// get home.
		m_flLungeAlong -= of2_ichthyosaur_lunge_back.GetFloat() * flInterval;

		SetAbsOrigin( LungePoint( MAX( m_flLungeAlong, 0.0f ) / m_flLungeLength ) );

		if ( m_flLungeAlong <= 0.0f )
		{
			Vector	vecBack;
			LungePoint( 0.0f, &vecBack );

			SetAbsVelocity( vecBack * ( -0.5f * of2_ichthyosaur_lunge_back.GetFloat() ) );
			PhysicsTouchTriggers();
			EndLunge();
			return true;
		}
	}

	PhysicsTouchTriggers();

	Vector	vecMoved = GetAbsOrigin() - vecWas;
	SetAbsVelocity( ( flInterval > 0.0f ) ? ( vecMoved / flInterval ) : vec3_origin );

	if ( bWasWet != ( GetAbsOrigin().z < m_flLeapSurfaceZ ) )
	{
		LeapSplash();
	}

	// Along the curve, nose first both ways. The curve is made to arrive level (SetLungeCurve).
	Vector	vecTangent;
	LungePoint( m_flLungeAlong / m_flLungeLength, &vecTangent );

	QAngle	angles = GetLocalAngles();
	float	flPitch = clamp( -RAD2DEG( asin( clamp( vecTangent.z, -1.0f, 1.0f ) ) ), -50.0f, 5.0f );

	angles.x = ( angles.x * 0.5f ) + ( flPitch * 0.5f );
	angles.y = UTIL_VecToYaw( vecTangent );
	angles.z *= 0.5f;

	SetLocalAngles( angles );
	GetMotor()->SetIdealYaw( angles.y );

	if ( m_pVictim != NULL )
	{
		DragVictim( flInterval );
	}

	UpdatePrey();

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: An NPC in our jaws: dead, and its body ours to carry for a while
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::CatchPrey( CAI_BaseNPC *pNPC )
{
	Vector	vecAt = pNPC->WorldSpaceCenter();

	CTakeDamageInfo info( this, this, pNPC->GetHealth() + 1000.0f, DMG_SLASH | DMG_REMOVENORAGDOLL );
	info.SetDamagePosition( vecAt );
	info.SetDamageForce( vec3_origin );

	m_iPreyBlood = pNPC->BloodColor();
	PreyBlood( vecAt, true );

	// Half way down its jaws (the user's measure), an antlion guard at their tip: it does not fit
	bool	bGuard = FClassnameIs( pNPC, "npc_antlionguard" );
	m_flPreyDist = ICH_LEAP_MOUTH * ( bGuard ? 1.0f : of2_ichthyosaur_prey_dist.GetFloat() );

	CBaseEntity	*pRagdoll = CreateServerRagdoll( pNPC, 0, info, COLLISION_GROUP_INTERACTIVE_DEBRIS, true );

	pNPC->TakeDamage( info );
	OF2_BodyRagdoll( pNPC, pRagdoll );

	if ( pNPC->IsAlive() )
	{
		// (something made it shrug that off; it does not get to walk about beside its own body)
		UTIL_Remove( pNPC );
	}

	ReleasePrey();

	if ( ( pRagdoll == NULL ) || ( physenv == NULL ) )
		return;

	// Held by the part of it nearest the mouth
	Vector	vecAhead;
	GetVectors( &vecAhead, NULL, NULL );

	Vector	vecMouth = GetAbsOrigin() + ( vecAhead * m_flPreyDist );

	IPhysicsObject	*pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int	nCount = pRagdoll->VPhysicsGetObjectList( pList, ARRAYSIZE( pList ) );

	if ( nCount <= 0 )
		return;

	IPhysicsObject	*pNearest = pList[0];
	float	flNearest = FLT_MAX;

	for ( int i = 0; i < nCount; i++ )
	{
		Vector	vecPart;
		pList[i]->GetPosition( &vecPart, NULL );

		float	flDist = vecPart.DistToSqr( vecMouth );

		if ( flDist < flNearest )
		{
			flNearest = flDist;
			pNearest = pList[i];
		}
	}

	m_pPreyController = physenv->CreateMotionController( this );
	m_pPreyController->AttachObject( pNearest, true );
	m_pPreyController->SetPriority( IPhysicsMotionController::HIGH_PRIORITY );

	m_hPrey				= pRagdoll;
	m_flPreyTime		= gpGlobals->curtime + of2_ichthyosaur_prey_time.GetFloat();
	m_vecPreyMouthWas	= vecMouth;
	m_vecPreyMouth		= vecMouth;
	m_flPreyMouthTime	= gpGlobals->curtime;
	m_flPreyBleedTime	= gpGlobals->curtime + 4.0f;
	m_flPreyDistHeld	= m_flPreyDist;

	// Swallowed after a while (the user asked), if it is the size of a person
	bool	bSwallow = ( of2_ichthyosaur_swallow.GetFloat() > 0.0f ) && ( bGuard == false );
	m_flSwallowTime		= bSwallow ? ( gpGlobals->curtime + of2_ichthyosaur_swallow.GetFloat() ) : 0.0f;

	// Down with it, as with the player: SelectSchedule dives while this runs, and no bite catches anyone
	m_flHoldTime	= m_flPreyTime;
}

//-----------------------------------------------------------------------------
// Purpose: The blood of what it has in its mouth (the user: "brutal"). bBurst: the bite itself, sprays every way
//			and marks on whatever is round about; else what runs from the body afterwards, with a trail under it.
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::PreyBlood( const Vector &vecAt, bool bBurst )
{
	int	nMuch = of2_ichthyosaur_blood.GetInt();

	if ( ( nMuch <= 0 ) || ( m_iPreyBlood == DONT_BLEED ) )
		return;

	trace_t	tr;

	if ( bBurst )
	{
		for ( int i = 0; i < ( 6 * nMuch ); i++ )
		{
			Vector	vecDir = RandomVector( -1.0f, 1.0f );
			vecDir.z = fabs( vecDir.z ) + 0.3f;
			VectorNormalize( vecDir );

			UTIL_BloodSpray( vecAt + ( vecDir * 8.0f ), vecDir, m_iPreyBlood, random->RandomInt( 10, 16 ), FX_BLOODSPRAY_ALL );
			UTIL_BloodImpact( vecAt, vecDir, m_iPreyBlood, random->RandomInt( 8, 14 ) );
		}

		// On the ground and the walls about
		for ( int i = 0; i < ( 14 * nMuch ); i++ )
		{
			Vector	vecDir = RandomVector( -1.0f, 1.0f );
			vecDir.z = -fabs( vecDir.z ) - 0.2f;
			VectorNormalize( vecDir );

			UTIL_TraceLine( vecAt, vecAt + ( vecDir * 220.0f ), MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );

			if ( tr.fraction < 1.0f )
			{
				UTIL_BloodDecalTrace( &tr, m_iPreyBlood );
			}
		}

		return;
	}

	for ( int i = 0; i < nMuch; i++ )
	{
		Vector	vecDir = RandomVector( -1.0f, 1.0f );
		VectorNormalize( vecDir );

		UTIL_BloodSpray( vecAt, vecDir, m_iPreyBlood, random->RandomInt( 5, 9 ), FX_BLOODSPRAY_DROPS | FX_BLOODSPRAY_GORE );
		UTIL_BloodDrips( vecAt, vecDir, m_iPreyBlood, random->RandomInt( 4, 8 ) );
	}

	// The trail
	Vector	vecDown( random->RandomFloat( -0.25f, 0.25f ), random->RandomFloat( -0.25f, 0.25f ), -1.0f );
	UTIL_TraceLine( vecAt, vecAt + ( vecDown * 160.0f ), MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );

	if ( tr.fraction < 1.0f )
	{
		UTIL_BloodDecalTrace( &tr, m_iPreyBlood );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Let the body go
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::ReleasePrey( void )
{
	if ( m_pPreyController != NULL )
	{
		if ( physenv != NULL )
		{
			physenv->DestroyMotionController( m_pPreyController );
		}

		m_pPreyController = NULL;
	}

	m_hPrey = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: IMotionEvent, called inside the physics step for the part of the body that is in our mouth: it is
//			sent to the mouth, hard, and the rest of the ragdoll hangs from it by its own joints. (The user asked
//			for the body attached to the mouth: a push ten times a second, the first version, let it trail.)
//			We move ten times a second and the client draws us sliding from each place to the next, so the mouth
//			it is held to slides the same way between the last two places it was.
//-----------------------------------------------------------------------------
IMotionEvent::simresult_e CNPC_Ichthyosaur::Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular )
{
	linear.Init();
	angular.Init();

	if ( ( m_hPrey == NULL ) || ( deltaTime <= 0.0f ) )
		return SIM_NOTHING;

	float	flSlide = clamp( ( gpGlobals->curtime - m_flPreyMouthTime ) / 0.1f, 0.0f, 1.0f );
	Vector	vecMouth = m_vecPreyMouthWas + ( ( m_vecPreyMouth - m_vecPreyMouthWas ) * flSlide );

	Vector	vecAt, vecVelocity;
	pObject->GetPosition( &vecAt, NULL );
	pObject->GetVelocity( &vecVelocity, NULL );

	// The speed that has it there in two steps, and the push that gives it that speed in this one
	Vector	vecWant = ( vecMouth - vecAt ) / ( deltaTime * 2.0f );
	float	flWant = vecWant.Length();

	if ( flWant > 3000.0f )
	{
		vecWant *= ( 3000.0f / flWant );
	}

	linear = ( vecWant - vecVelocity ) / deltaTime;

	return SIM_GLOBAL_ACCELERATION;
}

//-----------------------------------------------------------------------------
// Purpose: Once a think: where the mouth is now, for Simulate, and whether it is time to let the body go
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::UpdatePrey( void )
{
	CBaseEntity	*pPrey = m_hPrey;

	// (no controller with a body: a saved game, which does not keep one. The body is let go.)
	if ( ( pPrey == NULL ) || ( m_pPreyController == NULL ) || ( m_flPreyTime < gpGlobals->curtime ) || ( IsAlive() == false ) )
	{
		ReleasePrey();
		return;
	}

	Vector	vecAhead;
	GetVectors( &vecAhead, NULL, NULL );

	// Down the throat: drawn back from the jaws into the head over half a second, and gone
	if ( ( m_flSwallowTime > 0.0f ) && ( gpGlobals->curtime >= m_flSwallowTime ) )
	{
		float	flDown = ( gpGlobals->curtime - m_flSwallowTime ) / ICH_SWALLOW_TIME;

		if ( m_flPreyDist == m_flPreyDistHeld )
		{
			// The first of it: the jaws work
			int	iGulp = LookupSequence( "attackend" );

			if ( iGulp >= 0 )
			{
				int	iLayer = AddLayeredSequence( iGulp, 0 );
				SetLayerAutokill( iLayer, true );
				SetLayerBlendIn( iLayer, 0.1f );
				SetLayerBlendOut( iLayer, 0.2f );
			}

			EmitSound( "NPC_Ichthyosaur.AttackGrowl" );
		}

		if ( flDown >= 1.0f )
		{
			Vector	vecThroat = GetAbsOrigin() + ( vecAhead * m_flPreyDist );

			PreyBlood( vecThroat, false );
			PreyBlood( vecThroat, false );
			UTIL_Bubbles( vecThroat - Vector( 24, 24, 24 ), vecThroat + Vector( 24, 24, 24 ), 24 );

			if ( of2_ichthyosaur_debug.GetBool() )
			{
				Msg( "ich %d: swallowed it\n", entindex() );
			}

			ReleasePrey();
			UTIL_Remove( pPrey );

			// On with the hunt
			m_flHoldTime = gpGlobals->curtime + 0.5f;
			return;
		}

		m_flPreyDist = m_flPreyDistHeld + ( ( ICH_SWALLOW_DIST - m_flPreyDistHeld ) * flDown ) - 0.01f;
	}

	// Twice in one think (the lunge and PrescheduleThink both ask): only the first moves the mouth on
	if ( gpGlobals->curtime > m_flPreyMouthTime )
	{
		m_vecPreyMouthWas	= m_vecPreyMouth;
		m_flPreyMouthTime	= gpGlobals->curtime;

		// It runs for a while, on the way back to the water and down into it
		if ( m_flPreyBleedTime > gpGlobals->curtime )
		{
			PreyBlood( m_vecPreyMouth, false );
		}
	}

	m_vecPreyMouth = GetAbsOrigin() + ( vecAhead * m_flPreyDist );

	m_pPreyController->WakeObjects();
}

//-----------------------------------------------------------------------------
// Purpose: Get our conditions for a melee attack
// Input  : flDot - 
//			flDist - 
// Output : int
//-----------------------------------------------------------------------------
int CNPC_Ichthyosaur::MeleeAttack1Conditions( float flDot, float flDist )
{
	// OF2: led by half a second, as the chase is; it was a full second, which put a swimmer circling it out of
	// the cone it bites in nearly all the time
	Vector	predictedDir	= ( (GetEnemy()->GetAbsOrigin()+(GetEnemy()->GetSmoothedVelocity()*0.5f)) - GetAbsOrigin() );
	float	flPredictedDist = VectorNormalize( predictedDir );
	
	Vector	vBodyDir;
	GetVectors( &vBodyDir, NULL, NULL );

	float	flPredictedDot	= DotProduct( predictedDir, vBodyDir );

	if ( flPredictedDot < 0.8f )
		return COND_NOT_FACING_ATTACK;

	if ( ( flPredictedDist > ( GetAbsVelocity().Length() * 0.5f) ) && ( flDist > ( 128.0f * ICH_MODEL_SCALE ) ) )	// OF2: 128 at Valve's size
		return COND_TOO_FAR_TO_ATTACK;

	return COND_CAN_MELEE_ATTACK1;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEvent - 
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::HandleAnimEvent( animevent_t *pEvent )
{
	switch ( pEvent->event )
	{
	case ICH_AE_BITE:
		Bite();
		break;

	case ICH_AE_BITE_START:
		{
			EmitSound( "NPC_Ichthyosaur.AttackGrowl" );
		}
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::Bite( void )
{
	//Don't allow another bite too soon
	if ( m_flNextBiteTime > gpGlobals->curtime )
		return;

	// OF2: on a jump the bite animation only plays; LeapMove decides whether the jaws reach anyone
	if ( ( ( m_iLeapPhase != ICH_LEAP_NONE ) || ( m_iLungePhase != ICH_LUNGE_NONE ) ) && ( m_pVictim == NULL ) )
		return;

	CBaseEntity *pHurt;
	
	//FIXME: E3 HACK - Always damage bullseyes if we're scripted to hit them
	if ( ( GetEnemy() != NULL ) && ( GetEnemy()->Classify() == CLASS_BULLSEYE ) )
	{
		pHurt = GetEnemy();
	}
	else if ( m_pVictim != NULL )
	{
		// OF2: what we hold is not solid, the trace would miss it
		pHurt = m_pVictim;
	}
	else
	{
		// OF2: 108 and 32 at Valve's size
		pHurt = CheckTraceHullAttack( 108 * ICH_MODEL_SCALE, Vector(-32,-32,-32) * ICH_MODEL_SCALE, Vector(32,32,32) * ICH_MODEL_SCALE, 0, DMG_CLUB );
	}

	//Hit something
	if ( pHurt != NULL )
	{
		CTakeDamageInfo info( this, this, sk_ichthyosaur_melee_dmg.GetInt(), DMG_CLUB );

		if ( pHurt->IsPlayer() )
		{
			CBasePlayer *pPlayer = ToBasePlayer( pHurt );

			if ( pPlayer )
			{
				// OF2: the grab is on again (it was commented out). By chance, the user's choice: Valve's rule was
				// "under half health", which most players would never have got to see
				if ( pHurt == m_pVictim )
				{
					// Held: plain bites
				}
				else if ( ( ( m_flHoldTime < gpGlobals->curtime ) && ( random->RandomFloat( 0.0f, 1.0f ) < of2_ichthyosaur_grab_chance.GetFloat() ) ) || ( pPlayer->GetWaterLevel() < 1 ) )
				{
					if ( of2_ichthyosaur_grab.GetBool() )
					{
						EnsnareVictim( pHurt );

						if ( of2_ichthyosaur_debug.GetBool() )
						{
							Msg( "ich %d: grab %s (health %d of %d, hold %.1f s)\n", entindex(), ( m_pVictim != NULL ) ? "TAKEN" : "refused",
								pPlayer->GetHealth(), pPlayer->GetMaxHealth(), m_flHoldTime - gpGlobals->curtime );
						}
					}
				}
				else if ( of2_ichthyosaur_debug.GetBool() )
				{
					Msg( "ich %d: bite, no grab (health %d of %d, chance %.2f; %.1f s since last hold)\n", entindex(),
						pPlayer->GetHealth(), pPlayer->GetMaxHealth(), of2_ichthyosaur_grab_chance.GetFloat(),
						gpGlobals->curtime - m_flHoldTime );
					info.SetDamage( sk_ichthyosaur_melee_dmg.GetInt() * 3 );
				}
				else
				{
					info.SetDamage( sk_ichthyosaur_melee_dmg.GetInt() * 3 );
				}
				CalculateMeleeDamageForce( &info, GetAbsVelocity(), pHurt->GetAbsOrigin() );
				pHurt->TakeDamage( info );

				color32 red = {64, 0, 0, 255};
				UTIL_ScreenFade( pPlayer, red, 0.5, 0, FFADE_IN  );

				//Disorient the player
				QAngle angles = pPlayer->GetLocalAngles();

				angles.x += random->RandomInt( 60, 25 );
				angles.y += random->RandomInt( 60, 25 );
				angles.z = 0.0f;

				pPlayer->SetLocalAngles( angles );

				pPlayer->SnapEyeAngles( angles );
			}
		}
		else
		{
			CalculateMeleeDamageForce( &info, GetAbsVelocity(), pHurt->GetAbsOrigin() );
			pHurt->TakeDamage( info );
		}

		m_flNextBiteTime = gpGlobals->curtime + random->RandomFloat( 2.0f, 4.0f );

		//Bubbles!
		UTIL_Bubbles( pHurt->GetAbsOrigin()+Vector(-32.0f,-32.0f,-32.0f), pHurt->GetAbsOrigin()+Vector(32.0f,32.0f,0.0f), random->RandomInt( 16, 32 ) );
		
		// Play a random attack hit sound
		EmitSound( "NPC_Ichthyosaur.Bite" );

		if ( GetActivity() == ACT_MELEE_ATTACK1 )
		{
			SetActivity( (Activity) ACT_ICH_BITE_HIT );
		}
		
		return;
	}

	//Play the miss animation and sound
	if ( GetActivity() == ACT_MELEE_ATTACK1 )
	{
		SetActivity( (Activity) ACT_ICH_BITE_MISS );
	}

	//Miss sound
	EmitSound( "NPC_Ichthyosaur.BiteMiss" );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::Beached( void )
{
	trace_t	tr;
	Vector	testPos;

	testPos = GetAbsOrigin() - Vector( 0, 0, ICH_DEPTH_PREFERENCE );
	
	AI_TraceHull( GetAbsOrigin(), testPos, GetHullMins(), GetHullMaxs(), MASK_NPCSOLID, this, COLLISION_GROUP_NONE, &tr );

	return ( tr.fraction < 1.0f );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::PrescheduleThink( void )
{
	BaseClass::PrescheduleThink();
	
	//Ambient sounds
	/*
	if ( random->RandomInt( 0, 20 ) == 10 )
	{
		if ( random->RandomInt( 0, 1 ) )
		{
			ENVELOPE_CONTROLLER.SoundChangeVolume( m_pSwimSound, random->RandomFloat( 0.0f, 0.5f ), 1.0f );
		}
		else
		{
			ENVELOPE_CONTROLLER.SoundChangeVolume( m_pVoiceSound, random->RandomFloat( 0.0f, 0.5f ), 1.0f );
		}
	}
	*/

	// OF2: of2_ichthyosaur_debug. We think ten times a second; this lets about one think in five through.
	if ( of2_ichthyosaur_debug.GetBool() && ( ( gpGlobals->tickcount % 33 ) < 7 ) )
	{
		CBaseEntity	*pEnemy = GetEnemy();
		Vector		vecFacing;
		GetVectors( &vecFacing, NULL, NULL );

		float	flEnemyDist = 0.0f;
		float	flEnemyDot = 0.0f;

		if ( pEnemy != NULL )
		{
			Vector	vecToEnemy = pEnemy->GetAbsOrigin() - GetAbsOrigin();
			flEnemyDist = VectorNormalize( vecToEnemy );
			flEnemyDot = DotProduct( vecToEnemy, vecFacing );
		}

		Msg( "ich %d: state %d sched %s | enemy %s dist %.0f dot %.2f see %d melee %d notfacing %d toofar %d unreachable %d | water me %d enemy %d | goal %d left %.0f | speed %.0f pitch %.0f | bite in %.1f hold %.1f\n",
			entindex(), (int)m_NPCState, GetCurSchedule() ? GetCurSchedule()->GetName() : "none",
			pEnemy ? pEnemy->GetClassname() : "none", flEnemyDist, flEnemyDot,
			HasCondition( COND_SEE_ENEMY ), HasCondition( COND_CAN_MELEE_ATTACK1 ), HasCondition( COND_NOT_FACING_ATTACK ),
			HasCondition( COND_TOO_FAR_TO_ATTACK ), HasCondition( COND_ENEMY_UNREACHABLE ),
			GetWaterLevel(), pEnemy ? pEnemy->GetWaterLevel() : -1,
			GetNavigator()->IsGoalActive(), GetNavigator()->IsGoalActive() ? ( GetNavigator()->GetGoalPos() - GetAbsOrigin() ).Length() : 0.0f,
			GetAbsVelocity().Length(), GetLocalAngles().x,
			m_flNextBiteTime - gpGlobals->curtime, m_flHoldTime - gpGlobals->curtime );

		if ( GetNavigator()->IsGoalActive() )
		{
			NDebugOverlay::Line( GetAbsOrigin(), GetNavigator()->GetCurWaypointPos(), 0, 255, 0, true, 0.6f );
			NDebugOverlay::Cross3D( GetNavigator()->GetGoalPos(), 16.0f, 0, 255, 0, true, 0.6f );
		}
		else if ( m_bHasMoveTarget )
		{
			NDebugOverlay::Line( GetAbsOrigin(), m_vecLastMoveTarget, 255, 255, 0, true, 0.6f );
		}

		NDebugOverlay::BoxAngles( GetAbsOrigin(), GetHullMins(), GetHullMaxs(), vec3_angle, 255, 0, 0, 0, 0.6f );
	}

	// OF2: the breathing loop, louder and higher the faster it swims
	if ( m_pBreathSound == NULL )
	{
		CPASAttenuationFilter filter( this );
		m_pBreathSound = ENVELOPE_CONTROLLER.SoundCreate( filter, entindex(), "NPC_Ichthyosaur.OF2Breath" );
		ENVELOPE_CONTROLLER.Play( m_pBreathSound, 0.0f, 100 );
	}

	float flEffort = clamp( GetAbsVelocity().Length() / ICH_SWIM_SPEED_RUN, 0.0f, 1.0f );
	ENVELOPE_CONTROLLER.SoundChangeVolume( m_pBreathSound, 0.3f + ( 0.7f * flEffort ), 0.5f );
	ENVELOPE_CONTROLLER.SoundChangePitch( m_pBreathSound, 90.0f + ( 20.0f * flEffort ), 0.5f );

	//Pings
	if ( m_flNextPingTime < gpGlobals->curtime )
	{
		m_flNextPingTime = gpGlobals->curtime + random->RandomFloat( 3.0f, 8.0f );
	}
	
	//Growls
	if ( ( m_NPCState == NPC_STATE_COMBAT || m_NPCState == NPC_STATE_ALERT ) && ( m_flNextGrowlTime < gpGlobals->curtime ) )
	{
		m_flNextGrowlTime = gpGlobals->curtime + random->RandomFloat( 2.0f, 6.0f );

		// OF2: the timer played nothing. Not Valve's NPC_Ichthyosaur.Growl: four of its five wavs were never shipped.
		EmitSound( "NPC_Ichthyosaur.OF2Growl" );
	}

	//Randomly emit bubbles
	if ( random->RandomInt( 0, 10 ) == 0 )
	{
		UTIL_Bubbles( GetAbsOrigin()+(GetHullMins()*0.5f), GetAbsOrigin()+(GetHullMaxs()*0.5f), 1 );
	}

	//Check our water level
	if ( ( GetWaterLevel() != 3 ) && ( m_iLeapPhase == ICH_LEAP_NONE ) && ( m_iLungePhase == ICH_LUNGE_NONE ) )	// OF2: out of it on purpose on a jump or a lunge
	{
		if ( GetWaterLevel() < 2 )
		{
			DevMsg( 2, "Came out of water\n" );
			
			if ( Beached() )
			{
				SetSchedule( SCHED_ICH_THRASH );

				Vector vecNewVelocity = GetAbsVelocity();
				vecNewVelocity[2] = 8.0f;
				SetAbsVelocity( vecNewVelocity );
			}
		}
		else
		{
			//TODO: Wake effects
		}
	}

	// OF2: the body of an NPC it took off the land
	UpdatePrey();

	//If we have a victim, update them
	if ( m_pVictim != NULL )
	{
		//See if it's time to release the victim (OF2: or it has died)
		if ( ( m_flHoldTime < gpGlobals->curtime ) || ( m_pVictim->IsAlive() == false ) )
		{
			ReleaseVictim();
			return;
		}

		Bite();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pevInflictor - 
//			*pAttacker - 
//			flDamage - 
//			bitsDamageType - 
//-----------------------------------------------------------------------------
int	CNPC_Ichthyosaur::OnTakeDamage_Alive( const CTakeDamageInfo &info )
{
	//Release the player if he's struck us while being held
	if ( ( m_flHoldTime > gpGlobals->curtime ) && ( m_pVictim != NULL ) )
	{
		ReleaseVictim();
		
		//Don't give them as much time to flee
		m_flNextBiteTime = gpGlobals->curtime + 2.0f;

		SetSchedule( SCHED_ICH_THRASH );
	}

	return BaseClass::OnTakeDamage_Alive( info );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::EnsnareVictim( CBaseEntity *pVictim )
{
	// OF2: the player only. An NPC stops thinking when grabbed and is removed outright when let go.
	if ( ( m_pVictim != NULL ) || ( pVictim == NULL ) || ( pVictim->IsPlayer() == false ) || ( pVictim->IsAlive() == false ) )
		return;

	CBaseCombatCharacter* pBCC = (CBaseCombatCharacter *) pVictim;

	// OF2: the barnacle's interactions only have numbers once the first npc_barnacle has been made; before that
	// they are 0, DispatchInteraction refuses 0, and every grab was turned down (the user's log: "grab refused"
	// on each bite). The player's side of the grab is set directly instead; the interaction is still sent for
	// what else listens to it.
	pBCC->DispatchInteraction( g_interactionBarnacleVictimGrab, NULL, this );

	CHL2_Player	*pHeld = dynamic_cast< CHL2_Player * >( pVictim );

	if ( pHeld != NULL )
	{
		pHeld->OF2_SetHeldByNPC( true );

		{
		if ( pVictim->IsPlayer() )
		{
			CBasePlayer	*pPlayer = dynamic_cast< CBasePlayer * >((CBaseEntity *) pVictim);

			if ( pPlayer )
			{
				m_flHoldTime = MAX( gpGlobals->curtime+3.0f, pPlayer->PlayerDrownTime() - 2.0f );
				m_flHoldTime = MIN( m_flHoldTime, gpGlobals->curtime + of2_ichthyosaur_hold_max.GetFloat() );	// OF2
			}
		}
		else
		{
			m_flHoldTime	= gpGlobals->curtime + 4.0f;
		}
	
		}

		m_pVictim = pVictim;
		m_pVictim->AddSolidFlags( FSOLID_NOT_SOLID );

		// OF2: carried as our child, which the client moves with us every frame; put somewhere new each think
		// it jumped ten times a second. The player's Released interaction sets the move type back.
		m_vecVictimFree = m_pVictim->GetAbsOrigin();
		m_pVictim->SetGroundEntity( NULL );
		m_pVictim->SetAbsVelocity( vec3_origin );
		m_pVictim->SetMoveType( MOVETYPE_NONE );
		m_pVictim->SetParent( this );

		EmitSound( "NPC_Ichthyosaur.AttackGrowl" );	// OF2

		SetSchedule( SCHED_ICH_DROWN_VICTIM );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::ReleaseVictim( void )
{
	if ( m_pVictim == NULL )
		return;

	CBaseCombatCharacter *pBCC = (CBaseCombatCharacter *) m_pVictim;

	if ( of2_ichthyosaur_debug.GetBool() )
	{
		Msg( "ich %d: let go (hold had %.1f s left, victim %s, %.0f units from us)\n", entindex(), m_flHoldTime - gpGlobals->curtime,
			m_pVictim->IsAlive() ? "alive" : "dead", ( m_pVictim->GetAbsOrigin() - GetAbsOrigin() ).Length() );
	}

	// OF2: off us again, at the last place it fitted if we carried it into something, and with our speed
	m_pVictim->SetParent( NULL );

	trace_t	tr;
	CTraceFilterSkipTwoEntities filter( m_pVictim, this, COLLISION_GROUP_NONE );
	UTIL_TraceHull( m_pVictim->GetAbsOrigin(), m_pVictim->GetAbsOrigin(), m_pVictim->WorldAlignMins(), m_pVictim->WorldAlignMaxs(), MASK_PLAYERSOLID, &filter, &tr );

	if ( tr.startsolid )
	{
		m_pVictim->SetAbsOrigin( m_vecVictimFree );
	}

	m_pVictim->SetAbsVelocity( GetAbsVelocity() );

	pBCC->DispatchInteraction( g_interactionBarnacleVictimReleased, NULL, this );

	// OF2: and the player's side of letting go, for the same reason as in EnsnareVictim
	CHL2_Player	*pHeld = dynamic_cast< CHL2_Player * >( m_pVictim );

	if ( pHeld != NULL )
	{
		pHeld->OF2_SetHeldByNPC( false );
	}

	m_pVictim->RemoveSolidFlags( FSOLID_NOT_SOLID );

	m_pVictim			= NULL;
	m_flNextBiteTime	= gpGlobals->curtime + 8.0f;
	m_flHoldTime		= gpGlobals->curtime - 0.1f;
}

//-----------------------------------------------------------------------------
// Purpose: OF2: a held player cannot move and is not solid, so nothing may end us without letting go
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::Event_Killed( const CTakeDamageInfo &info )
{
	ReleaseVictim();
	ReleasePrey();	// OF2

	// OF2: a body on the server, so that it can be shoved, shot, pulled and displaced
	m_bForceServerRagdoll = true;

	BaseClass::Event_Killed( info );
}

//-----------------------------------------------------------------------------
// Purpose: OF2: the body is solid to the player (COLLISION_GROUP_INTERACTIVE; other corpses are
//			interactive debris, which they pass through), but only from when they are clear of
//			it: it dies with them at its mouth. Until then, and for good with
//			of2_ichthyosaur_corpse_solid 0, what they overlap is pushed away from them.
//			Bodies are found by model, every frame, so nothing has to be saved.
//-----------------------------------------------------------------------------
class COF2IchthyosaurCorpses : public CAutoGameSystemPerFrame
{
public:
	COF2IchthyosaurCorpses() : CAutoGameSystemPerFrame( "COF2IchthyosaurCorpses" ) {}

	virtual void FrameUpdatePostEntityThink( void );
};

static COF2IchthyosaurCorpses s_OF2IchthyosaurCorpses;

// The player's box is taken this much bigger when asking whether they are clear of the body
#define	ICH_CORPSE_CLEAR_DIST	4.0f

void COF2IchthyosaurCorpses::FrameUpdatePostEntityThink( void )
{
	bool bSolid = of2_ichthyosaur_corpse_solid.GetBool();
	float flPush = of2_ichthyosaur_corpse_push.GetFloat();
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();

	if ( gpGlobals->frametime <= 0.0f || pPlayer == NULL )
		return;

	Vector vecClear( ICH_CORPSE_CLEAR_DIST, ICH_CORPSE_CLEAR_DIST, ICH_CORPSE_CLEAR_DIST );
	Vector vecPlayerMins = pPlayer->GetAbsOrigin() + pPlayer->WorldAlignMins() - vecClear;
	Vector vecPlayerMaxs = pPlayer->GetAbsOrigin() + pPlayer->WorldAlignMaxs() + vecClear;
	Vector vecPlayerMid = pPlayer->WorldSpaceCenter();

	CBaseEntity *pBody = NULL;
	while ( ( pBody = gEntList.FindEntityByClassname( pBody, "prop_ragdoll" ) ) != NULL )
	{
		if ( pBody->GetModelName() == NULL_STRING || Q_stricmp( STRING( pBody->GetModelName() ), ICHTHYOSAUR_MODEL ) != 0 )
			continue;

		if ( pBody->GetCollisionGroup() == COLLISION_GROUP_INTERACTIVE )
		{
			if ( !bSolid )
			{
				pBody->SetCollisionGroup( COLLISION_GROUP_INTERACTIVE_DEBRIS );
			}
			continue;
		}

		// (any other group counts as not solid yet: a ragdoll made as interactive debris
		// turns itself into debris after five seconds, or sooner if it comes to rest)

		IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int nCount = pBody->VPhysicsGetObjectList( pList, ARRAYSIZE( pList ) );
		bool bTouching = false;

		for ( int i = 0; i < nCount; i++ )
		{
			Vector vecPos, vecMins, vecMaxs;
			QAngle angPart;
			pList[i]->GetPosition( &vecPos, &angPart );
			physcollision->CollideGetAABB( &vecMins, &vecMaxs, pList[i]->GetCollide(), vecPos, angPart );

			if ( !IsBoxIntersectingBox( vecMins, vecMaxs, vecPlayerMins, vecPlayerMaxs ) )
				continue;

			bTouching = true;

			if ( flPush <= 0.0f || !pPlayer->IsAlive() )
				continue;

			// away from the player, and along with them
			Vector vecAway = ( vecMins + vecMaxs ) * 0.5f - vecPlayerMid;
			if ( VectorNormalize( vecAway ) < 1.0f )
			{
				vecAway = vec3_origin;
			}

			Vector vecPush = vecAway * flPush + pPlayer->GetAbsVelocity();
			pList[i]->ApplyForceCenter( vecPush * ( pList[i]->GetMass() * gpGlobals->frametime ) );
		}

		if ( bSolid && !bTouching && nCount > 0 )
		{
			pBody->SetCollisionGroup( COLLISION_GROUP_INTERACTIVE );
		}
	}
}

void CNPC_Ichthyosaur::UpdateOnRemove( void )
{
	ReleaseVictim();
	ReleasePrey();	// OF2

	BaseClass::UpdateOnRemove();
}

void CNPC_Ichthyosaur::StopLoopingSounds( void )
{
	ENVELOPE_CONTROLLER.SoundDestroy( m_pBreathSound );
	m_pBreathSound = NULL;

	BaseClass::StopLoopingSounds();
}

//-----------------------------------------------------------------------------
// Purpose:
// Output : speed to move at
//-----------------------------------------------------------------------------
float CNPC_Ichthyosaur::GetGroundSpeed( void )
{
	if ( m_flHoldTime > gpGlobals->curtime )
		return	ICH_SWIM_SPEED_WALK/2.0f;

	if ( GetIdealActivity() == ACT_WALK )
		return ICH_SWIM_SPEED_WALK;

	if ( GetIdealActivity() == ACT_ICH_THRASH )
		return ICH_SWIM_SPEED_WALK;

	return ICH_SWIM_SPEED_RUN;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : type - 
// Output : int
//-----------------------------------------------------------------------------
int CNPC_Ichthyosaur::TranslateSchedule( int type )
{
	if ( type == SCHED_CHASE_ENEMY )	return SCHED_ICH_CHASE_ENEMY;
	//if ( type == SCHED_IDLE_STAND )		return SCHED_PATROL_WALK;
	// OF2: on again, now that it can move: with nothing to do it swims about slowly instead of hanging still
	if ( type == SCHED_IDLE_STAND && of2_ichthyosaur_wander.GetBool() )	return SCHED_ICH_PATROL_WALK;
	if ( type == SCHED_PATROL_RUN )		return SCHED_ICH_PATROL_RUN;
	if ( type == SCHED_PATROL_WALK )	return SCHED_ICH_PATROL_WALK;
	if ( type == SCHED_MELEE_ATTACK1 )	return SCHED_ICH_MELEE_ATTACK1;

	return BaseClass::TranslateSchedule( type );
}

//-----------------------------------------------------------------------------
// Purpose: OF2: a place to swim to without a node graph: a straight, clear line through the water, at
//			least flDist long, that ends under the surface
//-----------------------------------------------------------------------------
bool CNPC_Ichthyosaur::SetWanderGoal( float flDist )
{
	for ( int i = 0; i < 6; i++ )
	{
		Vector	vecDir;
		AngleVectors( QAngle( random->RandomFloat( -20.0f, 20.0f ), random->RandomFloat( 0.0f, 360.0f ), 0.0f ), &vecDir );

		float	flLength = random->RandomFloat( flDist, flDist * 2.0f ) + 32.0f;

		trace_t	tr;
		AI_TraceHull( GetAbsOrigin(), GetAbsOrigin() + ( vecDir * flLength ), GetHullMins(), GetHullMaxs(), MASK_NPCSOLID, this, COLLISION_GROUP_NONE, &tr );

		if ( ( flLength * tr.fraction ) < flDist )
			continue;

		Vector	vecGoal = tr.endpos - ( vecDir * 32.0f );

		if ( ( UTIL_PointContents( vecGoal + Vector( 0, 0, GetHullMaxs().z + ICH_DEPTH_PREFERENCE ) ) & MASK_WATER ) == 0 )
			continue;

		if ( GetNavigator()->SetGoal( vecGoal, AIN_CLEAR_TARGET ) )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pTask -
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::StartTask( const Task_t *pTask )
{
	switch ( pTask->iTask )
	{
	case TASK_ICH_THRASH_PATH:
		GetNavigator()->SetMovementActivity( (Activity) ACT_ICH_THRASH );
		TaskComplete();
		break;

	// OF2: what the chase falls back on when no path could be made (its 80 unit box rarely fits where a
	// swimmer is): swim straight at the enemy for flTaskData seconds. RunTask keeps the target on them.
	case TASK_ICH_CHARGE:
		if ( GetEnemy() == NULL )
		{
			TaskFail( FAIL_NO_ENEMY );
			return;
		}

		SetWait( pTask->flTaskData );
		SetIdealActivity( ACT_RUN );
		break;

	// OF2: the lunge at someone beside the water, the same way
	case TASK_ICH_LUNGE:
		if ( StartLunge() == false )
		{
			m_flNextLungeTime = gpGlobals->curtime + 2.0f;
			TaskFail( FAIL_NO_ROUTE );
			return;
		}

		SetIdealActivity( ACT_RUN );
		break;

	// OF2: the jump at someone hanging over the water. LeapMove does it; RunTask waits for it to end.
	case TASK_ICH_LEAP:
		if ( StartLeap() == false )
		{
			// Not from here: swim about and look again shortly
			m_flNextLeapTime = gpGlobals->curtime + 2.0f;
			TaskFail( FAIL_NO_ROUTE );
			return;
		}

		SetIdealActivity( ACT_RUN );
		break;

	case TASK_ICH_GET_PATH_TO_RANDOM_NODE:
		{
			if ( GetEnemy() == NULL || !GetNavigator()->SetRandomGoal( GetEnemy()->GetLocalOrigin(), pTask->flTaskData ) )
			{
				if (!GetNavigator()->SetRandomGoal( pTask->flTaskData ) )
				{
					// OF2: no air nodes within reach: anywhere in open water will do
					if ( !SetWanderGoal( pTask->flTaskData ) )
					{
						TaskFail(FAIL_NO_REACHABLE_NODE);
						return;
					}
				}
			}
					
			TaskComplete();
		}
		break;

	case TASK_ICH_GET_PATH_TO_DROWN_NODE:
		{
			// OF2: as deep as there is room for. A place inside the bottom has no route, and it then never dived.
			trace_t	tr;
			AI_TraceHull( GetAbsOrigin(), GetAbsOrigin() - Vector( 0, 0, pTask->flTaskData ), GetHullMins(), GetHullMaxs(), MASK_NPCSOLID, this, COLLISION_GROUP_NONE, &tr );

			float	flDepth = ( pTask->flTaskData * tr.fraction ) - ICH_HEIGHT_PREFERENCE;

			if ( flDepth < 32.0f )
			{
				TaskFail( FAIL_NO_ROUTE );
				return;
			}

			Vector	drownPos = GetLocalOrigin() - Vector( 0, 0, flDepth );

			if ( GetNavigator()->SetGoal( drownPos, AIN_CLEAR_TARGET ) == false )
			{
				TaskFail( FAIL_NO_ROUTE );
				return;
			}

			TaskComplete();
		}
		break;

	case TASK_MELEE_ATTACK1:
		m_flPlaybackRate = 1.0f;
		BaseClass::StartTask(pTask);
		break;

	default:
		BaseClass::StartTask(pTask);
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pTask - 
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::RunTask( const Task_t *pTask )
{
	switch ( pTask->iTask )
	{
	case TASK_ICH_GET_PATH_TO_RANDOM_NODE:
		return;
		break;

	case TASK_ICH_GET_PATH_TO_DROWN_NODE:
		return;
		break;

	case TASK_ICH_LUNGE:
		m_flLungeTaskTime = gpGlobals->curtime;

		if ( m_iLungePhase == ICH_LUNGE_NONE )
		{
			TaskComplete();
		}
		break;

	case TASK_ICH_LEAP:
		// OF2: LeapMove gives up a run-up that this has stopped vouching for
		m_flLeapTaskTime = gpGlobals->curtime;

		if ( m_iLeapPhase == ICH_LEAP_NONE )
		{
			TaskComplete();
		}
		break;

	case TASK_ICH_CHARGE:
		{
			CBaseEntity	*pEnemy = GetEnemy();

			if ( pEnemy == NULL )
			{
				TaskFail( FAIL_NO_ENEMY );
				return;
			}

			if ( IsWaitFinished() )
			{
				TaskComplete();
				return;
			}

			Vector	vecGoal = pEnemy->GetAbsOrigin() + ( pEnemy->GetSmoothedVelocity() * 0.5f );
			TranslateNavGoal( pEnemy, vecGoal );

			m_vecLastMoveTarget	= vecGoal;
			m_bHasMoveTarget	= true;
			m_flChargeTime		= gpGlobals->curtime;
		}
		break;

	default:
		BaseClass::RunTask(pTask);
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : desired yaw speed
//-----------------------------------------------------------------------------
float CNPC_Ichthyosaur::MaxYawSpeed( void )
{
	if ( GetIdealActivity() == ACT_MELEE_ATTACK1 )
		return 16.0f;

	if ( GetIdealActivity() == ACT_ICH_THRASH )
		return 16.0f;

	//Ramp up the yaw speed as we increase our speed
	return ICH_MIN_TURN_SPEED + ( (ICH_MAX_TURN_SPEED-ICH_MIN_TURN_SPEED) * ( fabs(GetAbsVelocity().Length()) / ICH_SWIM_SPEED_RUN ) );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEnemy - 
//			&chasePosition - 
//			&tolerance - 
//-----------------------------------------------------------------------------
void CNPC_Ichthyosaur::TranslateNavGoal( CBaseEntity *pEnemy, Vector &chasePosition )
{
	Vector offset = pEnemy->EyePosition() - pEnemy->GetAbsOrigin();
	chasePosition += offset;

	// OF2: now that it sees someone with their head out of the water: go for the part of them that is in it
	if ( ( UTIL_PointContents( chasePosition ) & MASK_WATER ) == 0 )
	{
		chasePosition = pEnemy->WorldSpaceCenter();

		if ( ( UTIL_PointContents( chasePosition ) & MASK_WATER ) == 0 )
		{
			chasePosition = pEnemy->GetAbsOrigin() + Vector( 0, 0, 8 );
		}
	}
}

float CNPC_Ichthyosaur::GetDefaultNavGoalTolerance()
{
	return GetHullWidth()*2.0f;	
}


//-----------------------------------------------------------------------------
//
// Schedules
//
//-----------------------------------------------------------------------------

//==================================================
// SCHED_ICH_CHASE_ENEMY
//==================================================

AI_DEFINE_SCHEDULE
(
	SCHED_ICH_CHASE_ENEMY,

	"	Tasks"
	"		TASK_SET_FAIL_SCHEDULE			SCHEDULE:SCHED_ICH_CHARGE"
	"		TASK_SET_TOLERANCE_DISTANCE		64"
	"		TASK_SET_GOAL					GOAL:ENEMY"
	"		TASK_GET_PATH_TO_GOAL			PATH:TRAVEL"
	"		TASK_RUN_PATH					0"
	"		TASK_WAIT_FOR_MOVEMENT			0"
	""
	"	Interrupts"
	"		COND_NEW_ENEMY"
	"		COND_ENEMY_DEAD"
	"		COND_ENEMY_UNREACHABLE"
	"		COND_CAN_MELEE_ATTACK1"
	"		COND_TOO_CLOSE_TO_ATTACK"
	"		COND_LOST_ENEMY"
	"		COND_TASK_FAILED"
);
	
//==================================================
// SCHED_ICH_PATROL_RUN
//==================================================

AI_DEFINE_SCHEDULE
(
	SCHED_ICH_PATROL_RUN,

	"	Tasks"
	"		TASK_SET_FAIL_SCHEDULE				SCHEDULE:SCHED_COMBAT_FACE"
	"		TASK_SET_TOLERANCE_DISTANCE			64"
	"		TASK_SET_ROUTE_SEARCH_TIME			4"
	"		TASK_ICH_GET_PATH_TO_RANDOM_NODE	200"
	"		TASK_RUN_PATH						0"
	"		TASK_WAIT_FOR_MOVEMENT				0"
	""
	"	Interrupts"
	"		COND_CAN_MELEE_ATTACK1"
	"		COND_GIVE_WAY"
	"		COND_NEW_ENEMY"
	"		COND_LIGHT_DAMAGE"
	"		COND_HEAVY_DAMAGE"
);

//==================================================
// SCHED_ICH_PATROL_WALK
//==================================================

AI_DEFINE_SCHEDULE
(
	SCHED_ICH_PATROL_WALK,

	"	Tasks"
	"		TASK_SET_FAIL_SCHEDULE				SCHEDULE:SCHED_COMBAT_FACE"
	"		TASK_SET_TOLERANCE_DISTANCE			64"
	"		TASK_SET_ROUTE_SEARCH_TIME			4"
	"		TASK_ICH_GET_PATH_TO_RANDOM_NODE	200"
	"		TASK_WALK_PATH						0"
	"		TASK_WAIT_FOR_MOVEMENT				0"
	""
	"	Interrupts"
	"		COND_CAN_MELEE_ATTACK1"
	"		COND_GIVE_WAY"
	"		COND_NEW_ENEMY"
	"		COND_LIGHT_DAMAGE"
	"		COND_HEAVY_DAMAGE"
);

//==================================================
// SCHED_ICH_DROWN_VICTIM
//==================================================

AI_DEFINE_SCHEDULE
(
	SCHED_ICH_DROWN_VICTIM,

	"	Tasks"
	"		TASK_SET_FAIL_SCHEDULE				SCHEDULE:SCHED_COMBAT_FACE"
	"		TASK_SET_TOLERANCE_DISTANCE			64"
	"		TASK_SET_ROUTE_SEARCH_TIME			4"
	"		TASK_ICH_GET_PATH_TO_DROWN_NODE		256"
	"		TASK_WALK_PATH						0"
	"		TASK_WAIT_FOR_MOVEMENT				0"
	""
	"	Interrupts"
	"		COND_NEW_ENEMY"
	"		COND_LIGHT_DAMAGE"
	"		COND_HEAVY_DAMAGE"
);

//=========================================================
// SCHED_ICH_MELEE_ATTACK1
//=========================================================

AI_DEFINE_SCHEDULE
(
	SCHED_ICH_MELEE_ATTACK1,

	"	Tasks"
	"		TASK_ANNOUNCE_ATTACK	1"
	"		TASK_MELEE_ATTACK1		0"
	""
	"	Interrupts"
	"		COND_NEW_ENEMY"
	"		COND_ENEMY_DEAD"
	"		COND_ENEMY_OCCLUDED"
);

//==================================================
// SCHED_ICH_CHARGE
// OF2: the chase without a path. The chase used to fall back on the slow patrol, which it then swam to the
// end of with its enemy in plain sight.
//==================================================

AI_DEFINE_SCHEDULE
(
	SCHED_ICH_CHARGE,

	"	Tasks"
	"		TASK_SET_FAIL_SCHEDULE				SCHEDULE:SCHED_ICH_PATROL_WALK"
	"		TASK_ICH_CHARGE						2"
	""
	"	Interrupts"
	"		COND_NEW_ENEMY"
	"		COND_ENEMY_DEAD"
	"		COND_CAN_MELEE_ATTACK1"
	"		COND_LOST_ENEMY"
);

//==================================================
// SCHED_ICH_LEAP
// OF2: out of the water at someone hanging over it. Nothing interrupts it: in the air it has to come down again.
//==================================================

AI_DEFINE_SCHEDULE
(
	SCHED_ICH_LEAP,

	"	Tasks"
	"		TASK_SET_FAIL_SCHEDULE				SCHEDULE:SCHED_ICH_PATROL_RUN"
	"		TASK_ICH_LEAP						0"
	""
	"	Interrupts"
);

//==================================================
// SCHED_ICH_LUNGE
// OF2: up out of the water after someone beside it, and back
//==================================================

AI_DEFINE_SCHEDULE
(
	SCHED_ICH_LUNGE,

	"	Tasks"
	"		TASK_SET_FAIL_SCHEDULE				SCHEDULE:SCHED_ICH_PATROL_RUN"
	"		TASK_ICH_LUNGE						0"
	""
	"	Interrupts"
);

//==================================================
// SCHED_ICH_THRASH
//==================================================

AI_DEFINE_SCHEDULE
(
	SCHED_ICH_THRASH,

	"	Tasks"
	"		TASK_SET_FAIL_SCHEDULE				SCHEDULE:SCHED_COMBAT_FACE"
	"		TASK_SET_TOLERANCE_DISTANCE			64"
	"		TASK_SET_ROUTE_SEARCH_TIME			4"
	"		TASK_ICH_GET_PATH_TO_RANDOM_NODE	64"
	"		TASK_ICH_THRASH_PATH				0"
	"		TASK_WAIT_FOR_MOVEMENT				0"
	""
	"	Interrupts"
);
