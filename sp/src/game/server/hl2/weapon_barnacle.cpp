//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: Barnacle. A live barnacle in the hand. Its tongue ends in a
//			sticky tip that the player throws like a heaving line.
//
//			The tongue is not made of physics objects. It is a line with a
//			length (COF2Tether) that bends over the edges of the world, and the
//			part of it past the last bend is a row of simulated points
//			(CBarnacleRope, on Source's own rope solver) that sags, swings and
//			trails behind the tip.
//
//			What the tip lands on decides what happens next:
//			- a loose object light enough: it is the tongue's to pull. A
//			  physics motion controller keeps it within the tongue's length,
//			  from inside the physics step, by taking away its speed away from
//			  the tongue and no more. It swings and drags as it would on a line.
//			- the world, or anything too heavy or held fast: the player hangs
//			  from it instead, as from a climb rope.
//
//			While it is reeled in, the length of the tongue picks up loose
//			things it touches. It holds hardest at the tip and less and less
//			towards the barnacle, so something heavy caught halfway along is
//			dragged a little and then let go, while the same thing on the tip
//			comes all the way in.
//
//			No ammo. Primary fire throws; held again afterwards it reels in.
//			Secondary fire held pays out. Reload lets go of everything and
//			takes the tongue back in.
//
//=============================================================================//

#include "cbase.h"
#include "basehlcombatweapon.h"
#include "player.h"
#include "in_buttons.h"
#include "rope_physics.h"
#include "of2_tether.h"
#include "hl_gamemovement.h"
#include "hl2_player.h"
#include "hl2/of2_tongue_shared.h"
#include "physics.h"
#include "vphysics_interface.h"
#include "movevars_shared.h"
#include "ndebugoverlay.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define BARNACLE_TONGUE_WIDTH		2.0f

// What the barnacle in the hand does when nothing else is going on
#define BARNACLE_IDLE_SEQUENCE		"idle01"
// Throwing: the barnacle swallowing, played backwards
#define BARNACLE_THROW_SEQUENCE		"slurp"

// Most things the length of the tongue can hold at once (of2_barnacle_collect_count)
#define BARNACLE_MAX_JUNK			8

// The tongue's whole path: the simulated points, the bends, the barnacle
#define BARNACLE_MAX_PATH			( OF2_TONGUE_NODES + OF2_TETHER_MAX_PIVOTS + 2 )

// The tip is swept through the world as a box this big, so it doesn't slip past small things
#define BARNACLE_TIP_SIZE			2.0f

// Share of its speed a point of the tongue keeps from one tick to the next
#define BARNACLE_ROPE_DAMPING		0.998f
// How many times a tick the tongue's points are pulled back to their spacing
#define BARNACLE_ROPE_ITERATIONS	12
// Share of its speed along a surface a point of the tongue loses on touching it
#define BARNACLE_ROPE_FRICTION		0.4f

// A tip that came off something doesn't stick again for this long, and something
// the tongue let go of isn't picked up again for this long
#define BARNACLE_RESTICK_TIME		0.75f
#define BARNACLE_RECATCH_TIME		2.0f
#define BARNACLE_MAX_DROPPED		4

// A free tip reeled in this close goes back into the barnacle. With something
// on the tongue, reeling stops this far out instead.
#define BARNACLE_ABSORB_DIST		20.0f
#define BARNACLE_MIN_LENGTH			40.0f

// Reeling takes up slack this many times faster than it shortens a taut tongue
#define BARNACLE_SLACK_REEL			3.0f

// Retracting gives up and takes the tongue in at once after this many times of2_barnacle_retract_time
#define BARNACLE_RETRACT_PATIENCE	2.0f

// A player moved up onto an edge, or standing by where the tongue is fixed,
// gets this much more tongue than it takes to reach
#define BARNACLE_MANTLE_SLACK		4.0f

// Reeled all the way in on the ground, the player counts as there within this
// much of the shortest the tongue goes
#define BARNACLE_RELEASE_DIST		16.0f

// Something on the length of the tongue is held this close to its place on it,
// and pulled back there this hard (speed per unit away, and the most speed)
#define BARNACLE_JUNK_SLACK			2.0f
#define BARNACLE_JUNK_PULL			20.0f
#define BARNACLE_JUNK_PULL_SPEED	400.0f

// The tip is more patient than the rest of the tongue with what it holds:
// this many times of2_barnacle_snag_dist and of2_barnacle_snag_time
#define BARNACLE_TIP_SNAG_DIST		1.5f
#define BARNACLE_TIP_SNAG_TIME		2.0f

ConVar of2_barnacle_max_length( "of2_barnacle_max_length", "900", FCVAR_NONE, "Length of the Barnacle's whole tongue. Read when thrown." );
ConVar of2_barnacle_throw_speed( "of2_barnacle_throw_speed", "2000", FCVAR_NONE, "Speed the Barnacle's tip is thrown at." );
ConVar of2_barnacle_throw_up( "of2_barnacle_throw_up", "120", FCVAR_NONE, "Upward speed added to the Barnacle's throw." );
ConVar of2_barnacle_throw_slack( "of2_barnacle_throw_slack", "0.01", FCVAR_NONE, "How much more tongue than the tip needs comes out behind it during the throw, as a share. 0 is a dead straight line." );
ConVar of2_barnacle_tip_weight( "of2_barnacle_tip_weight", "150", FCVAR_NONE, "The Barnacle's tip weighs this many times one point of its tongue. Heavier, the tongue trailing behind slows the throw less." );
ConVar of2_barnacle_rope_drag( "of2_barnacle_rope_drag", "1.5", FCVAR_NONE, "Air drag on the Barnacle's tongue (not its tip), so it hangs back in an arc and settles instead of whipping about." );
ConVar of2_barnacle_anchor_mass( "of2_barnacle_anchor_mass", "85", FCVAR_NONE, "Something heavier than this on the Barnacle's tip holds the player instead of being pulled: they hang from it as from a rope. The player weighs about 85." );
ConVar of2_barnacle_reel_speed( "of2_barnacle_reel_speed", "400", FCVAR_NONE, "Speed the Barnacle reels its tongue in (primary fire, held), pulling the player up when it is anchored." );
ConVar of2_barnacle_extrude_speed( "of2_barnacle_extrude_speed", "200", FCVAR_NONE, "Speed the Barnacle pays its tongue out (secondary fire, held), lowering the player when it is anchored." );
ConVar of2_barnacle_retract_time( "of2_barnacle_retract_time", "0.1", FCVAR_NONE, "How long the Barnacle takes to haul its tongue back in on reload." );
ConVar of2_barnacle_pump( "of2_barnacle_pump", "120", FCVAR_NONE, "How hard forward/back pushes the swing while hanging from the Barnacle (scaled by of2_tether_pump_scale, like a climb rope's pump strength)." );
ConVar of2_barnacle_pull( "of2_barnacle_pull", "10", FCVAR_NONE, "How hard the Barnacle's tongue pulls back what is on its tip when stretched: speed per unit over its length." );
ConVar of2_barnacle_pull_max_speed( "of2_barnacle_pull_max_speed", "300", FCVAR_NONE, "Fastest the Barnacle's tongue pulls back what is on its tip (on top of the speed it is reeled in at)." );
ConVar of2_barnacle_stick_tip( "of2_barnacle_stick_tip", "170", FCVAR_NONE, "How well the Barnacle's tongue holds on at its tip: the weight it can pull at full strength. Something twice as heavy it can still drag, but not lift." );
ConVar of2_barnacle_stick_mouth( "of2_barnacle_stick_mouth", "8", FCVAR_NONE, "How well the Barnacle's tongue holds on at the barnacle's end, as of2_barnacle_stick_tip. In between it falls off from the one to the other." );
ConVar of2_barnacle_stick_curve( "of2_barnacle_stick_curve", "2", FCVAR_NONE, "How the Barnacle's grip falls off along the tongue. 1 is evenly from tip to barnacle; higher keeps the strength near the tip and loses it sooner." );
ConVar of2_barnacle_stick_accel( "of2_barnacle_stick_accel", "1200", FCVAR_NONE, "Full strength for the Barnacle's tongue: how fast it can speed up the weight it holds well (gravity is 600)." );
ConVar of2_barnacle_collect_radius( "of2_barnacle_collect_radius", "12", FCVAR_NONE, "While reeling in, the Barnacle's tongue picks up loose things within this distance of it." );
ConVar of2_barnacle_collect_mass( "of2_barnacle_collect_mass", "85", FCVAR_NONE, "Heaviest single thing the length of the Barnacle's tongue picks up." );
ConVar of2_barnacle_collect_total_mass( "of2_barnacle_collect_total_mass", "200", FCVAR_NONE, "Most weight the length of the Barnacle's tongue carries, all told. Over that nothing more sticks." );
ConVar of2_barnacle_collect_count( "of2_barnacle_collect_count", "8", FCVAR_NONE, "Most things the length of the Barnacle's tongue carries (up to 8)." );
ConVar of2_barnacle_snag_dist( "of2_barnacle_snag_dist", "32", FCVAR_NONE, "Something the Barnacle's tongue holds that falls this far behind its place on the tongue is slipping." );
ConVar of2_barnacle_snag_time( "of2_barnacle_snag_time", "0.5", FCVAR_NONE, "Something that has been slipping off the Barnacle's tongue for this long is let go." );
ConVar of2_barnacle_sag( "of2_barnacle_sag", "0.1", FCVAR_NONE, "How much the weight of what the Barnacle's tongue carries pulls the tongue down there (per unit of mass)." );
ConVar of2_barnacle_spacing( "of2_barnacle_spacing", "20", FCVAR_NONE, "The loose part of the Barnacle's tongue is simulated as a point about every this much of its length (up to 48 points), so it is as stiff and hangs the same way whether it is short or long. Smaller is suppler." );
ConVar of2_barnacle_settle_speed( "of2_barnacle_settle_speed", "300", FCVAR_NONE, "The Barnacle's tongue is thrown out along the arc its tip flies, which is longer than the straight line to where the tip ends up. Once the throw is over that slack is taken up at this speed." );
ConVar of2_barnacle_drape( "of2_barnacle_drape", "1", FCVAR_NONE, "The length of the Barnacle's tongue lies on and drapes over loose physics objects, as it does over the world. 0: only over the world." );
ConVar of2_barnacle_slide_speed( "of2_barnacle_slide_speed", "120", FCVAR_NONE, "Where the Barnacle's tongue goes over an edge it slides along the edge, towards where it pulls on it evenly. This is its speed at the hardest pull. 0: it stays where it first touched." );
ConVar of2_barnacle_slide_friction( "of2_barnacle_slide_friction", "0.35", FCVAR_NONE, "How sticky the Barnacle's tongue is on an edge: how lopsided the pull has to be before it slides along it (0 always, 1 only when dragged almost straight along it). A climb rope has its own, lower." );
ConVar of2_barnacle_auto_release( "of2_barnacle_auto_release", "1", FCVAR_NONE, "The Barnacle takes its tongue back by itself once the player stands on the ground and has reeled in all there is to reel." );
ConVar of2_barnacle_debug( "of2_barnacle_debug", "0", FCVAR_NONE, "Draw the Barnacle's tongue as debug overlays: its line and bends, its points, and what it holds." );

