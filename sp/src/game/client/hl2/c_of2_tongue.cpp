//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: draws the Barnacle's tongue (server\hl2\weapon_barnacle.cpp).
//			The server says where it runs: its points, and the edges the
//			straight way to its tip goes over. Here it is drawn as one curved
//			tube through the points, round the edges it lies against
//			(of2_curve.cpp), from the barnacle in the player's hand, with the
//			texture stretched once over its whole length, shaded by the light
//			around it, and ending in a blob.
//
//=============================================================================//

#include "cbase.h"
#include "hl2/of2_tongue_shared.h"
#include "hl2/of2_rope_sim.h"
#include "of2_curve.h"
#include "c_basehlplayer.h"
#include "c_baseviewmodel.h"
#include "c_te_effect_dispatch.h"
#include "iviewrender.h"
#include "view_shared.h"
#include "view.h"
#include "model_types.h"
#include "debugoverlay_shared.h"
#include "engine/ivmodelrender.h"
#include "materialsystem/imaterialvar.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define TONGUE_MATERIAL			"cable/of2_tongue_beam"
#define TONGUE_SHINE_MATERIAL	"cable/of2_tongue_shine"

// The server never lets it get longer than of2_barnacle_max_length is likely to be set
#define TONGUE_RENDER_RADIUS	2048.0f

// Most corners between two of its points the tongue is drawn going round
#define TONGUE_MAX_CORNERS		24

// of2_rope_sim.cpp
extern ConVar of2_rope_corners;

ConVar of2_tongue_smooth( "of2_tongue_smooth", "6", FCVAR_NONE, "How many pieces the Barnacle's tongue is drawn in between two of its points. 1 draws straight lines." );
ConVar of2_tongue_shine( "of2_tongue_shine", "1.4", FCVAR_NONE, "Strength of the wet highlight along the Barnacle's tongue, as a multiple of the light where it is: over 1 it burns out to white in good light. 0 turns it off." );
ConVar of2_tongue_shine_width( "of2_tongue_shine_width", "0.45", FCVAR_NONE, "Width of the highlight along the Barnacle's tongue, as a share of the tongue's." );
ConVar of2_tongue_shine_follow( "of2_tongue_shine_follow", "1", FCVAR_NONE, "How far the highlight on the Barnacle's tongue moves towards the side the light comes from. 0 keeps it down the middle." );
ConVar of2_tongue_min_light( "of2_tongue_min_light", "0.25", FCVAR_NONE, "The Barnacle's tongue and the climb ropes take on the light where they are, but never get darker than this." );
ConVar of2_tongue_round( "of2_tongue_round", "0.55", FCVAR_NONE, "How much darker the Barnacle's tongue and the climb ropes get towards their edges, so they read as round. 0 is flat, 1 goes to black." );
ConVar of2_tongue_side_light( "of2_tongue_side_light", "1", FCVAR_NONE, "How much each side of the Barnacle's tongue and of the climb ropes follows the light falling on it from that side: bright towards a lamp or the sky, dark away from it. 0 is the same all round." );
ConVar of2_tongue_blob_radius( "of2_tongue_blob_radius", "1.4", FCVAR_NONE, "How thick the Barnacle's tongue gets at its tip, from its middle to its edge (the rest of the tongue is 1). It ends round at that size. 1 is no thickening, 0 a cut-off end." );
ConVar of2_tongue_blob_length( "of2_tongue_blob_length", "20", FCVAR_NONE, "Over how much of its end the Barnacle's tongue thickens towards its tip." );
ConVar of2_tongue_blob_shade( "of2_tongue_blob_shade", "1", FCVAR_NONE, "How bright the round end of the Barnacle's tongue is next to the rest of it. Under 1 is darker." );
ConVar of2_tongue_reflect( "of2_tongue_reflect", "0.5", FCVAR_NONE, "How strongly the Barnacle's tongue mirrors its surroundings (the map's cubemaps). 0 turns it off." );
ConVar of2_tongue_bend_radius( "of2_tongue_bend_radius", "12", FCVAR_NONE, "Where the Barnacle's tongue or a climb rope goes over an edge it is drawn going round it, starting and ending this far either side. 0 turns on the spot." );

// The barnacle in the hand (the viewmodel). Not archived.
ConVar of2_barnacle_world( "of2_barnacle_world", "1", FCVAR_NONE, "Draw the Barnacle in the hand where it is in the world, together with the start of its tongue, so that the tongue comes out of its mouth and not from behind it. 0 draws it as any other weapon, over the tongue." );
ConVar of2_barnacle_align( "of2_barnacle_align", "180", FCVAR_NONE, "While its tongue is fixed to something that holds the player, the Barnacle in the hand turns to point along it, up to this many degrees from where it points otherwise. 180 is all the way, 0 turns it off." );
ConVar of2_barnacle_mouth_out( "of2_barnacle_mouth_out", "16", FCVAR_NONE, "How far out of the Barnacle in the hand its tongue starts, the way its mouth points, from the root of the tongue inside the model. Negative is deeper in." );
ConVar of2_barnacle_pivot( "of2_barnacle_pivot", "10", FCVAR_NONE, "The Barnacle in the hand turns along its tongue about a point this far in from its rear end, the end towards the player." );
ConVar of2_barnacle_lift( "of2_barnacle_lift", "1", FCVAR_NONE, "The Barnacle in the hand is held up, to throw over it, when something close by (a ledge the player stands on, a fence) is between its mouth and what the crosshair is on. 0 turns it off." );
ConVar of2_barnacle_lift_up( "of2_barnacle_lift_up", "50", FCVAR_NONE, "Held up, the Barnacle in the hand is this much higher (as of2_viewmodel_up)." );
ConVar of2_barnacle_lift_pitch( "of2_barnacle_lift_pitch", "30", FCVAR_NONE, "Held up, the Barnacle in the hand is tilted down by this much (as of2_viewmodel_pitch)." );
ConVar of2_barnacle_lift_yaw( "of2_barnacle_lift_yaw", "0", FCVAR_NONE, "Held up, the Barnacle in the hand is turned left by this much (as of2_viewmodel_yaw; negative is right)." );
ConVar of2_barnacle_lift_range( "of2_barnacle_lift_range", "50", FCVAR_NONE, "How close what is in the way of the Barnacle has to be for it to be held up over it." );
ConVar of2_barnacle_lift_time( "of2_barnacle_lift_time", "0.35", FCVAR_NONE, "How long the Barnacle in the hand takes to be held up, and to come back down." );
ConVar of2_barnacle_lift_debug( "of2_barnacle_lift_debug", "0", FCVAR_NONE, "Draws what holding the Barnacle up goes by: the way from its lowered mouth to what the crosshair is on (green clear, red blocked, with a box where it stops), a cross on what the crosshair is on, the mouth as it is now (yellow), and the state as text." );
ConVar of2_barnacle_flick( "of2_barnacle_flick", "104", FCVAR_NONE, "How far the Barnacle in the hand whips its mouth up when it snaps a neck, in degrees. 0 turns it off." );
ConVar of2_barnacle_flick_raise( "of2_barnacle_flick_raise", "1.3", FCVAR_NONE, "How far the Barnacle in the hand rises when it whips, as a multiple of what the motion was drawn up with." );
ConVar of2_barnacle_equip_time( "of2_barnacle_equip_time", "0.4", FCVAR_NONE, "How long the Barnacle takes to come up into the hand when it is brought out. 0 has it there at once." );
ConVar of2_barnacle_equip_drop( "of2_barnacle_equip_drop", "14", FCVAR_NONE, "How far below its place the Barnacle starts from when it is brought out." );
ConVar of2_barnacle_equip_tilt( "of2_barnacle_equip_tilt", "25", FCVAR_NONE, "How far the Barnacle hangs its mouth down when it starts coming up into the hand, in degrees." );

