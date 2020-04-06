#include <vector>
#include "LocalEnergy.h"
#include "Determinants.h"
#include "workingArray.h"
#include "global.h"
#include "input.h"
#include "igl/slice.h"
#include "stan/math.hpp"
#include "Complex.h"

//functions for jastrow vars
template<typename T>
void BuildJastrowVars(const Eigen::Matrix<T, Eigen::Dynamic, 1> &vars, const Determinant &D, Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> &J, Eigen::Matrix<T, Eigen::Dynamic, 1> &Jmid, int &numVars)
{
    //pull jastrows from vars
    int norbs = Determinant::norbs;
    int nalpha = Determinant::nalpha;
    int nbeta = Determinant::nbeta;
    J.setZero(2 * norbs, 2 * norbs);
    for (int i = 0; i < 2 * norbs; i++)
    {
        for (int j = 0; j <= i; j++)
        {
            J(i, j) = vars[numVars];
            numVars++;
        }
    }
    //intermediates for efficient evaluation
    std::vector<int> open, closed;
    D.getOpenClosed(open, closed);
    Jmid.setZero(2 * norbs);
    for (int i = 0; i < 2 * norbs; i++)
    {
        Jmid[i] = J(i,i);
        for (int j = 0; j < closed.size(); j++)
        {
            if (closed[j] != i)
                Jmid[i] *= J(std::max(i, closed[j]), std::min(i, closed[j]));
        }
    }
}
template
void BuildJastrowVars(const Eigen::Matrix<double, Eigen::Dynamic, 1> &vars, const Determinant &D, Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> &J, Eigen::Matrix<double, Eigen::Dynamic, 1> &Jmid, int &numVars);
template
void BuildJastrowVars(const Eigen::Matrix<stan::math::var, Eigen::Dynamic, 1> &vars, const Determinant &D, Eigen::Matrix<stan::math::var, Eigen::Dynamic, Eigen::Dynamic> &J, Eigen::Matrix<stan::math::var, Eigen::Dynamic, 1> &Jmid, int &numVars);

//functions for hf vars
void concatenateGhf(const vector<int>& v1, const vector<int>& v2, vector<int>& result)
{
  int norbs = Determinant::norbs;
  result.clear();
  result = v1;
  result.insert(result.end(), v2.begin(), v2.end());    
  for (int j = v1.size(); j < v1.size() + v2.size(); j++)
    result[j] += norbs;
}

