// AlphabetManager.cpp
//
// Copyright (c) 2007 The Dasher Team
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

#include "AlphabetManager.h"

#include <I18n.h>

#include "ConversionManager.h"
#include "DasherInterfaceBase.h"
#include "DasherNode.h"
#include "NodeCreationManager.h"
#include "LanguageModelling/PPMLanguageModel.h"
#include "LanguageModelling/WordLanguageModel.h"
#include "LanguageModelling/MixtureLanguageModel.h"
#include "LanguageModelling/CTWLanguageModel.h"
#include "FileWordGenerator.h"

#include <vector>

using namespace Dasher;


CNodeManager* Dasher::CAlphBase::mgr() const
{return m_pMgr;}

CDasherInterfaceBase* Dasher::CSymbolNode::GetInterface()
{return m_pMgr->GetInterface();}

CAlphabetManager::CAlphabetManager(CSettingsStore *pSettingsStore, CDasherInterfaceBase *pInterface, CNodeCreationManager *pNCManager, const CAlphInfo *pAlphabet)
    : m_pBaseGroup(NULL), m_pInterface(pInterface), m_pLanguageModel(nullptr), m_pNCManager(pNCManager),
      m_pAlphabet(pAlphabet), m_pLastOutput(NULL),
      m_pSettingsStore(pSettingsStore)
{
    m_pSettingsStore->OnPreParameterChange.Subscribe(this, [this](Parameter parameter, const std::variant<bool, long, std::string>& newValue)
    {
      if(parameter == SP_ALPHABET_ID){
          const std::string value = std::get<std::string>(newValue);
          // Cycle the alphabet history
          std::vector<std::string> newHistory;
          newHistory.push_back(m_pSettingsStore->GetStringParameter(SP_ALPHABET_ID));
          std::string v;
          if ((v = m_pSettingsStore->GetStringParameter(SP_ALPHABET_1)) != value)
            newHistory.push_back(v);
          if ((v = m_pSettingsStore->GetStringParameter(SP_ALPHABET_2)) != value)
            newHistory.push_back(v);
          if ((v = m_pSettingsStore->GetStringParameter(SP_ALPHABET_3)) != value)
            newHistory.push_back(v);
          if ((v = m_pSettingsStore->GetStringParameter(SP_ALPHABET_4)) != value)
            newHistory.push_back(v);

          // Fill empty slots. 
          while (newHistory.size() < 4)
            newHistory.push_back("");

          m_pSettingsStore->SetStringParameter(SP_ALPHABET_1, newHistory[0]);
          m_pSettingsStore->SetStringParameter(SP_ALPHABET_2, newHistory[1]);
          m_pSettingsStore->SetStringParameter(SP_ALPHABET_3, newHistory[2]);
          m_pSettingsStore->SetStringParameter(SP_ALPHABET_4, newHistory[3]);
      }
    });
}

const string &CAlphabetManager::GetLabelText(symbol i) const {
  return m_pAlphabet->GetDisplayText(i);
}

void CAlphabetManager::Setup() {
    InitMap();

    for (char c = 33; (c&0x80)==0; c++) {
        if (m_map.GetSingleChar(c)==0) {
            m_sDelim = c;
            break;
        }
    }
    //else, if all single-octet chars are in alphabet - leave m_sDelim==""
    // (and we'll find a delimiter for each context)

    CreateLanguageModel();
}

void CAlphabetManager::InitMap() {
  for(int i = 1; i < m_pAlphabet->iEnd; i++){ // 1-indexed
    if (m_pAlphabet->SymbolPrintsNewLineCharacter(i)){
        m_map.AddParagraphSymbol(i);
    } else {
        m_map.Add(m_pAlphabet->GetText(i), i);
    }
  }

  /*ACL I'm really not sure where conversion characters should/shouldn't be included.
   They seemed to be included in the Alphabet Map, i.e. for reading training text via GetSymbols;
   but a TODO comment suggested they should _not_ be included in GetNumberSymbols(),
   and I couldn't find any code which would have called e.g. GetText on them.
   Moreover, if these characters are put into the AlphabetMap, they'll be fed into the
   LanguageModel just as any other "symbol", but with an out-of-bounds symbol number!
   (So maybe the range of allowed symbol numbers is wrong?). Hence, not including them atm.
   If they were needed, we could do something like the following:
   if (StartConvertCharacter)
   map->Add(StartConvertCharacter->Text, ++i);
   if (EndConvertCharacter)
   map->Add(EndConvertCharacter->Text, ++i);
   */
}

