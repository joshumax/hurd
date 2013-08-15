/* bunzip2 engine, modified by okuji@kuicr.kyoto-u.ac.jp. */

/* Stolen from util.c. */
#include <stdio.h>
#include <sys/types.h>

/* I/O interface */
extern int (*unzip_read) (char *buf, size_t maxread);
extern void (*unzip_write) (const char *buf, size_t nwrite);
extern void (*unzip_read_error) (void);
extern void (*unzip_error) (const char *msg);

/* bzip2 doesn't require window sliding. Just for buffering. */
#define INBUFSIZ	0x1000
#define OUTBUFSIZ	0x1000

static unsigned char inbuf[INBUFSIZ];
static unsigned char outbuf[OUTBUFSIZ];
static unsigned inptr;
static unsigned insize;
static unsigned outcnt;

/* ===========================================================================
 * Fill the input buffer. This is called only when the buffer is empty.
 */
static int
fill_inbuf (int eof_ok)
{
  int len;
  
  /* Read as much as possible */
  insize = 0;
  do
    {
      len = (*unzip_read)((char*)inbuf+insize, INBUFSIZ-insize);
      if (len == 0 || len == EOF)
	break;
      insize += len;
    }
  while (insize < INBUFSIZ);
  
  if (insize == 0)
    {
      if (eof_ok)
	return EOF;
      unzip_read_error();
    }
  
  inptr = 1;
  return inbuf[0];
}

static void
flush_outbuf (void)
{
  if (outcnt == 0)
    return;

  (*unzip_write) ((char *) outbuf, outcnt);
  outcnt = 0;
}

static inline int
bz2_getc (void *stream)
{
  return inptr < insize ? inbuf[inptr++] : fill_inbuf (1);
}

static inline int
bz2_putc (int c, void *stream)
{
  if (outcnt == OUTBUFSIZ)
    flush_outbuf ();
  outbuf[outcnt++] = c;
  return c;
}

static inline int
bz2_ferror (void *stream)
{
  return 0;
}

static inline int
bz2_fflush (void *stream)
{
  flush_outbuf ();
  return 0;
}

static inline int
bz2_fclose (void *stream)
{
  flush_outbuf ();
  return 0;
}

#define fprintf(s, f...)	/**/


/*-----------------------------------------------------------*/
/*--- A block-sorting, lossless compressor        bzip2.c ---*/
/*-----------------------------------------------------------*/

/*--
  This program is bzip2, a lossless, block-sorting data compressor,
  version 0.1pl2, dated 29-Aug-1997.

  Copyright (C) 1996, 1997 by Julian Seward.
     Guildford, Surrey, UK
     email: jseward@acm.org

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

  The GNU General Public License is contained in the file LICENSE.

  This program is based on (at least) the work of:
     Mike Burrows
     David Wheeler
     Peter Fenwick
     Alistair Moffat
     Radford Neal
     Ian H. Witten
     Robert Sedgewick
     Jon L. Bentley

  For more information on these sources, see the file ALGORITHMS.
--*/

/*----------------------------------------------------*/
/*--- IMPORTANT                                    ---*/
/*----------------------------------------------------*/

/*--
   WARNING:
      This program (attempts to) compress data by performing several
      non-trivial transformations on it.  Unless you are 100% familiar
      with *all* the algorithms contained herein, and with the
      consequences of modifying them, you should NOT meddle with the
      compression or decompression machinery.  Incorrect changes can
      and very likely *will* lead to disastrous loss of data.

   DISCLAIMER:
      I TAKE NO RESPONSIBILITY FOR ANY LOSS OF DATA ARISING FROM THE
      USE OF THIS PROGRAM, HOWSOEVER CAUSED.

      Every compression of a file implies an assumption that the
      compressed file can be decompressed to reproduce the original.
      Great efforts in design, coding and testing have been made to
      ensure that this program works correctly.  However, the
      complexity of the algorithms, and, in particular, the presence
      of various special cases in the code which occur with very low
      but non-zero probability make it impossible to rule out the
      possibility of bugs remaining in the program.  DO NOT COMPRESS
      ANY DATA WITH THIS PROGRAM UNLESS YOU ARE PREPARED TO ACCEPT THE
      POSSIBILITY, HOWEVER SMALL, THAT THE DATA WILL NOT BE RECOVERABLE.

      That is not to say this program is inherently unreliable.
      Indeed, I very much hope the opposite is true.  bzip2 has been
      carefully constructed and extensively tested.

   PATENTS:
      To the best of my knowledge, bzip2 does not use any patented
      algorithms.  However, I do not have the resources available to
      carry out a full patent search.  Therefore I cannot give any
      guarantee of the above statement.
--*/



/*----------------------------------------------------*/
/*--- and now for something much more pleasant :-) ---*/
/*----------------------------------------------------*/

/*---------------------------------------------*/
/*--
  Place a 1 beside your platform, and 0 elsewhere.
--*/

/*--
  Generic 32-bit Unix.
  Also works on 64-bit Unix boxes.
--*/
#define BZ_UNIX      1

/*--
  Win32, as seen by Jacob Navia's excellent
  port of (Chris Fraser & David Hanson)'s excellent
  lcc compiler.
--*/
#define BZ_LCCWIN32  0



/*---------------------------------------------*/
/*--
  Some stuff for all platforms.
--*/

#include <stdio.h>
#include <stdlib.h>
#if DEBUG
  #include <assert.h>
#endif
#include <string.h>
#include <signal.h>
#include <math.h>

#define ERROR_IF_EOF(i)       { if ((i) == EOF)  ioError(); }
#define ERROR_IF_NOT_ZERO(i)  { if ((i) != 0)    ioError(); }
#define ERROR_IF_MINUS_ONE(i) { if ((i) == (-1)) ioError(); }


/*---------------------------------------------*/
/*--
   Platform-specific stuff.
--*/

#if BZ_UNIX
   #include <sys/types.h>
   #include <utime.h>
   #include <unistd.h>
   #include <malloc.h>
   #include <sys/stat.h>
   #include <sys/times.h>

   #define Int32   int
   #define UInt32  unsigned int
   #define Char    char
   #define UChar   unsigned char
   #define Int16   short
   #define UInt16  unsigned short

   #define PATH_SEP    '/'
   #define MY_LSTAT    lstat
   #define MY_S_IFREG  S_ISREG
   #define MY_STAT     stat

   #define APPEND_FILESPEC(root, name) \
      root=snocString((root), (name))

   #define SET_BINARY_MODE(fd) /**/

   /*--
      You should try very hard to persuade your C compiler
      to inline the bits marked INLINE.  Otherwise bzip2 will
      run rather slowly.  gcc version 2.x is recommended.
   --*/
   #ifdef __GNUC__
      #define INLINE   inline
      #define NORETURN __attribute__ ((noreturn))
   #else
      #define INLINE   /**/
      #define NORETURN /**/
   #endif
