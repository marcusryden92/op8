//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef HUD_NUMERICDISPLAY_H
#define HUD_NUMERICDISPLAY_H
#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/Panel.h>

// OF2: soft dark rounded-rect shade behind HUD elements, fading out toward the
// edges of the rect. flAlpha is the darkness at the center (0..255).
void OF2_PaintBackdrop( int x0, int y0, int x1, int y1, float flAlpha );

//-----------------------------------------------------------------------------
// Purpose: OF2: a vector icon in the HUD's style: a closed outline, with
//			horizontal segments inside it lit from the bottom up to a fill level,
//			like the crosshair brackets, and a glow. pPoints is the outline in
//			0..1 units of the icon box, which is flAspect times as wide as it is
//			tall. Call Paint() from a panel's Paint(); one of these per icon, since
//			it keeps the two textures the icon is rasterized into (shape, glow)
//			and rebuilds them only when what they show changes.
//			Used for the health plus and the suit shield (CHudNumericDisplay) and
//			for the small icons of the night vision / stealth panel.
//-----------------------------------------------------------------------------
// One part of a line art icon (COF2HudIcon::PaintLineArt). Points are in 0..1 of the icon box.
enum
{
	OF2_ICON_OUTLINE,	// closed polygon, the stroke inside it; hides what is behind it
	OF2_ICON_RING,		// likewise, a circle
	OF2_ICON_LINE,		// open line through the points, the stroke centered on it, round ends;
						// the points are put on whole pixels, for straight pieces
	OF2_ICON_CURVE,		// the same with the points left where they are, for curves
};

struct OF2IconPart_t
{
	int nKind;
	const Vector2D *pPoints;
	int nPoints;
	float flCenterX, flCenterY;	// ring: its middle, in 0..1 of the box
	float flDiameter;			// ring: as a part of the box's height
};

class COF2HudIcon
{
public:
	COF2HudIcon();

	// Positions and sizes in pixels. flGlow: strength of the glow, 0..1. flFill: 0..1, or negative
	// for the outline alone, with no segments inside.
	void Paint( const Vector2D *pPoints, int nPoints, float flX, float flTop, float flTall, float flAspect,
		float flStroke, float flCornerRadius, float flGlowRadius, float flGlow, float flFill, Color clr );

	// Line art made of several parts, listed back to front: a closed one hides what is behind it,
	// so nothing shows through a shape that lies over another.
	void PaintLineArt( const OF2IconPart_t *pParts, int nParts, float flX, float flTop, float flTall, float flAspect,
		float flStroke, float flCornerRadius, float flGlowRadius, float flGlow, Color clr );

private:
	void SetTextures( const float *pShape, const float *pGlow, int wide, int tall, int originY, int brightParity );
	void Draw( float flGlow, Color clr );

	int m_nTexture;
	int m_nGlowTexture;
	int m_Key[8];
	int m_iX, m_iY, m_iWide, m_iTall;
};

//-----------------------------------------------------------------------------
// Purpose: Base class for all the hud elements that are just a numeric display
//			with some options for text and icons
//-----------------------------------------------------------------------------
class CHudNumericDisplay : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudNumericDisplay, vgui::Panel );

public:
	CHudNumericDisplay(vgui::Panel *parent, const char *name);

	void SetDisplayValue(int value);
	void SetSecondaryValue(int value);
	void SetShouldDisplayValue(bool state);
	void SetShouldDisplaySecondaryValue(bool state);
	void SetLabelText(const wchar_t *text);
	void SetIndent(bool state);
	void SetIsTime(bool state);

	bool ShouldDisplayValue( void ) { return m_bDisplayValue; }
	bool ShouldDisplaySecondaryValue( void ) { return m_bDisplaySecondaryValue; }

	virtual void Reset();

protected:
	// vgui overrides
	virtual void Paint();
	virtual void PaintLabel();

	virtual void PaintNumbers(vgui::HFont font, int xpos, int ypos, int value);

	// OF2: vector icons in place of text labels. pPoints is a closed outline in 0..1
	// units of the icon box, which is flAspect times as wide as it is tall. Drawn as
	// scanlined pixel rows: the outline, plus horizontal segments inside it lit from
	// the bottom up to flFill (0..1), like the crosshair brackets.
	void PaintIcon( const Vector2D *pPoints, int nPoints, float flAspect, float flFill, Color clr );

protected:

	int m_iValue;
	int m_iSecondaryValue;
	wchar_t m_LabelText[32];
	bool m_bDisplayValue, m_bDisplaySecondaryValue;
	bool m_bIndent;
	bool m_bIsTime;

	CPanelAnimationVar( float, m_flBlur, "Blur", "0" );
	CPanelAnimationVar( Color, m_TextColor, "TextColor", "FgColor" );
	CPanelAnimationVar( Color, m_Ammo2Color, "Ammo2Color", "FgColor" );

	CPanelAnimationVar( vgui::HFont, m_hNumberFont, "NumberFont", "HudNumbers" );
	CPanelAnimationVar( vgui::HFont, m_hNumberGlowFont, "NumberGlowFont", "HudNumbersGlow" );
	CPanelAnimationVar( vgui::HFont, m_hSmallNumberFont, "SmallNumberFont", "HudNumbersSmall" );
	CPanelAnimationVar( vgui::HFont, m_hTextFont, "TextFont", "Default" );

	CPanelAnimationVarAliasType( float, text_xpos, "text_xpos", "8", "proportional_float" );
	CPanelAnimationVarAliasType( float, text_ypos, "text_ypos", "20", "proportional_float" );
	CPanelAnimationVarAliasType( float, digit_xpos, "digit_xpos", "50", "proportional_float" );
	CPanelAnimationVarAliasType( float, digit_ypos, "digit_ypos", "2", "proportional_float" );
	CPanelAnimationVarAliasType( float, digit2_xpos, "digit2_xpos", "98", "proportional_float" );
	CPanelAnimationVarAliasType( float, digit2_ypos, "digit2_ypos", "16", "proportional_float" );

	// Icon box. icon_tall 0 = match the digits: sit on their baseline, icon_digit_ratio
	// of the number font's height tall (0.62 is Share Tech Mono's digit height).
	CPanelAnimationVarAliasType( float, icon_xpos, "icon_xpos", "8", "proportional_float" );
	CPanelAnimationVarAliasType( float, icon_ypos, "icon_ypos", "10", "proportional_float" );
	CPanelAnimationVarAliasType( float, icon_tall, "icon_tall", "0", "proportional_float" );
	CPanelAnimationVarAliasType( float, icon_stroke, "icon_stroke", "1.5", "proportional_float" );
	CPanelAnimationVarAliasType( float, icon_corner_radius, "icon_corner_radius", "0.75", "proportional_float" ); // 0 = sharp
	CPanelAnimationVar( float, m_flIconDigitRatio, "icon_digit_ratio", "0.62" );
	CPanelAnimationVar( float, m_flBackdropAlpha, "backdrop_alpha", "0" ); // OF2 dark shade behind the panel, 0 = none
	// Icon glow: strength at the digits' resting Blur (it pulses with Blur like the digit glow), and radius
	CPanelAnimationVar( float, m_flIconGlow, "icon_glow", "0.6" );
	CPanelAnimationVarAliasType( float, icon_glow_radius, "icon_glow_radius", "1.5", "proportional_float" );

private:
	COF2HudIcon m_Icon;
};


#endif // HUD_NUMERICDISPLAY_H
