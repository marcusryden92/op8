//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "engine/IEngineSound.h"
#include "vgui_controls/AnimationController.h"
#include "vgui_controls/Controls.h"
#include "vgui_controls/Panel.h"
#include "vgui/ISurface.h"
#include "../hud_crosshair.h"
#include "VGuiMatSurface/IMatSystemSurface.h"

#ifdef SIXENSE
#include "sixense/in_sixense.h"
#include "view.h"
int ScreenTransform( const Vector& point, Vector& screen );
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define	HEALTH_WARNING_THRESHOLD	25

static ConVar	hud_quickinfo( "hud_quickinfo", "1", FCVAR_ARCHIVE );

extern ConVar crosshair;

#define QUICKINFO_EVENT_DURATION	1.0f
#define	QUICKINFO_BRIGHTNESS_FULL	255
#define	QUICKINFO_BRIGHTNESS_DIM	180	// OF2: was 64, which made the brackets vanish outdoors
#define	QUICKINFO_FADE_IN_TIME		0.5f
#define QUICKINFO_FADE_OUT_TIME		2.0f

/*
==================================================
CHUDQuickInfo 
==================================================
*/

using namespace vgui;

class CHUDQuickInfo : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHUDQuickInfo, vgui::Panel );
public:
	CHUDQuickInfo( const char *pElementName );
	void Init( void );
	void VidInit( void );
	bool ShouldDraw( void );
	virtual void OnThink();
	virtual void Paint();
	
	virtual void ApplySchemeSettings( IScheme *scheme );
private:
	
	void	DrawWarning( int x, int y, bool bLeft, Color clrWarn, float &time );
	void	UpdateEventTime( void );
	bool	EventTimeElapsed( void );

	int		m_lastAmmo;
	int		m_lastHealth;

	float	m_ammoFade;
	float	m_healthFade;

	bool	m_warnAmmo;
	bool	m_warnHealth;

	bool	m_bFadedOut;
	
	bool	m_bDimmed;			// Whether or not we are dimmed down
	float	m_flLastEventTime;	// Last active event (controls dimmed state)

	CHudTexture	*m_icon_c;

	CHudTexture	*m_icon_rbn;	// right bracket
	CHudTexture	*m_icon_lbn;	// left bracket

	CHudTexture	*m_icon_rb;		// right bracket, full
	CHudTexture	*m_icon_lb;		// left bracket, full
	CHudTexture	*m_icon_rbe;	// right bracket, empty
	CHudTexture	*m_icon_lbe;	// left bracket, empty

	Color	m_clrCritical;		// OF2: low health is red; low ammo stays gHUD.m_clrCaution (amber)
};

DECLARE_HUDELEMENT( CHUDQuickInfo );

CHUDQuickInfo::CHUDQuickInfo( const char *pElementName ) :
	CHudElement( pElementName ), BaseClass( NULL, "HUDQuickInfo" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	SetHiddenBits( HIDEHUD_CROSSHAIR );
}

void CHUDQuickInfo::ApplySchemeSettings( IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );

	m_clrCritical = scheme->GetColor( "CriticalFg", Color( 255, 60, 40, 255 ) );

	SetPaintBackgroundEnabled( false );
	SetForceStereoRenderToFrameBuffer( true );
}


void CHUDQuickInfo::Init( void )
{
	m_ammoFade		= 0.0f;
	m_healthFade	= 0.0f;

	m_lastAmmo		= 0;
	m_lastHealth	= 100;

	m_warnAmmo		= false;
	m_warnHealth	= false;

	m_bFadedOut			= false;
	m_bDimmed			= false;
	m_flLastEventTime   = 0.0f;
}


void CHUDQuickInfo::VidInit( void )
{
	Init();

	m_icon_c = gHUD.GetIcon( "crosshair" );
	m_icon_rb = gHUD.GetIcon( "crosshair_right_full" );
	m_icon_lb = gHUD.GetIcon( "crosshair_left_full" );
	m_icon_rbe = gHUD.GetIcon( "crosshair_right_empty" );
	m_icon_lbe = gHUD.GetIcon( "crosshair_left_empty" );
	m_icon_rbn = gHUD.GetIcon( "crosshair_right" );
	m_icon_lbn = gHUD.GetIcon( "crosshair_left" );
}


