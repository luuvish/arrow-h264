/*!
 ***********************************************************************
 * \file read_comp_cavlc.c
 *
 * \brief
 *     Read Coefficient Components (CAVLC version)
 *
 * \author
 *    Main contributors (see contributors.h for copyright, address and affiliation details)
 *    - Alexis Michael Tourapis         <alexismt@ieee.org>
 ***********************************************************************
*/

#include "global.h"
#include "slice.h"
#include "bitstream_elements.h"
#include "bitstream.h"
#include "macroblock.h"
#include "mb_read.h"
#include "transform.h"
#include "neighbour.h"

#include "mb_read_syntax.h"


#define IS_I16MB(MB)    ((MB)->mb_type == I16MB || (MB)->mb_type == IPCM)
#define IS_DIRECT(MB)   ((MB)->mb_type == 0 && (currSlice->slice_type == B_SLICE))

#define TOTRUN_NUM       15
#define RUNBEFORE_NUM_M1  6


static int readSyntaxElement_Level_VLCN(Bitstream *currStream, int suffixLength)
{
    int leadingZeroBits = -1;
    for (int b = 0; !b; leadingZeroBits++)
        b = currStream->read_bits(1);
    int level_prefix = leadingZeroBits;

//    int level, sign;
//
//    if (suffixLength == 0) {
//        if (level_prefix < 14) {
//            sign  = level_prefix & 1;
//            level = (level_prefix >> 1) + 1;
//        } else if (level_prefix == 14) {
//            // escape
//            int level_suffix = currStream->u(4);
//            sign  = (level_suffix & 1);
//            level = (level_suffix >> 1) + 8;
//        } else {
//            // escape
//            int level_suffix = currStream->u(level_prefix - 3);
//            sign  = (level_suffix & 1);
//            level = (level_suffix >> 1) + (1 << (level_prefix - 4)) - 2047 + 15;
//        }
//    } else {
//        if (level_prefix < 15) {
//            int level_suffix = currStream->u(suffixLength);
//            sign  = (level_suffix & 1);
//            level = (level_suffix >> 1) + (level_prefix << (suffixLength - 1)) + 1;
//        } else { // escape
//            int level_suffix = currStream->u(level_prefix - 3);
//            sign  = (level_suffix & 1);
//            level = (level_suffix >> 1) + (1 << (level_prefix - 4)) - 2047 + (15 << (suffixLength - 1));
//        }
//    }
//
//    return sign ? -level : level;

    int levelSuffixSize = 0;
    if (level_prefix == 14 && suffixLength == 0)
        levelSuffixSize = 4;
    else if (level_prefix >= 15)
        levelSuffixSize = level_prefix - 3;
    else
        levelSuffixSize = suffixLength;

    int level_suffix = 0;
    if (levelSuffixSize > 0)
        level_suffix = currStream->u(levelSuffixSize);

    int levelCode = (imin(15, level_prefix) << suffixLength) + level_suffix;
    if (level_prefix >= 15 && suffixLength == 0)
        levelCode += 15;
    if (level_prefix >= 16)
        levelCode += (1 << (level_prefix - 3)) - 4096;

//    if ((levelCode % 2) == 0)
//        levelVal[i] = (levelCode + 2) >> 1;
//    else
//        levelVal[i] = (-levelCode - 1) >> 1;

    return (levelCode % 2 == 0) ? (levelCode + 2) >> 1 : (-levelCode - 1) >> 1;
}