void CAlphabetManager::CreateLanguageModel() {
  // FIXME - return to using enum here
  switch (m_pSettingsStore->GetLongParameter(LP_LANGUAGE_MODEL_ID)) {
    default:
      // If there is a bogus value for the language model ID, we'll default
      // to our trusty old PPM language model.
    case 0:
      m_pLanguageModel = new CPPMLanguageModel(m_pSettingsStore, m_pAlphabet->iEnd-1);
      break;
    case 2:
      m_pLanguageModel = new CWordLanguageModel(m_pSettingsStore, m_pAlphabet, &m_map);
      break;
    case 3:
      m_pLanguageModel = new CMixtureLanguageModel(m_pSettingsStore, m_pAlphabet, &m_map);
      break;
    case 4:
      m_pLanguageModel = new CCTWLanguageModel(m_pAlphabet->iEnd-1);
      break;
  }
}

CTrainer *CAlphabetManager::GetTrainer() {
  return new CTrainer(m_pInterface, m_pLanguageModel, m_pAlphabet, &m_map);
}

void CAlphabetManager::MakeLabels(CDasherScreen *pScreen) {
  if(m_pBaseGroup){
    delete m_pBaseGroup;
    m_pBaseGroup = nullptr;
  }
  for (vector<CDasherScreen::Label *>::iterator it=m_vLabels.begin(); it!=m_vLabels.end(); it++)
    delete (*it);
  m_vLabels.clear();
  for (map<const SGroupInfo *,CDasherScreen::Label *>::iterator it=m_mGroupLabels.begin(); it!=m_mGroupLabels.end(); it++)
    delete it->second;
  m_mGroupLabels.clear();
  m_pBaseGroup = copyGroups(m_pAlphabet,pScreen);
}

SGroupInfo *CAlphabetManager::copyGroups(const SGroupInfo *pBase, CDasherScreen *pScreen) {
  if (pBase==NULL) return NULL;
  DASHER_ASSERT(pBase->iNumChildNodes); //zero-element groups elided by CAlphIO
  if (m_vLabels.size()<pBase->iEnd) m_vLabels.resize(pBase->iEnd);
  string strGroupPrefix; 
  SGroupInfo * const next=copyGroups(pBase->pNext, pScreen);
  while (pBase->iNumChildNodes==1) {
    //were about to create a group node, which would have only one child
    // (eventually, if the group node were PopulateChildren'd).
    // Such a child would entirely fill it's parent (the group), and thus,
    // creation/destruction of the child would cause the node's colour to flash
    // between that for parent group and child.
    // Hence, instead we elide the group node and create the child _here_...
    
    //1. however we also have to take account of the appearance of the elided group. Hence:
    strGroupPrefix += pBase->strLabel;
    //2. group might contain a single subgroup, or a single symbol...
    if (!pBase->pChild) {
      //single symbol. Create its label, taking account of enclosing groups...
      // (symbols are never transparent)
      DASHER_ASSERT(pBase->iEnd == pBase->iStart+1);
      string symLabel = strGroupPrefix + GetLabelText(pBase->iStart);
      m_vLabels[pBase->iStart]=(symLabel.empty() ? NULL : pScreen->MakeLabel(symLabel));
      //then skip this group, return any siblings
      return next;
    }
    //...a subgroup, so go into it
    pBase = pBase->pChild;
    DASHER_ASSERT(pBase->pNext==NULL); //can't have siblings as parent has only one child
                                       //hence, original 'next' pointer is still valid
    //3. loop round...
  }
  //in or reached nontrivial subgroup - so make node for entire group
  //First, make (unpefixed) labels for all children in (original) group
  // (children of subgroups that are later elided, will have labels made at elision time)
  {
    SGroupInfo *pChild=pBase->pChild;
    for (int i=pBase->iStart; i<pBase->iEnd;)
      if (!pChild || i<pChild->iStart) {
        const string &symLabel(GetLabelText(i));
        m_vLabels[i] = (symLabel.empty() ? NULL : pScreen->MakeLabel(symLabel));
        i++;
      } else {
        i=pChild->iEnd;
        pChild = pChild->pNext;
      }
  }
  SGroupInfo *pRes = new SGroupInfo(*pBase);
  //apply properties of enclosing group(s)...
  pRes->strLabel = strGroupPrefix + pRes->strLabel;
  if (pRes->strLabel.length())
    m_mGroupLabels[pRes] = pScreen->MakeLabel(pRes->strLabel);
  //siblings (of this group or elided parent) copied already, from original
  // (passed-in) pBase: if pBase unchanged, then still valid, whereas if pBase
  // was changed by the above loop to be a subgroup of the original, then the subgroup
  // has no children, so should be spliced in place of the original pBase.
  pRes->pNext = next;

  //recurse on children
  pRes->pChild = copyGroups(pRes->pChild, pScreen);
  DASHER_ASSERT(pRes->iNumChildNodes>1);
  return pRes;
}

