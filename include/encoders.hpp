/*-----------------------------------------------------------------------------
 *  encoders.hpp - A header for a variety of encoders.
 *
 *  Coding-Style:
 *      emacs) Mode: C, tab-width: 8, c-basic-offset: 8, indent-tabs-mode: nil
 *      vi) tabstop: 8, expandtab
 *
 *  Authors:
 *      Takeshi Yamamuro <linguin.m.s_at_gmail.com>
 *      Fabrizio Silvestri <fabrizio.silvestri_at_isti.cnr.it>
 *      Rossano Venturini <rossano.venturini_at_isti.cnr.it>
 *-----------------------------------------------------------------------------
 */

#ifndef ENCODERS_HPP
#define ENCODERS_HPP

#include "open_coders.hpp"

/* Header files for a variety of compressions */
#include "compress/Gamma.hpp"
#include "compress/Delta.hpp"
#include "compress/VariableByte.hpp"
#include "compress/BinaryInterpolative.hpp"
#include "compress/Simple9.hpp"
#include "compress/Simple16.hpp"
#include "compress/PForDelta.hpp"
#include "compress/OPTPForDelta.hpp"
#include "compress/VSEncodingBlocks.hpp"
#include "compress/VSE-R.hpp"
#include "compress/VSEncodingRest.hpp"
#include "compress/VSEncodingBlocksHybrid.hpp"
#include "compress/VSEncodingSimpleV1.hpp"
#include "compress/VSEncodingSimpleV2.hpp"

#define NUMENCODERS     14

/* EncoderID */
#define E_GAMMA         0
#define E_DELTA         1
#define E_VARIABLEBYTE  2
#define E_BINARYIPL     3
#define E_SIMPLE9       4
#define E_SIMPLE16      5
#define E_P4D           6
#define E_OPTP4D        7
#define E_VSEBLOCKS     8
#define E_VSER          9
#define E_VSEREST       10
#define E_VSEHYB        11
#define E_VSESIMPLEV1   12
#define E_VSESIMPLEV2   13

typedef void (*pt2Enc)(uint32_t *, uint32_t, uint32_t *, uint32_t &);

pt2Enc encoders[NUMENCODERS] = {
        Gamma::encodeArray,
        Delta::encodeArray,
        VariableByte::encodeArray,
        BinaryInterpolative::encodeArray,
        Simple9::encodeArray,
        Simple16::encodeArray,
        PForDelta::encodeArray,
        OPTPForDelta::encodeArray,
        VSEncodingBlocks::encodeArray,
        VSE_R::encodeArray,
        VSEncodingRest::encodeArray,
        VSEncodingBlocksHybrid::encodeArray,
        VSEncodingSimpleV1::encodeArray,
        VSEncodingSimpleV2::encodeArray
};	

/* Extensions for these coresspinding indices */
const char *enc_ext[] = {
        ".Gamma",
        ".Delta",
        ".VariableByte",
        ".Interpolative",
        ".Simple9",
        ".Simple16",
        ".P4D",
        ".OP4D",
        ".VSE",
        ".VSERT",
        ".VSERest",
        ".VSEH",
        ".VSESimpleV1",
        ".VSESimpleV2"
};

#endif /* ENCODERS_HPP */
