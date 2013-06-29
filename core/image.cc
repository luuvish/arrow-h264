
/*!
 ***********************************************************************
 * \file image.c
 *
 * \brief
 *    Decode a Slice
 *
 * \author
 *    Main contributors (see contributors.h for copyright, address and affiliation details)
 *    - Inge Lille-Langoy               <inge.lille-langoy@telenor.com>
 *    - Rickard Sjoberg                 <rickard.sjoberg@era.ericsson.se>
 *    - Jani Lainema                    <jani.lainema@nokia.com>
 *    - Sebastian Purreiter             <sebastian.purreiter@mch.siemens.de>
 *    - Byeong-Moon Jeon                <jeonbm@lge.com>
 *    - Thomas Wedi                     <wedi@tnt.uni-hannover.de>
 *    - Gabi Blaettermann
 *    - Ye-Kui Wang                     <wyk@ieee.org>
 *    - Antti Hallapuro                 <antti.hallapuro@nokia.com>
 *    - Alexis Tourapis                 <alexismt@ieee.org>
 *    - Jill Boyce                      <jill.boyce@thomson.net>
 *    - Saurav K Bandyopadhyay          <saurav@ieee.org>
 *    - Zhenyu Wu                       <Zhenyu.Wu@thomson.net
 *    - Purvin Pandit                   <Purvin.Pandit@thomson.net>
 *
 ***********************************************************************
 */

#include <math.h>
#include <limits.h>

#include "global.h"
#include "slice.h"
#include "image.h"
#include "fmo.h"
#include "bitstream_nal.h"
#include "bitstream_cabac.h"
#include "bitstream.h"
#include "parset.h"

#include "sei.h"
#include "output.h"
#include "neighbour.h"
#include "memalloc.h"
#include "macroblock.h"
#include "mb.h"

#include "intra_prediction.h"
#include "deblock.h"

#include "biaridecod.h"

#include "erc_api.h"
#include "dpb_common.h"
#include "dpb_mvc.h"


static inline float psnr(int max_sample_sq, int samples, float sse_distortion ) 
{
  return (float) (sse_distortion == 0.0 ? 0.0 : (10.0 * log10(max_sample_sq * (double) ((double) samples / sse_distortion))));
}

static inline int iabs2(int x) 
{
  return (x) * (x);
}

static inline void reset_mbs(Macroblock *currMB)
{
  currMB->slice_nr = -1; 
  currMB->ei_flag  =  1;
  currMB->dpl_flag =  0;
}

static void setup_buffers(VideoParameters *p_Vid, int layer_id)
{
  CodingParameters *cps = p_Vid->p_EncodePar[layer_id];
  int i;

  if(p_Vid->last_dec_layer_id != layer_id)
  {
    p_Vid->imgY_ref = cps->imgY_ref;
    p_Vid->imgUV_ref = cps->imgUV_ref;
    if(cps->separate_colour_plane_flag)
    {
     for( i=0; i<MAX_PLANE; i++ )
     {
       p_Vid->mb_data_JV[i] = cps->mb_data_JV[i];
       p_Vid->intra_block_JV[i] = cps->intra_block_JV[i];
       p_Vid->ipredmode_JV[i] = cps->ipredmode_JV[i];
       p_Vid->siblock_JV[i] = cps->siblock_JV[i];
     }
     p_Vid->mb_data = NULL;
     p_Vid->intra_block = NULL;
     p_Vid->ipredmode = NULL;
     p_Vid->siblock = NULL;
    }
    else
    {
      p_Vid->mb_data = cps->mb_data;
      p_Vid->intra_block = cps->intra_block;
      p_Vid->ipredmode = cps->ipredmode;
      p_Vid->siblock = cps->siblock;
    }
    p_Vid->PicPos = cps->PicPos;
    p_Vid->nz_coeff = cps->nz_coeff;
    p_Vid->qp_per_matrix = cps->qp_per_matrix;
    p_Vid->qp_rem_matrix = cps->qp_rem_matrix;
    p_Vid->oldFrameSizeInMbs = cps->oldFrameSizeInMbs;
    p_Vid->img2buf = cps->img2buf;
    p_Vid->last_dec_layer_id = layer_id;
  }
}

#if MVC_EXTENSION_ENABLE
static void init_mvc_picture(Slice *currSlice)
{
  int i;
  VideoParameters *p_Vid = currSlice->p_Vid;
  DecodedPictureBuffer *p_Dpb = p_Vid->p_Dpb_layer[0];

  StorablePicture *p_pic = NULL;

  // find BL reconstructed picture
  if (currSlice->structure  == FRAME)
  {
    for (i = 0; i < (int)p_Dpb->used_size/*size*/; i++)
    {
      FrameStore *fs = p_Dpb->fs[i];
      if ((fs->frame->view_id == 0) && (fs->frame->frame_poc == currSlice->framepoc))
      {
        p_pic = fs->frame;
        break;
      }
    }
  }
  else if (currSlice->structure  == TOP_FIELD)
  {
    for (i = 0; i < (int)p_Dpb->used_size/*size*/; i++)
    {
      FrameStore *fs = p_Dpb->fs[i];
      if ((fs->top_field->view_id == 0) && (fs->top_field->top_poc == currSlice->toppoc))
      {
        p_pic = fs->top_field;
        break;
      }
    }
  }
  else
  {
    for (i = 0; i < (int)p_Dpb->used_size/*size*/; i++)
    {
      FrameStore *fs = p_Dpb->fs[i];
      if ((fs->bottom_field->view_id == 0) && (fs->bottom_field->bottom_poc == currSlice->bottompoc))
      {
        p_pic = fs->bottom_field;
        break;
      }
    }
  }
  if(!p_pic)
  {
    p_Vid->bFrameInit = 0;
  }
  else
  {
    process_picture_in_dpb_s(p_Vid, p_pic);
    store_proc_picture_in_dpb (currSlice->p_Dpb, clone_storable_picture(p_Vid, p_pic));
  }
}
#endif


