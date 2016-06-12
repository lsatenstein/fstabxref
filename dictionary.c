/*
 *  Copyright (c) 2000-2007 by Nicolas Devillard.
 * Copyright (c) 2014-2016 by Leslie Satenstein <lsatenstein@yahoo.com>
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*-------------------------------------------------------------------------*/

/**
   @file    dictionary.c
   @author  N. Devillard
            Leslie Satenstein, 2014/07/07 2016/01/03
   @brief   Implements a dictionary for string variables.

   This module implements a simple dictionary object, i.e. a list
   of string/string associations. This object is useful to store e.g.
   information retrieved from a configuration file (ini file).
   A configuration file consists of multiple entries of the type
   key=value

   Minimal allocated number of available entries for the dictionary was 64
   and can be revised by compiler switch cc -DDICTMINSZ=128. If dictionary
   becomes full, the dictionary will self resize by doubling the existing
   number of slots.

   DICTMINSZ parameter was set to 64 as ini files rarely exceeded 40 entries.
   Tests to 10000 entries (with application readjusting dictionary were completed
   Without problems.
   
   2014-2016 Leslie Satenstein
   This version added two fields to the dictionary. A hash column containing a hash of the
   key value. And a provisional section column.

   This addition allows:
       the sort of the dictionary entries into hash ascending sequence
       use a binary search to retrieve an entry.

   Retrieval by binary search of sorted hashtable having 1000 live key
   takes at most 10 probes.  (1000 < 2**10)


   Future..
   (Instead of doubling the size of the space allocated to the
   ini structure, one could also add the minimum DDICTMINSZ. This
   DICTMINSZ (not implemented in this version).)

   NOTE:
   There are two applications vis: A dictionary with key=value  pairs
   and an ini file format.

   Supported is a split definition of sections.
   a section has intervening sections between. For example:
   [a]
   some=stuff
   title="The ability to learn C!!"
   [Another_section]
   some=stuff;
   [a]
   continue=with more stuff
   [b]

   Ini File rules:
   section or key names may not be blank as for example
   []
   =more stuff
   .....
   NEW LOGIC
   ---------
Corrections or additions to dictionary.c
dictionary_realloc() returns a doubled size as if realloc was called
stderror(errno) added for calloc() and malloc().
Dictionary is sorted by hash value to enable binary search access.
Binsearch significantly improves iniparser_getstring and functions using
iniparser getstring. (typically a max of 10 probes for a 1024table entry.
This binary search method beats the sequential search key strings by a large
factor.

dictionary_quicksort() algorithm is used, when some activity throws
the table out of order. It is used to resequence the dictionary.
Many other tweaks, such as using pointers vs indexing.
quick sort The "test/dicttest" program required many passes for an initial sort
        and under 4 passes for subsequent sorts
unsigned integer   introduced as appropriate
dictionary_set will not do free/realloc for redefinition of a field value
                if it fits within the original allocation.
void *dictionary_ffree(void **ptr); to free *ptr and to set ptr to NULL
(simplified some code)
dictionary_media() code to indicate dictionary size, and available slots
Added #ifdef WANT_....  to allow shrinking the code size.
*/

/*---------------------------------------------------------------------------
                                Includes
 ---------------------------------------------------------------------------*/

#include "dictionary.h"
/* included in dictionary.h 
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
*/
/* prototypes are in dictionary.h */
/*(refer to dictionary.h for definition) of HAS_STRDUP */

static void * dictionary_realloc(void * ptr, int size,int bytes);

int main(int , char **);

#ifndef DICTMINSZ
#define DICTMINSZ   64
#endif

/*---------------------------------------------------------------------------
                            Private functions
 ---------------------------------------------------------------------------*/

/* If you are adding more entries than the initial allocated previsions
 * This function is called to double the allocated space
 * Doubles the allocated size associated to a pointer array */
/* 'size' is the current allocated size.                    */
/**
 * @brief dictionary_realloc Increase the allocation of the data dictionary rows
 * @param ptr       the array to be expanded
 * @param size      array's existing size
 * @param bytes     bytes of each entry
 * @return          NULL
 */
static
void * dictionary_realloc(void * ptr, int size,int bytes)
{
    void * newptr ;
    debug("size=%d,bytes=%d oldalloc=%d, newalloc=%d\n",size,bytes,size*bytes,size*bytes+2);

    newptr=calloc(bytes,2*size);		/* one pointer for safety*/
    if(newptr==NULL)
    {
        if ( dictionary_flagstatus(error,testflag))
            fprintf(stderr,"Function %s: calloc (returned NULL) %s\n",
                    __FUNCTION__,(char *) strerror (errno));
        return NULL ;
    }
    if(ptr!=NULL)
    {
        memcpy(newptr+bytes*size, ptr, size*bytes);
        free(ptr);
    }

    return newptr ;
}


