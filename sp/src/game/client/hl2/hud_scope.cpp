//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: Counter-Strike style sniper scope. While a weapon whose script
//			has "scope_overlay" "1" has the view zoomed, the screen is black
//			outside a circle with thin black lines across it, and the
//			viewmodel, crosshair and crosshair brackets are hidden
//			(OF2_IsScopeView, used by viewrender.cpp, hud_crosshair.cpp and
//			hud_quickinfo.cpp).
//
//			The circle's edge is a texture made here at the screen's own
//			resolution and drawn 1:1, so it stays sharp; Counter-Strike
//			stretches a 256 pixel quarter circle instead.
//
//=============================================================================//

#include "cbase.h"
#include "hudelement.h"
#include "hud.h"
#include "iclientmode.h"
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include "utlvector.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Not archived; they reset on restart.
static ConVar of2_scope_size( "of2_scope_size", "0.875", FCVAR_NONE, "Diameter of the sniper scope's circle, as a fraction of the screen height. Counter-Strike's is 0.875." );
static ConVar of2_scope_line( "of2_scope_line", "1", FCVAR_NONE, "Thickness of the sniper scope's lines, in pixels." );

// Samples per pixel each way for the circle's soft edge
#define SCOPE_EDGE_SAMPLES	4

//-----------------------------------------------------------------------------
// Purpose: the local player is looking through a scope
//-----------------------------------------------------------------------------
bool OF2_IsScopeView( void )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer == NULL || !pPlayer->IsAlive() )
		return false;

	C_BaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
	if ( pWeapon == NULL || !pWeapon->GetWpnData().m_bScopeOverlay )
		return false;

	// The weapon is the zoom's owner while it holds the view zoomed (CBasePlayer::SetFOV)
	return pPlayer->m_hZoomOwner.Get() == pWeapon;
}

//-----------------------------------------------------------------------------
// CHudScope: covers the whole screen, under the rest of the HUD
//-----------------------------------------------------------------------------
class CHudScope : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudScope, vgui::Panel );

public:
	CHudScope( const char *pElementName );
	virtual bool ShouldDraw( void );
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );

protected:
	virtual void Paint();

private:
	void	MakeCircleTexture( int diameter );

	int		m_nTexture;
	int		m_iTextureDiameter;	// the circle the texture holds, in pixels
	int		m_iTextureSize;		// the texture, padded to a power of two
};

#ifdef HL2_EPISODIC
DECLARE_HUDELEMENT( CHudScope );
#endif // HL2_EPISODIC

CHudScope::CHudScope( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudScope" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_nTexture = -1;
	m_iTextureDiameter = 0;
	m_iTextureSize = 0;

	SetHiddenBits( HIDEHUD_PLAYERDEAD );
}

void CHudScope::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	SetPaintBackgroundEnabled( false );

	// Under the health and ammo readouts
	SetZPos( -1 );
}

bool CHudScope::ShouldDraw( void )
{
	if ( GetWide() != ScreenWidth() || GetTall() != ScreenHeight() )
	{
		SetBounds( 0, 0, ScreenWidth(), ScreenHeight() );
	}

	return CHudElement::ShouldDraw() && OF2_IsScopeView();
}

//-----------------------------------------------------------------------------
// Purpose: Black outside a circle of this many pixels across, clear inside, with
//			the edge antialiased. Transparent padding beyond the circle's square.
//-----------------------------------------------------------------------------
void CHudScope::MakeCircleTexture( int diameter )
{
	int size = 1;
	while ( size < diameter )
	{
		size <<= 1;
	}

	CUtlVector<unsigned char> rgba;
	rgba.SetCount( size * size * 4 );
	V_memset( rgba.Base(), 0, rgba.Count() );

	const float flRadius = diameter * 0.5f;
	const float flRadiusSqr = flRadius * flRadius;
	const float flStep = 1.0f / SCOPE_EDGE_SAMPLES;

	for ( int py = 0; py < diameter; py++ )
	{
		for ( int px = 0; px < diameter; px++ )
		{
			// The share of the pixel that lies outside the circle
			int nOutside = 0;
			for ( int sy = 0; sy < SCOPE_EDGE_SAMPLES; sy++ )
			{
				float dy = py + ( sy + 0.5f ) * flStep - flRadius;
				for ( int sx = 0; sx < SCOPE_EDGE_SAMPLES; sx++ )
				{
					float dx = px + ( sx + 0.5f ) * flStep - flRadius;
					if ( dx * dx + dy * dy > flRadiusSqr )
					{
						nOutside++;
					}
				}
			}

			unsigned char *p = &rgba[( py * size + px ) * 4];
			p[3] = (unsigned char)( nOutside * 255 / ( SCOPE_EDGE_SAMPLES * SCOPE_EDGE_SAMPLES ) );
		}
	}

	if ( m_nTexture == -1 )
	{
		m_nTexture = vgui::surface()->CreateNewTextureID( true );
	}

	vgui::surface()->DrawSetTextureRGBA( m_nTexture, rgba.Base(), size, size, false, true );

	m_iTextureDiameter = diameter;
	m_iTextureSize = size;
}

void CHudScope::Paint()
{
	const int wide = GetWide();
	const int tall = GetTall();

	// Whole pixels, and even so the circle sits evenly about the middle of the screen
	int diameter = (int)( clamp( of2_scope_size.GetFloat(), 0.1f, 1.0f ) * tall );
	diameter &= ~1;
	if ( diameter < 2 )
		return;

	if ( diameter != m_iTextureDiameter )
	{
		MakeCircleTexture( diameter );
	}

	const int cx = wide / 2;
	const int cy = tall / 2;
	const int x0 = cx - diameter / 2;
	const int y0 = cy - diameter / 2;
	const int x1 = x0 + diameter;
	const int y1 = y0 + diameter;

	// The circle, then black around its square; nothing overlaps
	vgui::surface()->DrawSetColor( 255, 255, 255, 255 );
	vgui::surface()->DrawSetTexture( m_nTexture );
	vgui::surface()->DrawTexturedRect( x0, y0, x0 + m_iTextureSize, y0 + m_iTextureSize );

	vgui::surface()->DrawSetColor( 0, 0, 0, 255 );
	vgui::surface()->DrawFilledRect( 0, 0, x0, tall );
	vgui::surface()->DrawFilledRect( x1, 0, wide, tall );
	vgui::surface()->DrawFilledRect( x0, 0, x1, y0 );
	vgui::surface()->DrawFilledRect( x0, y1, x1, tall );

	// The cross, inside the circle's square (outside it is black already). Opaque, so
	// where the lines cross and where they meet the edge nothing shows through twice.
	const int line = MAX( of2_scope_line.GetInt(), 1 );
	const int lx = cx - line / 2;
	const int ly = cy - line / 2;
	vgui::surface()->DrawFilledRect( lx, y0, lx + line, y1 );
	vgui::surface()->DrawFilledRect( x0, ly, x1, ly + line );
}
