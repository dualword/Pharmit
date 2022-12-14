/*
Pharmit
Copyright (c) David Ryan Koes, University of Pittsburgh and contributors.
All rights reserved.

Pharmit is licensed under both the BSD 3-clause license and the GNU
Public License version 2. Any use of the code that retains its reliance
on the GPL-licensed OpenBabel library is subject to the terms of the GPL2.

Use of the Pharmit code independently of OpenBabel (or any other
GPL2 licensed software) may choose between the BSD or GPL licenses.

See the LICENSE file provided with the distribution for more information.

*/
/*
 * PMol.h
 *
 *  Created on: Aug 2, 2010
 *      Author: dkoes
 *
 *  This is specialized class for storing small molecule information.
 *  It has a fairly compressed binary output.  There is a single conformer
 *  per each molecule since this makes lookup faster.
 */

#ifndef PHARMITSERVER_PMOL_H_
#define PHARMITSERVER_PMOL_H_

#include <boost/unordered_map.hpp>
#include <openbabel/mol.h>
#include <openbabel/atom.h>
#include <openbabel/bond.h>
#include "FloatCoord.h"
#include "RMSD.h"
#include "BumpAllocator.h"

#define MAX_BONDS 3

struct Property
{
	unsigned char atom;
	char value;

};

/* Class for creating an PMol (from a more expressive mol) and writing out */
class PMolCreator
{
	//atoms are stored grouped by typ
	struct AtomGroup
	{
		unsigned short startIndex; //for convenience, not output to file
		unsigned char atomic_number;
		vector<FloatCoord> coords;

		AtomGroup(unsigned anum) :
				startIndex(0), atomic_number(anum)
		{
		}
	};

	//Bounds are grouped by type (single, double, triple)
	//and stored as adjacency lists

	//adjacencly lists, segregated by bond type
	//directly indexed by atom index; one directional
	vector<vector<unsigned char> > bonds[MAX_BONDS];
	vector<AtomGroup> atoms;

	struct Property
	{
		unsigned char atom;
		char value;

		Property(unsigned ai, char v) :
				atom(ai), value(v)
		{
		}
	};

	//additional properties
	vector<Property> iso;
	vector<Property> chg;

	string name;
	unsigned numAtoms;
	unsigned nSrcs;
	unsigned bndSize[MAX_BONDS];
	unsigned nDsts;
	public:
	PMolCreator()
	{
	}
	PMolCreator(OpenBabel::OBMol& mol, bool deleteH = false) :
			numAtoms(0), nSrcs(0), nDsts(0)
	{
		memset(bndSize, 0, sizeof(bndSize));
		copyFrom(mol, deleteH);
	}
	~PMolCreator()
	{
	}

	void copyFrom(OpenBabel::OBMol& mol, bool deleteH = false);
	#ifdef __RD_ROMOL_H__
	void copyFrom(RDKit::ROMol& mol, bool deleteH = false);
#endif

	//write custom binary data
	bool writeBinary(ostream& out) const;

};

struct AdjList
{
	unsigned char src;
	unsigned char nDsts;
	unsigned char dsts[];
};

#define PMOLHEADER_MAX (256)
struct PMolHeader
{
	unsigned char nAtoms;
	unsigned char nAtomTypes;
	unsigned char nISO;
	unsigned char nCHG;
	unsigned char nBnds; //this could actually be computed, so is available space
	unsigned char adjListSize[MAX_BONDS];
	FloatCoord coords[];
};

struct AtomTypeCnts
{
	unsigned char atomic_number;
	unsigned char cnt;
};

struct ASDDataItem
{
	string tag;
	string value;

	ASDDataItem(const string& t, const string& v) :
			tag(t), value(v)
	{
	}
};

//Created by PMolReader, can output to regular mol formats, as well as be reoriented
//Data is stored of the end of the structure and we store pointers into this data
class PMol
{
	AtomTypeCnts *atomtypes; //nAtomTypes
	Property *iso; //nISO many
	Property *chg; //nCHG many
	char *adjlists[MAX_BONDS]; //cast to adjlist and iterate carefully
	char *name;
	PMolHeader header;
	//char buffer[]; //header ends in a flexible array

	PMol() :
			atomtypes(NULL), iso(NULL), chg(NULL)
	{
		memset(adjlists, 0, sizeof(adjlists));
	}
	friend class PMolReader;

	//assume data is already copied in, setup pointers
	//return final offset
	unsigned setup();
	public:

	const char *getTitle() const
	{
		return name;
	}
	double getMolWeight() const;

	void getCoords(vector<FloatCoord>& coords, const RMSDResult& rms);
	void getCoords(vector<Eigen::Vector3f>& coords, const Eigen::Transform<double, 3, Eigen::Affine>& transform);

	//write sdf with associated meta data
	//rotate/translate points if requested
	void writeSDF(ostream& out, const vector<ASDDataItem>& sddata,
			const RMSDResult& rms);

	void writeSDF(ostream& out, const vector<ASDDataItem>& sddata)
	{
		RMSDResult rdummy;
		writeSDF(out, sddata, rdummy);
	}

	void writeSDF(ostream& out)
	{
		vector<ASDDataItem> dummy;
		RMSDResult rdummy;
		writeSDF(out, dummy, rdummy);
	}

	//mutating rotation/translation
	void rotate(const double *rotation);
	void translate(const double *translation);
};

//generated PMol's
class PMolReader
{
protected:
	//read into an already allocated buffer
	virtual void* allocate(unsigned size) = 0;
	public:
	virtual ~PMolReader()
	{
	}
	virtual PMol* readPMol(const char *data); //return pmol at data
	virtual PMol* readPMol(const unsigned char *data)
	{
		const char *d = (const char*) data;
		return readPMol(d);
	}
	virtual PMol* readPMol(FILE *f); //return amol at current pos

};

//owns all memory produced (bump allocator for threading)
class PMolReaderBumpAlloc: public PMolReader
{
	BumpAllocator<1024 * 1024> allocator; //1MB chunks

protected:
	virtual void* allocate(unsigned size);

};

//malloc's the required memory, must be freed by caller
class PMolReaderMalloc: public PMolReader
{

protected:
	virtual void* allocate(unsigned size);

};

//owns and maintains a buffer suitable for a single mol
//each read will overwrite previous - do NOT share between threads
class PMolReaderSingleAlloc: public PMolReader
{
	void *buffer;
	unsigned bsize;
	protected:
	virtual void* allocate(unsigned size);
	public:
	PMolReaderSingleAlloc() :
			bsize(2048)
	{
		buffer = malloc(bsize);
	}

	~PMolReaderSingleAlloc()
	{
		free(buffer);
	}

};

#endif /* PHARMITSERVER_PMOL_H_ */
