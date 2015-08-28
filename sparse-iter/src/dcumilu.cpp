/*
    -- MAGMA (version 1.6.3-beta1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date August 2015

       @author Hartwig Anzt

       @generated from zcumilu.cpp normal z -> d, Tue Aug 25 16:35:33 2015
*/
#include "common_magmasparse.h"
#include <cuda.h>  // for CUDA_VERSION

#define PRECISION_d


/**
    Purpose
    -------

    Prepares the ILU preconditioner via the cuSPARSE.

    Arguments
    ---------

    @param[in]
    A           magma_d_matrix
                input matrix A

    @param[in,out]
    precond     magma_d_preconditioner*
                preconditioner parameters
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_dgepr
    ********************************************************************/

extern "C" magma_int_t
magma_dcumilusetup(
    magma_d_matrix A,
    magma_d_preconditioner *precond,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    
    cusparseHandle_t cusparseHandle=NULL;
    cusparseMatDescr_t descrA=NULL;
    cusparseMatDescr_t descrL=NULL;
    cusparseMatDescr_t descrU=NULL;
#if CUDA_VERSION >= 7000
    csrilu02Info_t info_M=NULL;
    void *pBuffer = NULL;
#endif
    
    //magma_dprint_matrix(A, queue );
    // copy matrix into preconditioner parameter
    magma_d_matrix hA={Magma_CSR}, hACSR={Magma_CSR};
    magma_d_matrix hL={Magma_CSR}, hU={Magma_CSR};
    CHECK( magma_dmtransfer( A, &hA, A.memory_location, Magma_CPU, queue ));
    CHECK( magma_dmconvert( hA, &hACSR, hA.storage_type, Magma_CSR, queue ));

        // in case using fill-in
    if( precond->levels > 0 ){
        magma_d_matrix hAL={Magma_CSR}, hAUt={Magma_CSR};
        CHECK( magma_dsymbilu( &hACSR, precond->levels, &hAL, &hAUt,  queue ));
        magma_dmfree(&hAL, queue);
        magma_dmfree(&hAUt, queue);
    }

    CHECK( magma_dmtransfer(hACSR, &(precond->M), Magma_CPU, Magma_DEV, queue ));

    magma_dmfree( &hA, queue );
    magma_dmfree( &hACSR, queue );

    // CUSPARSE context //
    CHECK_CUSPARSE( cusparseCreate( &cusparseHandle ));
    CHECK_CUSPARSE( cusparseSetStream( cusparseHandle, queue ));
    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrA ));
    CHECK_CUSPARSE( cusparseSetMatType( descrA, CUSPARSE_MATRIX_TYPE_GENERAL ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrA, CUSPARSE_DIAG_TYPE_NON_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrA, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseCreateSolveAnalysisInfo( &(precond->cuinfo) ));
    // use kernel to manually check for zeros n the diagonal
    CHECK( magma_ddiagcheck( precond->M, queue ) );
    
#if CUDA_VERSION >= 7000
    // this version has the bug fixed where a zero on the diagonal causes a crash
    CHECK_CUSPARSE( cusparseCreateCsrilu02Info(&info_M) );
    int buffersize;
    int structural_zero;
    int numerical_zero;
    
    CHECK_CUSPARSE(
    cusparseDcsrilu02_bufferSize( cusparseHandle,
                         precond->M.num_rows, precond->M.nnz, descrA,
                         precond->M.dval, precond->M.drow, precond->M.dcol,
                         info_M,
                         &buffersize ) );
    
    CHECK( magma_malloc((void**)&pBuffer, buffersize) );

    CHECK_CUSPARSE( cusparseDcsrilu02_analysis( cusparseHandle,
            precond->M.num_rows, precond->M.nnz, descrA,
            precond->M.dval, precond->M.drow, precond->M.dcol,
            info_M, CUSPARSE_SOLVE_POLICY_NO_LEVEL, pBuffer ));
    
    CHECK_CUSPARSE( cusparseXcsrilu02_zeroPivot( cusparseHandle, info_M, &numerical_zero ) );
    CHECK_CUSPARSE( cusparseXcsrilu02_zeroPivot( cusparseHandle, info_M, &structural_zero ) );
    
    CHECK_CUSPARSE(
    cusparseDcsrilu02( cusparseHandle,
                         precond->M.num_rows, precond->M.nnz, descrA,
                         precond->M.dval, precond->M.drow, precond->M.dcol,
                         info_M, CUSPARSE_SOLVE_POLICY_NO_LEVEL, pBuffer) );
#else
    // this version contains the bug but is needed for backward compability
    CHECK_CUSPARSE( cusparseDcsrsm_analysis( cusparseHandle,
                CUSPARSE_OPERATION_NON_TRANSPOSE,
                precond->M.num_rows, precond->M.nnz, descrA,
                precond->M.dval, precond->M.drow, precond->M.dcol,
                precond->cuinfo ));
    CHECK_CUSPARSE( cusparseDcsrilu0( cusparseHandle, CUSPARSE_OPERATION_NON_TRANSPOSE,
                      precond->M.num_rows, descrA,
                      precond->M.dval,
                      precond->M.drow,
                      precond->M.dcol,
                      precond->cuinfo ));
#endif

    CHECK( magma_dmtransfer( precond->M, &hA, Magma_DEV, Magma_CPU, queue ));

    hL.diagorder_type = Magma_UNITY;
    CHECK( magma_dmconvert( hA, &hL , Magma_CSR, Magma_CSRL, queue ));
    hU.diagorder_type = Magma_VALUE;
    CHECK( magma_dmconvert( hA, &hU , Magma_CSR, Magma_CSRU, queue ));
    CHECK( magma_dmtransfer( hL, &(precond->L), Magma_CPU, Magma_DEV, queue ));
    CHECK( magma_dmtransfer( hU, &(precond->U), Magma_CPU, Magma_DEV, queue ));


    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrL ));
    CHECK_CUSPARSE( cusparseSetMatType( descrL, CUSPARSE_MATRIX_TYPE_TRIANGULAR ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrL, CUSPARSE_DIAG_TYPE_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrL, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseSetMatFillMode( descrL, CUSPARSE_FILL_MODE_LOWER ));
    CHECK_CUSPARSE( cusparseCreateSolveAnalysisInfo( &precond->cuinfoL ));
    CHECK_CUSPARSE( cusparseDcsrsm_analysis( cusparseHandle,
        CUSPARSE_OPERATION_NON_TRANSPOSE, precond->L.num_rows,
        precond->L.nnz, descrL,
        precond->L.dval, precond->L.drow, precond->L.dcol, precond->cuinfoL ));

    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrU ));
    CHECK_CUSPARSE( cusparseSetMatType( descrU, CUSPARSE_MATRIX_TYPE_TRIANGULAR ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrU, CUSPARSE_DIAG_TYPE_NON_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrU, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseSetMatFillMode( descrU, CUSPARSE_FILL_MODE_UPPER ));
    CHECK_CUSPARSE( cusparseCreateSolveAnalysisInfo( &precond->cuinfoU ));
    CHECK_CUSPARSE( cusparseDcsrsm_analysis( cusparseHandle,
        CUSPARSE_OPERATION_NON_TRANSPOSE, precond->U.num_rows,
        precond->U.nnz, descrU,
        precond->U.dval, precond->U.drow, precond->U.dcol, precond->cuinfoU ));


    if( precond->maxiter < 50 ){
        //prepare for iterative solves
        
        // extract the diagonal of L into precond->d
        CHECK( magma_djacobisetup_diagscal( precond->L, &precond->d, queue ));
        CHECK( magma_dvinit( &precond->work1, Magma_DEV, hA.num_rows, 1, MAGMA_D_ZERO, queue ));
        
        // extract the diagonal of U into precond->d2
        CHECK( magma_djacobisetup_diagscal( precond->U, &precond->d2, queue ));
        CHECK( magma_dvinit( &precond->work2, Magma_DEV, hA.num_rows, 1, MAGMA_D_ZERO, queue ));
    }

    
cleanup:
#if CUDA_VERSION >= 7000
    magma_free( pBuffer );
    cusparseDestroyCsrilu02Info( info_M );
#endif
    cusparseDestroySolveAnalysisInfo( precond->cuinfo );
    cusparseDestroyMatDescr( descrA );
    cusparseDestroyMatDescr( descrL );
    cusparseDestroyMatDescr( descrU );
    cusparseDestroy( cusparseHandle );
    magma_dmfree( &hA, queue );
    magma_dmfree( &hACSR, queue );
    magma_dmfree(&hA, queue );
    magma_dmfree(&hL, queue );
    magma_dmfree(&hU, queue );

    return info;
}



