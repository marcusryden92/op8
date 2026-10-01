//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: the Displacer on the HUD.
//
//			CHudDisplacer: status readout. A line of text over a signal bar,
//			built like the night vision indicator (hud_flashlight.cpp):
//			no destination / locked, with the bar running down towards the
//			edge of the range / "SIGNAL LOST" for a few seconds.
//
//			CHudDisplacerMarker: brackets drawn over the destination itself,
//			with its distance, held at the edge of the screen when it is out
//			of view.
//
//=============================================================================//

#include "cbase.h"
#include "hudelement.h"
#include "hud.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include "c_basehlplayer.h"
#include "c_weapon_displacer.h"
#include "view_scene.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: the player's Displacer, if they carry one
//-----------------------------------------------------------------------------
static C_WeaponDisplacer *GetLocalDisplacer( void )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return NULL;

	return dynamic_cast<C_WeaponDisplacer *>( pPlayer->Weapon_OwnsThisType( "weapon_displacer" ) );
}

class CHudDisplacer : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudDisplacer, vgui::Panel );

public:
	CHudDisplacer( const char *pElementName );
	virtual bool ShouldDraw( void );

protected:
	virtual void Paint();

private:
	bool	IsSignalLostShowing( C_WeaponDisplacer *pDisplacer );

	// The label and the bar are both centered in the panel, so there are no x positions
	CPanelAnimationVar( vgui::HFont, m_hTextFont, "TextFont", "Default" );
	CPanelAnimationVarAliasType( float, m_flTextY, "text_ypos", "4", "proportional_float" );
	CPanelAnimationVar( int, m_iDisabledAlpha, "DisabledAlpha", "70" );

	CPanelAnimationVarAliasType( float, m_flBarInsetY, "BarInsetY", "15", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flBarHeight, "BarHeight", "4", "proportional_float" );
	CPanelAnimationVar( int, m_nBarChunks, "BarChunks", "10" );
	CPanelAnimationVarAliasType( float, m_flBarChunkWidth, "BarChunkWidth", "4", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flBarChunkGap, "BarChunkGap", "2", "proportional_float" );

	CPanelAnimationVar( float, m_flSignalLostTime, "SignalLostTime", "3" );		// seconds the readout stays up
	CPanelAnimationVar( float, m_flSignalLostBlink, "SignalLostBlink", "0.5" );	// length of one blink
};

using namespace vgui;

#ifdef HL2_EPISODIC
DECLARE_HUDELEMENT( CHudDisplacer );
#endif // HL2_EPISODIC

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudDisplacer::CHudDisplacer( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudDisplacer" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	SetHiddenBits( HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD | HIDEHUD_NEEDSUIT );
}

bool CHudDisplacer::IsSignalLostShowing( C_WeaponDisplacer *pDisplacer )
{
	if ( pDisplacer->m_bHasDestination || pDisplacer->m_flSignalLostTime <= 0.0f )
		return false;

	float flElapsed = gpGlobals->curtime - pDisplacer->m_flSignalLostTime;
	return flElapsed >= 0.0f && flElapsed < m_flSignalLostTime;
}

//-----------------------------------------------------------------------------
// Purpose: shown while the Displacer is in hand, and with any other weapon
//			for as long as there is a destination or a lost signal to report
//-----------------------------------------------------------------------------
bool CHudDisplacer::ShouldDraw( void )
{
	if ( !CHudElement::ShouldDraw() )
		return false;

	C_WeaponDisplacer *pDisplacer = GetLocalDisplacer();
	if ( !pDisplacer )
		return false;

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer->GetActiveWeapon() == pDisplacer )
		return true;

	return pDisplacer->m_bHasDestination || IsSignalLostShowing( pDisplacer );
}

