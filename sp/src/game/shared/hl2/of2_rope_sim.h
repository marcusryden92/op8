//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: a loose rope as a chain of points, for func_climbrope. One end
//			is held at a root; the rest falls, swings, slides and bunches up
//			against the world. It can't stretch past its length but can go
//			slack. Shared so the server (where the rope can be grabbed) and the
//			client (what is drawn, every frame) run the same thing.
//
//=============================================================================//

#ifndef OF2_ROPE_SIM_H
#define OF2_ROPE_SIM_H
#ifdef _WIN32
#pragma once
#endif

#define OF2_ROPE_SIM_MAX_NODES	32

// About this much rope between two points, unless of2_rope_spacing says otherwise
#define OF2_ROPE_SIM_SPACING	16.0f

class ITraceFilter;

// The points of a rope only know about the world one by one, so the straight
// line from a point lying on a floor to the next one hanging over its edge goes
// through the corner. This finds that corner: where the two faces the line goes
// in and out by meet, stood flStandOff off both. pOut is the way from the
// corner out into the open. False if the line is clear, or it is not one
// corner that is in the way (a thin plate, two separate things).
bool OF2_RopeLinkCorner( const Vector &vecFrom, const Vector &vecTo, float flStandOff, unsigned int nMask, ITraceFilter *pFilter, Vector *pCorner, Vector *pOut = NULL );

class COF2RopeSim
{
public:
	COF2RopeSim();

	// Lays the rope out along a path from its root (pPath[0]). What is left past
	// the end of the path carries on straight down. It starts moving at
	// vecVelocity at its free end, less towards the root. bSpread: a rope longer
	// than the path is laid along all of it instead, bunched up evenly, and
	// nothing of it goes on past the path's end.
	void	Seed( const Vector *pPath, int nPath, float flLength, const Vector &vecVelocity, bool bSpread = false );

	// Hanging straight down from the root
	void	SeedHanging( const Vector &vecRoot, float flLength );

	// Share of its speed a point keeps from one step to the next (66 a second).
	// Lower makes it trail more and swing less.
	void	SetDamping( float flDamping )		{ m_flDamping = flDamping; }

	// More of its speed lost the faster a point moves: divided by one plus this
	// times the units it moved in a step. Cuts whipping about and leaves a slow sway.
	void	SetDrag( float flDrag )				{ m_flDrag = flDrag; }

	// Half the rope's thickness: its points rest this far off what they lie on
	void	SetRadius( float flRadius )			{ m_flRadius = MAX( flRadius, 0.5f ); }
	float	GetRadius( void ) const				{ return m_flRadius; }

	// The rope paid out or taken in at the root; the points stay where they are
	void	SetLength( float flLength );

	// Holds the far end as well, there: the rope then hangs between the two
	// ends, as slack as its length leaves it. ClearEndPin lets it go again.
	// When that end moves, the points between go with it, each by its share
	// (none at the root, all of the way at the end), before anything else is
	// worked out: a rope drawn taut stays taut and follows at once, instead of
	// being dragged after the end a step at a time.
	void	SetEndPin( const Vector &vecEnd )	{ if ( !m_bEndPinned ) m_vecEndPinWas = vecEnd; m_bEndPinned = true; m_vecEndPin = vecEnd; }
	void	ClearEndPin( void )					{ m_bEndPinned = false; }
	// How much of that going along there is: 1 all of it (the default), 0 none,
	// and the points between are only drawn after the ends by the rope itself
	void	SetCarry( float flCarry )			{ m_flCarry = flCarry; }

	// Moves it on by flTime, its root at vecRoot. vecWind pushes every point.
	void	Simulate( float flTime, const Vector &vecRoot, const Vector &vecWind );

	bool	IsSeeded( void ) const				{ return m_nNodes > 1; }
	int		GetNodeCount( void ) const			{ return m_nNodes; }
	const Vector &GetNode( int iNode ) const	{ return m_vecPos[iNode]; }
	float	GetSegment( void ) const			{ return m_flSegment; }

	// The corner the rope goes over between point iLink and the next, if any
	// (OF2_RopeLinkCorner, as of the last step)
	bool	GetLinkCorner( int iLink, Vector *pCorner ) const;

	// How far an end is from where the rope comes nearest it, and how far along
	// the rope that is from the root
	float	GetDistance( const Vector &vecPoint, float *pflAlong ) const;

private:
	void	Step( const Vector &vecRoot, const Vector &vecWind );
	static int NodesFor( float flLength );

	Vector	m_vecPos[OF2_ROPE_SIM_MAX_NODES];
	Vector	m_vecPrev[OF2_ROPE_SIM_MAX_NODES];
	int		m_nNodes;
	float	m_flSegment;
	float	m_flDamping;
	float	m_flTimeLeft;
	bool	m_bEndPinned;
	Vector	m_vecEndPin;
	Vector	m_vecEndPinWas;
	Vector	m_vecRootWas;
	float	m_flDrag;
	float	m_flRadius;
	float	m_flCarry;
	bool	m_bCorner[OF2_ROPE_SIM_MAX_NODES];
	Vector	m_vecCorner[OF2_ROPE_SIM_MAX_NODES];
};

#endif // OF2_ROPE_SIM_H