static const char *s_pTongueContext = "BarnacleTongueThink";

//-----------------------------------------------------------------------------
// An entity can be made of several physics objects (a ragdoll)
//-----------------------------------------------------------------------------
static bool HasPhysicsObject( CBaseEntity *pEntity, IPhysicsObject *pPhysics )
{
	IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int nCount = pEntity->VPhysicsGetObjectList( pList, ARRAYSIZE( pList ) );
	for ( int i = 0; i < nCount; i++ )
	{
		if ( pList[i] == pPhysics )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// What the tip sticks to: the world, props and anything else solid. Not the
// player or NPCs. Loose physics objects count whatever collision group they
// are in, or the smallest junk would be the hardest to catch.
//-----------------------------------------------------------------------------
class CTraceFilterTongueTip : public CTraceFilterSimple
{
public:
	CTraceFilterTongueTip( const IHandleEntity *pIgnore ) : CTraceFilterSimple( pIgnore, COLLISION_GROUP_NONE ) {}

	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		if ( pEntity )
		{
			if ( pEntity->IsPlayer() || pEntity->IsNPC() )
				return false;

			if ( pEntity->GetMoveType() == MOVETYPE_VPHYSICS && pEntity->IsSolid() )
				return pHandleEntity != GetPassEntity();
		}

		return CTraceFilterSimple::ShouldHitEntity( pHandleEntity, contentsMask );
	}
};

//-----------------------------------------------------------------------------
// What the length of the tongue lies on and drapes over: the world, static
// props and loose physics objects. Not what the tongue itself holds, which
// hangs on it and would otherwise push it about.
//-----------------------------------------------------------------------------
class CTraceFilterTongueDrape : public CTraceFilter
{
public:
	CTraceFilterTongueDrape( CBaseEntity *const *pIgnore, int nIgnore, bool bPhysics )
	{
		m_pIgnore = pIgnore;
		m_nIgnore = nIgnore;
		m_bPhysics = bPhysics;
	}

	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		// (a static prop is not an entity)
		CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		if ( pEntity == NULL || pEntity->IsWorld() )
			return true;

		if ( !m_bPhysics || pEntity->GetMoveType() != MOVETYPE_VPHYSICS || !pEntity->IsSolid() )
			return false;

		if ( pEntity->IsPlayer() || pEntity->IsNPC() )
			return false;

		for ( int i = 0; i < m_nIgnore; i++ )
		{
			if ( m_pIgnore[i] == pEntity )
				return false;
		}

		return true;
	}

private:
	CBaseEntity *const *m_pIgnore;
	int		m_nIgnore;
	bool	m_bPhysics;
};

//-----------------------------------------------------------------------------
// The loose part of the tongue: a row of points from where it starts (the
// barnacle, or the last edge the tongue bends over) to the tip. Source's rope
// solver moves the points; the rules for them are here.
//
// The first point is held at the start. The tip is either held too (stuck to
// something) or free: thrown, dangling, or being reeled in.
//-----------------------------------------------------------------------------
class CBarnacleRope : public CRopePhysics<OF2_TONGUE_NODES>
{
public:
	CBarnacleRope();

	// All of it at one point, the tip leaving at vecTipVelocity
	void	Seed( const Vector &vecStart, const Vector &vecTipVelocity );

	// The start has jumped to a new bend (bNewBend), or back to the one before:
	// the points are laid out again along the way the tongue lies now
	void	Rebase( const Vector &vecStart, bool bNewBend );

	// One tick. flLength is how much tongue there is from the start to the tip.
	// pTipPin is where the tip is held, or NULL if it is free.
	void	Step( const Vector &vecStart, float flLength, const Vector *pTipPin );

	// How many points it is made of now. That goes by its length: a point
	// about every of2_barnacle_spacing, so that a short tongue is as stiff
	// and hangs the same way as that much of a long one.
	int		GetCount( void ) const				{ return m_nCount; }
	const Vector &GetPos( int iNode ) const		{ return m_Nodes[iNode].m_vPos; }
	const Vector &GetTip( void ) const			{ return m_Nodes[m_nCount - 1].m_vPos; }
	Vector	GetTipVelocity( void ) const		{ return ( m_Nodes[m_nCount - 1].m_vPos - m_Nodes[m_nCount - 1].m_vPrevPos ) / TICK_INTERVAL; }
	float	GetSegment( void ) const			{ return m_flSegment; }
	// How long it is as it lies now, point to point
	float	GetLength( void ) const;

	// A free tip touched something this tick
	bool	GetTipHit( trace_t *pTrace ) const	{ if ( m_bTipHit ) *pTrace = m_TipHit; return m_bTipHit; }

	virtual void GetNodeForces( CSimplePhysics::CNode *pNodes, int iNode, Vector *pAccel );
	virtual void ApplyConstraints( CSimplePhysics::CNode *pNodes, int nNodes );

public:
	// A free tip sticks to what it touches (and says so through GetTipHit)
	bool	m_bSticky;
	// The points stop at the world
	bool	m_bCollide;
	// The tip's trace ignores this
	CBaseEntity *m_pIgnore;
	// The other points pass through these: what the tongue holds
	CBaseEntity *m_pDrapeIgnore[BARNACLE_MAX_JUNK + 1];
	int		m_nDrapeIgnore;
	// The throw. Each new bit of tongue leaves the start at m_vecFeedVelocity and
	// then flies free, as the tip did before it, so all of the tongue follows the
	// arc the tip took. Meanwhile the points are not held to each other and not
	// slowed by the air; that starts when the throw is over, and the tongue settles.
	bool	m_bFeeding;
	Vector	m_vecFeedVelocity;
	// How readily each point gives way to its neighbours: 1 for a bare point of
	// tongue, less for one with something heavy on it. Set before every Step.
	float	m_flInvMass[OF2_TONGUE_NODES];

private:
	// Lays nCount points evenly along the way the tongue lies now, moving as it
	// does. pStart: it starts from there instead (see Rebase).
	void	Relay( const Vector *pStart, bool bNewBend, int nCount );

	int		m_nCount;
	Vector	m_vecStart;
	Vector	m_vecTipPin;
	bool	m_bTipPinned;
	float	m_flSegment;

	bool	m_bTipHit;
	trace_t	m_TipHit;
};

CBarnacleRope::CBarnacleRope()
{
	m_bSticky = false;
	m_bCollide = true;
	m_pIgnore = NULL;
	m_nDrapeIgnore = 0;
	m_bFeeding = false;
	m_vecFeedVelocity.Init();
	m_nCount = 2;
	m_vecStart.Init();
	m_vecTipPin.Init();
	m_bTipPinned = false;
	m_flSegment = 1.0f;
	m_bTipHit = false;

	for ( int i = 0; i < OF2_TONGUE_NODES; i++ )
	{
		m_flInvMass[i] = 1.0f;
	}
}

void CBarnacleRope::Seed( const Vector &vecStart, const Vector &vecTipVelocity )
{
	// Just the two ends to begin with; more points come as it gets longer
	m_nCount = 2;
	for ( int i = 0; i < OF2_TONGUE_NODES; i++ )
	{
		m_Nodes[i].Init( vecStart );
	}

	m_Nodes[m_nCount - 1].m_vPrevPos = vecStart - vecTipVelocity * TICK_INTERVAL;

	m_vecStart = vecStart;
	m_bTipPinned = false;
	m_bTipHit = false;
}

float CBarnacleRope::GetLength( void ) const
{
	float flLength = 0.0f;
	for ( int i = 1; i < m_nCount; i++ )
	{
		flLength += m_Nodes[i].m_vPos.DistTo( m_Nodes[i - 1].m_vPos );
	}

	return flLength;
}

void CBarnacleRope::Rebase( const Vector &vecStart, bool bNewBend )
{
	Relay( &vecStart, bNewBend, m_nCount );
	m_vecStart = vecStart;
}

void CBarnacleRope::Relay( const Vector *pStart, bool bNewBend, int nCount )
{
	Vector vecPos[OF2_TONGUE_NODES + 1];
	Vector vecMove[OF2_TONGUE_NODES + 1];
	int nSource = 0;
	int iFirst = 0;

	if ( pStart )
	{
		vecPos[nSource] = *pStart;
		vecMove[nSource] = vec3_origin;
		nSource++;
	}

	if ( pStart && bNewBend )
	{
		// Where the run of points comes nearest the new start
		float flBest = FLT_MAX;
		int iBest = 0;
		for ( int i = 0; i < m_nCount - 1; i++ )
		{
			float flDist = CalcDistanceToLineSegment( *pStart, m_Nodes[i].m_vPos, m_Nodes[i + 1].m_vPos );
			if ( flDist < flBest )
			{
				flBest = flDist;
				iBest = i;
			}
		}

		// The tongue has come to lie over an edge. What is before that place on
		// it is no longer loose, however far off the edge the points are (thrown
		// in an arc, they can be well above it): kept, the tongue would run from
		// the bend back to where it used to start and out again. The old start
		// goes in any case. (A bend that went is the other way round: the start
		// is further back, and the tongue runs straight from there to where it
		// used to start, so every point stays.)
		iFirst = iBest + 1;
	}

	for ( int i = iFirst; i < m_nCount; i++ )
	{
		vecPos[nSource] = m_Nodes[i].m_vPos;
		vecMove[nSource] = m_Nodes[i].m_vPos - m_Nodes[i].m_vPrevPos;
		nSource++;
	}

	if ( nSource < 2 )
	{
		// (the new start is past everything but the tip)
		vecPos[1] = vecPos[0];
		vecMove[1] = vecMove[0];
		nSource = 2;
	}

	if ( m_bFeeding )
	{
		// What is just coming out of the start moves as the throw does, so the
		// points laid between there and the first one out get that too
		vecMove[0] = m_vecFeedVelocity * TICK_INTERVAL;
	}

	float flTotal = 0.0f;
	for ( int i = 1; i < nSource; i++ )
	{
		flTotal += vecPos[i].DistTo( vecPos[i - 1] );
	}

	// The same shape and movement, the points evenly along it
	nCount = clamp( nCount, 2, OF2_TONGUE_NODES );

	int iLeg = 0;
	float flLegStart = 0.0f;
	for ( int i = 0; i < nCount; i++ )
	{
		float flAlong = flTotal * i / ( nCount - 1 );

		while ( iLeg < nSource - 2 && flAlong > flLegStart + vecPos[iLeg].DistTo( vecPos[iLeg + 1] ) )
		{
			flLegStart += vecPos[iLeg].DistTo( vecPos[iLeg + 1] );
			iLeg++;
		}

		float flLeg = vecPos[iLeg].DistTo( vecPos[iLeg + 1] );
		float t = ( flLeg > 0.001f ) ? clamp( ( flAlong - flLegStart ) / flLeg, 0.0f, 1.0f ) : 0.0f;
		if ( i == nCount - 1 )
		{
			// The tip stays exactly where it is
			iLeg = nSource - 2;
			t = 1.0f;
		}

		Vector vecAt = vecPos[iLeg] + ( vecPos[iLeg + 1] - vecPos[iLeg] ) * t;
		Vector vecMoved = vecMove[iLeg] + ( vecMove[iLeg + 1] - vecMove[iLeg] ) * t;

		m_Nodes[i].m_vPos = vecAt;
		m_Nodes[i].m_vPrevPos = vecAt - vecMoved;
		m_Nodes[i].m_vPredicted = vecAt;
	}

	m_nCount = nCount;
}