static void copy_dec_picture_JV( VideoParameters *p_Vid, StorablePicture *dst, StorablePicture *src )
{
  dst->top_poc              = src->top_poc;
  dst->bottom_poc           = src->bottom_poc;
  dst->frame_poc            = src->frame_poc;
  dst->qp                   = src->qp;
  dst->slice_qp_delta       = src->slice_qp_delta;
  dst->chroma_qp_offset[0]  = src->chroma_qp_offset[0];
  dst->chroma_qp_offset[1]  = src->chroma_qp_offset[1];

  dst->poc                  = src->poc;

  dst->slice_type           = src->slice_type;
  dst->used_for_reference   = src->used_for_reference;
  dst->idr_flag             = src->idr_flag;
  dst->no_output_of_prior_pics_flag = src->no_output_of_prior_pics_flag;
  dst->long_term_reference_flag = src->long_term_reference_flag;
  dst->adaptive_ref_pic_buffering_flag = src->adaptive_ref_pic_buffering_flag;

  dst->dec_ref_pic_marking_buffer = src->dec_ref_pic_marking_buffer;

  dst->mb_aff_frame_flag    = src->mb_aff_frame_flag;
  dst->PicWidthInMbs        = src->PicWidthInMbs;
  dst->pic_num              = src->pic_num;
  dst->frame_num            = src->frame_num;
  dst->recovery_frame       = src->recovery_frame;
  dst->coded_frame          = src->coded_frame;

  dst->chroma_format_idc    = src->chroma_format_idc;

  dst->frame_mbs_only_flag  = src->frame_mbs_only_flag;
  dst->frame_cropping_flag  = src->frame_cropping_flag;

  dst->frame_crop_left_offset   = src->frame_crop_left_offset;
  dst->frame_crop_right_offset  = src->frame_crop_right_offset;
  dst->frame_crop_top_offset    = src->frame_crop_top_offset;
  dst->frame_crop_bottom_offset = src->frame_crop_bottom_offset;

#if (ENABLE_OUTPUT_TONEMAPPING)
  // store the necessary tone mapping sei into StorablePicture structure
  dst->seiHasTone_mapping = src->seiHasTone_mapping;

  dst->seiHasTone_mapping    = src->seiHasTone_mapping;
  dst->tone_mapping_model_id = src->tone_mapping_model_id;
  dst->tonemapped_bit_depth  = src->tonemapped_bit_depth;
  if( src->tone_mapping_lut )
  {
    int coded_data_bit_max = (1 << p_Vid->seiToneMapping->coded_data_bit_depth);
    dst->tone_mapping_lut      = (imgpel *)malloc(sizeof(int) * coded_data_bit_max);
    if (NULL == dst->tone_mapping_lut)
    {
      no_mem_exit("copy_dec_picture_JV: tone_mapping_lut");
    }
    memcpy(dst->tone_mapping_lut, src->tone_mapping_lut, sizeof(imgpel) * coded_data_bit_max);
  }
#endif
}


/*!
 ************************************************************************
 * \brief
 *    Initializes the parameters for a new picture
 ************************************************************************
 */
