//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: draws a rope-like thing as one smooth ribbon. See of2_curve.h.
//
//			Flat: a beam, one color across its width. (Nothing uses this now.)
//
//			As a tube (the Barnacle's tongue, func_climbrope): still a strip that faces the
//			view, but with several points across it, each shaded as the spot
//			on a round surface it stands for. Its width can change along it:
//			towards the end it thickens a little, and it ends round. The round
//			ending is one disc that faces the view, the size of the strip
//			there and shaded as a ball, so that along the line where the two
//			meet they are the same color and no seam shows.
//
//=============================================================================//

#include "cbase.h"
#include "beamdraw.h"
#include "view.h"
#include "of2_curve.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define CURVE_MAX_SMOOTH	8
// A texture stretched once over the whole length stops this short of its ends
#define CURVE_TEXTURE_INSET	0.03f
// A bend over an edge is drawn as a curve of this many pieces
#define CURVE_BEND_PIECES	8
// The stretch up to a blob is cut finer, up to this many pieces
#define CURVE_TIP_PIECES	24
#define CURVE_MAX_SEGS		( ( OF2_CURVE_MAX_POINTS - 1 ) * CURVE_MAX_SMOOTH + CURVE_TIP_PIECES + 1 )

// A tube has this many points across: its edges, one just inside each, and
// five for the highlight: its middle, where it ends either side, and one each
// side between, so that it is a hot line with a soft glow around it
#define TUBE_COLUMNS		9
#define TUBE_MIDDLE			4
// Most pieces of a tube's length drawn as one mesh
#define TUBE_RUN			256
// How far out the points just inside the edges are
#define TUBE_SHOULDER		0.85f
// Where the highlight's in-between points are, as a share of its half-width,
// and how bright it is there, as a share of its middle
#define TUBE_SHINE_CORE		0.3f
#define TUBE_SHINE_GLOW		0.3f

// The disc of a round ending: how many sides it has, and how far behind its
// own middle it is put so the strip is drawn over it where they meet
#define BLOB_DISC_SIDES		16
#define BLOB_DISC_BACK		0.05f
// A disc is a middle point, a ring this far out, and its rim
#define BLOB_DISC_RING		0.7f

// The highlight's round ending on that disc: how many sides its half oval
// has, how far past the disc's middle it reaches (as a share of the disc's
// size), and how far in front of the disc it is drawn
#define GLINT_SIDES			8
#define GLINT_LENGTH		0.55f
#define GLINT_FORWARD		0.3f

// However the light falls, a side is never shaded darker or brighter than this
#define TUBE_SIDE_MIN		0.4f
#define TUBE_SIDE_MAX		1.8f

static void DrawRibbon( IMaterial *pMaterial, const Vector *pPos, const Vector *pColor, const float *pTexCoord, const float *pRadius, int nCount, float flWidthScale, float flBrightness )
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
		seg.m_flWidth = pRadius[i] * 2.0f * flWidthScale;
		beamDraw.NextSeg( &seg );
	}

	beamDraw.End();
}

//-----------------------------------------------------------------------------
// The color of a spot on a round surface. vecColor is the light at the place
// (from all sides evenly); pBox is how bright it is from each of the six
// directions (+x -x +y -y +z -z); vecNormal is the way the spot faces;
// flEdge is how far it is from the middle of the tube towards its edge.
//-----------------------------------------------------------------------------
static Vector ShadeTube( const Vector &vecColor, const float *pBox, const Vector &vecNormal, float flEdge, const OF2CurveStyle_t &style )
{
	float flSide = 1.0f;

	float flAll = ( pBox[0] + pBox[1] + pBox[2] + pBox[3] + pBox[4] + pBox[5] ) / 6.0f;
	if ( flAll > 0.0001f && style.flSideLight > 0.0f )
	{
		float flFrom = vecNormal.x * vecNormal.x * ( ( vecNormal.x > 0.0f ) ? pBox[0] : pBox[1] )
			+ vecNormal.y * vecNormal.y * ( ( vecNormal.y > 0.0f ) ? pBox[2] : pBox[3] )
			+ vecNormal.z * vecNormal.z * ( ( vecNormal.z > 0.0f ) ? pBox[4] : pBox[5] );

		flSide = 1.0f + ( flFrom / flAll - 1.0f ) * MIN( style.flSideLight, 1.0f );
		flSide = clamp( flSide, TUBE_SIDE_MIN, TUBE_SIDE_MAX );

		// (the box is light as it is; colors are as the screen shows them)
		flSide = pow( flSide, 1.0f / 2.2f );
	}

	float flShade = flSide * ( 1.0f - clamp( style.flRound, 0.0f, 1.0f ) * flEdge * flEdge );

	return Vector( MIN( vecColor.x * flShade, 1.0f ), MIN( vecColor.y * flShade, 1.0f ), MIN( vecColor.z * flShade, 1.0f ) );
}

