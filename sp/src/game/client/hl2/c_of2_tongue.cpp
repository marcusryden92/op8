//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: draws the Barnacle's tongue (server\hl2\weapon_barnacle.cpp).
//			The server says where it runs: the points of its loose part, and
//			the places between that and the barnacle where it bends over an
//			edge. Here it is drawn as one curved tube through all of them
//			(of2_curve.cpp), from the barnacle in the player's hand, with the
//			texture stretched once over its whole length, shaded by the light
//			around it, and ending in a blob.
//
//=============================================================================//

#include "cbase.h"
#include "hl2/of2_tongue_shared.h"
#include "of2_curve.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define TONGUE_MATERIAL			"cable/of2_tongue_beam"
#define TONGUE_SHINE_MATERIAL	"cable/of2_tongue_shine"

// The server never lets it get longer than of2_barnacle_max_length is likely to be set
#define TONGUE_RENDER_RADIUS	2048.0f

ConVar of2_tongue_smooth( "of2_tongue_smooth", "3", FCVAR_NONE, "How many pieces the Barnacle's tongue is drawn in between two of its points. 1 draws straight lines." );
ConVar of2_tongue_shine( "of2_tongue_shine", "0.35", FCVAR_NONE, "Strength of the wet highlight along the Barnacle's tongue. 0 turns it off." );
ConVar of2_tongue_shine_width( "of2_tongue_shine_width", "0.5", FCVAR_NONE, "Width of the highlight along the Barnacle's tongue, as a share of the tongue's." );
ConVar of2_tongue_min_light( "of2_tongue_min_light", "0.25", FCVAR_NONE, "The Barnacle's tongue and the climb ropes take on the light where they are, but never get darker than this." );
ConVar of2_tongue_round( "of2_tongue_round", "0.55", FCVAR_NONE, "How much darker the Barnacle's tongue and the climb ropes get towards their edges, so they read as round. 0 is flat, 1 goes to black." );
ConVar of2_tongue_side_light( "of2_tongue_side_light", "1", FCVAR_NONE, "How much each side of the Barnacle's tongue and of the climb ropes follows the light falling on it from that side: bright towards a lamp or the sky, dark away from it. 0 is the same all round." );
ConVar of2_tongue_blob_radius( "of2_tongue_blob_radius", "1.4", FCVAR_NONE, "How thick the Barnacle's tongue gets at its tip, from its middle to its edge (the rest of the tongue is 1). It ends round at that size. 1 is no thickening, 0 a cut-off end." );
ConVar of2_tongue_blob_length( "of2_tongue_blob_length", "20", FCVAR_NONE, "Over how much of its end the Barnacle's tongue thickens towards its tip." );

// hl_gamemovement.cpp
Vector OF2_TetherHoldPos( CBasePlayer *pPlayer, bool bAtWeapon );

class C_OF2Tongue : public C_BaseEntity
{
	DECLARE_CLASS( C_OF2Tongue, C_BaseEntity );

public:
	DECLARE_CLIENTCLASS();

	C_OF2Tongue();

	// Nothing of it is a model
	virtual bool	ShouldDraw( void ) { return !IsDormant(); }
	virtual RenderGroup_t GetRenderGroup( void ) { return RENDER_GROUP_OPAQUE_ENTITY; }
	virtual void	GetRenderBounds( Vector &mins, Vector &maxs );
	virtual int		DrawModel( int flags );

	// Its points are moved smoothly from one tick to the next, like the
	// objects it holds on to. (Things without a model aren't, unless they ask.)
	virtual bool	ShouldInterpolate( void ) { return true; }

private:
	// The loose part, from the tip back to where it starts; m_nNodes of them
	// (the more the longer it is). The rest of the list is all at the start.
	Vector	m_vecNodes[OF2_TONGUE_NODES];
	CInterpolatedVarArray< Vector, OF2_TONGUE_NODES > m_iv_vecNodes;
	int		m_nNodes;

	// From where the loose part starts back to the barnacle
	Vector	m_vecBends[OF2_TONGUE_MAX_BENDS];
	int		m_nBends;
	float	m_flWidth;

	CMaterialReference	m_Material;
	CMaterialReference	m_ShineMaterial;
};

