//========= OF2 ===============================================================//
//
// Purpose: Night vision. See of2_nightvision.cpp.
//
//=============================================================================//

#ifndef OF2_NIGHTVISION_H
#define OF2_NIGHTVISION_H
#ifdef _WIN32
#pragma once
#endif

// 0 = off, 1 = fully on. Eases between the two when night vision is toggled.
float OF2_NightVisionAmount();

// Called by the color correction manager every frame, after the map's own
// corrections have set their weights and before the weights are committed.
void OF2_NightVisionApplyColorCorrection();

#endif // OF2_NIGHTVISION_H
