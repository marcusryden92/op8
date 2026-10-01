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

// Depth of field while night vision is on, in the terms viewpostprocess.cpp uses:
// fully blurred nearer than flNearBlurDepth, sharp between the two focus depths,
// fully blurred beyond flFarBlurDepth. Radii are in pixels.
struct OF2DepthOfField_t
{
	float flNearBlurDepth;
	float flNearFocusDepth;
	float flFarFocusDepth;
	float flFarBlurDepth;
	float flNearBlurRadius;
	float flFarBlurRadius;
};

// Returns false when night vision has no blur to apply right now. pDOF may be NULL.
bool OF2_NightVisionGetDepthOfField( OF2DepthOfField_t *pDOF );

#endif // OF2_NIGHTVISION_H
