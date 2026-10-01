//========= OF2 ===============================================================//
//
// Purpose: Night vision. Takes over the suit flashlight's key and battery; the
//			server only networks an on/off flag (m_HL2Local.m_bNightVision).
//
//			The picture is built from three parts:
//			- A color correction lookup turns the frame into amplified grayscale.
//			  The lookup is materials/correction/of2_nightvision.raw, generated
//			  by materialsrc/correction/of2_nightvision_lut.ps1.
//			- A dim, wide, shadowless projected light at the eye. Amplifying pure
//			  black still gives black, so this is what shows unlit areas.
//			- Grain drawn over the view.
//
//=============================================================================//

#include "cbase.h"
#include "of2_nightvision.h"
#include "c_basehlplayer.h"
#include "colorcorrectionmgr.h"
#include "iclientshadowmgr.h"
#include "ScreenSpaceEffects.h"
#include "igamesystem.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/itexture.h"
#include "vtf/vtf.h"
#include "vstdlib/random.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define NV_LOOKUP_NAME		"of2_nightvision"
#define NV_LOOKUP_FILE		"materials/correction/of2_nightvision.raw"

#define NV_LIGHT_TEXTURE	"effects/flashlight001"
#define NV_LIGHT_NEAR		4.0f
#define NV_LIGHT_CONSTANT	0.025f		// attenuation terms at of2_nightvision_light 1
#define NV_LIGHT_LINEAR		3.0f

#define NV_GRAIN_TEXTURE	"__of2_nvgrain"
#define NV_GRAIN_SIZE		256			// procedural textures must be power-of-two sized
#define NV_GRAIN_RATE		30.0f		// new patterns per second
#define NV_GRAIN_REF_HEIGHT	540.0f		// one screen pixel per texel up to here, two at 1080 lines, ...

// Tuning. None of these are archived, so the defaults here always apply.
static ConVar of2_nightvision_fade( "of2_nightvision_fade", "0.3", FCVAR_NONE, "Seconds night vision takes to fade in and out." );
static ConVar of2_nightvision_light( "of2_nightvision_light", "1", FCVAR_NONE, "Brightness of the night vision fill light, which is what shows completely unlit areas (0 = off)." );
static ConVar of2_nightvision_light_fov( "of2_nightvision_light_fov", "140", FCVAR_NONE, "Cone of the night vision fill light in degrees. Keep it wider than the view so its edge stays off screen." );
static ConVar of2_nightvision_light_range( "of2_nightvision_light_range", "1500", FCVAR_NONE, "Reach of the night vision fill light. It fades out over the last 40%." );
static ConVar of2_nightvision_grain( "of2_nightvision_grain", "0.12", FCVAR_NONE, "Strength of the night vision grain (0 = off)." );


//-----------------------------------------------------------------------------
// Purpose: Tracks the on/off state, and owns the lookup and the fill light
//-----------------------------------------------------------------------------
class COF2NightVision : public CAutoGameSystemPerFrame
{
public:
	COF2NightVision() : CAutoGameSystemPerFrame( "COF2NightVision" )
	{
		m_flAmount = 0.0f;
		m_CCHandle = INVALID_CLIENT_CCHANDLE;
		m_LightHandle = CLIENTSHADOW_INVALID_HANDLE;
	}

	virtual void LevelInitPostEntity();
	virtual void LevelShutdownPreEntity();
	virtual void Update( float frametime );

	float GetAmount() const { return m_flAmount; }
	void ApplyColorCorrection();

private:
	void UpdateLight( C_BasePlayer *pPlayer );
	void DestroyLight();

	float					m_flAmount;
	ClientCCHandle_t		m_CCHandle;
	ClientShadowHandle_t	m_LightHandle;
	CTextureReference		m_LightTexture;
};

static COF2NightVision g_OF2NightVision;

float OF2_NightVisionAmount()
{
	return g_OF2NightVision.GetAmount();
}

