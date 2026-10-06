//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: Tether. A line of fixed total length between two points that
//			wraps around the static world. See of2_tether.h.
//
//			Also in this file: the test tether behind the of2_tether_test
//			console commands.
//
//=============================================================================//

#include "cbase.h"
#include "of2_tether.h"
#include "player.h"
#include "beam_shared.h"
#include "beam_flags.h"
#include "hl_gamemovement.h"
#include "ndebugoverlay.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// HUD green and amber (ClientScheme.res "Normal" and "Caution"), for the debug overlays
#define TETHER_COLOR			10, 204, 88
#define TETHER_COLOR_PIVOT		230, 150, 0
// The line is longer than its total length
#define TETHER_COLOR_OVER		225, 40, 25

// How many times a line that just got blocked is halved back towards where it
// was clear, to find the edge it touched first
#define TETHER_EDGE_STEPS		6

// Hits from the two ends of a line this close together are on the two faces of one edge
#define TETHER_EDGE_DIST		4.0f

// A hit this close to where the trace was going is the surface that point sits on
#define TETHER_ARRIVE_DIST		1.0f

ConVar of2_tether_pivot_offset( "of2_tether_pivot_offset", "1.5", FCVAR_NONE, "How far off the corner a tether's pivot is put." );
ConVar of2_tether_pivot_mindist( "of2_tether_pivot_mindist", "8", FCVAR_NONE, "A tether gets no new pivot closer than this to the one before it." );
ConVar of2_tether_unwrap_angle( "of2_tether_unwrap_angle", "12", FCVAR_NONE, "A tether's pivot can go once the line bends less than this many degrees at it (and the way past it is clear)." );
ConVar of2_tether_unwrap_clear( "of2_tether_unwrap_clear", "6", FCVAR_NONE, "A tether's pivot also goes, however much the line bends at it, once the straight line past it is clear and misses the pivot by this much." );
ConVar of2_tether_wrap_props( "of2_tether_wrap_props", "1", FCVAR_NONE, "Tethers wrap around static props as well as world brushes." );

BEGIN_SIMPLE_DATADESC( COF2Tether )
	DEFINE_FIELD( m_vecStart,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecEnd,			FIELD_POSITION_VECTOR ),
	DEFINE_ARRAY( m_vecPivots,		FIELD_POSITION_VECTOR, OF2_TETHER_MAX_PIVOTS ),
	DEFINE_ARRAY( m_vecPivotOut,	FIELD_VECTOR, OF2_TETHER_MAX_PIVOTS ),
	DEFINE_FIELD( m_nPivots,		FIELD_INTEGER ),
	DEFINE_FIELD( m_flTotalLength,	FIELD_FLOAT ),
	DEFINE_FIELD( m_iPlayerEnd,		FIELD_INTEGER ),
	DEFINE_FIELD( m_iHeldEnd,		FIELD_INTEGER ),
	DEFINE_FIELD( m_bHeldAtWeapon,	FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bWrap,			FIELD_BOOLEAN ),
	DEFINE_ARRAY( m_hBeams,			FIELD_EHANDLE, OF2_TETHER_MAX_PIVOTS + 1 ),
	DEFINE_FIELD( m_hEndEntity,		FIELD_EHANDLE ),
END_DATADESC()

COF2Tether::COF2Tether()
{
	m_vecStart.Init();
	m_vecEnd.Init();
	for ( int i = 0; i < OF2_TETHER_MAX_PIVOTS; i++ )
	{
		m_vecPivots[i].Init();
		m_vecPivotOut[i].Init();
	}
	m_nPivots = 0;
	m_flTotalLength = 0.0f;
	m_iPlayerEnd = TETHER_END;
	m_iHeldEnd = TETHER_NONE;
	m_bHeldAtWeapon = false;
	m_bWrap = true;
}

void COF2Tether::Init( const Vector &vecStart, const Vector &vecEnd, float flTotalLength )
{
	m_vecStart = vecStart;
	m_vecEnd = vecEnd;
	m_nPivots = 0;
	SetTotalLength( flTotalLength );
}