void CBarnacleRope::Step( const Vector &vecStart, float flLength, const Vector *pTipPin )
{
	m_vecStart = vecStart;
	m_bTipPinned = ( pTipPin != NULL );
	if ( pTipPin )
	{
		m_vecTipPin = *pTipPin;

		// Something on the tip can lag behind the tongue's length for a moment.
		// The points are spread over the gap rather than fought over by both ends.
		flLength = MAX( flLength, vecStart.DistTo( m_vecTipPin ) );
	}

	flLength = MAX( flLength, 1.0f );

	// A point about every so much tongue. Not changed for every little
	// difference in length: only once the spacing is well off.
	float flSpacing = MAX( of2_barnacle_spacing.GetFloat(), 4.0f );
	float flLinks = flLength / flSpacing;
	int nWanted = clamp( (int)( flLinks + 0.5f ) + 1, 2, OF2_TONGUE_NODES );
	if ( nWanted != m_nCount && fabs( flLinks - ( m_nCount - 1 ) ) > 0.75f )
	{
		Relay( NULL, false, nWanted );
	}

	m_flSegment = flLength / ( m_nCount - 1 );
	m_bTipHit = false;

	// Exactly one step: the solver counts time up in a way that can otherwise
	// round to none, or two, in a tick
	m_Physics.Init( TICK_INTERVAL );
	m_Physics.Simulate( m_Nodes, m_nCount, this, TICK_INTERVAL, BARNACLE_ROPE_DAMPING );
}

void CBarnacleRope::GetNodeForces( CSimplePhysics::CNode *pNodes, int iNode, Vector *pAccel )
{
	if ( iNode == 0 || ( iNode == m_nCount - 1 && m_bTipPinned ) )
	{
		pAccel->Init();
		return;
	}

	// (CSimplePhysics moves a point by half of this times the step squared.
	// Doubled, things fall and fly as everything else in the game does.)
	pAccel->Init( 0.0f, 0.0f, -2.0f * GetCurrentGravity() );

	// The tip is not held back by the air; that would only shorten the throw
	float flDrag = of2_barnacle_rope_drag.GetFloat();
	if ( iNode < m_nCount - 1 && flDrag > 0.0f && !m_bFeeding )
	{
		Vector vecVelocity = ( pNodes[iNode].m_vPos - pNodes[iNode].m_vPrevPos ) / TICK_INTERVAL;
		*pAccel -= vecVelocity * ( 2.0f * flDrag );
	}
}

void CBarnacleRope::ApplyConstraints( CSimplePhysics::CNode *pNodes, int nNodes )
{
	int iTip = nNodes - 1;

	// During the throw nothing holds the points to each other (see m_bFeeding):
	// only the start stays where it is
	int nPasses = m_bFeeding ? 0 : BARNACLE_ROPE_ITERATIONS;
	pNodes[0].m_vPos = m_vecStart;

	// Neighbours no further apart than the tongue between them. Slack is fine.
	// Each gives way by its share: a held point not at all, one with something
	// heavy on it less than a bare one.
	for ( int k = 0; k < nPasses; k++ )
	{
		pNodes[0].m_vPos = m_vecStart;
		if ( m_bTipPinned )
		{
			pNodes[iTip].m_vPos = m_vecTipPin;
		}

		for ( int i = 0; i < iTip; i++ )
		{
			Vector vecLink = pNodes[i + 1].m_vPos - pNodes[i].m_vPos;
			float flLink = vecLink.Length();
			if ( flLink <= m_flSegment || flLink < 0.001f )
				continue;

			float flNear = ( i == 0 ) ? 0.0f : m_flInvMass[i];
			float flFar = ( i + 1 == iTip && m_bTipPinned ) ? 0.0f : m_flInvMass[i + 1];
			if ( flNear + flFar <= 0.0f )
				continue;

			Vector vecFix = vecLink * ( ( flLink - m_flSegment ) / flLink );
			pNodes[i].m_vPos += vecFix * ( flNear / ( flNear + flFar ) );
			pNodes[i + 1].m_vPos -= vecFix * ( flFar / ( flNear + flFar ) );
		}
	}

	// A few passes over the links leave a long tongue with a heavy tip
	// stretchy. This doesn't: no point is further from a held end than the
	// tongue between the two is long.
	for ( int i = 1; i <= iTip && !m_bFeeding; i++ )
	{
		if ( i == iTip && m_bTipPinned )
			break;

		Vector vecOut = pNodes[i].m_vPos - m_vecStart;
		float flOut = vecOut.Length();
		float flMost = m_flSegment * i;
		if ( flOut > flMost && flOut > 0.001f )
		{
			pNodes[i].m_vPos = m_vecStart + vecOut * ( flMost / flOut );
		}
	}

	if ( m_bTipPinned )
	{
		for ( int i = 1; i < iTip; i++ )
		{
			Vector vecOut = pNodes[i].m_vPos - m_vecTipPin;
			float flOut = vecOut.Length();
			float flMost = m_flSegment * ( iTip - i );
			if ( flOut > flMost && flOut > 0.001f )
			{
				pNodes[i].m_vPos = m_vecTipPin + vecOut * ( flMost / flOut );
			}
		}
	}

	// A free, sticky tip holds on to the first thing in its way
	bool bTipFree = !m_bTipPinned;
	if ( bTipFree && m_bSticky )
	{
		Vector vecSize( BARNACLE_TIP_SIZE, BARNACLE_TIP_SIZE, BARNACLE_TIP_SIZE );
		CTraceFilterTongueTip filter( m_pIgnore );

		trace_t tr;
		UTIL_TraceHull( pNodes[iTip].m_vPrevPos, pNodes[iTip].m_vPos, -vecSize, vecSize, MASK_SOLID, &filter, &tr );
		if ( !tr.startsolid && tr.fraction < 1.0f )
		{
			pNodes[iTip].m_vPos = tr.endpos;
			pNodes[iTip].m_vPrevPos = tr.endpos;

			m_TipHit = tr;
			m_bTipHit = true;

			// Should the solver run a second step this tick, it stays there
			m_vecTipPin = tr.endpos;
			m_bTipPinned = true;
		}
	}

	if ( !m_bCollide )
		return;

	// A point that moved into something stops where it touched it, and drags:
	// the tongue lies on the ground and drapes over what is in its way. This
	// comes last, so a tongue pulled tight over a crate is drawn going over it
	// even though the line it pulls along (the tether, which only bends around
	// the world and static props) runs straight through.
	CTraceFilterTongueDrape filter( m_pDrapeIgnore, m_nDrapeIgnore, of2_barnacle_drape.GetBool() );
	int iLast = ( bTipFree && !m_bSticky ) ? iTip : iTip - 1;
	for ( int i = 1; i <= iLast; i++ )
	{
		trace_t tr;
		UTIL_TraceLine( pNodes[i].m_vPrevPos, pNodes[i].m_vPos, MASK_SOLID, &filter, &tr );
		if ( tr.startsolid || tr.fraction == 1.0f )
			continue;

		// (its middle rests half its thickness off the surface)
		pNodes[i].m_vPos = tr.endpos + tr.plane.normal * ( BARNACLE_TONGUE_WIDTH * 0.5f );

		// Keep some of the speed along the surface, none into it
		Vector vecMoved = pNodes[i].m_vPos - pNodes[i].m_vPrevPos;
		vecMoved -= tr.plane.normal * DotProduct( vecMoved, tr.plane.normal );
		pNodes[i].m_vPrevPos = pNodes[i].m_vPos - vecMoved * ( 1.0f - BARNACLE_ROPE_FRICTION );
	}
}

//-----------------------------------------------------------------------------
// The tongue as the client draws it (client\hl2\c_of2_tongue.cpp): the points
// of its loose part, and the places between that and the barnacle where it
// bends over an edge. The barnacle end the client finds itself, in the
// player's hand.
//-----------------------------------------------------------------------------
class COF2Tongue : public CBaseEntity
{
	DECLARE_CLASS( COF2Tongue, CBaseEntity );

public:
	DECLARE_SERVERCLASS();

	int		UpdateTransmitState( void ) { return SetTransmitState( FL_EDICT_ALWAYS ); }

	// The weapon takes its tongue back in on a load rather than restore it half done
	int		ObjectCaps( void ) { return BaseClass::ObjectCaps() | FCAP_DONT_SAVE; }

	// The loose part, from the tip back to where it starts. Counted from the tip so
	// that a point keeps its place in the list when the tongue gains or loses one
	// (they come and go at the start), and the client can move each smoothly from
	// one tick to the next. The places past m_nNodes are all at the start.
	CNetworkArray( Vector, m_vecNodes, OF2_TONGUE_NODES );
	CNetworkVar( int, m_nNodes );
	// From where the loose part starts back to the barnacle
	CNetworkArray( Vector, m_vecBends, OF2_TONGUE_MAX_BENDS );
	CNetworkVar( int, m_nBends );
	CNetworkVar( float, m_flWidth );
};

LINK_ENTITY_TO_CLASS( of2_tongue, COF2Tongue );

IMPLEMENT_SERVERCLASS_ST( COF2Tongue, DT_OF2Tongue )
	SendPropArray3( SENDINFO_ARRAY3( m_vecNodes ), SendPropVector( SENDINFO_ARRAY( m_vecNodes ), 0, SPROP_NOSCALE ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_vecBends ), SendPropVector( SENDINFO_ARRAY( m_vecBends ), 0, SPROP_NOSCALE ) ),
	SendPropInt( SENDINFO( m_nNodes ), 7, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_nBends ), 6, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO( m_flWidth ) ),
END_SEND_TABLE()

//-----------------------------------------------------------------------------
// Something the tongue holds on to, and what the physics step needs to know
// to keep it there (CWeaponBarnacle::Simulate)
//-----------------------------------------------------------------------------
struct BarnacleHold_t
{
	EHANDLE			hEntity;
	IPhysicsObject	*pPhysics;
	// The motion controller has it (not so for what the player hangs from)
	bool			bControlled;
	// Where the tongue has hold of it, in the object's own space
	Vector			vecLocal;
	// How far along the tongue from the tip
	float			flAlong;
	float			flMass;

