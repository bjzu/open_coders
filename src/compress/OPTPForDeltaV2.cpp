/*-----------------------------------------------------------------------------
 *  OPTPForDeltaV2.cpp - A encoder/decoder for optimized PForDelta.
 *      This implementation given by Shuai Ding, who is of original authors
 *      proposing OPTPForDelta in http://dl.acm.org/citation.cfm?id=1526764.
 *
 *  Coding-Style:
 *      emacs) Mode: C, tab-width: 8, c-basic-offset: 8, indent-tabs-mode: nil
 *      vi) tabstop: 8, expandtab
 *
 *  Authors:
 *      Hao Yan, Shuai Ding, and Torsten Suel
 *      Takeshi Yamamuro <linguin.m.s_at_gmail.com>
 *       (modified this code partially)
 *-----------------------------------------------------------------------------
 */

#include "compress/OPTPForDeltaV2.hpp"

#define OPTPFORDELTAV2_NBLOCK   1
#define OPTPFORDELTAV2_BLOCKSZ  (128 * OPTPFORDELTAV2_NBLOCK)

/*
 * Lemme resume the block's format here, just to
 * not forget it too soon.
 *      |-----------------------------------|
 *      |             b |       nExceptions |
 *      |       22 bits |          10 bits  |
 *      |-----------------------------------|
 *      |        fixed_b(codewords)         |
 *      |-----------------------------------|
 *      |          s16(exceptions)          |
 *      |-----------------------------------|
 */
#define OPTPFORDELTAV2_B                22
#define OPTPFORDELTAV2_NEXCEPT          10

#define __optp4deltav2_copy(src, dest)  \
        __asm__ __volatile__(           \
                "movdqu %4, %%xmm0\n\t"         \
                "movdqu %5, %%xmm1\n\t"         \
                "movdqu %6, %%xmm2\n\t"         \
                "movdqu %7, %%xmm3\n\t"         \
                "movdqu %%xmm0, %0\n\t"         \
                "movdqu %%xmm1, %1\n\t"         \
                "movdqu %%xmm2, %2\n\t"         \
                "movdqu %%xmm3, %3\n\t"         \
                :"=m" (dest[0]), "=m" (dest[4]), "=m" (dest[8]), "=m" (dest[12])        \
                :"m" (src[0]), "m" (src[4]), "m" (src[8]), "m" (src[12])                \
                :"memory", "%xmm0", "%xmm1", "%xmm2", "%xmm3")
	
#define __optp4deltav2_zero32(dest)     \
        __asm__ __volatile__(           \
                "pxor   %%xmm0, %%xmm0\n\t"     \
                "movdqu %%xmm0, %0\n\t"         \
                "movdqu %%xmm0, %1\n\t"         \
                "movdqu %%xmm0, %2\n\t"         \
                "movdqu %%xmm0, %3\n\t"         \
                "movdqu %%xmm0, %4\n\t"         \
                "movdqu %%xmm0, %5\n\t"         \
                "movdqu %%xmm0, %6\n\t"         \
                "movdqu %%xmm0, %7\n\t"         \
                :"=m" (dest[0]), "=m" (dest[4]), "=m" (dest[8]), "=m" (dest[12]) ,               \
                        "=m" (dest[16]), "=m" (dest[20]), "=m" (dest[24]), "=m" (dest[28])      \
                ::"memory", "%xmm0")

