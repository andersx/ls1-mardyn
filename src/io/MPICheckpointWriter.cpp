/** \file MPICheckpointWriter.cpp
  * \brief temporary CheckpointWriter alternative using MPI-IO
  * \author Martin Bernreuther <bernreuther@hlrs.de>
*/

#include "io/MPICheckpointWriter.h"

#include <sstream>
#include <fstream>
#include <string>

#include "Common.h"
#include "Domain.h"
#include "utils/Logger.h"
#include "parallel/DomainDecompBase.h"

#ifdef ENABLE_MPI
#include <mpi.h>

#include "parallel/ParticleData.h"
#endif

using Log::global_log;
using namespace std;


extern Simulation* global_simulation;


MPICheckpointWriter::MPICheckpointWriter(unsigned long writeFrequency, string outputPrefix, bool incremental)
 : _outputPrefix(outputPrefix), _writeFrequency(writeFrequency), _incremental(incremental), _appendTimestamp(false)
{
	if (outputPrefix == "")
	{
		_outputPrefix=global_simulation->getOutputPrefix();
	}
	else if (outputPrefix == "default")
	{
		_appendTimestamp = true;
	}
}

MPICheckpointWriter::~MPICheckpointWriter(){}


void MPICheckpointWriter::readXML(XMLfileUnits& xmlconfig) {
	_writeFrequency = 1;
	xmlconfig.getNodeValue("writefrequency", _writeFrequency);
	global_log->info() << "MPICheckpointWriter\twrite frequency: " << _writeFrequency << endl;
	
	_outputPrefix = "mardyn";
	xmlconfig.getNodeValue("outputprefix", _outputPrefix);
	global_log->info() << "MPICheckpointWriter\toutput prefix: " << _outputPrefix << endl;
	
	_incremental = false;
	int incremental = 1;
	xmlconfig.getNodeValue("incremental", incremental);
	//_incremental = (incremental != 0);
	if(incremental > 0) {
		_incremental = true;
	}
	global_log->info() << "MPICheckpointWriter\tincremental numbers: " << _incremental << endl;
	
	_appendTimestamp = false;
	int appendTimestamp = 0;
	//_appendTimestamp = (appendTimestamp != 0);
	xmlconfig.getNodeValue("appendTimestamp", appendTimestamp);
	if(appendTimestamp > 0) {
		_appendTimestamp = true;
	}
	global_log->info() << "MPICheckpointWriter\tappend timestamp: " << _appendTimestamp << endl;
}

void MPICheckpointWriter::initOutput(ParticleContainer* particleContainer, DomainDecompBase* domainDecomp, Domain* domain) {}

