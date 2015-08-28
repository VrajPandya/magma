/*
    -- MAGMA (version 1.6.3-beta1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date August 2015
 
       @author Mark Gates
       @generated from cblas_z.cpp normal z -> s, Tue Aug 25 16:35:14 2015

    Wrappers around a few CBLAS functions.
    
    Primarily, we use the standard Fortran BLAS interface in MAGMA. However,
    functions that return a value (as opposed to subroutines that are void)
    are not portable, as they depend on how Fortran returns values. The routines
    here provide a portable interface. These are not identical to CBLAS, in
    particular, [cz]dot[uc] return real numbers (as in Fortran BLAS) rather
    than return values via an argument.
    
    Only these BLAS-1 functions are provided:
    
    magma_cblas_sasum / dasum
    magma_cblas_snrm2 / dnrm2
    magma_cblas_sdot  / ddot
    magma_cblas_sdot  / ddot

*/
#include "common_magma.h"

#define REAL

// ========================================
// Level 1 BLAS

// --------------------
/** Returns the sum of absolute values of vector x; i.e., one norm.

    To avoid dependence on CBLAS and incompatability issues between BLAS
    libraries, MAGMA uses its own implementation, following BLAS reference.
    
    @param[in]
    n       Number of elements in vector x. n >= 0.

    @param[in]
    x       REAL array on CPU host.
            The n element vector x of dimension (1 + (n-1)*incx).

    @param[in]
    incx    Stride between consecutive elements of x. incx > 0.

    @ingroup magma_sblas1
*/
extern "C"
float magma_cblas_sasum(
    magma_int_t n,
    const float *x, magma_int_t incx )
{
    if ( n <= 0 || incx <= 0 ) {
        return 0;
    }
    float result = 0;
    if ( incx == 1 ) {
        for( int i=0; i < n; ++i ) {
            result += MAGMA_S_ABS1( x[i] );
        }
    }
    else {
        int nincx = n*incx;
        for( int i=0; i < nincx; i += incx ) {
            result += MAGMA_S_ABS1( x[i] );
        }
    }
    return result;
}


// --------------------
/** Returns 2-norm of vector x. Avoids unnecesary over/underflow.

    To avoid dependence on CBLAS and incompatability issues between BLAS
    libraries, MAGMA uses its own implementation, following BLAS reference.
    
    @param[in]
    n       Number of elements in vector x. n >= 0.

    @param[in]
    x       REAL array on CPU host.
            The n element vector x of dimension (1 + (n-1)*incx).

    @param[in]
    incx    Stride between consecutive elements of x. incx > 0.

    @ingroup magma_sblas1
*/
static inline float sqr( float x ) { return x*x; }

extern "C"
float magma_cblas_snrm2(
    magma_int_t n,
    const float *x, magma_int_t incx )
{
    if (n <= 0 || incx <= 0) {
        return 0;
    }
    else {
        float scale = 0;
        float ssq   = 1;
        // the following loop is equivalent to this call to the lapack
        // auxiliary routine:
        // call zlassq( n, x, incx, scale, ssq )
        for( int ix=0; ix < 1 + (n-1)*incx; ix += incx ) {
            if ( real( x[ix] ) != 0 ) {
                float temp = fabs( real( x[ix] ));
                if (scale < temp) {
                    ssq = 1 + ssq * sqr(scale/temp);
                    scale = temp;
                }
                else {
                    ssq += sqr(temp/scale);
                }
            }
            #ifdef COMPLEX
            if ( imag( x[ix] ) != 0 ) {
                float temp = fabs( imag( x[ix] ));
                if (scale < temp) {
                    ssq = 1 + ssq * sqr(scale/temp);
                    scale = temp;
                }
                else {
                    ssq += sqr(temp/scale);
                }
            }
            #endif
        }
        return scale*magma_ssqrt(ssq);
    }
}


// --------------------
/** Returns dot product of vectors x and y; \f$ x^H y \f$.

    To avoid dependence on CBLAS and incompatability issues between BLAS
    libraries, MAGMA uses its own implementation, following BLAS reference.
    
    @param[in]
    n       Number of elements in vector x and y. n >= 0.

    @param[in]
    x       REAL array on CPU host.
            The n element vector x of dimension (1 + (n-1)*incx).

    @param[in]
    incx    Stride between consecutive elements of x. incx > 0.

    @param[in]
    y       REAL array on CPU host.
            The n element vector y of dimension (1 + (n-1)*incy).

    @param[in]
    incy    Stride between consecutive elements of dy. incy > 0.

    @ingroup magma_sblas1
*/
extern "C"
float magma_cblas_sdot(
    magma_int_t n,
    const float *x, magma_int_t incx,
    const float *y, magma_int_t incy )
{
    // after too many issues with MKL and other BLAS, just write our own dot product!
    float value = MAGMA_S_ZERO;
    magma_int_t i;
    if ( incx == 1 && incy == 1 ) {
        for( i=0; i < n; ++i ) {
            value += conj( x[i] ) * y[i];
        }
    }
    else {
        magma_int_t ix=0, iy=0;
        if ( incx < 0 ) { ix = (-n + 1)*incx; }
        if ( incy < 0 ) { iy = (-n + 1)*incy; }
        for( i=0; i < n; ++i ) {
            value += conj( x[ix] ) * y[iy];
            ix += incx;
            iy += incy;
        }
    }
    return value;
}


#ifdef COMPLEX
// --------------------
/** Returns dot product (unconjugated) of vectors x and y; \f$ x^T y \f$.

    To avoid dependence on CBLAS and incompatability issues between BLAS
    libraries, MAGMA uses its own implementation, following BLAS reference.

    @param[in]
    n       Number of elements in vector x and y. n >= 0.

    @param[in]
    x       REAL array on CPU host.
            The n element vector x of dimension (1 + (n-1)*incx).

    @param[in]
    incx    Stride between consecutive elements of x. incx > 0.

    @param[in]
    y       REAL array on CPU host.
            The n element vector y of dimension (1 + (n-1)*incy).

    @param[in]
    incy    Stride between consecutive elements of dy. incy > 0.

    @ingroup magma_sblas1
*/
extern "C"
float magma_cblas_sdot(
    magma_int_t n,
    const float *x, magma_int_t incx,
    const float *y, magma_int_t incy )
{
    // after too many issues with MKL and other BLAS, just write our own dot product!
    float value = MAGMA_S_ZERO;
    magma_int_t i;
    if ( incx == 1 && incy == 1 ) {
        for( i=0; i < n; ++i ) {
            value += x[i] * y[i];
        }
    }
    else {
        magma_int_t ix=0, iy=0;
        if ( incx < 0 ) { ix = (-n + 1)*incx; }
        if ( incy < 0 ) { iy = (-n + 1)*incy; }
        for( i=0; i < n; ++i ) {
            value += x[ix] * y[iy];
            ix += incx;
            iy += incy;
        }
    }
    return value;
}
#endif

#undef REAL
