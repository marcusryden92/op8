//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: Desert Eagle. Secondary fire switches a laser sight on and off.
//
//			Laser on: a red dot shows where the shot will land, the gun fires
//			slowly and the shot goes to the dot. Laser off: it fires fast, with
//			spread. The numbers are the of2_deagle_* convars below.
//
//			It takes the 357's ammo, so the damage is that ammo type's
//			(sk_plr_dmg_357 in cfg\skill.cfg), and an NPC given one handles it
//			as a 357.
//
//			The dot is the RPG's sprite, smaller. It is not the RPG's CLaserDot
//			entity: missiles steer towards those.
//
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "basehlcombatweapon.h"
#include "basecombatcharacter.h"
#include "ai_basenpc.h"
#include "player.h"
#include "in_buttons.h"
#include "soundent.h"
#include "Sprite.h"
#include "gamestats.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Not archived; they reset on restart.
ConVar of2_deagle_firerate( "of2_deagle_firerate", "0.22", FCVAR_NONE, "Seconds between Desert Eagle shots with the laser off." );
ConVar of2_deagle_firerate_laser( "of2_deagle_firerate_laser", "0.5", FCVAR_NONE, "Seconds between Desert Eagle shots with the laser on." );
ConVar of2_deagle_spread( "of2_deagle_spread", "5", FCVAR_NONE, "Desert Eagle spread with the laser off, as a cone in degrees." );
ConVar of2_deagle_spread_laser( "of2_deagle_spread_laser", "0", FCVAR_NONE, "Desert Eagle spread with the laser on, as a cone in degrees. 0 puts every shot on the dot." );
ConVar of2_deagle_laser_scale( "of2_deagle_laser_scale", "0.6", FCVAR_NONE, "Size of the Desert Eagle's laser dot, as a fraction of the RPG's." );

// However the convars are set, a shot takes this long
#define DEAGLE_MIN_REFIRE		0.05f

#define DEAGLE_LASER_SPRITE		"sprites/redglow1.vmt"

// The dot's size is rolled again this often, as the RPG's is
#define DEAGLE_LASER_FLICKER_TIME	0.05f

// NPCs use the 357's activities (weapon_357.cpp)
extern acttable_t *Get357Acttable();
extern int Get357ActtableCount();

//-----------------------------------------------------------------------------
// CWeaponDeagle
//-----------------------------------------------------------------------------
class CWeaponDeagle : public CBaseHLCombatWeapon
{
	DECLARE_CLASS( CWeaponDeagle, CBaseHLCombatWeapon );
public:

	CWeaponDeagle( void );

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	void	Precache( void );
	void	PrimaryAttack( void );
	void	ItemPostFrame( void );
	void	ItemBusyFrame( void );
	bool	Holster( CBaseCombatWeapon *pSwitchingTo = NULL );
	void	Drop( const Vector &vecVelocity );
	void	UpdateOnRemove( void );

	float	WeaponAutoAimScale()	{ return 0.6f; }
	float	GetFireRate( void );
	const Vector &GetBulletSpread( void );

	// NPCs
	int		CapabilitiesGet( void ) { return bits_CAP_WEAPON_RANGE_ATTACK1; }
	int		GetMinBurst() { return 1; }
	int		GetMaxBurst() { return 1; }
	float	GetMinRestTime( void ) { return 1.0f; }
	float	GetMaxRestTime( void ) { return 2.5f; }

	acttable_t	*ActivityList( void ) { return Get357Acttable(); }
	int		ActivityListCount( void ) { return Get357ActtableCount(); }

	void	Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator );
	void	FireNPCPrimaryAttack( CBaseCombatCharacter *pOperator, Vector &vecShootOrigin, Vector &vecShootDir );
	void	Operator_ForceNPCFire( CBaseCombatCharacter *pOperator, bool bSecondary );