void init_picture(VideoParameters *p_Vid, Slice *currSlice, InputParameters *p_Inp)
{
  int i;
  int nplane;
  StorablePicture *dec_picture = NULL;
  sps_t *active_sps = p_Vid->active_sps;
  DecodedPictureBuffer *p_Dpb = currSlice->p_Dpb;

  p_Vid->PicHeightInMbs = p_Vid->FrameHeightInMbs / ( 1 + currSlice->field_pic_flag );
  p_Vid->PicSizeInMbs   = p_Vid->PicWidthInMbs * p_Vid->PicHeightInMbs;
  p_Vid->FrameSizeInMbs = p_Vid->PicWidthInMbs * p_Vid->FrameHeightInMbs;

  p_Vid->bFrameInit = 1;
  if (p_Vid->dec_picture)
  {
    // this may only happen on slice loss
    exit_picture(p_Vid, &p_Vid->dec_picture);
  }
  p_Vid->dpb_layer_id = currSlice->layer_id;
  //set buffers;
  setup_buffers(p_Vid, currSlice->layer_id);

  if (p_Vid->recovery_point)
    p_Vid->recovery_frame_num = (currSlice->frame_num + p_Vid->recovery_frame_cnt) % p_Vid->max_frame_num;

  if (currSlice->idr_flag)
    p_Vid->recovery_frame_num = currSlice->frame_num;

  if (p_Vid->recovery_point == 0 &&
    currSlice->frame_num != p_Vid->pre_frame_num &&
    currSlice->frame_num != (p_Vid->pre_frame_num + 1) % p_Vid->max_frame_num)
  {
    if (active_sps->gaps_in_frame_num_value_allowed_flag == 0)
    {
      // picture error concealment
      if(p_Inp->conceal_mode !=0)
      {
        if((currSlice->frame_num) < ((p_Vid->pre_frame_num + 1) % p_Vid->max_frame_num))
        {
          /* Conceal lost IDR frames and any frames immediately
             following the IDR. Use frame copy for these since
             lists cannot be formed correctly for motion copy*/
          p_Vid->conceal_mode = 1;
          p_Vid->IDR_concealment_flag = 1;
          conceal_lost_frames(p_Dpb, currSlice);
          //reset to original concealment mode for future drops
          p_Vid->conceal_mode = p_Inp->conceal_mode;
        }
        else
        {
          //reset to original concealment mode for future drops
          p_Vid->conceal_mode = p_Inp->conceal_mode;

          p_Vid->IDR_concealment_flag = 0;
          conceal_lost_frames(p_Dpb, currSlice);
        }
      }
      else
      {   /* Advanced Error Concealment would be called here to combat unintentional loss of pictures. */
        error("An unintentional loss of pictures occurs! Exit\n", 100);
      }
    }
    if(p_Vid->conceal_mode == 0)
      fill_frame_num_gap(p_Vid, currSlice);
  }

  if(currSlice->nal_reference_idc)
  {
    p_Vid->pre_frame_num = currSlice->frame_num;
  }

  //calculate POC
  decode_poc(p_Vid, currSlice);

  if (p_Vid->recovery_frame_num == (int) currSlice->frame_num && p_Vid->recovery_poc == 0x7fffffff)
    p_Vid->recovery_poc = currSlice->framepoc;

  if(currSlice->nal_reference_idc)
    p_Vid->last_ref_pic_poc = currSlice->framepoc;

  if (currSlice->structure==FRAME ||currSlice->structure==TOP_FIELD)
  {
    gettime (&(p_Vid->start_time));             // start time
  }

  dec_picture = p_Vid->dec_picture = alloc_storable_picture (p_Vid, currSlice->structure, p_Vid->width, p_Vid->height, p_Vid->width_cr, p_Vid->height_cr, 1);
  dec_picture->top_poc=currSlice->toppoc;
  dec_picture->bottom_poc=currSlice->bottompoc;
  dec_picture->frame_poc=currSlice->framepoc;
  dec_picture->qp = currSlice->qp;
  dec_picture->slice_qp_delta = currSlice->slice_qp_delta;
  dec_picture->chroma_qp_offset[0] = p_Vid->active_pps->chroma_qp_index_offset;
  dec_picture->chroma_qp_offset[1] = p_Vid->active_pps->second_chroma_qp_index_offset;
  dec_picture->iCodingType = currSlice->structure==FRAME? (currSlice->mb_aff_frame_flag? FRAME_MB_PAIR_CODING:FRAME_CODING): FIELD_CODING; //currSlice->slice_type;
  dec_picture->layer_id = currSlice->layer_id;
#if (MVC_EXTENSION_ENABLE)
  dec_picture->view_id         = currSlice->view_id;
  dec_picture->inter_view_flag = currSlice->inter_view_flag;
  dec_picture->anchor_pic_flag = currSlice->anchor_pic_flag;
  if (dec_picture->view_id == 1)
  {
    if((p_Vid->profile_idc == MVC_HIGH) || (p_Vid->profile_idc == STEREO_HIGH))
      init_mvc_picture(currSlice);
  }
#endif

  // reset all variables of the error concealment instance before decoding of every frame.
  // here the third parameter should, if perfectly, be equal to the number of slices per frame.
  // using little value is ok, the code will allocate more memory if the slice number is larger
#if (DISABLE_ERC == 0)
  ercReset(p_Vid->erc_errorVar, p_Vid->PicSizeInMbs, p_Vid->PicSizeInMbs, dec_picture->size_x);
#endif
  p_Vid->erc_mvperMB = 0;

  switch (currSlice->structure )
  {
  case TOP_FIELD:
    {
      dec_picture->poc = currSlice->toppoc;
      p_Vid->number *= 2;
      break;
    }
  case BOTTOM_FIELD:
    {
      dec_picture->poc = currSlice->bottompoc;
      p_Vid->number = p_Vid->number * 2 + 1;
      break;
    }
  case FRAME:
    {
      dec_picture->poc = currSlice->framepoc;
      break;
    }
  default:
    error("p_Vid->structure not initialized", 235);
  }

  if (p_Vid->type > SI_SLICE)
  {
    p_Vid->type = P_SLICE;  // concealed element
  }

  // CAVLC init
  if (p_Vid->active_pps->entropy_coding_mode_flag == (Boolean) CAVLC)
  {
    memset(p_Vid->nz_coeff[0][0][0], -1, p_Vid->PicSizeInMbs * 48 *sizeof(byte)); // 3 * 4 * 4
  }

  // Set the slice_nr member of each MB to -1, to ensure correct when packet loss occurs
  // TO set Macroblock Map (mark all MBs as 'have to be concealed')
  if( (p_Vid->separate_colour_plane_flag != 0) )
  {
    for( nplane=0; nplane<MAX_PLANE; ++nplane )
    {      
      Macroblock *currMB = p_Vid->mb_data_JV[nplane];
      char *intra_block = p_Vid->intra_block_JV[nplane];
      for(i=0; i<(int)p_Vid->PicSizeInMbs; ++i)
      {
        reset_mbs(currMB++);
      }
      memset(p_Vid->ipredmode_JV[nplane][0], DC_PRED, 16 * p_Vid->FrameHeightInMbs * p_Vid->PicWidthInMbs * sizeof(char));
      if(p_Vid->active_pps->constrained_intra_pred_flag)
      {
        for (i=0; i<(int)p_Vid->PicSizeInMbs; ++i)
        {
          intra_block[i] = 1;
        }
      }
    }
  }
  else
  {
    Macroblock *currMB = p_Vid->mb_data;
    for(i=0; i<(int)p_Vid->PicSizeInMbs; ++i)
      reset_mbs(currMB++);
    if(p_Vid->active_pps->constrained_intra_pred_flag)
    {
      for (i=0; i<(int)p_Vid->PicSizeInMbs; ++i)
      {
        p_Vid->intra_block[i] = 1;
      }
    }
    memset(p_Vid->ipredmode[0], DC_PRED, 16 * p_Vid->FrameHeightInMbs * p_Vid->PicWidthInMbs * sizeof(char));
  }  

  dec_picture->slice_type = p_Vid->type;
  dec_picture->used_for_reference = (currSlice->nal_reference_idc != 0);
  dec_picture->idr_flag = currSlice->idr_flag;
  dec_picture->no_output_of_prior_pics_flag = currSlice->no_output_of_prior_pics_flag;
  dec_picture->long_term_reference_flag     = currSlice->long_term_reference_flag;
  dec_picture->adaptive_ref_pic_buffering_flag = currSlice->adaptive_ref_pic_marking_mode_flag;

  dec_picture->dec_ref_pic_marking_buffer = currSlice->dec_ref_pic_marking_buffer;
  currSlice->dec_ref_pic_marking_buffer   = NULL;

  dec_picture->mb_aff_frame_flag = currSlice->mb_aff_frame_flag;
  dec_picture->PicWidthInMbs     = p_Vid->PicWidthInMbs;

  p_Vid->get_mb_block_pos = dec_picture->mb_aff_frame_flag ? get_mb_block_pos_mbaff : get_mb_block_pos_normal;
  p_Vid->getNeighbour     = dec_picture->mb_aff_frame_flag ? getAffNeighbour : getNonAffNeighbour;

  dec_picture->pic_num   = currSlice->frame_num;
  dec_picture->frame_num = currSlice->frame_num;

  dec_picture->recovery_frame = (unsigned int) ((int) currSlice->frame_num == p_Vid->recovery_frame_num);

  dec_picture->coded_frame = (currSlice->structure==FRAME);

  dec_picture->chroma_format_idc = active_sps->chroma_format_idc;

  dec_picture->frame_mbs_only_flag = active_sps->frame_mbs_only_flag;
  dec_picture->frame_cropping_flag = active_sps->frame_cropping_flag;

  if (dec_picture->frame_cropping_flag)
  {
    dec_picture->frame_crop_left_offset   = active_sps->frame_crop_left_offset;
    dec_picture->frame_crop_right_offset  = active_sps->frame_crop_right_offset;
    dec_picture->frame_crop_top_offset    = active_sps->frame_crop_top_offset;
    dec_picture->frame_crop_bottom_offset = active_sps->frame_crop_bottom_offset;
  }

#if (ENABLE_OUTPUT_TONEMAPPING)
  // store the necessary tone mapping sei into StorablePicture structure
  if (p_Vid->seiToneMapping->seiHasTone_mapping)
  {
    int coded_data_bit_max = (1 << p_Vid->seiToneMapping->coded_data_bit_depth);
    dec_picture->seiHasTone_mapping    = 1;
    dec_picture->tone_mapping_model_id = p_Vid->seiToneMapping->model_id;
    dec_picture->tonemapped_bit_depth  = p_Vid->seiToneMapping->sei_bit_depth;
    dec_picture->tone_mapping_lut      = (imgpel *)malloc(coded_data_bit_max * sizeof(int));
    if (NULL == dec_picture->tone_mapping_lut)
    {
      no_mem_exit("init_picture: tone_mapping_lut");
    }
    memcpy(dec_picture->tone_mapping_lut, p_Vid->seiToneMapping->lut, sizeof(imgpel) * coded_data_bit_max);
    update_tone_mapping_sei(p_Vid->seiToneMapping);
  }
  else
    dec_picture->seiHasTone_mapping = 0;
#endif

  if( (p_Vid->separate_colour_plane_flag != 0) )
  {
    p_Vid->dec_picture_JV[0] = p_Vid->dec_picture;
    p_Vid->dec_picture_JV[1] = alloc_storable_picture (p_Vid, (PictureStructure) currSlice->structure, p_Vid->width, p_Vid->height, p_Vid->width_cr, p_Vid->height_cr, 1);
    copy_dec_picture_JV( p_Vid, p_Vid->dec_picture_JV[1], p_Vid->dec_picture_JV[0] );
    p_Vid->dec_picture_JV[2] = alloc_storable_picture (p_Vid, (PictureStructure) currSlice->structure, p_Vid->width, p_Vid->height, p_Vid->width_cr, p_Vid->height_cr, 1);
    copy_dec_picture_JV( p_Vid, p_Vid->dec_picture_JV[2], p_Vid->dec_picture_JV[0] );
  }
}

static void update_mbaff_macroblock_data(imgpel **cur_img, imgpel (*temp)[16], int x0, int width, int height)
{
  imgpel (*temp_evn)[16] = temp;
  imgpel (*temp_odd)[16] = temp + height; 
  imgpel **temp_img = cur_img;
  int y;

  for (y = 0; y < 2 * height; ++y)
    memcpy(*temp++, (*temp_img++ + x0), width * sizeof(imgpel));

  for (y = 0; y < height; ++y)
  {
    memcpy((*cur_img++ + x0), *temp_evn++, width * sizeof(imgpel));
    memcpy((*cur_img++ + x0), *temp_odd++, width * sizeof(imgpel));
  }
}