CWordGeneratorBase *CAlphabetManager::GetGameWords() {
  CFileWordGenerator *pGen = new CFileWordGenerator(m_pInterface, m_pAlphabet, &m_map);
  pGen->setAcceptUser(true);
  if (!m_pSettingsStore->GetStringParameter(SP_GAME_TEXT_FILE).empty()) {
    const string &gtf(m_pSettingsStore->GetStringParameter(SP_GAME_TEXT_FILE));
    if (pGen->ParseFile(gtf,true)) return pGen;
    ///TRANSLATORS: the string "GameTextFile" is the name of a setting in gsettings
    /// (or equivalent), and should not be translated. The %s is the value of that
    /// setting (this message displayed only if the user has provided a value)
    m_pInterface->FormatMessage("Note: GameTextFile setting specifies game sentences file '%s' but this does not exist", gtf.c_str());
  }
  pGen->setAcceptUser(false);
  m_pInterface->ScanFiles(pGen, m_pAlphabet->GetTrainingFile());
  if (pGen->HasLines()) return pGen;
  delete pGen;
  return NULL;
}

const CAlphInfo *CAlphabetManager::GetAlphabet() const {
  return m_pAlphabet;
}

CAlphabetManager::~CAlphabetManager() {
  //the alphabet belongs to the AlphIO, and may be reused later
  delete m_pLanguageModel;
  m_pSettingsStore->OnPreParameterChange.Unsubscribe(this);
}

void CAlphabetManager::WriteTrainFileFull(CDasherInterfaceBase *pInterface) {
    if (strTrainfileBuffer.empty()) return;
    if (!strTrainfileContext.empty()) {
        //If context begins with the default, skip that - it'll be entered by Trainer 1st anyway
        const string defaultContext = m_pAlphabet->GetDefaultContext();
        if (strTrainfileContext.rfind(defaultContext, 0) == 0)
        {
            strTrainfileContext = strTrainfileContext.substr(defaultContext.length());
        }

        string delimiter = m_sDelim;
        if (delimiter.empty()) {
          //find a character not in the context we want to write out
          char c = 33;
          while (strTrainfileContext.find(c)!=strTrainfileContext.length()) c++; //will terminate, context is ~~5 chars
          delimiter = string(&c,1);
        }
        strTrainfileBuffer = m_pAlphabet->GetContextEscapeChar() + delimiter + strTrainfileContext + delimiter + strTrainfileBuffer;
        strTrainfileContext = "";
    }
    pInterface->WriteTrainFile(m_pAlphabet->GetTrainingFile(), strTrainfileBuffer);
  strTrainfileBuffer="";
}

void CAlphBase::Do() {
  if (m_pMgr->m_pLastOutput && m_pMgr->m_pLastOutput == Parent())
    m_pMgr->m_pLastOutput=this;
  //Case where lastOutput != Parent to subclasses, if they want to.
  //Note if lastOutput==NULL, we leave it - so the first letter written after startup,
  // will register as a context switch and write out an empty/default context.
}

void CAlphBase::Undo() {
  if (m_pMgr->m_pLastOutput==this) m_pMgr->m_pLastOutput = Parent();
}

CAlphBase::CAlphBase(int iOffset, CDasherScreen::Label *pLabel, CAlphabetManager *pMgr)
: CDasherNode(iOffset, pLabel), m_pMgr(pMgr) {
}

CAlphNode::CAlphNode(int iOffset, CDasherScreen::Label *pLabel, CAlphabetManager *pMgr)
: CAlphBase(iOffset, pLabel, pMgr), m_pProbInfo(NULL) {
}

CSymbolNode::CSymbolNode(int iOffset, CDasherScreen::Label *pLabel, CAlphabetManager *pMgr, symbol _iSymbol)
: CAlphNode(iOffset, pLabel, pMgr), iSymbol(_iSymbol) {
}

CGroupNode::CGroupNode(int iOffset, CDasherScreen::Label *pLabel, CAlphabetManager *pMgr, const SGroupInfo* pGroup)
: CAlphNode(iOffset, pLabel, pMgr), m_pGroupInfo(pGroup) {
    renderInRootColor = pGroup==pMgr->m_pBaseGroup && iOffset & 1;
}

