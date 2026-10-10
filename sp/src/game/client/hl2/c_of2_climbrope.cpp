//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: draws func_climbrope (server\hl2\of2_climbrope.cpp).
//			Nobody holding it, it is a loose rope (of2_rope_sim.cpp) hanging from
//			its anchor, simulated here every frame. Held, it runs from the
//			anchor over the server's bends to the player's hand, and what is
//			left of it hangs loose from the hand. Drawn as one smooth tube
//			(of2_curve.cpp), with the texture counted from the anchor so that
//			it stays put on the rope when the player climbs.
//
//			A rope laid along a route (see the server) has a stretch before the
//			entity, from where it is tied to the last point it is held at.
//			Nobody can take hold of that, so it exists only here: a rope held
//			at both ends for every two such points, lying between them.
//
//=============================================================================//

#include "cbase.h"
#include "hl2/of2_rope_sim.h"
#include "of2_curve.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define CLIMBROPE_MAX_BENDS		16

// Same as the server's
#define CLIMBROPE_MAX_ROUTE		16

// Stretches of rope between two points it is held at, before the entity
#define CLIMBROPE_MAX_LAID		8

// How fast the rope goes from carried to carrying and back (shares a second).
// Slowly slack on landing, quickly taut on leaving the ground.
#define CLIMBROPE_GROUND_IN		2.0f
#define CLIMBROPE_GROUND_OUT	8.0f

// One copy of the rope texture every this many units. (The beams the rope used
// to be drawn with put one every 100, which looked stretched.)
#define CLIMBROPE_TEXTURE_REPEAT	50.0f

// Same as the server's (of2_climbrope.cpp)
#define CLIMBROPE_SWAY_GRAVITY	600.0f

// How much of its speed the rope keeps each step (66 a second). The part
// hanging from the hand is dragged around fast while swinging, so it is held
// back hard: it trails behind instead of whipping about. It is only drawn;
// nothing about it touches the player or the swing.
#define CLIMBROPE_LOOSE_DAMPING	0.995f
// The stretch between the anchor and the hand settles quickly too
#define CLIMBROPE_UPPER_DAMPING	0.97f

ConVar of2_climbrope_smooth( "of2_climbrope_smooth", "10", FCVAR_NONE, "How many pieces a climb rope is drawn in between two of its points. 1 draws straight lines." );
ConVar of2_climbrope_texture_scale( "of2_climbrope_texture_scale", "1", FCVAR_NONE, "How long one copy of a climb rope's texture is, as a multiple of 50 units." );
ConVar of2_climbrope_slack( "of2_climbrope_slack", "0.001", FCVAR_NONE, "How much longer than the straight line the rope above a hanging player is drawn, as a share. More bows and trails more in a swing; 0 is a straight line." );
ConVar of2_climbrope_slack_ground( "of2_climbrope_slack_ground", "0.04", FCVAR_NONE, "The same for a player standing on the ground, who only carries the rope: this much more, as long as there is that much rope left past the hand. 0.04 sags about 37 units over 300." );
ConVar of2_climbrope_carry_ground( "of2_climbrope_carry_ground", "0", FCVAR_NONE, "How much of the hand's movement the rope above it follows at once while the player stands on the ground (hanging, all of it). Less and the rope trails behind the hand and swings after it; 1 moves with it like a stick." );
ConVar of2_climbrope_hand_lag( "of2_climbrope_hand_lag", "0.08", FCVAR_NONE, "Seconds the end of a carried rope takes to catch up with the hand when the view turns, on the ground only. 0: it is fixed to the view." );
ConVar of2_climbrope_tail_damping( "of2_climbrope_tail_damping", "0.985", FCVAR_NONE, "Share of its speed the rope hanging from the hand keeps each step (66 a second). It was a flat 0.95, which left it hanging dead." );
ConVar of2_climbrope_tail_drag( "of2_climbrope_tail_drag", "0.01", FCVAR_NONE, "How much more speed the rope hanging from the hand loses the faster it moves, so it sways and trails but doesn't whip about in a swing. 0: none." );

// c_of2_tongue.cpp
extern ConVar of2_tongue_min_light;
extern ConVar of2_tongue_round;
extern ConVar of2_tongue_side_light;
extern ConVar of2_tongue_bend_radius;

// hl_gamemovement.cpp
Vector OF2_TetherHoldPos( CBasePlayer *pPlayer, bool bAtWeapon );