static void MbAffPostProc(VideoParameters *p_Vid)
{

  imgpel temp_buffer[32][16];

  StorablePicture *dec_picture = p_Vid->dec_picture;
  imgpel ** imgY  = dec_picture->imgY;
  imgpel ***imgUV = dec_picture->imgUV;

  short i, x0, y0;

  for (i=0; i<(int)dec_picture->PicSizeInMbs; i+=2)
  {
    if (dec_picture->motion.mb_field[i])
    {
      get_mb_pos(p_Vid, i, p_Vid->mb_size[IS_LUMA], &x0, &y0);
      update_mbaff_macroblock_data(imgY + y0, temp_buffer, x0, MB_BLOCK_SIZE, MB_BLOCK_SIZE);

      if (dec_picture->chroma_format_idc != YUV400)
      {
        x0 = (short) ((x0 * p_Vid->mb_cr_size_x) >> 4);
        y0 = (short) ((y0 * p_Vid->mb_cr_size_y) >> 4);

        update_mbaff_macroblock_data(imgUV[0] + y0, temp_buffer, x0, p_Vid->mb_cr_size_x, p_Vid->mb_cr_size_y);
        update_mbaff_macroblock_data(imgUV[1] + y0, temp_buffer, x0, p_Vid->mb_cr_size_x, p_Vid->mb_cr_size_y);
      }
    }
  }
}


static void Error_tracking(VideoParameters *p_Vid, Slice *currSlice)
{
    if (currSlice->redundant_pic_cnt == 0)
        p_Vid->Is_primary_correct = p_Vid->Is_redundant_correct = 1;

    if (currSlice->redundant_pic_cnt == 0 && p_Vid->type != I_SLICE) {
        for (int i = 0; i < currSlice->num_ref_idx_l0_active_minus1 + 1 ; i++) {
            if (currSlice->ref_flag[i] == 0)  // any reference of primary slice is incorrect
                p_Vid->Is_primary_correct = 0; // primary slice is incorrect
        }
    } else if (currSlice->redundant_pic_cnt != 0 && p_Vid->type != I_SLICE) {
        if (currSlice->ref_flag[currSlice->redundant_slice_ref_idx] == 0)  // reference of redundant slice is incorrect
            p_Vid->Is_redundant_correct = 0;  // redundant slice is incorrect
    }
}

static Slice *malloc_slice(InputParameters *p_Inp, VideoParameters *p_Vid)
{
    int i, j, memory_size = 0;
    Slice *currSlice;

    currSlice = (Slice *)calloc(1, sizeof(Slice));
    if (!currSlice) {
        snprintf(errortext, ET_SIZE, "Memory allocation for Slice datastruct in NAL-mode %d failed", p_Inp->FileFormat);
        error(errortext,100);
    }

    // create all context models
    currSlice->mot_ctx = create_contexts_MotionInfo();
    currSlice->tex_ctx = create_contexts_TextureInfo();

    currSlice->max_part_nr = 3;  //! assume data partitioning (worst case) for the following mallocs()
    currSlice->partArr = AllocPartition(currSlice->max_part_nr);

    memory_size += get_mem3Dint(&(currSlice->wp_weight), 2, MAX_REFERENCE_PICTURES, 3);
    memory_size += get_mem3Dint(&(currSlice->wp_offset), 6, MAX_REFERENCE_PICTURES, 3);
    memory_size += get_mem4Dint(&(currSlice->wbp_weight), 6, MAX_REFERENCE_PICTURES, MAX_REFERENCE_PICTURES, 3);

    memory_size += get_mem3Dpel(&(currSlice->mb_pred), MAX_PLANE, MB_BLOCK_SIZE, MB_BLOCK_SIZE);
    memory_size += get_mem3Dpel(&(currSlice->mb_rec ), MAX_PLANE, MB_BLOCK_SIZE, MB_BLOCK_SIZE);
    memory_size += get_mem3Dint(&(currSlice->mb_rres), MAX_PLANE, MB_BLOCK_SIZE, MB_BLOCK_SIZE);
    memory_size += get_mem3Dint(&(currSlice->cof    ), MAX_PLANE, MB_BLOCK_SIZE, MB_BLOCK_SIZE);

    memory_size += get_mem2Dpel(&currSlice->tmp_block_l0, MB_BLOCK_SIZE, MB_BLOCK_SIZE);
    memory_size += get_mem2Dpel(&currSlice->tmp_block_l1, MB_BLOCK_SIZE, MB_BLOCK_SIZE);
    memory_size += get_mem2Dpel(&currSlice->tmp_block_l2, MB_BLOCK_SIZE, MB_BLOCK_SIZE);
    memory_size += get_mem2Dpel(&currSlice->tmp_block_l3, MB_BLOCK_SIZE, MB_BLOCK_SIZE);
    memory_size += get_mem2Dint(&currSlice->tmp_res, MB_BLOCK_SIZE + 5, MB_BLOCK_SIZE + 5);

#if (MVC_EXTENSION_ENABLE)
    currSlice->view_id = MVC_INIT_VIEW_ID;
    currSlice->inter_view_flag = 0;
    currSlice->anchor_pic_flag = 0;
#endif
    // reference flag initialization
    for (i = 0; i < 17; i++)
        currSlice->ref_flag[i] = 1;
    for (i = 0; i < 6; i++) {
        currSlice->listX[i] = (StorablePicture **)calloc(MAX_LIST_SIZE, sizeof (StorablePicture*)); // +1 for reordering
        if (!currSlice->listX[i])
            no_mem_exit("malloc_slice: currSlice->listX[i]");
    }
    for (j = 0; j < 6; j++) {
        for (i = 0; i < MAX_LIST_SIZE; i++)
            currSlice->listX[j][i] = NULL;
        currSlice->listXsize[j]=0;
    }

    return currSlice;
}

static void copy_slice_info(Slice *currSlice, OldSliceParams *p_old_slice)
{
    VideoParameters *p_Vid = currSlice->p_Vid;

    p_old_slice->pps_id         = currSlice->pic_parameter_set_id;
    p_old_slice->frame_num      = currSlice->frame_num;
    p_old_slice->field_pic_flag = currSlice->field_pic_flag;

    if (currSlice->field_pic_flag)
        p_old_slice->bottom_field_flag = currSlice->bottom_field_flag;

    p_old_slice->nal_ref_idc = currSlice->nal_reference_idc;
    p_old_slice->idr_flag    = (byte) currSlice->idr_flag;

    if (currSlice->idr_flag)
        p_old_slice->idr_pic_id = currSlice->idr_pic_id;

    if (p_Vid->active_sps->pic_order_cnt_type == 0) {
        p_old_slice->pic_oder_cnt_lsb          = currSlice->pic_order_cnt_lsb;
        p_old_slice->delta_pic_oder_cnt_bottom = currSlice->delta_pic_order_cnt_bottom;
    }

    if (p_Vid->active_sps->pic_order_cnt_type == 1) {
        p_old_slice->delta_pic_order_cnt[0] = currSlice->delta_pic_order_cnt[0];
        p_old_slice->delta_pic_order_cnt[1] = currSlice->delta_pic_order_cnt[1];
    }
#if (MVC_EXTENSION_ENABLE)
    p_old_slice->view_id = currSlice->view_id;
    p_old_slice->inter_view_flag = currSlice->inter_view_flag; 
    p_old_slice->anchor_pic_flag = currSlice->anchor_pic_flag;
#endif
    p_old_slice->layer_id = currSlice->layer_id;
}

