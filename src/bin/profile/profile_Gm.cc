/*
 * profile_Gm.cc
 *
 *      Author: David McDonald II && evaleev
 */

#include "boys.h"
//#include "vector.h"
#include <ctime>
#include <iostream>
#include <iterator>
#include <functional>
#include <numeric>
#include <boost/chrono.hpp>
#include <cxxabi.h>

#ifndef SKIP_AXPY
#  include <mkl_cblas.h>
typedef MKL_INT BLAS_INT;
#endif

#ifdef SKIP_ALL
//# define SKIP_AXPY
//# define SKIP_DOT
//# define SKIP_GEMM
# define SKIP_EXP
# define SKIP_SQRT
# define SKIP_ERF
# define SKIP_CHEBYSHEV
# define SKIP_TAYLOR
# define SKIP_STGNG
# define SKIP_YUKAWA
#endif

#define AVOID_AUTO_VECTORIZATION 1 // set to 0 to measure performance within vectorizable loops
                                   // the default is to measure performance of a single call

using namespace std;

enum OperType {
  f12, f12_2, f12_o_r12, f12_t_f12
};
std::string to_string(OperType o) {
  std::string result;
  switch (o) {
    case f12: result = "f12"; break;
    case f12_2: result = "f12_2"; break;
    case f12_o_r12: result = "f12_o_r12"; break;
    case f12_t_f12: result = "f12_t_f12"; break;
  }
  return result;
}

/** profiles Kernel by running it repeatedly, and reporting the sum of values
 *
 * @param k the Kernel, needs to have 2 member functions: label and and eval, neither of which takes a parameter
 * @param nrepeats the number of times the kernel is run
 */
template <typename Kernel> void profile(const Kernel& k, int nrepeats);

void do_chebyshev(int mmax, double T, int nrepeats);
void do_taylor(int mmax, double T, int nrepeats);
template <OperType O> void do_stg6g(int mmax, double T, double rho, int nrepeats);
void do_yukawa(int mmax, double T, double U, int nrepeats);

template <typename Real, Real (Function)(Real) >
struct BasicKernel {
    typedef Real ResultType;
    BasicKernel(Real T, std::string label, double T_dec = 0.00001) : label_(label),
        T_(T), T_dec_(T_dec), sum_(0) {}
    std::string label() const { return label_; }
    void eval() const {
      sum_ += Function(T_);
      T_ += T_dec_;
    }
    Real sum() const {
      return sum_;
    }
    size_t ops_per_eval() const {
      return 1;
    }
    std::string label_;
    mutable Real T_;
    Real T_dec_;
    mutable Real sum_;
};

template <typename Real>
struct VectorOpKernel {
    VectorOpKernel(size_t veclen,
                   size_t nargs,
                   Real T,
                   std::string label) : result_(veclen, Real(0)), args_(nargs), label_(label) {
      for(size_t i=0; i<args_.size(); ++i) {
        args_[i] = new Real[veclen];
        std::fill(args_[i], args_[i] + veclen, T);
      }
    }
    ~VectorOpKernel() {
      for(size_t i=0; i<args_.size(); ++i) {
        delete[] args_[i];
      }
    }
    std::string label() const {
      return label_;
    }

    std::vector<Real> result_;
    std::vector<Real*> args_;
    std::string label_;
};

template <typename Real>
struct AXPYKernel : public VectorOpKernel<Real> {
  typedef Real ResultType;
  AXPYKernel(size_t veclen,
             Real a,
             Real T,
             std::string label) : VectorOpKernel<Real>(veclen, 1, T, label), a_(a) {
    y_ = &VectorOpKernel<Real>::result_[0];
    x_ = VectorOpKernel<Real>::args_[0];
    n_ = VectorOpKernel<Real>::result_.size();
  }
  void eval() const {
#pragma ivdep
    for(size_t v=0; v<n_; ++v)
      y_[v] += a_ * x_[v];
  }
  Real sum() const {
    return std::accumulate(y_, y_+n_, Real(0));
  }
  size_t ops_per_eval() const {
    return n_ * 2;
  }

  Real a_;
  Real* y_;
  Real const* x_;
  BLAS_INT n_;

};