CAlphNode *CAlphabetManager::GetRoot(CDasherNode *pParent, bool bEnteredLast, int iOffset) {
  //pParent is not a parent, just for document/context.
  int iNewOffset(max(-1,iOffset-1));

  pair<symbol, CLanguageModel::Context> p = GetContextSymbols(pParent, iNewOffset, &m_map);

  CAlphNode *pNewNode;
  if(p.first==0 || !bEnteredLast) {
    //couldn't extract last symbol (so probably using default context), or shouldn't
    pNewNode = new CGroupNode(iNewOffset, nullptr, this, m_pBaseGroup); //default background colour
  } else {
    //new node represents a symbol that's already happened - i.e. user has already steered through it;
    // so either we're rebuilding, or else creating a new root from existing text (in edit box)
    DASHER_ASSERT(!pParent);
    pNewNode = CreateSymbolRoot(iNewOffset, p.second, p.first);
    pNewNode->SetFlag(CDasherNode::NF_SEEN, true);
    pNewNode->CDasherNode::SetFlag(CDasherNode::NF_COMMITTED, true); //do NOT commit!
  }
  pNewNode->iContext = p.second;
  return pNewNode;
}

CAlphNode *CAlphabetManager::CreateSymbolRoot(int iOffset, CLanguageModel::Context ctx, symbol sym) {
  return new CSymbolNode(iOffset, m_vLabels[sym], this, sym);
}

pair<symbol, CLanguageModel::Context> CAlphabetManager::GetContextSymbols(CDasherNode *pParent, int iRootOffset, const CAlphabetMap *pAlphMap) {
  vector<symbol> vContextSymbols; bool bHaveFinalSymbol = true;
  //no context is ever available at offset -1 (=choice between symbols with offset 0)
  if (iRootOffset!=-1) {
    // TODO: make the LM get the context, rather than force it to fix max context length as an int
    int iStart = max(0, iRootOffset - m_pLanguageModel->GetContextLength());
    if(pParent) {
      pParent->GetContext(m_pInterface, pAlphMap, vContextSymbols, iStart, iRootOffset+1 - iStart);
    } else {
      pAlphMap->GetSymbols(vContextSymbols, m_pInterface->GetContext(iStart, iRootOffset+1 - iStart));
    }

    for (std::vector<symbol>::iterator it = vContextSymbols.end(); it!=vContextSymbols.begin();) {
      if (*(--it) == 0) {
        //found an impossible symbol! erase from beginning up to it (inclusive)
        vContextSymbols.erase(vContextSymbols.begin(), ++it);
        break;
      }
    }
  }
  if (vContextSymbols.empty()) {
    bHaveFinalSymbol = false;
    pAlphMap->GetSymbols(vContextSymbols, m_pAlphabet->GetDefaultContext());
  }

  CLanguageModel::Context iContext = m_pLanguageModel->CreateEmptyContext();

  //enter the symbols we could make sense of, into the LM context...
  for (vector<symbol>::iterator it=vContextSymbols.begin(); it != vContextSymbols.end(); it++) {
    m_pLanguageModel->EnterSymbol(iContext, *it);
  }
  return pair<symbol,CLanguageModel::Context>(bHaveFinalSymbol ? vContextSymbols[vContextSymbols.size()-1] : 0, iContext);
}

bool CSymbolNode::GameSearchNode(symbol sym) {
  if (sym == iSymbol) {
    SetFlag(NF_GAME, true);
    return true;
  }
  return false;
}
bool CGroupNode::GameSearchNode(symbol sym) {
  if (sym >= m_pGroupInfo->iStart && sym < m_pGroupInfo->iEnd) {
    if (GetFlag(NF_ALLCHILDREN)) {
      if (!GameSearchChildren(sym)) //recurse, to mark game child also
        DASHER_ASSERT(false); //sym within this group, should definitely be found!
    }
    SetFlag(NF_GAME, true);
    return true;
  }
  DASHER_ASSERT(!GameSearchChildren(sym));
  return false;
}

void CSymbolNode::GetContext(CDasherInterfaceBase *pInterface, const CAlphabetMap *pAlphabetMap, vector<symbol> &vContextSymbols, int iOffset, int iLength) {
  if (!GetFlag(NF_SEEN) && iOffset+iLength-1 == offset()) {
    if (iLength > 1) Parent()->GetContext(pInterface, pAlphabetMap, vContextSymbols, iOffset, iLength-numChars());
    vContextSymbols.push_back(iSymbol);
  } else {
    CDasherNode::GetContext(pInterface, pAlphabetMap, vContextSymbols, iOffset, iLength);
  }
}

symbol CSymbolNode::GetAlphSymbol() {
  return iSymbol;
}

void CSymbolNode::PopulateChildren() {
  m_pMgr->IterateChildGroups(this, m_pMgr->m_pBaseGroup, NULL);
}
int CAlphNode::ExpectedNumChildren() {
  return m_pMgr->m_pBaseGroup->iNumChildNodes;
}