//-----------------------------------------------------------------------------
// OF2: jet-HUD style segmented brackets. Sizes in 640x480 HUD units, scaled by YRES().
// The pair's outer edges form a box of OF2_BRACKET_ASPECT (width:height) around the crosshair.
// Segment line and gap sizes are rounded to whole pixels so the spacing is perfectly even;
// the segment count is however many fit, and the leftover pixels are split above and below.
//-----------------------------------------------------------------------------
static const float OF2_BRACKET_WIDE     = 8.0f;        // width of one bracket
static const float OF2_BRACKET_TALL     = 40.0f;       // bracket height
static const float OF2_BRACKET_ASPECT   = 5.0f / 3.0f; // width:height of the pair's outer edges
static const float OF2_SEGMENT_THICK    = 0.5f;        // segment line thickness (>= 1px)
static const float OF2_SEGMENT_GAP      = 0.25f;       // space between segments (>= 1px)
static const float OF2_RIM_GAP          = 0.25f;       // space between the segments and the spine/arms (>= 1px)
static const float OF2_UNLIT_ALPHA      = 0.2f;        // unlit segments' share of the color's alpha
static const float OF2_SCANLINE_DIM     = 0.7f;        // dimmed rows' share of the alpha, like the fonts' "scanlines" "2"

// Whole pixels, at least 1. NOTE: YRES() doesn't parenthesize its argument, so pass plain values.
static int OF2_Pixels( float flUnits )
{
	float flPixels = YRES( flUnits );
	return MAX( 1, (int)( flPixels + 0.5f ) );
}

static int OF2_BracketX( int xCenter, bool bLeft )
{
	const float flHalfWidth = OF2_BRACKET_TALL * OF2_BRACKET_ASPECT * 0.5f;
	const int halfWide = OF2_Pixels( flHalfWidth );
	return bLeft ? xCenter - halfWide
	             : xCenter + halfWide - OF2_Pixels( OF2_BRACKET_WIDE );
}

static int OF2_BracketY( float fY )
{
	return (int)fY - OF2_Pixels( OF2_BRACKET_TALL ) / 2;
}

// Filled rect drawn one pixel row at a time, with every other row dimmed (scanlines).
// Rows with ( y & 1 ) == iBrightParity are full brightness. Each row is drawn once,
// so translucent colors don't stack.
static void OF2_FillRectScanlined( int x0, int y0, int x1, int y1, Color clr, int iBrightParity )
{
	Color dim = clr;
	dim[3] = (unsigned char)( clr[3] * OF2_SCANLINE_DIM );

	for ( int row = y0; row < y1; row++ )
	{
		vgui::surface()->DrawSetColor( ( row & 1 ) == iBrightParity ? clr : dim );
		vgui::surface()->DrawFilledRect( x0, row, x1, row + 1 );
	}
}

//-----------------------------------------------------------------------------
// OF2: soft glow under a bracket. The lit parts (frame and lit segments) are
// rasterized and blurred into a white texture, rebuilt only when the bracket's
// shape or lit count changes; color and blinking come from the draw color.
//-----------------------------------------------------------------------------
static const float OF2_GLOW_STRENGTH	= 0.18f;	// glow alpha as a share of the bracket's
static const int   OF2_BACKDROP_ALPHA	= 96;		// black behind each segment stack, about 38%
static const float OF2_GLOW_RADIUS		= 1.0f;		// blur radius (HUD units)
static const float OF2_GLOW_GAIN		= 1.6f;		// the blur spreads the light thin; bring it back up
#define OF2_GLOW_MAX_RECTS	128

struct BracketGlow_t
{
	int nTexture;
	int key[6];
	int texWide, texTall, margin;
};

static void OF2_BoxBlur( float *pData, int wide, int tall, int r, bool bColumns )
{
	const int nLines = bColumns ? wide : tall, nLen = bColumns ? tall : wide, step = bColumns ? wide : 1;
	CUtlVector<float> line;
	line.SetCount( nLen );
	for ( int l = 0; l < nLines; l++ )
	{
		float *p = bColumns ? pData + l : pData + l * wide;
		for ( int i = 0; i < nLen; i++ )
			line[i] = p[i * step];
		for ( int i = 0; i < nLen; i++ )
		{
			float flSum = 0.0f;
			for ( int k = MAX( 0, i - r ); k <= MIN( nLen - 1, i + r ); k++ )
				flSum += line[k];
			p[i * step] = flSum / ( 2 * r + 1 );
		}
	}
}

