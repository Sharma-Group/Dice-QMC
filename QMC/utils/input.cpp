/*
  Developed by Sandeep Sharma
  Copyright (c) 2017, Sandeep Sharma
  
  This file is part of DICE.
  
  This program is free software: you can redistribute it and/or modify it under the terms
  of the GNU General Public License as published by the Free Software Foundation, 
  either version 3 of the License, or (at your option) any later version.
  
  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  
  See the GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License along with this program. 
  If not, see <http://www.gnu.org/licenses/>.
*/
#include "input.h"
#include "CPS.h"
#include "global.h"
#include "Determinants.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include "readSlater.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>


#ifndef SERIAL
#include "mpi.h"
#include <boost/mpi/environment.hpp>
#include <boost/mpi/communicator.hpp>
#include <boost/mpi.hpp>
#endif

using namespace Eigen;
using namespace boost;
using namespace std;


void readInput(string inputFile, schedule& schd, bool print) {
  if (commrank == 0) {
    
    property_tree::iptree input;
    property_tree::read_json(inputFile, input);
    
    if (print)	{
      cout << "**************************************************************" << endl;
      cout << "Input file  :" << endl;
      cout << "**************************************************************" << endl;
      //print input file
      stringstream ss;
      property_tree::json_parser::write_json(ss, input);
      cout << ss.str() << endl;
    }

    //check for realspace block, containing only system info, wave function info still in that block
    optional< property_tree::iptree& > realspace = input.get_child_optional("realspace");
    if (realspace) {
      //basis
      string basis = input.get("realspace.basis", "sto");
      if (basis == "gto") {
        schd.walkerBasis = REALSPACEGTO;
        schd.basis = boost::shared_ptr<Basis>(new gaussianBasis);
        schd.basis->read();
        readGeometry(schd.Ncoords, schd.Ncharge, schd.Nbasis, dynamic_cast<gaussianBasis&>(*schd.basis));
      }
      if (basis == "sto") {
        schd.walkerBasis = REALSPACESTO;
        //read gaussian basis just to read the nuclear charge and coordinates
        gaussianBasis gBasis ;
        gBasis.read();
        readGeometry(schd.Ncoords, schd.Ncharge, schd.Nbasis, gBasis);
        schd.basis = boost::shared_ptr<Basis>(new slaterBasis);
        map<string, Vector3d> atomList;
        for (int i=0; i<schd.Ncoords.size(); i++) {
          dynamic_cast<slaterBasis*>(&(*schd.basis))->atomName.push_back(slaterParser::AtomSymbols[schd.Ncharge[i]]);
          dynamic_cast<slaterBasis*>(&(*schd.basis))->atomCoord.push_back(schd.Ncoords[i]);
        }
        //dynamic_cast<slaterBasis*>(&(*schd.basis))->atomList = atomList;
        schd.basis->read();
      }
      
      //electrons, orbs
      schd.nalpha = input.get("realspace.nalpha", -1);
      schd.nbeta = input.get("realspace.nbeta", -1);
      schd.norbs = input.get("realspace.norbs", 0);
      
      //pseudopotential
      schd.pseudo = boost::shared_ptr<Pseudopotential>(new Pseudopotential);
      schd.nGrid = input.get("realspace.nGrid", 5);
      string pQuad = input.get("realspace.pseudo", "tetrahedral");
      if (pQuad == "tetrahedral") schd.pQuad = tetrahedral;
      else if (pQuad == "octahedral") schd.pQuad = octahedral;
      else if (pQuad == "icosahedral") schd.pQuad = icosahedral;
    }
    else schd.walkerBasis = ORBITALS;

    //minimal checking for correctness
    //wavefunction
    schd.wavefunctionType = algorithm::to_lower_copy(input.get("wavefunction.name", "jastrowslater"));
    
    //correlatedwavefunction options
    schd.hf = algorithm::to_lower_copy(input.get("wavefunction.hfType", "rhf"));
    schd.ifComplex = input.get("wavefunction.complex", false);
    schd.uagp = input.get("wavefunction.uagp", false); 
    optional< property_tree::iptree& > child = input.get_child_optional("wavefunction.correlators");
    if (child) {
      for (property_tree::iptree::value_type &correlator : input.get_child("wavefunction.correlators")) {
        int siteSize = stoi(correlator.first);
        string file = correlator.second.data();
	    schd.correlatorFiles[siteSize] = file;
      }
    }

    //resonating wave function
    schd.numResonants = input.get("wavefunction.numResonants", 1);
    schd.singleJastrow = input.get("wavefunction.singleJastrow", true);
    schd.readTransOrbs = input.get("wavefunction.readTransOrbs", true);
   
    //noci
    schd.nNociSlater = input.get("wavefunction.nNociSlater", 1); //this is really the same as numResonants

    // permuted wave function
    schd.numPermutations = input.get("wavefunction.numPermutations", 1);

    //ci and lanczos
    schd.nciAct = input.get("wavefunction.numAct", -1);
    schd.overlapCutoff = input.get("wavefunction.overlapCutoff", 1.e-5);
    if (schd.wavefunctionType == "sci") schd.ciCeption = true;
    else schd.ciCeption = false;
    schd.determinantFile = input.get("wavefunction.determinants", ""); //used for both sci and starting det
    schd.alpha = input.get("wavefunction.alpha", 0.01); //lanczos

    //rbm
    schd.numHidden = input.get("wavefunction.numHidden", 1);

    //realspace
    schd.fourBodyJastrow = false;
    optional< property_tree::iptree& > fbj = input.get_child_optional("wavefunction.fourBodyJastrow");
    if (fbj) {
      schd.fourBodyJastrow = true;
      string fbjBasis = input.get("wavefunction.fourBodyJastrow", "NC");
      if (fbjBasis == "NC") schd.fourBodyJastrowBasis = NC;
      else if (fbjBasis == "AB") schd.fourBodyJastrowBasis = AB;
      else if (fbjBasis == "SS") schd.fourBodyJastrowBasis = SS;
    }
    schd.Qmax = input.get("wavefunction.Qmax", 6);
    schd.QmaxEEN = input.get("wavefunction.QmaxEEN", 3);

    //hamiltonian
    string hamString = algorithm::to_lower_copy(input.get("hamiltonian", "abinitio"));
    if (hamString == "abinitio") schd.Hamiltonian = ABINITIO;
    else if (hamString == "hubbard") schd.Hamiltonian = HUBBARD;
   

    //sampling
    schd.epsilon = input.get("sampling.epsilon", 1.e-7);
    schd.screen = input.get("sampling.screentol", 1.e-8);
    schd.ctmc = input.get("sampling.ctmc", true); //if this is false, metropolis is used!
    schd.deterministic = input.get("sampling.deterministic", false);
    schd.stochasticIter = input.get("sampling.stochasticIter", 1e4);
    schd.integralSampleSize = input.get("sampling.integralSampleSize", 10);
    schd.seed = input.get("sampling.seed", getTime());
    
    //gfmc 
    schd.maxIter = input.get("sampling.maxIter", 50); //note: parameter repeated in optimizer for vmc
    schd.nwalk = input.get("sampling.nwalk", 100);
    schd.tau = input.get("sampling.tau", 0.001);
    schd.fn_factor = input.get("sampling.fn_factor", 1.0);
    schd.nGeneration = input.get("sampling.nGeneration", 30.0);
    
    //FCIQMC options
    schd.nAttemptsEach = input.get("sampling.nAttemptsEach", 1);
    schd.mainMemoryFac = input.get("sampling.mainMemoryFac", 5.0);
    schd.spawnMemoryFac = input.get("sampling.spawnMemoryFac", 5.0);
    schd.shiftDamping = input.get("sampling.shiftDamping", 0.01);
    schd.initialShift = input.get("sampling.initialShift", 0.0);
    schd.minSpawn = input.get("sampling.minSpawn", 0.01);
    schd.minPop = input.get("sampling.minPop", 1.0);
    schd.initialPop = input.get("sampling.initialPop", 100.0);
    schd.targetPop = input.get("sampling.targetPop", 1000.0);

    //realspace
    schd.realSpaceStep = input.get("sampling.realSpaceStep", 0.1);
    string stepType = input.get("sampling.rStepType", "spherical");
    if (stepType == "spherical") schd.rStepType = SPHERICAL;
    else if (stepType == "simple") schd.rStepType = SIMPLE;
    else if (stepType == "dmc") schd.rStepType = DMC;
    else if (stepType == "gaussian") schd.rStepType = GAUSSIAN;

    //trans
    schd.nMaxMacroIter = input.get("sampling.macroIter", 1);
    schd.nMaxMicroIter = input.get("sampling.microIter", 1);
    schd.maxMacroIter = input.get("sampling.maxMacroIter", 1);
 

    //optimization
    string method = algorithm::to_lower_copy(input.get("optimizer.method", "amsgrad")); 
    if (method == "amsgrad") schd.method = amsgrad;
    else if (method == "amsgrad_sgd") schd.method = amsgrad_sgd;
    else if (method == "sgd") schd.method = sgd;
    else if (method == "sr") schd.method = sr;
    else if (method == "lm") {
      schd.method = linearmethod;
      //unfortunately this has to be done here for amsgrad to work correctly
      schd.stepsizes = {0.1, 0.01, 1.0};
      child = input.get_child_optional("optimizer.stepsizes");
      if (child) {
        schd.stepsizes.resize(0);
        for (property_tree::iptree::value_type &step : input.get_child("optimizer.stepsizes")) {
          schd.stepsizes.push_back(stod(step.second.data()));
        }
      }
    }

    //general options
    schd.restart = input.get("optimizer.restart", false);
    schd.fullRestart = input.get("optimizer.fullRestart", false);
    child = input.get_child_optional("sampling.maxIter"); //to ensure maxiter is not reassigned
    if (!child) schd.maxIter = input.get("optimizer.maxIter", 50);
    schd.avgIter = input.get("optimizer.avgIter", 0);
    schd.stepsize = input.get("optimizer.stepsize", 0.001);
    schd.optimizeOrbs = input.get("optimizer.optimizeOrbs", true);
    schd.optimizeCps = input.get("optimizer.optimizeCps", true);
    schd.optimizeJastrow = input.get("optimizer.optimizeJastrow", true);//this is only used in jrbm, doesn't affect jslater
    schd.optimizeRBM = input.get("optimizer.optimizeRBM", true);
    schd.optimizeBackflow = input.get("optimizer.optimizeBackflow", true);
    
    // amsgrad, sgd
    schd.decay2 = input.get("optimizer.decay2", 0.001);
    schd.decay1 = input.get("optimizer.decay1", 0.1);
    schd.momentum = input.get("optimizer.momentum", 0.);
    
    //lm, sr options
    schd.cgIter = input.get("optimizer.cgIter", 20);
    schd.sDiagShift = input.get("optimizer.sDiagShift", 0.0);
    schd.hDiagShift = input.get("optimizer.hDiagShift", 0.1);
    schd.decay = input.get("optimizer.decay", 0.65);
    schd.sgdIter = input.get("optimizer.sgdIter", 1);
    schd.sgdStepsize = input.get("sgdStepsize", 0.1); 
    schd.CorrSampleFrac = input.get("optimizer.corrSampleFrac", 0.35);
    schd.direct = input.get("optimizer.direct", true);
    schd.dTol = input.get("optimizer.dTol", 1.e-3);
    schd.cgTol = input.get("optimizer.cgTol", 1.e-3);
    schd.tol = input.get("tol", 0.); 

    //lm for ci
    schd.diagMethod = input.get("optimizer.diagMethod", "power");
    schd.powerShift = input.get("optimizer.powerShift", 10);


    //debug and print options
    schd.printLevel = input.get("print.level", 0);
    schd.printVars = input.get("print.vars", false);
    schd.printGrad = input.get("print.grad", false);
    schd.debug = input.get("print.debug", false);
    
    //deprecated
    schd.actWidth = input.get("wavefunction.actWidth", 100);
    schd.numActive = input.get("wavefunction.numActive", -1);
    schd.expCorrelator = input.get("wavefunction.expCorrelator", false); 
    schd.PTlambda = input.get("PTlambda", 0.);
    schd.beta = input.get("beta", 1.);
    schd.excitationLevel = input.get("excitationLevel", 1);
    schd.sgdStepsize = input.get("optimizer.sgdStepsize", 0.1); 
    schd.doHessian = input.get("optimizer.doHessian", false);
    
  }

#ifndef SERIAL
  boost::mpi::communicator world;
  mpi::broadcast(world, schd, 0);
#endif
}

