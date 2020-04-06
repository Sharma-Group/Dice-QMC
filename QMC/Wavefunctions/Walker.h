/*
  Developed by Sandeep Sharma with contributions from James E. T. Smith and Adam A. Holmes, 2017
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
#ifndef HFWalker_HEADER_H
#define HFWalker_HEADER_H

#include "Determinants.h"
#include "WalkerHelper.h"
#include "CorrelatedWavefunction.h"
#include <array>
#include "igl/slice.h"
#include "igl/slice_into.h"
#include "Slater.h"
#include "AGP.h"
#include "Pfaffian.h"
#include "workingArray.h"
#include "LocalEnergy.h"
#include <unordered_set>

using namespace Eigen;

/**
 * Is essentially a single determinant used in the VMC/DMC simulation
 * At each step in VMC one need to be able to calculate the following
 * quantities
 * a. The local energy = <walker|H|Psi>/<walker|Psi>
 * b. The gradient     = <walker|H|Psi_t>/<walker/Psi>
 * c. The update       = <walker'|Psi>/<walker|Psi>
 *
 * To calculate these efficiently the walker uses the HFWalkerHelper class
 *
 **/

template<typename Corr, typename Reference>
struct Walker { };

template<typename Corr>
struct Walker<Corr, Slater> {

  Determinant d;
  WalkerHelper<Corr> corrHelper;
  WalkerHelper<Slater> refHelper;
  unordered_set<int> excitedOrbs;     //spin orbital indices of excited electrons (in virtual orbitals) in d 

  Walker() {};
  
  Walker(Corr &corr, const Slater &ref) 
  {
    initDet(ref.getHforbsA().real(), ref.getHforbsB().real());
    refHelper = WalkerHelper<Slater>(ref, d);
    corrHelper = WalkerHelper<Corr>(corr, d);
  }

  Walker(Corr &corr, const Slater &ref, const Determinant &pd) : d(pd), refHelper(ref, pd), corrHelper(corr, pd) {}; 