// pRects: x0, y0, x1, y1 relative to the bracket's top left
static void OF2_DrawBracketGlow( bool bLeft, int x, int y, int wide, int tall, const int (*pRects)[4], int nRects, int nLit, Color clr )
{
	static BracketGlow_t s_Glow[2] = { { -1 }, { -1 } };
	BracketGlow_t &glow = s_Glow[bLeft ? 0 : 1];

	const int r = OF2_Pixels( OF2_GLOW_RADIUS );
	const int key[6] = { wide, tall, nRects, nLit, r, ScreenHeight() };
	if ( glow.nTexture == -1 || V_memcmp( key, glow.key, sizeof( key ) ) )
	{
		V_memcpy( glow.key, key, sizeof( key ) );
		glow.margin = 3 * r;
		const int w = wide + 2 * glow.margin, h = tall + 2 * glow.margin;

		CUtlVector<float> mask;
		mask.SetCount( w * h );
		V_memset( mask.Base(), 0, w * h * sizeof( float ) );
		for ( int i = 0; i < nRects; i++ )
			for ( int py = pRects[i][1]; py < pRects[i][3]; py++ )
				for ( int px = pRects[i][0]; px < pRects[i][2]; px++ )
					mask[( py + glow.margin ) * w + px + glow.margin] = 1.0f;

		for ( int pass = 0; pass < 2; pass++ )
		{
			OF2_BoxBlur( mask.Base(), w, h, r, false );
			OF2_BoxBlur( mask.Base(), w, h, r, true );
		}

		// Padded to power-of-two sizes, so the texture maps 1:1 to the screen
		glow.texWide = 1; while ( glow.texWide < w ) glow.texWide <<= 1;
		glow.texTall = 1; while ( glow.texTall < h ) glow.texTall <<= 1;
		CUtlVector<unsigned char> rgba;
		rgba.SetCount( glow.texWide * glow.texTall * 4 );
		V_memset( rgba.Base(), 0, rgba.Count() );
		for ( int py = 0; py < h; py++ )
		{
			for ( int px = 0; px < w; px++ )
			{
				unsigned char *p = &rgba[( py * glow.texWide + px ) * 4];
				p[0] = p[1] = p[2] = 255;
				p[3] = (unsigned char)( clamp( mask[py * w + px] * OF2_GLOW_GAIN, 0.0f, 1.0f ) * 255.0f );
			}
		}

		if ( glow.nTexture == -1 )
			glow.nTexture = vgui::surface()->CreateNewTextureID( true );
		vgui::surface()->DrawSetTextureRGBA( glow.nTexture, rgba.Base(), glow.texWide, glow.texTall, false, true );
	}

	Color clrGlow = clr;
	clrGlow[3] = (unsigned char)( clr[3] * OF2_GLOW_STRENGTH );
	vgui::surface()->DrawSetColor( clrGlow );
	vgui::surface()->DrawSetTexture( glow.nTexture );
	vgui::surface()->DrawTexturedRect( x - glow.margin, y - glow.margin, x - glow.margin + glow.texWide, y - glow.margin + glow.texTall );
}

