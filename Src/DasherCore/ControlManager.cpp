// ControlManager.cpp
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

#include "ControlManager.h"
#include "DasherInterfaceBase.h"
#include <cstring>

using namespace Dasher;

CControlBase::CControlBase(CSettingsUser* pCreateFrom, CDasherInterfaceBase* pInterface, CNodeCreationManager* pNCManager)
	: CSettingsUser(pCreateFrom), m_pInterface(pInterface), m_pNCManager(pNCManager), m_pScreen(NULL), m_pRoot(NULL)
{
}

CControlBase::NodeTemplate* CControlBase::GetRootTemplate()
{
	return m_pRoot;
}

void CControlBase::SetRootTemplate(NodeTemplate* pRoot)
{
	if (m_pRoot || !pRoot) throw "SetRoot should only be called once, with a non-null root";
	m_pRoot = pRoot;
}

int CControlBase::getColour(NodeTemplate* pTemplate, CDasherNode* pParent)
{
	if (pTemplate->m_iColour != -1) return pTemplate->m_iColour;
	if (pParent) return (pParent->ChildCount() % 99) + 11;
	return 11;
}

CDasherNode* CControlBase::GetRoot(CDasherNode* pContext, int iOffset)
{
	if (!m_pRoot) return m_pNCManager->GetAlphabetManager()->GetRoot(pContext, false, iOffset);

	CContNode* pNewNode = new CContNode(iOffset, getColour(m_pRoot, pContext), m_pRoot, this);

	// FIXME - handle context properly

	//  pNewNode->SetContext(m_pLanguageModel->CreateEmptyContext());

	return pNewNode;
}

void CControlBase::ChangeScreen(CDasherScreen* pScreen)
{
	if (m_pScreen == pScreen) return;
	m_pScreen = pScreen;
	std::deque<NodeTemplate*> templateQueue(1, m_pRoot);
	std::set<NodeTemplate*> allTemplates(templateQueue.begin(), templateQueue.end());
	while (!templateQueue.empty())
	{
		NodeTemplate* head = templateQueue.front();
		templateQueue.pop_front();
		delete head->m_pLabel;
		head->m_pLabel = pScreen->MakeLabel(head->m_strLabel);
		for (auto child : head->successors)
		{
			if (!child) continue; //an escape back to the alphabet, no label/successors here
			if (allTemplates.find(child) == allTemplates.end())
			{
				allTemplates.insert(child);
				templateQueue.push_back(child);
			}
		}
	}
}

CControlBase::NodeTemplate::NodeTemplate(const std::string& strLabel, int iColour)
	: m_strLabel(strLabel), m_iColour(iColour), m_pLabel(NULL)
{
}

CControlBase::NodeTemplate::~NodeTemplate()
{
	delete m_pLabel;
}

CControlBase::CContNode::CContNode(int iOffset, int iColour, NodeTemplate* pTemplate, CControlBase* pMgr)
	: CDasherNode(iOffset, iColour, pTemplate->m_pLabel), m_pTemplate(pTemplate), m_pMgr(pMgr)
{
}

double CControlBase::CContNode::SpeedMul()
{
	return m_pMgr->GetBoolParameter(BP_SLOW_CONTROL_BOX) ? 0.5 : 1;
}

void CControlBase::CContNode::PopulateChildren()
{
	CDasherNode* pNewNode;

	const unsigned int iNChildren(static_cast<unsigned int>(m_pTemplate->successors.size()));
	unsigned int iLbnd(0), iIdx(0);
	int newOffset = m_pTemplate->calculateNewOffset(this, offset());

	for (auto child : m_pTemplate->successors)
	{
		const unsigned int iHbnd((++iIdx * CDasherModel::NORMALIZATION) / iNChildren);

		if (child == NULL)
		{
			// Escape back to alphabet

			pNewNode = m_pMgr->m_pNCManager->GetAlphabetManager()->GetRoot(this, false, newOffset + 1);
		}
		else
		{
			pNewNode = new CContNode(newOffset, m_pMgr->getColour(child, this), child, m_pMgr);
		}
		pNewNode->Reparent(this, iLbnd, iHbnd);
		iLbnd = iHbnd;
		DASHER_ASSERT(GetChildren().back()==pNewNode);
	}
}

