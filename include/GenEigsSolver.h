#ifndef GEN_EIGS_SOLVER_H
#define GEN_EIGS_SOLVER_H

#include <Eigen/Dense>
#include <vector>
#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>
#include <stdexcept>

#include "SelectionRule.h"
#include "UpperHessenbergQR.h"
#include "MatOp/DenseGenMatProd.h"
#include "MatOp/DenseGenRealShiftSolve.h"


template < typename Scalar = double,
           int SelectionRule = LARGEST_MAGN,
           typename OpType = DenseGenMatProd<double> >
class GenEigsSolver
{
private:
    typedef Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic> Matrix;
    typedef Eigen::Matrix<Scalar, Eigen::Dynamic, 1> Vector;
    typedef Eigen::Array<Scalar, Eigen::Dynamic, 1> Array;
    typedef Eigen::Array<bool, Eigen::Dynamic, 1> BoolArray;
    typedef Eigen::Map<const Matrix> MapMat;
    typedef Eigen::Map<const Vector> MapVec;

    typedef std::complex<Scalar> Complex;
    typedef Eigen::Matrix<Complex, Eigen::Dynamic, Eigen::Dynamic> ComplexMatrix;
    typedef Eigen::Matrix<Complex, Eigen::Dynamic, 1> ComplexVector;

    typedef Eigen::EigenSolver<Matrix> EigenSolver;
    typedef Eigen::HouseholderQR<Matrix> QRdecomp;
    typedef Eigen::HouseholderSequence<Matrix, Vector> QRQ;

    typedef std::pair<Complex, int> SortPair;

protected:
    OpType *op;             // object to conduct matrix operation,
                            // e.g. matrix-vector product
private:
    const int dim_n;        // dimension of matrix A

protected:
    const int nev;          // number of eigenvalues requested

private:
    const int ncv;          // number of ritz values
    int nmatop;             // number of matrix operations called
    int niter;              // number of restarting iterations

    Matrix fac_V;           // V matrix in the Arnoldi factorization
    Matrix fac_H;           // H matrix in the Arnoldi factorization
    Vector fac_f;           // residual in the Arnoldi factorization

protected:
    ComplexVector ritz_val; // ritz values

private:
    ComplexMatrix ritz_vec; // ritz vectors
    BoolArray ritz_conv;    // indicator of the convergence of ritz values

    const Scalar prec;      // precision parameter used to test convergence
                            // prec = epsilon^(2/3)
                            // epsilon is the machine precision,
                            // e.g. ~= 1e-16 for the "double" type

    // Arnoldi factorization starting from step-k
    void factorize_from(int from_k, int to_m, const Vector &fk)
    {
        if(to_m <= from_k) return;

        fac_f = fk;

        Vector v(dim_n), w(dim_n);
        Scalar beta = 0.0;
        // Keep the upperleft k x k submatrix of H and set other elements to 0
        fac_H.rightCols(ncv - from_k).setZero();
        fac_H.block(from_k, 0, ncv - from_k, from_k).setZero();
        for(int i = from_k; i <= to_m - 1; i++)
        {
            beta = fac_f.norm();
            v.noalias() = fac_f / beta;
            fac_V.col(i) = v; // The (i+1)-th column
            fac_H.block(i, 0, 1, i).setZero();
            fac_H(i, i - 1) = beta;

            op->perform_op(v.data(), w.data());
            nmatop++;

            Vector h = fac_V.leftCols(i + 1).transpose() * w;
            fac_H.block(0, i, i + 1, 1) = h;

            fac_f = w - fac_V.leftCols(i + 1) * h;
            // Correct f if it is not orthogonal to V
            // Typically the largest absolute value occurs in
            // the first element, i.e., <v1, f>, so we use this
            // to test the orthogonality
            Scalar v1f = fac_f.dot(fac_V.col(0));
            if(v1f > prec || v1f < -prec)
            {
                Vector Vf(i + 1);
                Vf.tail(i) = fac_V.block(0, 1, dim_n, i).transpose() * fac_f;
                Vf[0] = v1f;
                fac_f -= fac_V.leftCols(i + 1) * Vf;
            }
        }
    }

    static bool is_complex(Complex v, Scalar eps)
    {
        return std::abs(v.imag()) > eps;
    }

    static bool is_conj(Complex v1, Complex v2, Scalar eps)
    {
        return std::abs(v1 - std::conj(v2)) < eps;
    }

