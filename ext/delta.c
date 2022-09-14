<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<base href="https://fossil-scm.org/home/doc/tip/src/delta.c" />
<meta http-equiv="Content-Security-Policy" content="default-src 'self' data:; script-src 'self' 'nonce-1b8ba99f76f8c11ce7fe2c2361b6c85a2927cc317466ac95'; style-src 'self' 'unsafe-inline'; img-src * data:" />
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Fossil: Documentation</title>
<link rel="alternate" type="application/rss+xml" title="RSS Feed"  href="/home/timeline.rss" />
<link rel="stylesheet" href="/home/style.css?id=7cdf69c0" type="text/css" />
</head>
<body class="doc rpage-doc cpage-doc">
<div class="header">
  <div class="title"><h1>Fossil</h1>Documentation</div>
    <div class="status"><a href='/home/login'>Login</a>
</div>
</div>
<div class="mainmenu">
<a id='hbbtn' href='/home/sitemap' aria-label='Site Map'>&#9776;</a><a href='/home/doc/trunk/www/index.wiki' class=''>Home</a>
<a href='/home/timeline' class=''>Timeline</a>
<a href='/home/doc/trunk/www/permutedindex.html' class=''>Docs</a>
<a href='https://fossil-scm.org/forum/forum' class=''>Forum</a>
<a href='/home/uv/download.html' class='desktoponly'>Download</a>
</div>
<div id='hbdrop'></div>
<div class="content"><span id="debugMsg"></span>
<blockquote><pre>
/*
** Copyright (c) 2006 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the &quot;2-Clause License&quot; or &quot;FreeBSD License&quot;.)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This module implements the delta compress algorithm.
**
** Though developed specifically for fossil, the code in this file
** is generally applicable and is thus easily separated from the
** fossil source code base.  Nothing in this file depends on anything
** else in fossil.
*/
#include &quot;config.h&quot;
#include &lt;stdio.h&gt;
#include &lt;assert.h&gt;
#include &lt;stdlib.h&gt;
#include &lt;string.h&gt;
#include &quot;delta.h&quot;

/*
** Macros for turning debugging printfs on and off
*/
#if 0
# define DEBUG1(X) X
#else
# define DEBUG1(X)
#endif
#if 0
#define DEBUG2(X) X
/*
** For debugging:
** Print 16 characters of text from zBuf
*/
static const char *print16(const char *z){
  int i;
  static char zBuf[20];
  for(i=0; i&lt;16; i++){
    if( z[i]&gt;=0x20 &amp;&amp; z[i]&lt;=0x7e ){
      zBuf[i] = z[i];
    }else{
      zBuf[i] = &#39;.&#39;;
    }
  }
  zBuf[i] = 0;
  return zBuf;
}
#else
# define DEBUG2(X)
#endif

#if INTERFACE
/*
** The &quot;u32&quot; type must be an unsigned 32-bit integer.  Adjust this
*/
typedef unsigned int u32;

/*
** Must be a 16-bit value
*/
typedef short int s16;
typedef unsigned short int u16;

#endif /* INTERFACE */

/*
** The width of a hash window in bytes.  The algorithm only works if this
** is a power of 2.
*/
#define NHASH 16

/*
** The current state of the rolling hash.
**
** z[] holds the values that have been hashed.  z[] is a circular buffer.
** z[i] is the first entry and z[(i+NHASH-1)%NHASH] is the last entry of
** the window.
**
** Hash.a is the sum of all elements of hash.z[].  Hash.b is a weighted
** sum.  Hash.b is z[i]*NHASH + z[i+1]*(NHASH-1) + ... + z[i+NHASH-1]*1.
** (Each index for z[] should be module NHASH, of course.  The %NHASH operator
** is omitted in the prior expression for brevity.)
*/
typedef struct hash hash;
struct hash {
  u16 a, b;         /* Hash values */
  u16 i;            /* Start of the hash window */
  char z[NHASH];    /* The values that have been hashed */
};

/*
** Initialize the rolling hash using the first NHASH characters of z[]
*/
static void hash_init(hash *pHash, const char *z){
  u16 a, b, i;
  a = b = z[0];
  for(i=1; i&lt;NHASH; i++){
    a += z[i];
    b += a;
  }
  memcpy(pHash-&gt;z, z, NHASH);
  pHash-&gt;a = a &amp; 0xffff;
  pHash-&gt;b = b &amp; 0xffff;
  pHash-&gt;i = 0;
}

/*
** Advance the rolling hash by a single character &quot;c&quot;
*/
static void hash_next(hash *pHash, int c){
  u16 old = pHash-&gt;z[pHash-&gt;i];
  pHash-&gt;z[pHash-&gt;i] = c;
  pHash-&gt;i = (pHash-&gt;i+1)&amp;(NHASH-1);
  pHash-&gt;a = pHash-&gt;a - old + c;
  pHash-&gt;b = pHash-&gt;b - NHASH*old + pHash-&gt;a;
}

/*
** Return a 32-bit hash value
*/
static u32 hash_32bit(hash *pHash){
  return (pHash-&gt;a &amp; 0xffff) | (((u32)(pHash-&gt;b &amp; 0xffff))&lt;&lt;16);
}

/*
** Compute a hash on NHASH bytes.
**
** This routine is intended to be equivalent to:
**    hash h;
**    hash_init(&amp;h, zInput);
**    return hash_32bit(&amp;h);
*/
static u32 hash_once(const char *z){
  u16 a, b, i;
  a = b = z[0];
  for(i=1; i&lt;NHASH; i++){
    a += z[i];
    b += a;
  }
  return a | (((u32)b)&lt;&lt;16);
}

/*
** Write an base-64 integer into the given buffer.
*/
static void putInt(unsigned int v, char **pz){
  static const char zDigits[] =
    &quot;0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz~&quot;;
  /*  123456789 123456789 123456789 123456789 123456789 123456789 123 */
  int i, j;
  char zBuf[20];
  if( v==0 ){
    *(*pz)++ = &#39;0&#39;;
    return;
  }
  for(i=0; v&gt;0; i++, v&gt;&gt;=6){
    zBuf[i] = zDigits[v&amp;0x3f];
  }
  for(j=i-1; j&gt;=0; j--){
    *(*pz)++ = zBuf[j];
  }
}

