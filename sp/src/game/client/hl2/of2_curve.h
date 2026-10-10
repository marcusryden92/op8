//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: draws a rope-like thing (the Barnacle's tongue, func_climbrope)
//			as one smooth ribbon through a row of points, lit by the light
//			where it is.
//
//=============================================================================//

#ifndef OF2_CURVE_H
#define OF2_CURVE_H
#ifdef _WIN32
#pragma once
#endif

#define OF2_CURVE_MAX_POINTS	512

struct OF2CurveStyle_t
{
	OF2CurveStyle_t()
	{
		pMaterial = NULL;
		pShineMaterial = NULL;
		flWidth = 1.0f;
		vecTint.Init( 1, 1, 1 );
		flTextureRepeat = 0.0f;
		nSmooth = 4;
		flShine = 0.0f;
		flShineWidth = 0.5f;
		flShineFollow = 0.0f;
		flMinLight = 0.25f;
		bTube = false;
		flRound = 0.5f;
		flSideLight = 1.0f;
		flTipRadius = 0.0f;
		flTipLength = 0.0f;
		flTipShade = 1.0f;
		flBendRadius = 0.0f;
		flDrawLength = 0.0f;
	}

	IMaterial	*pMaterial;
	IMaterial	*pShineMaterial;	// drawn over the middle, added on; NULL for none
	float		flWidth;
	Vector		vecTint;

	// One copy of the texture every this many units, counted from the first
	// point. 0 stretches one copy over the whole length.
	float		flTextureRepeat;

	// Pieces between two points; 1 is straight lines
	int			nSmooth;

	float		flShine;
	float		flShineWidth;		// share of flWidth
	// The highlight sits towards the side the light comes from (1), or always
	// down the middle (0). Only with bTube.
	float		flShineFollow;
	float		flMinLight;			// it never gets darker than this

	// Shaded across its width as the round thing it is, instead of flat:
	// darker towards the edges (flRound: 0 not at all, 1 down to black), and
	// each side brighter or darker with the light that falls on it from there
	// (flSideLight: 0 not at all, 1 fully).
	bool		bTube;
	float		flRound;
	float		flSideLight;

	// The last point is the middle of a blob this big: over the last
	// flTipLength the thing swells to it, and it ends round. 0 for none.
	// Only with bTube.
	float		flTipRadius;
	float		flTipLength;
	// The round ending is this much darker (under 1) or brighter than the rest
	float		flTipShade;

	// Where it goes over an edge (pBend) it is drawn going round, starting and
	// ending this far either side of the point. 0 turns on the spot.
	float		flBendRadius;

	// Only this much of it is drawn, from the first point, looking as that
	// part of the whole does. 0 draws all of it.
	float		flDrawLength;
};

// pBend marks the points where it goes over an edge: the curve runs straight
// either side of them, so it doesn't round the corner off through the edge.
// pBend can be NULL.
void OF2_DrawCurve( const Vector *pPoints, const bool *pBend, int nPoints, const OF2CurveStyle_t &style );

#endif // OF2_CURVE_H