const Vector &COF2Tether::GetPoint( int iPoint ) const
{
	if ( iPoint <= 0 )
		return m_vecStart;

	if ( iPoint <= m_nPivots )
		return m_vecPivots[iPoint - 1];

	return m_vecEnd;
}

float COF2Tether::GetPathLength( void ) const
{
	float flLength = 0.0f;
	for ( int i = 1; i < GetPointCount(); i++ )
	{
		flLength += GetPoint( i ).DistTo( GetPoint( i - 1 ) );
	}

	return flLength;
}

const Vector &COF2Tether::GetNearestPoint( OF2TetherEnd_t end ) const
{
	return ( end == TETHER_START ) ? GetPoint( 1 ) : GetPoint( GetPointCount() - 2 );
}

const Vector &COF2Tether::GetSwingNextPoint( void ) const
{
	if ( m_nPivots == 0 )
		return GetSwingPoint();

	return ( m_iPlayerEnd == TETHER_START ) ? GetPoint( 2 ) : GetPoint( GetPointCount() - 3 );
}

float COF2Tether::GetFixedLength( OF2TetherEnd_t end ) const
{
	const Vector &vecEnd = ( end == TETHER_START ) ? m_vecStart : m_vecEnd;
	return GetPathLength() - vecEnd.DistTo( GetNearestPoint( end ) );
}

float COF2Tether::GetFreeLength( OF2TetherEnd_t end ) const
{
	return MAX( m_flTotalLength - GetFixedLength( end ), 0.0f );
}

Vector COF2Tether::GetPlayerHandPos( CBasePlayer *pPlayer, bool bAtWeapon )
{
	// The same point the client draws the tether to
	return OF2_TetherHoldPos( pPlayer, bAtWeapon );
}

//-----------------------------------------------------------------------------
// Only the world and static props are in a tether's way; never physics
// objects, NPCs, the player or brush entities.
//-----------------------------------------------------------------------------
bool COF2Tether::IsBlocked( const Vector &vecFrom, const Vector &vecTo, trace_t *pTrace )
{
	if ( of2_tether_wrap_props.GetBool() )
	{
		CTraceFilterWorldAndPropsOnly filter;
		UTIL_TraceLine( vecFrom, vecTo, MASK_SOLID_BRUSHONLY, &filter, pTrace );
	}
	else
	{
		CTraceFilterWorldOnly filter;
		UTIL_TraceLine( vecFrom, vecTo, MASK_SOLID_BRUSHONLY, &filter, pTrace );
	}

	// A point inside something can't tell where the way out is
	if ( pTrace->startsolid || pTrace->fraction == 1.0f )
		return false;

	return pTrace->endpos.DistToSqr( vecTo ) > TETHER_ARRIVE_DIST * TETHER_ARRIVE_DIST;
}

void COF2Tether::Update( const Vector &vecStart, const Vector &vecEnd )
{
	Vector vecOldStart = m_vecStart;
	Vector vecOldEnd = m_vecEnd;

	m_vecStart = vecStart;
	m_vecEnd = vecEnd;

	if ( !m_bWrap )
	{
		m_nPivots = 0;
		return;
	}

	if ( m_nPivots == 0 )
	{
		// One segment between the two ends: look from the end that kept more still
		if ( vecOldStart.DistToSqr( vecStart ) > vecOldEnd.DistToSqr( vecEnd ) )
		{
			WrapEnd( TETHER_START, vecOldStart );
		}
		else
		{
			WrapEnd( TETHER_END, vecOldEnd );
		}
	}
	else
	{
		WrapEnd( TETHER_START, vecOldStart );
		WrapEnd( TETHER_END, vecOldEnd );
	}

	// Pivots between two others have nothing moving next to them, so only the
	// ones next to the ends can come off
	while ( UnwrapEnd( TETHER_START ) )
	{
	}

	while ( UnwrapEnd( TETHER_END ) )
	{
	}
}