class C_OF2ClimbRope : public C_BaseEntity
{
	DECLARE_CLASS( C_OF2ClimbRope, C_BaseEntity );

public:
	DECLARE_CLIENTCLASS();

	C_OF2ClimbRope();

	virtual void	OnDataChanged( DataUpdateType_t updateType );
	virtual void	ClientThink( void );

	virtual bool	ShouldDraw( void ) { return !IsDormant(); }
	virtual RenderGroup_t GetRenderGroup( void ) { return RENDER_GROUP_OPAQUE_ENTITY; }
	virtual void	GetRenderBounds( Vector &mins, Vector &maxs );
	virtual int		DrawModel( int flags );

private:
	bool	IsHeld( void ) const { return m_hPlayer.Get() != NULL; }
	float	GetTailLength( void ) const { return MAX( m_flLength - m_flHeldLength, 0.0f ); }
	Vector	GetWind( void );

	// The rope from where it is tied out: as held by the player, or as it hangs loose
	int		GetPath( Vector *pPoints, bool *pBend, bool bHeld );

	// Adds points iFirst to iLast of a rope to a path, and the corners it goes
	// over on the way there from the point before (bLead: before iFirst as well)
	static void AddSim( const COF2RopeSim &sim, int iFirst, int iLast, bool bLead, Vector *pPoints, bool *pBend, int &nPoints );
	static void AddPoint( const Vector &vecPoint, bool bBend, Vector *pPoints, bool *pBend, int &nPoints );

	// Where the held rope ends this frame
	Vector	GetHand( C_BasePlayer *pPlayer );

	// The route's last fixed point, where the entity is
	int		GetRouteAnchor( void ) const;
	void	SeedLoose( void );
	void	SimulateLaid( void );

	float	m_flLength;
	float	m_flWidth;
	int		m_nRopeMaterial;
	float	m_flSway;
	// The player holding it, if any: how much of it runs from the anchor to
	// their hand, and where it bends on the way
	EHANDLE	m_hPlayer;
	float	m_flHeldLength;
	Vector	m_vecBends[CLIMBROPE_MAX_BENDS];
	int		m_nBends;

	// Held: where the last straight stretch above the hand starts (the last
	// bend, or the anchor), and how much rope there is from there to the hand
	Vector	GetUpperRoot( void );
	float	GetUpperLength( const Vector &vecHand );

	// Loose, the whole rope from the anchor; held, the rest of it from the hand
	COF2RopeSim	m_Sim;
	bool	m_bWasHeld;

	// Held, the stretch above the hand: fixed at both ends and a touch longer
	// than the gap, so it bows and trails in a swing like a rope under load
	// instead of standing like a stick. Only drawn; the player hangs by the
	// straight line.
	COF2RopeSim	m_Upper;
	int		m_nUpperBends;

	// 1 standing on the ground with the rope in hand, 0 hanging from it
	float	m_flGround;
	// How far the drawn end is behind the hand (of2_climbrope_hand_lag)
	Vector	m_vecHandLag;
	Vector	m_vecHandWas;
	bool	m_bHandKnown;

	// The route: where it is tied, then the points. Bit i of m_nRouteFixed: it
	// is held at point i.
	Vector	m_vecRoute[CLIMBROPE_MAX_ROUTE];
	int		m_nRoute;
	int		m_nRouteFixed;

	// The stretches before the entity, and the route they were laid out by
	COF2RopeSim	m_Laid[CLIMBROPE_MAX_LAID];
	int		m_nLaid;
	Vector	m_vecLaidEnd[CLIMBROPE_MAX_LAID];
	int		m_nLaidRoute;
	int		m_nLaidFixed;
	Vector	m_vecLaidRoute[CLIMBROPE_MAX_ROUTE];

	CMaterialReference	m_Material;
};

IMPLEMENT_CLIENTCLASS_DT( C_OF2ClimbRope, DT_FuncClimbRope, CFuncClimbRope )
	RecvPropFloat( RECVINFO( m_flLength ) ),
	RecvPropFloat( RECVINFO( m_flWidth ) ),
	RecvPropInt( RECVINFO( m_nRopeMaterial ) ),
	RecvPropFloat( RECVINFO( m_flSway ) ),
	RecvPropEHandle( RECVINFO( m_hPlayer ) ),
	RecvPropFloat( RECVINFO( m_flHeldLength ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_vecBends ), RecvPropVector( RECVINFO( m_vecBends[0] ) ) ),
	RecvPropInt( RECVINFO( m_nBends ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_vecRoute ), RecvPropVector( RECVINFO( m_vecRoute[0] ) ) ),
	RecvPropInt( RECVINFO( m_nRoute ) ),
	RecvPropInt( RECVINFO( m_nRouteFixed ) ),
