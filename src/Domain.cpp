
#include <iostream>
#include <string>
#include <cmath>

#include "Domain.h"

#include "particleContainer/ParticleContainer.h"
#include "parallel/DomainDecompBase.h"
#include "molecules/Molecule.h"
#include "ensemble/GrandCanonical.h"
#include "ensemble/PressureGradient.h"
#include "CutoffCorrections.h"
#include "Simulation.h"
#include "ensemble/EnsembleBase.h"

#include "utils/Logger.h"
using Log::global_log;

using namespace std;


Domain::Domain(int rank, PressureGradient* pg){
	_localRank = rank;
	_localUpot = 0;
	_localVirial = 0;   
	_globalUpot = 0;
	_globalVirial = 0; 
	_globalRho = 0;

	this->_universalPG = pg;

	this->_componentToThermostatIdMap = map<int, int>();
	this->_localThermostatN = map<int, unsigned long>();
	this->_localThermostatN[-1] = 0;
	this->_localThermostatN[0] = 0;
	this->_universalThermostatN = map<int, unsigned long>();
	this->_universalThermostatN[-1] = 0;
	this->_universalThermostatN[0] = 0;
	this->_localRotationalDOF = map<int, unsigned long>();
	this->_localRotationalDOF[-1] = 0;
	this->_localRotationalDOF[0] = 0;
	this->_universalRotationalDOF = map<int, unsigned long>();
	this->_universalRotationalDOF[-1] = 0;
	this->_universalRotationalDOF[0] = 0;
	this->_globalLength[0] = 0;
	this->_globalLength[1] = 0;
	this->_globalLength[2] = 0;
	this->_universalBTrans = map<int, double>();
	this->_universalBTrans[0] = 1.0;
	this->_universalBRot = map<int, double>();
	this->_universalBRot[0] = 1.0;
	this->_universalTargetTemperature = map<int, double>();
	this->_universalTargetTemperature[0] = 1.0;
	this->_globalTemperatureMap = map<int, double>();
	this->_globalTemperatureMap[0] = 1.0;
	this->_local2KETrans[0] = 0.0;
	this->_local2KERot[0] = 0.0; 

	this->_universalNVE = false;
	this->_globalUSteps = 0;
	this->_globalSigmaU = 0.0;
	this->_globalSigmaUU = 0.0;
#ifdef COMPLEX_POTENTIAL_SET
	this->_universalConstantAccelerationTimesteps = 30;
	if(!rank)
		for(int d = 0; d < 3; d++)
			this->_globalVelocitySum[d] = map<unsigned, long double>();
#endif
	this->_componentwiseThermostat = false;
#ifdef COMPLEX_POTENTIAL_SET
	this->_universalUndirectedThermostat = map<int, bool>();
	for(int d = 0; d < 3; d++)
	{
		this->_universalThermostatDirectedVelocity[d] = map<int, double>();
		this->_localThermostatDirectedVelocity[d] = map<int, double>();
	}
#endif
	this->_universalSelectiveThermostatCounter = 0;
	this->_universalSelectiveThermostatWarning = 0;
	this->_universalSelectiveThermostatError = 0;
}

void Domain::readXML(XMLfileUnits& xmlconfig) {
	string originalpath = xmlconfig.getcurrentnodepath();

	/* volume */
	if ( xmlconfig.changecurrentnode( "volume" )) {
		std::string type;
		xmlconfig.getNodeValue( "@type", type );
		global_log->info() << "Volume type: " << type << endl;
		if( type == "box" ) {
			xmlconfig.getNodeValueReduced( "lx", _globalLength[0] );
			xmlconfig.getNodeValueReduced( "ly", _globalLength[1] );
			xmlconfig.getNodeValueReduced( "lz", _globalLength[2] );
			global_log->info() << "Box size: " << _globalLength[0] << ", "
				<< _globalLength[1] << ", "
				<< _globalLength[2] << endl;
		}
		else {
			global_log->error() << "Unsupported volume type " << type << endl;
		}
	}
	xmlconfig.changecurrentnode(originalpath);

	/* temperature */
	double temperature = 0.;
	xmlconfig.getNodeValueReduced("temperature", temperature);
	setGlobalTemperature(temperature);
	global_log->info() << "Temperature: " << temperature << endl;
	xmlconfig.changecurrentnode(originalpath);

}

void Domain::setLocalUpot(double Upot) {_localUpot = Upot;}

double Domain::getLocalUpot() const {return _localUpot; }

void Domain::setLocalVirial(double Virial) {_localVirial = Virial;}

double Domain::getLocalVirial() const {return _localVirial; }

/* methods accessing thermostat info */
double Domain::getGlobalBetaTrans() { return _universalBTrans[0]; }
double Domain::getGlobalBetaTrans(int thermostat) { return _universalBTrans[thermostat]; }
double Domain::getGlobalBetaRot() { return _universalBRot[0]; }
double Domain::getGlobalBetaRot(int thermostat) { return _universalBRot[thermostat]; }

void Domain::setLocalSummv2(double summv2, int thermostat)
{
#ifndef NDEBUG
	global_log->debug() << "* local thermostat " << thermostat << ":  mvv = " << summv2 << endl;
#endif
	this->_local2KETrans[thermostat] = summv2;
}

void Domain::setLocalSumIw2(double sumIw2, int thermostat)
{
	_local2KERot[thermostat] = sumIw2;
} 

double Domain::getGlobalPressure()
{
	double globalTemperature = _globalTemperatureMap[0];
	return globalTemperature * _globalRho + _globalRho * getAverageGlobalVirial()/3.;
}

double Domain::getAverageGlobalVirial() const { return _globalVirial/_globalNumMolecules; }

double Domain::getAverageGlobalUpot() const { return _globalUpot/_globalNumMolecules; }






Comp2Param& Domain::getComp2Params(){
	return _comp2params; 
}