// hl_gamemovement.cpp
Vector OF2_TetherHoldPos( CBasePlayer *pPlayer, bool bAtWeapon );

//-----------------------------------------------------------------------------
// The barnacle in the hand
//-----------------------------------------------------------------------------
#define BARNACLE_CLASSNAME		"weapon_barnacle"
// The root of the model's own tongue, which is never drawn: the back of the mouth
#define BARNACLE_MOUTH_BONE		"Barnacle.tongue1"

// The whip before a neck snap: when its keys fall, as shares of the time it
// takes, and how high the front and the back of the barnacle are at each, in
// lengths of the barnacle. It crouches, nose down (the charge); the back is
// thrown up with the nose still hanging; it goes on up, more than its own length
// (the user asked for a lot), as the nose whips up and over: the snap, the fourth key; the
// nose bounces back off the top; and all of it settles. The user asked for a
// much deeper charge and a whiplash at the end, after a gentler up-and-down
// through five keys of their own (0, -0.15, 0.15, 1.45, 0.725, 0 in front;
// 0, 0.15, 0.45, 0.45, 0.225, 0 behind) did not look like enough to break a neck.
#define BARNACLE_FLICK_KEYS		7
#define BARNACLE_FLICK_SNAP_KEY	3
static const float s_flFlickKey[BARNACLE_FLICK_KEYS] = { 0.0f, 0.292f, 0.365f, OF2_BARNACLE_FLICK_SNAP, 0.489f, 0.730f, 1.0f };
static const float s_flFlickFront[BARNACLE_FLICK_KEYS] = { 0.0f, -1.20f, 0.0f, 2.60f, 2.05f, 0.80f, 0.0f };
static const float s_flFlickBack[BARNACLE_FLICK_KEYS] = { 0.0f, -0.50f, 0.70f, 1.60f, 1.30f, 0.50f, 0.0f };
// At least this much of the tongue from the mouth is drawn again with the barnacle (OF2_DrawBarnacleOverlay)
#define TONGUE_OVERLAY_LENGTH	32.0f
// ...or this much, when something that should hide it is nearer than that
#define TONGUE_OVERLAY_HIDDEN	8.0f
// A point of the tongue counts as in sight if what is in the way is this close to it (it lies on that)
#define TONGUE_SIGHT_CLEAR		6.0f
// A point this close in front of the plane through an edge is at the edge
#define TONGUE_EDGE_NEAR		0.5f
// How long the barnacle takes to turn along a tongue that takes the player's weight, and back
#define BARNACLE_ALIGN_TIME		0.2f
// Where the rear end of the model is along its length (the mesh runs from 7 down to -65, the mouth)
#define BARNACLE_REAR_Z			7.16f

// Where the mouth is in the model, found whenever the tongue is drawn
static Vector	s_vecMouthLocal( 0, 0, 0 );
static bool		s_bMouthKnown = false;
static bool		s_bDrawingOverlay = false;
static float	s_flFlickTime = -1000.0f;
static float	s_flFlickLength = 0.0f;
// The server is told where the mouth is when it has moved this far from where
// it holds the tongue, and no more often than that
#define BARNACLE_MOUTH_CHANGE	2.0f
#define BARNACLE_MOUTH_RESEND	0.15f
static float	s_flNextMouthSend = 0.0f;
// Turning along the tongue: how far in (0-1), and where the tongue went when last seen, from the eyes
static float	s_flAlign = 0.0f;
static Vector	s_vecAlignLocal( 1, 0, 0 );
static float	s_flAlignTime = 0.0f;
// Coming up into the hand: when it was last in it, and when it last came out
static float	s_flShownTime = -1000.0f;
static float	s_flEquipTime = -1000.0f;
// Held up over what is in the way of the mouth (of2_barnacle_lift): how far into that pose (0-1),
// whether it should be, and when the way was last seen blocked
static float	s_flLift = 0.0f;
static bool		s_bLiftWanted = false;
static float	s_flLiftBlockedTime = -1000.0f;
// It comes down once the way has been clear this long; the way has to be clear this wide,
// and is looked at this far out
#define BARNACLE_LIFT_HOLD		0.5f
#define BARNACLE_LIFT_WIDE		3.0f
#define BARNACLE_LIFT_REACH		1500.0f
// Not in the hand for this long, it has been away
#define BARNACLE_AWAY_TIME		0.1f