//-----------------------------------------------------------------------------
// vecOld is where this end was a tick ago
//-----------------------------------------------------------------------------
void COF2Tether::WrapEnd( OF2TetherEnd_t end, const Vector &vecOld )
{
	Vector vecMoving = ( end == TETHER_START ) ? m_vecStart : m_vecEnd;

	// Going around something can put more than one corner in the way at once
	for ( int i = 0; i < 4 && m_nPivots < OF2_TETHER_MAX_PIVOTS; i++ )
	{
		Vector vecFixed = GetNearestPoint( end );
		Vector vecPivot, vecOut;
		if ( !FindPivot( vecFixed, vecMoving, vecOld, &vecPivot, &vecOut ) )
			return;

		// No row of pivots along a curved surface
		if ( m_nPivots > 0 && vecPivot.DistTo( vecFixed ) < of2_tether_pivot_mindist.GetFloat() )
			return;

		InsertPivot( ( end == TETHER_START ) ? 0 : m_nPivots, vecPivot, vecOut );
	}
}

//-----------------------------------------------------------------------------
// If the line from vecFixed to vecMoving is blocked, works out the pivot that
// takes it around: just off the edge the line touched first as its moving end
// came from vecMovingOld. pOut is the direction away from that corner.
//-----------------------------------------------------------------------------
bool COF2Tether::FindPivot( const Vector &vecFixed, const Vector &vecMoving, const Vector &vecMovingOld, Vector *pPivot, Vector *pOut ) const
{
	trace_t tr;
	if ( !IsBlocked( vecFixed, vecMoving, &tr ) )
		return false;

	// The side the line came from, across the line
	Vector vecSide = vec3_origin;
	Vector vecBlocked = vecMoving;

	trace_t trTest;
	if ( !IsBlocked( vecFixed, vecMovingOld, &trTest ) )
	{
		// The first hit can be well into the face. Halve the way back to where
		// the line was clear: the last blocked line hits right at the edge.
		Vector vecClear = vecMovingOld;
		for ( int i = 0; i < TETHER_EDGE_STEPS; i++ )
		{
			Vector vecMid = ( vecClear + vecBlocked ) * 0.5f;
			if ( IsBlocked( vecFixed, vecMid, &trTest ) )
			{
				vecBlocked = vecMid;
				tr = trTest;
			}
			else
			{
				vecClear = vecMid;
			}
		}

		Vector vecLine = tr.endpos - vecFixed;
		VectorNormalize( vecLine );

		vecSide = vecMovingOld - vecMoving;
		vecSide -= vecLine * DotProduct( vecSide, vecLine );
		if ( VectorNormalize( vecSide ) < 0.01f )
		{
			vecSide.Init();
		}
	}

	// The pivot has to stand off both faces that meet at the edge, or the line
	// to the next corner dips back into one of them. The same line traced from
	// its other end hits the second face, if it comes out right by the first hit.
	// Failing that (a thin plate, or the hit isn't at an edge), go back around
	// the edge the way the line came.
	Vector vecOut = tr.plane.normal + vecSide;
	if ( IsBlocked( vecBlocked, vecFixed, &trTest ) &&
		 trTest.endpos.DistToSqr( tr.endpos ) < TETHER_EDGE_DIST * TETHER_EDGE_DIST &&
		 fabs( DotProduct( trTest.plane.normal, tr.plane.normal ) ) < 0.9f )
	{
		vecOut = tr.plane.normal + trTest.plane.normal;
	}

	Vector vecPivot = tr.endpos + vecOut * of2_tether_pivot_offset.GetFloat();

	if ( IsBlocked( tr.endpos, vecPivot, &trTest ) )
	{
		vecPivot = ( tr.endpos + trTest.endpos ) * 0.5f;
	}

	VectorNormalize( vecOut );

	*pPivot = vecPivot;
	*pOut = vecOut;
	return true;
}

