/*
 Pharmer: Efficient and Exact 3D Pharmacophore Search
 Copyright (C) 2011  David Ryan Koes and the University of Pittsburgh

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * main.cpp
 *
 *  Created on: Jul 30, 2010
 *      Author: dkoes
 *
 *  Pharmacophore tools for recognition and search.
 *  Really these would probably more suited as their own executables,
 *  but I'm too lazy to move away from Eclipe's default make process which
 *  requires a single target.
 */

#include "CommandLine2/CommandLine.h"
#include "pharmarec.h"
#include "pharmerdb.h"
#include "PharmerQuery.h"
#include <iostream>
#include <boost/shared_ptr.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include "queryparsers.h"
#include "Timer.h"
#include "MolFilter.h"
#include "PharmerServer.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <glob.h>
#include "ReadMCMol.h"
#include "Excluder.h"

using namespace boost;
using namespace OpenBabel;


cl::opt<bool> Quiet("q", cl::desc("quiet; suppress informational messages"),
		cl::init(true));
cl::opt<bool> ShowQuery("show-query", cl::desc("print query points"),
		cl::init(false));
cl::opt<bool> Print("print", cl::desc("print results"), cl::init(true));
cl::opt<string> Cmd("cmd",
		cl::desc("command [pharma, dbcreate, dbcreateserverdir, dbsearch, server]"),
		cl::Positional);
cl::list<string> Database("dbdir", cl::desc("database directory(s)"));
cl::list<string> inputFiles("in", cl::desc("input file(s)"));
cl::list<string> outputFiles("out", cl::desc("output file(s)"));

cl::opt<string> pharmaSpec("pharmaspec",
		cl::desc("pharmacophore specification"));
cl::opt<unsigned> NThreads("nthreads", cl::desc("utilize n threads; default 1"),
		cl::value_desc("n"), cl::init(1));
cl::opt<double> MaxRMSD("max-rmsd",
		cl::desc("maximum allowed RMSD; default max allowed by query"),
		cl::init(HUGE_VAL));
cl::opt<unsigned> MinWeight("min-weight",
		cl::desc("minimum allowed molecular weight"), cl::init(0));
cl::opt<unsigned> MaxWeight("max-weight",
		cl::desc("maximum allowed molecular weight"), cl::init(UINT_MAX));
cl::opt<unsigned> MinNRot("min-nrot",
		cl::desc("minimum allowed rotatable bonds"), cl::init(0));
cl::opt<unsigned> MaxNRot("max-nrot",
		cl::desc("maximum allowed rotatable bonds"), cl::init(UINT_MAX));

cl::opt<unsigned> ReduceConfs("reduceconfs",
		cl::desc("return at most n conformations for each molecule"),
		cl::value_desc("n"), cl::init(0));
cl::opt<unsigned> MaxOrient("max-orient",
		cl::desc("return at most n orientations of each conformation"),
		cl::value_desc("n"), cl::init(UINT_MAX));
cl::opt<unsigned> MaxHits("max-hits", cl::desc("return at most n results"),
		cl::value_desc("n"), cl::init(UINT_MAX));
cl::opt<unsigned> Port("port", cl::desc("port for server to listen on"),cl::init(17000));
cl::opt<string> LogDir("logdir", cl::desc("log directory for server"),
		cl::init("."));
cl::opt<bool> ExtraInfo("extra-info",
		cl::desc("Output additional molecular properties.  Slower."),
		cl::init(false));
cl::opt<bool> SortRMSD("sort-rmsd", cl::desc("Sort results by RMSD."),
		cl::init(false));
cl::opt<bool> FilePartition("file-partition",
		cl::desc("Partion database slices based on files"), cl::init(false));

cl::opt<string> MinServer("min-server",cl::desc("minimization server address"));
cl::opt<unsigned> MinPort("min-port",cl::desc("port for minimization server"));

cl::opt<string> Receptor("receptor",
		cl::desc("Receptor file for interaction pharmacophroes"));

