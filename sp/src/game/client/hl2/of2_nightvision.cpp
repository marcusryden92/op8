//========= OF2 ===============================================================//
//
// Purpose: Night vision. Takes over the suit flashlight's key and battery; the
//			server only networks an on/off flag (m_HL2Local.m_bNightVision).
//
//			The picture is built from three parts:
//			- A color correction lookup turns the frame into amplified grayscale.
//			  The game writes it to materials/correction/of2_nightvision.raw.
//			- A dim, wide, shadowless projected light at the eye. Amplifying pure
//			  black still gives black, so this is what shows unlit areas.
//			- Grain drawn over the view.
//			- Depth of field (viewpostprocess.cpp asks us for the focus settings).
//
//			All the tuning lives in scripts/of2_nightvision.txt, which is read
//			again whenever it is saved.
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

#define NV_SCRIPT_FILE		"scripts/of2_nightvision.txt"
#define NV_SCRIPT_POLL		0.5f		// seconds between checks for a newer script

#define NV_LOOKUP_NAME		"of2_nightvision"
#define NV_LOOKUP_DIR		"materials/correction"
#define NV_LOOKUP_FILE		NV_LOOKUP_DIR "/of2_nightvision.raw"
#define NV_LOOKUP_SIZE		32			// entries per axis; the engine's .raw format is fixed at this

#define NV_LIGHT_TEXTURE	"effects/flashlight001"
#define NV_LIGHT_NEAR		4.0f
#define NV_LIGHT_CONSTANT	0.025f		// attenuation terms at of2_nightvision_light 1
#define NV_LIGHT_LINEAR		3.0f

#define NV_GRAIN_TEXTURE	"__of2_nvgrain"
#define NV_GRAIN_SIZE		256			// procedural textures must be power-of-two sized
#define NV_GRAIN_RATE		30.0f		// new patterns per second
#define NV_GRAIN_REF_HEIGHT	540.0f		// one screen pixel per texel up to here, two at 1080 lines, ...

// Tuning. scripts/of2_nightvision.txt sets these when it loads; the values here are only
// the fallback for a missing file or key. They remain console variables so they can be
// tried out live, and none are archived, so the script always wins after a restart.
static ConVar of2_nightvision_fade( "of2_nightvision_fade", "0.3", FCVAR_NONE, "Seconds night vision takes to fade in and out." );
static ConVar of2_nightvision_light( "of2_nightvision_light", "0.1", FCVAR_NONE, "Brightness of the night vision fill light, which is what shows completely unlit areas (0 = off)." );
static ConVar of2_nightvision_light_fov( "of2_nightvision_light_fov", "140", FCVAR_NONE, "Cone of the night vision fill light in degrees. Keep it wider than the view so its edge stays off screen." );
static ConVar of2_nightvision_light_range( "of2_nightvision_light_range", "400", FCVAR_NONE, "Reach of the night vision fill light. It fades out over the last 40%." );
static ConVar of2_nightvision_grain( "of2_nightvision_grain", "0.18", FCVAR_NONE, "Strength of the night vision grain (0 = off)." );

// Depth of field. The goggles are short-sighted: the distance blurs heavily, and only
// things right in front of the lens blur on the near side.
// The depth the shader can see stops a few hundred units out (mat_dof_constant), so the
// far distances only make sense below that.
// These defaults were tuned in game by eye.
static ConVar of2_nightvision_dof( "of2_nightvision_dof", "1", FCVAR_NONE, "Depth of field while night vision is on." );
static ConVar of2_nightvision_dof_near_blur( "of2_nightvision_dof_near_blur", "8", FCVAR_NONE, "Anything nearer than this is fully blurred." );
static ConVar of2_nightvision_dof_near_focus( "of2_nightvision_dof_near_focus", "100", FCVAR_NONE, "The near blur is gone by this distance." );
static ConVar of2_nightvision_dof_far_focus( "of2_nightvision_dof_far_focus", "500", FCVAR_NONE, "The far blur starts at this distance." );
static ConVar of2_nightvision_dof_far_blur( "of2_nightvision_dof_far_blur", "600", FCVAR_NONE, "Anything beyond this is fully blurred." );
static ConVar of2_nightvision_dof_near_radius( "of2_nightvision_dof_near_radius", "20", FCVAR_NONE, "Strongest near blur, in pixels at 1080 lines (0 = no near blur)." );
static ConVar of2_nightvision_dof_far_radius( "of2_nightvision_dof_far_radius", "20", FCVAR_NONE, "Strongest far blur, in pixels at 1080 lines (0 = no far blur)." );

