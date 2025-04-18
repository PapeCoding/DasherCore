#include "DasherNode.h"
#include "DasherInterfaceBase.h"
#include "NodeCreationManager.h"

#include <I18n.h>

#include "MandarinAlphMgr.h"
#include "RoutingAlphMgr.h"

#include <string>

using namespace Dasher;

//Wraps the ParseFile of a provided Trainer, to setup progress notification
// - and then passes self, as a ProgressIndicator, to the Trainer's ParseFile method.
class ProgressNotifier : public AbstractParser, private CTrainer::ProgressIndicator
{
public:
	ProgressNotifier(CDasherInterfaceBase* pInterface, CTrainer* pTrainer)
		: AbstractParser(pInterface), m_bSystem(false), m_bUser(false), m_pInterface(pInterface), m_pTrainer(pTrainer)
	{
	}

	// update lock status with percent
	void bytesRead(off_t n)
	{
		const int iNewPercent = n / m_file_length * 100;
		if (iNewPercent != m_iPercent)
		{
			m_iPercent = iNewPercent;
			m_pInterface->SetLockStatus(m_strDisplay, m_iPercent);
		}
	}

	bool ParseFile(const std::string& strFilename, bool bUser)
	{
		m_file_length = m_pInterface->GetFileSize(strFilename);
		if (m_file_length == 0) return false;
		return AbstractParser::ParseFile(strFilename, bUser);
	}

	bool Parse(const std::string& strUrl, std::istream& in, bool bUser)
	{
		m_strDisplay = bUser ? "Training on User Text" : "Training on System Text";
		m_iPercent = 0;
 		m_bUser = m_bSystem = false; // Indication for 'file was neither parsed from user nor from system dir'
		m_pInterface->SetLockStatus(m_strDisplay, m_iPercent);
		m_pTrainer->SetProgressIndicator(this);

		if (m_pTrainer->Parse(strUrl, in, bUser)){
			m_bUser |= bUser;
			m_bSystem |= !bUser;

			m_pInterface->SetLockStatus("", -1); //Done, so unlock
			return true;
		}

		m_pInterface->SetLockStatus("", -1); //Unlock
		return false;
	}

	bool has_parsed_from_user_dir(){return m_bUser;}
	bool has_parsed_from_system_dir(){return m_bSystem;}

private:
	bool m_bSystem, m_bUser;
	CDasherInterfaceBase* m_pInterface;
	CTrainer* m_pTrainer;
	off_t m_file_length = 0;
	int m_iPercent = 0;
	std::string m_strDisplay;
};

CNodeCreationManager::CNodeCreationManager(
	CSettingsStore* pSettingsStore,
	CDasherInterfaceBase* pInterface,
	const CAlphIO* pAlphIO
): m_pInterface(pInterface), m_pScreen(nullptr), m_pSettingsStore(pSettingsStore)
{
	m_pSettingsStore->OnParameterChanged.Subscribe(this, [this](const Parameter p)
    {
        HandleParameterChange(p);
    });


	const Dasher::CAlphInfo* pAlphInfo(pAlphIO->GetInfo(m_pSettingsStore->GetStringParameter(SP_ALPHABET_ID)));

	switch (pAlphInfo->m_iConversionID)
	{
	case CAlphInfo::None: // No conversion required
		m_pAlphabetManager = new CAlphabetManager(m_pSettingsStore, pInterface, this, pAlphInfo);
		break;
	case CAlphInfo::Mandarin:
		//Mandarin Dasher!
		//(ACL) Modify AlphabetManager for Mandarin Dasher
		m_pAlphabetManager = new CMandarinAlphMgr(m_pSettingsStore, pInterface, this, pAlphInfo);
		break;
	case CAlphInfo::RoutingContextInsensitive: //these differ only in that conversion id 3 assumes the route by which
	case CAlphInfo::RoutingContextSensitive: //the user writes a symbol, is not dependent on context (e.g. just user preference),
		//whereas 4 assumes it does depend on context (e.g. phonetic chinese)
		m_pAlphabetManager = new CRoutingAlphMgr(m_pSettingsStore, pInterface, this, pAlphInfo);
		break;
	//TODO: we could even just switch from standard alphmgr, to case 3, automatically
	// if the alphabet has repeated symbols; and thus do away with much of the "conversionid"
	// tag (just a flag for context-sensitivity, and maybe the start/stop delimiters?)
	}
	//all other configuration changes, etc., that might be necessary for a particular conversion mode,
	// are implemented by AlphabetManager subclasses overriding the following two methods:
	m_pAlphabetManager->Setup();
	m_pTrainer = m_pAlphabetManager->GetTrainer();

	if (!pAlphInfo->GetTrainingFile().empty())
	{
		ProgressNotifier pn(pInterface, m_pTrainer);
		pInterface->ScanFiles(&pn, pAlphInfo->GetTrainingFile());
		if (!pn.has_parsed_from_user_dir())
		{
			///TRANSLATORS: These 3 messages will be displayed when the user has just chosen a new alphabet. The %s parameter will be the name of the alphabet.
			if(pn.has_parsed_from_system_dir())
			{
				pInterface->FormatMessage("No user training text found - if you have written in \"%s\" before, this means Dasher may not be learning from previous sessions", pAlphInfo->GetID().c_str());
			}
			else
			{
				pInterface->FormatMessage("No training text (user or system) found for \"%s\". Dasher will still work but entry will be slower. We suggest downloading a training text file from the Dasher website, or constructing your own.", pAlphInfo->GetID().c_str());
			}
		}
	}
	else
	{
		pInterface->FormatMessage("\"%s\" does not specify training file. Dasher will work but entry will be slower. Check you have the latest version of the alphabet definition.", pAlphInfo->GetID().c_str());
	}

	HandleParameterChange(LP_ORIENTATION);
}

CNodeCreationManager::~CNodeCreationManager()
{
	delete m_pAlphabetManager;
	delete m_pTrainer;

	m_pSettingsStore->OnParameterChanged.Unsubscribe(this);
}

void CNodeCreationManager::ChangeScreen(CDasherScreen* pScreen)
{
	if (m_pScreen == pScreen) return;
	m_pScreen = pScreen;
	m_pAlphabetManager->MakeLabels(pScreen);
}

void CNodeCreationManager::ImportTrainingText(const std::string& strPath)
{
	ProgressNotifier pn(m_pInterface, m_pTrainer);
	pn.ParseFile(strPath, true);
}