cl::opt<string> Prefixes("prefixes",
		cl::desc("[dbcreateserverdir,server] File of directory prefixes to use for striping."));
cl::opt<string> DBInfo("dbinfo",
		cl::desc("[dbcreateserverdir] JSON file describing database subset"));
cl::opt<string> Ligands("ligs", cl::desc("[dbcreateserverdir] Text file listing locations of molecules"));

typedef void (*pharmaOutputFn)(ostream&, vector<PharmaPoint>&, Excluder& excluder);

static void pharmaNoOutput(ostream&, vector<PharmaPoint>&, Excluder& excluder)
{
}

static void pharmaTxtOutput(ostream& out, vector<PharmaPoint>& points, Excluder& excluder)
{
	for (unsigned i = 0, n = points.size(); i < n; i++)
		out << points[i] << "\n";
}

static void pharmaJSONOutput(ostream& out, vector<PharmaPoint>& points, Excluder& excluder)
{
	Json::Value root;
	convertPharmaJson(root, points);
	excluder.addToJSON(root);
	Json::StyledStreamWriter writer;
	writer.write(out, root);
}

static void pharmaSDFOutput(ostream& out, vector<PharmaPoint>& points, Excluder& excluder)
{
	OBConversion conv;
	conv.SetOutFormat("SDF");
	OBMol mol;

	for (int p = 0, np = points.size(); p < np; p++)
	{
		OBAtom *atom = mol.NewAtom();
		atom->SetAtomicNum(points[p].pharma->atomic_number_label);
		atom->SetVector(points[p].x, points[p].y, points[p].z);
	}

	conv.Write(&mol, &out);
}

//identify all the pharma points within each mol in the input file
static void handle_pharma_cmd(const Pharmas& pharmas)
{

	if (outputFiles.size() > 0 && outputFiles.size() != inputFiles.size())
	{
		cerr << "Number of outputs must equal number of inputs.\n";
		exit(-1);
	}

	for (unsigned i = 0, n = inputFiles.size(); i < n; i++)
	{
		string fname = inputFiles[i];
		ifstream in(fname.c_str());
		if (!in)
		{
			cerr << "Error reading file " << fname << "\n";
			exit(-1);
		}

		ofstream out;
		pharmaOutputFn outfn = pharmaNoOutput;
		//output can be plain text, json, or an sdf file (collection of atoms)
		if (outputFiles.size() > 0)
		{
			out.open(outputFiles[i].c_str());
			if (!out)
			{
				cerr << "Error opening output file " << outputFiles[i] << "\n";
				exit(-1);
			}
			string ext = filesystem::extension(outputFiles[i]);
			if (ext == ".txt" || ext == "")
				outfn = pharmaTxtOutput;
			else if (ext == ".json")
				outfn = pharmaJSONOutput;
			else if (ext == ".sdf")
				outfn = pharmaSDFOutput;
			else
			{
				cerr << "Unsupported output format\n";
				exit(-1);
			}
		}

		//special case - convert  query to points
		if (filesystem::extension(fname) == ".json"
				|| filesystem::extension(fname) == ".ph4"
				|| filesystem::extension(fname) == ".query"
				|| filesystem::extension(fname) == ".txt"
				|| filesystem::extension(fname) == ".pml")
		{
			ifstream in(fname.c_str());
			vector<PharmaPoint> points;
			Excluder excluder;

			if (filesystem::extension(fname) == ".json"
					|| filesystem::extension(fname) == ".query")
			{
				JSonQueryParser parser;
				parser.parse(pharmas, in, points, excluder);
			}
			else if (filesystem::extension(fname) == ".ph4")
			{
				PH4Parser parser;
				parser.parse(pharmas, in, points, excluder);
			}
			else if (filesystem::extension(fname) == ".pml")
			{
				PMLParser parser;
				parser.parse(pharmas, in, points, excluder);
			}
			else
			{
				TextQueryParser parser;
				parser.parse(pharmas, in, points, excluder);
			}
			outfn(out, points, excluder);
		}
		else //pharma recognition
		{
			OBConversion conv;
			OBFormat *format = conv.FormatFromExt(fname.c_str());
			if (format == NULL)
			{
				cerr << "Invalid input file format " << fname << "\n";
				exit(-1);
			}
			conv.SetInFormat(format);
			OBMol mol;
			vector<PharmaPoint> points;

			OBMol receptor;
			if (Receptor.size() > 0)
			{
				OBConversion rconv;
				OBFormat *rformat = rconv.FormatFromExt(Receptor.c_str());
				if (format)
				{
					rconv.SetInFormat(rformat);
					ifstream rin(Receptor.c_str());
					rconv.Read(&receptor, &rin);
				}
			}

        		OBAromaticTyper aromatics;
        		OBAtomTyper atyper;
			while (conv.Read(&mol, &in))
			{
				//perform exactly the same analyses as dbcreate
				mol.AddHydrogens();

				mol.FindRingAtomsAndBonds();
				mol.FindChiralCenters();
				mol.PerceiveBondOrders();
				aromatics.AssignAromaticFlags(mol);
				mol.FindSSSR();
				atyper.AssignTypes(mol);
				atyper.AssignHyb(mol);

				if (receptor.NumAtoms() > 0)
				{
					vector<PharmaPoint> screenedout;
					getInteractionPoints(pharmas, receptor, mol, points,
							screenedout);
				}
				else
					getPharmaPoints(pharmas, mol, points);
				Excluder dummy;
				if (!Quiet)
					pharmaTxtOutput(cout, points, dummy);
				outfn(out, points, dummy);
			}
		}
	}
}

