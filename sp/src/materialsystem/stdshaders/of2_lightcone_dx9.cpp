//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: the light a flashlight makes in the air, as a volume: for each
//			pixel the light along the line of sight through a cone is added up
//			(of2_lightcone_ps20b.fxc). The client draws the cone's outside with
//			it and sets the numbers before each one
//			(client\hl2\c_of2_stealth.cpp, C_OF2LightCone).
//
//=============================================================================//

#include "BaseVSShader.h"
#include "of2_lightcone_vs20.inc"
#include "of2_lightcone_ps20b.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_VS_SHADER( OF2_LightCone, "Light in the air from a flashlight" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( CONEAPEX, SHADER_PARAM_TYPE_VEC4, "[0 0 0 8]", "Where the cone's sides meet; w: how far that is behind the lamp" )
		SHADER_PARAM( CONEAXIS, SHADER_PARAM_TYPE_VEC4, "[1 0 0 320]", "Direction; w: reach from the lamp" )
		SHADER_PARAM( CONESHAPE, SHADER_PARAM_TYPE_VEC4, "[0.82 0.22 2 1.5]", "cos^2 and tan^2 of the half angle, falloff along its length, softness of its edge" )
		SHADER_PARAM( CONECOLOR, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Colour, scaled" )
		SHADER_PARAM( CONEPLANE1, SHADER_PARAM_TYPE_VEC4, "[0 0 0 -1]", "A surface the light lands on: normal, distance" )
		SHADER_PARAM( CONEPLANE2, SHADER_PARAM_TYPE_VEC4, "[0 0 0 -1]", "" )
		SHADER_PARAM( CONEPLANE3, SHADER_PARAM_TYPE_VEC4, "[0 0 0 -1]", "" )
		SHADER_PARAM( CONEPLANE4, SHADER_PARAM_TYPE_VEC4, "[0 0 0 -1]", "" )
		SHADER_PARAM( CONEDEPTH, SHADER_PARAM_TYPE_TEXTURE, "", "The lamp's shadow depth map, set by the client while it has one. With it the planes are not needed." )
		SHADER_PARAM( CONESHADOW1, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "World to that map, a row each" )
		SHADER_PARAM( CONESHADOW2, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "" )
		SHADER_PARAM( CONESHADOW3, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "" )
		SHADER_PARAM( CONESHADOW4, SHADER_PARAM_TYPE_VEC4, "[0 0 0 1]", "" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_FALLBACK
	{
		if ( g_pHardwareConfig->GetDXSupportLevel() < 90 || !g_pHardwareConfig->SupportsPixelShaders_2_b() )
		{
			return "Wireframe";
		}

		return 0;
	}

	SHADER_INIT
	{
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, 0, 0 );

			// Added on top of the picture. $ignorez is for the eye being inside the cone,
			// when its far side is what gets drawn.
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableDepthTest( !IS_FLAG_SET( MATERIAL_VAR_IGNOREZ ) );
			pShaderShadow->EnableAlphaWrites( false );
			pShaderShadow->EnableBlending( true );
			pShaderShadow->BlendFunc( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
			pShaderShadow->EnableSRGBWrite( true );
			FogToBlack();

			// The lamp's shadow depth map, when it has one
			int nShadowFilterMode = g_pHardwareConfig->GetShadowFilterMode();
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->SetShadowDepthFiltering( SHADER_SAMPLER0 );

			DECLARE_STATIC_VERTEX_SHADER( of2_lightcone_vs20 );
			SET_STATIC_VERTEX_SHADER( of2_lightcone_vs20 );

			DECLARE_STATIC_PIXEL_SHADER( of2_lightcone_ps20b );
			SET_STATIC_PIXEL_SHADER_COMBO( FLASHLIGHTDEPTHFILTERMODE, nShadowFilterMode );
			SET_STATIC_PIXEL_SHADER( of2_lightcone_ps20b );
		}

		DYNAMIC_STATE
		{
			DECLARE_DYNAMIC_VERTEX_SHADER( of2_lightcone_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( of2_lightcone_vs20 );

			bool bShadowMap = params[CONEDEPTH]->IsTexture();
			if ( bShadowMap )
			{
				BindTexture( SHADER_SAMPLER0, CONEDEPTH, -1 );
			}
			else
			{
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );
			}

			DECLARE_DYNAMIC_PIXEL_SHADER( of2_lightcone_ps20b );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( SHADOWMAP, bShadowMap );
			SET_DYNAMIC_PIXEL_SHADER( of2_lightcone_ps20b );

			static const int s_nParams[8] = { CONEAPEX, CONEAXIS, CONESHAPE, CONECOLOR, CONEPLANE1, CONEPLANE2, CONEPLANE3, CONEPLANE4 };
			static const int s_nShadow[4] = { CONESHADOW1, CONESHADOW2, CONESHADOW3, CONESHADOW4 };
			float flConst[13][4];
			for ( int i = 0; i < 8; i++ )
			{
				params[s_nParams[i]]->GetVecValue( flConst[i], 4 );
			}

			pShaderAPI->GetWorldSpaceCameraPosition( flConst[8] );
			flConst[8][3] = 0.0f;

			for ( int i = 0; i < 4; i++ )
			{
				params[s_nShadow[i]]->GetVecValue( flConst[9 + i], 4 );
			}

			pShaderAPI->SetPixelShaderConstant( 0, flConst[0], 13 );
		}

		Draw();
	}
END_SHADER
