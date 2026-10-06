//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: Stealth, client side. Works out how well lit the player is and
//			tells the server, which has no lighting to ask (server\hl2\of2_stealth.cpp).
//
//			The light is what the engine would light a model with at that spot:
//			lightmaps, light styles and dynamic lights. Projected textures are
//			not in it, so the night vision's fill light does not give the
//			player away.
//
//=============================================================================//

#include "cbase.h"
#include "igamesystem.h"
#include "c_baseplayer.h"
#include "c_ai_basenpc.h"
#include "cliententitylist.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar of2_stealth_light_dark( "of2_stealth_light_dark", "0.08", FCVAR_NONE, "Stealth: brightness where the player stands (0-1, see of2_stealth_light_debug) at and below which they count as in the dark." );
static ConVar of2_stealth_light_bright( "of2_stealth_light_bright", "0.45", FCVAR_NONE, "Stealth: brightness at and above which the player counts as fully lit." );
static ConVar of2_stealth_light_debug( "of2_stealth_light_debug", "0", FCVAR_NONE, "Stealth: print the brightness where the player stands and the visibility that makes." );

#define STEALTH_LIGHT_INTERVAL		0.2f	// seconds between samples
#define STEALTH_LIGHT_RESEND		1.0f	// say it again this often even if nothing changed; the server forgets after 2
#define STEALTH_LIGHT_CHANGE		0.02f	// smallest change worth telling
#define STEALTH_NPC_LIGHT_PHASES	5		// an NPC that asks for it is sampled every this many player samples (once a second)

//-----------------------------------------------------------------------------
// Purpose: Brightness at a point, 0-1. The engine's light is linear, and half
//			of it does not look half as bright.
//-----------------------------------------------------------------------------
static float SampleBrightness( const Vector &vecPoint )
{
	Vector vecColor;
	engine->ComputeLighting( vecPoint, NULL, true, vecColor );

	return sqrt( clamp( vecColor.x * 0.299f + vecColor.y * 0.587f + vecColor.z * 0.114f, 0.0f, 1.0f ) );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
class COF2StealthLight : public CAutoGameSystemPerFrame
{
public:
	COF2StealthLight() : CAutoGameSystemPerFrame( "COF2StealthLight" )
	{
		Reset();
	}

	virtual void LevelInitPostEntity()	{ Reset(); }
	virtual void Update( float frametime );

private:
	void Reset()
	{
		m_flNextSample = 0.0f;
		m_flNextSend = 0.0f;
		m_flSent = -1.0f;
		m_iPhase = 0;
	}

	float	m_flNextSample;
	float	m_flNextSend;
	float	m_flSent;
	int		m_iPhase;
};

static COF2StealthLight g_OF2StealthLight;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void COF2StealthLight::Update( float frametime )
{
	if ( !engine->IsInGame() || engine->IsPlayingDemo() )
		return;

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer || !pPlayer->IsAlive() )
		return;

	// The clock goes back on a load
	float flNow = gpGlobals->curtime;
	if ( flNow < m_flNextSample && m_flNextSample - flNow <= STEALTH_LIGHT_INTERVAL )
		return;

	m_flNextSample = flNow + STEALTH_LIGHT_INTERVAL;

	// Shins and chest, and the brighter counts: legs in shadow do not hide a lit body
	float flHeight = pPlayer->GetViewOffset().z;
	float flLight = 0.0f;
	for ( int i = 0; i < 2; i++ )
	{
		flLight = MAX( flLight, SampleBrightness( pPlayer->GetAbsOrigin() + Vector( 0, 0, flHeight * ( i ? 0.8f : 0.25f ) ) ) );
	}

	float flDark = of2_stealth_light_dark.GetFloat();
	float flBright = MAX( of2_stealth_light_bright.GetFloat(), flDark + 0.01f );
	float flVisibility = RemapValClamped( flLight, flDark, flBright, 0.0f, 1.0f );

	if ( of2_stealth_light_debug.GetBool() )
	{
		engine->Con_NPrintf( 21, "light %.3f  visibility %.2f", flLight, flVisibility );
	}

	if ( fabs( flVisibility - m_flSent ) >= STEALTH_LIGHT_CHANGE || flNow >= m_flNextSend || m_flNextSend - flNow > STEALTH_LIGHT_RESEND )
	{
		char szCommand[32];
		Q_snprintf( szCommand, sizeof( szCommand ), "of2_vis %.2f", flVisibility );
		engine->ServerCmd( szCommand );

		m_flSent = flVisibility;
		m_flNextSend = flNow + STEALTH_LIGHT_RESEND;
	}

	// NPCs that want to know how dark it is where they stand (metrocops, for their
	// flashlights). A few each time round. Their own flashlight is a projected
	// texture, which is not in this, so it cannot talk itself off again.
	m_iPhase = ( m_iPhase + 1 ) % STEALTH_NPC_LIGHT_PHASES;

	int nHighest = ClientEntityList().GetHighestEntityIndex();
	for ( int i = gpGlobals->maxClients + 1; i <= nHighest; i++ )
	{
		if ( ( i % STEALTH_NPC_LIGHT_PHASES ) != m_iPhase )
			continue;

		C_BaseEntity *pEntity = ClientEntityList().GetBaseEntity( i );
		if ( !pEntity || pEntity->IsDormant() || !pEntity->IsNPC() )
			continue;

		C_AI_BaseNPC *pNPC = pEntity->MyNPCPointer();
		if ( !pNPC || !pNPC->m_bOF2WantsLight )
			continue;

		char szCommand[48];
		Q_snprintf( szCommand, sizeof( szCommand ), "of2_npclight %d %.2f", i, SampleBrightness( pNPC->WorldSpaceCenter() ) );
		engine->ServerCmd( szCommand );
	}
}