#endif



#if BZ_LCCWIN32
   #include <io.h>
   #include <fcntl.h>
   #include <sys\stat.h>

   #define Int32   int
   #define UInt32  unsigned int
   #define Int16   short
   #define UInt16  unsigned short
   #define Char    char
   #define UChar   unsigned char

   #define INLINE         /**/
   #define NORETURN       /**/
   #define PATH_SEP       '\\'
   #define MY_LSTAT       _stat
   #define MY_STAT        _stat
   #define MY_S_IFREG(x)  ((x) & _S_IFREG)

   #if 0
   /*-- lcc-win32 seems to expand wildcards itself --*/
   #define APPEND_FILESPEC(root, spec)                \
      do {                                            \
         if ((spec)[0] == '-') {                      \
            root = snocString((root), (spec));        \
         } else {                                     \
            struct _finddata_t c_file;                \
            long hFile;                               \
            hFile = _findfirst((spec), &c_file);      \
            if ( hFile == -1L ) {                     \
               root = snocString ((root), (spec));    \
            } else {                                  \
               int anInt = 0;                         \
               while ( anInt == 0 ) {                 \
                  root = snocString((root),           \
                            &c_file.name[0]);         \
                  anInt = _findnext(hFile, &c_file);  \
               }                                      \
            }                                         \
         }                                            \
      } while ( 0 )
   #else
   #define APPEND_FILESPEC(root, name)                \
      root = snocString ((root), (name))
   #endif

   #define SET_BINARY_MODE(fd)                        \
      do {                                            \
         int retVal = setmode ( fileno ( fd ),        \
                               O_BINARY );            \
         ERROR_IF_MINUS_ONE ( retVal );               \
      } while ( 0 )

#endif


/*---------------------------------------------*/
/*--
  Some more stuff for all platforms :-)
--*/

#define Bool   unsigned char
#define True   1
#define False  0

/*--
  IntNative is your platform's `native' int size.
  Only here to avoid probs with 64-bit platforms.
--*/
#define IntNative int


/*--
   change to 1, or compile with -DDEBUG=1 to debug
--*/
#ifndef DEBUG
#define DEBUG 0
#endif


/*---------------------------------------------------*/
/*---                                             ---*/
/*---------------------------------------------------*/

/*--
   Implementation notes, July 1997
   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

   Memory allocation
   ~~~~~~~~~~~~~~~~~
   All large data structures are allocated on the C heap,
   for better or for worse.  That includes the various
   arrays of pointers, striped words, bytes, frequency
   tables and buffers for compression and decompression.

   bzip2 can operate at various block-sizes, ranging from
   100k to 900k in 100k steps, and it allocates only as
   much as it needs to.  When compressing, we know from the
   command-line options what the block-size is going to be,
   so all allocation can be done at start-up; if that
   succeeds, there can be no further allocation problems.

   Decompression is more complicated.  Each compressed file
   contains, in its header, a byte indicating the block
   size used for compression.  This means bzip2 potentially
   needs to reallocate memory for each file it deals with,
   which in turn opens the possibility for a memory allocation
   failure part way through a run of files, by encountering
   a file requiring a much larger block size than all the
   ones preceding it.

   The policy is to simply give up if a memory allocation
   failure occurs.  During decompression, it would be
   possible to move on to subsequent files in the hope that
   some might ask for a smaller block size, but the
   complications for doing this seem more trouble than they
   are worth.


   Compressed file formats
   ~~~~~~~~~~~~~~~~~~~~~~~
   [This is now entirely different from both 0.21, and from
    any previous Huffman-coded variant of bzip.
    See the associated file bzip2.txt for details.]


   Error conditions
   ~~~~~~~~~~~~~~~~
   Dealing with error conditions is the least satisfactory
   aspect of bzip2.  The policy is to try and leave the
   filesystem in a consistent state, then quit, even if it
   means not processing some of the files mentioned in the
   command line.  `A consistent state' means that a file
   exists either in its compressed or uncompressed form,
   but not both.  This boils down to the rule `delete the
   output file if an error condition occurs, leaving the
   input intact'.  Input files are only deleted when we can
   be pretty sure the output file has been written and
   closed successfully.

   Errors are a dog because there's so many things to
   deal with.  The following can happen mid-file, and
   require cleaning up.

     internal `panics' -- indicating a bug
     corrupted or inconsistent compressed file
     can't allocate enough memory to decompress this file
     I/O error reading/writing/opening/closing
     signal catches -- Control-C, SIGTERM, SIGHUP.

   Other conditions, primarily pertaining to file names,
   can be checked in-between files, which makes dealing
   with them easier.
--*/



/*---------------------------------------------------*/
/*--- Misc (file handling) data decls             ---*/
/*---------------------------------------------------*/

static UInt32  bytesIn, bytesOut;
static Int32   verbosity;
static Bool    smallMode;
static UInt32  globalCrc;




static void    panic                 ( Char* );
static void    ioError               ( void );
static void    uncompressOutOfMemory ( Int32, Int32 );
static void    blockOverrun          ( void );
static void    badBlockHeader        ( void );
static void    crcError              ( UInt32, UInt32 );
static void    cleanUpAndFail        ( Int32 );
static void    compressedStreamEOF   ( void );



/*---------------------------------------------------*/
/*--- Data decls for the front end                ---*/
/*---------------------------------------------------*/

/*--
   The overshoot bytes allow us to avoid most of
   the cost of pointer renormalisation during
   comparison of rotations in sorting.
   The figure of 20 is derived as follows:
      qSort3 allows an overshoot of up to 10.
      It then calls simpleSort, which calls
      fullGtU, also with max overshoot 10.
      fullGtU does up to 10 comparisons without
      renormalising, giving 10+10 == 20.
--*/
#define NUM_OVERSHOOT_BYTES 20

/*--
  These are the main data structures for
  the Burrows-Wheeler transform.
--*/

/*--
  Pointers to compression and decompression
  structures.  Set by
     allocateCompressStructures   and
     setDecompressStructureSizes

  The structures are always set to be suitable
  for a block of size 100000 * blockSize100k.
--*/
static UInt16   *ll16;     /*-- small decompress --*/
static UChar    *ll4;      /*-- small decompress --*/

static Int32    *tt;       /*-- fast decompress  --*/
static UChar    *ll8;      /*-- fast decompress  --*/


/*--
  freq table collected to save a pass over the data
  during decompression.
--*/
static Int32   unzftab[256];


/*--
   index of the last char in the block, so
   the block size == last + 1.
--*/
static Int32  last;


/*--
  index in zptr[] of original string after sorting.
--*/
static Int32  origPtr;


/*--
  always: in the range 0 .. 9.
  The current block size is 100000 * this number.
--*/
static Int32  blockSize100k;


/*--
  Used when sorting.  If too many long comparisons
  happen, we stop sorting, randomise the block 
  slightly, and try again.
--*/