void Domain::calculateGlobalValues(
		DomainDecompBase* domainDecomp,
		ParticleContainer* particleContainer,
		bool collectThermostatVelocities,
		double Tfactor
		) {
	double Upot = _localUpot;
	double Virial = _localVirial;

	// To calculate Upot, Ukin and Pressure, intermediate values from all      
	// processes are needed. Here the         
	// intermediate values of all processes are summed up so that the root    
	// process can calculate the final values. to be able to calculate all     
	// values at this point, the calculation of the intermediate value sum_v2  
	// had to be moved from Thermostat to upd_postF and the final calculations  
	// of m_Ukin, m_Upot and Pressure had to be moved from Thermostat / upd_F  
	// to this point           
	
	/* FIXME stuff for the ensemble class */
	domainDecomp->collCommInit(2);
	domainDecomp->collCommAppendDouble(Upot);
	domainDecomp->collCommAppendDouble(Virial);
	domainDecomp->collCommAllreduceSum();
	Upot = domainDecomp->collCommGetDouble();
	Virial = domainDecomp->collCommGetDouble();
	domainDecomp->collCommFinalize();

	/* FIXME: why should process 0 do this alone? 
	 * we should keep symmetry of all proccesses! */
	// Process 0 has to add the dipole correction:
	// m_UpotCorr and m_VirialCorr already contain constant (internal) dipole correction
	_globalUpot = Upot + _UpotCorr;
	_globalVirial = Virial + _VirialCorr;

	/*
	 * thermostat ID 0 represents the entire system
	 */

	map<int, unsigned long>::iterator thermit;
	if( _componentwiseThermostat )
	{
#ifndef NDEBUG
		global_log->debug() << "* applying a componentwise thermostat" << endl;
#endif
		this->_localThermostatN[0] = 0;
		this->_localRotationalDOF[0] = 0;
		this->_local2KETrans[0] = 0;
		this->_local2KERot[0] = 0;
		for(thermit = _localThermostatN.begin(); thermit != _localThermostatN.end(); thermit++)
		{
			if(thermit->first == 0) continue;
			this->_localThermostatN[0] += thermit->second;
			this->_localRotationalDOF[0] += this->_localRotationalDOF[thermit->first];
			this->_local2KETrans[0] += this->_local2KETrans[thermit->first];
			this->_local2KERot[0] += this->_local2KERot[thermit->first];
		}
	}

	for(thermit = _universalThermostatN.begin(); thermit != _universalThermostatN.end(); thermit++)
	{
		// number of molecules on the local process. After the reduce operation
		// num_molecules will contain the global number of molecules
		unsigned long numMolecules = _localThermostatN[thermit->first];
		double summv2 = _local2KETrans[thermit->first];
		unsigned long rotDOF = _localRotationalDOF[thermit->first];
		double sumIw2 = (rotDOF > 0)? _local2KERot[thermit->first]: 0.0;

		domainDecomp->collCommInit(4);
		domainDecomp->collCommAppendDouble(summv2);
		domainDecomp->collCommAppendDouble(sumIw2);
		domainDecomp->collCommAppendUnsLong(numMolecules);
		domainDecomp->collCommAppendUnsLong(rotDOF);
		domainDecomp->collCommAllreduceSum();
		summv2 = domainDecomp->collCommGetDouble();
		sumIw2 = domainDecomp->collCommGetDouble();
		numMolecules = domainDecomp->collCommGetUnsLong();
		rotDOF = domainDecomp->collCommGetUnsLong();
		domainDecomp->collCommFinalize();
		global_log->debug() << "[ thermostat ID " << thermit->first << "]\tN = " << numMolecules << "\trotDOF = " << rotDOF 
			<< "\tmv2 = " <<  summv2 << "\tIw2 = " << sumIw2 << endl;

		this->_universalThermostatN[thermit->first] = numMolecules;
		this->_universalRotationalDOF[thermit->first] = rotDOF;
		assert((summv2 > 0.0) || (numMolecules == 0));

		/* calculate the temperature of the entire system */
		if(numMolecules > 0)
			_globalTemperatureMap[thermit->first] =
				(summv2 + sumIw2) / (double)(3*numMolecules + rotDOF);
		else
			_globalTemperatureMap[thermit->first] = _universalTargetTemperature[thermit->first];

		double Ti = Tfactor * _universalTargetTemperature[thermit->first];
		if((Ti > 0.0) && (numMolecules > 0) && !_universalNVE)
		{
			_universalBTrans[thermit->first] = pow(3.0*numMolecules*Ti / summv2, 0.4);
			if( sumIw2 == 0.0 ) 
				_universalBRot[thermit->first] = 1.0;
			else 
				_universalBRot[thermit->first] = pow(rotDOF*Ti / sumIw2, 0.4);
		}
		else
		{
			this->_universalBTrans[thermit->first] = 1.0;
			this->_universalBRot[thermit->first] = 1.0;
		}

		// heuristic handling of the unfortunate special case of an explosion in the system
		if( ( (_universalBTrans[thermit->first] < MIN_BETA) || (_universalBRot[thermit->first] < MIN_BETA) )
				&& (0 >= _universalSelectiveThermostatError) )
		{
			global_log->warning() << "Explosion!" << endl;
			global_log->debug() << "Selective thermostat will be applied to set " << thermit->first
				<< " (beta_trans = " << this->_universalBTrans[thermit->first]
				<< ", beta_rot = " << this->_universalBRot[thermit->first] << "!)" << endl;
			int rot_dof;
			double Utrans, Urot;
			double limit_energy =  KINLIMIT_PER_T * Ti;
			double limit_rot_energy;
			double vcorr, Dcorr;
			Molecule* tM;
			for( tM = particleContainer->begin();
					tM != particleContainer->end();
					tM = particleContainer->next() )
			{
				Utrans = tM->U_trans();
				if(Utrans > limit_energy)
				{
					vcorr = sqrt(limit_energy / Utrans);
					global_log->debug() << ": v(m" << tM->id() << ") *= " << vcorr << endl;
					tM->scale_v(vcorr);
					tM->scale_F(vcorr);
				}

				rot_dof = tM->component()->getRotationalDegreesOfFreedom();
				if(rot_dof > 0)
				{
					limit_rot_energy = 3.0*rot_dof * Ti;
					Urot = tM->U_rot();
					if(Urot > limit_rot_energy)
					{
						Dcorr = sqrt(limit_rot_energy / Urot);
						global_log->debug() << "D(m" << tM->id() << ") *= " << Dcorr << endl;
						tM->scale_D(Dcorr);
						tM->scale_M(Dcorr);
					}
				}
			}

			/* FIXME: Unnamed constant 3960... */
			if(3960 >= _universalSelectiveThermostatCounter)
			{
				if( _universalSelectiveThermostatWarning > 0 )
					_universalSelectiveThermostatError = _universalSelectiveThermostatWarning;
				if( _universalSelectiveThermostatCounter > 0 )
					_universalSelectiveThermostatWarning = _universalSelectiveThermostatCounter;
				_universalSelectiveThermostatCounter = 4000;
			}
			_universalBTrans[thermit->first] = 1.0;
			_universalBRot[thermit->first] = pow(this->_universalBRot[thermit->first], 0.0091);
		}
#ifdef NDEBUG
		if( (_universalSelectiveThermostatCounter > 0) &&
				((_universalSelectiveThermostatCounter % 20) == 10) )
#endif
			/* FIXME: why difference counters? */
			global_log->debug() << "counter " << _universalSelectiveThermostatCounter
				<< ",\t warning " << _universalSelectiveThermostatWarning
				<< ",\t error " << _universalSelectiveThermostatError << endl;

		if(collectThermostatVelocities && _universalUndirectedThermostat[thermit->first])
		{
			double sigv[3];
			for(int d=0; d < 3; d++)
				sigv[d] = _localThermostatDirectedVelocity[d][thermit->first];

			domainDecomp->collCommInit(3);
			for(int d=0; d < 3; d++) domainDecomp->collCommAppendDouble(sigv[d]);
			domainDecomp->collCommAllreduceSum();
			for(int d=0; d < 3; d++) sigv[d] = domainDecomp->collCommGetDouble();
			domainDecomp->collCommFinalize();

			for(int d=0; d < 3; d++)
			{
				_localThermostatDirectedVelocity[d][thermit->first] = 0.0;
				if(numMolecules > 0)
					_universalThermostatDirectedVelocity[d][thermit->first] = sigv[d] / numMolecules;
				else 
					_universalThermostatDirectedVelocity[d][thermit->first] = 0.0;
			}

#ifndef NDEBUG
			global_log->debug() << "* thermostat " << thermit->first
				<< " directed velocity: ("
				<< _universalThermostatDirectedVelocity[0][thermit->first]
				<< " / " << _universalThermostatDirectedVelocity[1][thermit->first]
				<< " / " << _universalThermostatDirectedVelocity[2][thermit->first] 
				<< ")" << endl;
#endif
		}

#ifndef NDEBUG
		global_log->debug() << "* Th" << thermit->first << " N=" << numMolecules
			<< " DOF=" << rotDOF + 3.0*numMolecules
			<< " Tcur=" << _globalTemperatureMap[thermit->first]
			<< " Ttar=" << _universalTargetTemperature[thermit->first]
			<< " Tfactor=" << Tfactor
			<< " bt=" << _universalBTrans[thermit->first]
			<< " br=" << _universalBRot[thermit->first] << "\n";
#endif
	}

	if(this->_universalSelectiveThermostatCounter > 0)
		this->_universalSelectiveThermostatCounter--;
	if(this->_universalSelectiveThermostatWarning > 0)
		this->_universalSelectiveThermostatWarning--;
	if(this->_universalSelectiveThermostatError > 0)
		this->_universalSelectiveThermostatError--;
}