  Determinant& getDet() {return d;}
  void readBestDeterminant(Determinant& d) const 
  {
    if (commrank == 0) {
      char file[5000];
      sprintf(file, "BestDeterminant.txt");
      std::ifstream ifs(file, std::ios::binary);
      boost::archive::binary_iarchive load(ifs);
      load >> d;
    }
#ifndef SERIAL
    MPI_Bcast(&d.reprA, DetLen, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(&d.reprB, DetLen, MPI_DOUBLE, 0, MPI_COMM_WORLD);
#endif
  }

  /**
   * makes det based on mo coeffs 
   */
  void guessBestDeterminant(Determinant& d, const Eigen::MatrixXd& HforbsA, const Eigen::MatrixXd& HforbsB) const 
  {
    int norbs = Determinant::norbs;
    int nalpha = Determinant::nalpha;
    int nbeta = Determinant::nbeta;

    d = Determinant();
    if (boost::iequals(schd.determinantFile, "")) {
      for (int i = 0; i < nalpha; i++) {
        int bestorb = 0;
        double maxovlp = 0;
        for (int j = 0; j < norbs; j++) {
          if (abs(HforbsA(j, i)) > maxovlp && !d.getoccA(j)) {
            maxovlp = abs(HforbsA(j, i));
            bestorb = j;
          }
        }
        d.setoccA(bestorb, true);
      }
      for (int i = 0; i < nbeta; i++) {
        int bestorb = 0;
        double maxovlp = 0;
        for (int j = 0; j < norbs; j++) {
          if (schd.hf == "rhf" || schd.hf == "uhf") {
            if (abs(HforbsB(j, i)) > maxovlp && !d.getoccB(j)) {
              bestorb = j;
              maxovlp = abs(HforbsB(j, i));
            }
          }
          else {
            if (abs(HforbsB(j+norbs, i+nalpha)) > maxovlp && !d.getoccB(j)) {
              bestorb = j;
              maxovlp = abs(HforbsB(j+norbs, i+nalpha));
            }
          }
        }
        d.setoccB(bestorb, true);
      }
    }
    else if (boost::iequals(schd.determinantFile, "bestDet")) {
      std::vector<Determinant> dets;
      std::vector<double> ci;
      readDeterminants(schd.determinantFile, dets, ci);
      d = dets[0];
    }
  }

  void initDet(const MatrixXd& HforbsA, const MatrixXd& HforbsB) 
  {
    bool readDeterminant = false;
    char file[5000];
    sprintf(file, "BestDeterminant.txt");

    {
      ifstream ofile(file);
      if (ofile)
        readDeterminant = true;
    }
    if (readDeterminant)
      readBestDeterminant(d);
    else
      guessBestDeterminant(d, HforbsA, HforbsB);
  }

  double getIndividualDetOverlap(int i) const
  {
    return (refHelper.thetaDet[i][0] * refHelper.thetaDet[i][1]).real();
  }

  double getDetOverlap(const Slater &ref) const
  {
    double ovlp = 0.0;
    for (int i = 0; i < refHelper.thetaDet.size(); i++) {
      ovlp += ref.getciExpansion()[i] * (refHelper.thetaDet[i][0] * refHelper.thetaDet[i][1]).real();
    }
    return ovlp;
  }

  double getDetFactor(int i, int a, const Slater &ref) const 
  {
    if (i % 2 == 0)
      return getDetFactor(i / 2, a / 2, 0, ref);
    else                                   
      return getDetFactor(i / 2, a / 2, 1, ref);
  }

  double getDetFactor(int I, int J, int A, int B, const Slater &ref) const 
  {
    if (I % 2 == J % 2 && I % 2 == 0)
      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 0, 0, ref);
    else if (I % 2 == J % 2 && I % 2 == 1)                  
      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 1, 1, ref);
    else if (I % 2 != J % 2 && I % 2 == 0)                  
      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 0, 1, ref);
    else                                                    
      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 1, 0, ref);
  }

  double getDetFactor(int i, int a, bool sz, const Slater &ref) const
  {
    int tableIndexi, tableIndexa;
    refHelper.getRelIndices(i, tableIndexi, a, tableIndexa, sz); 

    double detFactorNum = 0.0;
    double detFactorDen = 0.0;
    for (int j = 0; j < ref.getDeterminants().size(); j++)
    {
      double factor = (refHelper.rTable[j][sz](tableIndexa, tableIndexi) * refHelper.thetaDet[j][0] * refHelper.thetaDet[j][1]).real() * ref.getciExpansion()[j] /  getDetOverlap(ref);
      detFactorNum += ref.getciExpansion()[j] * factor * (refHelper.thetaDet[j][0] * refHelper.thetaDet[j][1]).real();
      detFactorDen += ref.getciExpansion()[j] * (refHelper.thetaDet[j][0] * refHelper.thetaDet[j][1]).real();
    }
    return detFactorNum / detFactorDen;
  }
  
  double getDetFactor(int i, int j, int a, int b, bool sz1, bool sz2, const Slater &ref) const
  {
    int tableIndexi, tableIndexa, tableIndexj, tableIndexb;
    refHelper.getRelIndices(i, tableIndexi, a, tableIndexa, sz1); 
    refHelper.getRelIndices(j, tableIndexj, b, tableIndexb, sz2) ;

    double detFactorNum = 0.0;
    double detFactorDen = 0.0;
    for (int j = 0; j < ref.getDeterminants().size(); j++)
    {
      double factor;
      if (sz1 == sz2 || refHelper.hftype == 2)
        factor =((refHelper.rTable[j][sz1](tableIndexa, tableIndexi) * refHelper.rTable[j][sz1](tableIndexb, tableIndexj) 
            - refHelper.rTable[j][sz1](tableIndexb, tableIndexi) *refHelper.rTable[j][sz1](tableIndexa, tableIndexj)) * refHelper.thetaDet[j][0] * refHelper.thetaDet[j][1]).real() * ref.getciExpansion()[j]/ getDetOverlap(ref);
      else
        factor = (refHelper.rTable[j][sz1](tableIndexa, tableIndexi) * refHelper.rTable[j][sz2](tableIndexb, tableIndexj) * refHelper.thetaDet[j][0] * refHelper.thetaDet[j][1]).real() * ref.getciExpansion()[j]/ getDetOverlap(ref);
      detFactorNum += ref.getciExpansion()[j] * factor * (refHelper.thetaDet[j][0] * refHelper.thetaDet[j][1]).real();
      detFactorDen += ref.getciExpansion()[j] * (refHelper.thetaDet[j][0] * refHelper.thetaDet[j][1]).real();
    }
    return detFactorNum / detFactorDen;
  }
 
  //only works for ghf
  double getDetFactor(std::array<unordered_set<int>, 2> &from, std::array<unordered_set<int>, 2> &to) const
  {
    if (from[0].size() + from[1].size() == 0) return 1.;
    int numExc = from[0].size() + from[1].size();
    VectorXi tableIndicesRow = VectorXi::Zero(from[0].size() + from[1].size());
    VectorXi tableIndicesCol = VectorXi::Zero(from[0].size() + from[1].size());
    Determinant dcopy = d;
    double parity = 1.;
    int count = 0;
    for (int sz = 0; sz < 2; sz++) {//iterate over spins
      auto itFrom = from[sz].begin();
      auto itTo = to[sz].begin();
      for (int n = 0; n < from[sz].size(); n++) {//iterate over excitations
        int i = *itFrom, a = *itTo;
        itFrom = std::next(itFrom); itTo = std::next(itTo);
        refHelper.getRelIndices(i, tableIndicesCol(count), a, tableIndicesRow(count), sz);
        count++;
        parity *= dcopy.parity(a, i, sz);
        dcopy.setocc(i, sz, false);  
        dcopy.setocc(a, sz, true);
      }
    }

    MatrixXcd detSlice = MatrixXcd::Zero(numExc,numExc);
    igl::slice(refHelper.rTable[0][0], tableIndicesRow, tableIndicesCol, detSlice);
    complex<double> det(0.,0.);
    if (detSlice.rows() == 1) det = detSlice(0, 0);
    else if (detSlice.rows() == 2) det = detSlice(0, 0) * detSlice(1, 1) - detSlice(0, 1) * detSlice(1, 0);
    else if (detSlice.rows() == 3) det =   detSlice(0, 0) * (detSlice(1, 1) * detSlice(2, 2) - detSlice(1, 2) * detSlice(2, 1))
                                         - detSlice(0, 1) * (detSlice(1, 0) * detSlice(2, 2) - detSlice(1, 2) * detSlice(2, 0))
                                         + detSlice(0, 2) * (detSlice(1, 0) * detSlice(2, 1) - detSlice(1, 1) * detSlice(2, 0));
    else if (detSlice.rows() == 4) det = detSlice(0,3) * detSlice(1,2) * detSlice(2,1) * detSlice(3,0) - detSlice(0,2) * detSlice(1,3) * detSlice(2,1) * detSlice(3,0) -
       detSlice(0,3) * detSlice(1,1) * detSlice(2,2) * detSlice(3,0) + detSlice(0,1) * detSlice(1,3) * detSlice(2,2) * detSlice(3,0) +
       detSlice(0,2) * detSlice(1,1) * detSlice(2,3) * detSlice(3,0) - detSlice(0,1) * detSlice(1,2) * detSlice(2,3) * detSlice(3,0) -
       detSlice(0,3) * detSlice(1,2) * detSlice(2,0) * detSlice(3,1) + detSlice(0,2) * detSlice(1,3) * detSlice(2,0) * detSlice(3,1) +
       detSlice(0,3) * detSlice(1,0) * detSlice(2,2) * detSlice(3,1) - detSlice(0,0) * detSlice(1,3) * detSlice(2,2) * detSlice(3,1) -
       detSlice(0,2) * detSlice(1,0) * detSlice(2,3) * detSlice(3,1) + detSlice(0,0) * detSlice(1,2) * detSlice(2,3) * detSlice(3,1) +
       detSlice(0,3) * detSlice(1,1) * detSlice(2,0) * detSlice(3,2) - detSlice(0,1) * detSlice(1,3) * detSlice(2,0) * detSlice(3,2) -
       detSlice(0,3) * detSlice(1,0) * detSlice(2,1) * detSlice(3,2) + detSlice(0,0) * detSlice(1,3) * detSlice(2,1) * detSlice(3,2) +
       detSlice(0,1) * detSlice(1,0) * detSlice(2,3) * detSlice(3,2) - detSlice(0,0) * detSlice(1,1) * detSlice(2,3) * detSlice(3,2) -
       detSlice(0,2) * detSlice(1,1) * detSlice(2,0) * detSlice(3,3) + detSlice(0,1) * detSlice(1,2) * detSlice(2,0) * detSlice(3,3) +
       detSlice(0,2) * detSlice(1,0) * detSlice(2,1) * detSlice(3,3) - detSlice(0,0) * detSlice(1,2) * detSlice(2,1) * detSlice(3,3) -
       detSlice(0,1) * detSlice(1,0) * detSlice(2,2) * detSlice(3,3) + detSlice(0,0) * detSlice(1,1) * detSlice(2,2) * detSlice(3,3);

    //complex<double> det = detSlice.determinant();
    double num = (det * refHelper.thetaDet[0][0] * refHelper.thetaDet[0][1]).real();
    double den = (refHelper.thetaDet[0][0] * refHelper.thetaDet[0][1]).real();
    return parity * num / den;
  }

  void update(int i, int a, bool sz, const Slater &ref, Corr &corr, bool doparity = true)
  {
    double p = 1.0;
    if (doparity) p *= d.parity(a, i, sz);
    d.setocc(i, sz, false);
    d.setocc(a, sz, true);
    if (refHelper.hftype == Generalized) {
      int norbs = Determinant::norbs;
      vector<int> cre{ a + sz * norbs }, des{ i + sz * norbs };
      refHelper.excitationUpdateGhf(ref, cre, des, sz, p, d);
    }
    else
    {
      vector<int> cre{ a }, des{ i };
      refHelper.excitationUpdate(ref, cre, des, sz, p, d);
    }

    corrHelper.updateHelper(corr, d, i, a, sz);
  }

  void update(int i, int j, int a, int b, bool sz, const Slater &ref, Corr& corr, bool doparity = true)
  {
    double p = 1.0;
    Determinant dcopy = d;
    if (doparity) p *= d.parity(a, i, sz);
    d.setocc(i, sz, false);
    d.setocc(a, sz, true);
    if (doparity) p *= d.parity(b, j, sz);
    d.setocc(j, sz, false);
    d.setocc(b, sz, true);
    if (refHelper.hftype == Generalized) {
      int norbs = Determinant::norbs;
      vector<int> cre{ a + sz * norbs, b + sz * norbs }, des{ i + sz * norbs, j + sz * norbs };
      refHelper.excitationUpdateGhf(ref, cre, des, sz, p, d);
    }
    else {
      vector<int> cre{ a, b }, des{ i, j };
      refHelper.excitationUpdate(ref, cre, des, sz, p, d);
    }
    corrHelper.updateHelper(corr, d, i, j, a, b, sz);
  }

  void updateWalker(const Slater &ref, Corr& corr, int ex1, int ex2, bool doparity = true)
  {
    int norbs = Determinant::norbs;
    int I = ex1 / 2 / norbs, A = ex1 - 2 * norbs * I;
    int J = ex2 / 2 / norbs, B = ex2 - 2 * norbs * J;
    if (I % 2 == J % 2 && ex2 != 0) {
      if (I % 2 == 1) {
        update(I / 2, J / 2, A / 2, B / 2, 1, ref, corr, doparity);
      }
      else {
        update(I / 2, J / 2, A / 2, B / 2, 0, ref, corr, doparity);
      }
    }
    else {
      if (I % 2 == 0)
        update(I / 2, A / 2, 0, ref, corr, doparity);
      else
        update(I / 2, A / 2, 1, ref, corr, doparity);

      if (ex2 != 0) {
        if (J % 2 == 1) {
          update(J / 2, B / 2, 1, ref, corr, doparity);
        }
        else {
          update(J / 2, B / 2, 0, ref, corr, doparity);
        }
      }
    }
  }

  void exciteWalker(const Slater &ref, Corr& corr, int excite1, int excite2, int norbs)
  {
    int I1 = excite1 / (2 * norbs), A1 = excite1 % (2 * norbs);

    if (I1 % 2 == 0)
      update(I1 / 2, A1 / 2, 0, ref, corr);
    else
      update(I1 / 2, A1 / 2, 1, ref, corr);

    if (excite2 != 0) {
      int I2 = excite2 / (2 * norbs), A2 = excite2 % (2 * norbs);
      if (I2 % 2 == 0)
        update(I2 / 2, A2 / 2, 0, ref, corr);
      else
        update(I2 / 2, A2 / 2, 1, ref, corr);
    }
  }

  void OverlapWithOrbGradient(const Slater &ref, Eigen::VectorXd &grad, double detovlp) const
  {
    int norbs = Determinant::norbs;
    Determinant walkerDet = d;

    //K and L are relative row and col indices
    int KA = 0, KB = 0;
    for (int k = 0; k < norbs; k++) { //walker indices on the row
      if (walkerDet.getoccA(k)) {
        for (int det = 0; det < ref.getDeterminants().size(); det++) {
          Determinant refDet = ref.getDeterminants()[det];
          int L = 0;
          for (int l = 0; l < norbs; l++) {
            if (refDet.getoccA(l)) {
              grad(2 * k * norbs + 2 * l) += ref.getciExpansion()[det] * (refHelper.thetaInv[0](L, KA) * refHelper.thetaDet[det][0] * refHelper.thetaDet[det][1]).real() /detovlp;
              grad(2 * k * norbs + 2 * l + 1) += ref.getciExpansion()[det] * (- refHelper.thetaInv[0](L, KA) * refHelper.thetaDet[det][0] * refHelper.thetaDet[det][1]).imag() /detovlp;
              L++;
            }
          }
        }
        KA++;
      }
      if (walkerDet.getoccB(k)) {
        for (int det = 0; det < ref.getDeterminants().size(); det++) {
          Determinant refDet = ref.getDeterminants()[det];
          int L = 0;
          for (int l = 0; l < norbs; l++) {
            if (refDet.getoccB(l)) {
              if (refHelper.hftype == UnRestricted) {
                grad(2 * norbs * norbs + 2 * k * norbs + 2 * l) += ref.getciExpansion()[det] * (refHelper.thetaInv[1](L, KB) * refHelper.thetaDet[det][0] * refHelper.thetaDet[det][1]).real() / detovlp;
                grad(2 * norbs * norbs + 2 * k * norbs + 2 * l + 1) += ref.getciExpansion()[det] * (- refHelper.thetaInv[1](L, KB) * refHelper.thetaDet[det][0] * refHelper.thetaDet[det][1]).imag() / detovlp;
              }
              else {
                grad(2 * k * norbs + 2 * l) += ref.getciExpansion()[det] * (refHelper.thetaInv[1](L, KB) * refHelper.thetaDet[det][0] * refHelper.thetaDet[det][1]).real() / detovlp;
                grad(2 * k * norbs + 2 * l + 1) += ref.getciExpansion()[det] * (- refHelper.thetaInv[1](L, KB) * refHelper.thetaDet[det][0] * refHelper.thetaDet[det][1]).imag() / detovlp;
              }
              L++;
            }
          }
        }
        KB++;
      }
    }
  }

  void OverlapWithOrbGradientGhf(const Slater &ref, Eigen::VectorXd &grad, double detovlp) const
  {
    int norbs = Determinant::norbs;
    Determinant walkerDet = d;
    Determinant refDet = ref.getDeterminants()[0];

    //K and L are relative row and col indices
    int K = 0;
    for (int k = 0; k < norbs; k++) { //walker indices on the row
      if (walkerDet.getoccA(k)) {
        int L = 0;
        for (int l = 0; l < norbs; l++) {
          if (refDet.getoccA(l)) {
            grad(4 * k * norbs + 2 * l) += (refHelper.thetaInv[0](L, K) * refHelper.thetaDet[0][0]).real() / detovlp;
            grad(4 * k * norbs + 2 * l + 1) += (- refHelper.thetaInv[0](L, K) * refHelper.thetaDet[0][0]).imag() / detovlp;
            L++;
          }
        }
        for (int l = 0; l < norbs; l++) {
          if (refDet.getoccB(l)) {
            grad(4 * k * norbs + 2 * norbs + 2 * l) += (refHelper.thetaInv[0](L, K) * refHelper.thetaDet[0][0]).real() / detovlp;
            grad(4 * k * norbs + 2 * norbs + 2 * l + 1) += (- refHelper.thetaInv[0](L, K) * refHelper.thetaDet[0][0]).imag() / detovlp;
            L++;
          }
        }
        K++;
      }
    }
    for (int k = 0; k < norbs; k++) { //walker indices on the row
      if (walkerDet.getoccB(k)) {
        int L = 0;
        for (int l = 0; l < norbs; l++) {
          if (refDet.getoccA(l)) {
            grad(4 * norbs * norbs +  4 * k * norbs + 2 * l) += (refHelper.thetaDet[0][0] * refHelper.thetaInv[0](L, K)).real() / detovlp;
            grad(4 * norbs * norbs +  4 * k * norbs + 2 * l + 1) += (- refHelper.thetaDet[0][0] * refHelper.thetaInv[0](L, K)).imag() / detovlp;
            L++;
          } 
        }
        for (int l = 0; l < norbs; l++) {
          if (refDet.getoccB(l)) {
            grad(4 * norbs * norbs +  4 * k * norbs + 2 * norbs + 2 * l) += (refHelper.thetaDet[0][0] * refHelper.thetaInv[0](L, K)).real() / detovlp;
            grad(4 * norbs * norbs +  4 * k * norbs + 2 * norbs + 2 * l + 1) += (- refHelper.thetaDet[0][0] * refHelper.thetaInv[0](L, K)).imag() / detovlp;
            L++;
          }
        }
        K++;
      }
    }
  }

  void OverlapWithGradient(const Slater &ref, Eigen::VectorBlock<VectorXd> &grad) const
  {
    double detovlp = getDetOverlap(ref);
    for (int i = 0; i < ref.ciExpansion.size(); i++)
      grad[i] += getIndividualDetOverlap(i) / detovlp;
    //grad[0] = 0.;
    if (ref.determinants.size() <= 1 && schd.optimizeOrbs) {
      //if (hftype == UnRestricted)
      VectorXd gradOrbitals;
      if (ref.hftype == UnRestricted) {
        gradOrbitals = VectorXd::Zero(4 * ref.HforbsA.rows() * ref.HforbsA.rows());
        OverlapWithOrbGradient(ref, gradOrbitals, detovlp);
      }
      else {
        gradOrbitals = VectorXd::Zero(2 * ref.HforbsA.rows() * ref.HforbsA.rows());
        if (ref.hftype == Restricted) OverlapWithOrbGradient(ref, gradOrbitals, detovlp);
        else OverlapWithOrbGradientGhf(ref, gradOrbitals, detovlp);
      }
      for (int i = 0; i < gradOrbitals.size(); i++)
        grad[ref.ciExpansion.size() + i] += gradOrbitals[i];
    }
  }

  friend ostream& operator<<(ostream& os, const Walker<Corr, Slater>& w) {
    os << w.d << endl << endl;
    os << "alphaTable\n" << w.refHelper.rTable[0][0] << endl << endl;
    os << "betaTable\n" << w.refHelper.rTable[0][1] << endl << endl;
    os << "dets\n" << w.refHelper.thetaDet[0][0] << "  " << w.refHelper.thetaDet[0][1] << endl << endl;
    os << "alphaInv\n" << w.refHelper.thetaInv[0] << endl << endl;
    os << "betaInv\n" << w.refHelper.thetaInv[1] << endl << endl;
    return os;
  }

  void OverlapWithLocalEnergyGradient(const Corr &corr, const Slater &ref, workingArray &work, Eigen::VectorXd &gradEloc) const
  {
    VectorXd v = VectorXd::Zero(corr.getNumVariables() + ref.getNumVariables());
    corr.getVariables(v);
    Eigen::VectorBlock<VectorXd> vtail = v.tail(v.rows() - corr.getNumVariables());
    ref.getVariables(vtail);
    LocalEnergySolver Solver(d, work);
    double Eloctest = 0.0;
    stan::math::gradient(Solver, v, Eloctest, gradEloc);
    //make sure no nan values persist
    for (int i = 0; i < gradEloc.rows(); i++)
    {
      if (std::isnan(gradEloc(i)))
        gradEloc(i) = 0.0;
    }
    if (schd.debug)
    {
      //cout << Eloc << "\t|\t" << Eloctest << endl;
      cout << Eloctest << endl;

      //below is very expensive and used only for debugging
      Eigen::VectorXd finiteGradEloc = Eigen::VectorXd::Zero(v.size());
      for (int i = 0; i < v.size(); ++i)
      {
        double dt = 0.00001;
        Eigen::VectorXd vdt = v;
        vdt(i) += dt;
        finiteGradEloc(i) = (Solver(vdt) - Eloctest) / dt;
      }
      for (int i = 0; i < v.size(); ++i)
      {
        cout << finiteGradEloc(i) << "\t" << gradEloc(i) << endl;
      }
    }
  }
};

