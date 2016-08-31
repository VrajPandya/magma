/*
    -- MAGMA (version 2.1.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date August 2016

       @generated from testing/testing_zgemv_batched.cpp, normal z -> d, Tue Aug 30 09:39:16 2016
       @author Mark Gates
       @author Azzam Haidar
       @author Tingxing Dong
*/

// includes, system
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// includes, project
#include "flops.h"
#include "magma_v2.h"
#include "magma_lapack.h"
#include "testings.h"

#if defined(_OPENMP)
#include <omp.h>
#include "../control/magma_threadsetting.h"  // internal header
#endif

/* ////////////////////////////////////////////////////////////////////////////
   -- Testing dgemm_batched
*/
int main( int argc, char** argv)
{
    TESTING_CHECK( magma_init() );
    magma_print_environment();

    real_Double_t   gflops, magma_perf, magma_time, cpu_perf, cpu_time;
    double          magma_error, work[1];
    magma_int_t M, N, Xm, Ym, lda, ldda;
    magma_int_t sizeA, sizeX, sizeY;
    magma_int_t incx = 1;
    magma_int_t incy = 1;
    magma_int_t ione     = 1;
    magma_int_t ISEED[4] = {0,0,0,1};
    int status = 0;
    magma_int_t batchCount;

    double *h_A, *h_X, *h_Y, *h_Ymagma;
    double *d_A, *d_X, *d_Y;
    double c_neg_one = MAGMA_D_NEG_ONE;
    double alpha = MAGMA_D_MAKE(  0.29, -0.86 );
    double beta  = MAGMA_D_MAKE( -0.48,  0.38 );
    double **A_array = NULL;
    double **X_array = NULL;
    double **Y_array = NULL;

    magma_opts opts( MagmaOptsBatched );
    opts.parse_opts( argc, argv );
    batchCount = opts.batchcount;
    opts.lapack |= opts.check;

    double tol = opts.tolerance * lapackf77_dlamch("E");

    printf("%% trans = %s\n", lapack_trans_const(opts.transA) );
    printf("%% BatchCount   M     N   MAGMA Gflop/s (ms)    CPU Gflop/s (ms)   MAGMA error\n");
    printf("%%============================================================================\n");
    for( int itest = 0; itest < opts.ntest; ++itest ) {
        for( int iter = 0; iter < opts.niter; ++iter ) {
            M = opts.msize[itest];
            N = opts.nsize[itest];
            lda    = M;
            ldda   = magma_roundup( M, opts.align );  // multiple of 32 by default
            gflops = FLOPS_DGEMV( M, N ) / 1e9 * batchCount;

            if ( opts.transA == MagmaNoTrans ) {
                Xm = N;
                Ym = M;
            } else {
                Xm = M;
                Ym = N;
            }

            sizeA = lda*N*batchCount;
            sizeX = incx*Xm*batchCount;
            sizeY = incy*Ym*batchCount;

            TESTING_CHECK( magma_dmalloc_cpu( &h_A,  sizeA ));
            TESTING_CHECK( magma_dmalloc_cpu( &h_X,  sizeX ));
            TESTING_CHECK( magma_dmalloc_cpu( &h_Y,  sizeY  ));
            TESTING_CHECK( magma_dmalloc_cpu( &h_Ymagma,  sizeY  ));

            TESTING_CHECK( magma_dmalloc( &d_A, ldda*N*batchCount ));
            TESTING_CHECK( magma_dmalloc( &d_X, sizeX ));
            TESTING_CHECK( magma_dmalloc( &d_Y, sizeY ));

            TESTING_CHECK( magma_malloc( (void**) &A_array, batchCount * sizeof(double*) ));
            TESTING_CHECK( magma_malloc( (void**) &X_array, batchCount * sizeof(double*) ));
            TESTING_CHECK( magma_malloc( (void**) &Y_array, batchCount * sizeof(double*) ));

            /* Initialize the matrices */
            lapackf77_dlarnv( &ione, ISEED, &sizeA, h_A );
            lapackf77_dlarnv( &ione, ISEED, &sizeX, h_X );
            lapackf77_dlarnv( &ione, ISEED, &sizeY, h_Y );
            
            /* =====================================================================
               Performs operation using MAGMABLAS
               =================================================================== */
            magma_dsetmatrix( M, N*batchCount, h_A, lda, d_A, ldda, opts.queue );
            magma_dsetvector( Xm*batchCount, h_X, incx, d_X, incx, opts.queue );
            magma_dsetvector( Ym*batchCount, h_Y, incy, d_Y, incy, opts.queue );
            
            magma_dset_pointer( A_array, d_A, ldda, 0, 0, ldda*N, batchCount, opts.queue );
            magma_dset_pointer( X_array, d_X, 1, 0, 0, incx*Xm, batchCount, opts.queue );
            magma_dset_pointer( Y_array, d_Y, 1, 0, 0, incy*Ym, batchCount, opts.queue );

            magma_time = magma_sync_wtime( opts.queue );
            magmablas_dgemv_batched(opts.transA, M, N,
                             alpha, A_array, ldda,
                                    X_array, incx,
                             beta,  Y_array, incy, batchCount, opts.queue);
            magma_time = magma_sync_wtime( opts.queue ) - magma_time;
            magma_perf = gflops / magma_time;
            magma_dgetvector( Ym*batchCount, d_Y, incy, h_Ymagma, incy, opts.queue );
            
            /* =====================================================================
               Performs operation using CPU BLAS
               =================================================================== */
            if ( opts.lapack ) {
                cpu_time = magma_wtime();
                #if !defined (BATCHED_DISABLE_PARCPU) && defined(_OPENMP)
                magma_int_t nthreads = magma_get_lapack_numthreads();
                magma_set_lapack_numthreads(1);
                magma_set_omp_numthreads(nthreads);
                #pragma omp parallel for schedule(dynamic)
                #endif
                for (int i=0; i < batchCount; i++)
                {
                   blasf77_dgemv(
                               lapack_trans_const(opts.transA),
                               &M, &N,
                               &alpha, h_A + i*lda*N, &lda,
                                       h_X + i*Xm, &incx,
                               &beta,  h_Y + i*Ym, &incy );
                }
                #if !defined (BATCHED_DISABLE_PARCPU) && defined(_OPENMP)
                    magma_set_lapack_numthreads(nthreads);
                #endif
                cpu_time = magma_wtime() - cpu_time;
                cpu_perf = gflops / cpu_time;
            }
            
            /* =====================================================================
               Check the result
               =================================================================== */
            if ( opts.lapack ) {
                // compute relative error for magma, relative to lapack,
                // |C_magma - C_lapack| / |C_lapack|
                magma_error = 0;
                for (int s=0; s < batchCount; s++) {
                    double Anorm = lapackf77_dlange( "F", &M, &N, h_A + s * lda * N, &lda, work );
                    double Xnorm = lapackf77_dlange( "F", &Xm, &ione, h_X + s * Xm, &Xm, work );
                    
                    blasf77_daxpy( &Ym, &c_neg_one, h_Y + s * Ym, &incy, h_Ymagma + s * Ym, &incy );
                    double err = lapackf77_dlange( "F", &Ym, &ione, h_Ymagma + s * Ym, &Ym, work ) / (Anorm * Xnorm);

                    if ( isnan(err) || isinf(err) ) {
                      magma_error = err;
                      break;
                    }
                    magma_error = max( err, magma_error );
                }

                bool okay = (magma_error < tol);
                status += ! okay;
                printf("%10lld %5lld %5lld    %7.2f (%7.2f)   %7.2f (%7.2f)   %8.2e  %s\n",
                   (long long) batchCount, (long long) M, (long long) N,
                   magma_perf,  1000.*magma_time,
                   cpu_perf,    1000.*cpu_time,
                   magma_error, (okay ? "ok" : "failed"));
            }
            else {
                printf("%10lld %5lld %5lld    %7.2f (%7.2f)     ---   (  ---  )     ---\n",
                   (long long) batchCount, (long long) M, (long long) N,
                   magma_perf,  1000.*magma_time);
            }
            
            magma_free_cpu( h_A  );
            magma_free_cpu( h_X  );
            magma_free_cpu( h_Y  );
            magma_free_cpu( h_Ymagma  );

            magma_free( d_A );
            magma_free( d_X );
            magma_free( d_Y );
            magma_free( A_array );
            magma_free( X_array );
            magma_free( Y_array );

            fflush( stdout);
        }
        if ( opts.niter > 1 ) {
            printf( "\n" );
        }
    }

    opts.cleanup();
    TESTING_CHECK( magma_finalize() );
    return status;
}