void CAlphabetManager::GetProbs(vector<unsigned int> *pProbInfo, CLanguageModel::Context context) {
  const unsigned int iSymbols = m_pBaseGroup->iEnd-1;
  
  // TODO - sort out size of control node - for the timebeing I'll fix the control node at 5%
  // TODO: New method (see commented code) has been removed as it wasn' working.

  const unsigned long iNorm(CDasherModel::NORMALIZATION);
  //the case for control mode on, generalizes to handle control mode off also,
  // as then iNorm - control_space == iNorm...
  const unsigned int iUniformAdd = max(1ul, ((iNorm * m_pSettingsStore->GetLongParameter(LP_UNIFORM)) / 1000) / iSymbols);
  const unsigned long iNonUniformNorm = iNorm - iSymbols * iUniformAdd;
  //  m_pLanguageModel->GetProbs(context, Probs, iNorm, ((iNorm * uniform) / 1000));

  //ACL used to test explicitly for MandarinDasher and if so called GetPYProbs instead
  // (by statically casting to PPMPYLanguageModel). However, have renamed PPMPYLanguageModel::GetPYProbs
  // to GetProbs as per ordinary language model, so no need to test....
  m_pLanguageModel->GetProbs(context, *pProbInfo, iNonUniformNorm, 0);

  DASHER_ASSERT(pProbInfo->size() == iSymbols+1);//initial 0

  for(unsigned int k(1); k < pProbInfo->size(); ++k)
    (*pProbInfo)[k] += iUniformAdd;

#ifdef DEBUG
  {
    unsigned long iTotal = 0;
    for(unsigned int k = 0; k < pProbInfo->size(); ++k)
      iTotal += (*pProbInfo)[k];
    DASHER_ASSERT(iTotal == iNorm);
  }
#endif
}

std::vector<unsigned int>* CAlphNode::GetProbInfo() {
  if (!m_pProbInfo) {
    m_pProbInfo = new std::vector<unsigned int>();
    m_pMgr->GetProbs(m_pProbInfo, iContext);

    // work out cumulative probs in place
    for(unsigned int i = 1; i < m_pProbInfo->size(); i++) {
      (*m_pProbInfo)[i] += (*m_pProbInfo)[i - 1];
    }
  }
  return m_pProbInfo;
}

std::vector<unsigned int>* CGroupNode::GetProbInfo() {
  if (Parent() && Parent()->mgr() == mgr() && Parent()->offset()==offset()) {
    return (static_cast<CAlphNode *>(Parent()))->GetProbInfo();
  }
  //nope, no usable parent. compute here...
  return CAlphNode::GetProbInfo();
}

void CGroupNode::PopulateChildren() {
  m_pMgr->IterateChildGroups(this, m_pGroupInfo, NULL);
}

int CGroupNode::ExpectedNumChildren() {
  return m_pGroupInfo->iNumChildNodes;
}

CGroupNode *CAlphabetManager::CreateGroupNode(CAlphNode *pParent, const SGroupInfo *pInfo) {

  // When creating a group node...
  // ...the offset is the same as the parent...

  CGroupNode *pNewNode = new CGroupNode(pParent->offset(), m_mGroupLabels[pInfo], this, pInfo);

  //...as is the context!
  pNewNode->iContext = m_pLanguageModel->CloneContext(pParent->iContext);

  return pNewNode;
}

CDasherNode* CAlphBase::RebuildGroup(CAlphNode *pParent, const SGroupInfo *pInfo) {
  CGroupNode *pRet=m_pMgr->CreateGroupNode(pParent, pInfo);
  if (isInGroup(pInfo)) {
    //created group node should contain this one
    m_pMgr->IterateChildGroups(pRet,pInfo,this);
  }
  return pRet;
}

CDasherNode* CGroupNode::RebuildGroup(CAlphNode* pParent, const SGroupInfo* pInfo) {
  if (pInfo == m_pGroupInfo) {
    //offset doesn't increase for groups...
    DASHER_ASSERT (offset() == pParent->offset());
    return this;
  }
  return CAlphBase::RebuildGroup(pParent, pInfo);
}

bool CGroupNode::isInGroup(const SGroupInfo *pInfo) {
  return pInfo->iStart <= m_pGroupInfo->iStart && pInfo->iEnd >= m_pGroupInfo->iEnd;
}