//template<typename Corr>
//struct Walker<Corr, BFSlater> {
//
//  Determinant d;
//  WalkerHelper<Corr> corrHelper;
//  WalkerHelper<BFSlater> refHelper;
//  unordered_set<int> excitedOrbs;     //spin orbital indices of excited electrons (in virtual orbitals) in d 
//
//  Walker() {};
//  
//  Walker(Corr &corr, const BFSlater &ref) 
//  {
//    initDet(ref.getHforbsA().real(), ref.getHforbsB().real());
//    refHelper = WalkerHelper<BFSlater>(ref, d);
//    corrHelper = WalkerHelper<Corr>(corr, d);
//  }
//
//  Walker(Corr &corr, const BFSlater &ref, const Determinant &pd) : d(pd), refHelper(ref, pd), corrHelper(corr, pd) {}; 
//
//  Determinant& getDet() {return d;}
//  void readBestDeterminant(Determinant& d) const 
//  {
//    if (commrank == 0) {
//      char file[5000];
//      sprintf(file, "BestDeterminant.txt");
//      std::ifstream ifs(file, std::ios::binary);
//      boost::archive::binary_iarchive load(ifs);
//      load >> d;
//    }
//#ifndef SERIAL
//    MPI_Bcast(&d.reprA, DetLen, MPI_DOUBLE, 0, MPI_COMM_WORLD);
//    MPI_Bcast(&d.reprB, DetLen, MPI_DOUBLE, 0, MPI_COMM_WORLD);
//#endif
//  }
//
//  /**
//   * makes det based on mo coeffs 
//   */
//  void guessBestDeterminant(Determinant& d, const Eigen::MatrixXd& HforbsA, const Eigen::MatrixXd& HforbsB) const 
//  {
//    int norbs = Determinant::norbs;
//    int nalpha = Determinant::nalpha;
//    int nbeta = Determinant::nbeta;
//
//    d = Determinant();
//    if (boost::iequals(schd.determinantFile, "")) {
//      for (int i = 0; i < nalpha; i++) {
//        int bestorb = 0;
//        double maxovlp = 0;
//        for (int j = 0; j < norbs; j++) {
//          if (abs(HforbsA(i, j)) > maxovlp && !d.getoccA(j)) {
//            maxovlp = abs(HforbsA(i, j));
//            bestorb = j;
//          }
//        }
//        d.setoccA(bestorb, true);
//      }
//      for (int i = 0; i < nbeta; i++) {
//        int bestorb = 0;
//        double maxovlp = 0;
//        for (int j = 0; j < norbs; j++) {
//          if (schd.hf == "rhf" || schd.hf == "uhf") {
//            if (abs(HforbsB(i, j)) > maxovlp && !d.getoccB(j)) {
//              bestorb = j;
//              maxovlp = abs(HforbsB(i, j));
//            }
//          }
//          else {
//            if (abs(HforbsB(i+norbs, j)) > maxovlp && !d.getoccB(j)) {
//              bestorb = j;
//              maxovlp = abs(HforbsB(i+norbs, j));
//            }
//          }
//        }
//        d.setoccB(bestorb, true);
//      }
//    }
//    else if (boost::iequals(schd.determinantFile, "bestDet")) {
//      std::vector<Determinant> dets;
//      std::vector<double> ci;
//      readDeterminants(schd.determinantFile, dets, ci);
//      d = dets[0];
//    }
//  }
//
//  void initDet(const MatrixXd& HforbsA, const MatrixXd& HforbsB) 
//  {
//    bool readDeterminant = false;
//    char file[5000];
//    sprintf(file, "BestDeterminant.txt");
//
//    {
//      ifstream ofile(file);
//      if (ofile)
//        readDeterminant = true;
//    }
//    if (readDeterminant)
//      readBestDeterminant(d);
//    else
//      guessBestDeterminant(d, HforbsA, HforbsB);
//  }
//
//  double getDetOverlap(const BFSlater &ref) const
//  {
//    return (refHelper.thetaDet[0] * refHelper.thetaDet[1]).real();
//  }
//
//  double getDetFactor(int i, int a, const BFSlater &ref) const 
//  {
//    if (i % 2 == 0)
//      return getDetFactor(i / 2, a / 2, 0, ref);
//    else                                   
//      return getDetFactor(i / 2, a / 2, 1, ref);
//  }
//
//  double getDetFactor(int I, int J, int A, int B, const BFSlater &ref) const 
//  {
//    if (I % 2 == J % 2 && I % 2 == 0)
//      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 0, 0, ref);
//    else if (I % 2 == J % 2 && I % 2 == 1)                  
//      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 1, 1, ref);
//    else if (I % 2 != J % 2 && I % 2 == 0)                  
//      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 0, 1, ref);
//    else                                                    
//      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 1, 0, ref);
//  }
//
//  double getDetFactor(int i, int a, bool sz, const BFSlater &ref) const
//  {
//    Determinant dcopy = d;
//    double parity *= dcopy.parity(a, i, sz);
//    dcopy.setocc(i, sz, false);  
//    dcopy.setocc(a, sz, true);
//    WalkerHelper<BFSlater> refHelperCopy = WalkerHelper<BFSlater>(ref, dcopy);
//    return parity * (refHelperCopy.thetaDet[0] * refHelperCopy.thetaDet[1]).real() /  getDetOverlap(ref);
//  }
//  
//  double getDetFactor(int i, int j, int a, int b, bool sz1, bool sz2, const BFSlater &ref) const
//  {
//    Determinant dcopy = d;
//    double parity *= dcopy.parity(a, i, sz1);
//    dcopy.setocc(i, sz1, false);  
//    dcopy.setocc(a, sz1, true);
//    parity *= dcopy.parity(b, j, sz2);
//    dcopy.setocc(j, sz2, false);  
//    dcopy.setocc(b, sz2, true);
//    WalkerHelper<BFSlater> refHelperCopy = WalkerHelper<BFSlater>(ref, dcopy);
//    return parity * (refHelperCopy.thetaDet[0] * refHelperCopy.thetaDet[1]).real() /  getDetOverlap(ref);
//  }
// 
////  //only works for ghf
////  double getDetFactor(std::array<unordered_set<int>, 2> &from, std::array<unordered_set<int>, 2> &to) const
////  {
////    if (from[0].size() + from[1].size() == 0) return 1.;
////    int numExc = from[0].size() + from[1].size();
////    VectorXi tableIndicesRow = VectorXi::Zero(from[0].size() + from[1].size());
////    VectorXi tableIndicesCol = VectorXi::Zero(from[0].size() + from[1].size());
////    Determinant dcopy = d;
////    double parity = 1.;
////    int count = 0;
////    for (int sz = 0; sz < 2; sz++) {//iterate over spins
////      auto itFrom = from[sz].begin();
////      auto itTo = to[sz].begin();
////      for (int n = 0; n < from[sz].size(); n++) {//iterate over excitations
////        int i = *itFrom, a = *itTo;
////        itFrom = std::next(itFrom); itTo = std::next(itTo);
////        refHelper.getRelIndices(i, tableIndicesCol(count), a, tableIndicesRow(count), sz);
////        count++;
////        parity *= dcopy.parity(a, i, sz);
////        dcopy.setocc(i, sz, false);  
////        dcopy.setocc(a, sz, true);
////      }
////    }
////
////    MatrixXcd detSlice = MatrixXcd::Zero(numExc,numExc);
////    igl::slice(refHelper.rTable[0][0], tableIndicesRow, tableIndicesCol, detSlice);
////    complex<double> det(0.,0.);
////    if (detSlice.rows() == 1) det = detSlice(0, 0);
////    else if (detSlice.rows() == 2) det = detSlice(0, 0) * detSlice(1, 1) - detSlice(0, 1) * detSlice(1, 0);
////    else if (detSlice.rows() == 3) det =   detSlice(0, 0) * (detSlice(1, 1) * detSlice(2, 2) - detSlice(1, 2) * detSlice(2, 1))
////                                         - detSlice(0, 1) * (detSlice(1, 0) * detSlice(2, 2) - detSlice(1, 2) * detSlice(2, 0))
////                                         + detSlice(0, 2) * (detSlice(1, 0) * detSlice(2, 1) - detSlice(1, 1) * detSlice(2, 0));
////    else if (detSlice.rows() == 4) det = detSlice(0,3) * detSlice(1,2) * detSlice(2,1) * detSlice(3,0) - detSlice(0,2) * detSlice(1,3) * detSlice(2,1) * detSlice(3,0) -
////       detSlice(0,3) * detSlice(1,1) * detSlice(2,2) * detSlice(3,0) + detSlice(0,1) * detSlice(1,3) * detSlice(2,2) * detSlice(3,0) +
////       detSlice(0,2) * detSlice(1,1) * detSlice(2,3) * detSlice(3,0) - detSlice(0,1) * detSlice(1,2) * detSlice(2,3) * detSlice(3,0) -
////       detSlice(0,3) * detSlice(1,2) * detSlice(2,0) * detSlice(3,1) + detSlice(0,2) * detSlice(1,3) * detSlice(2,0) * detSlice(3,1) +
////       detSlice(0,3) * detSlice(1,0) * detSlice(2,2) * detSlice(3,1) - detSlice(0,0) * detSlice(1,3) * detSlice(2,2) * detSlice(3,1) -
////       detSlice(0,2) * detSlice(1,0) * detSlice(2,3) * detSlice(3,1) + detSlice(0,0) * detSlice(1,2) * detSlice(2,3) * detSlice(3,1) +
////       detSlice(0,3) * detSlice(1,1) * detSlice(2,0) * detSlice(3,2) - detSlice(0,1) * detSlice(1,3) * detSlice(2,0) * detSlice(3,2) -
////       detSlice(0,3) * detSlice(1,0) * detSlice(2,1) * detSlice(3,2) + detSlice(0,0) * detSlice(1,3) * detSlice(2,1) * detSlice(3,2) +
////       detSlice(0,1) * detSlice(1,0) * detSlice(2,3) * detSlice(3,2) - detSlice(0,0) * detSlice(1,1) * detSlice(2,3) * detSlice(3,2) -
////       detSlice(0,2) * detSlice(1,1) * detSlice(2,0) * detSlice(3,3) + detSlice(0,1) * detSlice(1,2) * detSlice(2,0) * detSlice(3,3) +
////       detSlice(0,2) * detSlice(1,0) * detSlice(2,1) * detSlice(3,3) - detSlice(0,0) * detSlice(1,2) * detSlice(2,1) * detSlice(3,3) -
////       detSlice(0,1) * detSlice(1,0) * detSlice(2,2) * detSlice(3,3) + detSlice(0,0) * detSlice(1,1) * detSlice(2,2) * detSlice(3,3);
////
////    //complex<double> det = detSlice.determinant();
////    double num = (det * refHelper.thetaDet[0][0] * refHelper.thetaDet[0][1]).real();
////    double den = (refHelper.thetaDet[0][0] * refHelper.thetaDet[0][1]).real();
////    return parity * num / den;
////  }
////
////  void update(int i, int a, bool sz, const BFSlater &ref, Corr &corr, bool doparity = true)
////  {
////    double p = 1.0;
////    if (doparity) p *= d.parity(a, i, sz);
////    d.setocc(i, sz, false);
////    d.setocc(a, sz, true);
////    if (refHelper.hftype == Generalized) {
////      int norbs = Determinant::norbs;
////      vector<int> cre{ a + sz * norbs }, des{ i + sz * norbs };
////      refHelper.excitationUpdateGhf(ref, cre, des, sz, p, d);
////    }
////    else
////    {
////      vector<int> cre{ a }, des{ i };
////      refHelper.excitationUpdate(ref, cre, des, sz, p, d);
////    }
////
////    corrHelper.updateHelper(corr, d, i, a, sz);
////  }
//
//  void update(int i, int j, int a, int b, bool sz, const BFSlater &ref, Corr& corr, bool doparity = true)
//  {
//    d.setocc(i, sz, false);
//    d.setocc(a, sz, true);
//    d.setocc(j, sz, false);
//    d.setocc(b, sz, true);
//    if (refHelper.hftype == Generalized) {
//      refHelper.excitationUpdateGhf(ref, d);
//    }
//    else {
//      refHelper.excitationUpdate(ref, d);
//    }
//    corrHelper.updateHelper(corr, d, i, j, a, b, sz);
//  }
//
//  void updateWalker(const BFSlater &ref, Corr& corr, int ex1, int ex2, bool doparity = true)
//  {
//    int norbs = Determinant::norbs;
//    int I = ex1 / 2 / norbs, A = ex1 - 2 * norbs * I;
//    int J = ex2 / 2 / norbs, B = ex2 - 2 * norbs * J;
//    if (I % 2 == J % 2 && ex2 != 0) {
//      if (I % 2 == 1) {
//        update(I / 2, J / 2, A / 2, B / 2, 1, ref, corr, doparity);
//      }
//      else {
//        update(I / 2, J / 2, A / 2, B / 2, 0, ref, corr, doparity);
//      }
//    }
//    else {
//      if (I % 2 == 0)
//        update(I / 2, A / 2, 0, ref, corr, doparity);
//      else
//        update(I / 2, A / 2, 1, ref, corr, doparity);
//
//      if (ex2 != 0) {
//        if (J % 2 == 1) {
//          update(J / 2, B / 2, 1, ref, corr, doparity);
//        }
//        else {
//          update(J / 2, B / 2, 0, ref, corr, doparity);
//        }
//      }
//    }
//  }
//
//  void exciteWalker(const BFSlater &ref, Corr& corr, int excite1, int excite2, int norbs)
//  {
//    int I1 = excite1 / (2 * norbs), A1 = excite1 % (2 * norbs);
//
//    if (I1 % 2 == 0)
//      update(I1 / 2, A1 / 2, 0, ref, corr);
//    else
//      update(I1 / 2, A1 / 2, 1, ref, corr);
//
//    if (excite2 != 0) {
//      int I2 = excite2 / (2 * norbs), A2 = excite2 % (2 * norbs);
//      if (I2 % 2 == 0)
//        update(I2 / 2, A2 / 2, 0, ref, corr);
//      else
//        update(I2 / 2, A2 / 2, 1, ref, corr);
//    }
//  }
//
//  void OverlapWithOrbGradient(const BFSlater &ref, Eigen::VectorXd &grad, double detovlp) const
//  {
//    int norbs = Determinant::norbs;
//    Determinant walkerDet = d;
//    Determinant refDet = ref.getDeterminant();
//
//    //K and L are relative row and col indices
//    int KA = 0, KB = 0;
//    for (int k = 0; k < norbs; k++) { //walker indices on the row
//      if (walkerDet.getoccA(k)) {
//        int L = 0;
//        for (int l = 0; l < norbs; l++) {
//          if (refDet.getoccA(l)) {
//            grad(2*k*norbs + 2*l) += (refHelper.thetaInv[0](L, KA) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).real() /detovlp;
//            grad(2*k*norbs + 2*l + 1) += * (-refHelper.thetaInv[0](L, KA) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).imag() /detovlp;
//            L++;
//          }
//        }
//        KA++;
//      }
//      if (walkerDet.getoccB(k)) {
//        int L = 0;
//        for (int l = 0; l < norbs; l++) {
//          if (refDet.getoccB(l)) {
//            if (refHelper.hftype == UnRestricted) {
//              grad(2*norbs*norbs + 2*k*norbs + 2*l) += (refHelper.thetaInv[1](L, KB) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).real() / detovlp;
//              grad(2*norbs*norbs + 2*k*norbs + 2*l + 1) += (-refHelper.thetaInv[1](L, KB) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).imag() / detovlp;
//            }
//            else {
//              grad(2*k*norbs + 2*l) += (refHelper.thetaInv[1](L, KB) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).real() / detovlp;
//              grad(2*k*norbs + 2*l + 1) += (-refHelper.thetaInv[1](L, KB) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).imag() / detovlp;
//            }
//            L++;
//          }
//        }
//        KB++;
//      }
//      if (!walkerDet.getoccA(k) && !walkerDet.getoccB(k)) {
//        for (int i = 0; i < refHelper.doublons.size(); i++) {
//          int LA = 0, LB = 0;
//          int relIndexA = std::search_n(refHelper.closedOrbs[0].begin(), refHelper.closedOrbs[0].end(), 1, refHelper.doublons[i]) - refHelper.closedOrbs[0].begin();
//          int relIndexB = std::search_n(refHelper.closedOrbs[1].begin(), refHelper.closedOrbs[1].end(), 1, refHelper.doublons[i]) - refHelper.closedOrbs[1].begin();
//          for (int l = 0; l < norbs; l++) {
//            if (refDet.getoccA(l)) {
//              grad(2*k*norbs + 2*l) += ref.bf(refHelper.doublons[i], k) * (refHelper.thetaInv[0](LA, relIndexA) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).real() / detovlp;
//              grad(2*k*norbs + 2*l + 1) += ref.bf(refHelper.doublons[i], k) * (-refHelper.thetaInv[0](LA, relIndexA) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).imag() /detovlp;
//              LA++;
//            }
//            if (refDet.getoccB(l)) {
//              if (refHelper.hftype == UnRestricted) {
//                grad(2*norbs*norbs + 2*k*norbs + 2*l) += ref.bf(refHelper.doublons[i], k) * (refHelper.thetaInv[1](LB, relIndexB) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).real() / detovlp;
//                grad(2*norbs*norbs + 2*k*norbs + 2*l + 1) += ref.bf(refHelper.doublons[i], k) * (-refHelper.thetaInv[1](LB, relIndexB) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).imag() /detovlp;
//              }
//              else {
//                grad(2*k*norbs + 2*l) += ref.bf(refHelper.doublons[i], k) * (refHelper.thetaInv[1](LB, relIndexB) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).real() / detovlp;
//                grad(2*k*norbs + 2*l + 1) += ref.bf(refHelper.doublons[i], k) * (-refHelper.thetaInv[1](LB, relIndexB) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).imag() /detovlp;
//              }
//              LB++;
//            }
//          }
//        }
//      }
//    }
//  }
//
//  void OverlapWithOrbGradientGhf(const BFSlater &ref, Eigen::VectorXd &grad, double detovlp) const
//  {
//    int norbs = Determinant::norbs;
//    Determinant walkerDet = d;
//    Determinant refDet = ref.getDeterminants()[0];
//
//    //K and L are relative row and col indices
//    int KA = 0, KB = 0;
//    for (int k = 0; k < norbs; k++) { //walker indices on the row
//      if (walkerDet.getoccA(k)) {
//        int L = 0;
//        for (int l = 0; l < norbs; l++) {
//          if (refDet.getoccA(l)) {
//            grad(4*k*norbs + 2*l) += (refHelper.thetaInv[0](L, KA) * refHelper.thetaDet[0][0]).real() / detovlp;
//            grad(4*k*norbs + 2*l + 1) += (-refHelper.thetaInv[0](L, KA) * refHelper.thetaDet[0][0]).imag() / detovlp;
//            L++;
//          }
//        }
//        for (int l = 0; l < norbs; l++) {
//          if (refDet.getoccB(l)) {
//            grad(4*k*norbs + 2*norbs + 2*l) += (refHelper.thetaInv[0](L, KA) * refHelper.thetaDet[0][0]).real() / detovlp;
//            grad(4*k*norbs + 2*norbs + 2*l + 1) += (-refHelper.thetaInv[0](L, KA) * refHelper.thetaDet[0][0]).imag() / detovlp;
//            L++;
//          }
//        }
//        KA++;
//      }
//      if (walkerDet.getoccB(k)) {
//        int L = 0;
//        for (int l = 0; l < norbs; l++) {
//          if (refDet.getoccA(l)) {
//            grad(4*norbs*norbs + 4*k*norbs + 2*l) += (refHelper.thetaDet[0][0] * refHelper.thetaInv[0](L, KB)).real() / detovlp;
//            grad(4*norbs*norbs + 4*k*norbs + 2*l + 1) += (-refHelper.thetaDet[0][0] * refHelper.thetaInv[0](L, KB)).imag() / detovlp;
//            L++;
//          } 
//        }
//        for (int l = 0; l < norbs; l++) {
//          if (refDet.getoccB(l)) {
//            grad(4*norbs*norbs + 4*k*norbs + 2*norbs + 2*l) += (refHelper.thetaDet[0][0] * refHelper.thetaInv[0](L, KB)).real() / detovlp;
//            grad(4*norbs*norbs + 4*k*norbs + 2*norbs + 2*l + 1) += (-refHelper.thetaDet[0][0] * refHelper.thetaInv[0](L, KB)).imag() / detovlp;
//            L++;
//          }
//        }
//        KB++;
//      }
//      if (!walkerDet.getoccA(k) && !walkerDet.getoccB(k)) {
//        for (int i = 0; i < refHelper.doublons.size(); i++) {
//          int L = 0;
//          int relIndexA = std::search_n(refHelper.closedOrbs[0].begin(), refHelper.closedOrbs[0].end(), 1, refHelper.doublons[i]) - refHelper.closedOrbs[0].begin();
//          int relIndexB = std::search_n(refHelper.closedOrbs[1].begin(), refHelper.closedOrbs[1].end(), 1, refHelper.doublons[i]) - refHelper.closedOrbs[1].begin();
//          for (int l = 0; l < norbs; l++) {
//            if (refDet.getoccA(l)) {
//              grad(4*k*norbs + 2*l) += ref.bf(refHelper.doublons[i], k) * (refHelper.thetaDet[0][0] * refHelper.thetaInv[0](L, relIndexA)).real() / detovlp;
//              grad(4*k*norbs + 2*l + 1) += ref.bf(refHelper.doublons[i], k) * (-refHelper.thetaDet[0][0] * refHelper.thetaInv[0](L, relIndexA)).imag() / detovlp;
//              grad(4*norbs*norbs + 4*k*norbs + 2*l) += ref.bf(refHelper.doublons[i], k) * (refHelper.thetaDet[0][0] * refHelper.thetaInv[0](L, relIndexB)).real() / detovlp;
//              grad(4*norbs*norbs + 4*k*norbs + 2*l + 1) += ref.bf(refHelper.doublons[i], k) * (-refHelper.thetaDet[0][0] * refHelper.thetaInv[0](L, relIndexB)).imag() / detovlp;
//              L++;
//            }
//          }
//          for (int l = 0; l < norbs; l++) {
//            if (refDet.getoccB(l)) {
//              grad(4*k*norbs + 2*norbs + 2*l) += ref.bf(refHelper.doublons[i], k) * (refHelper.thetaInv[1](L, relIndexA) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).real() / detovlp;
//              grad(4*k*norbs + 2*norbs + 2*l + 1) += ref.bf(refHelper.doublons[i], k) * (-refHelper.thetaInv[1](L, relIndexA) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).imag() /detovlp;
//              grad(4*norbs*norbs + 4*k*norbs + 2*norbs + 2*l) += ref.bf(refHelper.doublons[i], k) * (refHelper.thetaInv[1](L, relIndexB) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).real() / detovlp;
//              grad(4*norbs*norbs + 4*k*norbs + 2*norbs + 2*l + 1) += ref.bf(refHelper.doublons[i], k) * (-refHelper.thetaInv[1](L, relIndexB) * refHelper.thetaDet[0] * refHelper.thetaDet[1]).imag() /detovlp;
//              L++;
//            }
//          }
//        }
//      }
//    }
//  }
//
//  void OverlapWithGradient(const BFSlater &ref, Eigen::VectorBlock<VectorXd> &grad) const
//  {
//    double detovlp = getDetOverlap(ref);
//    if (schd.optimizeOrbs) {
//      //if (hftype == UnRestricted)
//      VectorXd gradOrbitals;
//      if (ref.hftype == UnRestricted) {
//        gradOrbitals = VectorXd::Zero(4 * ref.HforbsA.rows() * ref.HforbsA.rows());
//        OverlapWithOrbGradient(ref, gradOrbitals, detovlp);
//      }
//      else {
//        gradOrbitals = VectorXd::Zero(2 * ref.HforbsA.rows() * ref.HforbsA.rows());
//        if (ref.hftype == Restricted) OverlapWithOrbGradient(ref, gradOrbitals, detovlp);
//        else OverlapWithOrbGradientGhf(ref, gradOrbitals, detovlp);
//      }
//      for (int i = 0; i < gradOrbitals.size(); i++)
//        grad[i] += gradOrbitals[i];
//    }
//  }
//
//  friend ostream& operator<<(ostream& os, const Walker<Corr, BFSlater>& w) {
//    os << w.d << endl << endl;
//    os << "alphaTable\n" << w.refHelper.rTable[0][0] << endl << endl;
//    os << "betaTable\n" << w.refHelper.rTable[0][1] << endl << endl;
//    os << "dets\n" << w.refHelper.thetaDet[0][0] << "  " << w.refHelper.thetaDet[0][1] << endl << endl;
//    os << "alphaInv\n" << w.refHelper.thetaInv[0] << endl << endl;
//    os << "betaInv\n" << w.refHelper.thetaInv[1] << endl << endl;
//    return os;
//  }
//
//};

