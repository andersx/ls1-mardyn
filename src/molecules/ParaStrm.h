/** \file parastrm.h
  * \brief Parameter stream
  * \author Martin Bernreuther <martin@ipvs.uni-stuttgart.de>
  * \version 0.1
  * \date 03.01.2006
*/
#ifndef PARASTRM_H_
#define PARASTRM_H_

#include <malloc.h>

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>

/** Parameter stream
 *
 * @author Martin Bernreuther
 */
class ParaStrm {
public:
	//enum Eparatype {UNDEF=0,DOUBLE,INT};

    /// Constructor generating a new, empty parameter stream.
	ParaStrm() : m_size(0)/*, m_pos(0)*/, m_pstrm(NULL), m_readpos(NULL) { }

    /// Constructor using an existing parameter stream (resets the reading "pointer" to the beginning of the stream).
	ParaStrm( const ParaStrm& param_stream ) : m_size( param_stream.m_size ), m_pstrm(NULL), m_readpos(NULL) {
        m_pstrm = (char *) malloc(m_size);
        assert(m_pstrm);
        memcpy( m_pstrm, param_stream.m_pstrm, m_size );
        m_readpos = m_pstrm;
	}

    /// Destructor
	~ParaStrm() {
		if (m_pstrm)
			free(m_pstrm);
	}

	/// end of stream reached?
	bool eos() const {
		return m_readpos >= m_pstrm + m_size;
	}
	/// stream empty?
	bool empty() const {
		return m_size == 0;
	}
	/// reset reading "pointer" to the beginning of the stream
	void reset_read() {
		m_readpos = m_pstrm; /*m_pos=0;*/
	}

    /// Copy parameter stream (resets the reading "pointer" to the beginning of the stream).
    ParaStrm& operator=( const ParaStrm& param_stream ) {
        m_size  = param_stream.m_size;
        m_pstrm = (char *) malloc(m_size);
        assert(m_pstrm);
        memcpy( m_pstrm, param_stream.m_pstrm, m_size );
        m_readpos = m_pstrm;
        return *this;
    }

	/// add value to the (end of) stream
	template<class T> ParaStrm& operator <<(T p) {
		ptrdiff_t oldsize = m_size;
		// determine relative reading position
		ptrdiff_t readpos = m_readpos - m_pstrm;
		// enlarge vector
		m_size += sizeof(p);
		if (!m_pstrm)
			m_pstrm = (char*) malloc(m_size);
		else
			m_pstrm = (char*) realloc(m_pstrm, m_size);
		// realloc might change m_pstrm!
		assert(m_pstrm);
		// save value
		*((T*) (m_pstrm + oldsize)) = p;
		//m_ptypes.push_back(UNDEF);
		// adjust m_readpos to maybe changed m_pstrm
		m_readpos = m_pstrm + readpos;
		return *this;
	}

	/// read value at actual stream position and move position
	template<class T> ParaStrm& operator >>(T& p) {
		assert(m_readpos+sizeof(T)<=m_pstrm+m_size);
		// get value
		p = *((T*) m_readpos);
		// move reading position
		m_readpos = (char*) ((T*) m_readpos + 1);
		//++m_pos;
		return *this;
	}

private:
	size_t m_size;  /**< Size of stored stream data in bytes. */
	char *m_pstrm;  /**< Base pointer of data storage. */
	char *m_readpos;  /**< Pointer to the current data item. */
	// we also could check for valid types...
	//std::vector<Eparatype> m_ptypes;
	//std::size_t m_pos;
};

#endif /*PARASTRM_H_*/
