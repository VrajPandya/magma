/*
    -- MAGMA (version 2.0.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date February 2016
       
       @author Stan Tomov
       @author Mark Gates
       
       @generated from magmablas/zbcyclic.cpp normal z -> d, Tue Feb  9 16:05:35 2016
*/

// include v1 header first; the v2 header will redefine non-q names,
// but we can undef them to get back to the v1 versions.
#include "magmablas_v1.h"

#include "magma_internal.h"

#ifdef HAVE_clBLAS
    #define dA( dev, i_, j_ )  dA[dev], ((i_) + (j_)*ldda)
#else
    #define dA( dev, i_, j_ ) (dA[dev] + (i_) + (j_)*ldda)
#endif

#define hA( i_, j_ ) (hA + (i_) + (j_)*lda)


//===========================================================================
// Set a matrix from CPU to multi-GPUs in 1D column block cyclic distribution.
// The dA arrays are pointers to the matrix data on the corresponding GPUs.
//===========================================================================
extern "C" void
magma_dsetmatrix_1D_col_bcyclic_q(
    magma_int_t m, magma_int_t n,
    const double *hA, magma_int_t lda,
    magmaDouble_ptr   *dA, magma_int_t ldda,
    magma_int_t ngpu, magma_int_t nb,
    magma_queue_t queues[ MagmaMaxGPUs ] )
{
    magma_int_t info = 0;
    if ( m < 0 )
        info = -1;
    else if ( n < 0 )
        info = -2;
    else if ( lda < m )
        info = -4;
    else if ( ldda < m )
        info = -6;
    else if ( ngpu < 1 )
        info = -7;
    else if ( nb < 1 )
        info = -8;
    
    if (info != 0) {
        magma_xerbla( __func__, -(info) );
        return;  //info;
    }
    
    magma_int_t j, dev, jb;
    
    magma_device_t cdevice;
    magma_getdevice( &cdevice );
    
    for( j = 0; j < n; j += nb ) {
        dev = (j/nb) % ngpu;
        magma_setdevice( dev );
        jb = min(nb, n-j);
        magma_dsetmatrix_async( m, jb,
                                hA(0,j), lda,
                                dA( dev, 0, j/(nb*ngpu)*nb ), ldda,
                                queues[dev] );
    }
    
    magma_setdevice( cdevice );
}


#undef magma_dsetmatrix_1D_col_bcyclic

extern "C" void
magma_dsetmatrix_1D_col_bcyclic(
    magma_int_t m, magma_int_t n,
    const double *hA, magma_int_t lda,
    magmaDouble_ptr   *dA, magma_int_t ldda,
    magma_int_t ngpu, magma_int_t nb )
{
    // uses NULL queue -> NULL stream in setmatrix_async
    //magma_queue_t queues[ MagmaMaxGPUs ] = { NULL };
    magma_queue_t queues[MagmaMaxGPUs];
    for( int dev=0; dev < ngpu; dev++ ) {
        magma_setdevice( dev );
        magma_queue_create( dev, &queues[dev] );
    }
    magma_dsetmatrix_1D_col_bcyclic_q( m, n, hA, lda, dA, ldda, ngpu, nb, queues );
    for( int dev=0; dev < ngpu; dev++ ) {
        magma_setdevice( dev );
        magma_queue_sync( queues[dev] );
        magma_queue_destroy( queues[dev] );
    }
}


//===========================================================================
// Get a matrix with 1D column block cyclic distribution from multi-GPUs to the CPU.
// The dA arrays are pointers to the matrix data on the corresponding GPUs.
//===========================================================================
extern "C" void
magma_dgetmatrix_1D_col_bcyclic_q(
    magma_int_t m, magma_int_t n,
    magmaDouble_const_ptr const *dA, magma_int_t ldda,
    double                 *hA, magma_int_t lda,
    magma_int_t ngpu, magma_int_t nb,
    magma_queue_t queues[ MagmaMaxGPUs ] )
{
    magma_int_t info = 0;
    if ( m < 0 )
        info = -1;
    else if ( n < 0 )
        info = -2;
    else if ( ldda < m )
        info = -4;
    else if ( lda < m )
        info = -6;
    else if ( ngpu < 1 )
        info = -7;
    else if ( nb < 1 )
        info = -8;
    
    if (info != 0) {
        magma_xerbla( __func__, -(info) );
        return;  //info;
    }
    
    magma_int_t j, dev, jb;
    
    magma_device_t cdevice;
    magma_getdevice( &cdevice );
    
    for( j = 0; j < n; j += nb ) {
        dev = (j/nb) % ngpu;
        magma_setdevice( dev );
        jb = min(nb, n-j);
        magma_dgetmatrix_async( m, jb,
                                dA( dev, 0, j/(nb*ngpu)*nb ), ldda,
                                hA(0,j), lda,
                                queues[dev] );
    }
    
    magma_setdevice( cdevice );
}


#undef magma_dgetmatrix_1D_col_bcyclic

extern "C" void
magma_dgetmatrix_1D_col_bcyclic(
    magma_int_t m, magma_int_t n,
    magmaDouble_const_ptr const *dA, magma_int_t ldda,
    double                 *hA, magma_int_t lda,
    magma_int_t ngpu, magma_int_t nb )
{
    // uses NULL queue -> NULL stream in setmatrix_async
    //magma_queue_t queues[ MagmaMaxGPUs ] = { NULL };
    magma_queue_t queues[MagmaMaxGPUs];
    for( int dev=0; dev < ngpu; dev++ ) {
        magma_setdevice( dev );
        magma_queue_create( dev, &queues[dev] );
    }
    magma_dgetmatrix_1D_col_bcyclic_q( m, n, dA, ldda, hA, lda, ngpu, nb, queues );
    for( int dev=0; dev < ngpu; dev++ ) {
        magma_setdevice( dev );
        magma_queue_sync( queues[dev] );
        magma_queue_destroy( queues[dev] );
    }
}


