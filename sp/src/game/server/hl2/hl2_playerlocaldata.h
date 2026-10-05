//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef HL2_PLAYERLOCALDATA_H
#define HL2_PLAYERLOCALDATA_H
#ifdef _WIN32
#pragma once
#endif

#include "networkvar.h"

#include "hl_movedata.h"

//-----------------------------------------------------------------------------
// Purpose: Player specific data for HL2 ( sent only to local player, too )
//-----------------------------------------------------------------------------
class CHL2PlayerLocalData
{
public:
	// Save/restore
	DECLARE_SIMPLE_DATADESC();
	DECLARE_CLASS_NOBASE( CHL2PlayerLocalData );
	DECLARE_EMBEDDED_NETWORKVAR();

	CHL2PlayerLocalData();

	CNetworkVar( float, m_flSuitPower );
	CNetworkVar( bool,	m_bZooming );
	CNetworkVar( int,	m_bitsActiveDevices );
	CNetworkVar( int,	m_iSquadMemberCount );
	CNetworkVar( int,	m_iSquadMedicCount );
	CNetworkVar( bool,	m_fSquadInFollowMode );
	CNetworkVar( bool,	m_bWeaponLowered );
	CNetworkVar( EHANDLE, m_hAutoAimTarget );
	CNetworkVar( Vector, m_vecAutoAimPoint );
	CNetworkVar( bool,	m_bDisplayReticle );
	CNetworkVar( bool,	m_bStickyAutoAim );
	CNetworkVar( bool,	m_bAutoAimTarget );
#ifdef HL2_EPISODIC
	CNetworkVar( float, m_flFlashBattery );
	CNetworkVar( bool,	m_bNightVision );	// OF2: night vision replaces the flashlight
	CNetworkVar( Vector, m_vecLocatorOrigin );

	// OF2: tether hang (climb rope, Barnacle). Whatever the player hangs from fills
	// these in; the movement code swings the player around the point, and changes
	// the length when climbing.
	CNetworkVar( bool,	m_bOnTether );
	CNetworkVar( Vector, m_vecTetherSwingPoint );
	CNetworkVar( float, m_flTetherSwingLength );
	CNetworkVar( float, m_flTetherMaxLength );		// as far as climbing down goes
	CNetworkVar( float, m_flTetherClimbSpeed );		// 0: no climbing
	CNetworkVar( float, m_flTetherPump );			// 0: no pumping
	CNetworkVar( float, m_flTetherMaxAngle );		// from straight down; 0: no limit
	CNetworkVar( bool,	m_bTetherAtWeapon );		// held where the weapon is on screen, not in the free hand
	CNetworkVar( bool,	m_bTetherMantling );		// being moved up onto the edge next to the swing point
	CNetworkVar( Vector, m_vecTetherMantleDest );	// where they will stand
	CNetworkVar( Vector, m_vecTetherMantleVia );	// ...by way of here
	CNetworkVar( bool,	m_bTetherMantleVia );		// not there yet
	CNetworkVar( Vector, m_vecTetherNextPoint );	// where the tether runs on to past the swing point
#endif

	// Ladder related data
	CNetworkVar( EHANDLE, m_hLadder );
	LadderMove_t			m_LadderMove;
};

EXTERN_SEND_TABLE(DT_HL2Local);


#endif // HL2_PLAYERLOCALDATA_H
