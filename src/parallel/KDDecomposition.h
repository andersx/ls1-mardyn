#ifndef KDDECOMPOSITION2_H_
#define KDDECOMPOSITION2_H_

#include <mpi.h>

#include <list>
#include <vector>

#define KDDIM 3

#include "DomainDecompBase.h"
#include "parallel/CollectiveCommunication.h"

class ParticleData;
class KDNode;


/** @brief KD tree based domain decomposition for better load balancing.
 *
 * The basic idea is to build up all possible subdivisions and do a A*-like
 * search of the best subdivision.
 *
 * The function to minimize is the load imbalance:
 * sum over all children i: \sum (load_i - _optimalLoad)^2
 *
 * During the downward pass an estimate is the expected load imbalance, which is
 * averaged over all children, in the upward pass, the exact imbalance is calculated.
 *
 * \note It is important for the A*-search that the estimate is an
 *       underestimation (<=) of the load imbalance.
 *
 * \note Some computation of the deviation / expected deviation is done in KDNode.
 */
class KDDecomposition: public DomainDecompBase{

	friend class KDDecompositionTest;

 public:
	/** @brief create an initial decomposition tree
	 *
	 * The constructor determines the number of cells and creates an initial decomposition
	 * of the domain (not yet balanced), which is stored in _decompTree and _ownArea.
	 * @param cutoffRadius largest cutoff radius of a molecule (determines a basic
	 *                     cell for loadbalancing)
	 * @param domain
	 * @param updateFrequency every n-th timestep, load will be balanced.
	 * @param fullSearchThreshold If a KDNode has a processor count less or equal this number,
	 *                            all possible decompositions will be investigated, so it
	 *                            influences the quality of the load balancing. I recommend to
	 *                            set it to 2 - 4.
	 */
	KDDecomposition(double cutoffRadius, Domain* domain, int updateFrequency = 100, int fullSearchThreshold = 2);

    KDDecomposition(){}

	// documentation see father class (DomainDecompBase.h)
	~KDDecomposition();


	/** @brief Read in XML configuration for KDDecomposition and all its included objects.
	 *
	 * The following xml object structure is handled by this method:
	 * \code{.xml}
	   <parallelisation type="KDDecomposition">
	     <updateFrequency>INTEGER</updateFrequency>
	     <fullSearchThreshold>INTEGER</fullSearchThreshold>
	   </parallelisation>
	   \endcode
	 */
	virtual void readXML(XMLfileUnits& xmlconfig);

	//###############################################
	//### The following methods are those of the  ###
	//### base class which have to be implemented ###
	//###############################################

	//! @brief exchange molecules between processes
	//!
	//! molecules which aren't in the domain of their process any
	//! more are transferred to their neighbours. Additionally, the
	//! molecules for the halo-region are transferred.
	//! In this implementation, the methods used for load-balancing and those
	//! for just exchanging particles without rebalancing are quite similar.
	//! Therefore, this method just calls balanceAndExchange(0,...), where
	//! the "0" says that only exchanging and no balancing has to be done.
	//! @param moleculeContainer needed to get those molecules which have to be exchanged
	//! @param domain is e.g. needed to get the size of the local domain
	void exchangeMolecules(ParticleContainer* moleculeContainer, Domain* domain);

	//! @brief balance the load (and optimise communication) and exchange boundary particles
	//!
	//! The workflow is as follows (some steps only if balancing shall be done):
	//! - if balance: _numParticlesPerCell is updated to be able to calculate the load of the cells
	//! - if balance: A new decomposition tree is calculated (using recDecomp) based on the particle distribution
	//! - if balance: find out neighbours
	//! - collect pointers to particles to be send to all neighbours
	//! - communicate with each neighbour how many particles will be sent and recieved
	//! - copy data to be sent into buffers
	//! - if balance: rebuild the molecule container (new size and region), replace the old decomposition tree
	//! - transfer data and insert the recieved molecules into the moleculeContainer
	//! - for processes which span the whole domain in at least one direction, ensure periodic boundary
	//! @param balance if true, a rebalancing should be performed, otherwise only exchange
	//! @param moleculeContainer needed for calculating load and to get the particles
	//! @param domain is e.g. needed to get the size of the local domain
	void balanceAndExchange(bool balance, ParticleContainer* moleculeContainer, Domain* domain);



	// documentation see father class (DomainDecompBase.h)
	bool procOwnsPos(double x, double y, double z, Domain* domain);

	//! @todo comment and thing
	double getBoundingBoxMin(int dimension, Domain* domain);
	//! @todo comment and thing
	double getBoundingBoxMax(int dimension, Domain* domain);

