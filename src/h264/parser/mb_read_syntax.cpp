
#include <math.h>

#include "global.h"
#include "slice.h"
#include "dpb.h"
#include "bitstream_cabac.h"
#include "bitstream.h"
#include "macroblock.h"
#include "mb_read.h"
#include "fmo.h"
#include "image.h"
#include "neighbour.h"
#include "transform.h"
#include "mv_prediction.h"
#include "intra_prediction.h"
#include "inter_prediction.h"

#include "mb_read_syntax.h"



static uint8_t readRefFrame_CABAC(mb_t *mb, uint8_t list)
{
    slice_t *slice = mb->p_Slice;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    uint8_t ref_idx = 0;

    int condTermFlagA = 0;
    int condTermFlagB = 0;
    int ctxIdxInc;

    PixelPos block_a, block_b;
    int mb_size[2] = { MB_BLOCK_SIZE, MB_BLOCK_SIZE };
    get4x4Neighbour(mb, mb->subblock_x - 1, mb->subblock_y    , mb_size, &block_a);
    get4x4Neighbour(mb, mb->subblock_x,     mb->subblock_y - 1, mb_size, &block_b);

#define IS_DIRECT(MB)   ((MB)->mb_type == 0 && (slice->slice_type == B_SLICE))
    if (block_a.available) {
        int b8a = ((block_a.x >> 1) & 0x01)+(block_a.y & 0x02);    
        mb_t *mb_a = &slice->mb_data[block_a.mb_addr];
        PicMotionParams *mv_info = &slice->dec_picture->mv_info[block_a.pos_y][block_a.pos_x];
        if (!(mb_a->mb_type == IPCM || IS_DIRECT(mb_a) || (mb_a->b8mode[b8a] == 0 && mb_a->b8pdir[b8a] == 2))) {
            if (slice->MbaffFrameFlag && !mb->mb_field_decoding_flag && mb_a->mb_field_decoding_flag)
                condTermFlagA = (mv_info->ref_idx[list] > 1 ? 1 : 0);
            else
                condTermFlagA = (mv_info->ref_idx[list] > 0 ? 1 : 0);
        }
    }
    if (block_b.available) {
        int b8b = ((block_b.x >> 1) & 0x01)+(block_b.y & 0x02);    
        mb_t *mb_b = &slice->mb_data[block_b.mb_addr];
        PicMotionParams *mv_info = &slice->dec_picture->mv_info[block_b.pos_y][block_b.pos_x];
        if (!(mb_b->mb_type == IPCM || IS_DIRECT(mb_b) || (mb_b->b8mode[b8b] == 0 && mb_b->b8pdir[b8b] == 2))) {
            if (slice->MbaffFrameFlag && !mb->mb_field_decoding_flag && mb_b->mb_field_decoding_flag)
                condTermFlagB = (mv_info->ref_idx[list] > 1 ? 2 : 0);
            else
                condTermFlagB = (mv_info->ref_idx[list] > 0 ? 2 : 0);
        }
    }
#undef IS_DIRECT

    ctxIdxInc = condTermFlagA + condTermFlagB;

    //int binIdx[] = { ctxIdxInc, 4, 5, 5, 5, 5, 5 };

    cabac_context_t *ctx = slice->mot_ctx->ref_no_contexts[0];
    if (dep_dp->decode_decision(ctx + ctxIdxInc)) {
        ctxIdxInc = 4;
        ref_idx = dep_dp->u(ctx + ctxIdxInc, 1) + 1;
    }

    return ref_idx;
}

static uint32_t exp_golomb_decode_eq_prob(cabac_engine_t *dep_dp, int k)
{
    uint32_t bins = 0;

    while (dep_dp->decode_bypass())
        bins += (1 << k++);
    while (k--)
        bins += (dep_dp->decode_bypass() << k);

    return bins;
}

