/*
    -- MAGMA (version 2.0.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date February 2016

       @generated from sparse-iter/src/magma_zcustomspmv.cpp normal z -> s, Tue Feb  9 16:05:58 2016
       @author Hartwig Anzt

*/
#include "magmasparse_internal.h"


/**
    Purpose
    -------

    This is an interface to any custom sparse matrix vector product.
    It should compute y = alpha*FUNCTION(x) + beta*y
    The vectors are located on the device, the scalars on the CPU.


    Arguments
    ---------

    @param[in]
    alpha       float
                scalar alpha

    @param[in]
    x           magma_s_matrix
                input vector x
                
    @param[in]
    beta        float
                scalar beta
    @param[out]
    y           magma_s_matrix
                output vector y
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_sblas
    ********************************************************************/

extern "C" magma_int_t
magma_scustomspmv(
    float alpha,
    magma_s_matrix x,
    float beta,
    magma_s_matrix y,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    // vector access via x.dval, y.dval
    // sizes are x.num_rows, x.num_cols
    
    return info;
}
