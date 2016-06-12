/************************************************************************************************
 *                                                                                              *
 *  Program fstablsblk      Author Leslie Satenstein                                            *
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


dictionary *ini;            //this is a data dictionary pointer. The DD will hold
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
const char nullchar='\0';
static void fstabToDictMatch(FILE *);
int main(int ,char **);


/********************************************
 *  strtrim(string,option)                  *
 *  option=HEADSTMT or 1   left  trim       *
 *  option=ENDSTMT or  2   right trim 	    *
 *  option=BOTHENDS or 3 left and right trim*
 ********************************************
 *  enum {HEADSTMT=1,ENDSTMT=2,BOTHENDS=3}; *
 *******************************************/
static
char * strtrim(char * restrict in,int option)
{
    char * restrict cp;

    if(option & 1   )
    {
        cp=in;
        while(*cp<=' '&& *cp != nullchar)
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
            *cp-- = nullchar;
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
    char workarea[256];


    //FILE *fin;
    int i;

    fin=fopen(fstab,"rb");
    if(fin==NULL)
    {
        fprintf(stderr,"Can't open file \"%s\" for reading\n",fstab);
        exit(89);
    }
    debug("reading %s\n",fstab);
    while(!feof(fin))
    {
        if(NULL== (fgets(buffer,sizeof(buffer),fin)))
            continue;

        strcpy(workarea,buffer);
        strtrim(workarea,3);

        /******************    LABEL LOGIC ***************************/
        if(!memcmp(workarea,"LABEL=",6))
        {
            i=sscanf(workarea,"%s %s%s%s%s%s",label,mnt_name,fstype,defs,dmpodr,dmpodr2);
            debug("sscan rc = %d label=%s, mount_name=%s device_id=%s\n",i,label+6,mnt_name,devid);
            if (i==6)
            {
                //fprintf(stderr,"label=[%s] label+6=[%s]\n",label,label+6);
                //fprintf(stderr,"label=%s hash=%10.8X\n",label+6,dictionary_hash(label+6));
                //dictionary_rawdump(uuid,stderr);
                devid =dictionary_get(ini,label+6,"not found");
                fprintf(f,"%-42s %-25s %-7s %s\t%s %s #/dev/%s\n",label,mnt_name,fstype,defs,dmpodr,dmpodr2,devid);
                continue;
            }
        }

        /******************************* END LABEL LOGIC ***********************/


        if(memcmp(workarea,"UUID=",5))
        {
            fputs(buffer,f);
            continue;
        }
        debug("Processing workarea=[%s]\n",workarea);
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
        debug("looking up [%s] in dictionary\n",uuidln+5);
        debug("This\n%s\n",buffer);

        devid=dictionary_get(ini,uuidln+5,"*not found");
        debug("dictionary_get() returned [%s]\n",devid);
        fprintf(f,"%-42s %-25s %-7s %s\t%s %s #/dev/%s\n",
                uuidln,mnt_name,fstype,defs,dmpodr,dmpodr2,devid);
    }
    fclose(fin);
}

/**
 * @brief Dictionary_fill_Entries
 *        Using the lines from ls -l /dev/disk/by-label
 *        parse the "line to create a data dictionary entry"
 *        NOTE. The fstab has /xyz   but tye by-label stores xyz
 * lrwxrwxrwx. 1 root root 10 Apr 25 16:05 sdb9xfceHome -> ../../sdb5
 * @param line
 *        The line from an fstab.
 */
static void Dictionary_fill_Entries(char *line)
{
    char work[256];
    int i;
    char device[15];
    char protocol[15];
    char label[60];
    char uuidln[60];
    char mount[120];


    strcpy(work,line);
    strtrim(work,3);
    *device=*protocol=*label=*uuidln=*mount=nullchar;

    /* some labels are omitted */

    if(!memcmp(work+13,"      ",6))
        i=sscanf(work,"%s%s%s%s",device,protocol,uuidln,mount);
    else
        i=sscanf(work,"%s%s%s%s%s",device,protocol,label,uuidln,mount);

    if(!strcmp("ntfs",protocol))
    {
        debug("processing NTFS\nwork=%s\n",work);
        switch(i)
        {
        case 1:			/* device=sdd4,protocol= label=, uuidln= mount= */
            strcpy(uuidln,label);
            break;
        case 2:
            if(!memcmp(label,"System",7))
            {
                strcpy(label,"System Reserved");
                strcpy(uuidln,mount);
                *mount=nullchar;
                debug("at protocol ntfs  case 2 dictionary_set(ini,%s,%s)\n",uuidln,device);
                dictionary_set(ini,uuidln,device);
            }
            break;
        case 3:				/* sda2  ntfs                   3C5A072D5A06E40C  */
            debug("at ntfs case 3 dictionary_set(ini,%s,%s)\n",uuidln,device);
            dictionary_set(ini,uuidln,device);
            break;
        case 4:
            debug("at ntfs case 4 label=%s  device=%s uuid=%s\n",label,device,uuidln);
            if ( *label!=nullchar)
                dictionary_set(ini,label,device);
            dictionary_set(ini,uuidln, device);
            break;
        case 5:
            debug("at ntfs case 5 label=%s  device=%s uuid=%s\n",label,device,uuidln);
            if(!memcmp(label,"System",7))
            {
                if(*label!=nullchar)   /* shift  over */
                {
                    strcat(label," ");
                    strcat(label,uuidln);
                    strcpy(uuidln,mount);
                    *mount=nullchar;
                    i--;
                    dictionary_set(ini,label,device);
                }
                dictionary_set(ini,uuidln,device);
                break;
            }
        default:
            break;
        }
    }
    else
    {
        debug("processing %s\n",protocol);
        switch(i)
        {
        case 1:
            debug("at case 1 for protocol %s\n",protocol);  /* device=sdd4,protocol= label=, uuidln= mount= */
            break;

        case 2:
            debug("at protocol %s 2 dictionary_set(ini,%s,%s)\n",protocol, uuidln,device);
            dictionary_set(ini,uuidln,device);
            break;

        case 3:				/* sdc1  xfs                    2b2e8ae3-6339-4df1-8f06-e91a16f3e424 */
            debug("at protocol %s case 3 dictionary_set(ini,%s,%s)\n",protocol, uuidln,device);
            dictionary_set(ini,uuidln,device);
            break;

        case 4:          /* sdd8  swap   sdd8F24swap     5c02759a-da32-40e0-9e85-4cab6fb02c94 */
            debug("at protocol=%s case 4   uuidln=%s. device=%s label=%s\n",protocol,uuidln,device,label);
            dictionary_set(ini,uuidln,device);
            if (*label!=nullchar)
                dictionary_set(ini,label,device);
            break;

        case 5:				/* sdb2  ext4   sdb2scratch     6e488205-8791-41c2-8043-5051f8d0b185 /scratch */
            debug("at protocol=%s case 5   uuidln=%s. device=%s label=%s\n",protocol,uuidln,device,label);
            if ( *label != nullchar)
                dictionary_set(ini,label,device);
            dictionary_set(ini,uuidln,device);

        default:
            break;
        }
    }
    if(i==100)
    {
        fprintf(stderr,"\n%s\n",work);
        fprintf(stderr,"i=%d, device=%s,protocol=%s label=%s, uuidln=%s mount=%s\n",i,device,protocol,label,uuidln,mount);
    }
    return;
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
    int  recordno=0;
    ini=dictionary_new(60,"uuid"); /* uuid is global */
    if(ini==NULL)
        exit(99);

    close(mkstemps(devdiskfile,4));

    sprintf(buffer,"/usr/bin/lsblk -f -l  >%s",devdiskfile);
    system(buffer);
#ifdef NDEBUG
    sprintf(buffer,"cat %s",devdiskfile);
    system( buffer);
#endif
    debug("devdiskfile=%s\n",devdiskfile);
    filein=fopen(devdiskfile,"rb");
    if(filein==NULL)
    {
        fprintf(stderr,"Can't open %s\n",devdiskfile);
        return ini;
    }
    fgets(buffer,sizeof(buffer),filein);

    while(!feof(filein))
    {
        fgets(buffer,sizeof(buffer),filein);
        if(*buffer==nullchar || *buffer=='N')
        {
            debug("Rejecting: %s",buffer);
            continue;
        }
        debug("buffer=%s\n",buffer);
        recordno++;
        Dictionary_fill_Entries(buffer);
    }
    fclose(filein);
    unlink(devdiskfile);
#ifdef NDEBUG
    fprintf(stderr,"dumping dictionary\n");
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
    int c;
    int err=0;
    *outfile=nullchar;
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

    while(  (c=(getopt(argc,argv,"HhI:i:o:O:")))  !=-1 )
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
            fprintf(stderr,"%s [Optonal -i AlternateInput] [-o alternateOutput] -h This message]\n", pgm);
            fprintf(stderr,"\tWithout arguments %s reads /etc/fstab and writes to standard output\n",pgm);
            fprintf(stderr,"\nUse as: %s -i Your_Alternate_Input  -o Your.output.file\n",pgm);
            fprintf(stderr,"%s reads the input file and appends the device info to it.\n",pgm);
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
        exit(1);
    fin=fopen(fstab,"rb");
    if(fin==NULL)
    {
        fprintf(stderr,"Can't open file \"%s\" for reading\n",fstab);
        exit(89);
    }
    if(*outfile != nullchar)
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
    debug("entering create dictionary\n");
    ini=create_dictionary();
    fstabToDictMatch(fout);

    return 0;
}
