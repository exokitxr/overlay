#ifndef _openvr_coreproxy_h_
#define _openvr_coreproxy_h_

#include <deque>
#include <algorithm>

#include <D3D11_1.h>
#include <DXGI1_4.h>
#include <wrl.h>

#include "third_party/openvr/src/headers/openvr.h"
#include "device/vr/openvr/test/out.h"
#include "device/vr/openvr/test/glcontext.h"
#include "device/vr/openvr/test/fnproxy.h"

namespace vr {
class PVRClientCore {
public:
  FnProxy &fnp;

  // Mutex mut;
  std::vector<size_t> waitSemsOrder;
  std::deque<size_t> submitSemsOrder;
  std::map<size_t, Semaphore> localSems;
  // bool rightEye;

  PVRClientCore(FnProxy &fnp);
  EVRInitError Init(EVRApplicationType eApplicationType, const char *pStartupInfo);
	void Cleanup();
  Semaphore *getLocalSemaphore(size_t id);
    
  void PreWaitGetPoses(bool *doRealWait);
  void PostWaitGetPoses();
  void PreSubmit(bool *doRealSubmit);
  void PostSubmit();
};
}

#endif