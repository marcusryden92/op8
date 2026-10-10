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
//			It takes hold of the living too: a headcrab is carried in alive and
//			eaten, a zombie loses its headcrab to it, and a hostile person is
//			held until the tongue is pulled tight, which breaks their neck.
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
#include "hl2/of2_rope_sim.h"
#include "physics.h"
#include "vphysics_interface.h"
#include "movevars_shared.h"
#include "ndebugoverlay.h"
#include "ai_basenpc.h"
#include "ai_baseactor.h"
#include "npc_headcrab.h"
#include "npc_BaseZombie.h"
#include "npc_combine.h"
#include "physics_prop_ragdoll.h"
#include "te_effect_dispatch.h"
#include "of2_stealth.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define BARNACLE_TONGUE_WIDTH		2.0f

// What the barnacle in the hand does when nothing else is going on
#define BARNACLE_IDLE_SEQUENCE		"idle01"
// Throwing: the barnacle swallowing, played backwards
#define BARNACLE_THROW_SEQUENCE		"slurp"
// Reeling in: the same, the right way round
#define BARNACLE_REEL_SEQUENCE		"slurp"
// A neck breaking on the tongue, or a headcrab coming off its zombie
#define BARNACLE_FLINCH_SEQUENCE	"flinch1"
// The throw is aimed at what the player looks at only if that is further off than this
#define BARNACLE_AIM_MIN_DIST		96.0f
// A neck snapping is heard from this far off, however far away it is
#define BARNACLE_SNAP_SOUND_DIST	48.0f
// No throw for this long after the weapon comes out
#define BARNACLE_DEPLOY_DELAY		0.4f	// (as long as it takes to come up: of2_barnacle_equip_time)
// With no hitbox to go by, the tongue has someone by the head if it landed no lower than this under their eyes
#define BARNACLE_HEAD_BELOW_EYES	8.0f
// Reeled up to something too big to kill outright, and biting it
#define BARNACLE_BITE_SEQUENCE		"attack_player"
// How often it bites
#define BARNACLE_BITE_INTERVAL		0.5f

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

// How often a tongue pulled tight on someone it can't kill outright hurts them
#define BARNACLE_STRAIN_INTERVAL	0.25f

// Someone the tongue has hold of who gets this much further off than it is long tears loose
#define BARNACLE_PREY_STRETCH		16.0f

// What the barnacle does when a headcrab reaches it
#define BARNACLE_EAT_SEQUENCE		"attack_smallthings"

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

ConVar of2_barnacle_max_length( "of2_barnacle_max_length", "1500", FCVAR_NONE, "Length of the Barnacle's whole tongue. Read when thrown." );
ConVar of2_barnacle_throw_speed( "of2_barnacle_throw_speed", "1100", FCVAR_NONE, "Speed the Barnacle's tip is thrown at." );
ConVar of2_barnacle_throw_up( "of2_barnacle_throw_up", "120", FCVAR_NONE, "Upward speed added to the Barnacle's throw." );
ConVar of2_barnacle_lead_cone( "of2_barnacle_lead_cone", "8", FCVAR_NONE, "Aiming ahead of someone moving, the crosshair is on what is behind them and the Barnacle threw for that distance, over their head. With prey within this many degrees of the crosshair and nearer than what it is on, the throw is for their distance instead. 0 turns it off." );
ConVar of2_barnacle_throw_slack( "of2_barnacle_throw_slack", "0.01", FCVAR_NONE, "How much more tongue than the tip needs comes out behind it during the throw, as a share. 0 is a dead straight line." );
ConVar of2_barnacle_tip_weight( "of2_barnacle_tip_weight", "150", FCVAR_NONE, "The Barnacle's tip weighs this many times one point of its tongue. Heavier, the tongue trailing behind slows the throw less." );
ConVar of2_barnacle_rope_drag( "of2_barnacle_rope_drag", "4", FCVAR_NONE, "Air drag on the Barnacle's tongue (not its tip), so it hangs back in an arc and settles instead of whipping about." );
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
ConVar of2_barnacle_spacing( "of2_barnacle_spacing", "20", FCVAR_NONE, "The Barnacle's tongue is simulated as a point about every this much of its length (up to 80 points), so it is as stiff and hangs the same way whether it is short or long. Smaller is suppler." );
ConVar of2_barnacle_corners( "of2_barnacle_corners", "1", FCVAR_NONE, "Where the straight line between two points of the Barnacle's tongue goes through a corner (one on a floor, the next over its edge), the two are held to each other by the way round the corner. 0: straight through it, as before. (The drawing has its own switch, of2_rope_corners.)" );
ConVar of2_barnacle_drape( "of2_barnacle_drape", "1", FCVAR_NONE, "The length of the Barnacle's tongue lies on and drapes over loose physics objects, as it does over the world. 0: only over the world." );
ConVar of2_barnacle_slide_speed( "of2_barnacle_slide_speed", "120", FCVAR_NONE, "Where the Barnacle's tongue goes over an edge it slides along the edge, towards where it pulls on it evenly. This is its speed at the hardest pull. 0: it stays where it first touched." );
ConVar of2_barnacle_slide_friction( "of2_barnacle_slide_friction", "0.35", FCVAR_NONE, "How sticky the Barnacle's tongue is on an edge: how lopsided the pull has to be before it slides along it (0 always, 1 only when dragged almost straight along it). A climb rope has its own, lower." );
ConVar of2_barnacle_prey( "of2_barnacle_prey", "1", FCVAR_NONE, "The Barnacle's tongue takes hold of living things: headcrabs (reeled in and eaten), the headcrab on a zombie, and hostile people. 0: it passes through them all." );
ConVar of2_barnacle_eat_health( "of2_barnacle_eat_health", "10", FCVAR_NONE, "Health the player gets when the Barnacle eats a headcrab." );
ConVar of2_barnacle_flick_time( "of2_barnacle_flick_time", "1.37", FCVAR_NONE, "How long the whip of the Barnacle in the hand takes when it snaps a neck, start to finish. The neck goes a little over two fifths of the way through, with the mouth at the top." );
ConVar of2_barnacle_alert_delay( "of2_barnacle_alert_delay", "1", FCVAR_NONE, "How long after the Barnacle's tongue lands on someone they take to notice, in seconds: the time there is to break their neck unnoticed." );
ConVar of2_barnacle_strain_damage( "of2_barnacle_strain_damage", "30", FCVAR_NONE, "Damage a second the Barnacle's tongue does, pulled tight, to someone it can't kill outright: an elite soldier, or anyone it has by something other than the head or chest." );
ConVar of2_barnacle_corpses( "of2_barnacle_corpses", "1", FCVAR_NONE, "While the player has the Barnacle, what dies leaves a body the tongue can take hold of (a ragdoll on the server, as the gravity gun's charged form makes them). 0: bodies are as in Half-Life 2, only there for the eye." );
ConVar of2_barnacle_corpse_speed( "of2_barnacle_corpse_speed", "0.5", FCVAR_NONE, "With a body on its tongue the Barnacle reels and pulls at this share of its usual speed." );
ConVar of2_barnacle_corpse_mass( "of2_barnacle_corpse_mass", "250", FCVAR_NONE, "Heaviest body the Barnacle pulls in. A body is pulled whatever of2_barnacle_anchor_mass says, up to this; a heavier one holds the player." );
ConVar of2_barnacle_corpse_heft( "of2_barnacle_corpse_heft", "4", FCVAR_NONE, "The Barnacle's tongue has a body by one of its parts, which weighs a few kilos, and pulled that part only as hard as it takes to move a few kilos: too little to bring the rest of the body up over an edge. The part is pulled as if it weighed this many times more. 1 is as it was; too high and the body is flung about." );
ConVar of2_barnacle_bite_damage( "of2_barnacle_bite_damage", "40", FCVAR_NONE, "Damage a second the Barnacle does to an enemy too big for its tongue to kill (an antlion guard), once the player has reeled themselves up to it and goes on reeling." );
ConVar of2_barnacle_bite_range( "of2_barnacle_bite_range", "120", FCVAR_NONE, "The Barnacle bites such an enemy once there is no more tongue than this between them. The tongue has hold of a part of its body, which the player can't get right up to." );
ConVar of2_barnacle_auto_release( "of2_barnacle_auto_release", "1", FCVAR_NONE, "The Barnacle takes its tongue back by itself once there is nothing left to reel: the player stands on the ground as near as the tongue gets them, or what it holds has been pulled all the way in." );
ConVar of2_barnacle_edge_haul( "of2_barnacle_edge_haul", "64", FCVAR_NONE, "Something on the Barnacle's tip that is pulled up to an edge the tongue goes over is, from this close to the edge, pulled towards a spot out past it instead, so it comes over the edge and is not jammed under it. 0 turns it off." );
ConVar of2_barnacle_edge_clear( "of2_barnacle_edge_clear", "24", FCVAR_NONE, "How far out from an edge, and on past it, that spot is when what is pulled has reached the edge." );
ConVar of2_barnacle_catch( "of2_barnacle_catch", "1", FCVAR_NONE, "A loose tongue (nothing on its tip) that lies over this many edges or more holds the player's weight there when they come off the ground: it sticks to the edge nearest its tip, and what is past that hangs on. 0: only a tip that has stuck holds them." );
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
// Bodies are client-side in Half-Life 2: nothing the tongue could touch. While
// the player has a Barnacle they are made on the server instead
// (CBaseCombatCharacter::BecomeRagdoll).
//-----------------------------------------------------------------------------
bool OF2_BarnacleWantsCorpses( void )
{
	if ( !of2_barnacle_corpses.GetBool() )
		return false;

	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	return pPlayer && pPlayer->Weapon_OwnsThisType( "weapon_barnacle" ) != NULL;
}

