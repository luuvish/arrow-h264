
/*!
 **************************************************************************************
 * \file
 *    parset.h
 * \brief
 *    Picture and Sequence Parameter Sets, decoder operations
 * 
 * \date 25 November 2002
 * \author
 *    Main contributors (see contributors.h for copyright, address and affiliation details)
 *      - Stephan Wenger        <stewe@cs.tu-berlin.de>
 ***************************************************************************************
 */


#ifndef _PARSET_H_
#define _PARSET_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "bitstream_nal.h"

#include "defines.h"

#define MAXIMUMPARSETRBSPSIZE   1500
#define MAXIMUMPARSETNALUSIZE   1500

#define MAXSPS  32
#define MAXPPS  256

#define MAXIMUMVALUEOFcpb_cnt   32
typedef struct hrd_parameters_t {
    unsigned int     cpb_cnt_minus1;                                   // ue(v)
    unsigned int     bit_rate_scale;                                   // u(4)
    unsigned int     cpb_size_scale;                                   // u(4)
    unsigned int     bit_rate_value_minus1[MAXIMUMVALUEOFcpb_cnt];    // ue(v)
    unsigned int     cpb_size_value_minus1[MAXIMUMVALUEOFcpb_cnt];    // ue(v)
    unsigned int     cbr_flag             [MAXIMUMVALUEOFcpb_cnt];    // u(1)
    unsigned int     initial_cpb_removal_delay_length_minus1;          // u(5)
    unsigned int     cpb_removal_delay_length_minus1;                  // u(5)
    unsigned int     dpb_output_delay_length_minus1;                   // u(5)
    unsigned int     time_offset_length;                               // u(5)
} hrd_parameters_t;

typedef struct vui_parameters_t {
    Boolean          aspect_ratio_info_present_flag;                   // u(1)
    unsigned int     aspect_ratio_idc;                                 // u(8)
    unsigned short   sar_width;                                        // u(16)
    unsigned short   sar_height;                                       // u(16)
    Boolean          overscan_info_present_flag;                       // u(1)
    Boolean          overscan_appropriate_flag;                        // u(1)
    Boolean          video_signal_type_present_flag;                   // u(1)
    unsigned int     video_format;                                     // u(3)
    Boolean          video_full_range_flag;                            // u(1)
    Boolean          colour_description_present_flag;                  // u(1)
    unsigned int     colour_primaries;                                 // u(8)
    unsigned int     transfer_characteristics;                         // u(8)
    unsigned int     matrix_coefficients;                              // u(8)
    Boolean          chroma_loc_info_present_flag;                     // u(1)
    unsigned int     chroma_sample_loc_type_top_field;                 // ue(v)
    unsigned int     chroma_sample_loc_type_bottom_field;              // ue(v)
    Boolean          timing_info_present_flag;                         // u(1)
    unsigned int     num_units_in_tick;                                // u(32)
    unsigned int     time_scale;                                       // u(32)
    Boolean          fixed_frame_rate_flag;                            // u(1)
    Boolean          nal_hrd_parameters_present_flag;                  // u(1)
    hrd_parameters_t nal_hrd_parameters;                               // hrd_paramters_t
    Boolean          vcl_hrd_parameters_present_flag;                  // u(1)
    hrd_parameters_t vcl_hrd_parameters;                               // hrd_paramters_t
    Boolean          low_delay_hrd_flag;                               // u(1)
    Boolean          pic_struct_present_flag;                          // u(1)
    Boolean          bitstream_restriction_flag;                       // u(1)
    Boolean          motion_vectors_over_pic_boundaries_flag;          // u(1)
    unsigned int     max_bytes_per_pic_denom;                          // ue(v)
    unsigned int     max_bits_per_mb_denom;                            // ue(v)
    unsigned int     log2_max_mv_length_horizontal;                    // ue(v)
    unsigned int     log2_max_mv_length_vertical;                      // ue(v)
    unsigned int     max_num_reorder_frames;                           // ue(v)
    unsigned int     max_dec_frame_buffering;                          // ue(v)
} vui_parameters_t;