struct DAXPYKernel : public VectorOpKernel<double> {
  typedef double ResultType;
  typedef double Real;
  DAXPYKernel(size_t veclen,
              Real a,
              Real T,
              std::string label) : VectorOpKernel<Real>(veclen, 1, T, label), a_(a) {
    y_ = &VectorOpKernel<Real>::result_[0];
    x_ = VectorOpKernel<Real>::args_[0];
    n_ = VectorOpKernel<Real>::result_.size();
  }
  void eval() const {
    cblas_daxpy(n_, a_, x_, 1, y_, 1);
  }
  Real sum() const {
    return std::accumulate(y_, y_+n_, Real(0));
  }
  size_t ops_per_eval() const {
    return n_ * 2;
  }

  Real a_;
  Real* y_;
  Real* x_;
  size_t n_;

};

template <typename Real>
struct DOTKernel : public VectorOpKernel<Real> {
  typedef Real ResultType;
  DOTKernel(size_t veclen,
            Real T,
            std::string label) : VectorOpKernel<Real>(veclen, 2, T, label), result_(0) {
    x1_ = VectorOpKernel<Real>::args_[0];
    x2_ = VectorOpKernel<Real>::args_[1];
    n_ = VectorOpKernel<Real>::result_.size();
  }
  void eval() const {
#pragma ivdep
    for(size_t v=0; v<n_; ++v)
      result_ += x1_[v] * x2_[v];
  }
  Real sum() const {
    return result_;
  }
  size_t ops_per_eval() const {
    return n_ * 2;
  }

  mutable Real result_;
  Real const* x1_;
  Real const* x2_;
  BLAS_INT n_;

};

struct DDOTKernel : public VectorOpKernel<double> {
  typedef double ResultType;
  typedef double Real;
  DDOTKernel(size_t veclen,
             Real T,
             std::string label) : VectorOpKernel<Real>(veclen, 2, T, label), result_(0) {
    x1_ = VectorOpKernel<Real>::args_[0];
    x2_ = VectorOpKernel<Real>::args_[1];
    n_ = VectorOpKernel<Real>::result_.size();
  }
  void eval() const {
    result_ += cblas_ddot(n_, x1_, 1, x2_, 1);
  }
  Real sum() const {
    return result_;
  }
  size_t ops_per_eval() const {
    return n_ * 2;
  }

  mutable Real result_;
  const Real* x1_;
  const Real* x2_;
  size_t n_;

};

struct DGEMMKernel : public VectorOpKernel<double> {
  typedef double ResultType;
  typedef double Real;
  DGEMMKernel(size_t n,
              Real T,
              std::string label) : VectorOpKernel<Real>(n*n, 2, T, label), n_(n) {
    c_ = &VectorOpKernel<Real>::result_[0];
    a_ = VectorOpKernel<Real>::args_[0];
    b_ = VectorOpKernel<Real>::args_[1];
  }
  void eval() const {
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, n_, n_, n_,
                1.0, a_, n_,
                b_, n_, 1.0, c_, n_);
  }
  Real sum() const {
    return std::accumulate(c_, c_+n_*n_, 0.0);
  }
  // approximate
  size_t ops_per_eval() const {
    return n_ * n_ * n_ * 2;
  }

  Real* c_;
  const Real* a_;
  const Real* b_;
  size_t n_;

};

