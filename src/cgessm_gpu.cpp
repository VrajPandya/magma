/*
    -- MAGMA (version 1.7.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date September 2015

       @author Hatem Ltaief
       @author Mathieu Faverge

       @generated from zgessm_gpu.cpp normal z -> c, Fri Sep 11 18:29:26 2015

*/
#include "common_magma.h"

/**
    Purpose
    -------
    CGESSM applies the factors L computed by CGETRF_INCPIV to
    a complex M-by-N tile A.
    
    Arguments
    ---------
    @param[in]
    order   magma_order_t
      -     = MagmaRowMajor:  dA is in row-major order (more efficient).
      -     = MagmaColMajor:  dA is in column-major order.

    @param[in]
    m       INTEGER
            The number of rows of the tile A.  M >= 0.

    @param[in]
    n       INTEGER
            The number of columns of the tile A.  N >= 0.

    @param[in]
    k       INTEGER
            The number of columns of the tile L.  K >= 0.

    @param[in]
    ib      INTEGER
            The inner-blocking size.  IB >= 0.

    @param[in]
    ipiv    INTEGER array on the cpu.
            The pivot indices array of size K as returned by
            CGETRF_INCPIV.

    @param[in]
    dL1     COMPLEX array, dimension(LDDL1, N)
            The IB-by-K matrix in which is stored L^(-1) as returned by GETRF_INCPIV

    @param[in]
    lddl1   INTEGER
            The leading dimension of the array L1.  LDDL1 >= max(1,2*IB).

    @param[in]
    dL      COMPLEX array, dimension(LDDL, N)
            The M-by-K lower triangular tile on the gpu.

    @param[in]
    lddl    INTEGER
            The leading dimension of the array L.  LDDL >= max(1,M).

    @param[in,out]
    dA      COMPLEX array, dimension (LDDA, N)
            On entry, the M-by-N tile A on the gpu.
            On exit, updated by the application of L on the gpu.

    @param[in]
    ldda    INTEGER
            The leading dimension of the array A.  LDDA >= max(1,M).

    @param[out]
    info    INTEGER
      -     = 0:  successful exit
      -     < 0:  if INFO = -i, the i-th argument had an illegal value
                  or another error occured, such as memory allocation failed.

    @ingroup magma_cgesv_tile
    ********************************************************************/
extern "C" magma_int_t
magma_cgessm_gpu(
    magma_order_t order, magma_int_t m, magma_int_t n, magma_int_t k, magma_int_t ib,
    magma_int_t *ipiv,
    magmaFloatComplex_ptr dL1, magma_int_t lddl1,
    magmaFloatComplex_ptr dL,  magma_int_t lddl,
    magmaFloatComplex_ptr dA,  magma_int_t ldda,
    magma_int_t *info)
{
#define AT(i,j) (dAT + (i)*ldda + (j)      )
#define L(i,j)  (dL  + (i)      + (j)*lddl )
#define dL1(j)  (dL1            + (j)*lddl1)

    magmaFloatComplex c_one     = MAGMA_C_ONE;
    magmaFloatComplex c_neg_one = MAGMA_C_NEG_ONE;

    int i, sb;
    magmaFloatComplex_ptr dAT;

    /* Check arguments */
    *info = 0;
    if (m < 0)
        *info = -1;
    else if (n < 0)
        *info = -2;
    else if (ldda < max(1,m))
        *info = -4;

    if (*info != 0) {
        magma_xerbla( __func__, -(*info) );
        return *info;
    }

    /* Quick return if possible */
    if (m == 0 || n == 0)
        return *info;

    if ( order == MagmaColMajor ) {
        magmablas_cgetmo_in( dA, dAT, ldda, m, n );
    } else {
        dAT = dA;
    }

    for (i = 0; i < k; i += ib) {
        sb = min(ib, k-i);

        magmablas_claswp( n, dAT, ldda, i+1, i+sb, ipiv, 1 );

#ifndef WITHOUTTRTRI
        magma_ctrmm( MagmaRight, MagmaLower, MagmaTrans, MagmaUnit,
                     n, sb,
                     c_one, dL1(i),   lddl1,
                            AT(i, 0), ldda);
#else
        magma_ctrsm( MagmaRight, MagmaLower, MagmaTrans, MagmaUnit,
                     n, sb,
                     c_one, L( i, i), lddl,
                            AT(i, 0), ldda);
#endif

        if ( (i+sb) < m) {
            magma_cgemm( MagmaNoTrans, MagmaTrans,
                         n, m-(i+sb), sb,
                         c_neg_one, AT(i,    0), ldda,
                                    L( i+sb, i), lddl,
                         c_one,     AT(i+sb, 0), ldda );
        }
    }

    if ( order == MagmaColMajor ) {
        magmablas_cgetmo_in( dA, dAT, ldda, m, n );
    }

    return *info;
} /* magma_cgessm_gpu */
