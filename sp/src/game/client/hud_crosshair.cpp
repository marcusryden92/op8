//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "hud.h"
#include "hud_crosshair.h"
#include "iclientmode.h"
#include "view.h"
#include "vgui_controls/Controls.h"
#include "vgui/ISurface.h"
#include "ivrenderview.h"
#include "materialsystem/imaterialsystem.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "client_virtualreality.h"
#include "sourcevr/isourcevirtualreality.h"

#ifdef SIXENSE
#include "sixense/in_sixense.h"
#endif

#ifdef PORTAL
#include "c_portal_player.h"
#endif // PORTAL

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar crosshair( "crosshair", "1", FCVAR_ARCHIVE );
ConVar cl_observercrosshair( "cl_observercrosshair", "1", FCVAR_ARCHIVE );

// OF2: Counter-Strike style crosshair, drawn in code instead of the weapon's font glyph.
// Sizes are in 640x480 HUD units and scale with the resolution.
static ConVar of2_crosshair( "of2_crosshair", "1", FCVAR_ARCHIVE, "Draw the OF2 line crosshair instead of the weapon's crosshair glyph" );
static ConVar of2_crosshair_size( "of2_crosshair_size", "3", FCVAR_ARCHIVE, "Length of each crosshair line (HUD units)" );
static ConVar of2_crosshair_gap( "of2_crosshair_gap", "2", FCVAR_ARCHIVE, "Gap between the center and each line at rest (HUD units)" );
static ConVar of2_crosshair_thickness( "of2_crosshair_thickness", "1.2", FCVAR_ARCHIVE, "Line thickness (HUD units, at least 1 pixel; odd pixel widths center the core stroke)" );
static ConVar of2_crosshair_dot( "of2_crosshair_dot", "0", FCVAR_ARCHIVE, "Draw a center dot" );
static ConVar of2_crosshair_core( "of2_crosshair_core", "1", FCVAR_ARCHIVE, "Darken the inner end of each line (the part nearest the center)" );
static ConVar of2_crosshair_core_length( "of2_crosshair_core_length", "0", FCVAR_ARCHIVE, "Length of the dark inner part (HUD units). 0 = a square as long as the line is thick" );
static ConVar of2_crosshair_core_shade( "of2_crosshair_core_shade", "0.715", FCVAR_ARCHIVE, "Brightness of the dark inner part, as a fraction of the HUD color (same hue)" );

// Dynamic spread: the gap opens with each shot and while moving, and recovers
static ConVar of2_crosshair_dynamic( "of2_crosshair_dynamic", "1", FCVAR_ARCHIVE, "Open the crosshair when firing and moving, like Counter-Strike" );
static ConVar of2_crosshair_kick( "of2_crosshair_kick", "1.2", FCVAR_ARCHIVE, "Gap added per shot (HUD units)" );
static ConVar of2_crosshair_maxspread( "of2_crosshair_maxspread", "8", FCVAR_ARCHIVE, "Most the shots can open the gap (HUD units)" );
static ConVar of2_crosshair_recover( "of2_crosshair_recover", "5", FCVAR_ARCHIVE, "How fast the shot spread recovers (per second; higher is faster)" );
static ConVar of2_crosshair_movespread( "of2_crosshair_movespread", "3", FCVAR_ARCHIVE, "Gap added at full sprint speed (HUD units)" );
static ConVar of2_crosshair_airspread( "of2_crosshair_airspread", "3", FCVAR_ARCHIVE, "Gap added while in the air (HUD units)" );

// HUD units to whole pixels, at least 1
static int OF2_CrosshairPixels( float flUnits )
{
	float flPixels = YRES( flUnits );
	return MAX( 1, (int)( flPixels + 0.5f ) );
}

