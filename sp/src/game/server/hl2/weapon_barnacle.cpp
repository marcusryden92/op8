//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: Barnacle. A live barnacle in the hand. Its tongue is a chain
//			of heavy, clinging beads that the player throws like a heaving
//			line; backing away then drags it along, up and over whatever it
//			lies across.
//
//			The throw works like a line gun: only the leading few beads are
//			thrown, and they pull the rest out of the barnacle one at a time,
//			so the tongue is taut behind them all the way out.
//
//			The beads are simulated and linked to each other. The last stretch,
//			from the barnacle to the nearest bead, is a COF2Tether: it has a
//			length and bends around the world, and holds that bead back with a
//			soft pull. How long each stretch is comes from
//			of2_barnacle_proportions; by default they are all the same.
//
//			No ammo. Primary fire throws; held again afterwards it reels in.
//			Secondary fire held pays out; reload hauls the tongue back in. Once
//			the tongue has caught on the world or on something heavier than the
//			player, they hang from it as from a climb rope.
//
//=============================================================================//

#include "cbase.h"
#include "basehlcombatweapon.h"
#include "player.h"
#include "in_buttons.h"
#include "Sprite.h"
#include "vphysics/constraints.h"
#include "of2_tether.h"
#include "hl_gamemovement.h"
#include "hl2_player.h"
#include "hl2/of2_tongue_shared.h"
#include "physics.h"
#include "ndebugoverlay.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Most beads of2_barnacle_beads can ask for
#define BARNACLE_MAX_BEADS			32

#define BARNACLE_TONGUE_WIDTH		2.0f

// What the barnacle in the hand does when nothing else is going on
#define BARNACLE_IDLE_SEQUENCE		"idle01"
// Throwing: the barnacle swallowing, played backwards
#define BARNACLE_THROW_SEQUENCE		"slurp"

// What a weld to a loose object holds before it tears: this much, plus the
// object's mass times of2_barnacle_stick_strength. It takes this many times
// more to twist it off.
#define BARNACLE_WELD_BASE			40.0f
#define BARNACLE_WELD_TORQUE		20.0f

// A bead torn off something doesn't stick again for this long
#define BARNACLE_RESTICK_TIME		0.75f

// of2_barnacle_show_beads: a flat blue square on each bead. The texture is 16 units
// across at scale 1.
#define BARNACLE_BEAD_SPRITE		"sprites/of2_bead.vmt"
#define BARNACLE_BEAD_SPRITE_SCALE	0.4f
#define BARNACLE_BEAD_COLOR			50, 110, 255

// The beads are meant to cling. A ball rolls however much grip it has, so they
// are all but stopped from turning, and given the grippiest surface there is.
#define BARNACLE_BEAD_SURFACE		"brakingrubbertire"
// No drag of their own through the air, though: that only shortens the throw.
#define BARNACLE_BEAD_DRAG			0.0f
#define BARNACLE_BEAD_TURN_DRAG		40.0f

// Within the thrown cluster, each bead nearer the far end leaves this much faster
#define BARNACLE_THROW_LEAD			0.02f

// The tongue stops paying out once its nearest bead is this slow, or after this long
// How far the nearest bead may be past the tongue's length before it is pulled back
#define BARNACLE_PULL_SLACK			2.0f

// A bead this close to the barnacle when reeling in goes back into it
#define BARNACLE_ABSORB_DIST		16.0f

// Retracting hauls the beads in this hard, and gives up and takes them in at once after this long
#define BARNACLE_RETRACT_PULL		30.0f
#define BARNACLE_RETRACT_MAX_TIME	1.5f

// Reeling in pulls the nearest bead up to this many times the reel speed, to catch up
#define BARNACLE_REEL_PULL_SCALE	1.5f

// Closest reeling in brings the player to where the tongue is fixed
#define BARNACLE_MIN_SWING			24.0f

#define BARNACLE_SETTLE_SPEED		60.0f
#define BARNACLE_SETTLE_MIN_TIME	0.3f
#define BARNACLE_SETTLE_MAX_TIME	3.0f

ConVar of2_barnacle_max_length( "of2_barnacle_max_length", "900", FCVAR_NONE, "Length of the Barnacle's whole tongue, from the hand to the last bead. Read when thrown." );
ConVar of2_barnacle_beads( "of2_barnacle_beads", "5", FCVAR_NONE, "Number of beads on the Barnacle's tongue (1 to 32). Read when thrown." );
ConVar of2_barnacle_proportions( "of2_barnacle_proportions", "1", FCVAR_NONE, "How the Barnacle's tongue length is shared out, as a list of numbers starting at the far end: the first is the stretch between the last bead and the one before it, and so on, and the last is the stretch from the first bead to the hand. Each stretch gets max length / sum of the list * its number. A list shorter than the bead count repeats its last number. Read when thrown." );
ConVar of2_barnacle_throw_cluster( "of2_barnacle_throw_cluster", "3", FCVAR_NONE, "How many of the Barnacle's beads are thrown. The rest are pulled out of it behind them, one by one." );
ConVar of2_barnacle_feed_speed( "of2_barnacle_feed_speed", "1.2", FCVAR_NONE, "A bead pulled out of the Barnacle starts at this share of the speed of the bead pulling it. Below 1 it lags and gets yanked along; above 1 it catches up and the tongue goes slack." );
ConVar of2_barnacle_throw_speed( "of2_barnacle_throw_speed", "700", FCVAR_NONE, "Speed the Barnacle's beads are thrown at." );
ConVar of2_barnacle_throw_up( "of2_barnacle_throw_up", "120", FCVAR_NONE, "Upward speed added to the Barnacle's throw." );
ConVar of2_barnacle_throw_spread( "of2_barnacle_throw_spread", "3", FCVAR_NONE, "Each of the Barnacle's thrown beads goes up to this many degrees off the aim." );
ConVar of2_barnacle_throw_speed_spread( "of2_barnacle_throw_speed_spread", "0.12", FCVAR_NONE, "Each of the Barnacle's thrown beads goes up to this share faster or slower." );
ConVar of2_barnacle_bead_mass( "of2_barnacle_bead_mass", "100", FCVAR_NONE, "Mass of one of the Barnacle's beads. Read when thrown." );
ConVar of2_barnacle_bead_radius( "of2_barnacle_bead_radius", "10", FCVAR_NONE, "Size of the Barnacle's beads. Read when thrown." );
ConVar of2_barnacle_pull( "of2_barnacle_pull", "4", FCVAR_NONE, "How hard the Barnacle pulls its nearest bead back when the tongue is stretched: speed per unit over its length." );
ConVar of2_barnacle_pull_max_speed( "of2_barnacle_pull_max_speed", "200", FCVAR_NONE, "Fastest the Barnacle pulls its nearest bead back." );
ConVar of2_barnacle_head_mass( "of2_barnacle_head_mass", "3", FCVAR_NONE, "The Barnacle's thrown beads (the head) weigh this many times the others, so the head flies on and the rest trails. Read when thrown." );
ConVar of2_barnacle_air_drag( "of2_barnacle_air_drag", "0.8", FCVAR_NONE, "Air drag on the Barnacle's trailing beads (not the head), so the tongue hangs back in an arc behind the head. Read when thrown." );
ConVar of2_barnacle_anchor_mass( "of2_barnacle_anchor_mass", "85", FCVAR_NONE, "A Barnacle bead stuck to something heavier than this holds the player instead of being dragged: they hang from it as from a rope. The player weighs about 85." );
ConVar of2_barnacle_reel_speed( "of2_barnacle_reel_speed", "200", FCVAR_NONE, "Speed the Barnacle reels its tongue in (primary fire, held), pulling the player up when it is anchored." );
ConVar of2_barnacle_extrude_speed( "of2_barnacle_extrude_speed", "200", FCVAR_NONE, "Speed the Barnacle pays its tongue out (secondary fire, held), lowering the player when it is anchored." );
ConVar of2_barnacle_retract_speed( "of2_barnacle_retract_speed", "1500", FCVAR_NONE, "Speed the Barnacle hauls its tongue back in on reload." );
ConVar of2_barnacle_pump( "of2_barnacle_pump", "120", FCVAR_NONE, "How hard forward/back pushes the swing while hanging from the Barnacle (scaled by of2_tether_pump_scale, like a climb rope's pump strength)." );
ConVar of2_barnacle_sticky( "of2_barnacle_sticky", "1", FCVAR_NONE, "The Barnacle's thrown beads (the cluster at the far end) stick to what they land on." );
ConVar of2_barnacle_stick_strength( "of2_barnacle_stick_strength", "8", FCVAR_NONE, "How firmly a Barnacle bead holds a loose object, in multiples of the object's own weight. Pulled harder than that, it tears off." );
ConVar of2_barnacle_show_beads( "of2_barnacle_show_beads", "0", FCVAR_NONE, "Mark each of the Barnacle's beads with a blue square, to see what they do. Read when thrown." );
ConVar of2_barnacle_debug( "of2_barnacle_debug", "0", FCVAR_NONE, "Draw the Barnacle's tongue tether and beads as debug overlays." );

