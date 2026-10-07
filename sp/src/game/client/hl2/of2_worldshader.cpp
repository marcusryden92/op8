//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: Puts world materials on OF2_LightmappedGeneric, the mod's copy
//			of the engine's brush shader whose flashlight pass does not shine
//			backwards (materialsystem\stdshaders\of2_lightmappedgeneric.cpp has
//			the whole story).
//
//			A material uses the shader its .vmt names, and every stock one, and
//			every one vbsp bakes into a map, names LightmappedGeneric. A mod
//			cannot replace that shader and cannot reach the files inside a map,
//			so the name is changed here, in memory, on the materials that are
//			loaded. Nothing on disk changes, and of2_worldshader 0 puts them back.
//
//=============================================================================//

#include "cbase.h"
#include "igamesystem.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterial.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "utldict.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define WORLDSHADER_STOCK		"LightmappedGeneric"
#define WORLDSHADER_FIXED		"OF2_LightmappedGeneric"
#define WORLDSHADER_INTERVAL	5.0f	// seconds between looks for materials loaded since

static void OF2WorldShaderChanged( IConVar *pConVar, const char *pOldValue, float flOldValue );

static ConVar of2_worldshader( "of2_worldshader", "1", FCVAR_NONE, "1: world brushes are drawn with OF2_LightmappedGeneric, whose flashlight pass does not shine backwards (needed for of2_police_flashlight_projected). 0: the engine's own shader.", OF2WorldShaderChanged );

//-----------------------------------------------------------------------------
// Purpose: A material's file, with a "patch" material (what vbsp bakes into a
//			map for cubemaps and blends: an include plus keys to add or change)
//			worked out into the material it stands for. NULL if it has no file.
//-----------------------------------------------------------------------------
static KeyValues *OF2_LoadMaterialKeys( const char *pszFile, int nDepth = 0 )
{
	KeyValues *pKeys = new KeyValues( "vmt" );
	if ( nDepth > 8 || !pKeys->LoadFromFile( filesystem, pszFile, "GAME" ) )
	{
		pKeys->deleteThis();
		return NULL;
	}

	if ( Q_stricmp( pKeys->GetName(), "patch" ) )
		return pKeys;

	KeyValues *pResult = OF2_LoadMaterialKeys( pKeys->GetString( "include" ), nDepth + 1 );
	if ( pResult )
	{
		static const char *pszBlocks[] = { "insert", "replace" };
		for ( int i = 0; i < ARRAYSIZE( pszBlocks ); i++ )
		{
			KeyValues *pBlock = pKeys->FindKey( pszBlocks[i] );
			for ( KeyValues *pKey = pBlock ? pBlock->GetFirstSubKey() : NULL; pKey; pKey = pKey->GetNextKey() )
			{
				KeyValues *pOld = pResult->FindKey( pKey->GetName() );
				if ( pOld )
				{
					pResult->RemoveSubKey( pOld );
					pOld->deleteThis();
				}
				pResult->AddSubKey( pKey->MakeCopy() );
			}
		}
	}

	pKeys->deleteThis();
	return pResult;
}

// Materials that could not be moved (made in code, no file): not tried again every few seconds
static CUtlDict< bool, int > s_Skipped;

//-----------------------------------------------------------------------------
// Purpose: Moves every loaded material from one of the two shaders to the other.
//			The material is set up again from its own file with the other shader's
//			name on it. (IMaterial::SetShader alone is no use: it starts the
//			material from nothing, textures and all, and the world goes white.)
//-----------------------------------------------------------------------------
static int OF2_SwapWorldShader( bool bFixed )
{
	const char *pszFrom = bFixed ? WORLDSHADER_STOCK : WORLDSHADER_FIXED;
	const char *pszTo = bFixed ? WORLDSHADER_FIXED : WORLDSHADER_STOCK;
	int nSwapped = 0;

	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
	{
		IMaterial *pMaterial = materials->GetMaterial( h );

		// Only ones that are loaded: asking the others for their shader would load them
		if ( !pMaterial || pMaterial->IsErrorMaterial() || !pMaterial->IsPrecached() )
			continue;

		const char *pszShader = pMaterial->GetShaderName();
		if ( !pszShader || Q_stricmp( pszShader, pszFrom ) || s_Skipped.Find( pMaterial->GetName() ) != s_Skipped.InvalidIndex() )
			continue;

		char szFile[MAX_PATH];
		Q_snprintf( szFile, sizeof( szFile ), "materials/%s.vmt", pMaterial->GetName() );

		// Its file says the stock shader either way
		KeyValues *pKeys = OF2_LoadMaterialKeys( szFile );
		if ( !pKeys || Q_stricmp( pKeys->GetName(), WORLDSHADER_STOCK ) )
		{
			if ( pKeys )
			{
				pKeys->deleteThis();
			}
			s_Skipped.Insert( pMaterial->GetName(), true );
			continue;
		}

		pKeys->SetName( pszTo );
		pMaterial->SetShaderAndParams( pKeys );
		pKeys->deleteThis();
		nSwapped++;
	}

	return nSwapped;
}

static void OF2WorldShaderChanged( IConVar *pConVar, const char *pOldValue, float flOldValue )
{
	if ( !engine->IsInGame() )
		return;

	int nSwapped = OF2_SwapWorldShader( of2_worldshader.GetBool() );
	Msg( "of2_worldshader: %d materials now on %s\n", nSwapped, of2_worldshader.GetBool() ? WORLDSHADER_FIXED : WORLDSHADER_STOCK );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
class COF2WorldShader : public CAutoGameSystemPerFrame
{
public:
	COF2WorldShader() : CAutoGameSystemPerFrame( "COF2WorldShader" )
	{
		m_flNextLook = 0.0f;
	}

	virtual void LevelInitPostEntity()
	{
		s_Skipped.RemoveAll();
		m_flNextLook = 0.0f;
	}

	// Materials keep loading after the level has (brush models, things spawned later)
	virtual void Update( float frametime )
	{
		if ( !of2_worldshader.GetBool() || !engine->IsInGame() )
			return;

		float flNow = gpGlobals->realtime;
		if ( flNow < m_flNextLook )
			return;

		m_flNextLook = flNow + WORLDSHADER_INTERVAL;

		int nSwapped = OF2_SwapWorldShader( true );
		if ( nSwapped )
		{
			DevMsg( "of2_worldshader: %d materials moved to %s\n", nSwapped, WORLDSHADER_FIXED );
		}
	}

private:
	float	m_flNextLook;
};

static COF2WorldShader g_OF2WorldShader;