void readCorrelator(const std::pair<int, std::string>& p,
		    std::vector<Correlator>& correlators) {
  readCorrelator(p.second, p.first, correlators);
}

void readCorrelator(std::string input, int correlatorSize,
		    std::vector<Correlator>& correlators) {
  ifstream dump(input.c_str());

  while (dump.good()) {

    std::string
      Line;
    std::getline(dump, Line);
    trim(Line);
    vector<string> tok;
    boost::split(tok, Line, is_any_of(", \t\n"), token_compress_on);

    string ArgName = *tok.begin();

    //if (dump.eof())
    //break;
    if (!ArgName.empty() && (boost::iequals(tok[0].substr(0,1), "#"))) continue;
    if (ArgName.empty()) continue;
    
    if (tok.size() != correlatorSize) {
      cout << "Something wrong in line : "<<Line<<endl;
      exit(0);
    }

    vector<int> asites, bsites;
    for (int i=0; i<correlatorSize; i++) {
      int site = atoi(tok[i].c_str());
      asites.push_back(site);
      bsites.push_back(site);
    }
    correlators.push_back(Correlator(asites, bsites));
  }
}


void readHF(MatrixXd& HfmatrixA, MatrixXd& HfmatrixB, std::string hf) 
{
  if (hf == "rhf" || hf == "ghf") {
    ifstream dump("hf.txt");
    for (int i = 0; i < HfmatrixA.rows(); i++) {
      for (int j = 0; j < HfmatrixA.rows(); j++){
        dump >> HfmatrixA(i, j);
	    HfmatrixB(i, j) = HfmatrixA(i, j);
      }
    }
  }
  else {
      ifstream dump("hf.txt");
      for (int i = 0; i < HfmatrixA.rows(); i++)
	{
	  for (int j = 0; j < HfmatrixA.rows(); j++)
	    dump >> HfmatrixA(i, j);
	  for (int j = 0; j < HfmatrixB.rows(); j++)
	    dump >> HfmatrixB(i, j);
	}
    }
/*
  if (schd.optimizeOrbs) {
    //double scale = pow(1.*HfmatrixA.rows(), 0.5);
    double scale = 0.05 * HfmatrixA.maxCoeff();
    HfmatrixA += scale * MatrixXd::Random(HfmatrixA.rows(), HfmatrixA.cols());
    HfmatrixB += scale * MatrixXd::Random(HfmatrixB.rows(), HfmatrixB.cols());
  }
*/
}
void readHF(MatrixXcd& HfmatrixA, MatrixXcd& HfmatrixB, std::string hf) 
{
  if (hf == "rhf" || hf == "ghf") {
    ifstream dump("hf.txt");
    for (int i = 0; i < HfmatrixA.rows(); i++) {
      for (int j = 0; j < HfmatrixA.rows(); j++){
        dump >> HfmatrixA(i, j);
	    HfmatrixB(i, j) = HfmatrixA(i, j);
      }
    }
  }
  else {
      ifstream dump("hf.txt");
      for (int i = 0; i < HfmatrixA.rows(); i++)
	{
	  for (int j = 0; j < HfmatrixA.rows(); j++)
	    dump >> HfmatrixA(i, j);
	  for (int j = 0; j < HfmatrixB.rows(); j++)
	    dump >> HfmatrixB(i, j);
	}
    }
/*
  if (schd.optimizeOrbs) {
    //double scale = pow(1.*HfmatrixA.rows(), 0.5);
    double scale = 0.05 * HfmatrixA.maxCoeff();
    HfmatrixA += scale * MatrixXd::Random(HfmatrixA.rows(), HfmatrixA.cols());
    HfmatrixB += scale * MatrixXd::Random(HfmatrixB.rows(), HfmatrixB.cols());
  }
*/
}