/**
    Purpose
    -------

    Prepares the ILU triangular solves via cuSPARSE using an ILU factorization
    matrix stored either in precond->M or on the device as
    precond->L and precond->U.

    Arguments
    ---------

    @param[in,out]
    precond     magma_d_preconditioner*
                preconditioner parameters
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_dgepr
    ********************************************************************/

extern "C" magma_int_t
magma_dcumilugeneratesolverinfo(
    magma_d_preconditioner *precond,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    
    cusparseHandle_t cusparseHandle=NULL;
    cusparseMatDescr_t descrL=NULL;
    cusparseMatDescr_t descrU=NULL;
    
    magma_d_matrix hA={Magma_CSR}, hL={Magma_CSR}, hU={Magma_CSR};
    
    if (precond->L.memory_location != Magma_DEV ){
        CHECK( magma_dmtransfer( precond->M, &hA,
        precond->M.memory_location, Magma_CPU, queue ));

        hL.diagorder_type = Magma_UNITY;
        CHECK( magma_dmconvert( hA, &hL , Magma_CSR, Magma_CSRL, queue ));
        hU.diagorder_type = Magma_VALUE;
        CHECK( magma_dmconvert( hA, &hU , Magma_CSR, Magma_CSRU, queue ));
        CHECK( magma_dmtransfer( hL, &(precond->L), Magma_CPU, Magma_DEV, queue ));
        CHECK( magma_dmtransfer( hU, &(precond->U), Magma_CPU, Magma_DEV, queue ));
        
        magma_dmfree(&hA, queue );
        magma_dmfree(&hL, queue );
        magma_dmfree(&hU, queue );
    }
    
    // CUSPARSE context //
    CHECK_CUSPARSE( cusparseCreate( &cusparseHandle ));
    CHECK_CUSPARSE( cusparseSetStream( cusparseHandle, queue ));


    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrL ));
    CHECK_CUSPARSE( cusparseSetMatType( descrL, CUSPARSE_MATRIX_TYPE_TRIANGULAR ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrL, CUSPARSE_DIAG_TYPE_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrL, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseSetMatFillMode( descrL, CUSPARSE_FILL_MODE_LOWER ));
    CHECK_CUSPARSE( cusparseCreateSolveAnalysisInfo( &precond->cuinfoL ));
    CHECK_CUSPARSE( cusparseDcsrsm_analysis( cusparseHandle,
        CUSPARSE_OPERATION_NON_TRANSPOSE, precond->L.num_rows,
        precond->L.nnz, descrL,
        precond->L.dval, precond->L.drow, precond->L.dcol, precond->cuinfoL ));


    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrU ));
    CHECK_CUSPARSE( cusparseSetMatType( descrU, CUSPARSE_MATRIX_TYPE_TRIANGULAR ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrU, CUSPARSE_DIAG_TYPE_NON_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrU, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseSetMatFillMode( descrU, CUSPARSE_FILL_MODE_UPPER ));
    CHECK_CUSPARSE( cusparseCreateSolveAnalysisInfo( &precond->cuinfoU ));
    CHECK_CUSPARSE( cusparseDcsrsm_analysis( cusparseHandle,
        CUSPARSE_OPERATION_NON_TRANSPOSE, precond->U.num_rows,
        precond->U.nnz, descrU,
        precond->U.dval, precond->U.drow, precond->U.dcol, precond->cuinfoU ));

    
    if( precond->maxiter < 50 ){
        //prepare for iterative solves

        // extract the diagonal of L into precond->d
        CHECK( magma_djacobisetup_diagscal( precond->L, &precond->d, queue ));
        CHECK( magma_dvinit( &precond->work1, Magma_DEV, precond->U.num_rows, 1, MAGMA_D_ZERO, queue ));
        
        // extract the diagonal of U into precond->d2
        CHECK( magma_djacobisetup_diagscal( precond->U, &precond->d2, queue ));
        CHECK( magma_dvinit( &precond->work2, Magma_DEV, precond->U.num_rows, 1, MAGMA_D_ZERO, queue ));
    }
    
cleanup:
    cusparseDestroyMatDescr( descrL );
    cusparseDestroyMatDescr( descrU );
    cusparseDestroy( cusparseHandle );
     
    return info;
}