static uint32_t unary_exp_golomb_mv_decode(cabac_engine_t *dep_dp, cabac_context_t* ctx, uint32_t max_bin)
{
    if (!dep_dp->decode_decision(ctx))
        return 0;

    uint32_t exp_start = 8;
    uint32_t l, k = 1;
    uint32_t bin = 1;
    uint32_t symbol = 0;

    ++ctx;
    do {
        l = dep_dp->decode_decision(ctx);
        if (++bin == 2) ctx++;
        if (bin == max_bin) ctx++;
        ++symbol;
        ++k;
    } while (l != 0 && k != exp_start);

    if (l != 0)
        symbol += exp_golomb_decode_eq_prob(dep_dp, 3) + 1;
    return symbol;
}

static int16_t read_mvd_CABAC(mb_t *mb, uint8_t xy, uint8_t list)
{
    slice_t *slice = mb->p_Slice;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    int16_t mvd = 0;

    PixelPos block_a, block_b;
    int mb_size[2] = { MB_BLOCK_SIZE, MB_BLOCK_SIZE };
    get4x4NeighbourBase(mb, mb->subblock_x - 1, mb->subblock_y    , mb_size, &block_a);
    get4x4NeighbourBase(mb, mb->subblock_x    , mb->subblock_y - 1, mb_size, &block_b);

    int condTermFlagA = 0;
    int condTermFlagB = 0;
    int ctxIdxInc;

    if (block_a.available) {
        mb_t *mb_a = &slice->mb_data[block_a.mb_addr];
        condTermFlagA = iabs(mb_a->mvd[list][block_a.y][block_a.x][xy]);
        if (slice->MbaffFrameFlag && xy == 1) {
            if (!mb->mb_field_decoding_flag && mb_a->mb_field_decoding_flag)
                condTermFlagA *= 2;
            else if (mb->mb_field_decoding_flag && !mb_a->mb_field_decoding_flag)
                condTermFlagA /= 2;
        }
    }
    if (block_b.available) {
        mb_t *mb_b = &slice->mb_data[block_b.mb_addr];
        condTermFlagB = iabs(mb_b->mvd[list][block_b.y][block_b.x][xy]);
        if (slice->MbaffFrameFlag && xy == 1) {
            if (!mb->mb_field_decoding_flag && mb_b->mb_field_decoding_flag)
                condTermFlagB *= 2;
            else if (mb->mb_field_decoding_flag && !mb_b->mb_field_decoding_flag)
                condTermFlagB /= 2;
        }
    }

    ctxIdxInc = condTermFlagA + condTermFlagB;
    ctxIdxInc = 5 * xy + (ctxIdxInc < 3 ? 0 : ctxIdxInc <= 32 ? 2 : 3);

    cabac_context_t *ctx = slice->mot_ctx->mv_res_contexts[0];
    if (dep_dp->decode_decision(ctx + ctxIdxInc)) {
        ctxIdxInc = 5 * xy;

        ctx = slice->mot_ctx->mv_res_contexts[1];
        mvd = unary_exp_golomb_mv_decode(dep_dp, ctx + ctxIdxInc, 3) + 1;
        if (dep_dp->decode_bypass())
            mvd = -mvd;
    }

    return mvd;
}

