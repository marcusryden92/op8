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

static ConVar of2_hud_stealth( "of2_hud_stealth", "1", FCVAR_ARCHIVE, "Show the noise and light meters beside the night vision battery on the HUD." );

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: OF2: a bar of chunks, lit from the left up to flValue (0-1).
//			Whole-pixel chunks, so they all come out the same size with the same gaps.
//-----------------------------------------------------------------------------
static void OF2_PaintChunkBar( int x, int y, int nChunks, int chunkWide, int chunkGap, int barTall, float flValue, Color clr, int nEmptyAlpha )
{
	int nLit = (int)( (float)nChunks * clamp( flValue, 0.0f, 1.0f ) + 0.5f );

	surface()->DrawSetColor( clr );
	for ( int i = 0; i < nChunks; i++ )
	{
		if ( i == nLit )
		{
			// the empty part of the bar
			clr[3] = nEmptyAlpha;
			surface()->DrawSetColor( clr );
		}

		surface()->DrawFilledRect( x, y, x + chunkWide, y + barTall );
		x += chunkWide + chunkGap;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Shows the flashlight icon
//			OF2: night vision took over the flashlight's key and battery, so
//			this is now a goggles icon with the battery as a bar of chunks.
//			The stealth meters are a panel of their own to its right
//			(CHudStealth, below), built the same way: the user wanted the
//			three bars the same width and on one line, so the two panels
//			have the same keys with the same values in HudLayout.res.
//			The icons are line art (COF2HudIcon::PaintLineArt), after
//			drawings the user gave: the user did not want them full, or
//			filling and emptying with their bars.
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

	COF2HudIcon	m_GogglesIcon;

	CPanelAnimationVar( int, m_iDisabledAlpha, "DisabledAlpha", "70" );

	// Icon, gap, bar, from PadX; all centered on one line across the middle of the panel
	CPanelAnimationVarAliasType( float, m_flPadX, "PadX", "8", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flIconTall, "IconTall", "11", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flIconWide, "IconWide", "18", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flIconGap, "IconGap", "4", "proportional_float" );

	CPanelAnimationVarAliasType( float, m_flIconStroke, "IconStroke", "1", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flIconCornerRadius, "IconCornerRadius", "0.75", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flIconGlowRadius, "IconGlowRadius", "1", "proportional_float" );
	CPanelAnimationVar( float, m_flIconGlow, "IconGlow", "0.6" );

	CPanelAnimationVarAliasType( float, m_flBarHeight, "BarHeight", "4", "proportional_float" );
	CPanelAnimationVar( int, m_nBarChunks, "BarChunks", "8" );
	CPanelAnimationVarAliasType( float, m_flBarChunkWidth, "BarChunkWidth", "4", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flBarChunkGap, "BarChunkGap", "2", "proportional_float" );
};

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

// OF2: the icons' parts, in 0..1 of their boxes (COF2HudIcon). Small on screen, so few details.
// Each is laid out on the pixels it has at 1080p, where the strokes land on whole pixels.

// Night vision goggles from the front, after a drawing the user gave: a housing with sloping
// shoulders and two small rings in it, a ring in the middle under them, and the two big lenses
// at the bottom corners. Back to front, so the lenses and the middle ring cut the housing's
// lower edge. 40 x 25 pixels.
static const Vector2D s_GogglesHousing[] =
{
	Vector2D( 0.275f, 0.0f ),  Vector2D( 0.725f, 0.0f ),  Vector2D( 0.925f, 0.2f ),
	Vector2D( 0.925f, 0.52f ), Vector2D( 0.075f, 0.52f ), Vector2D( 0.075f, 0.2f ),
};

static const OF2IconPart_t s_Goggles[] =
{
	{ OF2_ICON_OUTLINE, s_GogglesHousing, ARRAYSIZE( s_GogglesHousing ), 0.0f, 0.0f, 0.0f },
	{ OF2_ICON_RING, NULL, 0, 0.3375f, 0.26f, 0.28f },
	{ OF2_ICON_RING, NULL, 0, 0.6625f, 0.26f, 0.28f },
	{ OF2_ICON_RING, NULL, 0, 0.5f, 0.6f, 0.4f },
	{ OF2_ICON_RING, NULL, 0, 0.1625f, 0.74f, 0.52f },
	{ OF2_ICON_RING, NULL, 0, 0.8375f, 0.74f, 0.52f },
};

// An ear, after a drawing the user gave (Wikimedia Commons, "Oreille"): the rim, open at the
// front, sampled from that drawing's curve, and its inner fold made simpler. 16 x 25 pixels.
static const Vector2D s_EarRim[] =
{
	Vector2D( 0.0625f, 0.3263f ), Vector2D( 0.1128f, 0.1994f ), Vector2D( 0.2109f, 0.1108f ), Vector2D( 0.3413f, 0.0584f ),
	Vector2D( 0.4887f, 0.0400f ), Vector2D( 0.6374f, 0.0536f ), Vector2D( 0.7721f, 0.0970f ), Vector2D( 0.8773f, 0.1682f ),
	Vector2D( 0.9375f, 0.2650f ), Vector2D( 0.9373f, 0.3854f ), Vector2D( 0.8794f, 0.4970f ), Vector2D( 0.7743f, 0.6664f ),
	Vector2D( 0.6957f, 0.7975f ), Vector2D( 0.6407f, 0.8655f ), Vector2D( 0.5739f, 0.9093f ), Vector2D( 0.4924f, 0.9402f ),
	Vector2D( 0.4038f, 0.9586f ), Vector2D( 0.3027f, 0.9600f ), Vector2D( 0.2104f, 0.9338f ), Vector2D( 0.1711f, 0.8994f ),
	Vector2D( 0.1432f, 0.8419f ), Vector2D( 0.1097f, 0.7586f ),
};

static const Vector2D s_EarFold[] =
{
	Vector2D( 0.6875f, 0.3800f ), Vector2D( 0.6746f, 0.3338f ), Vector2D( 0.6438f, 0.2800f ), Vector2D( 0.5777f, 0.2400f ),
	Vector2D( 0.5000f, 0.2200f ), Vector2D( 0.4234f, 0.2400f ), Vector2D( 0.3625f, 0.2800f ), Vector2D( 0.3348f, 0.3313f ),
	Vector2D( 0.3438f, 0.3800f ), Vector2D( 0.4129f, 0.4000f ), Vector2D( 0.5000f, 0.4200f ), Vector2D( 0.5742f, 0.4650f ),
	Vector2D( 0.6250f, 0.5200f ), Vector2D( 0.6270f, 0.5800f ), Vector2D( 0.5938f, 0.6400f ), Vector2D( 0.5137f, 0.6975f ),
	Vector2D( 0.4375f, 0.7400f ),
};

static const OF2IconPart_t s_Ear[] =
{
	{ OF2_ICON_CURVE, s_EarRim, ARRAYSIZE( s_EarRim ), 0.0f, 0.0f, 0.0f },
	{ OF2_ICON_CURVE, s_EarFold, ARRAYSIZE( s_EarFold ), 0.0f, 0.0f, 0.0f },
};

// A light bulb, after an icon the user gave: the glass running down into its neck, two bars for
// the base, and seven rays round it. 22 x 25 pixels; the width has to be even, so that the
// middle is on a pixel edge.
static const Vector2D s_BulbGlass[] =
{
	Vector2D( 0.4091f, 0.7200f ), Vector2D( 0.4091f, 0.6663f ), Vector2D( 0.3347f, 0.6309f ), Vector2D( 0.2762f, 0.5772f ),
	Vector2D( 0.2392f, 0.5102f ), Vector2D( 0.2273f, 0.4366f ), Vector2D( 0.2416f, 0.3632f ), Vector2D( 0.2807f, 0.2973f ),
	Vector2D( 0.3410f, 0.2450f ), Vector2D( 0.4165f, 0.2115f ), Vector2D( 0.5000f, 0.2000f ), Vector2D( 0.5835f, 0.2115f ),
	Vector2D( 0.6590f, 0.2450f ), Vector2D( 0.7193f, 0.2973f ), Vector2D( 0.7584f, 0.3632f ), Vector2D( 0.7727f, 0.4366f ),
	Vector2D( 0.7608f, 0.5102f ), Vector2D( 0.7238f, 0.5772f ), Vector2D( 0.6653f, 0.6309f ), Vector2D( 0.5909f, 0.6663f ),
	Vector2D( 0.5909f, 0.7200f ), Vector2D( 0.4091f, 0.7200f ),
};

static const Vector2D s_BulbRayTop[] =		{ Vector2D( 0.5f, 0.04f ),      Vector2D( 0.5f, 0.08f ) };
static const Vector2D s_BulbRayLeft[] =		{ Vector2D( 0.0455f, 0.44f ),   Vector2D( 0.0909f, 0.44f ) };
static const Vector2D s_BulbRayRight[] =	{ Vector2D( 0.9091f, 0.44f ),   Vector2D( 0.9545f, 0.44f ) };
static const Vector2D s_BulbRayUpLeft[] =	{ Vector2D( 0.2091f, 0.184f ),  Vector2D( 0.1727f, 0.152f ) };
static const Vector2D s_BulbRayUpRight[] =	{ Vector2D( 0.7909f, 0.184f ),  Vector2D( 0.8273f, 0.152f ) };
static const Vector2D s_BulbRayDownLeft[] =	{ Vector2D( 0.2091f, 0.696f ),  Vector2D( 0.1727f, 0.728f ) };
static const Vector2D s_BulbRayDownRight[] = { Vector2D( 0.7909f, 0.696f ), Vector2D( 0.8273f, 0.728f ) };
static const Vector2D s_BulbBase1[] =		{ Vector2D( 0.4091f, 0.84f ),   Vector2D( 0.5909f, 0.84f ) };
static const Vector2D s_BulbBase2[] =		{ Vector2D( 0.4545f, 0.96f ),   Vector2D( 0.5455f, 0.96f ) };

static const OF2IconPart_t s_Bulb[] =
{
	{ OF2_ICON_CURVE, s_BulbGlass, ARRAYSIZE( s_BulbGlass ), 0.0f, 0.0f, 0.0f },
	{ OF2_ICON_LINE, s_BulbRayTop, 2, 0.0f, 0.0f, 0.0f },
	{ OF2_ICON_LINE, s_BulbRayLeft, 2, 0.0f, 0.0f, 0.0f },
	{ OF2_ICON_LINE, s_BulbRayRight, 2, 0.0f, 0.0f, 0.0f },
	{ OF2_ICON_CURVE, s_BulbRayUpLeft, 2, 0.0f, 0.0f, 0.0f },
	{ OF2_ICON_CURVE, s_BulbRayUpRight, 2, 0.0f, 0.0f, 0.0f },
	{ OF2_ICON_CURVE, s_BulbRayDownLeft, 2, 0.0f, 0.0f, 0.0f },
	{ OF2_ICON_CURVE, s_BulbRayDownRight, 2, 0.0f, 0.0f, 0.0f },
	{ OF2_ICON_LINE, s_BulbBase1, 2, 0.0f, 0.0f, 0.0f },
	{ OF2_ICON_LINE, s_BulbBase2, 2, 0.0f, 0.0f, 0.0f },
};

//-----------------------------------------------------------------------------
// Purpose: draws the goggles and the night vision battery
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

	// Everything is placed in whole pixels; the bar is in the middle of the panel and the icon centered on it
	int chunkCount = MAX( 1, m_nBarChunks );
	int chunkWide = MAX( 1, RoundFloatToInt( m_flBarChunkWidth ) );
	int chunkGap = MAX( 1, RoundFloatToInt( m_flBarChunkGap ) );
	int barTall = MAX( 1, RoundFloatToInt( m_flBarHeight ) );
	int iconTall = MAX( 4, RoundFloatToInt( m_flIconTall ) );
	int iconWide = MAX( 4, RoundFloatToInt( m_flIconWide ) );
	int barY = ( GetTall() - barTall ) / 2;
	int iconY = barY - ( iconTall - barTall ) / 2;
	int x = RoundFloatToInt( m_flPadX );

	float flBattery = clamp( pPlayer->m_HL2Local.m_flFlashBattery / 100.0f, 0.0f, 1.0f );

	Color clr = ( flBattery <= 0.25f ) ? gHUD.m_clrCaution : gHUD.m_clrNormal;
	clr[3] = bIsOn ? 255 : m_iDisabledAlpha;

	m_GogglesIcon.PaintLineArt( s_Goggles, ARRAYSIZE( s_Goggles ), x, iconY, iconTall, (float)iconWide / iconTall,
		m_flIconStroke, m_flIconCornerRadius, m_flIconGlowRadius, bIsOn ? m_flIconGlow : 0.0f, clr );
	x += iconWide + MAX( 1, RoundFloatToInt( m_flIconGap ) );

	OF2_PaintChunkBar( x, barY, chunkCount, chunkWide, chunkGap, barTall, flBattery, clr, bIsOn ? m_iDisabledAlpha : m_iDisabledAlpha / 3 );
#endif // HL2_EPISODIC
}