/*
** Read bytes from *pz and convert them into a positive integer.  When
** finished, leave *pz pointing to the first character past the end of
** the integer.  The *pLen parameter holds the length of the string
** in *pz and is decremented once for each character in the integer.
*/
static unsigned int getInt(const char **pz, int *pLen){
  static const signed char zValue[] = {
    -1, -1, -1, -1, -1, -1, -1, -1,   -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,   -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,   -1, -1, -1, -1, -1, -1, -1, -1,
     0,  1,  2,  3,  4,  5,  6,  7,    8,  9, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, 16,   17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32,   33, 34, 35, -1, -1, -1, -1, 36,
    -1, 37, 38, 39, 40, 41, 42, 43,   44, 45, 46, 47, 48, 49, 50, 51,
    52, 53, 54, 55, 56, 57, 58, 59,   60, 61, 62, -1, -1, -1, 63, -1,
  };
  unsigned int v = 0;
  int c;
  unsigned char *z = (unsigned char*)*pz;
  unsigned char *zStart = z;
  while( (c = zValue[0x7f&amp;*(z++)])&gt;=0 ){
     v = (v&lt;&lt;6) + c;
  }
  z--;
  *pLen -= z - zStart;
  *pz = (char*)z;
  return v;
}

/*
** Return the number digits in the base-64 representation of a positive integer
*/
static int digit_count(int v){
  unsigned int i, x;
  for(i=1, x=64; v&gt;=x; i++, x &lt;&lt;= 6){}
  return i;
}

#ifdef __GNUC__
# define GCC_VERSION (__GNUC__*1000000+__GNUC_MINOR__*1000+__GNUC_PATCHLEVEL__)
#else
# define GCC_VERSION 0
#endif

/*
** Compute a 32-bit big-endian checksum on the N-byte buffer.  If the
** buffer is not a multiple of 4 bytes length, compute the sum that would
** have occurred if the buffer was padded with zeros to the next multiple
** of four bytes.
*/
static unsigned int checksum(const char *zIn, size_t N){
  static const int byteOrderTest = 1;
  const unsigned char *z = (const unsigned char *)zIn;
  const unsigned char *zEnd = (const unsigned char*)&amp;zIn[N&amp;~3];
  unsigned sum = 0;
  assert( (z - (const unsigned char*)0)%4==0 );  /* Four-byte alignment */
  if( 0==*(char*)&amp;byteOrderTest ){
    /* This is a big-endian machine */
    while( z&lt;zEnd ){
      sum += *(unsigned*)z;
      z += 4;
    }
  }else{
    /* A little-endian machine */
#if GCC_VERSION&gt;=4003000
    while( z&lt;zEnd ){
      sum += __builtin_bswap32(*(unsigned*)z);
      z += 4;
    }
#elif defined(_MSC_VER) &amp;&amp; _MSC_VER&gt;=1300
    while( z&lt;zEnd ){
      sum += _byteswap_ulong(*(unsigned*)z);
      z += 4;
    }
#else
    unsigned sum0 = 0;
    unsigned sum1 = 0;
    unsigned sum2 = 0;
    while(N &gt;= 16){
      sum0 += ((unsigned)z[0] + z[4] + z[8] + z[12]);
      sum1 += ((unsigned)z[1] + z[5] + z[9] + z[13]);
      sum2 += ((unsigned)z[2] + z[6] + z[10]+ z[14]);
      sum  += ((unsigned)z[3] + z[7] + z[11]+ z[15]);
      z += 16;
      N -= 16;
    }
    while(N &gt;= 4){
      sum0 += z[0];
      sum1 += z[1];
      sum2 += z[2];
      sum  += z[3];
      z += 4;
      N -= 4;
    }
    sum += (sum2 &lt;&lt; 8) + (sum1 &lt;&lt; 16) + (sum0 &lt;&lt; 24);
#endif
  }
  switch(N&amp;3){
    case 3:   sum += (z[2] &lt;&lt; 8);
    case 2:   sum += (z[1] &lt;&lt; 16);
    case 1:   sum += (z[0] &lt;&lt; 24);
    default:  ;
  }
  return sum;
}

