/*
    -- MAGMA (version 2.0.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date February 2016
 
       @generated from testing/testing_ztrmv.cpp normal z -> c, Tue Feb  9 16:06:01 2016
       @author Chongxiao Cao
*/
// includes, system
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// includes, project
#include "testings.h"  // before magma.h, to include cublas_v2
#include "flops.h"
#include "magma.h"
#include "magma_lapack.h"


/* ////////////////////////////////////////////////////////////////////////////
   -- Testing ctrmv
*/
int main( int argc, char** argv)
{
    TESTING_INIT();

    real_Double_t   gflops, cublas_perf, cublas_time, cpu_perf, cpu_time;
    float          cublas_error, Cnorm, work[1];
    magma_int_t N;
    magma_int_t Ak;
    magma_int_t sizeA;
    magma_int_t lda, ldda;
    magma_int_t ione     = 1;
    magma_int_t ISEED[4] = {0,0,0,1};
    
    magmaFloatComplex *h_A, *h_x, *h_xcublas;
    magmaFloatComplex_ptr d_A, d_x;
    magmaFloatComplex c_neg_one = MAGMA_C_NEG_ONE;
    magma_int_t status = 0;
    
    magma_opts opts;
    opts.parse_opts( argc, argv );
    opts.lapack |= opts.check;  // check (-c) implies lapack (-l)
    
    float tol = opts.tolerance * lapackf77_slamch("E");
    
    printf("%% If running lapack (option --lapack), CUBLAS error is computed\n"
           "%% relative to CPU BLAS result.\n\n");
    printf("%% uplo = %s, transA = %s, diag = %s \n",
           lapack_uplo_const(opts.uplo), lapack_trans_const(opts.transA),
           lapack_diag_const(opts.diag) );
    printf("%%   N   CUBLAS Gflop/s (ms)   CPU Gflop/s (ms)  CUBLAS error\n");
    printf("%%=================================================================\n");
    for( int itest = 0; itest < opts.ntest; ++itest ) {
        for( int iter = 0; iter < opts.niter; ++iter ) {
            N = opts.nsize[itest];
            gflops = FLOPS_CTRMM(opts.side, N, 1) / 1e9;

            lda = N;
            Ak = N;
            
            ldda = magma_roundup( lda, opts.align );  // multiple of 32 by default
            
            sizeA = lda*Ak;
            
            TESTING_MALLOC_CPU( h_A,       magmaFloatComplex, lda*Ak );
            TESTING_MALLOC_CPU( h_x,       magmaFloatComplex, N      );
            TESTING_MALLOC_CPU( h_xcublas, magmaFloatComplex, N      );
            
            TESTING_MALLOC_DEV( d_A, magmaFloatComplex, ldda*Ak );
            TESTING_MALLOC_DEV( d_x, magmaFloatComplex, N       );
            
            /* Initialize the matrices */
            lapackf77_clarnv( &ione, ISEED, &sizeA, h_A );
            lapackf77_clarnv( &ione, ISEED, &N, h_x );
            
            /* =====================================================================
               Performs operation using CUBLAS
               =================================================================== */
            magma_csetmatrix( Ak, Ak, h_A, lda, d_A, ldda );
            magma_csetvector( N, h_x, 1, d_x, 1 );
            
            magmablasSetKernelStream( opts.queue );  // opts.handle also uses opts.queue
            cublas_time = magma_sync_wtime( opts.queue );
            #ifdef HAVE_CUBLAS
                cublasCtrmv( opts.handle, cublas_uplo_const(opts.uplo), cublas_trans_const(opts.transA),
                             cublas_diag_const(opts.diag),
                             N,
                             d_A, ldda,
                             d_x, 1 );
            #else
                magma_ctrmv( opts.uplo, opts.transA, opts.diag,
                             N,
                             d_A, 0, ldda,
                             d_x, 0, 1, opts.queue );
            #endif
            cublas_time = magma_sync_wtime( opts.queue ) - cublas_time;
            cublas_perf = gflops / cublas_time;
            
            magma_cgetvector( N, d_x, 1, h_xcublas, 1 );
            
            /* =====================================================================
               Performs operation using CPU BLAS
               =================================================================== */
            if ( opts.lapack ) {
                cpu_time = magma_wtime();
                blasf77_ctrmv( lapack_uplo_const(opts.uplo), lapack_trans_const(opts.transA), lapack_diag_const(opts.diag),
                               &N,
                               h_A, &lda,
                               h_x, &ione );
                cpu_time = magma_wtime() - cpu_time;
                cpu_perf = gflops / cpu_time;
            }
            
            /* =====================================================================
               Check the result
               =================================================================== */
            if ( opts.lapack ) {
                // compute relative error for both magma & cublas, relative to lapack,
                // |C_magma - C_lapack| / |C_lapack|
                Cnorm = lapackf77_clange( "M", &N, &ione, h_x, &N, work );
                
                blasf77_caxpy( &N, &c_neg_one, h_x, &ione, h_xcublas, &ione );
                cublas_error = lapackf77_clange( "M", &N, &ione, h_xcublas, &N, work ) / Cnorm;
                
                printf("%5d   %7.2f (%7.2f)   %7.2f (%7.2f)    %8.2e   %s\n",
                       (int) N,
                       cublas_perf, 1000.*cublas_time,
                       cpu_perf,    1000.*cpu_time,
                       cublas_error, (cublas_error < tol ? "ok" : "failed"));
                status += ! (cublas_error < tol);
            }
            else {
                printf("%5d   %7.2f (%7.2f)    ---   (  ---  )    ---     ---\n",
                       (int) N,
                       cublas_perf, 1000.*cublas_time);
            }
            
            TESTING_FREE_CPU( h_A );
            TESTING_FREE_CPU( h_x );
            TESTING_FREE_CPU( h_xcublas );
            
            TESTING_FREE_DEV( d_A );
            TESTING_FREE_DEV( d_x );
            fflush( stdout );
        }
        if ( opts.niter > 1 ) {
            printf( "\n" );
        }
    }

    opts.cleanup();
    TESTING_FINALIZE();
    return status;
}