	//! @brief writes information about the current decomposition into the given file
	//!
	//! This decomposition first writes a very small header with the following lines:
	//! - "size", followed by three double values for the size of the domain
	//! - "decompData Regions": just this text to state that now the header is finished and data follows
	//! - one line per proc, giving the bounding box (minx, miny, minz, maxx, maxy, maxz)
	//!
	//! An example file for 5 procs could look like this:
	//!
	//! |size 62.0 62.0 62.0 \n
	//!  decompData Regions \n
	//!  0.0 0.0 0.0 20.0 62.0 62.0 \n
	//!  20.0 0.0 0.0 62.0 40.0 25.0 \n
	//!  20.0 40.0 0.0 62.0 62.0 25.0 \n
	//!  20.0 0.0 25.0 62.0 30.0 62.0 \n
	//!  20.0 30.0 25.0 62.0 62.0 62.0
	//! @param filename name of the file into which the data will be written
	//! @param domain e.g. needed to get the bounding boxes
	void printDecomp(std::string filename, Domain* domain);

	// documentation see father class (DomainDecompBase.h)
	int getRank(void){ return _ownRank;}

	// documentation see father class (DomainDecompBase.h)
	int getNumProcs(){ return _numProcs;}

	// documentation see father class (DomainDecompBase.h)
	void barrier() { MPI_CHECK( MPI_Barrier(MPI_COMM_WORLD) ); }

	// documentation see father class (DomainDecompBase.h)
	double getTime() { return MPI_Wtime(); };

	//! @brief returns total number of molecules
	unsigned Ndistribution(unsigned localN, float* minrnd, float* maxrnd);

	//! @brief checks identity of random number generators
	void assertIntIdentity(int IX);
	void assertDisjunctivity(TMoleculeContainer* mm);


	//##################################################################
	// The following methods with prefix "collComm" are all used
	// in the context of collective communication. Each of the methods
	// basically has to call the corresponding method from the class
	// CollectiveCommunication (or CollectiveCommDummy in the sequential
	// case). To get information about how to use this methods, read
	// the documentation of the class CollectiveCommunication and of the
	// father class of this class (DomainDecompBase.h)
	//##################################################################
	void collCommInit(int numValues){ _collComm.init(MPI_COMM_WORLD, numValues); };
	void collCommFinalize(){ _collComm.finalize(); };
	void collCommAppendInt(int intValue){_collComm.appendInt(intValue);};
	void collCommAppendUnsLong(unsigned long unsLongValue){_collComm.appendUnsLong(unsLongValue);};
	void collCommAppendFloat(float floatValue){_collComm.appendFloat(floatValue);};
	void collCommAppendDouble(double doubleValue){_collComm.appendDouble(doubleValue);};
	void collCommAppendLongDouble(long double longDoubleValue){_collComm.appendLongDouble(longDoubleValue);};
	int collCommGetInt(){return _collComm.getInt(); };
	unsigned long collCommGetUnsLong(){return _collComm.getUnsLong(); };
	float collCommGetFloat(){return _collComm.getInt(); };
	double collCommGetDouble(){ return _collComm.getDouble(); };
	long double collCommGetLongDouble(){ return _collComm.getLongDouble(); };
	void collCommAllreduceSum(){ _collComm.allreduceSum(); };
	void collCommBroadcast(int root = 0){ _collComm.broadcast(root); };

	int getUpdateFrequency() { return _frequency; }
	void getUpdateFrequency(int frequency) { _frequency = frequency; }

 private:
	//$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
	//$ Methoden, die von balanceAndExchange benoetigt werden $
	//$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$

	//! @brief Each process collects the particles to be send to neighbours
	//!
	//! First, all processes (and their region, incl. halo) which need
	//! part of the sourceArea are determined.
	//! (Also all necessary periodic copies of the neighbours region)
	//! Then all particles (pointers to them) from those regions
	//! are stored in particlesToSend[procid]
	//! The area given by sourceArea and the area covered by the moleculeContainer
	//! should be the same!
	//! @param sourceArea area from which particles should be sent
	//! @param moleculeContainer needed to get the molecule pointers
	//! @param domain needed to get the domain size
	//! @param procIDs Here the ids of the neighbouring procs will be stored
	//! @param numMolsToSend Here the number of molecules to be sent are stored
	//!                      The vector has to be initialised in this method
	//! @param particlesToSend Here the pointers to the particles will be stored
	void getPartsToSend(KDNode* sourceArea, KDNode* decompTree, ParticleContainer* moleculeContainer, Domain* domain, std::vector<int>& procIDs, std::vector<int>& numMolsToSend, std::vector<std::list<Molecule*> >& particlesToSend);

