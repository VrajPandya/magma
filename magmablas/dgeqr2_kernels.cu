/*
   -- MAGMA (version 2.0.0) --
   Univ. of Tennessee, Knoxville
   Univ. of California, Berkeley
   Univ. of Colorado, Denver
   @date February 2016

   @author Azzam Haidar
   @author Tingxing Dong

   @generated from magmablas/zgeqr2_kernels.cu normal z -> d, Tue Feb  9 16:05:39 2016
*/

#include "magma_internal.h"
#include "batched_kernel_param.h"



__global__ void
dgeqrf_copy_upper_kernel_batched(                
                  int n, int nb,
                  double **dV_array,    int ldv,
                  double **dR_array,    int ldr)
{
    double *dV = dV_array[blockIdx.x];
    double *dR = dR_array[blockIdx.x];

    int tid = threadIdx.x;

    int column = (tid / nb + 1) * nb; 
    
    if ( tid < n && column < n) 
    {
        for (int i=column; i < n; i++)
        {
            dR[tid + i * ldr]  =  dV[tid + i * ldv];  
        }
    }
}


/**
    Purpose
    -------
    These are internal routines that might have many assumption.
    They are used in dgeqrf_batched.cpp   

    Copy part of the data in dV to dR
    
    Arguments
    ---------
    @param[in]
    n       INTEGER
            The order of the matrix .  N >= 0.

    @param[in]
    nb      INTEGER
            Tile size in matrix.  nb <= N.

    @param[in]
    dV_array    Array of pointers, dimension (batchCount).
            Each is a DOUBLE PRECISION array on the GPU, dimension (LDDA,N).

    @param[in]
    lddv    INTEGER
            The leading dimension of each array V.  LDDV >= max(1,N).

    @param[in,out]
    dR_array    Array of pointers, dimension (batchCount).
            Each is a DOUBLE PRECISION array on the GPU, dimension (LDDR,N).

    @param[in]
    lddr    INTEGER
            The leading dimension of each array R.  LDDR >= max(1,N).

    @param[in]
    batchCount  INTEGER
                The number of matrices to operate on.

    @param[in]
    queue   magma_queue_t
            Queue to execute in.

    @ingroup magma_dgeqrf_aux
    ********************************************************************/

void dgeqrf_copy_upper_batched(                
    magma_int_t n, magma_int_t nb,
    double **dV_array, magma_int_t lddv,
    double **dR_array, magma_int_t lddr,
    magma_int_t batchCount,
    magma_queue_t queue)
{
    /* 
        copy some data in dV to dR
    */
    if ( nb >= n) return;
    
    dgeqrf_copy_upper_kernel_batched
        <<< batchCount, n, 0, queue->cuda_stream() >>>
        ( n, nb, dV_array, lddv, dR_array, lddr );
}