void Domain::calculateThermostatDirectedVelocity(ParticleContainer* partCont)
{
	Molecule* tM;
	if(this->_componentwiseThermostat)
	{
		for( map<int, bool>::iterator thit = _universalUndirectedThermostat.begin();
				thit != _universalUndirectedThermostat.end();
				thit ++ )
		{
			if(thit->second)
				for(int d=0; d < 3; d++) _localThermostatDirectedVelocity[d][thit->first] = 0.0;
		}
		for(tM = partCont->begin(); tM != partCont->end(); tM = partCont->next() )
		{
			int cid = tM->componentid();
			int thermostat = this->_componentToThermostatIdMap[cid];
			if(this->_universalUndirectedThermostat[thermostat])
			{
				for(int d=0; d < 3; d++)
					_localThermostatDirectedVelocity[d][thermostat] += tM->v(d);
			}
		}
	}
	else if(this->_universalUndirectedThermostat[0])
	{
		for(int d=0; d < 3; d++) _localThermostatDirectedVelocity[d][0] = 0.0;
		for(tM = partCont->begin(); tM != partCont->end(); tM = partCont->next() )
		{
			for(int d=0; d < 3; d++)
				_localThermostatDirectedVelocity[d][0] += tM->v(d);
		}
	}
}

void Domain::calculateVelocitySums(ParticleContainer* partCont)
{
	Molecule* tM;
	if(this->_componentwiseThermostat)
	{
		for(tM = partCont->begin(); tM != partCont->end(); tM = partCont->next() )
		{
			int cid = tM->componentid();
			int thermostat = this->_componentToThermostatIdMap[cid];
			this->_localThermostatN[thermostat]++;
			this->_localRotationalDOF[thermostat] += tM->component()->getRotationalDegreesOfFreedom();
			if(this->_universalUndirectedThermostat[thermostat])
			{
				tM->calculate_mv2_Iw2( this->_local2KETrans[thermostat],
						this->_local2KERot[thermostat],
						this->_universalThermostatDirectedVelocity[0][thermostat],
						this->_universalThermostatDirectedVelocity[1][thermostat],
						this->_universalThermostatDirectedVelocity[2][thermostat]  );
			}
			else
			{
				tM->calculate_mv2_Iw2(_local2KETrans[thermostat], _local2KERot[thermostat]);
			}
		}
	}
	else
	{
		for(tM = partCont->begin(); tM != partCont->end(); tM = partCont->next() )
		{
			this->_localThermostatN[0]++;
			this->_localRotationalDOF[0] += tM->component()->getRotationalDegreesOfFreedom();
			if(this->_universalUndirectedThermostat[0])
			{
				tM->calculate_mv2_Iw2( this->_local2KETrans[0],
						this->_local2KERot[0],
						this->_universalThermostatDirectedVelocity[0][0],
						this->_universalThermostatDirectedVelocity[1][0],
						this->_universalThermostatDirectedVelocity[2][0]  );
			}
			else
			{
				tM->calculate_mv2_Iw2(_local2KETrans[0], _local2KERot[0]);
			}
		}
		global_log->debug() << "      * N = " << this->_localThermostatN[0]
			<< "rotDOF = " << this->_localRotationalDOF[0] << "   mv2 = "
			<< _local2KETrans[0] << " Iw2 = " << _local2KERot[0] << endl;
	}
}