END_RECV_TABLE()

C_OF2ClimbRope::C_OF2ClimbRope()
{
	m_flLength = 256.0f;
	m_flWidth = 1.0f;
	m_nRopeMaterial = 0;
	m_flSway = 0.0f;
	m_flHeldLength = 0.0f;
	m_nBends = 0;
	m_bWasHeld = false;
	m_nUpperBends = -1;
	m_flGround = 0.0f;
	m_vecHandLag.Init();
	m_vecHandWas.Init();
	m_bHandKnown = false;
	m_nRoute = 0;
	m_nRouteFixed = 0;
	m_nLaid = 0;
	m_nLaidRoute = -1;
	m_nLaidFixed = 0;
}

int C_OF2ClimbRope::GetRouteAnchor( void ) const
{
	int iAnchor = 0;
	for ( int i = 1; i < m_nRoute && i < CLIMBROPE_MAX_ROUTE; i++ )
	{
		if ( m_nRouteFixed & ( 1 << i ) )
		{
			iAnchor = i;
		}
	}

	return iAnchor;
}

//-----------------------------------------------------------------------------
// The loose rope as the map has it: from the entity along the rest of the
// route, and straight down from the end of that. (The server does the same.)
//-----------------------------------------------------------------------------
void C_OF2ClimbRope::SeedLoose( void )
{
	Vector vecPath[CLIMBROPE_MAX_ROUTE];
	int nPath = 0;
	vecPath[nPath++] = GetAbsOrigin();

	for ( int i = GetRouteAnchor() + 1; i < m_nRoute && i < CLIMBROPE_MAX_ROUTE; i++ )
	{
		vecPath[nPath++] = m_vecRoute[i];
	}

	m_Sim.Seed( vecPath, nPath, m_flLength, vec3_origin );
}

//-----------------------------------------------------------------------------
// The stretches before the entity: one rope for every two points it is held
// at, as long as the route between them, laid along it and left to settle.
//-----------------------------------------------------------------------------
void C_OF2ClimbRope::SimulateLaid( void )
{
	int nRoute = clamp( m_nRoute, 0, CLIMBROPE_MAX_ROUTE );

	bool bChanged = ( nRoute != m_nLaidRoute || m_nRouteFixed != m_nLaidFixed );
	for ( int i = 0; i < nRoute && !bChanged; i++ )
	{
		bChanged = ( m_vecRoute[i] != m_vecLaidRoute[i] );
	}

	if ( bChanged )
	{
		m_nLaid = 0;
		m_nLaidRoute = nRoute;
		m_nLaidFixed = m_nRouteFixed;

		int iFrom = 0;
		for ( int i = 0; i < nRoute; i++ )
		{
			m_vecLaidRoute[i] = m_vecRoute[i];

			if ( i == 0 || !( m_nRouteFixed & ( 1 << i ) ) || m_nLaid >= CLIMBROPE_MAX_LAID )
				continue;

			float flLength = 0.0f;
			for ( int j = iFrom + 1; j <= i; j++ )
			{
				flLength += m_vecRoute[j].DistTo( m_vecRoute[j - 1] );
			}

			COF2RopeSim &sim = m_Laid[m_nLaid];
			sim.SetRadius( m_flWidth * 0.5f );
			sim.Seed( &m_vecRoute[iFrom], i - iFrom + 1, flLength, vec3_origin );
			sim.SetEndPin( m_vecRoute[i] );
			sim.SetDamping( CLIMBROPE_UPPER_DAMPING );
			m_vecLaidEnd[m_nLaid] = m_vecRoute[i];
			m_nLaid++;

			iFrom = i;
		}
	}

	Vector vecFrom = ( nRoute > 0 ) ? m_vecRoute[0] : GetAbsOrigin();
	for ( int i = 0; i < m_nLaid; i++ )
	{
		m_Laid[i].SetEndPin( m_vecLaidEnd[i] );
		m_Laid[i].Simulate( gpGlobals->frametime, vecFrom, vec3_origin );
		vecFrom = m_vecLaidEnd[i];
	}
}