static uint8_t read_CBP_CABAC(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;
    sps_t *sps = slice->active_sps;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    uint8_t coded_block_pattern = 0;

    //  coding of luma part (bit by bit)
    for (int mb_y = 0; mb_y < 4; mb_y += 2) {
        for (int mb_x = 0; mb_x < 4; mb_x += 2) {
            int condTermFlagA = 0;
            int condTermFlagB = 0;

            if (mb_x == 0) {
                PixelPos block_a;
                int mb_size[2] = { MB_BLOCK_SIZE, MB_BLOCK_SIZE };
                get4x4Neighbour(mb, (mb_x << 2) - 1, (mb_y << 2), mb_size, &block_a);
                if (block_a.available && slice->mb_data[block_a.mb_addr].mb_type != IPCM) {
                    int a_cbp = slice->mb_data[block_a.mb_addr].cbp;
                    condTermFlagA = (a_cbp & (1 << (2 * (block_a.y >> 1) + 1))) == 0 ? 1 : 0;
                }
            } else
                condTermFlagA = (coded_block_pattern & (1 << mb_y)) == 0 ? 1 : 0;
            if (mb_y == 0) {
                if (mb->mb_up && mb->mb_up->mb_type != IPCM) {
                    int b_cbp = mb->mb_up->cbp;
                    condTermFlagB = (b_cbp & (1 << (2 + (mb_x >> 1)))) == 0 ? 2 : 0;
                }
            } else
                condTermFlagB = (coded_block_pattern & (1 << (mb_x >> 1))) == 0 ? 2 : 0;

            int ctxIdxInc = condTermFlagA + condTermFlagB;
            cabac_context_t *ctx = slice->mot_ctx->cbp_contexts[0];
            if (dep_dp->decode_decision(ctx + ctxIdxInc))
                coded_block_pattern += (1 << (mb_y + (mb_x >> 1)));
        }
    }

    if (sps->chroma_format_idc != YUV400 && sps->chroma_format_idc != YUV444) {
        // coding of chroma part
        // CABAC decoding for BinIdx 0
        cabac_context_t *ctx = slice->mot_ctx->cbp_contexts[1];
        int condTermFlagA = mb->mb_left && (mb->mb_left->mb_type == IPCM || mb->mb_left->cbp > 15) ? 1 : 0;
        int condTermFlagB = mb->mb_up   && (mb->mb_up  ->mb_type == IPCM || mb->mb_up  ->cbp > 15) ? 2 : 0;
        int ctxIdxInc = condTermFlagA + condTermFlagB;

        // CABAC decoding for BinIdx 1
        if (dep_dp->decode_decision(ctx + ctxIdxInc)) { // set the chroma bits
            ctx = slice->mot_ctx->cbp_contexts[2];
            condTermFlagA = mb->mb_left && (mb->mb_left->mb_type == IPCM || (mb->mb_left->cbp >> 4) == 2) ? 1 : 0;
            condTermFlagB = mb->mb_up   && (mb->mb_up  ->mb_type == IPCM || (mb->mb_up  ->cbp >> 4) == 2) ? 2 : 0;
            ctxIdxInc = condTermFlagA + condTermFlagB;

            coded_block_pattern += dep_dp->decode_decision(ctx + ctxIdxInc) ? 32 : 16;
        }
    }

    if (!coded_block_pattern)
        slice->last_dquant = 0;

    return coded_block_pattern;
}



uint32_t parse_mb_skip_run(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;
    pps_t *pps = slice->active_pps;

    Bitstream *bitstream = slice->partArr[0].bitstream;

    uint32_t mb_skip_run = 0;

    if (!pps->entropy_coding_mode_flag)
        mb_skip_run = bitstream->ue();

    return mb_skip_run;
}

bool parse_mb_skip_flag(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;
    pps_t *pps = slice->active_pps;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    bool mb_skip_flag = 0;

    if (pps->entropy_coding_mode_flag) {
        int tabIdx = slice->slice_type == B_slice ? 2 : 1;
        int ctxIdx = slice->slice_type == B_slice ? 7 : 0;
        cabac_context_t *ctx = &slice->mot_ctx->mb_type_contexts[tabIdx][ctxIdx];

        int condTermFlagA = mb->mb_left && !mb->mb_left->mb_skip_flag ? 1 : 0;
        int condTermFlagB = mb->mb_up   && !mb->mb_up  ->mb_skip_flag ? 1 : 0;
        int ctxIdxInc = condTermFlagA + condTermFlagB;

        mb_skip_flag = dep_dp->decode_decision(ctx + ctxIdxInc);
    }

    return mb_skip_flag;
}

bool parse_mb_field_decoding_flag(mb_t *mb)
{
	slice_t *slice = mb->p_Slice;
    pps_t *pps = slice->active_pps;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    bool mb_field_decoding_flag;

    if (!pps->entropy_coding_mode_flag)
        mb_field_decoding_flag = bitstream->f(1);
    else {
        cabac_context_t *ctx = slice->mot_ctx->mb_aff_contexts;

        int condTermFlagA = mb->mbAvailA && slice->mb_data[mb->mbAddrA].mb_field_decoding_flag ? 1 : 0;
        int condTermFlagB = mb->mbAvailB && slice->mb_data[mb->mbAddrB].mb_field_decoding_flag ? 1 : 0;
        int ctxIdxInc = condTermFlagA + condTermFlagB;

        mb_field_decoding_flag = dep_dp->decode_decision(ctx + ctxIdxInc);
    }

    return mb_field_decoding_flag;
}