//create a database
static void handle_dbcreate_cmd(const Pharmas& pharmas)
{
	if (Database.size() == 0)
	{
		cerr
				<< "Need to specify location of database directory to be created.\n";
		exit(-1);
	}

	//create directories
	for (unsigned i = 0, n = Database.size(); i < n; i++)
	{
		if (!filesystem::create_directory(Database[i]))
		{
			cerr << "Unable to create database directory " << Database[i]
					<< "\n";
			exit(-1);
		}
	}

	//check and setup input
	OBConversion conv;
	unsigned long long numBytes = 0;
	for (unsigned i = 0, n = inputFiles.size(); i < n; i++)
	{

		if (!filesystem::exists(inputFiles[i].c_str()))
		{
			cerr << "Invalid input file: " << inputFiles[i] << "\n";
			exit(-1);
		}
		OBFormat *format = conv.FormatFromExt(inputFiles[i].c_str());
		if (format == NULL)
		{
			cerr << "Invalid input format: " << inputFiles[i] << "\n";
			exit(-1);
		}

		numBytes += filesystem::file_size(inputFiles[i]);
	}

	//create databases
	//openbabel can't handled multithreaded reading, so we actually have to fork off a process
	//for each database
	for (unsigned d = 0, nd = Database.size(); d < nd; d++)
	{
		if (nd == 1 || fork() == 0)
		{
			Json::Value blank;
			PharmerDatabaseCreator db(pharmas, Database[d], blank);
			unsigned long uniqueid = 1;
			//now read files
			unsigned long readBytes = 0;
			for (unsigned i = 0, n = inputFiles.size(); i < n; i++)
			{
				if (FilePartition)
				{
					if (i % nd != d)
						continue;
				}
				ifstream in(inputFiles[i].c_str());
				OBFormat *format = conv.FormatFromExt(inputFiles[i].c_str());
				if (!Quiet)
					cout << "Adding " << inputFiles[i] << "\n";
				unsigned stride = nd;
				unsigned offset = d;
				if (FilePartition)
				{
					stride = 1;
					offset = 0;
				}
				ReadMCMol reader(in, format, stride, offset, ReduceConfs);
				OBMol mol;

				while (reader.read(mol))
				{
					db.addMolToDatabase(mol, uniqueid*nd+d, mol.GetTitle());
					uniqueid++;
				}

				db.writeStats();
				readBytes += filesystem::file_size(inputFiles[i]);
			}

			db.createSpatialIndex();
			exit(0);
		}
	}

	int status;
	while (wait(&status) > 0)
	{
		if (!WIFEXITED(status) && WEXITSTATUS(status) != 0)
			abort();
		continue;
	}

}