void C_OF2ClimbRope::AddPoint( const Vector &vecPoint, bool bBend, Vector *pPoints, bool *pBend, int &nPoints )
{
	if ( nPoints >= OF2_CURVE_MAX_POINTS )
		return;

	pPoints[nPoints] = vecPoint;
	pBend[nPoints++] = bBend;
}

void C_OF2ClimbRope::AddSim( const COF2RopeSim &sim, int iFirst, int iLast, bool bLead, Vector *pPoints, bool *pBend, int &nPoints )
{
	for ( int i = iFirst; i <= iLast && i < sim.GetNodeCount(); i++ )
	{
		// Where the rope goes over a corner between two of its points it is
		// drawn out to the corner and round it, like a bend of the held rope
		Vector vecCorner;
		if ( ( i > iFirst || bLead ) && sim.GetLinkCorner( i - 1, &vecCorner ) )
		{
			AddPoint( vecCorner, true, pPoints, pBend, nPoints );
		}

		AddPoint( sim.GetNode( i ), false, pPoints, pBend, nPoints );
	}
}

//-----------------------------------------------------------------------------
// The hand as it is this frame. On the ground the rope's end follows it with
// a little delay; how far behind it is, is worked out once a frame in
// ClientThink. Hanging, it is the hand itself: the point the player hangs by.
//-----------------------------------------------------------------------------
Vector C_OF2ClimbRope::GetHand( C_BasePlayer *pPlayer )
{
	return OF2_TetherHoldPos( pPlayer, false ) + m_vecHandLag * m_flGround;
}

Vector C_OF2ClimbRope::GetUpperRoot( void )
{
	int nBends = MIN( m_nBends, CLIMBROPE_MAX_BENDS );
	return ( nBends > 0 ) ? m_vecBends[nBends - 1] : GetAbsOrigin();
}

float C_OF2ClimbRope::GetUpperLength( const Vector &vecHand )
{
	// What is held, less what lies between the anchor and the last bend
	float flLength = m_flHeldLength;
	Vector vecFrom = GetAbsOrigin();
	for ( int i = 0; i < MIN( m_nBends, CLIMBROPE_MAX_BENDS ); i++ )
	{
		flLength -= vecFrom.DistTo( m_vecBends[i] );
		vecFrom = m_vecBends[i];
	}

	// (never less than the gap, and that little bit more)
	flLength = MAX( flLength, vecFrom.DistTo( vecHand ) );
	float flSlack = flLength * MAX( of2_climbrope_slack.GetFloat(), 0.0f );

	// Standing on the ground the player only carries the rope, and it hangs
	// between the hand and where it comes from. (The line they would hang by
	// stays straight; nothing but the drawing knows of this.) Out of what is
	// left past the hand: with all of the rope out it is taut.
	flSlack += MIN( flLength * MAX( of2_climbrope_slack_ground.GetFloat(), 0.0f ), GetTailLength() ) * m_flGround;

	return flLength + flSlack;
}

void C_OF2ClimbRope::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	if ( updateType == DATA_UPDATE_CREATED )
	{
		SetNextClientThink( CLIENT_THINK_ALWAYS );
	}
}

void C_OF2ClimbRope::GetRenderBounds( Vector &mins, Vector &maxs )
{
	float flRadius = m_flLength + 64.0f;

	// (what is laid before the entity reaches back to where the rope is tied)
	for ( int i = 0; i <= GetRouteAnchor() && i < m_nRoute; i++ )
	{
		flRadius = MAX( flRadius, m_vecRoute[i].DistTo( GetAbsOrigin() ) + 64.0f );
	}
	mins.Init( -flRadius, -flRadius, -flRadius );
	maxs.Init( flRadius, flRadius, flRadius );
}

//-----------------------------------------------------------------------------
// The same slow wandering push as the server's, so the two ropes agree
//-----------------------------------------------------------------------------
Vector C_OF2ClimbRope::GetWind( void )
{
	float flPush = CLIMBROPE_SWAY_GRAVITY * m_flSway / MAX( m_flLength, 32.0f );
	float flTime = gpGlobals->curtime + entindex();
	return Vector( sin( flTime * 0.7f ) * flPush, cos( flTime * 0.53f ) * flPush, 0.0f );
}

