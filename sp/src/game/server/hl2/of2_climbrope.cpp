//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: func_climbrope. A rope hanging from the point the entity is
//			placed at. The player grabs it with use, or by jumping into it,
//			and then hangs from it: forward/back climbs when looking along
//			the rope and pumps the swing otherwise, jump lets go.
//
//			Held, the rope is a COF2Tether from the anchor to the hand, bending
//			over edges; the hanging itself is the player's tether movement
//			(CHL2GameMovement::FullWalkMove). Loose, it is a simulated rope
//			(of2_rope_sim.cpp) that falls, swings and lies on the floor; that
//			is what the player can catch.
//
//			The client draws it (client\hl2\c_of2_climbrope.cpp), simulating
//			its own copy of the loose rope every frame.
//
//=============================================================================//

#include "cbase.h"
#include "of2_tether.h"
#include "hl2_player.h"
#include "in_buttons.h"
#include "hl_gamemovement.h"
#include "hl2/of2_rope_sim.h"
#include "ndebugoverlay.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define CLIMBROPE_DEFAULT_MATERIAL	"cable/of2_rope_beam.vmt"

// Bends the client is told about (C_OF2ClimbRope has the same)
#define CLIMBROPE_MAX_BENDS			16

// The sway pushes like gravity would on a rope held about m_flSway out
// (c_of2_climbrope.cpp has the same)
#define CLIMBROPE_SWAY_GRAVITY		600.0f

// Use reaches this much further than touching does
#define CLIMBROPE_USE_REACH			16.0f

// Use doesn't act twice within this time, so one press can't grab and let go
#define CLIMBROPE_USE_DELAY			0.3f

ConVar of2_climbrope_grab_dist( "of2_climbrope_grab_dist", "24", FCVAR_NONE, "How close the player's hand has to come to a climb rope to catch it in mid-air. Use reaches a little further." );
ConVar of2_climbrope_regrab_time( "of2_climbrope_regrab_time", "1.0", FCVAR_NONE, "Seconds after letting go of a climb rope before touching it catches it again." );
ConVar of2_climbrope_debug( "of2_climbrope_debug", "0", FCVAR_NONE, "Draw climb ropes' tethers, and the points of loose ones, as debug lines." );

class CFuncClimbRope : public CBaseEntity
{
	DECLARE_CLASS( CFuncClimbRope, CBaseEntity );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

public:
	CFuncClimbRope();

	void	Precache( void );
	void	Spawn( void );
	void	OnRestore( void );
	void	UpdateOnRemove( void );

	// The client draws it wherever its anchor is
	int		UpdateTransmitState( void ) { return SetTransmitState( FL_EDICT_ALWAYS ); }

	void	RopeThink( void );

	void	InputEnable( inputdata_t &inputdata );
	void	InputDisable( inputdata_t &inputdata );
	void	InputBreak( inputdata_t &inputdata );

private:
	const char *GetMaterial( void );
	Vector	GetWind( void );

	void	TryGrab( CHL2_Player *pPlayer );
	void	Grab( CHL2_Player *pPlayer );
	void	Release( void );

	void	UpdateHeld( CHL2_Player *pPlayer );
	void	UpdateClient( void );

	COF2Tether	m_Tether;

	// Not saved: laid out hanging on a load (OnRestore)
	COF2RopeSim	m_Sim;

	CNetworkVar( float, m_flLength );
	string_t	m_iszMaterial;
	CNetworkVar( int, m_nRopeMaterial );
	CNetworkVar( float, m_flWidth );
	float		m_flPump;
	float		m_flMaxAngle;
	CNetworkVar( float, m_flSway );
	float		m_flClimbSpeed;
	bool		m_bWrap;
	bool		m_bTouchGrab;
	bool		m_bDisabled;

	// The player holding it; how much of it runs from the anchor to their
	// hand, and where it bends on the way. Left as they were on letting go:
	// that is where the client's loose rope starts out from.
	CNetworkHandle( CHL2_Player, m_hPlayer );
	CNetworkVar( float, m_flHeldLength );
	CNetworkArray( Vector, m_vecBends, CLIMBROPE_MAX_BENDS );
	CNetworkVar( int, m_nBends );