/*
** Create a new delta.
**
** The delta is written into a preallocated buffer, zDelta, which
** should be at least 60 bytes longer than the target file, zOut.
** The delta string will be NUL-terminated, but it might also contain
** embedded NUL characters if either the zSrc or zOut files are
** binary.  This function returns the length of the delta string
** in bytes, excluding the final NUL terminator character.
**
** Output Format:
**
** The delta begins with a base64 number followed by a newline.  This
** number is the number of bytes in the TARGET file.  Thus, given a
** delta file z, a program can compute the size of the output file
** simply by reading the first line and decoding the base-64 number
** found there.  The delta_output_size() routine does exactly this.
**
** After the initial size number, the delta consists of a series of
** literal text segments and commands to copy from the SOURCE file.
** A copy command looks like this:
**
**     NNN@MMM,
**
** where NNN is the number of bytes to be copied and MMM is the offset
** into the source file of the first byte (both base-64).   If NNN is 0
** it means copy the rest of the input file.  Literal text is like this:
**
**     NNN:TTTTT
**
** where NNN is the number of bytes of text (base-64) and TTTTT is the text.
**
** The last term is of the form
**
**     NNN;
**
** In this case, NNN is a 32-bit bigendian checksum of the output file
** that can be used to verify that the delta applied correctly.  All
** numbers are in base-64.
**
** Pure text files generate a pure text delta.  Binary files generate a
** delta that may contain some binary data.
**
** Algorithm:
**
** The encoder first builds a hash table to help it find matching
** patterns in the source file.  16-byte chunks of the source file
** sampled at evenly spaced intervals are used to populate the hash
** table.
**
** Next we begin scanning the target file using a sliding 16-byte
** window.  The hash of the 16-byte window in the target is used to
** search for a matching section in the source file.  When a match
** is found, a copy command is added to the delta.  An effort is
** made to extend the matching section to regions that come before
** and after the 16-byte hash window.  A copy command is only issued
** if the result would use less space that just quoting the text
** literally. Literal text is added to the delta for sections that
** do not match or which can not be encoded efficiently using copy
** commands.
*/
int delta_create(
  const char *zSrc,      /* The source or pattern file */
  unsigned int lenSrc,   /* Length of the source file */
  const char *zOut,      /* The target file */
  unsigned int lenOut,   /* Length of the target file */
  char *zDelta           /* Write the delta into this buffer */
){
  int i, base;
  char *zOrigDelta = zDelta;
  hash h;
  int nHash;                 /* Number of hash table entries */
  int *landmark;             /* Primary hash table */
  int *collide;              /* Collision chain */
  int lastRead = -1;         /* Last byte of zSrc read by a COPY command */

  /* Add the target file size to the beginning of the delta
  */
  putInt(lenOut, &amp;zDelta);
  *(zDelta++) = &#39;\n&#39;;

  /* If the source file is very small, it means that we have no
  ** chance of ever doing a copy command.  Just output a single
  ** literal segment for the entire target and exit.
  */
  if( lenSrc&lt;=NHASH ){
    putInt(lenOut, &amp;zDelta);
    *(zDelta++) = &#39;:&#39;;
    memcpy(zDelta, zOut, lenOut);
    zDelta += lenOut;
    putInt(checksum(zOut, lenOut), &amp;zDelta);
    *(zDelta++) = &#39;;&#39;;
    return zDelta - zOrigDelta;
  }

  /* Compute the hash table used to locate matching sections in the
  ** source file.
  */
  nHash = lenSrc/NHASH;
  collide = fossil_malloc( nHash*2*sizeof(int) );
  memset(collide, -1, nHash*2*sizeof(int));
  landmark = &amp;collide[nHash];
  for(i=0; i&lt;lenSrc-NHASH; i+=NHASH){
    int hv = hash_once(&amp;zSrc[i]) % nHash;
    collide[i/NHASH] = landmark[hv];
    landmark[hv] = i/NHASH;
  }

  /* Begin scanning the target file and generating copy commands and
  ** literal sections of the delta.
  */
  base = 0;    /* We have already generated everything before zOut[base] */
  while( base+NHASH&lt;lenOut ){
    int iSrc, iBlock;
    unsigned int bestCnt, bestOfst=0, bestLitsz=0;
    hash_init(&amp;h, &amp;zOut[base]);
    i = 0;     /* Trying to match a landmark against zOut[base+i] */
    bestCnt = 0;
    while( 1 ){
      int hv;
      int limit = 250;

      hv = hash_32bit(&amp;h) % nHash;
      DEBUG2( printf(&quot;LOOKING: %4d [%s]\n&quot;, base+i, print16(&amp;zOut[base+i])); )
      iBlock = landmark[hv];
      while( iBlock&gt;=0 &amp;&amp; (limit--)&gt;0 ){
        /*
        ** The hash window has identified a potential match against
        ** landmark block iBlock.  But we need to investigate further.
        **
        ** Look for a region in zOut that matches zSrc. Anchor the search
        ** at zSrc[iSrc] and zOut[base+i].  Do not include anything prior to
        ** zOut[base] or after zOut[outLen] nor anything after zSrc[srcLen].
        **
        ** Set cnt equal to the length of the match and set ofst so that
        ** zSrc[ofst] is the first element of the match.  litsz is the number
        ** of characters between zOut[base] and the beginning of the match.
        ** sz will be the overhead (in bytes) needed to encode the copy
        ** command.  Only generate copy command if the overhead of the
        ** copy command is less than the amount of literal text to be copied.
        */
        int cnt, ofst, litsz;
        int j, k, x, y;
        int sz;
        int limitX;

        /* Beginning at iSrc, match forwards as far as we can.  j counts
        ** the number of characters that match */
        iSrc = iBlock*NHASH;
        y = base+i;
        limitX = ( lenSrc-iSrc &lt;= lenOut-y ) ? lenSrc : iSrc + lenOut - y;
        for(x=iSrc; x&lt;limitX; x++, y++){
          if( zSrc[x]!=zOut[y] ) break;
        }
        j = x - iSrc - 1;

        /* Beginning at iSrc-1, match backwards as far as we can.  k counts
        ** the number of characters that match */
        for(k=1; k&lt;iSrc &amp;&amp; k&lt;=i; k++){
          if( zSrc[iSrc-k]!=zOut[base+i-k] ) break;
        }
        k--;

        /* Compute the offset and size of the matching region */
        ofst = iSrc-k;
        cnt = j+k+1;
        litsz = i-k;  /* Number of bytes of literal text before the copy */
        DEBUG2( printf(&quot;MATCH %d bytes at %d: [%s] litsz=%d\n&quot;,
                        cnt, ofst, print16(&amp;zSrc[ofst]), litsz); )
        /* sz will hold the number of bytes needed to encode the &quot;insert&quot;
        ** command and the copy command, not counting the &quot;insert&quot; text */
        sz = digit_count(i-k)+digit_count(cnt)+digit_count(ofst)+3;
        if( cnt&gt;=sz &amp;&amp; cnt&gt;bestCnt ){
          /* Remember this match only if it is the best so far and it
          ** does not increase the file size */
          bestCnt = cnt;
          bestOfst = iSrc-k;
          bestLitsz = litsz;
          DEBUG2( printf(&quot;... BEST SO FAR\n&quot;); )
        }

        /* Check the next matching block */
        iBlock = collide[iBlock];
      }

      /* We have a copy command that does not cause the delta to be larger
      ** than a literal insert.  So add the copy command to the delta.
      */
      if( bestCnt&gt;0 ){
        if( bestLitsz&gt;0 ){
          /* Add an insert command before the copy */
          putInt(bestLitsz,&amp;zDelta);
          *(zDelta++) = &#39;:&#39;;
          memcpy(zDelta, &amp;zOut[base], bestLitsz);
          zDelta += bestLitsz;
          base += bestLitsz;
          DEBUG2( printf(&quot;insert %d\n&quot;, bestLitsz); )
        }
        base += bestCnt;
        putInt(bestCnt, &amp;zDelta);
        *(zDelta++) = &#39;@&#39;;
        putInt(bestOfst, &amp;zDelta);
        DEBUG2( printf(&quot;copy %d bytes from %d\n&quot;, bestCnt, bestOfst); )
        *(zDelta++) = &#39;,&#39;;
        if( bestOfst + bestCnt -1 &gt; lastRead ){
          lastRead = bestOfst + bestCnt - 1;
          DEBUG2( printf(&quot;lastRead becomes %d\n&quot;, lastRead); )
        }
        bestCnt = 0;
        break;
      }

      /* If we reach this point, it means no match is found so far */
      if( base+i+NHASH&gt;=lenOut ){
        /* We have reached the end of the file and have not found any
        ** matches.  Do an &quot;insert&quot; for everything that does not match */
        putInt(lenOut-base, &amp;zDelta);
        *(zDelta++) = &#39;:&#39;;
        memcpy(zDelta, &amp;zOut[base], lenOut-base);
        zDelta += lenOut-base;
        base = lenOut;
        break;
      }

      /* Advance the hash by one character.  Keep looking for a match */
      hash_next(&amp;h, zOut[base+i+NHASH]);
      i++;
    }
  }
  /* Output a final &quot;insert&quot; record to get all the text at the end of
  ** the file that does not match anything in the source file.
  */
  if( base&lt;lenOut ){
    putInt(lenOut-base, &amp;zDelta);
    *(zDelta++) = &#39;:&#39;;
    memcpy(zDelta, &amp;zOut[base], lenOut-base);
    zDelta += lenOut-base;
  }
  /* Output the final checksum record. */
  putInt(checksum(zOut, lenOut), &amp;zDelta);
  *(zDelta++) = &#39;;&#39;;
  fossil_free(collide);
  return zDelta - zOrigDelta;
}