//read in ligand file names and verify the files exist
struct LigandInfo
{
	filesystem::path file;
	long id;
	string name;

	LigandInfo(): id(0) {}
};

//create a database directory within the server framework
//in this framework we provide a file of prefixes where each line is
//a location (on a different hard drive) for creating a strip of the overall database
//for input molecules, we provide a file where each line is the location of the conformers
//if a single molecule in an sdf.gz file, also included on the line are the unique id of the molecule and the
//space delimited possible names for that molecule
//we also specify a database description file which is a json file describing the database
//the json object is indexed by database key; the keys define the subdirectory name to use in prefixes
static void handle_dbcreateserverdir_cmd(const Pharmas& pharmas)
{
	ifstream prefixes(Prefixes.c_str());
	if (Prefixes.size() == 0 || !prefixes)
	{
		cerr << "Problem with prefixes.\n";
		exit(-1);
	}

	ifstream dbinfo(DBInfo.c_str());
	if(DBInfo.size() == 0 || !dbinfo)
	{
		cerr << "Problem with database info.\n";
		exit(-1);
	}

	ifstream ligs(Ligands.c_str());
	if(Ligands.size() == 0 || !filesystem::exists(Ligands))
	{
		cerr << "Need ligand file\n";
		exit(-1);
	}

	//parse dbinfo into json
	Json::Value root; // will contains the root value after parsing.
	Json::Reader reader;
	if(!reader.parse(dbinfo, root)) {
		cerr << "Error reading database info JSON\n";
		exit(-1);
	}


	vector<LigandInfo> liginfos;
	string line;
	while(getline(ligs,line))
	{
		stringstream str(line);
		LigandInfo info;

		str >> info.file;

		if(!filesystem::exists(info.file))
		{
			cerr << "File " << info.file << " does not exist\n";
		}
		str >> info.id;
		if(info.id <= 0)
		{
			cerr << "Error in ligand file on line:\n" << line << "\n";
			exit(-1);
		}

		getline(str, info.name); //get rest as name
		liginfos.push_back(info);
	}

	//get key for database, this is the name of the subdir
	if(!root.isMember("subdir"))
	{
		cerr << "Database info needs subdir field.";
	}
	stringstream key;
	key << root["subdir"].asString() << "-" << time(NULL);
	string subset = root["subdir"].asString();

	//create directories
	vector<filesystem::path> directories;
	vector<filesystem::path> symlinks;
	if(!prefixes) {
		cerr << "Error with prefixes file\n";
		exit(-1);
	}

	string fname;
	while(getline(prefixes,fname))
	{
		if(filesystem::exists(fname))
		{
			filesystem::path dbpath(fname);
			dbpath /= key.str();
			filesystem::create_directories(dbpath);
			directories.push_back(dbpath);

			filesystem::path link(fname);
			link /= subset;
			symlinks.push_back(link);
		}
		else
		{
			cerr << "Prefix " << fname << " does not exist\n";
		}
	}

	//multi-thread (fork actually, due to openbabel) across all prefixes

	//create databases
	//openbabel can't handled multithreaded reading, so we actually have to fork off a process
	//for each database
	for (unsigned d = 0, nd = directories.size(); d < nd; d++)
	{
		if (d == (nd-1) || fork() == 0)
		{
			PharmerDatabaseCreator db(pharmas, directories[d], root);
			OBConversion conv;

			//now read files
			for (unsigned i = 0, n = liginfos.size(); i < n; i++)
			{
				if( (i%nd) == d )
				{ //part of our slice
					const LigandInfo info = liginfos[i];
					ifstream in(info.file.c_str());
					OBFormat *format = conv.FormatFromExt(info.file.c_str());

					if(format != NULL)
					{
						ReadMCMol reader(in, format, 1, 0, ReduceConfs);
						OBMol mol;

						while (reader.read(mol))
						{
							db.addMolToDatabase(mol, info.id, info.name);
						}
					}
				}
			}
			db.writeStats();
			db.createSpatialIndex();
			if(d != nd-1)
				exit(0);
		}
	}

	int status;
	while (wait(&status) > 0)
	{
		if (!WIFEXITED(status) && WEXITSTATUS(status) != 0)
			abort();
		continue;
	}

	//all done, create symlinks to non-timestamped directories
	assert(symlinks.size() == directories.size());
	for (unsigned d = 0, nd = directories.size(); d < nd; d++)
	{
		if(filesystem::exists(symlinks[d]))
		{
			//remove preexisting symlink
			if(filesystem::is_symlink(symlinks[d]))
			{
				filesystem::remove(symlinks[d]);
			}
			else
			{
				cerr << "Trying to replace a non-symlink: " << symlinks[d] << "\n";
				exit(-1);
			}
		}
		filesystem::create_directory_symlink(directories[d], symlinks[d]);
	}
}

