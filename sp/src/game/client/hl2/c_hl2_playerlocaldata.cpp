//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "c_hl2_playerlocaldata.h"
#include "dt_recv.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_RECV_TABLE_NOBASE( C_HL2PlayerLocalData, DT_HL2Local )
	RecvPropFloat( RECVINFO(m_flSuitPower) ),
	RecvPropInt( RECVINFO(m_bZooming) ),
	RecvPropInt( RECVINFO(m_bitsActiveDevices) ),
	RecvPropInt( RECVINFO(m_iSquadMemberCount) ),
	RecvPropInt( RECVINFO(m_iSquadMedicCount) ),
	RecvPropBool( RECVINFO(m_fSquadInFollowMode) ),
	RecvPropBool( RECVINFO(m_bWeaponLowered) ),
	RecvPropEHandle( RECVINFO(m_hAutoAimTarget) ),
	RecvPropVector( RECVINFO(m_vecAutoAimPoint) ),
	RecvPropEHandle( RECVINFO(m_hLadder) ),
	RecvPropBool( RECVINFO(m_bDisplayReticle) ),
	RecvPropBool( RECVINFO(m_bStickyAutoAim) ),
	RecvPropBool( RECVINFO(m_bAutoAimTarget) ),
#ifdef HL2_EPISODIC
	RecvPropFloat( RECVINFO(m_flFlashBattery) ),
	RecvPropBool( RECVINFO(m_bNightVision) ),	// OF2
	RecvPropVector( RECVINFO(m_vecLocatorOrigin) ),
	// OF2: tether hang
	RecvPropBool( RECVINFO(m_bOnTether) ),
	RecvPropVector( RECVINFO(m_vecTetherSwingPoint) ),
	RecvPropFloat( RECVINFO(m_flTetherSwingLength) ),
	RecvPropFloat( RECVINFO(m_flTetherMaxLength) ),
	RecvPropFloat( RECVINFO(m_flTetherClimbSpeed) ),
	RecvPropFloat( RECVINFO(m_flTetherPump) ),
	RecvPropFloat( RECVINFO(m_flTetherMaxAngle) ),
#endif
END_RECV_TABLE()

BEGIN_PREDICTION_DATA_NO_BASE( C_HL2PlayerLocalData )
	DEFINE_PRED_FIELD( m_hLadder, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE ),
#ifdef HL2_EPISODIC
	// OF2: tether hang. The movement code lets go and changes the length.
	DEFINE_PRED_FIELD( m_bOnTether, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_flTetherSwingLength, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_vecTetherSwingPoint, FIELD_VECTOR, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_flTetherMaxLength, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_flTetherClimbSpeed, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_flTetherPump, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_flTetherMaxAngle, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
#endif
END_PREDICTION_DATA()

C_HL2PlayerLocalData::C_HL2PlayerLocalData()
{
	m_flSuitPower = 0.0;
	m_bZooming = false;
	m_iSquadMemberCount = 0;
	m_iSquadMedicCount = 0;
	m_fSquadInFollowMode = false;
	m_bWeaponLowered = false;
	m_hLadder = NULL;
#ifdef HL2_EPISODIC
	m_flFlashBattery = 0.0f;
	m_bNightVision = false;	// OF2
	m_vecLocatorOrigin = vec3_origin;

	// OF2: tether hang
	m_bOnTether = false;
	m_vecTetherSwingPoint = vec3_origin;
	m_flTetherSwingLength = 0.0f;
	m_flTetherMaxLength = 0.0f;
	m_flTetherClimbSpeed = 0.0f;
	m_flTetherPump = 0.0f;
	m_flTetherMaxAngle = 0.0f;
#endif
}

