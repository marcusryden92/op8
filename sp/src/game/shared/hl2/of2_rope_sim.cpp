//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: a loose rope as a chain of points. See of2_rope_sim.h.
//
//			Verlet integration: each point keeps where it is and where it was,
//			which is its velocity. After every step the links are pulled back
//			to their length (only when too long, so the rope can bunch up), and
//			a point that went into the world is put back where it hit it.
//
//=============================================================================//

#include "cbase.h"
#include "of2_rope_sim.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Fixed steps, so it behaves the same at any frame rate and on both sides
#define ROPE_STEP				( 1.0f / 66.0f )
#define ROPE_MAX_STEPS			8

#define ROPE_GRAVITY			600.0f

// Share of its speed a point keeps from one step to the next, unless told otherwise
#define ROPE_DAMPING			0.995f

// How many times a step the links are pulled back to length. More is stiffer.
#define ROPE_ITERATIONS			8

// Share of its speed along a surface a point loses when it touches one
#define ROPE_FRICTION			0.3f

COF2RopeSim::COF2RopeSim()
{
	m_nNodes = 0;
	m_flSegment = OF2_ROPE_SIM_SPACING;
	m_flDamping = ROPE_DAMPING;
	m_flTimeLeft = 0.0f;
	m_bEndPinned = false;
	m_vecEndPin.Init();
}

int COF2RopeSim::NodesFor( float flLength )
{
	return clamp( (int)( flLength / OF2_ROPE_SIM_SPACING ) + 2, 2, OF2_ROPE_SIM_MAX_NODES );
}

void COF2RopeSim::Seed( const Vector *pPath, int nPath, float flLength, const Vector &vecVelocity )
{
	if ( nPath < 1 )
		return;

	m_nNodes = NodesFor( flLength );
	m_flSegment = MAX( flLength, 1.0f ) / ( m_nNodes - 1 );
	m_flTimeLeft = 0.0f;

	// Walk the path, a point every m_flSegment
	int iLeg = 0;
	float flLegStart = 0.0f;
	for ( int i = 0; i < m_nNodes; i++ )
	{
		float flAlong = i * m_flSegment;

		while ( iLeg < nPath - 1 && flAlong > flLegStart + pPath[iLeg].DistTo( pPath[iLeg + 1] ) )
		{
			flLegStart += pPath[iLeg].DistTo( pPath[iLeg + 1] );
			iLeg++;
		}

		if ( iLeg < nPath - 1 )
		{
			Vector vecLeg = pPath[iLeg + 1] - pPath[iLeg];
			float flLeg = VectorNormalize( vecLeg );
			m_vecPos[i] = pPath[iLeg] + vecLeg * MIN( flAlong - flLegStart, flLeg );
		}
		else
		{
			// Past the end of the path: straight down from there
			m_vecPos[i] = pPath[nPath - 1] - Vector( 0, 0, flAlong - flLegStart );
		}

		float flShare = (float)i / ( m_nNodes - 1 );
		m_vecPrev[i] = m_vecPos[i] - vecVelocity * flShare * ROPE_STEP;
	}
}

void COF2RopeSim::SeedHanging( const Vector &vecRoot, float flLength )
{
	Seed( &vecRoot, 1, flLength, vec3_origin );
}

void COF2RopeSim::SetLength( float flLength )
{
	if ( m_nNodes > 1 )
	{
		m_flSegment = MAX( flLength, 1.0f ) / ( m_nNodes - 1 );
	}
}

void COF2RopeSim::Simulate( float flTime, const Vector &vecRoot, const Vector &vecWind )
{
	if ( m_nNodes < 2 )
		return;

	m_flTimeLeft = MIN( m_flTimeLeft + flTime, ROPE_STEP * ROPE_MAX_STEPS );
	while ( m_flTimeLeft >= ROPE_STEP )
	{
		Step( vecRoot, vecWind );
		m_flTimeLeft -= ROPE_STEP;
	}

	// Between steps the root at least goes where it is held
	m_vecPos[0] = vecRoot;
	if ( m_bEndPinned )
	{
		m_vecPos[m_nNodes - 1] = m_vecEndPin;
	}
}

