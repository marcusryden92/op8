//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: Displacer. Secondary fire marks a destination on the aimed
//			surface; primary fire shoots a portal that sends whatever it hits
//			to that destination; tapping reload sends the player there and
//			holding reload clears it. No ammo, like the gravity gun.
//
//			Also in this file: the portal projectile and the two level-design
//			blockers, trigger_displacer_block and trigger_displacer_nosignal.
//			The third blocker is the tools/toolsnodisplace texture.
//
//=============================================================================//

#include "cbase.h"
#include "basehlcombatweapon.h"
#include "player.h"
#include "in_buttons.h"
#include "ai_basenpc.h"
#include "Sprite.h"
#include "triggers.h"
#include "physics_prop_ragdoll.h"
#include "IEffects.h"
#include "beam_flags.h"
#include "te_effect_dispatch.h"
#include "engine/IStaticPropMgr.h"
#include "engine/IEngineSound.h"
#include "ndebugoverlay.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The soft glow both teleporters in Kleiner's lab (d1_trainstation_05) are made of
#define DISPLACER_GLOW_SPRITE		"sprites/light_glow03.vmt"
#define DISPLACER_RING_SPRITE		"sprites/lgtning.vmt"

// The texture level designers put on surfaces that must not take a destination
#define DISPLACER_BLOCK_TEXTURE		"tools/toolsnodisplace"

// What the destination trace stops on: solid surfaces and NPCs. Its filter
// (CTraceFilterDisplacerDest) then lets it through loose objects and friendly NPCs.
#define MASK_DISPLACER_DEST			(CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_WINDOW|CONTENTS_GRATE|CONTENTS_MONSTER)

// The signal is lost this far past the range, so a destination set at the very
// end of the range isn't dropped again by the next step backwards
#define DISPLACER_RANGE_SLOP		16.0f

#define DISPLACER_THINK_INTERVAL	0.05f
#define DISPLACER_PORTAL_SIZE		3.0f

// HUD green (ClientScheme.res "Normal"), for the debug overlays
#define DISPLACER_COLOR				10, 204, 88

// Teleport light: the lab's white-blue pulled towards our green, and its near-white middle.
// c_weapon_displacer.cpp has the same glow color for the arcs and the light.
#define DISPLACER_GLOW_COLOR		150, 245, 215
#define DISPLACER_CORE_COLOR		230, 255, 248

ConVar of2_displacer_range( "of2_displacer_range", "1500", FCVAR_NONE, "How far away a Displacer destination or target can be marked, and how far the player can get from it before the signal is lost." );
ConVar of2_displacer_cooldown( "of2_displacer_cooldown", "0.5", FCVAR_NONE, "Seconds between Displacer shots or self-teleports." );
ConVar of2_displacer_clear_hold( "of2_displacer_clear_hold", "0.5", FCVAR_NONE, "Holding reload this long clears the Displacer destination; letting go sooner teleports the player." );
ConVar of2_displacer_portal_speed( "of2_displacer_portal_speed", "2600", FCVAR_NONE, "Speed of the Displacer's portal projectile." );
ConVar of2_displacer_portal_life( "of2_displacer_portal_life", "10", FCVAR_NONE, "Seconds before a Displacer portal that hit nothing removes itself." );
ConVar of2_displacer_debug( "of2_displacer_debug", "0", FCVAR_NONE, "Draw a box at the Displacer destination (the HUD marks it otherwise), and the box of the last thing placed there." );

static int s_nDisplacerRingTexture = 0;
static const char *s_pDestinationContext = "DisplacerDestinationThink";
static const char *s_pPortalExpireContext = "DisplacerPortalExpire";
static const char *s_pPortalImpactContext = "DisplacerPortalImpact";

//-----------------------------------------------------------------------------
// trigger_displacer_block: no destination inside it, and nothing may be
// teleported so that its box overlaps it.
//-----------------------------------------------------------------------------
class CTriggerDisplacerBlock : public CBaseTrigger
{
	DECLARE_CLASS( CTriggerDisplacerBlock, CBaseTrigger );

public:
	CTriggerDisplacerBlock();
	~CTriggerDisplacerBlock();

	void	Spawn( void );

	// True if the box (relative to vecOrigin) touches an enabled blocker
	static bool	IsBoxBlocked( const Vector &vecOrigin, const Vector &mins, const Vector &maxs );

private:
	static CUtlVector<CTriggerDisplacerBlock *>	s_Blockers;
};

LINK_ENTITY_TO_CLASS( trigger_displacer_block, CTriggerDisplacerBlock );

CUtlVector<CTriggerDisplacerBlock *> CTriggerDisplacerBlock::s_Blockers;

CTriggerDisplacerBlock::CTriggerDisplacerBlock()
{
	s_Blockers.AddToTail( this );
}

CTriggerDisplacerBlock::~CTriggerDisplacerBlock()
{
	s_Blockers.FindAndRemove( this );
}

void CTriggerDisplacerBlock::Spawn( void )
{
	BaseClass::Spawn();
	InitTrigger();
}

