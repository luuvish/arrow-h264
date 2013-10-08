#include "global.h"
#include "input_parameters.h"
#include "h264decoder.h"
#include "report.h"

#include "slice.h"
#include "macroblock.h"
#include "data_partition.h"
#include "bitstream_cabac.h"
#include "image.h"
#include "memalloc.h"
#include "dpb.h"
#include "fmo.h"
#include "output.h"
#include "parset.h"
#include "sei.h"

using vio::h264::mb_t;

#include "erc_api.h"
#include "output.h"

// Decoder definition. This should be the only global variable in the entire
// software. Global variables should be avoided.
DecoderParams  *p_Dec;
char errortext[ET_SIZE];


void error(const char *text, int code)
{
    fprintf(stderr, "%s\n", text);
    if (p_Dec) {
        flush_dpb(p_Dec->p_Vid->p_Dpb_layer[0]);
#if (MVC_EXTENSION_ENABLE)
        flush_dpb(p_Dec->p_Vid->p_Dpb_layer[1]);
#endif
    }

    exit(code);
}



VideoParameters::VideoParameters()
{
    this->out_buffer = new frame_store {};
    this->old_slice  = new slice_backup_t {};
    this->snr        = new SNRParameters;

    // Allocate new dpb buffer
    for (int i = 0; i < MAX_NUM_DPB_LAYERS; i++) {
        this->p_Dpb_layer[i] = new dpb_t;
        this->p_Dpb_layer[i]->layer_id = i;
        this->p_Dpb_layer[i]->p_Vid = this;
        this->p_Dpb_layer[i]->init_done = 0;

        this->p_EncodePar[i] = new CodingParameters;
        this->p_EncodePar[i]->layer_id = i;

        this->p_LayerPar[i] = new LayerParameters;
        this->p_LayerPar[i]->layer_id = i;
    }
    this->global_init_done[0] = 0;
    this->global_init_done[1] = 0;

    this->seiToneMapping = new ToneMappingSEI;

    this->ppSliceList = new slice_t*[MAX_NUM_DECSLICES];

    this->iNumOfSlicesAllocated = MAX_NUM_DECSLICES;
    this->pNextSlice            = nullptr;
    this->nalu                  = new nalu_t(MAX_CODED_FRAME_SIZE);
    this->pDecOuputPic.pY       = nullptr;
    this->pDecOuputPic.pU       = nullptr;
    this->pDecOuputPic.pV       = nullptr;
    this->pNextPPS              = new pps_t;
    this->first_sps             = 1;

    this->recovery_point        = 0;
    this->recovery_point_found  = 0;
    this->recovery_poc          = 0x7fffffff; /* set to a max value */

    this->number                = 0;
    this->type                  = I_slice;

    // B pictures
    this->snr->Bframe_ctr       = 0;
    this->snr->g_nFrame         = 0;
    this->snr->frame_ctr        = 0;
    this->snr->tot_time         = 0;

    this->dec_picture            = nullptr;

    this->MbToSliceGroupMap      = nullptr;
    this->MapUnitToSliceGroupMap = nullptr;

    this->recovery_flag          = 0;

    init_tone_mapping_sei(this->seiToneMapping);

    this->newframe               = 0;
    this->previous_frame_num     = 0;

    this->last_dec_layer_id      = -1;
}

VideoParameters::~VideoParameters()
{
    delete this->out_buffer;
    delete this->old_slice;
    delete this->snr;

    // Free new dpb layers
    for (int i = 0; i < MAX_NUM_DPB_LAYERS; i++) {
        delete this->p_Dpb_layer[i];
        delete this->p_EncodePar[i];
        delete this->p_LayerPar [i];
    }

    delete this->seiToneMapping;

    if (this->ppSliceList) {
        for (int i = 0; i < this->iNumOfSlicesAllocated; i++) {
            if (this->ppSliceList[i])
                delete this->ppSliceList[i];
        }
        delete []this->ppSliceList;
    }

    if (this->pNextSlice)
        delete this->pNextSlice;
    delete this->nalu;
    if (this->pDecOuputPic.pY)
        delete []this->pDecOuputPic.pY;
    delete this->pNextPPS;
}

