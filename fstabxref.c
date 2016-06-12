/************************************************************************************************
 *                                                                                              *
 *  Program fstabcref       Author Leslie Satenstein                                            *
 *                          Date:  April 25 2016                                                *
 *                                                                                              *
 *  Revision April 11,2016  0.1    First version      UUID=xxx                                  *
 *                          0.2    Added getopt, -h help -i input -o output                     *
 *                                 And restructured the code                                    *
 *                          0.3    Integrated mkstemps() and did some code rearrangements       *
 *                          0.4    integrate LABEL=xxx                                          *
 *  This program formats the /etc/fstab and adds a xref for the UUID value to the device id     *
 *  It relies on /etc/fstab and on /dev/disk/by-uuid                                            *
 *                                                                                              *
 * The program creates two temporary files in /tmp and works with those file.                   *
 * The program uses a rudamentary deta dictionary to store the information obtained from        *
 * /dev/disk/by-uuid and from /dev/disk/by-label.                                               *
 *  Step 1 Parse the command line to determine files to read and or write                       *
 *  Step 2 Read the /dev/disk/by-uuid to /tmp/tempfile and use it to create entries into the DD *
 *         The DD will contain UUID and device-id pulled from /dev/disk/by-uuid                 *
 *  Step 3 read the /dev/disk/by-label and create additional entries into the DD                *
 *         The key is label, the variable is the device-id                                      *
 *  Step 4 Read the fstab or the fstab pointed to the -i parameter                              *
 *         left adjust the lines, if not so, and check for UUID= or LABEL=                      *
 *         If it is a UUID= parse the UUID, directory fstype, default/other params and          *
 *         the dump parameters                                                                  *
 *         If it is a LABEL= parse the label name, the directory, fstype default/other parms    *
 * Step 5  If it is the UUID=  fetch the device-id corresponding to the UUID=xxxx values        *
 *         If it is the LABEL= fetch the device id corresponding to the LABEL=xxxx              *
 * Step 6  reconstruct the UUID= lines or the LABEL= lines appending the #/dev/xxxxx            *
 * Step 7  Empty the DD and unlink the temporary file and exit gracefully                       *
 *                                                                                              *
 ************************************************************************************************
 * Permission is hereby granted, free of charge, to any person obtaining a			*
 * copy of this software and associated documentation files (the "Software"),			*
 * to deal in the Software without restriction, including without limitation			*
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,			*
 * and/or sell copies of the Software, and to permit persons to whom the			*
 * Software is furnished to do so, subject to the following conditions:				*
 *												*
 * The above copyright notice and this permission notice shall be included in			*
 * all copies or substantial portions of the Software.						*
 *												*
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR			*
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,			*
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE			*
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER			*
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING			*
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER				*
 * DEALINGS IN THE SOFTWARE.									*
 ***********************************************************************************************/


//#define NDEBUG                //enable if you want the debug macros, (they are in dictionary.h)

#include "dictionary.h"
// commented #includes are first declared in dictionary.h
//#include <stdio.h>
//#include <string.h>
//#include <stdlib.h>
//#include <unistd.h>
#include <getopt.h>		//externals
#include <fcntl.h>
#include <sys/stat.h>           //chmod
#include <limits.h>
#include <libgen.h>


dictionary *ini;           //this is a data dictionary pointer. The DD will hold
//the UUID value as key and the /dev value as data


FILE *filein;
FILE *fin;              /* to read fstab */
FILE *fout;
char fstab[PATH_MAX]="/etc/fstab";
char outfile[PATH_MAX];
char buffer[PATH_MAX];
struct stat statbuf;
char pgm[257];
char devdiskfile[32]  ="/tmp/uuid.uXXXXXX.txt";

static void fstabToDictMatch(FILE *);
int main(int ,char **);
const char NULLCHAR='\0';

/********************************************
 *  strtrim(string,option)                  *
 *  option=HEADSTMT or 1   left  trim       *
 *  option=ENDSTMT or  2   right trim 	    *
 *  option=BOTHENDS or 3 left and right trim*
 ********************************************
 *  enum {HEADSTMT=1,ENDSTMT=2,BOTHENDS=3}; *
 *******************************************/
static
char * strtrim(char *in,int option)
{
    char * restrict cp;

    if(option & 1   )
    {
        cp=in;
        while(*cp<=' '&& *cp != NULLCHAR)
            cp++;
        if(cp!=in)
            memmove(in,cp,strlen(cp)+1);  /* must not use memcpy() */
    }
    /*
     *  Right side trim
     */
    if(option & 2   )
    {
        cp=in+strlen(in)-1;
        while( (cp>=in) &&  *cp <= ' ' )
            *cp-- = NULLCHAR;
    }
    return in;
}