	// It may be up to flLength from vecPull, which itself moves at
	// vecPullVelocity; flLengthRate is how fast flLength is changing
	Vector			vecPull;
	Vector			vecPullVelocity;
	float			flLength;
	float			flLengthRate;
	// Pulled back at flGain speed per unit over, up to flMaxSpeed, with no more than flMaxForce
	float			flGain;
	float			flMaxSpeed;
	float			flMaxForce;
	bool			bPullSet;

	// Since when it has been slipping; 0 if it isn't
	float			flSlipTime;
};

enum BarnacleTip_t
{
	// Thrown, dangling or being reeled in
	TIP_FREE = 0,
	// On something the tongue pulls
	TIP_PAYLOAD,
	// On something that holds the player
	TIP_ANCHORED,
};

//-----------------------------------------------------------------------------
// CWeaponBarnacle
//-----------------------------------------------------------------------------
class CWeaponBarnacle : public CBaseHLCombatWeapon, public IMotionEvent
{
	DECLARE_CLASS( CWeaponBarnacle, CBaseHLCombatWeapon );

public:
	CWeaponBarnacle();

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	void	Precache( void );
	bool	Deploy( void );
	void	WeaponIdle( void );
	void	ItemPostFrame( void );
	bool	Holster( CBaseCombatWeapon *pSwitchingTo = NULL );
	void	Drop( const Vector &vecVelocity );
	void	UpdateOnRemove( void );
	void	OnRestore( void );

	bool	CanBePickedUpByNPCs( void ) { return false; }

	// IMotionEvent: called for each thing the tongue holds, inside the physics step
	virtual simresult_e Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular );

private:
	float	PlaySequence( const char *pszSequence, float flRate = 1.0f, bool bRestart = false );
	void	Throw( void );
	void	ResetTongue( void );
	void	StartRetract( void );
	void	TongueThink( void );

	// Where the tongue comes out of the barnacle
	Vector	GetMouthPos( CBasePlayer *pOwner );

	// The tip
	bool	GetTipPin( Vector *pPin );
	void	StickTip( CHL2_Player *pPlayer, const trace_t &tr );
	void	FreeTip( void );
	void	CheckTip( void );

	// Not anchored: paying out, reeling, retracting. False if the tongue is all back in.
	bool	UpdateLoose( CBasePlayer *pOwner, const Vector &vecMouth, const Vector &vecTip );

	// Hanging from the tip
	void	Anchor( CHL2_Player *pPlayer, const Vector &vecTip );
	void	UpdateAnchored( CHL2_Player *pPlayer, const Vector &vecTip );

	// The tongue's whole path, from the tip in to the barnacle
	int		GetPath( Vector *pPath ) const;
	static Vector GetPathPoint( const Vector *pPath, int nPath, float flAlong );

	// What the tongue holds
	float	GetGrip( float flAlong ) const;
	void	HoldObject( BarnacleHold_t &hold );
	void	ReleaseHold( BarnacleHold_t &hold );
	bool	IsSlipping( BarnacleHold_t &hold, float flDist, float flTime );
	void	UpdateHolds( CBasePlayer *pOwner );
	void	CollectJunk( void );
	bool	CanCollect( CBaseEntity *pEntity );
	void	DropJunk( int iJunk, bool bRemember );
	void	WeighTongue( void );

	// Tells the client what to draw the tongue through
	void	UpdateTongue( void );
	void	DebugDraw( void );

private:
	COF2Tether		m_Tether;
	CBarnacleRope	m_Rope;
	CHandle<COF2Tongue>	m_hTongue;

	bool		m_bTongueOut;
	float		m_flMaxLength;
	// Still on its way out from the throw: the tongue follows the tip freely
	bool		m_bPayingOut;
	// The velocity the tip was thrown at; the rest of the tongue is fed out at the same
	Vector		m_vecLaunchVelocity;
	bool		m_bWasFeeding;
	// What the throw left the tongue longer than it needs to be; taken up as it settles
	float		m_flSlack;
	// The tether's bends at the last tick; a change moves where the loose part starts
	int			m_nLastPivots;

	int			m_iTip;
	// What the tip is on, if it is not the world, and where on it (m_TipHold.vecLocal)
	bool		m_bTipOnEntity;
	Vector		m_vecTipFixed;
	BarnacleHold_t m_TipHold;
	float		m_flTipStickTime;

	// Anchored: m_flLeash is how far the player can get from the tip, along
	// the tongue; m_flLeashMax all the tongue there is.
	float		m_flLeash;
	float		m_flLeashMax;

	// What the length of the tongue has picked up
	BarnacleHold_t m_Junk[BARNACLE_MAX_JUNK];
	int			m_nJunk;
	// ...and has let go of lately, so it isn't picked straight up again
	EHANDLE		m_hDropped[BARNACLE_MAX_DROPPED];
	float		m_flDroppedTime[BARNACLE_MAX_DROPPED];
	int			m_iNextDropped;

	IPhysicsMotionController *m_pController;

	bool		m_bRetracting;
	float		m_flRetractTime;
	float		m_flRetractSpeed;

	// How fast the tongue's length is changing this tick (reeling is negative)
	float		m_flLengthRate;

	// The buttons, as ItemPostFrame last saw them. Reeling needs a fresh press
	// after the throw, or holding the button through it would reel straight back.
	bool		m_bReeling;
	bool		m_bExtruding;
	bool		m_bAttackReleased;
};

LINK_ENTITY_TO_CLASS( weapon_barnacle, CWeaponBarnacle );

PRECACHE_WEAPON_REGISTER( weapon_barnacle );

IMPLEMENT_SERVERCLASS_ST( CWeaponBarnacle, DT_WeaponBarnacle )
END_SEND_TABLE()

BEGIN_DATADESC( CWeaponBarnacle )

	DEFINE_FIELD( m_hTongue,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_bTongueOut,		FIELD_BOOLEAN ),

	// The motion controller and the tongue's points can't be saved, so a loaded
	// game starts with the tongue in (OnRestore). Everything else about a
	// tongue that is out goes with it.

	DEFINE_THINKFUNC( TongueThink ),

END_DATADESC()

static void ClearHold( BarnacleHold_t &hold )
{
	hold.hEntity = NULL;
	hold.pPhysics = NULL;
	hold.bControlled = false;
	hold.vecLocal.Init();
	hold.flAlong = 0.0f;
	hold.flMass = 0.0f;
	hold.vecPull.Init();
	hold.vecPullVelocity.Init();
	hold.flLength = 0.0f;
	hold.flLengthRate = 0.0f;
	hold.flGain = 0.0f;
	hold.flMaxSpeed = 0.0f;
	hold.flMaxForce = 0.0f;
	hold.bPullSet = false;
	hold.flSlipTime = 0.0f;
}

CWeaponBarnacle::CWeaponBarnacle()
{
	m_bTongueOut = false;
	m_flMaxLength = 0.0f;
	m_bPayingOut = false;
	m_vecLaunchVelocity.Init();
	m_bWasFeeding = false;
	m_flSlack = 0.0f;
	m_nLastPivots = 0;
	m_iTip = TIP_FREE;
	m_bTipOnEntity = false;
	m_vecTipFixed.Init();
	m_flTipStickTime = 0.0f;
	m_flLeash = 0.0f;
	m_flLeashMax = 0.0f;
	m_nJunk = 0;
	m_iNextDropped = 0;
	m_pController = NULL;
	m_bRetracting = false;
	m_flRetractTime = 0.0f;
	m_flRetractSpeed = 0.0f;
	m_flLengthRate = 0.0f;
	m_bReeling = false;
	m_bExtruding = false;
	m_bAttackReleased = true;

	ClearHold( m_TipHold );
	for ( int i = 0; i < BARNACLE_MAX_JUNK; i++ )
	{
		ClearHold( m_Junk[i] );
	}

	for ( int i = 0; i < BARNACLE_MAX_DROPPED; i++ )
	{
		m_flDroppedTime[i] = 0.0f;
	}
}

void CWeaponBarnacle::Precache( void )
{
	UTIL_PrecacheOther( "of2_tongue" );
	PrecacheScriptSound( "Weapon_Barnacle.Throw" );
	PrecacheScriptSound( "Weapon_Barnacle.Stick" );
	PrecacheScriptSound( "Weapon_Barnacle.Retract" );

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// A tongue that is out isn't saved, and stays behind on a level change
//-----------------------------------------------------------------------------
void CWeaponBarnacle::OnRestore( void )
{
	BaseClass::OnRestore();

	// What it held was in the physics world of the game that was saved
	m_pController = NULL;
	m_nJunk = 0;
	ClearHold( m_TipHold );

	ResetTongue();
}

void CWeaponBarnacle::UpdateOnRemove( void )
{
	ResetTongue();
	BaseClass::UpdateOnRemove();
}

void CWeaponBarnacle::Drop( const Vector &vecVelocity )
{
	ResetTongue();
	BaseClass::Drop( vecVelocity );
}

//-----------------------------------------------------------------------------
// The viewmodel is the barnacle NPC's model, which has none of the weapon
// activities, so its sequences are played by name.
// A negative rate plays it backwards from its end (C_BaseViewModel::Interpolate).
// bRestart starts it over if it is already playing. Returns how long it takes.
//-----------------------------------------------------------------------------
float CWeaponBarnacle::PlaySequence( const char *pszSequence, float flRate, bool bRestart )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	CBaseViewModel *pViewModel = pOwner ? pOwner->GetViewModel() : NULL;
	if ( pViewModel == NULL )
		return 0.0f;

	int nSequence = pViewModel->LookupSequence( pszSequence );
	if ( nSequence < 0 )
		return 0.0f;

	if ( bRestart || pViewModel->GetSequence() != nSequence )
	{
		// (this puts the rate back to 1)
		pViewModel->SendViewModelMatchingSequence( nSequence );
	}

	pViewModel->SetPlaybackRate( flRate );

	return pViewModel->SequenceDuration( nSequence ) / MAX( fabs( flRate ), 0.01f );
}

bool CWeaponBarnacle::Deploy( void )
{
	bool bDeployed = BaseClass::Deploy();
	PlaySequence( BARNACLE_IDLE_SEQUENCE );
	return bDeployed;
}

void CWeaponBarnacle::WeaponIdle( void )
{
	// Not until the throw has played out
	if ( !HasWeaponIdleTimeElapsed() )
		return;

	PlaySequence( BARNACLE_IDLE_SEQUENCE );
}

bool CWeaponBarnacle::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	ResetTongue();
	return BaseClass::Holster( pSwitchingTo );
}