static Bool   blockRandomised;



/*---------------------------------------------------*/
/*--- Data decls for the back end                 ---*/
/*---------------------------------------------------*/

#define MAX_ALPHA_SIZE 258
#define MAX_CODE_LEN    23

#define RUNA 0
#define RUNB 1

#define N_GROUPS 6
#define G_SIZE   50
#define N_ITERS  4

#define MAX_SELECTORS (2 + (900000 / G_SIZE))

static Bool  inUse[256];
static Int32 nInUse;

static UChar seqToUnseq[256];
static UChar unseqToSeq[256];

static UChar selector   [MAX_SELECTORS];
static UChar selectorMtf[MAX_SELECTORS];

static UChar len  [N_GROUPS][MAX_ALPHA_SIZE];

/*-- decompress only --*/
static Int32 limit  [N_GROUPS][MAX_ALPHA_SIZE];
static Int32 base   [N_GROUPS][MAX_ALPHA_SIZE];
static Int32 perm   [N_GROUPS][MAX_ALPHA_SIZE];
static Int32 minLens[N_GROUPS];


/*---------------------------------------------------*/
/*--- 32-bit CRC grunge                           ---*/
/*---------------------------------------------------*/

/*--
  I think this is an implementation of the AUTODIN-II,
  Ethernet & FDDI 32-bit CRC standard.  Vaguely derived
  from code by Rob Warnock, in Section 51 of the
  comp.compression FAQ.
--*/

static UInt32 crc32Table[256] = {

   /*-- Ugly, innit? --*/

   0x00000000UL, 0x04c11db7UL, 0x09823b6eUL, 0x0d4326d9UL,
   0x130476dcUL, 0x17c56b6bUL, 0x1a864db2UL, 0x1e475005UL,
   0x2608edb8UL, 0x22c9f00fUL, 0x2f8ad6d6UL, 0x2b4bcb61UL,
   0x350c9b64UL, 0x31cd86d3UL, 0x3c8ea00aUL, 0x384fbdbdUL,
   0x4c11db70UL, 0x48d0c6c7UL, 0x4593e01eUL, 0x4152fda9UL,
   0x5f15adacUL, 0x5bd4b01bUL, 0x569796c2UL, 0x52568b75UL,
   0x6a1936c8UL, 0x6ed82b7fUL, 0x639b0da6UL, 0x675a1011UL,
   0x791d4014UL, 0x7ddc5da3UL, 0x709f7b7aUL, 0x745e66cdUL,
   0x9823b6e0UL, 0x9ce2ab57UL, 0x91a18d8eUL, 0x95609039UL,
   0x8b27c03cUL, 0x8fe6dd8bUL, 0x82a5fb52UL, 0x8664e6e5UL,
   0xbe2b5b58UL, 0xbaea46efUL, 0xb7a96036UL, 0xb3687d81UL,
   0xad2f2d84UL, 0xa9ee3033UL, 0xa4ad16eaUL, 0xa06c0b5dUL,
   0xd4326d90UL, 0xd0f37027UL, 0xddb056feUL, 0xd9714b49UL,
   0xc7361b4cUL, 0xc3f706fbUL, 0xceb42022UL, 0xca753d95UL,
   0xf23a8028UL, 0xf6fb9d9fUL, 0xfbb8bb46UL, 0xff79a6f1UL,
   0xe13ef6f4UL, 0xe5ffeb43UL, 0xe8bccd9aUL, 0xec7dd02dUL,
   0x34867077UL, 0x30476dc0UL, 0x3d044b19UL, 0x39c556aeUL,
   0x278206abUL, 0x23431b1cUL, 0x2e003dc5UL, 0x2ac12072UL,
   0x128e9dcfUL, 0x164f8078UL, 0x1b0ca6a1UL, 0x1fcdbb16UL,
   0x018aeb13UL, 0x054bf6a4UL, 0x0808d07dUL, 0x0cc9cdcaUL,
   0x7897ab07UL, 0x7c56b6b0UL, 0x71159069UL, 0x75d48ddeUL,
   0x6b93dddbUL, 0x6f52c06cUL, 0x6211e6b5UL, 0x66d0fb02UL,
   0x5e9f46bfUL, 0x5a5e5b08UL, 0x571d7dd1UL, 0x53dc6066UL,
   0x4d9b3063UL, 0x495a2dd4UL, 0x44190b0dUL, 0x40d816baUL,
   0xaca5c697UL, 0xa864db20UL, 0xa527fdf9UL, 0xa1e6e04eUL,
   0xbfa1b04bUL, 0xbb60adfcUL, 0xb6238b25UL, 0xb2e29692UL,
   0x8aad2b2fUL, 0x8e6c3698UL, 0x832f1041UL, 0x87ee0df6UL,
   0x99a95df3UL, 0x9d684044UL, 0x902b669dUL, 0x94ea7b2aUL,
   0xe0b41de7UL, 0xe4750050UL, 0xe9362689UL, 0xedf73b3eUL,
   0xf3b06b3bUL, 0xf771768cUL, 0xfa325055UL, 0xfef34de2UL,
   0xc6bcf05fUL, 0xc27dede8UL, 0xcf3ecb31UL, 0xcbffd686UL,
   0xd5b88683UL, 0xd1799b34UL, 0xdc3abdedUL, 0xd8fba05aUL,
   0x690ce0eeUL, 0x6dcdfd59UL, 0x608edb80UL, 0x644fc637UL,
   0x7a089632UL, 0x7ec98b85UL, 0x738aad5cUL, 0x774bb0ebUL,
   0x4f040d56UL, 0x4bc510e1UL, 0x46863638UL, 0x42472b8fUL,
   0x5c007b8aUL, 0x58c1663dUL, 0x558240e4UL, 0x51435d53UL,
   0x251d3b9eUL, 0x21dc2629UL, 0x2c9f00f0UL, 0x285e1d47UL,
   0x36194d42UL, 0x32d850f5UL, 0x3f9b762cUL, 0x3b5a6b9bUL,
   0x0315d626UL, 0x07d4cb91UL, 0x0a97ed48UL, 0x0e56f0ffUL,
   0x1011a0faUL, 0x14d0bd4dUL, 0x19939b94UL, 0x1d528623UL,
   0xf12f560eUL, 0xf5ee4bb9UL, 0xf8ad6d60UL, 0xfc6c70d7UL,
   0xe22b20d2UL, 0xe6ea3d65UL, 0xeba91bbcUL, 0xef68060bUL,
   0xd727bbb6UL, 0xd3e6a601UL, 0xdea580d8UL, 0xda649d6fUL,
   0xc423cd6aUL, 0xc0e2d0ddUL, 0xcda1f604UL, 0xc960ebb3UL,
   0xbd3e8d7eUL, 0xb9ff90c9UL, 0xb4bcb610UL, 0xb07daba7UL,
   0xae3afba2UL, 0xaafbe615UL, 0xa7b8c0ccUL, 0xa379dd7bUL,
   0x9b3660c6UL, 0x9ff77d71UL, 0x92b45ba8UL, 0x9675461fUL,
   0x8832161aUL, 0x8cf30badUL, 0x81b02d74UL, 0x857130c3UL,
   0x5d8a9099UL, 0x594b8d2eUL, 0x5408abf7UL, 0x50c9b640UL,
   0x4e8ee645UL, 0x4a4ffbf2UL, 0x470cdd2bUL, 0x43cdc09cUL,
   0x7b827d21UL, 0x7f436096UL, 0x7200464fUL, 0x76c15bf8UL,
   0x68860bfdUL, 0x6c47164aUL, 0x61043093UL, 0x65c52d24UL,
   0x119b4be9UL, 0x155a565eUL, 0x18197087UL, 0x1cd86d30UL,
   0x029f3d35UL, 0x065e2082UL, 0x0b1d065bUL, 0x0fdc1becUL,
   0x3793a651UL, 0x3352bbe6UL, 0x3e119d3fUL, 0x3ad08088UL,
   0x2497d08dUL, 0x2056cd3aUL, 0x2d15ebe3UL, 0x29d4f654UL,
   0xc5a92679UL, 0xc1683bceUL, 0xcc2b1d17UL, 0xc8ea00a0UL,
   0xd6ad50a5UL, 0xd26c4d12UL, 0xdf2f6bcbUL, 0xdbee767cUL,
   0xe3a1cbc1UL, 0xe760d676UL, 0xea23f0afUL, 0xeee2ed18UL,
   0xf0a5bd1dUL, 0xf464a0aaUL, 0xf9278673UL, 0xfde69bc4UL,
   0x89b8fd09UL, 0x8d79e0beUL, 0x803ac667UL, 0x84fbdbd0UL,
   0x9abc8bd5UL, 0x9e7d9662UL, 0x933eb0bbUL, 0x97ffad0cUL,
   0xafb010b1UL, 0xab710d06UL, 0xa6322bdfUL, 0xa2f33668UL,
   0xbcb4666dUL, 0xb8757bdaUL, 0xb5365d03UL, 0xb1f740b4UL
};


