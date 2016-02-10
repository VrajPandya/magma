/*
    -- MAGMA (version 2.0.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date February 2016

       @generated from testing/testing_zhemm_mgpu.cpp normal z -> d, Tue Feb  9 16:06:01 2016
       
       @author Mark Gates
       @author Azzam Haidar
*/

// includes, system
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

// includes, project
#include "flops.h"
#include "magma.h"
#include "magma_lapack.h"
#include "testings.h"

#include "trace.h"

/* ////////////////////////////////////////////////////////////////////////////
   -- Testing magma_dsymm_mgpu
*/
int main( int argc, char** argv)
{
    TESTING_INIT();

    double c_neg_one = MAGMA_D_NEG_ONE;
    double alpha     = MAGMA_D_MAKE( 3.456, 5.678 );
    double beta      = MAGMA_D_MAKE( 1.234, 2.456 );
    
    real_Double_t    gflops, gpu_perf=0., cpu_perf=0., gpu_time=0., cpu_time=0.;
    real_Double_t    gpu_perf2=0., gpu_time2=0.;
    double           Anorm, error, work[1];
    double *hA, *hB, *hC, *hR;
    magmaDouble_ptr dA[MagmaMaxGPUs], dB[MagmaMaxGPUs], dC[MagmaMaxGPUs], dwork[MagmaMaxGPUs];
    magmaDouble_ptr dA2;
    magma_int_t i, j, d, M, N, size, lda, ldb, ldc, ldda, lddb, lddc, msize, nb;
    magma_int_t ione     = 1;
    magma_int_t iseed[4] = {0,0,0,1};
    magma_int_t status = 0;
    
    magma_opts opts;
    opts.parse_opts( argc, argv );
    opts.ngpu = abs( opts.ngpu );  // always uses multi-GPU code
    
    double tol = opts.tolerance * lapackf77_dlamch("E");
    
    // default values
    nb = (opts.nb > 0 ? opts.nb : 64);
    
    magma_int_t gnode[MagmaMaxGPUs][MagmaMaxGPUs+2];
    magma_int_t ncmplx = 0;
    magma_buildconnection_mgpu( gnode, &ncmplx, opts.ngpu );
    
    printf("%% Initializing communication pattern... GPU-ncmplx %d\n", (int) ncmplx);
    for (i=0; i < ncmplx; ++i) {
        magma_int_t myngpu = gnode[i][MagmaMaxGPUs];
        printf("%% cmplx %d has %d GPUs:", i, myngpu);
        for (j=0; j < myngpu; ++j) {
            printf(" %d", (int) gnode[i][j]);
            if (j < myngpu-1) {
                printf(",");
            }
        }
        printf("\n");
    }

    // number of queues per GPU. Requires ngpu.
    magma_int_t nqueue  = opts.ngpu;
    // number of events per GPU. Require ngpu*ngpu.
    magma_int_t nevents = opts.ngpu*opts.ngpu;
    magma_queue_t queues[MagmaMaxGPUs][20];
    magma_event_t events[MagmaMaxGPUs][MagmaMaxGPUs*MagmaMaxGPUs + 10];
    for( d = 0; d < opts.ngpu; ++d ) {
        magma_setdevice( d );
        for( i = 0; i < nqueue; ++i ) {
            magma_queue_create( &queues[d][i] );
        }
        for( i = 0; i < nevents; ++i ) {
            cudaEventCreateWithFlags( &events[d][i], cudaEventDisableTiming );
        }
    }

    printf("%% nb %d, ngpu %d, version %d\n", (int) nb, (int) opts.ngpu, (int) opts.version );
    printf("%%   M     N    nb offset  CPU Gflop/s (sec)   GPU Gflop/s (sec)   CUBLAS hemm (sec)   ||R|| / ||A||*||B||\n");
    printf("%%========================================================================================================\n");
    for( int itest = 0; itest < opts.ntest; ++itest ) {
      M = opts.msize[itest];
      N = opts.nsize[itest];
      for( int offset = 0; offset < N; offset += min(N,nb) ) {
        for( int iter = 0; iter < opts.niter; ++iter ) {
            msize = M - offset;
            lda   = M;  // TODO depends on side
            ldb   = M;
            ldc   = M;
            ldda  = magma_roundup( lda, opts.align );  // multiple of 32 by default
            lddb  = magma_roundup( ldb, opts.align );  // multiple of 32 by default
            lddc  = magma_roundup( ldc, opts.align );  // multiple of 32 by default
            gflops = FLOPS_DSYMM( MagmaLeft, (double)msize, (double)N ) / 1e9;
            
            magma_int_t dworksiz = lddc*N + (M*N)*opts.ngpu;
            
            TESTING_MALLOC_CPU( hA, double, lda*M );
            TESTING_MALLOC_CPU( hB, double, ldb*N );
            TESTING_MALLOC_CPU( hC, double, ldc*N );
            
            TESTING_MALLOC_PIN( hR, double, ldc*N );

            for( d = 0; d < opts.ngpu; ++d ) {
                magma_int_t mlocal = ((M / nb) / opts.ngpu + 1) * nb;
                magma_setdevice( d );
                TESTING_MALLOC_DEV( dA[d],    double, ldda*mlocal );
                TESTING_MALLOC_DEV( dB[d],    double, lddb*N      );
                TESTING_MALLOC_DEV( dC[d],    double, lddc*N      );
                TESTING_MALLOC_DEV( dwork[d], double, dworksiz    );
            }
            
            if ( opts.check ) {
                magma_setdevice( 0 );
                TESTING_MALLOC_DEV( dA2, double, ldda*M );
            }

            size = lda*M;
            lapackf77_dlarnv( &ione, iseed, &size, hA );
            magma_dmake_symmetric( M, hA, lda );
            
            size = ldb*N;
            lapackf77_dlarnv( &ione, iseed, &size, hB );
            size = ldc*N;
            lapackf77_dlarnv( &ione, iseed, &size, hC );
            lapackf77_dlacpy( "Full", &M, &N, hC, &ldc, hR, &lda );
            
            /* ====================================================================
               Performs operation using MAGMA
               =================================================================== */
            magma_dsetmatrix_1D_col_bcyclic( M, M, hA, lda, dA, ldda, opts.ngpu, nb );
            for( d = 0; d < opts.ngpu; ++d ) {
                magma_setdevice( d );
                magma_dsetmatrix( M, N, hB, lda, dB[d], ldda );
                // since when offset != 0, the GPU that does beta*C may not be 0,
                // send initial hC to all GPUs.
                magma_dsetmatrix( M, N, hC, lda, dC[d], ldda );
            }
            
            trace_init( 1, opts.ngpu, nqueue, (magma_queue_t*) queues );
            
            gpu_time = magma_sync_wtime(0);
            
            magmablas_dsymm_mgpu(
                MagmaLeft, MagmaLower, msize, N,
                alpha, dA, ldda, offset,
                       dB, ldda,
                beta,  dC, ldda, dwork, dworksiz,
                opts.ngpu, nb, queues, nqueue, events, nevents, gnode, ncmplx);
            
            gpu_time = magma_sync_wtime(0) - gpu_time;
            gpu_perf = gflops / gpu_time;
            
            #ifdef TRACING
            char buf[80];
            snprintf( buf, sizeof(buf), "dsymm-m%d-n%d-nb%d-ngpu%d-run%d.svg",
                      (int) M, (int) N, (int) nb, (int) opts.ngpu, (int) iter );
            trace_finalize( buf, "trace.css" );
            #endif
            
            /* ====================================================================
               Performs operation using CUBLAS
               =================================================================== */
            if ( opts.check && iter == 0 ) {
                magma_setdevice( 0 );
                magmablasSetKernelStream( opts.queue );
                magma_dsetmatrix( M, M, hA, lda, dA2, ldda );
                magma_dsetmatrix( M, N, hB, lda, dB[0], ldda );
                magma_dsetmatrix( M, N, hC, lda, dwork[0], ldda );
                
                gpu_time2 = magma_sync_wtime(0);
                magma_dsymm(
                    MagmaLeft, MagmaLower, msize, N,
                    alpha, dA2 + offset + offset*ldda, ldda,
                           dB[0],    ldda,
                    beta,  dwork[0], ldda );
                gpu_time2 = magma_sync_wtime(0) - gpu_time2;
                gpu_perf2 = gflops / gpu_time2;
            }
            
            /* =====================================================================
               Performs operation using LAPACK
               =================================================================== */
            if ( opts.check ) {
                // store ||A||*||B||
                Anorm  = lapackf77_dlange("fro", &msize, &msize, hA + offset + offset*lda, &lda, work );
                Anorm *= lapackf77_dlange("fro", &msize, &N, hB, &lda, work );
                
                //printf( "A =" ); magma_dprint( M, M, hA, lda );
                //printf( "B =" ); magma_dprint( M, N, hB, lda );
                //printf( "C =" ); magma_dprint( M, N, hC, lda );
                
                cpu_time = magma_wtime();
                blasf77_dsymm( "Left", "Lower", &msize, &N,
                                &alpha, hA + offset + offset*lda, &lda,
                                        hB, &lda,
                                &beta,  hC, &lda );
                cpu_time = magma_wtime() - cpu_time;
                cpu_perf = gflops / cpu_time;
                
                for (magma_int_t dev=0; dev < opts.ngpu; ++dev) {
                    magma_setdevice( dev );
                    magma_dgetmatrix( M, N, dC[dev], ldda, hR, lda );
                    
                    // compute relative error ||R||/||A||*||B||, where R := C_magma - C_lapack = R - C
                    size = ldc*N;
                    blasf77_daxpy( &size, &c_neg_one, hC, &ione, hR, &ione );
                    error = lapackf77_dlange("fro", &msize, &N, hR, &lda, work) / Anorm;
                    
                    //printf( "R ="  ); magma_dprint( M, N, hR, lda );
                    bool okay = (error < tol);
                    status += ! okay;
                    if (dev == 0) {
                        printf( "%5d %5d %5d %5d   %7.1f (%7.4f)   %7.1f (%7.4f)   %7.1f (%7.4f)   %8.2e   %s\n",
                                (int) M, (int) N, (int) nb, (int) offset,
                                cpu_perf, cpu_time,
                                gpu_perf, gpu_time,
                                gpu_perf2, gpu_time2,
                                error, (okay ? "ok" : "failed") );
                    }
                    else {
                        printf( "    dev %d %74s  %8.2e   %s\n", dev, "",
                                error, (okay ? "ok" : "failed") );
                    }
                }
            } else {
                printf( "%5d %5d %5d %5d     ---   (  ---  )   %7.1f (%7.4f)     ---   (  ---  )   ---\n",
                        (int) M, (int) N, (int) nb, (int) offset,
                        gpu_perf, gpu_time );
            }
            
            TESTING_FREE_CPU( hA );
            TESTING_FREE_CPU( hB );
            TESTING_FREE_CPU( hC );
            
            TESTING_FREE_PIN( hR );
            
            for( d = 0; d < opts.ngpu; ++d ) {
                magma_setdevice( d );
                TESTING_FREE_DEV( dA[d]    );
                TESTING_FREE_DEV( dB[d]    );
                TESTING_FREE_DEV( dC[d]    );
                TESTING_FREE_DEV( dwork[d] );
            }
            
            if ( opts.check ) {
                magma_setdevice( 0 );
                TESTING_FREE_DEV( dA2 );
            }
            fflush( stdout );
        }
        if ( opts.niter > 1 ) {
            printf( "\n" );
        }
      }  // offset
      printf( "\n" );
    }

    for( d = 0; d < opts.ngpu; ++d ) {
        magma_setdevice( d );
        for( i = 0; i < nqueue; ++i ) {
            magma_queue_destroy( queues[d][i] );
        }
        for( i = 0; i < nevents; ++i ) {
            magma_event_destroy( events[d][i] );
        }
    }
    
    opts.cleanup();
    TESTING_FINALIZE();
    return status;
}