// Script keys that map straight onto the console variables above
struct NightVisionSetting_t
{
	const char	*pszKey;
	ConVar		*pVar;
};

static const NightVisionSetting_t s_NightVisionSettings[] =
{
	{ "fade",				&of2_nightvision_fade },
	{ "grain",				&of2_nightvision_grain },
	{ "light",				&of2_nightvision_light },
	{ "light_range",		&of2_nightvision_light_range },
	{ "light_fov",			&of2_nightvision_light_fov },
	{ "dof",				&of2_nightvision_dof },
	{ "dof_far_radius",		&of2_nightvision_dof_far_radius },
	{ "dof_far_focus",		&of2_nightvision_dof_far_focus },
	{ "dof_far_blur",		&of2_nightvision_dof_far_blur },
	{ "dof_near_radius",	&of2_nightvision_dof_near_radius },
	{ "dof_near_blur",		&of2_nightvision_dof_near_blur },
	{ "dof_near_focus",		&of2_nightvision_dof_near_focus },
};

// How the lookup maps scene brightness to picture brightness. These have no console
// variables: changing one means rebuilding the lookup, which the script reload does.
struct NightVisionPicture_t
{
	float	flGamma;		// amplification curve, out = in ^ gamma. Lower lifts dark areas more.
	float	flCeiling;		// level the curve tops out at, before the blow-out
	float	flKnee;			// input brightness where highlights start to blow out
	float	flWhitePoint;	// input brightness that is pure white, and everything above it
	Vector	vecTintDark;	// color of dim areas
	Vector	vecTintLight;	// color of bright areas

	NightVisionPicture_t()
	{
		flGamma = 0.4f;
		flCeiling = 0.92f;
		flKnee = 0.2f;
		flWhitePoint = 0.5f;
		vecTintDark.Init( 1.0f, 1.0f, 1.0f );
		vecTintLight.Init( 1.0f, 1.0f, 1.0f );
	}

	bool operator==( const NightVisionPicture_t &other ) const
	{
		return flGamma == other.flGamma && flCeiling == other.flCeiling && flKnee == other.flKnee &&
			flWhitePoint == other.flWhitePoint && vecTintDark == other.vecTintDark && vecTintLight == other.vecTintLight;
	}
};

//-----------------------------------------------------------------------------
// Purpose: Builds the lookup in the engine's .raw layout: 32x32x32 entries of
//			R G B bytes, red index changing fastest, blue slowest. Each entry is
//			the color that input color gets replaced with.
//-----------------------------------------------------------------------------
static void BuildNightVisionLookup( const NightVisionPicture_t &picture, CUtlBuffer &buf )
{
	const float flStep = 1.0f / ( NV_LOOKUP_SIZE - 1 );

	for ( int b = 0; b < NV_LOOKUP_SIZE; b++ )
	{
		for ( int g = 0; g < NV_LOOKUP_SIZE; g++ )
		{
			for ( int r = 0; r < NV_LOOKUP_SIZE; r++ )
			{
				float flR = r * flStep, flG = g * flStep, flB = b * flStep;

				// How bright the pixel is. Treats the three channels alike on purpose:
				// a pure red or blue light should show up as clearly as a green one.
				float flMax = MAX( flR, MAX( flG, flB ) );
				float flValue = 0.6f * flMax + 0.4f * ( flR + flG + flB ) / 3.0f;

				float flLevel = picture.flCeiling * powf( flValue, picture.flGamma );

				// Overexposure: ease from the curve up to pure white between the knee and the white point
				if ( picture.flWhitePoint > picture.flKnee )
				{
					float t = clamp( ( flValue - picture.flKnee ) / ( picture.flWhitePoint - picture.flKnee ), 0.0f, 1.0f );
					t = t * t * ( 3.0f - 2.0f * t );
					flLevel += ( 1.0f - flLevel ) * t;
				}
				else if ( flValue >= picture.flWhitePoint )
				{
					flLevel = 1.0f;
				}

				float flWash = flLevel * flLevel;
				for ( int c = 0; c < 3; c++ )
				{
					float flTint = Lerp( flWash, picture.vecTintDark[c], picture.vecTintLight[c] );
					buf.PutUnsignedChar( (unsigned char)RoundFloatToInt( 255.0f * clamp( flLevel * flTint, 0.0f, 1.0f ) ) );
				}
			}
		}
	}
}


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
		m_bLookupWritten = false;
		m_nScriptTime = 0;
		m_flNextScriptCheck = 0.0f;
	}

	virtual void LevelInitPostEntity();
	virtual void LevelShutdownPreEntity();
	virtual void Update( float frametime );

	float GetAmount() const { return m_flAmount; }
	void ApplyColorCorrection();
	void LoadScript();

