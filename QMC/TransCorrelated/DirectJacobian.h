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
#pragma once

#include <Eigen/Dense>
#include <Eigen/Core>
#include <Eigen/IterativeLinearSolvers>
#include <complex>
#include <vector>
#include <math.h>

using namespace std;
using namespace Eigen;

template<typename Scalar, typename Functor> struct DirectJacobian;
class Residuals;

namespace Eigen {
namespace internal {

template<typename F>
struct traits<DirectJacobian<double, F>> :  public Eigen::internal::traits<Eigen::SparseMatrix<double> >
{};

template<typename F>
struct traits<DirectJacobian<complex<double>,F>> :  public Eigen::internal::traits<Eigen::SparseMatrix<complex<double> > >
{};
}
}

template<typename Scalar, typename Functor>
struct DirectJacobian : public Eigen::EigenBase<DirectJacobian<Scalar,Functor> > {

  //using FunctorEvalResidue = boost::function<double (const VectorXd&, VectorXd&)>;
  typedef Scalar Scalar;
  typedef double RealScalar;
  typedef int StorageIndex;
  
  typedef Matrix<Scalar, Dynamic, 1> FVectorType;
  typedef Matrix<Scalar, Dynamic, Dynamic> FMatrixType;
  enum {
    ColsAtCompileTime = Eigen::Dynamic,
    MaxColsAtCompileTime = Eigen::Dynamic,
    IsRowMajor = false
  };
  
  vector<int> colIndex;
  FMatrixType Jac;
  Functor& func;
  Scalar eps;
  int nrows;
  int ncols;
  
  //here it assumes that the Jaccol has been initialized
  Index rows() const { return nrows; }
  Index cols() const { return ncols; }

  
  DirectJacobian(
      Functor& _func,
      int _nrows,
      int _ncols,
      Scalar epsfcn=1.e-4) : func(_func), nrows(_nrows), ncols(_ncols)
  {
    const Scalar epsmch = NumTraits<Scalar>::epsilon();
    eps = sqrt((std::max)(epsfcn,epsmch));
  };

  void setFvec(FVectorType& _xvec,
               FVectorType& _fvec) {
    xvec = _xvec;
    fvec = _fvec;
  }
  
  int PopulateJacobian(
      FVectorType& x)
  {
    int n = x.size();
    int colPerProc = n/commsize + 1;
    
    Jac.resize(n, colPerProc); 
    FVectorType fvecTmp = x, fvec = x;

    //func(x, fvec, true); //run in parallel

    /* computation of dense approximate jacobian. */
    int colIndex = 0;
    for (int j = commrank; j < n; j+=commsize) {

      Scalar temp = x[j];
      Scalar h = eps * abs(temp);
      if (h == 0.)
        h = eps;
      x[j] = temp + h;
      fvecTmp.setZero();
      func(x, fvecTmp, false);

      x[j] = temp - h;
      fvec.setZero();
      func(x, fvec, false);

      x[j] = temp;
      Jac.col(colIndex) = (fvecTmp - fvec)/2./h;
      colIndex++;
    }
    return 0;
  }

  template<typename Rhs>
  Eigen::Product<DirectJacobian<Scalar, Functor>, Rhs, Eigen::AliasFreeProduct> operator*(
      const MatrixBase<Rhs>& x) const {
    
    return Eigen::Product<DirectJacobian<Scalar, Functor>, Rhs, Eigen::AliasFreeProduct>(*this, x.derived());
  }
  
};
 
namespace Eigen {
namespace internal {

template<typename T, typename F, typename Rhs>
struct generic_product_impl<DirectJacobian<T,F>, Rhs, SparseShape, DenseShape, GemvProduct> // GEMV stands for matrix-vector
    : generic_product_impl_base<DirectJacobian<T,F>,Rhs,generic_product_impl<DirectJacobian<T,F>,Rhs> >
{
  typedef typename Product<DirectJacobian<T,F>,Rhs>::Scalar Scalar;
  
  template<typename Dest>
  static void scaleAndAddTo(Dest& dst,
                            const DirectJacobian<T,F>& lhs,
                            const Rhs& rhs,
                            const Scalar& alpha)
  {
    /*
    double eps = std::sqrt(1 + lhs.xvec.norm())*1.5e-6/ rhs.norm();
    typename DirectJacobian<T,F>::FVectorType xplusu = lhs.xvec + eps * rhs;
    Dest dsttmp = dst; dsttmp.setZero();
    lhs.func(xplusu, dsttmp);
    dst.noalias() += alpha*(dsttmp - lhs.fvec)/eps + 1.e-6*rhs;
    */

    dst.setZero();

    //multiply x with J
    int colIndex = 0;
    for (int i=commrank; i<lhs.cols(); i+=commsize) {
      dst += lhs.Jac.col(colIndex)*rhs(i);
      colIndex++;
    }
    int dstsize = dst.rows();
    MPI_Allreduce(MPI_IN_PLACE, &dst(0), dstsize, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

    //if (commrank == 0) cout << eps <<endl;
    /*
    typename DirectJacobian<T,F>::FVectorType xplusu = lhs.xvec + eps * rhs;
    typename DirectJacobian<T,F>::FVectorType xminusu = lhs.xvec - eps * rhs;
    Dest dstm = dst;
    lhs.func(xplusu, dst);
    lhs.func(xminusu, dstm);
    dst = (dst - dstm)/2./eps;// + 1.e-7*rhs;
    */
    //for(Index i=0; i<lhs.cols(); ++i)
    //dst += rhs(i) * lhs.JacCols[i];
  }
};
}
}