static bool OF2_IsBarnacle( C_BaseViewModel *pViewModel )
{
	C_BaseCombatWeapon *pWeapon = pViewModel ? pViewModel->GetOwningWeapon() : NULL;
	return pWeapon != NULL && Q_stricmp( pWeapon->GetWpnData().szClassName, BARNACLE_CLASSNAME ) == 0;
}

static bool OF2_BarnacleInWorld( C_BaseViewModel *pViewModel )
{
	return of2_barnacle_world.GetBool() && OF2_IsBarnacle( pViewModel );
}

//-----------------------------------------------------------------------------
// c_baseviewmodel.cpp: it is not drawn with the viewmodels then
//-----------------------------------------------------------------------------
bool OF2_BarnacleSkipViewModelPass( C_BaseViewModel *pViewModel )
{
	// Nor at all once the player is dead. (It stayed on screen: the user saw that.
	// By then the weapon may be gone, so this goes by the model.)
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer != NULL && !pPlayer->IsAlive() && pViewModel != NULL && pViewModel->GetModel() != NULL
		&& Q_stristr( modelinfo->GetModelName( pViewModel->GetModel() ), "v_barnacle" ) != NULL )
		return true;

	return !s_bDrawingOverlay && OF2_BarnacleInWorld( pViewModel );
}

//-----------------------------------------------------------------------------
// Where the tongue comes out of the barnacle. Drawn with the world, that is
// the model's mouth; drawn as a viewmodel the model isn't anywhere in the
// world, and it is the point the server has (pvecMouth is left alone).
//-----------------------------------------------------------------------------
static void OF2_BarnacleMouth( C_BasePlayer *pPlayer, Vector *pvecMouth )
{
	C_BaseViewModel *pViewModel = pPlayer->GetViewModel( 0 );
	if ( !OF2_IsBarnacle( pViewModel ) )
		return;

	int iBone = pViewModel->LookupBone( BARNACLE_MOUTH_BONE );
	if ( iBone < 0 )
		return;

	pViewModel->SetupBones( NULL, -1, BONE_USED_BY_ANYTHING, gpGlobals->curtime );

	matrix3x4_t matBone;
	pViewModel->GetBoneTransform( iBone, matBone );

	Vector vecBone;
	MatrixGetColumn( matBone, 3, vecBone );

	VectorITransform( vecBone, pViewModel->EntityToWorldTransform(), s_vecMouthLocal );

	// That bone is deep inside the body. The tongue starts further out the way
	// the mouth points (the model hangs mouth down), at the opening: from the
	// bone itself it read as coming from the middle of the model (the user saw that).
	s_vecMouthLocal.z -= of2_barnacle_mouth_out.GetFloat();
	VectorTransform( s_vecMouthLocal, pViewModel->EntityToWorldTransform(), vecBone );

	s_bMouthKnown = true;

	if ( OF2_BarnacleInWorld( pViewModel ) )
	{
		*pvecMouth = vecBone;
	}
}

//-----------------------------------------------------------------------------
// One end of the barnacle during its whip: a smooth line through the keys.
// At the snap there is a corner in it: it gets there at full speed and leaves
// at once the other way, a crack and not a swing. (Eased into the top and out
// of it, as it was for a while, it read as too soft.)
//-----------------------------------------------------------------------------
static float FlickSlope( const float *pHeight, int iKey )
{
	if ( iKey <= 0 || iKey >= BARNACLE_FLICK_KEYS - 1 )
		return 0.0f;

	return ( pHeight[iKey + 1] - pHeight[iKey - 1] ) / ( s_flFlickKey[iKey + 1] - s_flFlickKey[iKey - 1] );
}

static float FlickHeight( const float *pHeight, float flAt )
{
	int iKey = 0;
	while ( iKey < BARNACLE_FLICK_KEYS - 2 && flAt >= s_flFlickKey[iKey + 1] )
	{
		iKey++;
	}

	float flSpan = s_flFlickKey[iKey + 1] - s_flFlickKey[iKey];
	float t = clamp( ( flAt - s_flFlickKey[iKey] ) / flSpan, 0.0f, 1.0f );

	float flOut = FlickSlope( pHeight, iKey ) * flSpan;
	float flIn = FlickSlope( pHeight, iKey + 1 ) * flSpan;
	if ( iKey == BARNACLE_FLICK_SNAP_KEY )
	{
		flOut = pHeight[iKey + 1] - pHeight[iKey];
	}
	if ( iKey + 1 == BARNACLE_FLICK_SNAP_KEY )
	{
		flIn = pHeight[iKey + 1] - pHeight[iKey];
	}

	float t2 = t * t;
	float t3 = t2 * t;
	return ( 2.0f * t3 - 3.0f * t2 + 1.0f ) * pHeight[iKey] + ( t3 - 2.0f * t2 + t ) * flOut
		+ ( -2.0f * t3 + 3.0f * t2 ) * pHeight[iKey + 1] + ( t3 - t2 ) * flIn;
}

static void RotateAbout( matrix3x4_t &mat, const Vector &vecAxis, float flDegrees, const Vector &vecPivot )
{
	matrix3x4_t matTurn, matOut;
	MatrixBuildRotationAboutAxis( vecAxis, flDegrees, matTurn );

	Vector vecTurned;
	VectorRotate( vecPivot, matTurn, vecTurned );
	MatrixSetColumn( vecPivot - vecTurned, 3, matTurn );

	ConcatTransforms( matTurn, mat, matOut );
	mat = matOut;
}

