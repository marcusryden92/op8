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
//			A rope can be laid along a route of info_climbrope_point entities
//			instead of hanging straight down. At a point marked fixed it is
//			held (looped over a railing, say). The last fixed point is where
//			the rope that can be grabbed begins: the entity moves there, and
//			everything in here is about the rope from there on. What lies
//			between the place it is tied and that point is only drawn (by the
//			client, from the route).
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

// Points of a route, the place the rope is tied included (the same again)
#define CLIMBROPE_MAX_ROUTE			16

// The sway pushes like gravity would on a rope held about m_flSway out
// (c_of2_climbrope.cpp has the same)
#define CLIMBROPE_SWAY_GRAVITY		600.0f

// Use reaches this much further than touching does
#define CLIMBROPE_USE_REACH			16.0f

// Places along the line of sight a use looks for the rope at
#define CLIMBROPE_USE_STEPS			12

// Use doesn't act twice within this time, so one press can't grab and let go
#define CLIMBROPE_USE_DELAY			0.3f

// OF2: the rope past the hand of the player holding it is simulated here as the
// client draws it, so that where it lies on letting go is the same on both
// sides. These are the client's CLIMBROPE_LOOSE_DAMPING and the defaults of its
// of2_climbrope_tail_damping / of2_climbrope_tail_drag.
#define CLIMBROPE_LOOSE_DAMPING		0.995f
#define CLIMBROPE_TAIL_DAMPING		0.985f
#define CLIMBROPE_TAIL_DRAG			0.01f

ConVar of2_climbrope_grab_dist( "of2_climbrope_grab_dist", "24", FCVAR_NONE, "How close the player's hand has to come to a climb rope to catch it in mid-air. Use reaches a little further." );
ConVar of2_climbrope_regrab_time( "of2_climbrope_regrab_time", "1.0", FCVAR_NONE, "Seconds after letting go of a climb rope before touching it catches it again." );
ConVar of2_climbrope_slide_speed( "of2_climbrope_slide_speed", "200", FCVAR_NONE, "Where a held climb rope goes over an edge it slides along the edge, towards where it pulls on it evenly. This is its speed at the hardest pull. 0: it stays where it first touched." );
ConVar of2_climbrope_slide_friction( "of2_climbrope_slide_friction", "0.1", FCVAR_NONE, "How lopsided the pull has to be before a climb rope slides along an edge (0 always, 1 only when dragged almost straight along it)." );
ConVar of2_climbrope_use_dist( "of2_climbrope_use_dist", "96", FCVAR_NONE, "How far away use takes hold of a climb rope the player is looking at (one lying on the ground, say)." );
ConVar of2_climbrope_grab_wrap( "of2_climbrope_grab_wrap", "1", FCVAR_NONE, "A climb rope taken hold of starts out bent round the edges the loose rope lay over, found by following it out from where it is tied. 0: a straight line from there to the hand, as before." );
ConVar of2_climbrope_debug( "of2_climbrope_debug", "0", FCVAR_NONE, "Draw climb ropes' tethers, and the points of loose ones, as debug lines." );

//-----------------------------------------------------------------------------
// A place a climb rope is laid through
//-----------------------------------------------------------------------------
class CClimbRopePoint : public CPointEntity
{
	DECLARE_CLASS( CClimbRopePoint, CPointEntity );
	DECLARE_DATADESC();

public:
	string_t	m_iszNext;
	bool		m_bFixed;
};

LINK_ENTITY_TO_CLASS( info_climbrope_point, CClimbRopePoint );

BEGIN_DATADESC( CClimbRopePoint )
	DEFINE_KEYFIELD( m_iszNext,	FIELD_STRING,	"next" ),
	DEFINE_KEYFIELD( m_bFixed,	FIELD_BOOLEAN,	"fixed" ),
END_DATADESC()

class CFuncClimbRope : public CBaseEntity
{
	DECLARE_CLASS( CFuncClimbRope, CBaseEntity );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

public:
	CFuncClimbRope();

	void	Precache( void );
	void	Spawn( void );
	void	Activate( void );
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
	void	Grab( CHL2_Player *pPlayer, float flAlong );
	void	Release( void );

	void	UpdateHeld( CHL2_Player *pPlayer );
	void	UpdateClient( void );

	// The rope left past the hand of the player holding it
	float	GetTailLength( void ) const { return MAX( m_flLength - m_flHeldLength, 0.0f ); }
	void	SeedTail( const Vector &vecHand );

	// Reads the route, moves to its last fixed point and lays the loose rope out
	void	LayRoute( void );
	void	SeedLoose( void );

	COF2Tether	m_Tether;

	// Not saved: laid out hanging on a load (OnRestore)
	COF2RopeSim	m_Sim;