int main(int argc, char* argv[]) {
  if (argc < 2 or argc > 4) {
    std::cout << "Usage: profile_Gm mmax T nrepeats" << std::endl;
    return 1;
  }
  const int mmax  = atoi(argv[1]);
  const double T  = atol(argv[2]);
  const double stg_zeta = 0.10;
  const double rho = 1.0;
  const double U = stg_zeta * stg_zeta / (4 * rho);
  const int nrepeats  = atoi(argv[3]);

  using libint2::simd::VectorSSEDouble;
  using libint2::simd::VectorAVXDouble;
  const VectorSSEDouble T_sse(T);
  const VectorAVXDouble T_avx(T);

  cout << "mmax = " << mmax << endl;
  cout << " T   = "<< T << endl;

  // changes precision of cout to 15 for printing doubles and such.
  cout.precision(15);

#ifndef SKIP_AXPY
  const int n = 128;
  profile(DAXPYKernel(n, 1.0, 1.0, "daxpy"), nrepeats);
  profile(AXPYKernel<double>(n, 1.0, 1.0, "axpy [double]"), nrepeats);
  profile(AXPYKernel<VectorSSEDouble>(n, 1.0, 1.0, "axpy [SSE]"), nrepeats);
  profile(AXPYKernel<VectorAVXDouble>(n, 1.0, 1.0, "axpy [AVX]"), nrepeats);
#endif

#ifndef SKIP_DOT
  //const int n = 4096;
  profile(DDOTKernel(n, 1.0, "ddot"), nrepeats);
  profile(DOTKernel<double>(n, 1.0, "dot [double]"), nrepeats);
  profile(DOTKernel<VectorSSEDouble>(n, 1.0, "dot [SSE]"), nrepeats);
  profile(DOTKernel<VectorAVXDouble>(n, 1.0, "dot [AVX]"), nrepeats);
#endif

#ifndef SKIP_GEMM
  const int nn = 2048;
  profile(DGEMMKernel(nn, 1.0, "dgemm"), 1);
#endif

#ifndef SKIP_EXP
  profile(BasicKernel<double,std::exp>(-T,"exp(-T) [double]", -0.00001), nrepeats);
  profile(BasicKernel<VectorSSEDouble,libint2::simd::exp>(-T,"exp(-T) [SSE]", -0.00001), nrepeats);
  profile(BasicKernel<VectorAVXDouble,libint2::simd::exp>(-T,"exp(-T) [AVX]", -0.00001), nrepeats);
#endif
#ifndef SKIP_SQRT
  profile(BasicKernel<double,std::sqrt>(T,"sqrt(T) [double]"), nrepeats);
  profile(BasicKernel<VectorSSEDouble,libint2::simd::sqrt>(T,"sqrt(T) [SSE]"), nrepeats);
  profile(BasicKernel<VectorAVXDouble,libint2::simd::sqrt>(T,"sqrt(T) [AVX]"), nrepeats);
#endif
#ifndef SKIP_ERF
  profile(BasicKernel<double,erf>(T,"erf(T) [double]"), nrepeats);
  profile(BasicKernel<VectorSSEDouble,libint2::simd::erf>(T,"erf(T) [SSE]"), nrepeats);
  profile(BasicKernel<VectorAVXDouble,libint2::simd::erf>(T,"erf(T) [AVX]"), nrepeats);
  profile(BasicKernel<double,erfc>(T,"erfc(T) [double]"), nrepeats);
  profile(BasicKernel<VectorSSEDouble,libint2::simd::erfc>(T,"erfc(T) [SSE]"), nrepeats);
  profile(BasicKernel<VectorAVXDouble,libint2::simd::erfc>(T,"erfc(T) [AVX]"), nrepeats);
#endif
#ifndef SKIP_CHEBYSHEV
  do_chebyshev(mmax, T, nrepeats);
#endif
#ifndef SKIP_TAYLOR
  do_taylor(mmax, T, nrepeats);
#endif
#ifndef SKIP_STGNG
  do_stg6g<f12>(mmax, T, rho, nrepeats);
  do_stg6g<f12_o_r12>(mmax, T, rho, nrepeats);
  do_stg6g<f12_2>(mmax, T, rho, nrepeats);
  do_stg6g<f12_t_f12>(mmax, T, rho, nrepeats);
#endif
#ifndef SKIP_YUKAWA
  //do_yukawa(mmax, T, U, nrepeats);
#endif
  return 0;
}

template <typename Kernel>
void profile(const Kernel& k, int nrepeats) {
  std::cout << "===================== " << k.label() << " ======================" << std::endl;

  typedef typename Kernel::ResultType Real;

  const auto start = boost::chrono::high_resolution_clock::now();
#if AVOID_AUTO_VECTORIZATION
#pragma novector
#endif
  for (int i = 0; i < nrepeats; ++i) {
    k.eval();
  }
  const boost::chrono::duration<double> elapsed = boost::chrono::high_resolution_clock::now() - start;

  std::cout << "sum of " << k.label() << ": " << k.sum() << std::endl;

  cout << "Time = " << fixed << elapsed.count() << endl;
  cout << "Rate = " << fixed << nrepeats * k.ops_per_eval() / elapsed.count()  << endl;
}