void COF2RopeSim::Step( const Vector &vecRoot, const Vector &vecWind )
{
	Vector vecAccel = vecWind - Vector( 0, 0, ROPE_GRAVITY );
	float flStep2 = ROPE_STEP * ROPE_STEP;

	Vector vecStart[OF2_ROPE_SIM_MAX_NODES];
	for ( int i = 1; i < m_nNodes; i++ )
	{
		vecStart[i] = m_vecPos[i];

		Vector vecVelocity = ( m_vecPos[i] - m_vecPrev[i] ) * m_flDamping;
		m_vecPrev[i] = m_vecPos[i];
		m_vecPos[i] += vecVelocity + vecAccel * flStep2;
	}

	m_vecPos[0] = vecRoot;
	m_vecPrev[0] = vecRoot;

	// Held at the far end as well: it can't be shorter than the gap between the two
	int iLast = m_nNodes - 1;
	float flSegment = m_flSegment;
	if ( m_bEndPinned )
	{
		m_vecPos[iLast] = m_vecEndPin;
		m_vecPrev[iLast] = m_vecEndPin;
		flSegment = MAX( flSegment, vecRoot.DistTo( m_vecEndPin ) / iLast );
	}

	// Links that got too long are pulled back; a held end doesn't move
	for ( int k = 0; k < ROPE_ITERATIONS; k++ )
	{
		for ( int i = 0; i < iLast; i++ )
		{
			Vector vecLink = m_vecPos[i + 1] - m_vecPos[i];
			float flLink = vecLink.Length();
			if ( flLink <= flSegment || flLink < 0.001f )
				continue;

			bool bNearHeld = ( i == 0 );
			bool bFarHeld = ( m_bEndPinned && i + 1 == iLast );

			Vector vecFix = vecLink * ( ( flLink - flSegment ) / flLink );
			if ( bNearHeld && bFarHeld )
			{
				continue;
			}
			else if ( bNearHeld )
			{
				m_vecPos[i + 1] -= vecFix;
			}
			else if ( bFarHeld )
			{
				m_vecPos[i] += vecFix;
			}
			else
			{
				m_vecPos[i] += vecFix * 0.5f;
				m_vecPos[i + 1] -= vecFix * 0.5f;
			}
		}
	}

	// Between two held ends, a few passes over the links leave a long rope
	// looking like rubber. This doesn't: no point is further from either end
	// than the rope between the two is long.
	if ( m_bEndPinned )
	{
		for ( int i = 1; i < iLast; i++ )
		{
			Vector vecOut = m_vecPos[i] - vecRoot;
			float flOut = vecOut.Length();
			if ( flOut > flSegment * i && flOut > 0.001f )
			{
				m_vecPos[i] = vecRoot + vecOut * ( flSegment * i / flOut );
			}

			vecOut = m_vecPos[i] - m_vecEndPin;
			flOut = vecOut.Length();
			if ( flOut > flSegment * ( iLast - i ) && flOut > 0.001f )
			{
				m_vecPos[i] = m_vecEndPin + vecOut * ( flSegment * ( iLast - i ) / flOut );
			}
		}
	}

	// A point that moved into the world stops where it touched it, and drags
	CTraceFilterWorldAndPropsOnly filter;
	for ( int i = 1; i < ( m_bEndPinned ? iLast : m_nNodes ); i++ )
	{
		trace_t tr;
		UTIL_TraceLine( vecStart[i], m_vecPos[i], MASK_SOLID_BRUSHONLY, &filter, &tr );
		if ( tr.startsolid || tr.fraction == 1.0f )
			continue;

		m_vecPos[i] = tr.endpos + tr.plane.normal * 0.1f;

		// Keep some of the speed along the surface, none into it
		Vector vecVelocity = m_vecPos[i] - m_vecPrev[i];
		vecVelocity -= tr.plane.normal * DotProduct( vecVelocity, tr.plane.normal );
		m_vecPrev[i] = m_vecPos[i] - vecVelocity * ( 1.0f - ROPE_FRICTION );
	}
}

float COF2RopeSim::GetDistance( const Vector &vecPoint, float *pflAlong ) const
{
	float flBest = FLT_MAX;
	float flBestAlong = 0.0f;

	for ( int i = 0; i < m_nNodes - 1; i++ )
	{
		float t;
		float flDist = CalcDistanceToLineSegment( vecPoint, m_vecPos[i], m_vecPos[i + 1], &t );
		if ( flDist < flBest )
		{
			flBest = flDist;
			flBestAlong = ( i + t ) * m_flSegment;
		}
	}

	if ( pflAlong )
	{
		*pflAlong = flBestAlong;
	}

	return flBest;
}

