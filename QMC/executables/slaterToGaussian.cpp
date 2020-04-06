#include <vector>
#include <map>
#include "readSlater.h"
#include "slaterBasis.h"
#include <string>
#include <Eigen/Dense>
#include <boost/format.hpp>

using namespace std;
using namespace Eigen;

void getSTOnG(int N, int L, int NG,
              double zeta, double scale,
              vector<double>& exponent,
              vector<double>& coeffs);
extern "C" {
  void stong_(int*, int*, int*, double*, double*);
}
vector<string> slaterParser::AtomSymbols;

int main(int argc, char* argv[]) {
  string fname = "slaterBasis.json";
  slaterParser sp(fname);

  string aName;
  cout << "Atom name: "<<endl;
  cin >> aName;
  //string atomName = "B";
  //this is like the geometry
  vector<string> atomName(1);
  vector<Vector3d> atomCoord(1);
  Vector3d coord; coord[0] =0.; coord[1] = 0.; coord[2] = 0.;
  atomCoord[0] = coord;
  atomName[0] = aName;
  
  slaterBasis STO;  
  STO.atomName = atomName;
  STO.atomCoord = atomCoord;
  STO.read();
  

  int NG = 14;

  vector<double> gtoexp(NG), gtocoeff(NG);

  for (int i=0; i<STO.atomicBasis.size(); i++) {
    slaterBasisOnAtom &sh = STO.atomicBasis[i];

    for (int j=0; j<sh.exponents.size(); j++) {
      double zeta = sh.exponents[j];
      getSTOnG(sh.NL[2*j], sh.NL[2*j+1], NG, zeta, 1.0, gtoexp, gtocoeff);

      cout << atomName[0]<<"  "<<sp.angularMomItoS[sh.NL[2*j+1]]<<endl;
      for (int k=0; k<NG; k++) {
        cout << boost::format("%14.8f  %14.8f\n") % gtoexp[k] % gtocoeff[k];
      }
    }

  }
}


void getSTOnG(int N, int L, int NG,
              double zeta, double scale,
              vector<double>& exponent,
              vector<double>& coeffs) {
  
  stong_(&N, &L, &NG, &exponent[0], &coeffs[0]);
  for (int i=0; i<NG; i++) {
    //cout << exponent[i]<<"  "<<coeffs[i]<<"  "<<zeta<<"  "<<N<<"  "<<L <<endl;
    exponent[i] *= zeta*zeta;
    coeffs[i] *= scale*pow(zeta, 1-N+L)/GTOradialNorm(exponent[i], L);
  }

}