/*---------------------------------------------*/

static void initialiseCRC ( void )
{
   globalCrc = 0xffffffffUL;
}


/*---------------------------------------------*/

static UInt32 getFinalCRC ( void )
{
   return ~globalCrc;
}


/*---------------------------------------------*/

static UInt32 getGlobalCRC ( void )
{
   return globalCrc;
}


/*---------------------------------------------*/

static void setGlobalCRC ( UInt32 newCrc )
{
   globalCrc = newCrc;
}


/*---------------------------------------------*/

#define UPDATE_CRC(crcVar,cha)              \
{                                           \
   crcVar = (crcVar << 8) ^                 \
            crc32Table[(crcVar >> 24) ^     \
                       ((UChar)cha)];       \
}


/*---------------------------------------------------*/
/*--- Bit stream I/O                              ---*/
/*---------------------------------------------------*/


static UInt32 bsBuff;
static Int32  bsLive;
static void*  bsStream;
static Bool   bsWriting;


/*---------------------------------------------*/

static void bsSetStream ( void* f, Bool wr )
{
   if (bsStream != NULL) panic ( "bsSetStream" );
   bsStream = f;
   bsLive = 0;
   bsBuff = 0;
   bytesOut = 0;
   bytesIn = 0;
   bsWriting = wr;
}


/*---------------------------------------------*/

static void bsFinishedWithStream ( void )
{
   if (bsWriting)
      while (bsLive > 0) {
         bz2_putc ( (UChar)(bsBuff >> 24), bsStream );
         bsBuff <<= 8;
         bsLive -= 8;
         bytesOut++;
      }
   bsStream = NULL;
}


/*---------------------------------------------*/

#define bsNEEDR(nz)                           \
{                                             \
   while (bsLive < nz) {                      \
      Int32 zzi = bz2_getc ( bsStream );      \
      if (zzi == EOF) compressedStreamEOF();  \
      bsBuff = (bsBuff << 8) | (zzi & 0xffL); \
      bsLive += 8;                            \
   }                                          \
}


/*---------------------------------------------*/

#define bsR1(vz)                              \
{                                             \
   bsNEEDR(1);                                \
   vz = (bsBuff >> (bsLive-1)) & 1;           \
   bsLive--;                                  \
}


/*---------------------------------------------*/

static INLINE UInt32 bsR ( Int32 n )
{
   UInt32 v;
   bsNEEDR ( n );
   v = (bsBuff >> (bsLive-n)) & ((1 << n)-1);
   bsLive -= n;
   return v;
}


/*---------------------------------------------*/

static UChar bsGetUChar ( void )
{
   return (UChar)bsR(8);
}



/*---------------------------------------------*/

static Int32 bsGetUInt32 ( void )
{
   UInt32 u;
   u = 0;
   u = (u << 8) | bsR(8);
   u = (u << 8) | bsR(8);
   u = (u << 8) | bsR(8);
   u = (u << 8) | bsR(8);
   return u;
}


/*---------------------------------------------*/

static UInt32 bsGetIntVS ( UInt32 numBits )
{
   return (UInt32)bsR(numBits);
}



/*---------------------------------------------------*/
/*--- Huffman coding low-level stuff              ---*/
/*---------------------------------------------------*/


/*---------------------------------------------*/

static void hbCreateDecodeTables ( Int32 *limit,
				   Int32 *base,
				   Int32 *perm,
				   UChar *length,
				   Int32 minLen,
				   Int32 maxLen,
				   Int32 alphaSize )
{
   Int32 pp, i, j, vec;

   pp = 0;
   for (i = minLen; i <= maxLen; i++)
      for (j = 0; j < alphaSize; j++)
         if (length[j] == i) { perm[pp] = j; pp++; };

   for (i = 0; i < MAX_CODE_LEN; i++) base[i] = 0;
   for (i = 0; i < alphaSize; i++) base[length[i]+1]++;

   for (i = 1; i < MAX_CODE_LEN; i++) base[i] += base[i-1];

   for (i = 0; i < MAX_CODE_LEN; i++) limit[i] = 0;
   vec = 0;

   for (i = minLen; i <= maxLen; i++) {
      vec += (base[i+1] - base[i]);
      limit[i] = vec-1;
      vec <<= 1;
   }
   for (i = minLen + 1; i <= maxLen; i++)
      base[i] = ((limit[i-1] + 1) << 1) - base[i];
}