int CControlBase::CContNode::ExpectedNumChildren()
{
	return static_cast<int>(m_pTemplate->successors.size());
}

void CControlBase::CContNode::Output()
{
	m_pTemplate->happen(this);
}

const std::list<CControlBase::NodeTemplate*>& CControlParser::parsedNodes()
{
	return m_vParsed;
}

///Template used for all node defns read in from XML - just
/// execute a list of Actions.
class XMLNodeTemplate : public CControlBase::NodeTemplate
{
public:
	XMLNodeTemplate(const std::string& label, int color) : NodeTemplate(label, color)
	{
	}

	int calculateNewOffset(CControlBase::CContNode* pNode, int offsetBefore) override
	{
		int newOffset = offsetBefore;
		for (auto pAction : actions)
			newOffset = pAction->calculateNewOffset(pNode, newOffset);
		return newOffset;
	}

	void happen(CControlBase::CContNode* pNode) override
	{
		for (auto pAction : actions)
			pAction->happen(pNode);
	}

	~XMLNodeTemplate()
	{
		for (auto pAction : actions)
			delete pAction;
	}

	std::vector<CControlBase::Action*> actions;
};

CControlParser::CControlParser(CMessageDisplay* pMsgs) : AbstractXMLParser(pMsgs), m_bUser(false)
{
}

void CControlParser::ParseNodeRecursive(pugi::xml_node node, std::list<CControlBase::NodeTemplate*>& parent)
{
	XMLNodeTemplate* newNode = new XMLNodeTemplate(node.attribute("label").as_string(), node.attribute("color").as_int(-1));
	parent.push_back(newNode);

	std::string nodeName = node.attribute("name").as_string();
	if (!nodeName.empty()) namedNodes[nodeName] = newNode; //all refs resolved at end.

	for(pugi::xml_node sub_node : node.children())
	{
		std::string tag_name = sub_node.name();
		if(tag_name == "node")
		{
			ParseNodeRecursive(sub_node, newNode->successors);
		}
		else if (tag_name == "ref")
		{
			std::string target = sub_node.attribute("name").as_string();
			if (auto it = namedNodes.find(target); it != namedNodes.end())
			{
				parent.push_back(it->second);
			}
			else
			{
				parent.push_back(nullptr);
				unresolvedRefs.push_back({&parent.back(), target});
			}
		}
		else if (tag_name == "alph")
		{
			parent.push_back(nullptr);
		}
		else if (CControlBase::NodeTemplate* n = parseOther(sub_node))
		{
			parent.push_back(n);
		}
		else if (CControlBase::Action* a = parseAction(sub_node))
		{
			DASHER_ASSERT(!nodeStack.empty());
			newNode->actions.push_back(a);
		}
	}
}


bool CControlParser::Parse(pugi::xml_document& document, bool bUser)
{
	if (m_bUser)
	{
		// User File Mode!
		if (!bUser) return true; // So ignore system!
	}
	else
	{
		// System File Mode
		if (bUser) m_vParsed.clear(); // If we were user mode before, clear everything that was parsed so far
		m_bUser = true;
	}

	namedNodes.clear();
	unresolvedRefs.clear();

	for(pugi::xml_node sub_node : document.children())
	{
		std::string tag_name = sub_node.name();
		if(tag_name == "node")
		{
			ParseNodeRecursive(sub_node, m_vParsed);
		}
		else if (tag_name == "alph")
		{
			m_vParsed.push_back(nullptr);
		}
	}

	//resolve any forward references to nodes declared later
	for (auto [list_space, searched_ID] : unresolvedRefs)
	{
		auto target = namedNodes.find(searched_ID);
		if (target != namedNodes.end()){
			*(list_space) = target->second;
		}
	}

	//somehow, need to clear out any refs that weren't resolved...???
	return true;
}

class Dasher::SpeechHeader : public CDasherInterfaceBase::TextAction
{
public:
	SpeechHeader(CDasherInterfaceBase* pIntf) : TextAction(pIntf)
	{
	}