/* A set of unpacking functions */
static void __optp4deltav2_unpack0(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack1(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack2(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack3(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack4(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack5(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack6(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack7(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack8(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack9(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack10(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack11(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack12(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack13(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack16(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack20(uint32_t *out, uint32_t *in);
static void __optp4deltav2_unpack32(uint32_t *out, uint32_t *in);

/* A interface of unpacking functions above */
typedef void (*__optp4deltav2_unpacker)(uint32_t *out, uint32_t *in);

static __optp4deltav2_unpacker  __optp4deltav2_unpack[] = {
        __optp4deltav2_unpack0,
        __optp4deltav2_unpack1,
        __optp4deltav2_unpack2,
        __optp4deltav2_unpack3,
        __optp4deltav2_unpack4,
        __optp4deltav2_unpack5,
        __optp4deltav2_unpack6,
        __optp4deltav2_unpack7,
        __optp4deltav2_unpack8,
        __optp4deltav2_unpack9,
        __optp4deltav2_unpack10,
        __optp4deltav2_unpack11,
        __optp4deltav2_unpack12,
        __optp4deltav2_unpack13,
        __optp4deltav2_unpack16,
        __optp4deltav2_unpack20,
        __optp4deltav2_unpack32
};

/* A hard-corded Simple16 decoder wirtten in the original code */
static inline void __optp4deltav2_simple16_decode(uint32_t *in, uint32_t len,
                uint32_t *out, uint32_t &nvalue) __attribute__((always_inline));

static uint32_t __optp4deltav2_possLogs[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 16, 20, 32
};

void
OPTPForDeltaV2::encodeBlock(uint32_t *in,
                uint32_t len, uint32_t b,
                uint32_t *out, uint32_t &nvalue)
{
        uint32_t        curExcept;
        uint32_t        *exceptionsPositions;
        uint32_t        *exceptionsValues;
        uint32_t        *exceptions;
        uint32_t        *encodedExceptions;
        uint32_t        encodedExceptions_sz;
        BitsWriter      *wt;

        if (len > 0) {
                exceptionsPositions = new uint32_t[len];
                exceptionsValues = new uint32_t[len];
                exceptions = new uint32_t[2 * len];
                encodedExceptions = new uint32_t[2 * len + 2];

                if (exceptionsPositions == NULL ||
                                exceptionsValues == NULL ||
                                exceptions == NULL || encodedExceptions == NULL)
                        eoutput("Can't allocate memory");

                wt = new BitsWriter(out + 1);

                if (wt == NULL)
                        eoutput("Can't initialize a class");

                if (b == 32) {
                        *out = (b << OPTPFORDELTAV2_NEXCEPT) | curExcept;

                        for (uint32_t i = 0; i < len; i++)
                                wt->bit_writer(in[i], b);
                        wt->bit_flush();
                        nvalue = 1 + wt->written;

                        delete wt;

                        return;
                }

                curExcept = 0;

                for (uint32_t i = 0; i < len; i++) {
                        wt->bit_writer(in[i], b);

                        if (in[i] >= (1U << b)) {
                                e = in[i] >> b;
                                exceptionsPositions[curExcept] = i;
                                exceptionsValues[curExcept] = e;
                                curExcept++;
                        }
                }

                if (curExcept > 0) {
                        uint32_t        cur;
                        uint32_t        prev;
                        uint32_t        *pout;

                        for (uint32_t i = curExcept - 1; i > 0; i--) {
                                cur = exceptionsPositions[i];
                                prev = exceptionsPositions[i - 1];

                                exceptionsPositions[i] = cur - prev;
                        }

                        for (uint32_t i = 0; i < curExcept; i++) {
                                exceptions[i] = exceptionsPositions[i] - 1;
                                exceptions[i + curExcept] = exceptionsValues[i];
                        }

                        Simple16::encodeArray(exceptions, 2 * curExcept,
                                        encodedExceptions, encodedExceptions_sz);
                }

                wt->bit_flush();

                /* Write a header following the format */
                *out = (b << OPTPFORDELTAV2_NEXCEPT) | curExcept;
                nvalue = 1 + wt->written;

                /* Write exceptional values */
                memcpy(out + nvalue, encodedExceptions, encodedExceptions_sz);
                nvalue += encodedExceptions_sz;

                /* Finalization */
                delete[] exceptionsValues;
                delete[] encodedExceptions;
                delete wt;
        }
}

void
OPTPForDeltaV2::encodeArray(uint32_t *in, uint32_t len,
                uint32_t *out, uint32_t &nvalue)
{
        uint32_t        numBlocks;
        uint32_t        b;
        uint32_t        csize;

        numBlocks = int_utils::div_roundup(len,
                        OPTPFORDELTAV2_BLOCKSZ); 

        /* Output the number of blocks */
        *out++ = numBlocks;
        nvalue = 1;

        for (uint32_t i = 0; i < numBlocks; i++) {
                uint32_t        chunksz;

                chunksz = UINT32_MAX;

                if (__likely(i != numBlocks - 1)) {
                        for (uint32_t j = 0;
                                j < __array_size(__optp4deltav2_possLogs); j++) {
                                encodeBlock(in, OPTPFORDELTAV2_BLOCKSZ,
                                        __optp4deltav2_possLogs[j], out, csize);

                                if (chunksz > csize) {
                                        b = __optp4deltav2_possLogs[j];
                                        chunksz = cisze;
                                }
                        }

                        /* Do decoding with a optimized paramter */
                        encodeBlock(in, OPTPFORDELTAV2_BLOCKSZ, b, out, csize);

                        in += OPTPFORDELTAV2_BLOCKSZ; 
                        out += csize;
                } else {
                        /*
                         * This is a code to pack gabage in the tail of lists.
                         * I think it couldn't be a bottleneck.
                         */
                        uint32_t        nblk;

                        nblk = ((len % OPTPFORDELTAV2_BLOCKSZ) != 0)?
                                len % OPTPFORDELTAV2_BLOCKSZ : OPTPFORDELTAV2_BLOCKSZ; 

                        for (uint32_t j = 0;
                                j < __array_size(__optp4deltav2_possLogs); j++) {
                                encodeBlock(in, nblk,
                                        __optp4deltav2_possLogs[j], out, csize);

                                if (chunksz > csize) {
                                        b = __optp4deltav2_possLogs[j];
                                        chunksz = cisze;
                                }
                        }

                        /* Do decoding with a optimized paramter */
                        encodeBlock(in, nblk, b, out, csize);
                }

                nvalue += csize;
        }
}

void
OPTPForDeltaV2::decodeArray(uint32_t *in, uint32_t len,
                uint32_t *out, uint32_t nvalue)
{
        uint32_t        numBlocks;
        uint32_t        b;
        uint32_t        nExceptions;
        uint32_t        nvalue;
        uint32_t        lpos;
        uint32_t        except[2 * OPTPFORDELTAV2_BLOCKSZ + TAIL_MERGIN + 1];

        numBlocks = *in++;

        for (uint32_t i = 0; i < numBlocks; i++) {
                b = *in >> OPTPFORDELTAV2_NEXCEPT; 
                nExceptions = *in & ((1 << OPTPFORDELTAV2_NEXCEPT) - 1); 

                __optp4deltav2_unpack[b](out, ++in);
                in += ((b * OPTPFORDELTAV2_BLOCKSZ) >> 5);

                if (nExceptions != 0) {
                        __optp4deltav2_simple16_decode(in, 2 * nExceptions, except, nvalue);
                        in += nvalue;

                        for (uint32_t j = 0, lpos = except[0]; j < nExceptions; j++) {
                                out[lpos] += except[nExceptions + j] << b;
                                lpos += except[j + 1] + 1;
                        }
                }
        }
}

/* --- Intra functions below --- */

void
__optp4deltav2_simple16_decode(uint32_t *in, uint32_t len
                uint32_t *out, uint32_t &nvalue)
{
        uint32_t        hd;
        uint32_t        nlen;

        nvalue = 0;
        nlen = 0;

        while (len > nlen) {
                hd = *in >> 28;

                switch (hd) {
                case 0:
                        *out++ = (*in >> 27) & 0x01;
                        *out++ = (*in >> 26) & 0x01;
                        *out++ = (*in >> 25) & 0x01;
                        *out++ = (*in >> 24) & 0x01;
                        *out++ = (*in >> 23) & 0x01;
                        *out++ = (*in >> 22) & 0x01;
                        *out++ = (*in >> 21) & 0x01;
                        *out++ = (*in >> 20) & 0x01;
                        *out++ = (*in >> 19) & 0x01;
                        *out++ = (*in >> 18) & 0x01;
                        *out++ = (*in >> 17) & 0x01;
                        *out++ = (*in >> 16) & 0x01;
                        *out++ = (*in >> 15) & 0x01;
                        *out++ = (*in >> 14) & 0x01;
                        *out++ = (*in >> 13) & 0x01;
                        *out++ = (*in >> 12) & 0x01;
                        *out++ = (*in >> 11) & 0x01;
                        *out++ = (*in >> 10) & 0x01;
                        *out++ = (*in >> 9) & 0x01;
                        *out++ = (*in >> 8) & 0x01;
                        *out++ = (*in >> 7) & 0x01;
                        *out++ = (*in >> 6) & 0x01;
                        *out++ = (*in >> 5) & 0x01;
                        *out++ = (*in >> 4) & 0x01;
                        *out++ = (*in >> 3) & 0x01;
                        *out++ = (*in >> 2) & 0x01;
                        *out++ = (*in >> 1) & 0x01;
                        *out++ = *in++ & 0x01;

                        nvalue++;
                        nlen += 28;

                        break;

                case 1:
                        *out++ = (*in >> 26) & 0x03;
                        *out++ = (*in >> 24) & 0x03;
                        *out++ = (*in >> 22) & 0x03;
                        *out++ = (*in >> 20) & 0x03;
                        *out++ = (*in >> 18) & 0x03;
                        *out++ = (*in >> 16) & 0x03;
                        *out++ = (*in >> 14) & 0x03;

                        *out++ = (*in >> 13) & 0x01;
                        *out++ = (*in >> 12) & 0x01;
                        *out++ = (*in >> 11) & 0x01;
                        *out++ = (*in >> 10) & 0x01;
                        *out++ = (*in >> 9) & 0x01;
                        *out++ = (*in >> 8) & 0x01;
                        *out++ = (*in >> 7) & 0x01;
                        *out++ = (*in >> 6) & 0x01;
                        *out++ = (*in >> 5) & 0x01;
                        *out++ = (*in >> 4) & 0x01;
                        *out++ = (*in >> 3) & 0x01;
                        *out++ = (*in >> 2) & 0x01;
                        *out++ = (*in >> 1) & 0x01;
                        *out++ = *in++ & 0x01;

                        nvalue++;
                        nlen += 21;

                        break;

                case 2:
                        *out = (*in >> 27) & 0x01;
                        *out = (*in >> 26) & 0x01;
                        *out = (*in >> 25) & 0x01;
                        *out = (*in >> 24) & 0x01;
                        *out = (*in >> 23) & 0x01;
                        *out = (*in >> 22) & 0x01;
                        *out = (*in >> 21) & 0x01;

                        *out = (*in >> 19) & 0x03;
                        *out = (*in >> 17) & 0x03;
                        *out = (*in >> 15) & 0x03;
                        *out = (*in >> 13) & 0x03;
                        *out = (*in >> 11) & 0x03;
                        *out = (*in >> 9) & 0x03;
                        *out = (*in >> 7) & 0x03;

                        *out = (*in >> 6) & 0x01;
                        *out = (*in >> 5) & 0x01;
                        *out = (*in >> 4) & 0x01;
                        *out = (*in >> 3) & 0x01;
                        *out = (*in >> 2) & 0x01;
                        *out = (*in >> 1) & 0x01;
                        *out = *in++ & 0x01;

                        nvalue++;
                        nlen += 21;

                        break;

                case 3:
                        *out++ = (*in >> 27) & 0x01;
                        *out++ = (*in >> 26) & 0x01;
                        *out++ = (*in >> 25) & 0x01;
                        *out++ = (*in >> 24) & 0x01;
                        *out++ = (*in >> 23) & 0x01;
                        *out++ = (*in >> 22) & 0x01;
                        *out++ = (*in >> 21) & 0x01;
                        *out++ = (*in >> 20) & 0x01;
                        *out++ = (*in >> 19) & 0x01;
                        *out++ = (*in >> 18) & 0x01;
                        *out++ = (*in >> 17) & 0x01;
                        *out++ = (*in >> 16) & 0x01;
                        *out++ = (*in >> 15) & 0x01;
                        *out++ = (*in >> 14) & 0x01;

                        *out++ = (*in >> 12) & 0x03;
                        *out++ = (*in >> 10) & 0x03;
                        *out++ = (*in >> 8) & 0x03;
                        *out++ = (*in >> 6) & 0x03;
                        *out++ = (*in >> 4) & 0x03;
                        *out++ = (*in >> 2) & 0x03;
                        *out++ = *in++ & 0x03;

                        nvalue++;
                        nlen += 21;

                        break;

                case 4:
                        *out++ = (*in >> 26) & 0x03;
                        *out++ = (*in >> 24) & 0x03;
                        *out++ = (*in >> 22) & 0x03;
                        *out++ = (*in >> 20) & 0x03;
                        *out++ = (*in >> 18) & 0x03;
                        *out++ = (*in >> 16) & 0x03;
                        *out++ = (*in >> 14) & 0x03;
                        *out++ = (*in >> 12) & 0x03;
                        *out++ = (*in >> 10) & 0x03;
                        *out++ = (*in >> 8) & 0x03;
                        *out++ = (*in >> 6) & 0x03;
                        *out++ = (*in >> 4) & 0x03;
                        *out++ = (*in >> 2) & 0x03;
                        *out++ = *in++ & 0x03;

                        nvalue++;
                        nlen += 14;

                        break;

                case 5:
                        *out++ = (*in >> 24) & 0x0f;

                        *out++ = (*in >> 21) & 0x07;
                        *out++ = (*in >> 18) & 0x07;
                        *out++ = (*in >> 15) & 0x07;
                        *out++ = (*in >> 12) & 0x07;
                        *out++ = (*in >> 9) & 0x07;
                        *out++ = (*in >> 6) & 0x07;
                        *out++ = (*in >> 3) & 0x07;
                        *out++ = *in++ & 0x07;

                        nvalue++;
                        nlen += 9;

                        break;

                case 6:
                        *out++ = (*in >> 25) & 0x07;

                        *out++ = (*in >> 21) & 0x0f;
                        *out++ = (*in >> 17) & 0x0f;
                        *out++ = (*in >> 13) & 0x0f;
                        *out++ = (*in >> 9) & 0x0f;

                        *out++ = (*in >> 6) & 0x07;
                        *out++ = (*in >> 3) & 0x07;
                        *out++ = *in++ & 0x07;

                        nvalue++;
                        nlen += 8;

                        break;

                case 7:
                        *out++ = (*in >> 24) & 0x0f;
                        *out++ = (*in >> 20) & 0x0f;
                        *out++ = (*in >> 16) & 0x0f;
                        *out++ = (*in >> 12) & 0x0f;
                        *out++ = (*in >> 8) & 0x0f;
                        *out++ = (*in >> 4) & 0x0f;
                        *out++ = *in++ & 0x0f;

                        nvalue++;
                        nlen += 7;

                        break;

                case 8:
                        *out++ = (*in >> 23) & 0x1f;
                        *out++ = (*in >> 18) & 0x1f;
                        *out++ = (*in >> 13) & 0x1f;
                        *out++ = (*in >> 8) & 0x1f;

                        *out++ = (*in >> 4) & 0x0f;
                        *out++ = *in++ & 0x0f;

                        nvalue++;
                        nlen += 6;

                        break;

                case 9:
                        *out++ = (*in >> 24) & 0x0f;
                        *out++ = (*in >> 20) & 0x0f;

                        *out++ = (*in >> 15) & 0x1f;
                        *out++ = (*in >> 10) & 0x1f;
                        *out++ = (*in >> 5) & 0x1f;
                        *out++ = *in++ & 0x1f;

                        nvalue++;
                        nlen += 6;

                        break;

                case 10:
                        *out++ = (*in >> 22) & 0x3f;
                        *out++ = (*in >> 16) & 0x3f;
                        *out++ = (*in >> 10) & 0x3f;

                        *out++ = (*in >> 5) & 0x1f;
                        *out++ = *in++ & 0x1f;

                        nvalue++;
                        nlen += 5;

                        break;

                case 11:
                        *out++ = (*in >> 23) & 0x1f;
                        *out++ = (*in >> 18) & 0x1f;

                        *out++ = (*in >> 12) & 0x3f;
                        *out++ = (*in >> 6) & 0x3f;
                        *out++ = *in++ & 0x3f;

                        nvalue++;
                        nlen += 5;

                        break;

                case 12:
                        *out++ = (*in >> 21) & 0x7f;
                        *out++ = (*in >> 14) & 0x7f;
                        *out++ = (*in >> 7) & 0x7f;
                        *out++ = *in++ & 0x7f;
                       
                        nvalue++;
                        nlen += 4;

                        break;

                case 13:
                        *out++ = (*in >> 18) & 0x03ff;

                        *out++ = (*in >> 9) & 0x01ff;
                        *out++ = *in++ & 0x01ff;

                        nvalue++;
                        nlen += 3;

                        break;

                case 14:
                        *out++ = (*in >> 14) & 0x3fff;
                        *out++ = *in++ & 0x3fff;

                        nvalue++;
                        nlen += 2;

                        break;

                case 15:
                        *out++ = *in++ & 0x0fffffff;

                        nvalue++;
                        nlen += 1;

                        break;

                }
        }
}

void
__optp4deltav2_unpack0(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32) {
                __optp4deltav2_zero32(out);
        }
}

void
__optp4deltav2_unpack1(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 1) {
                out[0] = in[0] >> 31;
                out[1] = (in[0] >> 30) & 0x01;
                out[2] = (in[0] >> 29) & 0x01;
                out[3] = (in[0] >> 28) & 0x01;
                out[4] = (in[0] >> 27) & 0x01;
                out[5] = (in[0] >> 26) & 0x01;
                out[6] = (in[0] >> 25) & 0x01;
                out[7] = (in[0] >> 24) & 0x01;
                out[8] = (in[0] >> 23) & 0x01;
                out[9] = (in[0] >> 22) & 0x01;
                out[10] = (in[0] >> 21) & 0x01;
                out[11] = (in[0] >> 20) & 0x01;
                out[12] = (in[0] >> 19) & 0x01;
                out[13] = (in[0] >> 18) & 0x01;
                out[14] = (in[0] >> 17) & 0x01;
                out[15] = (in[0] >> 16) & 0x01;
                out[16] = (in[0] >> 15) & 0x01;
                out[17] = (in[0] >> 14) & 0x01;
                out[18] = (in[0] >> 13) & 0x01;
                out[19] = (in[0] >> 12) & 0x01;
                out[20] = (in[0] >> 11) & 0x01;
                out[21] = (in[0] >> 10) & 0x01;
                out[22] = (in[0] >> 9) & 0x01;
                out[23] = (in[0] >> 8) & 0x01;
                out[24] = (in[0] >> 7) & 0x01;
                out[25] = (in[0] >> 6) & 0x01;
                out[26] = (in[0] >> 5) & 0x01;
                out[27] = (in[0] >> 4) & 0x01;
                out[28] = (in[0] >> 3) & 0x01;
                out[29] = (in[0] >> 2) & 0x01;
                out[30] = (in[0] >> 1) & 0x01;
                out[31] = in[0] & 0x01;
        }
}

void
__optp4deltav2_unpack2(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 2) {
                out[0] = in[0] >> 30;
                out[1] = (in[0] >> 28) & 0x03;
                out[2] = (in[0] >> 26) & 0x03;
                out[3] = (in[0] >> 24) & 0x03;
                out[4] = (in[0] >> 22) & 0x03;
                out[5] = (in[0] >> 20) & 0x03;
                out[6] = (in[0] >> 18) & 0x03;
                out[7] = (in[0] >> 16) & 0x03;
                out[8] = (in[0] >> 14) & 0x03;
                out[9] = (in[0] >> 12) & 0x03;
                out[10] = (in[0] >> 10) & 0x03;
                out[11] = (in[0] >> 8) & 0x03;
                out[12] = (in[0] >> 6) & 0x03;
                out[13] = (in[0] >> 4) & 0x03;
                out[14] = (in[0] >> 2) & 0x03;
                out[15] = in[0] & 0x03;
                out[16] = in[1] >> 30;
                out[17] = (in[1] >> 28) & 0x03;
                out[18] = (in[1] >> 26) & 0x03;
                out[19] = (in[1] >> 24) & 0x03;
                out[20] = (in[1] >> 22) & 0x03;
                out[21] = (in[1] >> 20) & 0x03;
                out[22] = (in[1] >> 18) & 0x03;
                out[23] = (in[1] >> 16) & 0x03;
                out[24] = (in[1] >> 14) & 0x03;
                out[25] = (in[1] >> 12) & 0x03;
                out[26] = (in[1] >> 10) & 0x03;
                out[27] = (in[1] >> 8) & 0x03;
                out[28] = (in[1] >> 6) & 0x03;
                out[29] = (in[1] >> 4) & 0x03;
                out[30] = (in[1] >> 2) & 0x03;
                out[31] = in[1] & 0x03;
        }
}

void
__optp4deltav2_unpack3(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 3) {
                out[0] = in[0] >> 29;
                out[1] = (in[0] >> 26) & 0x07;
                out[2] = (in[0] >> 23) & 0x07;
                out[3] = (in[0] >> 20) & 0x07;
                out[4] = (in[0] >> 17) & 0x07;
                out[5] = (in[0] >> 14) & 0x07;
                out[6] = (in[0] >> 11) & 0x07;
                out[7] = (in[0] >> 8) & 0x07;
                out[8] = (in[0] >> 5) & 0x07;
                out[9] = (in[0] >> 2) & 0x07;
                out[10] = (in[0] << 1) & 0x07;
                out[10] |= in[1] >> 31;
                out[11] = (in[1] >> 28) & 0x07;
                out[12] = (in[1] >> 25) & 0x07;
                out[13] = (in[1] >> 22) & 0x07;
                out[14] = (in[1] >> 19) & 0x07;
                out[15] = (in[1] >> 16) & 0x07;
                out[16] = (in[1] >> 13) & 0x07;
                out[17] = (in[1] >> 10) & 0x07;
                out[18] = (in[1] >> 7) & 0x07;
                out[19] = (in[1] >> 4) & 0x07;
                out[20] = (in[1] >> 1) & 0x07;
                out[21] = (in[1] << 2) & 0x07;
                out[21] |= in[2] >> 30;
                out[22] = (in[2] >> 27) & 0x07;
                out[23] = (in[2] >> 24) & 0x07;
                out[24] = (in[2] >> 21) & 0x07;
                out[25] = (in[2] >> 18) & 0x07;
                out[26] = (in[2] >> 15) & 0x07;
                out[27] = (in[2] >> 12) & 0x07;
                out[28] = (in[2] >> 9) & 0x07;
                out[29] = (in[2] >> 6) & 0x07;
                out[30] = (in[2] >> 3) & 0x07;
                out[31] = in[2] & 0x07;
        }
}

void
__optp4deltav2_unpack4(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 4) {
                out[0] = in[0] >> 28;
                out[1] = (in[0] >> 24) & 0x0f;
                out[2] = (in[0] >> 20) & 0x0f;
                out[3] = (in[0] >> 16) & 0x0f;
                out[4] = (in[0] >> 12) & 0x0f;
                out[5] = (in[0] >> 8) & 0x0f;
                out[6] = (in[0] >> 4) & 0x0f;
                out[7] = in[0] & 0x0f;
                out[8] = in[1] >> 28;
                out[9] = (in[1] >> 24) & 0x0f;
                out[10] = (in[1] >> 20) & 0x0f;
                out[11] = (in[1] >> 16) & 0x0f;
                out[12] = (in[1] >> 12) & 0x0f;
                out[13] = (in[1] >> 8) & 0x0f;
                out[14] = (in[1] >> 4) & 0x0f;
                out[15] = in[1] & 0x0f;
                out[16] = in[2] >> 28;
                out[17] = (in[2] >> 24) & 0x0f;
                out[18] = (in[2] >> 20) & 0x0f;
                out[19] = (in[2] >> 16) & 0x0f;
                out[20] = (in[2] >> 12) & 0x0f;
                out[21] = (in[2] >> 8) & 0x0f;
                out[22] = (in[2] >> 4) & 0x0f;
                out[23] = in[2] & 0x0f;
                out[24] = in[3] >> 28;
                out[25] = (in[3] >> 24) & 0x0f;
                out[26] = (in[3] >> 20) & 0x0f;
                out[27] = (in[3] >> 16) & 0x0f;
                out[28] = (in[3] >> 12) & 0x0f;
                out[29] = (in[3] >> 8) & 0x0f;
                out[30] = (in[3] >> 4) & 0x0f;
                out[31] = in[3] & 0x0f;
        }
}

void
__optp4deltav2_unpack5(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 5) {
                out[0] = in[0] >> 27;
                out[1] = (in[0] >> 22) & 0x1f;
                out[2] = (in[0] >> 17) & 0x1f;
                out[3] = (in[0] >> 12) & 0x1f;
                out[4] = (in[0] >> 7) & 0x1f;
                out[5] = (in[0] >> 2) & 0x1f;
                out[6] = (in[0] << 3) & 0x1f;
                out[6] |= in[1] >> 29;
                out[7] = (in[1] >> 24) & 0x1f;
                out[8] = (in[1] >> 19) & 0x1f;
                out[9] = (in[1] >> 14) & 0x1f;
                out[10] = (in[1] >> 9) & 0x1f;
                out[11] = (in[1] >> 4) & 0x1f;
                out[12] = (in[1] << 1) & 0x1f;
                out[12] |= in[2] >> 0x1f;
                out[13] = (in[2] >> 26) & 0x1f;
                out[14] = (in[2] >> 21) & 0x1f;
                out[15] = (in[2] >> 16) & 0x1f;
                out[16] = (in[2] >> 11) & 0x1f;
                out[17] = (in[2] >> 6) & 0x1f;
                out[18] = (in[2] >> 1) & 0x1f;
                out[19] = (in[2] << 4) & 0x1f;
                out[19] |= in[3] >> 28;
                out[20] = (in[3] >> 23) & 0x1f;
                out[21] = (in[3] >> 18) & 0x1f;
                out[22] = (in[3] >> 13) & 0x1f;
                out[23] = (in[3] >> 8) & 0x1f;
                out[24] = (in[3] >> 3) & 0x1f;
                out[25] = (in[3] << 2) & 0x1f;
                out[25] |= in[4] >> 30;
                out[26] = (in[4] >> 25) & 0x1f;
                out[27] = (in[4] >> 20) & 0x1f;
                out[28] = (in[4] >> 15) & 0x1f;
                out[29] = (in[4] >> 10) & 0x1f;
                out[30] = (in[4] >> 5) & 0x1f;
                out[31] = in[4] & 0x1f;
        }
}

void
__optp4deltav2_unpack6(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 6) {
                out[0] = in[0] >> 26;
                out[1] = (in[0] >> 20) & 0x3f;
                out[2] = (in[0] >> 14) & 0x3f;
                out[3] = (in[0] >> 8) & 0x3f;
                out[4] = (in[0] >> 2) & 0x3f;
                out[5] = (in[0] << 4) & 0x3f;
                out[5] |= in[1] >> 28;
                out[6] = (in[1] >> 22) & 0x3f;
                out[7] = (in[1] >> 16) & 0x3f;
                out[8] = (in[1] >> 10) & 0x3f;
                out[9] = (in[1] >> 4) & 0x3f;
                out[10] = (in[1] << 2) & 0x3f;
                out[10] |= in[2] >> 30;
                out[11] = (in[2] >> 24) & 0x3f;
                out[12] = (in[2] >> 18) & 0x3f;
                out[13] = (in[2] >> 12) & 0x3f;
                out[14] = (in[2] >> 6) & 0x3f;
                out[15] = in[2] & 0x3f;
                out[16] = in[3] >> 26;
                out[17] = (in[3] >> 20) & 0x3f;
                out[18] = (in[3] >> 14) & 0x3f;
                out[19] = (in[3] >> 8) & 0x3f;
                out[20] = (in[3] >> 2) & 0x3f;
                out[21] = (in[3] << 4) & 0x3f;
                out[21] |= in[4] >> 28;
                out[22] = (in[4] >> 22) & 0x3f;
                out[23] = (in[4] >> 16) & 0x3f;
                out[24] = (in[4] >> 10) & 0x3f;
                out[25] = (in[4] >> 4) & 0x3f;
                out[26] = (in[4] << 2) & 0x3f;
                out[26] |= in[5] >> 30;
                out[27] = (in[5] >> 24) & 0x3f;
                out[28] = (in[5] >> 18) & 0x3f;
                out[29] = (in[5] >> 12) & 0x3f;
                out[30] = (in[5] >> 6) & 0x3f;
                out[31] = in[5] & 0x3f;
        }
}

void
__optp4deltav2_unpack7(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 7) {
                out[0] = in[0] >> 25;
                out[1] = (in[0] >> 18) & 0x7f;
                out[2] = (in[0] >> 11) & 0x7f;
                out[3] = (in[0] >> 4) & 0x7f;
                out[4] = (in[0] << 3) & 0x7f;
                out[4] |= in[1] >> 29;
                out[5] = (in[1] >> 22) & 0x7f;
                out[6] = (in[1] >> 15) & 0x7f;
                out[7] = (in[1] >> 8) & 0x7f;
                out[8] = (in[1] >> 1) & 0x7f;
                out[9] = (in[1] << 6) & 0x7f;
                out[9] |= in[2] >> 26;
                out[10] = (in[2] >> 19) & 0x7f;
                out[11] = (in[2] >> 12) & 0x7f;
                out[12] = (in[2] >> 5) & 0x7f;
                out[13] = (in[2] << 2) & 0x7f;
                out[13] |= in[3] >> 30;
                out[14] = (in[3] >> 23) & 0x7f;
                out[15] = (in[3] >> 16) & 0x7f;
                out[16] = (in[3] >> 9) & 0x7f;
                out[17] = (in[3] >> 2) & 0x7f;
                out[18] = (in[3] << 5) & 0x7f;
                out[18] |= in[4] >> 27;
                out[19] = (in[4] >> 20) & 0x7f;
                out[20] = (in[4] >> 13) & 0x7f;
                out[21] = (in[4] >> 6) & 0x7f;
                out[22] = (in[4] << 1) & 0x7f;
                out[22] |= in[5] >> 31;
                out[23] = (in[5] >> 24) & 0x7f;
                out[24] = (in[5] >> 17) & 0x7f;
                out[25] = (in[5] >> 10) & 0x7f;
                out[26] = (in[5] >> 3) & 0x7f;
                out[27] = (in[5] << 4) & 0x7f;
                out[27] |= in[6] >> 28;
                out[28] = (in[6] >> 21) & 0x7f;
                out[29] = (in[6] >> 14) & 0x7f;
                out[30] = (in[6] >> 7) & 0x7f;
                out[31] = in[6] & 0x7f;
        }
}

void
__optp4deltav2_unpack8(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 8) {
                out[0] = in[0] >> 24;
                out[1] = (in[0] >> 16) & 0xff;
                out[2] = (in[0] >> 8) & 0xff;
                out[3] = in[0] & 0xff;
                out[4] = in[1] >> 24;
                out[5] = (in[1] >> 16) & 0xff;
                out[6] = (in[1] >> 8) & 0xff;
                out[7] = in[1] & 0xff;
                out[8] = in[2] >> 24;
                out[9] = (in[2] >> 16) & 0xff;
                out[10] = (in[2] >> 8) & 0xff;
                out[11] = in[2] & 0xff;
                out[12] = in[3] >> 24;
                out[13] = (in[3] >> 16) & 0xff;
                out[14] = (in[3] >> 8) & 0xff;
                out[15] = in[3] & 0xff;
                out[16] = in[4] >> 24;
                out[17] = (in[4] >> 16) & 0xff;
                out[18] = (in[4] >> 8) & 0xff;
                out[19] = in[4] & 0xff;
                out[20] = in[5] >> 24;
                out[21] = (in[5] >> 16) & 0xff;
                out[22] = (in[5] >> 8) & 0xff;
                out[23] = in[5] & 0xff;
                out[24] = in[6] >> 24;
                out[25] = (in[6] >> 16) & 0xff;
                out[26] = (in[6] >> 8) & 0xff;
                out[27] = in[6] & 0xff;
                out[28] = in[7] >> 24;
                out[29] = (in[7] >> 16) & 0xff;
                out[30] = (in[7] >> 8) & 0xff;
                out[31] = in[7] & 0xff;
        }
}

void
__optp4deltav2_unpack9(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 9) {
                out[0] = in[0] >> 23;
                out[1] = (in[0] >> 14) & 0x01ff;
                out[2] = (in[0] >> 5) & 0x01ff;
                out[3] = (in[0] << 4) & 0x01ff;
                out[3] |= in[1] >> 28;
                out[4] = (in[1] >> 19) & 0x01ff;
                out[5] = (in[1] >> 10) & 0x01ff;
                out[6] = (in[1] >> 1) & 0x01ff;
                out[7] = (in[1] << 8) & 0x01ff;
                out[7] |= in[2] >> 24;
                out[8] = (in[2] >> 15) & 0x01ff;
                out[9] = (in[2] >> 6) & 0x01ff;
                out[10] = (in[2] << 3) & 0x01ff;
                out[10] |= in[3] >> 29;
                out[11] = (in[3] >> 20) & 0x01ff;
                out[12] = (in[3] >> 11) & 0x01ff;
                out[13] = (in[3] >> 2) & 0x01ff;
                out[14] = (in[3] << 7) & 0x01ff;
                out[14] |= in[4] >> 25;
                out[15] = (in[4] >> 16) & 0x01ff;
                out[16] = (in[4] >> 7) & 0x01ff;
                out[17] = (in[4] << 2) & 0x01ff;
                out[17] |= in[5] >> 30;
                out[18] = (in[5] >> 21) & 0x01ff;
                out[19] = (in[5] >> 12) & 0x01ff;
                out[20] = (in[5] >> 3) & 0x01ff;
                out[21] = (in[5] << 6) & 0x01ff;
                out[21] |= in[6] >> 26;
                out[22] = (in[6] >> 17) & 0x01ff;
                out[23] = (in[6] >> 8) & 0x01ff;
                out[24] = (in[6] << 1) & 0x01ff;
                out[24] |= in[7] >> 31;
                out[25] = (in[7] >> 22) & 0x01ff;
                out[26] = (in[7] >> 13) & 0x01ff;
                out[27] = (in[7] >> 4) & 0x01ff;
                out[28] = (in[7] << 5) & 0x01ff;
                out[28] |= in[8] >> 27;
                out[29] = (in[8] >> 18) & 0x01ff;
                out[30] = (in[8] >> 9) & 0x01ff;
                out[31] = in[8] & 0x01ff;
        }
}

void
__optp4deltav2_unpack10(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 10) {
                out[0] = in[0] >> 22;
                out[1] = (in[0] >> 12) & 0x03ff;
                out[2] = (in[0] >> 2) & 0x03ff;
                out[3] = (in[0] << 8) & 0x03ff;
                out[3] |= in[1] >> 24;
                out[4] = (in[1] >> 14) & 0x03ff;
                out[5] = (in[1] >> 4) & 0x03ff;
                out[6] = (in[1] << 6) & 0x03ff;
                out[6] |= in[2] >> 26;
                out[7] = (in[2] >> 16) & 0x03ff;
                out[8] = (in[2] >> 6) & 0x03ff;
                out[9] = (in[2] << 4) & 0x03ff;
                out[9] |= in[3] >> 28;
                out[10] = (in[3] >> 18) & 0x03ff;
                out[11] = (in[3] >> 8) & 0x03ff;
                out[12] = (in[3] << 2) & 0x03ff;
                out[12] |= in[4] >> 30;
                out[13] = (in[4] >> 20) & 0x03ff;
                out[14] = (in[4] >> 10) & 0x03ff;
                out[15] = in[4] & 0x03ff;
                out[16] = in[5] >> 22;
                out[17] = (in[5] >> 12) & 0x03ff;
                out[18] = (in[5] >> 2) & 0x03ff;
                out[19] = (in[5] << 8) & 0x03ff;
                out[19] |= in[6] >> 24;
                out[20] = (in[6] >> 14) & 0x03ff;
                out[21] = (in[6] >> 4) & 0x03ff;
                out[22] = (in[6] << 6) & 0x03ff;
                out[22] |= in[7] >> 26;
                out[23] = (in[7] >> 16) & 0x03ff;
                out[24] = (in[7] >> 6) & 0x03ff;
                out[25] = (in[7] << 4) & 0x03ff;
                out[25] |= in[8] >> 28;
                out[26] = (in[8] >> 18) & 0x03ff;
                out[27] = (in[8] >> 8) & 0x03ff;
                out[28] = (in[8] << 2) & 0x03ff;
                out[28] |= in[9] >> 30;
                out[29] = (in[9] >> 20) & 0x03ff;
                out[30] = (in[9] >> 10) & 0x03ff;
                out[31] = in[9] & 0x03ff;
        }
}

void
__optp4deltav2_unpack11(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 11) {
                out[0] = in[0] >> 21;
                out[1] = (in[0] >> 10) & 0x07ff;
                out[2] = (in[0] << 1) & 0x07ff;
                out[2] |= in[1] >> 31;
                out[3] = (in[1] >> 20) & 0x07ff;
                out[4] = (in[1] >> 9) & 0x07ff;
                out[5] = (in[1] << 2) & 0x07ff;
                out[5] |= in[2] >> 30;
                out[6] = (in[2] >> 19) & 0x07ff;
                out[7] = (in[2] >> 8) & 0x07ff;
                out[8] = (in[2] << 3) & 0x07ff;
                out[8] |= in[3] >> 29;
                out[9] = (in[3] >> 18) & 0x07ff;
                out[10] = (in[3] >> 7) & 0x07ff;
                out[11] = (in[3] << 4) & 0x07ff;
                out[11] |= in[4] >> 28;
                out[12] = (in[4] >> 17) & 0x07ff;
                out[13] = (in[4] >> 6) & 0x07ff;
                out[14] = (in[4] << 5) & 0x07ff;
                out[14] |= in[5] >> 27;
                out[15] = (in[5] >> 16) & 0x07ff;
                out[16] = (in[5] >> 5) & 0x07ff;
                out[17] = (in[5] << 6) & 0x07ff;
                out[17] |= in[6] >> 26;
                out[18] = (in[6] >> 15) & 0x07ff;
                out[19] = (in[6] >> 4) & 0x07ff;
                out[20] = (in[6] << 7) & 0x07ff;
                out[20] |= in[7] >> 25;
                out[21] = (in[7] >> 14) & 0x07ff;
                out[22] = (in[7] >> 3) & 0x07ff;
                out[23] = (in[7] << 8) & 0x07ff;
                out[23] |= in[8] >> 24;
                out[24] = (in[8] >> 13) & 0x07ff;
                out[25] = (in[8] >> 2) & 0x07ff;
                out[26] = (in[8] << 9) & 0x07ff;
                out[26] |= in[9] >> 23;
                out[27] = (in[9] >> 12) & 0x07ff;
                out[28] = (in[9] >> 1) & 0x07ff;
                out[29] = (in[9] << 10) & 0x07ff;
                out[29] |= in[10] >> 22;
                out[30] = (in[10] >> 11) & 0x07ff;
                out[31] = in[10] & 0x07ff;
        }
}

void
__optp4deltav2_unpack12(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 12) {
                out[0] = in[0] >> 20;
                out[1] = (in[0] >> 8) & 0x0fff;
                out[2] = (in[0] << 4) & 0x0fff;
                out[2] |= in[1] >> 28;
                out[3] = (in[1] >> 16) & 0x0fff;
                out[4] = (in[1] >> 4) & 0x0fff;
                out[5] = (in[1] << 8) & 0x0fff;
                out[5] |= in[2] >> 24;
                out[6] = (in[2] >> 12) & 0x0fff;
                out[7] = in[2] & 0x0fff;
                out[8] = in[3] >> 20;
                out[9] = (in[3] >> 8) & 0x0fff;
                out[10] = (in[3] << 4) & 0x0fff;
                out[10] |= in[4] >> 28;
                out[11] = (in[4] >> 16) & 0x0fff;
                out[12] = (in[4] >> 4) & 0x0fff;
                out[13] = (in[4] << 8) & 0x0fff;
                out[13] |= in[5] >> 24;
                out[14] = (in[5] >> 12) & 0x0fff;
                out[15] = in[5] & 0x0fff;
                out[16] = in[6] >> 20;
                out[17] = (in[6] >> 8) & 0x0fff;
                out[18] = (in[6] << 4) & 0x0fff;
                out[18] |= in[7] >> 28;
                out[19] = (in[7] >> 16) & 0x0fff;
                out[20] = (in[7] >> 4) & 0x0fff;
                out[21] = (in[7] << 8) & 0x0fff;
                out[21] |= in[8] >> 24;
                out[22] = (in[8] >> 12) & 0x0fff;
                out[23] = in[8] & 0x0fff;
                out[24] = in[9] >> 20;
                out[25] = (in[9] >> 8) & 0x0fff;
                out[26] = (in[9] << 4) & 0x0fff;
                out[26] |= in[10] >> 28;
                out[27] = (in[10] >> 16) & 0x0fff;
                out[28] = (in[10] >> 4) & 0x0fff;
                out[29] = (in[10] << 8) & 0x0fff;
                out[29] |= in[11] >> 24;
                out[30] = (in[11] >> 12) & 0x0fff;
                out[31] = in[11] & 0x0fff;
        }
}

void
__optp4deltav2_unpack13(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 13) {
                out[0] = in[0] >> 19;
                out[1] = (in[0] >> 6) & 0x1fff;
                out[2] = (in[0] << 7) & 0x1fff;
                out[2] |= in[1] >> 25;
                out[3] = (in[1] >> 12) & 0x1fff;
                out[4] = (in[1] << 1) & 0x1fff;
                out[4] |= in[2] >> 31;
                out[5] = (in[2] >> 18) & 0x1fff;
                out[6] = (in[2] >> 5) & 0x1fff;
                out[7] = (in[2] << 8) & 0x1fff;
                out[7] |= in[3] >> 24;
                out[8] = (in[3] >> 11) & 0x1fff;
                out[9] = (in[3] << 2) & 0x1fff;
                out[9] |= in[4] >> 30;
                out[10] = (in[4] >> 17) & 0x1fff;
                out[11] = (in[4] >> 4) & 0x1fff;
                out[12] = (in[4] << 9) & 0x1fff;
                out[12] |= in[5] >> 23;
                out[13] = (in[5] >> 10) & 0x1fff;
                out[14] = (in[5] << 3) & 0x1fff;
                out[14] |= in[6] >> 29;
                out[15] = (in[6] >> 16) & 0x1fff;
                out[16] = (in[6] >> 3) & 0x1fff;
                out[17] = (in[6] << 10) & 0x1fff;
                out[17] |= in[7] >> 22;
                out[18] = (in[7] >> 9) & 0x1fff;
                out[19] = (in[7] << 4) & 0x1fff;
                out[19] |= in[8] >> 28;
                out[20] = (in[8] >> 15) & 0x1fff;
                out[21] = (in[8] >> 2) & 0x1fff;
                out[22] = (in[8] << 11) & 0x1fff;
                out[22] |= in[9] >> 21;
                out[23] = (in[9] >> 8) & 0x1fff;
                out[24] = (in[9] << 5) & 0x1fff;
                out[24] |= in[10] >> 27;
                out[25] = (in[10] >> 14) & 0x1fff;
                out[26] = (in[10] >> 1) & 0x1fff;
                out[27] = (in[10] << 12) & 0x1fff;
                out[27] |= in[11] >> 20;
                out[28] = (in[11] >> 7) & 0x1fff;
                out[29] = (in[11] << 6) & 0x1fff;
                out[29] |= in[12] >> 26;
                out[30] = (in[12] >> 13) & 0x1fff;
                out[31] = in[12] & 0x1fff;
        }
}

void
__optp4deltav2_unpack16(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 16) {
                out[0] = in[0] >> 16;
                out[1] = in[0] & 0xffff;
                out[2] = in[1] >> 16;
                out[3] = in[1] & 0xffff;
                out[4] = in[2] >> 16;
                out[5] = in[2] & 0xffff;
                out[6] = in[3] >> 16;
                out[7] = in[3] & 0xffff;
                out[8] = in[4] >> 16;
                out[9] = in[4] & 0xffff;
                out[10] = in[5] >> 16;
                out[11] = in[5] & 0xffff;
                out[12] = in[6] >> 16;
                out[13] = in[6] & 0xffff;
                out[14] = in[7] >> 16;
                out[15] = in[7] & 0xffff;
                out[16] = in[8] >> 16;
                out[17] = in[8] & 0xffff;
                out[18] = in[9] >> 16;
                out[19] = in[9] & 0xffff;
                out[20] = in[10] >> 16;
                out[21] = in[10] & 0xffff;
                out[22] = in[11] >> 16;
                out[23] = in[11] & 0xffff;
                out[24] = in[12] >> 16;
                out[25] = in[12] & 0xffff;
                out[26] = in[13] >> 16;
                out[27] = in[13] & 0xffff;
                out[28] = in[14] >> 16;
                out[29] = in[14] & 0xffff;
                out[30] = in[15] >> 16;
                out[31] = in[15] & 0xffff;
        }
}