private:
	void	CheckLaserToggle( void );
	void	UpdateLaser( bool bReady );
	void	HideLaser( void );
	void	RemoveLaser( void );
	Vector	GetLaserDirection( CBasePlayer *pPlayer );

	bool				m_bLaserOn;
	CHandle<CSprite>	m_hLaserDot;

	// The dot's size flickers; not saved
	float				m_flLaserFlicker;
	float				m_flNextLaserFlicker;
};

LINK_ENTITY_TO_CLASS( weapon_deagle, CWeaponDeagle );
PRECACHE_WEAPON_REGISTER( weapon_deagle );

IMPLEMENT_SERVERCLASS_ST( CWeaponDeagle, DT_WeaponDeagle )
END_SEND_TABLE()

BEGIN_DATADESC( CWeaponDeagle )

	DEFINE_FIELD( m_bLaserOn, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_hLaserDot, FIELD_EHANDLE ),

END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CWeaponDeagle::CWeaponDeagle( void )
{
	m_bReloadsSingly	= false;
	m_bFiresUnderwater	= false;

	// Secondary fire is only the laser's switch
	m_bAltFiresUnderwater = true;

	m_bLaserOn = false;
	m_flLaserFlicker = 0.0f;
	m_flNextLaserFlicker = 0.0f;

	m_fMinRange1		= 24;
	m_fMaxRange1		= 1000;
	m_fMinRange2		= 24;
	m_fMaxRange2		= 200;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponDeagle::Precache( void )
{
	BaseClass::Precache();

	PrecacheModel( DEAGLE_LASER_SPRITE );
}

//-----------------------------------------------------------------------------
// Purpose: Time from one shot to the next
//-----------------------------------------------------------------------------
float CWeaponDeagle::GetFireRate( void )
{
	if ( GetOwner() && GetOwner()->IsNPC() )
		return 1.0f;

	float flRate = m_bLaserOn ? of2_deagle_firerate_laser.GetFloat() : of2_deagle_firerate.GetFloat();

	return MAX( flRate, DEAGLE_MIN_REFIRE );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const Vector &CWeaponDeagle::GetBulletSpread( void )
{
	if ( GetOwner() && GetOwner()->IsNPC() )
	{
		static Vector AllyCone = VECTOR_CONE_2DEGREES;
		static Vector NPCCone = VECTOR_CONE_5DEGREES;

		return GetOwner()->MyNPCPointer()->IsPlayerAlly() ? AllyCone : NPCCone;
	}

	// The VECTOR_CONE_ values are the sine of half the cone
	float flDegrees = m_bLaserOn ? of2_deagle_spread_laser.GetFloat() : of2_deagle_spread.GetFloat();
	float flSine = sin( DEG2RAD( clamp( flDegrees, 0.0f, 90.0f ) * 0.5f ) );

	static Vector cone;
	cone.Init( flSine, flSine, flSine );

	return cone;
}

//-----------------------------------------------------------------------------
// Purpose: Where the gun points, kick included: bullets take the view punch
//			with them, so the dot does too.
//-----------------------------------------------------------------------------
Vector CWeaponDeagle::GetLaserDirection( CBasePlayer *pPlayer )
{
	Vector vecDir;
	AngleVectors( pPlayer->EyeAngles() + pPlayer->GetPunchAngle(), &vecDir );

	return vecDir;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponDeagle::PrimaryAttack( void )
{
	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );

	if ( !pPlayer )
	{
		return;
	}

	if ( m_iClip1 <= 0 )
	{
		if ( !m_bFireOnEmpty )
		{
			Reload();
		}
		else
		{
			WeaponSound( EMPTY );
			m_flNextPrimaryAttack = gpGlobals->curtime + 0.15f;
		}

		return;
	}

	m_iPrimaryAttacks++;
	gamestats->Event_WeaponFired( pPlayer, true, GetClassname() );

	WeaponSound( SINGLE );
	pPlayer->DoMuzzleFlash();

	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	pPlayer->SetAnimation( PLAYER_ATTACK1 );

	m_flNextPrimaryAttack = gpGlobals->curtime + GetFireRate();

	m_iClip1--;

	// With the laser on the shot goes where the dot is, with no autoaim to pull it off
	Vector vecSrc		= pPlayer->Weapon_ShootPosition();
	Vector vecAiming	= m_bLaserOn ? GetLaserDirection( pPlayer ) : pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );

	pPlayer->FireBullets( 1, vecSrc, vecAiming, GetBulletSpread(), MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 0 );

	pPlayer->SetMuzzleFlashTime( gpGlobals->curtime + 0.5 );

	// Half the 357's kick; shooting from the hip throws it sideways more
	float flSideways = m_bLaserOn ? 1.0f : 2.0f;
	pPlayer->ViewPunch( QAngle( -4, random->RandomFloat( -flSideways, flSideways ), 0 ) );

	CSoundEnt::InsertSound( SOUND_COMBAT, GetAbsOrigin(), 600, 0.2, GetOwner() );

	if ( !m_iClip1 && pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
	{
		// HEV suit - indicate out of ammo condition
		pPlayer->SetSuitUpdate( "!HEV_AMO0", FALSE, 0 );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponDeagle::ItemPostFrame( void )
{
	CheckLaserToggle();

	BaseClass::ItemPostFrame();

	// After the base class, so a reload that starts this frame hides the dot
	UpdateLaser( true );
}

//-----------------------------------------------------------------------------
// Purpose: The gun is being drawn or reloaded: no dot, but the switch works
//-----------------------------------------------------------------------------
void CWeaponDeagle::ItemBusyFrame( void )
{
	CheckLaserToggle();

	BaseClass::ItemBusyFrame();

	UpdateLaser( false );
}

//-----------------------------------------------------------------------------
// Purpose: One switch per press of secondary fire
//-----------------------------------------------------------------------------
void CWeaponDeagle::CheckLaserToggle( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );

	if ( pPlayer == NULL || !( pPlayer->m_afButtonPressed & IN_ATTACK2 ) )
		return;

	m_bLaserOn = !m_bLaserOn;

	WeaponSound( m_bLaserOn ? SPECIAL1 : SPECIAL2 );

	m_iSecondaryAttacks++;
	gamestats->Event_WeaponFired( pPlayer, false, GetClassname() );
}

//-----------------------------------------------------------------------------
// Purpose: Put the dot where the gun points, or hide it.
// Input  : bReady - the gun is up and could fire
//-----------------------------------------------------------------------------
void CWeaponDeagle::UpdateLaser( bool bReady )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );

	// The base class's frame can put the gun away (out of ammo, switch to the next
	// weapon) before this runs, so check that it is still the one in hand
	if ( !m_bLaserOn || !bReady || pPlayer == NULL || pPlayer->GetActiveWeapon() != this || m_bInReload || m_bLowered )
	{
		HideLaser();
		return;
	}

	// The same line a bullet takes
	Vector vecSrc = pPlayer->Weapon_ShootPosition();
	Vector vecEnd = vecSrc + GetLaserDirection( pPlayer ) * MAX_TRACE_LENGTH;

	trace_t tr;
	UTIL_TraceLine( vecSrc, vecEnd, MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &tr );

	// Nothing to shine on
	if ( tr.fraction == 1.0f || ( tr.surface.flags & SURF_SKY ) )
	{
		HideLaser();
		return;
	}

	if ( m_hLaserDot == NULL )
	{
		m_hLaserDot = CSprite::SpriteCreate( DEAGLE_LASER_SPRITE, tr.endpos, false );

		if ( m_hLaserDot == NULL )
			return;

		// Left out of saves and level changes; this makes a new one when it is next needed
		m_hLaserDot->SetAsTemporary();
		m_hLaserDot->AddEffects( EF_NOSHADOW );
		m_hLaserDot->SetTransparency( kRenderGlow, 255, 255, 255, 255, kRenderFxNoDissipation );
		m_hLaserDot->SetSimulatedEveryTick( true );
	}

	m_hLaserDot->SetAbsOrigin( tr.endpos );

	if ( gpGlobals->curtime >= m_flNextLaserFlicker || gpGlobals->curtime < m_flNextLaserFlicker - DEAGLE_LASER_FLICKER_TIME )
	{
		m_flLaserFlicker = random->RandomFloat( -0.25f, 0.25f );
		m_flNextLaserFlicker = gpGlobals->curtime + DEAGLE_LASER_FLICKER_TIME;
	}

	// The RPG's dot (CLaserDot::LaserThink): it grows with distance to hold its size on
	// screen, and flickers by a quarter. This one is that times of2_deagle_laser_scale.
	float flDist = ( tr.endpos - pPlayer->GetAbsOrigin() ).Length();
	float flScale = RemapVal( flDist, 32, 1024, 0.01f, 0.5f );
	flScale = clamp( flScale * ( 1.0f + m_flLaserFlicker ), 0.1f, 32.0f );

	m_hLaserDot->SetScale( flScale * MAX( of2_deagle_laser_scale.GetFloat(), 0.0f ) );

	if ( !m_hLaserDot->IsOn() )
	{
		m_hLaserDot->TurnOn();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponDeagle::HideLaser( void )
{
	if ( m_hLaserDot != NULL && m_hLaserDot->IsOn() )
	{
		m_hLaserDot->TurnOff();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponDeagle::RemoveLaser( void )
{
	if ( m_hLaserDot != NULL )
	{
		UTIL_Remove( m_hLaserDot );
		m_hLaserDot = NULL;
	}
}

//-----------------------------------------------------------------------------
// Purpose: The laser stays switched on for when the gun comes back out
//-----------------------------------------------------------------------------
bool CWeaponDeagle::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	HideLaser();

	return BaseClass::Holster( pSwitchingTo );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponDeagle::Drop( const Vector &vecVelocity )
{
	RemoveLaser();

	BaseClass::Drop( vecVelocity );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponDeagle::UpdateOnRemove( void )
{
	RemoveLaser();

	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// Purpose: NPCs fire from their animation
//-----------------------------------------------------------------------------
void CWeaponDeagle::Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator )
{
	switch( pEvent->event )
	{
		case EVENT_WEAPON_PISTOL_FIRE:
			{
				Vector vecShootOrigin, vecShootDir;
				vecShootOrigin = pOperator->Weapon_ShootPosition();

				CAI_BaseNPC *npc = pOperator->MyNPCPointer();
				ASSERT( npc != NULL );

				vecShootDir = npc->GetActualShootTrajectory( vecShootOrigin );

				FireNPCPrimaryAttack( pOperator, vecShootOrigin, vecShootDir );
			}
			break;

		default:
			BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
			break;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponDeagle::FireNPCPrimaryAttack( CBaseCombatCharacter *pOperator, Vector &vecShootOrigin, Vector &vecShootDir )
{
	CSoundEnt::InsertSound( SOUND_COMBAT|SOUND_CONTEXT_GUNFIRE, pOperator->GetAbsOrigin(), SOUNDENT_VOLUME_PISTOL, 0.2, pOperator, SOUNDENT_CHANNEL_WEAPON, pOperator->GetEnemy() );

	WeaponSound( SINGLE_NPC );
	pOperator->FireBullets( 1, vecShootOrigin, vecShootDir, VECTOR_CONE_PRECALCULATED, MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 1 );
	pOperator->DoMuzzleFlash();
	m_iClip1 = m_iClip1 - 1;
}

//-----------------------------------------------------------------------------
// Purpose: Some things need this. (e.g. the new Force(X)Fire inputs or blindfire actbusy)
//-----------------------------------------------------------------------------
void CWeaponDeagle::Operator_ForceNPCFire( CBaseCombatCharacter *pOperator, bool bSecondary )
{
	// Ensure we have enough rounds in the clip
	m_iClip1++;

	Vector vecShootOrigin, vecShootDir;
	QAngle	angShootDir;
	GetAttachment( LookupAttachment( "muzzle" ), vecShootOrigin, angShootDir );
	AngleVectors( angShootDir, &vecShootDir );
	FireNPCPrimaryAttack( pOperator, vecShootOrigin, vecShootDir );
}
