#include "molecules/mixingrules/MixingRuleBase.h"

#include "utils/Logger.h"
#include "utils/xmlfileUnits.h"

using namespace std;
using Log::global_log;

void MixingRuleBase::readXML(XMLfileUnits& xmlconfig) {
	xmlconfig.getNodeValue("@cid1", _cid1);
	xmlconfig.getNodeValue("@cid2", _cid2);
	global_log->info() << "Component id1: " << _cid1 << endl;
	global_log->info() << "Component id2: " << _cid2 << endl;
}

