// 
// Notice Regarding Standards.  AMD does not provide a license or sublicense to
// any Intellectual Property Rights relating to any standards, including but not
// limited to any audio and/or video codec technologies such as MPEG-2, MPEG-4;
// AVC/H.264; HEVC/H.265; AAC decode/FFMPEG; AAC encode/FFMPEG; VC-1; and MP3
// (collectively, the "Media Technologies"). For clarity, you will pay any
// royalties due for such third party technologies, which may include the Media
// Technologies that are owed as a result of AMD providing the Software to you.
// 
// MIT license 
// 
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "BitStreamParserH265.h"
#include "public/common/ByteArray.h"

#include <vector>
#include <map>
#include <algorithm>

#include "public/include/components/VideoDecoderUVD.h"

//sizeId = 0
extern int scaling_list_default_0[1][6][16];
//sizeId = 1, 2
extern int scaling_list_default_1_2[2][6][64];
//sizeId = 3
extern int scaling_list_default_3[1][2][64];

//-------------------------------------------------------------------------------------------------
class HevcParser : public BitStreamParser
{
public:
    HevcParser(amf::AMFDataStream* stream, amf::AMFContext* pContext);
    virtual ~HevcParser();

    virtual int                     GetOffsetX() const;
    virtual int                     GetOffsetY() const;
    virtual int                     GetPictureWidth() const;
    virtual int                     GetPictureHeight() const;
    virtual int                     GetAlignedWidth() const;
    virtual int                     GetAlignedHeight() const;

    virtual void                    SetMaxFramesNumber(amf_size num) { m_maxFramesNumber = num; }

    virtual const unsigned char*    GetExtraData() const;
    virtual size_t                  GetExtraDataSize() const;
    virtual void                    SetUseStartCodes(bool bUse);
    virtual void                    SetFrameRate(double fps);
    virtual double                  GetFrameRate()  const;
    virtual void                    GetFrameRate(AMFRate *frameRate) const;
    virtual const wchar_t*          GetCodecComponent() 
    {
        if(m_SpsMap.size() != 0)
        {
            const SpsData &sps = m_SpsMap.cbegin()->second;
            switch(sps.bit_depth_luma_minus8 + 8)
            {
            case 8:
                return AMFVideoDecoderHW_H265_HEVC;
            case 10:
                return AMFVideoDecoderHW_H265_MAIN10;
            }
        }

        return AMFVideoDecoderHW_H265_HEVC;
    }

    virtual AMF_RESULT              QueryOutput(amf::AMFData** ppData);
    virtual AMF_RESULT              ReInit();

protected:
    // ISO-IEC 14496-15-2004.pdf, page 14, table 1 " NAL unit types in elementary streams.
    enum NalUnitType
    {
      NAL_UNIT_CODED_SLICE_TRAIL_N = 0, // 0
      NAL_UNIT_CODED_SLICE_TRAIL_R,     // 1
  
      NAL_UNIT_CODED_SLICE_TSA_N,       // 2
      NAL_UNIT_CODED_SLICE_TLA_R,       // 3
  
      NAL_UNIT_CODED_SLICE_STSA_N,      // 4
      NAL_UNIT_CODED_SLICE_STSA_R,      // 5

      NAL_UNIT_CODED_SLICE_RADL_N,      // 6
      NAL_UNIT_CODED_SLICE_RADL_R,      // 7
  
      NAL_UNIT_CODED_SLICE_RASL_N,      // 8
      NAL_UNIT_CODED_SLICE_RASL_R,      // 9

      NAL_UNIT_RESERVED_VCL_N10,
      NAL_UNIT_RESERVED_VCL_R11,
      NAL_UNIT_RESERVED_VCL_N12,
      NAL_UNIT_RESERVED_VCL_R13,
      NAL_UNIT_RESERVED_VCL_N14,
      NAL_UNIT_RESERVED_VCL_R15,

      NAL_UNIT_CODED_SLICE_BLA_W_LP,    // 16
      NAL_UNIT_CODED_SLICE_BLA_W_RADL,  // 17
      NAL_UNIT_CODED_SLICE_BLA_N_LP,    // 18
      NAL_UNIT_CODED_SLICE_IDR_W_RADL,  // 19
      NAL_UNIT_CODED_SLICE_IDR_N_LP,    // 20
      NAL_UNIT_CODED_SLICE_CRA,         // 21
      NAL_UNIT_RESERVED_IRAP_VCL22,
      NAL_UNIT_RESERVED_IRAP_VCL23,

      NAL_UNIT_RESERVED_VCL24,
      NAL_UNIT_RESERVED_VCL25,
      NAL_UNIT_RESERVED_VCL26,
      NAL_UNIT_RESERVED_VCL27,
      NAL_UNIT_RESERVED_VCL28,
      NAL_UNIT_RESERVED_VCL29,
      NAL_UNIT_RESERVED_VCL30,
      NAL_UNIT_RESERVED_VCL31,

      NAL_UNIT_VPS,                     // 32
      NAL_UNIT_SPS,                     // 33
      NAL_UNIT_PPS,                     // 34
      NAL_UNIT_ACCESS_UNIT_DELIMITER,   // 35
      NAL_UNIT_EOS,                     // 36
      NAL_UNIT_EOB,                     // 37
      NAL_UNIT_FILLER_DATA,             // 38
      NAL_UNIT_PREFIX_SEI,              // 39
      NAL_UNIT_SUFFIX_SEI,              // 40
      NAL_UNIT_RESERVED_NVCL41,
      NAL_UNIT_RESERVED_NVCL42,
      NAL_UNIT_RESERVED_NVCL43,
      NAL_UNIT_RESERVED_NVCL44,
      NAL_UNIT_RESERVED_NVCL45,
      NAL_UNIT_RESERVED_NVCL46,
      NAL_UNIT_RESERVED_NVCL47,
      NAL_UNIT_UNSPECIFIED_48,
      NAL_UNIT_UNSPECIFIED_49,
      NAL_UNIT_UNSPECIFIED_50,
      NAL_UNIT_UNSPECIFIED_51,
      NAL_UNIT_UNSPECIFIED_52,
      NAL_UNIT_UNSPECIFIED_53,
      NAL_UNIT_UNSPECIFIED_54,
      NAL_UNIT_UNSPECIFIED_55,
      NAL_UNIT_UNSPECIFIED_56,
      NAL_UNIT_UNSPECIFIED_57,
      NAL_UNIT_UNSPECIFIED_58,
      NAL_UNIT_UNSPECIFIED_59,
      NAL_UNIT_UNSPECIFIED_60,
      NAL_UNIT_UNSPECIFIED_61,
      NAL_UNIT_UNSPECIFIED_62,
      NAL_UNIT_UNSPECIFIED_63,
      NAL_UNIT_INVALID,
    };
    struct NalUnitHeader
    {
        amf_uint32 forbidden_zero_bit;
        amf_uint32 nal_unit_type;
        amf_uint32 nuh_layer_id;
        amf_uint32 nuh_temporal_id_plus1;
        amf_uint32 num_emu_byte_removed;
    };
    enum AMFH265_ScalingListSize
    {
      AMFH265_SCALING_LIST_4x4 = 0,
      AMFH265_SCALING_LIST_8x8,
      AMFH265_SCALING_LIST_16x16,
      AMFH265_SCALING_LIST_32x32,
      AMFH265_SCALING_LIST_SIZE_NUM
    };
    typedef struct{
        amf_uint32 general_profile_space;                         //u(2)
        amf_bool general_tier_flag;                              //u(1)
        amf_uint32 general_profile_idc;                           //u(5)
        amf_bool general_profile_compatibility_flag[32];         //u(1)
        amf_bool general_progressive_source_flag;                //u(1)
        amf_bool general_interlaced_source_flag;                 //u(1)
        amf_bool general_non_packed_constraint_flag;             //u(1)
        amf_bool general_frame_only_constraint_flag;             //u(1)
        amf_uint64 general_reserved_zero_44bits;                //u(44)
        amf_uint32 general_level_idc;                             //u(8)
        //maxNumSubLayersMinus1 max is 7 - 1 = 6
        amf_bool sub_layer_profile_present_flag[6];              //u(1)
        amf_bool sub_layer_level_present_flag[6];                //u(1)

        amf_uint32 reserved_zero_2bits[8];                        //u(2)

        amf_uint32 sub_layer_profile_space[6];                    //u(2)
        amf_bool sub_layer_tier_flag[6];                         //u(1)
        amf_uint32 sub_layer_profile_idc[6];                      //u(5)
        amf_bool sub_layer_profile_compatibility_flag[6][32];    //u(1)
        amf_bool sub_layer_progressive_source_flag[6];           //u(1)
        amf_bool sub_layer_interlaced_source_flag[6];            //u(1)
        amf_bool sub_layer_non_packed_constraint_flag[6];        //u(1)
        amf_bool sub_layer_frame_only_constraint_flag[6];        //u(1)
        amf_uint64 sub_layer_reserved_zero_44bits[6];           //u(44)
        amf_uint32 sub_layer_level_idc[6];                        //u(8)
    }AMFH265_profile_tier_level_t;

#define AMFH265_SCALING_LIST_NUM 6         ///< list number for quantization matrix
#define AMFH265_SCALING_LIST_MAX_I 64

    typedef struct{
        amf_bool scaling_list_pred_mode_flag[4][6];              //u(1)
        amf_uint32 scaling_list_pred_matrix_id_delta[4][6];       //ue(v)
        amf_int32 scaling_list_dc_coef_minus8[4][6];              //se(v)
        amf_int32 scaling_list_delta_coef;                        //se(v)         could have issues......
        amf_int32 ScalingList[AMFH265_SCALING_LIST_SIZE_NUM][AMFH265_SCALING_LIST_NUM][AMFH265_SCALING_LIST_MAX_I];
    }AMFH265_scaling_list_data_t;

    typedef struct{
        amf_int32 num_negative_pics;
        amf_int32 num_positive_pics;
        amf_int32 num_of_pics;
        amf_int32 num_of_delta_poc;
        amf_int32 deltaPOC[16];
        amf_bool used_by_curr_pic[16];
    }AMFH265_short_term_RPS_t;

    typedef struct{
        amf_int32 num_of_pics;
        amf_int32 POCs[32];
        amf_bool used_by_curr_pic[32];
    }AMFH265_long_term_RPS_t;

