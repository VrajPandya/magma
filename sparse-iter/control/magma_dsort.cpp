/*
    -- MAGMA (version 2.1.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date August 2016

       @generated from sparse-iter/control/magma_zsort.cpp, normal z -> d, Tue Aug 30 09:38:50 2016
       @author Hartwig Anzt
*/

//  in this file, many routines are taken from
//  the IO functions provided by MatrixMarket

#include "magmasparse_internal.h"


#define SWAP(a, b)  { tmp = val[a]; val[a] = val[b]; val[b] = tmp; }
#define SWAPM(a, b) { tmpv = val[a]; val[a] = val[b]; val[b] = tmpv;  \
                      tmpc = col[a]; col[a] = col[b]; col[b] = tmpc;  \
                      tmpr = row[a]; row[a] = row[b]; row[b] = tmpr; }

/**
    Purpose
    -------

    Sorts an array of values in increasing order.

    Arguments
    ---------

    @param[in,out]
    x           double*
                array to sort

    @param[in]
    first       magma_int_t
                pointer to first element

    @param[in]
    last        magma_int_t
                pointer to last element

    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_daux
    ********************************************************************/

extern "C"
magma_int_t
magma_dsort(
    double *x,
    magma_int_t first,
    magma_int_t last,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    
    double temp;
    magma_index_t pivot,j,i;
    
    if(first<last){
         pivot=first;
         i=first;
         j=last;

        while(i<j){
            while( MAGMA_D_ABS(x[i]) <= MAGMA_D_ABS(x[pivot]) && i<last )
                i++;
            while( MAGMA_D_ABS(x[j]) > MAGMA_D_ABS(x[pivot]) )
                j--;
            if( i<j ){
                temp = x[i];
                x[i] = x[j];
                x[j] = temp;
            }
        }

        temp=x[pivot];
        x[pivot]=x[j];
        x[j]=temp;
        CHECK( magma_dsort( x, first, j-1, queue ));
        CHECK( magma_dsort( x, j+1, last, queue ));
    }
cleanup:
    return info;
}


/**
    Purpose
    -------

    Sorts an array of values in increasing order.

    Arguments
    ---------

    @param[in,out]
    x           double*
                array to sort
                
    @param[in,out]
    col         magma_index_t*
                Target array, will be modified during operation.
                
    @param[in,out]
    row         magma_index_t*
                Target array, will be modified during operation.

    @param[in]
    first       magma_int_t
                pointer to first element

    @param[in]
    last        magma_int_t
                pointer to last element

    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_daux
    ********************************************************************/

extern "C"
magma_int_t
magma_dmsort(
    double *x,
    magma_index_t *col,
    magma_index_t *row,
    magma_int_t first,
    magma_int_t last,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    
    double temp;
    magma_index_t pivot,j,i, tmpcol, tmprow;
    
    if(first<last){
         pivot=first;
         i=first;
         j=last;

        while(i<j){
            while( MAGMA_D_ABS(x[i]) <= MAGMA_D_ABS(x[pivot]) && i<last )
                i++;
            while( MAGMA_D_ABS(x[j]) > MAGMA_D_ABS(x[pivot]) )
                j--;
            if( i<j ){
                temp = x[i];
                x[i] = x[j];
                x[j] = temp;
                tmpcol = col[i];
                col[i] = col[j];
                col[j] = tmpcol;
                tmprow = row[i];
                row[i] = row[j];
                row[j] = tmprow;
            }
        }

        temp=x[pivot];
        x[pivot]=x[j];
        x[j]=temp;
        CHECK( magma_dmsort( x, col, row, first, j-1, queue ));
        CHECK( magma_dmsort( x, col, row, j+1, last, queue ));
    }
cleanup:
    return info;
}


/**
    Purpose
    -------

    Sorts an array of integers in increasing order.

    Arguments
    ---------

    @param[in,out]
    x           magma_index_t*
                array to sort

    @param[in]
    first       magma_int_t
                pointer to first element

    @param[in]
    last        magma_int_t
                pointer to last element

    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_daux
    ********************************************************************/