IMPLEMENT_CLIENTCLASS_DT( C_OF2Tongue, DT_OF2Tongue, COF2Tongue )
	RecvPropArray3( RECVINFO_ARRAY( m_vecNodes ), RecvPropVector( RECVINFO( m_vecNodes[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_vecBends ), RecvPropVector( RECVINFO( m_vecBends[0] ) ) ),
	RecvPropInt( RECVINFO( m_nNodes ) ),
	RecvPropInt( RECVINFO( m_nBends ) ),
	RecvPropFloat( RECVINFO( m_flWidth ) ),
END_RECV_TABLE()

C_OF2Tongue::C_OF2Tongue() : m_iv_vecNodes( "C_OF2Tongue::m_iv_vecNodes" )
{
	m_nNodes = 0;
	m_nBends = 0;
	m_flWidth = 2.0f;

	for ( int i = 0; i < OF2_TONGUE_NODES; i++ )
	{
		m_vecNodes[i].Init();
	}

	AddVar( m_vecNodes, &m_iv_vecNodes, LATCH_SIMULATION_VAR );
}

//-----------------------------------------------------------------------------
// It moves with the player and can reach anywhere around them
//-----------------------------------------------------------------------------
void C_OF2Tongue::GetRenderBounds( Vector &mins, Vector &maxs )
{
	mins.Init( -TONGUE_RENDER_RADIUS, -TONGUE_RENDER_RADIUS, -TONGUE_RENDER_RADIUS );
	maxs.Init( TONGUE_RENDER_RADIUS, TONGUE_RENDER_RADIUS, TONGUE_RENDER_RADIUS );
}

int C_OF2Tongue::DrawModel( int flags )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer == NULL )
		return 0;

	if ( !m_Material.IsValid() )
	{
		m_Material.Init( TONGUE_MATERIAL, TEXTURE_GROUP_OTHER );
		m_ShineMaterial.Init( TONGUE_SHINE_MATERIAL, TEXTURE_GROUP_OTHER );
	}

	// From the barnacle, wherever the view has it this frame, over the bends
	// and along the loose part to the tip
	Vector vecPoints[1 + OF2_TONGUE_MAX_BENDS + OF2_TONGUE_NODES];
	bool bBend[1 + OF2_TONGUE_MAX_BENDS + OF2_TONGUE_NODES];
	int nPoints = 0;

	Vector vecMouth = OF2_TetherHoldPos( pPlayer, true );
	vecPoints[nPoints] = vecMouth;
	bBend[nPoints] = false;
	nPoints++;

	int nBends = clamp( m_nBends, 0, OF2_TONGUE_MAX_BENDS );
	for ( int i = nBends - 1; i >= 0; i-- )
	{
		vecPoints[nPoints] = m_vecBends[i];
		bBend[nPoints] = true;
		nPoints++;
	}

	int nNodes = clamp( m_nNodes, 0, OF2_TONGUE_NODES );
	if ( nNodes < 2 )
		return 0;

	// The loose part starts at the last bend, or at the barnacle if there is
	// none. In that case the server had the barnacle where it was a tick or
	// two ago, and the view has moved since. The points are carried along with
	// it, each by its share: all of the way at the barnacle, none at the tip.
	// For a tongue pulled straight that is exactly where they belong, so it
	// stays straight however the view turns.
	Vector vecCarry = ( nBends == 0 ) ? vecMouth - m_vecNodes[nNodes - 1] : vec3_origin;
	for ( int i = nNodes - 2; i >= 0; i-- )
	{
		float flShare = (float)i / ( nNodes - 1 );
		vecPoints[nPoints] = m_vecNodes[i] + vecCarry * flShare;
		bBend[nPoints] = false;
		nPoints++;
	}

	OF2CurveStyle_t style;
	style.pMaterial = m_Material;
	style.pShineMaterial = m_ShineMaterial;
	style.flWidth = m_flWidth;
	style.flTextureRepeat = 0.0f;
	style.nSmooth = of2_tongue_smooth.GetInt();
	style.flShine = of2_tongue_shine.GetFloat();
	style.flShineWidth = of2_tongue_shine_width.GetFloat();
	style.flMinLight = of2_tongue_min_light.GetFloat();
	style.bTube = true;
	style.flRound = of2_tongue_round.GetFloat();
	style.flSideLight = of2_tongue_side_light.GetFloat();
	style.flTipRadius = of2_tongue_blob_radius.GetFloat();
	style.flTipLength = of2_tongue_blob_length.GetFloat();

	OF2_DrawCurve( vecPoints, bBend, nPoints, style );
	return 1;
}
