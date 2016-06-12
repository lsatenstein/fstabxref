/* Copyright (c) 2000-2007 by Nicolas Devillard.
 * Copyright (x) 2014 by Leslie Satenstein <lsatenstein@yahoo.com>
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
   @file    dictionary.h
   @author  N. Devillard,Leslie Satenstein
   @brief   Implements a dictionary for string variables.

   This module implements a simple dictionary object, i.e. a list
   of string/string associations. This object is useful to store e.g.
   informations retrieved from a configuration file (ini files).
*/
/*--------------------------------------------------------------------------*/

#ifndef _DICTIONARY_H_
#define _DICTIONARY_H_
/*---------------------------------------------------------------------------
                                Includes
 ---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>

//#define WANT_INORDER_TEST
//#define WANT_DICTIONARY_TRIM
//#define WANT_KEYEXIST
//#define WANT_DICTIONARY_DUMP
//#define WANT_DICTIONARY_RAWDUMP
//#define WANT_DICTIONARY_META
//#define WANT_DICTIONARY_SHOW
#define restrict
/* define NDEBUG to obtain debug info  */
#ifndef NDEBUG
    #define debug(M, ...)
#else
    #define WANT_INORDER_TEST
    #define WANT_DICTIONARY_TRIM
    #define WANT_KEYEXIST
    #define WANT_DICTIONARY_DUMP
    #define WANT_DICTIONARY_RAWDUMP
    #define WANT_DICTIONARY_META
    #define WANT_DICTIONARY_SHOW

    #define debug(M, ...) fprintf(stdout, "DEBUG %s %d: " M "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__ );
    #define clean_errno() (errno == 0 ? "None" : strerror(errno))
    #define log_err(M, ...) fprintf(stderr, "[ERROR] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)
    #define log_warn(M, ...) fprintf(stderr, "[WARN] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)
    #define log_info(M, ...) fprintf(stderr, "[INFO] (%s:%d) " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
    #define check(A, M, ...) if(!(A)) { log_err(M, ##__VA_ARGS__); errno=0; goto error; }
    #define sentinel(M, ...)  { log_err(M, ##__VA_ARGS__); errno=0; goto error; }
    #define check_mem(A) check((A), "Out of memory.")
    #define check_debug(A, M, ...) if(!(A)) { debug(M, ##__VA_ARGS__); errno=0; goto error; }
#endif
/* Use debug1 where needed. Rename to debug when use is over */
#define debug1(M, ...) fprintf(stderr, "DEBUG %s %s %d: " M "\n",__FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__ );

typedef unsigned short  HASH_t;       /* 32 bit                    */

/** Invalid key token */
#define DICT_INVALID_KEY    ((char*)-1)

/*---------------------------------------------------------------------------
                                New types
 ---------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------*/
/**
  @brief    Dictionary object

  This object contains a list of string/string associations. Each
  association is identified by a unique string key. Looking up values
  in the dictionary is speeded up by the use of a (hopefully collision-free)
  hash function.
  Fields of significance are:
      filename pointer  Information Pointer
      n                 number of active keys
      size              actual number of allocated table rows
      lower             next available entry available to store a field in the table
                        or -1, to indicate the table should be resorted.
      val		column in the table that points to strings
      key		column in the pointer that points to keys
      hash		a column of unsigned hash integers corresponding
                        to related key
      skeys             skey[0] has count of entries between skey[1]...
                        skey[1]... has hash values of [sections]. This
                        field is completed for iniparser stuff. Otherwise it
                        is empty.
     index              Future array containing the original line number of
                        the iniparser entry. Not implemented.

 A comparison by hash is substantially faster than a comparison by key
 */