	//! @brief transfer of the molecule data to the neighbours
	//! After each process knows which particles have to be sent, the particles
	//! can be exchanged.
	//!
	//! @param procsToSendTo IDs of processes to which particles have to be sent
	//! @param procsToRecvFrom IDs of processes from which particles have to be received
	//! @param numMolsToSend Number of molecules to be sent to each neighbour
	//! @param numMolsToRecv Number of molecules to be recieved from each neighbour
	//! @param particlePtrsToSend pointers to the particles
	//! @param particlesRecvBufs buffers to store recieved particles
	//!
	//! @note The particlesRecvBufs is allocated in this method and has to be
	//!       deleted afterwards!
	void sendReceiveParticleData(std::vector<int>& procsToSendTo,
			std::vector<int>& procsToRecvFrom, std::vector<int>& numMolsToSend,
			std::vector<int>& numMolsToRecv, std::vector<std::list<Molecule*> >& particlePtrsToSend,
			std::vector<ParticleData*>& particlesRecvBufs);

	//! @brief corrects the position of particles outside the domain after a balance step
	//!
	//! After a new decomposition of the domain, all processes have a new part of
	//! the domain. Each process sends the particles, which it no longer possesses, to the
	//! new owner. Those particles which are still owned (old and new region overlap)
	//! have to be kept. Also particles from the new halo region are kept, all other
	//! particles have to be deleted. Now it can happen that a particle just left the
	//! global domain in the step before the rebalancing and now is in the halo region.
	//! If in this case the new owner of this halo cell is equal to the old owner of
	//! the particle, the particle has to be kept, but the coordinates have to be changed.
	//! In the following sketch, the particle marked with "x" belonged to process 2 in the
	//! old decomposition and just left the global area on the right side and still belongs
	//! to process 2 in the new decomposition, but the position has to be adjusted.
	//! This is done by this method.
	//!  ____________________              ____________________
	//! |              |     |            |______              |
	//! |              | P2  |            |      |             |
	//! |              | old |x    =>     |x P2  |             |
	//! |              |_____|            |  new |             |
	//! |                    |            |______|             |
	//! |____________________|            |____________________|
	//!
	//! @param ownArea contains the information about the own area determined by the new decomposition
	//! @param moleculeContainer needed to walk through all still owned particles
	//! @param domain needed to find out the size of the global domain
	void adjustOuterParticles(KDNode*& ownArea, ParticleContainer* moleculeContainer, Domain* domain);

	//! @brief create copies due to periodic boundary
	//!
	//! If a proc's area occupies the whole domain (per dimension), it has two periodic
	//! boundaries in that dimension. That means that is possesses at least two (up to eight)
	//! versions of the same cells/molecules. Those copies are created by this method
	//! and stored in the moleculeContainer
	//! @param moleculeContainer used to walk through the molecules, create copies and store them again
	//! @param domain needed to get the length of the domain
	//! @todo make it work with overlapping decomposition trees
	//! @todo more efficiency (don't run over all molecules)
	void createLocalCopies(ParticleContainer* moleculeContainer, Domain* domain);

	//$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
	//$ sonstige Methoden
	//$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$

	/**
	 * print all Decomposition Trees at all processors.
	 */
	void printDecompTrees(KDNode* root);


	void calculateCostsPar(KDNode* area, std::vector<std::vector<double> >& costsLeft, std::vector<std::vector<double> >& costsRight, MPI_Comm commGroup);


	//! @brief calculates the index of a certain cell in the global cell array
	//!
	//! During the construction of a new decomposition, a area is recursively
	//! divided into two smaller areas. Each of the corresponding dividing planes
	//! can be orthogonal to each of the three spacial axis. For each of these three
	//! divisions, all cells in the "left" part are traversed by using three loops over
	//! the three dimensions. E.g. if the dividing plane is orthogonal to the x-axis,
	//! the loop goes over the left part of all x-cells, over all y-cells and over all z-cells.
	//! To avoid code duplication for the three possibilities of the dividing plane, a
	//! special method is used. The tree loops don't run over x, y and z, but over
	//! divDim, dim1 and dim2, where those three values are chosen differently
	//! for each of the dividing planes (x: 0,1,2; y: 1,0,2; z: 2,0,1). Those three values plus
	//! the corresponding loop-indeces are used by this method to calculate the global index.
	unsigned int getGlobalIndex(int divDim, int dim1, int dim2, int index_dim, int index_dim1, int index_dim2, KDNode* localArea);