/**
    Purpose
    -------

    Performs the left triangular solves using the ILU preconditioner.

    Arguments
    ---------

    @param[in]
    b           magma_d_matrix
                RHS

    @param[in,out]
    x           magma_d_matrix*
                vector to precondition

    @param[in,out]
    precond     magma_d_preconditioner*
                preconditioner parameters
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_dgepr
    ********************************************************************/

extern "C" magma_int_t
magma_dapplycumilu_l(
    magma_d_matrix b,
    magma_d_matrix *x,
    magma_d_preconditioner *precond,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    
    cusparseHandle_t cusparseHandle=NULL;
    cusparseMatDescr_t descrL=NULL;
    
    double one = MAGMA_D_MAKE( 1.0, 0.0);

    // CUSPARSE context //
    CHECK_CUSPARSE( cusparseCreate( &cusparseHandle ));
    CHECK_CUSPARSE( cusparseSetStream( cusparseHandle, queue ));
    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrL ));
    CHECK_CUSPARSE( cusparseSetMatType( descrL, CUSPARSE_MATRIX_TYPE_TRIANGULAR ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrL, CUSPARSE_DIAG_TYPE_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrL, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseSetMatFillMode( descrL, CUSPARSE_FILL_MODE_LOWER ));
    CHECK_CUSPARSE( cusparseDcsrsm_solve( cusparseHandle,
                            CUSPARSE_OPERATION_NON_TRANSPOSE,
                            precond->L.num_rows,
                            b.num_rows*b.num_cols/precond->L.num_rows,
                            &one,
                            descrL,
                            precond->L.dval,
                            precond->L.drow,
                            precond->L.dcol,
                            precond->cuinfoL,
                            b.dval,
                            precond->L.num_rows,
                            x->dval,
                            precond->L.num_rows ));
    
    magma_device_sync();

cleanup:
    cusparseDestroyMatDescr( descrL );
    cusparseDestroy( cusparseHandle );
    return info;
}