void Domain::writeCheckpoint( string filename, 
		ParticleContainer* particleContainer,
		DomainDecompBase* domainDecomp )
{
	domainDecomp->assertDisjunctivity(particleContainer);
	if(!this->_localRank)
	{
		ofstream checkpointfilestream(filename.c_str());
		checkpointfilestream << "mardyn trunk " << CHECKPOINT_FILE_VERSION;
		checkpointfilestream << "\n";
		checkpointfilestream << " Length\t" << setprecision(9) << _globalLength[0] << " " << _globalLength[1] << " " << _globalLength[2] << "\n";
		if(this->_componentwiseThermostat)
		{
			for( map<int, int>::iterator thermit = this->_componentToThermostatIdMap.begin();
					thermit != this->_componentToThermostatIdMap.end();
					thermit++ )
			{
				if(0 >= thermit->second) continue;
				checkpointfilestream << " CT\t" << 1+thermit->first
					<< "\t" << thermit->second << "\n";
			}
			for( map<int, double>::iterator Tit = this->_universalTargetTemperature.begin();
					Tit != this->_universalTargetTemperature.end();
					Tit++ )
			{
				if((0 >= Tit->first) || (0 >= Tit->second)) continue;
				checkpointfilestream << " ThT " << Tit->first << "\t" << Tit->second << "\n";
			}
		}
		else
		{
			checkpointfilestream << " Temperature\t" << _universalTargetTemperature[0] << endl;
		}
#ifndef NDEBUG
		checkpointfilestream << "# rho\t" << this->_globalRho << "\n";
		checkpointfilestream << "# rc\t" << global_simulation->getcutoffRadius() << "\n";
		checkpointfilestream << "# rcT\t" << global_simulation->getTersoffCutoff() << "\n";
#endif
		if(this->_globalUSteps > 1)
		{
			checkpointfilestream << setprecision(13);
			checkpointfilestream << " I\t" << this->_globalUSteps << " "
				<< this->_globalSigmaU << " " << this->_globalSigmaUU << "\n";
			checkpointfilestream << setprecision(8);
		}
		vector<Component>* components = _simulation.getEnsemble()->components();
		checkpointfilestream << " NumberOfComponents\t" << components->size() << endl;
		for(vector<Component>::const_iterator pos=components->begin();pos!=components->end();++pos){
			pos->write(checkpointfilestream);
		}
		unsigned int numperline=_simulation.getEnsemble()->components()->size();
		unsigned int iout=0;
		for(vector<double>::const_iterator pos=_mixcoeff.begin();pos!=_mixcoeff.end();++pos){
			checkpointfilestream << *pos;
			iout++;
			// 2 parameters (xi and eta)
			if(iout/2>=numperline) {
				checkpointfilestream << endl;
				iout=0;
				--numperline;
			}
			else if(!(iout%2)) {
				checkpointfilestream << "\t";
			}
			else {
				checkpointfilestream << " ";
			}
		}
		checkpointfilestream << _epsilonRF << endl;
		map<unsigned, unsigned> componentSets = this->_universalPG->getComponentSets();
		for( map<unsigned, unsigned>::const_iterator uCSIDit = componentSets.begin();
				uCSIDit != componentSets.end();
				uCSIDit++ )
		{ 
			if(uCSIDit->first > 100) continue;
			checkpointfilestream << " S\t" << 1+uCSIDit->first << "\t" << uCSIDit->second << "\n";
		}
		map<unsigned, double> tau = this->_universalPG->getTau();
		for( map<unsigned, double>::const_iterator gTit = tau.begin();
				gTit != tau.end();
				gTit++ )
		{
			unsigned cosetid = gTit->first;
			double* ttargetv = this->_universalPG->getTargetVelocity(cosetid);
			double* tacc = this->_universalPG->getAdditionalAcceleration(cosetid);
			checkpointfilestream << " A\t" << cosetid << "\t"
				<< ttargetv[0] << " " << ttargetv[1] << " " << ttargetv[2] << "\t"
				<< gTit->second << "\t"
				<< tacc[0] << " " << tacc[1] << " " << tacc[2] << "\n";
			delete ttargetv;
			delete tacc;
		}
		for( map<int, bool>::iterator uutit = this->_universalUndirectedThermostat.begin();
				uutit != this->_universalUndirectedThermostat.end();
				uutit++ )
		{
			if(0 > uutit->first) continue;
			if(uutit->second) checkpointfilestream << " U\t" << uutit->first << "\n";
		}
		checkpointfilestream << " NumberOfMolecules\t" << _globalNumMolecules << endl;

		checkpointfilestream << " MoleculeFormat\t" << "ICRVQD" << endl;
		checkpointfilestream.close();
	}

	domainDecomp->writeMoleculesToFile(filename, particleContainer); 
}

void Domain::initParameterStreams(double cutoffRadius, double cutoffRadiusLJ){
	_comp2params.initialize(*(_simulation.getEnsemble()->components()), _mixcoeff, _epsilonRF, cutoffRadius, cutoffRadiusLJ); 
}

