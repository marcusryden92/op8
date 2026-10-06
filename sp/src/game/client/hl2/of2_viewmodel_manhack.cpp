//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: the folded manhack in the hand of weapon_manhack
//			(server\hl2\weapon_of2_manhack.cpp).
//
//			Its viewmodel (models/weapons/v_manhack.mdl) is a copy of the frag
//			grenade's with the material folder renamed, so the grenade meshes
//			can be hidden by never-drawn materials
//			(materials\models\weapons\v_manhack\*.vmt). Here Valve's manhack
//			model is drawn in the viewmodel pass on the bone the grenade hangs
//			on, so every animation of the hand carries it.
//
//			Placement is by eye: of2_vm_manhack_scale / _offset / _angles.
//
//=============================================================================//

#include "cbase.h"
#include "c_baseanimating.h"
#include "c_baseviewmodel.h"
#include "igamesystem.h"
#include "engine/ivmodelinfo.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define VM_MANHACK_MODEL		"models/manhack.mdl"	// precached by weapon_manhack on the server
#define VM_MANHACK_VIEWMODEL	"models/weapons/v_manhack.mdl"
#define VM_MANHACK_BONE			"ValveBiped.Grenade_body"

// Not archived; they reset on restart.
static ConVar of2_vm_manhack( "of2_vm_manhack", "1", FCVAR_NONE, "Draw the folded manhack in the hand of weapon_manhack. 0 leaves the hand empty." );
// The grenade is about 3.6 across and 8.7 long, along its bone's Z; the manhack's body about 12 x 18 x 14.
static ConVar of2_vm_manhack_scale( "of2_vm_manhack_scale", "0.45", FCVAR_NONE, "Size of the manhack in the hand, as a fraction of the real one." );
static ConVar of2_vm_manhack_offset( "of2_vm_manhack_offset", "0 0 -0.6", FCVAR_NONE, "Where the manhack's middle is, in the grenade bone's space (the grenade's long axis is Z)." );
static ConVar of2_vm_manhack_angles( "of2_vm_manhack_angles", "0 0 90", FCVAR_NONE, "How the manhack is turned in the grenade bone's space: pitch yaw roll." );
static ConVar of2_vm_manhack_sequence( "of2_vm_manhack_sequence", "Deploy", FCVAR_NONE, "The manhack sequence that holds the pose. Deploy starts folded." );
static ConVar of2_vm_manhack_cycle( "of2_vm_manhack_cycle", "0", FCVAR_NONE, "How far into that sequence the pose is, 0 to 1. Deploy: 0 folded, 1 open." );

//-----------------------------------------------------------------------------
// The manhack in the hand: client only, moved onto the bone each time it is drawn
//-----------------------------------------------------------------------------
class C_OF2ViewModelManhack : public C_BaseAnimating
{
	DECLARE_CLASS( C_OF2ViewModelManhack, C_BaseAnimating );

public:
	virtual int				DrawModel( int flags );

	// Drawn with the viewmodels, whatever the materials say
	virtual RenderGroup_t	GetRenderGroup( void ) { return RENDER_GROUP_VIEW_MODEL_OPAQUE; }
	virtual ShadowType_t	ShadowCastType( void ) { return SHADOWS_NONE; }
	virtual bool			ShouldReceiveProjectedTextures( int flags ) { return false; }

private:
	bool					PlaceInHand( void );
};