/*--------------------------------------------------------------------------*/
/* worker function for dictionary_quicksort  swap left with right 			    */
static inline void doSwap( register dictionary * restrict d, register int i, register int j)
{

    HASH_t   tmp;
    char    *uval;

    tmp=d->hash[i];
    d->hash[i]= d->hash[j];
    d->hash[j] = tmp;

    uval=d->val[i];
    d->val[i]=d->val[j];
    d->val[j]=uval;

    uval=d->key[i];
    d->key[i]=d->key[j];
    d->key[j]=uval;

}
#ifdef WANT_DICTIONARY_SHOW
void dictionary_show(dictionary * restrict d,int index,FILE *f)
{
    if(d==NULL)
    {
        fprintf(f,"Dictionary not defined\n");
    }
    else
    {
        if((index<0) || (index >=d->size))
            fprintf(f,"Dictionary Index Error, can't fetch %d\n",index);
        else
            fprintf(f,"Index: %4d hash=%10.8X %s=%s\n",index,d->hash[index],d->key[index],d->val[index]);
    }
}
#endif
#ifdef WANT_INORDER_TEST
#ifdef NDEBUG
static int dictionary_inordertest(dictionary *d,FILE *f)
{
    int i,j;
    if (d->lower<0)
        dictionary_createsortedlist(d);
    j=i=d->size-1;
    while(j>d->lower)
    {
        j=i-1;
        if (d->hash[j]>=d->hash[i])
            break;
        i--;
    }
    if (j!=d->lower)
    {
        fprintf(f,"%s: Problem detected in dictionary\n",__FUNCTION__);
        dictionary_rawdump(d,stdout);
        exit(-1);
    }
    return 0;
}
/*--------------------------------------------------------------------------*/
#endif
#endif
/**
 * @brief dictionary_grow  The double the allocation of the dictionary space
 *                         calls dictionary_realloc() to do the grunt work.
 * @param d                The dictionary
 * @return                 0 on success, -1 on a failure.
 */

static inline int dictionary_grow(dictionary * restrict d)
{
    int i;
    i=d->size;
    if ( dictionary_flagstatus(error,testflag))
        fprintf(stderr,"%s: Doubling dictionary setting sizes from %d to %d ",__FUNCTION__,i,i+i);
    /*dictionary_rawdump(d,stdout); */
    d->val  = (char **)dictionary_realloc(d->val,     i,sizeof(char *)) ;
    d->key  = (char **)dictionary_realloc(d->key,     i,sizeof(char *)) ;
    d->skeys = (HASH_t *)dictionary_realloc(d->skeys, i ,sizeof(HASH_t)) ;		/*list of hashes */
    d->hash = (HASH_t *)dictionary_realloc(d->hash,   i, sizeof(HASH_t)) ;
    if( (d->val==NULL) || (d->key==NULL) || (d->hash==NULL) || (d->skeys==NULL) )
    {
        /* Cannot grow dictionary */
        printf("\nFunction %s: Out of memory\n",__FUNCTION__);
        return -1 ;
    }
    /* size is doubled */
    d->size+=i;
    d->lower+=i-1;
    /*dictionary_createsortedlist(d);
      dictionary_rawdump(d,stdout); */
    if ( dictionary_flagstatus(error,testflag))
        fprintf(stderr,".Dictionary doubling completed successfully\n");
    return 0;
}

/*--------------------------------------------------------------------------*/
static
void dictionary_quicksort(dictionary *d,int first,int last)
{
    int 	pivot;
    register int i,j;
    HASH_t    pivotHash;
    
    if( first<last )
    {
        pivot=first;
        pivotHash=d->hash[pivot];
        i=first;
        j=last;
        while(i<j)
        {
            /* for items less than pivot, move index to right*/
            while(d->hash[i]<= pivotHash && i<last)
                i++;

            /* if the items are greater than pivot
                working from array end
             */
            while(d->hash[j]> pivotHash)
                j--;

            if(i<j)
                doSwap(d,i,j);
        }
        doSwap(d,pivot,j);
        dictionary_quicksort(d,first,j-1);
        dictionary_quicksort(d,j+1,last);

    }
}

#ifdef WANT_DICTIONARY_TRIM
/*---------------------------------------------------------------------------
                            Public (callable) functions
 ---------------------------------------------------------------------------*/
/**
 * @brief dictionary_trim     Trim over allocation of a dictionary
 *                            The input dictionary is modified
 *                            If there were modifications done.
 * @param d                   address of the dictionary pointer
 * @param trim                How much to trim (min of 4 spare)
 * @param verbose             Display details or not to display
 * @return                    Pointer to a dictionary
 */