/**
    Purpose
    -------

    Performs the right triangular solves using the ILU preconditioner.

    Arguments
    ---------

    @param[in]
    b           magma_d_matrix
                RHS

    @param[in,out]
    x           magma_d_matrix*
                vector to precondition

    @param[in,out]
    precond     magma_d_preconditioner*
                preconditioner parameters
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_dgepr
    ********************************************************************/

extern "C" magma_int_t
magma_dapplycumilu_r(
    magma_d_matrix b,
    magma_d_matrix *x,
    magma_d_preconditioner *precond,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    
    cusparseHandle_t cusparseHandle=NULL;
    cusparseMatDescr_t descrU=NULL;
    
    double one = MAGMA_D_MAKE( 1.0, 0.0);

    // CUSPARSE context //
    CHECK_CUSPARSE( cusparseCreate( &cusparseHandle ));
    CHECK_CUSPARSE( cusparseSetStream( cusparseHandle, queue ));
    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrU ));
    CHECK_CUSPARSE( cusparseSetMatType( descrU, CUSPARSE_MATRIX_TYPE_TRIANGULAR ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrU, CUSPARSE_DIAG_TYPE_NON_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrU, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseSetMatFillMode( descrU, CUSPARSE_FILL_MODE_UPPER ));
    CHECK_CUSPARSE( cusparseDcsrsm_solve( cusparseHandle,
                            CUSPARSE_OPERATION_NON_TRANSPOSE,
                            precond->U.num_rows,
                            b.num_rows*b.num_cols/precond->U.num_rows,
                            &one,
                            descrU,
                            precond->U.dval,
                            precond->U.drow,
                            precond->U.dcol,
                            precond->cuinfoU,
                            b.dval,
                            precond->U.num_rows,
                            x->dval,
                            precond->U.num_rows ));
    
    magma_device_sync();

cleanup:
    cusparseDestroyMatDescr( descrU );
    cusparseDestroy( cusparseHandle );
    return info; 
}




/**
    Purpose
    -------

    Prepares the IC preconditioner via cuSPARSE.

    Arguments
    ---------

    @param[in]
    A           magma_d_matrix
                input matrix A

    @param[in,out]
    precond     magma_d_preconditioner*
                preconditioner parameters
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_dhepr
    ********************************************************************/

