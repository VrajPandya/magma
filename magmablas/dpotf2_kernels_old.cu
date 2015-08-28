/*
    -- MAGMA (version 1.6.3-beta1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2013
       
       @author Azzam Haidar
       @author Tingxing Dong

       @generated from zpotf2_kernels_old.cu normal z -> d, Tue Aug 25 16:35:10 2015
*/

#include "common_magma.h"
#include "batched_kernel_param.h"
#include "magma_templates.h"

#define PRECISION_d


#define A(i, j)  (A + (i) + (j)*lda)   // A(i, j) means at i row, j column


// dynamically allocated shared memory, set to size number of threads when the kernel is launched.
// See CUDA Guide B.2.3
extern __shared__ double shared_data[];


// dynamically allocated shared memory, set to size number of threads when the kernel is launched.
// See CUDA Guide B.2.3
extern __shared__ double dble_shared_data[];

/////////////////////////////////////////////////////////////////////////////////////////////////
__global__ void ddot_kernel_batched(int n, double **x_array, int incx, int offset, magma_int_t *info_array, int gbstep)
{
    int tx = threadIdx.x;

    double *x = x_array[blockIdx.z]+offset;

    double *sdata = dble_shared_data;

    double res = MAGMA_D_ZERO;

    if (tx < n) {
        res = x[tx*incx];
    }

    sdata[tx] = MAGMA_D_REAL(res * MAGMA_D_CNJG(res));

    __syncthreads();

    for (int s = blockDim.x/2; s > 32; s >>= 1 ) {
        if (tx < s) {
            sdata[tx] += sdata[tx+s];
        }
        __syncthreads();
    }

    if (tx < 32) {
        volatile double* smem = sdata;
        smem[tx] += smem[tx+32];
        smem[tx] += smem[tx+16];
        smem[tx] += smem[tx+8];
        smem[tx] += smem[tx+4];
        smem[tx] += smem[tx+2];
        smem[tx] += smem[tx+1];
    }

    if (tx == 0) {
        double xreal = MAGMA_D_REAL(x[n*incx]);        
        x[n*incx] = MAGMA_D_MAKE(sqrt(xreal - sdata[0]), 0);
        if (xreal <= MAGMA_D_ZERO) {
            info_array[blockIdx.z] = offset + gbstep + 1;
        }
    }
}


void magma_dpotf2_ddot_batched(magma_int_t n, double **x_array, magma_int_t incx, magma_int_t offset, magma_int_t *info_array, magma_int_t gbstep, magma_int_t batchCount, magma_queue_t queue)
{
    /*
    Specialized Ddot
    1) performs ddot sum = x[0:n-1]*conj(x[0:n-1])
    2) updates x[n] = sqrt(x[n]-sum);
    */
    if (n > MAX_NTHREADS) {
        printf("n = %d > %d is not supported in dpotf2_ddot\n", (int) n, (int) MAX_NTHREADS);
    }
    int threadSize;

    if (n <= 1024 && n > 512) {
        threadSize = 1024;
    }
    else if (n <= 512 && n > 256 ) {
        threadSize = 512;
    }
    else if (n <= 256 && n > 128) {
        threadSize = 256;
    }
    else if (n <= 128 && n > 64) {
        threadSize = 128;
    }
    else {
        threadSize = 64;
    }

    
    dim3 grid(1, 1, batchCount);
    ddot_kernel_batched<<< grid, threadSize, 
                  threadSize * sizeof(double), queue>>> (n, x_array, incx, offset, info_array, gbstep);
}

/////////////////////////////////////////////////////////////////////////////////////////////////
__global__ void dscal_kernel_batched(int n, double **x_array, int incx, int offset, magma_int_t *info_array)
{
    // checkinfo to avoid computation of the singular matrix
    if (info_array[blockIdx.z] != 0 ) return;

    int id = threadIdx.x;
    double *x = x_array[blockIdx.z]+offset;

    __shared__ double factor;

    if (threadIdx.x == 0) {
        factor = MAGMA_D_MAKE(1.0/MAGMA_D_REAL(x[0]), 0.0);
    }

    __syncthreads();

    if ( id < n && id > 0) {
        x[id*incx] = x[id*incx] * factor;
        //printf("x=%f", x[id*incx]);
    }
}