void
__optp4deltav2_unpack20(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 32, out += 32, in += 20) {
                out[0] = in[0] >> 12;
                out[1] = (in[0] << 8) & 0x0fffff;
                out[1] |= in[1] >> 24;
                out[2] = (in[1] >> 4) & 0x0fffff;
                out[3] = (in[1] << 16) & 0x0fffff;
                out[3] |= in[2] >> 16;
                out[4] = (in[2] << 4) & 0x0fffff;
                out[4] |= in[3] >> 28;
                out[5] = (in[3] >> 8) & 0x0fffff;
                out[6] = (in[3] << 12) & 0x0fffff;
                out[6] |= in[4] >> 20;
                out[7] = in[4] & 0x0fffff;
                out[8] = in[5] >> 12;
                out[9] = (in[5] << 8) & 0x0fffff;
                out[9] |= in[6] >> 24;
                out[10] = (in[6] >> 4) & 0x0fffff;
                out[11] = (in[6] << 16) & 0x0fffff;
                out[11] |= in[7] >> 16;
                out[12] = (in[7] << 4) & 0x0fffff;
                out[12] |= in[8] >> 28;
                out[13] = (in[8] >> 8) & 0x0fffff;
                out[14] = (in[8] << 12) & 0x0fffff;
                out[14] |= in[9] >> 20;
                out[15] = in[9] & 0x0fffff;
                out[16] = in[10] >> 12;
                out[17] = (in[10] << 8) & 0x0fffff;
                out[17] |= in[11] >> 24;
                out[18] = (in[11] >> 4) & 0x0fffff;
                out[19] = (in[11] << 16) & 0x0fffff;
                out[19] |= in[12] >> 16;
                out[20] = (in[12] << 4) & 0x0fffff;
                out[20] |= in[13] >> 28;
                out[21] = (in[13] >> 8) & 0x0fffff;
                out[22] = (in[13] << 12) & 0x0fffff;
                out[22] |= in[14] >> 20;
                out[23] = in[14] & 0x0fffff;
                out[24] = in[15] >> 12;
                out[25] = (in[15] << 8) & 0x0fffff;
                out[25] |= in[16] >> 24;
                out[26] = (in[16] >> 4) & 0x0fffff;
                out[27] = (in[16] << 16) & 0x0fffff;
                out[27] |= in[17] >> 16;
                out[28] = (in[17] << 4) & 0x0fffff;
                out[28] |= in[18] >> 28;
                out[29] = (in[18] >> 8) & 0x0fffff;
                out[30] = (in[18] << 12) & 0x0fffff;
                out[30] |= in[19] >> 20;
                out[31] = in[19] & 0x0fffff;
        }
}

void
__optp4deltav2_unpack32(uint32_t *out, uint32_t *in)
{
        uint32_t        i;

        for (i = 0; i < (OPTPFORDELTAV2_NBLOCK << 5);
                        i += 16, out += 16, in += 16) {
                __optp4deltav2_copy(in, out);
        }
}