static int readSyntaxElement_NumCoeffTrailingOnes(Bitstream *currStream, int *coeff, int *ones, int nC)
{
    static const byte lentab[5][4][17] = {
        // 0 <= nC < 2
        {{ 1, 6, 8, 9,10,11,13,13,13,14,14,15,15,16,16,16,16},
         { 0, 2, 6, 8, 9,10,11,13,13,14,14,15,15,15,16,16,16},
         { 0, 0, 3, 7, 8, 9,10,11,13,13,14,14,15,15,16,16,16},
         { 0, 0, 0, 5, 6, 7, 8, 9,10,11,13,14,14,15,15,16,16}},
        // 2 <= nC < 4
        {{ 2, 6, 6, 7, 8, 8, 9,11,11,12,12,12,13,13,13,14,14},
         { 0, 2, 5, 6, 6, 7, 8, 9,11,11,12,12,13,13,14,14,14},
         { 0, 0, 3, 6, 6, 7, 8, 9,11,11,12,12,13,13,13,14,14},
         { 0, 0, 0, 4, 4, 5, 6, 6, 7, 9,11,11,12,13,13,13,14}},
        // 4 <= nC < 8
        {{ 4, 6, 6, 6, 7, 7, 7, 7, 8, 8, 9, 9, 9,10,10,10,10},
         { 0, 4, 5, 5, 5, 5, 6, 6, 7, 8, 8, 9, 9, 9,10,10,10},
         { 0, 0, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9,10,10,10},
         { 0, 0, 0, 4, 4, 4, 4, 4, 5, 6, 7, 8, 8, 9,10,10,10}},
        // nC == -1
        {{ 2, 6, 6, 6, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         { 0, 1, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         { 0, 0, 3, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         { 0, 0, 0, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        // nC == -2
        {{ 1, 7, 7, 9, 9,10,11,12,13, 0, 0, 0, 0, 0, 0, 0, 0},
         { 0, 2, 7, 7, 9,10,11,12,12, 0, 0, 0, 0, 0, 0, 0, 0},
         { 0, 0, 3, 7, 7, 9,10,11,12, 0, 0, 0, 0, 0, 0, 0, 0},
         { 0, 0, 0, 5, 6, 7, 7,10,11, 0, 0, 0, 0, 0, 0, 0, 0}},
    };

    static const byte codtab[5][4][17] = {
        // 0 <= nC < 2
        {{ 1, 5, 7, 7, 7, 7,15,11, 8,15,11,15,11,15,11, 7, 4},
         { 0, 1, 4, 6, 6, 6, 6,14,10,14,10,14,10, 1,14,10, 6},
         { 0, 0, 1, 5, 5, 5, 5, 5,13, 9,13, 9,13, 9,13, 9, 5},
         { 0, 0, 0, 3, 3, 4, 4, 4, 4, 4,12,12, 8,12, 8,12, 8}},
        // 2 <= nC < 4
        {{ 3,11, 7, 7, 7, 4, 7,15,11,15,11, 8,15,11, 7, 9, 7},
         { 0, 2, 7,10, 6, 6, 6, 6,14,10,14,10,14,10,11, 8, 6},
         { 0, 0, 3, 9, 5, 5, 5, 5,13, 9,13, 9,13, 9, 6,10, 5},
         { 0, 0, 0, 5, 4, 6, 8, 4, 4, 4,12, 8,12,12, 8, 1, 4}},
        // 4 <= nC < 8
        {{15,15,11, 8,15,11, 9, 8,15,11,15,11, 8,13, 9, 5, 1},
         { 0,14,15,12,10, 8,14,10,14,14,10,14,10, 7,12, 8, 4},
         { 0, 0,13,14,11, 9,13, 9,13,10,13, 9,13, 9,11, 7, 3},
         { 0, 0, 0,12,11,10, 9, 8,13,12,12,12, 8,12,10, 6, 2}},
        // nC == -1
        {{ 1, 7, 4, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         { 0, 1, 6, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         { 0, 0, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         { 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        // nC == -2
        {{ 1,15,14, 7, 6, 7, 7, 7, 7, 0, 0, 0, 0, 0, 0, 0, 0},
         { 0, 1,13,12, 5, 6, 6, 6, 5, 0, 0, 0, 0, 0, 0, 0, 0},
         { 0, 0, 1,11,10, 4, 5, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0},
         { 0, 0, 0, 1, 1, 9, 8, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0}},
    };

    if (nC >= 8) {
        int code = currStream->read_bits(6);
        *coeff = (code >> 2);
        *ones  = (code & 3);
        if (*coeff == 0 && *ones == 3)
            // #c = 0, #t1 = 3 =>  #c = 0
            *ones = 0;
        else
            (*coeff)++;
        return 0;
    }

    int tab = (nC == -2) ? 4 : (nC == -1) ? 3 : (nC < 2) ? 0 : (nC < 4) ? 1 : (nC < 8) ? 2 : 5;
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 17; i++) {
            int len = lentab[tab][j][i];
            int cod = codtab[tab][j][i];
            if (len > 0 && currStream->next_bits(len) == cod) {
                *coeff = i;
                *ones  = j;
                currStream->read_bits(len);
                return 0;
            }
        }
    }

    printf("ERROR: failed to find NumCoeff/TrailingOnes\n");
    exit(-1);
    return -1;
}

static int readSyntaxElement_TotalZeros(Bitstream *currStream, int *coeff, int tab)
{
    static const byte lentab[TOTRUN_NUM][16] = {
        { 1,3,3,4,4,5,5,6,6,7,7,8,8,9,9,9},
        { 3,3,3,3,3,4,4,4,4,5,5,6,6,6,6},
        { 4,3,3,3,4,4,3,3,4,5,5,6,5,6},
        { 5,3,4,4,3,3,3,4,3,4,5,5,5},
        { 4,4,4,3,3,3,3,3,4,5,4,5},
        { 6,5,3,3,3,3,3,3,4,3,6},
        { 6,5,3,3,3,2,3,4,3,6},
        { 6,4,5,3,2,2,3,3,6},
        { 6,6,4,2,2,3,2,5},
        { 5,5,3,2,2,2,4},
        { 4,4,3,3,1,3},
        { 4,4,2,1,3},
        { 3,3,1,2},
        { 2,2,1},
        { 1,1},
    };

    static const byte codtab[TOTRUN_NUM][16] = {
        {1,3,2,3,2,3,2,3,2,3,2,3,2,3,2,1},
        {7,6,5,4,3,5,4,3,2,3,2,3,2,1,0},
        {5,7,6,5,4,3,4,3,2,3,2,1,1,0},
        {3,7,5,4,6,5,4,3,3,2,2,1,0},
        {5,4,3,7,6,5,4,3,2,1,1,0},
        {1,1,7,6,5,4,3,2,1,1,0},
        {1,1,5,4,3,3,2,1,1,0},
        {1,1,1,3,3,2,2,1,0},
        {1,0,1,3,2,1,1,1,},
        {1,0,1,3,2,1,1,},
        {0,1,1,2,1,3},
        {0,1,1,1,1},
        {0,1,1,1},
        {0,1,1},
        {0,1},
    };

    for (int i = 0; i < 16; i++) {
        int len = lentab[tab][i];
        int cod = codtab[tab][i];
        if (len > 0 && currStream->next_bits(len) == cod) {
            *coeff = i;
            currStream->read_bits(len);
            return 0;
        }
    }

    printf("ERROR: failed to find Total Zeros !cdc\n");
    exit(-1);
    return -1;
}

static int readSyntaxElement_TotalZerosChromaDC(Bitstream *currStream, int *coeff, int yuv, int tab)
{
    static const byte lentab[3][TOTRUN_NUM][16] = {
        //YUV420
        {{ 1,2,3,3},
         { 1,2,2},
         { 1,1}},
        //YUV422
        {{ 1,3,3,4,4,4,5,5},
         { 3,2,3,3,3,3,3},
         { 3,3,2,2,3,3},
         { 3,2,2,2,3},
         { 2,2,2,2},
         { 2,2,1},
         { 1,1}},
        //YUV444
        {{ 1,3,3,4,4,5,5,6,6,7,7,8,8,9,9,9},
         { 3,3,3,3,3,4,4,4,4,5,5,6,6,6,6},
         { 4,3,3,3,4,4,3,3,4,5,5,6,5,6},
         { 5,3,4,4,3,3,3,4,3,4,5,5,5},
         { 4,4,4,3,3,3,3,3,4,5,4,5},
         { 6,5,3,3,3,3,3,3,4,3,6},
         { 6,5,3,3,3,2,3,4,3,6},
         { 6,4,5,3,2,2,3,3,6},
         { 6,6,4,2,2,3,2,5},
         { 5,5,3,2,2,2,4},
         { 4,4,3,3,1,3},
         { 4,4,2,1,3},
         { 3,3,1,2},
         { 2,2,1},
         { 1,1}}
    };

    static const byte codtab[3][TOTRUN_NUM][16] = {
        //YUV420
        {{ 1,1,1,0},
         { 1,1,0},
         { 1,0}},
        //YUV422
        {{ 1,2,3,2,3,1,1,0},
         { 0,1,1,4,5,6,7},
         { 0,1,1,2,6,7},
         { 6,0,1,2,7},
         { 0,1,2,3},
         { 0,1,1},
         { 0,1}},
        //YUV444
        {{1,3,2,3,2,3,2,3,2,3,2,3,2,3,2,1},
         {7,6,5,4,3,5,4,3,2,3,2,3,2,1,0},
         {5,7,6,5,4,3,4,3,2,3,2,1,1,0},
         {3,7,5,4,6,5,4,3,3,2,2,1,0},
         {5,4,3,7,6,5,4,3,2,1,1,0},
         {1,1,7,6,5,4,3,2,1,1,0},
         {1,1,5,4,3,3,2,1,1,0},
         {1,1,1,3,3,2,2,1,0},
         {1,0,1,3,2,1,1,1,},
         {1,0,1,3,2,1,1,},
         {0,1,1,2,1,3},
         {0,1,1,1,1},
         {0,1,1,1},
         {0,1,1},
         {0,1}}
    };

    for (int i = 0; i < 16; i++) {
        int len = lentab[yuv][tab][i];
        int cod = codtab[yuv][tab][i];
        if (len > 0 && currStream->next_bits(len) == cod) {
            *coeff = i;
            currStream->read_bits(len);
            return 0;
        }
    }

    printf("ERROR: failed to find Total Zeros\n");
    exit(-1);
    return -1;
}

static int readSyntaxElement_Run(Bitstream *currStream, int *coeff, int tab)
{
    static const byte lentab[TOTRUN_NUM][16] = {
        {1,1},
        {1,2,2},
        {2,2,2,2},
        {2,2,2,3,3},
        {2,2,3,3,3,3},
        {2,3,3,3,3,3,3},
        {3,3,3,3,3,3,3,4,5,6,7,8,9,10,11},
    };

    static const byte codtab[TOTRUN_NUM][16] = {
        {1,0},
        {1,1,0},
        {3,2,1,0},
        {3,2,1,1,0},
        {3,2,3,2,1,0},
        {3,0,1,3,2,5,4},
        {7,6,5,4,3,2,1,1,1,1,1,1,1,1,1},
    };

    for (int i = 0; i < 16; i++) {
        int len = lentab[tab][i];
        int cod = codtab[tab][i];
        if (len > 0 && currStream->next_bits(len) == cod) {
            *coeff = i;
            currStream->read_bits(len);
            return 0;
        }
    }

    printf("ERROR: failed to find Run\n");
    exit(-1);
    return -1;
}


static void read_coeff_4x4_CAVLC(mb_t *currMB, int block_type, int i, int j,
                                 int levelVal[16], int runVal[16], int *number_coefficients)
{
    slice_t *currSlice = currMB->p_Slice;
    sps_t *sps = currSlice->active_sps;
    VideoParameters *p_Vid = currMB->p_Vid;
    int mb_nr = currMB->mbAddrX;

    int cdc=0, cac=0;
    int dptype = 0;
    int max_coeff_num = 0;

    int num_cdc_coeff;
    if (sps->chroma_format_idc != YUV400)
        num_cdc_coeff = (((1 << sps->chroma_format_idc) & (~0x1)) << 1);
    else
        num_cdc_coeff = 0;

    int pl;
    switch (block_type) {
    case LUMA:
    case CB:
    case CR:
        max_coeff_num = 16;
        dptype = currMB->is_intra_block ? SE_LUM_AC_INTRA : SE_LUM_AC_INTER;
        pl = block_type == LUMA ? 0 : block_type == CB ? 1 : 2;
        p_Vid->nz_coeff[mb_nr][pl][j][i] = 0;
        break;
    case LUMA_INTRA16x16DC:
    case CB_INTRA16x16DC:
    case CR_INTRA16x16DC:
        max_coeff_num = 16;
        dptype = SE_LUM_DC_INTRA;
        pl = block_type == LUMA_INTRA16x16DC ? 0 : block_type == CB_INTRA16x16DC ? 1 : 2;
        p_Vid->nz_coeff[mb_nr][pl][j][i] = 0; 
        break;
    case LUMA_INTRA16x16AC:
    case CB_INTRA16x16AC:
    case CR_INTRA16x16AC:
        max_coeff_num = 15;
        dptype = SE_LUM_AC_INTRA;
        pl = block_type == LUMA_INTRA16x16AC ? 0 : block_type == CB_INTRA16x16AC ? 1 : 2;
        p_Vid->nz_coeff[mb_nr][pl][j][i] = 0; 
        break;
    case CHROMA_DC:
        max_coeff_num = num_cdc_coeff;
        cdc = 1;
        dptype = currMB->is_intra_block ? SE_CHR_DC_INTRA : SE_CHR_DC_INTER;
        p_Vid->nz_coeff[mb_nr][0][j][i] = 0; 
        break;
    case CHROMA_AC:
        max_coeff_num = 15;
        cac = 1;
        dptype = currMB->is_intra_block ? SE_CHR_AC_INTRA : SE_CHR_AC_INTER;
        p_Vid->nz_coeff[mb_nr][0][j][i] = 0; 
        break;
    default:
        error ("read_coeff_4x4_CAVLC: invalid block type", 600);
        p_Vid->nz_coeff[mb_nr][0][j][i] = 0; 
        break;
    }

    DataPartition *dP = &currSlice->partArr[assignSE2partition[currSlice->dp_mode][dptype]];
    Bitstream *currStream = dP->bitstream;

    int nC = 0;
    if (!cdc) {
        // luma or chroma AC    
        if (block_type == LUMA ||
            block_type == LUMA_INTRA16x16DC || block_type == LUMA_INTRA16x16AC ||
            block_type == CHROMA_AC) {
            nC = (!cac) ? predict_nnz(currMB, LUMA, i<<2, j<<2) :
                           predict_nnz_chroma(currMB, i, ((j-4)<<2));
        } else if (block_type == CB || block_type == CB_INTRA16x16DC || block_type == CB_INTRA16x16AC)
            nC = predict_nnz(currMB, CB, i<<2, j<<2);
        else
            nC = predict_nnz(currMB, CR, i<<2, j<<2);
    } else {
        nC = sps->ChromaArrayType == 1 ? -1 : sps->ChromaArrayType == 2 ? -2 : 0;
    }

    int TotalCoeff = 0;
    int TrailingOnes;
    readSyntaxElement_NumCoeffTrailingOnes(currStream, &TotalCoeff, &TrailingOnes, nC);

    if (!cdc) {
        if (block_type == LUMA ||
            block_type == LUMA_INTRA16x16DC || block_type == LUMA_INTRA16x16AC ||
            block_type == CHROMA_AC)
            p_Vid->nz_coeff[mb_nr][0][j][i] = (byte) TotalCoeff;
        else if (block_type == CB || block_type == CB_INTRA16x16DC || block_type == CB_INTRA16x16AC)
            p_Vid->nz_coeff[mb_nr][1][j][i] = (byte) TotalCoeff;
        else
            p_Vid->nz_coeff[mb_nr][2][j][i] = (byte) TotalCoeff;
    }

    memset(levelVal, 0, max_coeff_num * sizeof(int));
    memset(runVal, 0, max_coeff_num * sizeof(int));

    *number_coefficients = TotalCoeff;

    if (TotalCoeff > 0) {
        int suffixLength = (TotalCoeff > 10 && TrailingOnes < 3 ? 1 : 0);

        if (TrailingOnes) {
            int code = currStream->f(TrailingOnes);
            int ntr = TrailingOnes;
            for (int i = TotalCoeff - 1; i > TotalCoeff - 1 - TrailingOnes; i--) {
                int trailing_ones_sign_flag = (code >> (--ntr)) & 1;
                levelVal[i] = 1 - 2 * trailing_ones_sign_flag;
            }
        }

        for (int i = TotalCoeff - 1 - TrailingOnes; i >= 0; i--) {
            levelVal[i] = readSyntaxElement_Level_VLCN(currStream, suffixLength);

            if (i == TotalCoeff - 1 - TrailingOnes && TrailingOnes < 3)
                levelVal[i] += (levelVal[i] > 0 ? 1 : -1);

            if (suffixLength == 0)
                suffixLength = 1;
            if (iabs(levelVal[i]) > (3 << (suffixLength - 1)) && suffixLength < 6)
                suffixLength++;
        }

        int zerosLeft;
        if (TotalCoeff < max_coeff_num) {
            int yuv = p_Vid->active_sps->chroma_format_idc - 1;
            if (cdc)
                readSyntaxElement_TotalZerosChromaDC(currStream, &zerosLeft, yuv, TotalCoeff - 1);
            else
                readSyntaxElement_TotalZeros(currStream, &zerosLeft, TotalCoeff - 1);
        } else
            zerosLeft = 0;

        for (i = TotalCoeff - 1; i > 0; i--) {
//        for (i = 0; i < TotalCoeff - 1; i++) {
            if (zerosLeft > 0) {
                int run_before;
                readSyntaxElement_Run(currStream, &run_before, imin(zerosLeft - 1, RUNBEFORE_NUM_M1));
                runVal[i] = run_before;
            } else
                runVal[i] = 0;
            zerosLeft -= runVal[i];
        }
        runVal[i] = zerosLeft;
//        runVal[TotalCoeff - 1] = zerosLeft;
    }
}


static void read_tc_luma(mb_t *currMB, ColorPlane pl)
{
    slice_t *currSlice = currMB->p_Slice;
    sps_t *sps = currSlice->active_sps;

    const byte (*pos_scan4x4)[2] = !currSlice->field_pic_flag && !currMB->mb_field_decoding_flag ? SNGL_SCAN : FIELD_SCAN;
    const byte (*pos_scan8x8)[2] = !currSlice->field_pic_flag && !currMB->mb_field_decoding_flag ? SNGL_SCAN8x8 : FIELD_SCAN8x8;

    if (IS_I16MB(currMB) && !currMB->dpl_flag) {
        int16_t i16x16DClevel[16];
        currMB->residual_block_cavlc(i16x16DClevel, 0, 15, 16, pl, 0, 0);
    }

    int qp_per = currMB->qp_scaled[pl] / 6;
    int qp_rem = currMB->qp_scaled[pl] % 6;
    int transform_pl = sps->separate_colour_plane_flag ? currSlice->colour_plane_id : pl;
    int (*InvLevelScale4x4)[4] = currMB->is_intra_block ?
        currSlice->InvLevelScale4x4_Intra[transform_pl][qp_rem] :
        currSlice->InvLevelScale4x4_Inter[transform_pl][qp_rem];
    int (*InvLevelScale8x8)[8] = currMB->is_intra_block ?
        currSlice->InvLevelScale8x8_Intra[transform_pl][qp_rem] :
        currSlice->InvLevelScale8x8_Inter[transform_pl][qp_rem];

    int start_scan = IS_I16MB(currMB) ? 1 : 0;
    byte **nzcoeff = currSlice->p_Vid->nz_coeff[currMB->mbAddrX][pl];

    int cur_context; 
    if (IS_I16MB(currMB)) {
        if (pl == PLANE_Y)
            cur_context = LUMA_INTRA16x16AC;
        else if (pl == PLANE_U)
            cur_context = CB_INTRA16x16AC;
        else
            cur_context = CR_INTRA16x16AC;
    } else {
        if (pl == PLANE_Y)
            cur_context = LUMA;
        else if (pl == PLANE_U)
            cur_context = CB;
        else
            cur_context = CR;
    }

    for (int i8x8 = 0; i8x8 < 4; i8x8++) {
        int block_x = (i8x8 % 2) * 2;
        int block_y = (i8x8 / 2) * 2;

        for (int i4x4 = 0; i4x4 < 4; i4x4++) {
            if (currMB->cbp & (1 << i8x8)) {
                int i = block_x + (i4x4 % 2);
                int j = block_y + (i4x4 / 2);

                int levarr[16] = {0}, runarr[16] = {0}, numcoeff;
                read_coeff_4x4_CAVLC(currMB, cur_context, i, j, levarr, runarr, &numcoeff);

                int coef_ctr = start_scan - 1;

                for (int k = 0; k < numcoeff; ++k) {
                //for (int k = numcoeff - 1; k >= 0; k--) {
                    if (levarr[k] != 0) {
                        coef_ctr += runarr[k] + 1;

                        if (!currMB->transform_size_8x8_flag) {
                            currMB->s_cbp[pl].blk |= (int64)(0x01 << (j * 4 + i));
                            int i0 = pos_scan4x4[coef_ctr][0];
                            int j0 = pos_scan4x4[coef_ctr][1];

                            if (!currMB->TransformBypassModeFlag)
                                currSlice->cof[pl][j * 4 + j0][i * 4 + i0] = rshift_rnd_sf((levarr[k] * InvLevelScale4x4[j0][i0]) << qp_per, 4);
                            else
                                currSlice->cof[pl][j * 4 + j0][i * 4 + i0] = levarr[k];
                        } else {
                            currMB->s_cbp[pl].blk |= (int64)(0x33 << (block_y * 4 + block_x));
                            int i0 = pos_scan8x8[coef_ctr * 4 + i4x4][0];
                            int j0 = pos_scan8x8[coef_ctr * 4 + i4x4][1];

                            if (!currMB->TransformBypassModeFlag)
                                currSlice->cof[pl][block_y * 4 + j0][block_x * 4 + i0] = rshift_rnd_sf((levarr[k] * InvLevelScale8x8[j0][i0]) << qp_per, 6);
                            else
                                currSlice->cof[pl][block_y * 4 + j0][block_x * 4 + i0] = levarr[k];
                        }
                    }
                }
            }
        }
        if (!(currMB->cbp & (1 << i8x8))) {
            nzcoeff[block_y    ][block_x    ] = 0;
            nzcoeff[block_y    ][block_x + 1] = 0;
            nzcoeff[block_y + 1][block_x    ] = 0;
            nzcoeff[block_y + 1][block_x + 1] = 0;
        }
    }
}

static void read_tc_chroma_420(mb_t *currMB)
{
    VideoParameters *p_Vid = currMB->p_Vid;
    slice_t *currSlice = currMB->p_Slice;
    sps_t *sps = currSlice->active_sps;
    int mb_nr = currMB->mbAddrX;

    const byte (*pos_scan4x4)[2] = !currSlice->field_pic_flag && !currMB->mb_field_decoding_flag ? SNGL_SCAN : FIELD_SCAN;

    int qp_per_uv[2];
    int qp_rem_uv[2];
    for (int i = 0; i < 2; ++i) {
        qp_per_uv[i] = currMB->qp_scaled[i + 1] / 6;
        qp_rem_uv[i] = currMB->qp_scaled[i + 1] % 6;
    }

    if (currMB->cbp > 15) {
        for (int iCbCr = 0; iCbCr < 2; iCbCr++) {
            int (*InvLevelScale4x4)[4] = currMB->is_intra_block ?
                currSlice->InvLevelScale4x4_Intra[iCbCr + 1][qp_rem_uv[iCbCr]] :
                currSlice->InvLevelScale4x4_Inter[iCbCr + 1][qp_rem_uv[iCbCr]];

            int levelVal[16], runVal[16], numcoeff;
            read_coeff_4x4_CAVLC(currMB, CHROMA_DC, 0, 0, levelVal, runVal, &numcoeff);

            int cofu[4] = { 0 };

            int coeffNum = -1;
            for (int k = 0; k < numcoeff; ++k) {
            //for (int k = numcoeff - 1; k >= 0; k--) {
                if (levelVal[k] != 0) {
                    currMB->s_cbp[0].blk |= (int64)(0xf << (iCbCr * 4 + 16));
                    coeffNum += runVal[k] + 1;
                    cofu[coeffNum] = levelVal[k];
                }
            }

            int smb = (currSlice->slice_type == SP_SLICE && !currMB->is_intra_block) ||
                      (currSlice->slice_type == SI_SLICE && currMB->mb_type == SI4MB);
            if (smb || currMB->TransformBypassModeFlag) {
                currSlice->cof[iCbCr + 1][0][0] = cofu[0];
                currSlice->cof[iCbCr + 1][0][4] = cofu[1];
                currSlice->cof[iCbCr + 1][4][0] = cofu[2];
                currSlice->cof[iCbCr + 1][4][4] = cofu[3];
            } else {
                int temp[4];
                ihadamard2x2(cofu, temp);

                currSlice->cof[iCbCr + 1][0][0] = ((temp[0] * InvLevelScale4x4[0][0]) << qp_per_uv[iCbCr]) >> 5;
                currSlice->cof[iCbCr + 1][0][4] = ((temp[1] * InvLevelScale4x4[0][0]) << qp_per_uv[iCbCr]) >> 5;
                currSlice->cof[iCbCr + 1][4][0] = ((temp[2] * InvLevelScale4x4[0][0]) << qp_per_uv[iCbCr]) >> 5;
                currSlice->cof[iCbCr + 1][4][4] = ((temp[3] * InvLevelScale4x4[0][0]) << qp_per_uv[iCbCr]) >> 5;
            }
        }
    }

    if (currMB->cbp > 31) {
        int NumC8x8 = 4 / (sps->SubWidthC * sps->SubHeightC);
        for (int iCbCr = 0; iCbCr < 2; iCbCr++) {
            for (int i8x8 = 0; i8x8 < NumC8x8; i8x8++) {
                currMB->is_v_block = iCbCr;

                int (*InvLevelScale4x4)[4] = NULL;
                if (!currMB->TransformBypassModeFlag)
                    InvLevelScale4x4 = currMB->is_intra_block ?
                        currSlice->InvLevelScale4x4_Intra[iCbCr + 1][qp_rem_uv[iCbCr]] :
                        currSlice->InvLevelScale4x4_Inter[iCbCr + 1][qp_rem_uv[iCbCr]];

                for (int i4x4 = 0; i4x4 < 4; i4x4++) {
                    int i = (i8x8 % 2) * 2 + (i4x4 % 2);
                    int j = (i8x8 / 2) * 2 + (i4x4 / 2);

                    int levarr[16], runarr[16], numcoeff;
                    read_coeff_4x4_CAVLC(currMB, CHROMA_AC, iCbCr * 2 + i, j + 4, levarr, runarr, &numcoeff);

                    int coef_ctr = 0;
                    //for (int k = numcoeff - 1; k >= 0; k--) {
                    for (int k = 0; k < numcoeff; ++k) {
                        if (levarr[k] != 0) {
                            currMB->s_cbp[0].blk |= (int64)(0x1 << (i8x8 * 4 + i4x4 + 16));
                            coef_ctr += runarr[k] + 1;
                            int i0 = pos_scan4x4[coef_ctr][0];
                            int j0 = pos_scan4x4[coef_ctr][1];

                            if (!currMB->TransformBypassModeFlag)
                                currSlice->cof[iCbCr + 1][j * 4 + j0][i * 4 + i0] = rshift_rnd_sf((levarr[k] * InvLevelScale4x4[j0][i0]) << qp_per_uv[iCbCr], 4);
                            else
                                currSlice->cof[iCbCr + 1][j * 4 + j0][i * 4 + i0] = levarr[k];
                        }
                    }
                }
            }
        }
    } else
        memset(p_Vid->nz_coeff[mb_nr][1][0], 0, 2 * BLOCK_PIXELS * sizeof(byte));
}

static void read_tc_chroma_422(mb_t *currMB)
{
    VideoParameters *p_Vid = currMB->p_Vid;
    slice_t *currSlice = currMB->p_Slice;
    sps_t *sps = currSlice->active_sps;
    int mb_nr = currMB->mbAddrX;

    const byte (*pos_scan4x4)[2] = !currSlice->field_pic_flag && !currMB->mb_field_decoding_flag ? SNGL_SCAN : FIELD_SCAN;

    int qp_per_uv[2];
    int qp_rem_uv[2];
    for (int i = 0; i < 2; ++i) {
        qp_per_uv[i] = currMB->qp_scaled[i + 1] / 6;
        qp_rem_uv[i] = currMB->qp_scaled[i + 1] % 6;
    }

    if (currMB->cbp > 15) {
        for (int iCbCr = 0; iCbCr < 2; iCbCr++) {
            int qp_per_uv_dc = (currMB->qpc[iCbCr] + 3 + sps->QpBdOffsetC) / 6;       //for YUV422 only
            int qp_rem_uv_dc = (currMB->qpc[iCbCr] + 3 + sps->QpBdOffsetC) % 6;       //for YUV422 only
            int (*InvLevelScale4x4)[4] = currMB->is_intra_block ?
                currSlice->InvLevelScale4x4_Intra[iCbCr + 1][qp_rem_uv_dc] :
                currSlice->InvLevelScale4x4_Inter[iCbCr + 1][qp_rem_uv_dc];

            int levelVal[16], runVal[16], numcoeff;
            read_coeff_4x4_CAVLC(currMB, CHROMA_DC, 0, 0, levelVal, runVal, &numcoeff);

            int m3[2][4] = { { 0 }, { 0 } };

            int coeffNum = -1;
            //for (int k = numcoeff - 1; k >= 0; k--) {
            for (int k = 0; k < numcoeff; ++k) {
                if (levelVal[k] != 0) {
                    currMB->s_cbp[0].blk |= (int64)(0xff << (iCbCr * 8 + 16));
                    coeffNum += runVal[k] + 1;
                    int i0 = SCAN_YUV422[coeffNum][0];
                    int j0 = SCAN_YUV422[coeffNum][1];
                    m3[i0][j0] = levelVal[k];
                }
            }

            if (!currMB->TransformBypassModeFlag) {
                int m4[2][4];

                m4[0][0] = m3[0][0] + m3[1][0];
                m4[0][1] = m3[0][1] + m3[1][1];
                m4[0][2] = m3[0][2] + m3[1][2];
                m4[0][3] = m3[0][3] + m3[1][3];

                m4[1][0] = m3[0][0] - m3[1][0];
                m4[1][1] = m3[0][1] - m3[1][1];
                m4[1][2] = m3[0][2] - m3[1][2];
                m4[1][3] = m3[0][3] - m3[1][3];

                int temp[2][4];
                for (int i = 0; i < 2; ++i) {
                    int m6[4];

                    m6[0] = m4[i][0] + m4[i][2];
                    m6[1] = m4[i][0] - m4[i][2];
                    m6[2] = m4[i][1] - m4[i][3];
                    m6[3] = m4[i][1] + m4[i][3];

                    temp[i][0] = m6[0] + m6[3];
                    temp[i][1] = m6[1] + m6[2];
                    temp[i][2] = m6[1] - m6[2];
                    temp[i][3] = m6[0] - m6[3];
                }

                for (int j = 0; j < sps->MbHeightC; j += BLOCK_SIZE) {
                    for (int i = 0; i < sps->MbWidthC; i += BLOCK_SIZE)
                        currSlice->cof[iCbCr + 1][j][i] = rshift_rnd_sf((temp[i / 4][j / 4] * InvLevelScale4x4[0][0]) << qp_per_uv_dc, 6);
                }
            } else {
                for (int j = 0; j < sps->MbHeightC; j += BLOCK_SIZE) {
                    for (int i = 0; i < sps->MbWidthC; i += BLOCK_SIZE)
                        currSlice->cof[iCbCr + 1][j][i] = m3[i / 4][j / 4];
                }
            }
        }
    }

    if (currMB->cbp > 31) {
        int NumC8x8 = 4 / (sps->SubWidthC * sps->SubHeightC);
        for (int iCbCr = 0; iCbCr < 2; iCbCr++) {
            for (int i8x8 = 0; i8x8 < NumC8x8; i8x8++) {
                currMB->is_v_block = iCbCr;

                int (*InvLevelScale4x4)[4] = NULL;
                if (!currMB->TransformBypassModeFlag)
                    InvLevelScale4x4 = currMB->is_intra_block ?
                        currSlice->InvLevelScale4x4_Intra[iCbCr + 1][qp_rem_uv[iCbCr]] :
                        currSlice->InvLevelScale4x4_Inter[iCbCr + 1][qp_rem_uv[iCbCr]];

                for (int i4x4 = 0; i4x4 < 4; i4x4++) {
                    int i = (i8x8 % 2) * 2 + (i4x4 % 2);
                    int j = (i8x8 / 2) * 2 + (i4x4 / 2);

                    int levarr[16], runarr[16], numcoeff;
                    read_coeff_4x4_CAVLC(currMB, CHROMA_AC, iCbCr * 2 + i, j + 4, levarr, runarr, &numcoeff);

                    int coef_ctr = 0;
                    //for (int k = numcoeff - 1; k >= 0; k--) {
                    for (int k = 0; k < numcoeff; ++k) {
                        if (levarr[k] != 0) {
                            currMB->s_cbp[0].blk |= (int64)(0x1 << (i8x8 * 4 + i4x4 + 16));
                            coef_ctr += runarr[k] + 1;
                            int i0 = pos_scan4x4[coef_ctr][0];
                            int j0 = pos_scan4x4[coef_ctr][1];

                            if (!currMB->TransformBypassModeFlag)
                                currSlice->cof[iCbCr + 1][j * 4 + j0][i * 4 + i0] = rshift_rnd_sf((levarr[k] * InvLevelScale4x4[j0][i0]) << qp_per_uv[iCbCr], 4);
                            else
                                currSlice->cof[iCbCr + 1][j * 4 + j0][i * 4 + i0] = levarr[k];
                        }
                    }
                }
            }
        }
    } else
        memset(p_Vid->nz_coeff[mb_nr][1][0], 0, 2 * BLOCK_PIXELS * sizeof(byte));
}


void macroblock_t::read_CBP_and_coeffs_from_NAL_CAVLC()
{
    slice_t *slice = this->p_Slice;
    sps_t *sps = slice->active_sps;

    read_tc_luma(this, PLANE_Y);

    if (sps->chroma_format_idc == YUV420)
        read_tc_chroma_420(this);
    if (sps->chroma_format_idc == YUV422)
        read_tc_chroma_422(this);
    if (sps->chroma_format_idc == YUV444 && !sps->separate_colour_plane_flag) {
        read_tc_luma(this, PLANE_U);
        read_tc_luma(this, PLANE_V);
    }
}

void macroblock_t::residual_block_cavlc(int16_t coeffLevel[16], uint8_t startIdx, uint8_t endIdx,
                                        uint8_t maxNumCoeff, ColorPlane pl, int bx, int by)
{
    slice_t *slice = this->p_Slice;
    sps_t *sps = slice->active_sps;

    const byte (*pos_scan4x4)[2] = !slice->field_pic_flag && !this->mb_field_decoding_flag ? SNGL_SCAN : FIELD_SCAN;

    int block_type = pl == PLANE_Y ? LUMA_INTRA16x16DC :
                     pl == PLANE_U ? CB_INTRA16x16DC : CR_INTRA16x16DC;
    int levelVal[16], runVal[16], numcoeff;
    read_coeff_4x4_CAVLC(this, block_type, bx, by, levelVal, runVal, &numcoeff);

    int coeffNum = -1;
//    for (int k = numcoeff - 1; k >= 0; k--) {
    for (int k = 0; k < numcoeff; ++k) {
        if (levelVal[k] != 0) {
            coeffNum += runVal[k] + 1;
            coeffLevel[startIdx + coeffNum] = levelVal[k];
            int i0 = pos_scan4x4[coeffNum][0];
            int j0 = pos_scan4x4[coeffNum][1];
            slice->cof[pl][j0 << 2][i0 << 2] = levelVal[k];
        }
    }

    if (!this->TransformBypassModeFlag) {
        int transform_pl = sps->separate_colour_plane_flag ? slice->colour_plane_id : pl;
        itrans_2(this, (ColorPlane)transform_pl);
    }
}