/*
** Return the size (in bytes) of the output from applying
** a delta.
**
** This routine is provided so that an procedure that is able
** to call delta_apply() can learn how much space is required
** for the output and hence allocate nor more space that is really
** needed.
*/
int delta_output_size(const char *zDelta, int lenDelta){
  int size;
  size = getInt(&amp;zDelta, &amp;lenDelta);
  if( *zDelta!=&#39;\n&#39; ){
    /* ERROR: size integer not terminated by &quot;\n&quot; */
    return -1;
  }
  return size;
}


/*
** Apply a delta.
**
** The output buffer should be big enough to hold the whole output
** file and a NUL terminator at the end.  The delta_output_size()
** routine will determine this size for you.
**
** The delta string should be null-terminated.  But the delta string
** may contain embedded NUL characters (if the input and output are
** binary files) so we also have to pass in the length of the delta in
** the lenDelta parameter.
**
** This function returns the size of the output file in bytes (excluding
** the final NUL terminator character).  Except, if the delta string is
** malformed or intended for use with a source file other than zSrc,
** then this routine returns -1.
**
** Refer to the delta_create() documentation above for a description
** of the delta file format.
*/
int delta_apply(
  const char *zSrc,      /* The source or pattern file */
  int lenSrc,            /* Length of the source file */
  const char *zDelta,    /* Delta to apply to the pattern */
  int lenDelta,          /* Length of the delta */
  char *zOut             /* Write the output into this preallocated buffer */
){
  unsigned int limit;
  unsigned int total = 0;
#ifdef FOSSIL_ENABLE_DELTA_CKSUM_TEST
  char *zOrigOut = zOut;
#endif

  limit = getInt(&amp;zDelta, &amp;lenDelta);
  if( *zDelta!=&#39;\n&#39; ){
    /* ERROR: size integer not terminated by &quot;\n&quot; */
    return -1;
  }
  zDelta++; lenDelta--;
  while( *zDelta &amp;&amp; lenDelta&gt;0 ){
    unsigned int cnt, ofst;
    cnt = getInt(&amp;zDelta, &amp;lenDelta);
    switch( zDelta[0] ){
      case &#39;@&#39;: {
        zDelta++; lenDelta--;
        ofst = getInt(&amp;zDelta, &amp;lenDelta);
        if( lenDelta&gt;0 &amp;&amp; zDelta[0]!=&#39;,&#39; ){
          /* ERROR: copy command not terminated by &#39;,&#39; */
          return -1;
        }
        zDelta++; lenDelta--;
        DEBUG1( printf(&quot;COPY %d from %d\n&quot;, cnt, ofst); )
        total += cnt;
        if( total&gt;limit ){
          /* ERROR: copy exceeds output file size */
          return -1;
        }
        if( ofst+cnt &gt; lenSrc ){
          /* ERROR: copy extends past end of input */
          return -1;
        }
        memcpy(zOut, &amp;zSrc[ofst], cnt);
        zOut += cnt;
        break;
      }
      case &#39;:&#39;: {
        zDelta++; lenDelta--;
        total += cnt;
        if( total&gt;limit ){
          /* ERROR:  insert command gives an output larger than predicted */
          return -1;
        }
        DEBUG1( printf(&quot;INSERT %d\n&quot;, cnt); )
        if( cnt&gt;lenDelta ){
          /* ERROR: insert count exceeds size of delta */
          return -1;
        }
        memcpy(zOut, zDelta, cnt);
        zOut += cnt;
        zDelta += cnt;
        lenDelta -= cnt;
        break;
      }
      case &#39;;&#39;: {
        zDelta++; lenDelta--;
        zOut[0] = 0;
#ifdef FOSSIL_ENABLE_DELTA_CKSUM_TEST
        if( cnt!=checksum(zOrigOut, total) ){
          /* ERROR:  bad checksum */
          return -1;
        }
#endif
        if( total!=limit ){
          /* ERROR: generated size does not match predicted size */
          return -1;
        }
        return total;
      }
      default: {
        /* ERROR: unknown delta operator */
        return -1;
      }
    }
  }
  /* ERROR: unterminated delta */
  return -1;
}

/*
** Analyze a delta.  Figure out the total number of bytes copied from
** source to target, and the total number of bytes inserted by the delta,
** and return both numbers.
*/
int delta_analyze(
  const char *zDelta,    /* Delta to apply to the pattern */
  int lenDelta,          /* Length of the delta */
  int *pnCopy,           /* OUT: Number of bytes copied */
  int *pnInsert          /* OUT: Number of bytes inserted */
){
  unsigned int nInsert = 0;
  unsigned int nCopy = 0;

  (void)getInt(&amp;zDelta, &amp;lenDelta);
  if( *zDelta!=&#39;\n&#39; ){
    /* ERROR: size integer not terminated by &quot;\n&quot; */
    return -1;
  }
  zDelta++; lenDelta--;
  while( *zDelta &amp;&amp; lenDelta&gt;0 ){
    unsigned int cnt;
    cnt = getInt(&amp;zDelta, &amp;lenDelta);
    switch( zDelta[0] ){
      case &#39;@&#39;: {
        zDelta++; lenDelta--;
        (void)getInt(&amp;zDelta, &amp;lenDelta);
        if( lenDelta&gt;0 &amp;&amp; zDelta[0]!=&#39;,&#39; ){
          /* ERROR: copy command not terminated by &#39;,&#39; */
          return -1;
        }
        zDelta++; lenDelta--;
        nCopy += cnt;
        break;
      }
      case &#39;:&#39;: {
        zDelta++; lenDelta--;
        nInsert += cnt;
        if( cnt&gt;lenDelta ){
          /* ERROR: insert count exceeds size of delta */
          return -1;
        }
        zDelta += cnt;
        lenDelta -= cnt;
        break;
      }
      case &#39;;&#39;: {
        *pnCopy = nCopy;
        *pnInsert = nInsert;
        return 0;
      }
      default: {
        /* ERROR: unknown delta operator */
        return -1;
      }
    }
  }
  /* ERROR: unterminated delta */
  return -1;
}

