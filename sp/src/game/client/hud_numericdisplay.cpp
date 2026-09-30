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
	m_nWhiteTexture = -1;
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
// Purpose: OF2: draws a closed outline as a stroke of icon_stroke thickness, inset
//			into the shape. Each edge is one quad between the outline and its mitered
//			inner offset, so neighboring quads share edges and nothing is drawn twice
//			(no brighter corners with translucent colors).
//-----------------------------------------------------------------------------
void CHudNumericDisplay::PaintIconOutline( const Vector2D *pPoints, int nPoints, float flAspect, Color clr )
{
	const int MAX_POINTS = 32;
	if ( nPoints < 3 || nPoints > MAX_POINTS )
		return;

	if ( m_nWhiteTexture == -1 )
	{
		m_nWhiteTexture = surface()->DrawGetTextureId( "vgui/white" );
		if ( m_nWhiteTexture == -1 )
		{
			m_nWhiteTexture = surface()->CreateNewTextureID();
			surface()->DrawSetTextureFile( m_nWhiteTexture, "vgui/white", true, false );
		}
	}

	// Outline in pixels
	const float flTall = icon_tall;
	const float flWide = icon_tall * flAspect;
	Vector2D outer[MAX_POINTS], inner[MAX_POINTS];
	float flArea = 0.0f;
	for ( int i = 0; i < nPoints; i++ )
	{
		outer[i].Init( icon_xpos + pPoints[i].x * flWide, icon_ypos + pPoints[i].y * flTall );
	}
	for ( int i = 0; i < nPoints; i++ )
	{
		const Vector2D &a = outer[i], &b = outer[( i + 1 ) % nPoints];
		flArea += a.x * b.y - b.x * a.y;
	}
	const float flSign = ( flArea > 0.0f ) ? 1.0f : -1.0f; // makes the normals point inward

	// Inner outline: each vertex moved inward along the miter of its two edges
	const float flStroke = MAX( 1.0f, (float)(int)( icon_stroke + 0.5f ) );
	for ( int i = 0; i < nPoints; i++ )
	{
		Vector2D e1 = outer[i] - outer[( i + nPoints - 1 ) % nPoints];
		Vector2D e2 = outer[( i + 1 ) % nPoints] - outer[i];
		Vector2DNormalize( e1 );
		Vector2DNormalize( e2 );
		Vector2D n1( -e1.y * flSign, e1.x * flSign );
		Vector2D n2( -e2.y * flSign, e2.x * flSign );
		float flDenom = MAX( 0.25f, 1.0f + n1.Dot( n2 ) ); // limit very sharp miters
		inner[i] = outer[i] + ( n1 + n2 ) * ( flStroke / flDenom );
	}

	// Snap to whole pixels so straight edges stay crisp
	for ( int i = 0; i < nPoints; i++ )
	{
		outer[i].Init( (int)( outer[i].x + 0.5f ), (int)( outer[i].y + 0.5f ) );
		inner[i].Init( (int)( inner[i].x + 0.5f ), (int)( inner[i].y + 0.5f ) );
	}

	surface()->DrawSetTexture( m_nWhiteTexture );
	surface()->DrawSetColor( clr );
	for ( int i = 0; i < nPoints; i++ )
	{
		int j = ( i + 1 ) % nPoints;
		Vertex_t quad[4];
		quad[0].Init( outer[i] );
		quad[1].Init( outer[j] );
		quad[2].Init( inner[j] );
		quad[3].Init( inner[i] );
		surface()->DrawTexturedPolygon( 4, quad );
	}
}

//-----------------------------------------------------------------------------
// Purpose: renders the vgui panel
//-----------------------------------------------------------------------------
void CHudNumericDisplay::Paint()
{
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