//===========================================================================
// Set a matrix from CPU to multi-GPUs in 1D row block cyclic distribution.
// The dA arrays are pointers to the matrix data on the corresponding GPUs.
//===========================================================================
extern "C" void
magma_dsetmatrix_1D_row_bcyclic_q(
    magma_int_t m, magma_int_t n,
    const double    *hA, magma_int_t lda,
    magmaDouble_ptr      *dA, magma_int_t ldda,
    magma_int_t ngpu, magma_int_t nb,
    magma_queue_t queues[ MagmaMaxGPUs ] )
{
    magma_int_t info = 0;
    if ( m < 0 )
        info = -1;
    else if ( n < 0 )
        info = -2;
    else if ( lda < m )
        info = -4;
    else if ( ldda < (1+m/(nb*ngpu))*nb )
        info = -6;
    else if ( ngpu < 1 )
        info = -7;
    else if ( nb < 1 )
        info = -8;
    
    if (info != 0) {
        magma_xerbla( __func__, -(info) );
        return;  //info;
    }
    
    magma_int_t i, dev, jb;
    
    magma_device_t cdevice;
    magma_getdevice( &cdevice );
    
    for( i = 0; i < m; i += nb ) {
        dev = (i/nb) % ngpu;
        magma_setdevice( dev );
        jb = min(nb, m-i);
        magma_dsetmatrix_async( jb, n,
                                hA(i,0), lda,
                                dA( dev, i/(nb*ngpu)*nb, 0 ), ldda,
                                queues[dev] );
    }
    
    magma_setdevice( cdevice );
}


#undef magma_dsetmatrix_1D_row_bcyclic

extern "C" void
magma_dsetmatrix_1D_row_bcyclic(
    magma_int_t m, magma_int_t n,
    const double    *hA, magma_int_t lda,
    magmaDouble_ptr      *dA, magma_int_t ldda,
    magma_int_t ngpu, magma_int_t nb )
{
    // uses NULL queue -> NULL stream in setmatrix_async
    //magma_queue_t queues[ MagmaMaxGPUs ] = { NULL };
    magma_queue_t queues[MagmaMaxGPUs];
    for( int dev=0; dev < ngpu; dev++ ) {
        magma_setdevice( dev );
        magma_queue_create( dev, &queues[dev] );
    }
    magma_dsetmatrix_1D_row_bcyclic_q( m, n, hA, lda, dA, ldda, ngpu, nb, queues );
    for( int dev=0; dev < ngpu; dev++ ) {
        magma_setdevice( dev );
        magma_queue_sync( queues[dev] );
        magma_queue_destroy( queues[dev] );
    }
}


//===========================================================================
// Get a matrix with 1D row block cyclic distribution from multi-GPUs to the CPU.
// The dA arrays are pointers to the matrix data for the corresponding GPUs.
//===========================================================================
extern "C" void
magma_dgetmatrix_1D_row_bcyclic_q(
    magma_int_t m, magma_int_t n,
    magmaDouble_const_ptr const *dA, magma_int_t ldda,
    double                 *hA, magma_int_t lda,
    magma_int_t ngpu, magma_int_t nb,
    magma_queue_t queues[ MagmaMaxGPUs ] )
{
    magma_int_t info = 0;
    if ( m < 0 )
        info = -1;
    else if ( n < 0 )
        info = -2;
    else if ( ldda < (1+m/(nb*ngpu))*nb )
        info = -4;
    else if ( lda < m )
        info = -6;
    else if ( ngpu < 1 )
        info = -7;
    else if ( nb < 1 )
        info = -8;
    
    if (info != 0) {
        magma_xerbla( __func__, -(info) );
        return;  //info;
    }
    
    magma_int_t i, dev, jb;
    
    magma_device_t cdevice;
    magma_getdevice( &cdevice );
    
    for( i = 0; i < m; i += nb ) {
        dev = (i/nb) % ngpu;
        magma_setdevice( dev );
        jb = min(nb, m-i);
        magma_dgetmatrix_async( jb, n,
                                dA( dev, i/(nb*ngpu)*nb, 0 ), ldda,
                                hA(i,0), lda,
                                queues[dev] );
    }
    
    magma_setdevice( cdevice );
}


#undef magma_dgetmatrix_1D_row_bcyclic

extern "C" void
magma_dgetmatrix_1D_row_bcyclic(
    magma_int_t m, magma_int_t n,
    magmaDouble_const_ptr const *dA, magma_int_t ldda,
    double                 *hA, magma_int_t lda,
    magma_int_t ngpu, magma_int_t nb )
{
    // uses NULL queue -> NULL stream in setmatrix_async
    //magma_queue_t queues[ MagmaMaxGPUs ] = { NULL };
    magma_queue_t queues[MagmaMaxGPUs];
    for( int dev=0; dev < ngpu; dev++ ) {
        magma_setdevice( dev );
        magma_queue_create( dev, &queues[dev] );
    }
    magma_dgetmatrix_1D_row_bcyclic_q( m, n, dA, ldda, hA, lda, ngpu, nb, queues );
    for( int dev=0; dev < ngpu; dev++ ) {
        magma_setdevice( dev );
        magma_queue_sync( queues[dev] );
        magma_queue_destroy( queues[dev] );
    }
}
