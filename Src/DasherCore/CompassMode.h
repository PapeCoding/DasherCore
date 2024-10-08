
// DasherButtons.h
// Copyright 2005 by Chris Ball

#ifndef __COMPASS_MODE_H__
#define __COMPASS_MODE_H__

#include "Event.h"
#include "DasherButtons.h"

namespace Dasher {
/// \ingroup Input
/// @{
class CCompassMode : public CDasherButtons
{
 public:
  CCompassMode(CSettingsUser *pCreator, CDasherInterfaceBase *pInterface);

  virtual void HandleEvent(Parameter parameter);

  bool DecorateView(CDasherView *pView, CDasherInput *pInput);

  bool GetSettings(SModuleSettings **pSettings, int *iCount);

 protected:
  void SetupBoxes();

 private:
  int iTargetWidth;
};
}
/// @}

#endif
