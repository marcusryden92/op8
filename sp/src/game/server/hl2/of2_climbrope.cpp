//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: func_climbrope. A rope hanging from the point the entity is
//			placed at. The player grabs it with use, or by jumping into it,
//			and then hangs from it: forward/back climbs when looking along
//			the rope and pumps the swing otherwise, jump lets go.
//
//			The rope is a COF2Tether drawn as beams; the hanging itself is the
//			player's tether movement (CHL2GameMovement::FullWalkMove).
//
//=============================================================================//

#include "cbase.h"
#include "of2_tether.h"
#include "hl2_player.h"
#include "beam_shared.h"
#include "in_buttons.h"
#include "hl_gamemovement.h"
#include "beam_flags.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define CLIMBROPE_DEFAULT_MATERIAL	"cable/of2_rope_beam.vmt"

// The loose end is a pendulum of its own, for looks only
#define CLIMBROPE_END_GRAVITY		600.0f
#define CLIMBROPE_END_DAMPING		0.6f	// share of its speed lost per second

// Use reaches this much further than touching does
#define CLIMBROPE_USE_REACH			16.0f

// Use doesn't act twice within this time, so one press can't grab and let go
#define CLIMBROPE_USE_DELAY			0.3f

ConVar of2_climbrope_grab_dist( "of2_climbrope_grab_dist", "24", FCVAR_NONE, "How close the player's hand has to come to a climb rope to catch it in mid-air. Use reaches a little further." );
ConVar of2_climbrope_regrab_time( "of2_climbrope_regrab_time", "1.0", FCVAR_NONE, "Seconds after letting go of a climb rope before touching it catches it again." );
ConVar of2_climbrope_debug( "of2_climbrope_debug", "0", FCVAR_NONE, "Draw climb ropes' tethers as debug lines." );

class CFuncClimbRope : public CPointEntity
{
	DECLARE_CLASS( CFuncClimbRope, CPointEntity );
	DECLARE_DATADESC();

public:
	CFuncClimbRope();

	void	Precache( void );
	void	Spawn( void );
	void	UpdateOnRemove( void );

	void	RopeThink( void );

	void	InputEnable( inputdata_t &inputdata );
	void	InputDisable( inputdata_t &inputdata );
	void	InputBreak( inputdata_t &inputdata );

private:
	const char *GetMaterial( void );

	void	TryGrab( CHL2_Player *pPlayer );
	void	Grab( CHL2_Player *pPlayer );
	void	Release( void );

	void	UpdateHeld( CHL2_Player *pPlayer );
	void	UpdateLooseEnd( void );
	void	UpdateTail( const Vector &vecHand );
	void	RemoveTail( void );

	COF2Tether	m_Tether;

	float		m_flLength;
	string_t	m_iszMaterial;
	float		m_flWidth;
	float		m_flPump;
	float		m_flMaxAngle;
	float		m_flSway;
	float		m_flClimbSpeed;
	bool		m_bWrap;
	bool		m_bTouchGrab;
	bool		m_bDisabled;

	CHandle<CHL2_Player>	m_hPlayer;
	float		m_flNextUseTime;
	float		m_flNextTouchGrabTime;

	// Where the end nobody holds is, and how it moves
	Vector		m_vecLooseEnd;
	Vector		m_vecLooseEndVelocity;

	// The rope below the player's hand
	EHANDLE		m_hTailBeam;

	COutputEvent	m_OnGrab;
	COutputEvent	m_OnRelease;
};

LINK_ENTITY_TO_CLASS( func_climbrope, CFuncClimbRope );

