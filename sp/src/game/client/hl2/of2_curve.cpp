//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: draws a rope-like thing as one smooth ribbon. See of2_curve.h.
//
//=============================================================================//

#include "cbase.h"
#include "beamdraw.h"
#include "of2_curve.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define CURVE_MAX_SMOOTH	8
#define CURVE_MAX_SEGS		( ( OF2_CURVE_MAX_POINTS - 1 ) * CURVE_MAX_SMOOTH + 1 )

static void DrawRibbon( IMaterial *pMaterial, const Vector *pPos, const Vector *pColor, const float *pTexCoord, int nCount, float flWidth, float flBrightness )
{
	CMatRenderContextPtr pRenderContext( materials );

	CBeamSegDraw beamDraw;
	beamDraw.Start( pRenderContext, nCount - 1, pMaterial );

	for ( int i = 0; i < nCount; i++ )
	{
		BeamSeg_t seg;
		seg.m_vPos = pPos[i];
		seg.m_vColor = pColor[i] * flBrightness;
		seg.m_flAlpha = 1.0f;
		seg.m_flTexCoord = pTexCoord[i];
		seg.m_flWidth = flWidth;
		beamDraw.NextSeg( &seg );
	}

	beamDraw.End();
}

void OF2_DrawCurve( const Vector *pPoints, const bool *pBend, int nPoints, const OF2CurveStyle_t &style )
{
	nPoints = MIN( nPoints, OF2_CURVE_MAX_POINTS );
	if ( nPoints < 2 || style.pMaterial == NULL )
		return;

	// The materials are unlit, so the light at each point is put into the color
	Vector vecPointColor[OF2_CURVE_MAX_POINTS];
	float flMinLight = clamp( style.flMinLight, 0.0f, 1.0f );
	for ( int i = 0; i < nPoints; i++ )
	{
		Vector vecLight;
		engine->ComputeLighting( pPoints[i], NULL, true, vecLight );

		for ( int j = 0; j < 3; j++ )
		{
			vecPointColor[i][j] = clamp( LinearToGamma( vecLight[j] ), flMinLight, 1.0f ) * style.vecTint[j];
		}
	}

	// A curve through the points. Next to a bend it runs straight instead.
	static Vector vecPos[CURVE_MAX_SEGS];
	static Vector vecColor[CURVE_MAX_SEGS];
	static float flTexCoord[CURVE_MAX_SEGS];

	int nSmooth = clamp( style.nSmooth, 1, CURVE_MAX_SMOOTH );
	int nCount = 0;

	for ( int i = 0; i < nPoints - 1; i++ )
	{
		bool bStraight = pBend && ( pBend[i] || pBend[i + 1] );
		int nPieces = bStraight ? 1 : nSmooth;

		const Vector &vecBefore = pPoints[MAX( i - 1, 0 )];
		const Vector &vecAfter = pPoints[MIN( i + 2, nPoints - 1 )];

		for ( int j = 0; j < nPieces; j++ )
		{
			float t = (float)j / nPieces;
			if ( bStraight )
			{
				vecPos[nCount] = pPoints[i] + ( pPoints[i + 1] - pPoints[i] ) * t;
			}
			else
			{
				Catmull_Rom_Spline_Normalize( vecBefore, pPoints[i], pPoints[i + 1], vecAfter, t, vecPos[nCount] );
			}
			vecColor[nCount] = vecPointColor[i] + ( vecPointColor[i + 1] - vecPointColor[i] ) * t;
			nCount++;
		}
	}

	vecPos[nCount] = pPoints[nPoints - 1];
	vecColor[nCount] = vecPointColor[nPoints - 1];
	nCount++;

	// The texture runs from the first point
	float flLength = 0.0f;
	flTexCoord[0] = 0.0f;
	for ( int i = 1; i < nCount; i++ )
	{
		flLength += vecPos[i].DistTo( vecPos[i - 1] );
		flTexCoord[i] = flLength;
	}

	float flScale = ( style.flTextureRepeat > 0.0f ) ? 1.0f / style.flTextureRepeat : ( ( flLength > 0.0f ) ? 1.0f / flLength : 0.0f );
	for ( int i = 1; i < nCount; i++ )
	{
		flTexCoord[i] *= flScale;
	}

	DrawRibbon( style.pMaterial, vecPos, vecColor, flTexCoord, nCount, style.flWidth, 1.0f );

	// A narrow bright streak down the middle reads as a wet, round surface
	if ( style.pShineMaterial && style.flShine > 0.0f )
	{
		DrawRibbon( style.pShineMaterial, vecPos, vecColor, flTexCoord, nCount, style.flWidth * style.flShineWidth, style.flShine );
	}
}