bool CTriggerDisplacerBlock::IsBoxBlocked( const Vector &vecOrigin, const Vector &mins, const Vector &maxs )
{
	for ( int i = 0; i < s_Blockers.Count(); i++ )
	{
		CTriggerDisplacerBlock *pBlocker = s_Blockers[i];
		if ( pBlocker->m_bDisabled )
			continue;

		// Same test as CBaseTrigger::PointIsWithin, with a box
		Ray_t ray;
		trace_t tr;
		ray.Init( vecOrigin, vecOrigin, mins, maxs );
		enginetrace->ClipRayToCollideable( ray, MASK_ALL, pBlocker->CollisionProp(), &tr );
		if ( tr.startsolid )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Which things a portal can send at all (room at the destination is checked
// separately). The player isn't in here; self-teleport has its own path.
//-----------------------------------------------------------------------------
static bool Displacer_IsDisplaceable( CBaseEntity *pEntity )
{
	if ( pEntity == NULL || pEntity->IsWorld() || pEntity->IsPlayer() )
		return false;

	// Riding on or attached to something else
	if ( pEntity->GetMoveParent() != NULL )
		return false;

	if ( pEntity->IsNPC() )
	{
		// Rooted in place: barnacles, ceiling turrets, cameras
		return pEntity->IsAlive() && pEntity->GetMoveType() != MOVETYPE_NONE;
	}

	// Everything else has to be a loose physics object. This leaves out doors,
	// lifts and track trains, which move by other means.
	if ( pEntity->GetMoveType() != MOVETYPE_VPHYSICS )
		return false;

	IPhysicsObject *pPhys = pEntity->VPhysicsGetObject();
	if ( pPhys == NULL || !pPhys->IsMoveable() )
		return false;

	// Hinged or tied to something. A ragdoll's own joints don't count, and a
	// vehicle is allowed whatever holds its wheels on.
	if ( pEntity->GetServerVehicle() == NULL && pPhys->IsAttachedToConstraint( Ragdoll_IsPropRagdoll( pEntity ) ) )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// The box an entity needs room for, relative to its origin
//-----------------------------------------------------------------------------
static void Displacer_GetTeleportBox( CBaseEntity *pEntity, Vector &mins, Vector &maxs )
{
	if ( pEntity->IsPlayer() )
	{
		CBasePlayer *pPlayer = ToBasePlayer( pEntity );
		mins = pPlayer->GetPlayerMins();
		maxs = pPlayer->GetPlayerMaxs();
		return;
	}

	CAI_BaseNPC *pNPC = pEntity->MyNPCPointer();
	if ( pNPC )
	{
		// NPC movement hull
		mins = pNPC->GetHullMins();
		maxs = pNPC->GetHullMaxs();
		return;
	}

	// Props may be rotated, so use the enclosing world box
	Vector absMins, absMaxs;
	pEntity->CollisionProp()->WorldSpaceAABB( &absMins, &absMaxs );
	mins = absMins - pEntity->GetAbsOrigin();
	maxs = absMaxs - pEntity->GetAbsOrigin();
}

static bool Displacer_CanFitAt( CBaseEntity *pEntity, const Vector &vecDest, const Vector &mins, const Vector &maxs )
{
	unsigned int mask = MASK_SOLID;
	int collisionGroup = COLLISION_GROUP_NONE;
	if ( pEntity->IsPlayer() )
	{
		mask = MASK_PLAYERSOLID;
		collisionGroup = COLLISION_GROUP_PLAYER_MOVEMENT;
	}
	else if ( pEntity->IsNPC() )
	{
		mask = MASK_NPCSOLID;
	}

	trace_t tr;
	UTIL_TraceHull( vecDest, vecDest, mins, maxs, mask, pEntity, collisionGroup, &tr );
	if ( tr.startsolid || tr.allsolid )
		return false;

	return !CTriggerDisplacerBlock::IsBoxBlocked( vecDest, mins, maxs );
}

//-----------------------------------------------------------------------------
// Where an entity's origin goes so that its box rests against the destination
//-----------------------------------------------------------------------------
static Vector Displacer_PlaceBox( const Vector &vecPoint, const Vector &vecNormal, const Vector &mins, const Vector &maxs )
{
	Vector vecCenter = ( mins + maxs ) * 0.5f;
	Vector vecHalf = ( maxs - mins ) * 0.5f;

	if ( vecNormal.z > 0.7f )
	{
		// Floor: stand the box on the point
		return Vector( vecPoint.x - vecCenter.x, vecPoint.y - vecCenter.y, vecPoint.z - mins.z + 1.0f );
	}

	// Wall or ceiling: back the box off until its near side clears the surface
	float flExtent = fabs( vecNormal.x ) * vecHalf.x + fabs( vecNormal.y ) * vecHalf.y + fabs( vecNormal.z ) * vecHalf.z;
	return vecPoint + vecNormal * ( flExtent + 1.0f ) - vecCenter;
}

//-----------------------------------------------------------------------------
// Where something left or arrived: the teleport sprite, a ring, and on the
// client arcs, sparks and a flash of light (DisplacerTeleport, c_weapon_displacer.cpp).
// pArrived is the thing itself once it stands at the new place.
//-----------------------------------------------------------------------------
static void Displacer_TeleportEffect( const Vector &vecPos, float flRadius, CBaseEntity *pArrived, const char *pszSound )
{
	// The sprite is 128 units across at scale 1: a wide soft glow that lingers,
	// larger than what it swallows, and a brighter middle that goes out first
	float flScale = clamp( flRadius / 28.0f, 0.75f, 4.0f );

	CSprite *pGlow = CSprite::SpriteCreate( DISPLACER_GLOW_SPRITE, vecPos, false );
	if ( pGlow )
	{
		pGlow->SetTransparency( kRenderTransAdd, DISPLACER_GLOW_COLOR, 255, kRenderFxNone );
		pGlow->SetScale( flScale );
		pGlow->SetAsTemporary();
		pGlow->FadeAndDie( 0.6f );
	}

	CSprite *pCore = CSprite::SpriteCreate( DISPLACER_GLOW_SPRITE, vecPos, false );
	if ( pCore )
	{
		pCore->SetTransparency( kRenderTransAdd, DISPLACER_CORE_COLOR, 255, kRenderFxNone );
		pCore->SetScale( flScale * 0.5f );
		pCore->SetAsTemporary();
		pCore->FadeAndDie( 0.25f );
	}

	CBroadcastRecipientFilter filter;
	te->BeamRingPoint( filter, 0, vecPos,
		8.0f,		// start radius
		flRadius * 2.0f + 48.0f,	// end radius
		s_nDisplacerRingTexture,
		0,			// halo index
		0,			// start frame
		2,			// framerate
		0.25f,		// life
		10,			// width
		0,			// spread
		0,			// amplitude
		DISPLACER_GLOW_COLOR, 200,
		0,			// speed
		FBEAM_FADEOUT
		);

	CEffectData data;
	data.m_vOrigin = vecPos;
	data.m_flRadius = flRadius;
	if ( pArrived )
	{
		data.m_nEntIndex = pArrived->entindex();
	}
	DispatchEffect( "DisplacerTeleport", data );

	// Arcs crawling over whoever just arrived, as on things a Combine ball takes apart
	if ( pArrived && pArrived->IsNPC() )
	{
		CEffectData tesla;
		tesla.m_nEntIndex = pArrived->entindex();
		tesla.m_flMagnitude = 8;
		tesla.m_flScale = 2.0f;
		tesla.m_bCustomColors = true;
		tesla.m_CustomColors.m_vecColor1 = Vector( DISPLACER_GLOW_COLOR ) / 255.0f;
		DispatchEffect( "TeslaHitboxes", tesla );
	}

	CPASAttenuationFilter soundFilter( vecPos, pszSound );
	CBaseEntity::EmitSound( soundFilter, SOUND_FROM_WORLD, pszSound, &vecPos );
}

//-----------------------------------------------------------------------------
// A portal or a destination that came to nothing
//-----------------------------------------------------------------------------
static void Displacer_Fizzle( const Vector &vecPos )
{
	g_pEffects->Sparks( vecPos, 1, 2 );

	CPASAttenuationFilter filter( vecPos, "Weapon_Displacer.Fizzle" );
	CBaseEntity::EmitSound( filter, SOUND_FROM_WORLD, "Weapon_Displacer.Fizzle", &vecPos );
}

//-----------------------------------------------------------------------------
// The destination trace stops on the world, static props, brush entities and
// enemies (which become a moving destination), and goes through friends and
// loose objects.
//-----------------------------------------------------------------------------
class CTraceFilterDisplacerDest : public CTraceFilter
{
public:
	CTraceFilterDisplacerDest( CBasePlayer *pPlayer ) : m_pPlayer( pPlayer ) {}

	static bool IsTarget( CBaseEntity *pEntity, CBasePlayer *pPlayer )
	{
		CAI_BaseNPC *pNPC = pEntity ? pEntity->MyNPCPointer() : NULL;
		if ( pNPC == NULL || !pNPC->IsAlive() )
			return false;

		Disposition_t disposition = pNPC->IRelationType( pPlayer );
		return disposition == D_HT || disposition == D_FR;
	}

	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		if ( staticpropmgr->IsStaticProp( pHandleEntity ) )
			return true;

		CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		if ( pEntity == NULL )
			return false;

		if ( pEntity->IsWorld() || IsTarget( pEntity, m_pPlayer ) )
			return true;

		return pEntity->IsBSPModel() && pEntity->IsSolid() && pEntity->GetMoveType() != MOVETYPE_VPHYSICS;
	}

private:
	CBasePlayer *m_pPlayer;
};

//-----------------------------------------------------------------------------
// The weapon
//-----------------------------------------------------------------------------
class CWeaponDisplacer : public CBaseHLCombatWeapon
{
	DECLARE_CLASS( CWeaponDisplacer, CBaseHLCombatWeapon );

public:
	CWeaponDisplacer();

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	void	Precache( void );
	void	ItemPostFrame( void );
	void	PrimaryAttack( void );
	void	SecondaryAttack( void );
	bool	Deploy( void );
	bool	Holster( CBaseCombatWeapon *pSwitchingTo = NULL );
	void	Drop( const Vector &vecVelocity );
	void	UpdateOnRemove( void );
	void	OnRestore( void );

	bool	CanBePickedUpByNPCs( void ) { return false; }

	bool	HasDestination( void ) const { return m_bHasDestination; }

	// Sends pEntity to the destination. False if there is none, it has no room there, or it can't be paid for.
	bool	DisplaceEntity( CBaseEntity *pEntity );

	// The destination is dropped with a beep and a HUD readout (out of range, or a trigger_displacer_nosignal)
	void	SignalLost( void );

private:
	void	UpdateReloadKey( CBasePlayer *pOwner );
	void	TeleportSelf( void );
	void	DryFire( void );

	void	SetDestination( const Vector &vecPoint, const Vector &vecNormal, CBaseEntity *pTarget = NULL );
	void	ClearDestination( void );
	void	UpdateTarget( void );
	void	DestinationThink( void );
	bool	IsDestinationFromThisMap( void );
	bool	FindRoomAtDestination( CBaseEntity *pEntity, Vector *pResult );

	// Hooks for a cost per displacement (suit power or the like). Free for now.
	bool	CanAffordDisplacement( void ) { return true; }
	void	SpendDisplacementCost( void ) {}

private:
	CNetworkVar( bool, m_bHasDestination );
	CNetworkVector( m_vecDestPoint );			// where the secondary fire trace hit; the HUD marks it (hud_displacer.cpp)
	CNetworkVar( int, m_iSignal );				// 100 at the destination, 0 at the edge of the range
	CNetworkVar( float, m_flSignalLostTime );	// when the signal was last lost, for the HUD readout

	// A marked enemy: the destination follows it for as long as it lives. m_vecDestPoint is
	// then its middle, and things are put down on top of it to fall on it.
	CNetworkVar( bool, m_bDestIsTarget );
	CNetworkHandle( CBaseEntity, m_hDestTarget );

	Vector			m_vecDestPlace;				// the point things are placed against; m_vecDestPoint unless following a target
	Vector			m_vecDestNormal;
	string_t		m_iszDestMap;				// destinations don't survive a level change
	float			m_flReloadPressTime;		// when reload went down, -1 once that press is dealt with
};

//-----------------------------------------------------------------------------
// trigger_displacer_nosignal: walking into it loses the destination, and none
// can be set while standing in it.
//-----------------------------------------------------------------------------
class CTriggerDisplacerNoSignal : public CBaseTrigger
{
	DECLARE_CLASS( CTriggerDisplacerNoSignal, CBaseTrigger );

public:
	void	Spawn( void );
	void	StartTouch( CBaseEntity *pOther );

	static bool	IsPlayerInside( CBasePlayer *pPlayer );
};

LINK_ENTITY_TO_CLASS( trigger_displacer_nosignal, CTriggerDisplacerNoSignal );

void CTriggerDisplacerNoSignal::Spawn( void )
{
	// It is all about the player, whatever the flags say
	AddSpawnFlags( SF_TRIGGER_ALLOW_CLIENTS );

	BaseClass::Spawn();
	InitTrigger();
}

void CTriggerDisplacerNoSignal::StartTouch( CBaseEntity *pOther )
{
	BaseClass::StartTouch( pOther );

	CBasePlayer *pPlayer = ToBasePlayer( pOther );
	if ( pPlayer == NULL || m_bDisabled )
		return;

	CWeaponDisplacer *pDisplacer = dynamic_cast<CWeaponDisplacer *>( pPlayer->Weapon_OwnsThisType( "weapon_displacer" ) );
	if ( pDisplacer )
	{
		pDisplacer->SignalLost();
	}
}

bool CTriggerDisplacerNoSignal::IsPlayerInside( CBasePlayer *pPlayer )
{
	CBaseEntity *pEntity = NULL;
	while ( ( pEntity = gEntList.FindEntityByClassname( pEntity, "trigger_displacer_nosignal" ) ) != NULL )
	{
		CTriggerDisplacerNoSignal *pTrigger = static_cast<CTriggerDisplacerNoSignal *>( pEntity );
		if ( !pTrigger->m_bDisabled && pTrigger->IsTouching( pPlayer ) )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// The portal: a glow that flies straight until it touches something
//-----------------------------------------------------------------------------
class CDisplacerPortal : public CSprite
{
	DECLARE_CLASS( CDisplacerPortal, CSprite );

public:
	DECLARE_DATADESC();

	static CDisplacerPortal *PortalCreate( const Vector &vecOrigin, const Vector &vecVelocity, CBaseEntity *pOwner, CWeaponDisplacer *pWeapon );

	void	PortalTouch( CBaseEntity *pOther );
	void	ImpactThink( void );

	unsigned int PhysicsSolidMaskForEntity( void ) const;

private:
	CHandle<CWeaponDisplacer>	m_hWeapon;
	EHANDLE						m_hTarget;
};

LINK_ENTITY_TO_CLASS( displacer_portal, CDisplacerPortal );

BEGIN_DATADESC( CDisplacerPortal )

	DEFINE_FIELD( m_hWeapon, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hTarget, FIELD_EHANDLE ),

	DEFINE_ENTITYFUNC( PortalTouch ),
	DEFINE_THINKFUNC( ImpactThink ),

END_DATADESC()

CDisplacerPortal *CDisplacerPortal::PortalCreate( const Vector &vecOrigin, const Vector &vecVelocity, CBaseEntity *pOwner, CWeaponDisplacer *pWeapon )
{
	CDisplacerPortal *pPortal = CREATE_ENTITY( CDisplacerPortal, "displacer_portal" );
	pPortal->SpriteInit( DISPLACER_GLOW_SPRITE, vecOrigin );

	pPortal->SetTransparency( kRenderTransAdd, DISPLACER_GLOW_COLOR, 255, kRenderFxNone );
	pPortal->SetScale( 0.45f );

	pPortal->SetSolid( SOLID_BBOX );
	pPortal->AddSolidFlags( FSOLID_NOT_STANDABLE );
	UTIL_SetSize( pPortal, -Vector( DISPLACER_PORTAL_SIZE, DISPLACER_PORTAL_SIZE, DISPLACER_PORTAL_SIZE ), Vector( DISPLACER_PORTAL_SIZE, DISPLACER_PORTAL_SIZE, DISPLACER_PORTAL_SIZE ) );
	pPortal->SetMoveType( MOVETYPE_FLY );
	// Not COLLISION_GROUP_PROJECTILE: that group flies through grenades, dropped weapons,
	// items and debris, all of which a portal should be able to send
	pPortal->SetCollisionGroup( COLLISION_GROUP_NONE );
	pPortal->SetOwnerEntity( pOwner );
	pPortal->SetAbsVelocity( vecVelocity );
	pPortal->m_hWeapon = pWeapon;

	pPortal->SetTouch( &CDisplacerPortal::PortalTouch );
	pPortal->SetContextThink( &CBaseEntity::SUB_Remove, gpGlobals->curtime + of2_displacer_portal_life.GetFloat(), s_pPortalExpireContext );

	return pPortal;
}

//-----------------------------------------------------------------------------
// Fences and grates don't stop a portal, like bullets
//-----------------------------------------------------------------------------
unsigned int CDisplacerPortal::PhysicsSolidMaskForEntity( void ) const
{
	return BaseClass::PhysicsSolidMaskForEntity() & ~CONTENTS_GRATE;
}

void CDisplacerPortal::PortalTouch( CBaseEntity *pOther )
{
	if ( pOther == NULL || pOther == GetOwnerEntity() )
		return;

	bool bDisplaceable = Displacer_IsDisplaceable( pOther );

	// Fly through triggers, but not through the loose items that are also triggers
	if ( pOther->IsSolidFlagSet( FSOLID_TRIGGER | FSOLID_VOLUME_CONTENTS ) && !bDisplaceable )
		return;

	SetTouch( NULL );
	SetAbsVelocity( vec3_origin );
	SetMoveType( MOVETYPE_NONE );
	TurnOff();

	const trace_t &tr = GetTouchTrace();
	if ( pOther->IsWorld() && ( tr.surface.flags & SURF_SKY ) )
	{
		// Gone into the sky
		SetContextThink( &CBaseEntity::SUB_Remove, gpGlobals->curtime, s_pPortalExpireContext );
		return;
	}

	// The teleport waits for our next think. This touch can come from inside the
	// other entity's own move, or from a physics callback, where it can't be moved.
	m_hTarget = bDisplaceable ? pOther : NULL;
	SetContextThink( &CDisplacerPortal::ImpactThink, gpGlobals->curtime, s_pPortalImpactContext );
}

void CDisplacerPortal::ImpactThink( void )
{
	CBaseEntity *pTarget = m_hTarget;
	CWeaponDisplacer *pWeapon = m_hWeapon;

	if ( pTarget == NULL || pWeapon == NULL || !pWeapon->DisplaceEntity( pTarget ) )
	{
		Displacer_Fizzle( GetAbsOrigin() );
	}

	UTIL_Remove( this );
}

//-----------------------------------------------------------------------------
// CWeaponDisplacer
//-----------------------------------------------------------------------------
LINK_ENTITY_TO_CLASS( weapon_displacer, CWeaponDisplacer );

PRECACHE_WEAPON_REGISTER( weapon_displacer );

IMPLEMENT_SERVERCLASS_ST( CWeaponDisplacer, DT_WeaponDisplacer )
	SendPropBool( SENDINFO( m_bHasDestination ) ),
	SendPropVector( SENDINFO( m_vecDestPoint ), -1, SPROP_COORD ),
	SendPropInt( SENDINFO( m_iSignal ), 7, SPROP_UNSIGNED ),
	SendPropTime( SENDINFO( m_flSignalLostTime ) ),
	SendPropBool( SENDINFO( m_bDestIsTarget ) ),
	SendPropEHandle( SENDINFO( m_hDestTarget ) ),
END_SEND_TABLE()

BEGIN_DATADESC( CWeaponDisplacer )

	DEFINE_FIELD( m_bHasDestination,	FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vecDestPoint,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecDestPlace,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecDestNormal,		FIELD_VECTOR ),
	DEFINE_FIELD( m_bDestIsTarget,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_hDestTarget,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_iSignal,			FIELD_INTEGER ),
	DEFINE_FIELD( m_iszDestMap,			FIELD_STRING ),

	// m_flSignalLostTime and m_flReloadPressTime only matter for a moment; not saved

	DEFINE_THINKFUNC( DestinationThink ),

END_DATADESC()

CWeaponDisplacer::CWeaponDisplacer()
{
	m_bHasDestination = false;
	m_vecDestPoint.Init();
	m_vecDestPlace.Init();
	m_vecDestNormal.Init( 0, 0, 1 );
	m_bDestIsTarget = false;
	m_hDestTarget = NULL;
	m_iSignal = 0;
	m_flSignalLostTime = 0.0f;
	m_iszDestMap = NULL_STRING;
	m_flReloadPressTime = -1.0f;
}

void CWeaponDisplacer::Precache( void )
{
	PrecacheModel( DISPLACER_GLOW_SPRITE );
	s_nDisplacerRingTexture = PrecacheModel( DISPLACER_RING_SPRITE );

	PrecacheScriptSound( "Weapon_Displacer.Fire" );
	PrecacheScriptSound( "Weapon_Displacer.Empty" );
	PrecacheScriptSound( "Weapon_Displacer.Fizzle" );
	PrecacheScriptSound( "Weapon_Displacer.SetDestination" );
	PrecacheScriptSound( "Weapon_Displacer.ClearDestination" );
	PrecacheScriptSound( "Weapon_Displacer.SignalLost" );
	PrecacheScriptSound( "Weapon_Displacer.TeleportOut" );
	PrecacheScriptSound( "Weapon_Displacer.TeleportIn" );

	BaseClass::Precache();
}

void CWeaponDisplacer::OnRestore( void )
{
	BaseClass::OnRestore();

	if ( !m_bHasDestination )
		return;

	if ( !IsDestinationFromThisMap() )
	{
		// Carried into another level: the destination stays behind
		ClearDestination();
		return;
	}

	// Saves from before the placement point was kept apart from the marked point
	if ( m_vecDestPlace == vec3_origin )
	{
		m_vecDestPlace = m_vecDestPoint;
	}

	SetContextThink( &CWeaponDisplacer::DestinationThink, gpGlobals->curtime, s_pDestinationContext );
}

void CWeaponDisplacer::UpdateOnRemove( void )
{
	ClearDestination();
	BaseClass::UpdateOnRemove();
}

void CWeaponDisplacer::Drop( const Vector &vecVelocity )
{
	ClearDestination();
	BaseClass::Drop( vecVelocity );
}

bool CWeaponDisplacer::Deploy( void )
{
	m_flReloadPressTime = -1.0f;
	return BaseClass::Deploy();
}

bool CWeaponDisplacer::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	m_flReloadPressTime = -1.0f;
	return BaseClass::Holster( pSwitchingTo );
}

//-----------------------------------------------------------------------------
// The base weapon would send a clipless weapon with no ammo type to its
// "fire on empty" path and never look at reload, so the buttons are read here.
//-----------------------------------------------------------------------------
void CWeaponDisplacer::ItemPostFrame( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	UpdateReloadKey( pOwner );

	if ( ( pOwner->m_afButtonPressed & IN_ATTACK2 ) && m_flNextSecondaryAttack <= gpGlobals->curtime )
	{
		SecondaryAttack();
	}
	else if ( ( pOwner->m_nButtons & IN_ATTACK ) && m_flNextPrimaryAttack <= gpGlobals->curtime )
	{
		PrimaryAttack();
	}
	else if ( !( pOwner->m_nButtons & ( IN_ATTACK | IN_ATTACK2 | IN_RELOAD ) ) )
	{
		WeaponIdle();
	}
}

//-----------------------------------------------------------------------------
// Reload: a tap teleports the player, a hold clears the destination
//-----------------------------------------------------------------------------
void CWeaponDisplacer::UpdateReloadKey( CBasePlayer *pOwner )
{
	if ( pOwner->m_afButtonPressed & IN_RELOAD )
	{
		m_flReloadPressTime = gpGlobals->curtime;
	}

	if ( m_flReloadPressTime < 0.0f )
		return;

	if ( !( pOwner->m_nButtons & IN_RELOAD ) )
	{
		// Let go before the hold time
		m_flReloadPressTime = -1.0f;
		TeleportSelf();
	}
	else if ( gpGlobals->curtime - m_flReloadPressTime >= of2_displacer_clear_hold.GetFloat() )
	{
		m_flReloadPressTime = -1.0f;

		if ( m_bHasDestination )
		{
			ClearDestination();
			pOwner->EmitSound( "Weapon_Displacer.ClearDestination" );
		}
	}
}

void CWeaponDisplacer::DryFire( void )
{
	EmitSound( "Weapon_Displacer.Empty" );
	SendWeaponAnim( ACT_VM_DRYFIRE );
}

//-----------------------------------------------------------------------------
// Shoot a portal
//-----------------------------------------------------------------------------
void CWeaponDisplacer::PrimaryAttack( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	if ( !m_bHasDestination || !CanAffordDisplacement() )
	{
		// Nowhere to send anything
		DryFire();
		m_flNextPrimaryAttack = gpGlobals->curtime + 0.5f;
		return;
	}

	m_flNextPrimaryAttack = gpGlobals->curtime + MAX( of2_displacer_cooldown.GetFloat(), 0.1f );

	Vector vecForward, vecRight, vecUp;
	pOwner->EyeVectors( &vecForward, &vecRight, &vecUp );
	Vector vecEye = pOwner->Weapon_ShootPosition();

	// The portal leaves the gun, not the eyes, and heads for what the crosshair is on
	trace_t tr;
	UTIL_TraceLine( vecEye, vecEye + vecForward * MAX_TRACE_LENGTH, MASK_SHOT, pOwner, COLLISION_GROUP_NONE, &tr );

	Vector vecSrc = vecEye;
	Vector vecDir = vecForward;
	Vector vecMuzzle = vecEye + vecForward * 20.0f + vecRight * 6.0f - vecUp * 6.0f;
	Vector vecPortalSize( DISPLACER_PORTAL_SIZE, DISPLACER_PORTAL_SIZE, DISPLACER_PORTAL_SIZE );

	// Unless the target is right in front of us or the gun is poking into something
	if ( ( tr.endpos - vecEye ).Length() > 64.0f )
	{
		trace_t trMuzzle;
		UTIL_TraceHull( vecEye, vecMuzzle, -vecPortalSize, vecPortalSize, MASK_SOLID, pOwner, COLLISION_GROUP_NONE, &trMuzzle );
		if ( trMuzzle.fraction == 1.0f && !trMuzzle.startsolid )
		{
			vecSrc = vecMuzzle;
			vecDir = tr.endpos - vecMuzzle;
			VectorNormalize( vecDir );
		}
	}

	CDisplacerPortal::PortalCreate( vecSrc, vecDir * of2_displacer_portal_speed.GetFloat(), pOwner, this );

	EmitSound( "Weapon_Displacer.Fire" );
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	pOwner->SetAnimation( PLAYER_ATTACK1 );
	pOwner->ViewPunch( QAngle( -1, 0, 0 ) );
}

//-----------------------------------------------------------------------------
// Mark the destination
//-----------------------------------------------------------------------------
void CWeaponDisplacer::SecondaryAttack( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	m_flNextSecondaryAttack = gpGlobals->curtime + 0.3f;

	Vector vecForward;
	pOwner->EyeVectors( &vecForward );
	Vector vecEye = pOwner->EyePosition();

	trace_t tr;
	CTraceFilterDisplacerDest filter( pOwner );
	UTIL_TraceLine( vecEye, vecEye + vecForward * of2_displacer_range.GetFloat(), MASK_DISPLACER_DEST, &filter, &tr );

	// Nothing in range, or no signal where we stand
	if ( tr.fraction == 1.0f || tr.startsolid || ( tr.surface.flags & SURF_SKY ) || CTriggerDisplacerNoSignal::IsPlayerInside( pOwner ) )
	{
		DryFire();
		return;
	}

	// An enemy: the destination is the enemy itself, wherever it goes
	if ( CTraceFilterDisplacerDest::IsTarget( tr.m_pEnt, pOwner ) )
	{
		if ( CTriggerDisplacerBlock::IsBoxBlocked( tr.m_pEnt->WorldSpaceCenter(), vec3_origin, vec3_origin ) )
		{
			DryFire();
			Displacer_Fizzle( tr.endpos );
			return;
		}

		SetDestination( tr.m_pEnt->WorldSpaceCenter(), Vector( 0, 0, 1 ), tr.m_pEnt );

		pOwner->EmitSound( "Weapon_Displacer.SetDestination" );
		SendWeaponAnim( ACT_VM_SECONDARYATTACK );
		return;
	}

	// A surface or a volume the level designer has ruled out
	Vector vecProbe = tr.endpos + tr.plane.normal;
	if ( !Q_stricmp( tr.surface.name, DISPLACER_BLOCK_TEXTURE ) || CTriggerDisplacerBlock::IsBoxBlocked( vecProbe, vec3_origin, vec3_origin ) )
	{
		DryFire();
		Displacer_Fizzle( tr.endpos );
		return;
	}

	SetDestination( tr.endpos, tr.plane.normal );

	pOwner->EmitSound( "Weapon_Displacer.SetDestination" );
	SendWeaponAnim( ACT_VM_SECONDARYATTACK );
}

//-----------------------------------------------------------------------------
// Send the player to the destination
//-----------------------------------------------------------------------------
void CWeaponDisplacer::TeleportSelf( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	// Shares the cooldown with the portal
	if ( m_flNextPrimaryAttack > gpGlobals->curtime )
		return;

	if ( !m_bHasDestination || pOwner->IsInAVehicle() )
	{
		DryFire();
		return;
	}

	if ( !DisplaceEntity( pOwner ) )
	{
		// No room for us there
		DryFire();
		Displacer_Fizzle( m_vecDestPoint + m_vecDestNormal * 2.0f );
		return;
	}

	m_flNextPrimaryAttack = gpGlobals->curtime + MAX( of2_displacer_cooldown.GetFloat(), 0.1f );

	color32 flash = { DISPLACER_GLOW_COLOR, 96 };
	UTIL_ScreenFade( pOwner, flash, 0.3f, 0.0f, FFADE_IN );
}

//-----------------------------------------------------------------------------
// Find a spot for pEntity at the destination: against the surface first, then
// a little higher or further out if something is in the way.
//-----------------------------------------------------------------------------
bool CWeaponDisplacer::FindRoomAtDestination( CBaseEntity *pEntity, Vector *pResult )
{
	Vector mins, maxs;
	Displacer_GetTeleportBox( pEntity, mins, maxs );

	Vector vecCenter = ( mins + maxs ) * 0.5f;
	Vector vecBase = Displacer_PlaceBox( m_vecDestPlace, m_vecDestNormal, mins, maxs );
	Vector vecSurface = m_vecDestPlace + m_vecDestNormal;

	static const float flNudges[] = { 0.0f, 8.0f, 16.0f, 32.0f, 56.0f };

	for ( int i = 0; i < ARRAYSIZE( flNudges ); i++ )
	{
		// Raised, pulled back along the normal, and both
		Vector vecTries[3];
		vecTries[0] = vecBase + Vector( 0, 0, flNudges[i] );
		vecTries[1] = vecBase + m_vecDestNormal * flNudges[i];
		vecTries[2] = vecTries[1] + Vector( 0, 0, flNudges[i] );

		// With no nudge all three are the same spot
		int nTries = ( i == 0 ) ? 1 : ARRAYSIZE( vecTries );

		for ( int j = 0; j < nTries; j++ )
		{
			if ( !Displacer_CanFitAt( pEntity, vecTries[j], mins, maxs ) )
				continue;

			// A nudge mustn't put it on the far side of a thin wall or floor
			trace_t tr;
			UTIL_TraceLine( vecSurface, vecTries[j] + vecCenter, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
			if ( tr.fraction != 1.0f )
				continue;

			*pResult = vecTries[j];

			if ( of2_displacer_debug.GetBool() )
			{
				NDebugOverlay::Box( vecTries[j], mins, maxs, DISPLACER_COLOR, 32, 3.0f );
			}
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Orientation and velocity are kept: momentum in, momentum out
//-----------------------------------------------------------------------------
bool CWeaponDisplacer::DisplaceEntity( CBaseEntity *pEntity )
{
	if ( pEntity == NULL || !m_bHasDestination )
		return false;

	if ( !pEntity->IsPlayer() && !Displacer_IsDisplaceable( pEntity ) )
		return false;

	if ( !CanAffordDisplacement() )
		return false;

	// A marked enemy has moved since the last think, and can't be sent to itself
	UpdateTarget();
	if ( m_bDestIsTarget && pEntity == m_hDestTarget )
		return false;

	Vector vecDest;
	if ( !FindRoomAtDestination( pEntity, &vecDest ) )
		return false;

	Vector vecFrom = pEntity->WorldSpaceCenter();

	Vector mins, maxs;
	Displacer_GetTeleportBox( pEntity, mins, maxs );
	float flRadius = ( maxs - mins ).Length() * 0.5f;

	if ( !pEntity->IsPlayer() )
	{
		CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
		if ( pOwner )
		{
			pOwner->ForceDropOfCarriedPhysObjects( pEntity );
		}
	}

	pEntity->Teleport( &vecDest, NULL, NULL );

	if ( pEntity->IsNPC() )
	{
		// Whatever it was standing on isn't under it any more
		pEntity->SetGroundEntity( NULL );
	}
	else if ( !pEntity->IsPlayer() )
	{
		// Resting objects would hang in the air asleep
		IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int nCount = pEntity->VPhysicsGetObjectList( pList, ARRAYSIZE( pList ) );
		for ( int i = 0; i < nCount; i++ )
		{
			pList[i]->Wake();
		}
	}

	SpendDisplacementCost();

	Displacer_TeleportEffect( vecFrom, flRadius, NULL, "Weapon_Displacer.TeleportOut" );
	Displacer_TeleportEffect( pEntity->WorldSpaceCenter(), flRadius, pEntity, "Weapon_Displacer.TeleportIn" );

	return true;
}

//-----------------------------------------------------------------------------
// Destination upkeep
//-----------------------------------------------------------------------------
void CWeaponDisplacer::SetDestination( const Vector &vecPoint, const Vector &vecNormal, CBaseEntity *pTarget )
{
	m_bHasDestination = true;
	m_vecDestPoint = vecPoint;
	m_vecDestPlace = vecPoint;
	m_vecDestNormal = vecNormal;
	m_bDestIsTarget = ( pTarget != NULL );
	m_hDestTarget = pTarget;
	m_iszDestMap = gpGlobals->mapname;
	m_iSignal = 100;
	m_flSignalLostTime = 0.0f;

	UpdateTarget();
	SetContextThink( &CWeaponDisplacer::DestinationThink, gpGlobals->curtime, s_pDestinationContext );
}

void CWeaponDisplacer::ClearDestination( void )
{
	m_bHasDestination = false;
	m_bDestIsTarget = false;
	m_hDestTarget = NULL;
	m_iSignal = 0;
	m_iszDestMap = NULL_STRING;

	SetContextThink( NULL, TICK_NEVER_THINK, s_pDestinationContext );
}

void CWeaponDisplacer::SignalLost( void )
{
	if ( !m_bHasDestination )
		return;

	ClearDestination();
	m_flSignalLostTime = gpGlobals->curtime;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner )
	{
		pOwner->EmitSound( "Weapon_Displacer.SignalLost" );
	}
}

bool CWeaponDisplacer::IsDestinationFromThisMap( void )
{
	return m_iszDestMap != NULL_STRING && FStrEq( STRING( m_iszDestMap ), STRING( gpGlobals->mapname ) );
}

//-----------------------------------------------------------------------------
// Keeps the destination on a marked enemy. Things are placed standing on top
// of its hull, so they come down on it. Once it is dead or gone the
// destination stays on the ground where it last was.
//-----------------------------------------------------------------------------
void CWeaponDisplacer::UpdateTarget( void )
{
	if ( !m_bDestIsTarget )
		return;

	CBaseEntity *pTarget = m_hDestTarget;
	if ( pTarget && pTarget->IsAlive() )
	{
		Vector mins, maxs;
		Displacer_GetTeleportBox( pTarget, mins, maxs );

		m_vecDestPoint = pTarget->WorldSpaceCenter();
		m_vecDestPlace = pTarget->GetAbsOrigin() + Vector( ( mins.x + maxs.x ) * 0.5f, ( mins.y + maxs.y ) * 0.5f, maxs.z );
		m_vecDestNormal.Init( 0, 0, 1 );
		return;
	}

	m_bDestIsTarget = false;
	m_hDestTarget = NULL;

	Vector vecLast = m_vecDestPoint;
	trace_t tr;
	UTIL_TraceLine( vecLast, vecLast - Vector( 0, 0, 256 ), MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction < 1.0f && !tr.startsolid )
	{
		m_vecDestPoint = tr.endpos;
		m_vecDestNormal = tr.plane.normal;
	}
	m_vecDestPlace = m_vecDestPoint;
}

//-----------------------------------------------------------------------------
// Runs while a destination exists, whether or not the weapon is in hand
//-----------------------------------------------------------------------------
void CWeaponDisplacer::DestinationThink( void )
{
	if ( !m_bHasDestination )
		return;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL || !IsDestinationFromThisMap() )
	{
		ClearDestination();
		return;
	}

	UpdateTarget();

	float flRange = MAX( of2_displacer_range.GetFloat(), 1.0f );
	float flDist = ( pOwner->EyePosition() - m_vecDestPoint ).Length();
	if ( flDist > flRange + DISPLACER_RANGE_SLOP )
	{
		SignalLost();
		return;
	}

	m_iSignal = clamp( RoundFloatToInt( 100.0f * ( 1.0f - flDist / flRange ) ), 0, 100 );

	if ( of2_displacer_debug.GetBool() )
	{
		NDebugOverlay::Box( m_vecDestPlace, -Vector( 4, 4, 4 ), Vector( 4, 4, 4 ), DISPLACER_COLOR, 64, DISPLACER_THINK_INTERVAL * 2.0f );
		NDebugOverlay::Line( m_vecDestPlace, m_vecDestPlace + m_vecDestNormal * 16.0f, DISPLACER_COLOR, false, DISPLACER_THINK_INTERVAL * 2.0f );
	}

	SetContextThink( &CWeaponDisplacer::DestinationThink, gpGlobals->curtime + DISPLACER_THINK_INTERVAL, s_pDestinationContext );
}
