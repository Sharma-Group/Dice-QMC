#include "slaterBasis.h"
#include <string>
#include "readSlater.h"

using namespace std;
using namespace Eigen;


double STOradialNorm(double ex, int n) {
  return pow(2*ex, 1.*n+0.5)/sqrt(tgamma(2*n+1));
}

double GTOradialNorm(double ex, int l) {
  double n = (2*l+2+1)*0.5;
  double integral = tgamma(n)/(2. * pow(2*ex, n));
  return 1./sqrt(integral);
}



void slaterBasis::read() {
  string fname = "slaterBasis.json";
  slaterParser sp(fname);

  sp.readBasis(atomName, atomCoord, *this);

  norbs = 0;
  //we will use cartesian basis
  for (int i=0; i<atomicBasis.size(); i++) {
    norbs += atomicBasis[i].norbs;
  }
}


int slaterBasis::getNorbs() {return norbs;}


void slaterBasis::maxCoord(vector<Vector3d>& maxr) {
  int index = 0;  
  for (int i=0; i<atomicBasis.size(); i++) {
    
    auto aBasis = atomicBasis[i];
    Vector3d Basiscoord = aBasis.coord;

    //for each basis in aBasis evaluate the value
    for (int j=0; j<aBasis.exponents.size(); j++) {
      double ex = aBasis.exponents[j];
      int l = aBasis.NL[2*j+1];
      int N = aBasis.NL[2*j]-1;

      double rmax = N == 0 ? 1.0/ex/sqrt(3.0) : N/ex/sqrt(3.0); 
      if (l == 0) {
        maxr[index][0] = rmax; maxr[index][1] = rmax; maxr[index][2] = rmax;
        maxr[index] += aBasis.coord;
        index++;
      }
      if (l == 1) {
        maxr[index][0]   = rmax  ;   maxr[index][1]   = 0       ; maxr[index][2]   = 0       ;
        maxr[index+1][0] = 0     ;   maxr[index+1][1] = rmax    ; maxr[index+1][2] = 0       ;
        maxr[index+2][0] = 0     ;   maxr[index+2][1] = 0       ; maxr[index+2][2] = rmax    ;

        maxr[index  ] += aBasis.coord;
        maxr[index+1] += aBasis.coord;
        maxr[index+2] += aBasis.coord;
        index += 3;
      }
      if (l == 2) {
        maxr[index][0]   = 2*rmax;   maxr[index][1]   = 0       ; maxr[index][2]   = 0       ;
        maxr[index+1][0] = rmax  ;   maxr[index+1][1] = rmax    ; maxr[index+1][2] = 0       ;
        maxr[index+2][0] = rmax  ;   maxr[index+2][1] = 0       ; maxr[index+2][2] = rmax    ;
        maxr[index+3][0] = 0     ;   maxr[index+3][1] = 2*rmax  ; maxr[index+3][2] = 0       ;
        maxr[index+4][0] = 0     ;   maxr[index+4][1] = rmax    ; maxr[index+4][2] = rmax    ;
        maxr[index+5][0] = 0     ;   maxr[index+5][1] = 0       ; maxr[index+5][2] = 2*rmax  ;

        maxr[index  ] += aBasis.coord;
        maxr[index+1] += aBasis.coord;
        maxr[index+2] += aBasis.coord;
        maxr[index+3] += aBasis.coord;
        maxr[index+4] += aBasis.coord;
        maxr[index+5] += aBasis.coord;
        index += 6;
      }
      if (l == 3) {
        maxr[index][0]   = 3*rmax;   maxr[index][1]   = 0       ; maxr[index][2]   = 0       ;
        maxr[index+1][0] = 2*rmax;   maxr[index+1][1] = rmax    ; maxr[index+1][2] = 0       ;
        maxr[index+2][0] = 2*rmax;   maxr[index+2][1] = 0       ; maxr[index+2][2] = rmax    ;
        maxr[index+3][0] = rmax  ;   maxr[index+3][1] = 2*rmax  ; maxr[index+3][2] = 0       ;
        maxr[index+4][0] = rmax  ;   maxr[index+4][1] = rmax    ; maxr[index+4][2] = rmax    ;
        maxr[index+5][0] = rmax  ;   maxr[index+5][1] = 0       ; maxr[index+5][2] = 2*rmax  ;
        maxr[index+6][0] = 0     ;   maxr[index+6][1] = 3*rmax  ; maxr[index+6][2] = 0       ;
        maxr[index+7][0] = 0     ;   maxr[index+7][1] = 2*rmax  ; maxr[index+7][2] = rmax    ;
        maxr[index+8][0] = 0     ;   maxr[index+8][1] = rmax    ; maxr[index+8][2] = 2*rmax  ;
        maxr[index+9][0] = 0     ;   maxr[index+9][1] = 0       ; maxr[index+9][2] = 3*rmax  ;

        maxr[index  ] += aBasis.coord;
        maxr[index+1] += aBasis.coord;
        maxr[index+2] += aBasis.coord;
        maxr[index+3] += aBasis.coord;
        maxr[index+4] += aBasis.coord;
        maxr[index+5] += aBasis.coord;
        maxr[index+6] += aBasis.coord;
        maxr[index+7] += aBasis.coord;
        maxr[index+8] += aBasis.coord;
        maxr[index+9] += aBasis.coord;
        index += 10;
      }
    }
  }
}

