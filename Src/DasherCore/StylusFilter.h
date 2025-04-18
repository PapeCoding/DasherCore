#pragma once

#include <I18n.h>

#include "DefaultFilter.h"
#include "ClickFilter.h"

/// \ingroup InputFilter
/// @{
namespace Dasher {
class CStylusFilter : public CDefaultFilter, protected CZoomAdjuster {
 public:
  CStylusFilter(CSettingsStore* pSettingsStore, CDasherInterfaceBase *pInterface, CFrameRate *pFramerate, const char *szName=_("Stylus Control"));
  ///Override DefaultFilter (which supports pause), as we don't
  /// - motion requires continually holding stylus against screen
  virtual bool supportsPause() {return false;}

  //no Timer method required: DefaultFilter does fine when we're moving,
  // and will ignore any zoom scheduled by a click.
  
  void pause();
  virtual void KeyDown(unsigned long iTime, Keys::VirtualKey Key, CDasherView *pView, CDasherInput *pInput, CDasherModel *pModel);
  virtual void KeyUp(unsigned long iTime, Keys::VirtualKey Key, CDasherView *pView, CDasherInput *pInput, CDasherModel *pModel);
 protected:
  ///Transform coordinates of a click, to get location to zoom into.
  /// Default is to call the same CZoomAdjuster::AdjustZoomCoords as CClickFilter
  virtual void ApplyClickTransform(myint &iDasherX, myint &iDasherY, CDasherView *pView);
  ///Do not make a start handler when in stylus mode
  virtual CStartHandler *MakeStartHandler();
  virtual bool GetSettings(SModuleSettings **pSettings, int *iCount);
 private:
  unsigned long m_iKeyDownTime;
  CDasherModel *m_pModel;
};
}
/// @}