static const char *s_pTongueContext = "BarnacleTongueThink";
static const char *s_pStickContext = "BarnacleBeadStickThink";

//-----------------------------------------------------------------------------
// One bead of the chain: a small heavy ball. Nothing to see; the client draws
// the tongue through them (COF2Tongue below).
//-----------------------------------------------------------------------------
class COF2BarnacleBead : public CBaseEntity
{
	DECLARE_CLASS( COF2BarnacleBead, CBaseEntity );
	DECLARE_DATADESC();

public:
	void	Spawn( void );
	bool	CreateVPhysics( void );
	void	UpdateOnRemove( void );
	void	VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );

	// The tongue is drawn through the beads on the client, model or not
	int		UpdateTransmitState( void ) { return SetTransmitState( FL_EDICT_ALWAYS ); }

	// The weapon takes its tongue back in on a load rather than restore a half-built chain
	int		ObjectCaps( void ) { return BaseClass::ObjectCaps() | FCAP_DONT_SAVE; }

	// A sticky bead holds on to the first thing it lands on
	void	SetSticky( bool bSticky )	{ m_bSticky = bSticky; }
	bool	IsStuck( void ) const		{ return m_bStuckInPlace || m_pWeld != NULL; }

	// Stuck to something the player can't drag: the world, or anything heavier than flMass
	bool	IsAnchoring( float flMass ) const { return m_bStuckInPlace || ( m_pWeld != NULL && !m_bWeldBroken && m_flWeldedMass > flMass ); }

	// Lets go of whatever it is stuck to
	void	Unstick( void );

	// Once a tick: a weld that has torn can't be taken down from inside the physics step
	void	CheckWeld( void );

private:
	void	StickThink( void );
	void	InputConstraintBroken( inputdata_t &inputdata );

	bool	m_bSticky;
	// To the world, or to something else that doesn't move
	bool	m_bStuckInPlace;
	// To something that does
	IPhysicsConstraint *m_pWeld;
	EHANDLE	m_hWeldedTo;
	float	m_flWeldedMass;
	bool	m_bWeldBroken;

	// What it just hit, until the next think can stick it there
	EHANDLE	m_hStickTo;
	float	m_flNextStickTime;
};

LINK_ENTITY_TO_CLASS( of2_barnacle_bead, COF2BarnacleBead );

BEGIN_DATADESC( COF2BarnacleBead )
	DEFINE_THINKFUNC( StickThink ),
	// The physics engine reports a broken weld this way (the weld's game data is this entity)
	DEFINE_INPUTFUNC( FIELD_VOID, "ConstraintBroken", InputConstraintBroken ),
END_DATADESC()

void COF2BarnacleBead::Spawn( void )
{
	BaseClass::Spawn();

	CreateVPhysics();

	// Hits the world, props and NPCs; not the player, and not the other beads
	SetCollisionGroup( COLLISION_GROUP_INTERACTIVE_DEBRIS );
}

void COF2BarnacleBead::UpdateOnRemove( void )
{
	Unstick();
	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// The base class would play impact sounds and hurt what was hit; a bead does
// neither. Sticking has to wait for a think: this is inside the physics step.
//-----------------------------------------------------------------------------
void COF2BarnacleBead::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	if ( !m_bSticky || !of2_barnacle_sticky.GetBool() || IsStuck() || m_hStickTo != NULL || gpGlobals->curtime < m_flNextStickTime )
		return;

	CBaseEntity *pOther = pEvent->pEntities[!index];
	if ( pOther == NULL || pOther->IsPlayer() || pOther->IsNPC() || FClassnameIs( pOther, "of2_barnacle_bead" ) )
		return;

	m_hStickTo = pOther;
	SetContextThink( &COF2BarnacleBead::StickThink, gpGlobals->curtime, s_pStickContext );
}

void COF2BarnacleBead::StickThink( void )
{
	CBaseEntity *pOther = m_hStickTo;
	m_hStickTo = NULL;

	IPhysicsObject *pPhysics = VPhysicsGetObject();
	if ( pOther == NULL || pPhysics == NULL || IsStuck() )
		return;

	IPhysicsObject *pOtherPhysics = pOther->VPhysicsGetObject();
	if ( pOther->IsWorld() || pOther->GetMoveType() != MOVETYPE_VPHYSICS || pOtherPhysics == NULL || !pOtherPhysics->IsMoveable() )
	{
		// The world, or anything else that stays where it is: so does the bead
		pPhysics->EnableMotion( false );
		m_bStuckInPlace = true;
		return;
	}

	// A loose object: welded on, with a limit so that something snagged tears
	// off instead of holding the whole tongue
	constraint_fixedparams_t fixed;
	fixed.Defaults();
	fixed.InitWithCurrentObjectState( pOtherPhysics, pPhysics );
	fixed.constraint.forceLimit = BARNACLE_WELD_BASE + pOtherPhysics->GetMass() * of2_barnacle_stick_strength.GetFloat();
	fixed.constraint.torqueLimit = fixed.constraint.forceLimit * BARNACLE_WELD_TORQUE;

	m_pWeld = physenv->CreateFixedConstraint( pOtherPhysics, pPhysics, NULL, fixed );
	if ( m_pWeld == NULL )
		return;

	m_pWeld->SetGameData( (void *)this );
	m_hWeldedTo = pOther;
	m_flWeldedMass = pOtherPhysics->GetMass();
	m_bWeldBroken = false;

	PhysDisableObjectCollisions( pOtherPhysics, pPhysics );
}

