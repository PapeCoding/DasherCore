
// ButtonMode.h 
// Copyright 2009 by Alan Lawrence

#ifndef __BUTTON_MODE_H__
#define __BUTTON_MODE_H__

#include "Event.h"
#include "DasherButtons.h"

namespace Dasher {
/// \ingroup Input
/// @{
/// Handles the "menu mode" and "direct mode" input filters, according to the bMenu constructor parameter.
class CButtonMode : public CDasherButtons, protected CSettingsObserver
{
 public:
  CButtonMode(CSettingsUser *pCreator, CDasherInterfaceBase *pInterface, bool bMenu, int iID, const char *szName);

  virtual void HandleEvent(Parameter parameter);
  void Timer(unsigned long Time, CDasherView *pView, CDasherInput *pInput, CDasherModel *pModel, CExpansionPolicy **pol);
  bool DecorateView(CDasherView *pView, CDasherInput *pInput);

  //override to get mouse clicks/taps back again
  virtual void KeyDown(unsigned long Time, Keys::VirtualKey Key, CDasherView *pView, CDasherInput *pInput, CDasherModel *pModel);
  
  bool GetSettings(SModuleSettings **pSettings, int *iCount);
 protected: 
  void SetupBoxes();
  void DirectKeyDown(unsigned long iTime, Keys::VirtualKey Key, CDasherView* pView, CDasherModel* pModel);
 private:
  bool m_bHighlight;
  unsigned long m_iLastTime;
};
}
/// @}

#endif