/*---------------------------------------------------*/
/*--- Undoing the reversible transformation       ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/

#define SET_LL4(i,n)                                          \
   { if (((i) & 0x1) == 0)                                    \
        ll4[(i) >> 1] = (ll4[(i) >> 1] & 0xf0) | (n); else    \
        ll4[(i) >> 1] = (ll4[(i) >> 1] & 0x0f) | ((n) << 4);  \
   }


#define GET_LL4(i)                             \
    (((UInt32)(ll4[(i) >> 1])) >> (((i) << 2) & 0x4) & 0xF)


#define SET_LL(i,n)                       \
   { ll16[i] = (UInt16)(n & 0x0000ffff);  \
     SET_LL4(i, n >> 16);                 \
   }


#define GET_LL(i) \
   (((UInt32)ll16[i]) | (GET_LL4(i) << 16))


/*---------------------------------------------*/
/*--
  Manage memory for compression/decompression.
  When compressing, a single block size applies to
  all files processed, and that's set when the
  program starts.  But when decompressing, each file
  processed could have been compressed with a
  different block size, so we may have to free
  and reallocate on a per-file basis.

  A call with argument of zero means
  `free up everything.'  And a value of zero for
  blockSize100k means no memory is currently allocated.
--*/


/*---------------------------------------------*/

static void setDecompressStructureSizes ( Int32 newSize100k )
{
   if (! (0 <= newSize100k   && newSize100k   <= 9 &&
          0 <= blockSize100k && blockSize100k <= 9))
      panic ( "setDecompressStructureSizes" );

   if (newSize100k == blockSize100k) return;

   blockSize100k = newSize100k;

   if (ll16  != NULL) free ( ll16  );
   if (ll4   != NULL) free ( ll4   );
   if (ll8   != NULL) free ( ll8   );
   if (tt    != NULL) free ( tt    );

   if (newSize100k == 0) return;

   if (smallMode) {

      Int32 n = 100000 * newSize100k;
      ll16    = malloc ( n * sizeof(UInt16) );
      ll4     = malloc ( ((n+1) >> 1) * sizeof(UChar) );

      if (ll4 == NULL || ll16 == NULL) {
         Int32 totalDraw
            = n * sizeof(Int16) + ((n+1) >> 1) * sizeof(UChar);
         uncompressOutOfMemory ( totalDraw, n );
      }

   } else {

      Int32 n = 100000 * newSize100k;
      ll8     = malloc ( n * sizeof(UChar) );
      tt      = malloc ( n * sizeof(Int32) );

      if (ll8 == NULL || tt == NULL) {
         Int32 totalDraw
            = n * sizeof(UChar) + n * sizeof(UInt32);
         uncompressOutOfMemory ( totalDraw, n );
      }

   }
}



/*---------------------------------------------------*/
/*--- The new back end                            ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/

static void makeMaps ( void )
{
   Int32 i;
   nInUse = 0;
   for (i = 0; i < 256; i++)
      if (inUse[i]) {
         seqToUnseq[nInUse] = i;
         unseqToSeq[i] = nInUse;
         nInUse++;
      }
}



/*---------------------------------------------*/

static void recvDecodingTables ( void )
{
   Int32 i, j, t, nGroups, nSelectors, alphaSize;
   Int32 minLen, maxLen;
   Bool inUse16[16];

   /*--- Receive the mapping table ---*/
   for (i = 0; i < 16; i++)
      if (bsR(1) == 1) 
         inUse16[i] = True; else 
         inUse16[i] = False;

   for (i = 0; i < 256; i++) inUse[i] = False;

   for (i = 0; i < 16; i++)
      if (inUse16[i])
         for (j = 0; j < 16; j++)
            if (bsR(1) == 1) inUse[i * 16 + j] = True;

   makeMaps();
   alphaSize = nInUse+2;

   /*--- Now the selectors ---*/
   nGroups = bsR ( 3 );
   nSelectors = bsR ( 15 );
   for (i = 0; i < nSelectors; i++) {
      j = 0;
      while (bsR(1) == 1) j++;
      selectorMtf[i] = j;
   }

   /*--- Undo the MTF values for the selectors. ---*/
   {
      UChar pos[N_GROUPS], tmp, v;
      for (v = 0; v < nGroups; v++) pos[v] = v;
   
      for (i = 0; i < nSelectors; i++) {
         v = selectorMtf[i];
         tmp = pos[v];
         while (v > 0) { pos[v] = pos[v-1]; v--; }
         pos[0] = tmp;
         selector[i] = tmp;
      }
   }

   /*--- Now the coding tables ---*/
   for (t = 0; t < nGroups; t++) {
      Int32 curr = bsR ( 5 );
      for (i = 0; i < alphaSize; i++) {
         while (bsR(1) == 1) {
            if (bsR(1) == 0) curr++; else curr--;
         }
         len[t][i] = curr;
      }
   }

   /*--- Create the Huffman decoding tables ---*/
   for (t = 0; t < nGroups; t++) {
      minLen = 32;
      maxLen = 0;
      for (i = 0; i < alphaSize; i++) {
         if (len[t][i] > maxLen) maxLen = len[t][i];
         if (len[t][i] < minLen) minLen = len[t][i];
      }
      hbCreateDecodeTables ( 
         &limit[t][0], &base[t][0], &perm[t][0], &len[t][0],
         minLen, maxLen, alphaSize
      );
      minLens[t] = minLen;
   }
}


/*---------------------------------------------*/

#define GET_MTF_VAL(lval)                 \
{                                         \
   Int32 zt, zn, zvec, zj;                \
   if (groupPos == 0) {                   \
      groupNo++;                          \
      groupPos = G_SIZE;                  \
   }                                      \
   groupPos--;                            \
   zt = selector[groupNo];                \
   zn = minLens[zt];                      \
   zvec = bsR ( zn );                     \
   while (zvec > limit[zt][zn]) {         \
      zn++; bsR1(zj);                     \
      zvec = (zvec << 1) | zj;            \
   };                                     \
   lval = perm[zt][zvec - base[zt][zn]];  \
}


/*---------------------------------------------*/