    typedef struct{
        //CpbCnt = cpb_cnt_minus1
        amf_uint32 bit_rate_value_minus1[32];                     //ue(v)
        amf_uint32 cpb_size_value_minus1[32];                     //ue(v)
        amf_uint32 cpb_size_du_value_minus1[32];                  //ue(v)
        amf_uint32 bit_rate_du_value_minus1[32];                  //ue(v)
        amf_bool cbr_flag[32];                                   //u(1)
    }AMFH265_sub_layer_hrd_parameters;
    typedef struct{
        amf_bool nal_hrd_parameters_present_flag;                //u(1)
        amf_bool vcl_hrd_parameters_present_flag;                //u(1)
        amf_bool sub_pic_hrd_params_present_flag;                //u(1)
        amf_uint32 tick_divisor_minus2;                           //u(8)
        amf_uint32 du_cpb_removal_delay_increment_length_minus1;  //u(5)
        amf_bool sub_pic_cpb_params_in_pic_timing_sei_flag;      //u(1)
        amf_uint32 dpb_output_delay_du_length_minus1;             //u(5)
        amf_uint32 bit_rate_scale;                                //u(4)
        amf_uint32 cpb_size_scale;                                //u(4)
        amf_uint32 cpb_size_du_scale;                             //u(4)
        amf_uint32 initial_cpb_removal_delay_length_minus1;       //u(5)
        amf_uint32 au_cpb_removal_delay_length_minus1;            //u(5)
        amf_uint32 dpb_output_delay_length_minus1;                //u(5)
        amf_bool fixed_pic_rate_general_flag[7];                 //u(1)
        amf_bool fixed_pic_rate_within_cvs_flag[7];              //u(1)
        amf_uint32 elemental_duration_in_tc_minus1[7];            //ue(v)
        amf_bool low_delay_hrd_flag[7];                          //u(1)
        amf_uint32 cpb_cnt_minus1[7];                             //ue(v)
        //sub_layer_hrd_parameters()
        AMFH265_sub_layer_hrd_parameters sub_layer_hrd_parameters_0[7];
        //sub_layer_hrd_parameters()
        AMFH265_sub_layer_hrd_parameters sub_layer_hrd_parameters_1[7];
    }AMFH265_hrd_parameters_t;
    typedef struct
    {
        amf_bool aspect_ratio_info_present_flag;                 //u(1)
        amf_uint32 aspect_ratio_idc;                              //u(8)
        amf_uint32 sar_width;                                     //u(16)
        amf_uint32 sar_height;                                    //u(16)
        amf_bool overscan_info_present_flag;                     //u(1)
        amf_bool overscan_appropriate_flag;                      //u(1)
        amf_bool video_signal_type_present_flag;                 //u(1)
        amf_uint32 video_format;                                  //u(3)
        amf_bool video_full_range_flag;                          //u(1)
        amf_bool colour_description_present_flag;                //u(1)
        amf_uint32 colour_primaries;                              //u(8)
        amf_uint32 transfer_characteristics;                      //u(8)
        amf_uint32 matrix_coeffs;                                 //u(8)
        amf_bool chroma_loc_info_present_flag;                   //u(1)
        amf_uint32 chroma_sample_loc_type_top_field;              //ue(v)
        amf_uint32 chroma_sample_loc_type_bottom_field;           //ue(v)
        amf_bool neutral_chroma_indication_flag;                 //u(1)
        amf_bool field_seq_flag;                                 //u(1)
        amf_bool frame_field_info_present_flag;                  //u(1)
        amf_bool default_display_window_flag;                    //u(1)
        amf_uint32 def_disp_win_left_offset;                      //ue(v)
        amf_uint32 def_disp_win_right_offset;                     //ue(v)
        amf_uint32 def_disp_win_top_offset;                       //ue(v)
        amf_uint32 def_disp_win_bottom_offset;                    //ue(v)
        amf_bool vui_timing_info_present_flag;                   //u(1)
        amf_uint32 vui_num_units_in_tick;                         //u(32)
        amf_uint32 vui_time_scale;                                //u(32)
        amf_bool vui_poc_proportional_to_timing_flag;            //u(1)
        amf_uint32 vui_num_ticks_poc_diff_one_minus1;             //ue(v)
        amf_bool vui_hrd_parameters_present_flag;                //u(1)
        //hrd_parameters()
        AMFH265_hrd_parameters_t hrd_parameters;
        amf_bool bitstream_restriction_flag;                     //u(1)
        amf_bool tiles_fixed_structure_flag;                     //u(1)
        amf_bool motion_vectors_over_pic_boundaries_flag;        //u(1)
        amf_bool restricted_ref_pic_lists_flag;                  //u(1)
        amf_uint32 min_spatial_segmentation_idc;                  //ue(v)
        amf_uint32 max_bytes_per_pic_denom;                       //ue(v)
        amf_uint32 max_bits_per_min_cu_denom;                     //ue(v)
        amf_uint32 log2_max_mv_length_horizontal;                 //ue(v)
        amf_uint32 log2_max_mv_length_vertical;                   //ue(v)
    }AMFH265_vui_parameters_t;
    typedef struct{
        amf_uint32 rbsp_stop_one_bit; /* equal to 1 */
        amf_uint32 rbsp_alignment_zero_bit; /* equal to 0 */
    }AMFH265_rbsp_trailing_bits_t;

    struct SpsData
    {
        amf_uint32 sps_video_parameter_set_id;                    //u(4)
        amf_uint32 sps_max_sub_layers_minus1;                     //u(3)
        amf_bool sps_temporal_id_nesting_flag;                   //u(1)
        //profile_tier_level( sps_max_sub_layers_minus1 )
        AMFH265_profile_tier_level_t profile_tier_level;
        amf_uint32 sps_seq_parameter_set_id;                      //ue(v)
        amf_uint32 chroma_format_idc;                             //ue(v)
        amf_bool separate_colour_plane_flag;                     //u(1)
        amf_uint32 pic_width_in_luma_samples;                     //ue(v)
        amf_uint32 pic_height_in_luma_samples;                    //ue(v)
        amf_uint32 max_cu_width;
        amf_uint32 max_cu_height;
        amf_uint32 max_cu_depth;
        amf_bool conformance_window_flag;                        //u(1)
        amf_uint32 conf_win_left_offset;                          //ue(v)
        amf_uint32 conf_win_right_offset;                         //ue(v)
        amf_uint32 conf_win_top_offset;                           //ue(v)
        amf_uint32 conf_win_bottom_offset;                        //ue(v)
        amf_uint32 bit_depth_luma_minus8;                         //ue(v)
        amf_uint32 bit_depth_chroma_minus8;                        //ue(v)
        amf_uint32 log2_max_pic_order_cnt_lsb_minus4;             //ue(v)
        amf_bool sps_sub_layer_ordering_info_present_flag;       //u(1)
        amf_uint32 sps_max_dec_pic_buffering_minus1[6];           //ue(v)
        amf_uint32 sps_max_num_reorder_pics[6];                   //ue(v)
        amf_uint32 sps_max_latency_increase_plus1[6];             //ue(v)
        amf_uint32 log2_min_luma_coding_block_size_minus3;        //ue(v)
        amf_uint32 log2_diff_max_min_luma_coding_block_size;      //ue(v)
        amf_uint32 log2_min_transform_block_size_minus2;          //ue(v)
        amf_uint32 log2_diff_max_min_transform_block_size;        //ue(v)
        amf_uint32 max_transform_hierarchy_depth_inter;           //ue(v)
        amf_uint32 max_transform_hierarchy_depth_intra;           //ue(v)
        amf_bool scaling_list_enabled_flag;                      //u(1)
        amf_bool sps_scaling_list_data_present_flag;             //u(1)
        //scaling_list_data()
        AMFH265_scaling_list_data_t scaling_list_data;
        amf_bool amp_enabled_flag;                               //u(1)
        amf_bool sample_adaptive_offset_enabled_flag;            //u(1)
        amf_bool pcm_enabled_flag;                               //u(1)
        amf_uint32 pcm_sample_bit_depth_luma_minus1;              //u(4)
        amf_uint32 pcm_sample_bit_depth_chroma_minus1;            //u(4)
        amf_uint32 log2_min_pcm_luma_coding_block_size_minus3;    //ue(v)
        amf_uint32 log2_diff_max_min_pcm_luma_coding_block_size;  //ue(v)
        amf_bool pcm_loop_filter_disabled_flag;                  //u(1)
        amf_uint32 num_short_term_ref_pic_sets;                   //ue(v)
        //short_term_ref_pic_set(i) max is 64
        AMFH265_short_term_RPS_t stRPS[64];
        AMFH265_long_term_RPS_t ltRPS;
        //AMFH265_short_term_ref_pic_set_t short_term_ref_pic_set[64];
        amf_bool long_term_ref_pics_present_flag;                //u(1)
        amf_uint32 num_long_term_ref_pics_sps;                    //ue(v)
        //max is 32
        amf_uint32 lt_ref_pic_poc_lsb_sps[32];                    //u(v)
        amf_bool used_by_curr_pic_lt_sps_flag[32];               //u(1)
        amf_bool sps_temporal_mvp_enabled_flag;                  //u(1)
        amf_bool strong_intra_smoothing_enabled_flag;            //u(1)
        amf_bool vui_parameters_present_flag;                    //u(1)
        //vui_parameters()
        AMFH265_vui_parameters_t vui_parameters;
        amf_bool sps_extension_flag;                             //u(1)
        amf_bool sps_extension_data_flag;                        //u(1)
        //rbsp_trailing_bits( )
        AMFH265_rbsp_trailing_bits_t rbsp_trailing_bits;

