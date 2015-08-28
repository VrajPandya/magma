/*
    -- MAGMA (version 1.6.3-beta1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date August 2015

       @author Hartwig Anzt
       @author Eduardo Ponce

       @generated from zidr_acc.cpp normal z -> s, Tue Aug 25 16:35:33 2015
*/

#include "common_magmasparse.h"
#include <cuda_profiler_api.h>

#define RTOLERANCE     lapackf77_slamch( "E" )
#define ATOLERANCE     lapackf77_slamch( "E" )


/**
    Purpose
    -------

    Solves a system of linear equations
       A * X = B
    where A is a real symmetric N-by-N positive definite matrix A.
    This is a GPU implementation of the Induced Dimension Reduction method.

    Arguments
    ---------

    @param[in]
    A           magma_s_matrix
                input matrix A

    @param[in]
    b           magma_s_matrix
                RHS b

    @param[in,out]
    x           magma_s_matrix*
                solution approximation

    @param[in,out]
    solver_par  magma_s_solver_par*
                solver parameters

    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_sposv
    ********************************************************************/

// -1 = no print but write iniital p
//  0 = no printing
//  1 = print scalars
//  2 = print all (few iters)
// >2 = prints all (all iters)
#define MYDEBUG 0

#if MYDEBUG <= 0
#define printD(...)
#define printMatrix(s,m)
#elif MYDEBUG == 1
#define printD(...) printf("%% "__VA_ARGS__)
#define printMatrix(s,m)
#elif MYDEBUG == 2
#define printD(...) printf("%% " __VA_ARGS__)
#define printMatrix(s,m) magma_smatrixInfo_acc(s,m)
#else
#define printD(...) printf("%% " __VA_ARGS__)
#define printMatrix(s,m) magma_smatrixInfo_acc(s,m)
#endif


extern "C" void
magma_smatrixInfo_acc(
    const char *s,
    magma_s_matrix A )
{
    printD(" %s dims = %d x %d\n", s, A.num_rows, A.num_cols);
    printD(" %s location = %d = %s\n", s, A.memory_location, (A.memory_location == Magma_CPU) ? "CPU" : "DEV");
    printD(" %s storage = %d = %s\n", s, A.storage_type, (A.storage_type == Magma_CSR) ? "CSR" : "DENSE");
    printD(" %s major = %d = %s\n", s, A.major, (A.major == MagmaRowMajor) ? "row" : "column");
    printD(" %s nnz = %d\n", s, A.nnz);
    
    magma_int_t ldd = magma_roundup( A.num_rows, 32 );
    if (A.ld != ldd) {
        A.ld = A.num_rows;
    }
    if (A.memory_location == Magma_DEV)
        magma_sprint_gpu( A.num_rows, A.num_cols, A.dval, A.ld );
    else
        magma_sprint( A.num_rows, A.num_cols, A.val, A.ld );
}