void MPICheckpointWriter::doOutput( ParticleContainer* particleContainer, DomainDecompBase* domainDecomp, Domain* domain, unsigned long simstep, list<ChemicalPotential>* lmu ) {
	if( simstep % _writeFrequency == 0 ) {
		stringstream filenamestream;
		filenamestream << _outputPrefix;
		
		if(_incremental) {
			/* align file numbers with preceding '0's in the required range from 0 to _numberOfTimesteps. */
			
			unsigned long numTimesteps = _simulation.getNumTimesteps();
			int num_digits = (int) ceil( log( double( numTimesteps / _writeFrequency ) ) / log(10.) );
			filenamestream << "-" << aligned_number( simstep / _writeFrequency, num_digits, '0' );
		}
		if(_appendTimestamp) {
			filenamestream << "-" << gettimestring();
		}
		filenamestream << ".MPIrestart.dat";

		string filename = filenamestream.str();
		global_log->debug() << "MPICheckpointWriter filename:" << filename;
		
		unsigned long nummolecules=particleContainer->getNumberOfParticles();
		unsigned long numbb=1;
#ifdef ENABLE_MPI
		int num_procs;
		MPI_CHECK( MPI_Comm_size(MPI_COMM_WORLD, &num_procs) );
		unsigned long gap=7+3+sizeof(unsigned long)+num_procs*(6*sizeof(double)+2*sizeof(unsigned long));
		int ownrank;
		MPI_CHECK( MPI_Comm_rank(MPI_COMM_WORLD, &ownrank) );
		MPI_File mpifh;
		MPI_CHECK( MPI_File_open(MPI_COMM_WORLD,const_cast<char*>(filename.c_str()),MPI_MODE_WRONLY|MPI_MODE_CREATE,MPI_INFO_NULL,&mpifh) );
		MPI_Offset mpioffset;
		MPI_Status mpistat;
		unsigned long startidx;
		if(ownrank==0)
		{
			MPI_CHECK( MPI_File_write(mpifh,const_cast<char*>("MarDyn20140817"),15,MPI_CHAR,&mpistat) );
			mpioffset=64-sizeof(unsigned long);
			MPI_CHECK( MPI_File_write_at(mpifh,mpioffset,&gap,1,MPI_UNSIGNED_LONG,&mpistat) );
			mpioffset+=sizeof(unsigned long);
			//mpioffset=64;
			MPI_CHECK( MPI_File_write_at(mpifh,mpioffset,const_cast<char*>("ICRVQD"),7,MPI_CHAR,&mpistat) );
			mpioffset+=7;
			//
			MPI_CHECK( MPI_File_write_at(mpifh,mpioffset,const_cast<char*>("BB"),3,MPI_CHAR,&mpistat) );
			mpioffset+=3;
			numbb=(unsigned long)(num_procs);
			MPI_CHECK( MPI_File_write_at(mpifh,mpioffset,&numbb,1,MPI_UNSIGNED_LONG,&mpistat) );
			mpioffset+=sizeof(unsigned long);
			//
			startidx=0;
		}
		MPI_CHECK( MPI_Exscan(&nummolecules, &startidx, 1, MPI_UNSIGNED_LONG, MPI_SUM, MPI_COMM_WORLD) );
		//
		mpioffset=64+7+3+sizeof(unsigned long)+ownrank*(6*sizeof(double)+2*sizeof(unsigned long));
		double bbmin[3],bbmax[3];
		bbmin[0]=domainDecomp->getBoundingBoxMin(0,domain);
		MPI_CHECK( MPI_File_write_at(mpifh,mpioffset,&bbmin[0],1,MPI_DOUBLE,&mpistat) );
		mpioffset+=sizeof(double);
		bbmin[1]=domainDecomp->getBoundingBoxMin(1,domain);
		MPI_CHECK( MPI_File_write_at(mpifh,mpioffset,&bbmin[1],1,MPI_DOUBLE,&mpistat) );
		mpioffset+=sizeof(double);
		bbmin[2]=domainDecomp->getBoundingBoxMin(2,domain);
		MPI_CHECK( MPI_File_write_at(mpifh,mpioffset,&bbmin[2],1,MPI_DOUBLE,&mpistat) );
		mpioffset+=sizeof(double);
		bbmax[0]=domainDecomp->getBoundingBoxMax(0,domain);
		MPI_CHECK( MPI_File_write_at(mpifh,mpioffset,&bbmax[0],1,MPI_DOUBLE,&mpistat) );
		mpioffset+=sizeof(double);
		bbmax[1]=domainDecomp->getBoundingBoxMax(1,domain);
		MPI_CHECK( MPI_File_write_at(mpifh,mpioffset,&bbmax[1],1,MPI_DOUBLE,&mpistat) );
		mpioffset+=sizeof(double);
		bbmax[2]=domainDecomp->getBoundingBoxMax(2,domain);
		MPI_CHECK( MPI_File_write_at(mpifh,mpioffset,&bbmax[2],1,MPI_DOUBLE,&mpistat) );
		mpioffset+=sizeof(double);
		MPI_CHECK( MPI_File_write_at(mpifh,mpioffset,&startidx,1,MPI_UNSIGNED_LONG,&mpistat) );
		mpioffset+=sizeof(unsigned long);
		MPI_CHECK( MPI_File_write_at(mpifh,mpioffset,&nummolecules,1,MPI_UNSIGNED_LONG,&mpistat) );
		mpioffset+=sizeof(unsigned long);
		global_log->debug() << "MPICheckpointWriter" << ownrank << "\tBB " << ":\t"
		                    << bbmin[0] << ", " << bbmin[1] << ", " << bbmin[2] << " - "
		                    << bbmax[0] << ", " << bbmax[1] << ", " << bbmax[2]
		                    << "\tstarting index=" << startidx << " nummolecules=" << nummolecules;
		//
		MPI_Datatype mpidtParticle;
		ParticleData::setMPIType(mpidtParticle);
		MPI_Aint mpidtParticlesize=sizeof(ParticleData);	// !=MPI_Type_size
		// MPI_Type_get_extent
		/*
		ParticleData pd[2];
		MPI_Aint addr0,addr1;
		MPI_Get_address(pd,&addr0);
		MPI_Get_address(&pd[1],&addr1);
		mpidtParticlesize=addr1-addr0;
		*/
		mpioffset=64+gap+startidx*mpidtParticlesize;
		MPI_CHECK( MPI_File_set_view(mpifh,mpioffset,mpidtParticle,mpidtParticle,const_cast<char*>("external32"),MPI_INFO_NULL) );
		for (Molecule* pos = particleContainer->begin(); pos != particleContainer->end(); pos = particleContainer->next()) {
			MPI_CHECK( MPI_File_write(mpifh, pos, 1, mpidtParticle, &mpistat) );
		}
		MPI_CHECK( MPI_File_close(&mpifh) );
#else
		unsigned long gap=7+3+sizeof(unsigned long)+(6*sizeof(double)+2*sizeof(unsigned long));
		unsigned int i;
		unsigned int offset=0;
		ofstream ostrm(filename.c_str(),ios::out|ios::binary);
		ostrm << "MarDyn";
		offset+=6;
		ostrm << "20140817";
		offset+=8;
		for(i=0;i<64-offset-sizeof(unsigned long);++i) ostrm << '\0';
		ostrm.write((char*)&gap,sizeof(unsigned long));
		//offset=64
		//ostrm.seekp(offset);
		ostrm << "ICRVQD" << '\0';
		offset+=7;
		ostrm << "BB" << '\0';
		ostrm.write((char*)&numbb,sizeof(unsigned long));
		double bbmin[3],bbmax[3];
		bbmin[0]=domainDecomp->getBoundingBoxMin(0,domain);
		bbmin[1]=domainDecomp->getBoundingBoxMin(1,domain);
		bbmin[2]=domainDecomp->getBoundingBoxMin(2,domain);
		bbmax[0]=domainDecomp->getBoundingBoxMax(0,domain);
		bbmax[1]=domainDecomp->getBoundingBoxMax(1,domain);
		bbmax[2]=domainDecomp->getBoundingBoxMax(2,domain);
		ostrm.write((char*)bbmin,3*sizeof(double));
		ostrm.write((char*)bbmax,3*sizeof(double));
		unsigned long startidx=0;
		ostrm.write((char*)&startidx,sizeof(unsigned long));
		ostrm.write((char*)&nummolecules,sizeof(unsigned long));
		for (Molecule* pos = particleContainer->begin(); pos != particleContainer->end(); pos = particleContainer->next()) {
			unsigned long id=pos->id();
			ostrm.write((char*)&id,sizeof(unsigned long));
			unsigned long componentid=pos->componentid();
			ostrm.write((char*)&componentid,sizeof(unsigned long));
			double r[3],v[3],q[4],D[3];
			for(i=0;i<3;++i) r[i]=pos->r(i);
			ostrm.write((char*)r,3*sizeof(double));
			for(i=0;i<3;++i) v[i]=pos->v(i);
			ostrm.write((char*)v,3*sizeof(double));
			q[0]=pos->q().qw();
			q[1]=pos->q().qx();
			q[2]=pos->q().qy();
			q[3]=pos->q().qz();
			ostrm.write((char*)q,4*sizeof(double));
			for(i=0;i<3;++i) D[i]=pos->D(i);
			ostrm.write((char*)D,3*sizeof(double));
		}
		ostrm.close();
#endif
	}
}

void MPICheckpointWriter::finishOutput(ParticleContainer* particleContainer, DomainDecompBase* domainDecomp, Domain* domain) {}