// One crosshair line. With the core on, its inner end (the part nearest the
// crosshair center) is a dark shade of the same color, full line width; the two
// parts don't overlap. bHorizontal: the line runs along x. bInnerIsMax: the end
// toward the center is x1/y1.
static void OF2_CrosshairLine( int x0, int y0, int x1, int y1, bool bHorizontal, bool bInnerIsMax, Color clr )
{
	const int len = bHorizontal ? x1 - x0 : y1 - y0;
	const int thick = bHorizontal ? y1 - y0 : x1 - x0;
	const int coreLen = ( of2_crosshair_core_length.GetFloat() > 0.0f ) ? OF2_CrosshairPixels( of2_crosshair_core_length.GetFloat() ) : thick;
	const int core = of2_crosshair_core.GetBool() ? MIN( len - 1, coreLen ) : 0; // at least 1 pixel stays bright

	// Split the line at the core boundary
	int bx0 = x0, by0 = y0, bx1 = x1, by1 = y1; // bright part
	int dx0 = x0, dy0 = y0, dx1 = x1, dy1 = y1; // dark part
	if ( bHorizontal )
	{
		if ( bInnerIsMax )	{ bx1 = dx0 = x1 - core; }
		else				{ bx0 = dx1 = x0 + core; }
	}
	else
	{
		if ( bInnerIsMax )	{ by1 = dy0 = y1 - core; }
		else				{ by0 = dy1 = y0 + core; }
	}

	vgui::surface()->DrawSetColor( clr );
	vgui::surface()->DrawFilledRect( bx0, by0, bx1, by1 );

	if ( core > 0 )
	{
		const float flShade = clamp( of2_crosshair_core_shade.GetFloat(), 0.0f, 1.0f );
		vgui::surface()->DrawSetColor( Color( clr[0] * flShade, clr[1] * flShade, clr[2] * flShade, clr[3] ) );
		vgui::surface()->DrawFilledRect( dx0, dy0, dx1, dy1 );
	}
}

// Four lines around a gap, like Counter-Strike: Source. The gap is measured from
// the edges of the center line position, so the cross is symmetric for any thickness.
static void OF2_DrawCrosshair( int cx, int cy, Color clr, float flExtraGap )
{
	const int len = OF2_CrosshairPixels( of2_crosshair_size.GetFloat() );
	const int gap = OF2_CrosshairPixels( of2_crosshair_gap.GetFloat() + flExtraGap );
	const int thick = OF2_CrosshairPixels( of2_crosshair_thickness.GetFloat() );

	// The center "line" both axes are built around
	const int x0 = cx - thick / 2, x1 = x0 + thick;
	const int y0 = cy - thick / 2, y1 = y0 + thick;

	OF2_CrosshairLine( x0 - gap - len, y0, x0 - gap, y1, true, true, clr );		// left
	OF2_CrosshairLine( x1 + gap, y0, x1 + gap + len, y1, true, false, clr );	// right
	OF2_CrosshairLine( x0, y0 - gap - len, x1, y0 - gap, false, true, clr );	// top
	OF2_CrosshairLine( x0, y1 + gap, x1, y1 + gap + len, false, false, clr );	// bottom

	if ( of2_crosshair_dot.GetBool() )
	{
		vgui::surface()->DrawSetColor( clr );
		vgui::surface()->DrawFilledRect( x0, y0, x1, y1 );
	}
}