//-----------------------------------------------------------------------------
// The pivot next to this end goes when the way past it is clear and the line
// either runs nearly straight through it, pulls it away from its corner, or
// would miss it by a good way (of2_tether_unwrap_clear). Checking more than
// the trace keeps a pivot from going while the line only just grazes past its
// corner, and from coming back the next tick.
//-----------------------------------------------------------------------------
bool COF2Tether::UnwrapEnd( OF2TetherEnd_t end )
{
	if ( m_nPivots == 0 )
		return false;

	int iPivot = ( end == TETHER_START ) ? 0 : m_nPivots - 1;
	Vector vecPivot = GetPoint( iPivot + 1 );
	Vector vecPrev = GetPoint( iPivot );
	Vector vecNext = GetPoint( iPivot + 2 );

	Vector vecToPrev = vecPrev - vecPivot;
	Vector vecToNext = vecNext - vecPivot;
	float flPrev = VectorNormalize( vecToPrev );
	float flNext = VectorNormalize( vecToNext );

	// An end that has come all the way to the pivot leaves nothing to turn
	bool bStraight = flPrev < 1.0f || flNext < 1.0f ||
		DotProduct( vecToPrev, vecToNext ) <= -cos( DEG2RAD( of2_tether_unwrap_angle.GetFloat() ) );

	// Both sides pull on the pivot; wrapped, that presses it into the corner
	bool bLifted = DotProduct( vecToPrev + vecToNext, m_vecPivotOut[iPivot] ) > 0.05f;

	trace_t tr;
	if ( IsBlocked( vecPrev, vecNext, &tr ) )
		return false;

	if ( !bStraight && !bLifted )
	{
		// Neither, yet the way past is clear: the line has left the corner some
		// other way. A pivot doesn't slide along its edge, and an end can come
		// up past the edge and over what it belongs to (a player pulled up onto
		// a ledge and walking on: the line would run back to the lip and out
		// again). It goes if the straight line passes well clear of it, checked
		// from both sides so that an end inside something doesn't count.
		float flClear = CalcDistanceToLineSegment( vecPivot, vecPrev, vecNext );
		if ( tr.startsolid || flClear < of2_tether_unwrap_clear.GetFloat() )
			return false;

		trace_t trBack;
		if ( IsBlocked( vecNext, vecPrev, &trBack ) || trBack.startsolid )
			return false;
	}

	RemovePivot( iPivot );
	return true;
}

void COF2Tether::InsertPivot( int iPivot, const Vector &vecPivot, const Vector &vecOut )
{
	Assert( m_nPivots < OF2_TETHER_MAX_PIVOTS && iPivot <= m_nPivots );

	for ( int i = m_nPivots; i > iPivot; i-- )
	{
		m_vecPivots[i] = m_vecPivots[i - 1];
		m_vecPivotOut[i] = m_vecPivotOut[i - 1];
	}

	m_vecPivots[iPivot] = vecPivot;
	m_vecPivotOut[iPivot] = vecOut;
	m_nPivots++;
}

void COF2Tether::RemovePivot( int iPivot )
{
	Assert( iPivot >= 0 && iPivot < m_nPivots );

	m_nPivots--;
	for ( int i = iPivot; i < m_nPivots; i++ )
	{
		m_vecPivots[i] = m_vecPivots[i + 1];
		m_vecPivotOut[i] = m_vecPivotOut[i + 1];
	}
}

