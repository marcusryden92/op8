//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "basehlcombatweapon_shared.h"

#ifndef C_BASEHLCOMBATWEAPON_H
#define C_BASEHLCOMBATWEAPON_H
#ifdef _WIN32
#pragma once
#endif

class C_HLMachineGun : public C_BaseHLCombatWeapon
{
public:
	DECLARE_CLASS( C_HLMachineGun, C_BaseHLCombatWeapon );
	DECLARE_CLIENTCLASS();
};

class C_HLSelectFireMachineGun : public C_HLMachineGun
{
public:
	DECLARE_CLASS( C_HLSelectFireMachineGun, C_HLMachineGun );
	DECLARE_CLIENTCLASS();

	// OF2: for the HUD's fire mode indicator (CHudFireMode in hud_ammo.cpp)
	C_HLSelectFireMachineGun() { m_iFireMode = FIREMODE_FULLAUTO; m_bFireSelector = false; }

	int		m_iFireMode;		// FIREMODE_
	bool	m_bFireSelector;	// the player can switch modes on this weapon
};

class C_BaseHLBludgeonWeapon : public C_BaseHLCombatWeapon
{
public:
	DECLARE_CLASS( C_BaseHLBludgeonWeapon, C_BaseHLCombatWeapon );
	DECLARE_CLIENTCLASS();
};

#endif // C_BASEHLCOMBATWEAPON_H