//-----------------------------------------------------------------------------
// The base weapon would send a clipless weapon with no ammo type to its
// "fire on empty" path and never look at reload, so the buttons are read here.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::ItemPostFrame( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	if ( !( pOwner->m_nButtons & IN_ATTACK ) )
	{
		m_bAttackReleased = true;
	}

	m_bReeling = false;
	m_bExtruding = false;

	if ( ( pOwner->m_afButtonPressed & IN_RELOAD ) && m_bTongueOut )
	{
		if ( !m_bRetracting )
		{
			StartRetract();
		}
	}
	else if ( ( pOwner->m_afButtonPressed & IN_ATTACK ) && !m_bTongueOut && m_flNextPrimaryAttack <= gpGlobals->curtime )
	{
		Throw();
	}
	else if ( m_bTongueOut && !m_bRetracting )
	{
		// Primary held (pressed again after the throw) reels in; secondary held pays out
		m_bReeling = ( pOwner->m_nButtons & IN_ATTACK ) && m_bAttackReleased;
		m_bExtruding = !m_bReeling && ( pOwner->m_nButtons & IN_ATTACK2 );
	}

	if ( !( pOwner->m_nButtons & ( IN_ATTACK | IN_ATTACK2 | IN_RELOAD ) ) )
	{
		WeaponIdle();
	}
}

//-----------------------------------------------------------------------------
// Where the barnacle is on screen, unless that is in a wall
//-----------------------------------------------------------------------------
Vector CWeaponBarnacle::GetMouthPos( CBasePlayer *pOwner )
{
	Vector vecSize( BARNACLE_TIP_SIZE, BARNACLE_TIP_SIZE, BARNACLE_TIP_SIZE );

	trace_t tr;
	UTIL_TraceHull( pOwner->EyePosition(), OF2_TetherHoldPos( pOwner, true ), -vecSize, vecSize,
		MASK_SOLID, pOwner, COLLISION_GROUP_NONE, &tr );

	return tr.endpos;
}

//-----------------------------------------------------------------------------
// Throws the tip along the aim. The tongue comes out behind it as fast as the
// tip takes it (TongueThink), until it lands or the tongue is all out.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::Throw( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	ResetTongue();

	m_flMaxLength = MAX( of2_barnacle_max_length.GetFloat(), 64.0f );

	Vector vecForward;
	pOwner->EyeVectors( &vecForward );

	Vector vecMouth = GetMouthPos( pOwner );
	Vector vecVelocity = vecForward * of2_barnacle_throw_speed.GetFloat() + pOwner->GetAbsVelocity();
	vecVelocity.z += of2_barnacle_throw_up.GetFloat();

	m_Rope.Seed( vecMouth, vecVelocity );
	m_vecLaunchVelocity = vecVelocity;
	m_bWasFeeding = false;
	m_flSlack = 0.0f;

	m_Tether.Init( vecMouth, vecMouth, 1.0f );
	m_Tether.SetPlayerEnd( TETHER_START );
	m_nLastPivots = 0;

	m_iTip = TIP_FREE;
	m_bTipOnEntity = false;
	m_flTipStickTime = 0.0f;

	m_bTongueOut = true;
	m_bPayingOut = true;
	m_bRetracting = false;
	m_bAttackReleased = false;
	m_flLengthRate = 0.0f;

	EmitSound( "Weapon_Barnacle.Throw" );
	SetWeaponIdleTime( gpGlobals->curtime + PlaySequence( BARNACLE_THROW_SEQUENCE, -1.0f, true ) );
	pOwner->SetAnimation( PLAYER_ATTACK1 );

	m_flNextPrimaryAttack = gpGlobals->curtime + 0.5f;

	SetContextThink( &CWeaponBarnacle::TongueThink, gpGlobals->curtime, s_pTongueContext );
}

//-----------------------------------------------------------------------------
// Takes the tongue back in at once: it lets go of everything, the player lets
// go of it, and what the client draws goes
//-----------------------------------------------------------------------------
void CWeaponBarnacle::ResetTongue( void )
{
	CHL2_Player *pPlayer = dynamic_cast<CHL2_Player *>( GetOwner() );
	if ( pPlayer && pPlayer->GetTetherOwner() == this )
	{
		pPlayer->StopTether();
	}

	ReleaseHold( m_TipHold );
	for ( int i = 0; i < m_nJunk; i++ )
	{
		ReleaseHold( m_Junk[i] );
	}
	m_nJunk = 0;

	if ( m_pController )
	{
		if ( physenv )
		{
			physenv->DestroyMotionController( m_pController );
		}
		m_pController = NULL;
	}

	if ( m_hTongue != NULL )
	{
		UTIL_Remove( m_hTongue );
		m_hTongue = NULL;
	}

	m_iTip = TIP_FREE;
	m_bTipOnEntity = false;
	m_bTongueOut = false;
	m_bPayingOut = false;
	m_bRetracting = false;
	m_flLengthRate = 0.0f;

	SetContextThink( NULL, 0, s_pTongueContext );
}

//-----------------------------------------------------------------------------
// Reload, or the player letting go of a tongue they hung from: everything is
// let go of at once, and the tongue is hauled back in fast
//-----------------------------------------------------------------------------
void CWeaponBarnacle::StartRetract( void )
{
	CHL2_Player *pPlayer = dynamic_cast<CHL2_Player *>( GetOwner() );
	if ( pPlayer && pPlayer->GetTetherOwner() == this )
	{
		pPlayer->StopTether();
	}

	ReleaseHold( m_TipHold );
	for ( int i = 0; i < m_nJunk; i++ )
	{
		ReleaseHold( m_Junk[i] );
	}
	m_nJunk = 0;

	m_iTip = TIP_FREE;
	m_bTipOnEntity = false;
	m_bPayingOut = false;

	// From what is out, not from what could have been
	float flOut = MIN( m_Tether.GetTotalLength(), m_Tether.GetPathLength() );
	float flTime = MAX( of2_barnacle_retract_time.GetFloat(), 0.05f );
	m_Tether.SetTotalLength( flOut );

	m_bRetracting = true;
	m_flRetractTime = gpGlobals->curtime;
	m_flRetractSpeed = MAX( flOut, 64.0f ) / flTime;

	EmitSound( "Weapon_Barnacle.Retract" );
}

//-----------------------------------------------------------------------------
// Where the tip is held, if it is
//-----------------------------------------------------------------------------
bool CWeaponBarnacle::GetTipPin( Vector *pPin )
{
	if ( m_iTip == TIP_FREE )
		return false;

	CBaseEntity *pEntity = m_TipHold.hEntity;
	if ( !m_bTipOnEntity || pEntity == NULL )
	{
		*pPin = m_vecTipFixed;
	}
	else if ( m_TipHold.pPhysics )
	{
		// (where it is now, which its entity only catches up with after the physics step)
		m_TipHold.pPhysics->LocalToWorld( pPin, m_TipHold.vecLocal );
	}
	else
	{
		VectorTransform( m_TipHold.vecLocal, pEntity->EntityToWorldTransform(), *pPin );
	}

	return true;
}

//-----------------------------------------------------------------------------
// The tip has touched something. A loose object that is light enough is the
// tongue's to pull; anything else holds the player.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::StickTip( CHL2_Player *pPlayer, const trace_t &tr )
{
	CBaseEntity *pEntity = tr.m_pEnt;
	if ( pEntity && pEntity->IsWorld() )
	{
		pEntity = NULL;
	}

	// The part of it that was hit, and what all of it weighs
	IPhysicsObject *pPhysics = NULL;
	float flMass = 0.0f;
	if ( pEntity && pEntity->GetMoveType() == MOVETYPE_VPHYSICS )
	{
		IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int nCount = pEntity->VPhysicsGetObjectList( pList, ARRAYSIZE( pList ) );
		for ( int i = 0; i < nCount; i++ )
		{
			flMass += pList[i]->GetMass();
		}

		if ( nCount > 0 )
		{
			pPhysics = ( tr.physicsbone >= 0 && tr.physicsbone < nCount ) ? pList[tr.physicsbone] : pList[0];
		}
	}

	bool bLoose = pPhysics && pPhysics->IsMoveable() &&
		flMass <= of2_barnacle_anchor_mass.GetFloat() &&
		!pPhysics->IsAttachedToConstraint( true ) &&
		!( pPhysics->GetGameFlags() & FVPHYSICS_PLAYER_HELD );

	ClearHold( m_TipHold );
	m_TipHold.hEntity = pEntity;
	m_TipHold.pPhysics = pPhysics;
	m_TipHold.flMass = flMass;
	m_bTipOnEntity = ( pEntity != NULL );
	m_vecTipFixed = tr.endpos;

	if ( pPhysics )
	{
		pPhysics->WorldToLocal( &m_TipHold.vecLocal, tr.endpos );
	}
	else if ( pEntity )
	{
		VectorITransform( tr.endpos, pEntity->EntityToWorldTransform(), m_TipHold.vecLocal );
	}

	m_bPayingOut = false;
	EmitSound( "Weapon_Barnacle.Stick" );

	if ( bLoose )
	{
		m_iTip = TIP_PAYLOAD;
		HoldObject( m_TipHold );
	}
	else
	{
		m_iTip = TIP_ANCHORED;
		Anchor( pPlayer, tr.endpos );
	}
}

//-----------------------------------------------------------------------------
// The tip lets go of what it is on, and is loose again
//-----------------------------------------------------------------------------
void CWeaponBarnacle::FreeTip( void )
{
	ReleaseHold( m_TipHold );

	m_iTip = TIP_FREE;
	m_bTipOnEntity = false;

	// Not straight back onto what it just came off
	m_flTipStickTime = gpGlobals->curtime + BARNACLE_RESTICK_TIME;
}

//-----------------------------------------------------------------------------
// What the tip was on has gone (broken, picked up), or the player has taken
// it in hand
//-----------------------------------------------------------------------------
void CWeaponBarnacle::CheckTip( void )
{
	if ( m_iTip == TIP_FREE || !m_bTipOnEntity )
		return;

	CBaseEntity *pEntity = m_TipHold.hEntity;
	bool bGone = ( pEntity == NULL ) || ( m_TipHold.pPhysics && !HasPhysicsObject( pEntity, m_TipHold.pPhysics ) );
	if ( bGone )
	{
		// Nothing left to let go of
		m_TipHold.pPhysics = NULL;
		m_TipHold.bControlled = false;
	}

	bool bTaken = !bGone && m_iTip == TIP_PAYLOAD && ( m_TipHold.pPhysics->GetGameFlags() & FVPHYSICS_PLAYER_HELD );
	if ( !bGone && !bTaken )
		return;

	if ( m_iTip == TIP_ANCHORED )
	{
		StartRetract();
	}
	else
	{
		FreeTip();
	}
}

//-----------------------------------------------------------------------------
// The tip has caught on something the player can't move. From now on the
// player hangs from it like from a climb rope, over whatever the tongue lies
// over. They can walk out as far as the whole tongue reaches, swing, and reel
// in or pay out.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::Anchor( CHL2_Player *pPlayer, const Vector &vecTip )
{
	// The player holds it by the hand now, rather than its coming out of the barnacle
	Vector vecHand = COF2Tether::GetPlayerHandPos( pPlayer, true );
	m_Tether.Update( vecHand, vecTip );

	m_flLeashMax = m_flMaxLength;
	m_flLeash = m_flLeashMax;

	// As much as it takes to reach the hand, so the player isn't jerked
	float flFixed = m_Tether.GetFixedLength( TETHER_START );
	float flLength = MIN( flFixed + m_Tether.GetSwingPoint().DistTo( vecHand ), m_flLeash );
	m_Tether.SetTotalLength( flLength );

	// The mouse buttons reel, so forward/back only swings
	pPlayer->StartTether( this, 0.0f, of2_barnacle_pump.GetFloat(), 0.0f, true );
	pPlayer->UpdateTether( m_Tether.GetSwingPoint(), m_Tether.GetSwingNextPoint(), m_Tether.GetSwingLength(), MAX( m_flLeash - flFixed, 0.0f ) );
}

