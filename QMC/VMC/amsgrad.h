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
#ifndef OPTIMIZER_HEADER_H
#define OPTIMIZER_HEADER_H
#include <Eigen/Dense>
#include <boost/serialization/serialization.hpp>
#include "iowrapper.h"
#include "global.h"
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <math.h>
#ifndef SERIAL
#include "mpi.h"
#include <boost/mpi/environment.hpp>
#include <boost/mpi/communicator.hpp>
#include <boost/mpi.hpp>
#endif

using namespace Eigen;
using namespace boost;

/*
Adaptive stochastic gradient descent optimizer, AMSGrad: arXiv:1904.09237v1
This class is a functor for the AMSGrad optimizer.

*/
class AMSGrad
{
  private:
    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive &ar, const unsigned int version)
    {
       ar  & iter        
           & mom1
           & mom2;
    }

  public:
    double stepsize;
    std::vector<double> stepsizes; //if doing correlated sampling
    double decay_mom1;
    double decay_mom2;

    int maxIter;
    int iter;
    int avgIter;

    VectorXd mom1;
    VectorXd mom2;

    AMSGrad(double pstepsize=0.001, double pdecay_mom1=0.1, double pdecay_mom2=0.001, int pmaxIter=1000, int pavgIter=0) : stepsize(pstepsize), decay_mom1(pdecay_mom1), decay_mom2(pdecay_mom2), maxIter(pmaxIter), avgIter(pavgIter)
    {
        iter = 0;
    }

    AMSGrad(std::vector<double> pstepsizes, double pdecay_mom1=0.1, double pdecay_mom2=0.001, int pmaxIter=1000, int pavgIter=0) : stepsizes(pstepsizes), decay_mom1(pdecay_mom1), decay_mom2(pdecay_mom2), maxIter(pmaxIter), avgIter(pavgIter)
    {
        iter = 0;
    }

    void write(VectorXd& vars)
    {
        if (commrank == 0)
        {
            char file[5000];
            sprintf(file, "AMSGrad.bkp");
            std::ofstream ofs(file, std::ios::binary);
            boost::archive::binary_oarchive save(ofs);
            save << *this;
            save << mom1;
            save << mom2;
            save << vars;
            ofs.close();
        }
    }

    void read(VectorXd& vars)
    {
        if (commrank == 0)
        {
            char file[5000];
            sprintf(file, "AMSGrad.bkp");
            std::ifstream ifs(file, std::ios::binary);
            boost::archive::binary_iarchive load(ifs);
            load >> *this;
            load >> mom1;
            load >> mom2;
            load >> vars;
            ifs.close();
        }
    }

    template<typename Function>
    void optimize(VectorXd &vars, Function& getGradient, bool restart)
    {
        if (restart || schd.fullRestart)
        {
          if (commrank == 0)
          {
            read(vars);
            if (schd.fullRestart)
            {
              mom1 = VectorXd::Zero(vars.rows());
              mom2 = VectorXd::Zero(vars.rows());
            }
          }
#ifndef SERIAL
	    boost::mpi::communicator world;
	    boost::mpi::broadcast(world, *this, 0);
	    boost::mpi::broadcast(world, vars, 0);
#endif
        }
        else if (mom1.rows() == 0) //object is empty
        {
            mom1 = VectorXd::Zero(vars.rows());
            mom2 = VectorXd::Zero(vars.rows());
        }

        VectorXd grad = VectorXd::Zero(vars.rows());
        VectorXd avgVars = VectorXd::Zero(vars.rows());
        VectorXd deltaVars = VectorXd::Zero(vars.rows());
        double stepNorm = 0., angle = 0.;

        while (iter < maxIter)
        {
            //sampling
            double E0, stddev = 0.0, rt = 1.0;
            double acceptedFrac = getGradient(vars, grad, E0, stddev, rt);
            if (commrank == 0 && schd.printGrad) {cout << "totalGrad" << endl; cout << grad << endl;}
            write(vars);
            double oldNorm = stepNorm, dotProduct = 0.;
            stepNorm = 0.;

            if (commrank == 0)
            {
                for (int i = 0; i < vars.rows(); i++)
                {
                    //update momentum
                    mom1[i] = decay_mom1 * grad[i] + (1. - decay_mom1) * mom1[i];
                    mom2[i] = max(mom2[i], decay_mom2 * grad[i]*grad[i] + (1. - decay_mom2) * mom2[i]);   

                    //update variables
                    if (schd.method == amsgrad)
                    {
                      double delta = stepsize * mom1[i] / (pow(mom2[i], 0.5) + 1.e-8);  
                      vars[i] -= delta;
                      stepNorm += delta * delta;
                      dotProduct += delta * deltaVars[i];
                      deltaVars[i] = delta;
                    }
                    else if(schd.method == amsgrad_sgd) //amsgrad-sgd option, performs sgd for the first few iterations
                    {
                        if (iter < schd.sgdIter)
                        {
                            vars[i] -= stepsize * grad[i];
                        }
                        else
                        {
                            double delta = stepsize * mom1[i] / (pow(mom2[i], 0.5) + 1.e-8);  
                            vars[i] -= delta;
                            stepNorm += delta * delta;
                            dotProduct += delta * deltaVars[i];
                            deltaVars[i] = delta;
                        }
                    }                        
                }
                
                //norm and angle of update step
                stepNorm = pow(stepNorm, 0.5);
                if (oldNorm != 0) angle = acos(dotProduct/stepNorm/oldNorm) * 180 / 3.14159265;
            }
#ifndef SERIAL
            MPI_Bcast(&vars[0], vars.rows(), MPI_DOUBLE, 0, MPI_COMM_WORLD);
#endif
            //print
            if (commrank == 0)
            {
              std::cout << format("%5i %14.8f (%8.2e) %14.8f %8.1f %8.1f %10i  %6.6f %8.2f %8.2f\n") % iter % E0 % stddev % (grad.norm()) % (rt) % acceptedFrac % (schd.stochasticIter) % (stepNorm) % (angle) % ((getTime() - startofCalc));
            }

            //average variables
            if (maxIter - iter <= avgIter) avgVars += vars;

            //update iteration
            iter++;
        }//while
        
        if (avgIter != 0) {
          avgVars = avgVars/avgIter;
          write(avgVars);
          double E0, stddev = 0.0, rt = 1.0;
          getGradient(avgVars, grad, E0, stddev, rt);
          if (commrank == 0) {
            std::cout << "Average over last " << avgIter << " iterations" << endl;
            std::cout << format("0 %14.8f (%8.2e) %14.8f %8.1f %10i %8.2f\n")  % E0 % stddev % (grad.norm()) % (rt) % (schd.stochasticIter) % ((getTime() - startofCalc));
          }
        }
    }//optimize

    //same member function as above, with added correlated sampling function
    template<typename Function1, typename Function2>
    void optimize(VectorXd &vars, Function1& getGradient, Function2 &runCorrelatedSampling, bool restart)
    {
        if (restart || schd.fullRestart)
        {
          if (commrank == 0)
          {
            read(vars);
            if (schd.fullRestart)
            {
              mom1 = VectorXd::Zero(vars.rows());
              mom2 = VectorXd::Zero(vars.rows());
            }
          }
#ifndef SERIAL
	    boost::mpi::communicator world;
	    boost::mpi::broadcast(world, *this, 0);
	    boost::mpi::broadcast(world, vars, 0);
#endif
        }
        else if (mom1.rows() == 0) //object is empty
        {
            mom1 = VectorXd::Zero(vars.rows());
            mom2 = VectorXd::Zero(vars.rows());
        }

        VectorXd grad = VectorXd::Zero(vars.rows());
        double stepNorm = 0., angle = 0.;
        while (iter < maxIter)
        {
            double E0, stddev = 0.0, rt = 1.0;
            getGradient(vars, grad, E0, stddev, rt);
            write(vars);
            VectorXd update = VectorXd::Zero(vars.rows());

            //update variables and momentum
            for (int i = 0; i < vars.rows(); i++)
            {
                mom1[i] = decay_mom1 * grad[i] + (1. - decay_mom1) * mom1[i];
                mom2[i] = max(mom2[i], decay_mom2 * grad[i]*grad[i] + (1. - decay_mom2) * mom2[i]);   
                update[i] = mom1[i] / (pow(mom2[i], 0.5) + 1.e-8);  
            }

            //run correlated sampling
            std::vector<Eigen::VectorXd> V(stepsizes.size(), vars);
            std::vector<double> E(stepsizes.size(), 0.0);
            for (int i = 0; i < V.size(); i++)
            {
              V[i] -= stepsizes[i] * update;
            }
            runCorrelatedSampling(V, E);
            //find lowest energy
            int index = 0;
            for (int i = 0; i < E.size(); i++)
            {
              if (E[i] < E[index])
                index = i;              
            }
            //update variables
            vars = V[index];

            if (schd.printOpt && commrank == 0)
            {
              cout << "Correlated Sampling: " << endl;
              for (int i = 0; i < E.size(); i++)
              {
                cout << stepsizes[i] << " " << E[i] << "|";
              }
              cout << endl;
            }
#ifndef SERIAL
            MPI_Bcast(&vars[0], vars.rows(), MPI_DOUBLE, 0, MPI_COMM_WORLD);
#endif
            //print
            if (commrank == 0)
            {
              std::cout << format("%5i %14.8f (%8.2e) %14.8f %8.1f %10i  %6.6f\n") % iter % E0 % stddev % (grad.norm()) % (rt) % (schd.stochasticIter) % ((getTime() - startofCalc));
            }

            //update iteration
            iter++;
        }//while
    }//optimize
};
#endif
