/*
    -- MAGMA (version 2.1.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date August 2016

       @generated from sparse-iter/control/magma_zmcsrpass_gpu.cpp, normal z -> c, Tue Aug 30 09:38:49 2016
       @author Hartwig Anzt
*/

//  in this file, many routines are taken from
//  the IO functions provided by MatrixMarket

#include "magmasparse_internal.h"


/**
    Purpose
    -------

    Passes a CSR matrix to MAGMA (located on DEV).

    Arguments
    ---------

    @param[in]
    m           magma_int_t
                number of rows

    @param[in]
    n           magma_int_t
                number of columns

    @param[in]
    row         magmaIndex_ptr
                row pointer

    @param[in]
    col         magmaIndex_ptr
                column indices

    @param[in]
    val         magmaFloatComplex_ptr
                array containing matrix entries

    @param[out]
    A           magma_c_matrix*
                matrix in magma sparse matrix format
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_caux
    ********************************************************************/

extern "C"
magma_int_t
magma_ccsrset_gpu(
    magma_int_t m,
    magma_int_t n,
    magmaIndex_ptr row,
    magmaIndex_ptr col,
    magmaFloatComplex_ptr val,
    magma_c_matrix *A,
    magma_queue_t queue )
{   
    A->num_rows = m;
    A->num_cols = n;
    magma_index_t nnz;
    magma_index_getvector( 1, row+m, 1, &nnz, 1, queue );
    A->nnz = (magma_int_t) nnz;
    A->storage_type = Magma_CSR;
    A->memory_location = Magma_DEV;
    A->dval = val;
    A->dcol = col;
    A->drow = row;

    return MAGMA_SUCCESS;
}


/**
    Purpose
    -------

    Passes a MAGMA matrix to CSR structure (located on DEV).

    Arguments
    ---------

    @param[in]
    A           magma_c_matrix
                magma sparse matrix in CSR format

    @param[out]
    m           magma_int_t
                number of rows

    @param[out]
    n           magma_int_t
                number of columns

    @param[out]
    row         magmaIndex_ptr
                row pointer

    @param[out]
    col         magmaIndex_ptr
                column indices

    @param[out]
    val         magmaFloatComplex_ptr
                array containing matrix entries

    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_caux
    ********************************************************************/

extern "C"
magma_int_t
magma_ccsrget_gpu(
    magma_c_matrix A,
    magma_int_t *m,
    magma_int_t *n,
    magmaIndex_ptr *row,
    magmaIndex_ptr *col,
    magmaFloatComplex_ptr *val,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    
    magma_c_matrix A_DEV={Magma_CSR}, A_CSR={Magma_CSR};
    
    if ( A.memory_location == Magma_DEV && A.storage_type == Magma_CSR ) {
        *m = A.num_rows;
        *n = A.num_cols;
        *val = A.dval;
        *col = A.dcol;
        *row = A.drow;
    } else {
        CHECK( magma_cmconvert( A, &A_CSR, A.storage_type, Magma_CSR, queue ));
        CHECK( magma_cmtransfer( A_CSR, &A_DEV, A.memory_location, Magma_DEV, queue ));
        magma_ccsrget_gpu( A_DEV, m, n, row, col, val, queue );
    }
    
cleanup:
    magma_cmfree( &A_CSR, queue );
    magma_cmfree( &A_DEV, queue );
    return info;
}