template<typename T>
void BuildOrbitalVars(const Eigen::Matrix<T, Eigen::Dynamic, 1> &vars, const Determinant &D, std::array<Complex<T>, 2> &thetaDet, std::array<Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic>, 2> &R, int &numVars)
{
    int norbs = Determinant::norbs;
    int nalpha = Determinant::nalpha;
    int nbeta = Determinant::nbeta;

    //pull orbitals from vars
    std::array<Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic>, 2> HF;
    if (schd.hf == "rhf")
    {
        int size = norbs;
        HF[0].resize(size, size);
        HF[1].resize(size, size);
        for (int i = 0; i < size; i++)
        {
            for (int j = 0; j < size; j++)
            {
                HF[0](i, j).real(vars[numVars + 2 * i * norbs + 2 * j]);
                HF[0](i, j).imag(vars[numVars + 2 * i * norbs + 2 * j + 1]);
                HF[1](i, j).real(vars[numVars + 2 * i * norbs + 2 * j]);
                HF[1](i, j).imag(vars[numVars + 2 * i * norbs + 2 * j + 1]);
            }
        }
        numVars += 2 * norbs * norbs;
    }
    else if (schd.hf == "uhf")
    {
        int size = norbs;
        HF[0].resize(size, size);
        HF[1].resize(size, size);
        for (int i = 0; i < size; i++)
        {
            for (int j = 0; j < size; j++)
            {
                HF[0](i, j).real(vars[numVars + 2 * i * norbs + 2 * j]);
                HF[0](i, j).imag(vars[numVars + 2 * i * norbs + 2 * j + 1]);
                HF[1](i, j).real(vars[numVars + 2 * norbs * norbs + 2 * i * norbs + 2 * j]);
                HF[1](i, j).imag(vars[numVars + 2 * norbs * norbs + 2 * i * norbs + 2 * j + 1]);
            }
        }
        numVars += 2 * 2 * norbs * norbs;
    }
    else if (schd.hf == "ghf")
    {
        int size = 2 * norbs;
        HF[0].resize(size, size);
        HF[1].resize(size, size);
        for (int i = 0; i < size; i++)
        {
            for (int j = 0; j < size; j++)
            {
                HF[0](i, j).real(vars[numVars + 4 * i * norbs + 2 * j]);
                HF[0](i, j).imag(vars[numVars + 4 * i * norbs + 2 * j + 1]);
                HF[1](i, j).real(vars[numVars + 4 * i * norbs + 2 * j]);
                HF[1](i, j).imag(vars[numVars + 4 * i * norbs + 2 * j + 1]);
            }
        }
        numVars += 2 * 4 * norbs * norbs;
    }
    
    //hartree fock reference
    std::array<std::vector<int>, 2> closedOrbsRef;
    if (schd.hf == "rhf" || schd.hf == "uhf")
    {
        for (int i = 0; i < nalpha; i++)
        {
            closedOrbsRef[0].push_back(i);
        }
        for (int i = 0; i < nbeta; i++)
        {
            closedOrbsRef[1].push_back(i);
        }
    }
    else if (schd.hf == "ghf")
    {
        int nelec = nalpha + nbeta;
        for (int i = 0; i < nelec; i++)
        {
            closedOrbsRef[0].push_back(i);
            closedOrbsRef[1].push_back(i);
        }
    }
        
    //Calculate overlaps of reference and current determinant
    std::array<std::vector<int>, 2> closedOrbs, openOrbs;
    D.getOpenClosedAlphaBeta(openOrbs[0], closedOrbs[0], openOrbs[1], closedOrbs[1]);
    if (schd.hf == "rhf" || schd.hf == "uhf")
    {
        for (int sz = 0; sz < 2; sz++)
        {
            Eigen::Map<Eigen::VectorXi> rowClosed(&closedOrbs[sz][0], closedOrbs[sz].size());
            Eigen::Map<Eigen::VectorXi> colClosed(&closedOrbsRef[sz][0], closedOrbsRef[sz].size());

            Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> O;
            igl::slice(HF[sz], rowClosed, colClosed, O);

            Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> OInv;
            //Eigen::FullPivLU<Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic>> lua(O);
            //OInv = lua.inverse();
            //thetaDet[sz] = lua.determinant();
            OInv = stan::math::inverse(O);
            thetaDet[sz] = stan::math::determinant(O);

            Eigen::Map<Eigen::VectorXi> rowOpen(&openOrbs[sz][0], openOrbs[sz].size());
            Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> HfO;
            igl::slice(HF[sz], rowOpen, colClosed, HfO);
            R[sz] = HfO * OInv;
            //R[sz] = stan::math::multiply(HfO, OInv);
        }
     }
     else if (schd.hf == "ghf")
     {
         std::vector<int> workingVec;
         concatenateGhf(closedOrbs[0], closedOrbs[1], workingVec);
         Eigen::Map<Eigen::VectorXi> rowClosed(&workingVec[0], workingVec.size());
         Eigen::Map<Eigen::VectorXi> colClosed(&closedOrbsRef[0][0], closedOrbsRef[0].size());

         Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> O;
         igl::slice(HF[0], rowClosed, colClosed, O);

         Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> OInv;
         //Eigen::FullPivLU<Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic>> lua(O);
         //OInv = lua.inverse();
         //thetaDet[0] = lua.determinant();
         OInv = stan::math::inverse(O);
         thetaDet[0] = stan::math::determinant(O);
         thetaDet[1].real(1.0);
         thetaDet[1].imag(0.0);

         Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> HfO;
         std::vector<int> rowVec;
         concatenateGhf(openOrbs[0], openOrbs[1], rowVec);
         Eigen::Map<Eigen::VectorXi> rowOpen(&rowVec[0], rowVec.size());
         igl::slice(HF[0], rowOpen, colClosed, HfO);
         R[0] = HfO * OInv;
         //R[0] = stan::math::multiply(HfO, OInv);
         R[1] = R[0];
     }
}
template
void BuildOrbitalVars(const Eigen::Matrix<double, Eigen::Dynamic, 1> &vars, const Determinant &D, std::array<Complex<double>, 2> &thetaDet, std::array<Eigen::Matrix<Complex<double>, Eigen::Dynamic, Eigen::Dynamic>, 2> &R, int &numVars);
template
void BuildOrbitalVars(const Eigen::Matrix<stan::math::var, Eigen::Dynamic, 1> &vars, const Determinant &D, std::array<Complex<stan::math::var>, 2> &thetaDet, std::array<Eigen::Matrix<Complex<stan::math::var>, Eigen::Dynamic, Eigen::Dynamic>, 2> &R, int &numVars);