BEGIN_DATADESC( CFuncClimbRope )
	DEFINE_EMBEDDED( m_Tether ),

	DEFINE_KEYFIELD( m_flLength,		FIELD_FLOAT,	"length" ),
	DEFINE_KEYFIELD( m_iszMaterial,		FIELD_STRING,	"RopeMaterial" ),
	DEFINE_KEYFIELD( m_flWidth,			FIELD_FLOAT,	"width" ),
	DEFINE_KEYFIELD( m_flPump,			FIELD_FLOAT,	"pumpstrength" ),
	DEFINE_KEYFIELD( m_flMaxAngle,		FIELD_FLOAT,	"maxswingangle" ),
	DEFINE_KEYFIELD( m_flSway,			FIELD_FLOAT,	"sway" ),
	DEFINE_KEYFIELD( m_flClimbSpeed,	FIELD_FLOAT,	"climbspeed" ),
	DEFINE_KEYFIELD( m_bWrap,			FIELD_BOOLEAN,	"wrap" ),
	DEFINE_KEYFIELD( m_bTouchGrab,		FIELD_BOOLEAN,	"touchgrab" ),
	DEFINE_KEYFIELD( m_bDisabled,		FIELD_BOOLEAN,	"StartDisabled" ),

	DEFINE_FIELD( m_hPlayer,				FIELD_EHANDLE ),
	DEFINE_FIELD( m_flNextUseTime,			FIELD_TIME ),
	DEFINE_FIELD( m_flNextTouchGrabTime,	FIELD_TIME ),
	DEFINE_FIELD( m_vecLooseEnd,			FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecLooseEndVelocity,	FIELD_VECTOR ),
	DEFINE_FIELD( m_hTailBeam,				FIELD_EHANDLE ),

	DEFINE_THINKFUNC( RopeThink ),

	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Break", InputBreak ),

	DEFINE_OUTPUT( m_OnGrab, "OnGrab" ),
	DEFINE_OUTPUT( m_OnRelease, "OnRelease" ),
END_DATADESC()

CFuncClimbRope::CFuncClimbRope()
{
	m_flLength = 256.0f;
	m_iszMaterial = NULL_STRING;
	m_flWidth = 2.0f;
	m_flPump = 120.0f;
	m_flMaxAngle = 70.0f;
	m_flSway = 4.0f;
	m_flClimbSpeed = 100.0f;
	m_bWrap = true;
	m_bTouchGrab = true;
	m_bDisabled = false;
}

const char *CFuncClimbRope::GetMaterial( void )
{
	return ( m_iszMaterial != NULL_STRING ) ? STRING( m_iszMaterial ) : CLIMBROPE_DEFAULT_MATERIAL;
}

void CFuncClimbRope::Precache( void )
{
	BaseClass::Precache();

	// Hammer's material browser gives the name without the extension
	if ( m_iszMaterial != NULL_STRING && Q_stristr( STRING( m_iszMaterial ), ".vmt" ) == NULL )
	{
		char szMaterial[MAX_PATH];
		Q_snprintf( szMaterial, sizeof( szMaterial ), "%s.vmt", STRING( m_iszMaterial ) );
		m_iszMaterial = AllocPooledString( szMaterial );
	}

	PrecacheModel( GetMaterial() );
}

void CFuncClimbRope::Spawn( void )
{
	Precache();
	BaseClass::Spawn();

	m_flLength = MAX( m_flLength, 32.0f );

	m_vecLooseEnd = GetAbsOrigin() - Vector( 0, 0, m_flLength );
	m_vecLooseEndVelocity.Init();

	m_Tether.Init( GetAbsOrigin(), m_vecLooseEnd, m_flLength );
	m_Tether.SetPlayerEnd( TETHER_END );
	// Wrapping comes with the next step
	m_Tether.SetWrapping( false );

	SetThink( &CFuncClimbRope::RopeThink );
	SetNextThink( gpGlobals->curtime + TICK_INTERVAL );
}

void CFuncClimbRope::UpdateOnRemove( void )
{
	Release();
	RemoveTail();
	m_Tether.RemoveBeams();

	BaseClass::UpdateOnRemove();
}