void do_chebyshev(int mmax, double T, int nrepeats) {
  std::cout << "===================== Fm Cheb3 ======================" << std::endl;
  double* Fm_array = new double[mmax+1];
  double* Fm_array_sum = new double[mmax+1];
  //std::cout << "alignment of Fm = " << reinterpret_cast<unsigned long int>((char*) Fm_array) % 32ul << " bytes\n";

  libint2::FmEval_Chebyshev3 fmeval_cheb(mmax);
  std::cout << "done initialization:" << std::endl;
  std::fill(Fm_array_sum, Fm_array_sum+mmax+1, 0.0);
  const auto start = boost::chrono::high_resolution_clock::now();
#if AVOID_AUTO_VECTORIZATION
#pragma novector
#endif
  for (int i = 0; i < nrepeats; ++i) {
    // this computes all Fm for up to mmax
    fmeval_cheb.eval(Fm_array, T, mmax);
    for(int m=0; m<=mmax; ++m)
      Fm_array_sum[m] += Fm_array[m];
    T += 0.00001; // to ward-off unrealistic compiler optimizations
  }
  const boost::chrono::duration<double> elapsed = boost::chrono::high_resolution_clock::now() - start;

  std::cout << "sum of Fm:" << std::endl;
  std::copy(Fm_array_sum, Fm_array_sum+mmax+1, std::ostream_iterator<double>(std::cout,"\n"));

  cout << "Time = " << fixed << elapsed.count() << endl;
  cout << "Rate = " << fixed << nrepeats / elapsed.count()  << endl;

  delete[] Fm_array;
  delete[] Fm_array_sum;
}

void do_taylor(int mmax, double T, int nrepeats) {
  std::cout << "===================== Fm Taylor6 ======================" << std::endl;
  double* Fm_array = new double[mmax+1];
  double* Fm_array_sum = new double[mmax+1];

  libint2::FmEval_Taylor<double, 6> fmeval_taylor6(mmax, 1e-15);
  std::fill(Fm_array_sum, Fm_array_sum+mmax+1, 0.0);
  const auto start = boost::chrono::high_resolution_clock::now();
#if AVOID_AUTO_VECTORIZATION
#pragma novector
#endif
  for (int i = 0; i < nrepeats; ++i)
  {
    // this computes all Fm for up to mmax
    fmeval_taylor6.eval(Fm_array, T, mmax);
    for(int m=0; m<=mmax; ++m)
      Fm_array_sum[m] += Fm_array[m];
    T += 0.00001; // to ward-off unrealistic compiler optimizations
  }
  const boost::chrono::duration<double> elapsed = boost::chrono::high_resolution_clock::now() - start;
  std::cout << "sum of Fm (Taylor):" << std::endl;
  std::copy(Fm_array_sum, Fm_array_sum+mmax+1, std::ostream_iterator<double>(std::cout,"\n"));

  cout << "Time = " << fixed << elapsed.count() << endl;
  cout << "Rate = " << fixed << nrepeats / elapsed.count()  << endl;

  delete[] Fm_array;
  delete[] Fm_array_sum;
}