//functions for hf local energy evaluation
void getHfRelIndices(int i, int &relI, int a, int &relA, bool sz, const std::array<std::vector<int>, 2> &closedOrbs, const std::array<std::vector<int>, 2> &openOrbs)
{
    //std::array<vector<int>, 2> closedOrbs, openOrbs;
    //D.getOpenClosedAlphaBeta(openOrbs[0], closedOrbs[0], openOrbs[1], closedOrbs[1]);
    int factor = 0;
    if (schd.hf == "ghf" && sz != 0) 
        factor = 1;
    relI = std::search_n(closedOrbs[sz].begin(), closedOrbs[sz].end(), 1, i) - closedOrbs[sz].begin() + factor * closedOrbs[0].size();
    relA = std::search_n(openOrbs[sz].begin(), openOrbs[sz].end(), 1, a) - openOrbs[sz].begin() + factor * openOrbs[0].size();
    //relA = a + factor * Determinant::norbs;
}

template<typename T>
T JastrowSlaterLocalEnergy(const Determinant &D, const workingArray &work, const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> &J, const Eigen::Matrix<T, Eigen::Dynamic, 1> &Jmid, const std::array<Complex<T>, 2> &thetaDet, const std::array<Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic>, 2> &R)
{
    int norbs = Determinant::norbs;
    int nalpha = Determinant::nalpha;
    int nbeta = Determinant::nbeta;
    std::array<vector<int>, 2> closedOrbs, openOrbs;
    D.getOpenClosedAlphaBeta(openOrbs[0], closedOrbs[0], openOrbs[1], closedOrbs[1]);
    //workingArray work;
    //work.setCounterToZero();
    //generateAllScreenedSingleExcitation(D, schd.epsilon, schd.screen, work, false);
    //generateAllScreenedDoubleExcitation(D, schd.epsilon, schd.screen, work, false);
    T Eloc = D.Energy(I1, I2, coreE);
    for (int l = 0; l < work.nExcitations; l++)
    {
        int ex1 = work.excitation1[l], ex2 = work.excitation2[l];
        double tia = work.HijElement[l];

        int i = ex1 / 2 / norbs, a = ex1 - 2 * norbs * i;
        int j = ex2 / 2 / norbs, b = ex2 - 2 * norbs * j;

        int ri, ra, rj, rb;
        int sz1, sz2;

        T ovlpRatio = 1.0;
        if (j == b && b == 0) //single excitations
        {
            ovlpRatio *= Jmid[a] / Jmid[i] / J(std::max(i,a), std::min(i,a));
            if (i % 2 == 0) //alpha
                sz1 = 0; 
            else    //beta
                sz1 = 1;
            getHfRelIndices(i / 2, ri, a / 2, ra, sz1, closedOrbs, openOrbs);
            ovlpRatio *= (R[sz1](ra, ri) * thetaDet[0] * thetaDet[1]).real() / (thetaDet[0] * thetaDet[1]).real();
        }
        else //double excitations
        {
            ovlpRatio *= Jmid[a] * Jmid[b] * J(std::max(a,b), std::min(a,b)) * J(std::max(i,j), std::min(i,j)) / Jmid[i] / Jmid[j] / J(std::max(a,i), std::min(a,i)) / J(std::max(a,j), std::min(a,j)) / J(std::max(i,b), std::min(i,b)) / J(std::max(j,b), std::min(j,b));
            if (i % 2 == j % 2 && i % 2 == 0) //aa to aa
            {
                sz1 = 0;
                sz2 = 0;
            }
            else if (i % 2 == j % 2 && i % 2 == 1) //bb to bb
            {
                sz1 = 1;
                sz2 = 1;
            }
            else if (i % 2 != j % 2 && i % 2 == 0) //ab to ab
            {
                sz1 = 0;
                sz2 = 1;
            }
            else  //ba to ba
            {
                sz1 = 1;
                sz2 = 0;
            }
            getHfRelIndices(i / 2, ri, a / 2, ra, sz1, closedOrbs, openOrbs);
            getHfRelIndices(j / 2, rj, b / 2, rb, sz2, closedOrbs, openOrbs); 
            if (sz1 == sz2 || schd.hf == "ghf")
            {
                ovlpRatio *= ((R[sz1](ra, ri) * R[sz1](rb, rj) - R[sz1](rb, ri) * R[sz1](ra, rj))* thetaDet[0] * thetaDet[1]).real() / (thetaDet[0] * thetaDet[1]).real();
            }
            else
            {
                ovlpRatio *= ((R[sz1](ra, ri) * R[sz2](rb, rj)) * thetaDet[0] * thetaDet[1]).real() / (thetaDet[0] * thetaDet[1]).real();
            }
        }
        Eloc += tia * ovlpRatio;
    }
    return Eloc;
}    
template
double JastrowSlaterLocalEnergy(const Determinant &D, const workingArray &work, const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> &J, const Eigen::Matrix<double, Eigen::Dynamic, 1> &Jmid, const std::array<Complex<double>, 2> &thetaDet, const std::array<Eigen::Matrix<Complex<double>, Eigen::Dynamic, Eigen::Dynamic>, 2> &R);
template
stan::math::var JastrowSlaterLocalEnergy(const Determinant &D, const workingArray &work, const Eigen::Matrix<stan::math::var, Eigen::Dynamic, Eigen::Dynamic> &J, const Eigen::Matrix<stan::math::var, Eigen::Dynamic, 1> &Jmid, const std::array<Complex<stan::math::var>, 2> &thetaDet, const std::array<Eigen::Matrix<Complex<stan::math::var>, Eigen::Dynamic, Eigen::Dynamic>, 2> &R);