static uint8_t parse_mb_type_i_slice(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    uint8_t mb_type = 0;

    if (slice->slice_type == SI_slice) {
        cabac_context_t *ctx = slice->mot_ctx->mb_type_contexts[1]; // ctxIdxOffset = 0

        int condTermFlagA = mb->mb_left && mb->mb_left->mb_type != SI4MB ? 1 : 0;
        int condTermFlagB = mb->mb_up   && mb->mb_up  ->mb_type != SI4MB ? 1 : 0;
        int ctxIdxInc = condTermFlagA + condTermFlagB;

        mb_type = dep_dp->decode_decision(ctx + ctxIdxInc);
    }

    if (slice->slice_type == I_slice || mb_type == 1) {
        cabac_context_t *ctx = slice->mot_ctx->mb_type_contexts[0]; // ctxIdxOffset = 3

        int condTermFlagA = mb->mb_left && mb->mb_left->mb_type != I4MB && mb->mb_left->mb_type != I8MB ? 1 : 0;
        int condTermFlagB = mb->mb_up   && mb->mb_up  ->mb_type != I4MB && mb->mb_up  ->mb_type != I8MB ? 1 : 0;
        int ctxIdxInc = condTermFlagA + condTermFlagB;

        if (dep_dp->decode_decision(ctx + ctxIdxInc)) {
            if (!dep_dp->decode_terminate()) {
                mb_type += 1;
                mb_type += dep_dp->decode_decision(ctx + 4) * 12;
                if (dep_dp->decode_decision(ctx + 5))
                    mb_type += dep_dp->decode_decision(ctx + 6) * 4 + 4;
                mb_type += dep_dp->decode_decision(ctx + 7) * 2;
                mb_type += dep_dp->decode_decision(ctx + 8);
            } else
                mb_type += 25;
        }
    }

    return mb_type;
}

static uint8_t pares_mb_type_p_slice(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    uint8_t mb_type = 1;

    cabac_context_t *ctx = slice->mot_ctx->mb_type_contexts[1] + 4; // ctxIdxOffset = 14
    if (!dep_dp->decode_decision(ctx + 0)) {
        if (!dep_dp->decode_decision(ctx + 1))
            mb_type += dep_dp->decode_decision(ctx + 2) * 3;
        else
            mb_type -= dep_dp->decode_decision(ctx + 3) - 2;
    } else
        mb_type += 5;

    if (mb_type == 6) {
        ctx = slice->mot_ctx->mb_type_contexts[1] + 7; // ctxIdxOffset = 17

        if (dep_dp->decode_decision(ctx + 0)) {
            if (!dep_dp->decode_terminate()) {
                mb_type += 1;
                mb_type += dep_dp->decode_decision(ctx + 1) * 12;
                if (dep_dp->decode_decision(ctx + 2))
                    mb_type += dep_dp->decode_decision(ctx + 2) * 4 + 4;
                mb_type += dep_dp->decode_decision(ctx + 3) * 2;
                mb_type += dep_dp->decode_decision(ctx + 3);
            } else
                mb_type += 25;
        }
    }

    return mb_type;
}