#define MAXnum_ref_frames_in_pic_order_cnt_cycle  256
typedef struct seq_parameter_set_rbsp_t {
    Boolean          Valid;                  // indicates the parameter set is valid

    unsigned int     profile_idc;                                      // u(8)
    Boolean          constraint_set0_flag;                             // u(1)
    Boolean          constraint_set1_flag;                             // u(1)
    Boolean          constraint_set2_flag;                             // u(1)
    Boolean          constraint_set3_flag;                             // u(1)
    Boolean          constraint_set4_flag;                             // u(1)
    Boolean          constraint_set5_flag;                             // u(2)
    unsigned int     level_idc;                                        // u(8)
    unsigned int     seq_parameter_set_id;                             // ue(v)

    unsigned int     chroma_format_idc;                                // ue(v)
    unsigned         separate_colour_plane_flag;                       // u(1)
    unsigned int     bit_depth_luma_minus8;                            // ue(v)
    unsigned int     bit_depth_chroma_minus8;                          // ue(v)
    int              qpprime_y_zero_transform_bypass_flag;
    Boolean          seq_scaling_matrix_present_flag;                   // u(1)
    int              seq_scaling_list_present_flag[12];                 // u(1)
    int              ScalingList4x4[6][16];                             // se(v)
    int              ScalingList8x8[6][64];                             // se(v)
    Boolean          UseDefaultScalingMatrix4x4Flag[6];
    Boolean          UseDefaultScalingMatrix8x8Flag[6];

    unsigned int     log2_max_frame_num_minus4;                        // ue(v)
    unsigned int     pic_order_cnt_type;
    unsigned int     log2_max_pic_order_cnt_lsb_minus4;              // ue(v)
    Boolean          delta_pic_order_always_zero_flag;               // u(1)
    int              offset_for_non_ref_pic;                         // se(v)
    int              offset_for_top_to_bottom_field;                 // se(v)
    unsigned int     num_ref_frames_in_pic_order_cnt_cycle;          // ue(v)
    int              offset_for_ref_frame[MAXnum_ref_frames_in_pic_order_cnt_cycle]; // se(v)
    unsigned int     max_num_ref_frames;                               // ue(v)
    Boolean          gaps_in_frame_num_value_allowed_flag;             // u(1)

    unsigned int     pic_width_in_mbs_minus1;                          // ue(v)
    unsigned int     pic_height_in_map_units_minus1;                   // ue(v)
    Boolean          frame_mbs_only_flag;                              // u(1)
    Boolean          mb_adaptive_frame_field_flag;                   // u(1)
    Boolean          direct_8x8_inference_flag;                      // u(1)
    Boolean          frame_cropping_flag;                            // u(1)
    unsigned int     frame_crop_left_offset;                         // ue(v)
    unsigned int     frame_crop_right_offset;                        // ue(v)
    unsigned int     frame_crop_top_offset;                          // ue(v)
    unsigned int     frame_crop_bottom_offset;                       // ue(v)
    Boolean          vui_parameters_present_flag;                    // u(1)
    vui_parameters_t vui_seq_parameters;                  // vui_parameters_t



#if (MVC_EXTENSION_ENABLE)
    int max_dec_frame_buffering;
#endif
} seq_parameter_set_rbsp_t;



#if (MVC_EXTENSION_ENABLE)
typedef struct mvcvui_tag {
    int   num_ops_minus1;
    char *temporal_id;
    int  *num_target_output_views_minus1;
    int **view_id;
    char *timing_info_present_flag;
    int  *num_units_in_tick;
    int  *time_scale;
    char *fixed_frame_rate_flag;
    char *nal_hrd_parameters_present_flag;
    char *vcl_hrd_parameters_present_flag;
    char *low_delay_hrd_flag;
    char *pic_struct_present_flag;

    //hrd parameters;
    char cpb_cnt_minus1;
    char bit_rate_scale;
    char cpb_size_scale;
    int  bit_rate_value_minus1[32];
    int  cpb_size_value_minus1[32];
    char cbr_flag[32];
    char initial_cpb_removal_delay_length_minus1;
    char cpb_removal_delay_length_minus1;
    char dpb_output_delay_length_minus1;
    char time_offset_length;
} MVCVUI_t;