//functions for pfaffian vars
template <typename T>
Complex<T> calcPfaffian(const Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> &mat)
{
  Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> matCopy = mat;
  int size = mat.rows();
  Complex<T> one(1.0, 0.0);
  Complex<T> zero(0.0, 0.0);
  Complex<T> pfaffian(1.0, 0.0);
  int i = 0;
  while (i < size-1) {
    int currentSize = size-i;
    Eigen::Matrix<T, Eigen::Dynamic, 1> colNorm = matCopy.col(i).tail(currentSize-1).cwiseAbs();
    Eigen::VectorXcd::Index maxIndex;
    colNorm.maxCoeff(&maxIndex);
    int ip = i+1+maxIndex;
    //pivot if necessary
    if (ip != i+1) {
      matCopy.block(i,i,currentSize,currentSize).row(1).swap(matCopy.block(i,i,currentSize, currentSize).row(ip-i));
      matCopy.block(i,i,currentSize,currentSize).col(1).swap(matCopy.block(i,i,currentSize, currentSize).col(ip-i));
      pfaffian *= -one;
    }
    //gauss elimination
    if (matCopy(i,i+1) != zero) {
      pfaffian *= matCopy(i,i+1);
      Eigen::Matrix<Complex<T>, Eigen::Dynamic, 1> tau = matCopy.row(i).tail(currentSize-2);
      tau /= matCopy(i,i+1);
      if (i+2 < size) {
        matCopy.block(i+2,i+2,currentSize-2,currentSize-2) += tau * matCopy.col(i+1).tail(currentSize-2).transpose(); 
        matCopy.block(i+2,i+2,currentSize-2,currentSize-2) -= matCopy.col(i+1).tail(currentSize-2) * tau.transpose(); 
      }
    }
    else return zero;
    i++; i++;
  }
  return pfaffian;
}
template
Complex<double> calcPfaffian(const Eigen::Matrix<Complex<double>, Eigen::Dynamic, Eigen::Dynamic> &mat);
template
Complex<stan::math::var> calcPfaffian(const Eigen::Matrix<Complex<stan::math::var>, Eigen::Dynamic, Eigen::Dynamic> &mat);