//-----------------------------------------------------------------------------
// One filled circle of a blob, facing the view, shaded as a ball. vecSide is
// the direction across the strip there, so that the circle's shading and
// texture carry on from the strip's where the two meet.
//-----------------------------------------------------------------------------
static void DrawDisc( IMaterial *pMaterial, const Vector &vecCenter, float flRadius, const Vector &vecView, const Vector &vecSide,
	const Vector &vecColor, const float *pBox, float flTexCoord, const OF2CurveStyle_t &style )
{
	Vector vecUp = CrossProduct( vecView, vecSide );
	if ( VectorNormalize( vecUp ) < 0.01f )
		return;

	Vector vecMiddle = vecCenter - vecView * BLOB_DISC_BACK;

	// The middle, then a ring part of the way out, then the rim
	Vector vecPos[2][BLOB_DISC_SIDES];
	Vector vecShade[2][BLOB_DISC_SIDES];
	Vector vecFace[2][BLOB_DISC_SIDES];
	float flAcross[2][BLOB_DISC_SIDES];
	static const float flOut[2] = { BLOB_DISC_RING, 1.0f };

	for ( int r = 0; r < 2; r++ )
	{
		float flFacing = sqrt( 1.0f - flOut[r] * flOut[r] );
		for ( int i = 0; i < BLOB_DISC_SIDES; i++ )
		{
			float flAngle = 2.0f * M_PI_F * i / BLOB_DISC_SIDES;
			float flCos = cos( flAngle );
			Vector vecSpoke = vecSide * flCos + vecUp * sin( flAngle );

			vecPos[r][i] = vecMiddle + vecSpoke * ( flRadius * flOut[r] );
			vecFace[r][i] = vecView * flFacing + vecSpoke * flOut[r];
			vecShade[r][i] = ShadeTube( vecColor, pBox, vecFace[r][i], flOut[r], style ) * style.flTipShade;
			flAcross[r][i] = 0.5f + 0.5f * flOut[r] * flCos;
		}
	}

	Vector vecMiddleShade = ShadeTube( vecColor, pBox, vecView, 0.0f, style ) * style.flTipShade;

	CMatRenderContextPtr pRenderContext( materials );
	IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, pMaterial );

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, BLOB_DISC_SIDES * 3 );

	for ( int i = 0; i < BLOB_DISC_SIDES; i++ )
	{
		int j = ( i + 1 ) % BLOB_DISC_SIDES;

		// Middle to the ring
		meshBuilder.Position3fv( vecMiddle.Base() );
		meshBuilder.Normal3fv( vecView.Base() );
		meshBuilder.Color4f( vecMiddleShade.x, vecMiddleShade.y, vecMiddleShade.z, 1.0f );
		meshBuilder.TexCoord2f( 0, 0.5f, flTexCoord );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3fv( vecPos[0][i].Base() );
		meshBuilder.Normal3fv( vecFace[0][i].Base() );
		meshBuilder.Color4f( vecShade[0][i].x, vecShade[0][i].y, vecShade[0][i].z, 1.0f );
		meshBuilder.TexCoord2f( 0, flAcross[0][i], flTexCoord );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3fv( vecPos[0][j].Base() );
		meshBuilder.Normal3fv( vecFace[0][j].Base() );
		meshBuilder.Color4f( vecShade[0][j].x, vecShade[0][j].y, vecShade[0][j].z, 1.0f );
		meshBuilder.TexCoord2f( 0, flAcross[0][j], flTexCoord );
		meshBuilder.AdvanceVertex();

		// Ring to the rim, in two halves
		static const int iCorner[6][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 0, 1 } };
		for ( int k = 0; k < 6; k++ )
		{
			int r = iCorner[k][0];
			int s = iCorner[k][1] ? j : i;

			meshBuilder.Position3fv( vecPos[r][s].Base() );
			meshBuilder.Normal3fv( vecFace[r][s].Base() );
			meshBuilder.Color4f( vecShade[r][s].x, vecShade[r][s].y, vecShade[r][s].z, 1.0f );
			meshBuilder.TexCoord2f( 0, flAcross[r][s], flTexCoord );
			meshBuilder.AdvanceVertex();
		}
	}

	meshBuilder.End();
	pMesh->Draw();
}