extern "C"
magma_int_t
magma_dindexsort(
    magma_index_t *x,
    magma_int_t first,
    magma_int_t last,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    
    magma_index_t pivot,j,temp,i;

    if(first<last){
         pivot=first;
         i=first;
         j=last;

        while(i<j){
            while( x[i]<=x[pivot] && i<last )
                i++;
            while( x[j]>x[pivot] )
                j--;
            if( i<j ){
                temp = x[i];
                x[i] = x[j];
                x[j] = temp;
            }
        }

        temp=x[pivot];
        x[pivot]=x[j];
        x[j]=temp;
        CHECK( magma_dindexsort( x, first, j-1, queue ));
        CHECK( magma_dindexsort( x, j+1, last, queue ));
    }
cleanup:
    return info;
}


/**
    Purpose
    -------

    Sorts an array of integers, updates a respective array of values.

    Arguments
    ---------

    @param[in,out]
    x           magma_index_t*
                array to sort
                
    @param[in,out]
    y           double*
                array to sort

    @param[in]
    first       magma_int_t
                pointer to first element

    @param[in]
    last        magma_int_t
                pointer to last element

    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_daux
    ********************************************************************/

extern "C"
magma_int_t
magma_dindexsortval(
    magma_index_t *x,
    double *y,
    magma_int_t first,
    magma_int_t last,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    
    magma_index_t pivot,j,temp,i;
    double tempval;

    if(first<last){
         pivot=first;
         i=first;
         j=last;

        while(i<j){
            while( x[i]<=x[pivot] && i<last )
                i++;
            while( x[j]>x[pivot] )
                j--;
            if( i<j ){
                temp = x[i];
                x[i] = x[j];
                x[j] = temp;
                tempval = y[i];
                y[i] = y[j];
                y[j] = tempval;
            }
        }

        temp=x[pivot];
        x[pivot]=x[j];
        x[j]=temp;
        
        tempval=y[pivot];
        y[pivot]=y[j];
        y[j]=tempval;
        CHECK( magma_dindexsortval( x, y, first, j-1, queue ));
        CHECK( magma_dindexsortval( x, y, j+1, last, queue ));
    }
cleanup:
    return info;
}



/**
    Purpose
    -------

    Identifies the kth smallest/largest element in an array and reorders 
    such that these elements come to the front. The related arrays col and row
    are also reordered.

    Arguments
    ---------
    
    @param[in,out]
    val         double*
                Target array, will be modified during operation.
                
    @param[in,out]
    col         magma_index_t*
                Target array, will be modified during operation.
                
    @param[in,out]
    row         magma_index_t*
                Target array, will be modified during operation.

    @param[in]
    length      magma_int_t
                Length of the target array.

    @param[in]
    k           magma_int_t
                Element to be identified (largest/smallest).
                
    @param[in]
    r           magma_int_t
                rule how to sort: '1' -> largest, '0' -> smallest
                
    @param[out]
    element     double*
                location of the respective element
                
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_daux
    ********************************************************************/

