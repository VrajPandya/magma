/*
    -- MAGMA (version 1.5.0-beta2) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date May 2014

       @generated from zgecsrmv.cu normal z -> c, Fri May 30 10:41:36 2014

*/
#include "common_magma.h"

#if (GPUSHMEM < 200)
   #define BLOCK_SIZE 256
#else
   #define BLOCK_SIZE 256
#endif


// CSR-SpMV kernel
__global__ void 
cgecsrmv_kernel( int num_rows, int num_cols, 
                 magmaFloatComplex alpha, 
                 magmaFloatComplex *d_val, 
                 magma_index_t *d_rowptr, 
                 magma_index_t *d_colind,
                 magmaFloatComplex *d_x,
                 magmaFloatComplex beta, 
                 magmaFloatComplex *d_y){

    int row = blockIdx.x*blockDim.x+threadIdx.x;
    int j;

    if(row<num_rows){
        magmaFloatComplex dot = MAGMA_C_ZERO;
        int start = d_rowptr[ row ];
        int end = d_rowptr[ row+1 ];
        for( j=start; j<end; j++)
            dot += d_val[ j ] * d_x[ d_colind[j] ];
        d_y[ row ] =  dot *alpha + beta * d_y[ row ];
    }
}

// shifted CSR-SpMV kernel
__global__ void 
cgecsrmv_kernel_shift( int num_rows, int num_cols, 
                       magmaFloatComplex alpha, 
                       magmaFloatComplex lambda, 
                       magmaFloatComplex *d_val, 
                       magma_index_t *d_rowptr, 
                       magma_index_t *d_colind,
                       magmaFloatComplex *d_x,
                       magmaFloatComplex beta, 
                       int offset,
                       int blocksize,
                       magma_index_t *add_rows,
                       magmaFloatComplex *d_y){

    int row = blockIdx.x*blockDim.x+threadIdx.x;
    int j;

    if(row<num_rows){
        magmaFloatComplex dot = MAGMA_C_ZERO;
        int start = d_rowptr[ row ];
        int end = d_rowptr[ row+1 ];
        for( j=start; j<end; j++)
            dot += d_val[ j ] * d_x[ d_colind[j] ];
        if( row<blocksize )
            d_y[ row ] = dot * alpha - lambda 
                        * d_x[ offset+row ] + beta * d_y [ row ];
        else
            d_y[ row ] = dot * alpha - lambda 
                        * d_x[ add_rows[row-blocksize] ] + beta * d_y [ row ];   
    }
}


/*  -- MAGMA (version 1.5.0-beta2) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date May 2014

    Purpose
    =======
    
    This routine computes y = alpha *  A *  x + beta * y on the GPU.
    The input format is CSR (val, row, col).
    
    Arguments
    =========

    magma_int_t m                   number of rows in A
    magma_int_t n                   number of columns in A 
    magmaFloatComplex alpha        scalar multiplier
    magmaFloatComplex *d_val       array containing values of A in CSR
    magma_int_t *d_rowptr           rowpointer of A in CSR
    magma_int_t *d_colind           columnindices of A in CSR
    magmaFloatComplex *d_x         input vector x
    magmaFloatComplex beta         scalar multiplier
    magmaFloatComplex *d_y         input/output vector y

    ======================================================================    */

extern "C" magma_int_t
magma_cgecsrmv(     magma_trans_t transA,
                    magma_int_t m, magma_int_t n,
                    magmaFloatComplex alpha,
                    magmaFloatComplex *d_val,
                    magma_index_t *d_rowptr,
                    magma_index_t *d_colind,
                    magmaFloatComplex *d_x,
                    magmaFloatComplex beta,
                    magmaFloatComplex *d_y ){

    dim3 grid( (m+BLOCK_SIZE-1)/BLOCK_SIZE, 1, 1);

    cgecsrmv_kernel<<< grid, BLOCK_SIZE, 0, magma_stream >>>
                    (m, n, alpha, d_val, d_rowptr, d_colind, d_x, beta, d_y);

    return MAGMA_SUCCESS;
}



/*  -- MAGMA (version 1.5.0-beta2) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date May 2014

    Purpose
    =======
    
    This routine computes y = alpha * ( A -lambda I ) * x + beta * y on the GPU.
    It is a shifted version of the CSR-SpMV.
    
    Arguments
    =========

    magma_int_t m                   number of rows in A
    magma_int_t n                   number of columns in A 
    magmaFloatComplex alpha        scalar multiplier
    magmaFloatComplex alpha        scalar multiplier
    magmaFloatComplex *d_val       array containing values of A in CSR
    magma_int_t *d_rowptr           rowpointer of A in CSR
    magma_int_t *d_colind           columnindices of A in CSR
    magmaFloatComplex *d_x         input vector x
    magmaFloatComplex beta         scalar multiplier
    magmaFloatComplex *d_y         input/output vector y

    ======================================================================    */

extern "C" magma_int_t
magma_cgecsrmv_shift( magma_trans_t transA,
                      magma_int_t m, magma_int_t n,
                      magmaFloatComplex alpha,
                      magmaFloatComplex lambda,
                      magmaFloatComplex *d_val,
                      magma_index_t *d_rowptr,
                      magma_index_t *d_colind,
                      magmaFloatComplex *d_x,
                      magmaFloatComplex beta,
                      int offset,
                      int blocksize,
                      magma_index_t *add_rows,
                      magmaFloatComplex *d_y ){

    dim3 grid( (m+BLOCK_SIZE-1)/BLOCK_SIZE, 1, 1);

    cgecsrmv_kernel_shift<<< grid, BLOCK_SIZE, 0, magma_stream >>>
                         (m, n, alpha, lambda, d_val, d_rowptr, d_colind, d_x, 
                                    beta, offset, blocksize, add_rows, d_y);

    return MAGMA_SUCCESS;
}