static void DrawSegmentedBracket( int x, int y, bool bLeft, float flFill, Color clr )
{
	const int wide = OF2_Pixels( OF2_BRACKET_WIDE );
	int       tall = OF2_Pixels( OF2_BRACKET_TALL );
	const int line  = MAX( 1, (int)YRES( 1 ) ); // arm thickness
	const int spine = line * 2;                  // the spine is twice as thick

	// Segment column: as many whole-pixel segments as fit between the arms, with
	// OF2_RIM_GAP to the spine and both arms
	const int segThick = OF2_Pixels( OF2_SEGMENT_THICK );
	const int segGap   = OF2_Pixels( OF2_SEGMENT_GAP );
	const int rimGap   = OF2_Pixels( OF2_RIM_GAP );
	const int pitch    = segThick + segGap;
	const int column   = tall - 2 * ( line + rimGap ); // n segments take n * pitch - segGap
	const int nSegments = ( column + segGap ) / pitch;
	if ( nSegments < 1 )
		return;

	// Give up the leftover pixels (less than one pitch) so the rim gap is exact
	// top and bottom, keeping the bracket centered
	const int leftover = column - ( nSegments * pitch - segGap );
	y    += leftover / 2;
	tall -= leftover;

	const int bottom = y + tall - line - rimGap; // bottom edge of the lowest segment

	// Scanlines are phased so the segment rows are bright: with 1px segments on a
	// 2px pitch every segment stays at full brightness
	const int brightParity = ( bottom - 1 ) & 1;

	// Frame: full-height spine plus arms that stop at the spine, so the
	// translucent color is never drawn twice in the corners
	const int spineX = bLeft ? x : x + wide - spine;
	const int armX0  = bLeft ? x + spine : x;
	const int armX1  = bLeft ? x + wide : x + wide - spine;

	// Segments start at the rim gap from the spine and end a spine's width short
	// of the arm tips (the bracket's inner edge)
	const int segLen = MAX( 1, wide - 2 * spine - rimGap );
	const int nLit  = (int)( clamp( flFill, 0.0f, 1.0f ) * nSegments + 0.5f );
	const int segX0 = bLeft ? x + spine + rimGap : x + spine;
	const int segX1 = segX0 + segLen;

	// Faint backdrop behind the segment stack: its corners are the outer corners of
	// the top and bottom segments. Constant (it doesn't blink).
	{
		const int stackTop = bottom - ( nSegments - 1 ) * pitch - segThick;
		vgui::surface()->DrawSetColor( Color( 0, 0, 0, OF2_BACKDROP_ALPHA ) );
		vgui::surface()->DrawFilledRect( segX0, stackTop, segX1, bottom );
	}

	// 1 pixel line just outside the spine, in the crosshair's dark core color
	{
		static ConVarRef of2_crosshair_core_shade( "of2_crosshair_core_shade" );
		const float flShade = of2_crosshair_core_shade.IsValid() ? clamp( of2_crosshair_core_shade.GetFloat(), 0.0f, 1.0f ) : 0.715f;
		const Color normal = gHUD.m_clrNormal;
		const Color clrLine( normal[0] * flShade, normal[1] * flShade, normal[2] * flShade, clr[3] );
		const int lineX = bLeft ? x - 1 : x + wide;
		OF2_FillRectScanlined( lineX, y, lineX + 1, y + tall, clrLine, brightParity );
	}

	// Glow under the lit parts: the frame and the lit segments
	{
		int rects[OF2_GLOW_MAX_RECTS][4];
		int nRects = 0;
		rects[nRects][0] = spineX - x;	rects[nRects][1] = 0;				rects[nRects][2] = spineX + spine - x;	rects[nRects][3] = tall;	nRects++;
		rects[nRects][0] = armX0 - x;	rects[nRects][1] = 0;				rects[nRects][2] = armX1 - x;			rects[nRects][3] = line;	nRects++;
		rects[nRects][0] = armX0 - x;	rects[nRects][1] = tall - line;		rects[nRects][2] = armX1 - x;			rects[nRects][3] = tall;	nRects++;
		for ( int i = 0; i < nLit && nRects < OF2_GLOW_MAX_RECTS; i++ )
		{
			const int segBottom = bottom - i * pitch - y;
			rects[nRects][0] = segX0 - x;	rects[nRects][1] = segBottom - segThick;	rects[nRects][2] = segX1 - x;	rects[nRects][3] = segBottom;	nRects++;
		}
		OF2_DrawBracketGlow( bLeft, x, y, wide, tall, rects, nRects, nLit, clr );
	}

	OF2_FillRectScanlined( spineX, y, spineX + spine, y + tall, clr, brightParity );
	OF2_FillRectScanlined( armX0, y, armX1, y + line, clr, brightParity );
	OF2_FillRectScanlined( armX0, y + tall - line, armX1, y + tall, clr, brightParity );

	Color unlit = clr;
	unlit[3] = (unsigned char)( clr[3] * OF2_UNLIT_ALPHA );

	for ( int i = 0; i < nSegments; i++ ) // i = 0 is the bottom segment
	{
		int segBottom = bottom - i * pitch;

		OF2_FillRectScanlined( segX0, segBottom - segThick, segX1, segBottom, i < nLit ? clr : unlit, brightParity );
	}
}