extern "C"
magma_int_t
magma_dmorderstatistics(
    double *val,
    magma_index_t *col,
    magma_index_t *row,
    magma_int_t length,
    magma_int_t k,
    magma_int_t r,
    double *element,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    magma_int_t i, st;
    double tmpv;
    magma_index_t tmpc, tmpr;
    if( r == 0 ){
        for ( st = i = 0; i < length - 1; i++ ) {
            if ( magma_d_isnan_inf( val[i]) ) {
                printf("%% error: array contains %f + %fi.\n", MAGMA_D_REAL(val[i]), MAGMA_D_IMAG(val[i]) );
                info = MAGMA_ERR_NAN;
                goto cleanup;
            }
            if ( MAGMA_D_ABS(val[i]) > MAGMA_D_ABS(val[length-1]) ){
                continue;
            }
            SWAPM(i, st);
            st++;
        }
     
        SWAPM(length-1, st);
     
        if ( k == st ){
            *element = val[st];    
        }
        else if ( st > k ) {
            CHECK( magma_dmorderstatistics( val, col, row, st, k, r, element, queue ));
        }
        else {
             CHECK( magma_dmorderstatistics( val+st, col+st, row+st, length-st, k-st, r, element, queue ));
        }
    } else {
        for ( st = i = 0; i < length - 1; i++ ) {
            if ( magma_d_isnan_inf( val[i]) ) {
                printf("%% error: array contains %f + %fi.\n", MAGMA_D_REAL(val[i]), MAGMA_D_IMAG(val[i]) );
                info = MAGMA_ERR_NAN;
                goto cleanup;
            }
            if ( MAGMA_D_ABS(val[i]) < MAGMA_D_ABS(val[length-1]) ){
                continue;
            }
            SWAPM(i, st);
            st++;
        }
     
        SWAPM(length-1, st);
     
        if ( k == st ){
            *element = val[st];    
        }
        else if ( st > k ) {
            CHECK( magma_dmorderstatistics( val, col, row, st, k, r, element, queue ));
        }
        else {
            CHECK( magma_dmorderstatistics( val+st, col+st, row+st, length-st, k-st, r, element, queue ));
        }
    }
    
cleanup:
    return info;
}



/**
    Purpose
    -------

    Identifies the kth smallest/largest element in an array.

    Arguments
    ---------
    
    @param[in,out]
    val         double*
                Target array, will be modified during operation.

    @param[in]
    length      magma_int_t
                Length of the target array.

    @param[in]
    k           magma_int_t
                Element to be identified (largest/smallest).
                
    @param[in]
    r           magma_int_t
                rule how to sort: '1' -> largest, '0' -> smallest
                
    @param[out]
    element     double*
                location of the respective element
                
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_daux
    ********************************************************************/

extern "C"
magma_int_t
magma_dorderstatistics(
    double *val,
    magma_int_t length,
    magma_int_t k,
    magma_int_t r,
    double *element,
    magma_queue_t queue )
{
    magma_int_t info = 0;

    magma_int_t i, st;
    double tmp;
    if( r == 0 ){
        for ( st = i = 0; i < length - 1; i++ ) {
            if ( magma_d_isnan_inf( val[i]) ) {
                printf("%%error: array contains %f + %fi.\n", MAGMA_D_REAL(val[i]), MAGMA_D_IMAG(val[i]) );
                info = MAGMA_ERR_NAN;
                goto cleanup;
            }
            if ( MAGMA_D_ABS(val[i]) > MAGMA_D_ABS(val[length-1]) ){
                continue;
            }
            SWAP(i, st);
            st++;
        }
     
        SWAP(length-1, st);
     
        if ( k == st ){
            *element = val[st];    
        }
        else if ( st > k ) {
            CHECK( magma_dorderstatistics( val, st, k, r, element, queue ));
        }
        else {
             CHECK( magma_dorderstatistics( val+st, length-st, k-st, r, element, queue ));
        }
    } else {
        for ( st = i = 0; i < length - 1; i++ ) {
            if ( magma_d_isnan_inf( val[i]) ) {
                printf("%%error: array contains %f + %fi.\n", MAGMA_D_REAL(val[i]), MAGMA_D_IMAG(val[i]) );
                info = MAGMA_ERR_NAN;
                goto cleanup;
            }
            if ( MAGMA_D_ABS(val[i]) < MAGMA_D_ABS(val[length-1]) ){
                continue;
            }
            SWAP(i, st);
            st++;
        }
     
        SWAP(length-1, st);
     
        if ( k == st ){
            *element = val[st];    
        }
        else if ( st > k ) {
            CHECK( magma_dorderstatistics( val, st, k, r, element, queue ));
        }
        else {
             CHECK( magma_dorderstatistics( val+st, length-st, k-st, r, element, queue ));
        }
    }
    
cleanup:
    return info;
}
