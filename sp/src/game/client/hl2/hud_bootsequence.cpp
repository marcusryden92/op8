//========= OF2 ===============================================================//
//
// Purpose: Suit boot sequence. Terminal-style text in the top-left corner that
//			types itself out, fills progress bars (with stalls), prints results,
//			and scrolls/fades older lines away. Plays when the suit comes online,
//			or with the suit_bootsequence console command.
//
//			Lines and timing come from scripts/hud_bootsequence.txt, which is
//			reloaded every time the sequence starts.
//
//=============================================================================//

#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include "filesystem.h"
#include <KeyValues.h>
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/Panel.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

#define BOOT_SCRIPT_FILE	"scripts/hud_bootsequence.txt"
#define BOOT_MAX_STALLS		4
#define BOOT_MAX_TEXT		128
#define BOOT_MAX_RESULT		32

enum BootResultColor_t
{
	BOOT_RESULT_NORMAL,
	BOOT_RESULT_CAUTION,
	BOOT_RESULT_CRITICAL,
};

struct BootLine_t
{
	wchar_t	szText[BOOT_MAX_TEXT];
	wchar_t	szResult[BOOT_MAX_RESULT];
	float	flTypeRate;						// characters per second, <= 0 = instant
	float	flBarTime;						// seconds to fill the bar (without stalls), 0 = no bar
	int		nStalls;
	float	flStallAt[BOOT_MAX_STALLS];		// bar fraction where it freezes
	float	flStallTime[BOOT_MAX_STALLS];	// how long it freezes
	float	flPause;						// seconds after the line is finished
	int		iResultColor;
	int		iTextColor;						// color of the text and bar
	float	flAlpha;						// brightness, burst lines are dimmer
	char	szCommand[BOOT_MAX_TEXT];		// console command run when the line finishes
};

static int ParseBootColor( const char *pszColor )
{
	if ( !V_stricmp( pszColor, "critical" ) )
		return BOOT_RESULT_CRITICAL;
	if ( !V_stricmp( pszColor, "caution" ) )
		return BOOT_RESULT_CAUTION;
	return BOOT_RESULT_NORMAL;
}

//-----------------------------------------------------------------------------
// Purpose: Fills in a burst template. {word} {WORD} {path} {hex} {hex4} {num} {kb}
//			are replaced with random picks; anything else is copied as-is.
//-----------------------------------------------------------------------------
static void ExpandBurstTemplate( const char *pszTemplate, const CUtlStringList &words, const CUtlStringList &paths, char *pszOut, int nOutSize )
{
	int nOut = 0;
	for ( const char *p = pszTemplate; *p && nOut < nOutSize - 1; )
	{
		const char *pszEnd = ( *p == '{' ) ? strchr( p, '}' ) : NULL;
		if ( !pszEnd )
		{
			pszOut[nOut++] = *p++;
			continue;
		}

		char szToken[32];
		V_strncpy( szToken, p + 1, MIN( (int)sizeof( szToken ), (int)( pszEnd - p ) ) );

		char szValue[128];
		szValue[0] = 0;
		if ( !V_strcmp( szToken, "word" ) || !V_strcmp( szToken, "WORD" ) )
		{
			if ( words.Count() )
				V_strncpy( szValue, words[ RandomInt( 0, words.Count() - 1 ) ], sizeof( szValue ) );
			if ( szToken[0] == 'W' )
				V_strupr( szValue );
		}
		else if ( !V_strcmp( szToken, "path" ) )
		{
			if ( paths.Count() )
				V_strncpy( szValue, paths[ RandomInt( 0, paths.Count() - 1 ) ], sizeof( szValue ) );
		}
		else if ( !V_strcmp( szToken, "hex" ) )
		{
			V_snprintf( szValue, sizeof( szValue ), "%04X%04X", RandomInt( 0, 0xFFFF ), RandomInt( 0, 0xFFFF ) );
		}
		else if ( !V_strcmp( szToken, "hex4" ) )
		{
			V_snprintf( szValue, sizeof( szValue ), "%04X", RandomInt( 0, 0xFFFF ) );
		}
		else if ( !V_strcmp( szToken, "num" ) )
		{
			V_snprintf( szValue, sizeof( szValue ), "%d", RandomInt( 0, 9999 ) );
		}
		else if ( !V_strncmp( szToken, "num:", 4 ) )
		{
			// {num:MIN-MAX}
			int nMin = 0, nMax = 0;
			if ( sscanf( szToken + 4, "%d-%d", &nMin, &nMax ) != 2 )
			{
				pszOut[nOut++] = *p++;
				continue;
			}
			V_snprintf( szValue, sizeof( szValue ), "%d", RandomInt( MIN( nMin, nMax ), MAX( nMin, nMax ) ) );
		}
		else if ( !V_strcmp( szToken, "kb" ) )
		{
			V_snprintf( szValue, sizeof( szValue ), "%d", RandomInt( 4, 4096 ) );
		}
		else
		{
			// Not a placeholder, keep the brace
			pszOut[nOut++] = *p++;
			continue;
		}

		for ( const char *v = szValue; *v && nOut < nOutSize - 1; v++ )
		{
			pszOut[nOut++] = *v;
		}
		p = pszEnd + 1;
	}
	pszOut[nOut] = 0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
class CHudBootSequence : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudBootSequence, vgui::Panel );