template<typename Corr>
struct Walker<Corr, AGP> {

  Determinant d;
  WalkerHelper<Corr> corrHelper;
  WalkerHelper<AGP> refHelper;
  unordered_set<int> excitedOrbs;     //spin orbital indices of excited electrons (in virtual orbitals) in d 

  Walker() {};
  
  Walker(Corr &corr, const AGP &ref) 
  {
    initDet(ref.getPairMat().real());
    refHelper = WalkerHelper<AGP>(ref, d);
    corrHelper = WalkerHelper<Corr>(corr, d);
  }

  Walker(Corr &corr, const AGP &ref, const Determinant &pd) : d(pd), refHelper(ref, pd), corrHelper(corr, pd) {}; 

  Determinant& getDet() {return d;}
  void readBestDeterminant(Determinant& d) const 
  {
    if (commrank == 0) {
      char file[5000];
      sprintf(file, "BestDeterminant.txt");
      std::ifstream ifs(file, std::ios::binary);
      boost::archive::binary_iarchive load(ifs);
      load >> d;
    }
#ifndef SERIAL
  MPI_Bcast(&d.reprA, DetLen, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast(&d.reprB, DetLen, MPI_DOUBLE, 0, MPI_COMM_WORLD);
#endif
  }
  
  void guessBestDeterminant(Determinant& d, const Eigen::MatrixXd& pairMat) const 
  {
    int norbs = Determinant::norbs;
    int nalpha = Determinant::nalpha;
    int nbeta = Determinant::nbeta;
    for (int i = 0; i < nalpha; i++)
      d.setoccA(i, true);
    for (int i = 0; i < nbeta; i++)
      d.setoccB(i, true);
  }
  
  void initDet(const MatrixXd& pairMat) 
  {
    bool readDeterminant = false;
    char file[5000];
    sprintf(file, "BestDeterminant.txt");
  
    {
      ifstream ofile(file);
      if (ofile)
        readDeterminant = true;
    }
    if (readDeterminant)
      readBestDeterminant(d);
    else
      guessBestDeterminant(d, pairMat);
  }
  
  double getDetOverlap(const AGP &ref) const
  {
    return refHelper.thetaDet.real();
  }
  
  double getDetFactor(int i, int a, const AGP &ref) const 
  {
    if (i % 2 == 0)
      return getDetFactor(i / 2, a / 2, 0, ref);
    else                                   
      return getDetFactor(i / 2, a / 2, 1, ref);
  }
  
  double getDetFactor(int I, int J, int A, int B, const AGP &ref) const 
  {
    if (I % 2 == J % 2 && I % 2 == 0)
      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 0, 0, ref);
    else if (I % 2 == J % 2 && I % 2 == 1)                  
      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 1, 1, ref);
    else if (I % 2 != J % 2 && I % 2 == 0)                  
      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 0, 1, ref);
    else                                                    
      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 1, 0, ref);
  }
  
  double getDetFactor(int i, int a, bool sz, const AGP &ref) const
  {
    int tableIndexi, tableIndexa;
    refHelper.getRelIndices(i, tableIndexi, a, tableIndexa, sz); 
    return (refHelper.rTable[sz](tableIndexa, tableIndexi) * refHelper.thetaDet).real() / (refHelper.thetaDet).real();
  }
  
  double getDetFactor(int i, int j, int a, int b, bool sz1, bool sz2, const AGP &ref) const
  {
    int tableIndexi, tableIndexa, tableIndexj, tableIndexb;
    refHelper.getRelIndices(i, tableIndexi, a, tableIndexa, sz1); 
    refHelper.getRelIndices(j, tableIndexj, b, tableIndexb, sz2) ;
  
    complex<double> factor;
    if (sz1 == sz2)
      factor = refHelper.rTable[sz1](tableIndexa, tableIndexi) * refHelper.rTable[sz1](tableIndexb, tableIndexj) 
          - refHelper.rTable[sz1](tableIndexb, tableIndexi) *refHelper.rTable[sz1](tableIndexa, tableIndexj);
    else
      if (sz1 == 0) {
        factor = refHelper.rTable[sz1](tableIndexa, tableIndexi) * refHelper.rTable[sz2](tableIndexb, tableIndexj) 
        + refHelper.thetaInv(tableIndexj, tableIndexi) * refHelper.rTable[2](tableIndexa, tableIndexb);
      }
      else {
        factor = refHelper.rTable[sz1](tableIndexa, tableIndexi) * refHelper.rTable[sz2](tableIndexb, tableIndexj) 
        + refHelper.thetaInv(tableIndexi, tableIndexj) * refHelper.rTable[2](tableIndexb, tableIndexa);
      }
    return (factor * refHelper.thetaDet).real() / (refHelper.thetaDet).real();
  }
  
  double getDetFactor(std::array<unordered_set<int>, 2> &from, std::array<unordered_set<int>, 2> &to) const
  {
    return 0.;
  }
  
  void update(int i, int a, bool sz, const AGP &ref, Corr &corr, bool doparity = true)
  {
    double p = 1.0;
    if (doparity) p *= d.parity(a, i, sz);
    d.setocc(i, sz, false);
    d.setocc(a, sz, true);
    vector<int> cre{ a }, des{ i };
    refHelper.excitationUpdate(ref, cre, des, sz, p, d);
    corrHelper.updateHelper(corr, d, i, a, sz);
  }
  
  void update(int i, int j, int a, int b, bool sz, const AGP &ref, Corr &corr, bool doparity = true)
  {
    double p = 1.0;
    Determinant dcopy = d;
    if (doparity) p *= d.parity(a, i, sz);
    d.setocc(i, sz, false);
    d.setocc(a, sz, true);
    if (doparity) p *= d.parity(b, j, sz);
    d.setocc(j, sz, false);
    d.setocc(b, sz, true);
    vector<int> cre{ a, b }, des{ i, j };
    refHelper.excitationUpdate(ref, cre, des, sz, p, d);
    corrHelper.updateHelper(corr, d, i, j, a, b, sz);
  }
  
  void updateWalker(const AGP& ref, Corr &corr, int ex1, int ex2, bool doparity = true)
  {
    int norbs = Determinant::norbs;
    int I = ex1 / 2 / norbs, A = ex1 - 2 * norbs * I;
    int J = ex2 / 2 / norbs, B = ex2 - 2 * norbs * J;
    if (I % 2 == J % 2 && ex2 != 0) {
      if (I % 2 == 1) {
        update(I / 2, J / 2, A / 2, B / 2, 1, ref, corr, doparity);
      }
      else {
        update(I / 2, J / 2, A / 2, B / 2, 0, ref, corr, doparity);
      }
    }
    else {
      if (I % 2 == 0)
        update(I / 2, A / 2, 0, ref, corr, doparity);
      else
        update(I / 2, A / 2, 1, ref, corr, doparity);
  
      if (ex2 != 0) {
        if (J % 2 == 1) {
          update(J / 2, B / 2, 1, ref, corr, doparity);
        }
        else {
          update(J / 2, B / 2, 0, ref, corr, doparity);
        }
      }
    }
  }
  
  void exciteWalker(const AGP& ref, Corr &corr, int excite1, int excite2, int norbs)
  {
    int I1 = excite1 / (2 * norbs), A1 = excite1 % (2 * norbs);
  
    if (I1 % 2 == 0)
      update(I1 / 2, A1 / 2, 0, ref, corr);
    else
      update(I1 / 2, A1 / 2, 1, ref, corr);
  
    if (excite2 != 0) {
      int I2 = excite2 / (2 * norbs), A2 = excite2 % (2 * norbs);
      if (I2 % 2 == 0)
        update(I2 / 2, A2 / 2, 0, ref, corr);
      else
        update(I2 / 2, A2 / 2, 1, ref, corr);
    }
  }
  
  void OverlapWithGradient(const AGP &ref, Eigen::VectorBlock<VectorXd> &grad) const
  {
    if (schd.optimizeOrbs) {
      double detOvlp = getDetOverlap(ref);
      int norbs = Determinant::norbs;
      Determinant walkerDet = d;
  
      //K and L are relative row and col indices
      int K = 0;
      for (int k = 0; k < norbs; k++) { //walker indices on the row
        if (walkerDet.getoccA(k)) {
          int L = 0;
          for (int l = 0; l < norbs; l++) {
            if (walkerDet.getoccB(l)) {
              grad(2 * k * norbs + 2 * l) += (refHelper.thetaInv(L, K) * refHelper.thetaDet).real() / detOvlp ;
              grad(2 * k * norbs + 2 * l + 1) += (- refHelper.thetaInv(L, K) * refHelper.thetaDet).imag() / detOvlp ;
              L++;
            }
          }
          K++;
        }
      }
    }
  }
  
  friend ostream& operator<<(ostream& os, const Walker<Corr, AGP>& w) {
    os << w.d << endl << endl;
    os << "alphaTable\n" << w.refHelper.rTable[0] << endl << endl;
    os << "betaTable\n" << w.refHelper.rTable[1] << endl << endl;
    os << "thirdTable\n" << w.refHelper.rTable[2] << endl << endl;
    os << "dets\n" << w.refHelper.thetaDet << endl << endl;
    os << "thetaInv\n" << w.refHelper.thetaInv << endl << endl;
    return os;
  }

  void OverlapWithLocalEnergyGradient(const Corr &corr, const AGP &ref, workingArray &work, Eigen::VectorXd &gradEloc) const
  {
    cout << "Local Energy Gradient not implemented for AGP" << endl;
    exit(0);
  }
};