//-----------------------------------------------------------------------------
// Anchored: the same as holding a climb rope (CFuncClimbRope::UpdateHeld),
// except that reeling shortens what there is and paying out lengthens it
//-----------------------------------------------------------------------------
void CWeaponBarnacle::UpdateAnchored( CHL2_Player *pPlayer, const Vector &vecTip )
{
	Vector vecHand = COF2Tether::GetPlayerHandPos( pPlayer, true );

	float flFixed = m_Tether.GetFixedLength( TETHER_START );
	float flSwing = pPlayer->GetTetherSwingLength();

	if ( pPlayer->IsTetherMantling() )
	{
		// Reeled all the way up to an edge, and being moved up onto it
		// (CHL2GameMovement::TetherMantleMove): the tongue gives what that takes
		m_flLeash = MAX( m_flLeash, flFixed + m_Tether.GetSwingPoint().DistTo( vecHand ) + BARNACLE_MANTLE_SLACK );
	}
	else if ( m_bReeling )
	{
		// Standing over the point the tongue runs to (up on the ledge it goes
		// over, say), the hand can get no nearer than it is above it. Shorter
		// than that would drag them down off their feet. With the point
		// overhead it is the other way round: reeling is meant to lift them.
		float flClosest = OF2_TetherMinLength();
		float flAbove = vecHand.z - m_Tether.GetSwingPoint().z;
		if ( pPlayer->GetGroundEntity() != NULL && flAbove > 0.0f )
		{
			flClosest = MAX( flClosest, flAbove + BARNACLE_MANTLE_SLACK );
		}

		// From what the player has now, not from the most there could be
		m_flLeash = MIN( m_flLeash, flFixed + flSwing ) - of2_barnacle_reel_speed.GetFloat() * TICK_INTERVAL;
		m_flLeash = MAX( m_flLeash, flFixed + flClosest );

		// On their feet with nothing left to reel in, and as near as that gets
		// them: the tongue has done what it can, and comes back by itself (the
		// user asked for this: pulled up over an edge, they had to let go of it
		// by hand every time)
		if ( of2_barnacle_auto_release.GetBool() && pPlayer->GetGroundEntity() != NULL &&
			 m_flLeash <= flFixed + flClosest &&
			 vecHand.DistTo( m_Tether.GetSwingPoint() ) <= flClosest + BARNACLE_RELEASE_DIST )
		{
			StartRetract();
			return;
		}
	}
	else if ( m_bExtruding )
	{
		float flMore = of2_barnacle_extrude_speed.GetFloat() * TICK_INTERVAL;
		m_flLeash = MIN( m_flLeash + flMore, m_flLeashMax );
		flSwing += flMore;
	}

	flSwing = MIN( flSwing, m_flLeash - flFixed );
	m_Tether.SetTotalLength( flFixed + MAX( flSwing, 0.0f ) );
	m_Tether.Update( vecHand, vecTip );

	// A bend that came or went moved the point the player swings from
	flFixed = m_Tether.GetFixedLength( TETHER_START );
	pPlayer->UpdateTether( m_Tether.GetSwingPoint(), m_Tether.GetSwingNextPoint(), m_Tether.GetSwingLength(), MAX( m_flLeash - flFixed, 0.0f ) );
}

//-----------------------------------------------------------------------------
// Not anchored: the tongue's length while it is thrown, reeled in, paid out
// or retracted. What is on it follows from that (the rope for a free tip,
// UpdateHolds for the rest). False if the tongue is all back in.
//-----------------------------------------------------------------------------
bool CWeaponBarnacle::UpdateLoose( CBasePlayer *pOwner, const Vector &vecMouth, const Vector &vecTip )
{
	m_Tether.Update( vecMouth, vecTip );

	float flTotal = m_Tether.GetTotalLength();
	float flPath = m_Tether.GetPathLength();
	m_flLengthRate = 0.0f;

	if ( m_bRetracting )
	{
		flTotal -= m_flRetractSpeed * TICK_INTERVAL;

		float flPatience = MAX( of2_barnacle_retract_time.GetFloat(), 0.05f ) * BARNACLE_RETRACT_PATIENCE;
		if ( flTotal <= BARNACLE_ABSORB_DIST || gpGlobals->curtime > m_flRetractTime + flPatience )
		{
			ResetTongue();
			return false;
		}

		m_Tether.SetTotalLength( flTotal );
		return true;
	}

	// Taking it in hand ends the throw
	if ( m_bReeling || m_bExtruding || m_iTip != TIP_FREE )
	{
		m_bPayingOut = false;
	}

	if ( m_bPayingOut )
	{
		// The tongue comes out as fast as the tip takes it, until it is all out
		float flWanted = flPath * ( 1.0f + MAX( of2_barnacle_throw_slack.GetFloat(), 0.0f ) );
		flTotal = clamp( flWanted, flTotal, m_flMaxLength );
		if ( flTotal >= m_flMaxLength )
		{
			m_bPayingOut = false;
		}
	}
	else if ( m_bReeling )
	{
		// Slack is taken up first, and faster
		float flSpeed = of2_barnacle_reel_speed.GetFloat();
		bool bSlack = flTotal > flPath + 1.0f;
		flTotal -= flSpeed * ( bSlack ? BARNACLE_SLACK_REEL : 1.0f ) * TICK_INTERVAL;
		if ( bSlack )
		{
			flTotal = MAX( flTotal, flPath );
		}

		if ( m_iTip == TIP_FREE && m_nJunk == 0 )
		{
			// Nothing on it: all the way in
			if ( flTotal <= BARNACLE_ABSORB_DIST )
			{
				ResetTongue();
				return false;
			}
		}
		else if ( flTotal < BARNACLE_MIN_LENGTH )
		{
			// What it holds stays out in front of the barnacle
			flTotal = BARNACLE_MIN_LENGTH;
			flSpeed = 0.0f;
		}

		if ( !bSlack )
		{
			m_flLengthRate = -flSpeed;
		}
	}
	else if ( m_bExtruding )
	{
		flTotal = MIN( flTotal + of2_barnacle_extrude_speed.GetFloat() * TICK_INTERVAL, m_flMaxLength );
	}

	m_Tether.SetTotalLength( flTotal );
	return true;
}

//-----------------------------------------------------------------------------
// The tongue's whole path from the tip in: its loose points, its bends, and
// the barnacle
//-----------------------------------------------------------------------------
int CWeaponBarnacle::GetPath( Vector *pPath ) const
{
	int nPath = 0;
	int nPivots = m_Tether.GetPivotCount();

	// (with bends, the first loose point is the last bend)
	for ( int i = m_Rope.GetCount() - 1; i >= ( ( nPivots > 0 ) ? 1 : 0 ); i-- )
	{
		pPath[nPath++] = m_Rope.GetPos( i );
	}

	for ( int j = nPivots - 1; j >= 0; j-- )
	{
		pPath[nPath++] = m_Tether.GetPoint( j + 1 );
	}

	pPath[nPath++] = m_Tether.GetPoint( 0 );
	return nPath;
}

//-----------------------------------------------------------------------------
// The point flAlong from the start of a path. Past its end, its end.
//-----------------------------------------------------------------------------
Vector CWeaponBarnacle::GetPathPoint( const Vector *pPath, int nPath, float flAlong )
{
	for ( int i = 0; i < nPath - 1; i++ )
	{
		float flLeg = pPath[i].DistTo( pPath[i + 1] );
		if ( flAlong <= flLeg && flLeg > 0.001f )
			return pPath[i] + ( pPath[i + 1] - pPath[i] ) * ( MAX( flAlong, 0.0f ) / flLeg );

		flAlong -= flLeg;
	}

	return pPath[nPath - 1];
}

//-----------------------------------------------------------------------------
// How well the tongue holds on, flAlong from its tip: the weight it can pull
// at full strength. Most at the tip, falling off towards the barnacle, so of
// two heavy things the one on the tip is the one that comes in.
//-----------------------------------------------------------------------------
float CWeaponBarnacle::GetGrip( float flAlong ) const
{
	float flShare = 1.0f - clamp( flAlong / MAX( m_flMaxLength, 1.0f ), 0.0f, 1.0f );
	float flTip = of2_barnacle_stick_tip.GetFloat();
	float flMouth = of2_barnacle_stick_mouth.GetFloat();

	return flMouth + ( flTip - flMouth ) * pow( flShare, MAX( of2_barnacle_stick_curve.GetFloat(), 0.01f ) );
}

//-----------------------------------------------------------------------------
// Hands an object to the motion controller: from now on Simulate() is called
// for it in every physics step
//-----------------------------------------------------------------------------
void CWeaponBarnacle::HoldObject( BarnacleHold_t &hold )
{
	if ( hold.pPhysics == NULL )
		return;

	if ( m_pController == NULL )
	{
		m_pController = physenv->CreateMotionController( this );
	}

	m_pController->AttachObject( hold.pPhysics, true );
	hold.pPhysics->Wake();
	hold.bControlled = true;
}

void CWeaponBarnacle::ReleaseHold( BarnacleHold_t &hold )
{
	CBaseEntity *pEntity = hold.hEntity;
	if ( hold.bControlled && hold.pPhysics && pEntity && m_pController && HasPhysicsObject( pEntity, hold.pPhysics ) )
	{
		m_pController->DetachObject( hold.pPhysics );
		hold.pPhysics->Wake();
	}

	ClearHold( hold );
}

//-----------------------------------------------------------------------------
// Something that can't keep up with the tongue (too heavy for the grip it is
// held with, or caught on something) falls behind. Once it has been more than
// flDist behind for flTime, the tongue should let it go rather than fight.
//-----------------------------------------------------------------------------
bool CWeaponBarnacle::IsSlipping( BarnacleHold_t &hold, float flDist, float flTime )
{
	if ( hold.pPhysics == NULL || !hold.bPullSet )
		return false;

	Vector vecAttach;
	hold.pPhysics->LocalToWorld( &vecAttach, hold.vecLocal );

	if ( vecAttach.DistTo( hold.vecPull ) - hold.flLength <= flDist )
	{
		hold.flSlipTime = 0.0f;
		return false;
	}

	if ( hold.flSlipTime == 0.0f )
	{
		hold.flSlipTime = gpGlobals->curtime;
	}

	return gpGlobals->curtime - hold.flSlipTime > flTime;
}

