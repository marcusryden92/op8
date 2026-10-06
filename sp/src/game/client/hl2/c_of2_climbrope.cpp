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
//=============================================================================//

#include "cbase.h"
#include "hl2/of2_rope_sim.h"
#include "of2_curve.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define CLIMBROPE_MAX_BENDS		16

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
#define CLIMBROPE_TAIL_DAMPING	0.95f
// The stretch between the anchor and the hand settles quickly too
#define CLIMBROPE_UPPER_DAMPING	0.97f

ConVar of2_climbrope_smooth( "of2_climbrope_smooth", "3", FCVAR_NONE, "How many pieces a climb rope is drawn in between two of its points. 1 draws straight lines." );
ConVar of2_climbrope_texture_scale( "of2_climbrope_texture_scale", "1", FCVAR_NONE, "How long one copy of a climb rope's texture is, as a multiple of 50 units." );
ConVar of2_climbrope_slack( "of2_climbrope_slack", "0.001", FCVAR_NONE, "How much longer than the straight line the rope above a hanging player is drawn, as a share. More bows and trails more in a swing; 0 is a straight line." );

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

	// The rope from the anchor out: as held by the player, or as it hangs loose
	int		GetPath( Vector *pPoints, bool *pBend, bool bHeld );

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
	return MAX( flLength, vecFrom.DistTo( vecHand ) ) * ( 1.0f + MAX( of2_climbrope_slack.GetFloat(), 0.0f ) );
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

	if ( bHeld && pPlayer )
	{
		pPoints[nPoints] = GetAbsOrigin();
		pBend[nPoints++] = false;

		for ( int i = 0; i < MIN( m_nBends, CLIMBROPE_MAX_BENDS ); i++ )
		{
			pPoints[nPoints] = m_vecBends[i];
			pBend[nPoints++] = true;
		}

		// From the last bend (or the anchor) to the hand, along the rope as it
		// bows between the two. Its own first and last points are those two.
		if ( m_Upper.IsSeeded() )
		{
			for ( int i = 1; i < m_Upper.GetNodeCount() - 1 && nPoints < OF2_CURVE_MAX_POINTS - OF2_ROPE_SIM_MAX_NODES - 1; i++ )
			{
				pPoints[nPoints] = m_Upper.GetNode( i );
				pBend[nPoints++] = false;
			}
		}

		pPoints[nPoints] = OF2_TetherHoldPos( pPlayer, false );
		pBend[nPoints++] = false;

		// The rest hangs from the hand (its first point is the hand)
		if ( m_Sim.IsSeeded() && GetTailLength() > 1.0f )
		{
			for ( int i = 1; i < m_Sim.GetNodeCount() && nPoints < OF2_CURVE_MAX_POINTS; i++ )
			{
				pPoints[nPoints] = m_Sim.GetNode( i );
				pBend[nPoints++] = false;
			}
		}
	}
	else if ( m_Sim.IsSeeded() )
	{
		for ( int i = 0; i < m_Sim.GetNodeCount() && nPoints < OF2_CURVE_MAX_POINTS; i++ )
		{
			pPoints[nPoints] = m_Sim.GetNode( i );
			pBend[nPoints++] = false;
		}
	}

	return nPoints;
}

void C_OF2ClimbRope::ClientThink( void )
{
	bool bHeld = IsHeld();
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( bHeld && pPlayer == NULL )
		return;

	if ( !m_Sim.IsSeeded() || bHeld != m_bWasHeld )
	{
		if ( bHeld )
		{
			// Taken hold of: what is past the hand stays where it lay (slack on
			// a roof stays on the roof, to be drawn off it as the player goes),
			// from the place on the loose rope nearest the hand. With no loose
			// rope to go by, it hangs from the hand.
			Vector vecHand = OF2_TetherHoldPos( pPlayer, false );
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
			m_Sim.SetDamping( CLIMBROPE_TAIL_DAMPING );
		}
		else if ( m_bWasHeld && pPlayer )
		{
			// Let go: the whole rope as it lay (the server leaves its bends and
			// length as they were), moving with the player
			Vector vecPath[OF2_CURVE_MAX_POINTS];
			bool bBend[OF2_CURVE_MAX_POINTS];
			int nPath = GetPath( vecPath, bBend, true );
			m_Sim.Seed( vecPath, nPath, m_flLength, pPlayer->GetAbsVelocity() );
			m_Sim.SetDamping( CLIMBROPE_LOOSE_DAMPING );
		}
		else
		{
			m_Sim.SeedHanging( GetAbsOrigin(), m_flLength );
			m_Sim.SetDamping( CLIMBROPE_LOOSE_DAMPING );
		}

		m_bWasHeld = bHeld;

		// (the stretch above the hand is laid out anew on the next hold)
		m_nUpperBends = -1;
	}

	if ( bHeld )
	{
		Vector vecHand = OF2_TetherHoldPos( pPlayer, false );

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