const ColorPalette::Color& CGroupNode::getLabelColor(const ColorPalette* colorPalette)
{
    if(renderInRootColor || colorPalette == nullptr) return ColorPalette::noColor;
    const ColorPalette::Color& result = colorPalette->GetGroupLabelColor(m_pGroupInfo->colorGroup, UseAltColor());
    return result != ColorPalette::undefinedColor ? result : ColorPalette::noColor;
}

const ColorPalette::Color& CGroupNode::getOutlineColor(const ColorPalette* colorPalette)
{
    if(renderInRootColor || colorPalette == nullptr) return ColorPalette::noColor;
    const ColorPalette::Color& result = colorPalette->GetGroupOutlineColor(m_pGroupInfo->colorGroup, UseAltColor());
    return result != ColorPalette::undefinedColor ? result : ColorPalette::noColor;
}

const ColorPalette::Color& CGroupNode::getNodeColor(const ColorPalette* colorPalette)
{
    if(colorPalette == nullptr) return ColorPalette::noColor;
    if(renderInRootColor) return colorPalette->GetNamedColor(NamedColor::rootNode);
    return colorPalette->GetGroupColor(m_pGroupInfo->colorGroup, UseAltColor());
}

double CGroupNode::SpeedMul()
{
    if(Parent() != nullptr) return Parent()->SpeedMul();
    return 1.0;
}

bool CSymbolNode::isInGroup(const SGroupInfo *pInfo) {
  return (pInfo->iStart <= iSymbol && pInfo->iEnd > iSymbol);
}

double CSymbolNode::SpeedMul()
{
  const double speedMult = m_pMgr->GetAlphabet()->GetSymbolSpeedMultiplier(iSymbol);
  if(speedMult > 0) return speedMult;
  return 1.0;
}

CDasherNode *CAlphabetManager::CreateSymbolNode(CAlphNode *pParent, symbol iSymbol) {

    // TODO: Exceptions / error handling in general

    // Uniquely, a paragraph symbol can be two characters
    // (and we can't call numChars() on the symbol before we've constructed it!)
    int iNewOffset = pParent->offset()+1;
    if (m_pAlphabet->GetText(iSymbol)=="\r\n") iNewOffset++;
    CSymbolNode *pAlphNode = new CSymbolNode(iNewOffset, m_vLabels[iSymbol], this, iSymbol);
    //     std::stringstream ssLabel;

    //     ssLabel << GetLabelText(iSymbol) << ": " << pNewNode;

    //    pDisplayInfo->strDisplayText = ssLabel.str();

    pAlphNode->iContext = m_pLanguageModel->CloneContext(pParent->iContext);
    m_pLanguageModel->EnterSymbol(pAlphNode->iContext, iSymbol); // TODO: Don't use symbols?

  return pAlphNode;
}

CDasherNode* CAlphBase::RebuildSymbol(CAlphNode *pParent, symbol iSymbol) {
  return m_pMgr->CreateSymbolNode(pParent, iSymbol);
}

CDasherNode* CSymbolNode::RebuildSymbol(CAlphNode *pParent, symbol iSymbol) {
  if(iSymbol == this->iSymbol) {
    DASHER_ASSERT(offset() == pParent->offset() + numChars());
    return this;
  }
  return CAlphBase::RebuildSymbol(pParent, iSymbol);
}

const ColorPalette::Color& CSymbolNode::getLabelColor(const ColorPalette* colorPalette)
{
    if(colorPalette == nullptr) return ColorPalette::noColor;
    const ColorPalette::Color& result = colorPalette->GetNodeLabelColor(m_pMgr->GetAlphabet()->getColorGroup(iSymbol), m_pMgr->GetAlphabet()->getColorGroupOffset(iSymbol), UseAltColor());
    return result != ColorPalette::undefinedColor ? result : ColorPalette::noColor;
}

const ColorPalette::Color& CSymbolNode::getOutlineColor(const ColorPalette* colorPalette)
{
    if(colorPalette == nullptr) return ColorPalette::noColor;
    const ColorPalette::Color& result = colorPalette->GetNodeOutlineColor(m_pMgr->GetAlphabet()->getColorGroup(iSymbol), m_pMgr->GetAlphabet()->getColorGroupOffset(iSymbol), UseAltColor());
    return result != ColorPalette::undefinedColor ? result : ColorPalette::noColor;
}

const ColorPalette::Color& CSymbolNode::getNodeColor(const ColorPalette* colorPalette)
{
    if(colorPalette == nullptr) return ColorPalette::noColor;
    return colorPalette->GetNodeColor(m_pMgr->GetAlphabet()->getColorGroup(iSymbol), m_pMgr->GetAlphabet()->getColorGroupOffset(iSymbol), UseAltColor());
}

