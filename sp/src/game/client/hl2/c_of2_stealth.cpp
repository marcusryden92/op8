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
#include "view.h"
#include "materialsystem/imaterialvar.h"

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

//-----------------------------------------------------------------------------
// Purpose: OF2: The light a flashlight makes in the air (a metrocop's; the
//			server's of2_lightcone, which rides on the eyes). A volume: the
//			OF2_LightCone shader (stdshaders\of2_lightcone_dx9.cpp) adds up,
//			for every pixel, the light along the line of sight through the
//			cone. All that is drawn here is the cone's outside, for it to run
//			on, and what is worked out here is where the cone is and which
//			surfaces cut it off.
//-----------------------------------------------------------------------------
static ConVar of2_lightcone_edge( "of2_lightcone_edge", "1.5", FCVAR_NONE, "Flashlight cone in the air: how soft its outline is. Higher gathers the light towards the middle of the cone." );
static ConVar of2_lightcone_falloff( "of2_lightcone_falloff", "2", FCVAR_NONE, "Flashlight cone in the air: how quickly it thins out along its length. 1 is evenly, higher keeps the light near the head." );
static ConVar of2_lightcone_lens( "of2_lightcone_lens", "4", FCVAR_NONE, "Flashlight cone in the air: how wide it is where it starts, from its middle to its edge, in units." );
static ConVar of2_lightcone_clip( "of2_lightcone_clip", "1", FCVAR_NONE, "Flashlight cone in the air: cut it off at the walls, floor and ceiling it lands on, so that it is not seen from behind them." );

#define LIGHTCONE_MATERIAL			"effects/of2_lightcone"
#define LIGHTCONE_MATERIAL_INSIDE	"effects/of2_lightcone_inside"
#define LIGHTCONE_SIDES				16
#define LIGHTCONE_PLANES			4		// as many as the shader takes
#define LIGHTCONE_NEAR				14.0f	// closer to the cone than this, the eye counts as inside it

class C_OF2LightCone : public C_BaseEntity
{
	DECLARE_CLASS( C_OF2LightCone, C_BaseEntity );

public:
	DECLARE_CLIENTCLASS();

	C_OF2LightCone()
	{
		m_flConeFOV = 50.0f;
		m_flConeLength = 320.0f;
		m_flConeBrightness = 0.0f;
	}

	// Nothing of it is a model
	virtual bool	ShouldDraw( void ) { return !IsDormant(); }
	virtual bool	IsTransparent( void ) { return true; }
	virtual RenderGroup_t GetRenderGroup( void ) { return RENDER_GROUP_TRANSLUCENT_ENTITY; }
	virtual void	GetRenderBounds( Vector &mins, Vector &maxs );
	virtual int		DrawModel( int flags );

private:
	int		FindPlanes( const Vector &vecOrigin, const Vector &vecForward, const Vector &vecRight, const Vector &vecUp, float flLength, float flTan, cplane_t *pPlanes );

	float	m_flConeFOV;
	float	m_flConeLength;
	float	m_flConeBrightness;

	CMaterialReference	m_Material;
	CMaterialReference	m_MaterialInside;
};

IMPLEMENT_CLIENTCLASS_DT( C_OF2LightCone, DT_OF2LightCone, COF2LightCone )
	RecvPropFloat( RECVINFO( m_flConeFOV ) ),
	RecvPropFloat( RECVINFO( m_flConeLength ) ),
	RecvPropFloat( RECVINFO( m_flConeBrightness ) ),
END_RECV_TABLE()

//-----------------------------------------------------------------------------
// It can point any way
//-----------------------------------------------------------------------------
void C_OF2LightCone::GetRenderBounds( Vector &mins, Vector &maxs )
{
	float flReach = MAX( m_flConeLength, 16.0f ) + 16.0f;
	mins.Init( -flReach, -flReach, -flReach );
	maxs.Init( flReach, flReach, flReach );
}