        SpsData(void)
        {
            memset(this, 0, sizeof(*this));
        }
        bool Parse(amf_uint8 *data, size_t size);
        void ParsePTL(AMFH265_profile_tier_level_t *ptl, amf_bool profilePresentFlag, amf_uint32 maxNumSubLayersMinus1, amf_uint8 *nalu, size_t size, size_t &offset);
        void ParseSubLayerHrdParameters(AMFH265_sub_layer_hrd_parameters *sub_hrd, amf_uint32 CpbCnt, amf_bool sub_pic_hrd_params_present_flag, amf_uint8 *nalu, size_t size, size_t &offset);
        void ParseHrdParameters(AMFH265_hrd_parameters_t *hrd, amf_bool commonInfPresentFlag, amf_uint32 maxNumSubLayersMinus1, amf_uint8 *nalu, size_t size, size_t &offset);
        static void ParseScalingList(AMFH265_scaling_list_data_t * s_data, amf_uint8 *data, size_t size,size_t &offset);
        void ParseVUI(AMFH265_vui_parameters_t *vui, amf_uint32 maxNumSubLayersMinus1, amf_uint8 *data, size_t size,size_t &offset);
        void ParseShortTermRefPicSet(AMFH265_short_term_RPS_t *rps, amf_int32 stRpsIdx, amf_uint32 num_short_term_ref_pic_sets, AMFH265_short_term_RPS_t rps_ref[], amf_uint8 *data, size_t size,size_t &offset);
    };
    struct PpsData
    {
        amf_uint32 pps_pic_parameter_set_id;                      //ue(v)
        amf_uint32 pps_seq_parameter_set_id;                      //ue(v)
        amf_bool dependent_slice_segments_enabled_flag;          //u(1)
        amf_bool output_flag_present_flag;                       //u(1)
        amf_uint32 num_extra_slice_header_bits;                   //u(3)
        amf_bool sign_data_hiding_enabled_flag;                  //u(1)
        amf_bool cabac_init_present_flag;                        //u(1)
        amf_uint32 num_ref_idx_l0_default_active_minus1;          //ue(v)
        amf_uint32 num_ref_idx_l1_default_active_minus1;          //ue(v)
        amf_int32 init_qp_minus26;                                //se(v)
        amf_bool constrained_intra_pred_flag;                    //u(1)
        amf_bool transform_skip_enabled_flag;                    //u(1)
        amf_bool cu_qp_delta_enabled_flag;                       //u(1)
        amf_uint32 diff_cu_qp_delta_depth;                        //ue(v)
        amf_int32 pps_cb_qp_offset;                               //se(v)
        amf_int32 pps_cr_qp_offset;                               //se(v)
        amf_bool pps_slice_chroma_qp_offsets_present_flag;       //u(1)
        amf_bool weighted_pred_flag;                             //u(1)
        amf_bool weighted_bipred_flag;                           //u(1)
        amf_bool transquant_bypass_enabled_flag;                 //u(1)
        amf_bool tiles_enabled_flag;                             //u(1)
        amf_bool entropy_coding_sync_enabled_flag;               //u(1)
        amf_uint32 num_tile_columns_minus1;                       //ue(v)
        amf_uint32 num_tile_rows_minus1;                          //ue(v)
        amf_bool uniform_spacing_flag;                           //u(1)
        //PicWidthInCtbsY = Ceil( pic_width_in_luma_samples / CtbSizeY )  = 256 assume max width is 4096
        //CtbSizeY = 1<<CtbLog2SizeY   so min is 16
        // 4 <= CtbLog2SizeY <= 6
        amf_uint32 column_width_minus1[265];                      //ue(v)
        //2304/16=144 assume max height is 2304
        amf_uint32 row_height_minus1[144];                        //ue(v)
        amf_bool loop_filter_across_tiles_enabled_flag;          //u(1)
        amf_bool pps_loop_filter_across_slices_enabled_flag;     //u(1)
        amf_bool deblocking_filter_control_present_flag;         //u(1)
        amf_bool deblocking_filter_override_enabled_flag;        //u(1)
        amf_bool pps_deblocking_filter_disabled_flag;            //u(1)
        amf_int32 pps_beta_offset_div2;                           //se(v)
        amf_int32 pps_tc_offset_div2;                             //se(v)
        amf_bool pps_scaling_list_data_present_flag;             //u(1)
        //scaling_list_data( )
        AMFH265_scaling_list_data_t scaling_list_data;
        amf_bool lists_modification_present_flag;                //u(1)
        amf_uint32 log2_parallel_merge_level_minus2;              //ue(v)
        amf_bool slice_segment_header_extension_present_flag;    //u(1)
        amf_bool pps_extension_flag;                             //u(1)
        amf_bool pps_extension_data_flag;                        //u(1)
        //rbsp_trailing_bits( )
        AMFH265_rbsp_trailing_bits_t rbsp_trailing_bits;
        PpsData(void)
        {
            memset(this, 0, sizeof(*this));
        }
        bool Parse(amf_uint8 *data, size_t size);
    };
    // See ITU-T Rec. H.264 (04/2013) Advanced video coding for generic audiovisual services, page 28, 91.
    struct AccessUnitSigns
    {
        bool bNewPicture;
        AccessUnitSigns() :
            bNewPicture(false)
        {}
        bool Parse(amf_uint8 *data, size_t size, std::map<amf_uint32,SpsData> &spsMap, std::map<amf_uint32,PpsData> &ppsMap);
        bool IsNewPicture();
    };
    class ExtraDataBuilder
    {
    public:
        ExtraDataBuilder() : m_SPSCount(0), m_PPSCount(0){}

        void AddSPS(amf_uint8 *sps, size_t size);
        void AddPPS(amf_uint8 *pps, size_t size);
        bool GetExtradata(AMFByteArray   &extradata);

    private:
        AMFByteArray   m_SPSs;
        AMFByteArray   m_PPSs;
        amf_int32       m_SPSCount;
        amf_int32       m_PPSCount;
    };

    friend struct AccessUnitSigns;

    static const amf_uint32 MacroblocSize = 16;
    static const amf_uint8 NalUnitTypeMask = 0x1F; // b00011111
    static const amf_uint8 NalRefIdcMask = 0x60;   // b01100000
    static const amf_uint8 NalUnitLengthSize = 4U;

    static const size_t m_ReadSize = 1024*4;

    static const amf_uint16 maxSpsSize = 0xFFFF;
    static const amf_uint16 minSpsSize = 5;
    static const amf_uint16 maxPpsSize = 0xFFFF;

    NalUnitHeader ReadNextNaluUnit(size_t *offset, size_t *nalu, size_t *size);
    void          FindSPSandPPS();
    static inline NalUnitHeader GetNaluUnitType(amf_uint8 *nalUnit)
    {
        NalUnitHeader nalu_header;
        nalu_header.num_emu_byte_removed = 0;
        //read nalu header
        nalu_header.forbidden_zero_bit = (amf_uint32) ((nalUnit[0] >> 7)&1);
        nalu_header.nal_unit_type = (amf_uint32) ((nalUnit[0] >> 1)&63);
        nalu_header.nuh_layer_id = (amf_uint32) (((nalUnit[0]&1) << 6) | ((nalUnit[1] & 248) >> 3));
        nalu_header.nuh_temporal_id_plus1 = (amf_uint32) (nalUnit[1] & 7);

        return nalu_header;
    }
    size_t EBSPtoRBSP(amf_uint8 *streamBuffer,size_t begin_bytepos, size_t end_bytepos);
    AMFRect GetCropRect() const;


    AMFByteArray   m_ReadData;
    AMFByteArray   m_Extradata;
    
    AMFByteArray   m_EBSPtoRBSPData;

    bool           m_bUseStartCodes;
    amf_pts        m_currentFrameTimestamp;
    amf::AMFDataStreamPtr m_pStream;
    std::map<amf_uint32,SpsData> m_SpsMap;
    std::map<amf_uint32,PpsData> m_PpsMap;
    amf_size       m_PacketCount;
    bool            m_bEof;
    double          m_fps;
    amf_size        m_maxFramesNumber;
    amf::AMFContext* m_pContext;
};
//-------------------------------------------------------------------------------------------------
BitStreamParser* CreateHEVCParser(amf::AMFDataStream* stream, amf::AMFContext* pContext)
{
    return new HevcParser(stream, pContext);
}
//-------------------------------------------------------------------------------------------------
HevcParser::HevcParser(amf::AMFDataStream* stream, amf::AMFContext* pContext) :
    m_bUseStartCodes(false),
    m_currentFrameTimestamp(0),
    m_pStream(stream),
    m_PacketCount(0),
    m_bEof(false),
    m_fps(0),
    m_maxFramesNumber(0),
    m_pContext(pContext)
{
    stream->Seek(amf::AMF_SEEK_BEGIN, 0, NULL);
    FindSPSandPPS();
}
//-------------------------------------------------------------------------------------------------
HevcParser::~HevcParser()
{
//    LOG_DEBUG(L"HevcParser: parsed frames:" << m_PacketCount << L"\n");
}
//-------------------------------------------------------------------------------------------------
static const amf_int s_winUnitX[]={1,2,2,1};
static const amf_int s_winUnitY[]={1,2,1,1};

static amf_int getWinUnitX (amf_int chromaFormatIdc) { return s_winUnitX[chromaFormatIdc];      }
static amf_int getWinUnitY (amf_int chromaFormatIdc) { return s_winUnitY[chromaFormatIdc];      }
static const int MacroblockSize = 16;

AMFRect HevcParser::GetCropRect() const
{
    AMFRect rect ={0};
    if(m_SpsMap.size() == 0)
    {
        return rect;
    }
    const SpsData &sps = m_SpsMap.cbegin()->second;

    rect.right = amf_int32(sps.pic_width_in_luma_samples);
    rect.bottom = amf_int32(sps.pic_height_in_luma_samples);

    if (sps.conformance_window_flag)
    {
        rect.left += getWinUnitX(sps.chroma_format_idc) * sps.conf_win_left_offset;
        rect.right -= getWinUnitX(sps.chroma_format_idc) * sps.conf_win_right_offset;
        rect.top += getWinUnitX(sps.chroma_format_idc) * sps.conf_win_top_offset;
        rect.bottom -= getWinUnitX(sps.chroma_format_idc) * sps.conf_win_bottom_offset;
    }
    return rect;
}

int  HevcParser::GetOffsetX() const
{
    return GetCropRect().left;
}

int  HevcParser::GetOffsetY() const
{
    return GetCropRect().top;
}