typedef struct subset_seq_parameter_set_rbsp_t {
    Boolean      Valid;                  // indicates the parameter set is valid

    seq_parameter_set_rbsp_t sps;

    unsigned int bit_equal_to_one;
    int          num_views_minus1;
    int         *view_id;
    int         *num_anchor_refs_l0;
    int        **anchor_ref_l0;
    int         *num_anchor_refs_l1;
    int        **anchor_ref_l1;

    int         *num_non_anchor_refs_l0;
    int        **non_anchor_ref_l0;
    int         *num_non_anchor_refs_l1;
    int        **non_anchor_ref_l1;
   
    int          num_level_values_signalled_minus1;
    int         *level_idc;
    int         *num_applicable_ops_minus1;
    int        **applicable_op_temporal_id;
    int        **applicable_op_num_target_views_minus1;
    int       ***applicable_op_target_view_id;
    int        **applicable_op_num_views_minus1;

    unsigned int mvc_vui_parameters_present_flag;
    MVCVUI_t     MVCVUIParams;
} subset_seq_parameter_set_rbsp_t;
#endif



#define MAXnum_slice_groups_minus1  8
typedef struct pic_parameter_set_rbsp_t {
    Boolean          Valid;                  // indicates the parameter set is valid

    unsigned int     pic_parameter_set_id;                             // ue(v)
    unsigned int     seq_parameter_set_id;                             // ue(v)
    Boolean          entropy_coding_mode_flag;                         // u(1)
    Boolean          bottom_field_pic_order_in_frame_present_flag;                           // u(1)

    unsigned int     num_slice_groups_minus1;                       // ue(v)
    unsigned int     slice_group_map_type;                          // ue(v)
    unsigned int     run_length_minus1[MAXnum_slice_groups_minus1]; // ue(v)
    unsigned int     top_left[MAXnum_slice_groups_minus1];          // ue(v)
    unsigned int     bottom_right[MAXnum_slice_groups_minus1];      // ue(v)
    Boolean          slice_group_change_direction_flag;             // u(1)
    unsigned int     slice_group_change_rate_minus1;                // ue(v)
    unsigned int     pic_size_in_map_units_minus1;                  // ue(v)
    byte            *slice_group_id;                                // complete MBAmap u(v)

    int              num_ref_idx_l0_default_active_minus1;          // ue(v)
    int              num_ref_idx_l1_default_active_minus1;          // ue(v)
    Boolean          weighted_pred_flag;                            // u(1)
    unsigned int     weighted_bipred_idc;                           // u(2)
    int              pic_init_qp_minus26;                           // se(v)
    int              pic_init_qs_minus26;                           // se(v)
    int              chroma_qp_index_offset;                        // se(v)
    Boolean          deblocking_filter_control_present_flag;           // u(1)
    Boolean          constrained_intra_pred_flag;                      // u(1)
    Boolean          redundant_pic_cnt_present_flag;                   // u(1)

    Boolean          transform_8x8_mode_flag;                          // u(1)
    Boolean          pic_scaling_matrix_present_flag;                  // u(1)
    int              pic_scaling_list_present_flag[12];                // u(1)
    int              ScalingList4x4[6][16];                            // se(v)
    int              ScalingList8x8[6][64];                            // se(v)
    Boolean          UseDefaultScalingMatrix4x4Flag[6];
    Boolean          UseDefaultScalingMatrix8x8Flag[6];
    int              second_chroma_qp_index_offset;                    // se(v)
} pic_parameter_set_rbsp_t;


struct video_par;
struct slice_t;

pic_parameter_set_rbsp_t *AllocPPS (void);
seq_parameter_set_rbsp_t *AllocSPS (void);

void FreePPS (pic_parameter_set_rbsp_t *pps);
void FreeSPS (seq_parameter_set_rbsp_t *sps);

void MakePPSavailable (struct video_par *p_Vid, int id, pic_parameter_set_rbsp_t *pps);

void ProcessSPS (struct video_par *p_Vid, NALU_t *nalu);
void ProcessPPS (struct video_par *p_Vid, NALU_t *nalu);

void CleanUpPPS(struct video_par *p_Vid);

void activate_sps (struct video_par *p_Vid, seq_parameter_set_rbsp_t *sps);
void activate_pps (struct video_par *p_Vid, pic_parameter_set_rbsp_t *pps);

void UseParameterSet (struct slice_t *currSlice);

#if (MVC_EXTENSION_ENABLE)
void ProcessSubsetSPS (struct video_par *p_Vid, NALU_t *nalu);
void init_subset_sps_list(subset_seq_parameter_set_rbsp_t *subset_sps_list, int iSize);
void reset_subset_sps(subset_seq_parameter_set_rbsp_t *subset_sps);
int  GetBaseViewId(struct video_par *p_Vid, subset_seq_parameter_set_rbsp_t **subset_sps);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _PARSET_H_ */