/*-------------------------------------------------------------------------*/
typedef struct _dictionary_ 
{
    char       *filename ;  /** Header to control dictionary contents       */
    int                n ;  /** Number of entries in dictionary             */
    int             size ;  /** Storage size                                */
    int            lower ;  /** used by binary search routine               */
    int             info ;  /** used by set/unset   			    */
    /** Dictionary entries                          */
    char        **   key ;  /** List of string keys                         */
    char        **   val ;  /** List of string values                       */
    HASH_t      *   hash ;  /** List of hash values for keys                */
    HASH_t 	*  skeys ;  /** skey[0] has number of sections in dict	    */
} dictionary ;

/**
 *   @brief enum option. A list of enum values that can be set or unset in a flag.
 *                       The values allow for zero (display nothing),
 *                       informative, warnings, errors, disaster.
 *                       Application program can test if any of the above was set.
 *                       Additional values include setflag,testflag,unsetflag, and
 *			 Reinitialize the flag to zero.
 */

enum _printctl_  
{
    noshow=     1<<0,
    informative=1<<1,
    warning=    1<<2,
    error=      1<<3,
    disaster=   1<<4,
    anyflag=    ((1<<0)|(1<<1)|(1<<2)|(1<<3)|(1<<4)),

    setflag =   1<<9,
    unsetflag=  1<<10,
    testflag=   1<<11,
    clearflag=  1<<12,
    dumpflag=   1<<13

} PRINTCTL;

/*---------------------------------------------------------------------------
                            Function prototypes
 ---------------------------------------------------------------------------*/
int dictionary_binsearch(const dictionary *d, HASH_t hashu);
/**
  *@brief dictionary_isempty
  *@param          d pointer to dictionary
  *@param          hashu a hashed value of the key field
  *@returns        boolean 1 for empty 0 if there are items
*/ 
int dictionary_isempty(const dictionary *d);

/*---------------------------------------------------------------------------*/
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
unsigned
dictionary_flagstatus( unsigned setpoint, enum _printctl_  option);
/*-------------------------------------------------------------------------*/
/**
  @brief    Compute the hash key for a string.
  @param    key     Character string to use for key.
  @return   1 unsigned int of at least 32 bits.

  This hash function has been taken from an Article in Dr Dobbs Journal.
  This is normally a collision-free function, distributing keys evenly.
  The key is stored anyway in the struct so that collision can be avoided
  by comparing the key itself in last resort.
 */
/*--------------------------------------------------------------------------*/
/**
 * @brief dictionary_getIndex return the index entry number for key
 * @param d Dictionary
 * @param key key to look up
 * @return index number >=0, or -value if missing
 */
int dictionary_getIndex(const dictionary *d,const char *key);

int dictionary_getbool(const dictionary *d,const char *key,const int defalt);
/*--------------------------------------------------------------------------*/

HASH_t dictionary_hash(const char * key);

/*-------------------------------------------------------------------------*/
/**
  @brief    Create a new dictionary object.
  @param    size    Optional initial size of the dictionary.
  @return   1 newly allocated dictionary objet.

  This function allocates a new dictionary object of given size and returns
  it. If you do not know in advance (roughly) the number of entries in the
  dictionary, give size=0. The default with size set to zero is 64.
 */
/*--------------------------------------------------------------------------*/
dictionary * dictionary_new(unsigned size, const char *filename);

/*-------------------------------------------------------------------------*/
/**
  @brief    Delete a dictionary object
  @param    vd   dictionary object to deallocate.
  @return   void

  Deallocate a dictionary object and all memory associated to it.
  This is performed first by column, the filename,  and then by the object
  itself.
 */
/*--------------------------------------------------------------------------*/
void dictionary_del(dictionary ** vd);

/*-------------------------------------------------------------------------*/
/**
  @brief    Get a value from a dictionary.
  @param    d       dictionary object to search.
  @param    key     Key to look for in the dictionary.
  @param    def     Default value to return if key not found.
  @return   1 pointer to internally allocated character string.

  This function locates a key in a dictionary and returns a pointer to its
  value, or the passed 'def' pointer if no such key can be found in
  dictionary. The returned character pointer points to data internal to the
  dictionary object, you should not try to free it or modify it.
 */