void OF2_NightVisionApplyColorCorrection()
{
	g_OF2NightVision.ApplyColorCorrection();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COF2NightVision::LevelInitPostEntity()
{
	m_flAmount = 0.0f;

	if ( filesystem->FileExists( NV_LOOKUP_FILE, "GAME" ) )
	{
		m_CCHandle = g_pColorCorrectionMgr->AddColorCorrection( NV_LOOKUP_NAME, NV_LOOKUP_FILE );
	}
	else
	{
		Warning( "Night vision: missing color correction lookup '%s'\n", NV_LOOKUP_FILE );
	}

	m_LightTexture.Init( NV_LIGHT_TEXTURE, TEXTURE_GROUP_OTHER, true );
}

//-----------------------------------------------------------------------------
// Purpose: This has to be the pre-entity shutdown: the shadow manager destroys
//			whatever lights are left in its post-entity one, and ours would then
//			be a stale handle
//-----------------------------------------------------------------------------
void COF2NightVision::LevelShutdownPreEntity()
{
	DestroyLight();
	m_LightTexture.Shutdown();

	g_pColorCorrectionMgr->RemoveColorCorrection( m_CCHandle );
	m_CCHandle = INVALID_CLIENT_CCHANDLE;

	m_flAmount = 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: Eases toward the state the server networked, and keeps the fill
//			light on the eye while there is anything to see
//-----------------------------------------------------------------------------
void COF2NightVision::Update( float frametime )
{
	C_BaseHLPlayer *pPlayer = (C_BaseHLPlayer *)C_BasePlayer::GetLocalPlayer();
	bool bOn = pPlayer && pPlayer->IsAlive() && pPlayer->m_HL2Local.m_bNightVision;

	float flTarget = bOn ? 1.0f : 0.0f;
	float flFade = of2_nightvision_fade.GetFloat();
	if ( flFade > 0.0f )
	{
		m_flAmount = Approach( flTarget, m_flAmount, frametime / flFade );
	}
	else
	{
		m_flAmount = flTarget;
	}

	if ( pPlayer && m_flAmount > 0.0f )
	{
		UpdateLight( pPlayer );
	}
	else
	{
		DestroyLight();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Night vision replaces whatever correction the map has, so its
//			weight is exclusive: the map's corrections fade out as it fades in
//-----------------------------------------------------------------------------
void COF2NightVision::ApplyColorCorrection()
{
	if ( m_CCHandle == INVALID_CLIENT_CCHANDLE || m_flAmount <= 0.0f )
		return;

	g_pColorCorrectionMgr->SetColorCorrectionWeight( m_CCHandle, m_flAmount, true );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COF2NightVision::UpdateLight( C_BasePlayer *pPlayer )
{
	float flBrightness = of2_nightvision_light.GetFloat() * m_flAmount;
	if ( flBrightness <= 0.0f || !m_LightTexture.IsValid() )
	{
		DestroyLight();
		return;
	}

	Vector vecForward, vecRight, vecUp;
	pPlayer->EyeVectors( &vecForward, &vecRight, &vecUp );

	float flFOV = clamp( of2_nightvision_light_fov.GetFloat(), 10.0f, 170.0f );
	float flRange = MAX( of2_nightvision_light_range.GetFloat(), NV_LIGHT_NEAR * 2.0f );

	FlashlightState_t state;
	state.m_vecLightOrigin = pPlayer->EyePosition();
	BasisToQuaternion( vecForward, vecRight, vecUp, state.m_quatOrientation );
	state.m_NearZ = NV_LIGHT_NEAR;
	state.m_FarZ = flRange;
	state.m_fHorizontalFOVDegrees = flFOV;
	state.m_fVerticalFOVDegrees = flFOV;
	state.m_fQuadraticAtten = 0.0f;
	state.m_fLinearAtten = NV_LIGHT_LINEAR * flBrightness;
	state.m_fConstantAtten = NV_LIGHT_CONSTANT * flBrightness;
	state.m_Color[0] = 1.0f;
	state.m_Color[1] = 1.0f;
	state.m_Color[2] = 1.0f;
	state.m_Color[3] = 0.0f;
	state.m_pSpotlightTexture = m_LightTexture;
	state.m_nSpotlightTextureFrame = 0;
	state.m_bEnableShadows = false;
#ifdef ASW_PROJECTED_TEXTURES
	state.m_FarZAtten = flRange;
	state.m_bGlobalLight = false;
#endif
#ifdef MAPBASE
	state.m_bAlwaysDraw = false;
#endif

	if ( m_LightHandle == CLIENTSHADOW_INVALID_HANDLE )
	{
		m_LightHandle = g_pClientShadowMgr->CreateFlashlight( state );
	}
	else
	{
		g_pClientShadowMgr->UpdateFlashlightState( m_LightHandle, state );
	}

	g_pClientShadowMgr->UpdateProjectedTexture( m_LightHandle, true );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COF2NightVision::DestroyLight()
{
	if ( m_LightHandle != CLIENTSHADOW_INVALID_HANDLE )
	{
		g_pClientShadowMgr->DestroyFlashlight( m_LightHandle );
		m_LightHandle = CLIENTSHADOW_INVALID_HANDLE;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Fills the grain texture. Mostly dark with a few bright specks.
//-----------------------------------------------------------------------------
class COF2NightVisionGrainRegen : public ITextureRegenerator
{
public:
	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pSubRect );
	virtual void Release() {}
};

void COF2NightVisionGrainRegen::RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pSubRect )
{
	if ( pVTFTexture->Format() != IMAGE_FORMAT_BGRX8888 )
		return;

	// Fixed seed: this runs again whenever the texture has to be rebuilt
	CUniformRandomStream random;
	random.SetSeed( 1998 );

	unsigned char *pBits = pVTFTexture->ImageData( 0, 0, 0 );
	int nStride = pVTFTexture->RowSizeInBytes( 0 );
	for ( int y = 0; y < pVTFTexture->Height(); y++ )
	{
		unsigned char *pPixel = pBits + y * nStride;
		for ( int x = 0; x < pVTFTexture->Width(); x++, pPixel += 4 )
		{
			float flValue = random.RandomFloat( 0.0f, 1.0f );
			unsigned char nValue = (unsigned char)( flValue * flValue * 255.0f );
			pPixel[0] = nValue;
			pPixel[1] = nValue;
			pPixel[2] = nValue;
			pPixel[3] = 255;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draws the grain over the finished view (under the HUD)
//-----------------------------------------------------------------------------
class COF2NightVisionGrain : public IScreenSpaceEffect
{
public:
	COF2NightVisionGrain() : m_nPattern( -1 ), m_nOffsetX( 0 ), m_nOffsetY( 0 ) {}

	virtual void Init();
	virtual void Shutdown();
	virtual void SetParameters( KeyValues *params ) {}
	virtual void Enable( bool bEnable ) {}
	virtual bool IsEnabled() { return true; }

	virtual void Render( int x, int y, int w, int h );

private:
	COF2NightVisionGrainRegen	m_Regen;
	CTextureReference			m_GrainTexture;
	CMaterialReference			m_GrainMaterial;
	int		m_nPattern;
	int		m_nOffsetX;
	int		m_nOffsetY;
};

ADD_SCREENSPACE_EFFECT( COF2NightVisionGrain, of2_nightvision );

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COF2NightVisionGrain::Init()
{
	m_GrainTexture.InitProceduralTexture( NV_GRAIN_TEXTURE, TEXTURE_GROUP_CLIENT_EFFECTS, NV_GRAIN_SIZE, NV_GRAIN_SIZE,
		IMAGE_FORMAT_BGRX8888, TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_POINTSAMPLE );
	m_GrainTexture->SetTextureRegenerator( &m_Regen );
	m_GrainTexture->Download();

	KeyValues *pVMTKeyValues = new KeyValues( "UnlitGeneric" );
	pVMTKeyValues->SetString( "$basetexture", NV_GRAIN_TEXTURE );
	pVMTKeyValues->SetInt( "$additive", 1 );
	pVMTKeyValues->SetInt( "$ignorez", 1 );
	m_GrainMaterial.Init( "__of2_nvgrainmat", TEXTURE_GROUP_CLIENT_EFFECTS, pVMTKeyValues );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COF2NightVisionGrain::Shutdown()
{
	m_GrainMaterial.Shutdown();

	if ( m_GrainTexture.IsValid() )
	{
		m_GrainTexture->SetTextureRegenerator( NULL );
		m_GrainTexture.Shutdown();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COF2NightVisionGrain::Render( int x, int y, int w, int h )
{
	float flStrength = of2_nightvision_grain.GetFloat() * OF2_NightVisionAmount();
	if ( flStrength <= 0.0f || !m_GrainMaterial.IsValid() )
		return;

	// Shift the pattern at a fixed rate so it looks the same at any frame rate (and freezes when paused)
	int nPattern = (int)( gpGlobals->curtime * NV_GRAIN_RATE );
	if ( nPattern != m_nPattern )
	{
		m_nPattern = nPattern;
		m_nOffsetX = RandomInt( 0, NV_GRAIN_SIZE - 1 );
		m_nOffsetY = RandomInt( 0, NV_GRAIN_SIZE - 1 );
	}

	CMatRenderContextPtr pRenderContext( materials );

	// UnlitGeneric gets tone mapped in HDR, which would make the grain follow the exposure.
	// CViewRender::RenderView resets the scale like this right after the screen-space effects anyway.
	if ( g_pMaterialSystemHardwareConfig->GetHDRType() != HDR_TYPE_NONE )
	{
		pRenderContext->SetToneMappingScaleLinear( Vector( 1, 1, 1 ) );
	}

	flStrength = MIN( flStrength, 1.0f );
	m_GrainMaterial->ColorModulate( flStrength, flStrength, flStrength );

	pRenderContext->MatrixMode( MATERIAL_VIEW );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();
	pRenderContext->MatrixMode( MATERIAL_PROJECTION );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();

	// A whole number of screen pixels per texel keeps every grain the same size. The source
	// coordinates are the texels at the centers of the first and last destination pixel.
	float flScale = MAX( 1, RoundFloatToInt( h / NV_GRAIN_REF_HEIGHT ) );
	float flU0 = m_nOffsetX + 0.5f / flScale - 0.5f;
	float flV0 = m_nOffsetY + 0.5f / flScale - 0.5f;
	float flU1 = m_nOffsetX + ( w - 0.5f ) / flScale - 0.5f;
	float flV1 = m_nOffsetY + ( h - 0.5f ) / flScale - 0.5f;

	pRenderContext->DrawScreenSpaceRectangle( m_GrainMaterial, 0, 0, w, h,
		flU0, flV0, flU1, flV1,
		NV_GRAIN_SIZE, NV_GRAIN_SIZE );

	pRenderContext->MatrixMode( MATERIAL_VIEW );
	pRenderContext->PopMatrix();
	pRenderContext->MatrixMode( MATERIAL_PROJECTION );
	pRenderContext->PopMatrix();
}