void magma_dpotf2_dscal_batched(magma_int_t n, double **x_array, magma_int_t incx, magma_int_t offset, magma_int_t *info_array, magma_int_t batchCount, magma_queue_t queue)
{
    /*
    Specialized Dscal perform x[1:n-1]/x[0]
    */
    dim3 grid(1, 1, batchCount);
    dim3 threads(n, 1, 1); 

    dscal_kernel_batched<<< grid, threads, 0, queue >>> (n, x_array, incx, offset, info_array);
}

/////////////////////////////////////////////////////////////////////////////////////////////////
__global__ void dlacgv_kernel_batched(int n, double **x_array, int incx, int offset)
{
    int id = threadIdx.x;

    double *x = x_array[blockIdx.z]+offset;

    if ( id < n ) {
        x[id*incx] = MAGMA_D_CNJG(x[id*incx]);
    }
}

void magma_dlacgv_batched(magma_int_t n, double **x_array, magma_int_t incx, magma_int_t offset, magma_int_t batchCount, magma_queue_t queue)
{
    /*
    Purpose
    =======

    DLACGV conjugates a real vector of length N.

    Arguments
    =========

    N       (input) INTEGER
            The length of the vector X.  N >= 0.

    X       (input/output) DOUBLE PRECISION array, dimension
                           (1+(N-1)*abs(INCX))
            On entry, the vector of length N to be conjugated.
            On exit, X is overwritten with conjg(X).

    INCX    (input) INTEGER
            The spacing between successive elements of X.

    ===================================================================== */

    dim3 grid(1, 1, batchCount);
    dim3 threads(n, 1, 1);
   
    dlacgv_kernel_batched<<< grid, threads, 0, queue >>> (n, x_array, incx, offset);
}