//-----------------------------------------------------------------------------
// baseviewmodel_shared.cpp, every frame, with the viewmodel placed as its
// script has it (vecOrigin, angles): what the barnacle does on top of that.
//-----------------------------------------------------------------------------
void OF2_BarnacleViewModelPose( C_BasePlayer *pOwner, C_BaseViewModel *pViewModel, const Vector &vecEye, const QAngle &angEyes, Vector &vecOrigin, QAngle &angles )
{
	if ( pOwner == NULL || !OF2_IsBarnacle( pViewModel ) )
		return;

	Vector vecForward, vecRight, vecUp;
	AngleVectors( angEyes, &vecForward, &vecRight, &vecUp );

	// The viewmodels are drawn with a narrower field of view than the world: a
	// thing looks the same size, and is in the same place on screen, in the
	// world as it does this many times further away as a viewmodel
	float flNarrow = 1.0f;
	const CViewSetup *pSetup = view->GetPlayerViewSetup();
	if ( pSetup != NULL && pSetup->fov > 1.0f && pSetup->fovViewmodel > 1.0f && pSetup->fov < 179.0f && pSetup->fovViewmodel < 179.0f )
	{
		flNarrow = tan( DEG2RAD( pSetup->fov * 0.5f ) ) / tan( DEG2RAD( pSetup->fovViewmodel * 0.5f ) );
	}

	// Brought out: it comes up from below (the end of this function)
	if ( !pViewModel->IsEffectActive( EF_NODRAW ) )
	{
		float flNow = gpGlobals->curtime;
		if ( flNow - s_flShownTime > BARNACLE_AWAY_TIME || flNow < s_flShownTime )
		{
			s_flEquipTime = flNow;
		}
		s_flShownTime = flNow;
	}

	bool bWorld = OF2_BarnacleInWorld( pViewModel );
	if ( bWorld )
	{
		// So in the world it comes that much closer, and looks as its script placed it
		float flAhead = DotProduct( vecOrigin - vecEye, vecForward );
		vecOrigin -= vecForward * ( flAhead - flAhead / flNarrow );
	}

	matrix3x4_t mat;
	AngleMatrix( angles, vecOrigin, mat );

	float flTime = gpGlobals->curtime;
	float flPassed = clamp( flTime - s_flAlignTime, 0.0f, 0.1f );
	s_flAlignTime = flTime;

	// Hanging from the tongue, it points along it: turned about a point near its
	// rear end (below), so mouth and tongue lie on one line, and no further than
	// the cone allows
	C_BaseHLPlayer *pHL2 = dynamic_cast<C_BaseHLPlayer *>( pOwner );
	float flCone = of2_barnacle_align.GetFloat();
	bool bHolding = ( flCone > 0.0f && pHL2 != NULL && pHL2->m_HL2Local.m_bOnTether && pHL2->m_HL2Local.m_bTetherAtWeapon );

	Vector vecMouth = vecOrigin;
	if ( s_bMouthKnown )
	{
		VectorTransform( s_vecMouthLocal, mat, vecMouth );
	}

	// The mouth is low and to the side, so from a ledge the way from it to someone
	// below runs into the ledge although the eye sees them (the user: the tongue
	// just hit the edge in front of them). Then the barnacle is held up, like a
	// gun to fire over a fence (the user's idea): one pose (of2_barnacle_lift_up /
	// _pitch / _yaw, which the user set with the of2_viewmodel_* convars), all the
	// way or not at all. Raised only as far as it took, in steps, it went back and
	// forth as the player moved about an edge (the user saw that), so it also
	// stays up until the way has been clear for a while. The server follows, as
	// it is told where the mouth is (below).
	if ( bWorld && s_bMouthKnown && pHL2 != NULL && !pHL2->m_HL2Local.m_bOnTether )
	{
		if ( of2_barnacle_lift.GetBool() )
		{
			trace_t tr;
			UTIL_TraceLine( vecEye, vecEye + vecForward * BARNACLE_LIFT_REACH, MASK_SOLID, pOwner, COLLISION_GROUP_NONE, &tr );
			Vector vecAimed = tr.endpos;

			CTraceFilterWorldOnly worldOnly;
			Vector vecWide( BARNACLE_LIFT_WIDE, BARNACLE_LIFT_WIDE, BARNACLE_LIFT_WIDE );
			float flFar = vecMouth.DistTo( vecAimed );
			UTIL_TraceHull( vecMouth, vecAimed, -vecWide, vecWide, MASK_SOLID, &worldOnly, &tr );

			// (not clear, not stopped only where the eye's own line ends, and near enough to be a ledge)
			float flStopped = tr.startsolid ? 0.0f : flFar * tr.fraction;
			if ( flStopped <= flFar - 16.0f && flStopped <= of2_barnacle_lift_range.GetFloat() )
			{
				s_bLiftWanted = true;
				s_flLiftBlockedTime = flTime;
			}
			else if ( flTime - s_flLiftBlockedTime > BARNACLE_LIFT_HOLD || flTime < s_flLiftBlockedTime )
			{
				s_bLiftWanted = false;
			}

			if ( of2_barnacle_lift_debug.GetBool() )
			{
				bool bBlocked = ( flStopped <= flFar - 16.0f );
				bool bNear = ( flStopped <= of2_barnacle_lift_range.GetFloat() );
				NDebugOverlay::Line( vecMouth, tr.endpos, bBlocked ? 255 : 0, bBlocked ? 0 : 255, 0, true, 0.0f );
				NDebugOverlay::Cross3D( vecAimed, 6.0f, 255, 255, 255, true, 0.0f );
				if ( bBlocked )
				{
					NDebugOverlay::Box( tr.endpos, -vecWide, vecWide, 255, bNear ? 0 : 160, 0, 64, 0.0f );
				}

				engine->Con_NPrintf( 10, "barnacle lift: way %s at %.0f of %.0f (range %.0f)", bBlocked ? "BLOCKED" : "clear", flStopped, flFar, of2_barnacle_lift_range.GetFloat() );
				engine->Con_NPrintf( 11, "  wanted %d, raised %.2f, clear for %.2f s (comes down after %.2f)", s_bLiftWanted ? 1 : 0, s_flLift, flTime - s_flLiftBlockedTime, BARNACLE_LIFT_HOLD );
			}
		}
		else
		{
			s_bLiftWanted = false;
		}

		s_flLift = Approach( s_bLiftWanted ? 1.0f : 0.0f, s_flLift, flPassed / MAX( of2_barnacle_lift_time.GetFloat(), 0.01f ) );
	}

	if ( s_flLift > 0.0f )
	{
		// (eased at both ends)
		float flLifted = SimpleSpline( s_flLift );

		// As of2_viewmodel_up / _pitch / _yaw would do it: up along the view model's
		// own up, and the turn added to the turn its script places it with
		static ConVarRef of2_viewmodel_pitch( "of2_viewmodel_pitch" );
		static ConVarRef of2_viewmodel_yaw( "of2_viewmodel_yaw" );
		static ConVarRef of2_viewmodel_roll( "of2_viewmodel_roll" );

		QAngle angPlaced( of2_viewmodel_pitch.GetFloat(), of2_viewmodel_yaw.GetFloat(), of2_viewmodel_roll.GetFloat() );
		C_BaseCombatWeapon *pWeapon = pViewModel->GetOwningWeapon();
		if ( pWeapon != NULL )
		{
			angPlaced += pWeapon->GetWpnData().m_angViewmodelOffset;
		}

		matrix3x4_t matPlaced, matUnplaced, matBase, matLifted;
		AngleMatrix( angPlaced, matPlaced );
		MatrixInvert( matPlaced, matUnplaced );
		ConcatTransforms( mat, matUnplaced, matBase );

		angPlaced.x += of2_barnacle_lift_pitch.GetFloat() * flLifted;
		angPlaced.y += of2_barnacle_lift_yaw.GetFloat() * flLifted;
		AngleMatrix( angPlaced, matPlaced );
		ConcatTransforms( matBase, matPlaced, matLifted );

		Vector vecBase, vecBaseUp;
		MatrixGetColumn( mat, 3, vecBase );
		MatrixGetColumn( matBase, 2, vecBaseUp );
		MatrixSetColumn( vecBase + vecBaseUp * ( of2_barnacle_lift_up.GetFloat() * flLifted ), 3, matLifted );

		mat = matLifted;
		VectorTransform( s_vecMouthLocal, mat, vecMouth );
	}

	if ( of2_barnacle_lift_debug.GetBool() )
	{
		// (where the tongue would leave from now)
		NDebugOverlay::Cross3D( vecMouth, 2.0f, 255, 255, 0, true, 0.0f );
	}

	// What it turns about: near its rear end, as if held there. (It was the mouth,
	// which the user saw as turning about the middle of the body.) The tongue is
	// drawn from wherever that leaves the mouth.
	Vector vecPivot;
	VectorTransform( Vector( 0, 0, BARNACLE_REAR_Z - of2_barnacle_pivot.GetFloat() ), mat, vecPivot );

	// The server holds the tongue from where the mouth is, so it has to be told:
	// when that has moved by more than the model's idling does, and not while
	// the player hangs by it (they would be moved with it)
	if ( bWorld && s_bMouthKnown && pHL2 != NULL && !pHL2->m_HL2Local.m_bOnTether )
	{
		static ConVarRef of2_tether_weapon_forward( "of2_tether_weapon_forward" );
		static ConVarRef of2_tether_weapon_right( "of2_tether_weapon_right" );
		static ConVarRef of2_tether_weapon_down( "of2_tether_weapon_down" );

		Vector vecFromEye = vecMouth - vecEye;
		Vector vecAt( DotProduct( vecFromEye, vecForward ), DotProduct( vecFromEye, vecRight ), -DotProduct( vecFromEye, vecUp ) );
		Vector vecHas( of2_tether_weapon_forward.GetFloat(), of2_tether_weapon_right.GetFloat(), of2_tether_weapon_down.GetFloat() );

		if ( vecAt.DistTo( vecHas ) > BARNACLE_MOUTH_CHANGE && ( flTime >= s_flNextMouthSend || s_flNextMouthSend - flTime > BARNACLE_MOUTH_RESEND ) )
		{
			char szCommand[64];
			Q_snprintf( szCommand, sizeof( szCommand ), "of2_tether_mouth %.1f %.1f %.1f", vecAt.x, vecAt.y, vecAt.z );
			engine->ServerCmd( szCommand );

			s_flNextMouthSend = flTime + BARNACLE_MOUTH_RESEND;
		}
	}

	if ( bHolding )
	{
		Vector vecTo = pHL2->m_HL2Local.m_vecTetherSwingPoint - vecEye;
		if ( !bWorld )
		{
			// (to where that is on screen for a viewmodel)
			float flAhead = DotProduct( vecTo, vecForward );
			vecTo = vecForward * flAhead + ( vecTo - vecForward * flAhead ) / flNarrow;
		}

		vecTo -= vecPivot - vecEye;
		if ( VectorNormalize( vecTo ) > 1.0f )
		{
			s_vecAlignLocal.Init( DotProduct( vecTo, vecForward ), DotProduct( vecTo, vecRight ), DotProduct( vecTo, vecUp ) );
		}
	}

	s_flAlign = Approach( bHolding ? 1.0f : 0.0f, s_flAlign, flPassed / BARNACLE_ALIGN_TIME );

	if ( s_flAlign > 0.0f && flCone > 0.0f )
	{
		Vector vecTo = vecForward * s_vecAlignLocal.x + vecRight * s_vecAlignLocal.y + vecUp * s_vecAlignLocal.z;

		// The model hangs mouth down
		Vector vecPoints;
		MatrixGetColumn( mat, 2, vecPoints );
		vecPoints = -vecPoints;

		Vector vecAxis = CrossProduct( vecPoints, vecTo );
		if ( VectorNormalize( vecAxis ) > 0.001f )
		{
			float flTurn = RAD2DEG( acos( clamp( DotProduct( vecPoints, vecTo ), -1.0f, 1.0f ) ) );
			flTurn = MIN( flTurn, flCone ) * SimpleSpline( s_flAlign );
			RotateAbout( mat, vecAxis, flTurn, vecPivot );
		}
	}

	// A neck is about to snap: it whips, as an arm does to send a wave down a
	// rope. The user gave the heights of its two ends at five moments, in
	// lengths of the barnacle (s_flFlickFront, s_flFlickBack); the neck goes at
	// the fourth key, mouth at the top. The model is moved through them as the
	// stiff thing it is: raised by its back end, tipped by the difference.
	float flSince = ( s_flFlickLength > 0.0f ) ? ( flTime - s_flFlickTime ) / s_flFlickLength : -1.0f;
	if ( flSince >= 0.0f && flSince < 1.0f )
	{
		float flFront = FlickHeight( s_flFlickFront, flSince );
		float flBack = FlickHeight( s_flFlickBack, flSince );

		// (an upright barnacle is of2_barnacle_flick degrees up)
		float flTip = RAD2DEG( asin( clamp( flFront - flBack, -1.0f, 1.0f ) ) ) * of2_barnacle_flick.GetFloat() / 90.0f;
		float flLong = s_bMouthKnown ? s_vecMouthLocal.Length() : 30.0f;

		// (about its back end, which is where the model's origin is)
		Vector vecBase;
		MatrixGetColumn( mat, 3, vecBase );
		RotateAbout( mat, vecRight, flTip, vecBase );
		MatrixSetColumn( vecBase + vecUp * ( flBack * flLong * of2_barnacle_flick_raise.GetFloat() ), 3, mat );
	}

	// Just brought out: up from below with its mouth hanging, a little past its
	// place and back (the user asked for a slight equip motion; the model has
	// no animation for it)
	float flEquip = of2_barnacle_equip_time.GetFloat();
	if ( flEquip > 0.0f && flTime - s_flEquipTime >= 0.0f && flTime - s_flEquipTime < flEquip )
	{
		float t = ( flTime - s_flEquipTime ) / flEquip - 1.0f;
		float flLeft = -( 2.0f * t * t * t + t * t );

		Vector vecBase;
		MatrixGetColumn( mat, 3, vecBase );
		RotateAbout( mat, vecRight, -of2_barnacle_equip_tilt.GetFloat() * flLeft, vecBase );
		MatrixSetColumn( vecBase - vecUp * ( of2_barnacle_equip_drop.GetFloat() * flLeft ), 3, mat );
	}

	MatrixAngles( mat, angles, vecOrigin );
}

