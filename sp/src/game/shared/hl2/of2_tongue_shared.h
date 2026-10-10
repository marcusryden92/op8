//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: what the server (COF2Tongue, weapon_barnacle.cpp) and the
//			client (C_OF2Tongue, c_of2_tongue.cpp) have to agree on about the
//			Barnacle's tongue.
//
//=============================================================================//

#ifndef OF2_TONGUE_SHARED_H
#define OF2_TONGUE_SHARED_H
#ifdef _WIN32
#pragma once
#endif

// Most points the tongue is simulated as, from the barnacle out to the tip. How
// many there are at a time goes by its length: one about every
// of2_barnacle_spacing units, so this is enough for 1580 units at the default 20.
#define OF2_TONGUE_NODES		80

// Edges the straight way from the barnacle to the tip goes over (the same as
// OF2_TETHER_MAX_PIVOTS)
#define OF2_TONGUE_MAX_BENDS	16

// The barnacle in the hand whips before it snaps a neck: the server starts it
// ("OF2BarnacleFlick", with how long all of it takes) and snaps the neck when
// this share of that time has gone by, which is when the client has the mouth
// at the top.
#define OF2_BARNACLE_FLICK_SNAP	0.431f

#endif // OF2_TONGUE_SHARED_H