void CAlphabetManager::IterateChildGroups(CAlphNode *pParent, const SGroupInfo *pParentGroup, CAlphBase *buildAround) {
  std::vector<unsigned int> *pCProb(pParent->GetProbInfo());
  DASHER_ASSERT((*pCProb)[0] == 0);
  const int iMin(pParentGroup->iStart);
  const int iMax(pParentGroup->iEnd);
  unsigned int iRange(pParentGroup == m_pBaseGroup ? CDasherModel::NORMALIZATION : ((*pCProb)[iMax-1] - (*pCProb)[iMin-1]));

  // TODO: Think through alphabet file formats etc. to make this class easier.
  // TODO: Throw a warning if parent node already has children

  // Create child nodes and add them

  int i(iMin); //lowest index of child which we haven't yet added
  const SGroupInfo *pCurrentNode(pParentGroup->pChild);
  // The SGroupInfo structure has something like linked list behaviour
  // Each SGroupInfo contains a pNext, a pointer to a sibling group info
  while (i < iMax) {
    CDasherNode *pNewChild;
    bool bSymbol = !pCurrentNode //gone past last subgroup
                  || i < pCurrentNode->iStart; //not reached next subgroup
    const int iStart=i, iEnd = (bSymbol) ? i+1 : pCurrentNode->iEnd;
    //uint64_t is platform-dependently #defined in DasherTypes.h as an (unsigned) 64-bit int ("__int64" or "long long int")
    unsigned int iLbnd = (((*pCProb)[iStart-1] - (*pCProb)[iMin-1]) *
                          static_cast<uint64_t>(CDasherModel::NORMALIZATION)) /
                         iRange;
    unsigned int iHbnd = (((*pCProb)[iEnd-1] - (*pCProb)[iMin-1]) *
                          static_cast<uint64_t>(CDasherModel::NORMALIZATION)) /
                         iRange;
    if (bSymbol) {
      pNewChild = (buildAround) ? buildAround->RebuildSymbol(pParent, i) : CreateSymbolNode(pParent, i);
      i++; //make one symbol at a time - move onto next symbol in next iteration of (outer) loop
    } else {
      DASHER_ASSERT(pCurrentNode->iNumChildNodes > 1);
      pNewChild= (buildAround) ? buildAround->RebuildGroup(pParent, pCurrentNode) : CreateGroupNode(pParent, pCurrentNode);
      i = pCurrentNode->iEnd; //make one group at a time - so move past entire group...
      pCurrentNode = pCurrentNode->pNext; //next sibling of _original_ pCurrentNode (above)
      // (maybe not of pCurrentNode now, which might be a subgroup filling the original)
    }
    //created a new node - symbol or (group which will have >1 child).
    pNewChild->Reparent(pParent, iLbnd, iHbnd);
  }

  pParent->SetFlag(CDasherNode::NF_ALLCHILDREN, true);
}

CAlphNode::~CAlphNode() {
  delete m_pProbInfo;
  m_pMgr->m_pLanguageModel->ReleaseContext(iContext);
}

const std::string &CSymbolNode::outputText() const {
  return m_pMgr->m_pAlphabet->GetText(iSymbol);
}

string CSymbolNode::trainText() {
    return m_pMgr->m_pAlphabet->escape(outputText());
}

int CSymbolNode::numChars() {
  return (outputText()=="\r\n") ? 2 : 1;
}

void CSymbolNode::TrainSymbol()
{
    if (!m_pMgr->m_pSettingsStore->GetBoolParameter(BP_LM_ADAPTIVE)) return; // No training needed

    if (m_pMgr->m_pLastOutput != Parent()) {
      //Context changed. Flush to disk the old context + text written in it...
      m_pMgr->WriteTrainFileFull(m_pMgr->m_pInterface);

      ///Now extract the context in which this node was written.
      /// Since this node is being output now, its parent must already have been,
      /// so the simplest thing is to read from the edit buffer!
      int iStart = max(0, offset() - m_pMgr->m_pLanguageModel->GetContextLength());
      m_pMgr->strTrainfileContext = m_pMgr->m_pInterface->GetContext(iStart, offset()-iStart);
      if (m_pMgr->strTrainfileContext=="") //Even the empty context (as for a new document)
        m_pMgr->strTrainfileContext = m_pMgr->m_pAlphabet->GetDefaultContext(); //is a new ctx!
    }

    //Now handle outputting of this node
    m_pMgr->m_pLastOutput = this;
    string tr(trainText());
    m_pMgr->strTrainfileBuffer += tr;
    //an actual occurrence of the escape character, must be doubled (like \\)
    if (tr == m_pMgr->m_pAlphabet->GetContextEscapeChar()) m_pMgr->strTrainfileBuffer+=tr;
}