//-----------------------------------------------------------------------------
// "OF2BarnacleFlick": the barnacle in the hand is about to snap a neck.
// m_flScale is how long the whip takes.
//-----------------------------------------------------------------------------
void OF2BarnacleFlickCallback( const CEffectData &data )
{
	s_flFlickTime = gpGlobals->curtime;
	s_flFlickLength = data.m_flScale;
}

DECLARE_CLIENT_EFFECT( "OF2BarnacleFlick", OF2BarnacleFlickCallback );

//-----------------------------------------------------------------------------
// What can stand between the eye and the tongue and is not the map itself:
// models, people, doors
//-----------------------------------------------------------------------------
class CTraceFilterTongueSight : public CTraceFilterSimple
{
public:
	CTraceFilterTongueSight( const IHandleEntity *pIgnore ) : CTraceFilterSimple( pIgnore, COLLISION_GROUP_NONE ) {}

	virtual TraceType_t GetTraceType() const { return TRACE_ENTITIES_ONLY; }
};

//-----------------------------------------------------------------------------
// The tongue runs from vecFrom to an edge and on towards vecOn. This is the way
// "past the edge" points: halfway between the way in and the way out, so
// what is before the edge is behind the plane through it and what is after is
// in front, however sharply the tongue turns there. (The way in alone, as it
// was at first, has the far stretch behind the plane too once the tongue turns
// by a right angle or more, and both stretches then counted as the first. The
// user saw that and suggested the middle of the two.)
//-----------------------------------------------------------------------------
static Vector PastEdge( const Vector &vecFrom, const Vector &vecEdge, const Vector &vecOn )
{
	Vector vecIn = vecEdge - vecFrom;
	Vector vecOut = vecOn - vecEdge;
	VectorNormalize( vecIn );
	VectorNormalize( vecOut );

	Vector vecPast = vecIn + vecOut;
	if ( VectorNormalize( vecPast ) < 0.01f )
	{
		vecPast = vecIn;
	}

	return vecPast;
}

