/*
    -- MAGMA (version 1.6.3-beta1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date August 2015

       @generated from zlacpy_cnjg.cu normal z -> d, Tue Aug 25 16:35:08 2015

*/
#include "common_magma.h"

#define BLOCK_SIZE 64

/*********************************************************
 *
 * SWAP BLAS: permute to set of N elements
 *
 ********************************************************/
/*
 *  First version: line per line
 */
typedef struct {
    double *A1;
    double *A2;
    int n, lda1, lda2;
} magmagpu_dlacpy_cnjg_params_t;

__global__ void magmagpu_dlacpy_cnjg( magmagpu_dlacpy_cnjg_params_t params )
{
    unsigned int x = threadIdx.x + blockDim.x*blockIdx.x;
    unsigned int offset1 = x*params.lda1;
    unsigned int offset2 = x*params.lda2;
    if ( x < params.n )
    {
        double *A1  = params.A1 + offset1;
        double *A2  = params.A2 + offset2;
        *A2 = MAGMA_D_CNJG(*A1);
    }
}


extern "C" void 
magmablas_dlacpy_cnjg_q(
    magma_int_t n,
    double *dA1, magma_int_t lda1, 
    double *dA2, magma_int_t lda2,
    magma_queue_t queue )
{
    int blocksize = 64;
    dim3 blocks( magma_ceildiv( n, blocksize ) );
    magmagpu_dlacpy_cnjg_params_t params = { dA1, dA2, int(n), int(lda1), int(lda2) };
    magmagpu_dlacpy_cnjg<<< blocks, blocksize, 0, queue >>>( params );
}


extern "C" void 
magmablas_dlacpy_cnjg(
    magma_int_t n,
    double *dA1, magma_int_t lda1, 
    double *dA2, magma_int_t lda2)
{
    magmablas_dlacpy_cnjg_q( n, dA1, lda1, dA2, lda2, magma_stream );
}