//thread class for loading database info
struct LoadDatabase
{
	unsigned totalConf;
	unsigned totalMols;

	LoadDatabase() :
			totalConf(0), totalMols(0)
	{

	}

	void operator()( boost::shared_ptr<PharmerDatabaseSearcher>& database, unsigned i,
			filesystem::path dbpath)
	{
		shared_ptr<PharmerDatabaseSearcher> db(new PharmerDatabaseSearcher(dbpath));

		if (!db->isValid())
		{
			cerr << "Error reading database " << Database[i] << "\n";
			exit(-1);
		}
		totalConf += db->numConformations();
		totalMols += db->numMolecules();
		database = db;
	}
};

//load databases based on commandline arguments
static void loadDatabases(vector<filesystem::path>& dbpaths, StripedSearchers& databases)
{
	databases.totalConfs = 0;
	databases.totalMols = 0;
	databases.stripes.resize(dbpaths.size());
	vector<LoadDatabase> loaders(dbpaths.size());
	thread_group loading_threads;
	for (unsigned i = 0, n = dbpaths.size(); i < n; i++)
	{
		if (!filesystem::is_directory(dbpaths[i]))
		{
			cerr << "Invalid database directory path: " << dbpaths[i] << "\n";
			exit(-1);
		}

		loading_threads.add_thread(
				new thread(ref(loaders[i]), ref(databases.stripes[i]), i, dbpaths[i]));
	}
	loading_threads.join_all();

	BOOST_FOREACH(const LoadDatabase& ld, loaders)
	{
		databases.totalConfs += ld.totalConf;
		databases.totalMols += ld.totalMols;
	}
}

//from stack overflow
inline vector<string> glob(const std::string& pat){
    glob_t glob_result;
    glob(pat.c_str(),GLOB_TILDE,NULL,&glob_result);
    vector<string> ret;
    for(unsigned int i=0;i<glob_result.gl_pathc;++i){
        ret.push_back(string(glob_result.gl_pathv[i]));
    }
    globfree(&glob_result);
    return ret;
}