class C_OF2Tongue : public C_BaseEntity
{
	DECLARE_CLASS( C_OF2Tongue, C_BaseEntity );

public:
	DECLARE_CLIENTCLASS();

	C_OF2Tongue();
	~C_OF2Tongue();

	// Nothing of it is a model
	virtual bool	ShouldDraw( void ) { return !IsDormant(); }
	virtual RenderGroup_t GetRenderGroup( void ) { return RENDER_GROUP_OPAQUE_ENTITY; }
	virtual void	GetRenderBounds( Vector &mins, Vector &maxs );
	virtual int		DrawModel( int flags );

	// Its points are moved smoothly from one tick to the next, like the
	// objects it holds on to. (Things without a model aren't, unless they ask.)
	virtual bool	ShouldInterpolate( void ) { return true; }

private:
	// The tongue, from the tip back to the barnacle; m_nNodes of them
	// (the more the longer it is). The rest of the list is all at the barnacle.
	Vector	m_vecNodes[OF2_TONGUE_NODES];
	CInterpolatedVarArray< Vector, OF2_TONGUE_NODES > m_iv_vecNodes;
	int		m_nNodes;

	// The edges the straight way from the barnacle to the tip goes over
	Vector	m_vecBends[OF2_TONGUE_MAX_BENDS];
	int		m_nBends;
	float	m_flWidth;

	CMaterialReference	m_Material;
	CMaterialReference	m_ShineMaterial;
};

// The one that is out (there is one Barnacle)
static C_OF2Tongue *s_pTongue = NULL;

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

	s_pTongue = this;
}