	float		m_flNextUseTime;
	float		m_flNextTouchGrabTime;

	COutputEvent	m_OnGrab;
	COutputEvent	m_OnRelease;
};

LINK_ENTITY_TO_CLASS( func_climbrope, CFuncClimbRope );

IMPLEMENT_SERVERCLASS_ST( CFuncClimbRope, DT_FuncClimbRope )
	SendPropFloat( SENDINFO( m_flLength ) ),
	SendPropFloat( SENDINFO( m_flWidth ) ),
	SendPropModelIndex( SENDINFO( m_nRopeMaterial ) ),
	SendPropFloat( SENDINFO( m_flSway ) ),
	SendPropEHandle( SENDINFO( m_hPlayer ) ),
	SendPropFloat( SENDINFO( m_flHeldLength ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_vecBends ), SendPropVector( SENDINFO_ARRAY( m_vecBends ), -1, SPROP_COORD ) ),
	SendPropInt( SENDINFO( m_nBends ), 5, SPROP_UNSIGNED ),
END_SEND_TABLE()

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
	DEFINE_FIELD( m_flHeldLength,			FIELD_FLOAT ),
	DEFINE_FIELD( m_flNextUseTime,			FIELD_TIME ),
	DEFINE_FIELD( m_flNextTouchGrabTime,	FIELD_TIME ),

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
	m_flWidth = 1.0f;
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

	// The client finds the material by this index
	m_nRopeMaterial = PrecacheModel( GetMaterial() );
}

void CFuncClimbRope::Spawn( void )
{
	Precache();
	BaseClass::Spawn();

	SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );

	m_flLength = MAX( m_flLength.Get(), 32.0f );

	// Wrapping traces start at the anchor, and can't from inside a brush. One
	// placed in the ceiling it hangs from moves down to just below it.
	if ( UTIL_PointContents( GetAbsOrigin() ) & CONTENTS_SOLID )
	{
		trace_t tr;
		CTraceFilterWorldAndPropsOnly filter;
		UTIL_TraceLine( GetAbsOrigin() - Vector( 0, 0, 32 ), GetAbsOrigin(), MASK_SOLID_BRUSHONLY, &filter, &tr );
		if ( !tr.startsolid && tr.fraction < 1.0f )
		{
			SetAbsOrigin( tr.endpos - Vector( 0, 0, 1 ) );
		}
	}

	m_Sim.SeedHanging( GetAbsOrigin(), m_flLength );

	m_Tether.Init( GetAbsOrigin(), GetAbsOrigin() - Vector( 0, 0, m_flLength ), m_flLength );
	m_Tether.SetPlayerEnd( TETHER_END );
	m_Tether.SetWrapping( m_bWrap );

	SetThink( &CFuncClimbRope::RopeThink );
	SetNextThink( gpGlobals->curtime + TICK_INTERVAL );
}

void CFuncClimbRope::OnRestore( void )
{
	BaseClass::OnRestore();

	m_Sim.SeedHanging( GetAbsOrigin(), m_flLength );
	UpdateClient();
}