//-----------------------------------------------------------------------------
// Purpose: OF2: the stealth meters, in their own panel to the right of the
//			night vision one: an ear with how far the noise the player makes
//			carries, and a bulb with how visible the light makes them. Both
//			come from the server, which is what the NPCs go by
//			(m_HL2Local.m_flStealthNoise / m_flStealthLight).
//-----------------------------------------------------------------------------
class CHudStealth : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudStealth, vgui::Panel );

public:
	CHudStealth( const char *pElementName );
	virtual bool ShouldDraw( void );

protected:
	virtual void Paint();

private:
	COF2HudIcon	m_EarIcon;
	COF2HudIcon	m_BulbIcon;

	CPanelAnimationVar( int, m_iDisabledAlpha, "DisabledAlpha", "70" );
	CPanelAnimationVar( float, m_flCautionFrom, "CautionFrom", "0.75" );

	// Ear, gap, bar, MeterGap, bulb, gap, bar, from PadX; all centered on one line across the middle of the panel
	CPanelAnimationVarAliasType( float, m_flPadX, "PadX", "8", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flIconTall, "IconTall", "11", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flEarWide, "EarWide", "7", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flBulbWide, "BulbWide", "10", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flIconGap, "IconGap", "4", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flMeterGap, "MeterGap", "8", "proportional_float" );

	CPanelAnimationVarAliasType( float, m_flIconStroke, "IconStroke", "1", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flIconGlowRadius, "IconGlowRadius", "1", "proportional_float" );
	CPanelAnimationVar( float, m_flIconGlow, "IconGlow", "0.6" );

	CPanelAnimationVarAliasType( float, m_flBarHeight, "BarHeight", "4", "proportional_float" );
	CPanelAnimationVar( int, m_nBarChunks, "BarChunks", "8" );
	CPanelAnimationVarAliasType( float, m_flBarChunkWidth, "BarChunkWidth", "4", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flBarChunkGap, "BarChunkGap", "2", "proportional_float" );
};