//-----------------------------------------------------------------------------
// The highlight's own round ending, on the disc that ends the tube: half an
// oval that carries on from where the streak along the tube stops (at the
// disc's middle line, flPeak across it), as wide as the streak and as bright
// across, fading out towards the end. Only that half: over the tube itself
// the streak is drawn already, and twice would be twice as bright.
//-----------------------------------------------------------------------------
static void DrawGlint( IMaterial *pMaterial, const Vector &vecCenter, float flRadius, const Vector &vecView, const Vector &vecSide, const Vector &vecDir,
	const Vector &vecLight, float flPeak, float flHalfWidth, float flShine )
{
	// The way the tube runs, as the disc lies
	Vector vecOn = CrossProduct( vecView, vecSide );
	if ( VectorNormalize( vecOn ) < 0.01f )
		return;

	if ( DotProduct( vecOn, vecDir ) < 0.0f )
	{
		vecOn = -vecOn;
	}

	// Well in front of the disc, towards the eye: that moves nothing on screen,
	// and leaves no doubt which of the two is in front
	Vector vecMiddle = vecCenter + vecSide * ( flPeak * flRadius ) + vecView * GLINT_FORWARD;
	float flWide = flHalfWidth * flRadius;
	float flLong = MIN( GLINT_LENGTH, 0.9f - fabs( flPeak ) ) * flRadius;
	if ( flLong <= 0.0f )
		return;

	// The middle, a ring where the hot line gives way to the glow, and the rim
	Vector vecPos[2][GLINT_SIDES + 1];
	static const float flOut[2] = { TUBE_SHINE_CORE, 1.0f };
	for ( int r = 0; r < 2; r++ )
	{
		for ( int i = 0; i <= GLINT_SIDES; i++ )
		{
			float flAngle = M_PI_F * i / GLINT_SIDES;
			vecPos[r][i] = vecMiddle + ( vecSide * ( cos( flAngle ) * flWide ) + vecOn * ( sin( flAngle ) * flLong ) ) * flOut[r];
		}
	}

	Vector vecHot( MIN( vecLight.x * flShine, 1.0f ), MIN( vecLight.y * flShine, 1.0f ), MIN( vecLight.z * flShine, 1.0f ) );
	Vector vecGlow = vecHot * TUBE_SHINE_GLOW;

	CMatRenderContextPtr pRenderContext( materials );
	IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, pMaterial );

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, GLINT_SIDES * 3 );

	for ( int i = 0; i < GLINT_SIDES; i++ )
	{
		// Corners: which ring, which side, how bright (0 hot, 1 glow, 2 none)
		static const int iCorner[9][3] = {
			{ -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 },
			{ 0, 0, 1 }, { 1, 0, 2 }, { 1, 1, 2 },
			{ 0, 0, 1 }, { 1, 1, 2 }, { 0, 1, 1 } };

		for ( int k = 0; k < 9; k++ )
		{
			const Vector &vecAt = ( iCorner[k][0] < 0 ) ? vecMiddle : vecPos[iCorner[k][0]][i + iCorner[k][1]];
			Vector vecBright = ( iCorner[k][2] == 0 ) ? vecHot : ( ( iCorner[k][2] == 1 ) ? vecGlow : vec3_origin );

			meshBuilder.Position3fv( vecAt.Base() );
			meshBuilder.Color4f( vecBright.x, vecBright.y, vecBright.z, 1.0f );
			meshBuilder.TexCoord2f( 0, 0.5f, 0.5f );
			meshBuilder.AdvanceVertex();
		}
	}

	meshBuilder.End();
	pMesh->Draw();
}

