//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "hud_numericdisplay.h"
#include "iclientmode.h"

#include <Color.h>
#include <KeyValues.h>
#include <vgui/ISurface.h>
#include <vgui/ISystem.h>
#include <vgui/IVGui.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudNumericDisplay::CHudNumericDisplay(vgui::Panel *parent, const char *name) : BaseClass(parent, name)
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_iValue = 0;
	m_LabelText[0] = 0;
	m_iSecondaryValue = 0;
	m_bDisplayValue = true;
	m_bDisplaySecondaryValue = false;
	m_bIndent = false;
	m_bIsTime = false;

	m_nIconTexture = -1;
	m_nIconGlowTexture = -1;
	V_memset( m_IconKey, 0, sizeof( m_IconKey ) );
	m_iIconX = m_iIconY = m_iIconWide = m_iIconTall = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Resets values on restore/new map
//-----------------------------------------------------------------------------
void CHudNumericDisplay::Reset()
{
	m_flBlur = 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetDisplayValue(int value)
{
	m_iValue = value;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetSecondaryValue(int value)
{
	m_iSecondaryValue = value;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetShouldDisplayValue(bool state)
{
	m_bDisplayValue = state;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetShouldDisplaySecondaryValue(bool state)
{
	m_bDisplaySecondaryValue = state;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetLabelText(const wchar_t *text)
{
	wcsncpy(m_LabelText, text, sizeof(m_LabelText) / sizeof(wchar_t));
	m_LabelText[(sizeof(m_LabelText) / sizeof(wchar_t)) - 1] = 0;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetIndent(bool state)
{
	m_bIndent = state;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetIsTime(bool state)
{
	m_bIsTime = state;
}

//-----------------------------------------------------------------------------
// Purpose: paints a number at the specified position
//-----------------------------------------------------------------------------
void CHudNumericDisplay::PaintNumbers(HFont font, int xpos, int ypos, int value)
{
	surface()->DrawSetTextFont(font);
	wchar_t unicode[6];
	if ( !m_bIsTime )
	{
		V_snwprintf(unicode, ARRAYSIZE(unicode), L"%d", value);
	}
	else
	{
		int iMinutes = value / 60;
		int iSeconds = value - iMinutes * 60;
#ifdef PORTAL
		// portal uses a normal font for numbers so we need the seperate to be a renderable ':' char
		if ( iSeconds < 10 )
			V_snwprintf( unicode, ARRAYSIZE(unicode), L"%d:0%d", iMinutes, iSeconds );
		else
			V_snwprintf( unicode, ARRAYSIZE(unicode), L"%d:%d", iMinutes, iSeconds );		
#else
		if ( iSeconds < 10 )
			V_snwprintf( unicode, ARRAYSIZE(unicode), L"%d`0%d", iMinutes, iSeconds );
		else
			V_snwprintf( unicode, ARRAYSIZE(unicode), L"%d`%d", iMinutes, iSeconds );
#endif
	}

	// adjust the position to take into account 3 characters
	int charWidth = surface()->GetCharacterWidth(font, '0');
	if (value < 100 && m_bIndent)
	{
		xpos += charWidth;
	}
	if (value < 10 && m_bIndent)
	{
		xpos += charWidth;
	}

	surface()->DrawSetTextPos(xpos, ypos);
	surface()->DrawUnicodeString( unicode );
}

//-----------------------------------------------------------------------------
// Purpose: draws the text
//-----------------------------------------------------------------------------
void CHudNumericDisplay::PaintLabel( void )
{
	surface()->DrawSetTextFont(m_hTextFont);
	surface()->DrawSetTextColor(GetFgColor());
	surface()->DrawSetTextPos(text_xpos, text_ypos);
	surface()->DrawUnicodeString( m_LabelText );
}

//-----------------------------------------------------------------------------
// OF2 icons: shapes are rasterized into horizontal pixel spans, so they can be
// drawn with scanlines and filled with segments like the crosshair brackets
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// OF2 backdrop: one small texture of a soft rounded rectangle (black, alpha
// fading out toward the edges), stretched over the rect with filtering on
//-----------------------------------------------------------------------------
#define BACKDROP_TEXTURE_SIZE	64

void OF2_PaintBackdrop( int x0, int y0, int x1, int y1, float flAlpha )
{
	static int s_nTexture = -1;
	if ( s_nTexture == -1 || !surface()->IsTextureIDValid( s_nTexture ) )
	{
		unsigned char rgba[BACKDROP_TEXTURE_SIZE * BACKDROP_TEXTURE_SIZE * 4];
		for ( int y = 0; y < BACKDROP_TEXTURE_SIZE; y++ )
		{
			for ( int x = 0; x < BACKDROP_TEXTURE_SIZE; x++ )
			{
				// Distance from the center in a rounded-square metric (superellipse, p = 4)
				const float u = fabsf( ( x + 0.5f ) / BACKDROP_TEXTURE_SIZE * 2.0f - 1.0f );
				const float v = fabsf( ( y + 0.5f ) / BACKDROP_TEXTURE_SIZE * 2.0f - 1.0f );
				const float d = powf( u * u * u * u + v * v * v * v, 0.25f );

				// Solid in the middle, smooth fade to nothing at the edges
				const float t = clamp( ( d - 0.35f ) / ( 0.98f - 0.35f ), 0.0f, 1.0f );
				const float a = 1.0f - t * t * ( 3.0f - 2.0f * t );

				unsigned char *p = &rgba[( y * BACKDROP_TEXTURE_SIZE + x ) * 4];
				p[0] = p[1] = p[2] = 0;
				p[3] = (unsigned char)( a * 255.0f );
			}
		}
		s_nTexture = surface()->CreateNewTextureID( true );
		surface()->DrawSetTextureRGBA( s_nTexture, rgba, BACKDROP_TEXTURE_SIZE, BACKDROP_TEXTURE_SIZE, true, true );
	}

	surface()->DrawSetColor( Color( 255, 255, 255, (unsigned char)clamp( flAlpha, 0.0f, 255.0f ) ) );
	surface()->DrawSetTexture( s_nTexture );
	surface()->DrawTexturedRect( x0, y0, x1, y1 );
}

#define ICON_MAX_POINTS		32
#define ICON_ARC_STEPS		4		// segments per rounded corner
#define ICON_MAX_ROUNDED	( ICON_MAX_POINTS * ( ICON_ARC_STEPS + 1 ) )
#define ICON_MAX_SPANS		16
#define ICON_SCANLINE_DIM	0.7f	// dimmed rows' share of the alpha, like the fonts' "scanlines" "2"
#define ICON_UNLIT_ALPHA	0.2f	// unlit segments' share of the alpha

struct IconSpan_t
{
	float x0, x1;
};

// Spans inside a closed polygon along the horizontal line at y (even-odd rule), left to right
static int IconPolySpans( const Vector2D *pPoints, int nPoints, float y, IconSpan_t *pSpans )
{
	float xs[ICON_MAX_ROUNDED];
	int nx = 0;
	for ( int i = 0; i < nPoints; i++ )
	{
		const Vector2D &a = pPoints[i], &b = pPoints[( i + 1 ) % nPoints];
		if ( ( a.y <= y && y < b.y ) || ( b.y <= y && y < a.y ) )
		{
			xs[nx++] = a.x + ( y - a.y ) * ( b.x - a.x ) / ( b.y - a.y );
		}
	}

	for ( int i = 1; i < nx; i++ ) // insertion sort, a handful of values
	{
		float x = xs[i];
		int j = i - 1;
		for ( ; j >= 0 && xs[j] > x; j-- )
			xs[j + 1] = xs[j];
		xs[j + 1] = x;
	}

	int nSpans = 0;
	for ( int i = 0; i + 1 < nx && nSpans < ICON_MAX_SPANS; i += 2 )
	{
		pSpans[nSpans].x0 = xs[i];
		pSpans[nSpans].x1 = xs[i + 1];
		nSpans++;
	}
	return nSpans;
}

// Parts of a that are also in b
static int IconIntersectSpans( const IconSpan_t *a, int na, const IconSpan_t *b, int nb, IconSpan_t *pOut )
{
	int n = 0;
	for ( int i = 0; i < na; i++ )
	{
		for ( int j = 0; j < nb && n < ICON_MAX_SPANS; j++ )
		{
			float x0 = MAX( a[i].x0, b[j].x0 ), x1 = MIN( a[i].x1, b[j].x1 );
			if ( x1 > x0 )
			{
				pOut[n].x0 = x0;
				pOut[n].x1 = x1;
				n++;
			}
		}
	}
	return n;
}

// Parts of a that are not in b (both sorted left to right)
static int IconSubtractSpans( const IconSpan_t *a, int na, const IconSpan_t *b, int nb, IconSpan_t *pOut )
{
	int n = 0;
	for ( int i = 0; i < na; i++ )
	{
		float x = a[i].x0;
		for ( int j = 0; j < nb && n < ICON_MAX_SPANS; j++ )
		{
			if ( b[j].x1 <= x || b[j].x0 >= a[i].x1 )
				continue;
			if ( b[j].x0 > x )
			{
				pOut[n].x0 = x;
				pOut[n].x1 = b[j].x0;
				n++;
			}
			x = MAX( x, b[j].x1 );
		}
		if ( x < a[i].x1 && n < ICON_MAX_SPANS )
		{
			pOut[n].x0 = x;
			pOut[n].x1 = a[i].x1;
			n++;
		}
	}
	return n;
}

// The outline moved inward by flDist, with mitered corners
// Replaces each corner with an arc. flInset is how far this outline is inset from
// the outline that has radius flRadius: outward corners get flRadius - flInset
// (down to sharp) and inward corners flRadius + flInset, so a stroke between two
// such outlines keeps its thickness around the bend. Returns the new point count.
static int IconRoundPolygon( const Vector2D *pPoints, int nPoints, float flRadius, float flInset, Vector2D *pOut )
{
	float flArea = 0.0f;
	for ( int i = 0; i < nPoints; i++ )
	{
		const Vector2D &a = pPoints[i], &b = pPoints[( i + 1 ) % nPoints];
		flArea += a.x * b.y - b.x * a.y;
	}
	const float flSign = ( flArea > 0.0f ) ? 1.0f : -1.0f;

	int nOut = 0;
	for ( int i = 0; i < nPoints; i++ )
	{
		const Vector2D &p = pPoints[i];
		const Vector2D &prev = pPoints[( i + nPoints - 1 ) % nPoints];
		const Vector2D &next = pPoints[( i + 1 ) % nPoints];

		Vector2D u1 = prev - p, u2 = next - p;
		const float flLen1 = Vector2DNormalize( u1 ), flLen2 = Vector2DNormalize( u2 );
		const float flCross = ( p.x - prev.x ) * ( next.y - p.y ) - ( p.y - prev.y ) * ( next.x - p.x );
		const bool bConvex = flCross * flSign > 0.0f;

		float flR = bConvex ? flRadius - flInset : flRadius + flInset;
		const float flHalf = 0.5f * acosf( clamp( u1.Dot( u2 ), -1.0f, 1.0f ) ); // half the angle between the edges
		if ( flR < 0.25f || flLen1 < 0.01f || flLen2 < 0.01f || flHalf > 0.5f * M_PI_F - 0.01f )
		{
			pOut[nOut++] = p; // sharp, or no corner at all
			continue;
		}

		// Tangent points, kept within half of each edge
		float t = flR / tanf( flHalf );
		const float flMaxT = 0.5f * MIN( flLen1, flLen2 );
		if ( t > flMaxT )
		{
			t = flMaxT;
			flR = t * tanf( flHalf );
		}

		Vector2D bisect = u1 + u2;
		Vector2DNormalize( bisect );
		const Vector2D center = p + bisect * ( flR / sinf( flHalf ) );
		const Vector2D a1 = p + u1 * t, a2 = p + u2 * t;

		const float flStart = atan2f( a1.y - center.y, a1.x - center.x );
		float flSweep = atan2f( a2.y - center.y, a2.x - center.x ) - flStart;
		while ( flSweep > M_PI_F )	flSweep -= 2.0f * M_PI_F;
		while ( flSweep < -M_PI_F )	flSweep += 2.0f * M_PI_F;

		for ( int k = 0; k <= ICON_ARC_STEPS; k++ )
		{
			const float flAngle = flStart + flSweep * k / ICON_ARC_STEPS;
			pOut[nOut++].Init( center.x + flR * cosf( flAngle ), center.y + flR * sinf( flAngle ) );
		}
	}
	return nOut;
}

static void IconInsetPolygon( const Vector2D *pPoints, int nPoints, float flDist, Vector2D *pOut )
{
	float flArea = 0.0f;
	for ( int i = 0; i < nPoints; i++ )
	{
		const Vector2D &a = pPoints[i], &b = pPoints[( i + 1 ) % nPoints];
		flArea += a.x * b.y - b.x * a.y;
	}
	const float flSign = ( flArea > 0.0f ) ? 1.0f : -1.0f; // makes the normals point inward

	for ( int i = 0; i < nPoints; i++ )
	{
		Vector2D e1 = pPoints[i] - pPoints[( i + nPoints - 1 ) % nPoints];
		Vector2D e2 = pPoints[( i + 1 ) % nPoints] - pPoints[i];
		Vector2DNormalize( e1 );
		Vector2DNormalize( e2 );
		Vector2D n1( -e1.y * flSign, e1.x * flSign );
		Vector2D n2( -e2.y * flSign, e2.x * flSign );
		float flDenom = MAX( 0.25f, 1.0f + n1.Dot( n2 ) ); // limit very sharp miters
		pOut[i] = pPoints[i] + ( n1 + n2 ) * ( flDist / flDenom );
	}
}

#define ICON_SUBSAMPLES		4		// vertical samples per pixel row for anti-aliasing (horizontal is exact)
#define ICON_GLOW_GAIN		1.6f	// the blur spreads the light thin; this brings the halo back up

static int IconPosMod( int a, int b )
{
	int m = a % b;
	return ( m < 0 ) ? m + b : m;
}

// Adds the coverage of spans (on one sub-row) to a row of the coverage buffer
static void IconAccumulateSpans( const IconSpan_t *pSpans, int nSpans, float flWeight, float *pRow, int originX, int wide )
{
	for ( int s = 0; s < nSpans; s++ )
	{
		const float x0 = pSpans[s].x0 - originX, x1 = pSpans[s].x1 - originX;
		for ( int px = MAX( 0, (int)floor( x0 ) ); px < MIN( wide, (int)ceil( x1 ) ); px++ )
		{
			const float flCover = MIN( x1, px + 1.0f ) - MAX( x0, (float)px );
			if ( flCover > 0.0f )
				pRow[px] += flCover * flWeight;
		}
	}
}

// Box blur along rows (step 1) or columns (step = wide), radius r, in place
static void IconBoxBlur( float *pData, int wide, int tall, int r, bool bColumns )
{
	const int nLines = bColumns ? wide : tall;
	const int nLen = bColumns ? tall : wide;
	const int step = bColumns ? wide : 1;
	CUtlVector<float> line;
	line.SetCount( nLen );

	for ( int l = 0; l < nLines; l++ )
	{
		float *pLine = bColumns ? pData + l : pData + l * wide;
		for ( int i = 0; i < nLen; i++ )
			line[i] = pLine[i * step];

		for ( int i = 0; i < nLen; i++ )
		{
			float flSum = 0.0f;
			for ( int k = -r; k <= r; k++ )
			{
				int j = i + k;
				if ( j >= 0 && j < nLen )
					flSum += line[j];
			}
			pLine[i * step] = flSum / ( 2 * r + 1 );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: OF2: icon outline plus segments inside it, lit bottom-up to flFill,
//			with a glow. The shape is rasterized (anti-aliased, scanlined) into a
//			texture, and a blurred copy into a second one; both are rebuilt only
//			when something they show changes. The color and glow strength are
//			applied when drawing, so animations cost nothing.
//-----------------------------------------------------------------------------
void CHudNumericDisplay::PaintIcon( const Vector2D *pPoints, int nPoints, float flAspect, float flFill, Color clr )
{
	if ( nPoints < 3 || nPoints > ICON_MAX_POINTS )
		return;

	// Icon box: from the layout, or matched to the digits (on their baseline, digit height)
	float flTall = icon_tall;
	float flTop = icon_ypos;
	if ( flTall <= 0.0f )
	{
		flTall = m_flIconDigitRatio * surface()->GetFontTall( m_hNumberFont );
		flTop = digit_ypos + surface()->GetFontAscent( m_hNumberFont, L'0' ) - flTall;
	}
	const float flWide = flTall * flAspect;

	// Same segment sizes as the crosshair brackets
	const float flScale = ScreenHeight() / 480.0f;
	const int stroke   = MAX( 1, (int)( icon_stroke + 0.5f ) );
	const int segThick = MAX( 1, (int)( 0.5f * flScale + 0.5f ) );
	const int segGap   = MAX( 1, (int)( 0.25f * flScale + 0.5f ) );
	const int rimGap   = segGap;
	const int pitch    = segThick + segGap;
	const int inset    = stroke + rimGap; // outline edge to segment area
	const int glowRadius = MAX( 1, (int)( icon_glow_radius + 0.5f ) );

	// Outline snapped to whole pixels so straight edges stay crisp
	Vector2D outer[ICON_MAX_POINTS], inner[ICON_MAX_POINTS], area[ICON_MAX_POINTS];
	for ( int i = 0; i < nPoints; i++ )
	{
		outer[i].Init( (int)( icon_xpos + pPoints[i].x * flWide + 0.5f ), (int)( flTop + pPoints[i].y * flTall + 0.5f ) );
	}

	// The segment stack builds up from the bottom of the segment area
	IconInsetPolygon( outer, nPoints, inset, area );
	float flAreaMaxY = -FLT_MAX;
	for ( int i = 0; i < nPoints; i++ )
		flAreaMaxY = MAX( flAreaMaxY, area[i].y );
	const int stackBottom = (int)floor( flAreaMaxY ); // bottom edge of the lowest segment

	// Move each horizontal edge (by less than a pitch, into the shape) so it sits
	// exactly one rim gap from the nearest segment, like the bracket ends
	{
		float flArea = 0.0f;
		for ( int i = 0; i < nPoints; i++ )
		{
			const Vector2D &a = outer[i], &b = outer[( i + 1 ) % nPoints];
			flArea += a.x * b.y - b.x * a.y;
		}
		const float flSign = ( flArea > 0.0f ) ? 1.0f : -1.0f;

		int shift[ICON_MAX_POINTS] = {};
		for ( int i = 0; i < nPoints; i++ )
		{
			const int j = ( i + 1 ) % nPoints;
			if ( outer[i].y != outer[j].y || outer[i].x == outer[j].x )
				continue;

			const int y = (int)outer[i].y;
			const bool bInsideBelow = ( outer[j].x - outer[i].x ) * flSign > 0.0f;
			int delta;
			if ( bInsideBelow )
				delta = IconPosMod( ( stackBottom - segThick ) - ( y + inset ), pitch );	// area edge on a segment top
			else
				delta = -IconPosMod( ( y - inset ) - stackBottom, pitch );				// area edge on a segment bottom

			shift[i] = shift[j] = delta;
		}
		for ( int i = 0; i < nPoints; i++ )
			outer[i].y += shift[i];
	}

	IconInsetPolygon( outer, nPoints, stroke, inner );
	IconInsetPolygon( outer, nPoints, inset, area );

	// Rounded corners, each outline with its matching radius
	const float flCorner = MAX( 0.0f, (float)icon_corner_radius );
	Vector2D outerR[ICON_MAX_ROUNDED], innerR[ICON_MAX_ROUNDED], areaR[ICON_MAX_ROUNDED];
	const int nOuterR = IconRoundPolygon( outer, nPoints, flCorner, 0.0f, outerR );
	const int nInnerR = IconRoundPolygon( inner, nPoints, flCorner, stroke, innerR );
	const int nAreaR  = IconRoundPolygon( area, nPoints, flCorner, inset, areaR );

	// Segments that fit, from the bottom up
	float flAreaMinY = FLT_MAX;
	for ( int i = 0; i < nAreaR; i++ )
		flAreaMinY = MIN( flAreaMinY, areaR[i].y );
	const int areaTop = (int)ceil( flAreaMinY - 0.001f );
	const int nSegments = MAX( 0, ( stackBottom - areaTop + segGap ) / pitch );
	const int nLit = (int)( clamp( flFill, 0.0f, 1.0f ) * nSegments + 0.5f );

	// Scanlines phased so the segment rows are bright, as on the brackets
	const int brightParity = ( stackBottom - 1 ) & 1;

	// Texture rect: the outline's bounds plus room for the glow
	float flMinX = FLT_MAX, flMaxX = -FLT_MAX, flMinY = FLT_MAX, flMaxY = -FLT_MAX;
	for ( int i = 0; i < nPoints; i++ )
	{
		flMinX = MIN( flMinX, outer[i].x ); flMaxX = MAX( flMaxX, outer[i].x );
		flMinY = MIN( flMinY, outer[i].y ); flMaxY = MAX( flMaxY, outer[i].y );
	}
	const int margin = glowRadius * 3;
	const int originX = (int)flMinX - margin, originY = (int)flMinY - margin;
	const int wide = (int)flMaxX - (int)flMinX + 2 * margin;
	const int tall = (int)flMaxY - (int)flMinY + 2 * margin;

	// Rebuild the textures only when something they show changed
	const int key[8] = { originX, originY, wide, tall, nLit, nSegments, stroke * 1000 + (int)( flCorner * 100.0f ), (int)(intp)pPoints };
	if ( m_nIconTexture == -1 || V_memcmp( key, m_IconKey, sizeof( key ) ) )
	{
		V_memcpy( m_IconKey, key, sizeof( key ) );
		m_iIconX = originX; m_iIconY = originY; m_iIconWide = wide; m_iIconTall = tall;

		CUtlVector<float> shape, glow;
		shape.SetCount( wide * tall );
		glow.SetCount( wide * tall );
		V_memset( shape.Base(), 0, shape.Count() * sizeof( float ) );

		IconSpan_t spansA[ICON_MAX_SPANS], spansB[ICON_MAX_SPANS], spansOut[ICON_MAX_SPANS];

		// Outline, anti-aliased: outer shape minus the stroke's inner edge
		for ( int row = 0; row < tall; row++ )
		{
			for ( int s = 0; s < ICON_SUBSAMPLES; s++ )
			{
				const float y = originY + row + ( s + 0.5f ) / ICON_SUBSAMPLES;
				int nA = IconPolySpans( outerR, nOuterR, y, spansA );
				int nB = IconPolySpans( innerR, nInnerR, y, spansB );
				int nOut = IconSubtractSpans( spansA, nA, spansB, nB, spansOut );
				IconAccumulateSpans( spansOut, nOut, 1.0f / ICON_SUBSAMPLES, &shape[row * wide], originX, wide );
			}
		}

		// The glow comes from the outline and the lit segments; unlit ones don't shine
		for ( int i = 0; i < wide * tall; i++ )
			glow[i] = MIN( 1.0f, shape[i] );

		// Segments: each one is the part of the area that is inside it along both its
		// top and bottom edge, so it never crosses the rim, even on slanted sides
		for ( int i = 0; i < nSegments; i++ ) // i = 0 is the bottom segment
		{
			const int segBottom = stackBottom - i * pitch;
			const int segTop = segBottom - segThick;
			const float flValue = ( i < nLit ) ? 1.0f : ICON_UNLIT_ALPHA;

			int nA = IconPolySpans( areaR, nAreaR, segTop + 0.01f, spansA );
			int nB = IconPolySpans( areaR, nAreaR, segBottom - 0.01f, spansB );
			int nOut = IconIntersectSpans( spansA, nA, spansB, nB, spansOut );
			for ( int s = 0; s < nOut; s++ )
			{
				const int x0 = (int)ceil( spansOut[s].x0 - 0.001f ) - originX;
				const int x1 = (int)floor( spansOut[s].x1 + 0.001f ) - originX;
				if ( x1 - x0 < 2 )
					continue; // no single-pixel specks in sharp corners

				for ( int y = segTop - originY; y < segBottom - originY; y++ )
				{
					for ( int x = MAX( 0, x0 ); x < MIN( wide, x1 ); x++ )
					{
						shape[y * wide + x] = MAX( shape[y * wide + x], flValue );
						if ( i < nLit )
							glow[y * wide + x] = 1.0f;
					}
				}
			}
		}

		// Two box blurs in each direction, close enough to a gaussian
		for ( int pass = 0; pass < 2; pass++ )
		{
			IconBoxBlur( glow.Base(), wide, tall, glowRadius, false );
			IconBoxBlur( glow.Base(), wide, tall, glowRadius, true );
		}

		// White images with coverage as alpha; the color is applied when drawing.
		// Padded to power-of-two sizes: the engine stores textures that way, and an
		// odd-sized image would be squashed into part of the texture when drawn.
		int texWide = 1, texTall = 1;
		while ( texWide < wide ) texWide <<= 1;
		while ( texTall < tall ) texTall <<= 1;
		m_iIconWide = texWide;
		m_iIconTall = texTall;

		CUtlVector<unsigned char> rgbaShape, rgbaGlow;
		rgbaShape.SetCount( texWide * texTall * 4 );
		rgbaGlow.SetCount( texWide * texTall * 4 );
		V_memset( rgbaShape.Base(), 0, rgbaShape.Count() ); // transparent padding
		V_memset( rgbaGlow.Base(), 0, rgbaGlow.Count() );
		for ( int row = 0; row < tall; row++ )
		{
			const float flRowScale = ( ( ( originY + row ) & 1 ) == brightParity ) ? 1.0f : ICON_SCANLINE_DIM;
			for ( int x = 0; x < wide; x++ )
			{
				const int i = row * wide + x;
				const int t = row * texWide + x;
				unsigned char *pShape = &rgbaShape[t * 4], *pGlow = &rgbaGlow[t * 4];
				pShape[0] = pShape[1] = pShape[2] = 255;
				pShape[3] = (unsigned char)( clamp( shape[i], 0.0f, 1.0f ) * flRowScale * 255.0f );
				pGlow[0] = pGlow[1] = pGlow[2] = 255;
				pGlow[3] = (unsigned char)( clamp( glow[i] * ICON_GLOW_GAIN, 0.0f, 1.0f ) * flRowScale * 255.0f );
			}
		}

		if ( m_nIconTexture == -1 )
		{
			m_nIconTexture = surface()->CreateNewTextureID( true );
			m_nIconGlowTexture = surface()->CreateNewTextureID( true );
		}
		surface()->DrawSetTextureRGBA( m_nIconTexture, rgbaShape.Base(), texWide, texTall, false, true );
		surface()->DrawSetTextureRGBA( m_nIconGlowTexture, rgbaGlow.Base(), texWide, texTall, false, true );
	}

	// Glow first, pulsing with Blur like the digit glow (it rests at 0.6)
	const float flGlow = clamp( m_flIconGlow * m_flBlur / 0.6f, 0.0f, 1.0f );
	if ( flGlow > 0.0f )
	{
		Color clrGlow = clr;
		clrGlow[3] = (unsigned char)( clr[3] * flGlow );
		surface()->DrawSetColor( clrGlow );
		surface()->DrawSetTexture( m_nIconGlowTexture );
		surface()->DrawTexturedRect( m_iIconX, m_iIconY, m_iIconX + m_iIconWide, m_iIconY + m_iIconTall );
	}

	surface()->DrawSetColor( clr );
	surface()->DrawSetTexture( m_nIconTexture );
	surface()->DrawTexturedRect( m_iIconX, m_iIconY, m_iIconX + m_iIconWide, m_iIconY + m_iIconTall );
}


//-----------------------------------------------------------------------------
// Purpose: renders the vgui panel
//-----------------------------------------------------------------------------
void CHudNumericDisplay::Paint()
{
	// OF2: dark shade behind the readout, for bright backgrounds
	if ( m_flBackdropAlpha > 0.0f )
	{
		OF2_PaintBackdrop( 0, 0, GetWide(), GetTall(), m_flBackdropAlpha );
	}

	if (m_bDisplayValue)
	{
		// draw our numbers
		surface()->DrawSetTextColor(GetFgColor());
		PaintNumbers(m_hNumberFont, digit_xpos, digit_ypos, m_iValue);

		// draw the overbright blur
		for (float fl = m_flBlur; fl > 0.0f; fl -= 1.0f)
		{
			if (fl >= 1.0f)
			{
				PaintNumbers(m_hNumberGlowFont, digit_xpos, digit_ypos, m_iValue);
			}
			else
			{
				// draw a percentage of the last one
				Color col = GetFgColor();
				col[3] *= fl;
				surface()->DrawSetTextColor(col);
				PaintNumbers(m_hNumberGlowFont, digit_xpos, digit_ypos, m_iValue);
			}
		}
	}

	// total ammo
	if (m_bDisplaySecondaryValue)
	{
		surface()->DrawSetTextColor(GetFgColor());
		PaintNumbers(m_hSmallNumberFont, digit2_xpos, digit2_ypos, m_iSecondaryValue);
	}

	PaintLabel();
}