extern "C" magma_int_t
magma_dcumiccsetup(
    magma_d_matrix A,
    magma_d_preconditioner *precond,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    
    cusparseHandle_t cusparseHandle=NULL;
    cusparseMatDescr_t descrA=NULL;
    cusparseMatDescr_t descrL=NULL;
    cusparseMatDescr_t descrU=NULL;
#if CUDA_VERSION >= 7000
    csric02Info_t info_M=NULL;
    void *pBuffer = NULL;
#endif
    
    magma_d_matrix hA={Magma_CSR}, hACSR={Magma_CSR}, U={Magma_CSR};
    CHECK( magma_dmtransfer( A, &hA, A.memory_location, Magma_CPU, queue ));
    U.diagorder_type = Magma_VALUE;
    CHECK( magma_dmconvert( hA, &hACSR, hA.storage_type, Magma_CSR, queue ));

    // in case using fill-in
    if( precond->levels > 0 ){
            magma_d_matrix hAL={Magma_CSR}, hAUt={Magma_CSR};
            CHECK( magma_dsymbilu( &hACSR, precond->levels, &hAL, &hAUt,  queue ));
            magma_dmfree(&hAL, queue);
            magma_dmfree(&hAUt, queue);
    }

    CHECK( magma_dmconvert( hACSR, &U, Magma_CSR, Magma_CSRL, queue ));
    magma_dmfree( &hACSR, queue );
    CHECK( magma_dmtransfer(U, &(precond->M), Magma_CPU, Magma_DEV, queue ));

    // CUSPARSE context //
    CHECK_CUSPARSE( cusparseCreate( &cusparseHandle ));
    CHECK_CUSPARSE( cusparseSetStream( cusparseHandle, queue ));
    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrA ));
    CHECK_CUSPARSE( cusparseCreateSolveAnalysisInfo( &(precond->cuinfo) ));
    // use kernel to manually check for zeros n the diagonal
    CHECK( magma_ddiagcheck( precond->M, queue ) );
        
#if CUDA_VERSION >= 7000
    // this version has the bug fixed where a zero on the diagonal causes a crash
    CHECK_CUSPARSE( cusparseCreateCsric02Info(&info_M) );
    CHECK_CUSPARSE( cusparseSetMatType( descrA, CUSPARSE_MATRIX_TYPE_GENERAL ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrA, CUSPARSE_INDEX_BASE_ZERO ));
    int buffersize;
    int structural_zero;
    int numerical_zero;
    
    CHECK_CUSPARSE(
    cusparseDcsric02_bufferSize( cusparseHandle,
                         precond->M.num_rows, precond->M.nnz, descrA,
                         precond->M.dval, precond->M.drow, precond->M.dcol,
                         info_M,
                         &buffersize ) );
    
    CHECK( magma_malloc((void**)&pBuffer, buffersize) );

    CHECK_CUSPARSE( cusparseDcsric02_analysis( cusparseHandle,
            precond->M.num_rows, precond->M.nnz, descrA,
            precond->M.dval, precond->M.drow, precond->M.dcol,
            info_M, CUSPARSE_SOLVE_POLICY_NO_LEVEL, pBuffer ));
    CHECK_CUSPARSE( cusparseXcsric02_zeroPivot( cusparseHandle, info_M, &numerical_zero ) );
    CHECK_CUSPARSE( cusparseXcsric02_zeroPivot( cusparseHandle, info_M, &structural_zero ) );

    CHECK_CUSPARSE(
    cusparseDcsric02( cusparseHandle,
                         precond->M.num_rows, precond->M.nnz, descrA,
                         precond->M.dval, precond->M.drow, precond->M.dcol,
                         info_M, CUSPARSE_SOLVE_POLICY_NO_LEVEL, pBuffer) );    