	// The rope that can be grabbed: from the entity on. The "length" key is all
	// of it, from where it is tied (m_vecTied, where the entity was placed).
	CNetworkVar( float, m_flLength );
	float		m_flTotalLength;
	Vector		m_vecTied;
	string_t	m_iszRoute;

	// The route for the client: where it is tied, then the points. Bit i of
	// m_nRouteFixed: the rope is held at point i (the first always is).
	CNetworkArray( Vector, m_vecRoute, CLIMBROPE_MAX_ROUTE );
	CNetworkVar( int, m_nRoute );
	CNetworkVar( int, m_nRouteFixed );

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
	SendPropArray3( SENDINFO_ARRAY3( m_vecRoute ), SendPropVector( SENDINFO_ARRAY( m_vecRoute ), -1, SPROP_COORD ) ),
	SendPropInt( SENDINFO( m_nRoute ), 5, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_nRouteFixed ), CLIMBROPE_MAX_ROUTE, SPROP_UNSIGNED ),
END_SEND_TABLE()

BEGIN_DATADESC( CFuncClimbRope )
	DEFINE_EMBEDDED( m_Tether ),

	DEFINE_KEYFIELD( m_flTotalLength,	FIELD_FLOAT,	"length" ),
	DEFINE_KEYFIELD( m_iszRoute,		FIELD_STRING,	"route" ),
	DEFINE_FIELD( m_flLength,			FIELD_FLOAT ),
	DEFINE_FIELD( m_vecTied,			FIELD_POSITION_VECTOR ),
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
	m_flTotalLength = 0.0f;
	m_vecTied.Init();
	m_iszRoute = NULL_STRING;
	m_nRoute = 0;
	m_nRouteFixed = 0;
	m_iszMaterial = NULL_STRING;
	m_flWidth = 1.0f;
	m_flPump = 120.0f;
	m_flMaxAngle = 70.0f;
	m_flSway = 4.0f;
	m_flClimbSpeed = 200.0f;
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

	if ( m_flTotalLength <= 0.0f )
	{
		m_flTotalLength = 256.0f;
	}
	m_flTotalLength = MAX( m_flTotalLength, 32.0f );
	m_flLength = m_flTotalLength;

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

	m_vecTied = GetAbsOrigin();

	m_Sim.SetRadius( m_flWidth * 0.5f );
	m_Sim.SeedHanging( GetAbsOrigin(), m_flLength );

	m_Tether.Init( GetAbsOrigin(), GetAbsOrigin() - Vector( 0, 0, m_flLength ), m_flLength );
	m_Tether.SetPlayerEnd( TETHER_END );
	m_Tether.SetWrapping( m_bWrap );

	SetThink( &CFuncClimbRope::RopeThink );
	SetNextThink( gpGlobals->curtime + TICK_INTERVAL );
}

//-----------------------------------------------------------------------------
// Once every entity is there, on a new map and after a load alike
//-----------------------------------------------------------------------------
void CFuncClimbRope::Activate( void )
{
	BaseClass::Activate();

	LayRoute();
}

void CFuncClimbRope::OnRestore( void )
{
	BaseClass::OnRestore();

	UpdateClient();
}

//-----------------------------------------------------------------------------
// Works everything out again from where the rope is tied and how long all of
// it is, so it comes to the same thing however often it is called. The loose
// rope is not saved: after a load it lies as it was placed in the editor.
//-----------------------------------------------------------------------------
void CFuncClimbRope::LayRoute( void )
{
	// (a save from before ropes had routes)
	if ( m_flTotalLength <= 0.0f )
	{
		m_flTotalLength = m_flLength;
		m_vecTied = GetAbsOrigin();
	}

	int nRoute = 0;
	int nFixed = 1;
	int iAnchor = 0;
	m_vecRoute.Set( nRoute++, m_vecTied );

	CBaseEntity *pNext = ( m_iszRoute != NULL_STRING ) ? gEntList.FindEntityByName( NULL, m_iszRoute ) : NULL;
	while ( pNext && nRoute < CLIMBROPE_MAX_ROUTE )
	{
		CClimbRopePoint *pPoint = dynamic_cast<CClimbRopePoint *>( pNext );
		if ( pPoint == NULL )
		{
			Warning( "func_climbrope %s: %s on its route is not an info_climbrope_point\n", GetDebugName(), pNext->GetDebugName() );
			break;
		}

		if ( pPoint->m_bFixed )
		{
			nFixed |= ( 1 << nRoute );
			iAnchor = nRoute;
		}
		m_vecRoute.Set( nRoute++, pPoint->GetAbsOrigin() );

		pNext = ( pPoint->m_iszNext != NULL_STRING ) ? gEntList.FindEntityByName( NULL, pPoint->m_iszNext ) : NULL;
	}

	m_nRoute = nRoute;
	m_nRouteFixed = nFixed;

	// What lies between where it is tied and the last place it is held is used up
	float flLaid = 0.0f;
	for ( int i = 1; i <= iAnchor; i++ )
	{
		flLaid += m_vecRoute[i].DistTo( m_vecRoute[i - 1] );
	}

	if ( flLaid > m_flTotalLength - 32.0f )
	{
		Warning( "func_climbrope %s: %.0f units long, and %.0f of that are used up before its last fixed point\n", GetDebugName(), m_flTotalLength, flLaid );
	}

	m_flLength = MAX( m_flTotalLength - flLaid, 32.0f );
	SetAbsOrigin( m_vecRoute[iAnchor] );

	m_Sim.SetRadius( m_flWidth * 0.5f );
	SeedLoose();

	if ( m_hPlayer.Get() == NULL )
	{
		m_Tether.Init( GetAbsOrigin(), GetAbsOrigin() - Vector( 0, 0, m_flLength ), m_flLength );
	}
}