dictionary * dictionary_trim(dictionary *d,unsigned intrim,FILE * verbose)
{
    dictionary * restrict dn;
    int i,to,from,last,rows;      /*first,last,to,from are index ranges*/
    int  trim=intrim;
    int nsize;

    if(trim<=4)
        trim=4;
    else
    {
    
       trim+=(4-trim%4);           /* round up */
       if(verbose)
           fprintf(verbose,"Trim amount %u adjusted to %u \n",intrim,trim);
    }
    last=d->size-1;                     /* last, from are indexes       */
    from=d->lower+1;
    rows=(last-from) +1;                /* row n to row n = 1 row to move */
    nsize=rows+trim;
    nsize+=nsize%4;

    if( d->lower <= trim )
    {
        if(verbose)
            fprintf(verbose,"Omitting trim function: trim requested %u, already at %u\n",trim,d->lower);
        return d;

    }
    if(verbose)
    {
        fprintf(verbose,"*********** meta list of original dictionary %s ********\n",d->filename);
        //fprintf(verbose,"From\n");
        //dictionary_show(d,from,verbose);
        //fprintf(verbose,"To\n");
        //dictionary_show(d,last,verbose);
        //fprintf(verbose,"Rows=%d\n",rows);
        fprintf(verbose,"\nStats\n");
        dictionary_meta(d,verbose);
           
        //fprintf(verbose,"\nCreating trimmed dictionary with new size set to %d\n",nsize);
    }
    dn=dictionary_new(nsize,d->filename);

    if(verbose)
    {
        fprintf(verbose,"\n*********** meta list of new dictionary  before load ********\n");
        dictionary_meta(dn,verbose);
    }
    dn->n = d->n;
    to=(dn->size)- d->n;
    if(verbose)
    {
        fprintf(verbose,"Copying to row %d from row %d for amount %u rows\n", to,from,rows);
    }
    dn->lower=to-1;

    for(i=0;i<=dn->n;i++,to++,from++)
    {
        dn->key[to]=d->key[from];	/* valgrind ok */	
        dn->val[to]=d->val[from];	/* valgrind ok */
        dn->hash[to]=d->hash[from];     /* valgrind ok */
        dn->skeys[to]=d->skeys[from];	/* valgrind ok */
    }
    to=dn->lower+1;
    from=d->lower+1;
    if(verbose)
    {
        fprintf(verbose,"Raw dump of new \n");
        dictionary_show(dn,dn->lower+1,verbose);
        dictionary_show(dn,dn->size-1,verbose);
        dictionary_meta(dn,verbose);
        dictionary_rawdump(dn,verbose);
        fprintf(verbose,"rows=%d,d->n=%d\n",rows,dn->n);
        /* Free unused (trimmed cells */
        fprintf(verbose,"Performing cleanup of over-allocation\n");
        fprintf(verbose,"Before cleanup Original file name =%s, newfilename=%s\n",d->filename,dn->filename);

        fprintf(verbose,"Unused keys and vals freed\n");
        fprintf(verbose,"After cleanup Original file name =%s, newfilename=%s\n",d->filename,dn->filename);
    }
    d->size=dn->size;
    d->n   = dn->n;
    d->lower=dn->lower;
    d->info=dn->info;

    free(d->key);
    d->key=dn->key;
    
    free(d->val);
    d->val=dn->val;
    
    free(d->filename);
    d->filename=dn->filename;
    
    free(d->hash);
    d->hash=dn->hash;
    
    free(d->skeys);
    d->skeys=dn->skeys;
    
    if(verbose)
    {
        fprintf(verbose,"Trim completed\n");
        fprintf(verbose,"**********dictionary meta after trim *************\n");
        dictionary_meta(dn,verbose);
        dictionary_meta(d,verbose);
        fprintf(verbose,"\n");
    }
    free(dn);
#ifdef NDEBUG
    dictionary_inordertest(d,verbose);
#endif
    return d;
}
#endif