	void operator()(const std::string& strText)
	{
		m_pIntf->Speak(strText, false);
	}
};

class Dasher::CopyHeader : public CDasherInterfaceBase::TextAction
{
public:
	CopyHeader(CDasherInterfaceBase* pIntf) : TextAction(pIntf)
	{
	}

	void operator()(const std::string& strText)
	{
		m_pIntf->CopyToClipboard(strText);
	}
};

class TextDistanceAction : public CControlBase::Action
{
	CDasherInterfaceBase::TextAction* m_header;
	CControlManager::EditDistance m_dist;
public:
	TextDistanceAction(CDasherInterfaceBase::TextAction* header, CControlManager::EditDistance dist)
		: m_header(header), m_dist(dist)
	{
	}

	virtual void happen(CControlBase::CContNode* pNode)
	{
		m_header->executeOnDistance(m_dist);
	}
};

// TODO Duplicated logic, here and in DefaultFilter. Refactor. 
class Stop : public CControlBase::Action
{
public:
	void happen(CControlBase::CContNode* pNode)
	{
		pNode->mgr()->GetDasherInterface()->Done();
		pNode->mgr()->GetDasherInterface()->GetActiveInputMethod()->pause();
	}
};

class Pause : public CControlBase::Action
{
public:
	void happen(CControlBase::CContNode* pNode)
	{
		pNode->mgr()->GetDasherInterface()->GetActiveInputMethod()->pause();
	}
};

class SpeakCancel : public CControlBase::Action
{
public:
	void happen(CControlBase::CContNode* pNode)
	{
		pNode->mgr()->GetDasherInterface()->Speak("", true);
	}
};

template <typename T>
class MethodAction : public CControlBase::Action
{
public:
	///A "Method" is pointer to a function "void X()", that is a member of a T...
	typedef void (T::*Method)();

	MethodAction(T* pRecv, Method f) : m_pRecv(pRecv), m_f(f)
	{
	}

	virtual void happen(CControlBase::CContNode* pNode)
	{
		//invoke pointer-to-member-function m_f on object *m_pRecv!
		(m_pRecv->*m_f)();
	}

private:
	T* m_pRecv;
	Method m_f;
};

class Delete : public CControlBase::Action
{
	const bool m_bForwards;
	const CControlManager::EditDistance m_dist;
public:
	Delete(bool bForwards, CControlManager::EditDistance dist) : m_bForwards(bForwards), m_dist(dist)
	{
	}

	int calculateNewOffset(CControlBase::CContNode* pNode, int offsetBefore) override
	{
		if (m_bForwards)
			return offsetBefore;

		return pNode->mgr()->GetDasherInterface()->ctrlOffsetAfterMove(offsetBefore + 1, m_bForwards, m_dist) - 1;
	}

	virtual void happen(CControlBase::CContNode* pNode) override
	{
		pNode->mgr()->GetDasherInterface()->ctrlDelete(m_bForwards, m_dist);
	}
};

class Move : public CControlBase::Action
{
	const bool m_bForwards;
	const CControlManager::EditDistance m_dist;
public:
	Move(bool bForwards, CControlManager::EditDistance dist) : m_bForwards(bForwards), m_dist(dist)
	{
	}

	int calculateNewOffset(CControlBase::CContNode* pNode, int offsetBefore) override
	{
		return pNode->mgr()->GetDasherInterface()->ctrlOffsetAfterMove(offsetBefore + 1, m_bForwards, m_dist) - 1;
	}

	virtual void happen(CControlBase::CContNode* pNode)
	{
		pNode->mgr()->GetDasherInterface()->ctrlMove(m_bForwards, m_dist);
	}
};


