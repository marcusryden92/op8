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

// The pass that brings each point within reach of the one before it (see
// Step), and the share of what a point is pulled in by that the point before
// it gives up in speed. Settings rather than constants so the two can be
// compared in game: the user was not sure the rope looked better with them.
ConVar of2_rope_follow( "of2_rope_follow", "1", FCVAR_REPLICATED, "A loose rope's points are each pulled within reach of the one before, so a pull at one end reaches all of it at once and goes round what it lies over. 0: only the usual few passes over its links; it stretches more when dragged." );
ConVar of2_rope_follow_give( "of2_rope_follow_give", "0.9", FCVAR_REPLICATED, "With of2_rope_follow: how much of each such pull the point before feels. 0: none, and a hanging rope's free end thrashes. 1: all of it; the rope is at its calmest and stiffest." );

ConVar of2_rope_corners( "of2_rope_corners", "1", FCVAR_REPLICATED, "Where the straight line between two points of a rope or tongue goes through a corner (one on a floor, the next over its edge), the rope is held and drawn going round that corner. 0: straight through, as before." );
ConVar of2_rope_spacing( "of2_rope_spacing", "16", FCVAR_REPLICATED, "About how much rope there is between two points of a loose rope, which has 32 at most. Less follows what it lies on more closely. Read when a rope is laid out: on a grab, on letting go, on map start." );

// Grip, as opposed to ROPE_FRICTION, which only slows what already slides: a
// point is held where it lies by as much as it is pressed onto the surface (its
// own weight, and the pull of the rope going down over an edge), so rope lying
// on a ledge holds up some rope hanging from it. The user asked for this: with
// none, any weight at all drew it off the ledge.
ConVar of2_rope_grip( "of2_rope_grip", "1.0", FCVAR_REPLICATED, "How well a loose rope holds on to what it lies on: the most it can be moved along a surface in a step is cut by this times how far it was pressed into it. More and rope on a ledge holds more hanging weight before it slides off. 0: none, as before." );

// How many surfaces a point slides along in one step before it stops
#define ROPE_SLIDES				2

// Two faces more alike or more opposed than this have no one corner between them
#define ROPE_CORNER_MAX_DOT		0.9f

//-----------------------------------------------------------------------------
// The line is traced from both ends. Blocked both ways, the two faces hit are
// the ones the corner lies between (as COF2Tether::FindPivot works it out).
//-----------------------------------------------------------------------------
bool OF2_RopeLinkCorner( const Vector &vecFrom, const Vector &vecTo, float flStandOff, unsigned int nMask, ITraceFilter *pFilter, Vector *pCorner, Vector *pOut )
{
	float flLink = vecFrom.DistTo( vecTo );
	if ( flLink < 1.0f )
		return false;

	// (a point inside something can't tell where the way out is)
	trace_t trThere;
	UTIL_TraceLine( vecFrom, vecTo, nMask, pFilter, &trThere );
	if ( trThere.startsolid || trThere.fraction == 1.0f )
		return false;

	trace_t trBack;
	UTIL_TraceLine( vecTo, vecFrom, nMask, pFilter, &trBack );
	if ( trBack.startsolid || trBack.fraction == 1.0f )
		return false;

	const Vector &vecFace1 = trThere.plane.normal;
	const Vector &vecFace2 = trBack.plane.normal;
	float flAcross = DotProduct( vecFace1, vecFace2 );
	if ( fabs( flAcross ) > ROPE_CORNER_MAX_DOT )
		return false;

	// The point where the two faces meet that is nearest the second hit
	float flOff = DotProduct( vecFace1, trThere.endpos - trBack.endpos ) / ( 1.0f - flAcross * flAcross );
	Vector vecMeet = trBack.endpos + vecFace1 * flOff - vecFace2 * ( flOff * flAcross );

	// (nowhere near the line: the two hits were on different things)
	if ( CalcDistanceToLineSegment( vecMeet, vecFrom, vecTo ) > flLink * 0.75f )
		return false;

	*pCorner = vecMeet + ( vecFace1 + vecFace2 ) * flStandOff;

	if ( pOut )
	{
		*pOut = vecFace1 + vecFace2;
		VectorNormalize( *pOut );
	}

	return true;
}

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
	m_flDrag = 0.0f;
	m_flRadius = 0.5f;
	m_flCarry = 1.0f;
	memset( m_bCorner, 0, sizeof( m_bCorner ) );
}