static void getAndMoveToFrontDecode ( void )
{
   UChar  yy[256];
   Int32  i, j, nextSym, limitLast;
   Int32  EOB, groupNo, groupPos;

   limitLast = 100000 * blockSize100k;
   origPtr   = bsGetIntVS ( 24 );

   recvDecodingTables();
   EOB      = nInUse+1;
   groupNo  = -1;
   groupPos = 0;

   /*--
      Setting up the unzftab entries here is not strictly
      necessary, but it does save having to do it later
      in a separate pass, and so saves a block's worth of
      cache misses.
   --*/
   for (i = 0; i <= 255; i++) unzftab[i] = 0;

   for (i = 0; i <= 255; i++) yy[i] = (UChar) i;

   last = -1;

   GET_MTF_VAL(nextSym);

   while (True) {

      if (nextSym == EOB) break;

      if (nextSym == RUNA || nextSym == RUNB) {
         UChar ch;
         Int32 s = -1;
         Int32 N = 1;
         do {
            if (nextSym == RUNA) s = s + (0+1) * N; else
            if (nextSym == RUNB) s = s + (1+1) * N;
            N = N * 2;
            GET_MTF_VAL(nextSym);
         }
            while (nextSym == RUNA || nextSym == RUNB);

         s++;
         ch = seqToUnseq[yy[0]];
         unzftab[ch] += s;

         if (smallMode)
            while (s > 0) {
               last++; 
               ll16[last] = ch;
               s--;
            }
         else
            while (s > 0) {
               last++;
               ll8[last] = ch;
               s--;
            };

         if (last >= limitLast) blockOverrun();
         continue;

      } else {

         UChar tmp;
         last++; if (last >= limitLast) blockOverrun();

         tmp = yy[nextSym-1];
         unzftab[seqToUnseq[tmp]]++;
         if (smallMode)
            ll16[last] = seqToUnseq[tmp]; else
            ll8[last]  = seqToUnseq[tmp];

         /*--
            This loop is hammered during decompression,
            hence the unrolling.

            for (j = nextSym-1; j > 0; j--) yy[j] = yy[j-1];
         --*/

         j = nextSym-1;
         for (; j > 3; j -= 4) {
            yy[j]   = yy[j-1];
            yy[j-1] = yy[j-2];
            yy[j-2] = yy[j-3];
            yy[j-3] = yy[j-4];
         }
         for (; j > 0; j--) yy[j] = yy[j-1];

         yy[0] = tmp;
         GET_MTF_VAL(nextSym);
         continue;
      }
   }
}


/*---------------------------------------------------*/
/*--- Stuff for randomising repetitive blocks     ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/
static Int32 rNums[512] = { 
   619, 720, 127, 481, 931, 816, 813, 233, 566, 247, 
   985, 724, 205, 454, 863, 491, 741, 242, 949, 214, 
   733, 859, 335, 708, 621, 574, 73, 654, 730, 472, 
   419, 436, 278, 496, 867, 210, 399, 680, 480, 51, 
   878, 465, 811, 169, 869, 675, 611, 697, 867, 561, 
   862, 687, 507, 283, 482, 129, 807, 591, 733, 623, 
   150, 238, 59, 379, 684, 877, 625, 169, 643, 105, 
   170, 607, 520, 932, 727, 476, 693, 425, 174, 647, 
   73, 122, 335, 530, 442, 853, 695, 249, 445, 515, 
   909, 545, 703, 919, 874, 474, 882, 500, 594, 612, 
   641, 801, 220, 162, 819, 984, 589, 513, 495, 799, 
   161, 604, 958, 533, 221, 400, 386, 867, 600, 782, 
   382, 596, 414, 171, 516, 375, 682, 485, 911, 276, 
   98, 553, 163, 354, 666, 933, 424, 341, 533, 870, 
   227, 730, 475, 186, 263, 647, 537, 686, 600, 224, 
   469, 68, 770, 919, 190, 373, 294, 822, 808, 206, 
   184, 943, 795, 384, 383, 461, 404, 758, 839, 887, 
   715, 67, 618, 276, 204, 918, 873, 777, 604, 560, 
   951, 160, 578, 722, 79, 804, 96, 409, 713, 940, 
   652, 934, 970, 447, 318, 353, 859, 672, 112, 785, 
   645, 863, 803, 350, 139, 93, 354, 99, 820, 908, 
   609, 772, 154, 274, 580, 184, 79, 626, 630, 742, 
   653, 282, 762, 623, 680, 81, 927, 626, 789, 125, 
   411, 521, 938, 300, 821, 78, 343, 175, 128, 250, 
   170, 774, 972, 275, 999, 639, 495, 78, 352, 126, 
   857, 956, 358, 619, 580, 124, 737, 594, 701, 612, 
   669, 112, 134, 694, 363, 992, 809, 743, 168, 974, 
   944, 375, 748, 52, 600, 747, 642, 182, 862, 81, 
   344, 805, 988, 739, 511, 655, 814, 334, 249, 515, 
   897, 955, 664, 981, 649, 113, 974, 459, 893, 228, 
   433, 837, 553, 268, 926, 240, 102, 654, 459, 51, 
   686, 754, 806, 760, 493, 403, 415, 394, 687, 700, 
   946, 670, 656, 610, 738, 392, 760, 799, 887, 653, 
   978, 321, 576, 617, 626, 502, 894, 679, 243, 440, 
   680, 879, 194, 572, 640, 724, 926, 56, 204, 700, 
   707, 151, 457, 449, 797, 195, 791, 558, 945, 679, 
   297, 59, 87, 824, 713, 663, 412, 693, 342, 606, 
   134, 108, 571, 364, 631, 212, 174, 643, 304, 329, 
   343, 97, 430, 751, 497, 314, 983, 374, 822, 928, 
   140, 206, 73, 263, 980, 736, 876, 478, 430, 305, 
   170, 514, 364, 692, 829, 82, 855, 953, 676, 246, 
   369, 970, 294, 750, 807, 827, 150, 790, 288, 923, 
   804, 378, 215, 828, 592, 281, 565, 555, 710, 82, 
   896, 831, 547, 261, 524, 462, 293, 465, 502, 56, 
   661, 821, 976, 991, 658, 869, 905, 758, 745, 193, 
   768, 550, 608, 933, 378, 286, 215, 979, 792, 961, 
   61, 688, 793, 644, 986, 403, 106, 366, 905, 644, 
   372, 567, 466, 434, 645, 210, 389, 550, 919, 135, 
   780, 773, 635, 389, 707, 100, 626, 958, 165, 504, 
   920, 176, 193, 713, 857, 265, 203, 50, 668, 108, 
   645, 990, 626, 197, 510, 357, 358, 850, 858, 364, 
   936, 638
};


#define RAND_DECLS                                \
   Int32 rNToGo = 0;                              \
   Int32 rTPos  = 0;                              \

#define RAND_MASK ((rNToGo == 1) ? 1 : 0)

#define RAND_UPD_MASK                             \
   if (rNToGo == 0) {                             \
      rNToGo = rNums[rTPos];                      \
      rTPos++; if (rTPos == 512) rTPos = 0;       \
   }                                              \
   rNToGo--;



/*---------------------------------------------------*/
/*--- The Reversible Transformation (tm)          ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/


static INLINE Int32 indexIntoF ( Int32 indx, Int32 *cftab )
{
   Int32 nb, na, mid;
   nb = 0;
   na = 256;
   do {
      mid = (nb + na) >> 1;
      if (indx >= cftab[mid]) nb = mid; else na = mid;
   }
   while (na - nb != 1);
   return nb;
}



#define GET_SMALL(cccc)                     \
                                            \
      cccc = indexIntoF ( tPos, cftab );    \
      tPos = GET_LL(tPos);



static void undoReversibleTransformation_small ( void* dst )
{
   Int32  cftab[257], cftabAlso[257];
   Int32  i, j, tmp, tPos;
   UChar  ch;

   /*--
      We assume here that the global array unzftab will
      already be holding the frequency counts for
      ll8[0 .. last].
   --*/

   /*-- Set up cftab to facilitate generation of indexIntoF --*/
   cftab[0] = 0;
   for (i = 1; i <= 256; i++) cftab[i] = unzftab[i-1];
   for (i = 1; i <= 256; i++) cftab[i] += cftab[i-1];

   /*-- Make a copy of it, used in generation of T --*/
   for (i = 0; i <= 256; i++) cftabAlso[i] = cftab[i];

   /*-- compute the T vector --*/
   for (i = 0; i <= last; i++) {
      ch = (UChar)ll16[i];
      SET_LL(i, cftabAlso[ch]);
      cftabAlso[ch]++;
   }

   /*--
      Compute T^(-1) by pointer reversal on T.  This is rather
      subtle, in that, if the original block was two or more
      (in general, N) concatenated copies of the same thing,
      the T vector will consist of N cycles, each of length
      blocksize / N, and decoding will involve traversing one
      of these cycles N times.  Which particular cycle doesn't
      matter -- they are all equivalent.  The tricky part is to
      make sure that the pointer reversal creates a correct
      reversed cycle for us to traverse.  So, the code below
      simply reverses whatever cycle origPtr happens to fall into,
      without regard to the cycle length.  That gives one reversed
      cycle, which for normal blocks, is the entire block-size long.
      For repeated blocks, it will be interspersed with the other
      N-1 non-reversed cycles.  Providing that the F-subscripting
      phase which follows starts at origPtr, all then works ok.
   --*/
   i = origPtr;
   j = GET_LL(i);
   do {
      tmp = GET_LL(j);
      SET_LL(j, i);
      i = j;
      j = tmp;
   }
      while (i != origPtr);

   /*--
      We recreate the original by subscripting F through T^(-1).
      The run-length-decoder below requires characters incrementally,
      so tPos is set to a starting value, and is updated by
      the GET_SMALL macro.
   --*/
   tPos   = origPtr;

   /*-------------------------------------------------*/
   /*--
      This is pretty much a verbatim copy of the
      run-length decoder present in the distribution
      bzip-0.21; it has to be here to avoid creating
      block[] as an intermediary structure.  As in 0.21,
      this code derives from some sent to me by
      Christian von Roques.

      It allows dst==NULL, so as to support the test (-t)
      option without slowing down the fast decompression
      code.
   --*/
   {
      IntNative retVal;
      Int32     i2, count, chPrev, ch2;
      UInt32    localCrc;

      count    = 0;
      i2       = 0;
      ch2      = 256;   /*-- not a char and not EOF --*/
      localCrc = getGlobalCRC();

      {
         RAND_DECLS;
         while ( i2 <= last ) {
            chPrev = ch2;
            GET_SMALL(ch2);
            if (blockRandomised) {
               RAND_UPD_MASK;
               ch2 ^= (UInt32)RAND_MASK;
            }
            i2++;
   
            if (dst)
               retVal = bz2_putc ( ch2, dst );
   
            UPDATE_CRC ( localCrc, (UChar)ch2 );
   
            if (ch2 != chPrev) {
               count = 1;
            } else {
               count++;
               if (count >= 4) {
                  Int32 j2;
                  UChar z;
                  GET_SMALL(z);
                  if (blockRandomised) {
                     RAND_UPD_MASK;
                     z ^= RAND_MASK;
                  }
                  for (j2 = 0;  j2 < (Int32)z;  j2++) {
                     if (dst) retVal = bz2_putc (ch2, dst);
                     UPDATE_CRC ( localCrc, (UChar)ch2 );
                  }
                  i2++;
                  count = 0;
               }
            }
         }
      }

      setGlobalCRC ( localCrc );
   }
   /*-- end of the in-line run-length-decoder. --*/
}
#undef GET_SMALL