//-----------------------------------------------------------------------------
// OF2 dynamic spread. HL2's weapon accuracy lives on the server, so shots are
// detected here as the weapon's total ammo (magazine + reserve) going down,
// which reloads don't change. Returns the extra gap in HUD units.
//-----------------------------------------------------------------------------
static float OF2_UpdateCrosshairSpread( C_BasePlayer *pPlayer, C_BaseCombatWeapon *pWeapon )
{
	static C_BaseCombatWeapon *s_pLastWeapon = NULL;
	static int s_iLastAmmo = -1;
	static float s_flLastUpdate = 0.0f;
	static float s_flShotSpread = 0.0f;
	static float s_flMoveSpread = 0.0f;

	if ( !of2_crosshair_dynamic.GetBool() )
	{
		s_flShotSpread = s_flMoveSpread = 0.0f;
		return 0.0f;
	}

	const float dt = gpGlobals->frametime;

	int iAmmo = -1;
	if ( pWeapon )
	{
		iAmmo = MAX( 0, pWeapon->Clip1() );
		if ( pWeapon->GetPrimaryAmmoType() >= 0 )
			iAmmo += pPlayer->GetAmmoCount( pWeapon->GetPrimaryAmmoType() );
	}

	// New weapon, or we weren't drawn for a while: just take the current count
	const bool bResync = ( pWeapon != s_pLastWeapon ) || ( gpGlobals->curtime - s_flLastUpdate > 0.25f ) || ( gpGlobals->curtime < s_flLastUpdate );
	if ( !bResync && iAmmo >= 0 && iAmmo < s_iLastAmmo )
	{
		const int nShots = s_iLastAmmo - iAmmo;
		s_flShotSpread = MIN( of2_crosshair_maxspread.GetFloat(), s_flShotSpread + nShots * of2_crosshair_kick.GetFloat() );
	}
	s_pLastWeapon = pWeapon;
	s_iLastAmmo = iAmmo;
	s_flLastUpdate = gpGlobals->curtime;

	// Shots recover smoothly
	s_flShotSpread *= expf( -of2_crosshair_recover.GetFloat() * dt );

	// Movement and jumping ease in and out
	const float flSpeed = pPlayer->GetAbsVelocity().Length2D();
	float flTarget = MIN( 1.0f, flSpeed / 320.0f ) * of2_crosshair_movespread.GetFloat();
	if ( !( pPlayer->GetFlags() & FL_ONGROUND ) )
		flTarget += of2_crosshair_airspread.GetFloat();
	s_flMoveSpread += ( flTarget - s_flMoveSpread ) * MIN( 1.0f, dt * 10.0f );

	return s_flShotSpread + s_flMoveSpread;
}

using namespace vgui;

int ScreenTransform( const Vector& point, Vector& screen );

#ifdef TF_CLIENT_DLL
// If running TF, we use CHudTFCrosshair instead (which is derived from CHudCrosshair)
#else
DECLARE_HUDELEMENT( CHudCrosshair );
#endif

CHudCrosshair::CHudCrosshair( const char *pElementName ) :
		CHudElement( pElementName ), BaseClass( NULL, "HudCrosshair" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_pCrosshair = 0;

	m_clrCrosshair = Color( 0, 0, 0, 0 );

	m_vecCrossHairOffsetAngle.Init();

	SetHiddenBits( HIDEHUD_PLAYERDEAD | HIDEHUD_CROSSHAIR );
}

CHudCrosshair::~CHudCrosshair()
{
}

void CHudCrosshair::ApplySchemeSettings( IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );

	m_pDefaultCrosshair = gHUD.GetIcon("crosshair_default");
	SetPaintBackgroundEnabled( false );

    SetSize( ScreenWidth(), ScreenHeight() );

	SetForceStereoRenderToFrameBuffer( true );
}