template<OperType O>
void do_stg6g(int mmax, double T, double rho, int nrepeats) {
  std::cout << "===================== Gm STG-6G ======================" << std::endl;
  double* Gm_array = new double[mmax+1];
  double* Gm_array_sum = new double[mmax+1];

  const size_t ng = 9;
  std::vector< std::pair<double,double> > stg_ng(ng);
  //stg_ng[0] = make_pair(4.0001, 1.0);
#if 1
#if HAVE_LAPACK
  libint2::stg_ng_fit(ng, 1.0, stg_ng);
#else
  stg_ng[0] = make_pair(0.16015391600067220691727771683865433704907890673261,
                        0.20306992259915090794062652264516576964313257462623);
  stg_ng[1] = make_pair(0.58691138376032812074703122125162923674902800850316,
                        0.29474840080158909154305767626309566520662550324323);
  stg_ng[2] = make_pair(1.9052880179050650706871766123674791603725239722860,
                        0.20652315861651088693388092033220845569017370830588);
  stg_ng[3] = make_pair(6.1508186412033182882412135545092215700186355770734,
                        0.13232619560602867340217449542493153700747744735317);
  stg_ng[4] = make_pair(22.558816746266648614747394893787336699960416307706,
                        0.084097701098685716800769730376212853566993914234229);
  stg_ng[5] = make_pair(167.12355778570626548864380047361110482628234458031,
                        0.079234606133959413896805606690618531996594605785539);
#endif
#endif

  std::vector< std::pair<double,double> > stg_ng_sq(stg_ng.size() * stg_ng.size());
  for(int b=0, bk=0; b<stg_ng.size(); ++b) {
    for(int k=0; k<stg_ng.size(); ++k, ++bk) {
      const double exp = stg_ng[b].first  + stg_ng[k].first;
      const double coef = stg_ng[b].second * stg_ng[k].second * (O == f12_t_f12 ? 4.0 * stg_ng[b].first  * stg_ng[k].first : 1.0);
      stg_ng_sq[bk] = make_pair(exp, coef);
    }
  }

  libint2::GaussianGmEval<double, (O == f12 || O == f12_2) ? 0 : (O == f12_o_r12 ? -1 : 2)>
    gtg_eval(mmax, 1e-15);
  std::fill(Gm_array_sum, Gm_array_sum+mmax+1, 0.0);
  const auto start = boost::chrono::high_resolution_clock::now();
#if AVOID_AUTO_VECTORIZATION
#pragma novector
#endif
  for (int i = 0; i < nrepeats; ++i)
  {
    // this computes all Gm for up to mmax
    gtg_eval.eval(Gm_array, rho, T, mmax,
                  (O == f12 || O == f12_o_r12) ? stg_ng : stg_ng_sq);
    for(int m=0; m<=mmax; ++m)
      Gm_array_sum[m] += Gm_array[m];
    T += 0.00001; // to ward-off unrealistic compiler optimizations
    rho += 0.000001;
  }
  const boost::chrono::duration<double> elapsed = boost::chrono::high_resolution_clock::now() - start;

  std::cout << "sum of Gm (STG-" << ng << "G O=" << to_string(O) << " ):" << std::endl;
  std::copy(Gm_array_sum, Gm_array_sum+mmax+1, std::ostream_iterator<double>(std::cout,"\n"));

  cout << "Time = " << fixed << elapsed.count() << endl;
  cout << "Rate = " << fixed << nrepeats / elapsed.count()  << endl;

  delete[] Gm_array;
  delete[] Gm_array_sum;
}

#if 0
void do_yukawa(int mmax, double T, double U, int nrepeats) {
  std::cout << "===================== Gm Yukawa ======================" << std::endl;
  double* Gm_array = new double[mmax+2];
  double* Gm_array_sum = new double[mmax+2];
  double Gm[2]; // contains G_-1 and G_0

  libint2::YukawaGmEval<double> yukawa_eval(mmax, 1e-15);
  std::fill(Gm_array_sum, Gm_array_sum+mmax+2, 0.0);
  const auto start = boost::chrono::high_resolution_clock::now();
#if AVOID_AUTO_VECTORIZATION
#pragma novector
#endif
  for (int i = 0; i < nrepeats; ++i)
  {
    // this computes all Gm for up to mmax
//    asm("#tag1");
//    yukawa_eval.eval_yukawa_s1(Gm_array, T, U, mmax);
//    yukawa_eval.eval_yukawa_s2(Gm_array, T, U, mmax);
    yukawa_eval.eval_yukawa_Gm0U(Gm_array, U, mmax);
    for(int m=0; m<=mmax; ++m)
      Gm_array_sum[m] += Gm_array[m];

//    Gm_array_sum[0] += libint2::YukawaGmEval_Reference<double>::eval_Gm1(T, U);
//    Gm_array_sum[1] += libint2::YukawaGmEval_Reference<double>::eval_G0(T, U);
    T += 0.00001; // to ward-off unrealistic compiler optimizations
    U += 0.000001;
  }
  const boost::chrono::duration<double> elapsed = boost::chrono::high_resolution_clock::now() - start;

  std::cout << "sum of Gm (STG):" << std::endl;
  std::copy(Gm_array_sum, Gm_array_sum+mmax+2, std::ostream_iterator<double>(std::cout,"\n"));

  cout << "Time = " << fixed << elapsed.count() << endl;
  cout << "Rate = " << fixed << nrepeats / elapsed.count()  << endl;

  delete[] Gm_array;
  delete[] Gm_array_sum;
}
#endif