static uint8_t parse_mb_type_b_slice(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    uint8_t mb_type = 0;

    int condTermFlagA = mb->mb_left && mb->mb_left->mb_type != 0 ? 1 : 0;
    int condTermFlagB = mb->mb_up   && mb->mb_up  ->mb_type != 0 ? 1 : 0;
    int ctxIdxInc = condTermFlagA + condTermFlagB;

    cabac_context_t *ctx = slice->mot_ctx->mb_type_contexts[2]; // ctxIdxOffset = 27
    if (dep_dp->decode_decision(ctx + ctxIdxInc)) {
        mb_type = 1;
        if (!dep_dp->decode_decision(ctx + 4))
            mb_type += dep_dp->decode_decision(ctx + 6);
        else {
            mb_type += 2;
            if (!dep_dp->decode_decision(ctx + 5)) {
                mb_type += dep_dp->decode_decision(ctx + 6) * 4;
                mb_type += dep_dp->decode_decision(ctx + 6) * 2;
                mb_type += dep_dp->decode_decision(ctx + 6);
            } else {
                mb_type += 9;
                mb_type += dep_dp->decode_decision(ctx + 6) * 8; 
                mb_type += dep_dp->decode_decision(ctx + 6) * 4;
                mb_type += dep_dp->decode_decision(ctx + 6) * 2;
                if (mb_type < 22)
                    mb_type += dep_dp->decode_decision(ctx + 6);

                if (mb_type == 22)
                    mb_type = 23;
                else if (mb_type == 24)  
                    mb_type = 11;
                else if (mb_type == 26)  
                    mb_type = 22;
            }
        }
    }

    if (mb_type == 23) {
        if (dep_dp->decode_decision(ctx + 6)) {
            ctx = slice->mot_ctx->mb_type_contexts[1] + 7; // ctxIdxOffset = 32
            if (!dep_dp->decode_terminate()) {
                mb_type += 1;
                mb_type += dep_dp->decode_decision(ctx + 1) * 12;
                if (dep_dp->decode_decision(ctx + 2))
                    mb_type += dep_dp->decode_decision(ctx + 2) * 4 + 4;
                mb_type += dep_dp->decode_decision(ctx + 3) * 2;
                mb_type += dep_dp->decode_decision(ctx + 3);
            } else
                mb_type += 25;
        }
    }

    return mb_type;
}

uint32_t parse_mb_type(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;
    pps_t *pps = slice->active_pps;

    Bitstream *bitstream = slice->partArr[0].bitstream;

    uint32_t mb_type;

    if (!pps->entropy_coding_mode_flag) {
        mb_type = bitstream->ue();
        mb_type += (slice->slice_type == P_slice || slice->slice_type == SP_slice) ? 1 : 0;
    } else {
        uint8_t (*reading)(mb_t*) =
            slice->slice_type == I_slice || slice->slice_type == SI_slice ? parse_mb_type_i_slice :
            slice->slice_type == P_slice || slice->slice_type == SP_slice ? pares_mb_type_p_slice :
                                                                            parse_mb_type_b_slice;
        mb_type = reading(mb);
    }

    return mb_type;
}

static uint8_t parse_sub_mb_type_p_slice(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    cabac_context_t *ctx = slice->mot_ctx->b8_type_contexts[0]; // ctxIdxOffset = 21

    uint8_t sub_mb_type = 0;

    if (!dep_dp->decode_decision(ctx + 1)) {
        sub_mb_type += 1;
        if (dep_dp->decode_decision(ctx + 3))
            sub_mb_type += dep_dp->decode_decision(ctx + 4) ? 1 : 2;
    }

    return sub_mb_type;
}

static uint8_t parse_sub_mb_type_b_slice(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    cabac_context_t *ctx = slice->mot_ctx->b8_type_contexts[1]; // ctxIdxOffset = 36

    uint8_t sub_mb_type = 0;

    if (dep_dp->decode_decision(ctx)) {
        sub_mb_type += 1;
        if (dep_dp->decode_decision(ctx + 1)) {
            sub_mb_type += 2;
            if (dep_dp->decode_decision(ctx + 2)) {
                sub_mb_type += 4;
                if (dep_dp->decode_decision(ctx + 3))
                    sub_mb_type += 4;
                else
                    sub_mb_type += dep_dp->decode_decision(ctx + 3) * 2;
            } else
                sub_mb_type += dep_dp->decode_decision(ctx + 3) * 2;
        }
        sub_mb_type += dep_dp->decode_decision(ctx + 3);
    }

    return sub_mb_type;
}

