#ifndef DOUBLE_SHIFT_QR_H
#define DOUBLE_SHIFT_QR_H

#include <Eigen/Core>
#include <vector>
#include <stdexcept>

template <typename Scalar = double>
class DoubleShiftQR
{
private:
    typedef Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic> Matrix;
    typedef Eigen::Matrix<Scalar, 3, Eigen::Dynamic> Matrix3X;
    typedef Eigen::Matrix<Scalar, Eigen::Dynamic, 1> Vector;

    typedef Eigen::Ref<Matrix> GenericMatrix;
    typedef const Eigen::Ref<const Matrix> ConstGenericMatrix;

    int n;
    Matrix mat_H;
    Scalar shift_s;
    Scalar shift_t;
    // Householder reflectors
    Matrix3X ref_u;
    // Approximately zero
    const Scalar prec;
    bool computed;

    void compute_reflector(const Scalar &x1, const Scalar &x2, const Scalar &x3, int ind)
    {
        Scalar tmp = x2 * x2 + x3 * x3;
        // x1' = x1 - rho * ||x||
        // rho = -sign(x1)
        Scalar x1_new = x1 - ((x1 < 0) - (x1 > 0)) * std::sqrt(x1 * x1 + tmp);
        Scalar x_norm = std::sqrt(x1_new * x1_new + tmp);
        if(x_norm <= prec)
        {
            ref_u(0, ind) = 0;
            ref_u(1, ind) = 0;
            ref_u(2, ind) = 0;
        } else {
            ref_u(0, ind) = x1_new / x_norm;
            ref_u(1, ind) = x2 / x_norm;
            ref_u(2, ind) = x3 / x_norm;
        }
    }

    void compute_reflector(const Scalar *x, int ind)
    {
        compute_reflector(x[0], x[1], x[2], ind);
    }

    void compute_reflectors_from_block(GenericMatrix X, int start_ind)
    {
        // For the block X, we can assume that ncol == nrow,
        // and all sub-diagonal elements are non-zero
        const int nrow = X.rows();
        // For block size <= 2, there is no need to apply reflectors
        if(nrow == 1)
        {
            compute_reflector(0, 0, 0, start_ind);
            return;
        } else if(nrow == 2) {
            compute_reflector(0, 0, 0, start_ind);
            compute_reflector(0, 0, 0, start_ind + 1);
            return;
        }
        // For block size >=3, use the regular strategy
        Scalar x = X(0, 0) * (X(0, 0) - shift_s) + X(0, 1) * X(1, 0) + shift_t;
        Scalar y = X(1, 0) * (X(0, 0) + X(1, 1) - shift_s);
        Scalar z = X(2, 1) * X(1, 0);
        compute_reflector(x, y, z, start_ind);
        // Apply the first reflector
        apply_PX(X.template topRows<3>(), start_ind);
        apply_XP(X.topLeftCorner(std::min(nrow, 4), 3), start_ind);

        // Calculate the following reflectors
        for(int i = 1; i < nrow - 2; i++)
        {
            // If entering this loop, nrow is at least 4.

            compute_reflector(&X(i, i - 1), start_ind + i);
            // Apply the reflector to X
            apply_PX(X.block(i, i - 1, 3, nrow - i + 1), start_ind + i);
            apply_XP(X.block(0, i, std::min(nrow, i + 4), 3), start_ind + i);
        }

        // The last reflector
        compute_reflector(X(nrow - 2, nrow - 3), X(nrow - 1, nrow - 3), 0, start_ind + nrow - 2);
        compute_reflector(0, 0, 0, start_ind + nrow - 1);
        // Apply the reflector to X
        apply_PX(X.template block<2, 3>(nrow - 2, nrow - 3), start_ind + nrow - 2);
        apply_XP(X.block(0, nrow - 2, nrow, 2), start_ind + nrow - 2);
    }

    // P = I - 2 * u * u' = P'
    // PX = X - 2 * u * (u'X)
    void apply_PX(GenericMatrix X, int u_ind)
    {
        const int nrow = X.rows();
        const int ncol = X.cols();
        const Scalar sqrt_2 = std::sqrt(Scalar(2));

        Scalar u0 = sqrt_2 * ref_u(0, u_ind),
               u1 = sqrt_2 * ref_u(1, u_ind),
               u2 = sqrt_2 * ref_u(2, u_ind);

        if(u0 * u0 + u1 * u1 + u2 * u2 <= prec)
            return;

        if(nrow == 2)
        {
            for(int i = 0; i < ncol; i++)
            {
                Scalar tmp = u0 * X(0, i) + u1 * X(1, i);
                X(0, i) -= tmp * u0;
                X(1, i) -= tmp * u1;
            }
        } else {
            for(int i = 0; i < ncol; i++)
            {
                Scalar tmp = u0 * X(0, i) + u1 * X(1, i) + u2 * X(2, i);
                X(0, i) -= tmp * u0;
                X(1, i) -= tmp * u1;
                X(2, i) -= tmp * u2;
            }
        }
    }