//-----------------------------------------------------------------------------
// Purpose: Save CPU cycles by letting the HUD system early cull
// costly traversal.  Called per frame, return true if thinking and 
// painting need to occur.
//-----------------------------------------------------------------------------
bool CHudCrosshair::ShouldDraw( void )
{
	bool bNeedsDraw;

	if ( m_bHideCrosshair )
		return false;

	C_BasePlayer* pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return false;

	C_BaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
	if ( pWeapon && !pWeapon->ShouldDrawCrosshair() )
		return false;

#ifdef HL2_EPISODIC
	// OF2: the sniper scope's lines are the crosshair (hl2\hud_scope.cpp)
	extern bool OF2_IsScopeView( void );
	if ( OF2_IsScopeView() )
		return false;
#endif

#ifdef PORTAL
	C_Portal_Player *portalPlayer = ToPortalPlayer(pPlayer);
	if ( portalPlayer && portalPlayer->IsSuppressingCrosshair() )
		return false;
#endif // PORTAL

	/* disabled to avoid assuming it's an HL2 player.
	// suppress crosshair in zoom.
	if ( pPlayer->m_HL2Local.m_bZooming )
		return false;
	*/

	// draw a crosshair only if alive or spectating in eye
	if ( IsX360() )
	{
		bNeedsDraw = m_pCrosshair && 
			!engine->IsDrawingLoadingImage() &&
			!engine->IsPaused() && 
			( !pPlayer->IsSuitEquipped() || g_pGameRules->IsMultiplayer() ) &&
			g_pClientMode->ShouldDrawCrosshair() &&
			!( pPlayer->GetFlags() & FL_FROZEN ) &&
			( pPlayer->entindex() == render->GetViewEntity() ) &&
			( pPlayer->IsAlive() ||	( pPlayer->GetObserverMode() == OBS_MODE_IN_EYE ) || ( cl_observercrosshair.GetBool() && pPlayer->GetObserverMode() == OBS_MODE_ROAMING ) );
	}
	else
	{
		bNeedsDraw = m_pCrosshair && 
			crosshair.GetInt() &&
			!engine->IsDrawingLoadingImage() &&
			!engine->IsPaused() && 
			g_pClientMode->ShouldDrawCrosshair() &&
			!( pPlayer->GetFlags() & FL_FROZEN ) &&
			( pPlayer->entindex() == render->GetViewEntity() ) &&
			!pPlayer->IsInVGuiInputMode() &&
			( pPlayer->IsAlive() ||	( pPlayer->GetObserverMode() == OBS_MODE_IN_EYE ) || ( cl_observercrosshair.GetBool() && pPlayer->GetObserverMode() == OBS_MODE_ROAMING ) );
	}

	return ( bNeedsDraw && CHudElement::ShouldDraw() );
}

#ifdef TF_CLIENT_DLL
extern ConVar cl_crosshair_red;
extern ConVar cl_crosshair_green;
extern ConVar cl_crosshair_blue;
extern ConVar cl_crosshair_scale;
#endif


void CHudCrosshair::GetDrawPosition ( float *pX, float *pY, bool *pbBehindCamera, QAngle angleCrosshairOffset )
{
	QAngle curViewAngles = CurrentViewAngles();
	Vector curViewOrigin = CurrentViewOrigin();

	int vx, vy, vw, vh;
	vgui::surface()->GetFullscreenViewport( vx, vy, vw, vh );

	float screenWidth = vw;
	float screenHeight = vh;

	float x = screenWidth / 2;
	float y = screenHeight / 2;

	bool bBehindCamera = false;

	C_BasePlayer* pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( ( pPlayer != NULL ) && ( pPlayer->GetObserverMode()==OBS_MODE_NONE ) )
	{
		bool bUseOffset = false;
		
		Vector vecStart;
		Vector vecEnd;

		if ( UseVR() )
		{
			// These are the correct values to use, but they lag the high-speed view data...
			vecStart = pPlayer->Weapon_ShootPosition();
			Vector vecAimDirection = pPlayer->GetAutoaimVector( 1.0f );
			// ...so in some aim modes, they get zapped by something completely up-to-date.
			g_ClientVirtualReality.OverrideWeaponHudAimVectors ( &vecStart, &vecAimDirection );
			vecEnd = vecStart + vecAimDirection * MAX_TRACE_LENGTH;

			bUseOffset = true;
		}

#ifdef SIXENSE
		// TODO: actually test this Sixsense code interaction with things like HMDs & stereo.
        if ( g_pSixenseInput->IsEnabled() && !UseVR() )
		{
			// Never autoaim a predicted weapon (for now)
			vecStart = pPlayer->Weapon_ShootPosition();
			Vector aimVector;
			AngleVectors( CurrentViewAngles() - g_pSixenseInput->GetViewAngleOffset(), &aimVector );
			// calculate where the bullet would go so we can draw the cross appropriately
			vecEnd = vecStart + aimVector * MAX_TRACE_LENGTH;
			bUseOffset = true;
		}
#endif

		if ( bUseOffset )
		{
			trace_t tr;
			UTIL_TraceLine( vecStart, vecEnd, MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &tr );

			Vector screen;
			screen.Init();
			bBehindCamera = ScreenTransform(tr.endpos, screen) != 0;

			x = 0.5f * ( 1.0f + screen[0] ) * screenWidth + 0.5f;
			y = 0.5f * ( 1.0f - screen[1] ) * screenHeight + 0.5f;
		}
	}

	// MattB - angleCrosshairOffset is the autoaim angle.
	// if we're not using autoaim, just draw in the middle of the 
	// screen
	if ( angleCrosshairOffset != vec3_angle )
	{
		QAngle angles;
		Vector forward;
		Vector point, screen;

		// this code is wrong
		angles = curViewAngles + angleCrosshairOffset;
		AngleVectors( angles, &forward );
		VectorAdd( curViewOrigin, forward, point );
		ScreenTransform( point, screen );

		x += 0.5f * screen[0] * screenWidth + 0.5f;
		y += 0.5f * screen[1] * screenHeight + 0.5f;
	}

	*pX = x;
	*pY = y;
	*pbBehindCamera = bBehindCamera;
}