template <typename T>
void BuildPfaffianVars(const Eigen::Matrix<T, Eigen::Dynamic, 1> &vars, const Determinant &D, Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> &pairMat, Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> &thetaInv, Complex<T> &thetaPfaff, std::array<Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic>, 2> &rTable, int &numVars)
{
    //read pair matrix
    int norbs = Determinant::norbs;
    pairMat.setZero(2 * norbs, 2 * norbs);
    for (int i = 0; i < 2 * norbs; i++)
    {
        for (int j = 0; j < 2 * norbs; j++)
        {
            pairMat(i, j).real(vars[numVars + 4 * i * norbs + 2 * j]);
            pairMat(i, j).imag(vars[numVars + 4 * i * norbs + 2 * j + 1]);
        }
    }
    numVars += 8 * norbs * norbs;
    //Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> transpose = pairMat.transpose();
    pairMat = (pairMat - pairMat.transpose().eval())/2; 

    //fill open closed orbs
    std::array<std::vector<int>, 2> closedOrbs, openOrbs;
    D.getOpenClosedAlphaBeta(openOrbs[0], closedOrbs[0], openOrbs[1], closedOrbs[1]);
    //nopen and nclosed
    int nclosed = closedOrbs[0].size() + closedOrbs[1].size();
    int nopen = openOrbs[0].size() + openOrbs[1].size();

    //fMat
    Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> fMat;
    fMat.setZero(nopen * nclosed, nclosed);

    //map closed into eigen object
    Eigen::Map<Eigen::VectorXi> closedAlpha(&closedOrbs[0][0], closedOrbs[0].size());
    Eigen::Map<Eigen::VectorXi> closedBeta(&closedOrbs[1][0], closedOrbs[1].size());
    //make closed array of absoute indices 
    Eigen::VectorXi closed(nclosed);
    closed << closedAlpha, (closedBeta.array() + norbs).matrix();
    //calculate thetaInv 
    Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> theta;
    igl::slice(pairMat, closed, closed, theta);
    //Eigen::FullPivLU<Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic>> lua(theta);
    //thetaInv = lua.inverse();
    thetaInv = stan::math::inverse(theta);
    thetaPfaff = calcPfaffian<T>(theta);

    //map open into eigen object
    Eigen::Map<Eigen::VectorXi> openAlpha(&openOrbs[0][0], openOrbs[0].size());
    Eigen::Map<Eigen::VectorXi> openBeta(&openOrbs[1][0], openOrbs[1].size());
    //make open array of absolute indices 
    Eigen::VectorXi open(nopen);
    open << openAlpha, (openBeta.array() + norbs).matrix();
       
/*
    Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> fRow;
    Eigen::VectorXi rowSlice(1), colSlice(nclosed);                     
    for (int i = 0; i < closed.size(); i++)
    {                   
        for (int a = 0; a < open.size(); a++)
        {
            colSlice = closed;                                 
            rowSlice[0] = open[a];
            colSlice[i] = open[a];
            igl::slice(pairMat, rowSlice, colSlice, fRow);
            fMat.block(i * open.size() + a, 0, 1, closed.size()) = fRow;
        }
    }   
*/ 
    igl::slice(pairMat, open, closed, fMat);
    rTable[0] = fMat * thetaInv;
    //rTable[0] = stan::math::multiply(fMat, thetaInv);
    rTable[1] = - rTable[0] * fMat.transpose();
    //rTable[1] = stan::math::multiply(-rTable[0], fMat.transpose());
}

template
void BuildPfaffianVars(const Eigen::Matrix<double, Eigen::Dynamic, 1> &vars, const Determinant &D, Eigen::Matrix<Complex<double>, Eigen::Dynamic, Eigen::Dynamic> &pairMat, Eigen::Matrix<Complex<double>, Eigen::Dynamic, Eigen::Dynamic> &thetaInv, Complex<double> &thetaPfaff, std::array<Eigen::Matrix<Complex<double>, Eigen::Dynamic, Eigen::Dynamic>, 2> &rTable, int &numVars);
template
void BuildPfaffianVars(const Eigen::Matrix<stan::math::var, Eigen::Dynamic, 1> &vars, const Determinant &D, Eigen::Matrix<Complex<stan::math::var>, Eigen::Dynamic, Eigen::Dynamic> &pairMat, Eigen::Matrix<Complex<stan::math::var>, Eigen::Dynamic, Eigen::Dynamic> &thetaInv, Complex<stan::math::var> &thetaPfaff, std::array<Eigen::Matrix<Complex<stan::math::var>, Eigen::Dynamic, Eigen::Dynamic>, 2> &rTable, int &numVars);