/*---------------------------------------------*/

#define GET_FAST(cccc)                       \
                                             \
      cccc = ll8[tPos];                      \
      tPos = tt[tPos];



static void undoReversibleTransformation_fast ( void* dst )
{
   Int32  cftab[257];
   Int32  i, tPos;
   UChar  ch;

   /*--
      We assume here that the global array unzftab will
      already be holding the frequency counts for
      ll8[0 .. last].
   --*/

   /*-- Set up cftab to facilitate generation of T^(-1) --*/
   cftab[0] = 0;
   for (i = 1; i <= 256; i++) cftab[i] = unzftab[i-1];
   for (i = 1; i <= 256; i++) cftab[i] += cftab[i-1];

   /*-- compute the T^(-1) vector --*/
   for (i = 0; i <= last; i++) {
      ch = (UChar)ll8[i];
      tt[cftab[ch]] = i;
      cftab[ch]++;
   }

   /*--
      We recreate the original by subscripting L through T^(-1).
      The run-length-decoder below requires characters incrementally,
      so tPos is set to a starting value, and is updated by
      the GET_FAST macro.
   --*/
   tPos   = tt[origPtr];

   /*-------------------------------------------------*/
   /*--
      This is pretty much a verbatim copy of the
      run-length decoder present in the distribution
      bzip-0.21; it has to be here to avoid creating
      block[] as an intermediary structure.  As in 0.21,
      this code derives from some sent to me by
      Christian von Roques.
   --*/
   {
      IntNative retVal;
      Int32     i2, count, chPrev, ch2;
      UInt32    localCrc;

      count    = 0;
      i2       = 0;
      ch2      = 256;   /*-- not a char and not EOF --*/
      localCrc = getGlobalCRC();

      if (blockRandomised) {
         RAND_DECLS;
         while ( i2 <= last ) {
            chPrev = ch2;
            GET_FAST(ch2);
            RAND_UPD_MASK;
            ch2 ^= (UInt32)RAND_MASK;
            i2++;
   
            retVal = bz2_putc ( ch2, dst );
            UPDATE_CRC ( localCrc, (UChar)ch2 );
   
            if (ch2 != chPrev) {
               count = 1;
            } else {
               count++;
               if (count >= 4) {
                  Int32 j2;
                  UChar z;
                  GET_FAST(z);
                  RAND_UPD_MASK;
                  z ^= RAND_MASK;
                  for (j2 = 0;  j2 < (Int32)z;  j2++) {
                     retVal = bz2_putc (ch2, dst);
                     UPDATE_CRC ( localCrc, (UChar)ch2 );
                  }
                  i2++;
                  count = 0;
               }
            }
         }

      } else {

         while ( i2 <= last ) {
            chPrev = ch2;
            GET_FAST(ch2);
            i2++;
   
            retVal = bz2_putc ( ch2, dst );
            UPDATE_CRC ( localCrc, (UChar)ch2 );
   
            if (ch2 != chPrev) {
               count = 1;
            } else {
               count++;
               if (count >= 4) {
                  Int32 j2;
                  UChar z;
                  GET_FAST(z);
                  for (j2 = 0;  j2 < (Int32)z;  j2++) {
                     retVal = bz2_putc (ch2, dst);
                     UPDATE_CRC ( localCrc, (UChar)ch2 );
                  }
                  i2++;
                  count = 0;
               }
            }
         }

      }   /*-- if (blockRandomised) --*/

      setGlobalCRC ( localCrc );
   }
   /*-- end of the in-line run-length-decoder. --*/
}
#undef GET_FAST



