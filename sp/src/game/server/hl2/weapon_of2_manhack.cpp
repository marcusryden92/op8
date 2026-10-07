//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: a manhack thrown by hand. The frag grenade's weapon with the
//			grenade swapped for a folded manhack that is on the player's side:
//			it flies as a loose object, unfolds on the way (the stock
//			packed-up manhack, as metrocops release them) and goes for
//			whatever the player's allies would.
//
//			Primary fire throws it, secondary tosses it (crouched: drops it
//			ahead). The viewmodel is a copy of the grenade's with the grenade
//			hidden; the client draws the manhack in the hand
//			(client\hl2\of2_viewmodel_manhack.cpp).
//
//			For the player only. of2_give_manhack hands some over.
//
//=============================================================================//

#include "cbase.h"
#include "basehlcombatweapon.h"
#include "player.h"
#include "gamerules.h"
#include "npc_manhack.h"
#include "npcevent.h"
#include "in_buttons.h"
#include "soundent.h"
#include "gamestats.h"
#include "vphysics_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Not archived; they reset on restart.
ConVar of2_manhack_throw_speed( "of2_manhack_throw_speed", "700", FCVAR_NONE, "Speed a manhack leaves the hand at on a throw (primary fire). A frag grenade's is 1200." );
ConVar of2_manhack_toss_speed( "of2_manhack_toss_speed", "300", FCVAR_NONE, "Speed a manhack leaves the hand at on a toss (secondary fire)." );
ConVar of2_manhack_drop_speed( "of2_manhack_drop_speed", "120", FCVAR_NONE, "Speed a manhack leaves the hand at when dropped ahead (secondary fire, crouched)." );

#define MANHACK_PAUSED_NO			0
#define MANHACK_PAUSED_PRIMARY		1
#define MANHACK_PAUSED_SECONDARY	2

// Room the folded manhack needs where it leaves the hand
#define MANHACK_THROW_RADIUS		10.0f

// Seconds until the next one can be thrown
#define MANHACK_RETHROW_DELAY		0.5f

//-----------------------------------------------------------------------------
// CWeaponManhack: the throw is CWeaponFrag's, step for step
//-----------------------------------------------------------------------------
class CWeaponManhack : public CBaseHLCombatWeapon
{
	DECLARE_CLASS( CWeaponManhack, CBaseHLCombatWeapon );
public:
	DECLARE_SERVERCLASS();

	CWeaponManhack();

	void	Precache( void );
	void	Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator );
	void	PrimaryAttack( void );
	void	SecondaryAttack( void );
	void	ItemPostFrame( void );

	bool	Deploy( void );
	bool	Holster( CBaseCombatWeapon *pSwitchingTo = NULL );
	bool	Reload( void );

	// Not for NPCs
	int		CapabilitiesGet( void ) { return 0; }

private:
	void	ThrowManhack( CBasePlayer *pPlayer, float flSpeed, float flUp, float flBelowEye );

	bool	m_bRedraw;	// Bring the next one up once the throw has played
	int		m_AttackPaused;
	bool	m_fDrawbackFinished;

	DECLARE_ACTTABLE();
	DECLARE_DATADESC();
};

BEGIN_DATADESC( CWeaponManhack )
	DEFINE_FIELD( m_bRedraw, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_AttackPaused, FIELD_INTEGER ),
	DEFINE_FIELD( m_fDrawbackFinished, FIELD_BOOLEAN ),
END_DATADESC()

acttable_t	CWeaponManhack::m_acttable[] =
{
	{ ACT_RANGE_ATTACK1, ACT_RANGE_ATTACK_SLAM, true },

#ifdef MAPBASE
	// HL2:DM activities (for third-person animations in SP)
	{ ACT_HL2MP_IDLE,					ACT_HL2MP_IDLE_GRENADE,                    false },
	{ ACT_HL2MP_RUN,					ACT_HL2MP_RUN_GRENADE,                    false },
	{ ACT_HL2MP_IDLE_CROUCH,			ACT_HL2MP_IDLE_CROUCH_GRENADE,            false },
	{ ACT_HL2MP_WALK_CROUCH,			ACT_HL2MP_WALK_CROUCH_GRENADE,            false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK,	ACT_HL2MP_GESTURE_RANGE_ATTACK_GRENADE,    false },
	{ ACT_HL2MP_GESTURE_RELOAD,			ACT_HL2MP_GESTURE_RELOAD_GRENADE,        false },
	{ ACT_HL2MP_JUMP,					ACT_HL2MP_JUMP_GRENADE,			false },
#if EXPANDED_HL2DM_ACTIVITIES
	{ ACT_HL2MP_WALK,					ACT_HL2MP_WALK_GRENADE,					false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK2,	ACT_HL2MP_GESTURE_RANGE_ATTACK2_GRENADE,    false },
#endif
#endif
};