//functions for pfaffian local energy evaluation
void getPfaffRelIndices(int i, int &relI, int a, int &relA, bool sz, const std::array<vector<int>, 2> &closedOrbs, const std::array<vector<int>, 2> &openOrbs)
{
    //std::array<vector<int>, 2> closedOrbs, openOrbs;
    //D.getOpenClosedAlphaBeta(openOrbs[0], closedOrbs[0], openOrbs[1], closedOrbs[1]);
    int factor = 0;
    if (sz != 0) factor = 1;
    relI = std::search_n(closedOrbs[sz].begin(), closedOrbs[sz].end(), 1, i) - closedOrbs[sz].begin() + factor * closedOrbs[0].size();
    relA = std::search_n(openOrbs[sz].begin(), openOrbs[sz].end(), 1, a) - openOrbs[sz].begin() + factor * openOrbs[0].size();
}

template<typename T>
T JastrowPfaffianLocalEnergy(const Determinant &D, const workingArray &work, const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> &J, const Eigen::Matrix<T, Eigen::Dynamic, 1> &Jmid, const Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> &pairMat, const Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic> &thetaInv, const Complex<T> &thetaPfaff, const std::array<Eigen::Matrix<Complex<T>, Eigen::Dynamic, Eigen::Dynamic>, 2> &rTable)
{
    int norbs = Determinant::norbs;
    //fill open closed orbs
    std::array<std::vector<int>, 2> closedOrbs, openOrbs;
    D.getOpenClosedAlphaBeta(openOrbs[0], closedOrbs[0], openOrbs[1], closedOrbs[1]);
    //nopen and nclosed
    int nclosed = closedOrbs[0].size() + closedOrbs[1].size();
    int nopen = openOrbs[0].size() + openOrbs[1].size();

    //workingArray work;
    //work.setCounterToZero();
    //generateAllScreenedSingleExcitation(D, schd.epsilon, schd.screen, work, false);
    //generateAllScreenedDoubleExcitation(D, schd.epsilon, schd.screen, work, false);
    T Eloc = D.Energy(I1, I2, coreE);
    for (int l = 0; l < work.nExcitations; l++)
    {
        int ex1 = work.excitation1[l], ex2 = work.excitation2[l];
        double tia = work.HijElement[l];

        int i = ex1 / 2 / norbs, a = ex1 - 2 * norbs * i;
        int j = ex2 / 2 / norbs, b = ex2 - 2 * norbs * j;

        int ri, ra, rj, rb;
        int sz1, sz2;

        T ovlpRatio = 1.0;
        if (j == b && b == 0) //single excitations
        {
            ovlpRatio *= Jmid[a] / Jmid[i] / J(std::max(i,a), std::min(i,a));
            if (i % 2 == 0) //alpha
                sz1 = 0; 
            else    //beta
                sz1 = 1;
            getPfaffRelIndices(i / 2, ri, a / 2, ra, sz1, closedOrbs, openOrbs);
            //ovlpRatio *= fMat.row(ri * nopen + ra) * thetaInv.col(ri);
            ovlpRatio *= (rTable[0](ra, ri) * thetaPfaff).real() / thetaPfaff.real();
        }
        else //double excitations
        {
            ovlpRatio *= Jmid[a] * Jmid[b] * J(std::max(a,b), std::min(a,b)) * J(std::max(i,j), std::min(i,j)) / Jmid[i] / Jmid[j] / J(std::max(a,i), std::min(a,i)) / J(std::max(a,j), std::min(a,j)) / J(std::max(i,b), std::min(i,b)) / J(std::max(j,b), std::min(j,b));
            if (i % 2 == j % 2 && i % 2 == 0) //aa to aa
            {
                sz1 = 0;
                sz2 = 0;
            }
            else if (i % 2 == j % 2 && i % 2 == 1) //bb to bb
            {
                sz1 = 1;
                sz2 = 1;
            }
            else if (i % 2 != j % 2 && i % 2 == 0) //ab to ab
            {
                sz1 = 0;
                sz2 = 1;
            }
            else  //ba to ba
            {
                sz1 = 1;
                sz2 = 0;
            }
            getPfaffRelIndices(i / 2, ri, a / 2, ra, sz1, closedOrbs, openOrbs);
            getPfaffRelIndices(j / 2, rj, b / 2, rb, sz2, closedOrbs, openOrbs); 

            Complex<T> summand1, summand2, crossTerm;
            if (ri < rj)
            {
                crossTerm = pairMat(b / 2 + sz2 * norbs, a / 2 + sz1 * norbs);
/*
                T term1 = fMat.row(ri * nopen + ra) * thetaInv.col(ri);
                T term2 = fMat.row(rj * nopen + rb) * thetaInv.col(rj);
                T term3 = fMat.row(rj * nopen + rb) * thetaInv.col(ri);
                T term4 = fMat.row(ri * nopen + ra) * thetaInv.col(rj);
*/
                Complex<T> term1 = rTable[0](ra, ri); 
                Complex<T> term2 = rTable[0](rb, rj);
                Complex<T> term3 = rTable[0](rb, ri);
                Complex<T> term4 = rTable[0](ra, rj);
                summand1 = term1 * term2 - term3 * term4;
                //T term5 = fMat.row(ri * nopen + ra) * thetaInv * (-fMat.transpose().col(rj * nopen + rb));
                Complex<T> term5 = rTable[1](ra, rb);
                summand2 = thetaInv(ri, rj) * (term5 + crossTerm);
             }
             else
             { 
                 crossTerm = pairMat(a / 2 + sz1 * norbs, b / 2 + sz2 * norbs);
/*
                 T term1 = fMat.row(rj * nopen + rb) * thetaInv.col(rj);
                 T term2 = fMat.row(ri * nopen + ra) * thetaInv.col(ri);
                 T term3 = fMat.row(ri * nopen + ra) * thetaInv.col(rj);
                 T term4 = fMat.row(rj * nopen + rb) * thetaInv.col(ri);
*/
                Complex<T> term1 = rTable[0](rb, rj); 
                Complex<T> term2 = rTable[0](ra, ri);
                Complex<T> term3 = rTable[0](ra, rj);
                Complex<T> term4 = rTable[0](rb, ri);
                 summand1 = term1 * term2 - term3 * term4;
                 //T term5 = fMat.row(rj * nopen + rb) * thetaInv * (-fMat.transpose().col(ri * nopen + ra));
                Complex<T> term5 = rTable[1](rb, ra);
                 summand2 = thetaInv(rj, ri) * (term5 + crossTerm);
             }
             ovlpRatio *= ((summand1 + summand2) * thetaPfaff).real() / thetaPfaff.real();
        }
        Eloc += tia * ovlpRatio;
    }
    return Eloc;
}
template
double JastrowPfaffianLocalEnergy(const Determinant &D, const workingArray &work, const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> &J, const Eigen::Matrix<double, Eigen::Dynamic, 1> &Jmid, const Eigen::Matrix<Complex<double>, Eigen::Dynamic, Eigen::Dynamic> &pairMat, const Eigen::Matrix<Complex<double>, Eigen::Dynamic, Eigen::Dynamic> &thetaInv, const Complex<double> &thetaPfaff, const std::array<Eigen::Matrix<Complex<double>, Eigen::Dynamic, Eigen::Dynamic>, 2> &rTable);
template
stan::math::var JastrowPfaffianLocalEnergy(const Determinant &D, const workingArray &work, const Eigen::Matrix<stan::math::var, Eigen::Dynamic, Eigen::Dynamic> &J, const Eigen::Matrix<stan::math::var, Eigen::Dynamic, 1> &Jmid, const Eigen::Matrix<Complex<stan::math::var>, Eigen::Dynamic, Eigen::Dynamic> &pairMat, const Eigen::Matrix<Complex<stan::math::var>, Eigen::Dynamic, Eigen::Dynamic> &thetaInv, const Complex<stan::math::var> &thetaPfaff, const std::array<Eigen::Matrix<Complex<stan::math::var>, Eigen::Dynamic, Eigen::Dynamic>, 2> &rTable);