#ifdef HL2_EPISODIC
DECLARE_HUDELEMENT( CHudStealth );
#endif // HL2_EPISODIC

CHudStealth::CHudStealth( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudStealth" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	SetHiddenBits( HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD | HIDEHUD_NEEDSUIT );
}

bool CHudStealth::ShouldDraw( void )
{
	return of2_hud_stealth.GetBool() && CHudElement::ShouldDraw();
}

//-----------------------------------------------------------------------------
// Purpose: draws the noise and light meters
//-----------------------------------------------------------------------------
void CHudStealth::Paint()
{
#ifdef HL2_EPISODIC
	C_BaseHLPlayer *pPlayer = (C_BaseHLPlayer *)C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return;

	int chunkCount = MAX( 1, m_nBarChunks );
	int chunkWide = MAX( 1, RoundFloatToInt( m_flBarChunkWidth ) );
	int chunkGap = MAX( 1, RoundFloatToInt( m_flBarChunkGap ) );
	int barWide = chunkCount * ( chunkWide + chunkGap ) - chunkGap;
	int barTall = MAX( 1, RoundFloatToInt( m_flBarHeight ) );
	int iconTall = MAX( 4, RoundFloatToInt( m_flIconTall ) );
	int iconGap = MAX( 1, RoundFloatToInt( m_flIconGap ) );
	int earWide = MAX( 3, RoundFloatToInt( m_flEarWide ) );
	int bulbWide = MAX( 4, RoundFloatToInt( m_flBulbWide ) & ~1 );	// even: see s_BulbGlass
	int barY = ( GetTall() - barTall ) / 2;
	int iconY = barY - ( iconTall - barTall ) / 2;
	int x = RoundFloatToInt( m_flPadX );

	float flNoise = clamp( pPlayer->m_HL2Local.m_flStealthNoise, 0.0f, 1.0f );
	Color clr = ( flNoise >= m_flCautionFrom ) ? gHUD.m_clrCaution : gHUD.m_clrNormal;
	clr[3] = 255;
	m_EarIcon.PaintLineArt( s_Ear, ARRAYSIZE( s_Ear ), x, iconY, iconTall, (float)earWide / iconTall,
		m_flIconStroke, 0.0f, m_flIconGlowRadius, m_flIconGlow, clr );
	x += earWide + iconGap;
	OF2_PaintChunkBar( x, barY, chunkCount, chunkWide, chunkGap, barTall, flNoise, clr, m_iDisabledAlpha );
	x += barWide + RoundFloatToInt( m_flMeterGap );

	float flLight = clamp( pPlayer->m_HL2Local.m_flStealthLight, 0.0f, 1.0f );
	clr = ( flLight >= m_flCautionFrom ) ? gHUD.m_clrCaution : gHUD.m_clrNormal;
	clr[3] = 255;
	m_BulbIcon.PaintLineArt( s_Bulb, ARRAYSIZE( s_Bulb ), x, iconY, iconTall, (float)bulbWide / iconTall,
		m_flIconStroke, 0.0f, m_flIconGlowRadius, m_flIconGlow, clr );
	x += bulbWide + iconGap;
	OF2_PaintChunkBar( x, barY, chunkCount, chunkWide, chunkGap, barTall, flLight, clr, m_iDisabledAlpha );
#endif // HL2_EPISODIC
}