static bool IsCorpse( CBaseEntity *pEntity )
{
	return pEntity && Ragdoll_IsPropRagdoll( pEntity );
}

//-----------------------------------------------------------------------------
// The living things the tongue takes hold of, and what it does with each
//-----------------------------------------------------------------------------
enum BarnaclePrey_t
{
	PREY_NONE = 0,
	// Carried on the tip, alive, and eaten when it reaches the barnacle
	PREY_HEADCRAB,
	// Its headcrab is pulled off it, and that is carried
	PREY_ZOMBIE,
	// Held by where it was hit; killed when the tongue is pulled tight
	PREY_HUMAN,
	// Too big for any of that: it holds the player, who reels up to it, and is bitten
	PREY_BIG,
};

static BarnaclePrey_t BarnaclePreyKind( CBaseEntity *pEntity, CBaseEntity *pPlayer )
{
	if ( !of2_barnacle_prey.GetBool() || pEntity == NULL )
		return PREY_NONE;

	CAI_BaseNPC *pNPC = pEntity->MyNPCPointer();
	if ( pNPC == NULL || !pNPC->IsAlive() )
		return PREY_NONE;

	if ( dynamic_cast<CBaseHeadcrab *>( pNPC ) )
	{
		// (not one that a barnacle, this or another, already has)
		return pNPC->IsEFlagSet( EFL_IS_BEING_LIFTED_BY_BARNACLE ) ? PREY_NONE : PREY_HEADCRAB;
	}

	CNPC_BaseZombie *pZombie = dynamic_cast<CNPC_BaseZombie *>( pNPC );
	if ( pZombie )
		return pZombie->OF2_IsHeadless() ? PREY_NONE : PREY_ZOMBIE;

	if ( pPlayer == NULL || pNPC->IRelationType( pPlayer ) != D_HT )
		return PREY_NONE;

	// People: soldiers, police, citizens. Only those who are after the player.
	if ( pNPC->GetHullType() == HULL_HUMAN && dynamic_cast<CAI_BaseActor *>( pNPC ) )
		return PREY_HUMAN;

	// Other enemies that walk the ground: antlions and their guards, hunters.
	// What flies, the small machines and the fixed ones, the tongue passes through.
	Hull_t iHull = pNPC->GetHullType();
	if ( pNPC->GetMoveType() == MOVETYPE_STEP && !( pNPC->GetFlags() & FL_FLY ) &&
		 iHull != HULL_TINY && iHull != HULL_TINY_CENTERED && iHull != HULL_SMALL_CENTERED )
		return PREY_BIG;

	return PREY_NONE;
}