//-----------------------------------------------------------------------------
// Purpose: The surfaces of the world the light lands on, looked for down its
//			middle and round its side. The shader leaves out what is behind
//			them, which is exact for the walls, floor and ceiling of a room.
//			(It leaves out everything behind them: light going on past the
//			edge of a wall it partly lands on is lost too.)
//-----------------------------------------------------------------------------
int C_OF2LightCone::FindPlanes( const Vector &vecOrigin, const Vector &vecForward, const Vector &vecRight, const Vector &vecUp, float flLength, float flTan, cplane_t *pPlanes )
{
	const int nAround = 6;
	int nPlanes = 0;

	CTraceFilterWorldOnly filter;

	for ( int i = 0; i <= nAround && nPlanes < LIGHTCONE_PLANES; i++ )
	{
		Vector vecDir = vecForward;
		if ( i > 0 )
		{
			float flSin, flCos;
			SinCos( 2.0f * M_PI * ( i - 1 ) / nAround, &flSin, &flCos );
			vecDir += ( vecRight * flCos + vecUp * flSin ) * ( flTan * 0.8f );
			VectorNormalize( vecDir );
		}

		trace_t tr;
		UTIL_TraceLine( vecOrigin, vecOrigin + vecDir * flLength, MASK_OPAQUE, &filter, &tr );
		if ( tr.fraction == 1.0f || tr.startsolid || tr.allsolid )
			continue;

		// (The lamp has to be in front of it)
		if ( DotProduct( tr.plane.normal, vecOrigin ) - tr.plane.dist < 1.0f )
			continue;

		bool bHave = false;
		for ( int j = 0; j < nPlanes; j++ )
		{
			if ( DotProduct( pPlanes[j].normal, tr.plane.normal ) > 0.98f && fabs( pPlanes[j].dist - tr.plane.dist ) < 4.0f )
			{
				bHave = true;
				break;
			}
		}

		if ( !bHave )
		{
			pPlanes[nPlanes].normal = tr.plane.normal;
			pPlanes[nPlanes].dist = tr.plane.dist;
			nPlanes++;
		}
	}

	return nPlanes;
}

//-----------------------------------------------------------------------------
// One vertex of the cone's outside
//-----------------------------------------------------------------------------
static inline void LightConeVertex( CMeshBuilder &meshBuilder, const Vector &vecPos )
{
	meshBuilder.Position3fv( vecPos.Base() );
	meshBuilder.TexCoord2f( 0, 0.0f, 0.0f );
	meshBuilder.AdvanceVertex();
}

//-----------------------------------------------------------------------------
// One triangle of it, turned so that it is seen from outside the cone
// (bFromInside: from inside it). vecMiddle is any point inside the cone.
//-----------------------------------------------------------------------------
static void LightConeTriangle( CMeshBuilder &meshBuilder, const Vector &vecA, const Vector &vecB, const Vector &vecC, const Vector &vecMiddle, bool bFromInside )
{
	// (Seen from the side its points go round clockwise on)
	Vector vecCross = CrossProduct( vecB - vecA, vecC - vecA );
	bool bFacesOut = DotProduct( vecCross, ( vecA + vecB + vecC ) * ( 1.0f / 3.0f ) - vecMiddle ) < 0.0f;

	LightConeVertex( meshBuilder, vecA );
	if ( bFacesOut != bFromInside )
	{
		LightConeVertex( meshBuilder, vecB );
		LightConeVertex( meshBuilder, vecC );
	}
	else
	{
		LightConeVertex( meshBuilder, vecC );
		LightConeVertex( meshBuilder, vecB );
	}
}