uint8_t parse_sub_mb_type(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;
    pps_t *pps = slice->active_pps;

    Bitstream *bitstream = slice->partArr[0].bitstream;

    uint8_t sub_mb_type;

    if (!pps->entropy_coding_mode_flag) 
        sub_mb_type = bitstream->ue();
    else {
        if (slice->slice_type == P_slice || slice->slice_type == SP_slice)
            sub_mb_type = parse_sub_mb_type_p_slice(mb);
        else
            sub_mb_type = parse_sub_mb_type_b_slice(mb);
    }

    return sub_mb_type;
}


bool parse_transform_size_8x8_flag(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;
    pps_t *pps = slice->active_pps;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    bool transform_size_8x8_flag;

    if (!pps->entropy_coding_mode_flag)
        transform_size_8x8_flag = bitstream->f(1);
    else {
        cabac_context_t *ctx = slice->mot_ctx->transform_size_contexts;

        int condTermFlagA = mb->mb_left && mb->mb_left->transform_size_8x8_flag ? 1 : 0;
        int condTermFlagB = mb->mb_up   && mb->mb_up  ->transform_size_8x8_flag ? 1 : 0;
        int ctxIdxInc = condTermFlagA + condTermFlagB;

        transform_size_8x8_flag = dep_dp->decode_decision(ctx + ctxIdxInc);
    }

    return transform_size_8x8_flag;
}

int8_t parse_intra_pred_mode(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;
    pps_t *pps = slice->active_pps;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    int8_t intra_pred_mode = 0;

    if (!pps->entropy_coding_mode_flag) {
        if (bitstream->f(1))
            intra_pred_mode = -1;
        else
            intra_pred_mode = bitstream->f(3);
    } else {
        cabac_context_t *ctx = slice->mot_ctx->ipr_contexts;

        if (dep_dp->decode_decision(ctx))
            intra_pred_mode = -1;
        else {
            intra_pred_mode += dep_dp->decode_decision(ctx + 1);
            intra_pred_mode += dep_dp->decode_decision(ctx + 1) * 2;
            intra_pred_mode += dep_dp->decode_decision(ctx + 1) * 4;
        }
    }

    return intra_pred_mode;
}

uint8_t parse_intra_chroma_pred_mode(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;
    pps_t *pps = slice->active_pps;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    uint8_t intra_chroma_pred_mode;

    if (!pps->entropy_coding_mode_flag)
        intra_chroma_pred_mode = bitstream->ue();
    else {
        cabac_context_t *ctx = slice->mot_ctx->cipr_contexts;

        int condTermFlagA = mb->mb_left && mb->mb_left->intra_chroma_pred_mode != 0 && mb->mb_left->mb_type != IPCM ? 1 : 0;
        int condTermFlagB = mb->mb_up   && mb->mb_up  ->intra_chroma_pred_mode != 0 && mb->mb_up  ->mb_type != IPCM ? 1 : 0;
        int ctxIdxInc = condTermFlagA + condTermFlagB;

        // binIdx[] = { ctxIdxInc, 3, 3 };

        intra_chroma_pred_mode = dep_dp->decode_decision(ctx + ctxIdxInc);
        if (intra_chroma_pred_mode)
            intra_chroma_pred_mode = dep_dp->tu(ctx + 3, 0, 1) + 1;
    }

    return intra_chroma_pred_mode;
}

uint8_t parse_ref_idx(mb_t *mb, uint8_t list)
{
    slice_t *slice = mb->p_Slice;
    pps_t *pps = slice->active_pps;

    bool refidx_present =
        slice->slice_type == B_slice || !slice->allrefzero || mb->mb_type != P8x8;
    int num_ref_idx_active = list == LIST_0 ?
        slice->num_ref_idx_l0_active_minus1 + 1 :
        slice->num_ref_idx_l1_active_minus1 + 1;

    if (!refidx_present || num_ref_idx_active <= 1)
        return 0;

    Bitstream *bitstream = slice->partArr[0].bitstream;

    uint8_t ref_idx;

    if (!pps->entropy_coding_mode_flag) {
        if (num_ref_idx_active == 2)
            ref_idx = 1 - bitstream->f(1);
        else
            ref_idx = bitstream->ue();
    } else
        ref_idx = readRefFrame_CABAC(mb, list);

    return ref_idx;
}

