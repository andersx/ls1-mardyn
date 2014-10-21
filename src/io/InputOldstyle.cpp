#include "io/InputOldstyle.h"

#ifdef ENABLE_MPI
#include <mpi.h>
#endif

#include <climits>

#include "Domain.h"
#include "ensemble/BoxDomain.h"
#include "ensemble/EnsembleBase.h"
#include "ensemble/GrandCanonical.h"
#include "ensemble/PressureGradient.h"
#include "Simulation.h"
#include "molecules/Molecule.h"
#ifdef ENABLE_MPI
#include "parallel/ParticleData.h"
#include "parallel/DomainDecompBase.h"
#endif
#include "particleContainer/ParticleContainer.h"
#include "utils/Logger.h"
#include "utils/Timer.h"

using Log::global_log;
using namespace std;


InputOldstyle::InputOldstyle() {}

InputOldstyle::~InputOldstyle(){}

void InputOldstyle::setPhaseSpaceFile(string filename) {
	_phaseSpaceFile = filename;
}

void InputOldstyle::setPhaseSpaceHeaderFile(string filename) {
	_phaseSpaceHeaderFile = filename;
}

void InputOldstyle::readPhaseSpaceHeader(Domain* domain, double timestep)
{
	string token, token2;

	global_log->info() << "Opening phase space header file " << _phaseSpaceHeaderFile << endl;
	_phaseSpaceHeaderFileStream.open( _phaseSpaceHeaderFile.c_str() );
	_phaseSpaceHeaderFileStream >> token;
	if( token != "mardyn")
	{
		global_log->error() << _phaseSpaceHeaderFile << " not a valid mardyn input file." << endl;
		exit(1);
	}

	string inputversion;
	_phaseSpaceHeaderFileStream >> token >> inputversion;
	// FIXME: remove tag trunk from file specification?
	if(token != "trunk") {
		global_log->error() << "Wrong input file specifier (\'" << token << "\' instead of \'trunk\')." << endl;
		exit(1);
	}

	if( strtoul(inputversion.c_str(), NULL, 0) < 20080701 ) {
		global_log->error() << "Input version tool old (" << inputversion << ")" << endl;
		exit(1);
	}

	global_log->info() << "Reading phase space header from file " << _phaseSpaceHeaderFile << endl;


	vector<Component>& dcomponents = *(_simulation.getEnsemble()->components());
	bool header = true; // When the last header element is reached, "header" is set to false

	while(header) {
		char c;
		_phaseSpaceHeaderFileStream >> c;
		if(c == '#') {
			// comment line
			_phaseSpaceHeaderFileStream.ignore( INT_MAX,'\n' );
			continue;
		}
		_phaseSpaceHeaderFileStream.putback(c);

		token.clear();
		_phaseSpaceHeaderFileStream >> token;
		global_log->info() << "{{" << token << "}}" << endl;

		if((token == "currentTime") || (token == "t")) {
			// set current simulation time
			_phaseSpaceHeaderFileStream >> token;
			_simulation.setSimulationTime( strtod(token.c_str(), NULL) );
		}
		else if((token == "Temperature") || (token == "T")) {
			// set global thermostat temperature
			domain->disableComponentwiseThermostat(); //disable component wise thermostats
			double targetT;
			_phaseSpaceHeaderFileStream >> targetT;
			domain->setGlobalTemperature( targetT );
		}
		else if((token == "ThermostatTemperature") || (token == "ThT") || (token == "h")) {
			// set up a new thermostat
			int thermostat_id;
			double targetT;
			_phaseSpaceHeaderFileStream >> thermostat_id;
			_phaseSpaceHeaderFileStream >> targetT;
			global_log->info() << "Thermostat number " << thermostat_id << " has T = " << targetT << ".\n";
			domain->setTargetTemperature( thermostat_id, targetT );
		}
		else if((token == "ComponentThermostat") || (token == "CT") || (token == "o")) {
			// specify a thermostat for a component
			if( !domain->severalThermostats() )
				domain->enableComponentwiseThermostat();
			int component_id;
			int thermostat_id;
			_phaseSpaceHeaderFileStream >> component_id >> thermostat_id;
			global_log->info() << "Component " << component_id << " (internally: " << component_id - 1 << ") is regulated by thermostat number " << thermostat_id << ".\n";
			component_id--; // FIXME thermostat IDs start with 0 in the program but not in the config file?!
			if( thermostat_id < 0 ) // thermostat IDs start with 0
				continue;
			domain->setComponentThermostat( component_id, thermostat_id );
		}
		else if((token == "Undirected") || (token == "U")) {
			// set undirected thermostat
			int thermostat_id;
			_phaseSpaceHeaderFileStream >> thermostat_id;
			domain->enableUndirectedThermostat( thermostat_id );
		}
		else if((token == "Length") || (token == "L")) {
			// simulation box dimensions
			double globalLength[3];
			_phaseSpaceHeaderFileStream >> globalLength[0] >> globalLength[1] >> globalLength[2];
			_simulation.getEnsemble()->domain() = new BoxDomain();
			for( int d = 0; d < 3; d++ ) {
				static_cast<BoxDomain*>(_simulation.getEnsemble()->domain())->setLength(d, globalLength[d]);
				domain->setGlobalLength( d, _simulation.getEnsemble()->domain()->length(d));
			}
		}
		else if((token == "HeatCapacity") || (token == "cv") || (token == "I"))
		{
			unsigned N;
			double U, UU;
			_phaseSpaceFileStream >> N >> U >> UU;
			domain->init_cv(N, U, UU);
		}
		else if((token == "NumberOfComponents") || (token == "C")) {
			// read in component definitions and
			// read in mixing coefficients

			// components:
			unsigned int numcomponents = 0;
			_phaseSpaceHeaderFileStream >> numcomponents;
			global_log->info() << "Reading " << numcomponents << " components" << endl;
			dcomponents.resize(numcomponents);
			for( unsigned int i = 0; i < numcomponents; i++ ) {
                                global_log->info() << "comp. i = " << i << ": " << endl;
				dcomponents[i].setID(i);
				unsigned int numljcenters = 0;
				unsigned int numcharges = 0;
				unsigned int numdipoles = 0;
				unsigned int numquadrupoles = 0;
				unsigned int numtersoff = 0;
				_phaseSpaceHeaderFileStream >> numljcenters >> numcharges >> numdipoles 
					>> numquadrupoles >> numtersoff;

				double x, y, z, m;
				for( unsigned int j = 0; j < numljcenters; j++ ) {
					double eps, sigma, tcutoff, do_shift;
					_phaseSpaceHeaderFileStream >> x >> y >> z >> m >> eps >> sigma >> tcutoff >> do_shift;
					dcomponents[i].addLJcenter( x, y, z, m, eps, sigma, tcutoff, (do_shift != 0) );
                                        global_log->info() << "LJ at [" << x << " " << y << " " << z << "], mass: " << m << ", epsilon: " << eps << ", sigma: " << sigma << endl;
				}
				for( unsigned int j = 0; j < numcharges; j++ ) {
					double q;
					_phaseSpaceHeaderFileStream >> x >> y >> z >> m >> q;
					dcomponents[i].addCharge( x, y, z, m, q );
                                        global_log->info() << "charge at [" << x << " " << y << " " << z << "], mass: " << m << ", q: " << q << endl;
				}
				for( unsigned int j = 0; j < numdipoles; j++ ) {
					double eMyx,eMyy,eMyz,absMy;
					_phaseSpaceHeaderFileStream >> x >> y >> z >> eMyx >> eMyy >> eMyz >> absMy;
					dcomponents[i].addDipole( x, y, z, eMyx, eMyy, eMyz, absMy );
                                        global_log->info() << "dipole at [" << x << " " << y << " " << z << "] " << endl;
				}
				for( unsigned int j = 0; j < numquadrupoles; j++ ) {
					double eQx,eQy,eQz,absQ;
					_phaseSpaceHeaderFileStream >> x >> y >> z >> eQx >> eQy >> eQz >> absQ;
					dcomponents[i].addQuadrupole(x,y,z,eQx,eQy,eQz,absQ);
                                        global_log->info() << "quad at [" << x << " " << y << " " << z << "] " << endl;
				}
				for( unsigned int j = 0; j < numtersoff; j++ ) {
					double x, y, z, m, A, B, lambda, mu, R, S, c, d, h, n, beta;
					_phaseSpaceHeaderFileStream >> x >> y >> z;
					_phaseSpaceHeaderFileStream >> m >> A >> B;
					_phaseSpaceHeaderFileStream >> lambda >> mu >> R >> S;
					_phaseSpaceHeaderFileStream >> c >> d >> h >> n >> beta;
					dcomponents[i].addTersoff( x, y, z, m, A, B, lambda, mu, R, S, c, d, h, n, beta );
                                        global_log->info() << "solid at [" << x << " " << y << " " << z << "] " << endl;
				}
				double IDummy1,IDummy2,IDummy3;
				// FIXME! Was soll das hier? Was ist mit der Initialisierung im Fall I <= 0.
				_phaseSpaceHeaderFileStream >> IDummy1 >> IDummy2 >> IDummy3;
				if( IDummy1 > 0. ) dcomponents[i].setI11(IDummy1);
				if( IDummy2 > 0. ) dcomponents[i].setI22(IDummy2);
				if( IDummy3 > 0. ) dcomponents[i].setI33(IDummy3);
                                domain->setProfiledComponentMass(dcomponents[i].m());
                                global_log->info() << endl;
			}

#ifndef NDEBUG
			for (int i = 0; i < numcomponents; i++) {
				global_log->debug() << "Component " << (i+1) << " of " << numcomponents << endl;
				global_log->debug() << dcomponents[i] << endl;
			}
#endif

			// Mixing coefficients
			vector<double>& dmixcoeff = domain->getmixcoeff();
			dmixcoeff.clear();
			for( unsigned int i = 1; i < numcomponents; i++ ) {
				for( unsigned int j = i + 1; j <= numcomponents; j++ ) {
					double xi, eta;
					_phaseSpaceHeaderFileStream >> xi >> eta;
					dmixcoeff.push_back( xi );
					dmixcoeff.push_back( eta );
				}
			}
			// read in global factor \epsilon_{RF}
			// FIXME: Maybe this should go better to a seperate token?!
			_phaseSpaceHeaderFileStream >> token;
			domain->setepsilonRF( strtod(token.c_str(),NULL) );
			long int fpos;
			if( _phaseSpaceFile == _phaseSpaceHeaderFile ) {
				// in the case of a single phase space header + phase space file
				// find out the actual position, because the phase space definition will follow
				// FIXME: is there a more elegant way?
				fpos = _phaseSpaceHeaderFileStream.tellg();
				_phaseSpaceFileStream.seekg( fpos, ios::beg );
			}
			// FIXME: Is there a better solution than skipping the rest of the file?
			header = false;
		}
		else if((token == "NumberOfMolecules") || (token == "N")) {
			// set number of Molecules 
			// FIXME: Is this part called in any case as the token is handled in the readPhaseSpace method?
			_phaseSpaceHeaderFileStream >> token;
			domain->setglobalNumMolecules( strtoul(token.c_str(),NULL,0) );
		}
		else if((token == "AssignCoset") || (token == "S")) {
			unsigned component_id, cosetid;
			_phaseSpaceHeaderFileStream >> component_id >> cosetid;
			component_id--; // FIXME: Component ID starting with 0 in program ...
			domain->getPG()->assignCoset( component_id, cosetid );
		}
		else if((token == "Accelerate") || (token == "A")) {
			unsigned cosetid;
			_phaseSpaceHeaderFileStream >> cosetid;
			double v[3];
			for(unsigned d = 0; d < 3; d++) 
				_phaseSpaceHeaderFileStream >> v[d];
			double tau;
			_phaseSpaceHeaderFileStream >> tau;
			double ainit[3];
			for(unsigned d = 0; d < 3; d++) 
				_phaseSpaceHeaderFileStream >> ainit[d];
			domain->getPG()->specifyComponentSet(cosetid, v, tau, ainit, timestep);
		}
		else {
			global_log->error() << "Invalid token \'" << token << "\' found. Skipping rest of the header." << endl;
			header = false;
		}
	}

	_phaseSpaceHeaderFileStream.close();
}

