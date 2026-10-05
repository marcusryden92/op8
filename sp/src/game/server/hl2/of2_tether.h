//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: Tether. A line of fixed total length between two points that
//			wraps around the static world: where it would cut through geometry
//			it gets a pivot, and the pivot goes again once the line has come
//			off the corner. Shared by the climbable map rope and the Barnacle.
//
//			Not an entity. Whoever owns one embeds it (DEFINE_EMBEDDED) and
//			calls Update() with where the two ends are now.
//
//=============================================================================//

#ifndef OF2_TETHER_H
#define OF2_TETHER_H
#ifdef _WIN32
#pragma once
#endif

class CBasePlayer;

#define OF2_TETHER_MAX_PIVOTS	16

enum OF2TetherEnd_t
{
	TETHER_NONE = -1,
	TETHER_START = 0,
	TETHER_END,
};

class COF2Tether
{
	DECLARE_CLASS_NOBASE( COF2Tether );
	DECLARE_SIMPLE_DATADESC();

public:
	COF2Tether();

	// Start over as one straight segment
	void	Init( const Vector &vecStart, const Vector &vecEnd, float flTotalLength );

	// Once per tick, with where the ends are now. Wraps and unwraps.
	void	Update( const Vector &vecStart, const Vector &vecEnd );

	void	SetWrapping( bool bWrap )				{ m_bWrap = bWrap; }

	// Points in order: 0 is the start, GetPointCount() - 1 the end, pivots in between
	int		GetPointCount( void ) const				{ return m_nPivots + 2; }
	const Vector &GetPoint( int iPoint ) const;
	int		GetPivotCount( void ) const				{ return m_nPivots; }
	// One more pivot, after the others (when laying a tether along a known path)
	void	AppendPivot( const Vector &vecPivot, const Vector &vecOut )	{ if ( m_nPivots < OF2_TETHER_MAX_PIVOTS ) InsertPivot( m_nPivots, vecPivot, vecOut ); }
	// Drops the pivots after the first nKeep, counted from the start
	void	TruncatePivots( int nKeep )				{ m_nPivots = clamp( nKeep, 0, m_nPivots ); }

	float	GetTotalLength( void ) const			{ return m_flTotalLength; }
	void	SetTotalLength( float flLength )		{ m_flTotalLength = MAX( flLength, 0.0f ); }

	// Length of the line as it lies now, start to end over the pivots
	float	GetPathLength( void ) const;

	// The pivot next to this end, or the other end if there are none
	const Vector &GetNearestPoint( OF2TetherEnd_t end ) const;

	// How far this end may be from GetNearestPoint(): what is left of the
	// total length after every other segment
	float	GetFreeLength( OF2TetherEnd_t end ) const;

	// The rest of the line: everything but the segment next to this end
	float	GetFixedLength( OF2TetherEnd_t end ) const;

	// The same, named from the side of the end the player hangs on
	void	SetPlayerEnd( OF2TetherEnd_t end )		{ m_iPlayerEnd = end; }
	OF2TetherEnd_t GetPlayerEnd( void ) const		{ return (OF2TetherEnd_t)m_iPlayerEnd; }
	OF2TetherEnd_t GetFarEnd( void ) const			{ return ( m_iPlayerEnd == TETHER_START ) ? TETHER_END : TETHER_START; }
	const Vector &GetSwingPoint( void ) const		{ return GetNearestPoint( GetPlayerEnd() ); }
	// The point after that, going away from the player: where the line runs on to
	// once it is past the swing point. The swing point itself if it is the far end.
	const Vector &GetSwingNextPoint( void ) const;
	float	GetSwingLength( void ) const			{ return GetFreeLength( GetPlayerEnd() ); }
	float	GetFarEndAllowance( void ) const		{ return GetFreeLength( GetFarEnd() ); }

	// Where a player holds a tether
	static Vector GetPlayerHandPos( CBasePlayer *pPlayer, bool bAtWeapon = false );

	// The end the player has in hand, if any. Its beam is drawn to OF2_TetherHoldPos()
	// on the client, frame by frame, instead of to where this end was last tick.
	// bAtWeapon: it comes out of the weapon they hold, rather than being held in the free hand.
	void	SetHeldEnd( OF2TetherEnd_t end, bool bAtWeapon = false )	{ m_iHeldEnd = end; m_bHeldAtWeapon = bAtWeapon; }

	// The thing the end is fixed to, if it is one that moves (a Barnacle bead). Its beam
	// is tied to the entity, so on the client the two stay together between ticks.
	void	SetEndEntity( CBaseEntity *pEntity )		{ m_hEndEntity = pEntity; }

	// Draws the line as beams, one per segment. The alpha is the brightness.
	void	UpdateBeams( const char *pszMaterial, float flWidth, const color32 &color );
	void	RemoveBeams( void );

	// Segments, pivots and lengths as overlays that last until the next tick
	void	DebugDraw( void ) const;

private:
	// Adds pivots to the segment between a moved end and the point next to it
	void	WrapEnd( OF2TetherEnd_t end, const Vector &vecOld );
	bool	FindPivot( const Vector &vecFixed, const Vector &vecMoving, const Vector &vecMovingOld, Vector *pPivot, Vector *pOut ) const;

	// Removes the pivot next to an end once the line has come off its corner
	bool	UnwrapEnd( OF2TetherEnd_t end );

	void	InsertPivot( int iPivot, const Vector &vecPivot, const Vector &vecOut );
	void	RemovePivot( int iPivot );

	static bool	IsBlocked( const Vector &vecFrom, const Vector &vecTo, trace_t *pTrace );

private:
	Vector	m_vecStart;
	Vector	m_vecEnd;

	// From the start to the end
	Vector	m_vecPivots[OF2_TETHER_MAX_PIVOTS];
	// For each pivot, the direction away from the corner it sits on
	Vector	m_vecPivotOut[OF2_TETHER_MAX_PIVOTS];
	int		m_nPivots;

	// Only climbing, reeling and paying out change this
	float	m_flTotalLength;

	int		m_iPlayerEnd;
	int		m_iHeldEnd;
	bool	m_bHeldAtWeapon;
	bool	m_bWrap;

	EHANDLE	m_hBeams[OF2_TETHER_MAX_PIVOTS + 1];
	EHANDLE	m_hEndEntity;
};

#endif // OF2_TETHER_H
