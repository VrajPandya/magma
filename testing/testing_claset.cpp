/*
    -- MAGMA (version 1.7.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date September 2015

       @generated from testing_zlaset.cpp normal z -> c, Fri Sep 11 18:29:37 2015
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
#include "testings.h"
#include "magma_operators.h"

/* ////////////////////////////////////////////////////////////////////////////
   -- Testing claset
   Code is very similar to testing_clacpy.cpp
*/
int main( int argc, char** argv)
{
    TESTING_INIT();

    real_Double_t    gbytes, gpu_perf, gpu_time, cpu_perf, cpu_time;
    float           error, work[1];
    magmaFloatComplex  c_neg_one = MAGMA_C_NEG_ONE;
    magmaFloatComplex *h_A, *h_R;
    magmaFloatComplex_ptr d_A;
    magmaFloatComplex offdiag, diag;
    magma_int_t M, N, size, lda, ldda;
    magma_int_t ione     = 1;
    magma_int_t status = 0;
    
    magma_opts opts;
    opts.parse_opts( argc, argv );

    magma_uplo_t uplo[] = { MagmaLower, MagmaUpper, MagmaFull };

    printf("%% uplo    M     N    offdiag    diag    CPU GByte/s (ms)    GPU GByte/s (ms)   check\n");
    printf("%%===================================================================================\n");
    for( int iuplo = 0; iuplo < 3; ++iuplo ) {
      for( int itest = 0; itest < opts.ntest; ++itest ) {
        for( int iter = 0; iter < opts.niter; ++iter ) {
          for( int ival = 0; ival < 4; ++ival ) {
            // test combinations of zero & non-zero:
            // ival  offdiag  diag
            // 0     0        0
            // 1     0        3.14
            // 2     1.23     0
            // 3     1.23     3.14
            offdiag = MAGMA_C_MAKE( 1.2345, 6.7890 ) * (ival / 2);
            diag    = MAGMA_C_MAKE( 3.1415, 2.7183 ) * (ival % 2);
            
            M = opts.msize[itest];
            N = opts.nsize[itest];
            //M += 2;  // space for insets
            //N += 2;
            lda    = M;
            ldda   = magma_roundup( M, opts.align );
            size   = lda*N;
            if ( uplo[iuplo] == MagmaLower ) {
                // save lower trapezoid (with diagonal)
                if ( M > N ) {
                    gbytes = sizeof(magmaFloatComplex) * (1.*M*N - 0.5*N*(N-1)) / 1e9;
                } else {
                    gbytes = sizeof(magmaFloatComplex) * 0.5*M*(M+1) / 1e9;
                }
            }
            else if ( uplo[iuplo] == MagmaUpper ) {
                // save upper trapezoid (with diagonal)
                if ( N > M ) {
                    gbytes = sizeof(magmaFloatComplex) * (1.*M*N - 0.5*M*(M-1)) / 1e9;
                } else {
                    gbytes = sizeof(magmaFloatComplex) * 0.5*N*(N+1) / 1e9;
                }
            }
            else {
                // save entire matrix
                gbytes = sizeof(magmaFloatComplex) * 1.*M*N / 1e9;
            }
    
            TESTING_MALLOC_CPU( h_A, magmaFloatComplex, size   );
            TESTING_MALLOC_CPU( h_R, magmaFloatComplex, size   );
            
            TESTING_MALLOC_DEV( d_A, magmaFloatComplex, ldda*N );
            
            /* Initialize the matrix */
            for( int j = 0; j < N; ++j ) {
                for( int i = 0; i < M; ++i ) {
                    h_A[i + j*lda] = MAGMA_C_MAKE( i + j/10000., j );
                }
            }
            
            /* ====================================================================
               Performs operation using MAGMA
               =================================================================== */
            magma_csetmatrix( M, N, h_A, lda, d_A, ldda );
            
            magmablasSetKernelStream( opts.queue );
            gpu_time = magma_sync_wtime( opts.queue );
            //magmablas_claset( uplo[iuplo], M-2, N-2, offdiag, diag, d_A+1+ldda, ldda );  // inset by 1 row & col
            magmablas_claset( uplo[iuplo], M, N, offdiag, diag, d_A, ldda );
            gpu_time = magma_sync_wtime( opts.queue ) - gpu_time;
            gpu_perf = gbytes / gpu_time;
            
            /* =====================================================================
               Performs operation using LAPACK
               =================================================================== */
            cpu_time = magma_wtime();
            //magma_int_t M2 = M-2;  // inset by 1 row & col
            //magma_int_t N2 = N-2;
            //lapackf77_claset( lapack_uplo_const( uplo[iuplo] ), &M2, &N2, &offdiag, &diag, h_A+1+lda, &lda );
            lapackf77_claset( lapack_uplo_const( uplo[iuplo] ), &M, &N, &offdiag, &diag, h_A, &lda );
            cpu_time = magma_wtime() - cpu_time;
            cpu_perf = gbytes / cpu_time;
            
            if ( opts.verbose ) {
                printf( "A= " );  magma_cprint(     M, N, h_A, lda );
                printf( "dA=" );  magma_cprint_gpu( M, N, d_A, ldda );
            }
            
            /* =====================================================================
               Check the result
               =================================================================== */
            magma_cgetmatrix( M, N, d_A, ldda, h_R, lda );
            
            blasf77_caxpy(&size, &c_neg_one, h_A, &ione, h_R, &ione);
            error = lapackf77_clange("f", &M, &N, h_R, &lda, work);

            bool okay = (error == 0);
            status += ! okay;
            printf("%5s %5d %5d  %9.4f  %6.4f   %7.2f (%7.2f)   %7.2f (%7.2f)   %s\n",
                   lapack_uplo_const( uplo[iuplo] ), (int) M, (int) N,
                   real(offdiag), real(diag),
                   cpu_perf, cpu_time*1000., gpu_perf, gpu_time*1000.,
                   (okay ? "ok" : "failed") );
            
            TESTING_FREE_CPU( h_A );
            TESTING_FREE_CPU( h_R );
            
            TESTING_FREE_DEV( d_A );
            fflush( stdout );
          }
        }
        if ( opts.niter > 1 ) {
            printf( "\n" );
        }
      }
      printf( "\n" );
    }

    TESTING_FINALIZE();
    return status;
}