//-----------------------------------------------------------------------------
// Purpose: draws the status text and the signal bar
//-----------------------------------------------------------------------------
void CHudDisplacer::Paint()
{
	C_WeaponDisplacer *pDisplacer = GetLocalDisplacer();
	if ( !pDisplacer )
		return;

	// Whole-pixel chunks, so they all come out the same size with the same gaps
	int chunkCount = MAX( 1, m_nBarChunks );
	int chunkWide = MAX( 1, RoundFloatToInt( m_flBarChunkWidth ) );
	int chunkGap = MAX( 1, RoundFloatToInt( m_flBarChunkGap ) );
	int barTall = MAX( 1, RoundFloatToInt( m_flBarHeight ) );

	const wchar_t *pszLabel = L"NO DEST";
	Color clrText = gHUD.m_clrNormal;
	clrText[3] = m_iDisabledAlpha;
	int enabledChunks = 0;

	if ( pDisplacer->m_bHasDestination )
	{
		// Always at least one chunk while locked; amber over the last quarter of the range
		enabledChunks = clamp( (int)( (float)chunkCount * ( pDisplacer->m_iSignal / 100.0f ) + 0.5f ), 1, chunkCount );

		pszLabel = pDisplacer->m_bDestIsTarget ? L"TARGET LOCK" : L"DEST LOCK";
		clrText = ( enabledChunks <= ( chunkCount / 4 ) ) ? gHUD.m_clrCaution : gHUD.m_clrNormal;
		clrText[3] = 255;
	}
	else if ( IsSignalLostShowing( pDisplacer ) )
	{
		float flElapsed = gpGlobals->curtime - pDisplacer->m_flSignalLostTime;
		bool bBlinkOn = ( m_flSignalLostBlink <= 0.0f ) || ( fmod( flElapsed, m_flSignalLostBlink ) < m_flSignalLostBlink * 0.6f );

		pszLabel = L"SIGNAL LOST";
		clrText = gHUD.m_clrCaution;
		clrText[3] = bBlinkOn ? 255 : m_iDisabledAlpha;
	}

	// draw the label
	int labelLength = wcslen( pszLabel );
	int labelWide = 0;
	for ( int i = 0; i < labelLength; i++ )
	{
		labelWide += surface()->GetCharacterWidth( m_hTextFont, pszLabel[i] );
	}

	surface()->DrawSetTextFont( m_hTextFont );
	surface()->DrawSetTextColor( clrText );
	surface()->DrawSetTextPos( ( GetWide() - labelWide ) / 2, RoundFloatToInt( m_flTextY ) );
	surface()->DrawPrintText( pszLabel, labelLength );

	// draw the signal bar
	int barWide = chunkCount * chunkWide + ( chunkCount - 1 ) * chunkGap;
	int xpos = ( GetWide() - barWide ) / 2, ypos = RoundFloatToInt( m_flBarInsetY );

	surface()->DrawSetColor( clrText );
	for ( int i = 0; i < enabledChunks; i++ )
	{
		surface()->DrawFilledRect( xpos, ypos, xpos + chunkWide, ypos + barTall );
		xpos += ( chunkWide + chunkGap );
	}

	// draw the missing portion of the bar
	Color clrEmpty = clrText;
	clrEmpty[3] = pDisplacer->m_bHasDestination ? m_iDisabledAlpha : m_iDisabledAlpha / 3;
	surface()->DrawSetColor( clrEmpty );
	for ( int i = enabledChunks; i < chunkCount; i++ )
	{
		surface()->DrawFilledRect( xpos, ypos, xpos + chunkWide, ypos + barTall );
		xpos += ( chunkWide + chunkGap );
	}
}

//-----------------------------------------------------------------------------
// Purpose: marks the destination in the world. Covers the whole screen and
//			draws only the marker.
//-----------------------------------------------------------------------------
class CHudDisplacerMarker : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudDisplacerMarker, vgui::Panel );

public:
	CHudDisplacerMarker( const char *pElementName );
	virtual bool ShouldDraw( void );
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );

protected:
	virtual void Paint();

private:
	CPanelAnimationVar( vgui::HFont, m_hTextFont, "TextFont", "Default" );
	CPanelAnimationVarAliasType( float, m_flMarkerSize, "MarkerSize", "10", "proportional_float" );	// center to the outside of the brackets, up close
	CPanelAnimationVarAliasType( float, m_flMarkerArm, "MarkerArm", "4", "proportional_float" );		// length of each bracket leg, up close
	CPanelAnimationVar( float, m_flFullSizeDistance, "MarkerFullSizeDistance", "250" );	// full size within this many units, then shrinking like a thing in the world
	CPanelAnimationVar( float, m_flMinScale, "MarkerMinScale", "0.35" );				// but never smaller than this part of full size
	CPanelAnimationVarAliasType( float, m_flMarkerThickness, "MarkerThickness", "1", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flTextGap, "MarkerTextGap", "2", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flEdgeInset, "EdgeInset", "24", "proportional_float" );		// how far inside the screen edge an out-of-view marker sits
	CPanelAnimationVar( int, m_iMarkerAlpha, "MarkerAlpha", "230" );
	CPanelAnimationVar( int, m_iIdleAlpha, "IdleAlpha", "140" );		// with another weapon out, or held at the edge
	CPanelAnimationVar( int, m_iCautionSignal, "CautionSignal", "25" );	// amber at or below this signal, like the bar
};

#ifdef HL2_EPISODIC
DECLARE_HUDELEMENT( CHudDisplacerMarker );
#endif // HL2_EPISODIC

CHudDisplacerMarker::CHudDisplacerMarker( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudDisplacerMarker" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	SetHiddenBits( HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD | HIDEHUD_NEEDSUIT );
}

void CHudDisplacerMarker::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	SetPaintBackgroundEnabled( false );
}