//-----------------------------------------------------------------------------
// Purpose: the manhack viewmodel is up: go to its grenade bone
// Output : false if there is nothing to draw
//-----------------------------------------------------------------------------
bool C_OF2ViewModelManhack::PlaceInHand( void )
{
	if ( !of2_vm_manhack.GetBool() )
		return false;

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer == NULL )
		return false;

	C_BaseViewModel *pViewModel = pPlayer->GetViewModel( 0 );
	if ( pViewModel == NULL || pViewModel->GetModel() == NULL || pViewModel->IsEffectActive( EF_NODRAW ) )
		return false;

	if ( V_stricmp( modelinfo->GetModelName( pViewModel->GetModel() ), VM_MANHACK_VIEWMODEL ) != 0 )
		return false;

	int iBone = pViewModel->LookupBone( VM_MANHACK_BONE );
	if ( iBone < 0 )
		return false;

	matrix3x4_t matBone;
	pViewModel->GetBoneTransform( iBone, matBone );

	Vector vecOffset;
	QAngle angOffset;
	UTIL_StringToVector( vecOffset.Base(), of2_vm_manhack_offset.GetString() );
	UTIL_StringToVector( angOffset.Base(), of2_vm_manhack_angles.GetString() );

	matrix3x4_t matLocal, matWorld;
	AngleMatrix( angOffset, vecOffset, matLocal );
	ConcatTransforms( matBone, matLocal, matWorld );

	Vector vecOrigin;
	QAngle angles;
	MatrixAngles( matWorld, angles, vecOrigin );

	SetAbsOrigin( vecOrigin );
	SetAbsAngles( angles );

	float flScale = clamp( of2_vm_manhack_scale.GetFloat(), 0.01f, 4.0f );
	if ( GetModelScale() != flScale )
	{
		SetModelScale( flScale );
	}

	int iSequence = LookupSequence( of2_vm_manhack_sequence.GetString() );
	if ( iSequence >= 0 && GetSequence() != iSequence )
	{
		SetSequence( iSequence );
	}
	SetCycle( clamp( of2_vm_manhack_cycle.GetFloat(), 0.0f, 1.0f ) );

	// It moved after anything this frame could have set its bones up
	InvalidateBoneCache();
	return true;
}

int C_OF2ViewModelManhack::DrawModel( int flags )
{
	if ( !PlaceInHand() )
		return 0;

	return BaseClass::DrawModel( flags );
}

//-----------------------------------------------------------------------------
// Makes the manhack the first time its viewmodel is up in a level
//-----------------------------------------------------------------------------
class COF2ViewModelManhackSystem : public CAutoGameSystemPerFrame
{
public:
	COF2ViewModelManhackSystem( void ) : CAutoGameSystemPerFrame( "COF2ViewModelManhackSystem" )
	{
		m_flNextTry = 0.0f;
	}

	virtual void LevelInitPostEntity( void )
	{
		m_flNextTry = 0.0f;
	}

	virtual void LevelShutdownPreEntity( void )
	{
		if ( m_hManhack.Get() )
		{
			m_hManhack->Release();
		}
		m_hManhack = NULL;
	}

	virtual void Update( float frametime )
	{
		if ( m_hManhack.Get() || gpGlobals->curtime < m_flNextTry )
			return;

		C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
		C_BaseViewModel *pViewModel = pPlayer ? pPlayer->GetViewModel( 0 ) : NULL;
		if ( pViewModel == NULL || pViewModel->GetModel() == NULL )
			return;

		if ( V_stricmp( modelinfo->GetModelName( pViewModel->GetModel() ), VM_MANHACK_VIEWMODEL ) != 0 )
			return;

		// The model is only there once the server has precached it; ask again in a while if not
		m_flNextTry = gpGlobals->curtime + 1.0f;

		if ( modelinfo->GetModelIndex( VM_MANHACK_MODEL ) == -1 )
			return;

		C_OF2ViewModelManhack *pManhack = new C_OF2ViewModelManhack;
		if ( !pManhack->InitializeAsClientEntity( VM_MANHACK_MODEL, RENDER_GROUP_VIEW_MODEL_OPAQUE ) )
		{
			pManhack->Release();
			return;
		}

		m_hManhack = pManhack;
	}

private:
	CHandle<C_OF2ViewModelManhack>	m_hManhack;
	float							m_flNextTry;
};

static COF2ViewModelManhackSystem g_OF2ViewModelManhackSystem;