C_OF2Tongue::~C_OF2Tongue()
{
	if ( s_pTongue == this )
	{
		s_pTongue = NULL;
	}
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

	// From the barnacle, wherever the view has it this frame, along the points
	// to the tip
	Vector vecPoints[1 + OF2_TONGUE_MAX_BENDS + OF2_TONGUE_NODES + TONGUE_MAX_CORNERS];
	bool bBend[1 + OF2_TONGUE_MAX_BENDS + OF2_TONGUE_NODES + TONGUE_MAX_CORNERS];
	int nPoints = 0;

	Vector vecMouth = OF2_TetherHoldPos( pPlayer, true );
	OF2_BarnacleMouth( pPlayer, &vecMouth );
	vecPoints[nPoints] = vecMouth;
	bBend[nPoints] = false;
	nPoints++;

	int nNodes = clamp( m_nNodes, 0, OF2_TONGUE_NODES );
	if ( nNodes < 2 )
		return 0;

	// The barnacle on screen is not where the server has it: the view has moved
	// in the tick or two since, and drawn in the world the tongue starts in the
	// model's mouth, which is some way from the point the server holds it by.
	// The points are carried along with it, each by its share: all of the way
	// at the barnacle, falling to none at the first edge the tongue goes over,
	// or at the tip if there is none. For a tongue pulled straight that is
	// exactly where they belong, so it stays straight however the view turns.
	// (The share used to fall all the way to the tip, edge or no edge. With the
	// start in the mouth that carried the tongue well off an edge it lay over:
	// it was drawn going into the wall under the lip, as the user's screenshot
	// showed.)
	int nBends = clamp( m_nBends, 0, OF2_TONGUE_MAX_BENDS );
	const Vector &vecStart = m_vecNodes[nNodes - 1];

	float flReach = 0.0f;
	for ( int i = nNodes - 2; i >= 0; i-- )
	{
		flReach += m_vecNodes[i].DistTo( m_vecNodes[i + 1] );
	}

	if ( nBends > 0 )
	{
		// Up to where it passes the edge, as seen along the way there
		// (the server lists the edges from the tip's end back)
		const Vector &vecEdge = m_vecBends[nBends - 1];
		Vector vecAlong = PastEdge( vecStart, vecEdge, ( nBends > 1 ) ? m_vecBends[nBends - 2] : m_vecNodes[0] );

		float flUpTo = 0.0f;
		for ( int i = nNodes - 2; i >= 0; i-- )
		{
			float flStretch = m_vecNodes[i].DistTo( m_vecNodes[i + 1] );
			float flBefore = DotProduct( m_vecNodes[i + 1] - vecEdge, vecAlong );
			float flPast = DotProduct( m_vecNodes[i] - vecEdge, vecAlong );
			if ( flPast >= -TONGUE_EDGE_NEAR )
			{
				if ( flPast - flBefore > 0.001f )
				{
					flUpTo += flStretch * clamp( -flBefore / ( flPast - flBefore ), 0.0f, 1.0f );
				}
				flReach = flUpTo;
				break;
			}

			flUpTo += flStretch;
		}
	}
	flReach = MAX( flReach, 1.0f );

	Vector vecCarry = vecMouth - vecStart;
	float flTotal = 0.0f;
	float flRawAlong = 0.0f;
	for ( int i = nNodes - 2; i >= 0; i-- )
	{
		flRawAlong += m_vecNodes[i].DistTo( m_vecNodes[i + 1] );
		float flShare = clamp( 1.0f - flRawAlong / flReach, 0.0f, 1.0f );
		vecPoints[nPoints] = m_vecNodes[i] + vecCarry * flShare;
		bBend[nPoints] = false;
		flTotal += vecPoints[nPoints].DistTo( vecPoints[nPoints - 1] );
		nPoints++;
	}

	// The points only know about the world one by one, so where the tongue lies
	// over an edge the line from the last point before it to the first one after
	// cuts the corner. The server knows where the edges are that the straight
	// way to the tip goes over; where the tongue passes within a point's spacing
	// of one, it is drawn going out to it and round it. (Further off, it is
	// still in the air over that edge, or lies over it somewhere else.)
	float flSpacing = MAX( flTotal / ( nPoints - 1 ), 4.0f );
	for ( int b = 0; b < nBends; b++ )
	{
		int iLink = -1;
		float flBest = flSpacing;
		Vector vecAt;
		for ( int i = 0; i < nPoints - 1; i++ )
		{
			// (not one put in for another edge)
			if ( bBend[i] && bBend[i + 1] )
				continue;

			Vector vecClosest;
			CalcClosestPointOnLineSegment( m_vecBends[b], vecPoints[i], vecPoints[i + 1], vecClosest );
			float flDist = vecClosest.DistTo( m_vecBends[b] );
			if ( flDist < flBest )
			{
				flBest = flDist;
				iLink = i;
				vecAt = vecClosest;
			}
		}

		if ( iLink < 0 )
			continue;

		// All the way out to the edge from half a spacing in; less from further
		// off, so that it doesn't jump there as the tongue comes down on it
		float flPull = clamp( 2.0f - 2.0f * flBest / flSpacing, 0.0f, 1.0f );

		for ( int i = nPoints; i > iLink + 1; i-- )
		{
			vecPoints[i] = vecPoints[i - 1];
			bBend[i] = bBend[i - 1];
		}

		vecPoints[iLink + 1] = vecAt + ( m_vecBends[b] - vecAt ) * flPull;
		bBend[iLink + 1] = true;
		nPoints++;
	}

	// Those are the edges of the line that pulls. The tongue lies over others
	// too (slack on a floor with its end over the side, a crate's corner), and
	// there the same thing happens: one point on top, the next down the side,
	// and the line between them through the corner. Wherever the way from one
	// point to the next is blocked by a corner, it is drawn out to the corner
	// and round it (OF2_RopeLinkCorner; the server holds the points to each
	// other the same way).
	if ( of2_rope_corners.GetBool() )
	{
		CTraceFilterWorldAndPropsOnly cornerFilter;
		int nCorners = 0;
		for ( int i = 0; i < nPoints - 1 && nCorners < TONGUE_MAX_CORNERS; i++ )
		{
			// (not next to an edge it is drawn round already)
			if ( bBend[i] || bBend[i + 1] )
				continue;

			Vector vecCorner;
			if ( !OF2_RopeLinkCorner( vecPoints[i], vecPoints[i + 1], m_flWidth * 0.5f, MASK_SOLID_BRUSHONLY, &cornerFilter, &vecCorner ) )
				continue;

			for ( int j = nPoints; j > i + 1; j-- )
			{
				vecPoints[j] = vecPoints[j - 1];
				bBend[j] = bBend[j - 1];
			}

			vecPoints[i + 1] = vecCorner;
			bBend[i + 1] = true;
			nPoints++;
			nCorners++;

			// (on from the point after the corner)
			i++;
		}
	}

	OF2CurveStyle_t style;
	style.pMaterial = m_Material;
	style.pShineMaterial = m_ShineMaterial;
	style.flWidth = m_flWidth;
	style.flTextureRepeat = 0.0f;
	style.nSmooth = of2_tongue_smooth.GetInt();
	style.flShine = of2_tongue_shine.GetFloat();
	style.flShineWidth = of2_tongue_shine_width.GetFloat();
	style.flShineFollow = of2_tongue_shine_follow.GetFloat();
	style.flMinLight = of2_tongue_min_light.GetFloat();
	style.bTube = true;
	style.flRound = of2_tongue_round.GetFloat();
	style.flSideLight = of2_tongue_side_light.GetFloat();
	style.flTipRadius = of2_tongue_blob_radius.GetFloat();
	style.flTipLength = of2_tongue_blob_length.GetFloat();
	style.flBendRadius = of2_tongue_bend_radius.GetFloat();
	style.flTipShade = of2_tongue_blob_shade.GetFloat();

	// Drawn again in front of the world (OF2_DrawBarnacleOverlay): as far as the
	// first edge the straight way to the tip goes over, or all of it if there is
	// none. That way is clear, so nothing in the world should be hiding this
	// part anyway. The points aren't tied to the edges, so a slack tongue has no
	// place that is "at" the edge: it ends where it passes the edge as seen
	// from there: where it crosses the plane through the edge that lies halfway
	// between the stretch before it and the one after (PastEdge; the user's idea).
	// That is for the map's own walls. A model, a person or a door in front of
	// the tongue is to hide it as it hides anything (the user saw the tongue
	// drawn over them), and in this pass nothing can. So it also ends at the
	// first point that has one of those between it and the eye; from there on
	// the tongue is only what the world's pass drew of it.
	if ( s_bDrawingOverlay )
	{
		// (the server lists the edges from the tip's end back)
		Vector vecEdge = ( nBends > 0 ) ? m_vecBends[nBends - 1] : vecMouth;
		Vector vecAlong = PastEdge( vecMouth, vecEdge, ( nBends > 1 ) ? m_vecBends[nBends - 2] : vecPoints[nPoints - 1] );

		const Vector &vecEye = CurrentViewOrigin();
		CTraceFilterTongueSight filter( pPlayer );

		bool bEnds = false;
		bool bHidden = false;
		float flUpTo = 0.0f;
		for ( int i = 1; i < nPoints; i++ )
		{
			trace_t tr;
			UTIL_TraceLine( vecEye, vecPoints[i], MASK_SHOT, &filter, &tr );
			if ( tr.fraction < 1.0f && tr.endpos.DistTo( vecPoints[i] ) > TONGUE_SIGHT_CLEAR )
			{
				bEnds = true;
				bHidden = true;
				break;
			}

			float flStretch = vecPoints[i].DistTo( vecPoints[i - 1] );
			if ( nBends > 0 )
			{
				float flBefore = DotProduct( vecPoints[i - 1] - vecEdge, vecAlong );
				float flPast = DotProduct( vecPoints[i] - vecEdge, vecAlong );
				if ( flPast >= -TONGUE_EDGE_NEAR )
				{
					if ( flPast - flBefore > 0.001f )
					{
						flUpTo += flStretch * clamp( -flBefore / ( flPast - flBefore ), 0.0f, 1.0f );
					}
					bEnds = true;
					break;
				}
			}

			flUpTo += flStretch;
		}

		if ( bEnds )
		{
			// (never less than what is in the barnacle's mouth)
			style.flDrawLength = MAX( flUpTo, bHidden ? TONGUE_OVERLAY_HIDDEN : TONGUE_OVERLAY_LENGTH );
		}
	}

	// The material mirrors the map's cubemap ($envmap env_cubemap). Which one
	// is the engine's to say, as it does for a model: the one nearest the
	// middle of the tongue.
	modelrender->SetupLighting( vecPoints[nPoints / 2] );

	bool bFound = false;
	IMaterialVar *pReflect = m_Material->FindVar( "$envmaptint", &bFound, false );
	if ( bFound && pReflect )
	{
		float flReflect = MAX( of2_tongue_reflect.GetFloat(), 0.0f );
		pReflect->SetVecValue( flReflect, flReflect, flReflect );
	}

	OF2_DrawCurve( vecPoints, bBend, nPoints, style );
	return 1;
}