//-----------------------------------------------------------------------------
// One beam per segment, made and removed as the pivots come and go
//-----------------------------------------------------------------------------
void COF2Tether::UpdateBeams( const char *pszMaterial, float flWidth, const color32 &color )
{
	int nSegments = GetPointCount() - 1;

	for ( int i = 0; i < OF2_TETHER_MAX_PIVOTS + 1; i++ )
	{
		CBeam *pBeam = static_cast<CBeam *>( m_hBeams[i].Get() );
		if ( i >= nSegments )
		{
			if ( pBeam )
			{
				UTIL_Remove( pBeam );
				m_hBeams[i] = NULL;
			}
			continue;
		}

		if ( pBeam == NULL )
		{
			pBeam = CBeam::BeamCreate( pszMaterial, flWidth );
			if ( pBeam == NULL )
				continue;

			pBeam->PointsInit( GetPoint( i ), GetPoint( i + 1 ) );
			pBeam->SetColor( color.r, color.g, color.b );
			pBeam->SetBrightness( color.a );
			m_hBeams[i] = pBeam;
		}
		else if ( pBeam->GetType() != BEAM_POINTS )
		{
			// Was the last segment, tied to the end's entity, and isn't any more
			pBeam->PointsInit( GetPoint( i ), GetPoint( i + 1 ) );
		}
		else
		{
			pBeam->SetAbsStartPos( GetPoint( i ) );
			pBeam->SetAbsEndPos( GetPoint( i + 1 ) );
			pBeam->RelinkBeam();
		}

		if ( i == nSegments - 1 && m_hEndEntity != NULL )
		{
			pBeam->PointEntInit( GetPoint( i ), m_hEndEntity );
		}

		int nFlags = 0;
		if ( i == 0 && m_iHeldEnd == TETHER_START )
		{
			nFlags |= FBEAM_OF2_HELD_START;
		}
		if ( i == nSegments - 1 && m_iHeldEnd == TETHER_END )
		{
			nFlags |= FBEAM_OF2_HELD_END;
		}
		if ( nFlags != 0 && m_bHeldAtWeapon )
		{
			nFlags |= FBEAM_OF2_HELD_AT_WEAPON;
		}
		if ( pBeam->GetBeamFlags() != nFlags )
		{
			pBeam->SetBeamFlags( nFlags );
		}
	}
}

void COF2Tether::RemoveBeams( void )
{
	for ( int i = 0; i < OF2_TETHER_MAX_PIVOTS + 1; i++ )
	{
		if ( m_hBeams[i] != NULL )
		{
			UTIL_Remove( m_hBeams[i] );
			m_hBeams[i] = NULL;
		}
	}
}