template<typename Corr>
struct Walker<Corr, Pfaffian> {

  Determinant d;
  WalkerHelper<Corr> corrHelper;
  WalkerHelper<Pfaffian> refHelper;
  unordered_set<int> excitedOrbs;     //spin orbital indices of excited electrons (in virtual orbitals) in d 

  Walker() {};
  
  Walker(Corr &corr, const Pfaffian &ref) 
  {
    initDet(ref.getPairMat().real());
    refHelper = WalkerHelper<Pfaffian>(ref, d);
    corrHelper = WalkerHelper<Corr>(corr, d);
  }

  Walker(Corr &corr, const Pfaffian &ref, const Determinant &pd) : d(pd), refHelper(ref, pd), corrHelper(corr, pd) {}; 
  
  Determinant& getDet() {return d;}
  void readBestDeterminant(Determinant& d) const 
  {
    if (commrank == 0) {
      char file[5000];
      sprintf(file, "BestDeterminant.txt");
      std::ifstream ifs(file, std::ios::binary);
      boost::archive::binary_iarchive load(ifs);
      load >> d;
    }
#ifndef SERIAL
  MPI_Bcast(&d.reprA, DetLen, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast(&d.reprB, DetLen, MPI_DOUBLE, 0, MPI_COMM_WORLD);
#endif
  }
  
  void guessBestDeterminant(Determinant& d, const Eigen::MatrixXd& pairMat) const 
  {
    int norbs = Determinant::norbs;
    int nalpha = Determinant::nalpha;
    int nbeta = Determinant::nbeta;
    for (int i = 0; i < nalpha; i++)
      d.setoccA(i, true);
    for (int i = 0; i < nbeta; i++)
      d.setoccB(i, true);
  }
  
  void initDet(const MatrixXd& pairMat) 
  {
    bool readDeterminant = false;
    char file[5000];
    sprintf(file, "BestDeterminant.txt");
  
    {
      ifstream ofile(file);
      if (ofile)
        readDeterminant = true;
    }
    if (readDeterminant)
      readBestDeterminant(d);
    else
      guessBestDeterminant(d, pairMat);
  }
  
  double getDetOverlap(const Pfaffian &ref) const
  {
    return refHelper.thetaPfaff.real();
  }
  
  double getDetFactor(int i, int a, const Pfaffian &ref) const 
  {
    if (i % 2 == 0)
      return getDetFactor(i / 2, a / 2, 0, ref);
    else                                   
      return getDetFactor(i / 2, a / 2, 1, ref);
  }
  
  double getDetFactor(int I, int J, int A, int B, const Pfaffian &ref) const 
  {
    if (I % 2 == J % 2 && I % 2 == 0)
      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 0, 0, ref);
    else if (I % 2 == J % 2 && I % 2 == 1)                  
      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 1, 1, ref);
    else if (I % 2 != J % 2 && I % 2 == 0)                  
      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 0, 1, ref);
    else                                                    
      return getDetFactor(I / 2, J / 2, A / 2, B / 2, 1, 0, ref);
  }
  
  double getDetFactor(int i, int a, bool sz, const Pfaffian &ref) const
  {
    int nopen = refHelper.openOrbs[0].size() + refHelper.openOrbs[1].size();
    int tableIndexi, tableIndexa;
    refHelper.getRelIndices(i, tableIndexi, a, tableIndexa, sz); 
    //return refHelper.rTable[0](tableIndexi * nopen + tableIndexa, tableIndexi);
    //return ((refHelper.fMat.row(tableIndexi * nopen + tableIndexa) * refHelper.thetaInv.col(tableIndexi))(0,0) * refHelper.thetaPfaff).real() / (refHelper.thetaPfaff).real();
    return (refHelper.rTable[0](tableIndexa, tableIndexi) * refHelper.thetaPfaff).real() / (refHelper.thetaPfaff).real();
  }
  
  double getDetFactor(int i, int j, int a, int b, bool sz1, bool sz2, const Pfaffian &ref) const
  {
    int norbs = Determinant::norbs;
    int nopen = refHelper.openOrbs[0].size() + refHelper.openOrbs[1].size();
    int tableIndexi, tableIndexa, tableIndexj, tableIndexb;
    refHelper.getRelIndices(i, tableIndexi, a, tableIndexa, sz1); 
    refHelper.getRelIndices(j, tableIndexj, b, tableIndexb, sz2);
    //cout << "nopen  " << nopen << endl;
    //cout << "sz1  " << sz1 << "  ti  " << tableIndexi << "  ta  " << tableIndexa  << endl;
    //cout << "sz2  " << sz2 << "  tj  " << tableIndexj << "  tb  " << tableIndexb  << endl;
    complex<double> summand1, summand2, crossTerm;
    if (tableIndexi < tableIndexj) {
      crossTerm = (ref.getPairMat())(b + sz2 * norbs, a + sz1 * norbs);
      //summand1 = refHelper.rTable[0](tableIndexi * nopen + tableIndexa, tableIndexi) * refHelper.rTable[0](tableIndexj * nopen + tableIndexb, tableIndexj) 
      //    - refHelper.rTable[0](tableIndexj * nopen + tableIndexb, tableIndexi) * refHelper.rTable[0](tableIndexi * nopen + tableIndexa, tableIndexj);
      //summand2 = refHelper.thetaInv(tableIndexi, tableIndexj) * (refHelper.rTable[1](tableIndexi * nopen + tableIndexa, tableIndexj * nopen + tableIndexb) + crossTerm);
      //complex<double> term1 = refHelper.fMat.row(tableIndexi * nopen + tableIndexa) * refHelper.thetaInv.col(tableIndexi);
      //complex<double> term2 = refHelper.fMat.row(tableIndexj * nopen + tableIndexb) * refHelper.thetaInv.col(tableIndexj);
      //complex<double> term3 = refHelper.fMat.row(tableIndexj * nopen + tableIndexb) * refHelper.thetaInv.col(tableIndexi);
      //complex<double> term4 = refHelper.fMat.row(tableIndexi * nopen + tableIndexa) * refHelper.thetaInv.col(tableIndexj);
      complex<double> term1 = refHelper.rTable[0](tableIndexa, tableIndexi);
      complex<double> term2 = refHelper.rTable[0](tableIndexb, tableIndexj);
      complex<double> term3 = refHelper.rTable[0](tableIndexb, tableIndexi);
      complex<double> term4 = refHelper.rTable[0](tableIndexa, tableIndexj);
      summand1 = term1 * term2 - term3 * term4;
      //complex<double> term5 = refHelper.fMat.row(tableIndexi * nopen + tableIndexa) * refHelper.thetaInv * (-refHelper.fMat.transpose().col(tableIndexj * nopen + tableIndexb));
      complex<double> term5 = refHelper.rTable[1](tableIndexa, tableIndexb);
      summand2 = refHelper.thetaInv(tableIndexi, tableIndexj) * (term5 + crossTerm);
    }
    else { 
      crossTerm = (ref.getPairMat())(a + sz1 * norbs, b + sz2 * norbs);
      //summand1 = refHelper.rTable[0](tableIndexj * nopen + tableIndexb, tableIndexj) * refHelper.rTable[0](tableIndexi * nopen + tableIndexa, tableIndexi) 
      //    - refHelper.rTable[0](tableIndexi * nopen + tableIndexa, tableIndexj) * refHelper.rTable[0](tableIndexj * nopen + tableIndexb, tableIndexi);
      //summand2 = refHelper.thetaInv(tableIndexj, tableIndexi) * (refHelper.rTable[1](tableIndexj * nopen + tableIndexb, tableIndexi * nopen + tableIndexa) + crossTerm);
      //complex<double> term1 = refHelper.fMat.row(tableIndexj * nopen + tableIndexb) * refHelper.thetaInv.col(tableIndexj);
      //complex<double> term2 = refHelper.fMat.row(tableIndexi * nopen + tableIndexa) * refHelper.thetaInv.col(tableIndexi);
      //complex<double> term3 = refHelper.fMat.row(tableIndexi * nopen + tableIndexa) * refHelper.thetaInv.col(tableIndexj);
      //complex<double> term4 = refHelper.fMat.row(tableIndexj * nopen + tableIndexb) * refHelper.thetaInv.col(tableIndexi);
      complex<double> term1 = refHelper.rTable[0](tableIndexb, tableIndexj);
      complex<double> term2 = refHelper.rTable[0](tableIndexa, tableIndexi);
      complex<double> term3 = refHelper.rTable[0](tableIndexa, tableIndexj);
      complex<double> term4 = refHelper.rTable[0](tableIndexb, tableIndexi);
      summand1 = term1 * term2 - term3 * term4;
      //complex<double> term5 = refHelper.fMat.row(tableIndexj * nopen + tableIndexb) * refHelper.thetaInv * (-refHelper.fMat.transpose().col(tableIndexi * nopen + tableIndexa));
      complex<double> term5 = refHelper.rTable[1](tableIndexb, tableIndexa);
      summand2 = refHelper.thetaInv(tableIndexj, tableIndexi) * (term5 + crossTerm);
    }
    //cout << "double   " << crossTerm << "   " << summand1 << "  " << summand2 << endl; 
    return ((summand1 + summand2) * refHelper.thetaPfaff).real() / (refHelper.thetaPfaff).real();
  }
  
  double getDetFactor(std::array<unordered_set<int>, 2> &from, std::array<unordered_set<int>, 2> &to) const
  {
    return 0.;
  }
  
  void update(int i, int a, bool sz, const Pfaffian &ref, Corr &corr, bool doparity = true)
  {
    double p = 1.0;
    p *= d.parity(a, i, sz);
    d.setocc(i, sz, false);
    d.setocc(a, sz, true);
    refHelper.excitationUpdate(ref, i, a, sz, p, d);
    corrHelper.updateHelper(corr, d, i, a, sz);
  }
  
  void updateWalker(const Pfaffian& ref, Corr &corr, int ex1, int ex2, bool doparity = true)
  {
    int norbs = Determinant::norbs;
    int I = ex1 / 2 / norbs, A = ex1 - 2 * norbs * I;
    int J = ex2 / 2 / norbs, B = ex2 - 2 * norbs * J;
    
    if (I % 2 == 0)
      update(I / 2, A / 2, 0, ref, corr, doparity);
    else
      update(I / 2, A / 2, 1, ref, corr, doparity);
  
    if (ex2 != 0) {
      if (J % 2 == 1) 
        update(J / 2, B / 2, 1, ref, corr, doparity);
      else 
        update(J / 2, B / 2, 0, ref, corr, doparity);
    }
  }
  
  void exciteWalker(const Pfaffian& ref, Corr &corr, int excite1, int excite2, int norbs)
  {
    int I1 = excite1 / (2 * norbs), A1 = excite1 % (2 * norbs);
  
    if (I1 % 2 == 0)
      update(I1 / 2, A1 / 2, 0, ref, corr);
    else
      update(I1 / 2, A1 / 2, 1, ref, corr);
  
    if (excite2 != 0) {
      int I2 = excite2 / (2 * norbs), A2 = excite2 % (2 * norbs);
      if (I2 % 2 == 0)
        update(I2 / 2, A2 / 2, 0, ref, corr);
      else
        update(I2 / 2, A2 / 2, 1, ref, corr);
    }
  }
  
  void OverlapWithGradient(const Pfaffian &ref, Eigen::VectorBlock<VectorXd> &grad) const
  {
    if (schd.optimizeOrbs) {
      int norbs = Determinant::norbs;
      Determinant walkerDet = d;
      double detOvlp = getDetOverlap(ref);
      //K and L are relative row and col indices
      int K = 0;
      for (int k = 0; k < norbs; k++) { //walker indices on the row
        if (walkerDet.getoccA(k)) {
          int L = 0;
          for (int l = 0; l < norbs; l++) {
            if (walkerDet.getoccA(l)) {
              grad(4 * k * norbs + 2 * l) += (refHelper.thetaInv(L, K) * refHelper.thetaPfaff).real() / detOvlp / 2;
              grad(4 * k * norbs + 2 * l + 1) += (- refHelper.thetaInv(L, K) * refHelper.thetaPfaff).imag() / detOvlp / 2;
              L++;
            }
          }
          for (int l = 0; l < norbs; l++) {
            if (walkerDet.getoccB(l)) {
              grad(4 * k * norbs + 2 * norbs + 2 * l) += (refHelper.thetaInv(L, K) * refHelper.thetaPfaff).real() / detOvlp / 2;
              grad(4 * k * norbs + 2 * norbs + 2 * l + 1) += (- refHelper.thetaInv(L, K) * refHelper.thetaPfaff).imag() / detOvlp / 2;
              L++;
            }
          }
          K++;
        }
      }
      for (int k = 0; k < norbs; k++) { //walker indices on the row
        if (walkerDet.getoccB(k)) {
          int L = 0;
          for (int l = 0; l < norbs; l++) {
            if (walkerDet.getoccA(l)) {
              grad(4 * norbs * norbs + 4 * k * norbs + 2 * l) += (refHelper.thetaInv(L, K) * refHelper.thetaPfaff).real() / detOvlp / 2;
              grad(4 * norbs * norbs + 4 * k * norbs + 2 * l + 1) += (- refHelper.thetaInv(L, K) * refHelper.thetaPfaff).imag() / detOvlp / 2;
              L++;
            }
          }
          for (int l = 0; l < norbs; l++) {
            if (walkerDet.getoccB(l)) {
              grad(4 * norbs * norbs + 4 * k * norbs + 2 * norbs + 2 * l) += (refHelper.thetaInv(L, K) * refHelper.thetaPfaff).real() / detOvlp / 2;
              grad(4 * norbs * norbs + 4 * k * norbs + 2 * norbs + 2 * l + 1) += (- refHelper.thetaInv(L, K) * refHelper.thetaPfaff).imag() / detOvlp / 2;
              L++;
            }
          }
          K++;
        }
      }
    }
  }
  
  friend ostream& operator<<(ostream& os, const Walker<Corr, Pfaffian>& w) {
    os << w.d << endl << endl;
    os << "fMat\n" << w.refHelper.fMat << endl << endl;
    os << "fThetaInv\n" << w.refHelper.rTable[0] << endl << endl;
    os << "fThetaInvf\n" << w.refHelper.rTable[1] << endl << endl;
    os << "pfaff\n" << w.refHelper.thetaPfaff << endl << endl;
    os << "thetaInv\n" << w.refHelper.thetaInv << endl << endl;
    return os;
  }

  void OverlapWithLocalEnergyGradient(const Corr &corr, const Pfaffian &ref, workingArray &work, Eigen::VectorXd &gradEloc) const
  {
    VectorXd v = VectorXd::Zero(corr.getNumVariables() + ref.getNumVariables());
    corr.getVariables(v);
    Eigen::VectorBlock<VectorXd> vtail = v.tail(v.rows() - corr.getNumVariables());
    ref.getVariables(vtail);
    LocalEnergySolver Solver(d, work);
    double Eloctest = 0.0;
    stan::math::gradient(Solver, v, Eloctest, gradEloc);
    //make sure no nan values persist
    for (int i = 0; i < gradEloc.rows(); i++)
    {
      if (std::isnan(gradEloc(i)))
        gradEloc(i) = 0.0;
    }
    if (schd.debug)
    {
      //cout << Eloc << "\t|\t" << Eloctest << endl;
      cout << Eloctest << endl;

      //below is very expensive and used only for debugging
      Eigen::VectorXd finiteGradEloc = Eigen::VectorXd::Zero(v.size());
      for (int i = 0; i < v.size(); ++i)
      {
        double dt = 0.00001;
        Eigen::VectorXd vdt = v;
        vdt(i) += dt;
        finiteGradEloc(i) = (Solver(vdt) - Eloctest) / dt;
      }
      for (int i = 0; i < v.size(); ++i)
      {
        cout << finiteGradEloc(i) << "\t" << gradEloc(i) << endl;
      }
    }
  }
};

#endif