/*!
 ***********************************************************************
 * \brief
 *    decodes one I- or P-frame
 *
 ***********************************************************************
 */
int decode_one_frame(DecoderParams *pDecoder)
{
    VideoParameters *p_Vid = pDecoder->p_Vid;
    InputParameters *p_Inp = p_Vid->p_Inp;
    int current_header, iRet;
    Slice *currSlice;
    Slice **ppSliceList = p_Vid->ppSliceList;
    //read one picture first;
    current_header = 0; 
    p_Vid->iSliceNumOfCurrPic = 0;
    p_Vid->num_dec_mb = 0;

    if (p_Vid->newframe) {
        if (p_Vid->pNextPPS->Valid) {
            MakePPSavailable(p_Vid, p_Vid->pNextPPS->pic_parameter_set_id, p_Vid->pNextPPS);
            p_Vid->pNextPPS->Valid = 0;
        }

        //get the first slice from currentslice;
        assert(ppSliceList[p_Vid->iSliceNumOfCurrPic]);
        currSlice = p_Vid->pNextSlice;
        p_Vid->pNextSlice = ppSliceList[p_Vid->iSliceNumOfCurrPic];
        ppSliceList[p_Vid->iSliceNumOfCurrPic] = currSlice;
        assert(currSlice->current_slice_nr == 0);

        UseParameterSet(currSlice);

        init_picture(p_Vid, currSlice, p_Inp);

        p_Vid->iSliceNumOfCurrPic++;
        current_header = SOS;
    }

    while (current_header != SOP && current_header != EOS) {
        //no pending slices;
        assert(p_Vid->iSliceNumOfCurrPic < p_Vid->iNumOfSlicesAllocated);
        if (!ppSliceList[p_Vid->iSliceNumOfCurrPic])
            ppSliceList[p_Vid->iSliceNumOfCurrPic] = malloc_slice(p_Inp, p_Vid);
        currSlice = ppSliceList[p_Vid->iSliceNumOfCurrPic];
        currSlice->p_Vid = p_Vid;
        currSlice->p_Inp = p_Inp;
        currSlice->p_Dpb = p_Vid->p_Dpb_layer[0]; //set default value;

        current_header = read_new_slice(currSlice);
        //init;
        currSlice->current_header = current_header;

        // error tracking of primary and redundant slices.
        Error_tracking(p_Vid, currSlice);
        // If primary and redundant are received and primary is correct, discard the redundant
        // else, primary slice will be replaced with redundant slice.
        if (currSlice->frame_num == p_Vid->previous_frame_num &&
            currSlice->redundant_pic_cnt != 0 &&
            p_Vid->Is_primary_correct != 0 && current_header != EOS)
            continue;

        if ((current_header != SOP && current_header != EOS) ||
            (p_Vid->iSliceNumOfCurrPic == 0 && current_header == SOP)) {
            currSlice->current_slice_nr = (short) p_Vid->iSliceNumOfCurrPic;
            p_Vid->dec_picture->max_slice_id = (short) imax(currSlice->current_slice_nr, p_Vid->dec_picture->max_slice_id);
            if (p_Vid->iSliceNumOfCurrPic > 0) {
                currSlice->framepoc  = (*ppSliceList)->framepoc;
                currSlice->toppoc    = (*ppSliceList)->toppoc;
                currSlice->bottompoc = (*ppSliceList)->bottompoc;  
                currSlice->ThisPOC   = (*ppSliceList)->ThisPOC;
            }
            p_Vid->iSliceNumOfCurrPic++;
            if (p_Vid->iSliceNumOfCurrPic >= p_Vid->iNumOfSlicesAllocated) {
                Slice **tmpSliceList = (Slice **)realloc(p_Vid->ppSliceList, (p_Vid->iNumOfSlicesAllocated+MAX_NUM_DECSLICES)*sizeof(Slice*));
                if (!tmpSliceList) {
                    tmpSliceList = (Slice **)calloc((p_Vid->iNumOfSlicesAllocated+MAX_NUM_DECSLICES), sizeof(Slice*));
                    memcpy(tmpSliceList, p_Vid->ppSliceList, p_Vid->iSliceNumOfCurrPic*sizeof(Slice*));
                    //free;
                    free(p_Vid->ppSliceList);
                    ppSliceList = p_Vid->ppSliceList = tmpSliceList;
                } else {
                    ppSliceList = p_Vid->ppSliceList = tmpSliceList;
                    memset(p_Vid->ppSliceList+p_Vid->iSliceNumOfCurrPic, 0, sizeof(Slice*)*MAX_NUM_DECSLICES);
                }
                p_Vid->iNumOfSlicesAllocated += MAX_NUM_DECSLICES;
            }
            current_header = SOS;       
        } else {
            p_Vid->newframe = 1;
            currSlice->current_slice_nr = 0;
            //keep it in currentslice;
            ppSliceList[p_Vid->iSliceNumOfCurrPic] = p_Vid->pNextSlice;
            p_Vid->pNextSlice = currSlice; 
        }

        copy_slice_info(currSlice, p_Vid->old_slice);
    }
    iRet = current_header;

    decode_picture(p_Vid);

#if MVC_EXTENSION_ENABLE
    p_Vid->last_dec_view_id = p_Vid->dec_picture->view_id;
#endif
    if (p_Vid->dec_picture->structure == FRAME)
        p_Vid->last_dec_poc = p_Vid->dec_picture->frame_poc;
    else if (p_Vid->dec_picture->structure == TOP_FIELD)
        p_Vid->last_dec_poc = p_Vid->dec_picture->top_poc;
    else if (p_Vid->dec_picture->structure == BOTTOM_FIELD)
        p_Vid->last_dec_poc = p_Vid->dec_picture->bottom_poc;
    exit_picture(p_Vid, &p_Vid->dec_picture);
    p_Vid->previous_frame_num = ppSliceList[0]->frame_num;
    return (iRet);
}

/*!
 ************************************************************************
 * \brief
 *    Convert file read buffer to source picture structure
 * \param imgX
 *    Pointer to image plane
 * \param buf
 *    Buffer for file output
 * \param size_x
 *    horizontal image size in pixel
 * \param size_y
 *    vertical image size in pixel
 * \param symbol_size_in_bytes
 *    number of bytes used per pel
 ************************************************************************
 */