int COF2RopeSim::NodesFor( float flLength )
{
	float flSpacing = clamp( of2_rope_spacing.GetFloat(), 4.0f, 64.0f );
	return clamp( (int)( flLength / flSpacing ) + 2, 2, OF2_ROPE_SIM_MAX_NODES );
}

bool COF2RopeSim::GetLinkCorner( int iLink, Vector *pCorner ) const
{
	if ( iLink < 0 || iLink >= m_nNodes - 1 || !m_bCorner[iLink] )
		return false;

	*pCorner = m_vecCorner[iLink];
	return true;
}

void COF2RopeSim::Seed( const Vector *pPath, int nPath, float flLength, const Vector &vecVelocity, bool bSpread )
{
	if ( nPath < 1 )
		return;

	m_nNodes = NodesFor( flLength );
	m_flSegment = MAX( flLength, 1.0f ) / ( m_nNodes - 1 );
	m_flTimeLeft = 0.0f;
	memset( m_bCorner, 0, sizeof( m_bCorner ) );

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
			Vector vecMoved = ( vecRootMoved * ( 1.0f - flShare ) + vecEndMoved * flShare ) * m_flCarry;
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

		Vector vecVelocity = m_vecPos[i] - m_vecPrev[i];
		float flKeep = m_flDamping;
		if ( m_flDrag > 0.0f )
		{
			flKeep /= 1.0f + m_flDrag * vecVelocity.Length();
		}
		vecVelocity *= flKeep;
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
			bool bNearHeld = ( i == 0 );
			bool bFarHeld = ( m_bEndPinned && i + 1 == iLast );

			if ( m_bCorner[i] )
			{
				// Over a corner the rope between the two is the way out to the
				// corner and on from it, and each is drawn towards the corner,
				// not through it towards the other
				Vector vecNear = m_vecCorner[i] - m_vecPos[i];
				Vector vecFar = m_vecCorner[i] - m_vecPos[i + 1];
				float flNear = VectorNormalize( vecNear );
				float flFar = VectorNormalize( vecFar );
				float flOver = flNear + flFar - flSegment;
				if ( flOver <= 0.0f || ( bNearHeld && bFarHeld ) )
					continue;

				float flNearShare = bNearHeld ? 0.0f : ( bFarHeld ? 1.0f : 0.5f );
				m_vecPos[i] += vecNear * MIN( flOver * flNearShare, flNear );
				m_vecPos[i + 1] += vecFar * MIN( flOver * ( 1.0f - flNearShare ), flFar );
				continue;
			}

			Vector vecLink = m_vecPos[i + 1] - m_vecPos[i];
			float flLink = vecLink.Length();
			if ( flLink <= flSegment || flLink < 0.001f )
				continue;

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

	// A few passes over the links leave a long rope looking like rubber.
	if ( m_bEndPinned )
	{
		// Held at both ends, this doesn't: no point is further from either end
		// than the rope between the two is long.
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
	else if ( of2_rope_follow.GetBool() )
	{
		float flGive = clamp( of2_rope_follow_give.GetFloat(), 0.0f, 1.0f );

		// Held at the root only, each point in turn is brought within reach of
		// the one before it, which stays put. So a pull at the root reaches all
		// the way down the rope in one step, and reaches each point from the
		// one before it: round whatever the rope lies over, not straight at the
		// root through it. (Held to their distance from the root itself, points
		// hanging past an edge were hauled in under the floor the root was
		// carried across; made to stay in sight of the point before, they all
		// jumped to the edge at once.)
		//
		// Done just like that, a point is moved and the one before it feels
		// nothing of it, which is not how a rope works: every little sway at
		// the root grew on its way down, and the free end thrashed about on a
		// rope that was only hanging there. So what a point is moved by is
		// taken out of the speed of the one before it (of2_rope_follow_give), as
		// if the one had pulled on the other.
		for ( int i = 1; i <= iLast; i++ )
		{
			if ( m_bCorner[i - 1] )
			{
				// Over a corner, within reach of the corner: what is left of the
				// rope between the two once it has got there from the point before.
				// (This is what keeps a hanging end from being hauled in through
				// the wall under the edge.)
				const Vector &vecCorner = m_vecCorner[i - 1];
				Vector vecOut = m_vecPos[i] - vecCorner;
				float flOut = vecOut.Length();
				float flMost = MAX( flSegment - vecCorner.DistTo( m_vecPos[i - 1] ), 0.0f );
				if ( flOut > flMost && flOut > 0.001f )
				{
					m_vecPos[i] = vecCorner + vecOut * ( flMost / flOut );

					Vector vecPull = vecCorner - m_vecPos[i - 1];
					if ( i > 1 && VectorNormalize( vecPull ) > 0.001f )
					{
						m_vecPrev[i - 1] -= vecPull * ( ( flOut - flMost ) * flGive );
					}
				}
				continue;
			}

			Vector vecLink = m_vecPos[i] - m_vecPos[i - 1];
			float flLink = vecLink.Length();
			if ( flLink > flSegment && flLink > 0.001f )
			{
				Vector vecMoved = vecLink * ( flSegment / flLink - 1.0f );
				m_vecPos[i] += vecMoved;

				if ( i > 1 )
				{
					m_vecPrev[i - 1] += vecMoved * flGive;
				}
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
		Vector vecWant = vecTo - vecFrom;
		bool bTouched = false;
		Vector vecNormal( 0, 0, 1 );

		for ( int k = 0; k < ROPE_SLIDES; k++ )
		{
			// The rope has a thickness: it stops with its middle that far off a
			// surface, so the way is looked along that much further than the
			// point goes. (Stopped only once the middle itself got there, and put
			// back out by its thickness, it would sink and hop for ever.)
			Vector vecAhead = vecTo - vecFrom;
			if ( VectorNormalize( vecAhead ) < 0.001f )
				break;

			trace_t tr;
			UTIL_TraceLine( vecFrom, vecTo + vecAhead * m_flRadius, MASK_SOLID_BRUSHONLY, &filter, &tr );
			if ( tr.startsolid || tr.fraction == 1.0f )
				break;

			bTouched = true;
			vecNormal = tr.plane.normal;

			// The rest of the way, less the part of it into the surface
			vecFrom = tr.endpos + vecNormal * m_flRadius;
			Vector vecRest = vecTo - vecFrom;
			vecRest -= vecNormal * DotProduct( vecRest, vecNormal );
			vecTo = vecFrom + vecRest;
		}

		if ( !bTouched )
			continue;

		// (if the last try was cut short too, it stays where that one began)
		trace_t tr;
		UTIL_TraceLine( vecFrom, vecTo, MASK_SOLID_BRUSHONLY, &filter, &tr );
		m_vecPos[i] = tr.startsolid ? vecStart[i] : ( ( tr.fraction == 1.0f ) ? vecTo : vecFrom );

		// Held back along the surface by how hard it was pressed onto it
		float flGrip = of2_rope_grip.GetFloat() * -DotProduct( vecWant, vecNormal );
		if ( flGrip > 0.0f )
		{
			Vector vecSlid = m_vecPos[i] - vecStart[i];
			vecSlid -= vecNormal * DotProduct( vecSlid, vecNormal );
			float flSlid = vecSlid.Length();
			if ( flSlid > 0.001f )
			{
				Vector vecHeld = m_vecPos[i] - vecSlid * MIN( flGrip / flSlid, 1.0f );

				// (only if that is a place it could have got to)
				UTIL_TraceLine( m_vecPos[i], vecHeld, MASK_SOLID_BRUSHONLY, &filter, &tr );
				if ( !tr.startsolid && tr.fraction == 1.0f )
				{
					m_vecPos[i] = vecHeld;
				}
			}
		}

		// Keep some of the speed along the surface, none into it
		Vector vecVelocity = m_vecPos[i] - m_vecPrev[i];
		vecVelocity -= vecNormal * DotProduct( vecVelocity, vecNormal );
		m_vecPrev[i] = m_vecPos[i] - vecVelocity * ( 1.0f - ROPE_FRICTION );
	}

	// The corners the rope now lies over, for the next step and for whoever
	// draws it. Looked for from where the points have come to rest, which is
	// clear of the world; a rope hanging free has none and is left as it was.
	bool bCorners = of2_rope_corners.GetBool();
	for ( int i = 0; i < iLast; i++ )
	{
		m_bCorner[i] = bCorners && OF2_RopeLinkCorner( m_vecPos[i], m_vecPos[i + 1], m_flRadius, MASK_SOLID_BRUSHONLY, &filter, &m_vecCorner[i] );
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

