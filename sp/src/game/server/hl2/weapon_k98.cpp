//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: K98 sniper rifle. Bolt action, five rounds, secondary fire
//			looks through the scope.
//
//			Zoomed in the shot goes where the rifle points. From the hip it
//			has spread. Working the bolt takes the view out of the scope until
//			the next shot is ready. The numbers are the of2_k98_* convars below.
//
//			It takes the 357's ammo but does its own damage (sk_plr_dmg_k98 in
//			cfg\skill.cfg).
//
//			Zoomed in, the client draws a scope over the screen in place of the
//			rifle ("scope_overlay" in its script, client\hl2\hud_scope.cpp).
//
//			The bullet goes through people (of2_k98_penetrate of them), losing
//			some of its damage in each. Walls and machines stop it.
//
//			For the player only; NPCs cannot use it. of2_give_k98 hands one over.
//
//=============================================================================//

#include "cbase.h"
#include "basehlcombatweapon.h"
#include "basecombatcharacter.h"
#include "player.h"
#include "in_buttons.h"
#include "soundent.h"
#include "gamestats.h"
#include "ai_basenpc.h"
#include "shot_manipulator.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Also in cfg\skill.cfg. This has a real default so the rifle still kills if that file is missing.
ConVar sk_plr_dmg_k98( "sk_plr_dmg_k98", "100" );

// Not archived; they reset on restart.
ConVar of2_k98_firerate( "of2_k98_firerate", "1.5", FCVAR_NONE, "Seconds between K98 shots: the time it takes to work the bolt." );
ConVar of2_k98_zoom_fov( "of2_k98_zoom_fov", "20", FCVAR_NONE, "Field of view through the K98's scope, in degrees." );
ConVar of2_k98_bolt_unzoom( "of2_k98_bolt_unzoom", "1", FCVAR_NONE, "1: a K98 shot takes the view out of the scope until the bolt has been worked. 0: it stays in." );
ConVar of2_k98_spread( "of2_k98_spread", "4", FCVAR_NONE, "K98 spread from the hip, as a cone in degrees." );
ConVar of2_k98_spread_zoom( "of2_k98_spread_zoom", "0", FCVAR_NONE, "K98 spread through the scope, as a cone in degrees." );
ConVar of2_k98_penetrate( "of2_k98_penetrate", "2", FCVAR_NONE, "How many people one K98 bullet goes through (it can hit one more than this). 0: it stops in the first." );
ConVar of2_k98_penetrate_damage( "of2_k98_penetrate_damage", "0.75", FCVAR_NONE, "Share of its damage a K98 bullet keeps for each person it has gone through." );

// However the convars are set, a shot takes this long
#define K98_MIN_REFIRE		0.1f

// However the convar is set, a bullet goes through no more than this many
#define K98_MAX_PENETRATE	8

// Seconds for the view to go into and come out of the scope. At once, as in Counter-Strike:
// the scope picture comes and goes at once, and a gradual zoom showed under it.
#define K98_ZOOM_IN_TIME	0.0f
#define K98_ZOOM_OUT_TIME	0.0f

//-----------------------------------------------------------------------------
// CWeaponK98
//-----------------------------------------------------------------------------
class CWeaponK98 : public CBaseHLCombatWeapon
{
	DECLARE_CLASS( CWeaponK98, CBaseHLCombatWeapon );
public:

	CWeaponK98( void );

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	void	PrimaryAttack( void );
	bool	Reload( void );
	void	ItemPostFrame( void );
	void	ItemBusyFrame( void );
	bool	Holster( CBaseCombatWeapon *pSwitchingTo = NULL );
	void	Drop( const Vector &vecVelocity );

	bool	IsWeaponZoomed() { return m_bInZoom; }

	float	WeaponAutoAimScale()	{ return 0.6f; }
	float	GetFireRate( void );
	const Vector &GetBulletSpread( void );

	int		CapabilitiesGet( void ) { return 0; }

private:
	void	FireShot( CBasePlayer *pPlayer, const Vector &vecSrc, const Vector &vecDir );

	void	CheckZoomToggle( void );
	void	UpdateZoom( bool bReady );
	void	StopZoom( void );

	// Secondary fire's switch, and whether the view is in the scope right now
	bool	m_bZoomWanted;
	bool	m_bInZoom;

	// The bolt is being worked until then
	float	m_flBoltDoneTime;
};

LINK_ENTITY_TO_CLASS( weapon_k98, CWeaponK98 );
PRECACHE_WEAPON_REGISTER( weapon_k98 );

IMPLEMENT_SERVERCLASS_ST( CWeaponK98, DT_WeaponK98 )
END_SEND_TABLE()

