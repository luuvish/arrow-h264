/*!
 *************************************************************************************
 * \file mv_prediction.c
 *
 * \brief
 *    Motion Vector Prediction Functions
 *
 *  \author
 *      Main contributors (see contributors.h for copyright, address and affiliation details)
 *      - Alexis Michael Tourapis  <alexismt@ieee.org>
 *      - Karsten Suehring
 *************************************************************************************
 */

#include "global.h"
#include "macroblock.h"
#include "dpb.h"
#include "mv_prediction.h"
#include "slice.h"


// MV Prediction types
typedef enum {
  MVPRED_MEDIAN   = 0,
  MVPRED_L        = 1,
  MVPRED_U        = 2,
  MVPRED_UR       = 3
} MVPredTypes;


static inline int imedian(int a,int b,int c)
{
  if (a > b) // a > b
  { 
    if (b > c) 
      return(b); // a > b > c
    else if (a > c) 
      return(c); // a > c > b
    else 
      return(a); // c > a > b
  }
  else // b > a
  { 
    if (a > c) 
      return(a); // b > a > c
    else if (b > c)
      return(c); // b > c > a
    else
      return(b);  // c > b > a
  }
}


/*!
 ************************************************************************
 * \brief
 *    Get motion vector predictor
 ************************************************************************
 */
