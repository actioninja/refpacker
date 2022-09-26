#include "refpack.h"

#include <iostream>
#include <algorithm>

/*------------------------------------------------------------------*/
/*                                                                  */
/*               RefPack - Backward Reference Codex                 */
/*                                                                  */
/*                    by FrANK G. Barchard, EAC                     */
/*                                                                  */
/*------------------------------------------------------------------*/
/* Format Notes:                                                    */
/* -------------                                                    */
/* refpack is a sliding window (131k) lzss method, with byte        */
/* oriented coding.                                                 */
/*                                                                  */
/* huff fb5 style header:                                           */
/*      *10fb  fb5      refpack 1.0  reference pack                 */
/*                                                                  */
/*                                                                  */
/* header:                                                          */
/* [10fb] [unpacksize] [totalunpacksize]                            */
/*   2         3                                                    */
/*                                                                  */
/*                                                                  */
/*                                                                  */
/* format is:                                                       */
/* ----------                                                       */
/* 0ffnnndd_ffffffff          short ref, f=0..1023,n=3..10,d=0..3   */
/* 10nnnnnn_ddffffff_ffffffff long ref, f=0..16384,n=4..67,d=0..3   */
/* 110fnndd_f.._f.._nnnnnnnn  very long,f=0..131071,n=5..1028,d=0..3*/
/* 111ddddd                   literal, d=4..112                     */
/* 111111dd                   eof, d=0..3                           */
/*                                                                  */
/*------------------------------------------------------------------*/

uint32_t matchlen(const uint8_t *s, const uint8_t *d, uint32_t maxmatch) {
    uint32_t current;

    for (current = 0; current < maxmatch && *s++ == *d++; ++current);

    return current;
}

void compress(const CompressorInput &input, DecompressorInput *output) {
#if 0
    uint32_t quick=0;          // seems to prevent a long compression if set true.  Probably affects compression ratio too.
#endif

    int32_t len;
    uint32_t tlen;
    uint32_t tcost;
    uint32_t run;
    uint32_t toffset;
    uint32_t boffset;
    uint32_t blen;
    uint32_t bcost;
    uint32_t mlen;
    const uint8_t *tptr;
    const uint8_t *cptr;
    const uint8_t *rptr;
    uint8_t *to;

    int countliterals = 0;
    int countshort = 0;
    int countlong = 0;
    int countvlong = 0;
    long hash;
    long hoffset;
    long minhoffset;
    int i;
    int32_t *link;
    int32_t *hashtbl;
    int32_t *hashptr;

    len = input.lengthInBytes;

    output->buffer = new int8_t[input.lengthInBytes * 2 + 8192];   // same wild guess Frank Barchard makes
    to = static_cast<uint8_t *>(output->buffer);

    // write size into the stream
    for (i = 0; i < 4; i++, to++)
        *to = static_cast<int8_t>(input.lengthInBytes >> (i * 8) & 255);

    run = 0;
    cptr = rptr = static_cast<uint8_t *>(input.buffer);

    hashtbl = new int32_t[65536];
    link = new int32_t[131072];

    hashptr = hashtbl;
    for (i = 0; i < 65536L / 16; ++i) {
        *(hashptr + 0) = *(hashptr + 1) = *(hashptr + 2) = *(hashptr + 3) =
        *(hashptr + 4) = *(hashptr + 5) = *(hashptr + 6) = *(hashptr + 7) =
        *(hashptr + 8) = *(hashptr + 9) = *(hashptr + 10) = *(hashptr + 11) =
        *(hashptr + 12) = hashptr[13] = hashptr[14] = hashptr[15] = -1L;
        hashptr += 16;
    }

    while (len > 0) {
        boffset = 0;
        blen = bcost = 2;
        mlen = std::min(len, 1028);
        tptr = cptr - 1;
        hash = HASH(cptr);
        hoffset = hashtbl[hash];
        minhoffset = std::max(cptr - static_cast<uint8_t *>(input.buffer) - 131071, 0);


        if (hoffset >= minhoffset) {
            do {
                tptr = static_cast<uint8_t *>(input.buffer) + hoffset;
                if (cptr[blen] == tptr[blen]) {
                    tlen = matchlen(cptr, tptr, mlen);
                    if (tlen > blen) {
                        toffset = (cptr - 1) - tptr;
                        if (toffset < 1024 && tlen <= 10)       /* two byte long form */
                            tcost = 2;
                        else if (toffset < 16384 && tlen <= 67) /* three byte long form */
                            tcost = 3;
                        else                                /* four byte very long form */
                            tcost = 4;

                        if (tlen - tcost + 4 > blen - bcost + 4) {
                            blen = tlen;
                            bcost = tcost;
                            boffset = toffset;
                            if (blen >= 1028) break;
                        }
                    }
                }
            } while ((hoffset = link[hoffset & 131071]) >= minhoffset);
        }
        if (bcost >= blen) {
            hoffset = (cptr - static_cast<uint8_t *>(input.buffer));
            link[hoffset & 131071] = hashtbl[hash];
            hashtbl[hash] = hoffset;

            ++run;
            ++cptr;
            --len;
        } else {
            while (run > 3)                   /* literal block of data */
            {
                tlen = std::min((uint32_t) 112, run & ~3);
                run -= tlen;
                *to++ = (unsigned char) (0xe0 + (tlen >> 2) - 1);
                memcpy(to, rptr, tlen);
                rptr += tlen;
                to += tlen;
                ++countliterals;
            }
            if (bcost == 2)                   /* two byte long form */
            {
                *to++ = (unsigned char) (((boffset >> 8) << 5) + ((blen - 3) << 2) + run);
                *to++ = (unsigned char) boffset;
                ++countshort;
            } else if (bcost == 3)              /* three byte long form */
            {
                *to++ = (unsigned char) (0x80 + (blen - 4));
                *to++ = (unsigned char) ((run << 6) + (boffset >> 8));
                *to++ = (unsigned char) boffset;
                ++countlong;
            } else                            /* four byte very long form */
            {
                *to++ = (unsigned char) (0xc0 + ((boffset >> 16) << 4) + (((blen - 5) >> 8) << 2) + run);
                *to++ = (unsigned char) (boffset >> 8);
                *to++ = (unsigned char) (boffset);
                *to++ = (unsigned char) (blen - 5);
                ++countvlong;
            }
            if (run) {
                memcpy(to, rptr, run);
                to += run;
                run = 0;
            }
#if 0
            if (quick)
            {
                hoffset = (cptr-static_cast<uint8_t *>(input.buffer));
                link[hoffset&131071] = hashtbl[hash];
                hashtbl[hash] = hoffset;
                cptr += blen;
            }
            else
#endif
            {
                for (i = 0; i < (int) blen; ++i) {
                    hash = HASH(cptr);
                    hoffset = (cptr - static_cast<uint8_t *>(input.buffer));
                    link[hoffset & 131071] = hashtbl[hash];
                    hashtbl[hash] = hoffset;
                    ++cptr;
                }
            }

            rptr = cptr;
            len -= blen;
        }
    }
    while (run > 3)                       /* no match at end, use literal */
    {
        tlen = std::min((uint32_t) 112, run & ~3);
        run -= tlen;
        *to++ = (unsigned char) (0xe0 + (tlen >> 2) - 1);
        memcpy(to, rptr, tlen);
        rptr += tlen;
        to += tlen;
    }

    *to++ = (unsigned char) (0xfc + run); /* end of stream command + 0..3 literal */
    if (run) {
        memcpy(to, rptr, run);
        to += run;
    }

    delete[]link;
    delete[]hashtbl;

    output->lengthInBytes = (to - static_cast<uint8_t *>(output->buffer));
}