void CHUDQuickInfo::DrawWarning( int x, int y, bool bLeft, Color clrWarn, float &time )
{
	float scale	= (int)( fabs(sin(gpGlobals->curtime*8.0f)) * 128.0);

	// Only fade out at the low point of our blink
	if ( time <= (gpGlobals->frametime * 200.0f) )
	{
		if ( scale < 40 )
		{
			time = 0.0f;
			return;
		}
		else
		{
			// Counteract the offset below to survive another frame
			time += (gpGlobals->frametime * 200.0f);
		}
	}

	// Update our time
	time -= (gpGlobals->frametime * 200.0f);
	Color caution = clrWarn;
	// Valve had `scale * 255` here, which overflows the 0-255 alpha (scale goes up to 128)
	caution[3] = (unsigned char)MIN( 255, (int)( scale * 2.0f ) );

	DrawSegmentedBracket( x, y, bLeft, 1.0f, caution ); // full bracket, blinking
}

//-----------------------------------------------------------------------------
// Purpose: Save CPU cycles by letting the HUD system early cull
// costly traversal.  Called per frame, return true if thinking and 
// painting need to occur.
//-----------------------------------------------------------------------------
bool CHUDQuickInfo::ShouldDraw( void )
{
	if ( !m_icon_c || !m_icon_rb || !m_icon_rbe || !m_icon_lb || !m_icon_lbe )
		return false;

	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( player == NULL )
		return false;

	if ( !crosshair.GetBool() && !IsX360() )
		return false;

	return ( CHudElement::ShouldDraw() && !engine->IsDrawingLoadingImage() );
}

//-----------------------------------------------------------------------------
// Purpose: Checks if the hud element needs to fade out
//-----------------------------------------------------------------------------
void CHUDQuickInfo::OnThink()
{
	BaseClass::OnThink();

	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( player == NULL )
		return;

	// see if we should fade in/out
	bool bFadeOut = player->IsZoomed();

	// check if the state has changed
	if ( m_bFadedOut != bFadeOut )
	{
		m_bFadedOut = bFadeOut;

		m_bDimmed = false;

		if ( bFadeOut )
		{
			g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "Alpha", 0.0f, 0.0f, 0.25f, vgui::AnimationController::INTERPOLATOR_LINEAR );
		}
		else
		{
			g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "Alpha", QUICKINFO_BRIGHTNESS_FULL, 0.0f, QUICKINFO_FADE_IN_TIME, vgui::AnimationController::INTERPOLATOR_LINEAR );
		}
	}
	else if ( !m_bFadedOut )
	{
		// If we're dormant, fade out
		if ( EventTimeElapsed() )
		{
			if ( !m_bDimmed )
			{
				m_bDimmed = true;
				g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "Alpha", QUICKINFO_BRIGHTNESS_DIM, 0.0f, QUICKINFO_FADE_OUT_TIME, vgui::AnimationController::INTERPOLATOR_LINEAR );
			}
		}
		else if ( m_bDimmed )
		{
			// Fade back up, we're active
			m_bDimmed = false;
			g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "Alpha", QUICKINFO_BRIGHTNESS_FULL, 0.0f, QUICKINFO_FADE_IN_TIME, vgui::AnimationController::INTERPOLATOR_LINEAR );
		}
	}
}