void COF2Tether::DebugDraw( void ) const
{
	bool bOver = GetPathLength() > m_flTotalLength;

	for ( int i = 1; i < GetPointCount(); i++ )
	{
		if ( bOver )
		{
			NDebugOverlay::Line( GetPoint( i - 1 ), GetPoint( i ), TETHER_COLOR_OVER, false, NDEBUG_PERSIST_TILL_NEXT_SERVER );
		}
		else
		{
			NDebugOverlay::Line( GetPoint( i - 1 ), GetPoint( i ), TETHER_COLOR, false, NDEBUG_PERSIST_TILL_NEXT_SERVER );
		}
	}

	for ( int i = 0; i < m_nPivots; i++ )
	{
		NDebugOverlay::Cross3D( m_vecPivots[i], 2.0f, TETHER_COLOR_PIVOT, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
		NDebugOverlay::Line( m_vecPivots[i], m_vecPivots[i] + m_vecPivotOut[i] * 8.0f, TETHER_COLOR_PIVOT, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
	}

	NDebugOverlay::Cross3D( m_vecStart, 4.0f, TETHER_COLOR, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
	NDebugOverlay::Cross3D( m_vecEnd, 4.0f, TETHER_COLOR, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );

	char szText[128];
	Q_snprintf( szText, sizeof( szText ), "pivots %d  total %.0f  path %.0f  free: start %.0f, end %.0f",
		m_nPivots, m_flTotalLength, GetPathLength(), GetFreeLength( TETHER_START ), GetFreeLength( TETHER_END ) );
	NDebugOverlay::Text( m_vecStart, szText, false, NDEBUG_PERSIST_TILL_NEXT_SERVER );
}

//-----------------------------------------------------------------------------
// Test tether for the of2_tether_test commands: starts at a fixed point, ends
// on the player or at a second fixed point, and draws itself every tick.
//-----------------------------------------------------------------------------
class COF2TetherTest : public CPointEntity
{
	DECLARE_CLASS( COF2TetherTest, CPointEntity );
	DECLARE_DATADESC();

public:
	void	Spawn( void );
	void	TetherThink( void );

	void	Start( float flTotalLength );
	void	PinEnd( const Vector &vecEnd )		{ m_bEndPinned = true; m_vecEndPin = vecEnd; }
	void	FollowPlayer( void )				{ m_bEndPinned = false; }

private:
	Vector	GetEnd( void );

	COF2Tether	m_Tether;
	bool		m_bEndPinned;
	Vector		m_vecEndPin;
};

LINK_ENTITY_TO_CLASS( of2_tether_tester, COF2TetherTest );

BEGIN_DATADESC( COF2TetherTest )
	DEFINE_EMBEDDED( m_Tether ),
	DEFINE_FIELD( m_bEndPinned,	FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vecEndPin,	FIELD_POSITION_VECTOR ),
	DEFINE_THINKFUNC( TetherThink ),
END_DATADESC()

void COF2TetherTest::Spawn( void )
{
	BaseClass::Spawn();

	m_bEndPinned = false;
	m_vecEndPin = GetAbsOrigin();

	SetThink( &COF2TetherTest::TetherThink );
	SetNextThink( gpGlobals->curtime + TICK_INTERVAL );
}

Vector COF2TetherTest::GetEnd( void )
{
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	if ( m_bEndPinned || pPlayer == NULL )
		return m_vecEndPin;

	return COF2Tether::GetPlayerHandPos( pPlayer );
}

void COF2TetherTest::Start( float flTotalLength )
{
	m_bEndPinned = false;
	m_Tether.Init( GetAbsOrigin(), GetEnd(), flTotalLength );
}

void COF2TetherTest::TetherThink( void )
{
	m_Tether.Update( GetAbsOrigin(), GetEnd() );
	m_Tether.DebugDraw();

	SetNextThink( gpGlobals->curtime + TICK_INTERVAL );
}

static COF2TetherTest *TetherTest_Find( void )
{
	return static_cast<COF2TetherTest *>( gEntList.FindEntityByClassname( NULL, "of2_tether_tester" ) );
}

// A point just off the surface the player looks at
static bool TetherTest_AimPoint( CBasePlayer *pPlayer, Vector *pPoint )
{
	Vector vecForward;
	pPlayer->EyeVectors( &vecForward );

	trace_t tr;
	CTraceFilterWorldAndPropsOnly filter;
	UTIL_TraceLine( pPlayer->EyePosition(), pPlayer->EyePosition() + vecForward * MAX_TRACE_LENGTH, MASK_SOLID_BRUSHONLY, &filter, &tr );
	if ( tr.fraction == 1.0f || tr.startsolid )
	{
		Msg( "Nothing solid there.\n" );
		return false;
	}

	*pPoint = tr.endpos + tr.plane.normal * 4.0f;
	return true;
}

CON_COMMAND_F( of2_tether_test, "Starts a test tether at the point you look at; its other end follows you. Optional argument: total length (default 600).", FCVAR_CHEAT )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	Vector vecPoint;
	if ( pPlayer == NULL || !TetherTest_AimPoint( pPlayer, &vecPoint ) )
		return;

	COF2TetherTest *pTest = TetherTest_Find();
	if ( pTest == NULL )
	{
		pTest = static_cast<COF2TetherTest *>( CreateEntityByName( "of2_tether_tester" ) );
		if ( pTest == NULL )
			return;

		DispatchSpawn( pTest );
	}

	pTest->SetAbsOrigin( vecPoint );
	pTest->Start( ( args.ArgC() > 1 ) ? atof( args[1] ) : 600.0f );
}

CON_COMMAND_F( of2_tether_test_end, "Moves the far end of the test tether to the point you look at and leaves it there.", FCVAR_CHEAT )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	COF2TetherTest *pTest = TetherTest_Find();
	if ( pTest == NULL )
	{
		Msg( "No test tether; of2_tether_test starts one.\n" );
		return;
	}

	Vector vecPoint;
	if ( pPlayer == NULL || !TetherTest_AimPoint( pPlayer, &vecPoint ) )
		return;

	pTest->PinEnd( vecPoint );
}

CON_COMMAND_F( of2_tether_test_follow, "Puts the far end of the test tether back on you.", FCVAR_CHEAT )
{
	COF2TetherTest *pTest = TetherTest_Find();
	if ( pTest )
	{
		pTest->FollowPlayer();
	}
}

CON_COMMAND_F( of2_tether_test_clear, "Removes the test tether.", FCVAR_CHEAT )
{
	COF2TetherTest *pTest = TetherTest_Find();
	if ( pTest )
	{
		UTIL_Remove( pTest );
	}
}