void CSymbolNode::UntrainSymbol()
{
    if(!GetFlag(NF_SEEN)) return;
    if (!m_pMgr->m_pSettingsStore->GetBoolParameter(BP_LM_ADAPTIVE)) CAlphBase::Undo(); // No training needed

    if (m_pMgr->m_pLastOutput == this) {
      //Erase from training buffer, and move lastOutput backwards,
      // iff this node was actually written (i.e. not rebuilt _from_ context!)
      std::string &buf(m_pMgr->strTrainfileBuffer);
      std::string tr(trainText());
      if (tr.length()<=buf.length()
          && buf.substr(buf.length()-tr.length(),tr.length())==tr) {
        buf=buf.substr(0,buf.length()-tr.length());
        m_pMgr->m_pLastOutput = Parent();
      }
    }
}

void CSymbolNode::Do() {
    TrainSymbol();
    const std::vector<Action*>& uA = m_pMgr->GetAlphabet()->GetCharDoActions(iSymbol);
    for(Action* a : uA)
    {
        a->Broadcast(GetInterface(), GetInterface()->GetActionManager(), this);
    }
}

SymbolProb CSymbolNode::GetSymbolProb() const {
  //TODO probability here not right - Range() is relative to parent, not prev symbol
  return Dasher::SymbolProb(iSymbol, outputText(), Range() / (double)CDasherModel::NORMALIZATION);
}

void CSymbolNode::Undo() {
    UntrainSymbol();
    const std::vector<Action*>& uA = m_pMgr->GetAlphabet()->GetCharUndoActions(iSymbol);
    for(Action* a : uA)
    {
        a->Broadcast(GetInterface(), GetInterface()->GetActionManager(), this);
    }
}

CDasherNode* CGroupNode::RebuildParent() {

  if (Parent()) return Parent();

  if (m_pGroupInfo == m_pMgr->m_pBaseGroup) {
    //top level root node.
    //if (offset()>0), there was _something_ before us, like
    // a control node; but we no longer know what!
    return NULL;
  }
  
  //All other CGroupNode's have a container i.e. the parent group
  return CAlphBase::RebuildParent();
}

CDasherNode* CAlphBase::RebuildParent() {
  if (!Parent()) {
    //Parent's offset usually one less than this, but can be two for the paragraph symbol.
    int iNewOffset = offset()-numChars();

    CAlphNode *pNewNode = m_pMgr->GetRoot(NULL, iNewOffset!=-1, iNewOffset+1);

    RebuildForwardsFromAncestor(pNewNode);

    if (int flags=(GetFlag(NF_SEEN) ? NF_SEEN : 0) | (GetFlag(NF_COMMITTED) ? NF_COMMITTED : 0)) {
      for (CDasherNode *pNode=this; (pNode=pNode->Parent()); pNode->SetFlag(flags, true));
    }
  }
  return Parent();
}

void CAlphBase::RebuildForwardsFromAncestor(CAlphNode *pNewNode) {
  //now fill in the new node - recursively - until it reaches us
  m_pMgr->IterateChildGroups(pNewNode, m_pMgr->m_pBaseGroup, this);
}

// TODO: Shouldn't there be an option whether or not to learn as we write?
// For want of a better solution, game mode exemption explicit in this function
void CSymbolNode::SetFlag(int iFlag, bool bValue) {
  if ((iFlag & NF_COMMITTED) && bValue && !GetFlag(NF_COMMITTED | NF_GAME)
      && m_pMgr->m_pSettingsStore->GetBoolParameter(BP_LM_ADAPTIVE)) {
    //try to commit...if we have parent (else rebuilding (backwards) => don't)
    if (Parent()) {
      if (Parent()->mgr() != mgr()) return; //do not set flag
      CLanguageModel *pLM(m_pMgr->m_pLanguageModel);
      // (Note: for first symbol after startup: parent is (root) group node, which'll have the alphabet default context)
      CLanguageModel::Context ctx = pLM->CloneContext(static_cast<CAlphNode *>(Parent())->iContext);
      pLM->LearnSymbol(ctx, iSymbol);
      //could: pLM->ReleaseContext(ctx);
      //however, seems better to replace this node's context (i.e. which it uses to create its own children)
      // with the new (learned) context: the former was obtained by EnterSymbol rather than LearnSymbol, so
      // will be different iff this node was the first time its symbol was entered into its parent context.
      // (Yes, this node's context is unlikely to be used again, but not impossible...)
      pLM->ReleaseContext(iContext);
      iContext = ctx;
    }
  }
  CDasherNode::SetFlag(iFlag, bValue);
}