    // Implicitly restarted Arnoldi factorization
    void restart(int k)
    {
        if(k >= ncv)
            return;

        QRdecomp decomp_gen;
        UpperHessenbergQR<Scalar> decomp_hb;
        Vector em(ncv);
        em.setZero();
        em[ncv - 1] = 1;

        for(int i = k; i < ncv; i++)
        {
            if(is_complex(ritz_val[i], prec) && is_conj(ritz_val[i], ritz_val[i + 1], prec))
            {
                // H - mu * I = Q1 * R1
                // H <- R1 * Q1 + mu * I = Q1' * H * Q1
                // H - conj(mu) * I = Q2 * R2
                // H <- R2 * Q2 + conj(mu) * I = Q2' * H * Q2
                //
                // (H - mu * I) * (H - conj(mu) * I) = Q1 * Q2 * R2 * R1 = Q * R
                Scalar re = ritz_val[i].real();
                Scalar s = std::norm(ritz_val[i]);
                Matrix HH = fac_H;
                HH.diagonal().array() -= 2 * re;
                HH = fac_H * HH;
                HH.diagonal().array() += s;

                // NOTE: HH is no longer upper Hessenberg
                decomp_gen.compute(HH);
                QRQ Q = decomp_gen.householderQ();

                // V -> VQ
                fac_V.applyOnTheRight(Q);
                // H -> Q'HQ
                fac_H.applyOnTheRight(Q);
                fac_H.applyOnTheLeft(Q.adjoint());
                // em -> Q'em
                em.applyOnTheLeft(Q.adjoint());

                i++;
            } else {
                // QR decomposition of H - mu * I, mu is real
                fac_H.diagonal().array() -= ritz_val[i].real();
                decomp_hb.compute(fac_H);

                // V -> VQ
                decomp_hb.apply_YQ(fac_V);
                // H -> Q'HQ = RQ + mu * I
                fac_H = decomp_hb.matrix_RQ();
                fac_H.diagonal().array() += ritz_val[i].real();
                // em -> Q'em
                decomp_hb.apply_QtY(em);
            }
        }

        Vector fk = fac_f * em[k - 1] + fac_V.col(k) * fac_H(k, k - 1);
        factorize_from(k, ncv, fk);
        retrieve_ritzpair();
    }

    // Calculate the number of converged Ritz values
    int num_converged(Scalar tol)
    {
        // thresh = tol * max(prec, abs(theta)), theta for ritz value
        Array thresh = tol * ritz_val.head(nev).array().abs().max(prec);
        Array resid = ritz_vec.template bottomRows<1>().transpose().array().abs() * fac_f.norm();
        // Converged "wanted" ritz values
        ritz_conv = (resid < thresh);

        return ritz_conv.cast<int>().sum();
    }

    // Return the adjusted nev for restarting
    int nev_adjusted(int nconv)
    {
        int nev_new = nev;

        // Increase nev by one if ritz_val[nev - 1] and
        // ritz_val[nev] are conjugate pairs
        if(is_complex(ritz_val[nev - 1], prec) &&
           is_conj(ritz_val[nev - 1], ritz_val[nev], prec))
        {
            nev_new = nev + 1;
        }
        // Adjust nev_new again, according to dnaup2.f line 660~674 in ARPACK
        nev_new = nev_new + std::min(nconv, (ncv - nev_new) / 2);
        if(nev_new == 1 && ncv >= 6)
            nev_new = ncv / 2;
        else if(nev_new == 1 && ncv > 3)
            nev_new = 2;

        if(nev_new > ncv - 2)
            nev_new = ncv - 2;

        // Examine conjugate pairs again
        if(is_complex(ritz_val[nev_new - 1], prec) &&
           is_conj(ritz_val[nev_new - 1], ritz_val[nev_new], prec))
        {
            nev_new++;
        }

        return nev_new;
    }

    // Retrieve and sort ritz values and ritz vectors
    void retrieve_ritzpair()
    {
        EigenSolver eig(fac_H);
        ComplexVector evals = eig.eigenvalues();
        ComplexMatrix evecs = eig.eigenvectors();

        std::vector<SortPair> pairs(ncv);
        EigenvalueComparator<Complex, SelectionRule> comp;
        for(int i = 0; i < ncv; i++)
        {
            pairs[i].first = evals[i];
            pairs[i].second = i;
        }
        std::sort(pairs.begin(), pairs.end(), comp);

        // Copy the ritz values and vectors to ritz_val and ritz_vec, respectively
        for(int i = 0; i < ncv; i++)
        {
            ritz_val[i] = pairs[i].first;
        }
        for(int i = 0; i < nev; i++)
        {
            ritz_vec.col(i) = evecs.col(pairs[i].second);
        }
    }

protected:
    // Sort the first nev Ritz pairs in decreasing magnitude order
    // This is used to return the final results
    virtual void sort_ritzpair()
    {
        std::vector<SortPair> pairs(nev);
        EigenvalueComparator<Complex, LARGEST_MAGN> comp;
        for(int i = 0; i < nev; i++)
        {
            pairs[i].first = ritz_val[i];
            pairs[i].second = i;
        }
        std::sort(pairs.begin(), pairs.end(), comp);

        ComplexMatrix new_ritz_vec(ncv, nev);
        BoolArray new_ritz_conv(nev);

        for(int i = 0; i < nev; i++)
        {
            ritz_val[i] = pairs[i].first;
            new_ritz_vec.col(i) = ritz_vec.col(pairs[i].second);
            new_ritz_conv[i] = ritz_conv[pairs[i].second];
        }

        ritz_vec.swap(new_ritz_vec);
        ritz_conv.swap(new_ritz_conv);
    }

public:
    GenEigsSolver(OpType *op_, int nev_, int ncv_) :
        op(op_),
        dim_n(op->rows()),
        nev(nev_),
        ncv(ncv_ > dim_n ? dim_n : ncv_),
        nmatop(0),
        niter(0),
        prec(std::pow(std::numeric_limits<Scalar>::epsilon(), Scalar(2.0 / 3)))
    {
        if(nev_ < 1 || nev_ >= dim_n)
            throw std::invalid_argument("nev must be greater than zero and less than the size of the matrix");

        if(ncv_ <= nev_)
            throw std::invalid_argument("ncv must be greater than nev");
    }