/**
 * @brief fstabToDictMatch
 *        This function matches each line of an fstab file to the
 *        entries cleaned from the /dev/disk/by-uuid subdirectory
 *        and from /dev/disk/by-label. 
 *        NOTE: NOTE:
 *        LABEL=sde1Spare /Development       ext4   defaults,noatime     1 2 
 * @param f the stream to where the output is to be written.
 */

static void fstabToDictMatch(FILE *f)
{
    char *devid=NULL;
    char defs[96];
    char dmpodr[40];
    char dmpodr2[40];
    char fstype[40];
    char mnt_name[64];
    char label[96];
    char uuidln[50];
    char workarea[PATH_MAX];


    //FILE *fin;
    int i;

    fin=fopen(fstab,"rb");
    if(fin==NULL)
    {
        fprintf(stderr,"Can't open file \"%s\" for reading\n",fstab);
        exit(89);
    }
    while(!feof(fin))
    {
        if(NULL== (fgets(buffer,sizeof(buffer),fin)))
            continue;

        strcpy(workarea,buffer);
   	workarea[sizeof(workarea)-1]=NULLCHAR; 
        strtrim(workarea,3);
        *label= NULLCHAR;
        *mnt_name = NULLCHAR;
        *fstype = NULLCHAR; 
        *defs = NULLCHAR;
        *dmpodr = NULLCHAR;
        *dmpodr2 = NULLCHAR;
        devid=NULL;
        /******************    LABEL LOGIC ***************************/
        if(!memcmp(workarea,"LABEL=",6))
        {
            i=sscanf(workarea,"%s %s%s%s%s%s",label,mnt_name,fstype,defs,dmpodr,dmpodr2);
            debug("sscan rc = %d label=%s, mount_name=%s device_id=%s\n",i,label+6,mnt_name,devid);
            if (i==6)
            {
                devid =dictionary_get(ini,label+6,"not found");
                fprintf(f,"%-42s %-25s %-7s %s\t%s %s #/dev/%s\n",label,mnt_name,fstype,defs,dmpodr,dmpodr2,devid);
                continue;
            }
            //fputs(buffer,f);  /* will fall through aand fail memcmp()  test */
            //continue;
        }

        /******************************* END LABEL LOGIC ***********************/


        if(memcmp(workarea,"UUID=",5))
        {
            fputs(buffer,f);
            continue;
        }
        debug("Processing workarea=[%s]\n\n",workarea);
        /*
         *  sscanf( workarea, format, field of pointers )
         * Interpret  [^ ]      as a field ending in a blank
         * Interpret  [^' ']    as a field ending in a blank
         * Interpret [^ |\t]    as a field ending in blank or tab char
         * Interpret [^' '|\t]  as a field ending in blank or tab char
         * Interpret [^ |\t\n]  as a field ending in blank, tabchar or end-of-line
         *
         */

        i=sscanf(workarea,"%s%s%s%s%s%s",
                 uuidln,mnt_name,fstype,defs,dmpodr,dmpodr2);
        if(i!=6)
        {
            //fprintf(stderr,"I = %d\n",i);
            fputs(buffer,f);
            continue;
        }
        debug("looking up [%s] in dictionary\n",uuidln);
        debug("This buffer\n%s\n",buffer);

        devid=dictionary_get(ini,uuidln+5,"*not found");
        debug("dictionary_get() returned [%s]\n",devid);
        fprintf(f,"%-42s %-25s %-7s %s\t%s %s #/dev/%s\n",
                uuidln,mnt_name,fstype,defs,dmpodr,dmpodr2,devid);
    }
    fclose(fin);
}

/**
 * @brief Dictionary_fill_LABEL_Entries
 *        Using the lines from ls -l /dev/disk/by-label
 *        parse the "line to create a data dictionary entry"
 *        NOTE. The fstab has /xyz   but tye by-label stores xyz
 * lrwxrwxrwx. 1 root root 10 Apr 25 16:05 sdb9xfceHome -> ../../sdb5
 * @param line
 *        The line from an fstab.
 */