//-----------------------------------------------------------------------------
// The loose rope as the map has it: from the entity along the rest of the
// route, and straight down from the end of that. (The client does the same.)
//-----------------------------------------------------------------------------
void CFuncClimbRope::SeedLoose( void )
{
	Vector vecPath[CLIMBROPE_MAX_ROUTE];
	int nPath = 0;
	vecPath[nPath++] = GetAbsOrigin();

	int iAnchor = 0;
	for ( int i = 1; i < m_nRoute && i < CLIMBROPE_MAX_ROUTE; i++ )
	{
		if ( m_nRouteFixed & ( 1 << i ) )
		{
			iAnchor = i;
		}
	}

	for ( int i = iAnchor + 1; i < m_nRoute && i < CLIMBROPE_MAX_ROUTE; i++ )
	{
		vecPath[nPath++] = m_vecRoute[i];
	}

	m_Sim.Seed( vecPath, nPath, m_flLength, vec3_origin );
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

			for ( int i = 1; i < m_Sim.GetNodeCount() && GetTailLength() > 1.0f; i++ )
			{
				NDebugOverlay::Line( m_Sim.GetNode( i - 1 ), m_Sim.GetNode( i ), 10, 204, 88, false, NDEBUG_PERSIST_TILL_NEXT_SERVER );
			}
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
	m_Tether.SetSliding( of2_climbrope_slide_speed.GetFloat(), of2_climbrope_slide_friction.GetFloat() );
	m_Tether.Update( GetAbsOrigin(), vecHand );

	// A pivot that came or went moved the point the player swings from, and
	// with it how much rope is left on their side of it
	flFixed = m_Tether.GetFixedLength( TETHER_END );
	pPlayer->UpdateTether( m_Tether.GetSwingPoint(), m_Tether.GetSwingNextPoint(), m_Tether.GetSwingLength(), MAX( m_flLength - flFixed, 0.0f ) );

	UpdateClient();

	// What is past the hand trails after it (as the client's copy does)
	m_Sim.SetDamping( CLIMBROPE_TAIL_DAMPING );
	m_Sim.SetDrag( CLIMBROPE_TAIL_DRAG );
	m_Sim.SetLength( GetTailLength() );
	m_Sim.Simulate( TICK_INTERVAL, vecHand, vec3_origin );
}

//-----------------------------------------------------------------------------
// Taken hold of: what is past the hand stays where it lay, from the place on
// the loose rope nearest the hand. The client does the same with its own copy
// (C_OF2ClimbRope::ClientThink). It used to be only the client's: here the rest
// was taken to hang straight down from the hand when the player let go, through
// the floor they stood on, and a rope seen lying on a ledge could not be picked
// up there because this side had it somewhere else.
//-----------------------------------------------------------------------------
void CFuncClimbRope::SeedTail( const Vector &vecHand )
{
	if ( !m_Sim.IsSeeded() )
	{
		m_Sim.SeedHanging( vecHand, GetTailLength() );
		return;
	}

	int iNearest = 0;
	float flNearest = FLT_MAX;
	for ( int i = 0; i < m_Sim.GetNodeCount() - 1; i++ )
	{
		float flDist = CalcDistanceToLineSegment( vecHand, m_Sim.GetNode( i ), m_Sim.GetNode( i + 1 ) );
		if ( flDist < flNearest )
		{
			flNearest = flDist;
			iNearest = i;
		}
	}

	Vector vecPath[OF2_ROPE_SIM_MAX_NODES + 1];
	int nPath = 0;
	vecPath[nPath++] = vecHand;
	for ( int i = iNearest + 1; i < m_Sim.GetNodeCount(); i++ )
	{
		vecPath[nPath++] = m_Sim.GetNode( i );
	}

	m_Sim.Seed( vecPath, nPath, GetTailLength(), vec3_origin, true );
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
	float flAlong = 0.0f;
	float flDist = m_Sim.GetDistance( COF2Tether::GetPlayerHandPos( pPlayer ), &flAlong );

	if ( bUse )
	{
		// Use also takes it from where the player looks, and from under their
		// feet: a rope lying on the ground is further from the hand than the
		// hand reaches, and could not be picked up at all.
		Vector vecForward;
		pPlayer->EyeVectors( &vecForward );

		trace_t tr;
		CTraceFilterWorldAndPropsOnly filter;
		UTIL_TraceLine( pPlayer->EyePosition(), pPlayer->EyePosition() + vecForward * of2_climbrope_use_dist.GetFloat(), MASK_SOLID_BRUSHONLY, &filter, &tr );

		for ( int i = 0; i <= CLIMBROPE_USE_STEPS + 1; i++ )
		{
			// (along the line of sight, then the feet)
			Vector vecAt = pPlayer->EyePosition() + ( tr.endpos - pPlayer->EyePosition() ) * ( (float)i / CLIMBROPE_USE_STEPS );
			if ( i > CLIMBROPE_USE_STEPS )
			{
				vecAt = pPlayer->GetAbsOrigin();
			}

			float flAt = 0.0f;
			float flTry = m_Sim.GetDistance( vecAt, &flAt );
			if ( flTry < flDist )
			{
				flDist = flTry;
				flAlong = flAt;
			}
		}
	}

	float flReach = of2_climbrope_grab_dist.GetFloat() + ( bUse ? CLIMBROPE_USE_REACH : 0.0f );
	if ( flDist > flReach )
		return;

	Grab( pPlayer, flAlong );
}