IMPLEMENT_ACTTABLE( CWeaponManhack );

IMPLEMENT_SERVERCLASS_ST( CWeaponManhack, DT_WeaponManhack )
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_manhack, CWeaponManhack );
PRECACHE_WEAPON_REGISTER( weapon_manhack );

CWeaponManhack::CWeaponManhack()
{
	m_bRedraw = false;
	m_AttackPaused = MANHACK_PAUSED_NO;
	m_fDrawbackFinished = false;
}

void CWeaponManhack::Precache( void )
{
	BaseClass::Precache();

	// Also what the client draws in the hand
	UTIL_PrecacheOther( "npc_manhack" );
}

bool CWeaponManhack::Deploy( void )
{
	m_bRedraw = false;
	m_fDrawbackFinished = false;

	return BaseClass::Deploy();
}

bool CWeaponManhack::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	m_bRedraw = false;
	m_fDrawbackFinished = false;

	return BaseClass::Holster( pSwitchingTo );
}

//-----------------------------------------------------------------------------
// Purpose: the viewmodel's animations say when the hand lets go
//-----------------------------------------------------------------------------
void CWeaponManhack::Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	bool fThrew = false;

	switch( pEvent->event )
	{
		case EVENT_WEAPON_SEQUENCE_FINISHED:
			m_fDrawbackFinished = true;
			break;

		case EVENT_WEAPON_THROW:
			if ( pOwner )
			{
				ThrowManhack( pOwner, of2_manhack_throw_speed.GetFloat(), 0.1f, 0.0f );
				WeaponSound( SINGLE );
				fThrew = true;
			}
			break;

		// The grenade's roll along the floor: put down ahead
		case EVENT_WEAPON_THROW2:
			if ( pOwner )
			{
				ThrowManhack( pOwner, of2_manhack_drop_speed.GetFloat(), 0.0f, 16.0f );
				WeaponSound( SPECIAL1 );
				fThrew = true;
			}
			break;

		case EVENT_WEAPON_THROW3:
			if ( pOwner )
			{
				ThrowManhack( pOwner, of2_manhack_toss_speed.GetFloat(), 0.15f, 8.0f );
				WeaponSound( WPN_DOUBLE );
				fThrew = true;
			}
			break;

		default:
			BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
			break;
	}

	if ( fThrew )
	{
		pOwner->RemoveAmmo( 1, m_iPrimaryAmmoType );
		m_bRedraw = true;

		m_flNextPrimaryAttack	= gpGlobals->curtime + MANHACK_RETHROW_DELAY;
		m_flNextSecondaryAttack	= gpGlobals->curtime + MANHACK_RETHROW_DELAY;
		m_flTimeWeaponIdle = FLT_MAX; //NOTE: This is set once the animation has finished up!

		m_iPrimaryAttacks++;
		gamestats->Event_WeaponFired( pOwner, true, GetClassname() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: bring up the next one
//-----------------------------------------------------------------------------
bool CWeaponManhack::Reload( void )
{
	if ( !HasPrimaryAmmo() )
		return false;

	if ( ( m_bRedraw ) && ( m_flNextPrimaryAttack <= gpGlobals->curtime ) && ( m_flNextSecondaryAttack <= gpGlobals->curtime ) )
	{
		SendWeaponAnim( ACT_VM_DRAW );

		m_flNextPrimaryAttack	= gpGlobals->curtime + SequenceDuration();
		m_flNextSecondaryAttack	= gpGlobals->curtime + SequenceDuration();
		m_flTimeWeaponIdle = gpGlobals->curtime + SequenceDuration();

		m_bRedraw = false;
	}

	return true;
}

void CWeaponManhack::PrimaryAttack( void )
{
	if ( m_bRedraw || !HasPrimaryAmmo() )
		return;

	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( pPlayer == NULL )
		return;

	// Pull back, and hold there for as long as the button is down
	m_AttackPaused = MANHACK_PAUSED_PRIMARY;
	SendWeaponAnim( ACT_VM_PULLBACK_HIGH );

	m_flTimeWeaponIdle = FLT_MAX;
	m_flNextPrimaryAttack = FLT_MAX;
}

void CWeaponManhack::SecondaryAttack( void )
{
	if ( m_bRedraw || !HasPrimaryAmmo() )
		return;

	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( pPlayer == NULL )
		return;

	m_AttackPaused = MANHACK_PAUSED_SECONDARY;
	SendWeaponAnim( ACT_VM_PULLBACK_LOW );

	m_flTimeWeaponIdle = FLT_MAX;
	m_flNextSecondaryAttack	= FLT_MAX;
}

void CWeaponManhack::ItemPostFrame( void )
{
	if ( m_fDrawbackFinished )
	{
		CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

		if ( pOwner )
		{
			switch( m_AttackPaused )
			{
			case MANHACK_PAUSED_PRIMARY:
				if ( !( pOwner->m_nButtons & IN_ATTACK ) )
				{
					SendWeaponAnim( ACT_VM_THROW );
					m_fDrawbackFinished = false;
				}
				break;

			case MANHACK_PAUSED_SECONDARY:
				if ( !( pOwner->m_nButtons & IN_ATTACK2 ) )
				{
					// Crouched: put it down ahead; standing: toss it
					SendWeaponAnim( ( pOwner->m_nButtons & IN_DUCK ) ? ACT_VM_SECONDARYATTACK : ACT_VM_HAULBACK );
					m_fDrawbackFinished = false;
				}
				break;

			default:
				break;
			}
		}
	}

	BaseClass::ItemPostFrame();

	if ( m_bRedraw )
	{
		if ( IsViewModelSequenceFinished() )
		{
			Reload();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: a folded manhack on the player's side leaves the hand
// Input  : flSpeed - along the view
//			flUp - how far the throw is tilted up from the view
//			flBelowEye - how far under the eye it starts
//-----------------------------------------------------------------------------
void CWeaponManhack::ThrowManhack( CBasePlayer *pPlayer, float flSpeed, float flUp, float flBelowEye )
{
	Vector vecEye = pPlayer->EyePosition();
	Vector vForward, vRight;
	pPlayer->EyeVectors( &vForward, &vRight, NULL );

	// It is a solid thing the size of two fists: start clear of the player, and of any wall ahead
	Vector vecSrc = vecEye + vForward * 34.0f + vRight * 6.0f - Vector( 0, 0, flBelowEye );

	trace_t tr;
	Vector vecHull( MANHACK_THROW_RADIUS, MANHACK_THROW_RADIUS, MANHACK_THROW_RADIUS );
	UTIL_TraceHull( vecEye, vecSrc, -vecHull, vecHull, pPlayer->PhysicsSolidMaskForEntity(), pPlayer, pPlayer->GetCollisionGroup(), &tr );
	if ( tr.DidHit() )
	{
		vecSrc = tr.endpos;
	}

	CNPC_Manhack *pManhack = (CNPC_Manhack *)CreateEntityByName( "npc_manhack" );
	if ( pManhack == NULL )
		return;

	pManhack->SetAbsOrigin( vecSrc );
	pManhack->SetAbsAngles( QAngle( 0, pPlayer->EyeAngles().y, 0 ) );

	// Packed up it is a loose object until its unfolding animation starts the engine.
	// "Hacked" is Episode One's manhack turned by Alyx: green eye, the player's ally.
	pManhack->AddSpawnFlags( SF_MANHACK_PACKED_UP );
	pManhack->KeyValue( "Hacked", "1" );

	DispatchSpawn( pManhack );
	pManhack->Activate();

	vForward.z += flUp;

	Vector vecThrow;
	pPlayer->GetVelocity( &vecThrow, NULL );
	vecThrow += vForward * flSpeed;

	IPhysicsObject *pPhysics = pManhack->VPhysicsGetObject();
	if ( pPhysics )
	{
		AngularImpulse angImpulse( 0, random->RandomFloat( -200, 200 ), 0 );
		pPhysics->AddVelocity( &vecThrow, &angImpulse );
	}

#ifdef MAPBASE
	pPlayer->SetAnimation( PLAYER_ATTACK1 );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Manhacks to throw, without cheats on
//-----------------------------------------------------------------------------
CON_COMMAND( of2_give_manhack, "Gives you manhacks to throw." )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();

	if ( pPlayer == NULL || !pPlayer->IsAlive() )
		return;

	pPlayer->GiveNamedItem( "weapon_manhack" );
	pPlayer->GiveAmmo( 7, "Manhack" );

	CBaseCombatWeapon *pWeapon = pPlayer->Weapon_OwnsThisType( "weapon_manhack" );

	if ( pWeapon )
	{
		pPlayer->Weapon_Switch( pWeapon );
	}
}