public:
	CHudBootSequence( const char *pElementName );

	virtual void	LevelInit( void );
	virtual bool	ShouldDraw( void );
	virtual void	ApplySchemeSettings( IScheme *pScheme );
	virtual void	Paint( void );

	void			Start( void );
	void			Stop( void );

private:
	enum Phase_t
	{
		PHASE_TYPE,		// typing the text
		PHASE_BAR,		// filling the bar
		PHASE_PAUSE,	// result shown, waiting for the next line
		PHASE_DONE,		// all lines finished, holding then fading out
	};

	bool	LoadScript( void );
	void	Advance( void );
	float	BarFraction( const BootLine_t &line, float flElapsed ) const;
	float	BarTotalTime( const BootLine_t &line ) const;
	void	PaintLine( int y, int iLine, float flAlpha );

	CUtlVector<BootLine_t> m_Lines;

	bool	m_bRunning;
	int		m_iLine;			// current line
	int		m_iPhase;
	float	m_flPhaseStart;		// curtime the current phase started
	float	m_flScroll;			// lines scrolled off the top (smooth)
	int		m_iSuitState;		// -1 unknown, 0 off, 1 on

	// Settings from the script
	int		m_nMaxLines;
	int		m_nBarColumn;
	int		m_nBarWidth;
	float	m_flStartDelay;
	float	m_flScrollSpeed;
	float	m_flAgeFade;
	float	m_flEndHold;
	float	m_flEndFade;

	Color	m_clrCaution;
	Color	m_clrCritical;

	CPanelAnimationVar( vgui::HFont, m_hFont, "TextFont", "HudBootText" );
	CPanelAnimationVar( Color, m_TextColor, "TextColor", "FgColor" );
};

DECLARE_HUDELEMENT( CHudBootSequence );

