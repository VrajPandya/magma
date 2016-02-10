/*
    -- MAGMA (version 2.0.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date February 2016

       @generated from testing/testing_ztranspose.cpp normal z -> c, Tue Feb  9 16:06:04 2016
       @author Mark Gates

*/

// includes, system
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// includes, project
#include "magma.h"
#include "magma_lapack.h"
#include "magma_operators.h"  // conj
#include "testings.h"

#define COMPLEX

/* ////////////////////////////////////////////////////////////////////////////
   -- Testing ctranspose
   Code is very similar to testing_csymmetrize.cpp
*/
int main( int argc, char** argv)
{
    TESTING_INIT();

    // OpenCL use:  cl_mem  , offset  (two arguments);
    // else   use:  pointer + offset  (one argument).
    #ifdef HAVE_clBLAS
        #define d_A(i_, j_)   d_A, ((i_) + (j_)*ldda)
        #define d_B(i_, j_)   d_B, ((i_) + (j_)*lddb)
    #else
        #define d_A(i_, j_)  (d_A + (i_) + (j_)*ldda)
        #define d_B(i_, j_)  (d_B + (i_) + (j_)*lddb)
    #endif
    
    real_Double_t    gbytes, gpu_perf, gpu_time, gpu_perf2=0, gpu_time2=0, cpu_perf, cpu_time;
    float           error, error2, work[1];
    magmaFloatComplex  c_neg_one = MAGMA_C_NEG_ONE;
    magmaFloatComplex *h_A, *h_B, *h_R;
    magmaFloatComplex_ptr d_A, d_B;
    magma_int_t M, N, size, lda, ldda, ldb, lddb;
    magma_int_t ione     = 1;
    magma_int_t status = 0;
    
    magma_opts opts;
    opts.parse_opts( argc, argv );
    
    #ifdef COMPLEX
    magma_int_t ntrans = 2;
    magma_trans_t trans[] = { Magma_ConjTrans, MagmaTrans };
    #else
    magma_int_t ntrans = 1;
    magma_trans_t trans[] = { MagmaTrans };
    #endif

    printf("%% Inplace transpose requires M == N.\n");
    printf("%% Trans     M     N   CPU GByte/s (ms)    GPU GByte/s (ms)  check   Inplace GB/s (ms)  check\n");
    printf("%%=========================================================================================\n");
    for( int itest = 0; itest < opts.ntest; ++itest ) {
      for( int itran = 0; itran < ntrans; ++itran ) {
        for( int iter = 0; iter < opts.niter; ++iter ) {
            M = opts.msize[itest];
            N = opts.nsize[itest];
            lda    = M;
            ldda   = magma_roundup( M, opts.align );  // multiple of 32 by default
            ldb    = N;
            lddb   = magma_roundup( N, opts.align );  // multiple of 32 by default
            // load entire matrix, save entire matrix
            gbytes = sizeof(magmaFloatComplex) * 2.*M*N / 1e9;
            
            TESTING_MALLOC_CPU( h_A, magmaFloatComplex, lda*N  );  // input:  M x N
            TESTING_MALLOC_CPU( h_B, magmaFloatComplex, ldb*M  );  // output: N x M
            TESTING_MALLOC_CPU( h_R, magmaFloatComplex, ldb*M  );  // output: N x M
            
            TESTING_MALLOC_DEV( d_A, magmaFloatComplex, ldda*N );  // input:  M x N
            TESTING_MALLOC_DEV( d_B, magmaFloatComplex, lddb*M );  // output: N x M
            
            /* Initialize the matrix */
            for( int j = 0; j < N; ++j ) {
                for( int i = 0; i < M; ++i ) {
                    h_A[i + j*lda] = MAGMA_C_MAKE( i + j/10000., j );
                }
            }
            for( int j = 0; j < M; ++j ) {
                for( int i = 0; i < N; ++i ) {
                    h_B[i + j*ldb] = MAGMA_C_MAKE( i + j/10000., j );
                }
            }
            magma_csetmatrix( N, M, h_B, ldb, d_B(0,0), lddb );
            
            /* =====================================================================
               Performs operation using naive out-of-place algorithm
               (LAPACK doesn't implement transpose)
               =================================================================== */
            cpu_time = magma_wtime();
            //for( int j = 1; j < N-1; ++j ) {      // inset by 1 row & col
            //    for( int i = 1; i < M-1; ++i ) {  // inset by 1 row & col
            if ( trans[itran] == MagmaTrans ) {
                for( int j = 0; j < N; ++j ) {
                    for( int i = 0; i < M; ++i ) {
                        h_B[j + i*ldb] = h_A[i + j*lda];
                    }
                }
            }
            else {
                for( int j = 0; j < N; ++j ) {
                    for( int i = 0; i < M; ++i ) {
                        h_B[j + i*ldb] = conj( h_A[i + j*lda] );
                    }
                }
            }
            cpu_time = magma_wtime() - cpu_time;
            cpu_perf = gbytes / cpu_time;
            
            /* ====================================================================
               Performs operation using MAGMA, out-of-place
               =================================================================== */
            magma_csetmatrix( M, N, h_A, lda, d_A(0,0), ldda );
            magma_csetmatrix( N, M, h_B, ldb, d_B(0,0), lddb );
            
            magmablasSetKernelStream( opts.queue );
            gpu_time = magma_sync_wtime( opts.queue );
            if ( trans[itran] == MagmaTrans ) {
                //magmablas_ctranspose( M-2, N-2, d_A(1,1), ldda, d_B(1,1), lddb );  // inset by 1 row & col
                magmablas_ctranspose( M, N, d_A(0,0), ldda, d_B(0,0), lddb );
            }
            #ifdef HAVE_CUBLAS
            else {
                //magmablas_ctranspose_conj( M-2, N-2, d_A(1,1), ldda, d_B(1,1), lddb );  // inset by 1 row & col
                magmablas_ctranspose_conj( M, N, d_A(0,0), ldda, d_B(0,0), lddb );
            }
            #endif
            gpu_time = magma_sync_wtime( opts.queue ) - gpu_time;
            gpu_perf = gbytes / gpu_time;
            
            /* ====================================================================
               Performs operation using MAGMA, in-place
               =================================================================== */
            if ( M == N ) {
                magma_csetmatrix( M, N, h_A, lda, d_A(0,0), ldda );
                
                gpu_time2 = magma_sync_wtime( opts.queue );
                if ( trans[itran] == MagmaTrans ) {
                    //magmablas_ctranspose_inplace( N-2, d_A(1,1), ldda );  // inset by 1 row & col
                    magmablas_ctranspose_inplace( N, d_A(0,0), ldda );
                }
                #ifdef HAVE_CUBLAS
                else {
                    //magmablas_ctranspose_conj_inplace( N-2, d_A(1,1), ldda );  // inset by 1 row & col
                    magmablas_ctranspose_conj_inplace( N, d_A(0,0), ldda );
                }
                #endif
                gpu_time2 = magma_sync_wtime( opts.queue ) - gpu_time2;
                gpu_perf2 = gbytes / gpu_time2;
            }
            
            /* =====================================================================
               Check the result
               =================================================================== */
            // check out-of-place transpose (d_B)
            size = ldb*M;
            magma_cgetmatrix( N, M, d_B(0,0), lddb, h_R, ldb );
            blasf77_caxpy( &size, &c_neg_one, h_B, &ione, h_R, &ione );
            error = lapackf77_clange("f", &N, &M, h_R, &ldb, work );
            
            if ( M == N ) {
                // also check in-place tranpose (d_A)
                magma_cgetmatrix( N, M, d_A(0,0), ldda, h_R, ldb );
                blasf77_caxpy( &size, &c_neg_one, h_B, &ione, h_R, &ione );
                error2 = lapackf77_clange("f", &N, &M, h_R, &ldb, work );
    
                printf("%5c %5d %5d   %7.2f (%7.2f)   %7.2f (%7.2f)  %6s  %7.2f (%7.2f)  %s\n",
                       lapacke_trans_const( trans[itran] ),
                       (int) M, (int) N,
                       cpu_perf, cpu_time*1000., gpu_perf, gpu_time*1000.,
                       (error  == 0. ? "ok" : "failed"),
                       gpu_perf2, gpu_time2,
                       (error2 == 0. ? "ok" : "failed") );
                status += ! (error == 0. && error2 == 0.);
            }
            else {
                printf("%5c %5d %5d   %7.2f (%7.2f)   %7.2f (%7.2f)  %6s    ---   (  ---  )\n",
                       lapacke_trans_const( trans[itran] ),
                       (int) M, (int) N,
                       cpu_perf, cpu_time*1000., gpu_perf, gpu_time*1000.,
                       (error  == 0. ? "ok" : "failed") );
                status += ! (error == 0.);
            }
            
            TESTING_FREE_CPU( h_A );
            TESTING_FREE_CPU( h_B );
            TESTING_FREE_CPU( h_R );
            
            TESTING_FREE_DEV( d_A );
            TESTING_FREE_DEV( d_B );
            fflush( stdout );
        }
        if ( opts.niter > 1 ) {
            printf( "\n" );
        }
      }
    }

    opts.cleanup();
    TESTING_FINALIZE();
    return status;
}
