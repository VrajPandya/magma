/*
    -- MAGMA (version 2.0.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date February 2016

       @generated from magmablas/zgetmatrix_transpose.cpp normal z -> c, Tue Feb  9 16:05:33 2016

*/
#include "magma_internal.h"

//
//      m, n - dimensions in the output (hA) matrix.
//             This routine copies the dAT matrix from the GPU
//             to hA on the CPU. In addition, the output matrix
//             is transposed. The routine uses a buffer of size
//             2*lddwork*nb pointed to by dwork (lddwork > m) on the GPU. 
//             Note that lda >= m and lddat >= n.
//
extern "C" void 
magmablas_cgetmatrix_transpose_q(
    magma_int_t m, magma_int_t n,
    magmaFloatComplex_const_ptr dAT, magma_int_t ldda,
    magmaFloatComplex          *hA,  magma_int_t lda,
    magmaFloatComplex_ptr       dwork,  magma_int_t lddwork, magma_int_t nb,
    magma_queue_t queues[2] )
{
#define    hA(i_, j_)    (hA + (i_) + (j_)*lda)
#define   dAT(i_, j_)   (dAT + (i_) + (j_)*ldda)
#define dwork(i_, j_) (dwork + (i_) + (j_)*lddwork)

    magma_int_t i = 0, j = 0, ib;

    /* Quick return */
    if ( (m == 0) || (n == 0) )
        return;

    // TODO standard check arguments
    if (lda < m || ldda < n || lddwork < m) {
        fprintf( stderr, "%s: wrong arguments.\n", __func__ );
        return;
    }

    for (i=0; i < n; i += nb) {
        /* Move data from GPU to CPU using 2 buffers; 1st transpose the data on the GPU */
        ib = min(n-i, nb);
        
        magmablas_ctranspose_q( ib, m, dAT(i,0), ldda, dwork(0,(j%2)*nb), lddwork, queues[j%2] );
        magma_cgetmatrix_async( m, ib,
                                dwork(0,(j%2)*nb), lddwork,
                                hA(0,i), lda, queues[j%2] );
        j++;
    }
}