/////////////////////////////////////////////////////////////////////////////////////////////////
static __device__ void dpotf2_device(int m, int n, 
                              double *A, int lda, 
                              double alpha, 
                              double beta, magma_int_t *info, int gbstep)
{
    /*
    Each thread block load entire A into shared memory
    factorize it and copy back. n must be small enough to fit shared memory.
    n is checked by a macro POTF2_TILE_SIZE before the kernel. 
    */
    // checkinfo to avoid computation of the singular matrix
    if (*info != 0 ) return;

    int tx = threadIdx.x;
    double *sdata_A = shared_data;
    __shared__ double factor;
    __shared__ double sum[POTF2_TILE_SIZE];

    // load A into sdata_A
    if (tx < m)
    {
        for (int i=0; i < n; i++)
        {  
             sdata_A[tx + i * m] =  A[tx + i * lda];
        }
    }
    __syncthreads();

    for (int iter=0; iter < n; iter++)
    {
        double res = MAGMA_D_ZERO;
        double res1 = MAGMA_D_ZERO;

        //1) performs ddot sum = A[iter, 0:iter-1]*conj(A[iter, 0:iter-1])
        //2) updates A[iter,iter] = sqrt(A[iter,iter]-sum);
        if (tx < iter)
        {
            res = MAGMA_D_REAL (sdata_A[iter + tx * m] * MAGMA_D_CNJG(sdata_A[iter + tx * m]));         
            sum[tx] = res;
        }
        else
        {
            sum[tx] = 0.0;
        }
        __syncthreads();
        magma_sum_reduce<POTF2_TILE_SIZE>(tx, sum); //tried on K40: if m=32 n=32 the overall dpotf2_device routine time is 60ms n=16 time=25 n=8 time=20ms 
        //magma_sum_reduce_n(iter, tx, sum); //tried on K40: if m=32 n=32 the time went from 61ms to 70ms when switching to reduce_n. n=16 time=28.
        //magma_sum_reduce_inlined(iter, tx, sum); //tried on K40: similar to magma_sum_reduce<POTF2_TILE_SIZE>(tx, sum);
        
        __shared__ double xreal;
        if (tx == 0) {
            xreal = MAGMA_D_REAL(sdata_A[iter + iter * m]);        
            sdata_A[iter + iter * m] = MAGMA_D_MAKE(sqrt(xreal - sum[0]), 0);
            if (xreal <= MAGMA_D_ZERO) {
                *info = iter + gbstep + 1;
            }
        }
        __syncthreads();
        if (xreal <= MAGMA_D_ZERO) return;
        __syncthreads();

        //dlacgv conjugates a real vector of length iter. //TODO
        #if defined(PRECISION_z) || defined(PRECISION_c)
        if (tx < iter)
        {
            sdata_A[iter + tx * m] = MAGMA_D_CNJG(sdata_A[iter + tx * m]);
        }
        __syncthreads();  
        #endif
  
        // dgemv  
        // Compute elements iter:n-1 of column iter = A(iter:n,0:iter-1) * A(iter-1,0:iter-1) (row).
        if (tx < m && tx > iter)
        {
            for (int j=0; j < iter; j++)
            {
                res1 += sdata_A[tx + j * m]  *  sdata_A[iter + j * m]; // TODO move the dlacgv conj to be done automatically here implicitly.
            }   
            sdata_A [tx + iter * m] = alpha * res1 + sdata_A [tx + iter * m] * beta;   
        }
        __syncthreads();  

        //dlacgv conjugates a real vector of length iter.
        #if defined(PRECISION_z) || defined(PRECISION_c)
        if (tx < iter)
        {
            sdata_A[iter + tx * m] = MAGMA_D_CNJG(sdata_A[iter + tx * m]);
        }
        __syncthreads();  
        #endif

        // dscal perform A[iter:n-1, iter]/A[iter,iter];
        if (tx == 0) {
            factor = MAGMA_D_MAKE(1.0/MAGMA_D_REAL(sdata_A[iter + iter * m]), 0.0);
        }
        __syncthreads();

        if ( tx < m && tx > iter) {
            sdata_A[ tx + iter * m ]  *= factor;
        }
        __syncthreads();
    }// end of iter

    //copy sdata_A to A
    if (tx < m)
    {
        for (int i=0; i < n; i++)
        {  
             A[tx + i * lda] = sdata_A[tx + i * m];
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////
__global__ void dpotf2_kernel_batched(int m, int n, 
                              double **dA_array, int lda, 
                              double alpha, 
                              double beta, 
                              magma_int_t *info_array, int gbstep)
{
    /*
    Each thread block load entire dA_array[blockIdx.z] into shared memory
    factorize it and copy back. n must be small enough to fit shared memory.
    n is checked by a macro POTF2_TILE_SIZE before the kernel. 
    */
    int batchid = blockIdx.z;
    dpotf2_device(m, n, dA_array[batchid], lda, alpha, beta, &(info_array[batchid]), gbstep);
}
/////////////////////////////////////////////////////////////////////////////////////////////////
__global__ void dpotf2_kernel(int m, int n, 
                              double *dA, int lda, 
                              double alpha, 
                              double beta,
                              magma_int_t *info)
{
    dpotf2_device(m, n, dA, lda, alpha, beta, info, 0);
}
/////////////////////////////////////////////////////////////////////////////////////////////////
/**
    Purpose
    -------

    dpotf2 computes the Cholesky factorization of a real symmetric
    positive definite matrix A.

    The factorization has the form
        A = U**H * U,  if UPLO = MagmaUpper, or
        A = L  * L**H, if UPLO = MagmaLower,
    where U is an upper triangular matrix and L is lower triangular.

    This is the unblocked version of the algorithm, calling Level 2 BLAS.

    Arguments
    ---------

    @param[in]
    uplo    magma_uplo_t
            Specifies whether the upper or lower triangular part of the
            symmetric matrix A is stored.
      -     = MagmaUpper:  Upper triangular
      -     = MagmaLower:  Lower triangular

    @param[in]
    n       INTEGER
            The order of the matrix A.  N >= 0 and N <= 512.

    @param[in,out]
    dA      DOUBLE_PRECISION array, dimension (LDDA,N)
            On entry, the symmetric matrix A.  If UPLO = MagmaUpper, the leading
            n by n upper triangular part of A contains the upper
            triangular part of the matrix A, and the strictly lower
            triangular part of A is not referenced.  If UPLO = MagmaLower, the
            leading n by n lower triangular part of A contains the lower
            triangular part of the matrix A, and the strictly upper
            triangular part of A is not referenced.
    \n
            On exit, if INFO = 0, the factor U or L from the Cholesky
            factorization A = U**H * U  or A = L * L**H.

    @param[in]
    ldda    INTEGER
            The leading dimension of the array A.  LDDA >= max(1,N).

    @param[out]
    info    INTEGER
      -     = 0: successful exit
      -     < 0: if INFO = -k, the k-th argument had an illegal value
      -     > 0: if INFO = k, the leading minor of order k is not
                 positive definite, and the factorization could not be
                 completed.

    @ingroup magma_dposv_aux
    ********************************************************************/
extern "C" magma_int_t
magma_dpotf2_tile_batched(
    magma_uplo_t uplo, magma_int_t m, magma_int_t n,
    double **dA_array, magma_int_t lda,
    magma_int_t *info_array, magma_int_t gbstep, magma_int_t batchCount, magma_queue_t queue)
{
    magma_int_t arginfo = 0;
    
    if ( uplo != MagmaUpper && uplo != MagmaLower) {
        arginfo = -1;
    } else if (m < 0 || n < 0 || m > POTF2_TILE_SIZE || n > POTF2_TILE_SIZE) {
        arginfo = -2;
    } else if (lda < max(1,m)) {
        arginfo = -4;
    } else if (m < n) {
        arginfo = -10;
    }
    if (uplo == MagmaUpper) {
        printf("Upper side is unavailable \n");
        arginfo = -1;
    }

    if (arginfo != 0) {
        magma_xerbla( __func__, -(arginfo) );
        return arginfo;
    }
    
    // Quick return if possible
    if (m == 0 || n == 0) {
        return arginfo;
    }

    double alpha = MAGMA_D_NEG_ONE;
    double beta  = MAGMA_D_ONE;

    dim3 dimGrid(1, 1, batchCount);
    dim3 threads(POTF2_TILE_SIZE, 1);
    int shared_mem_size = sizeof(double)*m*n; // + sizeof(double)*(POTF2_TILE_SIZE+1);

    dpotf2_kernel_batched<<<dimGrid, threads, shared_mem_size, queue >>>(m, n, dA_array, lda, alpha, beta, info_array, gbstep);

    return arginfo;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////
extern "C" magma_int_t
magma_dpotf2_tile(
    magma_uplo_t uplo, magma_int_t m, magma_int_t n,
    double *dA, magma_int_t lda,
    magma_int_t *info)
{
    *info = 0;
    if ( uplo != MagmaUpper && uplo != MagmaLower) {
        *info = -1;
    } else if (m < 0 || n < 0 || m > POTF2_TILE_SIZE) {
        *info = -2;
    } else if (lda < max(1,m)) {
        *info = -4;
    } else if (m < n) {
        *info = -10;
    }
    if (uplo == MagmaUpper) {
        printf("Upper side is unavailable \n");
        *info = -1;
    }


    if (*info != 0) {
        magma_xerbla( __func__, -(*info) );
        return *info;
    }

    // Quick return if possible
    if (m == 0 || n == 0) {
        return *info;
    }

    double alpha = MAGMA_D_NEG_ONE;
    double beta  = MAGMA_D_ONE;

    dim3 dimGrid(1);
    dim3 threads(POTF2_TILE_SIZE, 1);
    int shared_mem_size = sizeof(double)*m*n; // + sizeof(double)*(POTF2_TILE_SIZE+1);

    dpotf2_kernel<<<dimGrid, threads, shared_mem_size, magma_stream >>>(m, n, dA, lda, alpha, beta, info);

    return *info;
}
