//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Special handling for hl2 usable ladders
//
//=============================================================================//
#include "cbase.h"
#include "hl_gamemovement.h"
#include "in_buttons.h"
#include "utlrbtree.h"
#include "hl2_shareddefs.h"
#include "movevars_shared.h"

#ifdef HL2MP
#include "hl2mp_gamerules.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar sv_autoladderdismount( "sv_autoladderdismount", "1", FCVAR_REPLICATED, "Automatically dismount from ladders when you reach the end (don't have to +USE)." );
static ConVar sv_ladderautomountdot( "sv_ladderautomountdot", "0.4", FCVAR_REPLICATED, "When auto-mounting a ladder by looking up its axis, this is the tolerance for looking now directly along the ladder axis." );

static ConVar sv_ladder_useonly( "sv_ladder_useonly", "0", FCVAR_REPLICATED, "If set, ladders can only be mounted by pressing +USE" );

#define USE_DISMOUNT_SPEED 100

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHL2GameMovement::CHL2GameMovement()
{
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : type - 
// Output : int
//-----------------------------------------------------------------------------
int CHL2GameMovement::GetCheckInterval( IntervalType_t type )
{
	// HL2 ladders need to check every frame!!!
	if ( type == LADDER )
		return 1;

	return BaseClass::GetCheckInterval( type );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CHL2GameMovement::IsForceMoveActive()
{
	LadderMove_t *lm = GetLadderMove();
	return lm->m_bForceLadderMove;
}

//-----------------------------------------------------------------------------
// Purpose: Debounce the USE button
//-----------------------------------------------------------------------------
void CHL2GameMovement::SwallowUseKey()
{
	mv->m_nOldButtons |= IN_USE;
	player->m_afButtonPressed &= ~IN_USE;

	GetHL2Player()->m_bPlayUseDenySound = false;
}

#if !defined( CLIENT_DLL )
// This is a simple helper class to reserver a player sized hull at a spot, owned by the current player so that nothing
//  can move into this spot and cause us to get stuck when we get there
class CReservePlayerSpot : public CBaseEntity
{
	DECLARE_CLASS( CReservePlayerSpot, CBaseEntity )
public:
	static CReservePlayerSpot *ReserveSpot( CBasePlayer *owner, const Vector& org, const Vector& mins, const Vector& maxs, bool& validspot );

	virtual void Spawn();
};

CReservePlayerSpot *CReservePlayerSpot::ReserveSpot( 
	CBasePlayer *owner, const Vector& org, const Vector& mins, const Vector& maxs, bool& validspot )
{
	CReservePlayerSpot *spot = ( CReservePlayerSpot * )CreateEntityByName( "reserved_spot" );
	Assert( spot );

	spot->SetAbsOrigin( org );
	UTIL_SetSize( spot, mins, maxs );
	spot->SetOwnerEntity( owner );
	spot->Spawn();

	// See if spot is valid
	trace_t tr;
	UTIL_TraceHull(
		org, 
		org, 
		mins,
		maxs,
		MASK_PLAYERSOLID,
		owner,
		COLLISION_GROUP_PLAYER_MOVEMENT,
		&tr );

	validspot = !tr.startsolid;

	if ( !validspot )
	{
		Vector org2 = org + Vector( 0, 0, 1 );

		// See if spot is valid
		trace_t tr;
		UTIL_TraceHull(
			org2, 
			org2, 
			mins,
			maxs,
			MASK_PLAYERSOLID,
			owner,
			COLLISION_GROUP_PLAYER_MOVEMENT,
			&tr );
		validspot = !tr.startsolid;
	}

	return spot;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CReservePlayerSpot::Spawn()
{
	BaseClass::Spawn();

	SetSolid( SOLID_BBOX );
	SetMoveType( MOVETYPE_NONE );
	// Make entity invisible
	AddEffects( EF_NODRAW );
}

LINK_ENTITY_TO_CLASS( reserved_spot, CReservePlayerSpot );

#endif
//-----------------------------------------------------------------------------
// Purpose: 
// Input  : mounting - 
//			transit_speed - 
//			goalpos - 
//			*ladder - 
//-----------------------------------------------------------------------------
void CHL2GameMovement::StartForcedMove( bool mounting, float transit_speed, const Vector& goalpos, CFuncLadder *ladder )
{
	LadderMove_t* lm = GetLadderMove();
	Assert( lm );
	// Already active, just ignore
	if ( lm->m_bForceLadderMove )
	{
		return;
	}

#if !defined( CLIENT_DLL )
	if ( ladder )
	{
		ladder->PlayerGotOn( GetHL2Player() );

		// If the Ladder only wants to be there for automount checking, abort now
		if ( ladder->DontGetOnLadder() )
			return;
	}
		
	// Reserve goal slot here
	bool valid = false;
	lm->m_hReservedSpot = CReservePlayerSpot::ReserveSpot( 
		player, 
		goalpos, 
		GetPlayerMins( ( player->GetFlags() & FL_DUCKING ) ? true : false ), 
		GetPlayerMaxs( ( player->GetFlags() & FL_DUCKING ) ? true : false ), 
		valid );
	if ( !valid )
	{
		// FIXME:  Play a deny sound?
		if ( lm->m_hReservedSpot )
		{
			UTIL_Remove( lm->m_hReservedSpot );
			lm->m_hReservedSpot = NULL;
		}
		return;
	}
#endif

	// Use current player origin as start and new origin as dest
	lm->m_vecGoalPosition	= goalpos;
	lm->m_vecStartPosition	= mv->GetAbsOrigin();

	// Figure out how long it will take to make the gap based on transit_speed
	Vector delta = lm->m_vecGoalPosition - lm->m_vecStartPosition;

	float distance = delta.Length();
	
	Assert( transit_speed > 0.001f );

	// Compute time required to move that distance
	float transit_time = distance / transit_speed;
	if ( transit_time < 0.001f )
	{
		transit_time = 0.001f;
	}

	lm->m_bForceLadderMove	= true;
	lm->m_bForceMount		= mounting;

	lm->m_flStartTime		= gpGlobals->curtime;
	lm->m_flArrivalTime		= lm->m_flStartTime + transit_time;

	lm->m_hForceLadder		= ladder;

	// Don't get stuck during this traversal since we'll just be slamming the player origin
	player->SetMoveType( MOVETYPE_NONE );
	player->SetMoveCollide( MOVECOLLIDE_DEFAULT );
	player->SetSolid( SOLID_NONE );
	SetLadder( ladder );

	// Debounce the use key
	SwallowUseKey();
}

//-----------------------------------------------------------------------------
// Purpose: Returns false when finished
//-----------------------------------------------------------------------------
bool CHL2GameMovement::ContinueForcedMove()
{
	LadderMove_t* lm = GetLadderMove();
	Assert( lm );
	Assert( lm->m_bForceLadderMove );

	// Suppress regular motion
	mv->m_flForwardMove = 0.0f;
	mv->m_flSideMove = 0.0f;
	mv->m_flUpMove = 0.0f;

	// How far along are we
	float frac = ( gpGlobals->curtime - lm->m_flStartTime ) / ( lm->m_flArrivalTime - lm->m_flStartTime );
	if ( frac > 1.0f )
	{
		lm->m_bForceLadderMove = false;
#if !defined( CLIENT_DLL )
		// Remove "reservation entity"
		if ( lm->m_hReservedSpot )
		{
			UTIL_Remove( lm->m_hReservedSpot );
			lm->m_hReservedSpot = NULL;
		}
#endif
	}

	frac = clamp( frac, 0.0f, 1.0f );

	// Move origin part of the way
	Vector delta = lm->m_vecGoalPosition - lm->m_vecStartPosition;

	// Compute interpolated position
	Vector org;
	VectorMA( lm->m_vecStartPosition, frac, delta, org );
	mv->SetAbsOrigin( org );

	// If finished moving, reset player to correct movetype (or put them on the ladder)
	if ( !lm->m_bForceLadderMove )
	{
		player->SetSolid( SOLID_BBOX );
		player->SetMoveType( MOVETYPE_WALK );

		if ( lm->m_bForceMount && lm->m_hForceLadder != NULL )
		{
			player->SetMoveType( MOVETYPE_LADDER );
			SetLadder( lm->m_hForceLadder );
		}

		// Zero out any velocity
		mv->m_vecVelocity.Init();
	}

	// Stil active
	return lm->m_bForceLadderMove;
}

//-----------------------------------------------------------------------------
// Purpose: Returns true if the player is on a ladder
// Input  : &trace - ignored
//-----------------------------------------------------------------------------
bool CHL2GameMovement::OnLadder( trace_t &trace )
{
#ifdef HL2_USES_FUNC_LADDER_CODE
	return ( GetLadder() != NULL ) ? true : BaseClass::OnLadder(trace);
#else
	return ( GetLadder() != NULL ) ? true : false;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : ladders - 
//			maxdist - 
//			**ppLadder - 
//			ladderOrigin - 
//-----------------------------------------------------------------------------
void CHL2GameMovement::Findladder( float maxdist, CFuncLadder **ppLadder, Vector& ladderOrigin, const CFuncLadder *skipLadder )
{
	CFuncLadder *bestLadder = NULL;
	float bestDist = MAX_COORD_INTEGER;
	Vector bestOrigin;

	bestOrigin.Init();

	float maxdistSqr = maxdist * maxdist;


	int c = CFuncLadder::GetLadderCount();
	for ( int i = 0 ; i < c; i++ )
	{
		CFuncLadder *ladder = CFuncLadder::GetLadder( i );

		if ( !ladder->IsEnabled() )
			continue;

		if ( skipLadder && ladder == skipLadder )
			continue;

		Vector topPosition;
		Vector bottomPosition;

		ladder->GetTopPosition( topPosition );
		ladder->GetBottomPosition( bottomPosition );

		Vector closest;
		CalcClosestPointOnLineSegment( mv->GetAbsOrigin(), bottomPosition, topPosition, closest, NULL );

		float distSqr = ( closest - mv->GetAbsOrigin() ).LengthSqr();

		// Too far away
		if ( distSqr > maxdistSqr )
		{
			continue;
		}

		// Need to trace to see if it's clear
		trace_t tr;

		UTIL_TraceLine( mv->GetAbsOrigin(), closest, 
			MASK_PLAYERSOLID,
			player,
			COLLISION_GROUP_NONE,
			&tr );

		if ( tr.fraction != 1.0f &&
			 tr.m_pEnt &&
			 tr.m_pEnt != ladder )
		{
			// Try a trace stepped up from the ground a bit, in case there's something at ground level blocking us.
			float sizez = GetPlayerMaxs().z - GetPlayerMins().z;

			UTIL_TraceLine( mv->GetAbsOrigin() + Vector( 0, 0, sizez * 0.5f ), closest, 
				MASK_PLAYERSOLID,
				player,
				COLLISION_GROUP_NONE,
				&tr );

			if ( tr.fraction != 1.0f &&
				 tr.m_pEnt &&
				 tr.m_pEnt != ladder &&
				 !tr.m_pEnt->IsSolidFlagSet( FSOLID_TRIGGER ) )
			{
				continue;
			}
		}

		// See if this is the best one so far
		if ( distSqr < bestDist )
		{
			bestDist = distSqr;
			bestLadder = ladder;
			bestOrigin = closest;
		}
	}

	// Return best ladder spot
	*ppLadder = bestLadder;
	ladderOrigin = bestOrigin;

}

#ifdef MAPBASE
bool CHL2GameMovement::ForcePlayerOntoLadder(CFuncLadder *ladder)
{
	Vector topPosition;
	Vector bottomPosition;

	ladder->GetTopPosition(topPosition);
	ladder->GetBottomPosition(bottomPosition);

	Vector closest;
	CalcClosestPointOnLineSegment(mv->GetAbsOrigin(), bottomPosition, topPosition, closest, NULL);

	StartForcedMove( true, player->MaxSpeed(), closest, ladder );

	return true;
}

bool CHL2GameMovement::MountPlayerOntoLadder(CFuncLadder *ladder)
{
	Vector topPosition;
	Vector bottomPosition;

	ladder->GetTopPosition(topPosition);
	ladder->GetBottomPosition(bottomPosition);

	Vector closest;
	CalcClosestPointOnLineSegment(mv->GetAbsOrigin(), bottomPosition, topPosition, closest, NULL);

#if 0
	// Compute parametric distance along ladder vector...
	float oldt;
	CalcDistanceSqrToLine( mv->GetAbsOrigin(), topPosition, bottomPosition, &oldt );
	
	// Perform the move accounting for any base velocity.
	VectorAdd (mv->m_vecVelocity, player->GetBaseVelocity(), mv->m_vecVelocity);
	TryPlayerMove();
	VectorSubtract (mv->m_vecVelocity, player->GetBaseVelocity(), mv->m_vecVelocity);

	// Pressed buttons are "changed(xor)" and'ed with the mask of currently held buttons
	bool pressing_forward_or_side = mv->m_flForwardMove != 0.0f || mv->m_flSideMove != 0.0f;

	Vector ladderVec = topPosition - bottomPosition;
	float LadderLength = VectorNormalize( ladderVec );
	// This test is not perfect by any means, but should help a bit
	bool moving_along_ladder = false;
	if ( pressing_forward_or_side )
	{
		float fwdDot = m_vecForward.Dot( ladderVec );
		if ( fabs( fwdDot ) > 0.9f )
		{
			moving_along_ladder = true;
		}
	}

	// Compute parametric distance along ladder vector...
	float newt;
	CalcDistanceSqrToLine( mv->GetAbsOrigin(), topPosition, bottomPosition, &newt );

	// Fudge of 2 units
	float tolerance = 1.0f / LadderLength;

	bool wouldleaveladder = false;
	// Moving pPast top or bottom?
	if ( newt < -tolerance )
	{
		wouldleaveladder = newt < oldt;
	}
	else if ( newt > ( 1.0f + tolerance ) )
	{
		wouldleaveladder = newt > oldt;
	}

	// See if we are near the top or bottom but not moving
	float dist1sqr, dist2sqr;

	dist1sqr = ( topPosition - mv->GetAbsOrigin() ).LengthSqr();
	dist2sqr = ( bottomPosition - mv->GetAbsOrigin() ).LengthSqr();

	float dist = MIN( dist1sqr, dist2sqr );
	bool neardismountnode = ( dist < 16.0f * 16.0f ) ? true : false;
	float ladderUnitsPerTick = ( MAX_CLIMB_SPEED * gpGlobals->interval_per_tick );
	bool neardismountnode2 = ( dist < ladderUnitsPerTick * ladderUnitsPerTick ) ? true : false;

	// Really close to node, cvar is set, and pressing a key, then simulate a +USE
	bool auto_dismount_use = ( neardismountnode2 && 
								sv_autoladderdismount.GetBool() && 
								pressing_forward_or_side && 
								!moving_along_ladder );

	bool fully_underwater = ( player->GetWaterLevel() == WL_Eyes ) ? true : false;
#endif

	StartForcedMove( true, player->MaxSpeed(), closest, ladder );

	return true;
}
#endif

static bool NearbyDismountLessFunc( const NearbyDismount_t& lhs, const NearbyDismount_t& rhs )
{
	return lhs.distSqr < rhs.distSqr;
}

void CHL2GameMovement::GetSortedDismountNodeList( const Vector &org, float radius, CFuncLadder *ladder, CUtlRBTree< NearbyDismount_t, int >& list )
{
	float radiusSqr = radius * radius;

	int i;
	int c = ladder->GetDismountCount();
	for ( i = 0; i < c; i++ )
	{
		CInfoLadderDismount *spot = ladder->GetDismount( i );
		if ( !spot )
			continue;

		float distSqr = ( spot->GetAbsOrigin() - org ).LengthSqr();
		if ( distSqr > radiusSqr )
			continue;

		NearbyDismount_t nd;
		nd.dismount = spot;
		nd.distSqr = distSqr;

		list.Insert( nd );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//			*ladder - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CHL2GameMovement::ExitLadderViaDismountNode( CFuncLadder *ladder, bool strict, bool useAlternate )
{
	// Find the best ladder exit node
	float bestDot = -99999.0f;
	float bestDistance = 99999.0f;
	Vector bestDest;
	bool found = false;

	// For 'alternate' dismount
	bool foundAlternate = false;
	Vector alternateDest;
	float alternateDist = 99999.0f;

	CUtlRBTree< NearbyDismount_t, int >	nearbyDismounts( 0, 0, NearbyDismountLessFunc );

	GetSortedDismountNodeList( mv->GetAbsOrigin(), 100.0f, ladder, nearbyDismounts );

	int i;

	for ( i = nearbyDismounts.FirstInorder(); i != nearbyDismounts.InvalidIndex() ; i = nearbyDismounts.NextInorder( i ) )
	{
		CInfoLadderDismount *spot = nearbyDismounts[ i ].dismount;
		if ( !spot )
		{
			Assert( !"What happened to the spot!!!" );
			continue;
		}

		// See if it's valid to put the player there...
		Vector org = spot->GetAbsOrigin() + Vector( 0, 0, 1 );

		trace_t tr;
		UTIL_TraceHull(
			org, 
			org, 
			GetPlayerMins( ( player->GetFlags() & FL_DUCKING ) ? true : false ),
			GetPlayerMaxs( ( player->GetFlags() & FL_DUCKING ) ? true : false ),
			MASK_PLAYERSOLID,
			player,
			COLLISION_GROUP_PLAYER_MOVEMENT,
			&tr );

		// Nope...
		if ( tr.startsolid )
		{
			continue;
		}

		// Find the best dot product
		Vector vecToSpot = org - ( mv->GetAbsOrigin() + player->GetViewOffset() );
		vecToSpot.z = 0.0f;
		float d = VectorNormalize( vecToSpot );

		float dot = vecToSpot.Dot( m_vecForward );

		// We're not facing at it...ignore
		if ( dot < 0.5f )
		{
			if( useAlternate && d < alternateDist )
			{
				alternateDest = org;
				alternateDist = d;
				foundAlternate = true;
			}

			continue;
		}

		if ( dot > bestDot )
		{
			bestDest = org;
			bestDistance = d;
			bestDot = dot;
			found = true;
		}
	}

	if ( found )
	{
		// Require a more specific 
		if ( strict && 
			( ( bestDot < 0.7f ) || ( bestDistance > 40.0f ) ) )
		{
			return false;
		}

		StartForcedMove( false, player->MaxSpeed(), bestDest, NULL );
		return true;
	}

	if( useAlternate )
	{
		// Desperate. Don't refuse to let a person off of a ladder if it can be helped. Use the
		// alternate dismount if there is one.
		if( foundAlternate && alternateDist <= 60.0f )
		{
			StartForcedMove( false, player->MaxSpeed(), alternateDest, NULL );
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : bOnLadder - 
//-----------------------------------------------------------------------------
void CHL2GameMovement::FullLadderMove()
{
#if !defined( CLIENT_DLL )
	CFuncLadder *ladder = GetLadder();
	Assert( ladder );
	if ( !ladder )
	{
#ifdef HL2_USES_FUNC_LADDER_CODE
		BaseClass::FullLadderMove();
#endif
		return;
	}

	CheckWater();

	// Was jump button pressed?  If so, don't do anything here
	if ( mv->m_nButtons & IN_JUMP )
	{
		CheckJumpButton();
		return;
	}
	else
	{
		mv->m_nOldButtons &= ~IN_JUMP;
	}

	player->SetGroundEntity( NULL );

	// Remember old positions in case we cancel this movement
	Vector oldVelocity	= mv->m_vecVelocity;
	Vector oldOrigin	= mv->GetAbsOrigin();

	Vector topPosition;
	Vector bottomPosition;

	ladder->GetTopPosition( topPosition );
	ladder->GetBottomPosition( bottomPosition );

	// Compute parametric distance along ladder vector...
	float oldt;
	CalcDistanceSqrToLine( mv->GetAbsOrigin(), topPosition, bottomPosition, &oldt );
	
	// Perform the move accounting for any base velocity.
	VectorAdd (mv->m_vecVelocity, player->GetBaseVelocity(), mv->m_vecVelocity);
	TryPlayerMove();
	VectorSubtract (mv->m_vecVelocity, player->GetBaseVelocity(), mv->m_vecVelocity);

	// Pressed buttons are "changed(xor)" and'ed with the mask of currently held buttons
	int buttonsChanged	= ( mv->m_nOldButtons ^ mv->m_nButtons );	// These buttons have changed this frame
	int buttonsPressed = buttonsChanged & mv->m_nButtons;
	bool pressed_use = ( buttonsPressed & IN_USE ) ? true : false;
	bool pressing_forward_or_side = mv->m_flForwardMove != 0.0f || mv->m_flSideMove != 0.0f;

	Vector ladderVec = topPosition - bottomPosition;
	float LadderLength = VectorNormalize( ladderVec );
	// This test is not perfect by any means, but should help a bit
	bool moving_along_ladder = false;
	if ( pressing_forward_or_side )
	{
		float fwdDot = m_vecForward.Dot( ladderVec );
		if ( fabs( fwdDot ) > 0.9f )
		{
			moving_along_ladder = true;
		}
	}

	// Compute parametric distance along ladder vector...
	float newt;
	CalcDistanceSqrToLine( mv->GetAbsOrigin(), topPosition, bottomPosition, &newt );

	// Fudge of 2 units
	float tolerance = 1.0f / LadderLength;

	bool wouldleaveladder = false;
	// Moving pPast top or bottom?
	if ( newt < -tolerance )
	{
		wouldleaveladder = newt < oldt;
	}
	else if ( newt > ( 1.0f + tolerance ) )
	{
		wouldleaveladder = newt > oldt;
	}

	// See if we are near the top or bottom but not moving
	float dist1sqr, dist2sqr;

	dist1sqr = ( topPosition - mv->GetAbsOrigin() ).LengthSqr();
	dist2sqr = ( bottomPosition - mv->GetAbsOrigin() ).LengthSqr();

	float dist = MIN( dist1sqr, dist2sqr );
	bool neardismountnode = ( dist < 16.0f * 16.0f ) ? true : false;
	float ladderUnitsPerTick = ( MAX_CLIMB_SPEED * gpGlobals->interval_per_tick );
	bool neardismountnode2 = ( dist < ladderUnitsPerTick * ladderUnitsPerTick ) ? true : false;

	// Really close to node, cvar is set, and pressing a key, then simulate a +USE
	bool auto_dismount_use = ( neardismountnode2 && 
								sv_autoladderdismount.GetBool() && 
								pressing_forward_or_side && 
								!moving_along_ladder );

	bool fully_underwater = ( player->GetWaterLevel() == WL_Eyes ) ? true : false;

	// If the user manually pressed use or we're simulating it, then use_dismount will occur
	bool use_dismount = pressed_use || auto_dismount_use;

	if ( fully_underwater && !use_dismount )
	{
		// If fully underwater, we require looking directly at a dismount node 
		///  to "float off" a ladder mid way...
		if ( ExitLadderViaDismountNode( ladder, true ) )
		{
			// See if they +used a dismount point mid-span..
			return;
		}
	}

	// If the movement would leave the ladder and they're not automated or pressing use, disallow the movement
	if ( !use_dismount )
	{
		if ( wouldleaveladder )
		{
			// Don't let them leave the ladder if they were on it
			mv->m_vecVelocity = oldVelocity;
			mv->SetAbsOrigin( oldOrigin );
		}
		return;
	}

	// If the move would not leave the ladder and we're near close to the end, then just accept the move
	if ( !wouldleaveladder && !neardismountnode )
	{
		// Otherwise, if the move would leave the ladder, disallow it.
		if ( pressed_use )
		{
			if ( ExitLadderViaDismountNode( ladder, false, IsX360() ) )
			{
				// See if they +used a dismount point mid-span..
				return;
			}

			player->SetMoveType( MOVETYPE_WALK );
			player->SetMoveCollide( MOVECOLLIDE_DEFAULT );
			SetLadder( NULL );
			GetHL2Player()->m_bPlayUseDenySound = false;

			// Dismount with a bit of velocity in facing direction
			VectorScale( m_vecForward, USE_DISMOUNT_SPEED, mv->m_vecVelocity );
			mv->m_vecVelocity.z = 50;
		}
		return;
	}

	// Debounce the use key
	if ( pressed_use )
	{
		SwallowUseKey();
	}

	// Try auto exit, if possible
	if ( ExitLadderViaDismountNode( ladder, false, pressed_use ) )
	{
		return;
	}

	if ( wouldleaveladder )
	{
		// Otherwise, if the move would leave the ladder, disallow it.
		if ( pressed_use )
		{
			player->SetMoveType( MOVETYPE_WALK );
			player->SetMoveCollide( MOVECOLLIDE_DEFAULT );
			SetLadder( NULL );

			// Dismount with a bit of velocity in facing direction
			VectorScale( m_vecForward, USE_DISMOUNT_SPEED, mv->m_vecVelocity );
			mv->m_vecVelocity.z = 50;
		}
		else
		{
			mv->m_vecVelocity = oldVelocity;
			mv->SetAbsOrigin( oldOrigin );
		}
	}
#endif
}

bool CHL2GameMovement::CheckLadderAutoMountEndPoint( CFuncLadder *ladder, const Vector& bestOrigin )
{
	// See if we're really near an endpoint
	if ( !ladder )
		return false;

	Vector top, bottom;
	ladder->GetTopPosition( top );
	ladder->GetBottomPosition( bottom );

	float d1, d2;

	d1 = ( top - mv->GetAbsOrigin() ).LengthSqr();
	d2 = ( bottom - mv->GetAbsOrigin() ).LengthSqr();

	if ( d1 > 16 * 16 && d2 > 16 * 16 )
		return false;

	Vector ladderAxis;

	if ( d1 < 16 * 16 )
	{
		// Close to top
		ladderAxis = bottom - top;
	}
	else
	{
		ladderAxis = top - bottom;
	}

	VectorNormalize( ladderAxis );

	if ( ladderAxis.Dot( m_vecForward ) > sv_ladderautomountdot.GetFloat() )
	{
		StartForcedMove( true, player->MaxSpeed(), bestOrigin, ladder );
		return true;
	}

	return false;
}

bool CHL2GameMovement::CheckLadderAutoMountCone( CFuncLadder *ladder, const Vector& bestOrigin, float maxAngleDelta, float maxDistToLadder )
{
	// Never 'back' onto ladders or stafe onto ladders
	if ( ladder != NULL && 
		( mv->m_flForwardMove > 0.0f ) )
	{
		Vector top, bottom;
		ladder->GetTopPosition( top );
		ladder->GetBottomPosition( bottom );

		Vector ladderAxis = top - bottom;
		VectorNormalize( ladderAxis );

		Vector probe = mv->GetAbsOrigin();

		Vector closest;
		CalcClosestPointOnLineSegment( probe, bottom, top, closest, NULL );

		Vector vecToLadder = closest - probe;

		float dist = VectorNormalize( vecToLadder );

		Vector flatLadder = vecToLadder;
		flatLadder.z = 0.0f;
		Vector flatForward = m_vecForward;
		flatForward.z = 0.0f;

		VectorNormalize( flatLadder );
		VectorNormalize( flatForward );

		float facingDot = flatForward.Dot( flatLadder );
		float angle = acos( facingDot ) * 180 / M_PI;

		bool closetoladder = ( dist != 0.0f && dist < maxDistToLadder ) ? true : false;
		bool reallyclosetoladder = ( dist != 0.0f && dist < 4.0f ) ? true : false;

		bool facingladderaxis = ( angle < maxAngleDelta ) ? true : false;
		bool facingalongaxis = ( (float)fabs( ladderAxis.Dot( m_vecForward ) ) > sv_ladderautomountdot.GetFloat() ) ? true : false;
#if 0
		Msg( "close %i length %.3f maxdist %.3f facing %.3f dot %.3f ang %.3f\n",
			closetoladder ? 1 : 0,
			dist,
			maxDistToLadder,
			(float)fabs( ladderAxis.Dot( m_vecForward ) ),
			facingDot, 
			angle);
#endif

		// Tracker 21776:  Don't mount ladders this way if strafing
		bool strafing = ( fabs( mv->m_flSideMove ) < 1.0f ) ? false : true;

		if ( ( ( facingDot > 0.0f && !strafing ) || facingalongaxis  ) && 
			( facingladderaxis || reallyclosetoladder ) && 
			closetoladder )
		{
			StartForcedMove( true, player->MaxSpeed(), bestOrigin, ladder );
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Must be facing toward ladder
// Input  : *ladder - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CHL2GameMovement::LookingAtLadder( CFuncLadder *ladder )
{
	if ( !ladder )
	{
		return false;
	}

	// Get ladder end points
	Vector top, bottom;
	ladder->GetTopPosition( top );
	ladder->GetBottomPosition( bottom );

	// Find closest point on ladder to player (could be an endpoint)
	Vector closest;
	CalcClosestPointOnLineSegment( mv->GetAbsOrigin(), bottom, top, closest, NULL );

	// Flatten our view direction to 2D
	Vector flatForward = m_vecForward;
	flatForward.z = 0.0f;

	// Because the ladder itself is not a solid, the player's origin may actually be 
	// permitted to pass it, and that will screw up our dot product.
	// So back up the player's origin a bit to do the facing calculation.
	Vector vecAdjustedOrigin = mv->GetAbsOrigin() - 8.0f * flatForward;

	// Figure out vector from player to closest point on ladder
	Vector vecToLadder = closest - vecAdjustedOrigin;

	// Flatten it to 2D
	Vector flatLadder = vecToLadder;
	flatLadder.z = 0.0f;

	// Normalize the vectors (unnecessary)
	VectorNormalize( flatLadder );
	VectorNormalize( flatForward );

	// Compute dot product to see if forward is in same direction as vec to ladder
	float facingDot = flatForward.Dot( flatLadder );

	float requiredDot = ( sv_ladder_useonly.GetBool() ) ? -0.99 : 0.0;

	// Facing same direction if dot > = requiredDot...
	bool facingladder = ( facingDot >= requiredDot );

	return facingladder;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &trace - 
//-----------------------------------------------------------------------------
bool CHL2GameMovement::CheckLadderAutoMount( CFuncLadder *ladder, const Vector& bestOrigin )
{
#if !defined( CLIENT_DLL )

	if ( ladder != NULL )
	{
		StartForcedMove( true, player->MaxSpeed(), bestOrigin, ladder );
		return true;
	}

#endif
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CHL2GameMovement::LadderMove( void )
{

	if ( player->GetMoveType() == MOVETYPE_NOCLIP )
	{
		SetLadder( NULL );
#ifdef HL2_USES_FUNC_LADDER_CODE
		return BaseClass::LadderMove();
#else
		return false;
#endif
	}

	// If being forced to mount/dismount continue to act like we are on the ladder
	if ( IsForceMoveActive() && ContinueForcedMove() )
	{
		return true;
	}

	CFuncLadder *bestLadder = NULL;
	Vector bestOrigin( 0, 0, 0 );

	CFuncLadder *ladder = GetLadder();

	// Something 1) deactivated the ladder...  or 2) something external applied
	//  a force to us.  In either case  make the player fall, etc.
	if ( ladder && 
		 ( !ladder->IsEnabled() ||
		 ( player->GetBaseVelocity().LengthSqr() > 1.0f ) ) )
	{
		GetHL2Player()->ExitLadder();
		ladder = NULL;
	}

	if ( !ladder )
	{
		Findladder( 64.0f, &bestLadder, bestOrigin, NULL );
	}

#if !defined (CLIENT_DLL)
	if( !ladder && bestLadder && sv_ladder_useonly.GetBool() )
	{
		GetHL2Player()->DisplayLadderHudHint();
	}
#endif

	int buttonsChanged	= ( mv->m_nOldButtons ^ mv->m_nButtons );	// These buttons have changed this frame
	int buttonsPressed = buttonsChanged & mv->m_nButtons;
	bool pressed_use = ( buttonsPressed & IN_USE ) ? true : false;

	// If I'm already moving on a ladder, use the previous ladder direction
	if ( !ladder && !pressed_use )
	{
		// If flying through air, allow mounting ladders if we are facing < 15 degress from the ladder and we are close
		if ( !ladder && !sv_ladder_useonly.GetBool() )
		{
			// Tracker 6625:  Don't need to be leaping to auto mount using this method...
			// But if we are on the ground, then we must not be backing into the ladder (Tracker 12961)
			bool onground = player->GetGroundEntity() ? true : false;
			if ( !onground || ( mv->m_flForwardMove > 0.0f ) )
			{
				if ( CheckLadderAutoMountCone( bestLadder, bestOrigin, 15.0f, 32.0f ) )
				{
					return true;
				}
			}
			
			// Pressing forward while looking at ladder and standing (or floating) near a mounting point
			if ( mv->m_flForwardMove > 0.0f )
			{
				if ( CheckLadderAutoMountEndPoint( bestLadder, bestOrigin ) )
				{
					return true;
				}
			}
		}

#ifdef HL2_USES_FUNC_LADDER_CODE
		return BaseClass::LadderMove();
#else
		return false;
#endif
	}

	if ( !ladder && 
		LookingAtLadder( bestLadder ) &&
		CheckLadderAutoMount( bestLadder, bestOrigin ) )
	{
		return true;
	}

	// Reassign the ladder
	ladder = GetLadder();
	if ( !ladder )
	{
#ifdef HL2_USES_FUNC_LADDER_CODE
		return BaseClass::LadderMove();
#else
		return false;
#endif
	}

	// Don't play the deny sound
	if ( pressed_use )
	{
		GetHL2Player()->m_bPlayUseDenySound = false;
	}

	// Make sure we are on the ladder
	player->SetMoveType( MOVETYPE_LADDER );
	player->SetMoveCollide( MOVECOLLIDE_DEFAULT );

	player->SetGravity( 0.0f );
	
	float forwardSpeed = 0.0f;
	float rightSpeed = 0.0f;

	float speed = player->MaxSpeed();


	if ( mv->m_nButtons & IN_BACK )
	{
		forwardSpeed -= speed;
	}
	
	if ( mv->m_nButtons & IN_FORWARD )
	{
		forwardSpeed += speed;
	}
	
	if ( mv->m_nButtons & IN_MOVELEFT )
	{
		rightSpeed -= speed;
	}
	
	if ( mv->m_nButtons & IN_MOVERIGHT )
	{
		rightSpeed += speed;
	}
	
	if ( mv->m_nButtons & IN_JUMP )
	{
		player->SetMoveType( MOVETYPE_WALK );
		// Remove from ladder
		SetLadder( NULL );

		// Jump in view direction
		Vector jumpDir = m_vecForward;

		// unless pressing backward or something like that
		if ( mv->m_flForwardMove < 0.0f )
		{
			jumpDir = -jumpDir;
		}

		VectorNormalize( jumpDir );

		VectorScale( jumpDir, MAX_CLIMB_SPEED, mv->m_vecVelocity );
		// Tracker 13558:  Don't add any extra z velocity if facing downward at all
		if ( m_vecForward.z >= 0.0f )
		{
			mv->m_vecVelocity.z = mv->m_vecVelocity.z + 50;
		}
#ifdef HL2_USES_FUNC_LADDER_CODE
		return BaseClass::LadderMove();
#else
		return false;
#endif
	}

	if ( forwardSpeed != 0 || rightSpeed != 0 )
	{
		// See if the player is looking toward the top or the bottom
		Vector velocity;

		VectorScale( m_vecForward, forwardSpeed, velocity );
		VectorMA( velocity, rightSpeed, m_vecRight, velocity );

		VectorNormalize( velocity );

		Vector ladderUp;
		ladder->ComputeLadderDir( ladderUp );
		VectorNormalize( ladderUp );

		Vector topPosition;
		Vector bottomPosition;

		ladder->GetTopPosition( topPosition );
		ladder->GetBottomPosition( bottomPosition );

		// Check to see if we've mounted the ladder in a bogus spot and, if so, just fall off the ladder...
		float dummyt = 0.0f;
		float distFromLadderSqr = CalcDistanceSqrToLine( mv->GetAbsOrigin(), topPosition, bottomPosition, &dummyt );
		if ( distFromLadderSqr > 36.0f )
		{
			// Uh oh, we fell off zee ladder...
			player->SetMoveType( MOVETYPE_WALK );
			// Remove from ladder
			SetLadder( NULL );
#ifdef HL2_USES_FUNC_LADDER_CODE
			return BaseClass::LadderMove();
#else
			return false;
#endif
		}

		bool ishorizontal = fabs( topPosition.z - bottomPosition.z ) < 64.0f ? true : false;

		float changeover = ishorizontal ? 0.0f : 0.3f;

		float factor = 1.0f;
		if ( velocity.z >= 0 )
		{
			float dotTop = ladderUp.Dot( velocity );
			if ( dotTop < -changeover )
			{
				// Aimed at bottom
				factor = -1.0f;
			}
		}
		else
		{
			float dotBottom = -ladderUp.Dot( velocity );
			if ( dotBottom > changeover )
			{
				factor = -1.0f;
			}
		}

#ifdef _XBOX
		if( sv_ladders_useonly.GetBool() )
		{
			// Stick up climbs up, stick down climbs down. No matter which way you're looking.
			if ( mv->m_nButtons & IN_FORWARD )
			{
				factor = 1.0f;
			}
			else if( mv->m_nButtons & IN_BACK )
			{
				factor = -1.0f;
			}
		}
#endif//_XBOX

		mv->m_vecVelocity = MAX_CLIMB_SPEED * factor * ladderUp;
	}
	else
	{
		mv->m_vecVelocity.Init();
	}

	return true;
}

void CHL2GameMovement::SetGroundEntity( trace_t *pm )
{
	CBaseEntity *newGround = pm ? pm->m_pEnt : NULL;

	//Adrian: Special case for combine balls.
	if ( newGround && newGround->GetCollisionGroup() == HL2COLLISION_GROUP_COMBINE_BALL_NPC )
	{
		return;
	}

	BaseClass::SetGroundEntity( pm );
}

bool CHL2GameMovement::CanAccelerate()
{
#ifdef HL2MP 
	if ( player->IsObserver() )
	{
		return true;
	}
#endif

	BaseClass::CanAccelerate();

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Allow bots etc to use slightly different solid masks
//-----------------------------------------------------------------------------
unsigned int CHL2GameMovement::PlayerSolidMask( bool brushOnly )
{
	int mask = 0;
#ifdef HL2MP
	if ( HL2MPRules()->IsTeamplay() )
	{
		switch ( player->GetTeamNumber() )
		{
		case TEAM_REBELS:
			mask = CONTENTS_TEAM1;
			break;

		case TEAM_COMBINE:
			mask = CONTENTS_TEAM2;
			break;
		}
	}
#endif
	return ( mask | BaseClass::PlayerSolidMask( brushOnly ) );
}

#ifdef HL2_EPISODIC
//-----------------------------------------------------------------------------
// OF2: tether hang. While m_HL2Local.m_bOnTether is set the player is held
// within m_flTetherSwingLength of m_vecTetherSwingPoint: on the ground they
// walk as usual, in the air they are a pendulum. Whatever they hang from
// (climb rope, Barnacle) sets those values; this code only lets go, and
// changes the length when climbing.
//-----------------------------------------------------------------------------
ConVar of2_tether_hand_height( "of2_tether_hand_height", "56", FCVAR_REPLICATED, "How far above the player's feet a tether is held." );
static ConVar of2_tether_damping( "of2_tether_damping", "0.15", FCVAR_REPLICATED, "Share of their speed a player hanging from a tether loses per second." );
static ConVar of2_tether_min_length( "of2_tether_min_length", "24", FCVAR_REPLICATED, "Closest a player can climb to the point their tether swings from." );
static ConVar of2_tether_release_up( "of2_tether_release_up", "120", FCVAR_REPLICATED, "Upward speed added when a player jumps off a tether." );
static ConVar of2_tether_release_forward( "of2_tether_release_forward", "80", FCVAR_REPLICATED, "Forward speed added when a player jumps off a tether." );
static ConVar of2_tether_pump_scale( "of2_tether_pump_scale", "0.5", FCVAR_REPLICATED, "Scales how hard forward/back pushes the swing on every tether, on top of each rope's own pump strength." );
static ConVar of2_tether_climb_angle( "of2_tether_climb_angle", "20", FCVAR_REPLICATED, "Forward/back climbs a tether only while the player looks along it to within this many degrees (full speed within half of it). Outside it only pumps the swing." );
static ConVar of2_tether_mantle( "of2_tether_mantle", "1", FCVAR_REPLICATED, "A player pulled all the way up to where their tether goes over an edge is moved up onto it, if there is room to stand there." );
static ConVar of2_tether_mantle_speed( "of2_tether_mantle_speed", "220", FCVAR_REPLICATED, "Speed a player is moved up and onto an edge at the top of their tether." );
static ConVar of2_tether_mantle_reach( "of2_tether_mantle_reach", "24", FCVAR_REPLICATED, "How far in from the edge a player is put down when moved up onto it." );
static ConVar of2_tether_mantle_height( "of2_tether_mantle_height", "32", FCVAR_REPLICATED, "The ground a player is moved up onto can be this far above or below the point their tether swings from." );

float OF2_TetherMinLength( void )
{
	return of2_tether_min_length.GetFloat();
}

float OF2_TetherHandHeight( CBasePlayer *pPlayer )
{
	// As far below the eyes as it is standing up, so that it comes down with
	// them in a crouch, as smoothly as the view does. (It only dropped to just
	// inside the crouched box before, which is where the crouched eyes are: the
	// rope was held in front of the face.)
	float flHeight = of2_tether_hand_height.GetFloat() - ( VEC_VIEW.z - pPlayer->GetViewOffset().z );

	// Inside the player's box, and off the ground
	return clamp( flHeight, 8.0f, MAX( pPlayer->GetPlayerMaxs().z - 8.0f, 8.0f ) );
}

//-----------------------------------------------------------------------------
// Where a tether is held: the hand, a little way in front of the player so it
// doesn't run through the view. This is both where it is drawn to and the
// point the player hangs by, so the two can't be told apart: turning the view
// carries the player's body around the tether, not the tether around them.
// The client asks every frame, for drawing; the movement code every tick.
//-----------------------------------------------------------------------------
static ConVar of2_tether_hold_forward( "of2_tether_hold_forward", "16", FCVAR_REPLICATED, "How far in front of the player a held tether is drawn." );
static ConVar of2_tether_hold_left( "of2_tether_hold_left", "6", FCVAR_REPLICATED, "How far to the left of the player a held tether is drawn, to keep it off the weapon sights." );

// A tether that comes out of the weapon in hand (the Barnacle) is drawn from
// about where that is on screen instead: bottom right, turning with the view
static ConVar of2_tether_weapon_forward( "of2_tether_weapon_forward", "20", FCVAR_REPLICATED, "How far in front of the eyes a tether coming out of the held weapon is drawn from." );
static ConVar of2_tether_weapon_right( "of2_tether_weapon_right", "9", FCVAR_REPLICATED, "How far to the right of the eyes a tether coming out of the held weapon is drawn from." );
static ConVar of2_tether_weapon_down( "of2_tether_weapon_down", "11", FCVAR_REPLICATED, "How far below the eyes a tether coming out of the held weapon is drawn from." );

//-----------------------------------------------------------------------------
// The same point from the eyes (bAtWeapon) or from the feet (otherwise), for
// a player looking along angEyes
//-----------------------------------------------------------------------------
static Vector TetherHoldOffset( CBasePlayer *pPlayer, const QAngle &angEyes, bool bAtWeapon )
{
	Vector vecForward, vecRight;
	if ( bAtWeapon )
	{
		Vector vecUp;
		AngleVectors( angEyes, &vecForward, &vecRight, &vecUp );

		return vecForward * of2_tether_weapon_forward.GetFloat()
			+ vecRight * of2_tether_weapon_right.GetFloat()
			- vecUp * of2_tether_weapon_down.GetFloat();
	}

	AngleVectors( QAngle( 0, angEyes.y, 0 ), &vecForward, &vecRight, NULL );

	return Vector( 0, 0, OF2_TetherHandHeight( pPlayer ) )
		+ vecForward * of2_tether_hold_forward.GetFloat() - vecRight * of2_tether_hold_left.GetFloat();
}

//-----------------------------------------------------------------------------
// The hold point sticks out of the player's box, so turning next to a wall
// would put it in the wall, and the tether with it: it caught on the wall
// there. So it is kept clear of the world, as if it had a small ball around
// it that only the world and static props stop: pushed out from inside the
// player (vecFrom) to where it belongs, it stops where the ball does.
//-----------------------------------------------------------------------------
static ConVar of2_tether_hold_clear( "of2_tether_hold_clear", "4", FCVAR_REPLICATED, "The point a tether is held by keeps this far off walls. 0: it goes where it goes, into them too." );

static Vector TetherClearHold( const Vector &vecFrom, const Vector &vecHold )
{
	float flClear = of2_tether_hold_clear.GetFloat();
	if ( flClear <= 0.0f )
		return vecHold;

	Vector vecSize( flClear, flClear, flClear );
	CTraceFilterWorldAndPropsOnly filter;

	trace_t tr;
	UTIL_TraceHull( vecFrom, vecHold, -vecSize, vecSize, MASK_SOLID_BRUSHONLY, &filter, &tr );
	if ( tr.startsolid )
		return vecHold;

	return tr.endpos;
}

Vector OF2_TetherHoldPos( CBasePlayer *pPlayer, bool bAtWeapon )
{
	QAngle angEyes;
#ifdef CLIENT_DLL
	engine->GetViewAngles( angEyes );
#else
	angEyes = pPlayer->EyeAngles();
#endif

	Vector vecBase = bAtWeapon ? pPlayer->EyePosition() : pPlayer->GetAbsOrigin();
	Vector vecFrom = bAtWeapon ? vecBase : vecBase + Vector( 0, 0, OF2_TetherHandHeight( pPlayer ) );

	return TetherClearHold( vecFrom, vecBase + TetherHoldOffset( pPlayer, angEyes, bAtWeapon ) );
}

//-----------------------------------------------------------------------------
// OF2: the point the player hangs by, from their feet, while they are being moved
//-----------------------------------------------------------------------------
Vector CHL2GameMovement::TetherHandOffset( void )
{
	bool bAtWeapon = GetHL2Player()->m_HL2Local.m_bTetherAtWeapon;

	Vector vecOffset = TetherHoldOffset( player, mv->m_vecViewAngles, bAtWeapon );
	Vector vecFrom( 0, 0, OF2_TetherHandHeight( player ) );
	if ( bAtWeapon )
	{
		vecOffset += player->GetViewOffset();
		vecFrom = player->GetViewOffset();
	}

	// (kept off walls, as OF2_TetherHoldPos does)
	return TetherClearHold( mv->GetAbsOrigin() + vecFrom, mv->GetAbsOrigin() + vecOffset ) - mv->GetAbsOrigin();
}

void CHL2GameMovement::FullWalkMove()
{
	CHL2_Player *pPlayer = GetHL2Player();
	if ( !pPlayer->m_HL2Local.m_bOnTether )
	{
		BaseClass::FullWalkMove();
		return;
	}

	bool bOnGround = ( player->GetGroundEntity() != NULL );

	// Swimming lets go
	if ( player->GetWaterLevel() >= WL_Waist )
	{
		pPlayer->m_HL2Local.m_bOnTether = false;
		BaseClass::FullWalkMove();
		return;
	}

	// So does jump in mid-air, with a push up and forwards. From the ground it
	// is an ordinary jump with the tether still in hand.
	if ( !bOnGround && ( mv->m_nButtons & IN_JUMP ) && !( mv->m_nOldButtons & IN_JUMP ) )
	{
		pPlayer->m_HL2Local.m_bOnTether = false;

		Vector vecForward( m_vecForward.x, m_vecForward.y, 0.0f );
		VectorNormalize( vecForward );
		mv->m_vecVelocity += vecForward * of2_tether_release_forward.GetFloat();
		mv->m_vecVelocity.z += of2_tether_release_up.GetFloat();

		BaseClass::FullWalkMove();
		return;
	}

	// On the way up onto an edge: nothing else moves them until they are there
	if ( pPlayer->m_HL2Local.m_bTetherMantling )
	{
		TetherMantleMove();
		return;
	}

	if ( bOnGround )
	{
		BaseClass::FullWalkMove();
	}

	Vector vecSwingPoint = pPlayer->m_HL2Local.m_vecTetherSwingPoint;
	Vector vecUpTether = vecSwingPoint - ( mv->GetAbsOrigin() + TetherHandOffset() );
	float flDist = VectorNormalize( vecUpTether );

	// Forward/back acts along the look direction: the part along the tether
	// climbs, the part across it pumps the swing
	float flInput = 0.0f;
	if ( mv->m_nButtons & IN_FORWARD )
	{
		flInput += 1.0f;
	}
	if ( mv->m_nButtons & IN_BACK )
	{
		flInput -= 1.0f;
	}

	Vector vecWish = m_vecForward * flInput;
	float flAlong = DotProduct( vecWish, vecUpTether );
	Vector vecAcross = vecWish - vecUpTether * flAlong;

	float flClimbAngle = clamp( of2_tether_climb_angle.GetFloat(), 1.0f, 89.0f );
	float flClimb = RemapValClamped( fabs( flAlong ), cos( DEG2RAD( flClimbAngle ) ), cos( DEG2RAD( flClimbAngle * 0.5f ) ), 0.0f, 1.0f );
	if ( flAlong < 0.0f )
	{
		flClimb = -flClimb;
	}

	// Standing, the tether runs through the hand: no slack to fall into when
	// stepping off a ledge, and free to walk until all of it is out
	float flLength = bOnGround ? flDist : pPlayer->m_HL2Local.m_flTetherSwingLength;
	float flMaxLength = pPlayer->m_HL2Local.m_flTetherMaxLength;

	flLength -= flClimb * pPlayer->m_HL2Local.m_flTetherClimbSpeed * gpGlobals->frametime;
	flLength = clamp( flLength, MIN( of2_tether_min_length.GetFloat(), flMaxLength ), flMaxLength );
	pPlayer->m_HL2Local.m_flTetherSwingLength = flLength;

	if ( bOnGround )
	{
		// Climbing up takes the player off the ground; otherwise a tether at
		// its full length only holds them back
		bool bStayOnGround = ( flClimb <= 0.0f );

		// So does a tether that is being taken in (a Barnacle reeling), where it
		// goes up steeply enough. It pulls along its own line, as on any loose
		// thing: the player gets the speed it comes in at, towards where it
		// goes, and leaves the ground if the upward part of that would lift
		// them over a step. (They were slid along the ground until right under
		// it before, and only then lifted.) Walking against it doesn't: that
		// pulls far slower than a reel.
		float flPull = ( gpGlobals->frametime > 0.0f ) ? MIN( ( flDist - flLength ) / gpGlobals->frametime, 1000.0f ) : 0.0f;
		if ( bStayOnGround && flPull * vecUpTether.z > sqrt( 2.0f * GetCurrentGravity() * player->m_Local.m_flStepSize ) )
		{
			bStayOnGround = false;

			float flToward = DotProduct( mv->m_vecVelocity, vecUpTether );
			if ( flToward < flPull )
			{
				mv->m_vecVelocity += vecUpTether * ( flPull - flToward );
			}
		}

		if ( TetherConstrain( bStayOnGround ) )
		{
			SetGroundEntity( NULL );
		}
		return;
	}

	// Pulled all the way up to where the tether goes over an edge (climbing, or
	// whatever they hang from has taken in all there was): up onto it
	if ( of2_tether_mantle.GetBool() )
	{
		float flMinLength = of2_tether_min_length.GetFloat();
		bool bAtTop = flLength <= flMinLength + 1.0f && flDist <= flMinLength + 8.0f;
		bool bPulling = flClimb > 0.0f || flMaxLength <= flMinLength + 1.0f;

		// ...or, with nowhere to stand there, around it: out from under an
		// overhang and up past its edge, to carry on up the tether beyond
		Vector vecVia, vecDest;
		if ( bAtTop && bPulling && ( FindTetherMantle( vecSwingPoint, &vecVia, &vecDest ) || FindTetherPass( vecSwingPoint, &vecVia, &vecDest ) ) )
		{
			pPlayer->m_HL2Local.m_bTetherMantling = true;
			pPlayer->m_HL2Local.m_bTetherMantleVia = true;
			pPlayer->m_HL2Local.m_vecTetherMantleVia = vecVia;
			pPlayer->m_HL2Local.m_vecTetherMantleDest = vecDest;
			TetherMantleMove();
			return;
		}
	}

	// Hanging: a pendulum, rigid when taut
	StartGravity();

	mv->m_vecVelocity += vecAcross * ( pPlayer->m_HL2Local.m_flTetherPump * of2_tether_pump_scale.GetFloat() * gpGlobals->frametime );
	mv->m_vecVelocity *= pow( 1.0f - clamp( of2_tether_damping.GetFloat(), 0.0f, 1.0f ), gpGlobals->frametime );
	CheckVelocity();

	TryPlayerMove();
	TetherConstrain( false );

	CategorizePosition();
	CheckVelocity();
	FinishGravity();

	if ( player->GetGroundEntity() != NULL )
	{
		mv->m_vecVelocity.z = 0.0f;
	}

	CheckFalling();
}

//-----------------------------------------------------------------------------
// OF2: is there somewhere to stand next to the point the tether swings from?
// That point is where the tether goes over an edge, or where it is fixed, so
// the top of the ledge is right by it. Looks to each side of it for ground
// the player fits on, that is above them, and that they can get to by going
// straight up from where they hang (to pVia) and then across (to pDest, just
// over the ground).
//-----------------------------------------------------------------------------
bool CHL2GameMovement::FindTetherMantle( const Vector &vecSwingPoint, Vector *pVia, Vector *pDest )
{
	Vector vecOrigin = mv->GetAbsOrigin();
	float flReach = of2_tether_mantle_reach.GetFloat();
	float flHeight = of2_tether_mantle_height.GetFloat();

	// The way they look first, then the way the swing point is from them,
	// then all round
	Vector vecDirs[10];
	int nDirs = 0;

	vecDirs[nDirs++].Init( m_vecForward.x, m_vecForward.y, 0.0f );
	vecDirs[nDirs++].Init( vecSwingPoint.x - vecOrigin.x, vecSwingPoint.y - vecOrigin.y, 0.0f );
	for ( int i = 0; i < 8; i++ )
	{
		float flAngle = DEG2RAD( 45.0f * i );
		vecDirs[nDirs++].Init( cos( flAngle ), sin( flAngle ), 0.0f );
	}

	for ( int i = 0; i < nDirs; i++ )
	{
		Vector vecDir = vecDirs[i];
		if ( VectorNormalize( vecDir ) < 0.01f )
			continue;

		// Ground to stand on, there
		Vector vecAbove = vecSwingPoint + vecDir * flReach;
		Vector vecBelow = vecAbove;
		vecAbove.z += flHeight;
		vecBelow.z -= flHeight;

		trace_t pm;
		TracePlayerBBox( vecAbove, vecBelow, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm );
		if ( pm.startsolid || pm.fraction == 1.0f || pm.plane.normal.z < 0.7f )
			continue;

		Vector vecDest = pm.endpos;
		if ( vecDest.z < vecOrigin.z + 16.0f )
			continue;

		// The way there: up, then across
		Vector vecUp( vecOrigin.x, vecOrigin.y, vecDest.z + 1.0f );
		TracePlayerBBox( vecOrigin, vecUp, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm );
		if ( pm.startsolid || pm.fraction < 1.0f )
			continue;

		Vector vecAcross( vecDest.x, vecDest.y, vecUp.z );
		TracePlayerBBox( vecUp, vecAcross, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm );
		if ( pm.startsolid || pm.fraction < 1.0f )
			continue;

		*pVia = vecUp;
		*pDest = vecAcross;
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// OF2: the tether goes over an edge and on from there (m_vecTetherNextPoint),
// and the player has been pulled up to that edge with the thing it is the edge
// of in their way: hanging under an overhang, say, with the tether going up
// around its lip. Is there a place just past the edge, along the way the
// tether runs on, where they fit and from where their hand sees the next
// point (so the tether comes off the edge)? And a way to it, in two straight
// legs: out and then up, or up and then across.
//-----------------------------------------------------------------------------
bool CHL2GameMovement::FindTetherPass( const Vector &vecSwingPoint, Vector *pVia, Vector *pDest )
{
	CHL2_Player *pPlayer = GetHL2Player();

	Vector vecNext = pPlayer->m_HL2Local.m_vecTetherNextPoint;
	Vector vecOn = vecNext - vecSwingPoint;
	float flOn = VectorNormalize( vecOn );
	if ( flOn < 1.0f )
		return false;

	Vector vecOrigin = mv->GetAbsOrigin();
	Vector vecHandOffset = TetherHandOffset();

	// With the hand this far past the edge
	float flPast = MIN( of2_tether_min_length.GetFloat() + 8.0f, flOn );
	Vector vecBase = vecSwingPoint + vecOn * flPast - vecHandOffset;

	// There, or failing that a little to any side of there: next to an edge
	// the player's box is usually half in what the edge belongs to
	static const float flRings[3] = { 0.0f, 18.0f, 34.0f };
	for ( int r = 0; r < 3; r++ )
	{
		for ( int i = 0; i < ( ( r == 0 ) ? 1 : 8 ); i++ )
		{
			float flAngle = DEG2RAD( 45.0f * i );
			Vector vecPlace = vecBase + Vector( cos( flAngle ), sin( flAngle ), 0.0f ) * flRings[r];

			trace_t pm;
			TracePlayerBBox( vecPlace, vecPlace, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm );
			if ( pm.startsolid || pm.fraction < 1.0f )
				continue;

			// Past the edge, and the tether would run clear from the hand
			Vector vecHand = vecPlace + vecHandOffset;
			if ( DotProduct( vecHand - vecSwingPoint, vecOn ) <= 0.0f )
				continue;

			trace_t tr;
			UTIL_TraceLine( vecHand, vecNext, MASK_SOLID_BRUSHONLY, player, COLLISION_GROUP_NONE, &tr );
			if ( tr.startsolid || ( tr.fraction < 1.0f && tr.endpos.DistToSqr( vecNext ) > 4.0f * 4.0f ) )
				continue;

			// Out and then up, or up and then across
			Vector vecVias[2];
			vecVias[0].Init( vecPlace.x, vecPlace.y, vecOrigin.z );
			vecVias[1].Init( vecOrigin.x, vecOrigin.y, vecPlace.z );
			for ( int k = 0; k < 2; k++ )
			{
				TracePlayerBBox( vecOrigin, vecVias[k], PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm );
				if ( pm.startsolid || pm.fraction < 1.0f )
					continue;

				TracePlayerBBox( vecVias[k], vecPlace, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm );
				if ( pm.startsolid || pm.fraction < 1.0f )
					continue;

				*pVia = vecVias[k];
				*pDest = vecPlace;
				return true;
			}
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// OF2: moves the player the way FindTetherMantle or FindTetherPass found: in
// a straight line to the way point, then in another to the end. The tether
// doesn't hold them meanwhile; its length follows them, so they are still on
// it when they arrive. Anything in the way ends it, and they hang again.
//-----------------------------------------------------------------------------
void CHL2GameMovement::TetherMantleMove( void )
{
	CHL2_Player *pPlayer = GetHL2Player();
	bool bVia = pPlayer->m_HL2Local.m_bTetherMantleVia;
	Vector vecTarget = pPlayer->m_HL2Local.m_vecTetherMantleDest;
	if ( bVia )
	{
		vecTarget = pPlayer->m_HL2Local.m_vecTetherMantleVia;
	}
	Vector vecStart = mv->GetAbsOrigin();

	float flSpeed = MAX( of2_tether_mantle_speed.GetFloat(), 10.0f );
	float flStep = flSpeed * gpGlobals->frametime;

	Vector vecGoal = vecTarget;
	Vector vecDir = vecTarget - vecStart;
	bool bReached = true;
	if ( VectorNormalize( vecDir ) > flStep )
	{
		vecGoal = vecStart + vecDir * flStep;
		bReached = false;
	}

	bool bArrived = bReached && !bVia;
	if ( bReached && bVia )
	{
		pPlayer->m_HL2Local.m_bTetherMantleVia = false;
	}

	trace_t pm;
	TracePlayerBBox( vecStart, vecGoal, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm );
	if ( !pm.startsolid )
	{
		mv->SetAbsOrigin( pm.endpos );
	}

	bool bBlocked = pm.startsolid || pm.fraction < 1.0f;

	// The tether runs out to wherever they are now
	Vector vecHand = mv->GetAbsOrigin() + TetherHandOffset();
	pPlayer->m_HL2Local.m_flTetherSwingLength = vecHand.DistTo( pPlayer->m_HL2Local.m_vecTetherSwingPoint );

	if ( bArrived || bBlocked )
	{
		pPlayer->m_HL2Local.m_bTetherMantling = false;
		mv->m_vecVelocity.Init();
		CategorizePosition();
		return;
	}

	SetGroundEntity( NULL );
	mv->m_vecVelocity = vecDir * flSpeed;
}

bool CHL2GameMovement::TetherConstrain( bool bStayOnGround )
{
	CHL2_Player *pPlayer = GetHL2Player();
	Vector vecSwingPoint = pPlayer->m_HL2Local.m_vecTetherSwingPoint;
	float flLength = pPlayer->m_HL2Local.m_flTetherSwingLength;
	Vector vecHandOffset = TetherHandOffset();

	// From the swing point out to the hand
	Vector vecDir = mv->GetAbsOrigin() + vecHandOffset - vecSwingPoint;
	float flDist = VectorNormalize( vecDir );
	if ( flDist <= flLength )
		return false;

	// Taut. Back to where the tether reaches: straight towards the swing
	// point, or along the ground if that is asked for and it reaches the ground.
	bool bLifted = true;
	Vector vecDest = vecSwingPoint + vecDir * flLength - vecHandOffset;

	float flDrop = vecSwingPoint.z - ( mv->GetAbsOrigin().z + vecHandOffset.z );
	Vector vecFlat( vecDir.x, vecDir.y, 0.0f );
	if ( bStayOnGround && fabs( flDrop ) < flLength && VectorNormalize( vecFlat ) > 0.001f )
	{
		bLifted = false;
		vecDir = vecFlat;
		vecDest = vecSwingPoint + vecFlat * sqrt( flLength * flLength - flDrop * flDrop ) - vecHandOffset;
		vecDest.z = mv->GetAbsOrigin().z;
	}

	// Something in the way doesn't stop them dead: they slide along it, as far
	// towards where the tether wants them as it allows. (Pulled up against a
	// wall or under an overhang, a straight pull would leave them stuck.)
	for ( int i = 0; i < 3; i++ )
	{
		trace_t pm;
		TracePlayerBBox( mv->GetAbsOrigin(), vecDest, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm );
		if ( pm.startsolid )
			break;

		mv->SetAbsOrigin( pm.endpos );
		if ( pm.fraction == 1.0f )
			break;

		Vector vecLeft = vecDest - pm.endpos;
		vecLeft -= pm.plane.normal * DotProduct( vecLeft, pm.plane.normal );
		if ( vecLeft.LengthSqr() < 0.1f * 0.1f )
			break;

		vecDest = pm.endpos + vecLeft;
	}

	// Only the speed away from the swing point goes
	float flOut = DotProduct( mv->m_vecVelocity, vecDir );
	if ( flOut > 0.0f )
	{
		mv->m_vecVelocity -= vecDir * flOut;
	}

	// Past the highest the swing may go: nothing that takes it higher still
	float flMaxAngle = pPlayer->m_HL2Local.m_flTetherMaxAngle;
	if ( bLifted && flMaxAngle > 0.0f && flMaxAngle < 180.0f )
	{
		vecFlat.Init( vecDir.x, vecDir.y, 0.0f );
		float flSin = VectorNormalize( vecFlat );
		if ( -vecDir.z < cos( DEG2RAD( flMaxAngle ) ) && flSin > 0.001f )
		{
			Vector vecHigher = vecFlat * -vecDir.z + Vector( 0, 0, flSin );
			float flHigher = DotProduct( mv->m_vecVelocity, vecHigher );
			if ( flHigher > 0.0f )
			{
				mv->m_vecVelocity -= vecHigher * flHigher;
			}
		}
	}

	return bLifted;
}
#endif // HL2_EPISODIC

#ifndef PORTAL	// Portal inherits from this but needs to declare it's own global interface
	// Expose our interface.
	static CHL2GameMovement g_GameMovement;
	IGameMovement *g_pGameMovement = ( IGameMovement * )&g_GameMovement;

	EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CGameMovement, IGameMovement,INTERFACENAME_GAMEMOVEMENT, g_GameMovement );
#endif