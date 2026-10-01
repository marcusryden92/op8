//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#ifndef WEAPON_ALYXGUN_H
#define WEAPON_ALYXGUN_H

#include "basehlcombatweapon.h"

#if defined( _WIN32 )
#pragma once
#endif

#ifdef MAPBASE
extern acttable_t *GetPistolActtable();
extern int GetPistolActtableCount();
#endif

class CWeaponAlyxGun : public CHLSelectFireMachineGun
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS( CWeaponAlyxGun, CHLSelectFireMachineGun );

	CWeaponAlyxGun();
	~CWeaponAlyxGun();

	DECLARE_SERVERCLASS();
	
	void	Precache( void );

	virtual int		GetMinBurst( void ) { return 4; }
	virtual int		GetMaxBurst( void ) { return 7; }
	virtual float	GetMinRestTime( void );
	virtual float	GetMaxRestTime( void );

	virtual void Equip( CBaseCombatCharacter *pOwner );

	float	GetFireRate( void );	// OF2: was 0.1f here; the player's rate is a convar
	int		CapabilitiesGet( void ) { return bits_CAP_WEAPON_RANGE_ATTACK1; }
	int		WeaponRangeAttack1Condition( float flDot, float flDist );
	int		WeaponRangeAttack2Condition( float flDot, float flDist );

	virtual const Vector& GetBulletSpread( void );

	// OF2: the player's side of it
	void	AddViewKick( void );
	Activity	GetPrimaryAttackActivity( void );
	void	SecondaryAttack( void );

	// OF2: past CHLSelectFireMachineGun, which plays one "burst" sound for a whole burst.
	// Every round makes the same sound here, whatever the fire mode.
	void	WeaponSound( WeaponSound_t shoot_type, float soundtime = 0.0f ) { CHLMachineGun::WeaponSound( shoot_type, soundtime ); }

	void FireNPCPrimaryAttack( CBaseCombatCharacter *pOperator, bool bUseWeaponAngles );

	void Operator_ForceNPCFire( CBaseCombatCharacter  *pOperator, bool bSecondary );
	void Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator );

	// OF2: the SetPickupTouch() override that made it impossible to pick up is gone;
	// it is a player weapon now (scripts\weapon_alyxgun.txt)

#ifdef MAPBASE
	virtual acttable_t		*GetBackupActivityList() { return GetPistolActtable(); }
	virtual int				GetBackupActivityListCount() { return GetPistolActtableCount(); }
#endif

	float m_flTooCloseTimer;

	DECLARE_ACTTABLE();

};

#endif // WEAPON_ALYXGUN_H