static void Dictionary_fill_LABEL_Entries(char *line)
{
    char label[64];
    char devptr[8];
    char *cp;
    int i;

    cp=strchr(line,'\n');
    if (cp)
        *cp=NULLCHAR;
    *label=*devptr=NULLCHAR;
    cp=strrchr(line,'/');       /*right most (last) slash in the line */
    if(cp)
        strcpy(devptr,cp+1);
    else
        return;
    strtrim(devptr,3);

    i=sscanf(line+40,"%s",label);
    if (i!=1)
        return;

    debug("label=%s devptr=%s\n",label,devptr);
    debug("label=%s Hash %10.8X\n",label,dictionary_hash(label)); 
    if(*label!=NULLCHAR)
    {
      if(dictionary_set(ini,label,devptr))
      {
          fprintf(stderr,"dictionary_set(%s,%s)) failed\n",
                  label,devptr);
          dictionary_del(&ini);
          exit(11);
      }
    }
#if 0
    if(dictionary_set(ini,devptr,label))
    {
        fprintf(stderr,"dictionary_set(%s,%s)) failed\n",
                devptr,label);
        dictionary_del(&ini);
        exit(19);
    }
#endif
    debug("label=[%s] dev=[%s]\n",label,devptr);
}


/**
 * @brief Dictionary_fill_UUID_Entries
 * @param line
 *      "lrwxrwxrwx. 1 root root 10 Apr 12 16:26 119a207e-0480-4298-907b-4f16a8c6316d -> ../../sdb7"
 *                              column 40  ----->|                          look for this --->|
 *
 */
static void  Dictionary_fill_UUID_Entries(char *line)
{

    char uuidptr[50];
    char devptr[4];
    char *cp;
    char *uptr;


    strtrim(line,3);
    uptr=strrchr(line,':');            /* where the UUID= starts 4 after date minutes field*/
    if(uptr==NULL)
    {
        debug("%s format missing \":\"\n",line);
        return;
    }
    uptr+=4;
    memcpy(uuidptr,uptr,sizeof(uuidptr)-1);        //to include all of the part
    uuidptr[sizeof(uuidptr)-1]=NULLCHAR;		 
    cp=strchr(uuidptr,' ');
    if(cp)
        *cp=NULLCHAR;
    else
       {
         fprintf(stderr,"Can't process line %s\n",line);
         return;
       }
    cp=strrchr(line,'/');
    if(cp)
        strcpy(devptr,cp+1);
    else
    {
        fprintf(stderr,"Line %s Format is invalid\n",line);
        dictionary_del(&ini);
        exit(27);
    }
  
    debug("UUID=[%s] Dev=[%s]\n",uuidptr,devptr);
    if(dictionary_set(ini,uuidptr,devptr))
    {
        fprintf(stderr,"dictionary_set(ini,%s,%s)) failed\n",
                uuidptr,devptr);
        dictionary_del(&ini);
        exit(29);
    }
}

/**
 * @brief create_dictionary
 *        Reading the ls -l /dev/disk/by-uuid/ image  Image similar to next line
 *        lrwxrwxrwx. 1 root root 10 Apr 12 16:26 119a207e-0480-4298-907b-4f16a8c6316d -> ../../sdb7
 *        create the ram array that holds
 *        key=uuid, val=device_id
 * @return the pointer to the dictionary structure.
 */
static dictionary * create_dictionary(void)
{
    FILE *filein;
    const char *LINEHEAD="lrwxrwxrwx.";

    ini=dictionary_new(32,"uuid"); /* ini is global */
    if(ini==NULL)
        exit(32);

    close(mkstemps(devdiskfile,4));
    sprintf(buffer,"ls -l /dev/disk/by-uuid >%s",devdiskfile);
    system(buffer);
#ifdef NDEBUG
    sprintf(buffer,"cat %s",devdiskfile);
    system( buffer);
#endif

    filein=fopen(devdiskfile,"rb");
    if(filein==NULL)
    {
        fprintf(stderr,"Can't open %s\n",devdiskfile);
        return ini;
    }

    while(!feof(filein))
    {
        fgets(buffer,sizeof(buffer),filein);
        if(memcmp(LINEHEAD,buffer,sizeof(LINEHEAD)-1))
        {
            debug("Rejecting: %s",buffer);
            continue;
        }
        Dictionary_fill_UUID_Entries(buffer);
    }
    fclose(filein);

    sprintf(buffer,"ls -l /dev/disk/by-label >%s",devdiskfile);
    system(buffer);
#ifdef NDEBUG
    sprintf(buffer,"cat %s",devdiskfile);
    system( buffer);
#endif

    filein=fopen(devdiskfile,"rb");
    if(filein==NULL)
    {
        fprintf(stderr,"Can't open %s\n",devdiskfile);
        return ini;
    }

    while(!feof(filein))
    {
        fgets(buffer,sizeof(buffer),filein);
        if(memcmp(LINEHEAD,buffer,sizeof(LINEHEAD)-1))
        {
            debug("Rejecting: %s",buffer);
            continue;
        }
        Dictionary_fill_LABEL_Entries(buffer);
    }
    fclose(filein);
    unlink(devdiskfile);
#ifdef NDEBUG
    debug("%s: showing meta info\n",__FUNCTION__);
    dictionary_meta(ini,stdout);
    dictionary_dump(ini,stdout);
    fprintf(stdout,"%s completed\n",__FUNCTION__);
#endif

    return ini;
}