extern "C" magma_int_t
magma_sidr_acc(
    magma_s_matrix A, magma_s_matrix b, magma_s_matrix *x,
    magma_s_solver_par *solver_par,
    magma_queue_t queue )
{
    // set queue for old dense routines
    magma_queue_t orig_queue = NULL;
    magmablasGetKernelStream( &orig_queue );

    // prepare solver feedback
    solver_par->solver = Magma_IDR;
    solver_par->numiter = 0;
    solver_par->info = MAGMA_SUCCESS;

    // constants
    const float c_zero = MAGMA_S_ZERO;
    const float c_one = MAGMA_S_ONE;
    const float c_n_one = MAGMA_S_NEG_ONE;

    // internal user options
    const magma_int_t smoothing = 1;   // 1 = enable, 0 = disabled, -1 = disabled with delayed x update
    const float angle = 0.7;          // [0-1]

    // local variables
    magma_int_t info = 0;
    magma_int_t iseed[4] = { 0, 0, 0, 1 };
    magma_int_t dofx = x->num_rows * x->num_cols;
    magma_int_t dofb = b.num_rows * b.num_cols;
    magma_int_t dofr = A.num_rows * b.num_cols;
    magma_int_t dofM; 
    magma_int_t dofP;
    magma_int_t doft;
    magma_int_t inc = 1;
    magma_int_t s;
    magma_int_t distr;
    magma_int_t k, i, sk;
    magma_int_t *piv = NULL;
    magma_int_t innerflag;
    magma_int_t ldd;
    float residual;
    float nrm;
    float nrmb;
    float nrmr;
    float nrmt;
    float rho;
    float om;
    float tr;
    float gamma;
    float mkk;
    float fk;

    // matrices and vectors
    magma_s_matrix dxs = {Magma_CSR};
    magma_s_matrix dP1 = {Magma_CSR}, dP = {Magma_CSR};
    magma_s_matrix dr = {Magma_CSR}, drs = {Magma_CSR};
    magma_s_matrix dG = {Magma_CSR};
    magma_s_matrix dU = {Magma_CSR};
    magma_s_matrix dM1 = {Magma_CSR}, dM = {Magma_CSR};
    magma_s_matrix df = {Magma_CSR};
    magma_s_matrix dt = {Magma_CSR}, dtt = {Magma_CSR};
    magma_s_matrix dc = {Magma_CSR};
    magma_s_matrix dv1 = {Magma_CSR}, dv = {Magma_CSR};
    magma_s_matrix dskp = {Magma_CSR}, skp = {Magma_CSR};
    magma_s_matrix dalpha = {Magma_CSR}, alpha = {Magma_CSR};
    magma_s_matrix dbeta = {Magma_CSR}, beta = {Magma_CSR};
    float *d1 = NULL, *d2 = NULL;
    
    // queue variables
    const magma_queue_t squeue = 0;    // synchronous kernel queues

    // performance variables
    long long int gpumem = 0;

    // chronometry
    real_Double_t tempo1, tempo2;

    // set synchrounous kernel queues
    queue = squeue;
    printD("Kernel queues: (orig, queue) = (%p, %p)\n", (void *)orig_queue, (void *)queue);

    // Set to Q
    magmablasSetKernelStream( queue );

    // initial s space
    // hack --> use "--restart" option as the shadow space number.
    s = 1;
    if ( solver_par->restart != 30 ) {
        if ( solver_par->restart > A.num_cols )
            s = A.num_cols;
        else
            s = solver_par->restart;
    }
    solver_par->restart = s;

    // set max iterations
    solver_par->maxiter = min( 2 * A.num_cols, solver_par->maxiter );

    // check if matrix A is square
    if ( A.num_rows != A.num_cols ) {
        printD("Error! matrix must be square.\n");
        info = MAGMA_ERR;
        goto cleanup;
    }
    gpumem += (A.nnz * sizeof(float)) + (A.nnz * sizeof(magma_index_t)) + ((A.num_rows + 1) * sizeof(magma_index_t));

    // initial solution vector
    // x = 0
    //magmablas_slaset( MagmaFull, x->num_rows, x->num_cols, c_zero, c_zero, x->dval, x->num_rows );
    printMatrix("X", *x);
    gpumem += x->nnz * sizeof(float);

    // initial RHS
    // b = 1
    //magmablas_slaset( MagmaFull, b.num_rows, b.num_cols, c_one, c_one, b.dval, b.num_rows );
    printMatrix("B", b);
    gpumem += b.nnz * sizeof(float);

    // |b|
    nrmb = magma_snrm2( b.num_rows, b.dval, inc );
    printD("init norm(b) ..........%lg\n", nrmb);

    // check for |b| == 0
    if ( nrmb == 0.0 ) {
        printD("RHS is zero, exiting...\n");
        magma_sscal( dofx, MAGMA_S_ZERO, x->dval, 1 );
        solver_par->init_res = 0.0;
        solver_par->final_res = 0.0;
        solver_par->iter_res = 0.0;
        solver_par->runtime = 0.0;
        goto cleanup;
    }

    // t = 0
    // make t twice as large to contain both, dt and dr
    ldd = magma_roundup( b.num_rows, 32 );
    CHECK( magma_svinit( &dt, Magma_DEV, ldd, 2 * b.num_cols, c_zero, queue ));
    gpumem += dt.nnz * sizeof(float);
    dt.num_rows = b.num_rows;
    dt.num_cols = b.num_cols;
    dt.nnz = dt.num_rows * dt.num_cols;
    doft = dt.ld * dt.num_cols;

    // redirect the dr.dval to the second part of dt
    CHECK( magma_svinit( &dr, Magma_DEV, b.num_rows, b.num_cols, c_zero, queue ));
    magma_free( dr.dval );
    dr.dval = dt.dval + ldd * b.num_cols;

    // r = b - A x
    CHECK( magma_sresidualvec( A, b, *x, &dr, &nrmr, queue ));
    printMatrix("R", dr);
    
    // |r|
    solver_par->init_res = nrmr;
    if ( solver_par->verbose > 0 ) {
        solver_par->res_vec[0] = (real_Double_t)nrmr;
    }

    // check if initial is guess good enough
    if ( nrmr <= solver_par->atol ||
        nrmr/nrmb <= solver_par->rtol ) {
        solver_par->final_res = solver_par->init_res;
        solver_par->iter_res = solver_par->init_res;
        goto cleanup;
    }

    // P = randn(n, s)
    // P = ortho(P)
//---------------------------------------
    // P = 0.0
    CHECK( magma_svinit( &dP, Magma_CPU, A.num_cols, s, c_zero, queue ));

    // P = randn(n, s)
    distr = 3;        // 1 = unif (0,1), 2 = unif (-1,1), 3 = normal (0,1) 
    dofP = dP.num_rows * dP.num_cols;
    lapackf77_slarnv( &distr, iseed, &dofP, dP.val );
    printMatrix("P1", dP);

    // transfer P to device
    CHECK( magma_smtransfer( dP, &dP1, Magma_CPU, Magma_DEV, queue ));
    dP1.major = dP.major;
    magma_smfree( &dP, queue );

    // P = ortho(P1)
    if ( dP1.num_cols > 1 ) {
        // P = magma_sqr(P1), QR factorization
        CHECK( magma_sqr( dP1.num_rows, dP1.num_cols, dP1, dP1.ld, &dP, NULL, queue ));
    } else {
        // P = P1 / |P1|
        nrm = magma_snrm2( dofP, dP1.dval, inc );
        nrm = 1.0 / nrm;
        magma_sscal( dofP, nrm, dP1.dval, 1 );
        CHECK( magma_smtransfer( dP1, &dP, Magma_DEV, Magma_DEV, queue ));
        dP.major = dP1.major;
    }
    magma_smfree(&dP1, queue );
//---------------------------------------
    printMatrix("P", dP);
    gpumem += dP.nnz * sizeof(float);

#if MYDEBUG == -1
    // Note: write P matrix to file to use in MATLAB for validation
    printf("P = ");
    magma_sprint_gpu( dP.num_rows, dP.num_cols, dP.dval, dP.ld );
#endif

    // allocate memory for the scalar products
    CHECK( magma_svinit( &skp, Magma_CPU, 4, 1, c_zero, queue ));
    CHECK( magma_svinit( &dskp, Magma_DEV, 4, 1, c_zero, queue ));
    gpumem += dskp.nnz * sizeof(float);

    CHECK( magma_svinit( &alpha, Magma_CPU, s, 1, c_zero, queue ));
    CHECK( magma_svinit( &dalpha, Magma_DEV, s, 1, c_zero, queue ));
    gpumem += dalpha.nnz * sizeof(float);

    CHECK( magma_svinit( &beta, Magma_CPU, s, 1, c_zero, queue ));
    CHECK( magma_svinit( &dbeta, Magma_DEV, s, 1, c_zero, queue ));
    gpumem += dbeta.nnz * sizeof(float);
    
    // workspace for merged dot product
    CHECK( magma_smalloc( &d1, 2 * dofb ));
    CHECK( magma_smalloc( &d2, 2 * dofb ));
    gpumem += 4 * dofb * sizeof(float);

    if ( smoothing > 0 ) {
        // set smoothing solution vector
        CHECK( magma_smtransfer( *x, &dxs, Magma_DEV, Magma_DEV, queue ));
        dxs.major = x->major;
        gpumem += dxs.nnz * sizeof(float);

        // tt = 0
        // make tt twice as large to contain both, dtt and drs
        ldd = magma_roundup( b.num_rows, 32 );
        CHECK( magma_svinit( &dtt, Magma_DEV, ldd, 2 * b.num_cols, c_zero, queue ));
        gpumem += dtt.nnz * sizeof(float);
        dtt.num_rows = b.num_rows;
        dtt.num_cols = b.num_cols;
        dtt.nnz = dtt.num_rows * dtt.num_cols;

        // redirect the drs.dval to the second part of dtt
        CHECK( magma_svinit( &drs, Magma_DEV, b.num_rows, b.num_cols, c_zero, queue ));
        magma_free( drs.dval );
        drs.dval = dtt.dval + ldd * b.num_cols;

        // set smoothing residual vector
        magma_scopy( dofr, dr.dval, 1, drs.dval, 1 );
    }

    // G(n,s) = 0
    if ( s > 1 ) {
        ldd = magma_roundup( A.num_rows, 32 );
        CHECK( magma_svinit( &dG, Magma_DEV, ldd, s, c_zero, queue ));
        dG.num_rows = A.num_rows;
    } else {
        CHECK( magma_svinit( &dG, Magma_DEV, A.num_rows, s, c_zero, queue ));
    }
    gpumem += dG.nnz * sizeof(float);

    // U(n,s) = 0
    if ( s > 1 ) {
        ldd = magma_roundup( A.num_cols, 32 );
        CHECK( magma_svinit( &dU, Magma_DEV, ldd, s, c_zero, queue ));
        dU.num_rows = A.num_cols;
    } else {
        CHECK( magma_svinit( &dU, Magma_DEV, A.num_cols, s, c_zero, queue ));
    }
    gpumem += dU.nnz * sizeof(float);

    // M1 = 0
    // M(s,s) = I
    CHECK( magma_svinit( &dM1, Magma_DEV, s, s, c_zero, queue ));
    CHECK( magma_svinit( &dM, Magma_DEV, s, s, c_zero, queue ));
    dofM = dM.num_rows * dM.num_cols;
    magmablas_slaset( MagmaFull, dM.num_rows, dM.num_cols, c_zero, c_one, dM.dval, dM.ld );
    gpumem += (dM1.nnz + dM.nnz) * sizeof(float);

    // f = 0
    CHECK( magma_svinit( &df, Magma_DEV, dP.num_cols, dr.num_cols, c_zero, queue ));
    gpumem += df.nnz * sizeof(float);

    // c = 0
    CHECK( magma_svinit( &dc, Magma_DEV, dM.num_cols, dr.num_cols, c_zero, queue ));
    gpumem += dc.nnz * sizeof(float);

    // v1 = 0
    // v = 0
    CHECK( magma_svinit( &dv1, Magma_DEV, dr.num_rows, dr.num_cols, c_zero, queue ));
    CHECK( magma_svinit( &dv, Magma_DEV, dr.num_rows, dr.num_cols, c_zero, queue ));
    gpumem += (dv1.nnz + dv.nnz) * sizeof(float);

    // piv = 0
    CHECK( magma_imalloc_pinned( &piv, s ));
    
    //--------------START TIME---------------
    // chronometry
    tempo1 = magma_sync_wtime( queue );
    if ( solver_par->verbose > 0 ) {
        solver_par->timing[0] = 0.0;
    }
    
cudaProfilerStart();

    om = MAGMA_S_ONE;
    innerflag = 0;
    solver_par->numiter = 0;

    // start iteration
    do
    {
        solver_par->numiter++;
    
        // new RHS for small systems
        // f = (r' P)' = P' r
        magmablas_sgemv( MagmaConjTrans, dP.num_rows, dP.num_cols, c_one, dP.dval, dP.ld, dr.dval, 1, c_zero, df.dval, 1 );
        printMatrix("F", df);

        // shadow space loop
        for ( k = 0; k < s; ++k ) {
            sk = s - k;
    
            // solve small system and make v orthogonal to P
            // f(k:s) = M(k:s,k:s) c(k:s)
//---------------------------------------
            // c(k:s) = f(k:s)
            magma_scopy( sk, &df.dval[k], 1, &dc.dval[k], 1 );

            // M1 = M
            magma_scopy( dofM, dM.dval, 1, dM1.dval, 1 );

            // c(k:s) = M1(k:s,k:s) \ c(k:s)
            CHECK( magma_sgesv_gpu( sk, dc.num_cols, &dM1.dval[k*dM1.ld+k], dM1.ld, piv, &dc.dval[k], dc.num_rows, &info ));
//---------------------------------------
            printMatrix("C", dc);

            // v1 = r - G(:,k:s) c(k:s)
//---------------------------------------
            // v1 = r
            magma_scopy( dofr, dr.dval, 1, dv1.dval, 1 );

            // v1 = v1 - G(:,k:s) c(k:s)
            magmablas_sgemv( MagmaNoTrans, dG.num_rows, sk, c_n_one, &dG.dval[k*dG.ld], dG.ld, &dc.dval[k], 1, c_one, dv1.dval, 1 );
//---------------------------------------
            printMatrix("V", dv1);

            // compute new U
            // U(:,k) = om * v1 + U(:,k:s) c(k:s)
//---------------------------------------
            // v1 = om * v1 + U(:,k:s) c(k:s)
            magmablas_sgemv( MagmaNoTrans, dU.num_rows, sk, c_one, &dU.dval[k*dU.ld], dU.ld, &dc.dval[k], 1, om, dv1.dval, 1 );

            // U(:,k) = v1
            magma_scopy( dU.num_rows, dv1.dval, 1, &dU.dval[k*dU.ld], 1 );
//---------------------------------------
            printMatrix("U", dU);

            // compute new G
            // G(:,k) = A U(:,k)
//---------------------------------------
            // v = A v1
            CHECK( magma_s_spmv( c_one, A, dv1, c_zero, dv, queue ));

            // G(:,k) = v
            magma_scopy( dG.num_rows, dv.dval, 1, &dG.dval[k*dG.ld], 1 );
//---------------------------------------
            printMatrix("G", dG);

            // bi-orthogonalize the new basis vectors
            for ( i = 0; i < k; ++i ) {
                // alpha = P(:,i)' G(:,k) / M(i,i)
//---------------------------------------
                // alpha = P(:,i)' G(:,k)
                CHECK( magma_smdotc( dP.num_rows, 1, &dP.dval[i*dP.ld], &dG.dval[k*dG.ld], d1, d2, (dalpha.dval)+i, queue ));
                magma_sgetvector( 1, (dalpha.dval)+i, 1, (alpha.val)+i, 1 );
                
                // alpha = alpha / M(i,i)
                magma_sgetvector( 1, &dM.dval[i*dM.ld+i], 1, &mkk, 1 );
                alpha.val[i] = alpha.val[i] / mkk;
                
//---------------------------------------
                printD("bi-ortho: i, k, alpha ...................%d, %d, (%lg, %lg)\n", i, k, MAGMA_S_REAL(alpha.val[i]), MAGMA_S_IMAG(alpha.val[i]));

                // G(:,k) = G(:,k) - alpha * G(:,i)
                magma_saxpy( dG.num_rows, -alpha.val[i], &dG.dval[i*dG.ld], 1, &dG.dval[k*dG.ld], 1 );
                printMatrix("G", dG);

                // U(:,k) = U(:,k) - alpha * U(:,i)
                // take this out of the loop
            }

            if ( k > 0 ) {
                // U(:,k) = U(:,k) - alpha * U(:,i) outside the loop using GEMV
                // copy scalars alpha needed for gemv to device
                magma_ssetvector( k, alpha.val, 1, dalpha.dval, 1 );
                magmablas_sgemv( MagmaNoTrans, dU.num_rows, k, c_n_one, &dU.dval[0], dU.ld, &dalpha.dval[0], 1, c_one, &dU.dval[k*dU.ld], 1 );
                printMatrix("U", dU);
            }
            
            // new column of M = P'G, first k-1 entries are zero
            // M(k:s,k) = (G(:,k)' P(:,k:s))' = P(:,k:s)' G(:,k)
            magmablas_sgemv( MagmaConjTrans, dP.num_rows, sk, c_one, &dP.dval[k*dP.ld], dP.ld, &dG.dval[k*dG.ld], 1, c_zero, &dM.dval[k*dM.ld+k], 1 );
            printMatrix("M", dM);

            // check M(k,k) == 0
            magma_sgetvector( 1, &dM.dval[k*dM.ld+k], 1, &mkk, 1 );
            if ( MAGMA_S_EQUAL(mkk, MAGMA_S_ZERO) ) {
                s = k; // for the x-update outside the loop
                info = MAGMA_DIVERGENCE;
                innerflag = 1;
                break;
            }

            // beta = f(k) / M(k,k)
            magma_sgetvector( 1, &df.dval[k], 1, &fk, 1 );
            beta.val[k] = fk / mkk;
            printD("beta: k ...................%d, (%lg, %lg)\n", k, MAGMA_S_REAL(beta.val[k]), MAGMA_S_IMAG(beta.val[k]));

            // make r orthogonal to q_i, i = 1..k
            // r = r - beta * G(:,k)
            magma_saxpy( dr.num_rows, -beta.val[k], &dG.dval[k*dG.ld], 1, dr.dval, 1 );
            printMatrix("R", dr);

            if ( smoothing < 0 ) {
                // |r|
                nrmr = magma_snrm2( dofb, dr.dval, inc );
                printD("norm(r): k ...................%d, %lg\n", k, nrmr);
            }
            else if ( smoothing == 0 ) {
                // x = x + beta * U(:,k)
                magma_saxpy( x->num_rows, beta.val[k], &dU.dval[k*dU.ld], 1, x->dval, 1 );
                printMatrix("X", *x);

                // |r|
                nrmr = magma_snrm2( dofb, dr.dval, inc );
                printD("norm(r): k ...................%d, %lg\n", k, nrmr);
            } else {
                // x = x + beta * U(:,k)
                magma_saxpy( x->num_rows, beta.val[k], &dU.dval[k*dU.ld], 1, x->dval, 1 );
                printMatrix("X", *x);

                // smoothing operation
//---------------------------------------
                //----MERGED---///
                // t = rs - r
                //magma_scopy( drs.num_rows * drs.num_cols, drs.dval, inc, dt.dval, inc );
                //magma_saxpy( dt.num_rows, c_n_one, dr.dval, inc, dt.dval, inc );
                //----MERGED---///
                magma_sidr_smoothing_1( drs.num_rows, drs.num_cols, drs.dval, dr.dval, dtt.dval, queue );
                /*
                // |t|
                dof = dtt.num_rows * dtt.num_cols;
                nrmt = magma_snrm2( dof, dtt.dval, inc );

                // gamma = t' rs
                gamma = magma_sdot( dtt.num_rows, dtt.dval, inc, drs.dval, inc );

                // gamma = gamma / (|t| * |t|)
                gamma = gamma / (nrmt * nrmt);
                */
                // merge into mdot
                //CHECK( magma_smdotc( dofr, 2, dtt.dval, dtt.dval, d1, d2, dskp.dval+2, queue ));
                CHECK( magma_smdotc( doft, 2, dtt.dval, dtt.dval, d1, d2, dskp.dval+2, queue ));
                magma_sgetvector( 2 , dskp.dval+2, 1, skp.val+2, 1 );
                // |t|
                //nrmt = sqrt( MAGMA_S_REAL ( skp.val[2] ) );
                // gamma = gamma / (|t| * |t|)
                gamma = skp.val[3] / skp.val[2];
                
                // rs = rs - gamma * t
                magma_saxpy( drs.num_rows, -gamma, dtt.dval, inc, drs.dval, inc );
                printMatrix("RS", drs);

                //----MERGED---///
                // t = xs - x
                //magma_scopy( dxs.num_rows * dxs.num_cols, dxs.dval, inc, dt.dval, inc );
                //magma_saxpy( dt.num_rows, c_n_one, x->dval, inc, dt.dval, inc );

                // xs = xs - gamma * t
                //magma_saxpy( dxs.num_rows, -gamma, dt.dval, inc, dxs.dval, inc );
                //----MERGED---///
                magma_sidr_smoothing_2( dxs.num_rows, dxs.num_cols, -gamma, x->dval, dxs.dval, queue );
                printMatrix("XS", dxs);

                // |rs|
                nrmr = magma_snrm2( dofb, drs.dval, inc );           
                printD("norm(rs): k ...................%d, %lg\n", k, nrmr);
//---------------------------------------
            }

            // store current timing and residual
            if ( solver_par->verbose > 0 ) {
                tempo2 = magma_sync_wtime( queue );
                if ( (solver_par->numiter)%solver_par->verbose==0 ) {
                    solver_par->res_vec[(solver_par->numiter)/solver_par->verbose]
                            = (real_Double_t) nrmr;
                    solver_par->timing[(solver_par->numiter)/solver_par->verbose]
                            = (real_Double_t) tempo2-tempo1;
                }
            }

            // check convergence or iteration limit
            if ( nrmr <= solver_par->atol ||
                nrmr/nrmb <= solver_par->rtol || 
                solver_par->numiter >= solver_par->maxiter ) {
                s = k; // for the x-update outside the loop
                innerflag = 1;
                break;
            }

            // new f = P' r (first k components are zero)
            if ( (k + 1) < s ) {
                // f(k+1:s) = f(k+1:s) - beta * M(k+1:s,k)
                magma_saxpy( sk - 1, -beta.val[k], &dM.dval[k*dM.ld+(k+1)], 1, &df.dval[k+1], 1 );
                printMatrix("F", df);
            }

            // iter = iter + 1
            solver_par->numiter++;
        }

        // update solution approximation x
        if ( smoothing < 0 ) {
            magma_ssetvector( s, beta.val, 1, dbeta.dval, 1 );
            magmablas_sgemv( MagmaNoTrans, dU.num_rows, s, c_one, &dU.dval[0], dU.ld, &dbeta.dval[0], 1, c_one, &x->dval[0], 1 );
            printMatrix("X", *x);
        }
 
        // check convergence or iteration limit or invalid of inner loop
        if ( innerflag == 1 ) {
            break;
        }

        // v = r
        magma_scopy( dr.num_rows * dr.num_cols, dr.dval, 1, dv.dval, 1 );

        // t = A v
        CHECK( magma_s_spmv( c_one, A, dv, c_zero, dt, queue ));
        printMatrix("T", dt);

        // computation of a new omega
        // om = omega(t, r, angle);
//---------------------------------------
        // |t|
        //CHECK( magma_smdotc( dofr, 2, dt.dval, dt.dval, d1, d2, dskp.dval, queue ));
        CHECK( magma_smdotc( doft, 2, dt.dval, dt.dval, d1, d2, dskp.dval, queue ));
        magma_sgetvector( 2 , dskp.dval, 1, skp.val, 1 );
        nrmt = sqrt(MAGMA_S_REAL(skp.val[0]));

        // tr = t' r
        tr = skp.val[1];
        printD("tr, norm(t): k .................... %d, (%lg, %lg), %lg\n", k, MAGMA_S_REAL(tr), MAGMA_S_IMAG(tr), nrmt);
        
        // rho = abs(tr / (|t| * |r|))
        rho = fabs( MAGMA_S_REAL(tr) / (nrmt * nrmr) );

        // om = tr / (|t| * |t|)
        om = tr / (nrmt * nrmt);
        if ( rho < angle )
            om = om * (angle / rho);
//---------------------------------------
        printD("omega: k .................... %d, (%lg, %lg)\n", k, MAGMA_S_REAL(om), MAGMA_S_IMAG(om));
        if ( MAGMA_S_EQUAL(om, MAGMA_S_ZERO) ) {
            info = MAGMA_DIVERGENCE;
            break;
        }

        // update approximation vector
        // x = x + om * v
        magma_saxpy(x->num_rows, om, dv.dval, 1, x->dval, 1);
        printMatrix("X", *x);

        // update residual vector
        // r = r - om * t
        magma_saxpy(dr.num_rows, -om, dt.dval, 1, dr.dval, 1);
        printMatrix("R", dr);

        if ( smoothing < 1 ) {
            // residual norm
            nrmr = magma_snrm2( dofb, dr.dval, inc );
            printD("norm(r): k ...................%d, %lg\n", k, nrmr);
        }
        else {
            // smoothing operation
//---------------------------------------
            //----MERGED---///
            // t = rs - r
            //magma_scopy( drs.num_rows * drs.num_cols, drs.dval, inc, dt.dval, inc );
            //magma_saxpy( dt.num_rows, c_n_one, dr.dval, inc, dt.dval, inc );
            //----MERGED---///
            magma_sidr_smoothing_1( drs.num_rows, drs.num_cols, drs.dval, dr.dval, dtt.dval, queue );
            /*
            // |t|
            dof = dt.num_rows * dt.num_cols;
            nrmt = magma_snrm2( dof, dtt.dval, inc );

            // gamma = t' rs
            gamma = magma_sdot( dtt.num_rows, dtt.dval, inc, drs.dval, inc );

            // gamma = gamma / (|t| * |t|)
            gamma = gamma / (nrmt * nrmt);
            */
            // merge into mdot
            //CHECK( magma_smdotc( dofr, 2, dtt.dval, dtt.dval, d1, d2, dskp.dval+2, queue ));
            CHECK( magma_smdotc( doft, 2, dtt.dval, dtt.dval, d1, d2, dskp.dval+2, queue ));
            magma_sgetvector( 2 , dskp.dval+2, 1, skp.val+2, 1 );
            // |t|
            //nrmt = sqrt( MAGMA_S_REAL( skp.val[2] ) );
            // gamma = gamma / (|t| * |t|)
            gamma = skp.val[3] / skp.val[2];

            // rs = rs - gamma * t
            magma_saxpy( drs.num_rows, -gamma, dtt.dval, inc, drs.dval, inc );
            printMatrix("RS", drs);

            //----MERGED---///
            // t = xs - x
            //magma_scopy( dxs.num_rows * dxs.num_cols, dxs.dval, inc, dt.dval, inc );
            //magma_saxpy( dt.num_rows, c_n_one, x->dval, inc, dt.dval, inc );

            // xs = xs - gamma * t
            //magma_saxpy( dxs.num_rows, -gamma, dt.dval, inc, dxs.dval, inc );
            //----MERGED---///
            magma_sidr_smoothing_2( dxs.num_rows, dxs.num_cols, -gamma, x->dval, dxs.dval, queue );
            printMatrix("XS", dxs);

            // |rs|
            nrmr = magma_snrm2( dofb, drs.dval, inc );           
            printD("norm(rs): k ...................%d, %lg\n", k, nrmr);
        }
//---------------------------------------

        // store current timing and residual
        if ( solver_par->verbose > 0 ) {
            tempo2 = magma_sync_wtime( queue );
            if ( (solver_par->numiter)%solver_par->verbose==0 ) {
                solver_par->res_vec[(solver_par->numiter)/solver_par->verbose]
                        = (real_Double_t) nrmr;
                solver_par->timing[(solver_par->numiter)/solver_par->verbose]
                        = (real_Double_t) tempo2-tempo1;
            }
        }

        // check convergence or iteration limit
        if ( nrmr <= solver_par->atol ||
            nrmr/nrmb <= solver_par->rtol || 
            solver_par->numiter >= solver_par->maxiter ) {
            break;
        }

#if MYDEBUG == 2
        // Note: exit loop after a few iterations
        if ( solver_par->numiter + 1 >= (2 * (s + 1)) ) {
            break;
        }
#endif
    }
    while ( solver_par->numiter + 1 <= solver_par->maxiter );
            
    if ( smoothing > 0 ) {
        magma_scopy( dofr, drs.dval, 1, dr.dval, 1 );
        magma_scopy( dofx, dxs.dval, 1, x->dval, 1 );
    }

cudaProfilerStop();

    // get last iteration timing
    tempo2 = magma_sync_wtime( queue );
    solver_par->runtime = (real_Double_t) tempo2-tempo1;
//--------------STOP TIME----------------

    // last stats
    printMatrix("B_last", b);
    printMatrix("X_last", *x);
    printMatrix("R_last", dr);
    printD("last norm(r): ................. %lg\n", nrmr);
    solver_par->iter_res = nrmr;
    CHECK( magma_sresidualvec( A, b, *x, &dr, &residual, queue ));
    solver_par->final_res = residual;
    printD("last residual: ................. %lg\n", residual);

    // set solver conclusion
    if ( info != MAGMA_SUCCESS ) {
        if ( solver_par->init_res > solver_par->final_res ) {
            info = MAGMA_SLOW_CONVERGENCE;
        }
        else {
            info = MAGMA_DIVERGENCE;
        }
    }
//---------------------------------------

    // print local stats
    printD("GPU memory = %f MB\n", (real_Double_t)gpumem / (1<<20));
    
cleanup:
    // free resources
    dr.dval = NULL;   // needed because its pointer is redirected to dt
    drs.dval = NULL;  // needed because its pointer is redirected to dtt
    magma_smfree( &dP1, queue );
    magma_smfree( &dP, queue );
    magma_smfree( &dr, queue );
    magma_smfree( &dG, queue );
    magma_smfree( &dU, queue );
    magma_smfree( &dM1, queue );
    magma_smfree( &dM, queue );
    magma_smfree( &df, queue );
    magma_smfree( &dt, queue );
    magma_smfree( &dc, queue );
    magma_smfree( &dv1, queue );
    magma_smfree( &dv, queue );
    magma_smfree( &dxs, queue );
    magma_smfree( &drs, queue ); 
    magma_smfree( &dtt, queue );
    magma_smfree( &dskp, queue );
    magma_smfree( &dalpha, queue );
    magma_smfree( &dbeta, queue );
    magma_smfree( &skp, queue );
    magma_smfree( &alpha, queue );
    magma_smfree( &beta, queue );
    magma_free_pinned( piv );
    magma_free( d1 );
    magma_free( d2 );

    magmablasSetKernelStream( orig_queue );
    solver_par->info = info;
    return info;
    /* magma_sidr_acc */
}