#else
    // this version contains the bug but is needed for backward compability
    CHECK_CUSPARSE( cusparseSetMatType( descrA, CUSPARSE_MATRIX_TYPE_SYMMETRIC ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrA, CUSPARSE_DIAG_TYPE_NON_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrA, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseSetMatFillMode( descrA, CUSPARSE_FILL_MODE_LOWER ));
    
    CHECK_CUSPARSE( cusparseDcsrsm_analysis( cusparseHandle,
                CUSPARSE_OPERATION_NON_TRANSPOSE,
                precond->M.num_rows, precond->M.nnz, descrA,
                precond->M.dval, precond->M.drow, precond->M.dcol,
                precond->cuinfo ));
    CHECK_CUSPARSE( cusparseDcsric0( cusparseHandle, CUSPARSE_OPERATION_NON_TRANSPOSE,
                      precond->M.num_rows, descrA,
                      precond->M.dval,
                      precond->M.drow,
                      precond->M.dcol,
                      precond->cuinfo ));
#endif

    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrL ));
    CHECK_CUSPARSE( cusparseSetMatType( descrL, CUSPARSE_MATRIX_TYPE_TRIANGULAR ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrL, CUSPARSE_DIAG_TYPE_NON_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrL, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseSetMatFillMode( descrL, CUSPARSE_FILL_MODE_LOWER ));
    CHECK_CUSPARSE( cusparseCreateSolveAnalysisInfo( &precond->cuinfoL ));
    CHECK_CUSPARSE( cusparseDcsrsm_analysis( cusparseHandle,
        CUSPARSE_OPERATION_NON_TRANSPOSE, precond->M.num_rows,
        precond->M.nnz, descrL,
        precond->M.dval, precond->M.drow, precond->M.dcol, precond->cuinfoL ));
    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrU ));
    CHECK_CUSPARSE( cusparseSetMatType( descrU, CUSPARSE_MATRIX_TYPE_TRIANGULAR ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrU, CUSPARSE_DIAG_TYPE_NON_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrU, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseSetMatFillMode( descrU, CUSPARSE_FILL_MODE_LOWER ));
    CHECK_CUSPARSE( cusparseCreateSolveAnalysisInfo( &precond->cuinfoU ));
    CHECK_CUSPARSE( cusparseDcsrsm_analysis( cusparseHandle,
        CUSPARSE_OPERATION_TRANSPOSE, precond->M.num_rows,
        precond->M.nnz, descrU,
        precond->M.dval, precond->M.drow, precond->M.dcol, precond->cuinfoU ));

    if( precond->maxiter < 50 ){
        //prepare for iterative solves
        
        // copy the matrix to precond->L and (transposed) to precond->U
        CHECK( magma_dmtransfer(precond->M, &(precond->L), Magma_DEV, Magma_DEV, queue ));
        CHECK( magma_dmtranspose( precond->L, &(precond->U), queue ));
        
        // extract the diagonal of L into precond->d
        CHECK( magma_djacobisetup_diagscal( precond->L, &precond->d, queue ));
        CHECK( magma_dvinit( &precond->work1, Magma_DEV, hA.num_rows, 1, MAGMA_D_ZERO, queue ));
        
        // extract the diagonal of U into precond->d2
        CHECK( magma_djacobisetup_diagscal( precond->U, &precond->d2, queue ));
        CHECK( magma_dvinit( &precond->work2, Magma_DEV, hA.num_rows, 1, MAGMA_D_ZERO, queue ));
    }



/*
    // to enable also the block-asynchronous iteration for the triangular solves
    CHECK( magma_dmtransfer( precond->M, &hA, Magma_DEV, Magma_CPU, queue ));
    hA.storage_type = Magma_CSR;

    magma_d_matrix hD, hR, hAt

    CHECK( magma_dcsrsplit( 256, hA, &hD, &hR, queue ));

    CHECK( magma_dmtransfer( hD, &precond->LD, Magma_CPU, Magma_DEV, queue ));
    CHECK( magma_dmtransfer( hR, &precond->L, Magma_CPU, Magma_DEV, queue ));

    magma_dmfree(&hD, queue );
    magma_dmfree(&hR, queue );

    CHECK( magma_d_cucsrtranspose(   hA, &hAt, queue ));

    CHECK( magma_dcsrsplit( 256, hAt, &hD, &hR, queue ));

    CHECK( magma_dmtransfer( hD, &precond->UD, Magma_CPU, Magma_DEV, queue ));
    CHECK( magma_dmtransfer( hR, &precond->U, Magma_CPU, Magma_DEV, queue ));
    
    magma_dmfree(&hD, queue );
    magma_dmfree(&hR, queue );
    magma_dmfree(&hA, queue );
    magma_dmfree(&hAt, queue );
*/

cleanup:
#if CUDA_VERSION >= 7000
    magma_free( pBuffer );
    cusparseDestroyCsric02Info( info_M );
#endif
    cusparseDestroySolveAnalysisInfo( precond->cuinfo );
    cusparseDestroyMatDescr( descrL );
    cusparseDestroyMatDescr( descrU );
    cusparseDestroyMatDescr( descrA );
    cusparseDestroy( cusparseHandle );
    magma_dmfree(&U, queue );
    magma_dmfree(&hA, queue );

    return info;
}