/*---------------------------------------------------*/
/*--- Processing of complete files and streams    ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/

static Bool uncompressStream ( void *zStream, void *stream )
{
   UChar      magic1, magic2, magic3, magic4;
   UChar      magic5, magic6;
   UInt32     storedBlockCRC, storedCombinedCRC;
   UInt32     computedBlockCRC, computedCombinedCRC;
   Int32      currBlockNo;
   IntNative  retVal;

   SET_BINARY_MODE(stream);
   SET_BINARY_MODE(zStream);

   ERROR_IF_NOT_ZERO ( bz2_ferror(stream) );
   ERROR_IF_NOT_ZERO ( bz2_ferror(zStream) );

   bsSetStream ( zStream, False );

   /*--
      A bad magic number is `recoverable from';
      return with False so the caller skips the file.
   --*/
   magic1 = bsGetUChar ();
   magic2 = bsGetUChar ();
   magic3 = bsGetUChar ();
   magic4 = bsGetUChar ();
   if (magic1 != 'B' ||
       magic2 != 'Z' ||
       magic3 != 'h' ||
       magic4 < '1'  ||
       magic4 > '9') {
     bsFinishedWithStream();
     retVal = bz2_fclose ( stream );
     ERROR_IF_EOF ( retVal );
     return False;
   }

   setDecompressStructureSizes ( magic4 - '0' );
   computedCombinedCRC = 0;

   if (verbosity >= 2) fprintf ( stderr, "\n    " );
   currBlockNo = 0;

   while (True) {
      magic1 = bsGetUChar ();
      magic2 = bsGetUChar ();
      magic3 = bsGetUChar ();
      magic4 = bsGetUChar ();
      magic5 = bsGetUChar ();
      magic6 = bsGetUChar ();
      if (magic1 == 0x17 && magic2 == 0x72 &&
          magic3 == 0x45 && magic4 == 0x38 &&
          magic5 == 0x50 && magic6 == 0x90) break;

      if (magic1 != 0x31 || magic2 != 0x41 ||
          magic3 != 0x59 || magic4 != 0x26 ||
          magic5 != 0x53 || magic6 != 0x59) badBlockHeader();

      storedBlockCRC = bsGetUInt32 ();

      if (bsR(1) == 1)
         blockRandomised = True; else
         blockRandomised = False;

      currBlockNo++;
      if (verbosity >= 2)
         fprintf ( stderr, "[%d: huff+mtf ", currBlockNo );
      getAndMoveToFrontDecode ();
      ERROR_IF_NOT_ZERO ( bz2_ferror(zStream) );

      initialiseCRC();
      if (verbosity >= 2) fprintf ( stderr, "rt+rld" );
      if (smallMode)
         undoReversibleTransformation_small ( stream );
         else
         undoReversibleTransformation_fast  ( stream );

      ERROR_IF_NOT_ZERO ( bz2_ferror(stream) );

      computedBlockCRC = getFinalCRC();
      if (verbosity >= 3)
         fprintf ( stderr, " {0x%x, 0x%x}", storedBlockCRC, computedBlockCRC );
      if (verbosity >= 2) fprintf ( stderr, "] " );

      /*-- A bad CRC is considered a fatal error. --*/
      if (storedBlockCRC != computedBlockCRC)
         crcError ( storedBlockCRC, computedBlockCRC );

      computedCombinedCRC = (computedCombinedCRC << 1) | (computedCombinedCRC >> 31);
      computedCombinedCRC ^= computedBlockCRC;
   };

   if (verbosity >= 2) fprintf ( stderr, "\n    " );

   storedCombinedCRC  = bsGetUInt32 ();
   if (verbosity >= 2)
      fprintf ( stderr,
                "combined CRCs: stored = 0x%x, computed = 0x%x\n    ",
                storedCombinedCRC, computedCombinedCRC );
   if (storedCombinedCRC != computedCombinedCRC)
      crcError ( storedCombinedCRC, computedCombinedCRC );


   bsFinishedWithStream ();
   ERROR_IF_NOT_ZERO ( bz2_ferror(zStream) );
   retVal = bz2_fclose ( zStream );
   ERROR_IF_EOF ( retVal );

   ERROR_IF_NOT_ZERO ( bz2_ferror(stream) );
   retVal = bz2_fflush ( stream );
   ERROR_IF_NOT_ZERO ( retVal );
   return True;
}


#if 0

#endif
/*---------------------------------------------------*/
/*--- Error [non-] handling grunge                ---*/
/*---------------------------------------------------*/



static void
myFree (void **p)
{
  free (*p);
  *p = NULL;
}

/*---------------------------------------------*/
/* Ugg... Orignal code doesn't free dynamic allocated memories. */

static void cleanUpAndFail ( Int32 ec )
{
  myFree ((void **) &ll16);
  myFree ((void **) &ll4);
  myFree ((void **) &ll8);
  myFree ((void **) &tt);

  (*unzip_error) (NULL);
}


/*---------------------------------------------*/

static void panic ( Char* s )
{
   cleanUpAndFail( 3 );
}



/*---------------------------------------------*/

static void crcError ( UInt32 crcStored, UInt32 crcComputed )
{
   cleanUpAndFail( 2 );
}


/*---------------------------------------------*/

static void compressedStreamEOF ( void )
{
   cleanUpAndFail( 2 );
}


/*---------------------------------------------*/

static void ioError ( )
{
   cleanUpAndFail( 1 );
}


/*---------------------------------------------*/

static void blockOverrun ()
{
   cleanUpAndFail( 2 );
}


/*---------------------------------------------*/

static void badBlockHeader ()
{
   cleanUpAndFail( 2 );
}



/*---------------------------------------------*/
static void uncompressOutOfMemory ( Int32 draw, Int32 blockSize )
{
   cleanUpAndFail(1);
}



/*-----------------------------------------------------------*/
/*--- end                                         bzip2.c ---*/
/*-----------------------------------------------------------*/

void
do_bunzip2 (void)
{
  Bool ret;
  
  /*-- Initialise --*/
  ll4                     = NULL;
  ll16                    = NULL;
  ll8                     = NULL;
  tt                      = NULL;
#ifdef SMALL_BZIP2
  smallMode               = True;
#else
  smallMode               = False;
#endif
  verbosity               = 0;
  blockSize100k           = 0;
  bsStream                = NULL;

  outcnt = 0;
  inptr = 0;
  insize = 0;
  
  ret = uncompressStream ((void *)1, (void *)2); /* Arguments ignored. */
  if (ret != True)
    cleanUpAndFail(1);
}