void Domain::initFarFieldCorr(double cutoffRadius, double cutoffRadiusLJ) {
	double UpotCorrLJ=0.;
	double VirialCorrLJ=0.;
	double MySelbstTerm=0.;
	vector<Component>* components = _simulation.getEnsemble()->components();
	unsigned int numcomp=components->size();
	for(unsigned int i=0;i<numcomp;++i) {
		Component& ci=(*components)[i];
		unsigned int numljcentersi=ci.numLJcenters();
		unsigned int numchargesi = ci.numCharges();
		unsigned int numdipolesi=ci.numDipoles();
		unsigned int numtersoffi = ci.numTersoff();

		// effective dipoles computed from point charge distributions
		double chargeBalance[3];
		for(unsigned d = 0; d < 3; d++) chargeBalance[d] = 0;
		for(unsigned int si = 0; si < numchargesi; si++)
		{
			double tq = ci.charge(si).q();
			for(unsigned d = 0; d < 3; d++) chargeBalance[d] += tq * ci.charge(si).r()[d];
		}
		// point dipoles
		for(unsigned int si=0;si<numdipolesi;++si)
		{
			double tmy = ci.dipole(si).absMy();
			double evect = 0;
			for(unsigned d = 0; d < 3; d++) evect += ci.dipole(si).e()[d] * ci.dipole(si).e()[d];
			double norm = 1.0 / sqrt(evect);
			for(unsigned d = 0; d < 3; d++) chargeBalance[d] += tmy * ci.dipole(si).e()[d] * norm;
		}
		double my2 = 0.0;
		for(unsigned d = 0; d < 3; d++) my2 += chargeBalance[d] * chargeBalance[d];
		MySelbstTerm += my2 * ci.getNumMolecules();

		for(unsigned int j=0;j<numcomp;++j) {
			Component& cj=(*components)[j];
			unsigned numtersoffj = cj.numTersoff();
			// no LJ interaction between Tersoff components
			if(numtersoffi && numtersoffj) continue;
			unsigned int numljcentersj=cj.numLJcenters();
			ParaStrm& params=_comp2params(i,j);
			params.reset_read();
			// LJ centers
			for(unsigned int si=0;si<numljcentersi;++si) {
				double xi=ci.ljcenter(si).rx();
				double yi=ci.ljcenter(si).ry();
				double zi=ci.ljcenter(si).rz();
				double tau1=sqrt(xi*xi+yi*yi+zi*zi);
				for(unsigned int sj=0;sj<numljcentersj;++sj) {
					double xj=cj.ljcenter(sj).rx();
					double yj=cj.ljcenter(sj).ry();
					double zj=cj.ljcenter(sj).rz();
					double tau2=sqrt(xj*xj+yj*yj+zj*zj);
					if(tau1+tau2>=cutoffRadiusLJ){
						global_log->error() << "Error calculating cutoff corrections, rc too small" << endl;
						exit(1);
					}
					double eps24;
					params >> eps24;
					double sig2;
					params >> sig2;
					double uLJshift6;
					params >> uLJshift6;  // 0 unless TRUNCATED_SHIFTED

					if(uLJshift6 == 0.0)
					{
						double fac=double(ci.getNumMolecules())*double(cj.getNumMolecules())*eps24;
						if(tau1==0. && tau2==0.)
						{
							UpotCorrLJ+=fac*(TICCu(-6,cutoffRadiusLJ,sig2)-TICCu(-3,cutoffRadiusLJ,sig2));
							VirialCorrLJ+=fac*(TICCv(-6,cutoffRadiusLJ,sig2)-TICCv(-3,cutoffRadiusLJ,sig2));
						}
						else if(tau1!=0. && tau2!=0.)
						{
							UpotCorrLJ += fac*( TISSu(-6,cutoffRadiusLJ,sig2,tau1,tau2)
									- TISSu(-3,cutoffRadiusLJ,sig2,tau1,tau2) );
							VirialCorrLJ += fac*( TISSv(-6,cutoffRadiusLJ,sig2,tau1,tau2)
									- TISSv(-3,cutoffRadiusLJ,sig2,tau1,tau2) );
						}
						else {
							if(tau2==0.) 
								tau2=tau1;
							UpotCorrLJ+=fac*(TICSu(-6,cutoffRadiusLJ,sig2,tau2)-TICSu(-3,cutoffRadiusLJ,sig2,tau2));
							VirialCorrLJ+=fac*(TICSv(-6,cutoffRadiusLJ,sig2,tau2)-TICSv(-3,cutoffRadiusLJ,sig2,tau2));
						}
					}
				}
			}
		}
	}

	double fac=M_PI*_globalRho/(3.*_globalNumMolecules);
	UpotCorrLJ*=fac;
	VirialCorrLJ*=-fac;

	double epsRFInvrc3=2.*(_epsilonRF-1.)/((cutoffRadius*cutoffRadius*cutoffRadius)*(2.*_epsilonRF+1.));
	MySelbstTerm*=-0.5*epsRFInvrc3;

	_UpotCorr=UpotCorrLJ+MySelbstTerm;
	_VirialCorr=VirialCorrLJ+3.*MySelbstTerm;

	global_log->info() << "Far field terms: U_pot_correction  = " << _UpotCorr << " virial_correction = " << _VirialCorr << endl;
}

void Domain::setupProfile(unsigned xun, unsigned yun, unsigned zun)
{
	this->_universalNProfileUnits[0] = xun;
	this->_universalNProfileUnits[1] = yun;
	this->_universalNProfileUnits[2] = zun;
	for(unsigned d = 0; d < 3; d++)
	{
		_universalInvProfileUnit[d] = (double)_universalNProfileUnits[d] / _globalLength[d];
	}
	this->resetProfile();
}

void Domain::considerComponentInProfile(int cid)
{
	this->_universalProfiledComponents[cid] = true;
}

void Domain::recordProfile(ParticleContainer* molCont)
{
	int cid;
	unsigned xun, yun, zun, unID;
	double mv2, Iw2;
	for(Molecule* thismol = molCont->begin(); thismol != molCont->end(); thismol = molCont->next())
	{
		cid = thismol->componentid();
		if(this->_universalProfiledComponents[cid])
		{
			xun = (unsigned)floor(thismol->r(0) * this->_universalInvProfileUnit[0]);
			yun = (unsigned)floor(thismol->r(1) * this->_universalInvProfileUnit[1]);
			zun = (unsigned)floor(thismol->r(2) * this->_universalInvProfileUnit[2]);
			unID = xun * this->_universalNProfileUnits[1] * this->_universalNProfileUnits[2]
				+ yun * this->_universalNProfileUnits[2] + zun;
			this->_localNProfile[unID] += 1.0;
			for(int d=0; d<3; d++) this->_localvProfile[d][unID] += thismol->v(d);
			this->_localDOFProfile[unID] += 3.0 + (long double)(thismol->component()->getRotationalDegreesOfFreedom());

			// record _twice_ the total (ordered + unordered) kinetic energy
			mv2 = 0.0;
			Iw2 = 0.0;
			thismol->calculate_mv2_Iw2(mv2, Iw2);
			this->_localKineticProfile[unID] += mv2+Iw2;
		}
	}
	this->_globalAccumulatedDatasets++;
}

