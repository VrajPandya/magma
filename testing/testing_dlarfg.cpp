/*
    -- MAGMA (version 1.6.3-beta1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date August 2015

       @generated from testing_zlarfg.cpp normal z -> d, Tue Aug 25 16:35:25 2015
       @author Mark Gates
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "flops.h"
#include "magma.h"
#include "magma_lapack.h"
#include "testings.h"

#define PRECISION_d

int main( int argc, char** argv)
{
    TESTING_INIT();

    real_Double_t   gflops, gpu_perf, gpu_time, cpu_perf, cpu_time;
    double *h_x, *h_x2, *h_tau, *h_tau2;
    magmaDouble_ptr d_x, d_tau;
    double c_neg_one = MAGMA_D_NEG_ONE;
    double      error, error2, work[1];
    magma_int_t N, nb, lda, ldda, size;
    magma_int_t ione     = 1;
    magma_int_t ISEED[4] = {0,0,0,1};    magma_int_t status = 0;

    magma_opts opts;
    opts.parse_opts( argc, argv );

    double tol = opts.tolerance * lapackf77_dlamch("E");
    
    // does larfg on nb columns, one after another
    nb = (opts.nb > 0 ? opts.nb : 64);
    
    printf("%%   N    nb    CPU GFLop/s (ms)    GPU GFlop/s (ms)   error      tau error\n");
    printf("%%=========================================================================\n");
    for( int itest = 0; itest < opts.ntest; ++itest ) {
        for( int iter = 0; iter < opts.niter; ++iter ) {
            N = opts.nsize[itest];
            lda  = N;
            ldda = magma_roundup( N, opts.align );  // multiple of 32 by default
            gflops = FLOPS_DLARFG( N ) / 1e9 * nb;
    
            TESTING_MALLOC_CPU( h_x,    double, N*nb );
            TESTING_MALLOC_CPU( h_x2,   double, N*nb );
            TESTING_MALLOC_CPU( h_tau,  double, nb   );
            TESTING_MALLOC_CPU( h_tau2, double, nb   );
        
            TESTING_MALLOC_DEV( d_x,   double, ldda*nb );
            TESTING_MALLOC_DEV( d_tau, double, nb      );
            
            /* Initialize the vectors */
            size = N*nb;
            lapackf77_dlarnv( &ione, ISEED, &size, h_x );
            
            /* =====================================================================
               Performs operation using MAGMABLAS
               =================================================================== */
            magma_dsetmatrix( N, nb, h_x, N, d_x, ldda );
            
            magmablasSetKernelStream( opts.queue );
            gpu_time = magma_sync_wtime( opts.queue );
            for( int j = 0; j < nb; ++j ) {
                magmablas_dlarfg( N, &d_x[0+j*ldda], &d_x[1+j*ldda], ione, &d_tau[j] );
            }
            gpu_time = magma_sync_wtime( opts.queue ) - gpu_time;
            gpu_perf = gflops / gpu_time;
            
            magma_dgetmatrix( N, nb, d_x, ldda, h_x2, N );
            magma_dgetvector( nb, d_tau, 1, h_tau2, 1 );
            
            /* =====================================================================
               Performs operation using LAPACK
               =================================================================== */
            cpu_time = magma_wtime();
            for( int j = 0; j < nb; ++j ) {
                lapackf77_dlarfg( &N, &h_x[0+j*lda], &h_x[1+j*lda], &ione, &h_tau[j] );
            }
            cpu_time = magma_wtime() - cpu_time;
            cpu_perf = gflops / cpu_time;
            
            /* =====================================================================
               Error Computation and Performance Comparison
               =================================================================== */
            blasf77_daxpy( &size, &c_neg_one, h_x, &ione, h_x2, &ione );
            error = lapackf77_dlange( "F", &N, &nb, h_x2, &N, work )
                  / lapackf77_dlange( "F", &N, &nb, h_x,  &N, work );
            
            // tau can be 0
            blasf77_daxpy( &nb, &c_neg_one, h_tau, &ione, h_tau2, &ione );
            error2 = lapackf77_dlange( "F", &nb, &ione, h_tau,  &nb, work );
            if ( error2 != 0 ) {
                error2 = lapackf77_dlange( "F", &nb, &ione, h_tau2, &nb, work ) / error2;
            }

            printf("%5d %5d   %7.2f (%7.2f)   %7.2f (%7.2f)   %8.2e   %8.2e   %s\n",
                   (int) N, (int) nb, cpu_perf, 1000.*cpu_time, gpu_perf, 1000.*gpu_time,
                   error, error2,
                   (error < tol && error2 < tol ? "ok" : "failed") );
            status += ! (error < tol && error2 < tol);
            
            TESTING_FREE_CPU( h_x   );
            TESTING_FREE_CPU( h_x2  );
            TESTING_FREE_CPU( h_tau );
        
            TESTING_FREE_DEV( d_x   );
            TESTING_FREE_DEV( d_tau );
            fflush( stdout );
        }
        if ( opts.niter > 1 ) {
            printf( "\n" );
        }
    }

    TESTING_FINALIZE();
    return status;
}
