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
	// The icon is rasterized into two textures (shape, glow) that are rebuilt only
	// when what they show changes (m_IconKey)
	int m_nIconTexture;
	int m_nIconGlowTexture;
	int m_IconKey[8];
	int m_iIconX, m_iIconY, m_iIconWide, m_iIconTall;
};


#endif // HUD_NUMERICDISPLAY_H
