/*
    -- MAGMA (version 1.6.3-beta1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date August 2015

       @generated from zgelqf.cpp normal z -> s, Tue Aug 25 16:35:16 2015

*/
#include "common_magma.h"

#define REAL

/**
    Purpose
    -------
    SGELQF computes an LQ factorization of a REAL M-by-N matrix A:
    A = L * Q.

    Arguments
    ---------
    @param[in]
    m       INTEGER
            The number of rows of the matrix A.  M >= 0.

    @param[in]
    n       INTEGER
            The number of columns of the matrix A.  N >= 0.

    @param[in,out]
    A       REAL array, dimension (LDA,N)
            On entry, the M-by-N matrix A.
            On exit, the elements on and below the diagonal of the array
            contain the m-by-min(m,n) lower trapezoidal matrix L (L is
            lower triangular if m <= n); the elements above the diagonal,
            with the array TAU, represent the orthogonal matrix Q as a
            product of elementary reflectors (see Further Details).
    \n
            Higher performance is achieved if A is in pinned memory, e.g.
            allocated using magma_malloc_pinned.

    @param[in]
    lda     INTEGER
            The leading dimension of the array A.  LDA >= max(1,M).

    @param[out]
    tau     REAL array, dimension (min(M,N))
            The scalar factors of the elementary reflectors (see Further
            Details).

    @param[out]
    work    (workspace) REAL array, dimension (MAX(1,LWORK))
            On exit, if INFO = 0, WORK[0] returns the optimal LWORK.
    \n
            Higher performance is achieved if WORK is in pinned memory, e.g.
            allocated using magma_malloc_pinned.

    @param[in]
    lwork   INTEGER
            The dimension of the array WORK.  LWORK >= max(1,M).
            For optimum performance LWORK >= M*NB, where NB is the
            optimal blocksize.
    \n
            If LWORK = -1, then a workspace query is assumed; the routine
            only calculates the optimal size of the WORK array, returns
            this value as the first entry of the WORK array, and no error
            message related to LWORK is issued.

    @param[out]
    info    INTEGER
      -     = 0:  successful exit
      -     < 0:  if INFO = -i, the i-th argument had an illegal value
                  or another error occured, such as memory allocation failed.

    Further Details
    ---------------
    The matrix Q is represented as a product of elementary reflectors

       Q = H(k) . . . H(2) H(1), where k = min(m,n).

    Each H(i) has the form

       H(i) = I - tau * v * v'

    where tau is a real scalar, and v is a real vector with
    v(1:i-1) = 0 and v(i) = 1; v(i+1:n) is stored on exit in A(i,i+1:n),
    and tau in TAU(i).

    @ingroup magma_sgelqf_comp
    ********************************************************************/
extern "C" magma_int_t
magma_sgelqf(
    magma_int_t m, magma_int_t n,
    float *A,    magma_int_t lda,   float *tau,
    float *work, magma_int_t lwork,
    magma_int_t *info)
{
    #define  dA(i_, j_)  (dA  + (i_) + (j_)*ldda)
    #define dAT(i_, j_)  (dAT + (i_) + (j_)*ldda)
    
    const float c_one = MAGMA_S_ONE;
    const magma_int_t        ione  = 1;
    MAGMA_UNUSED( ione );  // used only for real
    
    magmaFloat_ptr dA, dAT;
    magma_int_t min_mn, maxm, maxn, maxdim, nb;
    magma_int_t iinfo, ldda, lddat;
    int lquery;

    /* Function Body */
    *info = 0;
    nb = magma_get_sgelqf_nb(m);
    min_mn = min(m,n);

    work[0] = MAGMA_S_MAKE( (float)(m*nb), 0 );
    lquery = (lwork == -1);
    if (m < 0) {
        *info = -1;
    } else if (n < 0) {
        *info = -2;
    } else if (lda < max(1,m)) {
        *info = -4;
    } else if (lwork < max(1,m) && ! lquery) {
        *info = -7;
    }
    if (*info != 0) {
        magma_xerbla( __func__, -(*info) );
        return *info;
    }
    else if (lquery) {
        return *info;
    }

    /*  Quick return if possible */
    if (min_mn == 0) {
        work[0] = c_one;
        return *info;
    }

    maxm = magma_roundup( m, 32 );
    maxn = magma_roundup( n, 32 );
    maxdim = max(maxm, maxn);

    // copy to GPU and transpose
    if (maxdim*maxdim < 2*maxm*maxn) {
        // close to square, do everything in-place
        ldda  = maxdim;
        lddat = maxdim;

        if (MAGMA_SUCCESS != magma_smalloc( &dA, maxdim*maxdim )) {
            *info = MAGMA_ERR_DEVICE_ALLOC;
            return *info;
        }

        magma_ssetmatrix( m, n, A, lda, dA(0,0), ldda );
        dAT = dA;
        magmablas_stranspose_inplace( lddat, dAT(0,0), lddat );
    }
    else {
        // rectangular, do everything out-of-place
        ldda  = maxm;
        lddat = maxn;

        if (MAGMA_SUCCESS != magma_smalloc( &dA, 2*maxn*maxm )) {
            *info = MAGMA_ERR_DEVICE_ALLOC;
            return *info;
        }

        magma_ssetmatrix( m, n, A, lda, dA(0,0), ldda );

        dAT = dA + maxn * maxm;
        magmablas_stranspose( m, n, dA(0,0), ldda, dAT(0,0), lddat );
    }

    // factor QR
    magma_sgeqrf2_gpu( n, m, dAT(0,0), lddat, tau, &iinfo );
    assert( iinfo >= 0 );
    if ( iinfo > 0 ) {
        *info = iinfo;
    }
    
    // conjugate tau
    #ifdef COMPLEX
    lapackf77_slacgv( &min_mn, tau, &ione );
    #endif

    // undo transpose
    if (maxdim*maxdim < 2*maxm*maxn) {
        magmablas_stranspose_inplace( lddat, dAT(0,0), lddat );
        magma_sgetmatrix( m, n, dA(0,0), ldda, A, lda );
    } else {
        magmablas_stranspose( n, m, dAT(0,0), lddat, dA(0,0), ldda );
        magma_sgetmatrix( m, n, dA(0,0), ldda, A, lda );
    }

    magma_free( dA );

    return *info;
} /* magma_sgelqf */