void CFuncClimbRope::UpdateOnRemove( void )
{
	Release();
	m_Tether.RemoveBeams();

	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// A slow push that wanders around, sized to hold the rope about m_flSway units out
//-----------------------------------------------------------------------------
Vector CFuncClimbRope::GetWind( void )
{
	float flPush = CLIMBROPE_SWAY_GRAVITY * m_flSway / MAX( m_flLength.Get(), 32.0f );
	float flTime = gpGlobals->curtime + entindex();
	return Vector( sin( flTime * 0.7f ) * flPush, cos( flTime * 0.53f ) * flPush, 0.0f );
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
		m_Sim.Simulate( TICK_INTERVAL, GetAbsOrigin(), GetWind() );
		TryGrab( static_cast<CHL2_Player *>( UTIL_GetLocalPlayer() ) );
	}

	if ( of2_climbrope_debug.GetBool() )
	{
		if ( pPlayer )
		{
			m_Tether.DebugDraw();
		}
		else
		{
			for ( int i = 1; i < m_Sim.GetNodeCount(); i++ )
			{
				NDebugOverlay::Line( m_Sim.GetNode( i - 1 ), m_Sim.GetNode( i ), 10, 204, 88, false, NDEBUG_PERSIST_TILL_NEXT_SERVER );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// The player's movement code climbs by changing the length it swings on, so
// that is where the tether's length comes from while it is held
//-----------------------------------------------------------------------------
void CFuncClimbRope::UpdateHeld( CHL2_Player *pPlayer )
{
	Vector vecHand = COF2Tether::GetPlayerHandPos( pPlayer );

	float flFixed = m_Tether.GetFixedLength( TETHER_END );
	m_Tether.SetTotalLength( MIN( flFixed + pPlayer->GetTetherSwingLength(), m_flLength.Get() ) );
	m_Tether.Update( GetAbsOrigin(), vecHand );

	// A pivot that came or went moved the point the player swings from, and
	// with it how much rope is left on their side of it
	flFixed = m_Tether.GetFixedLength( TETHER_END );
	pPlayer->UpdateTether( m_Tether.GetSwingPoint(), m_Tether.GetSwingLength(), MAX( m_flLength - flFixed, 0.0f ) );

	UpdateClient();
}

//-----------------------------------------------------------------------------
// What the client draws a held rope by
//-----------------------------------------------------------------------------
void CFuncClimbRope::UpdateClient( void )
{
	m_flHeldLength = m_Tether.GetTotalLength();

	int nBends = MIN( m_Tether.GetPivotCount(), CLIMBROPE_MAX_BENDS );
	for ( int i = 0; i < nBends; i++ )
	{
		m_vecBends.Set( i, m_Tether.GetPoint( i + 1 ) );
	}
	m_nBends = nBends;
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

	// Wherever the loose rope lies nearest
	float flDist = m_Sim.GetDistance( COF2Tether::GetPlayerHandPos( pPlayer ), NULL );

	float flReach = of2_climbrope_grab_dist.GetFloat() + ( bUse ? CLIMBROPE_USE_REACH : 0.0f );
	if ( flDist > flReach )
		return;

	Grab( pPlayer );
}

void CFuncClimbRope::Grab( CHL2_Player *pPlayer )
{
	Vector vecHand = COF2Tether::GetPlayerHandPos( pPlayer );

	// Held, the rope is only the tether: straight from the anchor to the hand,
	// bending over what it swings across. Nothing of the loose rope's simulation
	// carries over into the swing.
	float flLength = MIN( GetAbsOrigin().DistTo( vecHand ), m_flLength.Get() );
	m_Tether.Init( GetAbsOrigin(), vecHand, flLength );
	float flFixed = 0.0f;

	m_hPlayer = pPlayer;
	m_flNextUseTime = gpGlobals->curtime + CLIMBROPE_USE_DELAY;

	pPlayer->StartTether( this, m_flClimbSpeed, m_flPump, m_flMaxAngle );
	pPlayer->UpdateTether( m_Tether.GetSwingPoint(), m_Tether.GetSwingLength(), MAX( m_flLength - flFixed, 0.0f ) );

	UpdateClient();

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

	// The loose rope starts out as it lay: from the anchor over the bends to
	// the hand, the rest straight down from there, all moving with the player.
	// The client does the same from what it drew.
	Vector vecPath[CLIMBROPE_MAX_BENDS + 2];
	int nPath = 0;
	for ( int i = 0; i < m_Tether.GetPointCount() - 1 && nPath < CLIMBROPE_MAX_BENDS + 1; i++ )
	{
		vecPath[nPath++] = m_Tether.GetPoint( i );
	}
	vecPath[nPath++] = OF2_TetherHoldPos( pPlayer );

	m_Sim.Seed( vecPath, nPath, m_flLength, pPlayer->GetAbsVelocity() );

	m_OnRelease.FireOutput( pPlayer, this );
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
