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
#include <algorithm>
#include <random>
#include <chrono>
#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include <iostream>
#include <Eigen/Dense>
#include <Eigen/Core>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#ifndef SERIAL
#include "mpi.h"
#include <boost/mpi/environment.hpp>
#include <boost/mpi/communicator.hpp>
#include <boost/mpi.hpp>
#endif
#include <boost/function.hpp>
#include <boost/functional.hpp>
#include <boost/bind.hpp>
#include "input.h"
#include "integral.h"
#include "SHCIshm.h"
#include "math.h"
#include "runTranscorrelated.h"
#include "Jastrow.h"
#include "CorrelatedWavefunction.h"
#include "evaluateE.h"

using namespace Eigen;
using namespace boost;
using namespace std;


int main(int argc, char *argv[])
{

#ifndef SERIAL
  boost::mpi::environment env(argc, argv);
  boost::mpi::communicator world;
#endif
  startofCalc = getTime();

  initSHM();
  //license();

  cout.precision(10);
  string inputFile = "input.dat";
  if (argc > 1)
    inputFile = string(argv[1]);
  readInput(inputFile, schd, false);

  generator = std::mt19937(schd.seed + commrank);

  if (schd.walkerBasis == ORBITALS) {
    readIntegralsAndInitializeDeterminantStaticVariables("FCIDUMP");
    
    
    if (schd.wavefunctionType == "JastrowSlater") {
      CorrelatedWavefunction<Jastrow, Slater> wave;
      runTranscorrelated(wave, false);
    }
    else {
      CorrelatedWavefunction<Gutzwiller, Slater> wave;
      runTranscorrelated(wave, true);
    }
  }    

  boost::interprocess::shared_memory_object::remove(shciint2.c_str());
  boost::interprocess::shared_memory_object::remove(shciint2shm.c_str());
  return 0;
}