/**
* @brief dictionary_createsortedlist
*        Sort the dictionary into hash key sequence.
*        A sequence and that a binary search for retrieval could produce fetch benefits.
*
*        An extra field was introduced into the dictionary to indicate if a re-sort
*        was necessary or and the location of the last zero entry. (sort is low to high values)
*        Used by iniparser.c
* @param d The dictionary pointer to the dictionary to be sorted.
" @returns void (nothing)
*/
/*--------------------------------------------------------------------------*/
void
dictionary_createsortedlist( dictionary *d)
{
    /* recall, index = 0 .. size-1 readjusted in the do while() */
    /* do the quick sort. at every pass last element is the max value  */
    /* then sort again from max element to beginning after which first element  */
    /* is the lowest. We are squeezing the sort from both ends to eliminate compares*/
    debug("%s: called\n",__FUNCTION__);
    int size=d->size-1;
    if(d==NULL)
    {
        if ( dictionary_flagstatus(error,testflag))
            fprintf(stderr,"%s: Dictionary missing\n",__FUNCTION__);
        return;
    }
    debug("Dictionary %s is sorted\n",d->filename);
    if (d->n > 0)
    {
        dictionary_quicksort(d,0,size);
    }
    /* The test immediately below is to skip past all the early entries with zero to find the
     * lowest real zero entry.  If the d->lower is negative, a re-sort is forced. This can
     * happen if there are deletions or insertions into the dictionary after the
     * initial build.
     */
    register int i;
    for(i=0;i<=d->size;i++)
    {
        if(d->hash[i]!=0)
        {  /* d->lower may equal 0 */
            if(i<1) i=1;
            d->lower = i - 1;
            break;
        }
    }
}
/*--------------------------------------------------------------------------*/
/**
 * @brief dictionary_flagstatus
 *                  initialize, set, unset, test a flag
 *                  When you set a flag to a new value, the
 *                  previous value is returned. Used as
 *                  dictionary_flagstatus((warning|error), clearflag);
 *                  if(dictionary_flagstatus(warning,testflag))
 *                       printf(warning message)
 *                  else
 *                  if (dictionary_flagstatus(error,testflag))
 *                     {do actions for error }
 *
 * @param setpoint the bits in the flag that are set/unset/tested
 * @param option   setflag, unsetflag, testflag,clearflag,dumpflag
 *                 dumpflag returns flag, other options for setting return
 *                 previous_flag.
 */
