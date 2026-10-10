//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: client side of the Displacer (server\hl2\weapon_displacer.cpp).
//			Only carries the destination state for the HUD.
//
//=============================================================================//

#ifndef C_WEAPON_DISPLACER_H
#define C_WEAPON_DISPLACER_H
#ifdef _WIN32
#pragma once
#endif

#include "c_basehlcombatweapon.h"

class C_WeaponDisplacer : public C_BaseHLCombatWeapon
{
	DECLARE_CLASS( C_WeaponDisplacer, C_BaseHLCombatWeapon );
public:
	C_WeaponDisplacer( void );

	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();

	bool	m_bHasDestination;
	Vector	m_vecDestPoint;
	int		m_iSignal;				// 100 at the destination, 0 at the edge of the range
	float	m_flSignalLostTime;		// when the signal was last lost, 0 if it hasn't been

	// A marked enemy: the destination follows it. m_vecDestPoint is its middle as of the
	// server's last update; the entity itself, when the client has it, is smoother.
	bool	m_bDestIsTarget;
	EHANDLE	m_hDestTarget;

	bool	m_bCharging;			// charging to fire; the gun in the hands spins up
	float	m_flSelfTeleportTime;	// when the player last sent themselves, 0 if never

	// Where the HUD should mark the destination
	Vector	GetMarkerPosition( void );

private:
	C_WeaponDisplacer( const C_WeaponDisplacer & );
};

#endif // C_WEAPON_DISPLACER_H