BEGIN_DATADESC( CWeaponK98 )

	DEFINE_FIELD( m_bZoomWanted, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bInZoom, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flBoltDoneTime, FIELD_TIME ),

END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CWeaponK98::CWeaponK98( void )
{
	m_bReloadsSingly	= false;
	m_bFiresUnderwater	= false;

	// Secondary fire is only the scope
	m_bAltFiresUnderwater = true;

	m_bZoomWanted = false;
	m_bInZoom = false;
	m_flBoltDoneTime = 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: Time from one shot to the next
//-----------------------------------------------------------------------------
float CWeaponK98::GetFireRate( void )
{
	return MAX( of2_k98_firerate.GetFloat(), K98_MIN_REFIRE );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const Vector &CWeaponK98::GetBulletSpread( void )
{
	// The VECTOR_CONE_ values are the sine of half the cone
	float flDegrees = m_bInZoom ? of2_k98_spread_zoom.GetFloat() : of2_k98_spread.GetFloat();
	float flSine = sin( DEG2RAD( clamp( flDegrees, 0.0f, 90.0f ) * 0.5f ) );

	static Vector cone;
	cone.Init( flSine, flSine, flSine );

	return cone;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponK98::PrimaryAttack( void )
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

	// The animation is the shot and the bolt being worked after it
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	pPlayer->SetAnimation( PLAYER_ATTACK1 );

	m_flNextPrimaryAttack = gpGlobals->curtime + GetFireRate();

	m_iClip1--;

	// Through the scope the shot goes where the rifle points, kick included, with no
	// autoaim to pull it off
	Vector vecSrc = pPlayer->Weapon_ShootPosition();
	Vector vecAiming;

	if ( m_bInZoom )
	{
		AngleVectors( pPlayer->EyeAngles() + pPlayer->GetPunchAngle(), &vecAiming );
	}
	else
	{
		vecAiming = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );
	}

	// The spread is applied here, once, so that every part of the shot is on one line
	Vector vecDir = CShotManipulator( vecAiming ).ApplySpread( GetBulletSpread() );
	VectorNormalize( vecDir );

	FireShot( pPlayer, vecSrc, vecDir );

	pPlayer->SetMuzzleFlashTime( gpGlobals->curtime + 0.5 );

	pPlayer->ViewPunch( QAngle( -5, random->RandomFloat( -1, 1 ), 0 ) );

	CSoundEnt::InsertSound( SOUND_COMBAT, GetAbsOrigin(), 1000, 0.2, GetOwner() );

	// After the shot, so that it was fired through the scope
	if ( of2_k98_bolt_unzoom.GetBool() )
	{
		m_flBoltDoneTime = m_flNextPrimaryAttack;
	}

	if ( !m_iClip1 && pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
	{
		// HEV suit - indicate out of ammo condition
		pPlayer->SetSuitUpdate( "!HEV_AMO0", FALSE, 0 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Whether a K98 bullet goes on through this: living things up to
//			about a person's size. Not machines, and nothing big.
//-----------------------------------------------------------------------------
static bool K98_GoesThrough( CBaseEntity *pEntity )
{
	CAI_BaseNPC *pNPC = pEntity ? pEntity->MyNPCPointer() : NULL;

	if ( pNPC == NULL )
		return false;

	if ( pNPC->BloodColor() == DONT_BLEED || pNPC->BloodColor() == BLOOD_COLOR_MECH )
		return false;

	switch ( pNPC->GetHullType() )
	{
	case HULL_MEDIUM_TALL:
	case HULL_LARGE:
	case HULL_LARGE_CENTERED:
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: One shot along a line: a bullet for the first thing in the way, and
//			one more for each person it goes through, each ignoring the people
//			before it.
//-----------------------------------------------------------------------------
void CWeaponK98::FireShot( CBasePlayer *pPlayer, const Vector &vecSrc, const Vector &vecDir )
{
	// Find the people it goes through first, nearest first
	CUtlVector<CBaseEntity *> passed;

	CTraceFilterSimpleList filter( COLLISION_GROUP_NONE );
	filter.AddEntityToIgnore( pPlayer );

	int nMaxPassed = clamp( of2_k98_penetrate.GetInt(), 0, K98_MAX_PENETRATE );
	Vector vecEnd = vecSrc + vecDir * MAX_TRACE_LENGTH;

	while ( passed.Count() < nMaxPassed )
	{
		trace_t tr;
		UTIL_TraceLine( vecSrc, vecEnd, MASK_SHOT, &filter, &tr );

		if ( tr.startsolid || tr.fraction == 1.0f || !K98_GoesThrough( tr.m_pEnt ) )
			break;

		passed.AddToTail( tr.m_pEnt );
		filter.AddEntityToIgnore( tr.m_pEnt );
	}

	float flKeep = clamp( of2_k98_penetrate_damage.GetFloat(), 0.0f, 1.0f );

	// Farthest first: someone killed nearer by can leave a ragdoll in the line
	for ( int i = passed.Count(); i >= 0; i-- )
	{
		// This bullet ignores the people before the one it is for
		passed.SetCount( i );

		// The ammo is the 357's, the damage is this rifle's. Zero would mean the ammo's.
		FireBulletsInfo_t info( 1, vecSrc, vecDir, vec3_origin, MAX_TRACE_LENGTH, m_iPrimaryAmmoType );
		info.m_flDamage = MAX( sk_plr_dmg_k98.GetFloat() * pow( flKeep, (float)i ), 1.0f );
		info.m_iTracerFreq = 0;
		info.m_pAttacker = pPlayer;
		info.m_pIgnoreEntList = &passed;

		pPlayer->FireBullets( info );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A K98 and ammo for it, without cheats on
//-----------------------------------------------------------------------------
CON_COMMAND( of2_give_k98, "Gives you the K98 sniper rifle and ammo for it." )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();

	if ( pPlayer == NULL || !pPlayer->IsAlive() )
		return;

	pPlayer->GiveNamedItem( "weapon_k98" );
	pPlayer->GiveAmmo( 32, "357" );

	CBaseCombatWeapon *pRifle = pPlayer->Weapon_OwnsThisType( "weapon_k98" );

	if ( pRifle )
	{
		pPlayer->Weapon_Switch( pRifle );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Reloading comes out of the scope for good
//-----------------------------------------------------------------------------
bool CWeaponK98::Reload( void )
{
	bool bReloading = BaseClass::Reload();

	if ( bReloading )
	{
		StopZoom();
	}

	return bReloading;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponK98::ItemPostFrame( void )
{
	CheckZoomToggle();

	BaseClass::ItemPostFrame();

	// After the base class, so a shot fired this frame takes the view out
	UpdateZoom( true );
}

//-----------------------------------------------------------------------------
// Purpose: The rifle is being drawn or reloaded: no scope
//-----------------------------------------------------------------------------
void CWeaponK98::ItemBusyFrame( void )
{
	BaseClass::ItemBusyFrame();

	UpdateZoom( false );
}

//-----------------------------------------------------------------------------
// Purpose: One switch per press of secondary fire
//-----------------------------------------------------------------------------
void CWeaponK98::CheckZoomToggle( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );

	if ( pPlayer == NULL || !( pPlayer->m_afButtonPressed & IN_ATTACK2 ) )
		return;

	m_bZoomWanted = !m_bZoomWanted;

	m_iSecondaryAttacks++;
	gamestats->Event_WeaponFired( pPlayer, false, GetClassname() );
}

//-----------------------------------------------------------------------------
// Purpose: Put the view in the scope or take it out.
// Input  : bReady - the rifle is up and could fire
//-----------------------------------------------------------------------------
void CWeaponK98::UpdateZoom( bool bReady )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );

	if ( pPlayer == NULL )
		return;

	// The base class's frame can put the rifle away (out of ammo, switch to the next
	// weapon) before this runs, so check that it is still the one in hand
	bool bZoom = m_bZoomWanted && bReady && pPlayer->GetActiveWeapon() == this && !m_bInReload && !m_bLowered;

	if ( gpGlobals->curtime < m_flBoltDoneTime )
	{
		bZoom = false;
	}

	if ( bZoom == m_bInZoom )
		return;

	// This fails while something else has the view zoomed (the suit); it is tried again next frame
	if ( bZoom )
	{
		if ( pPlayer->SetFOV( this, of2_k98_zoom_fov.GetInt(), K98_ZOOM_IN_TIME ) )
		{
			m_bInZoom = true;
			WeaponSound( SPECIAL1 );
		}
	}
	else
	{
		if ( pPlayer->SetFOV( this, 0, K98_ZOOM_OUT_TIME ) )
		{
			m_bInZoom = false;
			WeaponSound( SPECIAL2 );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Out of the scope, and the switch off
//-----------------------------------------------------------------------------
void CWeaponK98::StopZoom( void )
{
	m_bZoomWanted = false;
	m_flBoltDoneTime = 0.0f;

	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );

	if ( m_bInZoom && pPlayer != NULL && pPlayer->SetFOV( this, 0, K98_ZOOM_OUT_TIME ) )
	{
		m_bInZoom = false;
		WeaponSound( SPECIAL2 );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CWeaponK98::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	StopZoom();

	return BaseClass::Holster( pSwitchingTo );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponK98::Drop( const Vector &vecVelocity )
{
	StopZoom();

	BaseClass::Drop( vecVelocity );
}
