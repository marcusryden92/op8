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

// Most points the loose part of the tongue is simulated as: from the last place
// it bends over an edge (or the barnacle, if it doesn't) out to the tip. How
// many there are at a time goes by its length.
#define OF2_TONGUE_NODES		48

// Places where the tongue bends over an edge, between the barnacle and the
// loose part (the same as OF2_TETHER_MAX_PIVOTS)
#define OF2_TONGUE_MAX_BENDS	16

#endif // OF2_TONGUE_SHARED_H