unsigned long InputOldstyle::readPhaseSpace(ParticleContainer* particleContainer, list<ChemicalPotential>* lmu, Domain* domain, DomainDecompBase* domainDecomp) {

	Timer inputTimer;
	inputTimer.start();

#ifdef ENABLE_MPI
	if (domainDecomp->getRank() == 0) 
	{ // Rank 0 only
#endif
	global_log->info() << "Opening phase space file " << _phaseSpaceFile << endl;
	_phaseSpaceFileStream.open( _phaseSpaceFile.c_str() );
	if (!_phaseSpaceFileStream.is_open()) {
		global_log->error() << "Could not open phaseSpaceFile " << _phaseSpaceFile << endl;
		exit(1);
	}
	global_log->info() << "Reading phase space file " << _phaseSpaceFile << endl;
#ifdef ENABLE_MPI
	} // Rank 0 only
#endif

	string token;
	vector<Component>& dcomponents = *(_simulation.getEnsemble()->components());
	unsigned int numcomponents = dcomponents.size();
	unsigned long nummolecules;
	unsigned long maxid = 0; // stores the highest molecule ID found in the phase space file
	string ntypestring("ICRVQD");
	enum Ndatatype { ICRVQD, IRV, ICRV } ntype = ICRVQD;

#ifdef ENABLE_MPI
	if (domainDecomp->getRank() == 0) 
	{ // Rank 0 only
#endif
	while(_phaseSpaceFileStream && (token != "NumberOfMolecules") && (token != "N")) {
		_phaseSpaceFileStream >> token;
	}
	if((token != "NumberOfMolecules") && (token != "N")) {
		global_log->error() << "Expected the token 'NumberOfMolecules (N)' instead of '" << token << "'" << endl;
		exit(1);
	}
	_phaseSpaceFileStream >> nummolecules;
#ifdef ENABLE_MPI
	} // Rank 0 only
	// TODO: Better do the following in setGlobalNumMolecules?!
	MPI_Bcast(&nummolecules, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
#endif
	domain->setglobalNumMolecules( nummolecules );
	global_log->info() << " number of molecules: " << nummolecules << endl;

	
#ifdef ENABLE_MPI
	if (domainDecomp->getRank() == 0) 
	{ // Rank 0 only
#endif
	streampos spos = _phaseSpaceFileStream.tellg();
	_phaseSpaceFileStream >> token;
	if((token=="MoleculeFormat") || (token == "M")) {

        _phaseSpaceFileStream >> ntypestring;
		ntypestring.erase( ntypestring.find_last_not_of( " \t\n") + 1 );
		ntypestring.erase( 0, ntypestring.find_first_not_of( " \t\n" ) );
		
		if (ntypestring == "ICRVQD") ntype = ICRVQD;
		else if (ntypestring == "ICRV") ntype = ICRV;
		else if (ntypestring == "IRV")  ntype = IRV;
		else {
			global_log->error() << "Unknown molecule format '" << ntypestring << "'" << endl;
			exit(1);
		}
	} else {
		_phaseSpaceFileStream.seekg(spos);
	}
	global_log->info() << " molecule format: " << ntypestring << endl;
	
	if( numcomponents < 1 ) {
		global_log->warning() << "No components defined! Setting up single one-centered LJ" << endl;
		numcomponents = 1;
		dcomponents.resize( numcomponents );
		dcomponents[0].setID(0);
		dcomponents[0].addLJcenter(0., 0., 0., 1., 1., 1., 6., false);
	}

#ifdef ENABLE_MPI
	} // Rank 0 only
#endif

#ifdef ENABLE_MPI
#define PARTICLE_BUFFER_SIZE  (16*1024)
	ParticleData particle_buff[PARTICLE_BUFFER_SIZE];
	int particle_buff_pos = 0;
	MPI_Datatype mpi_Particle;
	ParticleData::setMPIType(mpi_Particle);
#endif
	
	double x, y, z, vx, vy, vz, q0, q1, q2, q3, Dx, Dy, Dz;
	unsigned long id;
	unsigned int componentid;

	x=y=z=vx=vy=vz=q1=q2=q3=Dx=Dy=Dz=0.;
	q0=1.;

	for( unsigned long i = 0; i < domain->getglobalNumMolecules(); i++ ) {

#ifdef ENABLE_MPI
		if (domainDecomp->getRank() == 0) 
		{ // Rank 0 only
#endif	
		switch ( ntype ) {
			case ICRVQD:
				_phaseSpaceFileStream >> id >> componentid >> x >> y >> z >> vx >> vy >> vz
					>> q0 >> q1 >> q2 >> q3 >> Dx >> Dy >> Dz;
				break;
			case ICRV :
				_phaseSpaceFileStream >> id >> componentid >> x >> y >> z >> vx >> vy >> vz;
				break;
			case IRV :
				_phaseSpaceFileStream >> id >> x >> y >> z >> vx >> vy >> vz;
				break;
		}
		if(        ( x < 0.0 || x >= domain->getGlobalLength(0) )
				|| ( y < 0.0 || y >= domain->getGlobalLength(1) ) 
				|| ( z < 0.0 || z >= domain->getGlobalLength(2) ) ) 
		{
			global_log->warning() << "Molecule " << id << " out of box: " << x << ";" << y << ";" << z << endl;
		}

		if( componentid > numcomponents ) {
			global_log->error() << "Molecule id " << id << " has wrong componentid: " << componentid << ">" << numcomponents << endl;
			exit(1);
		}
		componentid --; // TODO: Component IDs start with 0 in the program.

		// store only those molecules within the domain of this process
		// The neccessary check is performed in the particleContainer addPartice method
		// FIXME: Datastructures? Pass pointer instead of object, so that we do not need to copy?!
		Molecule m1 = Molecule(id,&dcomponents[componentid],x,y,z,vx,vy,vz,q0,q1,q2,q3,Dx,Dy,Dz);
#ifdef ENABLE_MPI
		ParticleData::MoleculeToParticleData(particle_buff[particle_buff_pos], m1);
		} // Rank 0 only
		
		particle_buff_pos++;
		if ((particle_buff_pos >= PARTICLE_BUFFER_SIZE) || (i == domain->getglobalNumMolecules() - 1)) {
			MPI_Bcast(&particle_buff_pos, 1, MPI_INT, 0, MPI_COMM_WORLD);
			MPI_Bcast(particle_buff, PARTICLE_BUFFER_SIZE, mpi_Particle, 0, MPI_COMM_WORLD); // TODO: MPI_COMM_WORLD 
			for (int j = 0; j < particle_buff_pos; j++) {
				Molecule *m;
				ParticleData::ParticleDataToMolecule(particle_buff[j], &m);
				particleContainer->addParticle(*m);
				
				// TODO: The following should be done by the addPartice method.
				dcomponents[m->componentid()].incNumMolecules();
				domain->setglobalRotDOF(dcomponents[m->componentid()].getRotationalDegreesOfFreedom() + domain->getglobalRotDOF());
				
				if(m->id() > maxid) maxid = m->id();

				std::list<ChemicalPotential>::iterator cpit;
				for(cpit = lmu->begin(); cpit != lmu->end(); cpit++) {
					if( !cpit->hasSample() && (componentid == cpit->getComponentID()) ) {
						cpit->storeMolecule(*m);
					}
				}
				delete m;
			}			
			particle_buff_pos = 0;
		}
#else
		particleContainer->addParticle(m1);
		
		 // TODO: The following should be done by the addPartice method.
		dcomponents[componentid].incNumMolecules();
		domain->setglobalRotDOF(dcomponents[componentid].getRotationalDegreesOfFreedom() + domain->getglobalRotDOF());
		
		if(id > maxid) maxid = id;

		std::list<ChemicalPotential>::iterator cpit;
		for(cpit = lmu->begin(); cpit != lmu->end(); cpit++) {
			if( !cpit->hasSample() && (componentid == cpit->getComponentID()) ) {
				cpit->storeMolecule(m1);
			}
		}
#endif

		// Print status message
		unsigned long iph = domain->getglobalNumMolecules() / 100;
		if( iph != 0 && (i % iph) == 0 )
			global_log->info() << "Finished reading molecules: " << i/iph << "%\r" << flush;
	}

	global_log->info() << "Finished reading molecules: 100%" << endl;
	global_log->info() << "Reading Molecules done" << endl;

	// TODO: Shouldn't we always calculate this?
	if( !domain->getglobalRho() ){
		domain->setglobalRho( domain->getglobalNumMolecules() / domain->getGlobalVolume() );
		global_log->info() << "Calculated Rho_global = " << domain->getglobalRho() << endl;
	}

#ifdef ENABLE_MPI
	if (domainDecomp->getRank() == 0) 
	{ // Rank 0 only
#endif	
	_phaseSpaceFileStream.close();
#ifdef ENABLE_MPI
	} // Rank 0 only
#endif

	inputTimer.stop();
	global_log->info() << "Initial IO took:                 " << inputTimer.get_etime() << " sec" << endl;
#ifdef ENABLE_MPI
	MPI_CHECK( MPI_Type_free(&mpi_Particle) );
#endif
	return maxid;
}