static void buffer2img (imgpel** imgX, unsigned char* buf, int size_x, int size_y, int symbol_size_in_bytes)
{
  int i,j;

  uint16 tmp16, ui16;
  unsigned long  tmp32, ui32;

  if (symbol_size_in_bytes> sizeof(imgpel))
  {
    error ("Source picture has higher bit depth than imgpel data type. \nPlease recompile with larger data type for imgpel.", 500);
  }

  if (( sizeof(char) == sizeof (imgpel)) && ( sizeof(char) == symbol_size_in_bytes))
  {
    // imgpel == pixel_in_file == 1 byte -> simple copy
    memcpy(&imgX[0][0], buf, size_x * size_y);
  }
  else
  {
    // sizeof (imgpel) > sizeof(char)
    if (testEndian())
    {
      // big endian
      switch (symbol_size_in_bytes)
      {
      case 1:
        {
          for(j = 0; j < size_y; ++j)
            for(i = 0; i < size_x; ++i)
            {
              imgX[j][i]= buf[i+j*size_x];
            }
          break;
        }
      case 2:
        {
          for(j=0;j<size_y;++j)
            for(i=0;i<size_x;++i)
            {
              memcpy(&tmp16, buf+((i+j*size_x)*2), 2);
              ui16  = (uint16) ((tmp16 >> 8) | ((tmp16&0xFF)<<8));
              imgX[j][i] = (imgpel) ui16;
            }
          break;
        }
      case 4:
        {
          for(j=0;j<size_y;++j)
            for(i=0;i<size_x;++i)
            {
              memcpy(&tmp32, buf+((i+j*size_x)*4), 4);
              ui32  = ((tmp32&0xFF00)<<8) | ((tmp32&0xFF)<<24) | ((tmp32&0xFF0000)>>8) | ((tmp32&0xFF000000)>>24);
              imgX[j][i] = (imgpel) ui32;
            }
        }
      default:
        {
           error ("reading only from formats of 8, 16 or 32 bit allowed on big endian architecture", 500);
           break;
        }
      }

    }
    else
    {
      // little endian
      if (symbol_size_in_bytes == 1)
      {
        for (j=0; j < size_y; ++j)
        {
          for (i=0; i < size_x; ++i)
          {
            imgX[j][i]=*(buf++);
          }
        }
      }
      else
      {
        for (j=0; j < size_y; ++j)
        {
          int jpos = j*size_x;
          for (i=0; i < size_x; ++i)
          {
            imgX[j][i]=0;
            memcpy(&(imgX[j][i]), buf +((i+jpos)*symbol_size_in_bytes), symbol_size_in_bytes);
          }
        }
      }

    }
  }
}


/*!
 ***********************************************************************
 * \brief
 *    compute generic SSE
 ***********************************************************************
 */
static int64 compute_SSE(imgpel **imgRef, imgpel **imgSrc, int xRef, int xSrc, int ySize, int xSize)
{
  int i, j;
  imgpel *lineRef, *lineSrc;
  int64 distortion = 0;

  for (j = 0; j < ySize; j++)
  {
    lineRef = &imgRef[j][xRef];    
    lineSrc = &imgSrc[j][xSrc];

    for (i = 0; i < xSize; i++)
      distortion += iabs2( *lineRef++ - *lineSrc++ );
  }
  return distortion;
}

/*!
 ************************************************************************
 * \brief
 *    Calculate the value of frame_no
 ************************************************************************
*/
void calculate_frame_no(VideoParameters *p_Vid, StorablePicture *p)
{
  InputParameters *p_Inp = p_Vid->p_Inp;
  // calculate frame number
  int  psnrPOC = p_Vid->active_sps->mb_adaptive_frame_field_flag ? p->poc /(p_Inp->poc_scale) : p->poc/(p_Inp->poc_scale);
  
  if (psnrPOC==0)// && p_Vid->psnr_number)
  {
    p_Vid->idr_psnr_number = p_Vid->g_nFrame * p_Vid->ref_poc_gap/(p_Inp->poc_scale);
  }
  p_Vid->psnr_number = imax(p_Vid->psnr_number, p_Vid->idr_psnr_number+psnrPOC);

  p_Vid->frame_no = p_Vid->idr_psnr_number + psnrPOC;
}


/*!
************************************************************************
* \brief
*    Find PSNR for all three components.Compare decoded frame with
*    the original sequence. Read p_Inp->jumpd frames to reflect frame skipping.
* \param p_Vid
*      video encoding parameters for current picture
* \param p
*      picture to be compared
* \param p_ref
*      file pointer piont to reference YUV reference file
************************************************************************
*/
void find_snr(VideoParameters *p_Vid, 
              StorablePicture *p,
              int *p_ref)
{
  InputParameters *p_Inp = p_Vid->p_Inp;
  SNRParameters   *snr   = p_Vid->snr;

  int k;
  int ret;
  int64 diff_comp[3] = {0};
  int64  status;
  int symbol_size_in_bytes = (p_Vid->pic_unit_bitsize_on_disk >> 3);
  int comp_size_x[3], comp_size_y[3];
  int64 framesize_in_bytes;

  unsigned int max_pix_value_sqd[3];

  Boolean rgb_output = (Boolean) (p_Vid->active_sps->vui_parameters.matrix_coefficients==0);
  unsigned char *buf;
  imgpel **cur_ref [3];
  imgpel **cur_comp[3]; 
  // picture error concealment
  char yuv_types[4][6]= {"4:0:0","4:2:0","4:2:2","4:4:4"};

  max_pix_value_sqd[0] = iabs2(p_Vid->max_pel_value_comp[0]);
  max_pix_value_sqd[1] = iabs2(p_Vid->max_pel_value_comp[1]);
  max_pix_value_sqd[2] = iabs2(p_Vid->max_pel_value_comp[2]);

  cur_ref[0]  = p_Vid->imgY_ref;
  cur_ref[1]  = p->chroma_format_idc != YUV400 ? p_Vid->imgUV_ref[0] : NULL;
  cur_ref[2]  = p->chroma_format_idc != YUV400 ? p_Vid->imgUV_ref[1] : NULL;

  cur_comp[0] = p->imgY;
  cur_comp[1] = p->chroma_format_idc != YUV400 ? p->imgUV[0]  : NULL;
  cur_comp[2] =  p->chroma_format_idc!= YUV400 ? p->imgUV[1]  : NULL; 

  comp_size_x[0] = p_Inp->source.width[0];
  comp_size_y[0] = p_Inp->source.height[0];
  comp_size_x[1] = comp_size_x[2] = p_Inp->source.width[1];
  comp_size_y[1] = comp_size_y[2] = p_Inp->source.height[1];

  framesize_in_bytes = (((int64) comp_size_x[0] * comp_size_y[0]) + ((int64) comp_size_x[1] * comp_size_y[1] ) * 2) * symbol_size_in_bytes;

  // KS: this buffer should actually be allocated only once, but this is still much faster than the previous version
  buf = (unsigned char *)malloc ( comp_size_x[0] * comp_size_y[0] * symbol_size_in_bytes );

  if (NULL == buf)
  {
    no_mem_exit("find_snr: buf");
  }

  status = lseek (*p_ref, framesize_in_bytes * p_Vid->frame_no, SEEK_SET);
  if (status == -1)
  {
    fprintf(stderr, "Warning: Could not seek to frame number %d in reference file. Shown PSNR might be wrong.\n", p_Vid->frame_no);
    free (buf);
    return;
  }

  if(rgb_output)
    lseek (*p_ref, framesize_in_bytes/3, SEEK_CUR);

  for (k = 0; k < ((p->chroma_format_idc != YUV400) ? 3 : 1); ++k)
  {

    if(rgb_output && k == 2)
      lseek (*p_ref, -framesize_in_bytes, SEEK_CUR);

    ret = read(*p_ref, buf, comp_size_x[k] * comp_size_y[k] * symbol_size_in_bytes);
    if (ret != comp_size_x[k] * comp_size_y[k] * symbol_size_in_bytes)
    {
      printf ("Warning: could not read from reconstructed file\n");
      memset (buf, 0, comp_size_x[k] * comp_size_y[k] * symbol_size_in_bytes);
      close(*p_ref);
      *p_ref = -1;
      break;
    }
    buffer2img(cur_ref[k], buf, comp_size_x[k], comp_size_y[k], symbol_size_in_bytes);

    // Compute SSE
    diff_comp[k] = compute_SSE(cur_ref[k], cur_comp[k], 0, 0, comp_size_y[k], comp_size_x[k]);

    // Collecting SNR statistics
    snr->snr[k] = psnr( max_pix_value_sqd[k], comp_size_x[k] * comp_size_y[k], (float) diff_comp[k]);   

    if (snr->frame_ctr == 0) // first
    {
      snr->snra[k] = snr->snr[k];                                                        // keep luma snr for first frame
    }
    else
    {
      snr->snra[k] = (float)(snr->snra[k]*(snr->frame_ctr)+snr->snr[k])/(snr->frame_ctr + 1); // average snr chroma for all frames
    }
  }

  if(rgb_output)
    lseek (*p_ref, framesize_in_bytes * 2 / 3, SEEK_CUR);

  free (buf);

  // picture error concealment
  if(p->concealed_pic)
  {
    fprintf(stdout,"%04d(P)  %8d %5d %5d %7.4f %7.4f %7.4f  %s %5d\n",
      p_Vid->frame_no, p->frame_poc, p->pic_num, p->qp,
      snr->snr[0], snr->snr[1], snr->snr[2], yuv_types[p->chroma_format_idc], 0);
  }
}