bool CHudDisplacerMarker::ShouldDraw( void )
{
	// A 640 wide panel stops short of the right edge on a widescreen display
	if ( GetWide() != ScreenWidth() || GetTall() != ScreenHeight() )
	{
		SetBounds( 0, 0, ScreenWidth(), ScreenHeight() );
	}

	if ( !CHudElement::ShouldDraw() )
		return false;

	C_WeaponDisplacer *pDisplacer = GetLocalDisplacer();
	return pDisplacer && pDisplacer->m_bHasDestination;
}

void CHudDisplacerMarker::Paint()
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	C_WeaponDisplacer *pDisplacer = GetLocalDisplacer();
	if ( !pPlayer || !pDisplacer || !pDisplacer->m_bHasDestination )
		return;

	int wide = GetWide(), tall = GetTall();

	// Where it is on the screen, -1 to 1 each way. Behind the view the numbers
	// are huge but still point the right way.
	Vector screen;
	Vector vecMarker = pDisplacer->GetMarkerPosition();
	bool bPinned = ( ScreenTransform( vecMarker, screen ) != 0 );

	float flLimitX = 1.0f - 2.0f * m_flEdgeInset / wide;
	float flLimitY = 1.0f - 2.0f * m_flEdgeInset / tall;
	if ( bPinned || fabs( screen.x ) > flLimitX || fabs( screen.y ) > flLimitY )
	{
		// Out of view: slide it along its direction from the middle of the screen to the edge
		bPinned = true;
		if ( screen.x == 0.0f && screen.y == 0.0f )
		{
			screen.y = -1.0f;
		}

		float flOver = MAX( fabs( screen.x ) / flLimitX, fabs( screen.y ) / flLimitY );
		screen.x /= flOver;
		screen.y /= flOver;
	}

	int cx = RoundFloatToInt( 0.5f * ( 1.0f + screen.x ) * wide );
	int cy = RoundFloatToInt( 0.5f * ( 1.0f - screen.y ) * tall );

	Color clr = ( pDisplacer->m_iSignal <= m_iCautionSignal ) ? gHUD.m_clrCaution : gHUD.m_clrNormal;
	clr[3] = ( bPinned || pPlayer->GetActiveWeapon() != pDisplacer ) ? m_iIdleAlpha : m_iMarkerAlpha;

	// Whole pixels, and no two rectangles overlap, so nothing translucent is drawn twice
	// It shrinks with distance as if it stood in the world, down to a size that can still be read.
	// The line thickness stays.
	float flDist = ( pPlayer->EyePosition() - vecMarker ).Length();
	float flScale = clamp( m_flFullSizeDistance / MAX( flDist, 1.0f ), m_flMinScale, 1.0f );

	int thick = MAX( 1, RoundFloatToInt( m_flMarkerThickness ) );
	int half = MAX( thick * 3, RoundFloatToInt( m_flMarkerSize * flScale ) );
	int arm = clamp( RoundFloatToInt( m_flMarkerArm * flScale ), thick + 1, half - thick );
	int left = cx - half, right = cx + half, top = cy - half, bottom = cy + half;

	surface()->DrawSetColor( clr );

	// Each corner: the leg along the top or bottom edge, then the leg up the side without the corner square
	surface()->DrawFilledRect( left, top, left + arm, top + thick );
	surface()->DrawFilledRect( left, top + thick, left + thick, top + arm );

	surface()->DrawFilledRect( right - arm, top, right, top + thick );
	surface()->DrawFilledRect( right - thick, top + thick, right, top + arm );

	surface()->DrawFilledRect( left, bottom - thick, left + arm, bottom );
	surface()->DrawFilledRect( left, bottom - arm, left + thick, bottom - thick );

	surface()->DrawFilledRect( right - arm, bottom - thick, right, bottom );
	surface()->DrawFilledRect( right - thick, bottom - arm, right, bottom - thick );

	// The exact spot, when it is the real one
	if ( !bPinned )
	{
		surface()->DrawFilledRect( cx - thick, cy - thick, cx + thick, cy + thick );
	}

	// Distance in meters under it (a unit is three quarters of an inch)
	wchar_t wszDistance[16];
	V_snwprintf( wszDistance, ARRAYSIZE( wszDistance ), L"%dM", RoundFloatToInt( flDist * 0.01905f ) );

	int textLength = wcslen( wszDistance );
	int textWide = 0;
	for ( int i = 0; i < textLength; i++ )
	{
		textWide += surface()->GetCharacterWidth( m_hTextFont, wszDistance[i] );
	}

	surface()->DrawSetTextFont( m_hTextFont );
	surface()->DrawSetTextColor( clr );
	surface()->DrawSetTextPos( cx - textWide / 2, bottom + RoundFloatToInt( m_flTextGap ) );
	surface()->DrawPrintText( wszDistance, textLength );
}