</pre></blockquote>
<script nonce='1b8ba99f76f8c11ce7fe2c2361b6c85a2927cc317466ac95'>/* builtin.c:620 */
(function(){
if(window.NodeList && !NodeList.prototype.forEach){NodeList.prototype.forEach = Array.prototype.forEach;}
if(!window.fossil) window.fossil={};
window.fossil.version = "2.20 [e9d7cf3e92] 2022-09-13 20:11:04 UTC";
window.fossil.rootPath = "/home"+'/';
window.fossil.config = {projectName: "Fossil",
shortProjectName: "",
projectCode: "CE59BB9F186226D80E49D1FA2DB29F935CCA0333",
/* Length of UUID hashes for display purposes. */hashDigits: 8, hashDigitsUrl: 16,
diffContextLines: 5,
editStateMarkers: {/*Symbolic markers to denote certain edit states.*/isNew:'[+]', isModified:'[*]', isDeleted:'[-]'},
confirmerButtonTicks: 3 /*default fossil.confirmer tick count.*/,
skin:{isDark: false/*true if the current skin has the 'white-foreground' detail*/}
};
window.fossil.user = {name: "guest",isAdmin: false};
if(fossil.config.skin.isDark) document.body.classList.add('fossil-dark-style');
window.fossil.page = {name:"doc/tip/src/delta.c"};
})();
</script>
<script nonce='1b8ba99f76f8c11ce7fe2c2361b6c85a2927cc317466ac95'>/* doc.c:430 */
window.addEventListener('load', ()=>window.fossil.pikchr.addSrcView(), false);
</script>
</div>
<div class="footer">
This page was generated in about
0.006s by
Fossil 2.20 [e9d7cf3e92] 2022-09-13 20:11:04
</div>
<script nonce="1b8ba99f76f8c11ce7fe2c2361b6c85a2927cc317466ac95">/* style.c:913 */
function debugMsg(msg){
var n = document.getElementById("debugMsg");
if(n){n.textContent=msg;}
}
</script>
<script nonce='1b8ba99f76f8c11ce7fe2c2361b6c85a2927cc317466ac95'>
/* hbmenu.js *************************************************************/
(function() {
var hbButton = document.getElementById("hbbtn");
if (!hbButton) return;
if (!document.addEventListener) return;
var panel = document.getElementById("hbdrop");
if (!panel) return;
if (!panel.style) return;
var panelBorder = panel.style.border;
var panelInitialized = false;
var panelResetBorderTimerID = 0;
var animate = panel.style.transition !== null && (typeof(panel.style.transition) == "string");
var animMS = panel.getAttribute("data-anim-ms");
if (animMS) {
animMS = parseInt(animMS);
if (isNaN(animMS) || animMS == 0)
animate = false;
else if (animMS < 0)
animMS = 400;
}
else
animMS = 400;
var panelHeight;
function calculatePanelHeight() {
panel.style.maxHeight = '';
var es   = window.getComputedStyle(panel),
edis = es.display,
epos = es.position,
evis = es.visibility;
panel.style.visibility = 'hidden';
panel.style.position   = 'absolute';
panel.style.display    = 'block';
panelHeight = panel.offsetHeight + 'px';
panel.style.display    = edis;
panel.style.position   = epos;
panel.style.visibility = evis;
}
function showPanel() {
if (panelResetBorderTimerID) {
clearTimeout(panelResetBorderTimerID);
panelResetBorderTimerID = 0;
}
if (animate) {
if (!panelInitialized) {
panelInitialized = true;
calculatePanelHeight();
panel.style.transition = 'max-height ' + animMS +
'ms ease-in-out';
panel.style.overflowY  = 'hidden';
panel.style.maxHeight  = '0';
}
setTimeout(function() {
panel.style.maxHeight = panelHeight;
panel.style.border    = panelBorder;
}, 40);
}
panel.style.display = 'block';
document.addEventListener('keydown',panelKeydown,true);
document.addEventListener('click',panelClick,false);
}
var panelKeydown = function(event) {
var key = event.which || event.keyCode;
if (key == 27) {
event.stopPropagation();
panelToggle(true);
}
};
var panelClick = function(event) {
if (!panel.contains(event.target)) {
panelToggle(true);
}
};
function panelShowing() {
if (animate) {
return panel.style.maxHeight == panelHeight;
}
else {
return panel.style.display == 'block';
}
}
function hasChildren(element) {
var childElement = element.firstChild;
while (childElement) {
if (childElement.nodeType == 1)
return true;
childElement = childElement.nextSibling;
}
return false;
}
window.addEventListener('resize',function(event) {
panelInitialized = false;
},false);
hbButton.addEventListener('click',function(event) {
event.stopPropagation();
event.preventDefault();
panelToggle(false);
},false);
function panelToggle(suppressAnimation) {
if (panelShowing()) {
document.removeEventListener('keydown',panelKeydown,true);
document.removeEventListener('click',panelClick,false);
if (animate) {
if (suppressAnimation) {
var transition = panel.style.transition;
panel.style.transition = '';
panel.style.maxHeight = '0';
panel.style.border = 'none';
setTimeout(function() {
panel.style.transition = transition;
}, 40);
}
else {
panel.style.maxHeight = '0';
panelResetBorderTimerID = setTimeout(function() {
panel.style.border = 'none';
panelResetBorderTimerID = 0;
}, animMS);
}
}
else {
panel.style.display = 'none';
}
}
else {
if (!hasChildren(panel)) {
var xhr = new XMLHttpRequest();
xhr.onload = function() {
var doc = xhr.responseXML;
if (doc) {
var sm = doc.querySelector("ul#sitemap");
if (sm && xhr.status == 200) {
panel.innerHTML = sm.outerHTML;
showPanel();
}
}
}
var url = hbButton.href + (hbButton.href.includes("?")?"&popup":"?popup")
xhr.open("GET", url);
xhr.responseType = "document";
xhr.send();
}
else {
showPanel();
}
}
}
})();
/* fossil.bootstrap.js *************************************************************/
"use strict";
(function () {
if(typeof window.CustomEvent === "function") return false;
window.CustomEvent = function(event, params) {
if(!params) params = {bubbles: false, cancelable: false, detail: null};
const evt = document.createEvent('CustomEvent');
evt.initCustomEvent( event, !!params.bubbles, !!params.cancelable, params.detail );
return evt;
};
})();
(function(global){
const F = global.fossil;
const timestring = function f(){
if(!f.rx1){
f.rx1 = /\.\d+Z$/;
}
const d = new Date();
return d.toISOString().replace(f.rx1,'').split('T').join(' ');
};
const localTimeString = function ff(d){
if(!ff.pad){
ff.pad = (x)=>(''+x).length>1 ? x : '0'+x;
}
d || (d = new Date());
return [
d.getFullYear(),'-',ff.pad(d.getMonth()+1),
'-',ff.pad(d.getDate()),
' ',ff.pad(d.getHours()),':',ff.pad(d.getMinutes()),
':',ff.pad(d.getSeconds())
].join('');
};
F.message = function f(msg){
const args = Array.prototype.slice.call(arguments,0);
const tgt = f.targetElement;
if(args.length) args.unshift(
localTimeString()+':'
);
if(tgt){
tgt.classList.remove('error');
tgt.innerText = args.join(' ');
}
else{
if(args.length){
args.unshift('Fossil status:');
console.debug.apply(console,args);
}
}
return this;
};
F.message.targetElement =
document.querySelector('#fossil-status-bar');
if(F.message.targetElement){
F.message.targetElement.addEventListener(
'dblclick', ()=>F.message(), false
);
}
F.error = function f(msg){
const args = Array.prototype.slice.call(arguments,0);
const tgt = F.message.targetElement;
args.unshift(timestring(),'UTC:');
if(tgt){
tgt.classList.add('error');
tgt.innerText = args.join(' ');
}
else{
args.unshift('Fossil error:');
console.error.apply(console,args);
}
return this;
};
F.encodeUrlArgs = function(obj,tgtArray,fakeEncode){
if(!obj) return '';
const a = (tgtArray instanceof Array) ? tgtArray : [],
enc = fakeEncode ? (x)=>x : encodeURIComponent;
let k, i = 0;
for( k in obj ){
if(i++) a.push('&');
a.push(enc(k),'=',enc(obj[k]));
}
return a===tgtArray ? a : a.join('');
};
F.repoUrl = function(path,urlParams){
if(!urlParams) return this.rootPath+path;
const url=[this.rootPath,path];
url.push('?');
if('string'===typeof urlParams) url.push(urlParams);
else if(urlParams && 'object'===typeof urlParams){
this.encodeUrlArgs(urlParams, url);
}
return url.join('');
};
F.isObject = function(v){
return v &&
(v instanceof Object) &&
('[object Object]' === Object.prototype.toString.apply(v) );
};
F.mergeLastWins = function(){
var k, o, i;
const n = arguments.length, rc={};
for(i = 0; i < n; ++i){
if(!F.isObject(o = arguments[i])) continue;
for( k in o ){
if(o.hasOwnProperty(k)) rc[k] = o[k];
}
}
return rc;
};
F.hashDigits = function(hash,forUrl){
const n = ('number'===typeof forUrl)
? forUrl : F.config[forUrl ? 'hashDigitsUrl' : 'hashDigits'];
return ('string'==typeof hash ? hash.substr(
0, n
) : hash);
};
F.onPageLoad = function(callback){
window.addEventListener('load', callback, false);
return this;
};
F.onDOMContentLoaded = function(callback){
window.addEventListener('DOMContentLoaded', callback, false);
return this;
};
F.shortenFilename = function(name){
const a = name.split('/');
if(a.length<=2) return name;
while(a.length>2) a.shift();
return '.../'+a.join('/');
};
F.page.addEventListener = function f(eventName, callback){
if(!f.proxy){
f.proxy = document.createElement('span');
}
f.proxy.addEventListener(eventName, callback, false);
return this;
};
F.page.dispatchEvent = function(eventName, eventDetail){
if(this.addEventListener.proxy){
try{
this.addEventListener.proxy.dispatchEvent(
new CustomEvent(eventName,{detail: eventDetail})
);
}catch(e){
console.error(eventName,"event listener threw:",e);
}
}
return this;
};
F.page.setPageTitle = function(title){
const t = document.querySelector('title');
if(t) t.innerText = title;
return this;
};
F.debounce = function f(func, wait, immediate) {
var timeout;
if(!wait) wait = f.$defaultDelay;
return function() {
const context = this, args = Array.prototype.slice.call(arguments);
const later = function() {
timeout = undefined;
if(!immediate) func.apply(context, args);
};
const callNow = immediate && !timeout;
clearTimeout(timeout);
timeout = setTimeout(later, wait);
if(callNow) func.apply(context, args);
};
};
F.debounce.$defaultDelay = 500;
})(window);
/* fossil.dom.js *************************************************************/
"use strict";
(function(F){
const argsToArray = (a)=>Array.prototype.slice.call(a,0);
const isArray = (v)=>v instanceof Array;
const dom = {
create: function(elemType){
return document.createElement(elemType);
},
createElemFactory: function(eType){
return function(){
return document.createElement(eType);
};
},
remove: function(e){
if(e.forEach){
e.forEach(
(x)=>x.parentNode.removeChild(x)
);
}else{
e.parentNode.removeChild(e);
}
return e;
},
clearElement: function f(e){
if(!f.each){
f.each = function(e){
if(e.forEach){
e.forEach((x)=>f(x));
return e;
}
while(e.firstChild) e.removeChild(e.firstChild);
};
}
argsToArray(arguments).forEach(f.each);
return arguments[0];
},
};
dom.splitClassList = function f(str){
if(!f.rx){
f.rx = /(\s+|\s*,\s*)/;
}
return str ? str.split(f.rx) : [str];
};
dom.div = dom.createElemFactory('div');
dom.p = dom.createElemFactory('p');
dom.code = dom.createElemFactory('code');
dom.pre = dom.createElemFactory('pre');
dom.header = dom.createElemFactory('header');
dom.footer = dom.createElemFactory('footer');
dom.section = dom.createElemFactory('section');
dom.span = dom.createElemFactory('span');
dom.strong = dom.createElemFactory('strong');
dom.em = dom.createElemFactory('em');
dom.ins = dom.createElemFactory('ins');
dom.del = dom.createElemFactory('del');
dom.label = function(forElem, text){
const rc = document.createElement('label');
if(forElem){
if(forElem instanceof HTMLElement){
forElem = this.attr(forElem, 'id');
}
dom.attr(rc, 'for', forElem);
}
if(text) this.append(rc, text);
return rc;
};
dom.img = function(src){
const e = this.create('img');
if(src) e.setAttribute('src',src);
return e;
};
dom.a = function(href,label){
const e = this.create('a');
if(href) e.setAttribute('href',href);
if(label) e.appendChild(dom.text(true===label ? href : label));
return e;
};
dom.hr = dom.createElemFactory('hr');
dom.br = dom.createElemFactory('br');
dom.text = function(){
return document.createTextNode(argsToArray(arguments).join(''));
};
dom.button = function(label,callback){
const b = this.create('button');
if(label) b.appendChild(this.text(label));
if('function' === typeof callback){
b.addEventListener('click', callback, false);
}
return b;
};
dom.textarea = function(){
const rc = this.create('textarea');
let rows, cols, readonly;
if(1===arguments.length){
if('boolean'===typeof arguments[0]){
readonly = !!arguments[0];
}else{
rows = arguments[0];
}
}else if(arguments.length){
rows = arguments[0];
cols = arguments[1];
readonly = arguments[2];
}
if(rows) rc.setAttribute('rows',rows);
if(cols) rc.setAttribute('cols', cols);
if(readonly) rc.setAttribute('readonly', true);
return rc;
};
dom.select = dom.createElemFactory('select');
dom.option = function(value,label){
const a = arguments;
var sel;
if(1==a.length){
if(a[0] instanceof HTMLElement){
sel = a[0];
}else{
value = a[0];
}
}else if(2==a.length){
if(a[0] instanceof HTMLElement){
sel = a[0];
value = a[1];
}else{
value = a[0];
label = a[1];
}
}
else if(3===a.length){
sel = a[0];
value = a[1];
label = a[2];
}
const o = this.create('option');
if(undefined !== value){
o.value = value;
this.append(o, this.text(label || value));
}else if(undefined !== label){
this.append(o, label);
}
if(sel) this.append(sel, o);
return o;
};
dom.h = function(level){
return this.create('h'+level);
};
dom.ul = dom.createElemFactory('ul');
dom.li = function(parent){
const li = this.create('li');
if(parent) parent.appendChild(li);
return li;
};
dom.createElemFactoryWithOptionalParent = function(childType){
return function(parent){
const e = this.create(childType);
if(parent) parent.appendChild(e);
return e;
};
};
dom.table = dom.createElemFactory('table');
dom.thead = dom.createElemFactoryWithOptionalParent('thead');
dom.tbody = dom.createElemFactoryWithOptionalParent('tbody');
dom.tfoot = dom.createElemFactoryWithOptionalParent('tfoot');
dom.tr = dom.createElemFactoryWithOptionalParent('tr');
dom.td = dom.createElemFactoryWithOptionalParent('td');
dom.th = dom.createElemFactoryWithOptionalParent('th');
dom.fieldset = function(legendText){
const fs = this.create('fieldset');
if(legendText){
this.append(
fs,
(legendText instanceof HTMLElement)
? legendText
: this.append(this.legend(legendText))
);
}
return fs;
};
dom.legend = function(legendText){
const rc = this.create('legend');
if(legendText) this.append(rc, legendText);
return rc;
};
dom.append = function f(parent){
const a = argsToArray(arguments);
a.shift();
for(let i in a) {
var e = a[i];
if(isArray(e) || e.forEach){
e.forEach((x)=>f.call(this, parent,x));
continue;
}
if('string'===typeof e
|| 'number'===typeof e
|| 'boolean'===typeof e
|| e instanceof Error) e = this.text(e);
parent.appendChild(e);
}
return parent;
};
dom.input = function(type){
return this.attr(this.create('input'), 'type', type);
};
dom.checkbox = function(value, checked){
const rc = this.input('checkbox');
if(1===arguments.length && 'boolean'===typeof value){
checked = !!value;
value = undefined;
}
if(undefined !== value) rc.value = value;
if(!!checked) rc.checked = true;
return rc;
};
dom.radio = function(){
const rc = this.input('radio');
let name, value, checked;
if(1===arguments.length && 'boolean'===typeof name){
checked = arguments[0];
name = value = undefined;
}else if(2===arguments.length){
name = arguments[0];
if('boolean'===typeof arguments[1]){
checked = arguments[1];
}else{
value = arguments[1];
checked = undefined;
}
}else if(arguments.length){
name = arguments[0];
value = arguments[1];
checked = arguments[2];
}
if(name) this.attr(rc, 'name', name);
if(undefined!==value) rc.value = value;
if(!!checked) rc.checked = true;
return rc;
};
const domAddRemoveClass = function f(action,e){
if(!f.rxSPlus){
f.rxSPlus = /\s+/;
f.applyAction = function(e,a,v){
if(!e || !v
) return;
else if(e.forEach){
e.forEach((E)=>E.classList[a](v));
}else{
e.classList[a](v);
}
};
}
var i = 2, n = arguments.length;
for( ; i < n; ++i ){
let c = arguments[i];
if(!c) continue;
else if(isArray(c) ||
('string'===typeof c
&& c.indexOf(' ')>=0
&& (c = c.split(f.rxSPlus)))
|| c.forEach
){
c.forEach((k)=>k ? f.applyAction(e, action, k) : false);
}else if(c){
f.applyAction(e, action, c);
}
}
return e;
};
dom.addClass = function(e,c){
const a = argsToArray(arguments);
a.unshift('add');
return domAddRemoveClass.apply(this, a);
};
dom.removeClass = function(e,c){
const a = argsToArray(arguments);
a.unshift('remove');
return domAddRemoveClass.apply(this, a);
};
dom.toggleClass = function f(e,c){
if(e.forEach){
e.forEach((x)=>x.classList.toggle(c));
}else{
e.classList.toggle(c);
}
return e;
};
dom.hasClass = function(e,c){
return (e && e.classList) ? e.classList.contains(c) : false;
};
dom.moveTo = function(dest,e){
const n = arguments.length;
var i = 1;
const self = this;
for( ; i < n; ++i ){
e = arguments[i];
this.append(dest, e);
}
return dest;
};
dom.moveChildrenTo = function f(dest,e){
if(!f.mv){
f.mv = function(d,v){
if(d instanceof Array){
d.push(v);
if(v.parentNode) v.parentNode.removeChild(v);
}
else d.appendChild(v);
};
}
const n = arguments.length;
var i = 1;
for( ; i < n; ++i ){
e = arguments[i];
if(!e){
console.warn("Achtung: dom.moveChildrenTo() passed a falsy value at argument",i,"of",
arguments,arguments[i]);
continue;
}
if(e.forEach){
e.forEach((x)=>f.mv(dest, x));
}else{
while(e.firstChild){
f.mv(dest, e.firstChild);
}
}
}
return dest;
};
dom.replaceNode = function f(old,nu){
var i = 1, n = arguments.length;
++f.counter;
try {
for( ; i < n; ++i ){
const e = arguments[i];
if(e.forEach){
e.forEach((x)=>f.call(this,old,e));
continue;
}
old.parentNode.insertBefore(e, old);
}
}
finally{
--f.counter;
}
if(!f.counter){
old.parentNode.removeChild(old);
}
};
dom.replaceNode.counter = 0;
dom.attr = function f(e){
if(2===arguments.length) return e.getAttribute(arguments[1]);
const a = argsToArray(arguments);
if(e.forEach){
e.forEach(function(x){
a[0] = x;
f.apply(f,a);
});
return e;
}
a.shift();
while(a.length){
const key = a.shift(), val = a.shift();
if(null===val || undefined===val){
e.removeAttribute(key);
}else{
e.setAttribute(key,val);
}
}
return e;
};
const enableDisable = function f(enable){
var i = 1, n = arguments.length;
for( ; i < n; ++i ){
let e = arguments[i];
if(e.forEach){
e.forEach((x)=>f(enable,x));
}else{
e.disabled = !enable;
}
}
return arguments[1];
};
dom.enable = function(e){
const args = argsToArray(arguments);
args.unshift(true);
return enableDisable.apply(this,args);
};
dom.disable = function(e){
const args = argsToArray(arguments);
args.unshift(false);
return enableDisable.apply(this,args);
};
dom.selectOne = function(x,origin){
var src = origin || document,
e = src.querySelector(x);
if(!e){
e = new Error("Cannot find DOM element: "+x);
console.error(e, src);
throw e;
}
return e;
};
dom.flashOnce = function f(e,howLongMs,afterFlashCallback){
if(e.dataset.isBlinking){
return;
}
if(2===arguments.length && 'function' ===typeof howLongMs){
afterFlashCallback = howLongMs;
howLongMs = f.defaultTimeMs;
}
if(!howLongMs || 'number'!==typeof howLongMs){
howLongMs = f.defaultTimeMs;
}
e.dataset.isBlinking = true;
const transition = e.style.transition;
e.style.transition = "opacity "+howLongMs+"ms ease-in-out";
const opacity = e.style.opacity;
e.style.opacity = 0;
setTimeout(function(){
e.style.transition = transition;
e.style.opacity = opacity;
delete e.dataset.isBlinking;
if(afterFlashCallback) afterFlashCallback();
}, howLongMs);
return e;
};
dom.flashOnce.defaultTimeMs = 400;
dom.flashOnce.eventHandler = (event)=>dom.flashOnce(event.target)
dom.flashNTimes = function(e,n,howLongMs,afterFlashCallback){
const args = argsToArray(arguments);
args.splice(1,1);
if(arguments.length===3 && 'function'===typeof howLongMs){
afterFlashCallback = howLongMs;
howLongMs = args[1] = this.flashOnce.defaultTimeMs;
}else if(arguments.length<3){
args[1] = this.flashOnce.defaultTimeMs;
}
n = +n;
const self = this;
const cb = args[2] = function f(){
if(--n){
setTimeout(()=>self.flashOnce(e, howLongMs, f),
howLongMs+(howLongMs*0.1));
}else if(afterFlashCallback){
afterFlashCallback();
}
};
this.flashOnce.apply(this, args);
return this;
};
dom.addClassBriefly = function f(e, className, howLongMs, afterCallback){
if(arguments.length<4 && 'function'===typeof howLongMs){
afterCallback = howLongMs;
howLongMs = f.defaultTimeMs;
}else if(arguments.length<3 || !+howLongMs){
howLongMs = f.defaultTimeMs;
}
this.addClass(e, className);
setTimeout(function(){
dom.removeClass(e, className);
if(afterCallback) afterCallback();
}, howLongMs);
return this;
};
dom.addClassBriefly.defaultTimeMs = 1000;
dom.copyTextToClipboard = function(text){
if( window.clipboardData && window.clipboardData.setData ){
window.clipboardData.setData('Text',text);
return true;
}else{
const x = document.createElement("textarea");
x.style.position = 'fixed';
x.value = text;
document.body.appendChild(x);
x.select();
var rc;
try{
document.execCommand('copy');
rc = true;
}catch(err){
rc = false;
}finally{
document.body.removeChild(x);
}
return rc;
}
};
dom.copyStyle = function f(e, style){
if(e.forEach){
e.forEach((x)=>f(x, style));
return e;
}
if(style){
let k;
for(k in style){
if(style.hasOwnProperty(k)) e.style[k] = style[k];
}
}
return e;
};
dom.effectiveHeight = function f(e){
if(!e) return 0;
if(!f.measure){
f.measure = function callee(e, depth){
if(!e) return;
const m = e.getBoundingClientRect();
if(0===depth){
callee.top = m.top;
callee.bottom = m.bottom;
}else{
callee.top = m.top ? Math.min(callee.top, m.top) : callee.top;
callee.bottom = Math.max(callee.bottom, m.bottom);
}
Array.prototype.forEach.call(e.children,(e)=>callee(e,depth+1));
if(0===depth){
f.extra += callee.bottom - callee.top;
}
return f.extra;
};
}
f.extra = 0;
f.measure(e,0);
return f.extra;
};
dom.parseHtml = function(){
let childs, string, tgt;
if(1===arguments.length){
string = arguments[0];
}else if(2==arguments.length){
tgt = arguments[0];
string  = arguments[1];
}
if(string){
const newNode = new DOMParser().parseFromString(string, 'text/html');
childs = newNode.documentElement.querySelector('body');
childs = childs ? Array.prototype.slice.call(childs.childNodes, 0) : [];
}else{
childs = [];
}
return tgt ? this.moveTo(tgt, childs) : childs;
};
F.connectPagePreviewers = function f(selector,methodNamespace){
if('string'===typeof selector){
selector = document.querySelectorAll(selector);
}else if(!selector.forEach){
selector = [selector];
}
if(!methodNamespace){
methodNamespace = F.page;
}
selector.forEach(function(e){
e.addEventListener(
'click', function(r){
const eTo = '#'===e.dataset.fPreviewTo[0]
? document.querySelector(e.dataset.fPreviewTo)
: methodNamespace[e.dataset.fPreviewTo],
eFrom = '#'===e.dataset.fPreviewFrom[0]
? document.querySelector(e.dataset.fPreviewFrom)
: methodNamespace[e.dataset.fPreviewFrom],
asText = +(e.dataset.fPreviewAsText || 0);
eTo.textContent = "Fetching preview...";
methodNamespace[e.dataset.fPreviewVia](
(eFrom instanceof Function ? eFrom.call(methodNamespace) : eFrom.value),
function(r){
if(eTo instanceof Function) eTo.call(methodNamespace, r||'');
else if(!r){
dom.clearElement(eTo);
}else if(asText){
eTo.textContent = r;
}else{
dom.parseHtml(dom.clearElement(eTo), r);
}
}
);
}, false
);
});
return this;
};
return F.dom = dom;
})(window.fossil);
/* fossil.pikchr.js *************************************************************/
(function(F){
"use strict";
const D = F.dom, P = F.pikchr = {};
P.addSrcView = function f(svg){
if(!f.hasOwnProperty('parentClick')){
f.parentClick = function(ev){
if(ev.altKey || ev.metaKey || ev.ctrlKey
|| this.classList.contains('toggle')){
this.classList.toggle('source');
ev.stopPropagation();
ev.preventDefault();
}
};
};
if(!svg) svg = 'svg.pikchr';
if('string' === typeof svg){
document.querySelectorAll(svg).forEach((e)=>f.call(this, e));
return this;
}else if(svg.forEach){
svg.forEach((e)=>f.call(this, e));
return this;
}
if(svg.dataset.pikchrProcessed){
return this;
}
svg.dataset.pikchrProcessed = 1;
const parent = svg.parentNode.parentNode;
const srcView = parent ? svg.parentNode.nextElementSibling : undefined;
if(!srcView || !srcView.classList.contains('pikchr-src')){
return this;
}
parent.addEventListener('click', f.parentClick, false);
return this;
};
})(window.fossil);
</script>
</body>
</html>