void CFuncClimbRope::RopeThink( void )
{
	SetNextThink( gpGlobals->curtime + TICK_INTERVAL );

	CHL2_Player *pPlayer = m_hPlayer;
	if ( pPlayer )
	{
		// The player lets go on their own by jumping; use does it too
		bool bUse = ( pPlayer->GetButtonPressed() & IN_USE ) && gpGlobals->curtime >= m_flNextUseTime;
		if ( !pPlayer->IsOnTether() || pPlayer->GetTetherOwner() != this || bUse )
		{
			Release();
			pPlayer = NULL;
		}
	}

	if ( pPlayer )
	{
		UpdateHeld( pPlayer );
	}
	else
	{
		UpdateLooseEnd();
		m_Tether.Update( GetAbsOrigin(), m_vecLooseEnd );

		TryGrab( static_cast<CHL2_Player *>( UTIL_GetLocalPlayer() ) );
	}

	color32 color = { 255, 255, 255, 255 };
	m_Tether.UpdateBeams( GetMaterial(), m_flWidth, color );

	if ( of2_climbrope_debug.GetBool() )
	{
		m_Tether.DebugDraw();
	}
}

//-----------------------------------------------------------------------------
// The player's movement code climbs by changing the length it swings on, so
// that is where the tether's length comes from while it is held
//-----------------------------------------------------------------------------
void CFuncClimbRope::UpdateHeld( CHL2_Player *pPlayer )
{
	Vector vecHand = OF2_TetherHoldPos( pPlayer );

	float flFixed = m_Tether.GetFixedLength( TETHER_END );
	m_Tether.SetTotalLength( MIN( flFixed + pPlayer->GetTetherSwingLength(), m_flLength ) );
	m_Tether.Update( GetAbsOrigin(), vecHand );

	flFixed = m_Tether.GetFixedLength( TETHER_END );
	pPlayer->UpdateTether( m_Tether.GetSwingPoint(), m_Tether.GetSwingLength(), MAX( m_flLength - flFixed, 0.0f ) );

	UpdateTail( vecHand );
}

//-----------------------------------------------------------------------------
// The end nobody holds hangs from the anchor and sways a little. When the
// player lets go it starts where their hand was, so it swings back by itself.
//-----------------------------------------------------------------------------
void CFuncClimbRope::UpdateLooseEnd( void )
{
	float flFrameTime = TICK_INTERVAL;

	m_vecLooseEndVelocity.z -= CLIMBROPE_END_GRAVITY * flFrameTime;

	// A slow push that wanders around, sized to hold the end about m_flSway units out
	float flPush = CLIMBROPE_END_GRAVITY * m_flSway / m_flLength;
	float flTime = gpGlobals->curtime + entindex();
	m_vecLooseEndVelocity.x += sin( flTime * 0.7f ) * flPush * flFrameTime;
	m_vecLooseEndVelocity.y += cos( flTime * 0.53f ) * flPush * flFrameTime;

	m_vecLooseEndVelocity *= pow( 1.0f - CLIMBROPE_END_DAMPING, flFrameTime );
	m_vecLooseEnd += m_vecLooseEndVelocity * flFrameTime;

	Vector vecDir = m_vecLooseEnd - GetAbsOrigin();
	float flDist = VectorNormalize( vecDir );
	if ( flDist > m_flLength )
	{
		m_vecLooseEnd = GetAbsOrigin() + vecDir * m_flLength;

		float flOut = DotProduct( m_vecLooseEndVelocity, vecDir );
		if ( flOut > 0.0f )
		{
			m_vecLooseEndVelocity -= vecDir * flOut;
		}
	}
}

//-----------------------------------------------------------------------------
// With use from the ground or the air, or by coming close enough in mid-air
//-----------------------------------------------------------------------------
void CFuncClimbRope::TryGrab( CHL2_Player *pPlayer )
{
	if ( m_bDisabled || pPlayer == NULL || !pPlayer->IsAlive() || pPlayer->IsOnTether() )
		return;

	if ( pPlayer->GetMoveType() != MOVETYPE_WALK || pPlayer->IsInAVehicle() )
		return;

	bool bUse = ( pPlayer->GetButtonPressed() & IN_USE ) && gpGlobals->curtime >= m_flNextUseTime;
	bool bTouch = m_bTouchGrab && pPlayer->GetGroundEntity() == NULL && gpGlobals->curtime >= m_flNextTouchGrabTime;
	if ( !bUse && !bTouch )
		return;

	Vector vecHand = COF2Tether::GetPlayerHandPos( pPlayer );
	float flDist = CalcDistanceToLineSegment( vecHand, GetAbsOrigin(), m_vecLooseEnd );

	float flReach = of2_climbrope_grab_dist.GetFloat() + ( bUse ? CLIMBROPE_USE_REACH : 0.0f );
	if ( flDist > flReach )
		return;

	Grab( pPlayer );
}