int C_OF2ClimbRope::GetPath( Vector *pPoints, bool *pBend, bool bHeld )
{
	int nPoints = 0;
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();

	// From where it is tied to the entity, a stretch at a time. Each ends where
	// the next begins (and the last at the entity), so that point is left out.
	for ( int i = 0; i < m_nLaid; i++ )
	{
		AddSim( m_Laid[i], 0, m_Laid[i].GetNodeCount() - 2, false, pPoints, pBend, nPoints );

		Vector vecCorner;
		if ( m_Laid[i].GetLinkCorner( m_Laid[i].GetNodeCount() - 2, &vecCorner ) )
		{
			AddPoint( vecCorner, true, pPoints, pBend, nPoints );
		}
	}

	if ( bHeld && pPlayer )
	{
		AddPoint( GetAbsOrigin(), false, pPoints, pBend, nPoints );

		for ( int i = 0; i < MIN( m_nBends, CLIMBROPE_MAX_BENDS ); i++ )
		{
			AddPoint( m_vecBends[i], true, pPoints, pBend, nPoints );
		}

		// From the last bend (or the anchor) to the hand, along the rope as it
		// bows between the two. Its own first and last points are those two.
		if ( m_Upper.IsSeeded() )
		{
			AddSim( m_Upper, 1, m_Upper.GetNodeCount() - 2, true, pPoints, pBend, nPoints );

			Vector vecCorner;
			if ( m_Upper.GetLinkCorner( m_Upper.GetNodeCount() - 2, &vecCorner ) )
			{
				AddPoint( vecCorner, true, pPoints, pBend, nPoints );
			}
		}

		AddPoint( GetHand( pPlayer ), false, pPoints, pBend, nPoints );

		// The rest hangs from the hand (its first point is the hand)
		if ( m_Sim.IsSeeded() && GetTailLength() > 1.0f )
		{
			AddSim( m_Sim, 1, m_Sim.GetNodeCount() - 1, true, pPoints, pBend, nPoints );
		}
	}
	else if ( m_Sim.IsSeeded() )
	{
		AddSim( m_Sim, 0, m_Sim.GetNodeCount() - 1, false, pPoints, pBend, nPoints );
	}

	return nPoints;
}