#if (MVC_EXTENSION_ENABLE)
void VideoParameters::OpenOutputFiles(int view0_id, int view1_id)
{
    InputParameters *p_Inp = this->p_Inp;
    char out_ViewFileName[2][FILE_NAME_SIZE], chBuf[FILE_NAME_SIZE], *pch;  
    if (strcasecmp(p_Inp->outfile, "\"\"") != 0 && strlen(p_Inp->outfile) > 0) {
        strcpy(chBuf, p_Inp->outfile);
        pch = strrchr(chBuf, '.');
        if (pch)
            *pch = '\0';
        if (strcmp("nul", chBuf)) {
            sprintf(out_ViewFileName[0], "%s_ViewId%04d.yuv", chBuf, view0_id);
            sprintf(out_ViewFileName[1], "%s_ViewId%04d.yuv", chBuf, view1_id);
            if (this->p_out_mvc[0] >= 0) {
                close(this->p_out_mvc[0]);
                this->p_out_mvc[0] = -1;
            }
            if ((this->p_out_mvc[0] = open(out_ViewFileName[0], O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1) {
                snprintf(errortext, ET_SIZE, "Error open file %s ", out_ViewFileName[0]);
                fprintf(stderr, "%s\n", errortext);
                exit(500);
            }
      
            if (this->p_out_mvc[1] >= 0) {
                close(this->p_out_mvc[1]);
                this->p_out_mvc[1] = -1;
            }
            if ((this->p_out_mvc[1] = open(out_ViewFileName[1], O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1) {
                snprintf(errortext, ET_SIZE, "Error open file %s ", out_ViewFileName[1]);
                fprintf(stderr, "%s\n", errortext);
                exit(500);
            }
        }
    }
}
#endif


void DecoderParams::OpenDecoder(InputParameters *p_Inp)
{
    this->p_Vid = new VideoParameters;
    this->p_Vid->p_Inp = this->p_Inp = new InputParameters {};

    p_Dec = this;
    memcpy(this->p_Inp, p_Inp, sizeof(InputParameters));
    this->p_Vid->conceal_mode         = p_Inp->conceal_mode;
    this->p_Vid->snr->idr_psnr_number = p_Inp->ref_offset;

    // Set defaults
    this->p_Vid->p_out = -1;
    for (int i = 0; i < MAX_VIEW_NUM; i++)
        this->p_Vid->p_out_mvc[i] = -1;

    if (p_Inp->DecodeAllLayers == 1)
        this->p_Vid->OpenOutputFiles(0, 1);
    else { //Normal AVC      
        if (strcasecmp(p_Inp->outfile, "\"\"") != 0 && strlen(p_Inp->outfile) > 0) {
            if ((this->p_Vid->p_out_mvc[0] = open(p_Inp->outfile, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1) {
                snprintf(errortext, ET_SIZE, "Error open file %s ", p_Inp->outfile);
                error(errortext, 500);
            }
        }
        this->p_Vid->p_out = this->p_Vid->p_out_mvc[0];
    }

    if (strlen(this->p_Inp->reffile) > 0 && strcmp(this->p_Inp->reffile, "\"\"")) {
        if ((this->p_Vid->p_ref = open(this->p_Inp->reffile, O_RDONLY)) == -1) {
            fprintf(stdout, " Input reference file                   : %s does not exist \n", this->p_Inp->reffile);
            fprintf(stdout, "                                          SNR values are not available\n");
        }
    } else
        this->p_Vid->p_ref = -1;

    this->p_Vid->bitstream.open(
        this->p_Inp->infile,
        this->p_Inp->FileFormat ? bitstream_t::type::RTP : bitstream_t::type::ANNEX_B,
        this->p_Vid->nalu->max_size);

    this->p_Vid->active_sps = NULL;
    this->p_Vid->active_subset_sps = NULL;
    init_subset_sps_list(this->p_Vid->SubsetSeqParSet, MAXSPS);
}


int DecoderParams::DecodeOneFrame()
{
    int iRet = this->decode_one_frame();
    if (iRet == SOP)
        iRet = DEC_SUCCEED;
    else if (iRet == EOS)
        iRet = DEC_EOS;
    else
        iRet |= DEC_ERRMASK;
    return iRet;
}

void DecoderParams::FinitDecoder()
{
#if (MVC_EXTENSION_ENABLE)
    flush_dpb(this->p_Vid->p_Dpb_layer[0]);
    flush_dpb(this->p_Vid->p_Dpb_layer[1]);
#endif

    this->p_Vid->bitstream.reset();

    this->p_Vid->newframe = 0;
    this->p_Vid->previous_frame_num = 0;
}


void free_layer_buffers(VideoParameters *p_Vid, int layer_id)
{
    CodingParameters *cps = p_Vid->p_EncodePar[layer_id];

    if (!p_Vid->global_init_done[layer_id])
        return;

    // free mem, allocated for structure p_Vid
    if (p_Vid->active_sps->separate_colour_plane_flag) {
        for (int i = 0; i < 3; i++) {
            delete []cps->mb_data_JV[i];
            cps->mb_data_JV[i] = nullptr;
        }
    } else {
        if (cps->mb_data) {
            delete []cps->mb_data;
            cps->mb_data = nullptr;
        }
    }

    p_Vid->global_init_done[layer_id] = 0;
}

static void free_global_buffers(VideoParameters *p_Vid)
{
    if (p_Vid->dec_picture) {
        free_storable_picture(p_Vid->dec_picture);
        p_Vid->dec_picture = NULL;
    }
#if MVC_EXTENSION_ENABLE
    if (p_Vid->active_subset_sps && p_Vid->active_subset_sps->sps.Valid &&
        (p_Vid->active_subset_sps->sps.profile_idc == MVC_HIGH || p_Vid->active_subset_sps->sps.profile_idc == STEREO_HIGH))
        free_img_data(p_Vid, &p_Vid->tempData3);
#endif
}

void DecoderParams::CloseDecoder()
{
    this->p_Vid->report();
    FmoFinit(this->p_Vid);

    free_layer_buffers(this->p_Vid, 0);
    free_layer_buffers(this->p_Vid, 1);
    free_global_buffers(this->p_Vid);

    this->p_Vid->bitstream.close();

#if (MVC_EXTENSION_ENABLE)
    for (int i = 0; i < MAX_VIEW_NUM; i++) {
        if (this->p_Vid->p_out_mvc[i] != -1)
            close(this->p_Vid->p_out_mvc[i]);
    }
#endif

    if (this->p_Vid->p_ref != -1)
        close(this->p_Vid->p_ref);

#if (DISABLE_ERC == 0)
    ercClose(this->p_Vid, this->p_Vid->erc_errorVar);
#endif

    CleanUpPPS(this->p_Vid);
#if (MVC_EXTENSION_ENABLE)
    for (int i = 0; i < MAXSPS; i++)
        reset_subset_sps(this->p_Vid->SubsetSeqParSet+i);
#endif

    for (int i = 0; i < MAX_NUM_DPB_LAYERS; i++)
        free_dpb(this->p_Vid->p_Dpb_layer[i]);

    delete this->p_Inp;
    delete this->p_Vid;
}