static void GetMotionVectorPredictorMBAFF (mb_t *currMB, 
                                    PixelPos *block,        // <--> block neighbors
                                    MotionVector *pmv,
                                    short  ref_frame,
                                    PicMotionParams **mv_info,
                                    int    list,
                                    int    mb_x,
                                    int    mb_y,
                                    int    blockshape_x,
                                    int    blockshape_y)
{
    int mv_a, mv_b, mv_c, pred_vec=0;
    int mvPredType, rFrameL, rFrameU, rFrameUR;
    int hv;
    VideoParameters *p_Vid = currMB->p_Vid;

    mvPredType = MVPRED_MEDIAN;

    if (currMB->mb_field_decoding_flag) {
        rFrameL  = block[0].available
            ? (p_Vid->mb_data[block[0].mb_addr].mb_field_decoding_flag
            ? mv_info[block[0].pos_y][block[0].pos_x].ref_idx[list]
            : mv_info[block[0].pos_y][block[0].pos_x].ref_idx[list] * 2) : -1;
        rFrameU  = block[1].available
            ? (p_Vid->mb_data[block[1].mb_addr].mb_field_decoding_flag
            ? mv_info[block[1].pos_y][block[1].pos_x].ref_idx[list]
            : mv_info[block[1].pos_y][block[1].pos_x].ref_idx[list] * 2) : -1;
        rFrameUR = block[2].available
            ? (p_Vid->mb_data[block[2].mb_addr].mb_field_decoding_flag
            ? mv_info[block[2].pos_y][block[2].pos_x].ref_idx[list]
            : mv_info[block[2].pos_y][block[2].pos_x].ref_idx[list] * 2) : -1;
    } else {
        rFrameL = block[0].available
            ? (p_Vid->mb_data[block[0].mb_addr].mb_field_decoding_flag
            ? mv_info[block[0].pos_y][block[0].pos_x].ref_idx[list] >>1
            : mv_info[block[0].pos_y][block[0].pos_x].ref_idx[list]) : -1;
        rFrameU  = block[1].available
            ? (p_Vid->mb_data[block[1].mb_addr].mb_field_decoding_flag
            ? mv_info[block[1].pos_y][block[1].pos_x].ref_idx[list] >>1
            : mv_info[block[1].pos_y][block[1].pos_x].ref_idx[list]) : -1;
        rFrameUR = block[2].available
            ? (p_Vid->mb_data[block[2].mb_addr].mb_field_decoding_flag
            ? mv_info[block[2].pos_y][block[2].pos_x].ref_idx[list] >>1
            : mv_info[block[2].pos_y][block[2].pos_x].ref_idx[list]) : -1;
    }

    /* Prediction if only one of the neighbors uses the reference frame
     *  we are checking
     */
    if (rFrameL == ref_frame && rFrameU != ref_frame && rFrameUR != ref_frame)       
        mvPredType = MVPRED_L;
    else if (rFrameL != ref_frame && rFrameU == ref_frame && rFrameUR != ref_frame)  
        mvPredType = MVPRED_U;
    else if (rFrameL != ref_frame && rFrameU != ref_frame && rFrameUR == ref_frame)  
        mvPredType = MVPRED_UR;
    // Directional predictions
    if (blockshape_x == 8 && blockshape_y == 16) {
        if (mb_x == 0) {
            if (rFrameL == ref_frame)
                mvPredType = MVPRED_L;
        } else {
            if (rFrameUR == ref_frame)
                mvPredType = MVPRED_UR;
        }
    } else if (blockshape_x == 16 && blockshape_y == 8) {
        if (mb_y == 0) {
            if (rFrameU == ref_frame)
                mvPredType = MVPRED_U;
        } else {
            if (rFrameL == ref_frame)
                mvPredType = MVPRED_L;
        }
    }

    for (hv = 0; hv < 2; hv++) {
        if (hv == 0) {
            mv_a = block[0].available ? mv_info[block[0].pos_y][block[0].pos_x].mv[list].mv_x : 0;
            mv_b = block[1].available ? mv_info[block[1].pos_y][block[1].pos_x].mv[list].mv_x : 0;
            mv_c = block[2].available ? mv_info[block[2].pos_y][block[2].pos_x].mv[list].mv_x : 0;
        } else {
            if (currMB->mb_field_decoding_flag) {
                mv_a = block[0].available
                    ? p_Vid->mb_data[block[0].mb_addr].mb_field_decoding_flag
                    ? mv_info[block[0].pos_y][block[0].pos_x].mv[list].mv_y
                    : mv_info[block[0].pos_y][block[0].pos_x].mv[list].mv_y / 2
                    : 0;
                mv_b = block[1].available
                    ? p_Vid->mb_data[block[1].mb_addr].mb_field_decoding_flag
                    ? mv_info[block[1].pos_y][block[1].pos_x].mv[list].mv_y
                    : mv_info[block[1].pos_y][block[1].pos_x].mv[list].mv_y / 2
                    : 0;
                mv_c = block[2].available
                    ? p_Vid->mb_data[block[2].mb_addr].mb_field_decoding_flag
                    ? mv_info[block[2].pos_y][block[2].pos_x].mv[list].mv_y
                    : mv_info[block[2].pos_y][block[2].pos_x].mv[list].mv_y / 2
                    : 0;
            } else {
                mv_a = block[0].available
                    ? p_Vid->mb_data[block[0].mb_addr].mb_field_decoding_flag
                    ? mv_info[block[0].pos_y][block[0].pos_x].mv[list].mv_y * 2
                    : mv_info[block[0].pos_y][block[0].pos_x].mv[list].mv_y
                    : 0;
                mv_b = block[1].available
                    ? p_Vid->mb_data[block[1].mb_addr].mb_field_decoding_flag
                    ? mv_info[block[1].pos_y][block[1].pos_x].mv[list].mv_y * 2
                    : mv_info[block[1].pos_y][block[1].pos_x].mv[list].mv_y
                    : 0;
                mv_c = block[2].available
                    ? p_Vid->mb_data[block[2].mb_addr].mb_field_decoding_flag
                    ? mv_info[block[2].pos_y][block[2].pos_x].mv[list].mv_y * 2
                    : mv_info[block[2].pos_y][block[2].pos_x].mv[list].mv_y
                    : 0;
            }
        }

        switch (mvPredType) {
        case MVPRED_MEDIAN:
            if (!(block[1].available || block[2].available))
                pred_vec = mv_a;
            else
                pred_vec = imedian(mv_a, mv_b, mv_c);
            break;
        case MVPRED_L:
            pred_vec = mv_a;
            break;
        case MVPRED_U:
            pred_vec = mv_b;
            break;
        case MVPRED_UR:
            pred_vec = mv_c;
            break;
        default:
            break;
        }

        if (hv == 0)
            pmv->mv_x = (short) pred_vec;
        else
            pmv->mv_y = (short) pred_vec;
    }
}

/*!
 ************************************************************************
 * \brief
 *    Get motion vector predictor
 ************************************************************************
 */