//-----------------------------------------------------------------------------
// Works out, for everything the tongue holds, what Simulate() needs for the
// physics step that follows: where it is pulled towards and how hard
//-----------------------------------------------------------------------------
void CWeaponBarnacle::UpdateHolds( CBasePlayer *pOwner )
{
	float flAccel = of2_barnacle_stick_accel.GetFloat();
	float flSnagDist = of2_barnacle_snag_dist.GetFloat();
	float flSnagTime = of2_barnacle_snag_time.GetFloat();

	if ( m_iTip == TIP_PAYLOAD && m_TipHold.pPhysics )
	{
		// What is on the tip may be as far from the last bend (or the barnacle)
		// as there is tongue left for it
		BarnacleHold_t &hold = m_TipHold;
		hold.vecPull = m_Tether.GetNearestPoint( TETHER_END );
		hold.vecPullVelocity = ( m_Tether.GetPivotCount() == 0 ) ? pOwner->GetAbsVelocity() : vec3_origin;
		hold.flLength = m_Tether.GetFarEndAllowance();
		hold.flLengthRate = m_flLengthRate;
		hold.flGain = of2_barnacle_pull.GetFloat();
		hold.flMaxSpeed = of2_barnacle_pull_max_speed.GetFloat();
		hold.flMaxForce = GetGrip( 0.0f ) * flAccel;
		hold.bPullSet = true;

		// (an object that has gone to sleep is not simulated)
		hold.pPhysics->Wake();

		if ( IsSlipping( hold, flSnagDist * BARNACLE_TIP_SNAG_DIST, flSnagTime * BARNACLE_TIP_SNAG_TIME ) )
		{
			FreeTip();
		}
	}

	if ( m_nJunk == 0 )
		return;

	Vector vecPath[BARNACLE_MAX_PATH];
	int nPath = GetPath( vecPath );

	for ( int i = m_nJunk - 1; i >= 0; i-- )
	{
		BarnacleHold_t &hold = m_Junk[i];
		CBaseEntity *pEntity = hold.hEntity;
		if ( pEntity == NULL || !HasPhysicsObject( pEntity, hold.pPhysics ) )
		{
			// Gone (broken, picked up): nothing left to let go of
			hold.pPhysics = NULL;
			DropJunk( i, false );
			continue;
		}

		if ( hold.pPhysics->GetGameFlags() & FVPHYSICS_PLAYER_HELD )
		{
			DropJunk( i, false );
			continue;
		}

		// Its place on the tongue, which comes in as the tongue does
		Vector vecPull = GetPathPoint( vecPath, nPath, hold.flAlong );
		hold.vecPullVelocity = hold.bPullSet ? ( vecPull - hold.vecPull ) / TICK_INTERVAL : vec3_origin;
		if ( hold.vecPullVelocity.LengthSqr() > 1000.0f * 1000.0f )
		{
			hold.vecPullVelocity.Init();
		}

		hold.vecPull = vecPull;
		hold.flLength = BARNACLE_JUNK_SLACK;
		hold.flLengthRate = 0.0f;
		hold.flGain = BARNACLE_JUNK_PULL;
		hold.flMaxSpeed = BARNACLE_JUNK_PULL_SPEED;
		hold.flMaxForce = GetGrip( hold.flAlong ) * flAccel;
		hold.bPullSet = true;

		hold.pPhysics->Wake();

		if ( IsSlipping( hold, flSnagDist, flSnagTime ) )
		{
			DropJunk( i, true );
		}
	}
}