void readGeometry(vector<Vector3d>& Ncoords,
                  vector<double>  & Ncharge,
                  vector<int> & Nbasis,
                  gaussianBasis& gBasis) {
  int N = gBasis.natm;
  Ncoords.resize(N);
  Ncharge.resize(N);
  Nbasis.assign(N, 0);

  int stride = gBasis.atm.size()/N;
  for (int i=0; i<N; i++) {
    Ncharge[i] = gBasis.atm[i*stride];
    Ncoords[i][0] = gBasis.env[ gBasis.atm[i*stride+1] +0];
    Ncoords[i][1] = gBasis.env[ gBasis.atm[i*stride+1] +1];
    Ncoords[i][2] = gBasis.env[ gBasis.atm[i*stride+1] +2];
  }

  for (int i = 0; i < gBasis.nbas; i++) {
    int index = gBasis.bas[i * 8];
    int l = gBasis.bas[i * 8 + 1];
    int n = gBasis.bas[i * 8 + 3];
    Nbasis[index] += n * (2 * l + 1);
    if (l == 2) Nbasis[index] += 1; //we use a cartesian basis, this results in 6 d orbitals, not 5
  }
}

void readPairMat(MatrixXd& pairMat) 
{
  ifstream dump("pairMat.txt");
  for (int i = 0; i < pairMat.rows(); i++) {
    for (int j = 0; j < pairMat.rows(); j++){
      dump >> pairMat(i, j);
    }
  }
}