int C_OF2LightCone::DrawModel( int flags )
{
	if ( m_flConeBrightness <= 0.0f || m_flConeLength < 1.0f )
		return 0;

	if ( !m_Material.IsValid() )
	{
		m_Material.Init( LIGHTCONE_MATERIAL, TEXTURE_GROUP_OTHER );
		m_MaterialInside.Init( LIGHTCONE_MATERIAL_INSIDE, TEXTURE_GROUP_OTHER );
	}

	Vector vecOrigin = GetRenderOrigin();
	Vector vecForward, vecRight, vecUp;
	AngleVectors( GetRenderAngles(), &vecForward, &vecRight, &vecUp );

	float flLength = m_flConeLength;
	float flHalfAngle = DEG2RAD( clamp( m_flConeFOV, 1.0f, 170.0f ) * 0.5f );
	float flSin, flCos;
	SinCos( flHalfAngle, &flSin, &flCos );
	float flTan = flSin / flCos;

	// The lamp is not a point: the cone starts this wide, so its sides meet behind it
	float flLens = MAX( of2_lightcone_lens.GetFloat(), 0.25f );
	float flBehind = flLens / flTan;

	// Is the eye in it, or so close that its near side would be cut away? Then the far
	// side is drawn, over everything: nothing but the surfaces below can hide it then.
	Vector vecEye = CurrentViewOrigin() - vecOrigin;
	float flEyeAlong = DotProduct( vecEye, vecForward );
	float flEyeOut = ( vecEye - vecForward * flEyeAlong ).Length();
	bool bInside = flEyeAlong > -LIGHTCONE_NEAR && flEyeAlong < flLength + LIGHTCONE_NEAR &&
		flEyeOut < ( flLens + MAX( flEyeAlong, 0.0f ) * flTan ) * 1.03f + 0.5f + LIGHTCONE_NEAR / flCos;

	IMaterial *pMaterial = bInside ? m_MaterialInside : m_Material;
	if ( !pMaterial || pMaterial->IsErrorMaterial() )
		return 0;

	cplane_t planes[LIGHTCONE_PLANES];
	int nPlanes = of2_lightcone_clip.GetBool() ? FindPlanes( vecOrigin, vecForward, vecRight, vecUp, flLength, flTan, planes ) : 0;

	// Seen from the side, straight through the middle, this makes it as bright as the
	// server asked for (less what the soft edge takes), however wide the cone is
	float flScale = m_flConeBrightness / ( 2.0f * flTan );

	bool bFound = false;
	IMaterialVar *pVar = pMaterial->FindVar( "$coneapex", &bFound, false );
	if ( !bFound )
		return 0;

	Vector vecApex = vecOrigin - vecForward * flBehind;
	pVar->SetVecValue( vecApex.x, vecApex.y, vecApex.z, flBehind );
	pMaterial->FindVar( "$coneaxis", &bFound )->SetVecValue( vecForward.x, vecForward.y, vecForward.z, flLength );
	pMaterial->FindVar( "$coneshape", &bFound )->SetVecValue( flCos * flCos, flTan * flTan, MAX( of2_lightcone_falloff.GetFloat(), 0.0f ), MAX( of2_lightcone_edge.GetFloat(), 0.01f ) );
	pMaterial->FindVar( "$conecolor", &bFound )->SetVecValue( flScale, flScale * 0.98f, flScale * 0.92f, 0.0f );

	static const char *s_pszPlanes[LIGHTCONE_PLANES] = { "$coneplane1", "$coneplane2", "$coneplane3", "$coneplane4" };
	for ( int i = 0; i < LIGHTCONE_PLANES; i++ )
	{
		pVar = pMaterial->FindVar( s_pszPlanes[i], &bFound );
		if ( i < nPlanes )
		{
			pVar->SetVecValue( planes[i].normal.x, planes[i].normal.y, planes[i].normal.z, planes[i].dist );
		}
		else
		{
			// (Everywhere is in front of this one)
			pVar->SetVecValue( 0.0f, 0.0f, 0.0f, -1.0f );
		}
	}

	// The outside of the cone: its sides and both ends. A little bigger than the light,
	// since flat sides cut inside a round one.
	Vector vecNear[LIGHTCONE_SIDES], vecFar[LIGHTCONE_SIDES];
	float flNearRadius = flLens * 1.03f + 0.5f;
	float flFarRadius = ( flLens + flLength * flTan ) * 1.03f + 0.5f;
	Vector vecNearMiddle = vecOrigin;
	Vector vecFarMiddle = vecOrigin + vecForward * flLength;
	Vector vecMiddle = vecOrigin + vecForward * ( flLength * 0.5f );

	for ( int i = 0; i < LIGHTCONE_SIDES; i++ )
	{
		float flS, flC;
		SinCos( 2.0f * M_PI * i / LIGHTCONE_SIDES, &flS, &flC );
		Vector vecOut = vecRight * flC + vecUp * flS;

		vecNear[i] = vecNearMiddle + vecOut * flNearRadius;
		vecFar[i] = vecFarMiddle + vecOut * flFarRadius;
	}

	CMatRenderContextPtr pRenderContext( materials );
	IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, pMaterial );

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, LIGHTCONE_SIDES * 4 );

	for ( int i = 0; i < LIGHTCONE_SIDES; i++ )
	{
		int iNext = ( i + 1 ) % LIGHTCONE_SIDES;

		LightConeTriangle( meshBuilder, vecNear[i], vecNear[iNext], vecFar[iNext], vecMiddle, bInside );
		LightConeTriangle( meshBuilder, vecNear[i], vecFar[iNext], vecFar[i], vecMiddle, bInside );
		LightConeTriangle( meshBuilder, vecNearMiddle, vecNear[i], vecNear[iNext], vecMiddle, bInside );
		LightConeTriangle( meshBuilder, vecFarMiddle, vecFar[i], vecFar[iNext], vecMiddle, bInside );
	}

	meshBuilder.End();
	pMesh->Draw();

	return 1;
}