//-----------------------------------------------------------------------------
// flAlong: how far along the loose rope it is taken hold of
//-----------------------------------------------------------------------------
void CFuncClimbRope::Grab( CHL2_Player *pPlayer, float flAlong )
{
	Vector vecHand = COF2Tether::GetPlayerHandPos( pPlayer );

	// Held, the rope is only the tether: from the anchor to the hand, bending
	// over what it swings across. Nothing of the loose rope's simulation carries
	// over into the swing, but where it lay does: a rope hanging over an edge is
	// taken hold of going over that edge. The tether finds its bends as an end
	// of it moves, so its end is taken out from the anchor along the loose rope,
	// point by point, to the hand. (A straight line from the anchor to the hand
	// went through whatever the rope lay over.)
	m_Tether.Init( GetAbsOrigin(), GetAbsOrigin(), m_flLength );
	if ( of2_climbrope_grab_wrap.GetBool() && m_Sim.IsSeeded() )
	{
		int nUpTo = clamp( (int)( flAlong / MAX( m_Sim.GetSegment(), 1.0f ) ), 0, m_Sim.GetNodeCount() - 1 );

		// (its bends stay where they are found until it is held)
		m_Tether.SetSliding( 0.0f, 0.0f );
		for ( int i = 1; i <= nUpTo; i++ )
		{
			m_Tether.Update( GetAbsOrigin(), m_Sim.GetNode( i ) );
		}
	}
	m_Tether.Update( GetAbsOrigin(), vecHand );

	m_Tether.SetTotalLength( MIN( m_Tether.GetPathLength(), m_flLength.Get() ) );
	float flFixed = m_Tether.GetFixedLength( TETHER_END );

	m_hPlayer = pPlayer;
	m_flNextUseTime = gpGlobals->curtime + CLIMBROPE_USE_DELAY;

	pPlayer->StartTether( this, m_flClimbSpeed, m_flPump, m_flMaxAngle, false );
	pPlayer->UpdateTether( m_Tether.GetSwingPoint(), m_Tether.GetSwingNextPoint(), m_Tether.GetSwingLength(), MAX( m_flLength - flFixed, 0.0f ) );

	UpdateClient();
	SeedTail( vecHand );

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
	// the hand, and on along what trailed from the hand (SeedTail), all moving
	// with the player. The client does the same from what it drew.
	Vector vecPath[CLIMBROPE_MAX_BENDS + 2 + OF2_ROPE_SIM_MAX_NODES];
	int nPath = 0;
	for ( int i = 0; i < m_Tether.GetPointCount() - 1 && nPath < CLIMBROPE_MAX_BENDS + 1; i++ )
	{
		vecPath[nPath++] = m_Tether.GetPoint( i );
	}
	vecPath[nPath++] = OF2_TetherHoldPos( pPlayer );

	for ( int i = 1; i < m_Sim.GetNodeCount() && GetTailLength() > 1.0f; i++ )
	{
		vecPath[nPath++] = m_Sim.GetNode( i );
	}

	m_Sim.Seed( vecPath, nPath, m_flLength, pPlayer->GetAbsVelocity() );
	m_Sim.SetDamping( CLIMBROPE_LOOSE_DAMPING );
	m_Sim.SetDrag( 0.0f );

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
