// ScreenGameModule.h
//
// Created by Alan Lawrence on 17/06/2011.
// Copyright 2011 The Dasher Team.
//
// This file is part of Dasher.
//
// Dasher is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// Dasher is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Dasher; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#pragma once

#include "GameModule.h"

namespace Dasher {
  class CScreenGameModule : public CGameModule {
  public:
    CScreenGameModule(CSettingsStore* pSettingsStore, CDasherInterfaceBase *pInterface,CDasherView *pView, CDasherModel *pModel);
    void HandleEditEvent(CEditEvent::EditEventType type, const std::string& strText, CDasherNode* node) override;
  protected:
    virtual void ChunkGenerated() override;
    virtual void DrawText(CDasherView *pView) override;
  private:
    std::string m_strEntered, m_strTarget;
    CDasherScreen::Label *m_pLabEntered, *m_pLabTarget, *m_pLabWrong;
    int m_iFirstSym, m_iLastSym;
  };
}