private:
	void UpdateLight( C_BasePlayer *pPlayer );
	void DestroyLight();
	void WriteLookup();
	void ReloadLookup();

	float					m_flAmount;
	ClientCCHandle_t		m_CCHandle;
	ClientShadowHandle_t	m_LightHandle;
	CTextureReference		m_LightTexture;

	NightVisionPicture_t	m_Picture;				// what the lookup on disk was built from
	bool					m_bLookupWritten;		// false until we have written it this session
	long					m_nScriptTime;			// file time of the script when it was last read
	float					m_flNextScriptCheck;
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

CON_COMMAND( of2_nightvision_reload, "Reads scripts/of2_nightvision.txt again. Saving the file does this by itself; use this to throw away values typed in the console." )
{
	g_OF2NightVision.LoadScript();
}

bool OF2_NightVisionGetDepthOfField( OF2DepthOfField_t *pDOF )
{
	float flAmount = g_OF2NightVision.GetAmount();
	if ( flAmount <= 0.0f || !of2_nightvision_dof.GetBool() )
		return false;

	// The blur grows with the picture as night vision fades in
	float flRadiusScale = flAmount * ScreenHeight() / 1080.0f;
	float flNearRadius = MAX( of2_nightvision_dof_near_radius.GetFloat(), 0.0f ) * flRadiusScale;
	float flFarRadius = MAX( of2_nightvision_dof_far_radius.GetFloat(), 0.0f ) * flRadiusScale;
	if ( flNearRadius <= 0.0f && flFarRadius <= 0.0f )
		return false;

	if ( pDOF )
	{
		// The shader divides by the width of each ramp, so keep them from collapsing
		pDOF->flNearBlurDepth = of2_nightvision_dof_near_blur.GetFloat();
		pDOF->flNearFocusDepth = MAX( of2_nightvision_dof_near_focus.GetFloat(), pDOF->flNearBlurDepth + 1.0f );
		pDOF->flFarFocusDepth = MAX( of2_nightvision_dof_far_focus.GetFloat(), pDOF->flNearFocusDepth );
		pDOF->flFarBlurDepth = MAX( of2_nightvision_dof_far_blur.GetFloat(), pDOF->flFarFocusDepth + 1.0f );
		pDOF->flNearBlurRadius = flNearRadius;
		pDOF->flFarBlurRadius = flFarRadius;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COF2NightVision::LevelInitPostEntity()
{
	m_flAmount = 0.0f;

	// Reads the settings, and writes the lookup if this is the first level of the session
	LoadScript();

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

	// Pick up the script as soon as it is saved, so it can be tuned with the game running
	if ( pPlayer && gpGlobals->realtime >= m_flNextScriptCheck )
	{
		m_flNextScriptCheck = gpGlobals->realtime + NV_SCRIPT_POLL;
		if ( filesystem->GetFileTime( NV_SCRIPT_FILE, "MOD" ) != m_nScriptTime )
		{
			LoadScript();
		}
	}

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
// Purpose: Reads scripts/of2_nightvision.txt. Most keys just set a console
//			variable; the picture keys rebuild the lookup when they change.
//-----------------------------------------------------------------------------
void COF2NightVision::LoadScript()
{
	m_nScriptTime = filesystem->GetFileTime( NV_SCRIPT_FILE, "MOD" );

	NightVisionPicture_t picture;

	KeyValues *pScript = new KeyValues( "NightVision" );
	if ( pScript->LoadFromFile( filesystem, NV_SCRIPT_FILE, "MOD" ) )
	{
		for ( KeyValues *pKey = pScript->GetFirstSubKey(); pKey; pKey = pKey->GetNextKey() )
		{
			const char *pszKey = pKey->GetName();

			bool bFound = false;
			for ( int i = 0; i < ARRAYSIZE( s_NightVisionSettings ); i++ )
			{
				if ( !V_stricmp( pszKey, s_NightVisionSettings[i].pszKey ) )
				{
					s_NightVisionSettings[i].pVar->SetValue( pKey->GetString() );
					bFound = true;
					break;
				}
			}
			if ( bFound )
				continue;

			if ( !V_stricmp( pszKey, "gamma" ) )
			{
				picture.flGamma = pKey->GetFloat();
			}
			else if ( !V_stricmp( pszKey, "ceiling" ) )
			{
				picture.flCeiling = pKey->GetFloat();
			}
			else if ( !V_stricmp( pszKey, "knee" ) )
			{
				picture.flKnee = pKey->GetFloat();
			}
			else if ( !V_stricmp( pszKey, "white_point" ) )
			{
				picture.flWhitePoint = pKey->GetFloat();
			}
			else if ( !V_stricmp( pszKey, "tint_dark" ) )
			{
				UTIL_StringToVector( picture.vecTintDark.Base(), pKey->GetString() );
			}
			else if ( !V_stricmp( pszKey, "tint_light" ) )
			{
				UTIL_StringToVector( picture.vecTintLight.Base(), pKey->GetString() );
			}
			else
			{
				Warning( "Night vision: unknown setting '%s' in %s\n", pszKey, NV_SCRIPT_FILE );
			}
		}

		Msg( "Night vision: loaded %s\n", NV_SCRIPT_FILE );
	}
	else
	{
		// Missing, or caught half-saved. Keep what we have rather than snapping to the built-in look.
		Warning( "Night vision: can't read %s, keeping the current settings\n", NV_SCRIPT_FILE );
		picture = m_Picture;
	}
	pScript->deleteThis();

	if ( !m_bLookupWritten || !( picture == m_Picture ) )
	{
		m_Picture = picture;
		WriteLookup();
		ReloadLookup();
	}
}

//-----------------------------------------------------------------------------
// Purpose: The engine can only load a lookup from a file, so write one
//-----------------------------------------------------------------------------
void COF2NightVision::WriteLookup()
{
	CUtlBuffer buf( 0, NV_LOOKUP_SIZE * NV_LOOKUP_SIZE * NV_LOOKUP_SIZE * 3 );
	BuildNightVisionLookup( m_Picture, buf );

	filesystem->CreateDirHierarchy( NV_LOOKUP_DIR, "MOD" );
	if ( filesystem->WriteFile( NV_LOOKUP_FILE, "MOD", buf ) )
	{
		m_bLookupWritten = true;
	}
	else
	{
		Warning( "Night vision: can't write %s\n", NV_LOOKUP_FILE );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Makes the engine read the lookup file again, if a level has it loaded
//-----------------------------------------------------------------------------
void COF2NightVision::ReloadLookup()
{
	if ( m_CCHandle == INVALID_CLIENT_CCHANDLE )
		return;

	CMatRenderContextPtr pRenderContext( materials );
	ColorCorrectionHandle_t hLookup = (ColorCorrectionHandle_t)m_CCHandle;
	pRenderContext->LockLookup( hLookup );
	pRenderContext->LoadLookup( hLookup, NV_LOOKUP_FILE );
	pRenderContext->UnlockLookup( hLookup );
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
