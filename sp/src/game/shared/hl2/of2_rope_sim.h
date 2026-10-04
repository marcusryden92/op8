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

// About this much rope between two points
#define OF2_ROPE_SIM_SPACING	16.0f

class COF2RopeSim
{
public:
	COF2RopeSim();

	// Lays the rope out along a path from its root (pPath[0]). What is left past
	// the end of the path carries on straight down. It starts moving at
	// vecVelocity at its free end, less towards the root.
	void	Seed( const Vector *pPath, int nPath, float flLength, const Vector &vecVelocity );

	// Hanging straight down from the root
	void	SeedHanging( const Vector &vecRoot, float flLength );

	// Share of its speed a point keeps from one step to the next (66 a second).
	// Lower makes it trail more and swing less.
	void	SetDamping( float flDamping )		{ m_flDamping = flDamping; }

	// The rope paid out or taken in at the root; the points stay where they are
	void	SetLength( float flLength );

	// Moves it on by flTime, its root at vecRoot. vecWind pushes every point.
	void	Simulate( float flTime, const Vector &vecRoot, const Vector &vecWind );

	bool	IsSeeded( void ) const				{ return m_nNodes > 1; }
	int		GetNodeCount( void ) const			{ return m_nNodes; }
	const Vector &GetNode( int iNode ) const	{ return m_vecPos[iNode]; }

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
};

#endif // OF2_ROPE_SIM_H