void CHudCrosshair::Paint( void )
{
	if ( !m_pCrosshair )
		return;

	if ( !IsCurrentViewAccessAllowed() )
		return;

	C_BasePlayer* pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return;

	float x, y;
	bool bBehindCamera;
	GetDrawPosition ( &x, &y, &bBehindCamera, m_vecCrossHairOffsetAngle );

	if( bBehindCamera )
		return;

	if ( of2_crosshair.GetBool() )
	{
		Color clr = gHUD.m_clrNormal;
		clr[3] = 255;
		const float flSpread = OF2_UpdateCrosshairSpread( pPlayer, pPlayer->GetActiveWeapon() );
		OF2_DrawCrosshair( (int)( x + 0.5f ), (int)( y + 0.5f ), clr, flSpread );
		return;
	}

	float flWeaponScale = 1.f;
	int iTextureW = m_pCrosshair->Width();
	int iTextureH = m_pCrosshair->Height();
	C_BaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
	if ( pWeapon )
	{
		pWeapon->GetWeaponCrosshairScale( flWeaponScale );
	}

	float flPlayerScale = 1.0f;
#ifdef TF_CLIENT_DLL
	Color clr( cl_crosshair_red.GetInt(), cl_crosshair_green.GetInt(), cl_crosshair_blue.GetInt(), 255 );
	flPlayerScale = cl_crosshair_scale.GetFloat() / 32.0f;  // the player can change the scale in the options/multiplayer tab
#else
	Color clr = m_clrCrosshair;
#endif
	float flWidth = flWeaponScale * flPlayerScale * (float)iTextureW;
	float flHeight = flWeaponScale * flPlayerScale * (float)iTextureH;
	int iWidth = (int)( flWidth + 0.5f );
	int iHeight = (int)( flHeight + 0.5f );
	int iX = (int)( x + 0.5f );
	int iY = (int)( y + 0.5f );

	m_pCrosshair->DrawSelfCropped (
		iX-(iWidth/2), iY-(iHeight/2),
		0, 0,
		iTextureW, iTextureH,
		iWidth, iHeight,
		clr );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudCrosshair::SetCrosshairAngle( const QAngle& angle )
{
	VectorCopy( angle, m_vecCrossHairOffsetAngle );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudCrosshair::SetCrosshair( CHudTexture *texture, const Color& clr )
{
	m_pCrosshair = texture;
	m_clrCrosshair = clr;
}

//-----------------------------------------------------------------------------
// Purpose: Resets the crosshair back to the default
//-----------------------------------------------------------------------------
void CHudCrosshair::ResetCrosshair()
{
	SetCrosshair( m_pDefaultCrosshair, Color(255, 255, 255, 255) );
}