void slaterBasis::eval(const Vector3d& x, double* values) {

  int index = 0;  
  for (int i=0; i<atomicBasis.size(); i++) {
    
    auto aBasis = atomicBasis[i];
    double xN = x[0] - aBasis.coord[0],
        yN = x[1] - aBasis.coord[1],
        zN = x[2] - aBasis.coord[2];
    double RiN =  pow( pow(xN, 2) +
                       pow(yN, 2) +
                       pow(zN, 2), 0.5);

    //for each basis in aBasis evaluate the value
    for (int j=0; j<aBasis.exponents.size(); j++) {
      int l = aBasis.NL[2*j+1];
      int N = aBasis.NL[2*j]-1;
      double ex = pow(RiN, N) * aBasis.radialNorm[j] * exp(-aBasis.exponents[j]*RiN);

      if (l == 0) { //s
        values[index] = ex * sqrt(1./3.141592653589)/2.0;
        index++;
      }
      if (l == 1) { //p
        double c =  sqrt(3./4.0/3.141592653589);
        values[index]    = xN * ex * c /RiN;
        values[index+1]  = yN * ex * c /RiN;
        values[index+2]  = zN * ex * c /RiN;
        index += 3;
      }
      if (l == 2) {//d
        values[index]    = xN * xN * ex / pow(RiN, 2);
        values[index+1]  = xN * yN * ex / pow(RiN, 2);
        values[index+2]  = xN * zN * ex / pow(RiN, 2);
        values[index+3]  = yN * yN * ex / pow(RiN, 2);
        values[index+4]  = yN * zN * ex / pow(RiN, 2);
        values[index+5]  = zN * zN * ex / pow(RiN, 2);
        index += 6;
      }
      if (l == 3) {//f
        values[index+0 ]    = xN * xN * xN * ex / pow(RiN, 3);
        values[index+1 ]    = xN * xN * yN * ex / pow(RiN, 3);
        values[index+2 ]    = xN * xN * zN * ex / pow(RiN, 3);
        values[index+3 ]    = xN * yN * yN * ex / pow(RiN, 3);
        values[index+4 ]    = xN * yN * zN * ex / pow(RiN, 3);
        values[index+5 ]    = xN * zN * zN * ex / pow(RiN, 3);
        values[index+6 ]    = yN * yN * yN * ex / pow(RiN, 3);
        values[index+7 ]    = yN * yN * zN * ex / pow(RiN, 3);
        values[index+8 ]    = yN * zN * zN * ex / pow(RiN, 3);
        values[index+9 ]    = zN * zN * zN * ex / pow(RiN, 3);
        index += 10;
      }
      
    }
  }
}



void slaterBasis::eval(const vector<Vector3d>& x, vector<double>& values) {
  int index = 0;
  for (int i=0; i<x.size(); i++) {
    eval(x[i], &values[index]);
    index += norbs;
  }
}

