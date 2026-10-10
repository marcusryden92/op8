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
#include "c_baseviewmodel.h"
#include "igamesystem.h"
#include "engine/ivmodelinfo.h"

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
	RecvPropBool( RECVINFO( m_bCharging ) ),
	RecvPropTime( RECVINFO( m_flSelfTeleportTime ) ),
END_RECV_TABLE()

C_WeaponDisplacer::C_WeaponDisplacer( void )
{
	m_bHasDestination = false;
	m_vecDestPoint.Init();
	m_iSignal = 0;
	m_flSignalLostTime = 0.0f;
	m_bDestIsTarget = false;
	m_bCharging = false;
	m_flSelfTeleportTime = 0.0f;
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
// half its size, and the entity (if any) is the thing itself, which may have
// just arrived there or just left.
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

	// OF2: Arcs and sparks come off the thing's own shape: spots on its hitboxes, taken
	// relative to its middle as this side has it and put about the middle the server sent.
	// That also serves where it left, which it is no longer at.
	Vector vecSpots[DISPLACER_FX_ARCS + DISPLACER_FX_SPARKS];
	int nSpots = 0;

	C_BaseAnimating *pAnimating = pEntity ? pEntity->GetBaseAnimating() : NULL;
	if ( pAnimating )
	{
		MDLCACHE_CRITICAL_SECTION();

		matrix3x4_t *pHitboxToWorld[MAXSTUDIOBONES];
		CStudioHdr *pStudioHdr = pAnimating->GetModelPtr();
		mstudiohitboxset_t *pSet = pStudioHdr ? pStudioHdr->pHitboxSet( pAnimating->GetHitboxSet() ) : NULL;
		if ( pSet && pSet->numhitboxes > 0 && pAnimating->HitboxToWorldTransforms( pHitboxToWorld ) )
		{
			Vector vecShift = vecOrigin - pAnimating->WorldSpaceCenter();
			for ( nSpots = 0; nSpots < ARRAYSIZE( vecSpots ); nSpots++ )
			{
				mstudiobbox_t *pBox = pSet->pHitbox( RandomInt( 0, pSet->numhitboxes - 1 ) );

				Vector vecLocal;
				vecLocal.x = RandomFloat( pBox->bbmin.x, pBox->bbmax.x );
				vecLocal.y = RandomFloat( pBox->bbmin.y, pBox->bbmax.y );
				vecLocal.z = RandomFloat( pBox->bbmin.z, pBox->bbmax.z );

				VectorTransform( vecLocal, *pHitboxToWorld[pBox->bone], vecSpots[nSpots] );
				vecSpots[nSpots] += vecShift;
			}
		}
	}

	// Arcs out to whatever is near
	float flReach = flRadius + 96.0f;
	for ( int i = 0; i < DISPLACER_FX_ARCS; i++ )
	{
		Vector vecDir = RandomVector( -1.0f, 1.0f );
		VectorNormalize( vecDir );

		// Without hitboxes, from near the middle as before
		Vector vecStart = vecOrigin + vecDir * RandomFloat( 0.0f, flRadius * 0.3f );
		if ( i < nSpots )
		{
			// Away from the thing, not back through it
			vecStart = vecSpots[i];
			Vector vecOut = vecStart - vecOrigin;
			VectorNormalize( vecOut );
			vecDir += vecOut * 1.5f;
			VectorNormalize( vecDir );
		}

		trace_t tr;
		UTIL_TraceLine( vecStart, vecStart + vecDir * flReach, MASK_SOLID, pEntity, COLLISION_GROUP_NONE, &tr );

		// Those that find nothing stop short in the air
		Vector vecEnd = tr.endpos;
		if ( tr.fraction == 1.0f )
		{
			vecEnd = vecStart + vecDir * flReach * RandomFloat( 0.4f, 0.7f );
		}

		BeamInfo_t beamInfo;
		beamInfo.m_nType = TE_BEAMTESLA;
		beamInfo.m_vecStart = vecStart;
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
		// OF2: white (the user asked; they had the teleport light's color)
		beamInfo.m_flRed = 255.0f;
		beamInfo.m_flGreen = 255.0f;
		beamInfo.m_flBlue = 255.0f;
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
		if ( DISPLACER_FX_ARCS + i < nSpots )
		{
			vecPos = vecSpots[DISPLACER_FX_ARCS + i];
		}
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

//-----------------------------------------------------------------------------
// OF2: "DisplacerWhiten": the entity turns into a white silhouette over m_flScale
// seconds (the server sends it at the end of that), stays white for a moment
// and gets its own look back over m_flMagnitude. C_BaseAnimating::InternalDrawModel
// asks OF2_DisplacerWhiten how white a model is and draws it over itself that much.
//-----------------------------------------------------------------------------
#define DISPLACER_WHITE_HOLD	0.04f	// all white this long, so it arrives that way

struct DisplacerWhiten_t
{
	EHANDLE	hEntity;
	float	flStart;
	float	flRise;
	float	flFall;
};

static CUtlVector<DisplacerWhiten_t> s_DisplacerWhiten;

void DisplacerWhitenCallback( const CEffectData &data )
{
	C_BaseEntity *pEntity = data.GetEntity();
	if ( pEntity == NULL || pEntity->entindex() == 0 )
		return;

	for ( int i = s_DisplacerWhiten.Count() - 1; i >= 0; i-- )
	{
		if ( s_DisplacerWhiten[i].hEntity == pEntity )
		{
			s_DisplacerWhiten.Remove( i );
		}
	}

	DisplacerWhiten_t entry;
	entry.hEntity = pEntity;
	entry.flStart = gpGlobals->curtime;
	entry.flRise = MAX( data.m_flScale, 0.01f );
	entry.flFall = MAX( data.m_flMagnitude, 0.01f );
	s_DisplacerWhiten.AddToTail( entry );
}

DECLARE_CLIENT_EFFECT( "DisplacerWhiten", DisplacerWhitenCallback );

// 0 = as it is, 1 = all white. What rides on a whitened thing (a soldier's gun) goes with it.
float OF2_DisplacerWhiten( C_BaseEntity *pEntity )
{
	if ( s_DisplacerWhiten.Count() == 0 )
		return 0.0f;

	for ( int i = s_DisplacerWhiten.Count() - 1; i >= 0; i-- )
	{
		const DisplacerWhiten_t &entry = s_DisplacerWhiten[i];
		float flTime = gpGlobals->curtime - entry.flStart;

		// Over, gone, or from before a load
		if ( entry.hEntity.Get() == NULL || flTime < 0.0f || flTime >= entry.flRise + DISPLACER_WHITE_HOLD + entry.flFall )
		{
			s_DisplacerWhiten.Remove( i );
			continue;
		}

		bool bMatch = false;
		C_BaseEntity *pCheck = pEntity;
		for ( int nDepth = 0; pCheck && nDepth < 4; nDepth++ )
		{
			if ( entry.hEntity == pCheck )
			{
				bMatch = true;
				break;
			}
			pCheck = pCheck->GetMoveParent();
		}

		if ( !bMatch )
			continue;

		if ( flTime < entry.flRise )
			return flTime / entry.flRise;

		flTime -= entry.flRise + DISPLACER_WHITE_HOLD;
		if ( flTime <= 0.0f )
			return 1.0f;

		return 1.0f - flTime / entry.flFall;
	}

	return 0.0f;
}

//-----------------------------------------------------------------------------
// "DisplacerZap": a thin arc from the flying portal (the entity) to something
// solid near it. m_vNormal is the way it flies. None if nothing is in reach.
//-----------------------------------------------------------------------------
#define DISPLACER_ZAP_REACH		320.0f
#define DISPLACER_ZAP_TRIES		6
#define DISPLACER_ZAP_ARCS		3	// at most, per event

static ConVar of2_displacer_gun_beam( "of2_displacer_gun_beam", "1000", FCVAR_NONE, "A Displacer portal stays joined to the gun by an arc this far out, in units. 0 for none." );

// Where the gun in the hands has its muzzle, as a place in the world (further down)
static bool OF2_DisplacerGunMuzzle( Vector &vecMuzzle );

void DisplacerZapCallback( const CEffectData &data )
{
	C_BaseEntity *pPortal = data.GetEntity();
	if ( pPortal && pPortal->entindex() == 0 )
	{
		pPortal = NULL;
	}

	Vector vecOrigin = pPortal ? pPortal->GetAbsOrigin() : data.m_vOrigin;

	// One color for every arc, to the gun and to the surroundings alike: the portal's green
	// (the user asked; DISPLACER_PORTAL_RIM in weapon_displacer.cpp, a little brighter)
	const Vector vecArcColor( 60, 255, 130 );

	// The arc back to the gun. Each lasts a moment and the next event draws another,
	// so it flickers but does not let go.
	Vector vecGun;
	if ( pPortal && of2_displacer_gun_beam.GetFloat() > 0.0f && OF2_DisplacerGunMuzzle( vecGun )
		&& vecGun.DistTo( vecOrigin ) <= of2_displacer_gun_beam.GetFloat() )
	{
		BeamInfo_t beamInfo;
		beamInfo.m_nType = TE_BEAMPOINTS;
		beamInfo.m_pStartEnt = pPortal;
		beamInfo.m_nStartAttachment = 0;
		beamInfo.m_pEndEnt = NULL;
		beamInfo.m_nEndAttachment = 0;
		beamInfo.m_vecStart = vecOrigin;
		beamInfo.m_vecEnd = vecGun;
		beamInfo.m_pszModelName = "sprites/lgtning.vmt";
		beamInfo.m_flHaloScale = 0.0f;
		beamInfo.m_flLife = 0.1f;
		beamInfo.m_flWidth = RandomFloat( 4.0f, 6.0f );
		beamInfo.m_flEndWidth = 1.5f;
		beamInfo.m_flFadeLength = 0.0f;
		beamInfo.m_flAmplitude = RandomFloat( 5.0f, 10.0f );
		beamInfo.m_flBrightness = 255.0f;
		beamInfo.m_flSpeed = 150.0f;
		beamInfo.m_nStartFrame = 0;
		beamInfo.m_flFrameRate = 30.0f;
		beamInfo.m_flRed = vecArcColor.x;
		beamInfo.m_flGreen = vecArcColor.y;
		beamInfo.m_flBlue = vecArcColor.z;
		beamInfo.m_nSegments = 16;
		beamInfo.m_bRenderable = true;
		beamInfo.m_nFlags = FBEAM_ONLYNOISEONCE;

		beams->CreateBeamEntPoint( beamInfo );
	}

	int nArcs = 0;
	for ( int i = 0; i < DISPLACER_ZAP_TRIES && nArcs < DISPLACER_ZAP_ARCS; i++ )
	{
		// Leaning ahead: the portal has moved on by the time the arc is seen
		Vector vecDir = RandomVector( -1.0f, 1.0f );
		VectorNormalize( vecDir );
		vecDir += data.m_vNormal * 0.5f;
		VectorNormalize( vecDir );

		trace_t tr;
		UTIL_TraceLine( vecOrigin, vecOrigin + vecDir * DISPLACER_ZAP_REACH, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction == 1.0f || tr.startsolid )
			continue;

		BeamInfo_t beamInfo;
		beamInfo.m_nType = TE_BEAMPOINTS;
		beamInfo.m_pStartEnt = pPortal;
		beamInfo.m_nStartAttachment = 0;
		// BeamInfo_t leaves its entities unset, and CreateBeamEntPoint reads both
		beamInfo.m_pEndEnt = NULL;
		beamInfo.m_nEndAttachment = 0;
		beamInfo.m_vecStart = vecOrigin;
		beamInfo.m_vecEnd = tr.endpos;
		beamInfo.m_pszModelName = "sprites/lgtning.vmt";
		beamInfo.m_flHaloScale = 0.0f;
		beamInfo.m_flLife = RandomFloat( 0.08f, 0.18f );
		beamInfo.m_flWidth = RandomFloat( 2.5f, 5.0f );
		beamInfo.m_flEndWidth = 1.0f;
		beamInfo.m_flFadeLength = 0.0f;
		beamInfo.m_flAmplitude = RandomFloat( 6.0f, 14.0f );
		beamInfo.m_flBrightness = 255.0f;
		beamInfo.m_flSpeed = 150.0f;
		beamInfo.m_nStartFrame = 0;
		beamInfo.m_flFrameRate = 30.0f;
		beamInfo.m_flRed = vecArcColor.x;
		beamInfo.m_flGreen = vecArcColor.y;
		beamInfo.m_flBlue = vecArcColor.z;
		beamInfo.m_nSegments = 10;
		beamInfo.m_bRenderable = true;
		beamInfo.m_nFlags = FBEAM_ONLYNOISEONCE;

		if ( pPortal )
		{
			beams->CreateBeamEntPoint( beamInfo );
		}
		else
		{
			beams->CreateBeamPoints( beamInfo );
		}

		if ( RandomInt( 0, 2 ) == 0 )
		{
			FX_ElectricSpark( tr.endpos, 1, 1, &tr.plane.normal );
		}
		nArcs++;
	}
}

DECLARE_CLIENT_EFFECT( "DisplacerZap", DisplacerZapCallback );

//-----------------------------------------------------------------------------
// The gun in the hands. The viewmodel (models/weapons/v_disp_hands.mdl) is a copy
// of the gravity gun's with the material folder renamed, so the gun's mesh can be
// hidden by a never-drawn material (materials\models\weapons\v_disp_hands) and
// only its arms are left. Here the Displacer without arms
// (models/weapons/v_displacer_gun.mdl, sp\modelsrc\displacer\make_gun.pl) is drawn
// in the viewmodel pass on the bone the gravity gun hangs on, so every animation
// of the arms carries it. The same way as the manhack (of2_viewmodel_manhack.cpp).
//
// Placement is by eye: of2_vm_displacer_scale / _offset / _angles. It is fixed to
// the arms, so whatever moves or sizes the viewmodel (a script's "viewmodel_offset",
// the of2_viewmodel_* convars) takes both along.
//-----------------------------------------------------------------------------
#define VM_DISPLACER_MODEL		"models/weapons/v_displacer_gun.mdl"	// precached by the weapon on the server
#define VM_DISPLACER_VIEWMODEL	"models/weapons/v_disp_hands.mdl"

// Not archived; they reset on restart.
static ConVar of2_vm_displacer( "of2_vm_displacer", "1", FCVAR_NONE, "Draw the Displacer in the hands of its viewmodel. 0 leaves them empty." );
static ConVar of2_vm_displacer_bone( "of2_vm_displacer_bone", "Base", FCVAR_NONE, "The viewmodel bone the Displacer is fixed to. Base is the gravity gun's body; ValveBiped.Bip01_R_Hand the right hand." );
static ConVar of2_vm_displacer_scale( "of2_vm_displacer_scale", "1", FCVAR_NONE, "Size of the Displacer in the hands." );
// Where the user lined it up with the arms, 2026-10-08
static ConVar of2_vm_displacer_offset( "of2_vm_displacer_offset", "3.2 -3.5 -9", FCVAR_NONE, "Where the Displacer's origin (the back of its body) is, in the bone's space. On Base, Z is along the barrel." );
static ConVar of2_vm_displacer_angles( "of2_vm_displacer_angles", "20 -15 -90", FCVAR_NONE, "How the Displacer is turned in the bone's space: pitch yaw roll." );
// The same again along the viewmodel's axes, which is easier to steer by eye than the bone's.
// On top of the two above; of2_vm_displacer_print gives the two that say the same.
static ConVar of2_vm_displacer_forward( "of2_vm_displacer_forward", "0", FCVAR_NONE, "Moves the Displacer alone away from the camera (units; negative is closer). The arms stay." );
static ConVar of2_vm_displacer_right( "of2_vm_displacer_right", "0", FCVAR_NONE, "Moves the Displacer alone right (units; negative is left)." );
static ConVar of2_vm_displacer_up( "of2_vm_displacer_up", "0", FCVAR_NONE, "Moves the Displacer alone up (units; negative is down)." );
static ConVar of2_vm_displacer_pitch( "of2_vm_displacer_pitch", "0", FCVAR_NONE, "Tilts the Displacer alone down, about its own origin (degrees; negative is up)." );
static ConVar of2_vm_displacer_yaw( "of2_vm_displacer_yaw", "0", FCVAR_NONE, "Turns the Displacer alone left (degrees; negative is right)." );
static ConVar of2_vm_displacer_roll( "of2_vm_displacer_roll", "0", FCVAR_NONE, "Rolls the Displacer alone clockwise (degrees; negative is counterclockwise)." );
static ConVar of2_vm_displacer_sequence( "of2_vm_displacer_sequence", "", FCVAR_NONE, "Hold the Displacer in this sequence of its own (idle, spinup, spin, fire). Empty: follow the viewmodel." );

// The placement last drawn, in the bone's space, all convars taken together
static Vector s_vecDisplacerPlaced( 0, 0, 0 );
static QAngle s_angDisplacerPlaced( 0, 0, 0 );

CON_COMMAND( of2_vm_displacer_print, "Prints where the Displacer is in the hands as one offset and one set of angles: what of2_vm_displacer_offset / _angles would be with the forward / right / up / pitch / yaw / roll ones back at 0." )
{
	Msg( "of2_vm_displacer_offset \"%.2f %.2f %.2f\"\n", s_vecDisplacerPlaced.x, s_vecDisplacerPlaced.y, s_vecDisplacerPlaced.z );
	Msg( "of2_vm_displacer_angles \"%.1f %.1f %.1f\"\n", s_angDisplacerPlaced.x, s_angDisplacerPlaced.y, s_angDisplacerPlaced.z );
	Msg( "of2_vm_displacer_scale %g\n", of2_vm_displacer_scale.GetFloat() );
}

class C_OF2ViewModelDisplacer : public C_BaseAnimating
{
	DECLARE_CLASS( C_OF2ViewModelDisplacer, C_BaseAnimating );

public:
	C_OF2ViewModelDisplacer( void )
	{
		m_flSequenceStart = 0.0f;
		m_flLastViewModelCycle = 0.0f;
		m_flLastPlaced = -1.0f;
		m_nLastViewModelSequence = -1;
		m_flChargeStart = 0.0f;
		m_bWasCharging = false;
	}

	virtual int				DrawModel( int flags );

	// Drawn with the viewmodels, whatever the materials say
	virtual RenderGroup_t	GetRenderGroup( void ) { return RENDER_GROUP_VIEW_MODEL_OPAQUE; }
	virtual ShadowType_t	ShadowCastType( void ) { return SHADOWS_NONE; }
	virtual bool			ShouldReceiveProjectedTextures( int flags ) { return false; }

	// When it was last drawn in the hands; before that it is nowhere in particular
	float					m_flLastPlaced;

private:
	bool					PlaceInHands( void );
	void					FollowViewModel( C_BaseViewModel *pViewModel );

	float					m_flSequenceStart;
	float					m_flLastViewModelCycle;
	int						m_nLastViewModelSequence;
	float					m_flChargeStart;
	bool					m_bWasCharging;
};

//-----------------------------------------------------------------------------
// Purpose: the gun's own moving parts: its fire sequence when the arms fire, its
//			spin-up when they mark a destination, idle otherwise
//-----------------------------------------------------------------------------
void C_OF2ViewModelDisplacer::FollowViewModel( C_BaseViewModel *pViewModel )
{
	const char *pszSequence = of2_vm_displacer_sequence.GetString();
	bool bHeld = ( pszSequence[0] != 0 );

	// The arms started something, or started the same thing again
	int nViewModelSequence = pViewModel->GetSequence();
	float flViewModelCycle = pViewModel->GetCycle();
	if ( nViewModelSequence != m_nLastViewModelSequence || flViewModelCycle < m_flLastViewModelCycle )
	{
		m_flSequenceStart = gpGlobals->curtime;
	}
	m_nLastViewModelSequence = nViewModelSequence;
	m_flLastViewModelCycle = flViewModelCycle;

	// Charging to fire: the spin-up, then spinning until the portal leaves
	C_WeaponDisplacer *pWeapon = dynamic_cast<C_WeaponDisplacer *>( pViewModel->GetOwningWeapon() );
	bool bCharging = ( pWeapon != NULL && pWeapon->m_bCharging );
	if ( bCharging && !m_bWasCharging )
	{
		m_flChargeStart = gpGlobals->curtime;
	}
	m_bWasCharging = bCharging;

	if ( bCharging && !bHeld )
	{
		int iSpinUp = MAX( LookupSequence( "spinup" ), 0 );
		int iSpin = MAX( LookupSequence( "spin" ), 0 );
		float flSpinUp = SequenceDuration( iSpinUp );
		float flSpin = SequenceDuration( iSpin );
		float flTime = gpGlobals->curtime - m_flChargeStart;

		int iChargeSequence = iSpinUp;
		float flChargeCycle = ( flSpinUp > 0.0f ) ? flTime / flSpinUp : 1.0f;
		if ( flChargeCycle >= 1.0f )
		{
			iChargeSequence = iSpin;
			flChargeCycle = ( flSpin > 0.0f ) ? ( flTime - flSpinUp ) / flSpin : 0.0f;
			flChargeCycle -= floor( flChargeCycle );
		}

		if ( GetSequence() != iChargeSequence )
		{
			SetSequence( iChargeSequence );
		}
		SetCycle( flChargeCycle );
		return;
	}

	// The player sent themselves: the spin-up once, with nothing from the arms
	if ( !bHeld && pWeapon != NULL && pWeapon->m_flSelfTeleportTime > 0.0f )
	{
		int iSpinUp = MAX( LookupSequence( "spinup" ), 0 );
		float flSpinUp = SequenceDuration( iSpinUp );
		float flTime = gpGlobals->curtime - pWeapon->m_flSelfTeleportTime;
		if ( flTime >= 0.0f && flTime < flSpinUp )
		{
			if ( GetSequence() != iSpinUp )
			{
				SetSequence( iSpinUp );
			}
			SetCycle( flTime / flSpinUp );
			return;
		}
	}

	if ( !bHeld )
	{
		switch ( pViewModel->GetSequenceActivity( nViewModelSequence ) )
		{
		case ACT_VM_PRIMARYATTACK:
			pszSequence = "fire";
			break;

		case ACT_VM_SECONDARYATTACK:
			pszSequence = "spinup";
			break;

		default:
			pszSequence = "idle";
			break;
		}
	}

	int iSequence = LookupSequence( pszSequence );
	if ( iSequence < 0 )
	{
		iSequence = 0;
	}

	float flDuration = SequenceDuration( iSequence );
	float flCycle = ( flDuration > 0.0f ) ? ( gpGlobals->curtime - m_flSequenceStart ) / flDuration : 0.0f;
	if ( bHeld || IsSequenceLooping( iSequence ) )
	{
		flCycle -= floor( flCycle );
	}
	else if ( flCycle >= 1.0f )
	{
		// Done before the arms are: back to idle, from wherever that is by now
		iSequence = MAX( LookupSequence( "idle" ), 0 );
		flDuration = SequenceDuration( iSequence );
		flCycle = ( flDuration > 0.0f ) ? gpGlobals->curtime / flDuration : 0.0f;
		flCycle -= floor( flCycle );
	}

	if ( GetSequence() != iSequence )
	{
		SetSequence( iSequence );
	}
	SetCycle( flCycle );
}

//-----------------------------------------------------------------------------
// Purpose: the Displacer viewmodel is up: go to its bone
// Output : false if there is nothing to draw
//-----------------------------------------------------------------------------
bool C_OF2ViewModelDisplacer::PlaceInHands( void )
{
	if ( !of2_vm_displacer.GetBool() )
		return false;

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer == NULL )
		return false;

	C_BaseViewModel *pViewModel = pPlayer->GetViewModel( 0 );
	if ( pViewModel == NULL || pViewModel->GetModel() == NULL || pViewModel->IsEffectActive( EF_NODRAW ) )
		return false;

	if ( V_stricmp( modelinfo->GetModelName( pViewModel->GetModel() ), VM_DISPLACER_VIEWMODEL ) != 0 )
		return false;

	int iBone = pViewModel->LookupBone( of2_vm_displacer_bone.GetString() );
	if ( iBone < 0 )
		return false;

	// The bones it is being drawn with. Not GetBoneTransform: that reads the hitbox bone cache,
	// which is kept for a tenth of a second and so can be from before the viewmodel was put in
	// place for this frame (the gun stayed behind when of2_viewmodel_* moved the arms).
	if ( !pViewModel->SetupBones( NULL, -1, BONE_USED_BY_ANYTHING, gpGlobals->curtime ) )
		return false;

	matrix3x4_t matBone;
	pViewModel->GetCachedBoneMatrix( iBone, matBone );

	Vector vecOffset;
	QAngle angOffset;
	UTIL_StringToVector( vecOffset.Base(), of2_vm_displacer_offset.GetString() );
	UTIL_StringToVector( angOffset.Base(), of2_vm_displacer_angles.GetString() );

	matrix3x4_t matLocal, matWorld;
	AngleMatrix( angOffset, vecOffset, matLocal );
	ConcatTransforms( matBone, matLocal, matWorld );

	Vector vecOrigin;
	QAngle angles;
	MatrixAngles( matWorld, angles, vecOrigin );

	// The gun alone, moved and turned along the viewmodel's axes as the of2_viewmodel_* convars
	// do for arms and gun together. It turns about its own origin, so it stays where it is.
	Vector vecNudge( of2_vm_displacer_forward.GetFloat(), of2_vm_displacer_right.GetFloat(), of2_vm_displacer_up.GetFloat() );
	QAngle angNudge( of2_vm_displacer_pitch.GetFloat(), of2_vm_displacer_yaw.GetFloat(), of2_vm_displacer_roll.GetFloat() );
	if ( vecNudge != vec3_origin || angNudge != vec3_angle )
	{
		Vector vecForward, vecRight, vecUp;
		AngleVectors( pViewModel->GetAbsAngles(), &vecForward, &vecRight, &vecUp );
		vecOrigin += ( vecForward * vecNudge.x + vecRight * vecNudge.y + vecUp * vecNudge.z ) * pViewModel->GetModelScale();

		matrix3x4_t matView, matViewInverse, matNudge, matViewNudge, matTurn, matGun, matTurned;
		AngleMatrix( pViewModel->GetAbsAngles(), matView );
		MatrixInvert( matView, matViewInverse );
		AngleMatrix( angNudge, matNudge );
		ConcatTransforms( matView, matNudge, matViewNudge );
		ConcatTransforms( matViewNudge, matViewInverse, matTurn );
		AngleMatrix( angles, matGun );
		ConcatTransforms( matTurn, matGun, matTurned );
		MatrixAngles( matTurned, angles );
	}

	// Where that leaves it in the bone's space, for of2_vm_displacer_print. The bone's axes are
	// as long as the viewmodel's scale.
	{
		Vector vecBoneOrigin, vecAxis[3];
		MatrixGetColumn( matBone, 3, vecBoneOrigin );
		float flBoneScale = 1.0f;
		for ( int i = 0; i < 3; i++ )
		{
			MatrixGetColumn( matBone, i, vecAxis[i] );
			flBoneScale = VectorNormalize( vecAxis[i] );
		}

		matrix3x4_t matBoneTurn, matBoneTurnInverse, matPlaced, matInBone;
		SetIdentityMatrix( matBoneTurn );
		for ( int i = 0; i < 3; i++ )
		{
			MatrixSetColumn( vecAxis[i], i, matBoneTurn );
		}
		MatrixInvert( matBoneTurn, matBoneTurnInverse );
		AngleMatrix( angles, matPlaced );
		ConcatTransforms( matBoneTurnInverse, matPlaced, matInBone );
		MatrixAngles( matInBone, s_angDisplacerPlaced );

		Vector vecDelta = vecOrigin - vecBoneOrigin;
		if ( flBoneScale > 0.0f )
		{
			s_vecDisplacerPlaced.Init( DotProduct( vecDelta, vecAxis[0] ) / flBoneScale, DotProduct( vecDelta, vecAxis[1] ) / flBoneScale, DotProduct( vecDelta, vecAxis[2] ) / flBoneScale );
		}
	}

	SetAbsOrigin( vecOrigin );
	SetAbsAngles( angles );

	// The viewmodel's own size ("viewmodel_scale", of2_viewmodel_scale) is in its bones, and so
	// in the place worked out above; the gun has to be sized to match
	float flScale = clamp( of2_vm_displacer_scale.GetFloat(), 0.01f, 4.0f ) * pViewModel->GetModelScale();
	if ( GetModelScale() != flScale )
	{
		SetModelScale( flScale );
	}

	FollowViewModel( pViewModel );

	// It moved after anything this frame could have set its bones up
	InvalidateBoneCache();
	m_flLastPlaced = gpGlobals->curtime;
	return true;
}

int C_OF2ViewModelDisplacer::DrawModel( int flags )
{
	if ( !PlaceInHands() )
		return 0;

	return BaseClass::DrawModel( flags );
}

//-----------------------------------------------------------------------------
// Makes the gun the first time its viewmodel is up in a level
//-----------------------------------------------------------------------------
class COF2ViewModelDisplacerSystem : public CAutoGameSystemPerFrame
{
public:
	COF2ViewModelDisplacerSystem( void ) : CAutoGameSystemPerFrame( "COF2ViewModelDisplacerSystem" )
	{
		m_flNextTry = 0.0f;
	}

	virtual void LevelInitPostEntity( void )
	{
		m_flNextTry = 0.0f;
	}

	virtual void LevelShutdownPreEntity( void )
	{
		if ( m_hGun.Get() )
		{
			m_hGun->Release();
		}
		m_hGun = NULL;
	}

	virtual void Update( float frametime )
	{
		if ( m_hGun.Get() || gpGlobals->curtime < m_flNextTry )
			return;

		C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
		C_BaseViewModel *pViewModel = pPlayer ? pPlayer->GetViewModel( 0 ) : NULL;
		if ( pViewModel == NULL || pViewModel->GetModel() == NULL )
			return;

		if ( V_stricmp( modelinfo->GetModelName( pViewModel->GetModel() ), VM_DISPLACER_VIEWMODEL ) != 0 )
			return;

		// The model is only there once the server has precached it; ask again in a while if not
		m_flNextTry = gpGlobals->curtime + 1.0f;

		if ( modelinfo->GetModelIndex( VM_DISPLACER_MODEL ) == -1 )
			return;

		C_OF2ViewModelDisplacer *pGun = new C_OF2ViewModelDisplacer;
		if ( !pGun->InitializeAsClientEntity( VM_DISPLACER_MODEL, RENDER_GROUP_VIEW_MODEL_OPAQUE ) )
		{
			pGun->Release();
			return;
		}

		m_hGun = pGun;
	}

	C_OF2ViewModelDisplacer *GetGun( void ) { return m_hGun.Get(); }

private:
	CHandle<C_OF2ViewModelDisplacer>	m_hGun;
	float								m_flNextTry;
};

static COF2ViewModelDisplacerSystem g_OF2ViewModelDisplacerSystem;

//-----------------------------------------------------------------------------
// Purpose: where the gun in the hands has its muzzle. Viewmodels are drawn with a
//			narrower field of view than the world, so the place is moved to where
//			it is seen on screen.
// Output : false while the gun is not up
//-----------------------------------------------------------------------------
void FormatViewModelAttachment( Vector &vOrigin, bool bInverse );	// c_baseviewmodel.cpp

static bool OF2_DisplacerGunMuzzle( Vector &vecMuzzle )
{
	C_OF2ViewModelDisplacer *pGun = g_OF2ViewModelDisplacerSystem.GetGun();
	if ( pGun == NULL || pGun->m_flLastPlaced < 0.0f || gpGlobals->curtime - pGun->m_flLastPlaced > 0.2f )
		return false;

	int iBone = pGun->LookupBone( "blast_emitter" );
	if ( iBone < 0 || !pGun->SetupBones( NULL, -1, BONE_USED_BY_ANYTHING, gpGlobals->curtime ) )
		return false;

	matrix3x4_t matBone;
	pGun->GetCachedBoneMatrix( iBone, matBone );
	MatrixGetColumn( matBone, 3, vecMuzzle );

	FormatViewModelAttachment( vecMuzzle, false );
	return true;
}