    // Initialization and clean-up
    void init(const Scalar *init_resid)
    {
        // Reset all matrices/vectors to zero
        fac_V.resize(dim_n, ncv);
        fac_H.resize(ncv, ncv);
        fac_f.resize(dim_n);
        ritz_val.resize(ncv);
        ritz_vec.resize(ncv, nev);
        ritz_conv.resize(nev);

        fac_V.setZero();
        fac_H.setZero();
        fac_f.setZero();
        ritz_val.setZero();
        ritz_vec.setZero();
        ritz_conv.setZero();

        Vector v(dim_n);
        std::copy(init_resid, init_resid + dim_n, v.data());
        Scalar vnorm = v.norm();
        if(vnorm < prec)
            throw std::invalid_argument("initial residual vector cannot be zero");
        v /= vnorm;

        Vector w(dim_n);
        op->perform_op(v.data(), w.data());
        nmatop++;

        fac_H(0, 0) = v.dot(w);
        fac_f = w - v * fac_H(0, 0);
        fac_V.col(0) = v;
    }
    // Initialization with random initial coefficients
    void init()
    {
        Vector init_resid = Vector::Random(dim_n);
        init_resid.array() -= 0.5;
        init(init_resid.data());
    }

    // Compute Ritz pairs and return the number of iteration
    int compute(int maxit = 1000, Scalar tol = 1e-10)
    {
        // The m-step Arnoldi factorization
        factorize_from(1, ncv, fac_f);
        retrieve_ritzpair();
        // Restarting
        int i, nconv, nev_adj;
        for(i = 0; i < maxit; i++)
        {
            nconv = num_converged(tol);
            if(nconv >= nev)
                break;

            nev_adj = nev_adjusted(nconv);
            restart(nev_adj);
        }
        // Sorting results
        sort_ritzpair();

        niter += i + 1;

        return std::min(nev, nconv);
    }

    // Return the number of restarting iterations
    int num_iterations() { return niter; }

    // Return the number of matrix operations
    int num_operations() { return nmatop; }

    // Return converged eigenvalues
    ComplexVector eigenvalues()
    {
        int nconv = ritz_conv.cast<int>().sum();
        ComplexVector res(nconv);

        if(!nconv)
            return res;

        int j = 0;
        for(int i = 0; i < nev; i++)
        {
            if(ritz_conv[i])
            {
                res[j] = ritz_val[i];
                j++;
            }
        }

        return res;
    }

    // Return converged eigenvectors
    ComplexMatrix eigenvectors()
    {
        int nconv = ritz_conv.cast<int>().sum();
        ComplexMatrix res(dim_n, nconv);

        if(!nconv)
            return res;

        ComplexMatrix ritz_vec_conv(ncv, nconv);
        int j = 0;
        for(int i = 0; i < nev; i++)
        {
            if(ritz_conv[i])
            {
                ritz_vec_conv.col(j) = ritz_vec.col(i);
                j++;
            }
        }

        res.noalias() = fac_V * ritz_vec_conv;

        return res;
    }
};





template <typename Scalar = double,
          int SelectionRule = LARGEST_MAGN,
          typename OpType = DenseGenRealShiftSolve<double> >
class GenEigsRealShiftSolver: public GenEigsSolver<Scalar, SelectionRule, OpType>
{
private:
    typedef std::complex<Scalar> Complex;
    typedef Eigen::Array<Complex, Eigen::Dynamic, 1> ComplexArray;

    Scalar sigma;

    // First transform back the ritz values, and then sort
    void sort_ritzpair()
    {
        ComplexArray ritz_val_org = Scalar(1.0) / this->ritz_val.head(this->nev).array() + sigma;
        this->ritz_val.head(this->nev) = ritz_val_org;
        GenEigsSolver<Scalar, SelectionRule, OpType>::sort_ritzpair();
    }
public:
    GenEigsRealShiftSolver(OpType *op_, int nev_, int ncv_, Scalar sigma_) :
        GenEigsSolver<Scalar, SelectionRule, OpType>(op_, nev_, ncv_),
        sigma(sigma_)
    {
        this->op->set_shift(sigma);
    }
};

#endif // GEN_EIGS_SOLVER_H