void decompress(const DecompressorInput &input, CompressorInput *output) {
    const uint8_t *index;
    uint8_t *ref;
    uint8_t *destIndex;
    uint8_t first;
    uint8_t second;
    uint8_t third;
    uint8_t forth;
    uint32_t run;

    index = static_cast<const uint8_t *>(input.buffer);

    output->lengthInBytes = 0;
    // read size from the stream
    for (int i = 0; i < 4; i++, index++)
        output->lengthInBytes |= (*index) << (i * 8);

    output->buffer = new
            uint8_t[output->lengthInBytes];

    destIndex = static_cast<uint8_t *>(output->buffer);

    for (;;) {
        first = *index++;
        if (!(first & 0x80))          /* short form */
        {
            second = *index++;
            run = first & 3;
            while (run--)
                *destIndex++ = *index++;
            ref = destIndex - 1 - (((first & 0x60) << 3) + second);
            run = ((first & 0x1c) >> 2) + 3 - 1;
            do {
                *destIndex++ = *ref++;
            } while (run--);
            continue;
        }
        if (!(first & 0x40))          /* long form */
        {
            second = *index++;
            third = *index++;
            run = second >> 6;
            while (run--)
                *destIndex++ = *index++;

            ref = destIndex - 1 - (((second & 0x3f) << 8) + third);

            run = (first & 0x3f) + 4 - 1;
            do {
                *destIndex++ = *ref++;
            } while (run--);
            continue;
        }
        if (!(first & 0x20))          /* very long form */
        {
            second = *index++;
            third = *index++;
            forth = *index++;
            run = first & 3;
            while (run--)
                *destIndex++ = *index++;

            ref = destIndex - 1 - (((first & 0x10) >> 4 << 16) + (second << 8) + third);

            run = ((first & 0x0c) >> 2 << 8) + forth + 5 - 1;
            do {
                *destIndex++ = *ref++;
            } while (run--);
            continue;
        }
        run = ((first & 0x1f) << 2) + 4;  /* literal */
        if (run <= 112) {
            while (run--)
                *destIndex++ = *index++;
            continue;
        }
        run = first & 3;              /* eof (+0..3 literal) */
        while (run--)
            *destIndex++ = *index++;
        break;
    }

//	if (static_cast<uint32_t>(static_cast<const uint8_t *>(destIndex)-static_cast<uint8_t *>(output->buffer))!=output->lengthInBytes)
//	{
//		Fatal("What happened?");
//	}
}