/*--------------------------------------------------------------------------*/
unsigned
dictionary_flagstatus( unsigned setpoint, enum _printctl_ option)
{
    static enum _printctl_ flag=0;
    enum _printctl_ previous_flag;

    previous_flag=flag;
    switch (option)
    {
    case clearflag:                 /* initialize flag to setpoint value */
        flag=setpoint;              /* used to restore or set flag value */
        break;

    case setflag:                   /* set bit(s) in flag to on  */
        flag|=setpoint;
        break;

    case unsetflag:                 /* unset bit(s) in flag to off */
        flag&=(!setpoint);
        break;

    case testflag:                  /* test flags against setpoint for on status */
        return(flag & setpoint);

    case dumpflag:
        return (flag);

    default:                        /* unknown command do not change flag  */
        return (0xffffffff);
        break;
    }
    return (previous_flag);
}
/*--------------------------------------------------------------------------*/
/**
  *@brief dictionary_isempty
  *@param          pointer to dictionary
  *@returns        boolean 1 for empty 0 if there are items
*/ 
/*--------------------------------------------------------------------------*/
int dictionary_isempty(const dictionary *d)
{
    if(d==NULL)
    {
        if ( dictionary_flagstatus(error,testflag))
            fprintf(stderr,"%s: dictionary is not defined\n",__FUNCTION__);
        return -1;
    }
    return(!d->n);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief dictionary_binsearch      Perform a binary search against the sorted
 *                                  Dictionary  table. Before searching, the
 *                                  routine checks to determine if a sort is required.
 * @param d                         The dictionary to be sorted/searched
 * @param hashu                     a hashed value of the string "section:key"
 * @return                          The index into the dictionary where there was a match.
 *                                  Note. No two distinct entries can hash to the same value.
 */
/*--------------------------------------------------------------------------*/
int dictionary_binsearch(const dictionary *d, HASH_t hashu)
{
    register int upper;
    register int lower;
    register int   mid;
    HASH_t         tmp;

    lower=31415927;             /* very large number */

    if(d == NULL)
        goto exit;
    if(d->n )
    {
        if(d->lower <0 )
            dictionary_createsortedlist((dictionary *)d);
    }
    else
        goto exit;

    if(d->lower < 0)        /* sort should not fail */
        goto exit;

    upper=d->size;
    lower=d->lower;

    while(lower <= upper)
    {
        mid =(upper+lower)/2;

        tmp=d->hash[mid];
        if(hashu==tmp)
            return(mid);

        if(hashu < tmp)
            upper=mid-1;
        else
            lower=mid+1;

    }

exit:   
    return(1-lower);					/* not found */

}


int dictionary_getIndex(const dictionary *d,const char *key)
{

    return (dictionary_binsearch( d, dictionary_hash(key)));
}

int dictionary_getbool(const dictionary *d,const char *key,const int defalt)
{
   char *cp;
   cp=dictionary_get(d,key,NULL);
   if(cp==NULL ||  NULL==strchr("FfTtYy01",*cp))
          return (defalt);
    return(NULL!=strchr("TtYy1", *cp));
}

/*--------------------------------------------------------------------------*/
#ifdef WANT_KEYEXIST
/**
 * @brief dictionary_keyexist(). For a valid dictionary
          return 1 if key exists;
          return 0 if key does not exist
          returns -1 if dictionary is invalid

 *
 * @param d   pointer to dictionary
 * @param key character string to test
 * @return    (-2),-1,index value >0
 */
/*--------------------------------------------------------------------------*/
int dictionary_keyexist(dictionary *d,const char *key)
{
    int i;

    if (d==NULL || ((d->n)> d->size) )
        i= -2;
    else
    if(key==NULL)
        (i=-1);
    else
        i=dictionary_binsearch( d, dictionary_hash(key));

    return(i);
}
#endif
/*--------------------------------------------------------------------------*/
/**
 * @brief dictionary_ffree
 *        Test a pointer for non NULL. If NULL, do nothing
 *        argument passed is the address of the pointer.
 *        After freeing the pointer, we set it to NULL
 * @param address to ptr with allocated memory.
 */
/*--------------------------------------------------------------------------*/
void dictionary_ffree(void **ptr)
{
    register void *cp = *ptr;
    if(cp != NULL)                  /* ptr is a pointer to an malloc'ed area */
        free(cp);
    *ptr=NULL;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Compute the hash key for a string.
	This hash function has been taken from an Article in Dr Dobbs Journal.
 	By Bob Jenkins.  A reaffirmation of the algorithm appears in Wikapaedia
  	by Bob, with color to indicate the distribution of the bits for the hash.
  	This is normally a collision-free function, distributing keys evenly.

  	The key is stored anyway in the struct so that collision can be avoided
 	 by comparing the key itself in last resort.
  @param    key     Character string to use for key.
  @return   unsigned int, the hash..

 */
/*--------------------------------------------------------------------------*/
HASH_t dictionary_hash(const char * cp)
{
    HASH_t  hashk = 0 ;
    
    while(*cp != '\0')
    {
        hashk += *cp++;
        hashk += (hashk<<10);
        hashk ^= (hashk>>6) ;
    }
    hashk += (hashk <<3);
    hashk ^= (hashk >>11);
    hashk += (hashk <<15);

    return hashk ;
}
#ifdef WANT_DICTIONARY_META
/*--------------------------------------------------------------------------*/
/**
 * @brief dictionary_meta
 *        provide meta data information about the dictionary.
 *        filename,size,used slots,and section info.
 * @param d pointer to a dictionary
 * @param f output file (typically stdout or stderr )
 */
/*--------------------------------------------------------------------------*/
void dictionary_meta(dictionary *d,FILE *f)
{
    fprintf(f,"dictionary name...:%s\n",d->filename);
    fprintf(f,"dictionary size...:%d\n",d->size);
    fprintf(f,"dictionary used...:%d\n",d->n);
    fprintf(f,"dictionary Resrved:%d\n",1);
    fprintf(f,"dictionary avail..:%d\n",d->size-d->n);
    fprintf(f,"dictionary_lower..:%d\n",d->lower);
#ifdef   _INIPARSER_H_
    fprintf(f,"dictionary Section:%d\n",d->skeys[0]);
#endif
    return;
}
#endif



/*-------------------------------------------------------------------------*/
/**
  @brief    Create a new dictionary object.
  @param    size    Optional initial size of the dictionary.
  @return   1 newly allocated dictionary objet.

  This function allocates a new dictionary object of given size and returns
  it. If you do not know in advance (roughly) the number of entries in the
  dictionary, give size=0.
 **/
/*--------------------------------------------------------------------------*/
dictionary * dictionary_new(unsigned size,const char *filename)
{
    dictionary  *   d ;
    /* If no size was specified, allocate space for DICTMINSZ */
    if (size<DICTMINSZ) 
	size=DICTMINSZ ;
    if(size%4 != 0)
        size+= 4-size%4; 		//round up to multiple of 4
    if (!(d = (dictionary *)calloc(1, sizeof(dictionary))))
    {
        return NULL;
    }
    d->size = size;               /* want one slot as cushion */
    d->lower=size-2;
    d->n    = 1;
    d->val  = (char **) calloc(size, sizeof(char**));
    d->key  = (char **) calloc(size, sizeof(char**));
    d->skeys= (HASH_t  *)calloc(size, sizeof(HASH_t));
    d->hash = (HASH_t  *)calloc(size, sizeof(HASH_t));
    d->filename = strdup(filename);
    if(d->hash==NULL||d->key==NULL||d->val==NULL||d->skeys==NULL||d->filename==NULL)
    {
        if ( dictionary_flagstatus(error,testflag))
        {
            printf("%s Out of Memory! Unable to continue!",__FUNCTION__);
        }
        exit(-1);
    }
    d->hash[d->size-1]=(HASH_t)-1;    /* max unsigned */
    d->skeys[0]=0;
    debug("d->hash[%d]=%10.8X\n",d->size-1,d->hash[d->size-1]);
    //dictionary_rawdump(d,stdout);
    return d ;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Delete a dictionary object
  @param    d   dictionary object to deallocate.
  @return   void

  Deallocate a dictionary object and all memory associated to it.
 */
void dictionary_del(dictionary ** vd)
{
    int     i ;
    dictionary *d=*vd;
    if (d==NULL)
        return ;
    for (i=0 ; i<=d->size ; i++)
    {
        if(d->hash[i]!=0)
        {
            if (d->key[i])
                free(d->key[i]);
            if (d->val[i])
                free(d->val[i]);
        }
    }
    free(d->val);
    free(d->key);
    free(d->hash);
    free(d->skeys);
    if(d->filename!=NULL)
        free(d->filename);
    free(d);
    *vd=NULL;
    return ;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Get a value from a dictionary.
  @param    d       dictionary object to search.
  @param    key     Key to look for in the dictionary.
  @param    defmsg  Default value to return if key not found.
  @return   1 pointer to internally allocated character string.

  This function locates a key in a dictionary and returns a pointer to its
  value, or the passed 'def' pointer if no such key can be found in
  dictionary. The returned character pointer points to data internal to the
  dictionary object, you should not try to free it or modify it.
 */
/*--------------------------------------------------------------------------*/
char * dictionary_get(const dictionary * d, const char * key, char * defmsg)
{
    int         i ;
    HASH_t     hashu;
    hashu=dictionary_hash(key);
    i=dictionary_binsearch(d,hashu);
    if (i<0)
    {
        i=-i;
        //printf("\n%s: i=%d, hashu=%10.8X, key=%s\n",__FUNCTION__, -i, hashu, key);
        //dictionary_show(d, i,    stdout);
        //dictionary_show(d, i-1,  stdout);
        //dictionary_show(d, i+1,  stdout);
        if ( dictionary_flagstatus(error,testflag))
            fprintf(stderr,"%s: key \"%s\" Not found\n",__FUNCTION__,key);
        return (defmsg);
    }
    if( d->key[i]!=NULL)
    {
        if(  !strcmp(key,d->key[i]))
            return (d->val[i]);
        
    }
    return defmsg;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Set a value into a dictionary.
  @param    d       dictionary object to modify.
  @param    key     Key to modify or add.
  @param    val     Value to add.
  @return   int     0 if Ok, anything else otherwise

  If the given key is found in the dictionary, the associated value is
  replaced by the provided one. If the key cannot be found in the
  dictionary, it is added to it.

  It is Ok to provide a NULL value for val, but NULL values for the dictionary
  or the key are considered as errors: the function will return immediately
  in such a case.

  Notice that if you dictionary_set a variable to NULL, a call to
  dictionary_get will return a NULL value: the variable will be found, and
  its value (NULL) is returned. In other words, setting the variable
  content to NULL is equivalent to deleting the variable from the
  dictionary. It is not possible (in this implementation) to have a key in
  the dictionary without value.

  This function returns non-zero in case of failure.
  Resets the dictionary lower variable to possibly force a resort.
 */

/*--------------------------------------------------------------------------*/
/**
 * @brief dictionary_set
 * @param d
 * @param key
 * @param val
 * @return
 */
int
dictionary_set(dictionary * d, const char * key, const char * val)
{
    int         i,j ;

    char *cpdk;  /* eliminate redundant index lookups cp dict key */
    char *cpdv;  /* char ptr dictionary value */
    HASH_t hashk;
    if (d==NULL || key==NULL)
    {
        debug("Dictionary not defined\n");
        return -1 ;
    }
    /* Compute hash for this key */
    debug("Wanting to insert\"%s=%s\"\n",key,val);
    //dictionary_rawdump(d,stderr);
    hashk=dictionary_hash(key);
    i=dictionary_binsearch(d,hashk);


    if ( i>0 )
    {
        debug("hash=%10.8X,d->hash[i]=%10.8X, d->hash[i+1]=%10.8X,at i=%d\n",
              hashk,d->hash[i],d->hash[i+1],i);
        cpdk=d->key[i];
        if ( 0==strcmp(key, cpdk) )    /* Same key */
        {
            /* Found a valid same key value: modify and return */
            /* if new value fits in space reserved for old,
             * overwrite old space and exit. No need to redo
             * key hash, sort or free()/strdup
             */
            cpdv=d->val[i];
            if (cpdv != NULL)
            {
                if ( strlen(val) > strlen(cpdv) )
                {
                    free(cpdv);
                    d->val[i]= cpdv = strdup(val) ;
                }
                else
                    strcpy(cpdv,val);
            }
            /* Value has been modified: return */
            d->info=i;
        }
        else
        {
            debug("collision detected!\n");
            debug("hash=%10.8X,key=%s,newkey=%s val=%s,newval=%s\n",
                  hashk,cpdk,key,d->val[i],val);
            return (-1);
        }
        return 0;
    }

    /*
     * we move the table entries down to make room
     * for the insert and we adjust dictionary meta
     */
    i=-i;
    if(i > d->size)
    {
        /* this should not occur as highest entry in dictionary is set to 0xffff */
        debug("OH OH \n");
        d->n++;
        fprintf(stderr,"%s: Dictionary has %d entries,i=%d,nextslot=%d\n",__FUNCTION__,d->n-1,i,d->size-d->n);
        dictionary_rawdump(d,stderr);
        i=d->size-d->n;
        d->key[i]=(char*)key;
        d->val[i]=(char*)val;
        d->hash[i]=hashk;
        d->lower--;
        d->info=i;
        dictionary_rawdump(d,stderr);
        exit(0);
    }

    /* does dict need expanding ? */
    if( d->n == d->size )
        if (dictionary_grow(d))
            return -1;
    /* test d->lower ==0  */
    if (d->hash[d->lower]!=0)
    {
        printf("%s: logic Error at d->lower\n",__FUNCTION__);
        dictionary_rawdump(d,stdout);
        dictionary_del(&d);
        exit(-1);
    }
    for (i=d->lower+1;i<d->size;i++)
    {
        /* shift rows down until place for new item */
        j=i-1;
        if(d->hash[i]<hashk)
        {
            d->hash[j]=d->hash[i];
            d->key[j]= (char*)d->key[i];
            d->val[j]= (char *)d->val[i];
            debug("hash %10.8X < d->hash[%d]=%10.8X  \n",hashk,i,d->hash[i]);
            continue;
        }
        goto doInsert;
    }

    if(i==d->size)
    {
        j=i-1;
doInsert:
        d->hash[j] = hashk;
        d->key[j]  = strdup(key);
        d->val[j]  = (val!=NULL) ? strdup(val) : NULL;
        debug("After\n");
        d->info = j;
        d->n++;
        d->lower--;
        return 0;
    }

    printf("%s: SYSTEM ERROR at %d\n",__FUNCTION__,__LINE__);
    return -1;

}

/*-------------------------------------------------------------------------*/
/**
  @brief    Delete a key in a dictionary
  @param    d       dictionary object to modify.
  @param    key     Key to remove.
  @return   void

  This function deletes a key in a dictionary. Nothing is done if the
  key cannot be found. The entries preceding the key are moved up one row.
  The vacant row is zero filled.
 */
/*--------------------------------------------------------------------------*/
int dictionary_unset(dictionary * d, const char * key)
{
    HASH_t        hashk ;
    int         i,size  ;
    register int       k;
    register int   lower;

    if (key == NULL)
        return 0x0fffffff;		/* very large number */

    if(d==NULL)
    {
       fprintf(stderr,"Dictionary not allocated\n");
       exit(3);
    }
    hashk = dictionary_hash(key);
    i=dictionary_binsearch(d,hashk);

    if( (i<0)  || (d->hash[i] != hashk) )
    {
        if ( dictionary_flagstatus(error,testflag))
            fprintf(stderr,"Function %s: hash %10.8X, key \"%s\" Not found!\n",__FUNCTION__, hashk, key);
        dictionary_rawdump(d,stdout);
        return i;
    }
    debug("%s: doing brute force search for %s with hash %10.8X\n",__FUNCTION__,key,hashk);
    /* try brute force search */
    /* begin search at position i obtained from binsearch */
    lower=d->lower;
    size=d->size-1;             /* index 0...size-1 */
    for (; i<=size ; i++)
    {
        
        /* Compare hash */
        if (hashk==d->hash[i])
        {
            /* Compare string, to avoid hash collisions */
            if(d->key[i]==NULL)
                continue;
            if (!strcmp(key, d->key[i]))
            {
                /* Found key */
                break ;
            }
            else
            {
                /* each hash value is unique, can't have duplicates
                 * As two keys could hash to the same value
                 * the test is to confirm the key value
                 * having the same hash calculation and to
                 * remove it. So far, all testing with 1000 keys
                 * produced no duplicates.
                 */
                for (k=lower;k<=size;k++)
                    if(!strcmp(key,d->key[k]))
                        break;
                i=k;
            }
        }
    }
    if (i>size)
    {
        debug(" Key not Found! unset i=%d, d->size=%d,hash=%10.8X,key=%s\n",
              i,d->size,hashk,key);
        /* Key not found */
        return i;
    }
    /*shifting the table up with leading zeros */
    /* after  first freeing  allocated memory  **/
    if(d->hash[i])
    {
        if(d->key[i])
        {
            free(d->key[i]);
         }
        if(d->val[i])
        {
            free(d->val[i]);
        }
    }
    /* UNRAVELLING THE SHUFFLE UPWARDS IS FASTER THAN MERGING THE SHUFFLE      */
    /* UNRAVELLING THE SHUFFLE UPWARDS IS FASTER THAN MERGING THE SHUFFLE      */
    /* UNRAVELLING THE SHUFFLE UPWARDS IS FASTER THAN MERGING THE SHUFFLE      */

    char **toc;         /* for the two character arrays, use of the pointers cuts*/
    char **fromc;
    /* by half, the cpu used. Also separating out into three *
     * loops adds to that performance increase               */
    toc=&d->key[i];
    fromc=&d->key[i-1];
    for (k=i;k>lower;k--)
    {
        //d->key[k]=d->key[k-1];
        *toc-- = *fromc--;
    }

    toc=&d->val[i];
    fromc=&d->val[i-1];
    /* 
     * next two loops are separated as they are faster 
     * than being combined 
     */ 
    for (k=i;k>lower;k--)
    {
        *toc-- = *fromc--;
    }

    /* the following code was faster than using pointers */
    for (k=i;k>lower;k--)
    {
        d->hash[k]=d->hash[k-1];
    }

    d->key[lower]=NULL;
    d->val[lower]=NULL;
    d->hash[lower]=0;
    d->lower++;
    d->n-- ;
    return 0;
}

#ifdef WANT_DICTIONARY_DUMP
/*-------------------------------------------------------------------------*/
/**
  @brief    Dump a dictionary to an opened file pointer.
  @param    d   Dictionary to dump
  @param    f   Opened file pointer.
  @return   void

  Dumps a dictionary onto an opened file pointer. Key pairs are printed out
  as @c [Key]=[Value], one per line. It is Ok to provide stdout or stderr as
  output file pointers.
 */
/*--------------------------------------------------------------------------*/
void dictionary_dump(dictionary * d, FILE * out)
{
    int     i ;

    if (d==NULL || out==NULL)
        return ;
    if (d->n<1)
    {
        fprintf(out, "%s:empty dictionary\n",__FUNCTION__);
        return ;
    }
    for (i=0 ; i<d->size ; i++)
    {
        if (d->key[i])
        {
            fprintf(out, "%.4d)[%20s]\t[%s]\n",
                    i,d->key[i],
                    d->val[i] ? d->val[i] : "UNDEF");
        }
    }
    return ;
}
#else
void dictionary_dump(dictionary * d, FILE * out)
{
}

#endif
/*-------------------------------------------------------------------------*/
#ifdef WANT_DICTIONARY_RAWDUMP
/*--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------*/
/**
  @brief    Raw Dump a dictionary to an opened file pointer.
  @param    d   Dictionary to dump
  @param    f   Opened file pointer.
  @return   void

  Dumps a raw dictionary listing onto an opened file pointer. Key pairs are printed out
  as @c [Key]=[Value], one per line. It is OK to provide stdout or stderr as
  output file pointers. Recent change, Do not display unassigned slots.
 */
/*--------------------------------------------------------------------------*/
void dictionary_rawdump(dictionary *d, FILE *out)
{
    register int i;
    int skip=0;			/* do not display multitude of unassigned rows */
    char *val;

    fprintf(out,"\nfilename:%s\n",d->filename);
    fprintf(out,"size  = %d\n", d->size);
    fprintf(out,"n     = %d\n", d->n   );
    fprintf(out,"lower = %d\n", d->lower);
    for(i=0;i<d->size;i++)
    {
        if(0 ==d->hash[i])
        {
            if(!skip)
            {
                fprintf(out,"\nUnassigned %d rows beginning %d to ",d->lower+1,i);
                skip=1;
            }
            continue;
        }
        if(skip)
        {
            fprintf(out,"%d\n\n",i-1);
            skip=0;
        }
        val=d->val[i];
        if(val==NULL)
            val="";
        fprintf(out,"(%5d),hash=%10.8X,key=[%30s]val=%s\n",i,d->hash[i],d->key[i],val);
    }
    /* dump list of sections */
#ifdef _INIPARSER_H_
    fprintf(out,"sections=%d\n",  d->skeys[0]);
    for(i=1;i<=d->skeys[0];i++)
        fprintf(out,"section %d: hash=%10.X\n",i,d->skeys[i]);
#else
    d->skeys[0]=0;
#endif
}
#else
void dictionary_rawdump(dictionary *d,FILE *out)
{
}
#endif
/* vim: set ts=4 et sw=4 tw=75 */