void Domain::collectProfile(DomainDecompBase* dode)
{
	unsigned unIDs = this->_universalNProfileUnits[0] * this->_universalNProfileUnits[1]
		* this->_universalNProfileUnits[2];
	dode->collCommInit(10*unIDs);
	for(unsigned unID = 0; unID < unIDs; unID++)
	{
		dode->collCommAppendLongDouble(this->_localNProfile[unID]);
		for(int d=0; d<3; d++)
			dode->collCommAppendLongDouble(_localvProfile[d][unID]);
		dode->collCommAppendLongDouble(this->_localDOFProfile[unID]);
		dode->collCommAppendLongDouble(_localKineticProfile[unID]);

                dode->collCommAppendLongDouble(this->_localWidomProfile[unID]);
                dode->collCommAppendLongDouble(this->_localWidomInstances[unID]);
                dode->collCommAppendLongDouble(this->_localWidomProfileTloc[unID]);
                dode->collCommAppendLongDouble(this->_localWidomInstancesTloc[unID]);
	}
	dode->collCommAllreduceSum();
	for(unsigned unID = 0; unID < unIDs; unID++)
	{
		_universalNProfile[unID] = (double)dode->collCommGetLongDouble();
		for(int d=0; d<3; d++)
			this->_universalvProfile[d][unID]
				= (double)dode->collCommGetLongDouble();
		this->_universalDOFProfile[unID]
			= (double)dode->collCommGetLongDouble();
		this->_universalKineticProfile[unID]
			= (double)dode->collCommGetLongDouble();

		this->_globalWidomProfile[unID]
			= (double)dode->collCommGetLongDouble();
		this->_globalWidomInstances[unID]
			= (double)dode->collCommGetLongDouble();
		this->_globalWidomProfileTloc[unID]
			= (double)dode->collCommGetLongDouble();
		this->_globalWidomInstancesTloc[unID]
			= (double)dode->collCommGetLongDouble();

		/*
		 * construct the temperature profile
		 */
		double Tun = this->getGlobalCurrentTemperature();
		if((!this->_localRank) && (_universalDOFProfile[unID] >= 1000))
		{
		  double twoEkin = this->_universalKineticProfile[unID];

		  double vvNN = 0.0;
		  for(unsigned d = 0; d < 3; d++)
		     vvNN += _universalvProfile[d][unID] * _universalvProfile[d][unID];
		  double twoEkindir = _universalProfiledComponentMass * vvNN / _universalNProfile[unID];

		  Tun = (twoEkin - twoEkindir) / _universalDOFProfile[unID];
		}
		// domainDecomp->doBroadcast(&Tun); // no longer needed, since MPI_Reduce (branch) was replaced with MPI_Allreduce (trunk)
		this->_universalTProfile[unID] = Tun; 
	}
	dode->collCommFinalize();
}