//-----------------------------------------------------------------------------
// The strip, TUBE_COLUMNS points across, and the circles of the blob at its
// end (pDiscs: which points along it they sit on)
//-----------------------------------------------------------------------------
static void DrawTube( IMaterial *pMaterial, const Vector *pPos, const Vector *pColor, const float ( *pBox )[6], const float *pTexCoord, const float *pRadius,
	int nCount, const OF2CurveStyle_t &style, const int *pDiscs, int nDiscs )
{
	static Vector s_vecPos[CURVE_MAX_SEGS][TUBE_COLUMNS];
	static Vector s_vecShade[CURVE_MAX_SEGS][TUBE_COLUMNS];
	static Vector s_vecSide[CURVE_MAX_SEGS];
	static Vector s_vecView[CURVE_MAX_SEGS];
	static float s_flAcross[CURVE_MAX_SEGS][TUBE_COLUMNS];
	// (the way each point faces: what a material with an $envmap mirrors by)
	static Vector s_vecNormal[CURVE_MAX_SEGS][TUBE_COLUMNS];

	const Vector &vecEye = CurrentViewOrigin();

	// The highlight's half-width, and how far off the middle of the strip it can
	// go and still fit
	float flShineEdge = clamp( style.flShineWidth, 0.1f, 0.6f );
	float flShineLimit = TUBE_SHOULDER - 0.05f - flShineEdge;

	Vector vecLastDir( 1, 0, 0 );
	Vector vecLastSide( 0, 0, 1 );

	for ( int i = 0; i < nCount; i++ )
	{
		// The way it runs here, the way to the eye, and across both: the
		// strip is laid out to the sides, facing the eye
		Vector vecDir = pPos[MIN( i + 1, nCount - 1 )] - pPos[MAX( i - 1, 0 )];
		if ( VectorNormalize( vecDir ) < 0.001f )
		{
			vecDir = vecLastDir;
		}
		vecLastDir = vecDir;

		Vector vecView = vecEye - pPos[i];
		VectorNormalize( vecView );

		Vector vecSide = CrossProduct( vecDir, vecView );
		if ( VectorNormalize( vecSide ) < 0.01f )
		{
			// Seen end on
			vecSide = vecLastSide;
		}
		vecLastSide = vecSide;

		// The middle of the strip is the part of the tube that faces the eye most
		Vector vecFront = CrossProduct( vecSide, vecDir );
		if ( VectorNormalize( vecFront ) < 0.01f )
		{
			vecFront = vecView;
		}

		// Something wet shines where it mirrors the light, not where it faces
		// the eye: the highlight sits on the side the light comes from. That is
		// found from how much brighter the place is from one side than from the
		// others; lit evenly, it stays in the middle.
		float flPeak = 0.0f;
		if ( style.flShineFollow > 0.0f )
		{
			const float *pSides = pBox[i];
			Vector vecLight( pSides[0] - pSides[1], pSides[2] - pSides[3], pSides[4] - pSides[5] );
			float flAll = ( pSides[0] + pSides[1] + pSides[2] + pSides[3] + pSides[4] + pSides[5] ) / 6.0f;
			float flOneSided = VectorNormalize( vecLight );
			if ( flAll > 0.0001f && flOneSided > 0.0001f )
			{
				float flPull = MIN( flOneSided / ( flAll * 3.0f ), 1.0f ) * MIN( style.flShineFollow, 1.0f );
				Vector vecHalf = vecView + vecLight * flPull;
				float flSideways = DotProduct( vecHalf, vecSide );
				float flFacing = MAX( DotProduct( vecHalf, vecFront ), 0.01f );
				flPeak = flSideways / sqrt( flSideways * flSideways + flFacing * flFacing );
			}
		}
		flPeak = clamp( flPeak, -flShineLimit, flShineLimit );

		// How far each point across is from the middle towards the edge
		const float flTubeAcross[TUBE_COLUMNS] = { -1.0f, -TUBE_SHOULDER,
			flPeak - flShineEdge, flPeak - flShineEdge * TUBE_SHINE_CORE, flPeak, flPeak + flShineEdge * TUBE_SHINE_CORE, flPeak + flShineEdge,
			TUBE_SHOULDER, 1.0f };

		for ( int c = 0; c < TUBE_COLUMNS; c++ )
		{
			float flAcross = flTubeAcross[c];
			s_flAcross[i][c] = flAcross;
			Vector vecNormal = vecFront * sqrt( 1.0f - flAcross * flAcross ) + vecSide * flAcross;

			s_vecPos[i][c] = pPos[i] + vecSide * ( flAcross * pRadius[i] );
			s_vecNormal[i][c] = vecNormal;
			s_vecShade[i][c] = ShadeTube( pColor[i], pBox[i], vecNormal, fabs( flAcross ), style );
		}

		s_vecSide[i] = vecSide;
		s_vecView[i] = vecView;
	}

	// (in runs: a long tongue drawn finely is more than one mesh may hold)
	for ( int iFrom = 0; iFrom < nCount - 1; iFrom += TUBE_RUN )
	{
		int nRun = MIN( TUBE_RUN, nCount - 1 - iFrom );

		CMatRenderContextPtr pRenderContext( materials );
		IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, pMaterial );

		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, nRun * ( TUBE_COLUMNS - 1 ) );

		static const int iCorner[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
		for ( int i = iFrom; i < iFrom + nRun; i++ )
		{
			for ( int c = 0; c < TUBE_COLUMNS - 1; c++ )
			{
				for ( int k = 0; k < 4; k++ )
				{
					int iAlong = i + iCorner[k][0];
					int iAcross = c + iCorner[k][1];
					const Vector &vecShade = s_vecShade[iAlong][iAcross];

					meshBuilder.Position3fv( s_vecPos[iAlong][iAcross].Base() );
					meshBuilder.Normal3fv( s_vecNormal[iAlong][iAcross].Base() );
					meshBuilder.Color4f( vecShade.x, vecShade.y, vecShade.z, 1.0f );
					meshBuilder.TexCoord2f( 0, 0.5f + 0.5f * s_flAcross[iAlong][iAcross], pTexCoord[iAlong] );
					meshBuilder.AdvanceVertex();
				}
			}
		}

		meshBuilder.End();
		pMesh->Draw();
	}

	// A bright streak reads as a wet, round surface. It is added on over the
	// four middle strips of the very same points as the tube under it: a hot
	// line, a glow either side of it, nothing where those strips end. It may
	// be asked for brighter than the light there, and then burns out to white.
	// (As a strip of its own, with points of its own, it lay a hair in front
	// of the tube in some places and behind it in others, and flickered.)
	if ( style.pShineMaterial && style.flShine > 0.0f )
	{
		CMatRenderContextPtr pRenderContext( materials );
		IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, style.pShineMaterial );

		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, ( nCount - 1 ) * 4 );

		static const int iCorner[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
		for ( int i = 0; i < nCount - 1; i++ )
		{
			for ( int c = TUBE_MIDDLE - 2; c <= TUBE_MIDDLE + 1; c++ )
			{
				for ( int k = 0; k < 4; k++ )
				{
					int iAlong = i + iCorner[k][0];
					int iAcross = c + iCorner[k][1];
					int nOff = abs( iAcross - TUBE_MIDDLE );
					float flBright = ( nOff == 0 ) ? style.flShine : ( ( nOff == 1 ) ? style.flShine * TUBE_SHINE_GLOW : 0.0f );
					const Vector &vecLight = pColor[iAlong];

					meshBuilder.Position3fv( s_vecPos[iAlong][iAcross].Base() );
					meshBuilder.Color4f( MIN( vecLight.x * flBright, 1.0f ), MIN( vecLight.y * flBright, 1.0f ), MIN( vecLight.z * flBright, 1.0f ), 1.0f );
					meshBuilder.TexCoord2f( 0, 0.5f, 0.5f );
					meshBuilder.AdvanceVertex();
				}
			}
		}

		meshBuilder.End();
		pMesh->Draw();
	}

	for ( int i = 0; i < nDiscs; i++ )
	{
		int iAt = pDiscs[i];
		DrawDisc( pMaterial, pPos[iAt], pRadius[iAt], s_vecView[iAt], s_vecSide[iAt], pColor[iAt], pBox[iAt], pTexCoord[iAt], style );

		// The wet streak ends round on it too
		if ( style.pShineMaterial && style.flShine > 0.0f && iAt > 0 )
		{
			DrawGlint( style.pShineMaterial, pPos[iAt], pRadius[iAt], s_vecView[iAt], s_vecSide[iAt], pPos[iAt] - pPos[iAt - 1],
				pColor[iAt], s_flAcross[iAt][TUBE_MIDDLE], flShineEdge, style.flShine );
		}
	}
}