//-----------------------------------------------------------------------------
// The part of a body nearest a point: its bone, its hit group, and the middle
// of it in that bone's space
//-----------------------------------------------------------------------------
static bool NearestHitbox( CBaseAnimating *pAnimating, const Vector &vecPoint, int *pBone, int *pGroup, Vector *pLocal )
{
	CStudioHdr *pStudioHdr = pAnimating->GetModelPtr();
	if ( pStudioHdr == NULL )
		return false;

	mstudiohitboxset_t *pSet = pStudioHdr->pHitboxSet( pAnimating->GetHitboxSet() );
	if ( pSet == NULL || pSet->numhitboxes == 0 )
		return false;

	float flBest = FLT_MAX;
	for ( int i = 0; i < pSet->numhitboxes; i++ )
	{
		mstudiobbox_t *pBox = pSet->pHitbox( i );

		matrix3x4_t matBone;
		pAnimating->GetBoneTransform( pBox->bone, matBone );

		Vector vecLocal = ( pBox->bbmin + pBox->bbmax ) * 0.5f;
		Vector vecMiddle;
		VectorTransform( vecLocal, matBone, vecMiddle );

		float flDist = vecMiddle.DistToSqr( vecPoint );
		if ( flDist < flBest )
		{
			flBest = flDist;
			*pBone = pBox->bone;
			*pGroup = pBox->group;
			*pLocal = vecLocal;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// What the tip sticks to: the world, props and anything else solid. Not the
// player, and of NPCs only those the tongue has a use for (BarnaclePreyKind). Loose physics objects count whatever collision group they
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
			if ( pEntity->IsPlayer() )
				return false;

			// (the entity the trace passes is the player)
			if ( pEntity->IsNPC() )
				return BarnaclePreyKind( pEntity, const_cast<CBaseEntity *>( EntityFromEntityHandle( GetPassEntity() ) ) ) != PREY_NONE;

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
// The tongue: a row of points from the barnacle to the tip. Source's rope
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

	// The tip has stopped at vecTip. Every point loses the speed it has that
	// way over the ground and keeps the rest: sideways, and up or down
	void	StopRunning( const Vector &vecTip );

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
	// The edges the straight way from the start to a held tip goes over, in
	// that order (the tether's bends). The points are not tied to them: pulled
	// tight the tongue cannot be shorter than the way over them, and that is all.
	Vector	m_vecVia[OF2_TETHER_MAX_PIVOTS];
	int		m_nVia;

private:
	// Lays nCount points evenly along the way the tongue lies now, moving as it does
	void	Relay( int nCount );
	// The shortest way from the start to the held tip: straight, or over m_vecVia
	int		GetLine( Vector *pLine ) const;

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
	m_nVia = 0;
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

void CBarnacleRope::StopRunning( const Vector &vecTip )
{
	// The way the throw went, on the level. (Thrown straight up or down there
	// is no such way, and it is the line itself.)
	Vector vecAhead = vecTip - m_Nodes[0].m_vPos;
	Vector vecLevel( vecAhead.x, vecAhead.y, 0.0f );
	if ( vecLevel.Length() > 0.1f * vecAhead.Length() )
	{
		vecAhead = vecLevel;
	}

	if ( VectorNormalize( vecAhead ) < 0.001f )
		return;

	for ( int i = 1; i < m_nCount - 1; i++ )
	{
		Vector vecMove = m_Nodes[i].m_vPos - m_Nodes[i].m_vPrevPos;
		vecMove -= vecAhead * DotProduct( vecMove, vecAhead );
		m_Nodes[i].m_vPrevPos = m_Nodes[i].m_vPos - vecMove;
	}
}

int CBarnacleRope::GetLine( Vector *pLine ) const
{
	int nLine = 0;
	pLine[nLine++] = m_vecStart;
	for ( int i = 0; i < m_nVia && i < OF2_TETHER_MAX_PIVOTS; i++ )
	{
		pLine[nLine++] = m_vecVia[i];
	}
	pLine[nLine++] = m_vecTipPin;
	return nLine;
}

void CBarnacleRope::Relay( int nCount )
{
	Vector vecPos[OF2_TONGUE_NODES];
	Vector vecMove[OF2_TONGUE_NODES];
	int nSource = m_nCount;

	for ( int i = 0; i < m_nCount; i++ )
	{
		vecPos[i] = m_Nodes[i].m_vPos;
		vecMove[i] = m_Nodes[i].m_vPos - m_Nodes[i].m_vPrevPos;
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
		Vector vecLine[OF2_TETHER_MAX_PIVOTS + 2];
		int nLine = GetLine( vecLine );
		float flLine = 0.0f;
		for ( int j = 1; j < nLine; j++ )
		{
			flLine += vecLine[j].DistTo( vecLine[j - 1] );
		}

		flLength = MAX( flLength, flLine );
	}

	flLength = MAX( flLength, 1.0f );

	// A point about every so much tongue. Not changed for every little
	// difference in length: only once the spacing is well off.
	float flSpacing = MAX( of2_barnacle_spacing.GetFloat(), 4.0f );
	float flLinks = flLength / flSpacing;
	int nWanted = clamp( (int)( flLinks + 0.5f ) + 1, 2, OF2_TONGUE_NODES );
	if ( nWanted != m_nCount && fabs( flLinks - ( m_nCount - 1 ) ) > 0.75f )
	{
		Relay( nWanted );
	}
	else if ( m_bFeeding && m_Nodes[1].m_vPos.DistTo( m_Nodes[0].m_vPos ) > 1.75f * flLength / ( m_nCount - 1 ) )
	{
		// A long throw runs out of points to add, and what is fed out after
		// that would all be one stretch from the start to the first point,
		// growing. When the throw ended that stretch was pulled in to the
		// length of any other in a single tick.
		Relay( m_nCount );
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

	// The points only know about the world one by one. With one lying on a floor
	// and the next hanging over its edge, the straight line between them goes
	// through the corner, and held to each other along it they were pulled into
	// the wall and up to the lip. Where that is so, the tongue between the two
	// is the way out to the corner and on from it (OF2_RopeLinkCorner), and each
	// is drawn towards the corner. Looked for from where the points were a tick
	// ago, which is clear of things. Nothing changes for a link with a clear line.
	CTraceFilterTongueDrape filter( m_pDrapeIgnore, m_nDrapeIgnore, of2_barnacle_drape.GetBool() );
	bool bCorner[OF2_TONGUE_NODES];
	Vector vecCorner[OF2_TONGUE_NODES];
	bool bCorners = m_bCollide && nPasses > 0 && of2_barnacle_corners.GetBool();
	for ( int i = 0; i < iTip; i++ )
	{
		bCorner[i] = bCorners && OF2_RopeLinkCorner( pNodes[i].m_vPrevPos, pNodes[i + 1].m_vPrevPos, BARNACLE_TONGUE_WIDTH * 0.5f, MASK_SOLID, &filter, &vecCorner[i] );
	}

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
			float flNear = ( i == 0 ) ? 0.0f : m_flInvMass[i];
			float flFar = ( i + 1 == iTip && m_bTipPinned ) ? 0.0f : m_flInvMass[i + 1];
			if ( flNear + flFar <= 0.0f )
				continue;

			if ( bCorner[i] )
			{
				Vector vecToNear = vecCorner[i] - pNodes[i].m_vPos;
				Vector vecToFar = vecCorner[i] - pNodes[i + 1].m_vPos;
				float flNearWay = VectorNormalize( vecToNear );
				float flFarWay = VectorNormalize( vecToFar );
				float flOver = flNearWay + flFarWay - m_flSegment;
				if ( flOver <= 0.0f )
					continue;

				pNodes[i].m_vPos += vecToNear * MIN( flOver * flNear / ( flNear + flFar ), flNearWay );
				pNodes[i + 1].m_vPos += vecToFar * MIN( flOver * flFar / ( flNear + flFar ), flFarWay );
				continue;
			}

			Vector vecLink = pNodes[i + 1].m_vPos - pNodes[i].m_vPos;
			float flLink = vecLink.Length();
			if ( flLink <= m_flSegment || flLink < 0.001f )
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

	// Where the tongue lies against one of the edges the straight way to its
	// tip goes over, it runs over that edge as over a pulley: what is past the
	// edge can be no further from it than the tongue left over once it has got
	// there from the start, and likewise back from a held tip. So taking the
	// tongue in draws the far part up to the edge and over it, rather than
	// straight at the barnacle through whatever the edge belongs to. Nothing is
	// moved to the edge and no point is tied to it; and only where the tongue
	// has come down on it, since one still in the air above is not held by it.
	// (The user asked for this in place of starting the tongue again from the
	// edge the moment the straight way first crossed it.)
	float flReach = MAX( of2_barnacle_spacing.GetFloat(), 4.0f );
	float flBefore = 0.0f;
	for ( int k = 0; k < m_nVia && k < OF2_TETHER_MAX_PIVOTS && !m_bFeeding; k++ )
	{
		// (how much tongue it takes to get here from the start, at the least)
		flBefore += m_vecVia[k].DistTo( ( k == 0 ) ? m_vecStart : m_vecVia[k - 1] );

		// Where along the tongue it comes nearest this edge, and how near
		float flNearest = FLT_MAX;
		float flAt = 0.0f;
		for ( int i = 0; i < iTip; i++ )
		{
			float t = 0.0f;
			float flDist = CalcDistanceToLineSegment( m_vecVia[k], pNodes[i].m_vPos, pNodes[i + 1].m_vPos, &t );
			if ( flDist < flNearest )
			{
				flNearest = flDist;
				flAt = i + clamp( t, 0.0f, 1.0f );
			}
		}

		// All of it from within half a point's spacing; less from further off,
		// so the tongue isn't tugged as it comes down on the edge
		float flHold = clamp( 2.0f - 2.0f * flNearest / flReach, 0.0f, 1.0f );
		if ( flHold <= 0.0f )
			continue;

		for ( int i = (int)flAt + 1; i <= iTip; i++ )
		{
			if ( i == iTip && m_bTipPinned )
				break;

			Vector vecOut = pNodes[i].m_vPos - m_vecVia[k];
			float flOut = vecOut.Length();
			float flMost = MAX( m_flSegment * i - flBefore, 0.0f ) + flNearest;
			if ( flOut > flMost && flOut > 0.001f )
			{
				pNodes[i].m_vPos = m_vecVia[k] + vecOut * ( 1.0f + ( flMost / flOut - 1.0f ) * flHold );
			}
		}

		if ( !m_bTipPinned )
			continue;

		float flAfter = 0.0f;
		for ( int j = k; j < m_nVia; j++ )
		{
			flAfter += m_vecVia[j].DistTo( ( j + 1 < m_nVia ) ? m_vecVia[j + 1] : m_vecTipPin );
		}

		for ( int i = 1; i <= (int)flAt; i++ )
		{
			Vector vecOut = pNodes[i].m_vPos - m_vecVia[k];
			float flOut = vecOut.Length();
			float flMost = MAX( m_flSegment * ( iTip - i ) - flAfter, 0.0f ) + flNearest;
			if ( flOut > flMost && flOut > 0.001f )
			{
				pNodes[i].m_vPos = m_vecVia[k] + vecOut * ( 1.0f + ( flMost / flOut - 1.0f ) * flHold );
			}
		}
	}

	if ( m_bTipPinned )
	{
		// Held at both ends, a point can be no further off the shortest way
		// between them (straight, or over the edges it bends round) than where
		// its two reaches meet. The two checks either side of this one move a
		// point towards an end, which is along that way and hardly towards it:
		// on their own they let a tongue with a unit of slack sag three times
		// as far as it could, and bounce there.
		Vector vecLine[OF2_TETHER_MAX_PIVOTS + 2];
		int nLine = GetLine( vecLine );
		float flLine = 0.0f;
		for ( int j = 1; j < nLine; j++ )
		{
			flLine += vecLine[j].DistTo( vecLine[j - 1] );
		}

		for ( int i = 1; i < iTip && flLine > 0.001f; i++ )
		{
			float flNear = m_flSegment * i;
			float flFar = m_flSegment * ( iTip - i );
			float flAlong = ( flLine * flLine + flNear * flNear - flFar * flFar ) / ( 2.0f * flLine );
			float flMost = sqrt( MAX( flNear * flNear - flAlong * flAlong, 0.0f ) );

			// The two reaches only meet like that when neither end is nearer the
			// point than the place they meet at. With far more tongue than the
			// way is long (a high lob that lands close by), one reach lies
			// inside the other, and the point can be anywhere the shorter one
			// lets it. (Without this the limit came out as nothing, and the
			// whole arc was put on the line within three ticks of the tip
			// landing: the snap the user kept seeing on slow, high throws.)
			if ( flAlong < 0.0f )
			{
				flMost = flNear;
			}
			else if ( flAlong > flLine )
			{
				flMost = flFar;
			}

			Vector vecOn = vecLine[0];
			float flOff = FLT_MAX;
			for ( int j = 1; j < nLine; j++ )
			{
				Vector vecClosest;
				CalcClosestPointOnLineSegment( pNodes[i].m_vPos, vecLine[j - 1], vecLine[j], vecClosest );
				float flDist = vecClosest.DistTo( pNodes[i].m_vPos );
				if ( flDist < flOff )
				{
					flOff = flDist;
					vecOn = vecClosest;
				}
			}

			if ( flOff > flMost && flOff > 0.001f )
			{
				pNodes[i].m_vPos = vecOn + ( pNodes[i].m_vPos - vecOn ) * ( flMost / flOff );
			}
		}

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
	// On someone alive, who goes about their business until it is pulled tight
	TIP_PREY,
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
	void	PlayReaction( const char *pszSequence );
	void	Throw( void );
	void	ResetTongue( void );
	void	StartRetract( void );
	void	TongueThink( void );

	// Where the tongue comes out of the barnacle
	Vector	GetMouthPos( CBasePlayer *pOwner );

	// The tip
	bool	GetTipPin( Vector *pPin );
	void	StickTip( CHL2_Player *pPlayer, const trace_t &tr );
	bool	CatchOnEdge( CHL2_Player *pPlayer );
	void	FreeTip( void );
	void	CheckTip( void );

	// The living: what the tip does on touching one, the headcrab it carries,
	// and the person it has hold of
	bool	StickToNPC( CHL2_Player *pPlayer, CAI_BaseNPC *pNPC, const trace_t &tr );
	void	GrabHeadcrab( CBaseHeadcrab *pCrab );
	void	CarryPrey( void );
	void	ReleaseCarried( void );
	void	EatCarried( CBasePlayer *pOwner );
	void	UpdatePrey( CHL2_Player *pPlayer );
	void	BitePrey( CHL2_Player *pPlayer );
	void	WindUpSnap( void );
	void	SnapPrey( CHL2_Player *pPlayer, CAI_BaseNPC *pNPC );
	void	RipHeadcrab( CHL2_Player *pPlayer, CAI_BaseNPC *pNPC );

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
	float	GetHaulScale( void ) const;
	bool	HoldsCorpse( void ) const;
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
	// What the throw left the tongue longer than it needs to be: it hangs by that until it is reeled
	float		m_flSlack;
	// When the throw ended
	float		m_flSlackTime;
	// The tongue has stuck where it lies over an edge, not by its tip: the player
	// hangs from there (TIP_ANCHORED, m_vecTipFixed is that place) and the tip
	// stays loose, with m_flTail of tongue past the edge
	bool		m_bCaught;
	float		m_flTail;

	int			m_iTip;
	// What the tip is on, if it is not the world, and where on it (m_TipHold.vecLocal)
	bool		m_bTipOnEntity;
	Vector		m_vecTipFixed;
	BarnacleHold_t m_TipHold;
	float		m_flTipStickTime;

	// A headcrab on the tip, alive: it goes where the tip goes
	CHandle<CBaseHeadcrab> m_hCarried;
	// Someone the tip has hold of (m_TipHold.hEntity, TIP_PREY): by which bone,
	// whether pulling tight kills them at once, and when it next hurts them if not
	int			m_iPreyBone;
	bool		m_bPreySnap;
	// ...whether it is the head it has them by (nothing else gets hurt), and when they notice
	bool		m_bPreyHead;
	float		m_flPreyAlertTime;
	// ...or, if it is a zombie, takes its headcrab off instead
	bool		m_bPreyZombie;
	float		m_flNextStrain;
	// When the neck in its grip goes (WindUpSnap); 0 while none is about to
	float		m_flSnapTime;
	// The tip is on an enemy too big to kill outright (TIP_ANCHORED, by
	// m_iPreyBone); m_bBiting while the barnacle is at it
	bool		m_bTipBig;
	bool		m_bBiting;

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

	// The barnacle is busy with something (PlayReaction) until then; reeling
	// doesn't start its own animation over it
	float		m_flReactTime;
};

LINK_ENTITY_TO_CLASS( weapon_barnacle, CWeaponBarnacle );

PRECACHE_WEAPON_REGISTER( weapon_barnacle );

IMPLEMENT_SERVERCLASS_ST( CWeaponBarnacle, DT_WeaponBarnacle )
END_SEND_TABLE()

BEGIN_DATADESC( CWeaponBarnacle )

	DEFINE_FIELD( m_hTongue,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_bTongueOut,		FIELD_BOOLEAN ),
	// (so that a headcrab saved on the tip is let go of on the load)
	DEFINE_FIELD( m_hCarried,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_flReactTime,	FIELD_TIME ),

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
	m_flSlackTime = 0.0f;
	m_bCaught = false;
	m_flTail = 0.0f;
	m_iTip = TIP_FREE;
	m_bTipOnEntity = false;
	m_vecTipFixed.Init();
	m_flTipStickTime = 0.0f;
	m_iPreyBone = -1;
	m_bPreySnap = false;
	m_bPreyZombie = false;
	m_bPreyHead = false;
	m_flPreyAlertTime = 0.0f;
	m_flNextStrain = 0.0f;
	m_flSnapTime = 0.0f;
	m_bTipBig = false;
	m_bBiting = false;
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
	m_flReactTime = 0.0f;

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
	PrecacheScriptSound( "NPC_Barnacle.BreakNeck" );
	PrecacheScriptSound( "Weapon_Barnacle.BreakNeck" );
	PrecacheScriptSound( "NPC_Barnacle.FinalBite" );

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

//-----------------------------------------------------------------------------
// Something the barnacle does once, over whatever it was doing, and then goes
// back to that
//-----------------------------------------------------------------------------
void CWeaponBarnacle::PlayReaction( const char *pszSequence )
{
	float flTime = PlaySequence( pszSequence, 1.0f, true );
	m_flReactTime = gpGlobals->curtime + flTime;
	SetWeaponIdleTime( m_flReactTime );
}

bool CWeaponBarnacle::Deploy( void )
{
	bool bDeployed = BaseClass::Deploy();

	// Not thrown by the click that brought it out. (Throwing something carried by hand puts the
	// weapon away and brings it back out on that very click, and the tongue went with the
	// thing thrown: the user saw that.)
	m_bAttackReleased = false;
	m_flNextPrimaryAttack = MAX( m_flNextPrimaryAttack.Get(), gpGlobals->curtime + BARNACLE_DEPLOY_DELAY );
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

	if ( m_bReeling && gpGlobals->curtime >= m_flReactTime )
	{
		// It swallows for as long as it reels (the sequence loops), or bites
		// what it has been reeled up to, and goes back to idling as soon as the
		// button is let go
		PlaySequence( m_bBiting ? BARNACLE_BITE_SEQUENCE : BARNACLE_REEL_SEQUENCE );
		SetWeaponIdleTime( gpGlobals->curtime );
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

	// The mouth is well off to the side of the eyes (it is where the model's
	// is), and the throw is a lob: it leaves with some speed upwards and then
	// falls. Thrown straight ahead it went past what the player aimed at, and
	// thrown straight at it, over it (the user: over a metrocop's head). So
	// when they look at something in reach, it is thrown so that the tip's arc
	// goes through that very spot. People are found by their hitboxes: a head
	// can be outside the box an NPC collides by.
	float flSpeed = of2_barnacle_throw_speed.GetFloat();
	Vector vecExtra( 0.0f, 0.0f, of2_barnacle_throw_up.GetFloat() );

	trace_t trAim;
	UTIL_TraceLine( pOwner->EyePosition(), pOwner->EyePosition() + vecForward * m_flMaxLength, MASK_SHOT, pOwner, COLLISION_GROUP_NONE, &trAim );
	// Leading someone who runs across, the crosshair is on the wall behind them, and the arc made
	// for that distance is still over their head where they are (the user saw that). So with
	// prey close to the crosshair and nearer than what it is on, the throw is for the spot on the
	// line of sight that is as far away as they are: where the player aims, at their distance.
	float flCone = of2_barnacle_lead_cone.GetFloat();
	if ( flCone > 0.0f && BarnaclePreyKind( trAim.m_pEnt, pOwner ) == PREY_NONE )
	{
		Vector vecEye = pOwner->EyePosition();
		float flBest = cos( DEG2RAD( flCone ) );
		float flNear = trAim.endpos.DistTo( vecEye );
		float flAt = -1.0f;

		CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
		for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
		{
			CAI_BaseNPC *pNPC = ppAIs[i];
			if ( pNPC == NULL || !pNPC->IsAlive() || BarnaclePreyKind( pNPC, pOwner ) == PREY_NONE )
				continue;

			Vector vecTo = pNPC->EyePosition() - vecEye;
			float flFar = VectorNormalize( vecTo );
			float flDot = DotProduct( vecTo, vecForward );
			if ( flFar < BARNACLE_AIM_MIN_DIST || flFar * flDot >= flNear || flDot < flBest || !pOwner->FVisible( pNPC, MASK_SOLID_BRUSHONLY ) )
				continue;

			flBest = flDot;
			flAt = flFar * flDot;
		}

		if ( flAt > 0.0f )
		{
			trAim.endpos = vecEye + vecForward * flAt;
			trAim.fraction = flAt / m_flMaxLength;
		}
	}

	if ( trAim.endpos.DistTo( pOwner->EyePosition() ) > BARNACLE_AIM_MIN_DIST && flSpeed > 1.0f )
	{
		Vector vecAim = trAim.endpos - vecMouth;
		if ( trAim.fraction < 1.0f )
		{
			// Where to throw for the arc to come down on it: found by how long
			// the tip takes to get there, which the answer changes a little
			float flTime = vecAim.Length() / flSpeed;
			for ( int i = 0; i < 4; i++ )
			{
				vecAim = trAim.endpos - vecMouth - vecExtra * flTime;
				vecAim.z += 0.5f * GetCurrentGravity() * flTime * flTime;
				flTime = vecAim.Length() / flSpeed;
			}
		}

		if ( VectorNormalize( vecAim ) > 1.0f )
		{
			vecForward = vecAim;
		}
	}

	Vector vecVelocity = vecForward * flSpeed + pOwner->GetAbsVelocity();
	vecVelocity += vecExtra;

	m_Rope.Seed( vecMouth, vecVelocity );
	m_vecLaunchVelocity = vecVelocity;
	m_bWasFeeding = false;
	m_flSlack = 0.0f;
	m_bCaught = false;
	m_flTail = 0.0f;

	m_Tether.Init( vecMouth, vecMouth, 1.0f );
	m_Tether.SetPlayerEnd( TETHER_START );

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

	ReleaseCarried();
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
	m_bTipBig = false;
	m_bBiting = false;
	m_bCaught = false;
	m_flTail = 0.0f;
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

	ReleaseCarried();
	ReleaseHold( m_TipHold );
	for ( int i = 0; i < m_nJunk; i++ )
	{
		ReleaseHold( m_Junk[i] );
	}
	m_nJunk = 0;

	m_iTip = TIP_FREE;
	m_bTipOnEntity = false;
	m_bTipBig = false;
	m_bBiting = false;
	m_bPayingOut = false;
	m_bCaught = false;
	m_flTail = 0.0f;

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
	else if ( ( m_iTip == TIP_PREY || m_bTipBig ) && m_iPreyBone >= 0 && pEntity->GetBaseAnimating() )
	{
		// (someone alive: the tip goes with the part of them it has)
		matrix3x4_t matBone;
		pEntity->GetBaseAnimating()->GetBoneTransform( m_iPreyBone, matBone );
		VectorTransform( m_TipHold.vecLocal, matBone, *pPin );
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

	// Something alive: the tongue has other uses for that
	if ( pEntity && pEntity->IsNPC() )
	{
		if ( !StickToNPC( pPlayer, pEntity->MyNPCPointer(), tr ) )
		{
			// Nothing there it can hold: it slaps off
			m_flTipStickTime = gpGlobals->curtime + BARNACLE_RESTICK_TIME;
		}
		return;
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

	// (a body is pulled in even if it weighs more than the player: it drags)
	float flMost = IsCorpse( pEntity ) ? MAX( of2_barnacle_corpse_mass.GetFloat(), of2_barnacle_anchor_mass.GetFloat() ) : of2_barnacle_anchor_mass.GetFloat();

	bool bLoose = pPhysics && pPhysics->IsMoveable() &&
		flMass <= flMost &&
		!pPhysics->IsAttachedToConstraint( true ) &&
		!( pPhysics->GetGameFlags() & FVPHYSICS_PLAYER_HELD );

	ClearHold( m_TipHold );
	m_bTipBig = false;
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
// The tip has touched something alive. A headcrab is caught and carried. A
// person, or a zombie hit up where its headcrab sits, is held by the part of
// them that was hit, and notices; what becomes of them is decided when the
// tongue is pulled tight (UpdatePrey). Something too big for that holds the
// player, like a wall that walks, and is bitten when they get to it (BitePrey).
// False if there is nothing here for the tongue.
//-----------------------------------------------------------------------------
bool CWeaponBarnacle::StickToNPC( CHL2_Player *pPlayer, CAI_BaseNPC *pNPC, const trace_t &tr )
{
	BarnaclePrey_t iKind = BarnaclePreyKind( pNPC, pPlayer );
	if ( iKind == PREY_NONE )
		return false;

	if ( iKind == PREY_HEADCRAB )
	{
		GrabHeadcrab( dynamic_cast<CBaseHeadcrab *>( pNPC ) );
		return true;
	}

	// The tip is stopped by the box around them; what it has hold of is the
	// part of the body nearest there
	int iBone = -1;
	int iGroup = HITGROUP_GENERIC;
	Vector vecLocal;
	bool bBody = NearestHitbox( pNPC, tr.endpos, &iBone, &iGroup, &vecLocal );
	bool bHead = bBody ? ( iGroup == HITGROUP_HEAD ) : ( tr.endpos.z > pNPC->EyePosition().z - BARNACLE_HEAD_BELOW_EYES );

	// A zombie only by the head, where the headcrab sits (the chest counted too, until the user
	// asked for the head alone, as for people)
	if ( iKind == PREY_ZOMBIE && !bHead )
		return false;

	ClearHold( m_TipHold );
	m_TipHold.hEntity = pNPC;
	m_bTipOnEntity = true;
	m_vecTipFixed = tr.endpos;

	if ( bBody )
	{
		m_iPreyBone = iBone;
		m_TipHold.vecLocal = vecLocal;
	}
	else
	{
		m_iPreyBone = -1;
		VectorITransform( tr.endpos, pNPC->EntityToWorldTransform(), m_TipHold.vecLocal );
	}

	// By the head, pulling tight breaks their neck. An elite soldier's doesn't
	// break; held by the head they are hurt instead. Held anywhere else, nobody
	// is hurt at all: the tongue only has hold of them. (The user asked for that;
	// it used to break a neck from the chest too, and hurt from anywhere.)
	CNPC_Combine *pSoldier = dynamic_cast<CNPC_Combine *>( pNPC );
	m_bPreyHead = bHead;
	m_bPreySnap = bHead && !( pSoldier && pSoldier->IsElite() );
	m_bPreyZombie = ( iKind == PREY_ZOMBIE );
	m_flNextStrain = 0.0f;
	m_flSnapTime = 0.0f;
	m_bTipBig = ( iKind == PREY_BIG );

	m_bPayingOut = false;
	EmitSound( "Weapon_Barnacle.Stick" );

	// They know where that came from: something big at once, a person only after
	// a moment (UpdatePrey), which is the time there is to break their neck
	// without anyone the wiser (the user asked for a second)
	m_flPreyAlertTime = gpGlobals->curtime + of2_barnacle_alert_delay.GetFloat();

	if ( m_bTipBig )
	{
		pNPC->UpdateEnemyMemory( pPlayer, pPlayer->GetAbsOrigin() );
		m_flPreyAlertTime = 0.0f;
		m_bPreySnap = false;
		m_iTip = TIP_ANCHORED;

		Vector vecPin;
		GetTipPin( &vecPin );
		Anchor( pPlayer, vecPin );
		return true;
	}

	m_iTip = TIP_PREY;
	return true;
}

//-----------------------------------------------------------------------------
// A headcrab on the tip stays alive and struggles (CBaseHeadcrab::
// OF2_SetBarnacled); the tip stays loose, and the headcrab is put where it is
// every tick (CarryPrey). Reeled all the way in, it is eaten.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::GrabHeadcrab( CBaseHeadcrab *pCrab )
{
	if ( pCrab == NULL )
		return;

	ReleaseCarried();

	pCrab->OF2_SetBarnacled( true );
	m_hCarried = pCrab;

	m_bPayingOut = false;
	EmitSound( "Weapon_Barnacle.Stick" );
}

void CWeaponBarnacle::CarryPrey( void )
{
	CBaseHeadcrab *pCrab = m_hCarried;
	if ( pCrab == NULL )
		return;

	if ( !pCrab->IsAlive() )
	{
		// Shot off the tongue
		m_hCarried = NULL;
		return;
	}

	// By its middle, but never into anything: the tip lies on the floor, and a
	// headcrab hung by its middle from there was half under it. Its box is let
	// down from where it stands on the tip to where it hangs from it, and stops
	// on what is in the way.
	Vector vecTip = m_Rope.GetTip();
	Vector vecOrigin = vecTip - ( pCrab->WorldSpaceCenter() - pCrab->GetAbsOrigin() );

	CTraceFilterWorldAndPropsOnly filter;
	trace_t tr;
	UTIL_TraceHull( vecTip + Vector( 0, 0, 1 ), vecOrigin, pCrab->WorldAlignMins(), pCrab->WorldAlignMaxs(), MASK_NPCSOLID_BRUSHONLY, &filter, &tr );
	if ( !tr.allsolid )
	{
		vecOrigin = tr.endpos;
	}

	pCrab->SetAbsOrigin( vecOrigin );
	pCrab->SetAbsVelocity( vec3_origin );
}

void CWeaponBarnacle::ReleaseCarried( void )
{
	CBaseHeadcrab *pCrab = m_hCarried;
	m_hCarried = NULL;

	if ( pCrab && pCrab->IsAlive() )
	{
		pCrab->OF2_SetBarnacled( false );
	}
}

//-----------------------------------------------------------------------------
// The headcrab has reached the barnacle, which eats it. That does the player
// some good too, for no reason anyone can give (the user thought it funny).
//-----------------------------------------------------------------------------
void CWeaponBarnacle::EatCarried( CBasePlayer *pOwner )
{
	CBaseHeadcrab *pCrab = m_hCarried;
	m_hCarried = NULL;

	if ( pCrab == NULL || !pCrab->IsAlive() )
		return;

	UTIL_BloodImpact( pCrab->WorldSpaceCenter(), Vector( 0, 0, 1 ), pCrab->BloodColor(), 4 );

	// Gone, without a body left over. (An NPC a barnacle is lifting is not
	// removed by this kind of damage, so first it isn't any more.)
	pCrab->RemoveEFlags( EFL_IS_BEING_LIFTED_BY_BARNACLE );

	CTakeDamageInfo info( this, pOwner, pCrab->GetHealth() + 1000.0f, DMG_SLASH | DMG_REMOVENORAGDOLL );
	info.SetDamagePosition( pCrab->WorldSpaceCenter() );
	pCrab->TakeDamage( info );
	if ( pCrab->IsAlive() )
	{
		UTIL_Remove( pCrab );
	}

	EmitSound( "NPC_Barnacle.FinalBite" );

	if ( pOwner )
	{
		pOwner->TakeHealth( of2_barnacle_eat_health.GetFloat(), DMG_GENERIC );
	}

	float flTime = PlaySequence( BARNACLE_EAT_SEQUENCE, 1.0f, true );
	SetWeaponIdleTime( gpGlobals->curtime + flTime );
	m_flNextPrimaryAttack = gpGlobals->curtime + MAX( flTime, 0.5f );
}

//-----------------------------------------------------------------------------
// Someone on the tip. The tongue gives as they move about, as far as it goes;
// further than that and they tear loose. Reeled tight, it kills them if it
// has them by the head or chest, and hurts them steadily if not.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::UpdatePrey( CHL2_Player *pPlayer )
{
	CBaseEntity *pEntity = m_TipHold.hEntity;
	CAI_BaseNPC *pNPC = pEntity ? pEntity->MyNPCPointer() : NULL;
	if ( pNPC == NULL || !pNPC->IsAlive() )
	{
		FreeTip();
		return;
	}

	float flPath = m_Tether.GetPathLength();
	float flTotal = m_Tether.GetTotalLength();

	if ( flPath > m_flMaxLength + BARNACLE_PREY_STRETCH )
	{
		FreeTip();
		return;
	}

	// They have felt it by now, unless their neck is already on its way
	if ( m_flPreyAlertTime != 0.0f && m_flSnapTime == 0.0f && gpGlobals->curtime >= m_flPreyAlertTime )
	{
		m_flPreyAlertTime = 0.0f;
		pNPC->UpdateEnemyMemory( pPlayer, pPlayer->GetAbsOrigin() );
	}

	// The barnacle is already whipping: the neck goes when its mouth is at the
	// top, whatever the player does in between
	if ( m_flSnapTime != 0.0f )
	{
		if ( gpGlobals->curtime >= m_flSnapTime )
		{
			SnapPrey( pPlayer, pNPC );
		}
		return;
	}

	if ( !m_bReeling )
	{
		if ( flPath > flTotal )
		{
			m_Tether.SetTotalLength( MIN( flPath, m_flMaxLength ) );
		}
		return;
	}

	// Still slack to take up
	if ( flPath < flTotal - 1.0f )
		return;

	if ( m_bPreyZombie )
	{
		RipHeadcrab( pPlayer, pNPC );
		return;
	}

	if ( m_bPreySnap )
	{
		WindUpSnap();
		return;
	}

	// Only the head is worth pulling at
	if ( !m_bPreyHead )
		return;

	if ( gpGlobals->curtime < m_flNextStrain )
		return;

	m_flNextStrain = gpGlobals->curtime + BARNACLE_STRAIN_INTERVAL;

	float flDamage = of2_barnacle_strain_damage.GetFloat() * BARNACLE_STRAIN_INTERVAL;
	if ( pNPC->GetHealth() <= flDamage )
	{
		// That would finish them: the same end as the others
		WindUpSnap();
		return;
	}

	Vector vecPin;
	GetTipPin( &vecPin );

	CTakeDamageInfo info( this, pPlayer, flDamage, DMG_CLUB );
	info.SetDamagePosition( vecPin );
	pNPC->TakeDamage( info );
}

//-----------------------------------------------------------------------------
// The player has reeled themselves up to an enemy the tongue can't kill, and
// goes on reeling: the barnacle bites it, for as long as they do. (The user
// asked for this, and for the tongue to stay on it meanwhile.)
//-----------------------------------------------------------------------------
void CWeaponBarnacle::BitePrey( CHL2_Player *pPlayer )
{
	CBaseEntity *pEntity = m_TipHold.hEntity;
	CAI_BaseNPC *pNPC = pEntity ? pEntity->MyNPCPointer() : NULL;
	if ( pNPC == NULL || !pNPC->IsAlive() )
		return;

	m_bBiting = true;

	if ( gpGlobals->curtime < m_flNextStrain )
		return;

	m_flNextStrain = gpGlobals->curtime + BARNACLE_BITE_INTERVAL;

	Vector vecPin;
	GetTipPin( &vecPin );

	Vector vecDir = pPlayer->EyePosition() - vecPin;
	VectorNormalize( vecDir );
	UTIL_BloodImpact( vecPin, vecDir, pNPC->BloodColor(), 4 );
	EmitSound( "NPC_Barnacle.FinalBite" );

	CTakeDamageInfo info( this, pPlayer, of2_barnacle_bite_damage.GetFloat() * BARNACLE_BITE_INTERVAL, DMG_SLASH );
	info.SetDamagePosition( vecPin );
	pNPC->TakeDamage( info );
}

//-----------------------------------------------------------------------------
// Pulled tight on a zombie, the tongue takes its headcrab off it. The headcrab
// is alive and stays on the tip, carried like any other; what is left of the
// zombie has nothing to drive it, and drops. (The user asked for this to wait
// for the pull, like the neck of a soldier, rather than happen on the hit.)
//-----------------------------------------------------------------------------
void CWeaponBarnacle::RipHeadcrab( CHL2_Player *pPlayer, CAI_BaseNPC *pNPC )
{
	Vector vecPin;
	GetTipPin( &vecPin );

	CNPC_BaseZombie *pZombie = dynamic_cast<CNPC_BaseZombie *>( pNPC );
	if ( pZombie == NULL || pZombie->OF2_IsHeadless() )
	{
		FreeTip();
		return;
	}

	pZombie->ReleaseHeadcrab( pZombie->EyePosition(), vec3_origin, true, false, false );

	// (it doesn't say which headcrab it let go; it is the one that calls the zombie its owner)
	CBaseHeadcrab *pCrab = NULL;
	CBaseEntity *pFound = NULL;
	while ( ( pFound = gEntList.FindEntityByClassname( pFound, pZombie->GetHeadcrabClassname() ) ) != NULL )
	{
		if ( pFound->GetOwnerEntity() == pZombie && pFound->IsAlive() && !pFound->IsEFlagSet( EFL_IS_BEING_LIFTED_BY_BARNACLE ) )
		{
			pCrab = dynamic_cast<CBaseHeadcrab *>( pFound );
			break;
		}
	}

	if ( pCrab == NULL )
	{
		// No room for it to come off there: the tongue slips off instead
		FreeTip();
		return;
	}

	// The tip is loose again, with the headcrab on it
	ClearHold( m_TipHold );
	m_iPreyBone = -1;
	m_iTip = TIP_FREE;
	m_bTipOnEntity = false;

	GrabHeadcrab( pCrab );
	PlayReaction( BARNACLE_FLINCH_SEQUENCE );

	CTakeDamageInfo info( this, pPlayer, pZombie->GetHealth() + 1000.0f, DMG_GENERIC );
	info.SetDamagePosition( vecPin );
	pZombie->TakeDamage( info );
}

//-----------------------------------------------------------------------------
// A neck is about to go: the barnacle in the hand whips first (client:
// c_of2_tongue.cpp), and the neck snaps as its mouth gets to the top
// (UpdatePrey). The user asked for the snap to fall on that moment.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::WindUpSnap( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL || m_flSnapTime != 0.0f )
		return;

	float flTime = MAX( of2_barnacle_flick_time.GetFloat(), 0.0f );
	m_flSnapTime = gpGlobals->curtime + flTime * OF2_BARNACLE_FLICK_SNAP;

	CEffectData flick;
	flick.m_vOrigin = pOwner->EyePosition();
	flick.m_flScale = flTime;
	DispatchEffect( "OF2BarnacleFlick", flick );
}

//-----------------------------------------------------------------------------
// The neck goes. What is left is a ragdoll, made here rather than left to
// their dying so that the tongue can keep hold of it: from now on it is
// something on the tip like any other, and comes in when reeled.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::SnapPrey( CHL2_Player *pPlayer, CAI_BaseNPC *pNPC )
{
	Vector vecPin;
	GetTipPin( &vecPin );

	CTakeDamageInfo info( this, pPlayer, pNPC->GetHealth() + 1000.0f, DMG_CLUB | DMG_REMOVENORAGDOLL );
	info.SetDamagePosition( vecPin );
	info.SetDamageForce( vec3_origin );

	CBaseEntity *pRagdoll = CreateServerRagdoll( pNPC, 0, info, COLLISION_GROUP_INTERACTIVE_DEBRIS, true );

	m_flSnapTime = 0.0f;

	// The crack comes from the way the neck is, but always from the same short
	// distance, so it is as loud from a tongue's length away as from up close
	// (the user asked: played where the neck is, it was too quiet to hear)
	Vector vecEar = pPlayer->EyePosition();
	Vector vecToNeck = vecPin - vecEar;
	float flAway = VectorNormalize( vecToNeck );
	Vector vecCrack = vecEar + vecToNeck * MIN( flAway, BARNACLE_SNAP_SOUND_DIST );

	CPASAttenuationFilter filter( vecCrack, "Weapon_Barnacle.BreakNeck" );
	CBaseEntity::EmitSound( filter, SOUND_FROM_WORLD, "Weapon_Barnacle.BreakNeck", &vecCrack );
	PlayReaction( BARNACLE_FLINCH_SEQUENCE );
	// Stealth: a squadmate who sees this knows where the tongue comes from, and the
	// body others may find is the ragdoll made above
	OF2_KillLeadsTo( pPlayer );
	pNPC->TakeDamage( info );
	OF2_KillLeadsTo( NULL );
	OF2_BodyRagdoll( pNPC, pRagdoll );
	if ( pNPC->IsAlive() )
	{
		// (something made them shrug it off; they don't get to keep walking next to their own body)
		UTIL_Remove( pNPC );
	}

	ClearHold( m_TipHold );
	m_iPreyBone = -1;
	m_iTip = TIP_FREE;
	m_bTipOnEntity = false;

	if ( pRagdoll == NULL )
		return;

	// By the part of it nearest where the tongue had them
	IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int nCount = pRagdoll->VPhysicsGetObjectList( pList, ARRAYSIZE( pList ) );
	if ( nCount <= 0 )
		return;

	IPhysicsObject *pNearest = pList[0];
	float flNearest = FLT_MAX;
	float flMass = 0.0f;
	for ( int i = 0; i < nCount; i++ )
	{
		flMass += pList[i]->GetMass();

		Vector vecPart;
		pList[i]->GetPosition( &vecPart, NULL );
		float flDist = vecPart.DistToSqr( vecPin );
		if ( flDist < flNearest )
		{
			flNearest = flDist;
			pNearest = pList[i];
		}
	}

	m_TipHold.hEntity = pRagdoll;
	m_TipHold.pPhysics = pNearest;
	m_TipHold.flMass = flMass;
	pNearest->GetPosition( &m_vecTipFixed, NULL );
	pNearest->WorldToLocal( &m_TipHold.vecLocal, m_vecTipFixed );

	m_bTipOnEntity = true;
	m_iTip = TIP_PAYLOAD;
	HoldObject( m_TipHold );
}

//-----------------------------------------------------------------------------
// The tip lets go of what it is on, and is loose again
//-----------------------------------------------------------------------------
void CWeaponBarnacle::FreeTip( void )
{
	ReleaseHold( m_TipHold );

	m_iTip = TIP_FREE;
	m_bTipOnEntity = false;
	m_bTipBig = false;
	m_bCaught = false;
	m_flTail = 0.0f;
	m_iPreyBone = -1;

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
	if ( !bGone && m_bTipBig && !pEntity->IsAlive() )
	{
		// (bitten to death, or killed some other way while the tongue had it)
		bGone = true;
	}

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
// A loose tongue lying over an edge takes the player's weight: it is sticky
// all along, not only at its tip. (The user: thrown over a ledge with the
// tip left dangling beyond it, it only held them once reeling had dragged the
// tip against something.) The player hangs from the edge nearest the tip, as
// from a tip stuck there. The tip itself stays loose and what is past the
// edge hangs on from it.
//-----------------------------------------------------------------------------
bool CWeaponBarnacle::CatchOnEdge( CHL2_Player *pPlayer )
{
	int nEdges = of2_barnacle_catch.GetInt();
	int nPivots = m_Tether.GetPivotCount();
	if ( nEdges <= 0 || nPivots < nEdges )
		return false;

	if ( m_iTip != TIP_FREE || m_bPayingOut || m_bRetracting || m_hCarried != NULL || gpGlobals->curtime < m_flTipStickTime )
		return false;

	// Only their weight does it: standing, the tongue is theirs to drag about
	if ( pPlayer->GetGroundEntity() != NULL || pPlayer->IsOnTether() )
		return false;

	Vector vecCatch = m_Tether.GetPoint( nPivots );
	m_Tether.TruncatePivots( nPivots - 1 );

	ClearHold( m_TipHold );
	m_bTipBig = false;
	m_bTipOnEntity = false;
	m_vecTipFixed = vecCatch;
	m_iTip = TIP_ANCHORED;
	m_bCaught = true;

	Anchor( pPlayer, vecCatch );

	// The rest of what lies out is past the edge, and stays as it is
	m_flTail = MAX( m_Rope.GetLength() - m_Tether.GetTotalLength(), 0.0f );
	m_flSlack = 0.0f;
	m_flLeashMax = MAX( m_flMaxLength - m_flTail, m_Tether.GetTotalLength() );
	m_flLeash = m_flLeashMax;

	EmitSound( "Weapon_Barnacle.Stick" );
	return true;
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
	else if ( m_bReeling && m_flSlack > 0.0f )
	{
		// More tongue is out than the way to the tip is long (the arc of the
		// throw). Reeling takes that in first, and the tongue straightens; it
		// only pulls once it is as short as that way (TongueThink).
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
		if ( m_bTipBig )
		{
			// On an enemy the tongue stays, however near: there the barnacle
			// gets its teeth in instead
			if ( m_Tether.GetPathLength() <= of2_barnacle_bite_range.GetFloat() )
			{
				BitePrey( pPlayer );
			}
		}
		else if ( of2_barnacle_auto_release.GetBool() && pPlayer->GetGroundEntity() != NULL &&
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
	else if ( m_bReeling && m_flSlack > 0.0f )
	{
		// (the slack the throw left comes in first, as in UpdateAnchored)
	}
	else if ( m_bReeling )
	{
		// Slack is taken up first, and faster
		float flSpeed = of2_barnacle_reel_speed.GetFloat();
		bool bSlack = flTotal > flPath + 1.0f;
		if ( !bSlack )
		{
			flSpeed *= GetHaulScale();
		}

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
				// (with a headcrab on the tip, that goes in too)
				EatCarried( pOwner );
				ResetTongue();
				return false;
			}
		}
		else if ( flTotal < BARNACLE_MIN_LENGTH )
		{
			// What it holds stays out in front of the barnacle
			flTotal = BARNACLE_MIN_LENGTH;
			flSpeed = 0.0f;

			// ...and once it is there, it has come as far as it can: nothing
			// the barnacle could swallow, so the tongue lets go of it and comes
			// back by itself, as it does when the player has reeled themselves
			// all the way (the user asked for the same here). Something on the
			// tip that is still on its way in is waited for.
			// Not a body: that stays on the tongue until the player lets go (the
			// user asked: there is to be a way to hide bodies).
			if ( of2_barnacle_auto_release.GetBool() && !HoldsCorpse() &&
				 ( m_iTip != TIP_PAYLOAD || flPath <= BARNACLE_MIN_LENGTH + BARNACLE_RELEASE_DIST ) )
			{
				StartRetract();
				return true;
			}
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
// The tongue's whole path from the tip in, as it lies
//-----------------------------------------------------------------------------
int CWeaponBarnacle::GetPath( Vector *pPath ) const
{
	int nPath = 0;
	for ( int i = m_Rope.GetCount() - 1; i >= 0; i-- )
	{
		pPath[nPath++] = m_Rope.GetPos( i );
	}

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
// A body comes in slower than anything else: at the usual speed it arrived at
// the barnacle as a missile (of2_barnacle_corpse_speed, which the user asked for)
//-----------------------------------------------------------------------------
bool CWeaponBarnacle::HoldsCorpse( void ) const
{
	bool bCorpse = ( m_iTip == TIP_PAYLOAD && IsCorpse( m_TipHold.hEntity ) );
	for ( int i = 0; i < m_nJunk && !bCorpse; i++ )
	{
		bCorpse = IsCorpse( m_Junk[i].hEntity );
	}

	return bCorpse;
}

float CWeaponBarnacle::GetHaulScale( void ) const
{
	return HoldsCorpse() ? clamp( of2_barnacle_corpse_speed.GetFloat(), 0.05f, 1.0f ) : 1.0f;
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
		hold.flMaxSpeed = of2_barnacle_pull_max_speed.GetFloat() * GetHaulScale();
		hold.flMaxForce = GetGrip( 0.0f ) * flAccel;
		// Pulled straight at an edge the tongue goes over, a body ends up jammed
		// under the lip, falls behind and is let go (the user: it drops at the
		// edge). So as it gets near, it is pulled towards a spot out from the
		// corner and on along the tongue instead, further the nearer it is, and
		// so comes up over the edge.
		int nPivots = m_Tether.GetPivotCount();
		float flHaul = of2_barnacle_edge_haul.GetFloat();
		if ( nPivots > 0 && flHaul > 0.0f )
		{
			Vector vecAttach;
			hold.pPhysics->LocalToWorld( &vecAttach, hold.vecLocal );

			Vector vecEdge = hold.vecPull;
			Vector vecOn = m_Tether.GetPoint( nPivots - 1 ) - vecEdge;
			Vector vecBack = vecAttach - vecEdge;
			VectorNormalize( vecOn );
			float flTo = VectorNormalize( vecBack );
			if ( flTo < flHaul )
			{
				// (the corner is inside the turn the tongue makes there)
				Vector vecOut = -( vecOn + vecBack );
				if ( VectorNormalize( vecOut ) < 0.01f )
				{
					vecOut.Init( 0.0f, 0.0f, 1.0f );
				}

				float flShare = 1.0f - flTo / flHaul;
				hold.vecPull = vecEdge + ( vecOut + vecOn ) * ( of2_barnacle_edge_clear.GetFloat() * flShare );
			}
		}

		hold.bPullSet = true;

		// (an object that has gone to sleep is not simulated)
		hold.pPhysics->Wake();

		// A body is not let go for falling behind: only by the player
		if ( !IsCorpse( hold.hEntity ) && IsSlipping( hold, flSnagDist * BARNACLE_TIP_SNAG_DIST, flSnagTime * BARNACLE_TIP_SNAG_TIME ) )
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
		hold.flMaxSpeed = BARNACLE_JUNK_PULL_SPEED * ( IsCorpse( pEntity ) ? GetHaulScale() : 1.0f );
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

	// The edges the straight way to the tip goes over, from the tip's end back.
	// The client draws the tongue round the ones it lies against.
	int nBends = 0;
	if ( m_bCaught )
	{
		// (the edge it has stuck to is one of them)
		pTongue->m_vecBends.Set( nBends++, m_vecTipFixed );
	}
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
	m_bBiting = false;

	m_Tether.SetSliding( of2_barnacle_slide_speed.GetFloat(), of2_barnacle_slide_friction.GetFloat() );

	// The player let go of a tongue they hung from (jumped off, was teleported)
	if ( m_iTip == TIP_ANCHORED && ( !pPlayer->IsOnTether() || pPlayer->GetTetherOwner() != this ) )
	{
		StartRetract();
	}

	Vector vecMouth = GetMouthPos( pPlayer );
	Vector vecNear = ( m_iTip == TIP_ANCHORED ) ? COF2Tether::GetPlayerHandPos( pPlayer, true ) : vecMouth;

	// All of the tongue is simulated, from the barnacle to the tip, whatever
	// edges the straight way between the two goes over (the tether's bends).
	// (It used to be only the part past the last bend, started again from there
	// whenever a bend came or went, with a straight line drawn for the rest:
	// a tongue thrown over an edge jumped to it. The user rejected that.)
	Vector vecPin;
	bool bPinned = GetTipPin( &vecPin );
	Vector vecStart = vecNear;

	// The throw: the tongue is fed out of the barnacle rather than pulled out
	// by the tip. Every bit of it leaves at the velocity the tip was thrown at
	// and flies free, so the whole tongue lies along the arc the tip took. Its
	// length is whatever has come out.
	bool bFeeding = m_bPayingOut && !bPinned;

	float flLength = m_Tether.GetTotalLength();
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
		// The throw is over. What came out lies along the arc the tip flew,
		// which is longer than the straight line the tongue is otherwise
		// measured by. That much more of it is out, and stays out: the tongue
		// falls and hangs by its own weight. Reeling takes it up first.
		if ( m_bWasFeeding )
		{
			m_flSlack = MAX( m_Rope.GetLength() - flLength, 0.0f );
			m_flSlackTime = gpGlobals->curtime;

			// The tip has stuck. Every point behind it is still flying at the
			// speed of the throw, which nothing took off it while it was fed
			// out. What carries them on towards the tip goes with it. The rest
			// stays: sideways (thrown while strafing, the tongue is moving with the
			// player) and up or down, so each part of the arc goes on rising or
			// falling as it was and the tongue settles by its own weight. (Stopped
			// dead, it lost its swing and dropped from a standstill; with only the
			// speed along the tongue taken off, a straight throw still did, since
			// there all of the speed is along the tongue. The user rejected both.)
			if ( bPinned )
			{
				m_Rope.StopRunning( vecPin );
			}
		}

		// The player's weight does the same as reeling: hanging from it, the
		// tongue is pulled tight by itself. (The user asked: left as thrown, it
		// hung in a curve beside the straight line they swung on.)
		bool bHanging = ( m_iTip == TIP_ANCHORED && pPlayer->GetGroundEntity() == NULL );

		if ( m_bReeling || m_bRetracting || bHanging )
		{
			// (slack comes in faster than tongue that pulls, as it does in UpdateLoose)
			float flSpeed = of2_barnacle_reel_speed.GetFloat() * ( m_bRetracting ? 1.0f : BARNACLE_SLACK_REEL );
			m_flSlack = MAX( m_flSlack - flSpeed * TICK_INTERVAL, 0.0f );
		}

		flLength += m_flSlack;
		if ( m_bCaught )
		{
			flLength += m_flTail;
		}
	}

	m_bWasFeeding = bFeeding;
	m_Rope.m_bFeeding = bFeeding;
	m_Rope.m_vecFeedVelocity = m_vecLaunchVelocity;

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
	m_Rope.m_bSticky = !bPinned && !m_bRetracting && m_hCarried == NULL && gpGlobals->curtime >= m_flTipStickTime;
	m_Rope.m_bCollide = !m_bRetracting;

	m_Rope.m_nVia = m_Tether.GetPivotCount();
	for ( int i = 0; i < m_Rope.m_nVia; i++ )
	{
		m_Rope.m_vecVia[i] = m_Tether.GetPoint( i + 1 );
	}

	// Stuck to an edge: the tongue goes over that too, and its tip is loose
	if ( m_bCaught && m_Rope.m_nVia < OF2_TETHER_MAX_PIVOTS )
	{
		m_Rope.m_vecVia[m_Rope.m_nVia++] = m_vecTipFixed;
	}

	m_Rope.Step( vecStart, flLength, ( bPinned && !m_bCaught ) ? &vecPin : NULL );

	if ( of2_barnacle_debug.GetBool() && bPinned && gpGlobals->curtime < m_flSlackTime + 1.5f )
	{
		// The first moments after the tip sticks: how far the tongue stands off
		// the straight line (most of any point; above it is positive)
		Vector vecLine = vecPin - vecStart;
		float flLine = VectorNormalize( vecLine );
		float flOff = 0.0f;
		for ( int i = 1; i < m_Rope.GetCount() - 1; i++ )
		{
			Vector vecOut = m_Rope.GetPos( i ) - vecStart;
			vecOut -= vecLine * DotProduct( vecOut, vecLine );
			if ( vecOut.Length() > fabs( flOff ) )
			{
				flOff = ( vecOut.z < 0.0f ) ? -vecOut.Length() : vecOut.Length();
			}
		}

		Msg( "tongue +%.3f: line %.1f, lies %.1f, allowed %.1f (slack %.2f), off the line %.1f, %d points\n",
			gpGlobals->curtime - m_flSlackTime, flLine, m_Rope.GetLength(), flLength, m_flSlack, flOff, m_Rope.GetCount() );
	}

	bool bJustAnchored = false;
	trace_t tr;
	if ( !bPinned && m_Rope.GetTipHit( &tr ) )
	{
		StickTip( pPlayer, tr );
		bPinned = GetTipPin( &vecPin );
		bJustAnchored = ( m_iTip == TIP_ANCHORED );

		// The tongue is now measured by the way to where the tip stuck, and
		// what lies out can be far longer than that: thrown to its full length,
		// the tip was held back and fell, and came down nearer by. All of that
		// is slack too, or the tongue is put on the straight line in one tick
		// (the user: it snapped the moment the tip touched the ground). It
		// stays as it lies and sinks by its own weight.
		if ( bPinned )
		{
			m_flSlack = MAX( m_Rope.GetLength() - m_Tether.GetTotalLength(), 0.0f );
			m_flSlackTime = gpGlobals->curtime;
		}
	}

	if ( !bPinned && CatchOnEdge( pPlayer ) )
	{
		bPinned = GetTipPin( &vecPin );
		bJustAnchored = true;
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

	// What is alive on the tip
	CarryPrey();
	if ( m_iTip == TIP_PREY )
	{
		UpdatePrey( pPlayer );
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

	// (a body hangs on the part the tongue has: of2_barnacle_corpse_heft)
	if ( IsCorpse( pHold->hEntity ) )
	{
		flGive /= MAX( of2_barnacle_corpse_heft.GetFloat(), 1.0f );
	}

	if ( flGive <= 0.0f )
		return SIM_NOTHING;

	// The push that makes the change, or as much of it as the grip is good for
	float flPush = MAX( flChange / flGive, -pHold->flMaxForce * deltaTime );

	Vector vecLinear = vecCenterVelocity * ( flPush / deltaTime );
	pObject->WorldToLocalVector( &linear, vecLinear );
	angular = angCenterVelocity * ( flPush / deltaTime );

	return SIM_LOCAL_ACCELERATION;
}