void Domain::outputProfile(const char* prefix)
{
	if(this->_localRank) return;

	string vzpryname(prefix);
	string Tpryname(prefix);
	string rhpryname(prefix);
        string upryname(prefix);
	rhpryname += ".rhpry";
	vzpryname += ".vzpry";
	Tpryname += ".Tpry";
        upryname += ".upr";
	ofstream rhpry(rhpryname.c_str());
	ofstream vzpry(vzpryname.c_str());
	ofstream Tpry(Tpryname.c_str());
	ofstream upry(upryname.c_str());
	if (!(vzpry && Tpry && rhpry && upry))
	{
		return;
	}
	rhpry.precision(6);
	rhpry << "# y\trho\ttotal DOF\n# \n";
	vzpry.precision(5);
	vzpry << "# y\tvz\tv\n# \n";
	Tpry.precision(6);
        Tpry << "# coord\t2Ekin/#DOF\t2Ekin/3N (dir.)\tT\n# \n";
	upry.precision(5);
        upry << "# coord\t\tmu_conf(loc)  mu_conf(glob) \t\t mu_rho(loc)  mu_rho(glob) \t "
             << "mu_at(loc)  mu_at(glob) \t\t mu_T(loc)  mu_T(glob) \t mu_id(loc)  "
             << "mu_id(glob) \t\t mu_res(loc)  mu_res(glob) \t mu(loc)  mu(glob) \t\t #(loc)  "
             << "#(glob)\n";

	double layerVolume = this->_globalLength[0] * this->_globalLength[1] * this->_globalLength[2]
		/ this->_universalNProfileUnits[1];
	for(unsigned y = 0; y < this->_universalNProfileUnits[1]; y++)
	{
		double yval = (y + 0.5) / this->_universalInvProfileUnit[1];

		long double Ny = 0.0;
		long double DOFy = 0.0;
		long double twoEkiny = 0.0;
                long double twoEkindiry = 0.0;
		long double velocitysumy[3];
                long double widomSigExpy = 0.0;
                long double widomInstancesy = 0.0;
                long double widomSigExpyTloc = 0.0;
                long double widomInstancesyTloc = 0.0;
		for(unsigned d = 0; d < 3; d++) velocitysumy[d] = 0.0;
		for(unsigned x = 0; x < this->_universalNProfileUnits[0]; x++)
		{
			for(unsigned z = 0; z < this->_universalNProfileUnits[2]; z++)
			{
				unsigned unID = x * this->_universalNProfileUnits[1] * this->_universalNProfileUnits[2]
					+ y * this->_universalNProfileUnits[2] + z;
				Ny += this->_universalNProfile[unID];
				DOFy += this->_universalDOFProfile[unID];
				twoEkiny += this->_universalKineticProfile[unID];
				for(unsigned d = 0; d < 3; d++) velocitysumy[d] += this->_universalvProfile[d][unID];
                                widomSigExpy += this->_globalWidomProfile[unID];
                                widomInstancesy += this->_globalWidomInstances[unID];
                                widomSigExpyTloc += this->_globalWidomProfileTloc[unID];
                                widomInstancesyTloc += this->_globalWidomInstancesTloc[unID];
			}
		}
                double rho_loc = Ny / (layerVolume * this->_globalAccumulatedDatasets);

		if(Ny >= 64.0)
		{
		   double vvdir = 0.0;
		   for(unsigned d = 0; d < 3; d++)
		   {
		      double vd = velocitysumy[d] / Ny;
		      vvdir += vd*vd;
		   }
                   twoEkindiry = Ny * _universalProfiledComponentMass * vvdir;

		   rhpry << yval << "\t" << rho_loc << "\t" << DOFy << "\n";
		   vzpry << yval << "\t" << (velocitysumy[2] / Ny) << "\t" << sqrt(vvdir) << "\n";
                   Tpry << yval << "\t" << (twoEkiny / DOFy) << "\t"
                        << (twoEkindiry / (3.0*Ny)) << "\t" << ((twoEkiny - twoEkindiry) / DOFy) << "\n";

                   if(widomInstancesy >= 100.0)
                   {
                      double mu_res_glob = -log(widomSigExpy / widomInstancesy);
                      double mu_conf_glob = mu_res_glob * _globalTemperatureMap[0];
                      double mu_id_glob = _globalTemperatureMap[0] * log(_globalDecisiveDensity);

                      double mu_T_glob = 0.0;
                      double mu_rho_loc = 0.0;
                      double mu_T_loc = 0.0;
                      double mu_res_loc = 0.0;
                      double mu_conf_loc = 0.0;
                      if(Ny >= 10.0)
                      {
                         mu_T_glob = 3.0*_globalTemperatureMap[0] * log(_universalLambda);
                         mu_res_loc = -log(widomSigExpyTloc / widomInstancesyTloc);              

                         if(widomInstancesyTloc >= 100.0)
                         {
                            double Tloc = (twoEkiny - twoEkindiry) / DOFy;
                            // mu_id_loc = Tloc * log(rho_loc * _universalLambda*_universalLambda*_universalLambda);
                            mu_rho_loc = Tloc * log(rho_loc);
                            mu_T_loc = 3.0*Tloc * (log(_universalLambda) + 0.5*log(_globalTemperatureMap[0] / Tloc));
                            mu_conf_loc = Tloc * mu_res_loc;
                         }
                      }
               
                      upry << yval << " \t\t " << mu_conf_loc << "  " << mu_conf_glob << " \t\t "
                           << mu_rho_loc << "  " << mu_id_glob - mu_T_glob << " \t "
                           << mu_conf_loc + mu_rho_loc << "  " << mu_conf_glob + mu_id_glob - mu_T_glob << " \t\t "
                           << mu_T_loc << "  " << mu_T_glob << " \t "
                           << mu_rho_loc + mu_T_loc << "  " << mu_id_glob << " \t\t "
                           << mu_res_loc << "  " << mu_res_glob << " \t "
                           << mu_rho_loc + mu_T_loc + mu_conf_loc << "  " << mu_id_glob + mu_conf_glob << " \t\t "
                           << widomInstancesy << "  " << widomInstancesyTloc << "\n";
                   }
		}
		else
		{
			rhpry << yval << "\t0.000\t" << DOFy << "\n";
		}
	}

	rhpry.close();
	vzpry.close();
	Tpry.close();
	upry.close();
}

void Domain::resetProfile()
{
	unsigned unIDs = this->_universalNProfileUnits[0] * this->_universalNProfileUnits[1]
		* this->_universalNProfileUnits[2];
	for(unsigned unID = 0; unID < unIDs; unID++)
	{
		this->_localNProfile[unID] = 0.0;
		this->_universalNProfile[unID] = 0.0;
		for(int d=0; d<3; d++)
		{
			this->_localvProfile[d][unID] = 0.0;
			this->_universalvProfile[d][unID] = 0.0;
		}
		this->_localDOFProfile[unID] = 0.0;
		this->_universalDOFProfile[unID] = 0.0;
		this->_localKineticProfile[unID] = 0.0;
		this->_universalKineticProfile[unID] = 0.0;

		this->_localWidomProfile[unID] = 0.0;
		this->_globalWidomProfile[unID] = 0.0;
		this->_localWidomInstances[unID] = 0.0;
		this->_globalWidomInstances[unID] = 0.0;
		this->_localWidomProfileTloc[unID] = 0.0;
		this->_globalWidomProfileTloc[unID] = 0.0;
		this->_localWidomInstancesTloc[unID] = 0.0;
		this->_globalWidomInstancesTloc[unID] = 0.0;
	}
	this->_globalAccumulatedDatasets = 0;
}


void Domain::Nadd(unsigned cid, int N, int localN)
{
	Ensemble* ensemble = _simulation.getEnsemble();
	Component* component = ensemble->component(cid);
	component->incNumMolecules(N);
	unsigned int rotationDegreesOfFreeedom = component->getRotationalDegreesOfFreedom();
	
	this->_globalNumMolecules += N;
	this->_localRotationalDOF[0] += localN * rotationDegreesOfFreeedom;
	this->_universalRotationalDOF[0] += N * rotationDegreesOfFreeedom;
	if( (this->_componentwiseThermostat)
			&& (this->_componentToThermostatIdMap[cid] > 0) )
	{
		int thid = this->_componentToThermostatIdMap[cid];
		this->_localThermostatN[thid] += localN;
		this->_universalThermostatN[thid] += N;
		this->_localRotationalDOF[thid] += localN * rotationDegreesOfFreeedom;
		this->_universalRotationalDOF[thid] += N * rotationDegreesOfFreeedom;
	}
	this->_localThermostatN[0] += localN;
	this->_universalThermostatN[0] += N;
	this->_localRotationalDOF[0] += localN * rotationDegreesOfFreeedom;
	this->_universalRotationalDOF[0] += N * rotationDegreesOfFreeedom;
}