void OF2_DrawCurve( const Vector *pPoints, const bool *pBend, int nPoints, const OF2CurveStyle_t &style )
{
	nPoints = MIN( nPoints, OF2_CURVE_MAX_POINTS );
	if ( nPoints < 2 || style.pMaterial == NULL )
		return;

	// Where it goes over an edge it doesn't turn on the spot: it goes round.
	// Each such point is swapped for a short curve that leaves the straight
	// run coming in, passes through the point itself, and joins the straight
	// run going out, both without a kink. Through the point, not inside it:
	// the point stands just clear of the edge, and a curve that cut the corner
	// would dip into what the edge belongs to. This one swings wide instead.
	Vector vecRounded[OF2_CURVE_MAX_POINTS];
	bool bRounded[OF2_CURVE_MAX_POINTS];
	if ( pBend && style.flBendRadius > 0.0f )
	{
		int nOut = 0;
		for ( int i = 0; i < nPoints; i++ )
		{
			// (as long as there is room left for it and for the points still to come)
			bool bRoom = nOut + CURVE_BEND_PIECES + 1 + ( nPoints - 1 - i ) <= OF2_CURVE_MAX_POINTS;
			float flReach = 0.0f;
			Vector vecIn, vecOut;
			if ( pBend[i] && i > 0 && i < nPoints - 1 && bRoom )
			{
				vecIn = pPoints[i] - pPoints[i - 1];
				vecOut = pPoints[i + 1] - pPoints[i];
				float flIn = VectorNormalize( vecIn );
				float flOut = VectorNormalize( vecOut );

				// Less than half of either run, so two bends can share one
				flReach = MIN( style.flBendRadius, MIN( flIn, flOut ) * 0.45f );
			}

			if ( flReach < 0.5f )
			{
				vecRounded[nOut] = pPoints[i];
				bRounded[nOut] = pBend[i];
				nOut++;
				continue;
			}

			Vector vecFrom = pPoints[i] - vecIn * flReach;
			Vector vecTo = pPoints[i] + vecOut * flReach;
			Vector vecPull1 = pPoints[i] + vecIn * ( flReach / 3.0f );
			Vector vecPull2 = pPoints[i] - vecOut * ( flReach / 3.0f );

			for ( int k = 0; k <= CURVE_BEND_PIECES; k++ )
			{
				float t = (float)k / CURVE_BEND_PIECES;
				float s = 1.0f - t;

				vecRounded[nOut] = vecFrom * ( s * s * s ) + vecPull1 * ( 3.0f * s * s * t ) + vecPull2 * ( 3.0f * s * t * t ) + vecTo * ( t * t * t );
				bRounded[nOut] = true;
				nOut++;
			}
		}

		pPoints = vecRounded;
		pBend = bRounded;
		nPoints = nOut;
	}

	// The materials are unlit, so the light at each point is put into the color.
	// For a tube, also how bright it is from each side.
	Vector vecPointColor[OF2_CURVE_MAX_POINTS];
	float flPointBox[OF2_CURVE_MAX_POINTS][6];
	float flMinLight = clamp( style.flMinLight, 0.0f, 1.0f );
	for ( int i = 0; i < nPoints; i++ )
	{
		Vector vecLight;
		Vector vecBox[6];
		engine->ComputeLighting( pPoints[i], NULL, true, vecLight, vecBox );

		for ( int j = 0; j < 3; j++ )
		{
			vecPointColor[i][j] = clamp( LinearToGamma( vecLight[j] ), flMinLight, 1.0f ) * style.vecTint[j];
		}

		for ( int j = 0; j < 6; j++ )
		{
			flPointBox[i][j] = MAX( vecBox[j].x * 0.30f + vecBox[j].y * 0.59f + vecBox[j].z * 0.11f, 0.0f );
		}
	}

	bool bTip = style.bTube && style.flTipLength > 0.0f && style.flTipRadius >= style.flWidth * 0.5f;

	// A curve through the points. Next to a bend it runs straight instead.
	static Vector vecPos[CURVE_MAX_SEGS];
	static Vector vecColor[CURVE_MAX_SEGS];
	static float flBox[CURVE_MAX_SEGS][6];
	static float flAlong[CURVE_MAX_SEGS];
	static float flTexCoord[CURVE_MAX_SEGS];
	static float flRadius[CURVE_MAX_SEGS];

	int nSmooth = clamp( style.nSmooth, 1, CURVE_MAX_SMOOTH );
	int nCount = 0;

	for ( int i = 0; i < nPoints - 1; i++ )
	{
		bool bStraight = pBend && ( pBend[i] || pBend[i + 1] );
		int nPieces = bStraight ? 1 : nSmooth;

		if ( bTip && i == nPoints - 2 )
		{
			// Enough pieces for the blob's swell to come out round
			int nWanted = (int)( pPoints[i].DistTo( pPoints[i + 1] ) / ( style.flTipLength / 8.0f ) ) + 1;
			nPieces = clamp( nWanted, nPieces, CURVE_TIP_PIECES );
		}

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
			for ( int k = 0; k < 6; k++ )
			{
				flBox[nCount][k] = flPointBox[i][k] + ( flPointBox[i + 1][k] - flPointBox[i][k] ) * t;
			}
			nCount++;
		}
	}

	vecPos[nCount] = pPoints[nPoints - 1];
	vecColor[nCount] = vecPointColor[nPoints - 1];
	for ( int k = 0; k < 6; k++ )
	{
		flBox[nCount][k] = flPointBox[nPoints - 1][k];
	}
	nCount++;

	// The texture runs from the first point
	float flLength = 0.0f;
	flAlong[0] = 0.0f;
	for ( int i = 1; i < nCount; i++ )
	{
		flLength += vecPos[i].DistTo( vecPos[i - 1] );
		flAlong[i] = flLength;
	}

	float flScale = ( style.flTextureRepeat > 0.0f ) ? 1.0f / style.flTextureRepeat : ( ( flLength > 0.0f ) ? 1.0f / flLength : 0.0f );
	for ( int i = 0; i < nCount; i++ )
	{
		flTexCoord[i] = flAlong[i] * flScale;

		// One copy over the whole length: kept off the texture's two ends. Right
		// at them it is drawn mixed with the other end (the texture repeats),
		// which left the round ending paler than the tongue it ends.
		if ( style.flTextureRepeat <= 0.0f )
		{
			flTexCoord[i] = CURVE_TEXTURE_INSET + flTexCoord[i] * ( 1.0f - 2.0f * CURVE_TEXTURE_INSET );
		}
	}

	// Half its width everywhere, but for the blob: over the last stretch it
	// swells, gently at first and levelling off into the blob's own size
	for ( int i = 0; i < nCount; i++ )
	{
		flRadius[i] = style.flWidth * 0.5f;

		float flFromEnd = flLength - flAlong[i];
		if ( bTip && flFromEnd < style.flTipLength )
		{
			float flSwell = 1.0f - flFromEnd / style.flTipLength;
			flSwell = flSwell * flSwell * ( 3.0f - 2.0f * flSwell );
			flRadius[i] += ( style.flTipRadius - flRadius[i] ) * flSwell;
		}
	}

	if ( style.bTube )
	{
		// One circle, at the very end: the round ending. (More of them along
		// the swell showed up as separate balls: a disc faces the view, the
		// strip runs through it at a slant, and half of each disc came out in
		// front of the strip.)
		int iDisc = nCount - 1;
		DrawTube( style.pMaterial, vecPos, vecColor, flBox, flTexCoord, flRadius, nCount, style, &iDisc, bTip ? 1 : 0 );
	}
	else
	{
		DrawRibbon( style.pMaterial, vecPos, vecColor, flTexCoord, flRadius, nCount, 1.0f, 1.0f );
	}

	// (a tube has put its highlight on itself)
	if ( !style.bTube && style.pShineMaterial && style.flShine > 0.0f )
	{
		DrawRibbon( style.pShineMaterial, vecPos, vecColor, flTexCoord, flRadius, nCount, style.flShineWidth, style.flShine );
	}
}