CControlManager::CControlManager(CSettingsUser* pCreateFrom, CNodeCreationManager* pNCManager, CDasherInterfaceBase* pInterface)
	: CSettingsObserver(pCreateFrom), CControlBase(pCreateFrom, pInterface, pNCManager), CControlParser(pInterface), m_pSpeech(NULL), m_pCopy(NULL)
{
	//TODO, used to be able to change label+colour of root/pause/stop from controllabels.xml
	// (or, get the root node title "control" from the alphabet!)
	m_pSpeech = new SpeechHeader(pInterface);
	m_pCopy = new CopyHeader(pInterface);
	SetRootTemplate(new NodeTemplate("", 8)); //default NodeTemplate does nothing

	// Key in actions map is name plus arguments in alphabetical order.
	m_actions["stop"] = new Stop();
	m_actions["pause"] = new Pause();
	if (pInterface->SupportsSpeech())
	{
		m_actions["speak what=all"] = new TextDistanceAction(m_pSpeech, EDIT_FILE);
		m_actions["speak what=page"] = new TextDistanceAction(m_pSpeech, EDIT_PAGE);
		m_actions["speak what=paragraph"] = new TextDistanceAction(m_pSpeech, EDIT_PARAGRAPH);
		m_actions["speak what=sentence"] = new TextDistanceAction(m_pSpeech, EDIT_SENTENCE);
		m_actions["speak what=line"] = new TextDistanceAction(m_pSpeech, EDIT_LINE);
		m_actions["speak what=word"] = new TextDistanceAction(m_pSpeech, EDIT_WORD);
		m_actions["speak what=new"] = new MethodAction<SpeechHeader>(m_pSpeech, &SpeechHeader::executeOnNew);
		m_actions["speak what=repeat"] = new MethodAction<SpeechHeader>(m_pSpeech, &SpeechHeader::executeLast);
		m_actions["speak what=cancel"] = new SpeakCancel();
	}
	if (pInterface->SupportsClipboard())
	{
		m_actions["copy what=all"] = new TextDistanceAction(m_pCopy, EDIT_FILE);
		m_actions["copy what=page"] = new TextDistanceAction(m_pCopy, EDIT_PAGE);
		m_actions["copy what=paragraph"] = new TextDistanceAction(m_pCopy, EDIT_PARAGRAPH);
		m_actions["copy what=sentence"] = new TextDistanceAction(m_pCopy, EDIT_SENTENCE);
		m_actions["copy what=line"] = new TextDistanceAction(m_pCopy, EDIT_LINE);
		m_actions["copy what=word"] = new TextDistanceAction(m_pCopy, EDIT_WORD);
		m_actions["copy what=new"] = new MethodAction<CopyHeader>(m_pCopy, &CopyHeader::executeOnNew);
		m_actions["copy what=repeat"] = new MethodAction<CopyHeader>(m_pCopy, &CopyHeader::executeLast);
	}
	m_actions["move dist=char forward=yes"] = new Move(true, EDIT_CHAR);
	m_actions["move dist=word forward=yes"] = new Move(true, EDIT_WORD);
	m_actions["move dist=line forward=yes"] = new Move(true, EDIT_LINE);
	m_actions["move dist=sentence forward=yes"] = new Move(true, EDIT_SENTENCE);
	m_actions["move dist=paragraph forward=yes"] = new Move(true, EDIT_PARAGRAPH);
	m_actions["move dist=page forward=yes"] = new Move(true, EDIT_PAGE);
	m_actions["move dist=all forward=yes"] = new Move(true, EDIT_FILE);

	m_actions["move dist=char forward=no"] = new Move(false, EDIT_CHAR);
	m_actions["move dist=word forward=no"] = new Move(false, EDIT_WORD);
	m_actions["move dist=line forward=no"] = new Move(false, EDIT_LINE);
	m_actions["move dist=sentence forward=no"] = new Move(false, EDIT_SENTENCE);
	m_actions["move dist=paragraph forward=no"] = new Move(false, EDIT_PARAGRAPH);
	m_actions["move dist=page forward=no"] = new Move(false, EDIT_PAGE);
	m_actions["move dist=all forward=no"] = new Move(false, EDIT_FILE);

	m_actions["delete dist=char forward=yes"] = new Delete(true, EDIT_CHAR);
	m_actions["delete dist=word forward=yes"] = new Delete(true, EDIT_WORD);
	m_actions["delete dist=line forward=yes"] = new Delete(true, EDIT_LINE);
	m_actions["delete dist=sentence forward=yes"] = new Delete(true, EDIT_SENTENCE);
	m_actions["delete dist=paragraph forward=yes"] = new Delete(true, EDIT_PARAGRAPH);
	m_actions["delete dist=page forward=yes"] = new Delete(true, EDIT_PAGE);
	m_actions["delete dist=all forward=yes"] = new Delete(true, EDIT_FILE);

	m_actions["delete dist=char forward=no"] = new Delete(false, EDIT_CHAR);
	m_actions["delete dist=word forward=no"] = new Delete(false, EDIT_WORD);
	m_actions["delete dist=line forward=no"] = new Delete(false, EDIT_LINE);
	m_actions["delete dist=sentence forward=no"] = new Delete(false, EDIT_SENTENCE);
	m_actions["delete dist=paragraph forward=no"] = new Delete(false, EDIT_PARAGRAPH);
	m_actions["delete dist=page forward=no"] = new Delete(false, EDIT_PAGE);
	m_actions["delete dist=all forward=no"] = new Delete(false, EDIT_FILE);
}