void makeDeriv(double* values, int NR, int NX, int NY, int NZ, int &norbs,
               double* xN, double &RiN, double& eta, double& scale,
               int& index, bool mixed = false) {

  double ex   = scale * exp(-eta*RiN);
  double poly = pow(RiN, NR);
  double ang  = pow(xN[0],NX) * pow (xN[1], NY) * pow (xN[2], NZ);
  double fr      = ex*poly;

  //*************************
  double DfDr   = -eta*ex*poly + NR*pow(RiN, NR-1)*ex;

  double DrDx = (xN[0]/RiN);
  double DrDy = (xN[1]/RiN);
  double DrDz = (xN[2]/RiN);

  double DangDx = NX >= 1 ?
      NX * pow(xN[0],NX-1) * pow (xN[1], NY) * pow (xN[2], NZ) :
      0.0;

  double DangDy = NY >= 1 ?
      NY * pow(xN[0],NX) * pow (xN[1], NY-1) * pow (xN[2], NZ) :
      0.0;

  double DangDz = NZ >= 1 ?
      NZ * pow(xN[0],NX) * pow (xN[1], NY) * pow (xN[2], NZ-1) :
      0.0;

  //************************
  double D2fDr2 =  eta*eta*ex*poly
                    + 2 * (-eta*ex)*NR*pow(RiN, NR-1)
                    + NR*(NR-1)*pow(RiN, NR-2)*ex;

  double D2rDx2 = 1./RiN - xN[0]*xN[0]/pow(RiN,3);
  double D2rDy2 = 1./RiN - xN[1]*xN[1]/pow(RiN,3);
  double D2rDz2 = 1./RiN - xN[2]*xN[2]/pow(RiN,3);
  
  double D2angDx2 = NX >= 2 ?
      NX * (NX -1 ) * pow(xN[0],NX-2) * pow (xN[1], NY) * pow (xN[2], NZ) :
      0.0;
  
  double D2angDy2 = NY >= 2 ?
      NY * (NY -1 ) * pow(xN[0],NX) * pow (xN[1], NY-2) * pow (xN[2], NZ) :
      0.0;
  
  double D2angDz2 = NZ >= 2 ?
      NZ * (NZ -1 ) * pow(xN[0],NX) * pow (xN[1], NY) * pow (xN[2], NZ-2) :
      0.0;

  //************************
  
  values[           index] =  fr * ang;
  values[1* norbs + index] =  DfDr * DrDx * ang + fr * DangDx;
  values[2* norbs + index] =  DfDr * DrDy * ang + fr * DangDy;
  values[3* norbs + index] =  DfDr * DrDz * ang + fr * DangDz;
  
  values[4* norbs + index] =  (D2fDr2*DrDx*DrDx + DfDr * D2rDx2)*ang
                                 + 2 * (DfDr*DrDx) * DangDx
                                 + fr * D2angDx2;
  
  values[7* norbs + index] =  (D2fDr2*DrDy*DrDy + DfDr * D2rDy2)*ang
                                 + 2 * (DfDr*DrDy) * DangDy
                                 + fr * D2angDy2;

  values[9* norbs + index] =  (D2fDr2*DrDz*DrDz + DfDr * D2rDz2)*ang
                                 + 2 * (DfDr*DrDz) * DangDz
                                 + fr * D2angDz2;

  //if (mixed) {

    double D2rDxDy = - xN[0]*xN[1]/pow(RiN,3);
    double D2rDxDz = - xN[0]*xN[2]/pow(RiN,3);
    double D2rDyDz = - xN[1]*xN[2]/pow(RiN,3);
    
    double D2angDxDy = (NX >= 1 && NY >= 1) ?
        NX * NY * pow(xN[0],NX-1) * pow (xN[1], NY-1) * pow (xN[2], NZ) :
        0.0;
    
    double D2angDxDz = (NX >= 1 && NZ >= 1) ?
        NX * NZ * pow(xN[0],NX-1) * pow (xN[1], NY) * pow (xN[2], NZ-1) :
        0.0;

    double D2angDyDz = (NY >= 1 && NZ >= 1) ?
        NY * NZ * pow(xN[0],NX) * pow (xN[1], NY-1) * pow (xN[2], NZ-1) :
        0.0;
  
    values[5* norbs + index] =  (D2fDr2*DrDx*DrDy + DfDr * D2rDxDy)*ang
                                   + (DfDr*DrDx) * DangDy
                                   + (DfDr*DrDy) * DangDx
                                   + fr * D2angDxDy;
    
    values[6* norbs + index] =  (D2fDr2*DrDx*DrDz + DfDr * D2rDxDz)*ang
                                   + (DfDr*DrDx) * DangDz
                                   + (DfDr*DrDz) * DangDx
                                   + fr * D2angDxDz;

    values[8* norbs + index] =  (D2fDr2*DrDy*DrDz + DfDr * D2rDyDz)*ang
                                   + (DfDr*DrDy) * DangDz
                                   + (DfDr*DrDz) * DangDy
                                   + fr * D2angDyDz;
  
  //}
}
  

void slaterBasis::eval_deriv2(const Vector3d& x, double* values) {

  int index = 0;
  for (int i=0; i<atomicBasis.size(); i++) {
    auto aBasis = atomicBasis[i];

    double xN[3];
    xN[0] = x[0] - aBasis.coord[0];
    xN[1] = x[1] - aBasis.coord[1];
    xN[2] = x[2] - aBasis.coord[2];
    
    double RiN =  pow( pow(xN[0], 2) +
                       pow(xN[1], 2) +
                       pow(xN[2], 2), 0.5);

    //for each basis in aBasis evaluate the value
    for (int j=0; j<aBasis.exponents.size(); j++) {
      int l = aBasis.NL[2*j+1];
      int N = aBasis.NL[2*j] - 1;
      double eta   = aBasis.exponents[j];
      double scale = aBasis.radialNorm[j];
      
      if (l == 0) { //s
        scale *= sqrt(1./3.141592653589)/2.0;
        makeDeriv(values, N, 0, 0, 0, norbs, xN, RiN, eta, scale, index);
        index++;
      }
      else if (l == 1) { //p
        scale *= sqrt(3./4.0/3.141592653589);
        makeDeriv(values, N-l, 1, 0, 0, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 0, 1, 0, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 0, 0, 1, norbs, xN, RiN, eta, scale, index); index++;
      }
      else if (l == 2) {//d
        makeDeriv(values, N-l, 2, 0, 0, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 1, 1, 0, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 1, 0, 1, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 0, 2, 0, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 0, 1, 1, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 0, 0, 2, norbs, xN, RiN, eta, scale, index); index++;
      }
      else if (l == 3) {//f
        makeDeriv(values, N-l, 3, 0, 0, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 2, 1, 0, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 2, 0, 1, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 1, 2, 0, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 1, 1, 1, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 1, 0, 2, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 0, 3, 0, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 0, 2, 1, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 0, 1, 2, norbs, xN, RiN, eta, scale, index); index++;
        makeDeriv(values, N-l, 0, 0, 3, norbs, xN, RiN, eta, scale, index); index++;
      }                   
      
    }
  }

}
