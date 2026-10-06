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

// How many surfaces a point slides along in one step before it stops
#define ROPE_SLIDES				2

COF2RopeSim::COF2RopeSim()
{
	m_nNodes = 0;
	m_flSegment = OF2_ROPE_SIM_SPACING;
	m_flDamping = ROPE_DAMPING;
	m_flTimeLeft = 0.0f;
	m_bEndPinned = false;
	m_vecEndPin.Init();
	m_vecEndPinWas.Init();
	m_vecRootWas.Init();
}

int COF2RopeSim::NodesFor( float flLength )
{
	return clamp( (int)( flLength / OF2_ROPE_SIM_SPACING ) + 2, 2, OF2_ROPE_SIM_MAX_NODES );
}

void COF2RopeSim::Seed( const Vector *pPath, int nPath, float flLength, const Vector &vecVelocity, bool bSpread )
{
	if ( nPath < 1 )
		return;

	m_nNodes = NodesFor( flLength );
	m_flSegment = MAX( flLength, 1.0f ) / ( m_nNodes - 1 );
	m_flTimeLeft = 0.0f;

	// Laid out anew, it is held only at its root until told otherwise
	m_bEndPinned = false;
	m_vecRootWas = pPath[0];

	// Walk the path, a point every m_flSegment; or, spread, every so much
	// that the last point comes out at the end of the path
	float flEvery = m_flSegment;
	if ( bSpread && nPath > 1 )
	{
		float flPath = 0.0f;
		for ( int i = 1; i < nPath; i++ )
		{
			flPath += pPath[i].DistTo( pPath[i - 1] );
		}
		flEvery = MIN( flEvery, flPath / ( m_nNodes - 1 ) );
	}

	int iLeg = 0;
	float flLegStart = 0.0f;
	for ( int i = 0; i < m_nNodes; i++ )
	{
		float flAlong = i * flEvery;

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

	if ( m_bEndPinned )
	{
		// Held at both ends: where the ends have gone, the rope between goes
		// too, each point by its share of each end's move. (Where it was a
		// step ago goes along, so this is not speed.)
		Vector vecRootMoved = vecRoot - m_vecRootWas;
		Vector vecEndMoved = m_vecEndPin - m_vecEndPinWas;
		for ( int i = 1; i < m_nNodes - 1; i++ )
		{
			float flShare = (float)i / ( m_nNodes - 1 );
			Vector vecMoved = vecRootMoved * ( 1.0f - flShare ) + vecEndMoved * flShare;
			m_vecPos[i] += vecMoved;
			m_vecPrev[i] += vecMoved;
		}

		m_vecEndPinWas = m_vecEndPin;
	}
	m_vecRootWas = vecRoot;

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

	// A few passes over the links leave a long rope looking like rubber, and
	// one that lies on a roof is hardly dragged along by a root that has gone
	// over the edge. This doesn't, and it is: no point is further from a held
	// end than the rope between the two is long.
	for ( int i = 1; i <= iLast; i++ )
	{
		if ( i == iLast && m_bEndPinned )
			break;

		Vector vecOut = m_vecPos[i] - vecRoot;
		float flOut = vecOut.Length();
		if ( flOut > flSegment * i && flOut > 0.001f )
		{
			m_vecPos[i] = vecRoot + vecOut * ( flSegment * i / flOut );
		}

		if ( m_bEndPinned )
		{
			vecOut = m_vecPos[i] - m_vecEndPin;
			flOut = vecOut.Length();
			if ( flOut > flSegment * ( iLast - i ) && flOut > 0.001f )
			{
				m_vecPos[i] = m_vecEndPin + vecOut * ( flSegment * ( iLast - i ) / flOut );
			}
		}
	}

	// A point that moved into the world goes on along what it touched, and
	// drags. (Stopped dead where it touched, a rope lying on a roof stayed
	// there when its other end was carried over the edge: every pull on it
	// goes a little into the roof.)
	CTraceFilterWorldAndPropsOnly filter;
	for ( int i = 1; i < ( m_bEndPinned ? iLast : m_nNodes ); i++ )
	{
		Vector vecFrom = vecStart[i];
		Vector vecTo = m_vecPos[i];
		bool bTouched = false;
		Vector vecNormal( 0, 0, 1 );

		for ( int k = 0; k < ROPE_SLIDES; k++ )
		{
			trace_t tr;
			UTIL_TraceLine( vecFrom, vecTo, MASK_SOLID_BRUSHONLY, &filter, &tr );
			if ( tr.startsolid || tr.fraction == 1.0f )
				break;

			bTouched = true;
			vecNormal = tr.plane.normal;

			// The rest of the way, less the part of it into the surface
			vecFrom = tr.endpos + vecNormal * 0.1f;
			Vector vecRest = vecTo - vecFrom;
			vecRest -= vecNormal * DotProduct( vecRest, vecNormal );
			vecTo = vecFrom + vecRest;
		}

		if ( !bTouched )
			continue;

		// (if the last try was cut short too, it stays where that one began)
		trace_t tr;
		UTIL_TraceLine( vecFrom, vecTo, MASK_SOLID_BRUSHONLY, &filter, &tr );
		m_vecPos[i] = ( tr.startsolid || tr.fraction == 1.0f ) ? vecTo : vecFrom;

		// Keep some of the speed along the surface, none into it
		Vector vecVelocity = m_vecPos[i] - m_vecPrev[i];
		vecVelocity -= vecNormal * DotProduct( vecVelocity, vecNormal );
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