void CHUDQuickInfo::Paint()
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( player == NULL )
		return;

	C_BaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( pWeapon == NULL )
		return;

	float fX, fY;
	bool bBehindCamera = false;
	CHudCrosshair::GetDrawPosition( &fX, &fY, &bBehindCamera );

	// if the crosshair is behind the camera, don't draw it
	if( bBehindCamera )
		return;

	int		xCenter	= (int)fX;

	float	scalar  = 230.0f/255.0f;	// OF2: was 138, too faint against bright scenes
	
	// Check our health for a warning
	int	health	= player->GetHealth();
	if ( health != m_lastHealth )
	{
		UpdateEventTime();
		m_lastHealth = health;

		if ( health <= HEALTH_WARNING_THRESHOLD )
		{
			if ( m_warnHealth == false )
			{
				m_healthFade = 255;
				m_warnHealth = true;
				
				CLocalPlayerFilter filter;
				C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "HUDQuickInfo.LowHealth" );
			}
		}
		else
		{
			m_warnHealth = false;
		}
	}

	// Check our ammo for a warning
	int	ammo = pWeapon->Clip1();
	if ( ammo != m_lastAmmo )
	{
		UpdateEventTime();
		m_lastAmmo	= ammo;

		// Find how far through the current clip we are
		float ammoPerc = (float) ammo / (float) pWeapon->GetMaxClip1();

		// Warn if we're below a certain percentage of our clip's size
		if (( pWeapon->GetMaxClip1() > 1 ) && ( ammoPerc <= ( 1.0f - CLIP_PERC_THRESHOLD )))
		{
			if ( m_warnAmmo == false )
			{
				m_ammoFade = 255;
				m_warnAmmo = true;

				CLocalPlayerFilter filter;
				C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "HUDQuickInfo.LowAmmo" );
			}
		}
		else
		{
			m_warnAmmo = false;
		}
	}

	// OF2: no center dot here; the crosshair (hud_crosshair.cpp, of2_crosshair_dot) has its own

	if( IsX360() )
	{
		// Because the fixed reticle draws on half-texels, this rather unsightly hack really helps
		// center the appearance of the quickinfo on 360 displays.
		xCenter += 1;
	}

	if ( !hud_quickinfo.GetInt() )
		return;

	int	sinScale = (int)( fabs(sin(gpGlobals->curtime*8.0f)) * 128.0f );

	// Update our health
	if ( m_healthFade > 0.0f )
	{
		DrawWarning( OF2_BracketX( xCenter, true ), OF2_BracketY( fY ), true, m_clrCritical, m_healthFade );
	}
	else
	{
		float healthPerc = (float) health / 100.0f;
		healthPerc = clamp( healthPerc, 0.0f, 1.0f );

		Color healthColor = m_warnHealth ? m_clrCritical : gHUD.m_clrNormal;
		
		if ( m_warnHealth )
		{
			healthColor[3] = (unsigned char)MIN( 255, sinScale * 2 ); // was 255 * sinScale, overflowed
		}
		else
		{
			healthColor[3] = 255 * scalar;
		}

		DrawSegmentedBracket( OF2_BracketX( xCenter, true ), OF2_BracketY( fY ), true, healthPerc, healthColor );
	}

	// Update our ammo
	if ( m_ammoFade > 0.0f )
	{
		DrawWarning( OF2_BracketX( xCenter, false ), OF2_BracketY( fY ), false, gHUD.m_clrCaution, m_ammoFade );
	}
	else
	{
		float ammoPerc;

		if ( pWeapon->GetMaxClip1() <= 0 )
		{
			ammoPerc = 0.0f;
		}
		else
		{
			ammoPerc = 1.0f - ( (float) ammo / (float) pWeapon->GetMaxClip1() );
			ammoPerc = clamp( ammoPerc, 0.0f, 1.0f );
		}

		Color ammoColor = m_warnAmmo ? gHUD.m_clrCaution : gHUD.m_clrNormal;
		
		if ( m_warnAmmo )
		{
			ammoColor[3] = (unsigned char)MIN( 255, sinScale * 2 ); // was 255 * sinScale, overflowed
		}
		else
		{
			ammoColor[3] = 255 * scalar;
		}

		// Valve's ammoPerc is the EMPTY fraction, hence 1.0f - ammoPerc
		DrawSegmentedBracket( OF2_BracketX( xCenter, false ), OF2_BracketY( fY ), false, 1.0f - ammoPerc, ammoColor );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHUDQuickInfo::UpdateEventTime( void )
{
	m_flLastEventTime = gpGlobals->curtime;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CHUDQuickInfo::EventTimeElapsed( void )
{
	if (( gpGlobals->curtime - m_flLastEventTime ) > QUICKINFO_EVENT_DURATION )
		return true;

	return false;
}