void pad_buf(imgpel *pImgBuf, int iWidth, int iHeight, int iStride, int iPadX, int iPadY)
{
  int j;
  imgpel *pLine0 = pImgBuf - iPadX, *pLine;
  int i;
  for(i=-iPadX; i<0; i++)
    pImgBuf[i] = *pImgBuf;
  for(i=0; i<iPadX; i++)
    pImgBuf[i+iWidth] = *(pImgBuf+iWidth-1);

  for(j=-iPadY; j<0; j++)
    memcpy(pLine0+j*iStride, pLine0, iStride*sizeof(imgpel));
  for(j=1; j<iHeight; j++)
  {
    pLine = pLine0 + j*iStride;
    for(i=0; i<iPadX; i++)
      pLine[i] = pLine[iPadX];
    pLine += iPadX+iWidth-1;
    for(i=1; i<iPadX+1; i++)
      pLine[i] = *pLine;
  }
  pLine = pLine0 + (iHeight-1)*iStride;
  for(j=iHeight; j<iHeight+iPadY; j++)
    memcpy(pLine0+j*iStride,  pLine, iStride*sizeof(imgpel));
}

void pad_dec_picture(VideoParameters *p_Vid, StorablePicture *dec_picture)
{
  int iPadX = p_Vid->iLumaPadX;
  int iPadY = p_Vid->iLumaPadY;
  int iWidth = dec_picture->size_x;
  int iHeight = dec_picture->size_y;
  int iStride = dec_picture->iLumaStride;

  pad_buf(*dec_picture->imgY, iWidth, iHeight, iStride, iPadX, iPadY);

  if(dec_picture->chroma_format_idc != YUV400) 
  {
    iPadX = p_Vid->iChromaPadX;
    iPadY = p_Vid->iChromaPadY;
    iWidth = dec_picture->size_x_cr;
    iHeight = dec_picture->size_y_cr;
    iStride = dec_picture->iChromaStride;
    pad_buf(*dec_picture->imgUV[0], iWidth, iHeight, iStride, iPadX, iPadY);
    pad_buf(*dec_picture->imgUV[1], iWidth, iHeight, iStride, iPadX, iPadY);
  }
}


/*!
 ************************************************************************
 * \brief
 *    finish decoding of a picture, conceal errors and store it
 *    into the DPB
 ************************************************************************
 */
