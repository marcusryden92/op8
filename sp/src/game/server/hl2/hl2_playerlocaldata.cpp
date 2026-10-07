//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "hl2_playerlocaldata.h"
#include "hl2_player.h"
#include "mathlib/mathlib.h"
#include "entitylist.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_SEND_TABLE_NOBASE( CHL2PlayerLocalData, DT_HL2Local )
	SendPropFloat( SENDINFO(m_flSuitPower), 10, SPROP_UNSIGNED | SPROP_ROUNDUP, 0.0, 100.0 ),
	SendPropInt( SENDINFO(m_bZooming), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO(m_bitsActiveDevices), MAX_SUIT_DEVICES, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO(m_iSquadMemberCount) ),
	SendPropInt( SENDINFO(m_iSquadMedicCount) ),
	SendPropBool( SENDINFO(m_fSquadInFollowMode) ),
	SendPropBool( SENDINFO(m_bWeaponLowered) ),
	SendPropEHandle( SENDINFO(m_hAutoAimTarget) ),
	SendPropVector( SENDINFO(m_vecAutoAimPoint) ),
	SendPropEHandle( SENDINFO(m_hLadder) ),
	SendPropBool( SENDINFO(m_bDisplayReticle) ),
	SendPropBool( SENDINFO(m_bStickyAutoAim) ),
	SendPropBool( SENDINFO(m_bAutoAimTarget) ),
#ifdef HL2_EPISODIC
	SendPropFloat( SENDINFO(m_flFlashBattery) ),
	SendPropBool( SENDINFO(m_bNightVision) ),	// OF2
	SendPropFloat( SENDINFO(m_flStealthLight), 8, SPROP_UNSIGNED, 0.0f, 1.0f ),	// OF2
	SendPropFloat( SENDINFO(m_flStealthNoise), 8, SPROP_UNSIGNED, 0.0f, 1.0f ),
	SendPropVector( SENDINFO(m_vecLocatorOrigin) ),
	// OF2: tether hang
	SendPropBool( SENDINFO(m_bOnTether) ),
	SendPropVector( SENDINFO(m_vecTetherSwingPoint) ),
	SendPropFloat( SENDINFO(m_flTetherSwingLength) ),
	SendPropFloat( SENDINFO(m_flTetherMaxLength) ),
	SendPropFloat( SENDINFO(m_flTetherClimbSpeed) ),
	SendPropFloat( SENDINFO(m_flTetherPump) ),
	SendPropFloat( SENDINFO(m_flTetherMaxAngle) ),
	SendPropBool( SENDINFO(m_bTetherAtWeapon) ),
	SendPropBool( SENDINFO(m_bTetherMantling) ),
	SendPropVector( SENDINFO(m_vecTetherMantleDest) ),
	SendPropVector( SENDINFO(m_vecTetherMantleVia) ),
	SendPropBool( SENDINFO(m_bTetherMantleVia) ),
	SendPropVector( SENDINFO(m_vecTetherNextPoint) ),
#endif
END_SEND_TABLE()

BEGIN_SIMPLE_DATADESC( CHL2PlayerLocalData )
	DEFINE_FIELD( m_flSuitPower, FIELD_FLOAT ),
	DEFINE_FIELD( m_bZooming, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bitsActiveDevices, FIELD_INTEGER ),
	DEFINE_FIELD( m_iSquadMemberCount, FIELD_INTEGER ),
	DEFINE_FIELD( m_iSquadMedicCount, FIELD_INTEGER ),
	DEFINE_FIELD( m_fSquadInFollowMode, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bWeaponLowered, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bDisplayReticle, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bStickyAutoAim, FIELD_BOOLEAN ),
#ifdef HL2_EPISODIC
	DEFINE_FIELD( m_flFlashBattery, FIELD_FLOAT ),
	DEFINE_FIELD( m_bNightVision, FIELD_BOOLEAN ),	// OF2
	DEFINE_FIELD( m_vecLocatorOrigin, FIELD_POSITION_VECTOR ),
	// OF2: tether hang
	DEFINE_FIELD( m_bOnTether, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vecTetherSwingPoint, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_flTetherSwingLength, FIELD_FLOAT ),
	DEFINE_FIELD( m_flTetherMaxLength, FIELD_FLOAT ),
	DEFINE_FIELD( m_flTetherClimbSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( m_flTetherPump, FIELD_FLOAT ),
	DEFINE_FIELD( m_flTetherMaxAngle, FIELD_FLOAT ),
	DEFINE_FIELD( m_bTetherAtWeapon, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bTetherMantling, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vecTetherMantleDest, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecTetherMantleVia, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_bTetherMantleVia, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vecTetherNextPoint, FIELD_POSITION_VECTOR ),
#endif
	// Ladder related stuff
	DEFINE_FIELD( m_hLadder, FIELD_EHANDLE ),
	DEFINE_EMBEDDED( m_LadderMove ),
END_DATADESC()

CHL2PlayerLocalData::CHL2PlayerLocalData()
{
	m_flSuitPower = 0.0;
	m_bZooming = false;
	m_bWeaponLowered = false;
	m_hAutoAimTarget.Set(NULL);
	m_hLadder.Set(NULL);
	m_vecAutoAimPoint.GetForModify().Init();
	m_bDisplayReticle = false;
#ifdef HL2_EPISODIC
	m_flFlashBattery = 0.0f;
	m_bNightVision = false;	// OF2
	m_flStealthLight = 1.0f;
	m_flStealthNoise = 0.0f;

	// OF2: tether hang
	m_bOnTether = false;
	m_vecTetherSwingPoint.GetForModify().Init();
	m_flTetherSwingLength = 0.0f;
	m_flTetherMaxLength = 0.0f;
	m_flTetherClimbSpeed = 0.0f;
	m_flTetherPump = 0.0f;
	m_flTetherMaxAngle = 0.0f;
	m_bTetherAtWeapon = false;
	m_bTetherMantling = false;
	m_vecTetherMantleDest.GetForModify().Init();
	m_vecTetherMantleVia.GetForModify().Init();
	m_bTetherMantleVia = false;
	m_vecTetherNextPoint.GetForModify().Init();
#endif
}