CON_COMMAND( suit_bootsequence, "Plays the suit boot sequence (reloads " BOOT_SCRIPT_FILE ")" )
{
	CHudBootSequence *pBoot = GET_HUDELEMENT( CHudBootSequence );
	if ( pBoot )
	{
		pBoot->Start();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CHudBootSequence::CHudBootSequence( const char *pElementName ) :
	CHudElement( pElementName ), BaseClass( NULL, "HudBootSequence" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	SetHiddenBits( HIDEHUD_PLAYERDEAD );

	m_bRunning = false;
	m_iLine = 0;
	m_iPhase = PHASE_DONE;
	m_flPhaseStart = 0.0f;
	m_flScroll = 0.0f;
	m_iSuitState = -1;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudBootSequence::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

	SetPaintBackgroundEnabled( false );

	m_clrCaution = pScheme->GetColor( "DamagedFg", Color( 255, 176, 0, 255 ) );
	m_clrCritical = pScheme->GetColor( "CriticalFg", Color( 255, 60, 40, 255 ) );
}

//-----------------------------------------------------------------------------
// Purpose: New map or loaded save: forget the suit state, so a suit that is
//			already on doesn't count as being switched on
//-----------------------------------------------------------------------------
void CHudBootSequence::LevelInit( void )
{
	Stop();
	m_iSuitState = -1;
}

//-----------------------------------------------------------------------------
// Purpose: Called every frame, even while hidden, so this is where the suit is
//			watched and the sequence advances
//-----------------------------------------------------------------------------
bool CHudBootSequence::ShouldDraw( void )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer )
	{
		int iSuitState = pPlayer->IsSuitEquipped() ? 1 : 0;
		if ( m_iSuitState == 0 && iSuitState == 1 )
		{
			Start();
		}
		m_iSuitState = iSuitState;
	}

	if ( !m_bRunning )
		return false;

	Advance();

	// Smooth scroll once there are more lines than fit. Speeds up when it falls
	// behind, so bursts rush by instead of queueing up.
	float flTarget = MAX( 0, m_iLine + 1 - m_nMaxLines );
	if ( m_flScroll < flTarget )
	{
		float flSpeed = MAX( m_flScrollSpeed, ( flTarget - m_flScroll ) * 15.0f );
		m_flScroll = MIN( flTarget, m_flScroll + flSpeed * gpGlobals->frametime );
	}

	return m_bRunning && CHudElement::ShouldDraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudBootSequence::Start( void )
{
	if ( !LoadScript() )
		return;

	m_bRunning = true;
	m_iLine = 0;
	m_iPhase = PHASE_TYPE;
	m_flPhaseStart = gpGlobals->curtime + m_flStartDelay;
	m_flScroll = 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudBootSequence::Stop( void )
{
	m_bRunning = false;
	m_iPhase = PHASE_DONE;
}

//-----------------------------------------------------------------------------
// Purpose: Reads the lines and settings. Returns false if there is nothing to play.
//-----------------------------------------------------------------------------
bool CHudBootSequence::LoadScript( void )
{
	m_Lines.RemoveAll();

	KeyValues *pKV = new KeyValues( "BootSequence" );
	if ( !pKV->LoadFromFile( g_pFullFileSystem, BOOT_SCRIPT_FILE, "GAME" ) )
	{
		Warning( "suit_bootsequence: couldn't load %s\n", BOOT_SCRIPT_FILE );
		pKV->deleteThis();
		return false;
	}

	m_flStartDelay	= pKV->GetFloat( "start_delay", 0.5f );
	m_nMaxLines		= MAX( 1, pKV->GetInt( "max_lines", 12 ) );
	m_nBarColumn	= pKV->GetInt( "bar_column", 0 );
	m_nBarWidth		= clamp( pKV->GetInt( "bar_width", 20 ), 1, BOOT_MAX_TEXT - 1 );
	m_flScrollSpeed	= MAX( 0.1f, pKV->GetFloat( "scroll_speed", 12.0f ) );
	m_flAgeFade		= clamp( pKV->GetFloat( "age_fade", 0.5f ), 0.0f, 1.0f );
	m_flEndHold		= pKV->GetFloat( "end_hold", 2.5f );
	m_flEndFade		= MAX( 0.01f, pKV->GetFloat( "end_fade", 1.5f ) );
	float flTypeRate = pKV->GetFloat( "type_rate", 45.0f );

	// Gibberish bursts
	const float flBurstRate  = MAX( 1.0f, pKV->GetFloat( "burst_rate", 70.0f ) );
	const float flBurstAlpha = clamp( pKV->GetFloat( "burst_alpha", 0.55f ), 0.0f, 1.0f );
	CUtlStringList burstWords, burstPaths;
	V_SplitString( pKV->GetString( "burst_words", "" ), " ", burstWords );
	V_SplitString( pKV->GetString( "burst_paths", "" ), " ", burstPaths );
	KeyValues *pBurstSets = pKV->FindKey( "burst_sets" );

	KeyValues *pLines = pKV->FindKey( "lines" );
	for ( KeyValues *pLine = pLines ? pLines->GetFirstTrueSubKey() : NULL; pLine; pLine = pLine->GetNextTrueSubKey() )
	{
		// "burst" "<count>": that many random lines from a template set, rushing by
		int nBurst = pLine->GetInt( "burst", 0 );
		if ( nBurst > 0 )
		{
			KeyValues *pSet = pBurstSets ? pBurstSets->FindKey( pLine->GetString( "set", "default" ) ) : NULL;
			CUtlVector<const char *> templates;
			CUtlStringList setWords, setPaths; // "words" / "paths" in the set replace the global lists
			if ( pSet )
			{
				FOR_EACH_VALUE( pSet, pTemplate )
				{
					if ( !V_stricmp( pTemplate->GetName(), "words" ) )
						V_SplitString( pTemplate->GetString(), " ", setWords );
					else if ( !V_stricmp( pTemplate->GetName(), "paths" ) )
						V_SplitString( pTemplate->GetString(), " ", setPaths );
					else
						templates.AddToTail( pTemplate->GetString() );
				}
			}
			const CUtlStringList &words = setWords.Count() ? setWords : burstWords;
			const CUtlStringList &paths = setPaths.Count() ? setPaths : burstPaths;
			const float flAlpha = clamp( pLine->GetFloat( "alpha", flBurstAlpha ), 0.0f, 1.0f );
			const int iColor = ParseBootColor( pLine->GetString( "color", "normal" ) );
			if ( !templates.Count() )
			{
				Warning( "suit_bootsequence: burst set \"%s\" has no templates\n", pLine->GetString( "set", "default" ) );
				continue;
			}

			const float flRate = MAX( 1.0f, pLine->GetFloat( "rate", flBurstRate ) );
			for ( int i = 0; i < nBurst; i++ )
			{
				BootLine_t &line = m_Lines[ m_Lines.AddToTail() ];
				V_memset( &line, 0, sizeof( line ) );

				char szText[BOOT_MAX_TEXT];
				ExpandBurstTemplate( templates[ RandomInt( 0, templates.Count() - 1 ) ], words, paths, szText, sizeof( szText ) );
				g_pVGuiLocalize->ConvertANSIToUnicode( szText, line.szText, sizeof( line.szText ) );

				// Uneven pacing, with the odd hiccup
				line.flPause = RandomFloat( 0.3f, 1.7f ) / flRate;
				if ( RandomInt( 0, 24 ) == 0 )
					line.flPause += RandomFloat( 0.1f, 0.35f );
				line.flAlpha = flAlpha;
				line.iTextColor = iColor;
			}

			// The stop after the burst, and its command
			m_Lines.Tail().flPause = pLine->GetFloat( "pause", 0.4f );
			V_strncpy( m_Lines.Tail().szCommand, pLine->GetString( "command", "" ), sizeof( m_Lines.Tail().szCommand ) );
			continue;
		}

		BootLine_t &line = m_Lines[ m_Lines.AddToTail() ];

		// "#Token" looks the text up in the localization files
		const char *pszText = pLine->GetString( "text", "" );
		const wchar_t *pszLocalized = ( pszText[0] == '#' ) ? g_pVGuiLocalize->Find( pszText ) : NULL;
		if ( pszLocalized )
			V_wcsncpy( line.szText, pszLocalized, sizeof( line.szText ) );
		else
			g_pVGuiLocalize->ConvertANSIToUnicode( pszText, line.szText, sizeof( line.szText ) );

		g_pVGuiLocalize->ConvertANSIToUnicode( pLine->GetString( "result", "" ), line.szResult, sizeof( line.szResult ) );

		line.flTypeRate	= pLine->GetFloat( "type_rate", flTypeRate );
		line.flBarTime	= MAX( 0.0f, pLine->GetFloat( "bar", 0.0f ) );
		line.flPause	= MAX( 0.0f, pLine->GetFloat( "pause", 0.2f ) );

		line.iResultColor = ParseBootColor( pLine->GetString( "result_color", "normal" ) );
		line.iTextColor	= ParseBootColor( pLine->GetString( "color", "normal" ) );
		line.flAlpha	= clamp( pLine->GetFloat( "alpha", 1.0f ), 0.0f, 1.0f );
		V_strncpy( line.szCommand, pLine->GetString( "command", "" ), sizeof( line.szCommand ) );

		// Any number of "stall" "<bar fraction> <seconds>" keys, in increasing order
		line.nStalls = 0;
		FOR_EACH_VALUE( pLine, pValue )
		{
			if ( V_stricmp( pValue->GetName(), "stall" ) || line.nStalls >= BOOT_MAX_STALLS )
				continue;

			float flAt = 0.0f, flTime = 0.0f;
			if ( sscanf( pValue->GetString(), "%f %f", &flAt, &flTime ) == 2 )
			{
				line.flStallAt[line.nStalls] = clamp( flAt, 0.0f, 1.0f );
				line.flStallTime[line.nStalls] = MAX( 0.0f, flTime );
				line.nStalls++;
			}
		}
	}

	pKV->deleteThis();

	if ( !m_Lines.Count() )
	{
		Warning( "suit_bootsequence: %s has no lines\n", BOOT_SCRIPT_FILE );
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Moves through the phases. Phase start times are advanced by exact
//			durations, so timing stays right even through frame hitches.
//-----------------------------------------------------------------------------
void CHudBootSequence::Advance( void )
{
	while ( m_iPhase != PHASE_DONE )
	{
		const BootLine_t &line = m_Lines[m_iLine];
		float flElapsed = gpGlobals->curtime - m_flPhaseStart;
		if ( flElapsed < 0.0f )
			return;

		float flDuration;
		const int iOldPhase = m_iPhase;
		switch ( m_iPhase )
		{
		case PHASE_TYPE:
			flDuration = ( line.flTypeRate > 0.0f ) ? wcslen( line.szText ) / line.flTypeRate : 0.0f;
			if ( flElapsed < flDuration )
				return;
			m_iPhase = ( line.flBarTime > 0.0f ) ? PHASE_BAR : PHASE_PAUSE;
			break;

		case PHASE_BAR:
			flDuration = BarTotalTime( line );
			if ( flElapsed < flDuration )
				return;
			m_iPhase = PHASE_PAUSE;
			break;

		default:
		case PHASE_PAUSE:
			flDuration = line.flPause;
			if ( flElapsed < flDuration )
				return;
			if ( m_iLine + 1 < m_Lines.Count() )
			{
				m_iLine++;
				m_iPhase = PHASE_TYPE;
			}
			else
			{
				m_iPhase = PHASE_DONE; // m_flPhaseStart becomes the end time
			}
			break;
		}

		m_flPhaseStart += flDuration;

		// The line just finished (result shown): run its command. Commands are
		// queued by the engine, so they run after this frame's HUD update.
		if ( iOldPhase != PHASE_PAUSE && m_iPhase == PHASE_PAUSE && line.szCommand[0] )
		{
			engine->ClientCmd_Unrestricted( line.szCommand );
		}
	}

	// Hold, fade, then finish
	if ( gpGlobals->curtime > m_flPhaseStart + m_flEndHold + m_flEndFade )
	{
		Stop();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Bar fill (0..1) after flElapsed seconds, including the stalls
//-----------------------------------------------------------------------------
float CHudBootSequence::BarFraction( const BootLine_t &line, float flElapsed ) const
{
	float flStalled = 0.0f; // stall time already passed
	for ( int i = 0; i < line.nStalls; i++ )
	{
		float flStallStart = line.flStallAt[i] * line.flBarTime + flStalled;
		if ( flElapsed < flStallStart )
			break;
		if ( flElapsed < flStallStart + line.flStallTime[i] )
			return line.flStallAt[i];
		flStalled += line.flStallTime[i];
	}
	return clamp( ( flElapsed - flStalled ) / line.flBarTime, 0.0f, 1.0f );
}

float CHudBootSequence::BarTotalTime( const BootLine_t &line ) const
{
	float flTotal = line.flBarTime;
	for ( int i = 0; i < line.nStalls; i++ )
	{
		flTotal += line.flStallTime[i];
	}
	return flTotal;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudBootSequence::Paint( void )
{
	if ( !m_bRunning || !m_Lines.Count() )
		return;

	// Fade everything out after the last line
	float flAlpha = 1.0f;
	if ( m_iPhase == PHASE_DONE )
	{
		float flFading = gpGlobals->curtime - ( m_flPhaseStart + m_flEndHold );
		if ( flFading > 0.0f )
			flAlpha = 1.0f - flFading / m_flEndFade;
	}
	if ( flAlpha <= 0.0f )
		return;

	const int lineTall = surface()->GetFontTall( m_hFont );
	const int nShown = m_iLine + 1;

	for ( int i = MAX( 0, (int)m_flScroll - 1 ); i < nShown; i++ )
	{
		// Position in lines from the top; the first slot is headroom where
		// lines fade out as they scroll away
		float flPos = i - m_flScroll;
		if ( flPos <= -1.0f )
			continue;

		float flLineAlpha = flAlpha * m_Lines[i].flAlpha;
		if ( flPos < 0.0f )
			flLineAlpha *= 1.0f + flPos;

		// Older lines dim as new ones are written
		flLineAlpha *= 1.0f - m_flAgeFade * (float)( nShown - 1 - i ) / m_nMaxLines;

		if ( flLineAlpha > 0.0f )
		{
			PaintLine( (int)( ( flPos + 1.0f ) * lineTall ), i, flLineAlpha );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: One line: typed text, bar at a fixed column, "; result", cursor
//-----------------------------------------------------------------------------
void CHudBootSequence::PaintLine( int y, int iLine, float flAlpha )
{
	const BootLine_t &line = m_Lines[iLine];
	const bool bCurrent = ( iLine == m_iLine && m_iPhase != PHASE_DONE );
	const float flElapsed = bCurrent ? gpGlobals->curtime - m_flPhaseStart : 0.0f;
	const int charWide = surface()->GetCharacterWidth( m_hFont, '0' ); // monospaced font

	Color clrText = ( line.iTextColor == BOOT_RESULT_CRITICAL ) ? m_clrCritical :
					( line.iTextColor == BOOT_RESULT_CAUTION ) ? m_clrCaution : m_TextColor;
	clrText[3] = (unsigned char)( clrText[3] * clamp( flAlpha, 0.0f, 1.0f ) );

	surface()->DrawSetTextFont( m_hFont );
	surface()->DrawSetTextColor( clrText );

	// Text, typed out
	const int nLen = wcslen( line.szText );
	int nChars = nLen;
	if ( bCurrent && m_iPhase == PHASE_TYPE && line.flTypeRate > 0.0f )
	{
		nChars = clamp( (int)( flElapsed * line.flTypeRate ), 0, nLen );
	}
	surface()->DrawSetTextPos( 0, y );
	surface()->DrawPrintText( line.szText, nChars );
	int column = nChars;

	// Bar
	if ( line.flBarTime > 0.0f && !( bCurrent && m_iPhase == PHASE_TYPE ) )
	{
		int nSteps = m_nBarWidth;
		if ( bCurrent && m_iPhase == PHASE_BAR )
		{
			nSteps = (int)( BarFraction( line, flElapsed ) * m_nBarWidth );
		}

		wchar_t szBar[BOOT_MAX_TEXT];
		for ( int i = 0; i < nSteps; i++ )
		{
			szBar[i] = L'=';
		}

		column = MAX( nLen + 1, m_nBarColumn );
		surface()->DrawSetTextPos( column * charWide, y );
		surface()->DrawPrintText( szBar, nSteps );
		column += nSteps;
	}

	// Result
	if ( line.szResult[0] && ( !bCurrent || m_iPhase == PHASE_PAUSE ) )
	{
		surface()->DrawSetTextPos( column * charWide, y );
		surface()->DrawPrintText( L"; ", 2 );
		column += 2;

		if ( line.iResultColor != BOOT_RESULT_NORMAL )
		{
			Color clrResult = ( line.iResultColor == BOOT_RESULT_CRITICAL ) ? m_clrCritical : m_clrCaution;
			clrResult[3] = clrText[3];
			surface()->DrawSetTextColor( clrResult );
		}

		const int nResultLen = wcslen( line.szResult );
		surface()->DrawPrintText( line.szResult, nResultLen );
		column += nResultLen;

		surface()->DrawSetTextColor( clrText );
	}

	// Blinking cursor on the active line
	if ( bCurrent && ( (int)( gpGlobals->curtime * 4.0f ) & 1 ) == 0 )
	{
		surface()->DrawSetTextPos( column * charWide, y );
		surface()->DrawPrintText( L"_", 1 );
	}
}
