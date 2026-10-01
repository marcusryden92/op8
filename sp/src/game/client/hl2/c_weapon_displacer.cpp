//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: client side of the Displacer (server\hl2\weapon_displacer.cpp):
//			the weapon's networked destination state, and the arcs, sparks and
//			light where something is taken or put down.
//
//=============================================================================//

#include "cbase.h"
#include "c_weapon__stubs.h"
#include "c_weapon_displacer.h"
#include "c_te_effect_dispatch.h"
#include "fx.h"
#include "iviewrender_beams.h"
#include "beam_shared.h"
#include "dlight.h"
#include "iefx.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

STUB_WEAPON_CLASS_IMPLEMENT( weapon_displacer, C_WeaponDisplacer );

IMPLEMENT_CLIENTCLASS_DT( C_WeaponDisplacer, DT_WeaponDisplacer, CWeaponDisplacer )
	RecvPropBool( RECVINFO( m_bHasDestination ) ),
	RecvPropVector( RECVINFO( m_vecDestPoint ) ),
	RecvPropInt( RECVINFO( m_iSignal ) ),
	RecvPropTime( RECVINFO( m_flSignalLostTime ) ),
	RecvPropBool( RECVINFO( m_bDestIsTarget ) ),
	RecvPropEHandle( RECVINFO( m_hDestTarget ) ),
END_RECV_TABLE()

C_WeaponDisplacer::C_WeaponDisplacer( void )
{
	m_bHasDestination = false;
	m_vecDestPoint.Init();
	m_iSignal = 0;
	m_flSignalLostTime = 0.0f;
	m_bDestIsTarget = false;
}

Vector C_WeaponDisplacer::GetMarkerPosition( void )
{
	if ( m_bDestIsTarget )
	{
		// Out of sight the entity may not be kept up to date here; the server's point is
		C_BaseEntity *pTarget = m_hDestTarget;
		if ( pTarget && !pTarget->IsDormant() )
			return pTarget->WorldSpaceCenter();
	}

	return m_vecDestPoint;
}

//-----------------------------------------------------------------------------
// "DisplacerTeleport": m_vOrigin is the middle of the thing, m_flRadius about
// half its size, and the entity (if any) is the thing itself, just arrived.
//-----------------------------------------------------------------------------
#define DISPLACER_FX_ARCS		9
#define DISPLACER_FX_SPARKS		6

void DisplacerTeleportCallback( const CEffectData &data )
{
	const Vector &vecOrigin = data.m_vOrigin;
	float flRadius = MAX( data.m_flRadius, 16.0f );

	// The teleport light's color (DISPLACER_GLOW_COLOR in weapon_displacer.cpp)
	const Vector vecColor( 150, 245, 215 );

	// With no entity sent the index comes through as the world's; the arcs must not ignore that
	C_BaseEntity *pEntity = data.GetEntity();
	if ( pEntity && pEntity->entindex() == 0 )
	{
		pEntity = NULL;
	}

	// Arcs out to whatever is near
	float flReach = flRadius + 96.0f;
	for ( int i = 0; i < DISPLACER_FX_ARCS; i++ )
	{
		Vector vecDir = RandomVector( -1.0f, 1.0f );
		VectorNormalize( vecDir );

		trace_t tr;
		UTIL_TraceLine( vecOrigin, vecOrigin + vecDir * flReach, MASK_SOLID, pEntity, COLLISION_GROUP_NONE, &tr );

		// Those that find nothing stop short in the air
		Vector vecEnd = tr.endpos;
		if ( tr.fraction == 1.0f )
		{
			vecEnd = vecOrigin + vecDir * flReach * RandomFloat( 0.4f, 0.7f );
		}

		BeamInfo_t beamInfo;
		beamInfo.m_nType = TE_BEAMTESLA;
		beamInfo.m_vecStart = vecOrigin + vecDir * RandomFloat( 0.0f, flRadius * 0.3f );
		beamInfo.m_vecEnd = vecEnd;
		beamInfo.m_pszModelName = "sprites/lgtning.vmt";
		beamInfo.m_flHaloScale = 0.0f;
		beamInfo.m_flLife = RandomFloat( 0.15f, 0.4f );
		beamInfo.m_flWidth = RandomFloat( 3.0f, 7.0f );
		beamInfo.m_flEndWidth = 1.0f;
		beamInfo.m_flFadeLength = 0.0f;
		beamInfo.m_flAmplitude = RandomFloat( 12.0f, 28.0f );
		beamInfo.m_flBrightness = 255.0f;
		beamInfo.m_flSpeed = 150.0f;
		beamInfo.m_nStartFrame = 0;
		beamInfo.m_flFrameRate = 30.0f;
		beamInfo.m_flRed = vecColor.x;
		beamInfo.m_flGreen = vecColor.y;
		beamInfo.m_flBlue = vecColor.z;
		beamInfo.m_nSegments = 18;
		beamInfo.m_bRenderable = true;
		beamInfo.m_nFlags = FBEAM_ONLYNOISEONCE;

		beams->CreateBeamPoints( beamInfo );

		if ( tr.fraction < 1.0f )
		{
			FX_ElectricSpark( tr.endpos, 1, 1, &tr.plane.normal );
		}
	}

	// Sparks all through the space it takes up, as when a Combine ball takes something apart
	for ( int i = 0; i < DISPLACER_FX_SPARKS; i++ )
	{
		Vector vecPos = vecOrigin + RandomVector( -flRadius * 0.5f, flRadius * 0.5f );
		Vector vecDir = RandomVector( -1.0f, 1.0f );
		VectorNormalize( vecDir );

		FX_ElectricSpark( vecPos, 2, 1, &vecDir );
	}

	// And a flash of light on the surroundings
	dlight_t *dl = effects->CL_AllocDlight( 0 );
	dl->origin = vecOrigin;
	dl->color.r = vecColor.x;
	dl->color.g = vecColor.y;
	dl->color.b = vecColor.z;
	dl->color.exponent = 3;
	dl->radius = flRadius * 2.0f + 160.0f;
	dl->die = gpGlobals->curtime + 0.4f;
	dl->decay = dl->radius / 0.4f;
}

DECLARE_CLIENT_EFFECT( "DisplacerTeleport", DisplacerTeleportCallback );
