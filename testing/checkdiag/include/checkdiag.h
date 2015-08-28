/*
 * HEADERS 
 */

#if defined(_MSC_VER)
# pragma once
#endif
#if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 3))
# pragma once
#endif

#ifndef CHECKDIAG_H
#define CHECKDIAG_H



#if defined(_WIN32) || defined(__hpux)
#define FORTRAN_WRAPPER(x) x
#else
#define FORTRAN_WRAPPER(x) x ## _
#endif

#ifdef __cplusplus
    extern "C" {
#endif

/* Source: dcheck_eig.f */
#define dcheck_eig FORTRAN_WRAPPER(dcheck_eig)
void dcheck_eig(
    char *JOBZ, magma_int_t  *MATYPE, magma_int_t  *N, magma_int_t  *NB,
    double* A, magma_int_t  *LDA,
    double *AD, double *AE, double *D1, double *EIG,
    double *Z, magma_int_t  *LDZ,
    double *WORK, double *RWORK, double *RESU);

#ifdef __cplusplus
    }   /* extern "C" */
#endif

#endif /* CHECKDIAG_H */
