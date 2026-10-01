//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "hudelement.h"
#include "hud_numericdisplay.h"
#include <vgui_controls/Panel.h>
#include "hud.h"
#include "hud_suitpower.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include <vgui_controls/AnimationController.h>
#include <vgui/ISurface.h>
#include "c_basehlplayer.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: Shows the flashlight icon
//			OF2: night vision took over the flashlight's key and battery, so this
//			is now the night vision indicator: a label over a battery bar.
//			The panel keeps its name because HudLayout.res and the HUD
//			animations refer to it.
//-----------------------------------------------------------------------------
class CHudFlashlight : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudFlashlight, vgui::Panel );

public:
	CHudFlashlight( const char *pElementName );
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );

protected:
	virtual void Paint();

private:
	void SetFlashlightState( bool flashlightOn );
	void Reset( void );
	
	bool	m_bFlashlightOn;

	// OF2: the label and the bar are both centered in the panel, so there are no x positions
	CPanelAnimationVar( vgui::HFont, m_hTextFont, "TextFont", "Default" );
	CPanelAnimationVarAliasType( float, m_flTextY, "text_ypos", "4", "proportional_float" );
	CPanelAnimationVar( int, m_iDisabledAlpha, "DisabledAlpha", "70" );

	CPanelAnimationVarAliasType( float, m_flBarInsetY, "BarInsetY", "15", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flBarHeight, "BarHeight", "4", "proportional_float" );
	CPanelAnimationVar( int, m_nBarChunks, "BarChunks", "5" );
	CPanelAnimationVarAliasType( float, m_flBarChunkWidth, "BarChunkWidth", "4", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flBarChunkGap, "BarChunkGap", "2", "proportional_float" );
};

using namespace vgui;

#ifdef HL2_EPISODIC
DECLARE_HUDELEMENT( CHudFlashlight );
#endif // HL2_EPISODIC

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudFlashlight::CHudFlashlight( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudFlashlight" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	SetHiddenBits( HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD | HIDEHUD_NEEDSUIT );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pScheme - 
//-----------------------------------------------------------------------------
void CHudFlashlight::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings(pScheme);
}

//-----------------------------------------------------------------------------
// Purpose: Start with our background off
//-----------------------------------------------------------------------------
void CHudFlashlight::Reset( void )
{
	g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( "SuitFlashlightOn" ); 
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudFlashlight::SetFlashlightState( bool flashlightOn )
{
	if ( m_bFlashlightOn == flashlightOn )
		return;

	m_bFlashlightOn = flashlightOn;
}

//-----------------------------------------------------------------------------
// Purpose: draws the night vision label and its battery bar
//-----------------------------------------------------------------------------
void CHudFlashlight::Paint()
{
#ifdef HL2_EPISODIC
	C_BaseHLPlayer *pPlayer = (C_BaseHLPlayer *)C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return;

	// Only paint if we're using the new flashlight code
	if ( pPlayer->m_HL2Local.m_flFlashBattery < 0.0f )
	{
		SetPaintBackgroundEnabled( false );
		return;
	}

	bool bIsOn = pPlayer->m_HL2Local.m_bNightVision;
	SetFlashlightState( bIsOn );

	// OF2: whole-pixel chunks, so they all come out the same size with the same gaps
	int chunkCount = MAX( 1, m_nBarChunks );
	int chunkWide = MAX( 1, RoundFloatToInt( m_flBarChunkWidth ) );
	int chunkGap = MAX( 1, RoundFloatToInt( m_flBarChunkGap ) );
	int barTall = MAX( 1, RoundFloatToInt( m_flBarHeight ) );
	int enabledChunks = (int)((float)chunkCount * (pPlayer->m_HL2Local.m_flFlashBattery * 1.0f/100.0f) + 0.5f );

	Color clrNightVision;
	clrNightVision = ( enabledChunks <= ( chunkCount / 4 ) ) ? gHUD.m_clrCaution : gHUD.m_clrNormal;
	clrNightVision[3] = ( bIsOn ) ? 255 : m_iDisabledAlpha;

	// draw the label
	const wchar_t *pszLabel = L"NVG";
	int labelLength = wcslen( pszLabel );
	int labelWide = 0;
	for ( int i = 0; i < labelLength; i++ )
	{
		labelWide += surface()->GetCharacterWidth( m_hTextFont, pszLabel[i] );
	}

	surface()->DrawSetTextFont( m_hTextFont );
	surface()->DrawSetTextColor( clrNightVision );
	surface()->DrawSetTextPos( ( GetWide() - labelWide ) / 2, RoundFloatToInt( m_flTextY ) );
	surface()->DrawPrintText( pszLabel, labelLength );

	// draw the battery bar
	int barWide = chunkCount * chunkWide + ( chunkCount - 1 ) * chunkGap;
	int xpos = ( GetWide() - barWide ) / 2, ypos = RoundFloatToInt( m_flBarInsetY );

	surface()->DrawSetColor( clrNightVision );
	for (int i = 0; i < enabledChunks; i++)
	{
		surface()->DrawFilledRect( xpos, ypos, xpos + chunkWide, ypos + barTall );
		xpos += (chunkWide + chunkGap);
	}

	// draw the exhausted portion of the bar.
	clrNightVision[3] = ( bIsOn ) ? m_iDisabledAlpha : m_iDisabledAlpha / 3;
	surface()->DrawSetColor( clrNightVision );
	for (int i = enabledChunks; i < chunkCount; i++)
	{
		surface()->DrawFilledRect( xpos, ypos, xpos + chunkWide, ypos + barTall );
		xpos += (chunkWide + chunkGap);
	}
#endif // HL2_EPISODIC
}