CControlBase::NodeTemplate* CControlManager::parseOther(pugi::xml_node node)
{
	if (strcmp(node.name(), "root") == 0) return GetRootTemplate();
	return CControlParser::parseOther(node);
}

CControlBase::Action* CControlManager::parseAction(pugi::xml_node node)
{
	std::map<std::string, std::string> arguments;
	for(auto attribute : node.attributes())
	{
		arguments[attribute.name()] = attribute.as_string();
	}

	std::stringstream key;
	// Key in actions map is name plus arguments in alphabetical order.
	key << node.name();
	if (!arguments.empty())
	{
		for (auto arg : arguments)
		{
			key << " " << arg.first << "=" << arg.second;
		}
	}

	if (auto it = m_actions.find(key.str()); it != m_actions.end()){
		return it->second;
	}

	return CControlParser::parseAction(node);
}

CControlManager::~CControlManager()
{
}

void CControlManager::HandleEvent(Parameter parameter)
{
	switch (parameter)
	{
	case BP_COPY_ALL_ON_STOP:
	case BP_SPEAK_ALL_ON_STOP:
	case SP_INPUT_FILTER:
		updateActions();
	}
}

void CControlManager::updateActions()
{
	// decide if removal of pause and stop are worth the trouble 
	// reimplement if yes 
	// imo with control.xml it isn't.
	GetRootTemplate()->successors.clear();

	for (auto pNode : parsedNodes())
		GetRootTemplate()->successors.push_back(pNode);

	// If nothing was read from control.xml, add alphabet and control box
	if (GetRootTemplate()->successors.empty())
	{
		m_pMsgs->Message("Control box is empty.", false);
		GetRootTemplate()->successors.push_back(NULL);
		GetRootTemplate()->successors.push_back(GetRootTemplate());
	}

	if (CDasherScreen* pScreen = m_pScreen)
	{
		//hack to make ChangeScreen do something
		m_pScreen = NULL; //i.e. make it think the screen has changed
		ChangeScreen(pScreen);
	}
}

CControlBoxIO::CControlBoxIO(CMessageDisplay* pMsgs) : AbstractXMLParser(pMsgs)
{
}

CControlManager* CControlBoxIO::CreateControlManager(
	const std::string& id, CSettingsUser* pCreateFrom, CNodeCreationManager* pNCManager,
	CDasherInterfaceBase* pInterface) const
{
	auto mgr = new CControlManager(pCreateFrom, pNCManager, pInterface);
	auto it = m_controlFiles.find(id);
	if (it != m_controlFiles.end())
		mgr->ParseFile(it->second, true);
	mgr->updateActions();
	return mgr;
}


void CControlBoxIO::GetControlBoxes(std::vector<std::string>* pList) const
{
	for (auto id_filename : m_controlFiles)
		pList->push_back(id_filename.first);
}

bool CControlBoxIO::Parse(pugi::xml_document& document, bool bUser)
{
	std::string name = document.child("control").attribute("name").as_string();

	if (!bUser && m_controlFiles.count(name)) return true; // Ignore system files if that name already taken

	m_controlFiles[name] = GetDesc();

	return true;
}