/**
    Purpose
    -------

    Prepares the IC preconditioner solverinfo via cuSPARSE for a triangular
    matrix present on the device in precond->M.

    Arguments
    ---------
    
    @param[in,out]
    precond     magma_d_preconditioner*
                preconditioner parameters
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_dhepr
    ********************************************************************/

extern "C" magma_int_t
magma_dcumicgeneratesolverinfo(
    magma_d_preconditioner *precond,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    
    cusparseHandle_t cusparseHandle=NULL;
    cusparseMatDescr_t descrL=NULL;
    cusparseMatDescr_t descrU=NULL;
    
    // CUSPARSE context //
    CHECK_CUSPARSE( cusparseCreate( &cusparseHandle ));
    CHECK_CUSPARSE( cusparseSetStream( cusparseHandle, queue ));
    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrL ));
    CHECK_CUSPARSE( cusparseSetMatType( descrL, CUSPARSE_MATRIX_TYPE_TRIANGULAR ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrL, CUSPARSE_DIAG_TYPE_NON_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrL, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseSetMatFillMode( descrL, CUSPARSE_FILL_MODE_LOWER ));
    CHECK_CUSPARSE( cusparseCreateSolveAnalysisInfo( &precond->cuinfoL ));
    CHECK_CUSPARSE( cusparseDcsrsm_analysis( cusparseHandle,
        CUSPARSE_OPERATION_NON_TRANSPOSE, precond->M.num_rows,
        precond->M.nnz, descrL,
        precond->M.dval, precond->M.drow, precond->M.dcol, precond->cuinfoL ));
    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrU ));
    CHECK_CUSPARSE( cusparseSetMatType( descrU, CUSPARSE_MATRIX_TYPE_TRIANGULAR ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrU, CUSPARSE_DIAG_TYPE_NON_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrU, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseSetMatFillMode( descrU, CUSPARSE_FILL_MODE_LOWER ));
    CHECK_CUSPARSE( cusparseCreateSolveAnalysisInfo( &precond->cuinfoU ));
    CHECK_CUSPARSE( cusparseDcsrsm_analysis( cusparseHandle,
        CUSPARSE_OPERATION_TRANSPOSE, precond->M.num_rows,
        precond->M.nnz, descrU,
        precond->M.dval, precond->M.drow, precond->M.dcol, precond->cuinfoU ));


/*
    // to enable also the block-asynchronous iteration for the triangular solves
    CHECK( magma_dmtransfer( precond->M, &hA, Magma_DEV, Magma_CPU, queue ));
    hA.storage_type = Magma_CSR;

    CHECK( magma_dcsrsplit( 256, hA, &hD, &hR, queue ));

    CHECK( magma_dmtransfer( hD, &precond->LD, Magma_CPU, Magma_DEV, queue ));
    CHECK( magma_dmtransfer( hR, &precond->L, Magma_CPU, Magma_DEV, queue ));

    magma_dmfree(&hD, queue );
    magma_dmfree(&hR, queue );

    CHECK( magma_d_cucsrtranspose(   hA, &hAt, queue ));

    CHECK( magma_dcsrsplit( 256, hAt, &hD, &hR, queue ));

    CHECK( magma_dmtransfer( hD, &precond->UD, Magma_CPU, Magma_DEV, queue ));
    CHECK( magma_dmtransfer( hR, &precond->U, Magma_CPU, Magma_DEV, queue ));
    
    magma_dmfree(&hD, queue );
    magma_dmfree(&hR, queue );
    magma_dmfree(&hA, queue );
    magma_dmfree(&hAt, queue );
*/

cleanup:
    cusparseDestroyMatDescr( descrL );
    cusparseDestroyMatDescr( descrU );
    cusparseDestroy( cusparseHandle );
    return info;
}



