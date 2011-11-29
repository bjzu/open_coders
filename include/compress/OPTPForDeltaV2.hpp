/*-----------------------------------------------------------------------------
 *  OPTPForDeltaV2.hpp - A encoder/decoder for optimized PForDelta.
 *      This implementation given by Shuai Ding, who is of original authors
 *      proposing OPTPForDelta in http://dl.acm.org/citation.cfm?id=1526764.
 *
 *  Coding-Style:
 *      emacs) Mode: C, tab-width: 8, c-basic-offset: 8, indent-tabs-mode: nil
 *      vi) tabstop: 8, expandtab
 *
 *  A Author:
 *      Hao Yan, Shuai Ding, and Torsten Suel
 *-----------------------------------------------------------------------------
 */

#ifndef OPTPFORDELTAV2_HPP
#define OPTPFORDELTAV2_HPP

#include "open_coders.hpp"
#include "compress/Simple16.hpp"
#include "io/BitsWriter.hpp"

class OPTPForDeltaV2 {
        private:
                static void encodeBlock(uint32_t *in,
                                uint32_t len, uint32_t b,
                                uint32_t *out, uint32_t &nvalue);

        public:
                static void encodeArray(uint32_t *in, uint32_t len,
                                uint32_t *out, uint32_t &nvalue);
                static void decodeArray(uint32_t *in, uint32_t len,
                                uint32_t *out, uint32_t nvalue);
};

#endif /* OPTPFORDELTAV2_HPP */