void COF2BarnacleBead::InputConstraintBroken( inputdata_t &inputdata )
{
	m_bWeldBroken = true;
}

void COF2BarnacleBead::CheckWeld( void )
{
	if ( m_pWeld && ( m_bWeldBroken || m_hWeldedTo == NULL ) )
	{
		Unstick();

		// Not straight back onto what it was just torn from
		m_flNextStickTime = gpGlobals->curtime + BARNACLE_RESTICK_TIME;
	}
}

void COF2BarnacleBead::Unstick( void )
{
	IPhysicsObject *pPhysics = VPhysicsGetObject();

	if ( m_pWeld )
	{
		physenv->DestroyConstraint( m_pWeld );
		m_pWeld = NULL;

		CBaseEntity *pOther = m_hWeldedTo;
		if ( pOther && pOther->VPhysicsGetObject() && pPhysics )
		{
			PhysEnableObjectCollisions( pOther->VPhysicsGetObject(), pPhysics );
		}
	}

	if ( m_bStuckInPlace && pPhysics )
	{
		pPhysics->EnableMotion( true );
		pPhysics->Wake();
	}

	m_hWeldedTo = NULL;
	m_bWeldBroken = false;
	m_bStuckInPlace = false;
}

//-----------------------------------------------------------------------------
// The tongue as the client draws it (client\hl2\c_of2_tongue.cpp): the points
// it runs through from the far end in, each one either a bead or a place where
// it bends over an edge. The barnacle end the client finds itself, in the
// player's hand.
//-----------------------------------------------------------------------------
class COF2Tongue : public CBaseEntity
{
	DECLARE_CLASS( COF2Tongue, CBaseEntity );

public:
	DECLARE_SERVERCLASS();

	int		UpdateTransmitState( void ) { return SetTransmitState( FL_EDICT_ALWAYS ); }
	int		ObjectCaps( void ) { return BaseClass::ObjectCaps() | FCAP_DONT_SAVE; }

	// A bead, or NULL for a bend at m_vecNodes
	CNetworkArray( EHANDLE, m_hNodes, OF2_TONGUE_MAX_NODES );
	CNetworkArray( Vector, m_vecNodes, OF2_TONGUE_MAX_NODES );
	CNetworkVar( int, m_nNodes );
	CNetworkVar( float, m_flWidth );
};

LINK_ENTITY_TO_CLASS( of2_tongue, COF2Tongue );

IMPLEMENT_SERVERCLASS_ST( COF2Tongue, DT_OF2Tongue )
	SendPropArray3( SENDINFO_ARRAY3( m_hNodes ), SendPropEHandle( SENDINFO_ARRAY( m_hNodes ) ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_vecNodes ), SendPropVector( SENDINFO_ARRAY( m_vecNodes ), -1, SPROP_COORD ) ),
	SendPropInt( SENDINFO( m_nNodes ), 7, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO( m_flWidth ) ),
END_SEND_TABLE()

bool COF2BarnacleBead::CreateVPhysics( void )
{
	float flRadius = MAX( of2_barnacle_bead_radius.GetFloat(), 1.0f );

	SetSolid( SOLID_BBOX );
	SetCollisionBounds( Vector( -flRadius, -flRadius, -flRadius ), Vector( flRadius, flRadius, flRadius ) );

	objectparams_t params = g_PhysDefaultObjectParams;
	params.pGameData = static_cast<void *>( this );
	params.mass = MAX( of2_barnacle_bead_mass.GetFloat(), 0.1f );

	int nSurface = physprops->GetSurfaceIndex( BARNACLE_BEAD_SURFACE );
	if ( nSurface < 0 )
	{
		nSurface = physprops->GetSurfaceIndex( "rubber" );
	}

	IPhysicsObject *pPhysicsObject = physenv->CreateSphereObject( flRadius, nSurface, GetAbsOrigin(), GetAbsAngles(), &params, false );
	if ( pPhysicsObject == NULL )
		return false;

	float flDrag = BARNACLE_BEAD_DRAG;
	float flTurnDrag = BARNACLE_BEAD_TURN_DRAG;
	pPhysicsObject->SetDamping( &flDrag, &flTurnDrag );

	VPhysicsSetObject( pPhysicsObject );
	SetMoveType( MOVETYPE_VPHYSICS );
	pPhysicsObject->Wake();

	return true;
}

//-----------------------------------------------------------------------------
// CWeaponBarnacle
//-----------------------------------------------------------------------------
class CWeaponBarnacle : public CBaseHLCombatWeapon
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

private:
	float	PlaySequence( const char *pszSequence, float flRate = 1.0f, bool bRestart = false );
	void	Throw( void );
	void	ResetTongue( void );
	void	TongueThink( void );

	// Works out m_nBeads and m_flStretch from the convars
	void	MeasureTongue( void );

	// Where the tongue comes out of the barnacle
	Vector	GetMouthPos( CBasePlayer *pOwner );

	// One more bead out of the barnacle, linked to the one before it
	COF2BarnacleBead *AddBead( const Vector &vecPos, const Vector &vecVelocity, bool bSticky );

	// Tells the client what to draw the tongue through
	void	UpdateTongue( void );
	void	WrapLinks( void );

	// The tether runs from the barnacle to the bead that came out last
	void	AttachTether( CBasePlayer *pOwner );

	// Keeps the nearest bead within what the tether has left for it
	// True if it had to pull
	bool	HoldBackBead( IPhysicsObject *pBead, float flPull, float flMaxSpeed );
	// Passes the pull on the nearest bead down the chain, along every taut link
	void	PullChain( void );

	void	RemoveBead( int iBead );
	bool	AbsorbBead( CBasePlayer *pOwner );
	void	StartRetract( void );
	bool	UpdateLoose( CBasePlayer *pOwner );

	// Hanging from the tongue once it has caught on something the player can't move
	int		FindAnchor( void );
	void	Anchor( CHL2_Player *pPlayer, int iAnchor );
	void	Unanchor( void );
	void	UpdateAnchored( CHL2_Player *pPlayer );