/**
    Purpose
    -------

    Performs the left triangular solves using the ICC preconditioner.

    Arguments
    ---------

    @param[in]
    b           magma_d_matrix
                RHS

    @param[in,out]
    x           magma_d_matrix*
                vector to precondition

    @param[in,out]
    precond     magma_d_preconditioner*
                preconditioner parameters
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_dhepr
    ********************************************************************/

extern "C" magma_int_t
magma_dapplycumicc_l(
    magma_d_matrix b,
    magma_d_matrix *x,
    magma_d_preconditioner *precond,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    
    cusparseHandle_t cusparseHandle=NULL;
    cusparseMatDescr_t descrL=NULL;
    
    double one = MAGMA_D_MAKE( 1.0, 0.0);

    // CUSPARSE context //
    CHECK_CUSPARSE( cusparseCreate( &cusparseHandle ));
    CHECK_CUSPARSE( cusparseSetStream( cusparseHandle, queue ));
    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrL ));
    CHECK_CUSPARSE( cusparseSetMatType( descrL, CUSPARSE_MATRIX_TYPE_TRIANGULAR ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrL, CUSPARSE_DIAG_TYPE_NON_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatFillMode( descrL, CUSPARSE_FILL_MODE_LOWER ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrL, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseDcsrsm_solve( cusparseHandle,
                            CUSPARSE_OPERATION_NON_TRANSPOSE,
                            precond->M.num_rows,
                            b.num_rows*b.num_cols/precond->M.num_rows,
                            &one,
                            descrL,
                            precond->M.dval,
                            precond->M.drow,
                            precond->M.dcol,
                            precond->cuinfoL,
                            b.dval,
                            precond->M.num_rows,
                            x->dval,
                            precond->M.num_rows ));
    
    magma_device_sync();

cleanup:
    cusparseDestroyMatDescr( descrL );
    cusparseDestroy( cusparseHandle );
    return info; 
}




/**
    Purpose
    -------

    Performs the right triangular solves using the ICC preconditioner.

    Arguments
    ---------

    @param[in]
    b           magma_d_matrix
                RHS

    @param[in,out]
    x           magma_d_matrix*
                vector to precondition

    @param[in,out]
    precond     magma_d_preconditioner*
                preconditioner parameters
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_dhepr
    ********************************************************************/

extern "C" magma_int_t
magma_dapplycumicc_r(
    magma_d_matrix b,
    magma_d_matrix *x,
    magma_d_preconditioner *precond,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    
    cusparseHandle_t cusparseHandle=NULL;
    cusparseMatDescr_t descrU=NULL;
    
    double one = MAGMA_D_MAKE( 1.0, 0.0);

    // CUSPARSE context //
    CHECK_CUSPARSE( cusparseCreate( &cusparseHandle ));
    CHECK_CUSPARSE( cusparseSetStream( cusparseHandle, queue ));
    CHECK_CUSPARSE( cusparseCreateMatDescr( &descrU ));
    CHECK_CUSPARSE( cusparseSetMatType( descrU, CUSPARSE_MATRIX_TYPE_TRIANGULAR ));
    CHECK_CUSPARSE( cusparseSetMatDiagType( descrU, CUSPARSE_DIAG_TYPE_NON_UNIT ));
    CHECK_CUSPARSE( cusparseSetMatIndexBase( descrU, CUSPARSE_INDEX_BASE_ZERO ));
    CHECK_CUSPARSE( cusparseSetMatFillMode( descrU, CUSPARSE_FILL_MODE_LOWER ));
    CHECK_CUSPARSE( cusparseDcsrsm_solve( cusparseHandle,
                            CUSPARSE_OPERATION_TRANSPOSE,
                            precond->M.num_rows,
                            b.num_rows*b.num_cols/precond->M.num_rows,
                            &one,
                            descrU,
                            precond->M.dval,
                            precond->M.drow,
                            precond->M.dcol,
                            precond->cuinfoU,
                            b.dval,
                            precond->M.num_rows,
                            x->dval,
                            precond->M.num_rows ));
    
    magma_device_sync();

cleanup:
    cusparseDestroyMatDescr( descrU );
    cusparseDestroy( cusparseHandle );
    return info; 
}