    // x is a pointer to a vector
    // Px = x - 2 * dot(x, u) * u
    void apply_PX(Scalar *x, int u_ind)
    {
        Scalar u0 = ref_u(0, u_ind),
               u1 = ref_u(1, u_ind),
               u2 = ref_u(2, u_ind);

        if(u0 * u0 + u1 * u1 + u2 * u2 <= prec)
            return;

        Scalar dot2 = x[0] * u0 + x[1] * u1 + (std::abs(u2) <= prec ? 0 : (x[2] * u2));
        dot2 *= 2;
        x[0] -= dot2 * u0;
        x[1] -= dot2 * u1;
        if(std::abs(u2) > prec)
            x[2] -= dot2 * u2;
    }

    // XP = X - 2 * (X * u) * u'
    void apply_XP(GenericMatrix X, int u_ind)
    {
        const int nrow = X.rows();
        const int ncol = X.cols();
        const Scalar sqrt_2 = std::sqrt(Scalar(2));

        Scalar u0 = sqrt_2 * ref_u(0, u_ind),
               u1 = sqrt_2 * ref_u(1, u_ind),
               u2 = sqrt_2 * ref_u(2, u_ind);
        Scalar *X0 = &X(0, 0), *X1 = &X(0, 1);

        if(u0 * u0 + u1 * u1 + u2 * u2 <= prec)
            return;

        if(ncol == 2)
        {
            for(int i = 0; i < nrow; i++)
            {
                Scalar tmp = u0 * X0[i] + u1 * X1[i];
                X0[i] -= tmp * u0;
                X1[i] -= tmp * u1;
            }
        } else {
            Scalar *X2 = &X(0, 2);
            for(int i = 0; i < nrow; i++)
            {
                Scalar tmp = u0 * X0[i] + u1 * X1[i] + u2 * X2[i];
                X0[i] -= tmp * u0;
                X1[i] -= tmp * u1;
                X2[i] -= tmp * u2;
            }
        }
    }

public:
    DoubleShiftQR() :
        n(0),
        prec(std::pow(std::numeric_limits<Scalar>::epsilon(), Scalar(0.9))),
        computed(false)
    {}

    DoubleShiftQR(ConstGenericMatrix &mat, Scalar s, Scalar t) :
        n(mat.rows()),
        mat_H(n, n),
        shift_s(s),
        shift_t(t),
        ref_u(3, n),
        prec(std::pow(std::numeric_limits<Scalar>::epsilon(), Scalar(0.9))),
        computed(false)
    {
        compute(mat, s, t);
    }

    void compute(ConstGenericMatrix &mat, Scalar s, Scalar t)
    {
        if(mat.rows() != mat.cols())
            throw std::invalid_argument("DoubleShiftQR: matrix must be square");

        n = mat.rows();
        mat_H.resize(n, n);
        shift_s = s;
        shift_t = t;
        ref_u.resize(3, n);

        mat_H = mat.template triangularView<Eigen::Upper>();
        mat_H.diagonal(-1) = mat.diagonal(-1);

        std::vector<int> zero_ind;
        zero_ind.reserve(n - 1);
        zero_ind.push_back(0);
        for(int i = 1; i < n - 1; i++)
        {
            if(std::abs(mat_H(i, i - 1)) <= prec)
            {
                mat_H(i, i - 1) = 0;
                zero_ind.push_back(i);
            }
        }
        zero_ind.push_back(n);

        for(std::vector<int>::size_type i = 0; i < zero_ind.size() - 1; i++)
        {
            int start = zero_ind[i];
            int end = zero_ind[i + 1] - 1;
            // Call this block X
            compute_reflectors_from_block(mat_H.block(start, start, end - start + 1, end - start + 1), start);
            // Apply reflectors to the block right to X
            if(end < n - 1 && end - start >= 2)
            {
                for(int j = start; j < end; j++)
                {
                    apply_PX(mat_H.block(j, end + 1, std::min(3, end - j + 1), n - 1 - end), j);
                }
            }
            // Apply reflectors to the block above X
            if(start > 0 && end - start >= 2)
            {
                for(int j = start; j < end; j++)
                {
                    apply_XP(mat_H.block(0, j, start, std::min(3, end - j + 1)), j);
                }
            }
        }

        computed = true;
    }

    Matrix matrix_QtHQ()
    {
        if(!computed)
            throw std::logic_error("DoubleShiftQR: need to call compute() first");

        return mat_H;
    }

    // Q = P0 * P1 * ...
    // Q'y = P_{n-2} * ... * P1 * P0 * y
    void apply_QtY(Vector &y)
    {
        if(!computed)
            throw std::logic_error("DoubleShiftQR: need to call compute() first");

        Scalar *y_ptr = y.data();
        for(int i = 0; i < n - 1; i++, y_ptr++)
        {
            apply_PX(y_ptr, i);
        }
    }

    // Q = P0 * P1 * ...
    // YQ = Y * P0 * P1 * ...
    void apply_YQ(GenericMatrix Y)
    {
        if(!computed)
            throw std::logic_error("DoubleShiftQR: need to call compute() first");

        int nrow = Y.rows();
        for(int i = 0; i < n - 2; i++)
        {
            apply_XP(Y.block(0, i, nrow, 3), i);
        }
        apply_XP(Y.block(0, n - 2, nrow, 2), n - 2);
    }
};


#endif // DOUBLE_SHIFT_QR_H