private:
	COF2Tether	m_Tether;

	// From the far end inwards: 0 leads the throw, m_nBeadsOut - 1 is the one
	// nearest the barnacle, which the tether ends on
	CHandle<COF2BarnacleBead>	m_hBeads[BARNACLE_MAX_BEADS];
	EHANDLE		m_hBeadSprites[BARNACLE_MAX_BEADS];
	CHandle<COF2Tongue>	m_hTongue;
	// i joins bead i and bead i + 1
	IPhysicsConstraint *m_pBeadLinks[BARNACLE_MAX_BEADS - 1];
	// ...and is drawn bent over edges like this (not saved: the tongue goes on a load)
	COF2Tether	m_LinkTethers[BARNACLE_MAX_BEADS - 1];

	// Beads on the whole tongue, and how many of them are out of the barnacle
	int			m_nBeads;
	int			m_nBeadsOut;
	// i is the length of tongue between bead i and the next one in, which for
	// the last bead is the barnacle
	float		m_flStretch[BARNACLE_MAX_BEADS];

	bool		m_bTongueOut;
	// Still on its way out: beads keep coming as the ones before them pull
	bool		m_bPayingOut;
	float		m_flThrowTime;

	// Hanging from bead m_iAnchor. m_flLeash is how far the player can get from it,
	// along the tongue; m_flLeashMax all the tongue there is from there in.
	bool		m_bAnchored;
	int			m_iAnchor;
	float		m_flLeash;
	float		m_flLeashMax;

	bool		m_bRetracting;
	float		m_flRetractTime;

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

	DEFINE_EMBEDDED( m_Tether ),
	DEFINE_ARRAY( m_hBeads,			FIELD_EHANDLE, BARNACLE_MAX_BEADS ),
	DEFINE_ARRAY( m_hBeadSprites,	FIELD_EHANDLE, BARNACLE_MAX_BEADS ),
	DEFINE_FIELD( m_hTongue,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_bTongueOut,		FIELD_BOOLEAN ),

	// m_pBeadLinks can't be saved, so a loaded game starts with the tongue in (OnRestore).
	// Everything else about a tongue that is out goes with it.

	DEFINE_THINKFUNC( TongueThink ),

END_DATADESC()

CWeaponBarnacle::CWeaponBarnacle()
{
	for ( int i = 0; i < BARNACLE_MAX_BEADS - 1; i++ )
	{
		m_pBeadLinks[i] = NULL;
	}

	for ( int i = 0; i < BARNACLE_MAX_BEADS; i++ )
	{
		m_flStretch[i] = 0.0f;
	}

	m_nBeads = 0;
	m_nBeadsOut = 0;
	m_bTongueOut = false;
	m_bPayingOut = false;
	m_flThrowTime = 0.0f;
	m_bAnchored = false;
	m_iAnchor = 0;
	m_flLeash = 0.0f;
	m_flLeashMax = 0.0f;
	m_bRetracting = false;
	m_flRetractTime = 0.0f;
	m_bReeling = false;
	m_bExtruding = false;
	m_bAttackReleased = true;
}