int HevcParser::GetPictureWidth() const
{
    return GetCropRect().Width();
}
//-------------------------------------------------------------------------------------------------
int HevcParser::GetPictureHeight() const
{
    return GetCropRect().Height();

}
//-------------------------------------------------------------------------------------------------
int HevcParser::GetAlignedWidth() const
{
    if(m_SpsMap.size() == 0)
    {
        return 0;
    }
    const SpsData &sps = m_SpsMap.cbegin()->second;

    amf_int32 blocksize = sps.log2_min_luma_coding_block_size_minus3+3;
    int width =int(sps.pic_width_in_luma_samples / (1<<blocksize) * (1<<blocksize));
    return width;
}
//-------------------------------------------------------------------------------------------------
int HevcParser::GetAlignedHeight() const
{
    if(m_SpsMap.size() == 0)
    {
        return 0;
    }
    const SpsData &sps = m_SpsMap.cbegin()->second;

    amf_int32 blocksize = sps.log2_min_luma_coding_block_size_minus3+3;
    int height = int(sps.pic_height_in_luma_samples / (1<<blocksize) * (1<<blocksize));
    return height;
}
//-------------------------------------------------------------------------------------------------
const unsigned char* HevcParser::GetExtraData() const
{
    return m_Extradata.GetData();
}
//-------------------------------------------------------------------------------------------------
size_t HevcParser::GetExtraDataSize() const
{
    return m_Extradata.GetSize();
};
//-------------------------------------------------------------------------------------------------
void HevcParser::SetUseStartCodes(bool bUse)
{
    m_bUseStartCodes = bUse;
}
//-------------------------------------------------------------------------------------------------
void HevcParser::SetFrameRate(double fps)
{
    m_fps = fps;
}
//-------------------------------------------------------------------------------------------------
double HevcParser::GetFrameRate()  const
{
    if(m_fps != 0)
    {
        return m_fps;
    }
    if(m_SpsMap.size() > 0)
    {
        const SpsData &sps = m_SpsMap.cbegin()->second;
        if(sps.vui_parameters_present_flag && sps.vui_parameters.vui_timing_info_present_flag && sps.vui_parameters.vui_num_units_in_tick)
        {
            // according to the latest h264 standard nuit_field_based_flag is always = 1 and therefore this must be divided by two 
            // some old clips may get wrong FPS. This is just a sample. Use container information
            return (double)sps.vui_parameters.vui_time_scale / sps.vui_parameters.vui_num_units_in_tick / 2;
        }
    }
    return 25.0;
}
//-------------------------------------------------------------------------------------------------
void     HevcParser::GetFrameRate(AMFRate *frameRate) const
{
    if(m_SpsMap.size() > 0)
    {
        const SpsData &sps = m_SpsMap.cbegin()->second;
        if(sps.vui_parameters_present_flag && sps.vui_parameters.vui_timing_info_present_flag && sps.vui_parameters.vui_num_units_in_tick)
        {
            // according to the latest h264 standard nuit_field_based_flag is always = 1 and therefore this must be divided by two 
            // some old clips may get wrong FPS. This is just a sample. Use container information
            frameRate->num = sps.vui_parameters.vui_time_scale / 2;
            frameRate->den = sps.vui_parameters.vui_num_units_in_tick;
            return;
        }
    }
    frameRate->num = 0;
    frameRate->den = 0;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT HevcParser::QueryOutput(amf::AMFData** ppData)
{
    if(m_bFrozen)
    {
        return AMF_OK;
    }
    if((m_bEof && m_ReadData.GetSize() == 0) || m_maxFramesNumber && m_PacketCount >= m_maxFramesNumber)
    {
        return AMF_EOF;
    }
    bool newPictureDetected = false;
    size_t packetSize = 0;
    size_t readSize = 0;
    std::vector<size_t> naluStarts;
    std::vector<size_t> naluSizes;
    size_t dataOffset = 0;
    bool bSliceFound = false;
	amf_uint32 prev_slice_nal_unit_type;
	
    do 
    {
		size_t naluSize = 0;
        size_t naluOffset = 0;
        size_t naluAnnexBOffset = dataOffset;
        NalUnitHeader   naluHeader = ReadNextNaluUnit(&dataOffset, &naluOffset, &naluSize);

		if (bSliceFound == true)
		{
			if (prev_slice_nal_unit_type != naluHeader.nal_unit_type)
			{
				newPictureDetected = true;
			}
		}

        if(NAL_UNIT_ACCESS_UNIT_DELIMITER == naluHeader.nal_unit_type)
        {
            if(packetSize > 0)
            {
                newPictureDetected = true;
            }
        }
        else if(NAL_UNIT_PREFIX_SEI == naluHeader.nal_unit_type)
        {
            if(bSliceFound)
            {
                newPictureDetected = true;
            }
        }
        else if (
        NAL_UNIT_CODED_SLICE_TRAIL_R == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_TRAIL_N == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_TLA_R == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_TSA_N == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_STSA_R == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_STSA_N == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_BLA_W_LP == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_BLA_W_RADL == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_BLA_N_LP == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_IDR_W_RADL == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_IDR_N_LP == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_CRA == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_RADL_N == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_RADL_R == naluHeader.nal_unit_type
       || NAL_UNIT_CODED_SLICE_RASL_N == naluHeader.nal_unit_type
       || NAL_UNIT_CODED_SLICE_RASL_R == naluHeader.nal_unit_type
        )
        {

			if (bSliceFound == true)
			{

				if (prev_slice_nal_unit_type != naluHeader.nal_unit_type)
				{
					newPictureDetected = true;
				}
				else
				{
					AccessUnitSigns naluAccessUnitsSigns;
					naluAccessUnitsSigns.Parse(m_ReadData.GetData() + naluOffset, naluSize, m_SpsMap, m_PpsMap);
					newPictureDetected = naluAccessUnitsSigns.IsNewPicture() && bSliceFound;
				}
				bSliceFound = true;
				prev_slice_nal_unit_type = naluHeader.nal_unit_type;
			}
			else
			{
				AccessUnitSigns naluAccessUnitsSigns;
				naluAccessUnitsSigns.Parse(m_ReadData.GetData() + naluOffset, naluSize, m_SpsMap, m_PpsMap);

				newPictureDetected = naluAccessUnitsSigns.IsNewPicture() && bSliceFound;
				bSliceFound = true;
				prev_slice_nal_unit_type = naluHeader.nal_unit_type;
			}

        }

		if (naluSize > 0 && !newPictureDetected )
        {
            packetSize += naluSize;
            if(!m_bUseStartCodes)
            {
                packetSize += NalUnitLengthSize;
                naluStarts.push_back(naluOffset);
                naluSizes.push_back(naluSize);
            }
            else
            {
                size_t startCodeSize = naluOffset - naluAnnexBOffset;
                packetSize += startCodeSize;
            }
        }
        if(!newPictureDetected)
        {
            readSize = dataOffset;
        }
        if(naluHeader.nal_unit_type == NAL_UNIT_INVALID)
        {
	  		break;
        }
    } while (!newPictureDetected);


    amf::AMFBufferPtr pictureBuffer;
    AMF_RESULT ar = m_pContext->AllocBuffer(amf::AMF_MEMORY_HOST, packetSize, &pictureBuffer);

    amf_uint8 *data = (amf_uint8*)pictureBuffer->GetNative();
    if(m_bUseStartCodes)
    {
        memcpy(data, m_ReadData.GetData(), packetSize);
    }
    else
    {
        for( size_t i=0; i < naluStarts.size(); i++)
        {
            // copy size
            amf_uint32 naluSize= (amf_uint32)naluSizes[i];
            *data++ = (naluSize >> 24);
            *data++ = ((naluSize & 0x00FF0000) >> 16);
            *data++ = ((naluSize & 0x0000FF00) >> 8);
            *data++ = ((naluSize & 0x000000FF));

            memcpy(data, m_ReadData.GetData() + naluStarts[i], naluSize);
            data += naluSize;
        }
    }

    pictureBuffer->SetPts(m_currentFrameTimestamp);
    amf_pts frameDuration = amf_pts(AMF_SECOND / GetFrameRate()); // In 100 NanoSeconds
    pictureBuffer->SetDuration(frameDuration);
    m_currentFrameTimestamp += frameDuration;

//    if (newPictureDetected)
    {
    // shift remaining data in m_ReadData
        size_t remainingData = m_ReadData.GetSize() - readSize;
        memmove(m_ReadData.GetData(), m_ReadData.GetData()+readSize, remainingData);
        m_ReadData.SetSize(remainingData);
    }
    *ppData = pictureBuffer.Detach();
    m_PacketCount++;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
HevcParser::NalUnitHeader   HevcParser::ReadNextNaluUnit(size_t *offset, size_t *nalu, size_t *size)
{
    *size = 0;
    size_t startOffset = *offset;

    bool newNalFound = false;
    size_t zerosCount = 0;

    while(!newNalFound)
    {
        // read next portion if needed
        size_t ready = m_ReadData.GetSize() - *offset;
		//printf("ReadNextNaluUnit: remaining data size for read: %d\n", ready);
		if (ready == 0)
		{
			if (m_bEof == false)
			{
				m_ReadData.SetSize(m_ReadData.GetSize() + m_ReadSize);
                ready = 0;
                m_pStream->Read(m_ReadData.GetData() + *offset, m_ReadSize, &ready);
			}
			

			if (ready != m_ReadSize && ready != 0)
			{
				m_ReadData.SetSize(m_ReadData.GetSize() - (m_ReadSize - ready));
			}
				
			
			

            if(ready == 0 )
            {
				if (m_bEof == false)
				m_ReadData.SetSize(m_ReadData.GetSize() - m_ReadSize);

                m_bEof = true;
                newNalFound = startOffset != *offset; 
                *offset = m_ReadData.GetSize();
                break; // EOF
            }
        }
        amf_uint8* data= m_ReadData.GetData() + *offset;
        for(size_t i = 0; i < ready; i++)
        {
            amf_uint8 ch = *data++;
            if (0 == ch)
            {
                zerosCount++;
            }
            else 
            {
                if (1 == ch && zerosCount >=2) // We found a start code in Annex B stream
                {
                    if(*offset + (i - zerosCount) > startOffset)
                    {
                        ready = i - zerosCount;
                        newNalFound = true; // new NAL
                        break; 
                    }
                    else
                    {
                        *nalu = *offset + zerosCount + 1;
                    }
                }
                zerosCount = 0;
            }
        }
        // if zeros found but not a new NAL - continue with zerosCount on the next iteration
        *offset += ready;
    }
    if(!newNalFound)
    {
        NalUnitHeader header_nalu;
        header_nalu.nal_unit_type = NAL_UNIT_INVALID;
        return header_nalu; // EOF
    }
    *size = *offset - *nalu;
    // get NAL type
    return GetNaluUnitType(m_ReadData.GetData() + *nalu);
}
//-------------------------------------------------------------------------------------------------
void    HevcParser::FindSPSandPPS()
{
    ExtraDataBuilder extraDataBuilder;

    bool newPictureDetected = false;
    size_t dataOffset = 0;
    do 
    {
        
        size_t naluSize = 0;
        size_t naluOffset = 0;
        size_t naluAnnexBOffset = dataOffset;
        NalUnitHeader   naluHeader = ReadNextNaluUnit(&dataOffset, &naluOffset, &naluSize);

        if (naluHeader.nal_unit_type == NAL_UNIT_INVALID )
        {
            break; // EOF
        }

        if (naluHeader.nal_unit_type == NAL_UNIT_SPS)
        {
            m_EBSPtoRBSPData.SetSize(naluSize);
            memcpy(m_EBSPtoRBSPData.GetData(), m_ReadData.GetData() + naluOffset, naluSize);
            size_t newNaluSize = EBSPtoRBSP(m_EBSPtoRBSPData.GetData(),0, naluSize);

            SpsData sps;
            sps.Parse(m_EBSPtoRBSPData.GetData(), newNaluSize);
            m_SpsMap[sps.sps_video_parameter_set_id] = sps;
            extraDataBuilder.AddSPS(m_ReadData.GetData()+naluOffset, naluSize);
        }
        else if (naluHeader.nal_unit_type == NAL_UNIT_PPS)
        {
            m_EBSPtoRBSPData.SetSize(naluSize);
            memcpy(m_EBSPtoRBSPData.GetData(), m_ReadData.GetData() + naluOffset, naluSize);
            size_t newNaluSize = EBSPtoRBSP(m_EBSPtoRBSPData.GetData(),0, naluSize);

            PpsData pps;
            pps.Parse(m_EBSPtoRBSPData.GetData(), newNaluSize);
            m_PpsMap[pps.pps_pic_parameter_set_id] = pps;
            extraDataBuilder.AddPPS(m_ReadData.GetData()+naluOffset, naluSize);
        }
        else if (
        NAL_UNIT_CODED_SLICE_TRAIL_R == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_TRAIL_N == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_TLA_R == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_TSA_N == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_STSA_R == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_STSA_N == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_BLA_W_LP == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_BLA_W_RADL == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_BLA_N_LP == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_IDR_W_RADL == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_IDR_N_LP == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_CRA == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_RADL_N == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_RADL_R == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_RASL_N == naluHeader.nal_unit_type
        || NAL_UNIT_CODED_SLICE_RASL_R == naluHeader.nal_unit_type
        )
        {
            break; // frame data
        }
    } while (true);

    m_pStream->Seek(amf::AMF_SEEK_BEGIN, 0, NULL);
    m_ReadData.SetSize(0);
    // It will fail if SPS or PPS are absent
    extraDataBuilder.GetExtradata(m_Extradata);
}
//-------------------------------------------------------------------------------------------------
bool HevcParser::SpsData::Parse(amf_uint8 *nalu, size_t size)
{
    size_t offset = 16; // 2 bytes NALU header + 
    amf_uint32 activeVPS = Parser::readBits(nalu, offset,4);
    amf_uint32 max_sub_layer_minus1 = Parser::readBits(nalu, offset,3);
    amf_uint32 sps_temporal_id_nesting_flag = Parser::getBit(nalu, offset);
    AMFH265_profile_tier_level_t ptl;
    memset (&ptl,0,sizeof(ptl));
    ParsePTL(&ptl, true, max_sub_layer_minus1, nalu, size, offset);
    amf_uint32 SPS_ID = Parser::ExpGolomb::readUe(nalu, offset);

    sps_video_parameter_set_id = activeVPS;
    sps_max_sub_layers_minus1 = max_sub_layer_minus1;
    sps_temporal_id_nesting_flag = sps_temporal_id_nesting_flag;
    memcpy (&profile_tier_level,&ptl,sizeof(ptl));
    sps_seq_parameter_set_id = SPS_ID;

    chroma_format_idc = Parser::ExpGolomb::readUe(nalu, offset);
    if (chroma_format_idc == 3)
    {
        separate_colour_plane_flag = Parser::getBit(nalu, offset);
    }
    pic_width_in_luma_samples = Parser::ExpGolomb::readUe(nalu, offset);
    pic_height_in_luma_samples = Parser::ExpGolomb::readUe(nalu, offset);
    conformance_window_flag = Parser::getBit(nalu, offset);
    if (conformance_window_flag)
    {
        conf_win_left_offset = Parser::ExpGolomb::readUe(nalu, offset);
        conf_win_right_offset = Parser::ExpGolomb::readUe(nalu, offset);
        conf_win_top_offset = Parser::ExpGolomb::readUe(nalu, offset);
        conf_win_bottom_offset = Parser::ExpGolomb::readUe(nalu, offset);
    }
    bit_depth_luma_minus8 = Parser::ExpGolomb::readUe(nalu, offset);
    bit_depth_chroma_minus8 = Parser::ExpGolomb::readUe(nalu, offset);
    log2_max_pic_order_cnt_lsb_minus4 = Parser::ExpGolomb::readUe(nalu, offset);
    sps_sub_layer_ordering_info_present_flag = Parser::getBit(nalu, offset);
    for (amf_uint32 i=(sps_sub_layer_ordering_info_present_flag?0:sps_max_sub_layers_minus1); i<=sps_max_sub_layers_minus1; i++)
    {
        sps_max_dec_pic_buffering_minus1[i] = Parser::ExpGolomb::readUe(nalu, offset);
        sps_max_num_reorder_pics[i] = Parser::ExpGolomb::readUe(nalu, offset);
        sps_max_latency_increase_plus1[i] = Parser::ExpGolomb::readUe(nalu, offset);
    }
    log2_min_luma_coding_block_size_minus3 = Parser::ExpGolomb::readUe(nalu, offset);

    int log2MinCUSize = log2_min_luma_coding_block_size_minus3 +3;

    log2_diff_max_min_luma_coding_block_size = Parser::ExpGolomb::readUe(nalu, offset);

    int maxCUDepthDelta = log2_diff_max_min_luma_coding_block_size;
    max_cu_width = ( 1<<(log2MinCUSize + maxCUDepthDelta) );
    max_cu_height = ( 1<<(log2MinCUSize + maxCUDepthDelta) );

    log2_min_transform_block_size_minus2 = Parser::ExpGolomb::readUe(nalu, offset);

    amf_uint32 QuadtreeTULog2MinSize = log2_min_transform_block_size_minus2 + 2;
    int addCuDepth = AMF_MAX (0, log2MinCUSize - (int)QuadtreeTULog2MinSize );
    max_cu_depth = (maxCUDepthDelta + addCuDepth);

    log2_diff_max_min_transform_block_size = Parser::ExpGolomb::readUe(nalu, offset);
    max_transform_hierarchy_depth_inter = Parser::ExpGolomb::readUe(nalu, offset);
    max_transform_hierarchy_depth_intra = Parser::ExpGolomb::readUe(nalu, offset);
    scaling_list_enabled_flag = Parser::getBit(nalu, offset);
    if (scaling_list_enabled_flag)
    {
        sps_scaling_list_data_present_flag = Parser::getBit(nalu, offset);
        if (sps_scaling_list_data_present_flag)
        {
            ParseScalingList(&scaling_list_data, nalu, size, offset);
        }
    }
    amp_enabled_flag = Parser::getBit(nalu, offset);
    sample_adaptive_offset_enabled_flag = Parser::getBit(nalu, offset);
    pcm_enabled_flag = Parser::getBit(nalu, offset);
    if (pcm_enabled_flag)
    {
        pcm_sample_bit_depth_luma_minus1 = Parser::readBits(nalu, offset,4);
        pcm_sample_bit_depth_chroma_minus1 = Parser::readBits(nalu, offset,4);
        log2_min_pcm_luma_coding_block_size_minus3 = Parser::ExpGolomb::readUe(nalu, offset);
        log2_diff_max_min_pcm_luma_coding_block_size = Parser::ExpGolomb::readUe(nalu, offset);
        pcm_loop_filter_disabled_flag = Parser::getBit(nalu, offset);
    }
    num_short_term_ref_pic_sets = Parser::ExpGolomb::readUe(nalu, offset);
    for (amf_uint32 i=0; i<num_short_term_ref_pic_sets; i++)
    {
        //short_term_ref_pic_set( i )
        ParseShortTermRefPicSet(&stRPS[i], i, num_short_term_ref_pic_sets, stRPS, nalu, size, offset);
    }
    long_term_ref_pics_present_flag = Parser::getBit(nalu, offset);
    if (long_term_ref_pics_present_flag)
    {
        num_long_term_ref_pics_sps = Parser::ExpGolomb::readUe(nalu, offset);
        ltRPS.num_of_pics = num_long_term_ref_pics_sps;
        for (amf_uint32 i=0; i<num_long_term_ref_pics_sps; i++)
        {
            //The number of bits used to represent lt_ref_pic_poc_lsb_sps[ i ] is equal to log2_max_pic_order_cnt_lsb_minus4 + 4.
            lt_ref_pic_poc_lsb_sps[i] = Parser::readBits(nalu, offset,(log2_max_pic_order_cnt_lsb_minus4 + 4));
            used_by_curr_pic_lt_sps_flag[i] = Parser::getBit(nalu, offset);
            ltRPS.POCs[i]=lt_ref_pic_poc_lsb_sps[i];
            ltRPS.used_by_curr_pic[i] = used_by_curr_pic_lt_sps_flag[i];            
        }
    }
    sps_temporal_mvp_enabled_flag = Parser::getBit(nalu, offset);
    strong_intra_smoothing_enabled_flag = Parser::getBit(nalu, offset);
    vui_parameters_present_flag = Parser::getBit(nalu, offset);
    if (vui_parameters_present_flag)
    {
        //vui_parameters()
        ParseVUI(&vui_parameters, sps_max_sub_layers_minus1, nalu, size, offset);
    }
    sps_extension_flag = Parser::getBit(nalu, offset);
    if( sps_extension_flag )
    {
        //while( more_rbsp_data( ) )
            //sps_extension_data_flag u(1)
    }
    return true;
}
//-------------------------------------------------------------------------------------------------
bool HevcParser::PpsData::Parse(amf_uint8 *nalu, size_t size)
{
    size_t offset = 16; // 2 bytes NALU header

    amf_uint32 PPS_ID = Parser::ExpGolomb::readUe(nalu, offset);
    
    pps_pic_parameter_set_id = PPS_ID;
    amf_uint32 activeSPS = Parser::ExpGolomb::readUe(nalu, offset);

    pps_seq_parameter_set_id = activeSPS;
    dependent_slice_segments_enabled_flag = Parser::getBit(nalu, offset);
    output_flag_present_flag = Parser::getBit(nalu, offset);
    num_extra_slice_header_bits = Parser::readBits(nalu, offset,3);
    sign_data_hiding_enabled_flag = Parser::getBit(nalu, offset);
    cabac_init_present_flag = Parser::getBit(nalu, offset);
    num_ref_idx_l0_default_active_minus1 = Parser::ExpGolomb::readUe(nalu, offset);
    num_ref_idx_l1_default_active_minus1 = Parser::ExpGolomb::readUe(nalu, offset);
    init_qp_minus26 = Parser::ExpGolomb::readSe(nalu, offset);
    constrained_intra_pred_flag = Parser::getBit(nalu, offset);
    transform_skip_enabled_flag = Parser::getBit(nalu, offset);
    cu_qp_delta_enabled_flag = Parser::getBit(nalu, offset);
    if (cu_qp_delta_enabled_flag)
    {
        diff_cu_qp_delta_depth = Parser::ExpGolomb::readUe(nalu, offset);
    }
    pps_cb_qp_offset = Parser::ExpGolomb::readSe(nalu, offset);
    pps_cr_qp_offset = Parser::ExpGolomb::readSe(nalu, offset);
    pps_slice_chroma_qp_offsets_present_flag = Parser::getBit(nalu, offset);
    weighted_pred_flag = Parser::getBit(nalu, offset);
    weighted_bipred_flag = Parser::getBit(nalu, offset);
    transquant_bypass_enabled_flag = Parser::getBit(nalu, offset);
    tiles_enabled_flag = Parser::getBit(nalu, offset);
    entropy_coding_sync_enabled_flag = Parser::getBit(nalu, offset);
    if (tiles_enabled_flag)
    {
        num_tile_columns_minus1 = Parser::ExpGolomb::readUe(nalu, offset);
        num_tile_rows_minus1 = Parser::ExpGolomb::readUe(nalu, offset);
        uniform_spacing_flag = Parser::getBit(nalu, offset);
        if (!uniform_spacing_flag)
        {
            for (amf_uint32 i=0; i<num_tile_columns_minus1; i++)
            {
                column_width_minus1[i] = Parser::ExpGolomb::readUe(nalu, offset);
            }
            for (amf_uint32 i=0; i<num_tile_rows_minus1; i++)
            {
                row_height_minus1[i] = Parser::ExpGolomb::readUe(nalu, offset);
            }
        }
        loop_filter_across_tiles_enabled_flag = Parser::getBit(nalu, offset);
    }
    else
         loop_filter_across_tiles_enabled_flag = 1;
    pps_loop_filter_across_slices_enabled_flag = Parser::getBit(nalu, offset);
    deblocking_filter_control_present_flag = Parser::getBit(nalu, offset);
    if (deblocking_filter_control_present_flag)
    {
        deblocking_filter_override_enabled_flag = Parser::getBit(nalu, offset);
        pps_deblocking_filter_disabled_flag = Parser::getBit(nalu, offset);
        if (!pps_deblocking_filter_disabled_flag)
        {
            pps_beta_offset_div2 = Parser::ExpGolomb::readSe(nalu, offset);
            pps_tc_offset_div2 = Parser::ExpGolomb::readSe(nalu, offset);
        }
    }
    pps_scaling_list_data_present_flag = Parser::getBit(nalu, offset);
    if (pps_scaling_list_data_present_flag)
    {
        SpsData::ParseScalingList(&scaling_list_data, nalu, size, offset);
    }
    lists_modification_present_flag = Parser::getBit(nalu, offset);
    log2_parallel_merge_level_minus2 = Parser::ExpGolomb::readUe(nalu, offset);
    slice_segment_header_extension_present_flag = Parser::getBit(nalu, offset);
    pps_extension_flag = Parser::getBit(nalu, offset);
    if (pps_extension_flag)
    {
        //while( more_rbsp_data( ) )
            //pps_extension_data_flag u(1)
        //rbsp_trailing_bits( )
    }
    return true;
}
//-------------------------------------------------------------------------------------------------
void HevcParser::SpsData::ParsePTL(AMFH265_profile_tier_level_t *ptl, amf_bool profilePresentFlag, amf_uint32 maxNumSubLayersMinus1, amf_uint8 *nalu, size_t size, size_t &offset)
{
    if(profilePresentFlag)
    {
        ptl->general_profile_space = Parser::readBits(nalu, offset,2);
        ptl->general_tier_flag = Parser::getBit(nalu, offset);
        ptl->general_profile_idc = Parser::readBits(nalu, offset,5);
        for (int i=0; i < 32; i++)
        {
            ptl->general_profile_compatibility_flag[i] = Parser::getBit(nalu, offset);
        }
        ptl->general_progressive_source_flag = Parser::getBit(nalu, offset);
        ptl->general_interlaced_source_flag = Parser::getBit(nalu, offset);
        ptl->general_non_packed_constraint_flag = Parser::getBit(nalu, offset);
        ptl->general_frame_only_constraint_flag = Parser::getBit(nalu, offset);
        //readBits is limited to 32 
//        ptl->general_reserved_zero_44bits = Parser::readBits(nalu, offset,44);
        offset+=44;
    }

    ptl->general_level_idc = Parser::readBits(nalu, offset,8);
    for(amf_uint32 i=0; i < maxNumSubLayersMinus1; i++)
    {
        ptl->sub_layer_profile_present_flag[i] = Parser::getBit(nalu, offset);
        ptl->sub_layer_level_present_flag[i] = Parser::getBit(nalu, offset);
    }
    if (maxNumSubLayersMinus1 > 0)
    {
        for(amf_uint32 i=maxNumSubLayersMinus1; i<8; i++)
        {               
            ptl->reserved_zero_2bits[i] = Parser::readBits(nalu, offset,2);
        }
    }
    for(amf_uint32 i=0; i<maxNumSubLayersMinus1; i++)
    {
        if(ptl->sub_layer_profile_present_flag[i])
        {
            ptl->sub_layer_profile_space[i] = Parser::readBits(nalu, offset,2);
            ptl->sub_layer_tier_flag[i] = Parser::getBit(nalu, offset);
            ptl->sub_layer_profile_idc[i] = Parser::readBits(nalu, offset,5);
            for(int j = 0; j<32; j++)
            {
                ptl->sub_layer_profile_compatibility_flag[i][j] = Parser::getBit(nalu, offset);
            }
            ptl->sub_layer_progressive_source_flag[i] = Parser::getBit(nalu, offset);
            ptl->sub_layer_interlaced_source_flag[i] = Parser::getBit(nalu, offset);
            ptl->sub_layer_non_packed_constraint_flag[i] = Parser::getBit(nalu, offset);
            ptl->sub_layer_frame_only_constraint_flag[i] = Parser::getBit(nalu, offset);
            ptl->sub_layer_reserved_zero_44bits[i] = Parser::readBits(nalu, offset,44);
        }
        if(ptl->sub_layer_level_present_flag[i])
        {
            ptl->sub_layer_level_idc[i] = Parser::readBits(nalu, offset,8);
        }
    }
}
//-------------------------------------------------------------------------------------------------
void HevcParser::SpsData::ParseSubLayerHrdParameters(AMFH265_sub_layer_hrd_parameters *sub_hrd, amf_uint32 CpbCnt, amf_bool sub_pic_hrd_params_present_flag, amf_uint8 *nalu, size_t size,size_t &offset)
{
    for (amf_uint32 i=0; i<=CpbCnt; i++)
    {
        sub_hrd->bit_rate_value_minus1[i] = Parser::ExpGolomb::readUe(nalu, offset);
        sub_hrd->cpb_size_value_minus1[i] = Parser::ExpGolomb::readUe(nalu, offset);
        if(sub_pic_hrd_params_present_flag)
        {
            sub_hrd->cpb_size_du_value_minus1[i] = Parser::ExpGolomb::readUe(nalu, offset);
            sub_hrd->bit_rate_du_value_minus1[i] = Parser::ExpGolomb::readUe(nalu, offset);
        }
        sub_hrd->cbr_flag[i] = Parser::getBit(nalu, offset);
    }
}
//-------------------------------------------------------------------------------------------------
void HevcParser::SpsData::ParseHrdParameters(AMFH265_hrd_parameters_t *hrd, amf_bool commonInfPresentFlag, amf_uint32 maxNumSubLayersMinus1, amf_uint8 *nalu, size_t size,size_t &offset)
{
    if (commonInfPresentFlag)
    {
        hrd->nal_hrd_parameters_present_flag = Parser::getBit(nalu, offset);
        hrd->vcl_hrd_parameters_present_flag = Parser::getBit(nalu, offset);
        if (hrd->nal_hrd_parameters_present_flag || hrd->vcl_hrd_parameters_present_flag)
        {
            hrd->sub_pic_hrd_params_present_flag = Parser::getBit(nalu, offset);
            if (hrd->sub_pic_hrd_params_present_flag)
            {
                hrd->tick_divisor_minus2 = Parser::readBits(nalu, offset,8);
                hrd->du_cpb_removal_delay_increment_length_minus1 = Parser::readBits(nalu, offset,5);
                hrd->sub_pic_cpb_params_in_pic_timing_sei_flag = Parser::getBit(nalu, offset);
                hrd->dpb_output_delay_du_length_minus1 = Parser::readBits(nalu, offset,5);
            }
            hrd->bit_rate_scale = Parser::readBits(nalu, offset,4);
            hrd->cpb_size_scale = Parser::readBits(nalu, offset,4);
            if (hrd->sub_pic_hrd_params_present_flag)
            {
                hrd->cpb_size_du_scale = Parser::readBits(nalu, offset,4);
            }
            hrd->initial_cpb_removal_delay_length_minus1 = Parser::readBits(nalu, offset,5);
            hrd->au_cpb_removal_delay_length_minus1 = Parser::readBits(nalu, offset,5);
            hrd->dpb_output_delay_length_minus1 = Parser::readBits(nalu, offset,5);
        }
    }
    for (amf_uint32 i=0; i<= maxNumSubLayersMinus1; i++)
    {
        hrd->fixed_pic_rate_general_flag[i] = Parser::getBit(nalu, offset);
        if (!hrd->fixed_pic_rate_general_flag[i])
        {
            hrd->fixed_pic_rate_within_cvs_flag[i] = Parser::getBit(nalu, offset);
        }
        if (hrd->fixed_pic_rate_within_cvs_flag[i])
        {
            hrd->elemental_duration_in_tc_minus1[i] = Parser::ExpGolomb::readUe(nalu, offset);
        }
        else
        {
            hrd->low_delay_hrd_flag[i] = Parser::getBit(nalu, offset);
        }
        if (!hrd->low_delay_hrd_flag[i])
        {
            hrd->cpb_cnt_minus1[i] = Parser::ExpGolomb::readUe(nalu, offset);
        }
        if (hrd->nal_hrd_parameters_present_flag)
        {
            //sub_layer_hrd_parameters( i )
            ParseSubLayerHrdParameters(&hrd->sub_layer_hrd_parameters_0[i], hrd->cpb_cnt_minus1[i], hrd->sub_pic_hrd_params_present_flag, nalu, size, offset);
        }
        if (hrd->vcl_hrd_parameters_present_flag)
        {
            //sub_layer_hrd_parameters( i )
            ParseSubLayerHrdParameters(&hrd->sub_layer_hrd_parameters_1[i], hrd->cpb_cnt_minus1[i], hrd->sub_pic_hrd_params_present_flag, nalu, size, offset);
        }
    }
}
//-------------------------------------------------------------------------------------------------
void HevcParser::SpsData::ParseScalingList(AMFH265_scaling_list_data_t * s_data, amf_uint8 *nalu, size_t size, size_t &offset)
{
    for (int sizeId=0; sizeId < 4; sizeId++)
    {
        for (int matrixId=0; matrixId < ((sizeId == 3)? 2:6); matrixId++)
        {
            s_data->scaling_list_pred_mode_flag[sizeId][matrixId] = Parser::getBit(nalu, offset);
            if(!s_data->scaling_list_pred_mode_flag[sizeId][matrixId])
            {
                int refMatrixId = matrixId - s_data->scaling_list_pred_matrix_id_delta[sizeId][matrixId];
                int coefNum = std::min(64, (1<< (4 + (sizeId<<1))));

                s_data->scaling_list_pred_matrix_id_delta[sizeId][matrixId] = Parser::ExpGolomb::readUe(nalu, offset);
                //fill in scaling_list_dc_coef_minus8
                if (!s_data->scaling_list_pred_matrix_id_delta[sizeId][matrixId])
                {
                    if (sizeId>1)
                    {
                        s_data->scaling_list_dc_coef_minus8[sizeId-2][matrixId] = 8;
                    }
                }
                else
                {
                    if (sizeId>1)
                    {
                        s_data->scaling_list_dc_coef_minus8[sizeId-2][matrixId] = s_data->scaling_list_dc_coef_minus8[sizeId-2][refMatrixId];
                    }
                }

                for (int i=0; i<coefNum; i++)
                {
                    if (s_data->scaling_list_pred_matrix_id_delta[sizeId][matrixId] == 0)
                    {
                        if (sizeId == 0)
                        {
                            s_data->ScalingList[sizeId][matrixId][i] = scaling_list_default_0[sizeId][matrixId][i];
                        }
                        else if(sizeId == 1 || sizeId == 2)
                        {
                            s_data->ScalingList[sizeId][matrixId][i] = scaling_list_default_1_2[sizeId][matrixId][i];
                        }
                        else if(sizeId == 3)
                        {
                            s_data->ScalingList[sizeId][matrixId][i] = scaling_list_default_3[sizeId][matrixId][i];
                        }
                    }
                    else
                    {
                        s_data->ScalingList[sizeId][matrixId][i] = s_data->ScalingList[sizeId][refMatrixId][i];
                    }
                }
            }
            else
            {
                int nextCoef = 8;
                int coefNum = std::min(64, (1<< (4 + (sizeId<<1))));
                if (sizeId > 1)
                {
                    s_data->scaling_list_dc_coef_minus8[sizeId-2][matrixId] = Parser::ExpGolomb::readSe(nalu, offset);
                    nextCoef = s_data->scaling_list_dc_coef_minus8[sizeId-2][matrixId] + 8;
                }
                for (int i=0; i < coefNum; i++)
                {
                    s_data->scaling_list_delta_coef = Parser::ExpGolomb::readSe(nalu, offset);
                    nextCoef = (nextCoef + s_data->scaling_list_delta_coef +256)%256;
                    s_data->ScalingList[sizeId][matrixId][i] = nextCoef;
                }
            }
        }
    }
}
void HevcParser::SpsData::ParseShortTermRefPicSet(AMFH265_short_term_RPS_t *rps, amf_int32 stRpsIdx, amf_uint32 num_short_term_ref_pic_sets, AMFH265_short_term_RPS_t rps_ref[], amf_uint8 *nalu, size_t size,size_t &offset)
{
    amf_uint32 interRPSPred = 0;
    amf_uint32 delta_idx_minus1 = 0;
    amf_int32 i=0;

    if (stRpsIdx != 0)
    {
        interRPSPred = Parser::getBit(nalu, offset);
    }
    if (interRPSPred)
    {
        amf_uint32 delta_rps_sign, abs_delta_rps_minus1;
        amf_bool used_by_curr_pic_flag[16], use_delta_flag[16];
        if (stRpsIdx == num_short_term_ref_pic_sets)
        {
            delta_idx_minus1 = Parser::ExpGolomb::readUe(nalu, offset);
        }
        delta_rps_sign = Parser::getBit(nalu, offset);
        abs_delta_rps_minus1 = Parser::ExpGolomb::readUe(nalu, offset);
        amf_int32 delta_rps = (amf_int32) (1 - 2*delta_rps_sign) * (abs_delta_rps_minus1 + 1);
        amf_int32 k=0, k0=0, k1=0, k2 = 0, k3 = 0;
        amf_int32 ref_idx = stRpsIdx - delta_idx_minus1 - 1;
        for (int j=0; j<= (rps_ref[ref_idx].num_negative_pics + rps_ref[ref_idx].num_positive_pics); j++)
        {
            used_by_curr_pic_flag[j] = Parser::getBit(nalu, offset);
            if (!used_by_curr_pic_flag[j])
            {
                use_delta_flag[j] = Parser::getBit(nalu, offset);
            }
            else
            {
                use_delta_flag[j] = 1;
            }
        }

        for (int j=rps_ref[ref_idx].num_positive_pics - 1; j>= 0; j--)
        {
            amf_int32 delta_poc = delta_rps + rps_ref[ref_idx].deltaPOC[rps_ref[ref_idx].num_negative_pics + j];  //positive deltaPOC from ref_rps
            if (delta_poc<0 && use_delta_flag[rps_ref[ref_idx].num_negative_pics + j])
            {
                rps->deltaPOC[i] = delta_poc;
                rps->used_by_curr_pic[i++] = used_by_curr_pic_flag[rps_ref[ref_idx].num_negative_pics + j];
            }
        }
        if (delta_rps < 0 && use_delta_flag[rps_ref[ref_idx].num_of_pics])
        {
            rps->deltaPOC[i] = delta_rps;
            rps->used_by_curr_pic[i++] = used_by_curr_pic_flag[rps_ref[ref_idx].num_of_pics];
        }
        for (int j=0; j<rps_ref[ref_idx].num_negative_pics; j++)
        {
            amf_int32 delta_poc = delta_rps + rps_ref[ref_idx].deltaPOC[j];
            if (delta_poc < 0 && use_delta_flag[j])
            {
                rps->deltaPOC[i]=delta_poc;
                rps->used_by_curr_pic[i++] = used_by_curr_pic_flag[j];
            }
        }
        rps->num_negative_pics = i;
        
        
        for (int j=rps_ref[ref_idx].num_negative_pics - 1; j>= 0; j--)
        {
            amf_int32 delta_poc = delta_rps + rps_ref[ref_idx].deltaPOC[j];  //positive deltaPOC from ref_rps
            if (delta_poc>0 && use_delta_flag[j])
            {
                rps->deltaPOC[i] = delta_poc;
                rps->used_by_curr_pic[i++] = used_by_curr_pic_flag[j];
            }
        }
        if (delta_rps > 0 && use_delta_flag[rps_ref[ref_idx].num_of_pics])
        {
            rps->deltaPOC[i] = delta_rps;
            rps->used_by_curr_pic[i++] = used_by_curr_pic_flag[rps_ref[ref_idx].num_of_pics];
        }
        for (int j=0; j<rps_ref[ref_idx].num_positive_pics; j++)
        {
            amf_int32 delta_poc = delta_rps + rps_ref[ref_idx].deltaPOC[rps_ref[ref_idx].num_negative_pics+j];
            if (delta_poc > 0 && use_delta_flag[rps_ref[ref_idx].num_negative_pics+j])
            {
                rps->deltaPOC[i]=delta_poc;
                rps->used_by_curr_pic[i++] = used_by_curr_pic_flag[rps_ref[ref_idx].num_negative_pics+j];
            }
        }
        rps->num_positive_pics = i -rps->num_negative_pics ;
        rps->num_of_delta_poc = rps_ref[ref_idx].num_negative_pics + rps_ref[ref_idx].num_positive_pics;
        rps->num_of_pics = i;

    }
    else
    {
        rps->num_negative_pics = Parser::ExpGolomb::readUe(nalu, offset);
        rps->num_positive_pics = Parser::ExpGolomb::readUe(nalu, offset);
        amf_int32 prev = 0;
        amf_int32 poc;
        amf_uint32 delta_poc_s0_minus1,delta_poc_s1_minus1;
        for (int j=0; j < rps->num_negative_pics; j++)
        {
            delta_poc_s0_minus1 = Parser::ExpGolomb::readUe(nalu, offset);
            poc = prev - delta_poc_s0_minus1 - 1;
            prev = poc;
            rps->deltaPOC[j] = poc;
            rps->used_by_curr_pic[j] = Parser::getBit(nalu, offset);
        }
        prev = 0;
        for (int j=rps->num_negative_pics; j < rps->num_negative_pics + rps->num_positive_pics; j++)
        {
            delta_poc_s1_minus1 = Parser::ExpGolomb::readUe(nalu, offset);
            poc = prev + delta_poc_s1_minus1 + 1;
            prev = poc;
            rps->deltaPOC[j] = poc;
            rps->used_by_curr_pic[j] = Parser::getBit(nalu, offset);
        }
        rps->num_of_pics = rps->num_negative_pics + rps->num_positive_pics;
        rps->num_of_delta_poc = rps->num_negative_pics + rps->num_positive_pics;
    }
}

void HevcParser::SpsData::ParseVUI(AMFH265_vui_parameters_t *vui, amf_uint32 maxNumSubLayersMinus1, amf_uint8 *nalu, size_t size, size_t &offset)
{
    vui->aspect_ratio_info_present_flag = Parser::getBit(nalu, offset);
    if (vui->aspect_ratio_info_present_flag)
    {
        vui->aspect_ratio_idc = Parser::readBits(nalu, offset,8);
        if (vui->aspect_ratio_idc == 255)
        {
            vui->sar_width = Parser::readBits(nalu, offset,16);
            vui->sar_height = Parser::readBits(nalu, offset,16);
        }
    }
    vui->overscan_info_present_flag = Parser::getBit(nalu, offset);
    if (vui->overscan_info_present_flag)
    {
        vui->overscan_appropriate_flag = Parser::getBit(nalu, offset);
    }
    vui->video_signal_type_present_flag = Parser::getBit(nalu, offset);
    if(vui->video_signal_type_present_flag)
    {
        vui->video_format = Parser::readBits(nalu, offset,3);
        vui->video_full_range_flag = Parser::getBit(nalu, offset);
        vui->colour_description_present_flag = Parser::getBit(nalu, offset);
        if (vui->colour_description_present_flag)
        {
            vui->colour_primaries = Parser::readBits(nalu, offset,8);
            vui->transfer_characteristics = Parser::readBits(nalu, offset,8);
            vui->matrix_coeffs = Parser::readBits(nalu, offset,8);
        }
    }
    vui->chroma_loc_info_present_flag = Parser::getBit(nalu, offset);
    if (vui->chroma_loc_info_present_flag)
    {
        vui->chroma_sample_loc_type_top_field = Parser::ExpGolomb::readUe(nalu, offset);
        vui->chroma_sample_loc_type_bottom_field = Parser::ExpGolomb::readUe(nalu, offset);
    }
    vui->neutral_chroma_indication_flag = Parser::getBit(nalu, offset);
    vui->field_seq_flag = Parser::getBit(nalu, offset);
    vui->frame_field_info_present_flag = Parser::getBit(nalu, offset);
    vui->default_display_window_flag = Parser::getBit(nalu, offset);
    if (vui->default_display_window_flag)
    {
        vui->def_disp_win_left_offset = Parser::ExpGolomb::readUe(nalu, offset);
        vui->def_disp_win_right_offset = Parser::ExpGolomb::readUe(nalu, offset);
        vui->def_disp_win_top_offset = Parser::ExpGolomb::readUe(nalu, offset);
        vui->def_disp_win_bottom_offset = Parser::ExpGolomb::readUe(nalu, offset);
    }
    vui->vui_timing_info_present_flag = Parser::getBit(nalu, offset);
    if (vui->vui_timing_info_present_flag)
    {
        vui->vui_num_units_in_tick = Parser::readBits(nalu, offset,32);
        vui->vui_time_scale = Parser::readBits(nalu, offset,32);
        vui->vui_poc_proportional_to_timing_flag = Parser::getBit(nalu, offset);
        if (vui->vui_poc_proportional_to_timing_flag)
        {
            vui->vui_num_ticks_poc_diff_one_minus1 = Parser::ExpGolomb::readUe(nalu, offset);
        }
        vui->vui_hrd_parameters_present_flag = Parser::getBit(nalu, offset);
        if (vui->vui_hrd_parameters_present_flag)
        {
            ParseHrdParameters(&vui->hrd_parameters, 1, maxNumSubLayersMinus1, nalu, size, offset);
        }
    }
    vui->bitstream_restriction_flag = Parser::getBit(nalu, offset);
    if (vui->bitstream_restriction_flag)
    {
        vui->tiles_fixed_structure_flag = Parser::getBit(nalu, offset);
        vui->motion_vectors_over_pic_boundaries_flag = Parser::getBit(nalu, offset);
        vui->restricted_ref_pic_lists_flag = Parser::getBit(nalu, offset);
        vui->min_spatial_segmentation_idc = Parser::ExpGolomb::readUe(nalu, offset);
        vui->max_bytes_per_pic_denom = Parser::ExpGolomb::readUe(nalu, offset);
        vui->max_bits_per_min_cu_denom = Parser::ExpGolomb::readUe(nalu, offset);
        vui->log2_max_mv_length_horizontal = Parser::ExpGolomb::readUe(nalu, offset);
        vui->log2_max_mv_length_vertical = Parser::ExpGolomb::readUe(nalu, offset);
    }
}


//-------------------------------------------------------------------------------------------------
bool HevcParser::AccessUnitSigns::Parse(amf_uint8 *nalu, size_t size, std::map<amf_uint32,SpsData> &spsMap, std::map<amf_uint32,PpsData> &ppsMap)
{
    size_t offset = 16; // 2 bytes NALU header

    bNewPicture = Parser::getBit(nalu, offset);
    return true;
}
//-------------------------------------------------------------------------------------------------
bool HevcParser::AccessUnitSigns::IsNewPicture()
{
    return bNewPicture;
}
//-------------------------------------------------------------------------------------------------
void HevcParser::ExtraDataBuilder::AddSPS(amf_uint8 *sps, size_t size)
{
    m_SPSCount++;
    size_t pos = m_SPSs.GetSize();
    amf_uint16 spsSize = size & maxSpsSize;
    m_SPSs.SetSize(pos + spsSize +2);
    amf_uint8 *data = m_SPSs.GetData() + pos;
    *data++ = Parser::getLowByte(spsSize);
    *data++ = Parser::getHiByte(spsSize);
    memcpy(data , sps, (size_t)spsSize);
}
//-------------------------------------------------------------------------------------------------
void HevcParser::ExtraDataBuilder::AddPPS(amf_uint8 *pps, size_t size)
{
    m_PPSCount++;
    size_t pos = m_PPSs.GetSize();
    amf_uint16 ppsSize = size & maxPpsSize;
    m_PPSs.SetSize(pos + ppsSize +2);
    amf_uint8 *data = m_PPSs.GetData() + pos;
    *data++ = Parser::getLowByte(ppsSize);
    *data++ = Parser::getHiByte(ppsSize);
    memcpy(data , pps, (size_t)ppsSize);
}
//-------------------------------------------------------------------------------------------------
// ISO-IEC 14496-15-2004.pdf, #5.2.4.1.1
//-------------------------------------------------------------------------------------------------
bool HevcParser::ExtraDataBuilder::GetExtradata(AMFByteArray   &extradata)
{
    if( m_SPSs.GetSize() == 0  || m_PPSs .GetSize() ==0 )
    {
        return false;
    }

    if (m_SPSCount > 0x1F)
    {
        return false;
    }


    if (m_SPSs.GetSize() < minSpsSize)
    {
        return false;
    }

    extradata.SetSize(
        21 +                // reserved
        1 +                 // length size
        1 +                 // array size
        3 +                 // SPS type + SPS count (2)
        m_SPSs.GetSize() +
        3 +                 // PPS type + PPS count (2)
        m_PPSs .GetSize()
        );

    amf_uint8 *data = extradata.GetData();
    
    memset(data, 0, extradata.GetSize());

    *data = 0x01; // configurationVersion
    data+=21;
    *data++ = (0xFC | (NalUnitLengthSize - 1));   // reserved(11111100) + lengthSizeMinusOne

    *data++ = static_cast<amf_uint8>(2); // reserved(11100000) + numOfSequenceParameterSets


    *data++ = NAL_UNIT_SPS;
    *data++ = Parser::getLowByte(m_SPSCount);
    *data++ = Parser::getHiByte(m_SPSCount);

    memcpy(data, m_SPSs.GetData(), m_SPSs.GetSize());
    data += m_SPSs.GetSize();


    *data++ = NAL_UNIT_PPS;
    *data++ = Parser::getLowByte(m_PPSCount);
    *data++ = Parser::getHiByte(m_PPSCount);
    memcpy(data, m_PPSs.GetData(), m_PPSs.GetSize());
    data += m_PPSs.GetSize();
    return true;
}
//-------------------------------------------------------------------------------------------------
#define ZEROBYTES_SHORTSTARTCODE 2 //indicates the number of zero bytes in the short start-code prefix
//-------------------------------------------------------------------------------------------------
size_t HevcParser::EBSPtoRBSP(amf_uint8 *streamBuffer,size_t begin_bytepos, size_t end_bytepos)
{
    int count = 0;
    if(end_bytepos < begin_bytepos)
    {
        return end_bytepos;
    }
    amf_uint8 *streamBuffer_i=streamBuffer+begin_bytepos;
    amf_uint8 *streamBuffer_end=streamBuffer+end_bytepos;
    int iReduceCount=0;
    for(; streamBuffer_i!=streamBuffer_end; )
    { 
        //starting from begin_bytepos to avoid header information
        //in NAL unit, 0x000000, 0x000001 or 0x000002 shall not occur at any amf_uint8-aligned position
        register amf_uint8 tmp=*streamBuffer_i;
        if(count == ZEROBYTES_SHORTSTARTCODE)
        {
            if(tmp == 0x03)
            {
                //check the 4th amf_uint8 after 0x000003, except when cabac_zero_word is used, in which case the last three bytes of this NAL unit must be 0x000003
                if((streamBuffer_i+1 != streamBuffer_end) && (streamBuffer_i[1] > 0x03))
                {
                    return -1;
                }
                //if cabac_zero_word is used, the final amf_uint8 of this NAL unit(0x03) is discarded, and the last two bytes of RBSP must be 0x0000
                if(streamBuffer_i+1 == streamBuffer_end)
                {
                    break;
                }
                memmove(streamBuffer_i,streamBuffer_i+1,streamBuffer_end-streamBuffer_i-1);
                streamBuffer_end--;
                iReduceCount++;
                count = 0;
                tmp = *streamBuffer_i;
            }
            else if(tmp < 0x03) 
            {
            }
        }
        if(tmp == 0x00)
        {
            count++;
        }
        else
        {
            count = 0;
        }
        streamBuffer_i++;
    }
    return end_bytepos - begin_bytepos + iReduceCount;
}

//sizeId = 0
int scaling_list_default_0 [1][6][16] =  {{{16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16},
                                           {16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16},
                                           {16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16},
                                           {16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16},
                                           {16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16},
                                           {16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16}}};
//sizeId = 1, 2
int scaling_list_default_1_2 [2][6][64] = {{{16,16,16,16,16,16,16,16,16,16,17,16,17,16,17,18,17,18,18,17,18,21,19,20,21,20,19,21,24,22,22,24,24,22,22,24,25,25,27,30,27,25,25,29,31,35,35,31,29,36,41,44,41,36,47,54,54,47,65,70,65,88,88,115},
                                            {16,16,16,16,16,16,16,16,16,16,17,16,17,16,17,18,17,18,18,17,18,21,19,20,21,20,19,21,24,22,22,24,24,22,22,24,25,25,27,30,27,25,25,29,31,35,35,31,29,36,41,44,41,36,47,54,54,47,65,70,65,88,88,115},
                                            {16,16,16,16,16,16,16,16,16,16,17,16,17,16,17,18,17,18,18,17,18,21,19,20,21,20,19,21,24,22,22,24,24,22,22,24,25,25,27,30,27,25,25,29,31,35,35,31,29,36,41,44,41,36,47,54,54,47,65,70,65,88,88,115},
                                            {16,16,16,16,16,16,16,16,16,16,17,17,17,17,17,18,18,18,18,18,18,20,20,20,20,20,20,20,24,24,24,24,24,24,24,24,25,25,25,25,25,25,25,28,28,28,28,28,28,33,33,33,33,33,41,41,41,41,54,54,54,71,71,91},
                                            {16,16,16,16,16,16,16,16,16,16,17,17,17,17,17,18,18,18,18,18,18,20,20,20,20,20,20,20,24,24,24,24,24,24,24,24,25,25,25,25,25,25,25,28,28,28,28,28,28,33,33,33,33,33,41,41,41,41,54,54,54,71,71,91},
                                            {16,16,16,16,16,16,16,16,16,16,17,17,17,17,17,18,18,18,18,18,18,20,20,20,20,20,20,20,24,24,24,24,24,24,24,24,25,25,25,25,25,25,25,28,28,28,28,28,28,33,33,33,33,33,41,41,41,41,54,54,54,71,71,91}},
                                           {{16,16,16,16,16,16,16,16,16,16,17,16,17,16,17,18,17,18,18,17,18,21,19,20,21,20,19,21,24,22,22,24,24,22,22,24,25,25,27,30,27,25,25,29,31,35,35,31,29,36,41,44,41,36,47,54,54,47,65,70,65,88,88,115},
                                            {16,16,16,16,16,16,16,16,16,16,17,16,17,16,17,18,17,18,18,17,18,21,19,20,21,20,19,21,24,22,22,24,24,22,22,24,25,25,27,30,27,25,25,29,31,35,35,31,29,36,41,44,41,36,47,54,54,47,65,70,65,88,88,115},
                                            {16,16,16,16,16,16,16,16,16,16,17,16,17,16,17,18,17,18,18,17,18,21,19,20,21,20,19,21,24,22,22,24,24,22,22,24,25,25,27,30,27,25,25,29,31,35,35,31,29,36,41,44,41,36,47,54,54,47,65,70,65,88,88,115},
                                            {16,16,16,16,16,16,16,16,16,16,17,17,17,17,17,18,18,18,18,18,18,20,20,20,20,20,20,20,24,24,24,24,24,24,24,24,25,25,25,25,25,25,25,28,28,28,28,28,28,33,33,33,33,33,41,41,41,41,54,54,54,71,71,91},
                                            {16,16,16,16,16,16,16,16,16,16,17,17,17,17,17,18,18,18,18,18,18,20,20,20,20,20,20,20,24,24,24,24,24,24,24,24,25,25,25,25,25,25,25,28,28,28,28,28,28,33,33,33,33,33,41,41,41,41,54,54,54,71,71,91},
                                            {16,16,16,16,16,16,16,16,16,16,17,17,17,17,17,18,18,18,18,18,18,20,20,20,20,20,20,20,24,24,24,24,24,24,24,24,25,25,25,25,25,25,25,28,28,28,28,28,28,33,33,33,33,33,41,41,41,41,54,54,54,71,71,91}}};
//sizeId = 3
int scaling_list_default_3 [1][2][64] = {{{16,16,16,16,16,16,16,16,16,16,17,16,17,16,17,18,17,18,18,17,18,21,19,20,21,20,19,21,24,22,22,24,24,22,22,24,25,25,27,30,27,25,25,29,31,35,35,31,29,36,41,44,41,36,47,54,54,47,65,70,65,88,88,115},
                                          {16,16,16,16,16,16,16,16,16,16,17,17,17,17,17,18,18,18,18,18,18,20,20,20,20,20,20,20,24,24,24,24,24,24,24,24,25,25,25,25,25,25,25,28,28,28,28,28,28,33,33,33,33,33,41,41,41,41,54,54,54,71,71,91}}};

//-------------------------------------------------------------------------------------------------
AMF_RESULT              HevcParser::ReInit()
{
    m_currentFrameTimestamp = 0;
    m_pStream->Seek(amf::AMF_SEEK_BEGIN, 0, NULL);
    m_PacketCount = 0;
    m_bEof = false;
    return AMF_OK;
}
