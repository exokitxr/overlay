#include "stdafx.h"
#define GENFILE
#include "BaseCommon.h"

GEN_INTERFACE("Overlay", "010")
GEN_INTERFACE("Overlay", "011")
// TODO 012
GEN_INTERFACE("Overlay", "013")
GEN_INTERFACE("Overlay", "014")
// TODO 015
GEN_INTERFACE("Overlay", "016")
GEN_INTERFACE("Overlay", "017")
GEN_INTERFACE("Overlay", "018")
GEN_INTERFACE("Overlay", "019")

#include "GVROverlay.gen.h"

vr::EVROverlayError CVROverlay_011::GetOverlayTexture(vr::VROverlayHandle_t ulOverlayHandle, void** pNativeTextureHandle,
	void* pNativeTextureRef, uint32_t* pWidth, uint32_t* pHeight, uint32_t* pNativeFormat, EGraphicsAPIConvention* pAPI,
	vr::EColorSpace* pColorSpace) {

	// It should be fairly simple, but it's unlikely to be used so I can't be bothered implementing it now
  getOut() << "abort CVROverlay_011::GetOverlayTexture" << std::endl; abort();
	// STUBBED();
}

vr::EVROverlayError CVROverlay_013::GetOverlayTexture(vr::VROverlayHandle_t ulOverlayHandle, void** pNativeTextureHandle,
	void* pNativeTextureRef, uint32_t* pWidth, uint32_t* pHeight, uint32_t* pNativeFormat, EGraphicsAPIConvention* pAPI,
	vr::EColorSpace* pColorSpace) {

	// It should be fairly simple, but it's unlikely to be used so I can't be bothered implementing it now
  getOut() << "abort CVROverlay_013::GetOverlayTexture" << std::endl; abort();
  // STUBBED();
}