void readMat(MatrixXd& mat, std::string fileName) 
{
  ifstream dump(fileName);
  for (int i = 0; i < mat.rows(); i++) {
    for (int j = 0; j < mat.cols(); j++){
      dump >> mat(i, j);
    }
  }
}

void readMat(MatrixXcd& mat, std::string fileName) 
{
  ifstream dump(fileName);
  for (int i = 0; i < mat.rows(); i++) {
    for (int j = 0; j < mat.cols(); j++){
      dump >> mat(i, j);
    }
  }
}

void readDeterminants(std::string input, vector<Determinant> &determinants,
                      vector<double> &ciExpansion)
{
  ifstream dump(input.c_str());
  while (dump.good())
    {
      std::string Line;
      std::getline(dump, Line);

      trim_if(Line, is_any_of(", \t\n"));
      
      vector<string> tok;
      boost::split(tok, Line, is_any_of(", \t\n"), token_compress_on);

      if (tok.size() > 2 )
	{
	  ciExpansion.push_back(atof(tok[0].c_str()));
	  determinants.push_back(Determinant());
	  Determinant& det = *determinants.rbegin();
	  for (int i=0; i<Determinant::norbs; i++) 
	    {
	      if (boost::iequals(tok[1+i], "2")) 
		{
		  det.setoccA(i, true);
		  det.setoccB(i, true);
		}
	      else if (boost::iequals(tok[1+i], "a")) 
		{
		  det.setoccA(i, true);
		  det.setoccB(i, false);
		}
	      if (boost::iequals(tok[1+i], "b")) 
		{
		  det.setoccA(i, false);
		  det.setoccB(i, true);
		}
	      if (boost::iequals(tok[1+i], "0")) 
		{
		  det.setoccA(i, false);
		  det.setoccB(i, false);
		}
	    }

	  //***************I AM USING alpha-beta format here, but the wavefunction is coming from Dice that uses alpha0 beta0 alpha1 beta1... format
	  //So the signs need to be adjusted appropriately
	  //cout << det<<"   "<<getParityForDiceToAlphaBeta(det)<<endl;
	  *ciExpansion.rbegin() *= getParityForDiceToAlphaBeta(det);
	}
    }
}