//load striped databases from the specified prefixes
//get the keys for each database from the database json file
static void loadFromPrefixes(vector<filesystem::path>& prefixes, unordered_map<string, StripedSearchers >& databases)
{
	assert(prefixes.size() > 0);
	filesystem::path jsons = prefixes[0] / "*" / "dbinfo.json";
	vector<string> infos = glob(jsons.c_str());

	for(unsigned i = 0, n = infos.size(); i < n; i++)
	{
		filesystem::path subdir(infos[i]);
		subdir = subdir.remove_filename();
		filesystem::path name = subdir.filename();

		Json::Value json;
		Json::Reader reader;
		ifstream info(infos[i].c_str());
		if(!reader.parse(info, json)) {
			cerr << "Error reading database info " << infos[i] << "\n";
			exit(-1);
		}
		if(!json.isMember("subdir")) {
			cerr << "Missing key from database info " << infos[i] << "\n";
			exit(-1);
		}
		string specified = json["subdir"].asString();

		if(specified != name.string())
		{
			cout << "Ignoring " << name << "\n";
			continue;
		}
		else
		{
			cout << "Loading " << name << "\n";
		}

		vector<filesystem::path> dbpaths(prefixes.size());
		for(unsigned p = 0, np = prefixes.size(); p < np; p++)
		{
			filesystem::path dir = prefixes[p] / name;
			dbpaths[p] = dir;
		}

		loadDatabases(dbpaths, databases[specified]);
	}
}

//search the database
static void handle_dbsearch_cmd()
{
	if (pharmaSpec.size() != 0)
	{
		cerr
				<< "Warning: pharmaspec option not valid for database search; database specification will be used instead\n";
	}

	//databases
	if (Database.size() == 0)
	{
		cerr << "Require database directory path.\n";
		exit(-1);
	}

	StripedSearchers databases;
	unsigned totalC = 0;
	unsigned totalM = 0;

	vector<filesystem::path> dbpaths;
	for(unsigned i = 0, n = Database.size(); i < n; i++)
	{
		dbpaths.push_back(filesystem::path(Database[i]));
	}
	loadDatabases(dbpaths, databases);

	if (!Quiet)
		cout << "Searching " << totalC << " conformations of " << totalM
				<< " compounds.\n";

	//query parameters
	QueryParameters params;
	params.maxRMSD = MaxRMSD;
	params.minWeight = MinWeight;
	params.maxWeight = MaxWeight;
	params.minRot = MinNRot;
	params.maxRot = MaxNRot;
	params.reduceConfs = ReduceConfs;
	params.orientationsPerConf = MaxOrient;
	params.maxHits = MaxHits;
	if (SortRMSD)
		params.sort = SortType::RMSD;

	//data parameters
	DataParameters dparams;
	dparams.extraInfo = ExtraInfo;
	dparams.sort = SortType::RMSD;

	//query file
	if (inputFiles.size() < 1)
	{
		cerr << "Need input pharmacophore query file(s).\n";
		exit(-1);
	}

	Timer timer;

	if (outputFiles.size() > 0 && outputFiles.size() != inputFiles.size())
	{
		cerr << "Number of outputs must equal number of inputs\n";
		exit(-1);
	}

	for (unsigned i = 0, n = inputFiles.size(); i < n; i++)
	{

		if (!PharmerQuery::validFormat(filesystem::extension(inputFiles[i])))
		{
			cerr << "Invalid extension for query file: " << inputFiles[i]
					<< "\n";
			exit(-1);
		}
		cout << "Query " << inputFiles[i] << "\n";
		ifstream qfile(inputFiles[i].c_str());
		if (!qfile)
		{
			cerr << "Could not open query file: " << inputFiles[i] << "\n";
			exit(-1);
		}

		PharmerQuery query(databases.stripes, qfile,
				filesystem::extension(inputFiles[i]), params,
				NThreads * databases.stripes.size());

		string err;
		if (!query.isValid(err))
		{
			cerr << err << "\n";
			exit(-1);
		}
		if (ShowQuery)
			query.print(cout);

		query.execute(); //blocking

		if (Print) //dump to stdout
		{
			query.outputData(dparams, cout);
		}

		//output file
		if (outputFiles.size() > 0)
		{
			string outname = outputFiles[i];
			string oext = filesystem::extension(outname);
			ofstream out;

			if (oext != ".sdf" && oext != ".txt" && oext != "" && oext != ".gz")
			{
				cerr << "Invalid output format.  Support only .sdf and .txt\n";
				exit(-1);
			}
			out.open(outname.c_str());
			if (!out)
			{
				cerr << "Could not open output file: " << outname << "\n";
				exit(-1);
			}

			if(oext == ".gz") //assumed to be compressed sdf
			{
				boost::iostreams::filtering_ostream gzout;
				gzout.push(boost::iostreams::gzip_compressor());
				gzout.push(out);
				query.outputMols(gzout);
			}
			else if (oext != ".sdf") //text output
			{
				query.outputData(dparams, out);
			}
			else //mol output
			{
				query.outputMols(out);
			}
		}

		cout << "NumResults: " << query.numResults() << "\n";
	}

	cout << "Time: " << timer.elapsed() << "\n";
}