void Domain::evaluateRho(
		unsigned long localN, DomainDecompBase* domainDecomp
		) {
	domainDecomp->collCommInit(1);
	domainDecomp->collCommAppendUnsLong(localN);
	domainDecomp->collCommAllreduceSum();
	this->_globalNumMolecules = domainDecomp->collCommGetUnsLong();
	domainDecomp->collCommFinalize();

	this->_globalRho = this->_globalNumMolecules /
		(this->_globalLength[0] * this->_globalLength[1] * this->_globalLength[2]);
}

void Domain::setTargetTemperature(int thermostat, double targetT)
{
	if(thermostat < 0)
	{
		global_log->warning() << "Warning: thermostat \'" << thermostat << "\' (T = "
			<< targetT << ") will be ignored." << endl;
		return;
	}

	this->_universalTargetTemperature[thermostat] = targetT;
	if(!(this->_universalUndirectedThermostat[thermostat] == true))
		this->_universalUndirectedThermostat[thermostat] = false;

	/* FIXME: Substantial change in program behaviour! */
	if(thermostat == 0) {
		global_log->warning() << "Disabling the component wise thermostat!" << endl;
		disableComponentwiseThermostat();
	}
	if(thermostat >= 1) {
		if( ! _componentwiseThermostat ) {
			/* FIXME: Substantial change in program behaviour! */
			global_log->warning() << "Enabling the component wise thermostat!" << endl;
			_componentwiseThermostat = true;
			_universalTargetTemperature.erase(0);
			_universalUndirectedThermostat.erase(0);
			for(int d=0; d < 3; d++) this->_universalThermostatDirectedVelocity[d].erase(0);
			vector<Component>* components = _simulation.getEnsemble()->components();
			for( vector<Component>::iterator tc = components->begin(); tc != components->end(); tc ++ ) {
				if(!(this->_componentToThermostatIdMap[ tc->ID() ] > 0)) {
					this->_componentToThermostatIdMap[ tc->ID() ] = -1;
				}
			}
		}
	}
}

void Domain::enableComponentwiseThermostat()
{
	if(this->_componentwiseThermostat) return;

	this->_componentwiseThermostat = true;
	this->_universalTargetTemperature.erase(0);
	vector<Component>* components = _simulation.getEnsemble()->components();
	for( vector<Component>::iterator tc = components->begin(); tc != components->end(); tc ++ ) {
		if(!(this->_componentToThermostatIdMap[ tc->ID() ] > 0)) {
			this->_componentToThermostatIdMap[ tc->ID() ] = -1;
		}
	}
}

void Domain::enableUndirectedThermostat(int tst)
{
	this->_universalUndirectedThermostat[tst] = true;
	for(int d=0; d < 3; d++)
	{
		this->_universalThermostatDirectedVelocity[d][tst] = 0.0;
		this->_localThermostatDirectedVelocity[d][tst] = 0.0;
	}
}

void Domain::setGlobalTemperature(double temp)
{
	this->disableComponentwiseThermostat();
	this->_universalTargetTemperature[0] = temp;
}

vector<double> & Domain::getmixcoeff() { return _mixcoeff; }

double Domain::getepsilonRF() const { return _epsilonRF; }

void Domain::setepsilonRF(double erf) { _epsilonRF = erf; }

unsigned long Domain::getglobalNumMolecules() const { return _globalNumMolecules; }

void Domain::setglobalNumMolecules(unsigned long glnummol) { _globalNumMolecules = glnummol; }

double Domain::getglobalRho(){ return _globalRho;}

void Domain::setglobalRho(double grho){ _globalRho = grho;}

unsigned long Domain::getglobalRotDOF()
{
	return this->_universalRotationalDOF[0]; 
}

void Domain::setglobalRotDOF(unsigned long grotdof)
{
	this->_universalRotationalDOF[0] = grotdof;
}

void Domain::setGlobalLength(int index, double length) {
	_globalLength[index] = length;
}

void Domain::record_cv()
{
	if(_localRank != 0) return;

	this->_globalUSteps ++;
	this->_globalSigmaU += this->_globalUpot;
	this->_globalSigmaUU += this->_globalUpot*_globalUpot;
}

double Domain::cv()
{
	if((_localRank != 0) || (_globalUSteps == 0)) return 0.0;

	double id = 1.5 + 0.5*_universalRotationalDOF[0]/_globalNumMolecules;
	double conf = (_globalSigmaUU - _globalSigmaU*_globalSigmaU/_globalUSteps)
		/ (_globalUSteps * _globalNumMolecules * _globalTemperatureMap[0] * _globalTemperatureMap[0]);

	return id + conf;
}

//! methods implemented by Stefan Becker <stefan.becker@mv.uni-kl.de>
// the following two methods are used by the MmspdWriter (writing the output file in a format used by MegaMol)
double Domain::getSigma(unsigned cid, unsigned nthSigma){
  return _simulation.getEnsemble()->component(cid)->getSigma(nthSigma);
}
unsigned Domain::getNumberOfComponents(){
  return _simulation.getEnsemble()->components()->size();
}

void Domain::submitDU(unsigned cid, double DU, double* r)
{
   unsigned xun, yun, zun;
   xun = (unsigned)floor(r[0] * this->_universalInvProfileUnit[0]);
   yun = (unsigned)floor(r[1] * this->_universalInvProfileUnit[1]);
   zun = (unsigned)floor(r[2] * this->_universalInvProfileUnit[2]);
   int unID = xun * this->_universalNProfileUnits[1] * this->_universalNProfileUnits[2]
                  + yun * this->_universalNProfileUnits[2] + zun;
   if(unID < 0) return;

   _localWidomProfile[unID] += exp(-DU / _globalTemperatureMap[0]);
   _localWidomInstances[unID] += 1.0;

   double Tloc = _universalTProfile[unID];
   if(Tloc != 0.0)
   {
      _localWidomProfileTloc[unID] += exp(-DU/Tloc);
      _localWidomInstancesTloc[unID] += 1.0;
   }
}