void exit_picture(VideoParameters *p_Vid, StorablePicture **dec_picture)
{
  InputParameters *p_Inp = p_Vid->p_Inp;
  SNRParameters   *snr   = p_Vid->snr;
  char yuv_types[4][6]= {"4:0:0","4:2:0","4:2:2","4:4:4"};
#if (DISABLE_ERC == 0)
  int ercStartMB;
  int ercSegment;
  frame recfr;
#endif
  int structure, frame_poc, slice_type, refpic, qp, pic_num, chroma_format_idc, is_idr;

  int64 tmp_time;                   // time used by decoding the last frame
  char   yuvFormat[10];

  // return if the last picture has already been finished
  if (*dec_picture==NULL || (p_Vid->num_dec_mb != p_Vid->PicSizeInMbs && (p_Vid->yuv_format != YUV444 || !p_Vid->separate_colour_plane_flag)))
  {
    return;
  }

#if (DISABLE_ERC == 0)
  recfr.p_Vid = p_Vid;
  recfr.yptr = &(*dec_picture)->imgY[0][0];
  if ((*dec_picture)->chroma_format_idc != YUV400)
  {
    recfr.uptr = &(*dec_picture)->imgUV[0][0][0];
    recfr.vptr = &(*dec_picture)->imgUV[1][0][0];
  }

  //! this is always true at the beginning of a picture
  ercStartMB = 0;
  ercSegment = 0;

  //! mark the start of the first segment
  if (!(*dec_picture)->mb_aff_frame_flag)
  {
    int i;
    ercStartSegment(0, ercSegment, 0 , p_Vid->erc_errorVar);
    //! generate the segments according to the macroblock map
    for(i = 1; i < (int) (*dec_picture)->PicSizeInMbs; ++i)
    {
      if(p_Vid->mb_data[i].ei_flag != p_Vid->mb_data[i-1].ei_flag)
      {
        ercStopSegment(i-1, ercSegment, 0, p_Vid->erc_errorVar); //! stop current segment

        //! mark current segment as lost or OK
        if(p_Vid->mb_data[i-1].ei_flag)
          ercMarkCurrSegmentLost((*dec_picture)->size_x, p_Vid->erc_errorVar);
        else
          ercMarkCurrSegmentOK((*dec_picture)->size_x, p_Vid->erc_errorVar);

        ++ercSegment;  //! next segment
        ercStartSegment(i, ercSegment, 0 , p_Vid->erc_errorVar); //! start new segment
        ercStartMB = i;//! save start MB for this segment
      }
    }
    //! mark end of the last segment
    ercStopSegment((*dec_picture)->PicSizeInMbs-1, ercSegment, 0, p_Vid->erc_errorVar);
    if(p_Vid->mb_data[i-1].ei_flag)
      ercMarkCurrSegmentLost((*dec_picture)->size_x, p_Vid->erc_errorVar);
    else
      ercMarkCurrSegmentOK((*dec_picture)->size_x, p_Vid->erc_errorVar);

    //! call the right error concealment function depending on the frame type.
    p_Vid->erc_mvperMB /= (*dec_picture)->PicSizeInMbs;

    p_Vid->erc_img = p_Vid;

    if((*dec_picture)->slice_type == I_SLICE || (*dec_picture)->slice_type == SI_SLICE) // I-frame
      ercConcealIntraFrame(p_Vid, &recfr, (*dec_picture)->size_x, (*dec_picture)->size_y, p_Vid->erc_errorVar);
    else
      ercConcealInterFrame(&recfr, p_Vid->erc_object_list, (*dec_picture)->size_x, (*dec_picture)->size_y, p_Vid->erc_errorVar, (*dec_picture)->chroma_format_idc);
  }
#endif

  pic_deblock(p_Vid, *dec_picture);

  if ((*dec_picture)->mb_aff_frame_flag)
    MbAffPostProc(p_Vid);

  if (p_Vid->structure != FRAME)
    p_Vid->number /= 2;
#if (MVC_EXTENSION_ENABLE)
  if((*dec_picture)->used_for_reference || ((*dec_picture)->inter_view_flag == 1))
    pad_dec_picture(p_Vid, *dec_picture);
#else
  if((*dec_picture)->used_for_reference)
    pad_dec_picture(p_Vid, *dec_picture);
#endif
  structure  = (*dec_picture)->structure;
  slice_type = (*dec_picture)->slice_type;
  frame_poc  = (*dec_picture)->frame_poc;  
  refpic     = (*dec_picture)->used_for_reference;
  qp         = (*dec_picture)->qp;
  pic_num    = (*dec_picture)->pic_num;
  is_idr     = (*dec_picture)->idr_flag;

  chroma_format_idc = (*dec_picture)->chroma_format_idc;
#if MVC_EXTENSION_ENABLE
  store_picture_in_dpb(p_Vid->p_Dpb_layer[(*dec_picture)->view_id], *dec_picture);
#else
  store_picture_in_dpb(p_Vid->p_Dpb_layer[0], *dec_picture);
#endif

  *dec_picture=NULL;

  if (p_Vid->last_has_mmco_5)
  {
    p_Vid->pre_frame_num = 0;
  }

  if (p_Inp->silent == FALSE)
  {
    if (structure==TOP_FIELD || structure==FRAME)
    {
      if(slice_type == I_SLICE && is_idr) // IDR picture
        strcpy(p_Vid->cslice_type,"IDR");
      else if(slice_type == I_SLICE) // I picture
        strcpy(p_Vid->cslice_type," I ");
      else if(slice_type == P_SLICE) // P pictures
        strcpy(p_Vid->cslice_type," P ");
      else if(slice_type == SP_SLICE) // SP pictures
        strcpy(p_Vid->cslice_type,"SP ");
      else if (slice_type == SI_SLICE)
        strcpy(p_Vid->cslice_type,"SI ");
      else if(refpic) // stored B pictures
        strcpy(p_Vid->cslice_type," B ");
      else // B pictures
        strcpy(p_Vid->cslice_type," b ");

      if (structure==FRAME)
      {
        strncat(p_Vid->cslice_type,")       ",8-strlen(p_Vid->cslice_type));
      }
    }
    else if (structure==BOTTOM_FIELD)
    {
      if(slice_type == I_SLICE && is_idr) // IDR picture
        strncat(p_Vid->cslice_type,"|IDR)",8-strlen(p_Vid->cslice_type));
      else if(slice_type == I_SLICE) // I picture
        strncat(p_Vid->cslice_type,"| I )",8-strlen(p_Vid->cslice_type));
      else if(slice_type == P_SLICE) // P pictures
        strncat(p_Vid->cslice_type,"| P )",8-strlen(p_Vid->cslice_type));
      else if(slice_type == SP_SLICE) // SP pictures
        strncat(p_Vid->cslice_type,"|SP )",8-strlen(p_Vid->cslice_type));
      else if (slice_type == SI_SLICE)
        strncat(p_Vid->cslice_type,"|SI )",8-strlen(p_Vid->cslice_type));
      else if(refpic) // stored B pictures
        strncat(p_Vid->cslice_type,"| B )",8-strlen(p_Vid->cslice_type));
      else // B pictures
        strncat(p_Vid->cslice_type,"| b )",8-strlen(p_Vid->cslice_type));   
    }
  }

  if ((structure==FRAME)||structure==BOTTOM_FIELD)
  {
    gettime (&(p_Vid->end_time));              // end time

    tmp_time  = timediff(&(p_Vid->start_time), &(p_Vid->end_time));
    p_Vid->tot_time += tmp_time;
    tmp_time  = timenorm(tmp_time);
    sprintf(yuvFormat,"%s", yuv_types[chroma_format_idc]);

    if (p_Inp->silent == FALSE)
    {
      SNRParameters   *snr = p_Vid->snr;
      if (p_Vid->p_ref != -1)
        fprintf(stdout,"%05d(%s%5d %5d %5d %8.4f %8.4f %8.4f  %s %7d\n",
        p_Vid->frame_no, p_Vid->cslice_type, frame_poc, pic_num, qp, snr->snr[0], snr->snr[1], snr->snr[2], yuvFormat, (int) tmp_time);
      else
        fprintf(stdout,"%05d(%s%5d %5d %5d                             %s %7d\n",
        p_Vid->frame_no, p_Vid->cslice_type, frame_poc, pic_num, qp, yuvFormat, (int)tmp_time);
    }
    else
      fprintf(stdout,"Completed Decoding frame %05d.\r",snr->frame_ctr);

    fflush(stdout);

    if(slice_type == I_SLICE || slice_type == SI_SLICE || slice_type == P_SLICE || refpic)   // I or P pictures
    {
#if (MVC_EXTENSION_ENABLE)
      if((p_Vid->ppSliceList[0])->view_id!=0)
#endif
        ++(p_Vid->number);
    }
    else
      ++(p_Vid->Bframe_ctr);    // B pictures
    ++(snr->frame_ctr);

#if (MVC_EXTENSION_ENABLE)
    if ((p_Vid->ppSliceList[0])->view_id != 0)
#endif
      ++(p_Vid->g_nFrame);   
  }
}




#if (MVC_EXTENSION_ENABLE)
int GetVOIdx(VideoParameters *p_Vid, int iViewId)
{
  int iVOIdx = -1;
  int *piViewIdMap;
  if(p_Vid->active_subset_sps)
  {
    piViewIdMap = p_Vid->active_subset_sps->view_id;
    for(iVOIdx = p_Vid->active_subset_sps->num_views_minus1; iVOIdx>=0; iVOIdx--)
      if(piViewIdMap[iVOIdx] == iViewId)
        break;
  }
  else
  {
    sub_sps_t *curr_subset_sps;
    int i;

    curr_subset_sps = p_Vid->SubsetSeqParSet;
    for(i=0; i<MAXSPS; i++)
    {
      if(curr_subset_sps->num_views_minus1>=0 && curr_subset_sps->sps.Valid)
      {
        break;
      }
      curr_subset_sps++;
    }

    if( i < MAXSPS )
    {
      p_Vid->active_subset_sps = curr_subset_sps;

      piViewIdMap = p_Vid->active_subset_sps->view_id;
      for(iVOIdx = p_Vid->active_subset_sps->num_views_minus1; iVOIdx>=0; iVOIdx--)
        if(piViewIdMap[iVOIdx] == iViewId)
          break;

      return iVOIdx;
    }
    else
    {
      iVOIdx = 0;
    }
  }

  return iVOIdx;
}

#endif