int main(int argc, char *argv[])
{
	cl::ParseCommandLineOptions(argc, argv);
	obErrorLog.StopLogging(); //just slows us down, possibly buggy?
	//if a pharma specification file was given, load that into the global Pharmas
	Pharmas pharmas(defaultPharmaVec);
	if (pharmaSpec.size() > 0)
	{
		ifstream pharmin(pharmaSpec.c_str());
		if (!pharmas.read(pharmin))
		{
			cerr << "Invalid pharmacophore specification file.\n";
			exit(-1);
		}
	}

	if (Cmd == "pharma")
	{
		handle_pharma_cmd(pharmas);
	}
	else if(Cmd == "showpharma")
	{
		pharmas.write(cout);
	}
	else if (Cmd == "dbcreate")
	{
		handle_dbcreate_cmd(pharmas);
	}
	else if(Cmd == "dbcreateserverdir")
	{
		handle_dbcreateserverdir_cmd(pharmas);
	}
	else if (Cmd == "dbsearch")
	{
		handle_dbsearch_cmd();
	}
	else if (Cmd == "server")
	{
		unordered_map<string, StripedSearchers > databases;

                //total hack time - fcgi uses select which can't
		//deal with file descriptors higher than 1024, so let's reserve some
		#define MAXRESERVEDFD (SERVERTHREADS*2)
		int reservedFD[MAXRESERVEDFD] = {0,};
		for(unsigned i = 0; i < MAXRESERVEDFD; i++)
		{
				reservedFD[i] = open("/dev/null",O_RDONLY);
		}
		//loadDatabases will open a whole bunch of files
		if(Prefixes.length() > 0 && Database.size() > 0)
		{
			cerr << "Cannot specify both dbdir and prefixes\n";
			exit(-1);
		}
		else if(Database.size() > 0)
		{
			//only one subset
			vector<filesystem::path> dbpaths;
			for(unsigned i = 0, n = Database.size(); i < n; i++)
			{
				dbpaths.push_back(filesystem::path(Database[i]));
			}
			loadDatabases(dbpaths, databases[""]);
		}
		else
		{
			//use prefixes
			ifstream prefixes(Prefixes.c_str());
			vector<filesystem::path> dbpaths;
			string line;
			while(getline(prefixes, line))
			{
				if(filesystem::exists(line))
					dbpaths.push_back(filesystem::path(line));
				else
					cerr << line << " does not exist\n";
			}
			if(dbpaths.size() == 0)
			{
				cerr << "No valid prefixes\n";
				exit(-1);
			}
			loadFromPrefixes(dbpaths, databases);
		}
		//now free reserved fds
		for(unsigned i = 0; i < MAXRESERVEDFD; i++)
		{
				close(reservedFD[i]);
		}
		pharmer_server(Port, databases, LogDir, MinServer, MinPort);
	}
	else
	{
		cl::PrintHelpMessage();
		if (Cmd.size() == 0)
			cerr << "Command [pharma, dbcreate, dbsearch] required.\n";
		else
			cerr << Cmd << " not a valid command.\n";
		exit(-1);
	}

	return 0;
}