	//! @brief provides a "real" modulo function
	//!
	//! The C build-in modulo operation uses a symmetric modulo,
	//! This method provides a mathematical modulo (important for negative numbers)
	//! @param number Number to calculate the modulo for
	//! @param modulo obviously the modulo
	int mod(int number, int modulo);


	//! @brief exchange decomposition data and build up the resulting tree
	//!
	//! After the new decomposition has been determined (by recDecompPar), each
	//! process knows its own Area (and that part of the decomp tree which lies on the
	//! the path between the root node and the node with the own Area), but each process
	//! has to know the complete decomposition. This method exchanges the decomposition
	//! data between the processes and builds up a complete decomposition Tree on
	//! all processes.
	void completeTreeInfo(KDNode*& root, KDNode*& ownArea);


	//! @brief Find all processes which own a part of the given area
	//!
	//! This method is supposed to run recursively. It should usually be called
	//! with decompTree and testNode pointing both to the root of a decomposition tree.
	//! In the recursive calls, testNode will be changed to walk through the whole tree.
	//! The given area can be partly outside of the area covered by the decomposition tree
	//! (usually because of the halo region), in this case the periodic boundary conditions
	//! are applied to get the corresponding area on the other side of the region.
	//! @param low low corner of the area for which the owners shall be found
	//! @param high high corner of the area for which the owners shall be found
	//! @param decompTree root KDNode of a domain decomposition tree. The area covered by
	//!                   this KDNode must not overlap the domain
	//! @param testNode KDNode currenty tested for intersection (method is used recursively)
	//! @param procIDs here the ids of those procs which own a part will be stored
	//! @param neighbHaloAreas here the corresponding (see procIDs) areas will be stored. For
	//!                        each procID six double values are stored, first the three
	//!                        coordinates for the low corner of the area, then the high corner
	//! @todo make it work for overlapping decomposition trees
	void getOwningProcs(int low[KDDIM], int high[KDDIM], KDNode* decompTree, KDNode* testNode, std::vector<int>* procIDs, std::vector<int>* neighbHaloAreas);


	//! @brief
	//! @todo _numParticles should perhaps not be a member variable (think about that)
	void getNumParticles(ParticleContainer* moleculeContainer);

	void balance();

	bool decompose(KDNode* fatherNode, KDNode*& ownArea, MPI_Comm commGroup);

	bool decompose(KDNode* fatherNode, KDNode*& ownArea, MPI_Comm commGroup, const double globalMinimalDeviation);

	/**
	 * Get the load for the area represented by this node.
	 *
	 * @note: if this implementation is too slow for large domains, we could change
	 *        it so that all division costs for
	 */
	bool calculateAllSubdivisions(KDNode* node, std::list<KDNode*>& subdivededNodes, MPI_Comm commGroup);

	//######################################
	//###    private member variables    ###
	//######################################

	//! Rank of the local process (to be set by the constructor)
	int _ownRank;
	//! total number of processes (to be set by the constructor)
	int _numProcs;

	//! Length of one cell. The length of the cells is the cutoff-radius, or a
	//! little bit larger as there has to be a natural number of cells.
	double _cellSize[KDDIM];

	//! Number of cells in the global domain in each coordinate direction.
	//!  _globalCellsPerDim does not include the halo region
	int _globalCellsPerDim[KDDIM];

	//! global number of cells (without halo)
	int _globalNumCells;

	// Pointer to the root element of the decomposition tree
	KDNode* _decompTree;

	// each process owns an area in the decomposition
	KDNode* _ownArea;

	//! Number of particles for each cell (including halo?)
	unsigned int* _numParticlesPerCell;

	ParticleContainer* _moleculeContainer;

	//! variable used for different kinds of collective operations
	CollectiveCommunication _collComm;

	/* TODO: This may not be equal to the number simulation steps if balanceAndExchange 
	 * is not called exactly once in every simulatin step! */
	//! number of simulation steps. Can be used to trigger load-balancing every _frequency steps
	size_t _steps;

	//! determines how often rebalancing is done
	int _frequency;

	/*
	 * Threshold for full tree search. If a node has more than _fullSearchThreshold processors,
	 * it is for each dimension divided in the middle only. Otherwise, all possible subdivisions
	 * are created.
	 */
	int _fullSearchThreshold;

	// mpi data type for particle data
	MPI_Datatype _mpi_Particle_data;

};


#endif /* KDDECOMPOSITION2_H_ */