/**
 * @brief main
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char *argv[])
{
    int c=0;
    int err=0;
    *outfile=NULLCHAR;
    fout=stdout;
    unlink("/tmp/uuid.*");
    close(mkstemps(devdiskfile,4));
    debug("devdiskfile=%s\n",devdiskfile);
    if(!isatty(fileno(stdout)))
    {
       fprintf(stderr,"%s: Redirectecting output nulls the output file\n",argv[0]);
       fprintf(stderr,"\t Use %s -o filename to create filename \n",argv[0]);
       goto help;
    }
    while((c=(getopt(argc,argv,"HhI:i:o:O:")))  !=-1 )
    {
        switch (c)
        {
        case 'h':
        case 'H':
help:
            if(memcmp("./",argv[0],2))
                sprintf(pgm,"%s/%s",getcwd(buffer,sizeof(buffer)),basename(argv[0]) );
            else
                strcpy(pgm,argv[0]);
            fprintf(stderr,"%s Help Information\n",pgm);
            strcpy(pgm,basename(argv[0]));
            fprintf(stderr,"%s [Optonal -i AlternateInput] [-o alternateOutput] -h This message!\n", pgm);
            fprintf(stderr,"\tWithout arguments %s reads /etc/fstab and writes to standard output\n",pgm);
            fprintf(stderr,"\nUse as: %s -i Your_Alternate_Input  -o Your.output.file\n",pgm);
            fprintf(stderr,"%s reads the input file and appends the device info to it.\n\n",pgm);
            fprintf(stderr,"%s processes the /etc/fstab or a copy of the /etc/fstab and reformats it\n"
                           "adding a #/dev/xxxxx reference, where xxxx is obtained from the /dev/disk/by-uuid\n"
                           "or from /dev/disk/by-label.  This program written by Leslie Satenstein 25April 2016\n",pgm);
            fprintf(stderr,"If uncertain about %s's use, copy /etc/fstab to /tmp and try it out\n",pgm);
            err=1;
            break;

        case 'i':
        case 'I':
            if(strlen(optarg))
            {
                strcpy(fstab,optarg);
                if(stat(fstab,&statbuf) ==-1)
                {
                    fprintf(stderr,"File %s not accessable.\n",fstab);
                    err=1;
                }
                else
                    if((statbuf.st_mode & S_IFMT)!=S_IFREG)
                    {
                        fprintf(stderr,"File %s is not a regular file\n",fstab);
                        err=1;
                    }

            }
            else
            {
                fprintf(stderr,"-i needs a path/filename\n");
                err=1;
            }
            break;
        case 'o':
        case 'O':
            if(!strcmp("/etc/fstab",optarg))
            {
                fprintf(stderr,"You cannot write directly to /etc/fstab\n");
                err=1;
            }
            strcpy(outfile,optarg);
            break;
        default:
            break;
        }
    }
    if(!strcmp(fstab,outfile))
    {
        fprintf(stderr,"Input file may not equal output file\n");
        err=1;
    }
    if(err)
        exit(41);
    fin=fopen(fstab,"rb");
    if(fin==NULL)
    {
        fprintf(stderr,"Can't open file \"%s\" for reading\n",fstab);
        exit(49);
    }
    if(*outfile != NULLCHAR)
    {
        fout=fopen(outfile,"wb");
        if(fout==NULL)
        {
            fout=stdout;
            fprintf(stderr,"Unable to create %s\n",outfile);
            fprintf(stderr,"Redirecting output to stdout\n");
        }
    }
    /*
     * create the script that will create a file that
     * will contain output from ls -l  /dev/disk/by-uuid
     * then
     * create the dictionary and entries that will hold the UUID and /dev/xxx
     */

    ini=create_dictionary();  //uses devdiskfile


#ifdef NDEBUG       
    dictionary_rawdump(ini,stderr);
#endif
    /* Now we match the /etc/fstab or other fstab to the dictionary */
    fprintf(stderr,"\nDo not use redirection to force an overwrite /etc/fstab\n");
    fprintf(stderr,"Input is from %s\n",fstab);
    if(fout==stdout)
        fprintf(stderr,"Output is to standard output\n%s  -h for help\n\n",basename(argv[0]));
    else
        fprintf(stderr,"Output is to %s\n\n",outfile);

    
    fstabToDictMatch(fout);
    dictionary_del(&ini);
    return 0;
}