void CFuncClimbRope::Grab( CHL2_Player *pPlayer )
{
	Vector vecHand = COF2Tether::GetPlayerHandPos( pPlayer );

	// As much rope as it takes to reach the hand, so the player hangs where they took hold
	float flLength = MIN( GetAbsOrigin().DistTo( vecHand ), m_flLength );
	m_Tether.Init( GetAbsOrigin(), OF2_TetherHoldPos( pPlayer ), flLength );

	m_hPlayer = pPlayer;
	m_flNextUseTime = gpGlobals->curtime + CLIMBROPE_USE_DELAY;
	m_Tether.SetHeldEnd( TETHER_END );

	pPlayer->StartTether( this, m_flClimbSpeed, m_flPump, m_flMaxAngle );
	pPlayer->UpdateTether( GetAbsOrigin(), flLength, m_flLength );

	m_OnGrab.FireOutput( pPlayer, this );
}

void CFuncClimbRope::Release( void )
{
	CHL2_Player *pPlayer = m_hPlayer;
	if ( pPlayer == NULL )
		return;

	if ( pPlayer->GetTetherOwner() == this )
	{
		pPlayer->StopTether();
	}

	m_hPlayer = NULL;
	m_flNextUseTime = gpGlobals->curtime + CLIMBROPE_USE_DELAY;
	m_flNextTouchGrabTime = gpGlobals->curtime + of2_climbrope_regrab_time.GetFloat();

	// The rope swings on from where it was held
	m_vecLooseEnd = OF2_TetherHoldPos( pPlayer );
	m_vecLooseEndVelocity = pPlayer->GetAbsVelocity();
	m_Tether.SetHeldEnd( TETHER_NONE );
	m_Tether.Init( GetAbsOrigin(), m_vecLooseEnd, m_flLength );

	RemoveTail();

	m_OnRelease.FireOutput( pPlayer, this );
}

//-----------------------------------------------------------------------------
// What is left of the rope below the hand: it carries on the way the rope
// runs, sagging towards straight down, and stops at the floor
//-----------------------------------------------------------------------------
void CFuncClimbRope::UpdateTail( const Vector &vecHand )
{
	float flLeft = m_flLength - m_Tether.GetTotalLength();
	if ( flLeft < 1.0f )
	{
		RemoveTail();
		return;
	}

	Vector vecDir = vecHand - m_Tether.GetSwingPoint();
	VectorNormalize( vecDir );
	vecDir = vecDir * 0.5f + Vector( 0, 0, -1 );
	VectorNormalize( vecDir );

	trace_t tr;
	CTraceFilterWorldAndPropsOnly filter;
	UTIL_TraceLine( vecHand, vecHand + vecDir * flLeft, MASK_SOLID_BRUSHONLY, &filter, &tr );

	CBeam *pBeam = static_cast<CBeam *>( m_hTailBeam.Get() );
	if ( pBeam == NULL )
	{
		pBeam = CBeam::BeamCreate( GetMaterial(), m_flWidth );
		if ( pBeam == NULL )
			return;

		pBeam->PointsInit( vecHand, tr.endpos );
		pBeam->SetBeamFlags( FBEAM_OF2_HELD_START );
		m_hTailBeam = pBeam;
	}
	else
	{
		pBeam->SetAbsStartPos( vecHand );
		pBeam->SetAbsEndPos( tr.endpos );
		pBeam->RelinkBeam();
	}
}

void CFuncClimbRope::RemoveTail( void )
{
	if ( m_hTailBeam != NULL )
	{
		UTIL_Remove( m_hTailBeam );
		m_hTailBeam = NULL;
	}
}

void CFuncClimbRope::InputEnable( inputdata_t &inputdata )
{
	m_bDisabled = false;
}

void CFuncClimbRope::InputDisable( inputdata_t &inputdata )
{
	m_bDisabled = true;
	Release();
}

void CFuncClimbRope::InputBreak( inputdata_t &inputdata )
{
	Release();
}