int16_t parse_mvd(mb_t *mb, uint8_t xy, uint8_t list)
{
    slice_t *slice = mb->p_Slice;
    pps_t *pps = slice->active_pps;

    Bitstream *bitstream = slice->partArr[0].bitstream;

    int16_t mvd;

    if (!pps->entropy_coding_mode_flag) 
        mvd = bitstream->se();
    else
        mvd = read_mvd_CABAC(mb, xy, list);

    return mvd;
}

uint8_t parse_coded_block_pattern(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;
    sps_t *sps = slice->active_sps;
    pps_t *pps = slice->active_pps;

    Bitstream *bitstream = slice->partArr[0].bitstream;

    uint8_t coded_block_pattern;

    if (!pps->entropy_coding_mode_flag) {
        //! gives CBP value from codeword number, both for intra and inter
        static const uint8_t NCBP[2][48][2] = {
            { { 15,  0 } , {  0,  1 } , {  7,  2 } , { 11,  4 } , { 13,  8 } , { 14,  3 },
              {  3,  5 } , {  5, 10 } , { 10, 12 } , { 12, 15 } , {  1,  7 } , {  2, 11 },
              {  4, 13 } , {  8, 14 } , {  6,  6 } , {  9,  9 } , {  0,  0 } , {  0,  0 },
              {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 },
              {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 },
              {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 },
              {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 },
              {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 } , {  0,  0 } },
            { { 47,  0 } , { 31, 16 } , { 15,  1 } , {  0,  2 } , { 23,  4 } , { 27,  8 },
              { 29, 32 } , { 30,  3 } , {  7,  5 } , { 11, 10 } , { 13, 12 } , { 14, 15 },
              { 39, 47 } , { 43,  7 } , { 45, 11 } , { 46, 13 } , { 16, 14 } , {  3,  6 },
              {  5,  9 } , { 10, 31 } , { 12, 35 } , { 19, 37 } , { 21, 42 } , { 26, 44 },
              { 28, 33 } , { 35, 34 } , { 37, 36 } , { 42, 40 } , { 44, 39 } , {  1, 43 },
              {  2, 45 } , {  4, 46 } , {  8, 17 } , { 17, 18 } , { 18, 20 } , { 20, 24 },
              { 24, 19 } , {  6, 21 } , {  9, 26 } , { 22, 28 } , { 25, 23 } , { 32, 27 },
              { 33, 29 } , { 34, 30 } , { 36, 22 } , { 40, 25 } , { 38, 38 } , { 41, 41 } }
        };

        bool normal  = (sps->chroma_format_idc == 0 || sps->chroma_format_idc == 3 ? 0 : 1);
        bool inter   = (mb->is_intra_block ? 0 : 1);
        int  cbp_idx = bitstream->ue();
        coded_block_pattern = NCBP[normal][cbp_idx][inter];
    } else
        coded_block_pattern = read_CBP_CABAC(mb);

    return coded_block_pattern;
}

int8_t parse_mb_qp_delta(mb_t *mb)
{
    slice_t *slice = mb->p_Slice;
    pps_t *pps = slice->active_pps;

    Bitstream *bitstream = slice->partArr[0].bitstream;
    cabac_engine_t *dep_dp = &bitstream->de_cabac;

    int8_t mb_qp_delta;

    if (!pps->entropy_coding_mode_flag)
        mb_qp_delta = bitstream->se();
    else {
        cabac_context_t *ctx = slice->mot_ctx->delta_qp_contexts;

        int ctxIdxInc = slice->last_dquant != 0 ? 1 : 0;

        mb_qp_delta = dep_dp->decode_decision(ctx + ctxIdxInc);

        //int binIdx[] = { ctxIdxInc, 2, 3, 3, 3, 3, 3 };

        if (mb_qp_delta) {
            ctxIdxInc = 2;
            int act_sym = dep_dp->u(ctx + ctxIdxInc, 1) + 1;

            mb_qp_delta = (act_sym + 1) >> 1;
            if ((act_sym & 1) == 0) // lsb is signed bit
                mb_qp_delta = -mb_qp_delta;
        }
    }

    return mb_qp_delta;
}