void CWeaponBarnacle::Precache( void )
{
	UTIL_PrecacheOther( "of2_tongue" );
	PrecacheScriptSound( "Weapon_Barnacle.Throw" );
	PrecacheModel( BARNACLE_BEAD_SPRITE );
	UTIL_PrecacheOther( "of2_barnacle_bead" );

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// The beads and their links aren't saved, and stay behind on a level change
//-----------------------------------------------------------------------------
void CWeaponBarnacle::OnRestore( void )
{
	BaseClass::OnRestore();

	for ( int i = 0; i < BARNACLE_MAX_BEADS - 1; i++ )
	{
		m_pBeadLinks[i] = NULL;
	}

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
		if ( m_bAnchored )
		{
			// Letting go while hanging: back in at once, ready to throw again in mid-air
			ResetTongue();
		}
		else if ( !m_bRetracting )
		{
			StartRetract();
			m_flNextPrimaryAttack = gpGlobals->curtime + 0.4f;
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
// Shares the tongue's length out between its stretches, by the numbers in
// of2_barnacle_proportions. Like m_flStretch, those start at the far end: the
// first is the stretch between the last bead and the one before it, the last
// is the stretch from the nearest bead to the barnacle.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::MeasureTongue( void )
{
	m_nBeads = clamp( of2_barnacle_beads.GetInt(), 1, BARNACLE_MAX_BEADS );

	// Any list of numbers will do: spaces, commas, brackets
	int nShares = 0;
	const char *pszList = of2_barnacle_proportions.GetString();
	while ( *pszList && nShares < m_nBeads )
	{
		if ( ( *pszList >= '0' && *pszList <= '9' ) || *pszList == '.' )
		{
			char *pszEnd;
			float flValue = strtod( pszList, &pszEnd );
			if ( pszEnd != pszList )
			{
				m_flStretch[nShares++] = flValue;
				pszList = pszEnd;
				continue;
			}
		}

		pszList++;
	}

	// A short list repeats its last number
	for ( int i = nShares; i < m_nBeads; i++ )
	{
		m_flStretch[i] = ( nShares > 0 ) ? m_flStretch[nShares - 1] : 1.0f;
	}

	float flTotal = 0.0f;
	for ( int i = 0; i < m_nBeads; i++ )
	{
		flTotal += m_flStretch[i];
	}

	float flUnit = ( flTotal > 0.0f ) ? of2_barnacle_max_length.GetFloat() / flTotal : 0.0f;
	for ( int i = 0; i < m_nBeads; i++ )
	{
		m_flStretch[i] = MAX( m_flStretch[i] * flUnit, 1.0f );
	}
}

//-----------------------------------------------------------------------------
// Where the barnacle is on screen, unless that is in a wall
//-----------------------------------------------------------------------------
Vector CWeaponBarnacle::GetMouthPos( CBasePlayer *pOwner )
{
	float flRadius = of2_barnacle_bead_radius.GetFloat();

	trace_t tr;
	UTIL_TraceHull( pOwner->EyePosition(), OF2_TetherHoldPos( pOwner, true ),
		Vector( -flRadius, -flRadius, -flRadius ), Vector( flRadius, flRadius, flRadius ),
		MASK_SOLID, pOwner, COLLISION_GROUP_NONE, &tr );

	return tr.endpos;
}

COF2BarnacleBead *CWeaponBarnacle::AddBead( const Vector &vecPos, const Vector &vecVelocity, bool bSticky )
{
	if ( m_nBeadsOut >= m_nBeads )
		return NULL;

	COF2BarnacleBead *pBead = static_cast<COF2BarnacleBead *>( CBaseEntity::Create( "of2_barnacle_bead", vecPos, vec3_angle, GetOwner() ) );
	if ( pBead == NULL )
		return NULL;

	IPhysicsObject *pPhysics = pBead->VPhysicsGetObject();
	if ( pPhysics == NULL )
	{
		UTIL_Remove( pBead );
		return NULL;
	}

	pPhysics->SetVelocity( &vecVelocity, NULL );
	pBead->SetSticky( bSticky );

	if ( bSticky )
	{
		// The head: heavier, so it carries on and pulls the rest out behind it
		pPhysics->SetMass( pPhysics->GetMass() * MAX( of2_barnacle_head_mass.GetFloat(), 0.1f ) );
	}
	else
	{
		// The rest is held back by the air, so the tongue trails behind the head in an arc
		float flDrag = MAX( of2_barnacle_air_drag.GetFloat(), 0.0f );
		float flTurnDrag = BARNACLE_BEAD_TURN_DRAG;
		pPhysics->SetDamping( &flDrag, &flTurnDrag );
	}

	int iBead = m_nBeadsOut++;
	m_hBeads[iBead] = pBead;

	if ( of2_barnacle_show_beads.GetBool() )
	{
		CSprite *pSprite = CSprite::SpriteCreate( BARNACLE_BEAD_SPRITE, vecPos, false );
		if ( pSprite )
		{
			pSprite->SetTransparency( kRenderTransColor, BARNACLE_BEAD_COLOR, 255, kRenderFxNone );
			pSprite->SetScale( BARNACLE_BEAD_SPRITE_SCALE );
			pSprite->SetParent( pBead );
			m_hBeadSprites[iBead] = pSprite;
		}
	}

	if ( iBead > 0 && m_hBeads[iBead - 1] != NULL )
	{
		// A length of tongue to the bead before it: it can go slack but not stretch
		COF2BarnacleBead *pPrev = m_hBeads[iBead - 1];
		IPhysicsObject *pPrevPhysics = pPrev->VPhysicsGetObject();

		// (where it is now, which its entity only catches up with after the physics step)
		Vector vecPrev;
		pPrevPhysics->GetPosition( &vecPrev, NULL );

		constraint_lengthparams_t length;
		length.Defaults();
		length.InitWorldspace( pPrevPhysics, pPhysics, vecPrev, vecPos );
		length.totalLength = m_flStretch[iBead - 1];
		length.minLength = 0.0f;
		m_pBeadLinks[iBead - 1] = physenv->CreateLengthConstraint( pPrevPhysics, pPhysics, NULL, length );
		m_LinkTethers[iBead - 1].Init( vecPrev, vecPos, m_flStretch[iBead - 1] );
	}

	return pBead;
}

void CWeaponBarnacle::AttachTether( CBasePlayer *pOwner )
{
	COF2BarnacleBead *pBead = m_hBeads[m_nBeadsOut - 1];
	Vector vecMouth = GetMouthPos( pOwner );

	m_Tether.Init( vecMouth, pBead->GetAbsOrigin(), vecMouth.DistTo( pBead->GetAbsOrigin() ) );
	m_Tether.SetPlayerEnd( TETHER_START );
	m_Tether.SetHeldEnd( TETHER_START, true );
	m_Tether.SetEndEntity( pBead );
}

//-----------------------------------------------------------------------------
// Throws the leading beads along the aim, each a little off it and the far
// ones a little faster. The rest follow as those pull them out (TongueThink).
//-----------------------------------------------------------------------------
void CWeaponBarnacle::Throw( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	ResetTongue();
	MeasureTongue();

	Vector vecForward, vecRight, vecUp;
	pOwner->EyeVectors( &vecForward, &vecRight, &vecUp );

	Vector vecMouth = GetMouthPos( pOwner );
	float flSpread = tan( DEG2RAD( clamp( of2_barnacle_throw_spread.GetFloat(), 0.0f, 45.0f ) ) );
	float flSpeedSpread = clamp( of2_barnacle_throw_speed_spread.GetFloat(), 0.0f, 0.9f );

	int nCluster = clamp( of2_barnacle_throw_cluster.GetInt(), 1, m_nBeads );
	for ( int i = 0; i < nCluster; i++ )
	{
		Vector vecDir = vecForward + vecRight * RandomFloat( -flSpread, flSpread ) + vecUp * RandomFloat( -flSpread, flSpread );
		VectorNormalize( vecDir );

		float flSpeed = of2_barnacle_throw_speed.GetFloat()
			* ( 1.0f + BARNACLE_THROW_LEAD * ( nCluster - 1 - i ) )
			* ( 1.0f + RandomFloat( -flSpeedSpread, flSpeedSpread ) );

		Vector vecVelocity = vecDir * flSpeed + pOwner->GetAbsVelocity();
		vecVelocity.z += of2_barnacle_throw_up.GetFloat();

		if ( AddBead( vecMouth, vecVelocity, true ) == NULL )
		{
			ResetTongue();
			return;
		}
	}

	AttachTether( pOwner );

	m_bTongueOut = true;
	m_bPayingOut = true;
	m_flThrowTime = gpGlobals->curtime;
	m_bAttackReleased = false;

	EmitSound( "Weapon_Barnacle.Throw" );
	SetWeaponIdleTime( gpGlobals->curtime + PlaySequence( BARNACLE_THROW_SEQUENCE, -1.0f, true ) );
	pOwner->SetAnimation( PLAYER_ATTACK1 );

	m_flNextPrimaryAttack = gpGlobals->curtime + 0.5f;

	SetContextThink( &CWeaponBarnacle::TongueThink, gpGlobals->curtime, s_pTongueContext );
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
		m_hTongue = pTongue;
	}

	// From the far end in: each bead, then where the stretch after it bends.
	// The last stretch, from the nearest bead to the barnacle, is the tether,
	// which runs the other way.
	int nNodes = 0;
	for ( int i = 0; i < m_nBeadsOut && nNodes < OF2_TONGUE_MAX_NODES; i++ )
	{
		// A bead stuck in place goes as a fixed point: the client draws the tongue
		// straight into and out of those, as at a bend. Curved through it, the
		// tongue would bow out around it.
		COF2BarnacleBead *pBead = m_hBeads[i];
		if ( pBead && pBead->IsStuck() && pBead->VPhysicsGetObject() )
		{
			Vector vecBead;
			pBead->VPhysicsGetObject()->GetPosition( &vecBead, NULL );
			pTongue->m_hNodes.Set( nNodes, NULL );
			pTongue->m_vecNodes.Set( nNodes, vecBead );
		}
		else
		{
			pTongue->m_hNodes.Set( nNodes, pBead );
			pTongue->m_vecNodes.Set( nNodes, vec3_origin );
		}
		nNodes++;

		if ( i < m_nBeadsOut - 1 )
		{
			const COF2Tether &link = m_LinkTethers[i];
			for ( int j = 0; j < link.GetPivotCount() && nNodes < OF2_TONGUE_MAX_NODES; j++ )
			{
				pTongue->m_hNodes.Set( nNodes, NULL );
				pTongue->m_vecNodes.Set( nNodes, link.GetPoint( j + 1 ) );
				nNodes++;
			}
		}
	}

	for ( int j = m_Tether.GetPivotCount() - 1; j >= 0 && nNodes < OF2_TONGUE_MAX_NODES; j-- )
	{
		pTongue->m_hNodes.Set( nNodes, NULL );
		pTongue->m_vecNodes.Set( nNodes, m_Tether.GetPoint( j + 1 ) );
		nNodes++;
	}

	pTongue->m_nNodes = nNodes;
}

//-----------------------------------------------------------------------------
// The stretches between the beads bend over edges like the last one does.
// The physics link between two beads is still straight; this is so the
// tongue is drawn over the edge rather than through it.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::WrapLinks( void )
{
	for ( int i = 0; i < m_nBeadsOut - 1; i++ )
	{
		COF2BarnacleBead *pFar = m_hBeads[i];
		COF2BarnacleBead *pNear = m_hBeads[i + 1];
		if ( pFar == NULL || pNear == NULL || pFar->VPhysicsGetObject() == NULL || pNear->VPhysicsGetObject() == NULL )
			continue;

		Vector vecFar, vecNear;
		pFar->VPhysicsGetObject()->GetPosition( &vecFar, NULL );
		pNear->VPhysicsGetObject()->GetPosition( &vecNear, NULL );

		m_LinkTethers[i].Update( vecFar, vecNear );
	}
}

//-----------------------------------------------------------------------------
// Takes the tongue back in at once: beads, links and what the client draws all
// go, and the player lets go of it
//-----------------------------------------------------------------------------
void CWeaponBarnacle::ResetTongue( void )
{
	CHL2_Player *pPlayer = dynamic_cast<CHL2_Player *>( GetOwner() );
	if ( pPlayer && pPlayer->GetTetherOwner() == this )
	{
		pPlayer->StopTether();
	}

	for ( int i = BARNACLE_MAX_BEADS - 1; i >= 0; i-- )
	{
		RemoveBead( i );
	}

	m_Tether.SetEndEntity( NULL );
	m_Tether.SetHeldEnd( TETHER_NONE );
	m_Tether.RemoveBeams();

	if ( m_hTongue != NULL )
	{
		UTIL_Remove( m_hTongue );
		m_hTongue = NULL;
	}

	m_nBeadsOut = 0;
	m_bTongueOut = false;
	m_bPayingOut = false;
	m_bAnchored = false;
	m_bRetracting = false;

	SetContextThink( NULL, 0, s_pTongueContext );
}

//-----------------------------------------------------------------------------
// One bead gone, with its link to the bead before it. Doesn't change m_nBeadsOut.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::RemoveBead( int iBead )
{
	if ( iBead > 0 && m_pBeadLinks[iBead - 1] )
	{
		physenv->DestroyConstraint( m_pBeadLinks[iBead - 1] );
		m_pBeadLinks[iBead - 1] = NULL;
	}

	if ( m_hBeadSprites[iBead] != NULL )
	{
		UTIL_Remove( m_hBeadSprites[iBead] );
		m_hBeadSprites[iBead] = NULL;
	}

	if ( m_hBeads[iBead] != NULL )
	{
		UTIL_Remove( m_hBeads[iBead] );
		m_hBeads[iBead] = NULL;
	}
}

//-----------------------------------------------------------------------------
// Reload: everything lets go, and the tongue is hauled back in fast
//-----------------------------------------------------------------------------
void CWeaponBarnacle::StartRetract( void )
{
	if ( m_bAnchored )
	{
		Unanchor();
	}

	for ( int i = 0; i < m_nBeadsOut; i++ )
	{
		if ( m_hBeads[i] != NULL )
		{
			m_hBeads[i]->SetSticky( false );
			m_hBeads[i]->Unstick();
		}
	}

	m_bRetracting = true;
	m_bPayingOut = false;
	m_flRetractTime = gpGlobals->curtime;
}

//-----------------------------------------------------------------------------
// The innermost bead that would anchor the player: stuck to the world, or to
// something heavier than the player. -1 if none.
//-----------------------------------------------------------------------------
int CWeaponBarnacle::FindAnchor( void )
{
	for ( int i = m_nBeadsOut - 1; i >= 0; i-- )
	{
		if ( m_hBeads[i] != NULL && m_hBeads[i]->IsAnchoring( of2_barnacle_anchor_mass.GetFloat() ) )
			return i;
	}

	return -1;
}

//-----------------------------------------------------------------------------
// The tongue has caught on something the player can't move. From now on the
// player hangs from that bead like from a climb rope: the beads between it and
// the barnacle go, and the tether runs from the hand to it the way the tongue
// lay, over whatever it went over. The player can walk out as far as the whole
// tongue reaches, swing, and reel in or pay out.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::Anchor( CHL2_Player *pPlayer, int iAnchor )
{
	Vector vecHand = COF2Tether::GetPlayerHandPos( pPlayer );

	// The way the tongue runs now, from the hand to the anchor: over the bends
	// of the last stretch, then back through the beads and their stretches' bends
	Vector vecRoute[OF2_TONGUE_MAX_NODES + 2];
	int nRoute = 0;
	vecRoute[nRoute++] = vecHand;

	for ( int j = 0; j < m_Tether.GetPivotCount() && nRoute < OF2_TONGUE_MAX_NODES; j++ )
	{
		vecRoute[nRoute++] = m_Tether.GetPoint( j + 1 );
	}

	for ( int i = m_nBeadsOut - 1; i > iAnchor && nRoute < OF2_TONGUE_MAX_NODES; i-- )
	{
		if ( m_hBeads[i] != NULL && m_hBeads[i]->VPhysicsGetObject() )
		{
			m_hBeads[i]->VPhysicsGetObject()->GetPosition( &vecRoute[nRoute++], NULL );
		}

		const COF2Tether &link = m_LinkTethers[i - 1];
		for ( int j = link.GetPivotCount() - 1; j >= 0 && nRoute < OF2_TONGUE_MAX_NODES; j-- )
		{
			vecRoute[nRoute++] = link.GetPoint( j + 1 );
		}
	}

	for ( int i = m_nBeadsOut - 1; i > iAnchor; i-- )
	{
		RemoveBead( i );
	}
	m_nBeadsOut = iAnchor + 1;

	// All of the tongue from the anchor in, beads not yet out included
	m_flLeashMax = 0.0f;
	for ( int i = iAnchor; i < m_nBeads; i++ )
	{
		m_flLeashMax += m_flStretch[i];
	}
	m_flLeash = m_flLeashMax;

	COF2BarnacleBead *pAnchor = m_hBeads[iAnchor];
	Vector vecAnchor;
	pAnchor->VPhysicsGetObject()->GetPosition( &vecAnchor, NULL );
	vecRoute[nRoute++] = vecAnchor;

	// Laid straight from the hand to the anchor, a tether that has the tongue
	// going over a wall would cut through it, and wrapping can't tell which way
	// round it should go. So it keeps the points of the route where the view
	// from the last one kept is cut off: those are where it goes over things.
	m_Tether.Init( vecHand, vecAnchor, m_flLeash );
	CTraceFilterWorldAndPropsOnly filter;
	Vector vecFrom = vecHand;
	for ( int k = 1; k < nRoute - 1; k++ )
	{
		trace_t tr;
		UTIL_TraceLine( vecFrom, vecRoute[k + 1], MASK_SOLID_BRUSHONLY, &filter, &tr );
		if ( tr.startsolid || tr.fraction == 1.0f )
			continue;

		m_Tether.AppendPivot( vecRoute[k], Vector( 0, 0, 1 ) );
		vecFrom = vecRoute[k];
	}

	// As much as it takes to reach the hand, so the player isn't jerked
	float flFixed = m_Tether.GetFixedLength( TETHER_START );
	float flLength = MIN( flFixed + m_Tether.GetSwingPoint().DistTo( vecHand ), m_flLeash );
	m_Tether.SetTotalLength( flLength );

	m_Tether.SetPlayerEnd( TETHER_START );
	m_Tether.SetHeldEnd( TETHER_START, true );
	m_Tether.SetEndEntity( pAnchor );

	// The mouse buttons reel, so forward/back only swings
	pPlayer->StartTether( this, 0.0f, of2_barnacle_pump.GetFloat(), 0.0f );
	pPlayer->UpdateTether( m_Tether.GetSwingPoint(), m_Tether.GetSwingLength(), MAX( m_flLeash - flFixed, 0.0f ) );

	m_iAnchor = iAnchor;
	m_bAnchored = true;
	m_bPayingOut = false;
}

void CWeaponBarnacle::Unanchor( void )
{
	CHL2_Player *pPlayer = dynamic_cast<CHL2_Player *>( GetOwner() );
	if ( pPlayer && pPlayer->GetTetherOwner() == this )
	{
		pPlayer->StopTether();
	}

	m_bAnchored = false;

	if ( pPlayer && m_nBeadsOut > 0 )
	{
		AttachTether( pPlayer );
	}
}

//-----------------------------------------------------------------------------
// Anchored: the same as holding a climb rope (CFuncClimbRope::UpdateHeld),
// except that reeling shortens what there is and paying out lengthens it
//-----------------------------------------------------------------------------
void CWeaponBarnacle::UpdateAnchored( CHL2_Player *pPlayer )
{
	COF2BarnacleBead *pAnchor = m_hBeads[m_iAnchor];

	Vector vecAnchor;
	pAnchor->VPhysicsGetObject()->GetPosition( &vecAnchor, NULL );
	Vector vecHand = COF2Tether::GetPlayerHandPos( pPlayer );

	float flFixed = m_Tether.GetFixedLength( TETHER_START );
	float flSwing = pPlayer->GetTetherSwingLength();

	if ( m_bReeling )
	{
		// From what the player has now, not from the most there could be
		m_flLeash = MIN( m_flLeash, flFixed + flSwing ) - of2_barnacle_reel_speed.GetFloat() * TICK_INTERVAL;
		m_flLeash = MAX( m_flLeash, flFixed + BARNACLE_MIN_SWING );
	}
	else if ( m_bExtruding )
	{
		float flMore = of2_barnacle_extrude_speed.GetFloat() * TICK_INTERVAL;
		m_flLeash = MIN( m_flLeash + flMore, m_flLeashMax );
		flSwing += flMore;
	}

	flSwing = MIN( flSwing, m_flLeash - flFixed );
	m_Tether.SetTotalLength( flFixed + MAX( flSwing, 0.0f ) );
	m_Tether.Update( vecHand, vecAnchor );

	// A bend that came or went moved the point the player swings from
	flFixed = m_Tether.GetFixedLength( TETHER_START );
	pPlayer->UpdateTether( m_Tether.GetSwingPoint(), m_Tether.GetSwingLength(), MAX( m_flLeash - flFixed, 0.0f ) );
}

//-----------------------------------------------------------------------------
// The nearest bead has come in: it goes back into the barnacle, and the next
// one becomes the nearest. False once the last one is in.
//-----------------------------------------------------------------------------
bool CWeaponBarnacle::AbsorbBead( CBasePlayer *pOwner )
{
	RemoveBead( m_nBeadsOut - 1 );
	m_nBeadsOut--;

	if ( m_nBeadsOut <= 0 )
	{
		ResetTongue();
		return false;
	}

	AttachTether( pOwner );
	return true;
}

//-----------------------------------------------------------------------------
// Not anchored: the tongue drags its beads, and whatever light thing they
// hold, after the player. Also paying out a throw, reeling in, paying out by
// hand, and retracting. False if the tongue is all back in.
//-----------------------------------------------------------------------------
bool CWeaponBarnacle::UpdateLoose( CBasePlayer *pOwner )
{
	COF2BarnacleBead *pBead = m_hBeads[m_nBeadsOut - 1];
	IPhysicsObject *pPhysics = pBead->VPhysicsGetObject();
	Vector vecMouth = GetMouthPos( pOwner );

	Vector vecBead;
	pPhysics->GetPosition( &vecBead, NULL );
	m_Tether.Update( vecMouth, vecBead );

	if ( m_bRetracting || m_bReeling )
	{
		// The tongue shortens, hauling the nearest bead in; each bead that
		// reaches the barnacle goes back into it
		float flSpeed = m_bRetracting ? of2_barnacle_retract_speed.GetFloat() : of2_barnacle_reel_speed.GetFloat();
		float flTotal = MIN( m_Tether.GetTotalLength(), m_Tether.GetPathLength() ) - flSpeed * TICK_INTERVAL;
		m_Tether.SetTotalLength( MAX( flTotal, 0.0f ) );

		if ( m_Tether.GetPathLength() < BARNACLE_ABSORB_DIST )
			return AbsorbBead( pOwner );

		if ( m_bRetracting )
		{
			if ( gpGlobals->curtime > m_flRetractTime + BARNACLE_RETRACT_MAX_TIME )
			{
				ResetTongue();
				return false;
			}

			if ( HoldBackBead( pPhysics, BARNACLE_RETRACT_PULL, flSpeed ) )
			{
				PullChain();
			}
			return true;
		}

		// Reeling hauls it in at least as fast as the tongue shortens
		if ( HoldBackBead( pPhysics, BARNACLE_RETRACT_PULL, flSpeed * BARNACLE_REEL_PULL_SCALE ) )
		{
			PullChain();
		}
		return true;
	}
	else if ( m_bPayingOut )
	{
		Vector vecVelocity;
		pPhysics->GetVelocity( &vecVelocity, NULL );

		// The stretch to the nearest bead grows as it is pulled away. Once it is
		// as long as it gets, the next bead comes out after it and the stretch
		// starts over from there.
		float flStretch = m_flStretch[m_nBeadsOut - 1];
		if ( m_Tether.GetPathLength() >= flStretch && m_nBeadsOut < m_nBeads )
		{
			// It has to start exactly one stretch behind the bead pulling it. The
			// stretch has usually overshot by part of a tick's travel; starting the
			// new bead at the barnacle would leave their link over-long, and the
			// link would snap the two together, yanking the far beads back.
			Vector vecBack = m_Tether.GetNearestPoint( TETHER_END ) - vecBead;
			float flBack = VectorNormalize( vecBack );
			Vector vecStart = vecBead + vecBack * MIN( flStretch, flBack );

			COF2BarnacleBead *pNext = AddBead( vecStart, vecVelocity * of2_barnacle_feed_speed.GetFloat(), false );
			if ( pNext )
			{
				pBead = pNext;
				pPhysics = pNext->VPhysicsGetObject();
				AttachTether( pOwner );
			}
		}
		else
		{
			m_Tether.SetTotalLength( clamp( m_Tether.GetPathLength(), m_Tether.GetTotalLength(), flStretch ) );
		}

		// It stops paying out once the head has landed: stuck to something, or
		// come to rest. Otherwise the beads still in flight would keep coming and
		// pile up behind it.
		COF2BarnacleBead *pHead = m_hBeads[0];
		Vector vecHeadVelocity = vec3_origin;
		if ( pHead && pHead->VPhysicsGetObject() )
		{
			pHead->VPhysicsGetObject()->GetVelocity( &vecHeadVelocity, NULL );
		}

		bool bHeadHeld = false;
		for ( int i = 0; i < m_nBeadsOut; i++ )
		{
			if ( m_hBeads[i] != NULL && m_hBeads[i]->IsStuck() )
			{
				bHeadHeld = true;
			}
		}

		float flTime = gpGlobals->curtime - m_flThrowTime;
		if ( bHeadHeld || flTime > BARNACLE_SETTLE_MAX_TIME || ( flTime > BARNACLE_SETTLE_MIN_TIME && vecHeadVelocity.Length() < BARNACLE_SETTLE_SPEED ) )
		{
			m_bPayingOut = false;
			m_Tether.SetTotalLength( MIN( m_Tether.GetTotalLength(), m_Tether.GetPathLength() ) );
		}
	}
	else if ( m_bExtruding )
	{
		// More tongue: the stretch to the nearest bead grows, and once it is as
		// long as it gets the next bead comes out of the barnacle
		float flStretch = m_flStretch[m_nBeadsOut - 1];
		float flTotal = m_Tether.GetTotalLength() + of2_barnacle_extrude_speed.GetFloat() * TICK_INTERVAL;
		if ( flTotal >= flStretch && m_nBeadsOut < m_nBeads )
		{
			if ( AddBead( vecMouth, vec3_origin, false ) )
			{
				AttachTether( pOwner );
				return true;
			}
		}

		m_Tether.SetTotalLength( MIN( flTotal, flStretch ) );
	}

	if ( HoldBackBead( pPhysics, of2_barnacle_pull.GetFloat(), of2_barnacle_pull_max_speed.GetFloat() ) )
	{
		PullChain();
	}
	return true;
}

void CWeaponBarnacle::TongueThink( void )
{
	CHL2_Player *pPlayer = dynamic_cast<CHL2_Player *>( GetOwner() );
	COF2BarnacleBead *pBead = ( m_nBeadsOut > 0 ) ? m_hBeads[m_nBeadsOut - 1].Get() : NULL;
	if ( !m_bTongueOut || pPlayer == NULL || !pPlayer->IsAlive() || pBead == NULL || pBead->VPhysicsGetObject() == NULL )
	{
		ResetTongue();
		return;
	}

	// A weld that tore during the physics step is taken down here
	for ( int i = 0; i < m_nBeadsOut; i++ )
	{
		if ( m_hBeads[i] != NULL )
		{
			m_hBeads[i]->CheckWeld();
		}
	}

	if ( !m_bAnchored && !m_bRetracting )
	{
		int iAnchor = FindAnchor();
		if ( iAnchor >= 0 )
		{
			Anchor( pPlayer, iAnchor );
		}
	}

	if ( m_bAnchored )
	{
		// The player let go (jumped off, was teleported): the tongue is back in
		// at once, ready to throw again in mid-air
		if ( !pPlayer->IsOnTether() || pPlayer->GetTetherOwner() != this )
		{
			ResetTongue();
			return;
		}

		// The anchor came loose (torn off, or what it held went away): the
		// tongue is hauled back in
		COF2BarnacleBead *pAnchor = m_hBeads[m_iAnchor];
		if ( pAnchor == NULL || pAnchor->VPhysicsGetObject() == NULL || !pAnchor->IsAnchoring( of2_barnacle_anchor_mass.GetFloat() ) )
		{
			StartRetract();
		}
		else
		{
			UpdateAnchored( pPlayer );
		}
	}

	if ( !m_bAnchored && !UpdateLoose( pPlayer ) )
		return;

	WrapLinks();
	UpdateTongue();

	if ( of2_barnacle_debug.GetBool() )
	{
		m_Tether.DebugDraw();
		for ( int i = 0; i < m_nBeadsOut; i++ )
		{
			if ( m_hBeads[i] != NULL )
			{
				NDebugOverlay::Cross3D( m_hBeads[i]->GetAbsOrigin(), 3.0f, 255, 255, 255, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
			}
		}
	}

	SetContextThink( &CWeaponBarnacle::TongueThink, gpGlobals->curtime + TICK_INTERVAL, s_pTongueContext );
}

//-----------------------------------------------------------------------------
// The tether isn't simulated, so it can't pull. Instead the nearest bead is
// kept within the length the tether has left past its last bend: any speed
// away from there is taken off, and it is nudged back in (flPull: speed per
// unit over, up to flMaxSpeed). Soft, so the chain is dragged along (and up
// and over things) rather than snapped back.
//-----------------------------------------------------------------------------
bool CWeaponBarnacle::HoldBackBead( IPhysicsObject *pBead, float flPull, float flMaxSpeed )
{
	Vector vecBead;
	pBead->GetPosition( &vecBead, NULL );

	Vector vecDir = vecBead - m_Tether.GetNearestPoint( TETHER_END );
	float flDist = VectorNormalize( vecDir );
	float flOver = flDist - m_Tether.GetFarEndAllowance();
	// A little give, so a bead at rest isn't nudged (and woken) every tick
	if ( flOver <= BARNACLE_PULL_SLACK )
		return false;

	Vector vecVelocity;
	AngularImpulse angVelocity;
	pBead->GetVelocity( &vecVelocity, &angVelocity );

	float flOut = DotProduct( vecVelocity, vecDir );
	if ( flOut > 0.0f )
	{
		vecVelocity -= vecDir * flOut;
	}

	vecVelocity -= vecDir * MIN( flOver * flPull, flMaxSpeed );

	pBead->Wake();
	pBead->SetVelocity( &vecVelocity, &angVelocity );
	return true;
}

//-----------------------------------------------------------------------------
// The links between the beads are physics constraints, which share a pull
// between everything on them. With heavy beads that grip the floor, pulling
// the nearest one hardly moves the rest. So the pull is passed on by hand:
// down the chain, while the links are taut, each bead goes towards the one
// before it at least as fast as that one goes. Stops at the first slack link.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::PullChain( void )
{
	for ( int i = m_nBeadsOut - 2; i >= 0; i-- )
	{
		COF2BarnacleBead *pNear = m_hBeads[i + 1];
		COF2BarnacleBead *pFar = m_hBeads[i];
		if ( pNear == NULL || pFar == NULL || pNear->VPhysicsGetObject() == NULL || pFar->VPhysicsGetObject() == NULL )
			return;

		IPhysicsObject *pNearPhysics = pNear->VPhysicsGetObject();
		IPhysicsObject *pFarPhysics = pFar->VPhysicsGetObject();
		if ( !pFarPhysics->IsMotionEnabled() )
			return;

		Vector vecNear, vecFar;
		pNearPhysics->GetPosition( &vecNear, NULL );
		pFarPhysics->GetPosition( &vecFar, NULL );

		Vector vecDir = vecNear - vecFar;
		float flDist = VectorNormalize( vecDir );
		if ( flDist < m_flStretch[i] - BARNACLE_PULL_SLACK )
			return;

		Vector vecNearVelocity, vecFarVelocity;
		AngularImpulse angFar;
		pNearPhysics->GetVelocity( &vecNearVelocity, NULL );
		pFarPhysics->GetVelocity( &vecFarVelocity, &angFar );

		float flNear = DotProduct( vecNearVelocity, vecDir );
		float flFar = DotProduct( vecFarVelocity, vecDir );
		if ( flFar < flNear )
		{
			vecFarVelocity += vecDir * ( flNear - flFar );
			pFarPhysics->Wake();
			pFarPhysics->SetVelocity( &vecFarVelocity, &angFar );
		}
	}
}