//-----------------------------------------------------------------------------
// Could the length of the tongue pick this up?
//-----------------------------------------------------------------------------
bool CWeaponBarnacle::CanCollect( CBaseEntity *pEntity )
{
	if ( pEntity == NULL || pEntity->IsPlayer() || pEntity->IsNPC() || pEntity == this )
		return false;

	if ( pEntity->GetMoveType() != MOVETYPE_VPHYSICS || pEntity->GetParent() != NULL )
		return false;

	IPhysicsObject *pPhysics = pEntity->VPhysicsGetObject();
	if ( pPhysics == NULL || !pPhysics->IsMoveable() || pPhysics->IsAttachedToConstraint( true ) )
		return false;

	if ( pPhysics->GetGameFlags() & FVPHYSICS_PLAYER_HELD )
		return false;

	if ( pPhysics->GetMass() > of2_barnacle_collect_mass.GetFloat() )
		return false;

	if ( m_iTip != TIP_FREE && m_TipHold.hEntity == pEntity )
		return false;

	for ( int i = 0; i < m_nJunk; i++ )
	{
		if ( m_Junk[i].hEntity == pEntity )
			return false;
	}

	for ( int i = 0; i < BARNACLE_MAX_DROPPED; i++ )
	{
		if ( m_hDropped[i] == pEntity && gpGlobals->curtime < m_flDroppedTime[i] + BARNACLE_RECATCH_TIME )
			return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// While it is reeled in, the tongue picks up loose things it touches. Each is
// held at the place along the tongue where it was caught, by the spot on it
// that was nearest the tongue, and stays a physics object: it swings, turns
// and knocks into things.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::CollectJunk( void )
{
	int nMost = clamp( of2_barnacle_collect_count.GetInt(), 0, BARNACLE_MAX_JUNK );
	if ( m_nJunk >= nMost )
		return;

	float flCarried = 0.0f;
	for ( int i = 0; i < m_nJunk; i++ )
	{
		flCarried += m_Junk[i].flMass;
	}

	Vector vecPath[BARNACLE_MAX_PATH];
	int nPath = GetPath( vecPath );

	float flRadius = of2_barnacle_collect_radius.GetFloat();
	Vector vecMins = vecPath[0];
	Vector vecMaxs = vecPath[0];
	for ( int i = 1; i < nPath; i++ )
	{
		AddPointToBounds( vecPath[i], vecMins, vecMaxs );
	}
	vecMins -= Vector( flRadius, flRadius, flRadius );
	vecMaxs += Vector( flRadius, flRadius, flRadius );

	CBaseEntity *pList[64];
	int nCount = UTIL_EntitiesInBox( pList, ARRAYSIZE( pList ), vecMins, vecMaxs, 0 );
	for ( int i = 0; i < nCount && m_nJunk < nMost; i++ )
	{
		CBaseEntity *pEntity = pList[i];
		if ( !CanCollect( pEntity ) )
			continue;

		IPhysicsObject *pPhysics = pEntity->VPhysicsGetObject();
		float flMass = pPhysics->GetMass();
		if ( flCarried + flMass > of2_barnacle_collect_total_mass.GetFloat() )
			continue;

		// Where the tongue comes nearest it, and how far along the tongue that is
		Vector vecCenter = pEntity->WorldSpaceCenter();
		Vector vecOnTongue = vecPath[0];
		float flBest = FLT_MAX;
		float flAlong = 0.0f;
		float flRun = 0.0f;
		for ( int k = 0; k < nPath - 1; k++ )
		{
			float t;
			float flDist = CalcDistanceToLineSegment( vecCenter, vecPath[k], vecPath[k + 1], &t );
			float flLeg = vecPath[k].DistTo( vecPath[k + 1] );
			if ( flDist < flBest )
			{
				flBest = flDist;
				vecOnTongue = vecPath[k] + ( vecPath[k + 1] - vecPath[k] ) * t;
				flAlong = flRun + flLeg * t;
			}
			flRun += flLeg;
		}

		// Near enough is measured to its box, not its middle
		Vector vecNearest;
		pEntity->CollisionProp()->CalcNearestPoint( vecOnTongue, &vecNearest );
		if ( vecNearest.DistTo( vecOnTongue ) > flRadius )
			continue;

		BarnacleHold_t &hold = m_Junk[m_nJunk++];
		ClearHold( hold );
		hold.hEntity = pEntity;
		hold.pPhysics = pPhysics;
		hold.flAlong = flAlong;
		hold.flMass = flMass;
		pPhysics->WorldToLocal( &hold.vecLocal, vecNearest );
		HoldObject( hold );

		flCarried += flMass;
	}
}

//-----------------------------------------------------------------------------
// bRemember: it slipped off, and shouldn't be picked straight up again
//-----------------------------------------------------------------------------
void CWeaponBarnacle::DropJunk( int iJunk, bool bRemember )
{
	if ( bRemember )
	{
		m_hDropped[m_iNextDropped] = m_Junk[iJunk].hEntity;
		m_flDroppedTime[m_iNextDropped] = gpGlobals->curtime;
		m_iNextDropped = ( m_iNextDropped + 1 ) % BARNACLE_MAX_DROPPED;
	}

	ReleaseHold( m_Junk[iJunk] );

	m_nJunk--;
	if ( iJunk < m_nJunk )
	{
		m_Junk[iJunk] = m_Junk[m_nJunk];
		ClearHold( m_Junk[m_nJunk] );
	}
}

//-----------------------------------------------------------------------------
// What the tongue carries weighs it down where it hangs: those points give
// way less to their neighbours, so the tongue sags there. A free tip is heavy
// too, so that it carries the tongue out behind it.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::WeighTongue( void )
{
	for ( int i = 0; i < OF2_TONGUE_NODES; i++ )
	{
		m_Rope.m_flInvMass[i] = 1.0f;
	}

	m_Rope.m_flInvMass[m_Rope.GetCount() - 1] = 1.0f / MAX( of2_barnacle_tip_weight.GetFloat(), 1.0f );

	float flSag = MAX( of2_barnacle_sag.GetFloat(), 0.0f );
	float flSegment = MAX( m_Rope.GetSegment(), 0.01f );
	for ( int i = 0; i < m_nJunk; i++ )
	{
		// Counted from the tip. Past the loose part it hangs from a bend or the barnacle.
		int iNode = m_Rope.GetCount() - 1 - (int)( m_Junk[i].flAlong / flSegment + 0.5f );
		if ( iNode < 1 || iNode >= m_Rope.GetCount() )
			continue;

		m_Rope.m_flInvMass[iNode] = MIN( m_Rope.m_flInvMass[iNode], 1.0f / ( 1.0f + m_Junk[i].flMass * flSag ) );
	}
}

void CWeaponBarnacle::UpdateTongue( void )
{
	COF2Tongue *pTongue = m_hTongue;
	if ( pTongue == NULL )
	{
		// It rides along with the player so that it is always in their view
		CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
		if ( pOwner == NULL )
			return;

		pTongue = static_cast<COF2Tongue *>( CBaseEntity::Create( "of2_tongue", pOwner->GetAbsOrigin(), vec3_angle, pOwner ) );
		if ( pTongue == NULL )
			return;

		pTongue->SetParent( pOwner );
		pTongue->m_flWidth = BARNACLE_TONGUE_WIDTH;

		// The client moves it smoothly from one tick to the next, as it does
		// the objects the tongue holds
		pTongue->SetSimulatedEveryTick( true );
		m_hTongue = pTongue;
	}

	int nNodes = m_Rope.GetCount();
	for ( int i = 0; i < OF2_TONGUE_NODES; i++ )
	{
		pTongue->m_vecNodes.Set( i, m_Rope.GetPos( MAX( nNodes - 1 - i, 0 ) ) );
	}
	pTongue->m_nNodes = nNodes;

	// From where the loose part starts, back towards the barnacle
	int nBends = 0;
	for ( int j = m_Tether.GetPivotCount() - 1; j >= 0 && nBends < OF2_TONGUE_MAX_BENDS; j-- )
	{
		pTongue->m_vecBends.Set( nBends++, m_Tether.GetPoint( j + 1 ) );
	}

	pTongue->m_nBends = nBends;
	pTongue->SetSimulationTime( gpGlobals->curtime );
}

void CWeaponBarnacle::DebugDraw( void )
{
	m_Tether.DebugDraw();

	for ( int i = 0; i < m_Rope.GetCount(); i++ )
	{
		NDebugOverlay::Cross3D( m_Rope.GetPos( i ), 2.0f, 255, 255, 255, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
	}

	// What it holds: green while it keeps up, amber while it is slipping
	if ( m_iTip == TIP_PAYLOAD && m_TipHold.pPhysics && m_TipHold.bPullSet )
	{
		Vector vecAttach;
		m_TipHold.pPhysics->LocalToWorld( &vecAttach, m_TipHold.vecLocal );
		bool bSlipping = ( m_TipHold.flSlipTime != 0.0f );
		NDebugOverlay::Line( m_TipHold.vecPull, vecAttach, bSlipping ? 230 : 10, bSlipping ? 150 : 204, bSlipping ? 0 : 88, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
	}

	for ( int i = 0; i < m_nJunk; i++ )
	{
		if ( m_Junk[i].pPhysics == NULL || !m_Junk[i].bPullSet )
			continue;

		Vector vecAttach;
		m_Junk[i].pPhysics->LocalToWorld( &vecAttach, m_Junk[i].vecLocal );
		bool bSlipping = ( m_Junk[i].flSlipTime != 0.0f );
		NDebugOverlay::Line( m_Junk[i].vecPull, vecAttach, bSlipping ? 230 : 10, bSlipping ? 150 : 204, bSlipping ? 0 : 88, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
		NDebugOverlay::Cross3D( vecAttach, 3.0f, 230, 150, 0, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
	}
}

void CWeaponBarnacle::TongueThink( void )
{
	CHL2_Player *pPlayer = dynamic_cast<CHL2_Player *>( GetOwner() );
	if ( !m_bTongueOut || pPlayer == NULL || !pPlayer->IsAlive() )
	{
		ResetTongue();
		return;
	}

	CheckTip();

	m_Tether.SetSliding( of2_barnacle_slide_speed.GetFloat(), of2_barnacle_slide_friction.GetFloat() );

	// The player let go of a tongue they hung from (jumped off, was teleported)
	if ( m_iTip == TIP_ANCHORED && ( !pPlayer->IsOnTether() || pPlayer->GetTetherOwner() != this ) )
	{
		StartRetract();
	}

	Vector vecMouth = GetMouthPos( pPlayer );
	Vector vecNear = ( m_iTip == TIP_ANCHORED ) ? COF2Tether::GetPlayerHandPos( pPlayer, true ) : vecMouth;

	// The loose part: from the last bend, or the near end if there is none, to the tip
	Vector vecPin;
	bool bPinned = GetTipPin( &vecPin );
	Vector vecStart = ( m_Tether.GetPivotCount() > 0 ) ? m_Tether.GetNearestPoint( TETHER_END ) : vecNear;

	// The throw, with nothing in the tongue's way yet: it is fed out of the
	// barnacle rather than pulled out by the tip. Every bit of it leaves at the
	// velocity the tip was thrown at and flies free, so the whole tongue lies
	// along the arc the tip took. Its length is whatever has come out.
	bool bFeeding = m_bPayingOut && !bPinned && m_Tether.GetPivotCount() == 0;

	float flLength = m_Tether.GetFarEndAllowance();
	if ( bFeeding )
	{
		flLength = m_Rope.GetLength() + m_Rope.GetTipVelocity().Length() * TICK_INTERVAL;
		if ( flLength >= m_flMaxLength )
		{
			// All out: from here the tongue holds the tip back
			bFeeding = false;
			m_bPayingOut = false;
			flLength = m_flMaxLength;
			m_Tether.SetTotalLength( m_flMaxLength );
		}
	}

	if ( !bFeeding )
	{
		// The throw is over. The arc is longer than the straight line the
		// tongue is now measured by; rather than snap to that, it keeps the
		// difference as slack and gives it up as it settles.
		if ( m_bWasFeeding )
		{
			m_flSlack = MAX( m_Rope.GetLength() - flLength, 0.0f );
		}

		flLength += m_flSlack;
		m_flSlack = MAX( m_flSlack - of2_barnacle_settle_speed.GetFloat() * TICK_INTERVAL, 0.0f );
	}

	m_bWasFeeding = bFeeding;
	m_Rope.m_bFeeding = bFeeding;
	m_Rope.m_vecFeedVelocity = m_vecLaunchVelocity;

	if ( m_bPayingOut && !bPinned && !bFeeding )
	{
		// The tongue comes out as fast as the tip takes it, so the throw isn't
		// held back by a length measured a tick ago
		float flReach = vecStart.DistTo( m_Rope.GetTip() ) + m_Rope.GetTipVelocity().Length() * TICK_INTERVAL;
		flReach *= 1.0f + MAX( of2_barnacle_throw_slack.GetFloat(), 0.0f );
		float flMost = MAX( m_flMaxLength - m_Tether.GetFixedLength( TETHER_END ), flLength );
		flLength = clamp( flReach, flLength, flMost );
	}

	WeighTongue();
	m_Rope.m_pIgnore = pPlayer;

	// The tongue drapes over loose things, but not over the ones hanging on it
	m_Rope.m_nDrapeIgnore = 0;
	if ( m_iTip != TIP_FREE && m_TipHold.hEntity != NULL )
	{
		m_Rope.m_pDrapeIgnore[m_Rope.m_nDrapeIgnore++] = m_TipHold.hEntity;
	}
	for ( int i = 0; i < m_nJunk; i++ )
	{
		if ( m_Junk[i].hEntity != NULL )
		{
			m_Rope.m_pDrapeIgnore[m_Rope.m_nDrapeIgnore++] = m_Junk[i].hEntity;
		}
	}
	m_Rope.m_bSticky = !bPinned && !m_bRetracting && gpGlobals->curtime >= m_flTipStickTime;
	m_Rope.m_bCollide = !m_bRetracting;
	m_Rope.Step( vecStart, flLength, bPinned ? &vecPin : NULL );

	bool bJustAnchored = false;
	trace_t tr;
	if ( !bPinned && m_Rope.GetTipHit( &tr ) )
	{
		StickTip( pPlayer, tr );
		bPinned = GetTipPin( &vecPin );
		bJustAnchored = ( m_iTip == TIP_ANCHORED );
	}

	Vector vecTip = bPinned ? vecPin : m_Rope.GetTip();

	if ( m_iTip == TIP_ANCHORED )
	{
		if ( !bJustAnchored )
		{
			UpdateAnchored( pPlayer, vecTip );
		}
	}
	else if ( !UpdateLoose( pPlayer, vecMouth, vecTip ) )
	{
		return;
	}

	// A bend that came or went moved where the loose part starts
	if ( m_Tether.GetPivotCount() != m_nLastPivots )
	{
		m_Rope.Rebase( m_Tether.GetNearestPoint( TETHER_END ), m_Tether.GetPivotCount() > m_nLastPivots );
		m_nLastPivots = m_Tether.GetPivotCount();
	}

	UpdateHolds( pPlayer );

	if ( m_bReeling && !m_bRetracting )
	{
		CollectJunk();
	}

	UpdateTongue();

	if ( of2_barnacle_debug.GetBool() )
	{
		DebugDraw();
	}

	SetContextThink( &CWeaponBarnacle::TongueThink, gpGlobals->curtime + TICK_INTERVAL, s_pTongueContext );
}

//-----------------------------------------------------------------------------
// IMotionEvent. Called inside the physics step for each object the tongue
// holds, so what it does is part of that step rather than a correction after
// it. A tongue only pulls: an object within its length is left alone. One
// that is past it loses its speed away from the tongue and is brought back
// gently, by a pull where the tongue has hold of it (so it turns and swings),
// never harder than the tongue's grip there allows.
//-----------------------------------------------------------------------------
IMotionEvent::simresult_e CWeaponBarnacle::Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular )
{
	linear.Init();
	angular.Init();

	const BarnacleHold_t *pHold = NULL;
	if ( m_iTip == TIP_PAYLOAD && m_TipHold.pPhysics == pObject )
	{
		pHold = &m_TipHold;
	}
	else
	{
		for ( int i = 0; i < m_nJunk; i++ )
		{
			if ( m_Junk[i].pPhysics == pObject )
			{
				pHold = &m_Junk[i];
				break;
			}
		}
	}

	if ( pHold == NULL || !pHold->bPullSet || deltaTime <= 0.0f )
		return SIM_NOTHING;

	Vector vecAttach;
	pObject->LocalToWorld( &vecAttach, pHold->vecLocal );

	Vector vecDir = vecAttach - pHold->vecPull;
	float flOver = VectorNormalize( vecDir ) - pHold->flLength;
	if ( flOver <= 0.0f )
		return SIM_NOTHING;

	// Its speed away from the tongue, and the speed it should have instead
	Vector vecVelocity;
	pObject->GetVelocityAtPoint( vecAttach, &vecVelocity );

	float flOut = DotProduct( vecVelocity - pHold->vecPullVelocity, vecDir );
	float flWanted = pHold->flLengthRate - MIN( flOver * pHold->flGain, pHold->flMaxSpeed );
	float flChange = flWanted - flOut;
	if ( flChange >= 0.0f )
		return SIM_NOTHING;

	// What a push of one unit along the tongue, where it has hold, does to the
	// object, and so to the speed of that spot
	Vector vecCenterVelocity;
	AngularImpulse angCenterVelocity;
	pObject->CalculateVelocityOffset( vecDir, vecAttach, &vecCenterVelocity, &angCenterVelocity );

	Vector vecTurn, vecMassCenter;
	pObject->LocalToWorldVector( &vecTurn, angCenterVelocity );
	pObject->LocalToWorld( &vecMassCenter, pObject->GetMassCenterLocalSpace() );
	vecTurn *= DEG2RAD( 1.0f );

	float flGive = DotProduct( vecCenterVelocity + CrossProduct( vecTurn, vecAttach - vecMassCenter ), vecDir );
	flGive = MAX( flGive, pObject->GetInvMass() );
	if ( flGive <= 0.0f )
		return SIM_NOTHING;

	// The push that makes the change, or as much of it as the grip is good for
	float flPush = MAX( flChange / flGive, -pHold->flMaxForce * deltaTime );

	Vector vecLinear = vecCenterVelocity * ( flPush / deltaTime );
	pObject->WorldToLocalVector( &linear, vecLinear );
	angular = angCenterVelocity * ( flPush / deltaTime );

	return SIM_LOCAL_ACCELERATION;
}