static void GetMotionVectorPredictorNormal (mb_t *currMB, 
                                            PixelPos *block,      // <--> block neighbors
                                            MotionVector *pmv,
                                            short  ref_frame,
                                            PicMotionParams **mv_info,
                                            int    list,
                                            int    mb_x,
                                            int    mb_y,
                                            int    blockshape_x,
                                            int    blockshape_y)
{
    int mvPredType = MVPRED_MEDIAN;

    int rFrameL    = block[0].available ? mv_info[block[0].pos_y][block[0].pos_x].ref_idx[list] : -1;
    int rFrameU    = block[1].available ? mv_info[block[1].pos_y][block[1].pos_x].ref_idx[list] : -1;
    int rFrameUR   = block[2].available ? mv_info[block[2].pos_y][block[2].pos_x].ref_idx[list] : -1;

    /* Prediction if only one of the neighbors uses the reference frame
     *  we are checking
     */
    if (rFrameL == ref_frame && rFrameU != ref_frame && rFrameUR != ref_frame)       
        mvPredType = MVPRED_L;
    else if(rFrameL != ref_frame && rFrameU == ref_frame && rFrameUR != ref_frame)  
        mvPredType = MVPRED_U;
    else if(rFrameL != ref_frame && rFrameU != ref_frame && rFrameUR == ref_frame)  
        mvPredType = MVPRED_UR;

    // Directional predictions
    if (blockshape_x == 8 && blockshape_y == 16) {
        if (mb_x == 0) {
            if (rFrameL == ref_frame)
                mvPredType = MVPRED_L;
        } else {
            if (rFrameUR == ref_frame)
                mvPredType = MVPRED_UR;
        }
    } else if (blockshape_x == 16 && blockshape_y == 8) {
        if (mb_y == 0) {
            if (rFrameU == ref_frame)
                mvPredType = MVPRED_U;
        } else {
            if (rFrameL == ref_frame)
                mvPredType = MVPRED_L;
        }
    }

    switch (mvPredType) {
    case MVPRED_MEDIAN:
        if (!(block[1].available || block[2].available)) {
            if (block[0].available)
                *pmv = mv_info[block[0].pos_y][block[0].pos_x].mv[list];
            else
                *pmv = zero_mv;
        } else {
            MotionVector *mv_a = block[0].available ? &mv_info[block[0].pos_y][block[0].pos_x].mv[list] : (MotionVector *) &zero_mv;
            MotionVector *mv_b = block[1].available ? &mv_info[block[1].pos_y][block[1].pos_x].mv[list] : (MotionVector *) &zero_mv;
            MotionVector *mv_c = block[2].available ? &mv_info[block[2].pos_y][block[2].pos_x].mv[list] : (MotionVector *) &zero_mv;

            pmv->mv_x = (short) imedian(mv_a->mv_x, mv_b->mv_x, mv_c->mv_x);
            pmv->mv_y = (short) imedian(mv_a->mv_y, mv_b->mv_y, mv_c->mv_y);
        }
        break;
    case MVPRED_L:
        if (block[0].available)
            *pmv = mv_info[block[0].pos_y][block[0].pos_x].mv[list];
        else
            *pmv = zero_mv;
        break;
    case MVPRED_U:
        if (block[1].available)
            *pmv = mv_info[block[1].pos_y][block[1].pos_x].mv[list];
        else
            *pmv = zero_mv;
        break;
    case MVPRED_UR:
        if (block[2].available)
            *pmv = mv_info[block[2].pos_y][block[2].pos_x].mv[list];
        else
            *pmv = zero_mv;
        break;
    default:
        break;
    }
}

void GetMVPredictor(mb_t *currMB, PixelPos *block, MotionVector *pmv,
                    short ref_frame, PicMotionParams **mv_info,
                    int list, int mb_x, int mb_y, int blockshape_x, int blockshape_y)
{
    slice_t *currSlice = currMB->p_Slice;

    if (currSlice->MbaffFrameFlag)
        GetMotionVectorPredictorMBAFF(currMB, block, pmv, ref_frame, mv_info,
                                      list, mb_x, mb_y, blockshape_x, blockshape_y);
    else
        GetMotionVectorPredictorNormal(currMB, block, pmv, ref_frame, mv_info,
                                       list, mb_x, mb_y, blockshape_x, blockshape_y);
}