void C_OF2ClimbRope::ClientThink( void )
{
	bool bHeld = IsHeld();
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( bHeld && pPlayer == NULL )
		return;

	m_Sim.SetRadius( m_flWidth * 0.5f );
	m_Upper.SetRadius( m_flWidth * 0.5f );

	SimulateLaid();

	// Carrying the rope or hanging from it, and the end of a carried rope a
	// little behind the hand
	bool bCarried = bHeld && pPlayer->GetGroundEntity() != NULL;
	m_flGround = Approach( bCarried ? 1.0f : 0.0f, m_flGround, gpGlobals->frametime * ( bCarried ? CLIMBROPE_GROUND_IN : CLIMBROPE_GROUND_OUT ) );

	float flLag = of2_climbrope_hand_lag.GetFloat();
	if ( bHeld && m_bWasHeld && flLag > 0.0f && m_flGround > 0.0f )
	{
		// Only what the hand moves about the player, which is what turning the
		// view does. Walking, the rope's end stays in the hand.
		Vector vecHandNow = OF2_TetherHoldPos( pPlayer, false ) - pPlayer->GetAbsOrigin();
		if ( m_bHandKnown )
		{
			// (what it was behind by, and what the hand has moved since)
			m_vecHandLag += m_vecHandWas - vecHandNow;
			m_vecHandLag *= exp( -gpGlobals->frametime / flLag );
			if ( m_vecHandLag.LengthSqr() > 32.0f * 32.0f )
			{
				m_vecHandLag.Init();
			}
		}

		m_vecHandWas = vecHandNow;
		m_bHandKnown = true;
	}
	else
	{
		m_vecHandLag.Init();
		m_bHandKnown = false;
	}

	if ( !m_Sim.IsSeeded() || bHeld != m_bWasHeld )
	{
		if ( bHeld )
		{
			// Taken hold of: what is past the hand stays where it lay (slack on
			// a roof stays on the roof, to be drawn off it as the player goes),
			// from the place on the loose rope nearest the hand. With no loose
			// rope to go by, it hangs from the hand.
			Vector vecHand = GetHand( pPlayer );
			if ( m_Sim.IsSeeded() )
			{
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
			else
			{
				m_Sim.SeedHanging( vecHand, GetTailLength() );
			}
		}
		else if ( m_bWasHeld && pPlayer )
		{
			// Let go: the whole rope as it lay (the server leaves its bends and
			// length as they were), moving with the player
			Vector vecPath[OF2_CURVE_MAX_POINTS];
			bool bBend[OF2_CURVE_MAX_POINTS];
			int nPath = GetPath( vecPath, bBend, true );
			// (from the entity on: what is laid before it stays as it is)
			int iFrom = 0;
			for ( int i = 0; i < nPath; i++ )
			{
				if ( !bBend[i] && vecPath[i].DistToSqr( GetAbsOrigin() ) < 0.01f )
				{
					iFrom = i;
					break;
				}
			}

			m_Sim.Seed( &vecPath[iFrom], nPath - iFrom, m_flLength, pPlayer->GetAbsVelocity() );
		}
		else
		{
			SeedLoose();
		}

		if ( !bHeld )
		{
			m_Sim.SetDamping( CLIMBROPE_LOOSE_DAMPING );
			m_Sim.SetDrag( 0.0f );
		}

		m_bWasHeld = bHeld;

		// (the stretch above the hand is laid out anew on the next hold)
		m_nUpperBends = -1;
	}

	if ( bHeld )
	{
		Vector vecHand = GetHand( pPlayer );

		m_Sim.SetDamping( clamp( of2_climbrope_tail_damping.GetFloat(), 0.0f, 1.0f ) );
		m_Sim.SetDrag( MAX( of2_climbrope_tail_drag.GetFloat(), 0.0f ) );
		m_Sim.SetLength( GetTailLength() );
		m_Sim.Simulate( gpGlobals->frametime, vecHand, vec3_origin );

		// The stretch above the hand. Laid out straight when it is taken hold
		// of, and again whenever a bend comes or goes (it starts somewhere else then).
		Vector vecRoot = GetUpperRoot();
		if ( !m_Upper.IsSeeded() || m_nUpperBends != m_nBends )
		{
			Vector vecPath[2] = { vecRoot, vecHand };
			m_Upper.Seed( vecPath, 2, vecRoot.DistTo( vecHand ), pPlayer->GetAbsVelocity() );
			m_Upper.SetDamping( CLIMBROPE_UPPER_DAMPING );
			m_nUpperBends = m_nBends;
		}

		// Hanging, the rope follows its ends at once and stays taut. Carried, it
		// is left to be drawn after the hand by its own links, and trails.
		m_Upper.SetCarry( 1.0f + ( clamp( of2_climbrope_carry_ground.GetFloat(), 0.0f, 1.0f ) - 1.0f ) * m_flGround );
		m_Upper.SetEndPin( vecHand );
		m_Upper.SetLength( GetUpperLength( vecHand ) );
		m_Upper.Simulate( gpGlobals->frametime, vecRoot, vec3_origin );
	}
	else
	{
		m_Sim.Simulate( gpGlobals->frametime, GetAbsOrigin(), GetWind() );
	}
}

int C_OF2ClimbRope::DrawModel( int flags )
{
	if ( !m_Material.IsValid() )
	{
		const model_t *pModel = modelinfo->GetModel( m_nRopeMaterial );
		if ( pModel == NULL )
			return 0;

		char szMaterial[MAX_PATH];
		Q_StripExtension( modelinfo->GetModelName( pModel ), szMaterial, sizeof( szMaterial ) );
		m_Material.Init( szMaterial, TEXTURE_GROUP_OTHER );
	}

	Vector vecPoints[OF2_CURVE_MAX_POINTS];
	bool bBend[OF2_CURVE_MAX_POINTS];
	int nPoints = GetPath( vecPoints, bBend, IsHeld() );

	OF2CurveStyle_t style;
	style.pMaterial = m_Material;
	style.flWidth = m_flWidth;
	style.flTextureRepeat = MAX( CLIMBROPE_TEXTURE_REPEAT * of2_climbrope_texture_scale.GetFloat(), 0.1f );
	style.nSmooth = of2_climbrope_smooth.GetInt();
	style.flMinLight = of2_tongue_min_light.GetFloat();

	// Round like the Barnacle's tongue, and by the same settings: darker towards its
	// edges, each side taking the light that falls on it, and a round end. No
	// thickening and no wet shine; those are the tongue's.
	style.bTube = true;
	style.flRound = of2_tongue_round.GetFloat();
	style.flSideLight = of2_tongue_side_light.GetFloat();
	style.flTipRadius = m_flWidth * 0.5f;
	style.flTipLength = 8.0f;
	style.flBendRadius = of2_tongue_bend_radius.GetFloat();

	OF2_DrawCurve( vecPoints, bBend, nPoints, style );
	return 1;
}