//-----------------------------------------------------------------------------
// viewrender.cpp (DrawViewModels), after the viewmodels, with the world's
// field of view and the viewmodels' depth: in front of everything in the
// world. The barnacle is where it would be in the world, so its tongue comes
// out of its mouth, but nothing in the world hides it: drawn with the world
// it went into every wall the player hung against (the user saw that). So
// that the tongue and the barnacle still hide each other as they should, the
// start of the tongue is drawn again here first.
//-----------------------------------------------------------------------------
void OF2_DrawBarnacleOverlay( void )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	C_BaseViewModel *pViewModel = pPlayer ? pPlayer->GetViewModel( 0 ) : NULL;
	if ( !OF2_BarnacleInWorld( pViewModel ) || !pViewModel->ShouldDraw() || !pPlayer->IsAlive() )
		return;

	s_bDrawingOverlay = true;

	if ( s_pTongue != NULL && s_pTongue->ShouldDraw() )
	{
		s_pTongue->DrawModel( STUDIO_RENDER );
	}

	pViewModel->DrawModel( STUDIO_RENDER );

	s_bDrawingOverlay = false;

	// (where its mouth is has to be known before the tongue is first thrown)
	Vector vecMouth;
	OF2_BarnacleMouth( pPlayer, &vecMouth );
}