/*--------------------------------------------------------------------------*/
char * dictionary_get(const dictionary * d, const char * key, char * def);

/**
 * @brief dictionary_show  Show an row of the dictionary, using the row's index.
 * @param d      The dictionary
 * @param index  The index into the dictionary ini data
 * @param f      The File * -- stdout, stderr, or pointer to /dev/null.
 */
void dictionary_show(dictionary *d,int index,FILE *f);



/*-------------------------------------------------------------------------*/
/**
  @brief    Set a value in a dictionary.
  @param    vd       dictionary object to modify.
  @param    key     Key to modify or add.
  @param    val     Value to add.
  @return   int     0 if Ok, anything else otherwise

  If the given key is found in the dictionary, the associated value is
  replaced by the provided one. If the new value fits in the old value's space
  the same physical space is used. If the key cannot be found in the
  dictionary, it is added to it.

  It is Ok to provide a NULL value for val, but NULL values for the dictionary
  or the key are considered as errors: the function will return immediately
  in such a case.

  Notice that if you "dictionary_set" a variable to NULL, a call to
  "dictionary_get" will return a NULL value: the variable will be found, and
  its value (NULL) is returned. In other words, setting the variable
  content to NULL is equivalent to deleting the variable from the
  dictionary. It is not possible (in this implementation) to have a key in
  the dictionary without value.

  No check is made for contents of a key. You may have a key of "*" and
  it will be accepted.

  This function returns non-zero in case of failure.
 */
/**
 * @brief dictionary_keyexist(). For a valid dictionary
 *       return >(-1) 		if key exists;
 *       returns -2		if dictionary is invalid
 *
 * @param d  pointer to dictionary
 * @param key character string to test
 * @return (-1),0,1
 */
int dictionary_keyexist(dictionary *d,const char *key);

/*--------------------------------------------------------------------------*/
int dictionary_set(dictionary * vd, const char * key, const char * val);

/*-------------------------------------------------------------------------*/


/**
 * @brief dictionary_trim     Trim over allocation of a dictionary
 *                            The input dictionary is modified
 *                            If there were modifications done.
 * @param dn                  address of the dictionary pointer
 * @param trim                How much to trim (min of 4 spare)
 * @param verbose             Display details or not to display
 * @return                    Pointer to a dictionary
 */

dictionary * dictionary_trim(dictionary *d,unsigned trim,FILE *verbose);
/*--------------------------------------------------------------------------*/

/**
  @brief    Delete a key in a dictionary
  @param    d       dictionary object to modify.
  @param    key     Key to remove.
  @return   void

  This function deletes a key in a dictionary. Nothing is done if the
  key cannot be found. It also frees up the memory assigned to the corresponding
  val and all key, hastval to NULL, zero, NULL and sets a flag to reorder
  the table.
 */
/*--------------------------------------------------------------------------*/
int dictionary_unset(dictionary * d, const char * key);

/*-------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------*/

void dictionary_dump(dictionary * d, FILE * f);

/*--------------------------------------------------------------------------*/


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

void dictionary_rawdump(dictionary * d, FILE * f);

/*-------------------------------------------------------------------------*/
/**
  @brief    free allocated space of the passed pointer and set that pointer to NULL
            Pass the address of a pointer containing the dynamically
            allocated memory.
            
  @param    ptr  to a pointer refering to an malloc'd area
  @return   the return code from free();

*/
/*--------------------------------------------------------------------------*/

void dictionary_ffree(void **ptr);

/*--------------------------------------------------------------------------*/
/**
 * @brief   Sorts the table of dictionary entries into
 *          hash sequence. the sort is performed if d->lower is negative.
 * @param d The pointer to the data dictionary table which is the table to
 *          be sorted.
 */
/*--------------------------------------------------------------------------*/

void dictionary_createsortedlist(dictionary *d);

void dictionary_meta(dictionary *d, FILE *f);

/*--------------------------------------------------------------------------*/
#endif
