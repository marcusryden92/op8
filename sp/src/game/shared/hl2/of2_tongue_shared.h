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

// Points the tongue is drawn through: its beads, and the places between them
// where it bends over an edge. Bends past this many are left out.
#define OF2_TONGUE_MAX_NODES	64

#endif // OF2_TONGUE_SHARED_H
