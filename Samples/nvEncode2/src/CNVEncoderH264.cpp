/*
 * Copyright 1993-2013 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */

#include<sstream>
#include <include/videoFormats.h>
#include <CNVEncoderH264.h>
#include <xcodeutil.h>

#include <helper_cuda_drvapi.h>    // helper file for CUDA Driver API calls and error checking
#include <include/helper_nvenc.h>

#include <nvapi.h> // NVidia NVAPI - functions to query system-info (eg. version of Geforce driver)

#ifndef INFINITE
#define INFINITE UINT_MAX
#endif

#pragma warning (disable:4311)



/**
* \file CNVEncoderH264.cpp
* \brief CNVEncoderH264 is the Class interface for the Hardware Encoder (NV Encode API H.264)
* \date 2011 
*  This file contains the CNvEncoderH264 class declaration and data structures
*/


// H264 Encoder
CNvEncoderH264::CNvEncoderH264()
{
    m_uMaxHeight = 0;
    m_uMaxWidth = 0;
    m_uCurHeight = 0;
    m_uCurWidth = 0;
    m_dwFrameNumInGOP = 0;
	memset( (void *) &m_sei_user_payload, 0, sizeof(m_sei_user_payload) );
}

CNvEncoderH264::~CNvEncoderH264()
{
	DestroyEncoder();

	if ( m_sei_user_payload.payload != NULL )
		delete [] m_sei_user_payload.payload;
}

HRESULT CNvEncoderH264::InitializeEncoder()
{
    return E_FAIL;
}

HRESULT CNvEncoderH264::InitializeEncoderCodec(void * const p)
{
	NV_ENC_CONFIG_H264_VUI_PARAMETERS *pvui;

	static const uint8_t x264_sei_uuid[16] = // X264's unregistered_user SEI
	{   // random ID number generated according to ISO-11578
		0xdc, 0x45, 0xe9, 0xbd, 0xe6, 0xd9, 0x48, 0xb7,
		0x96, 0x2c, 0xd8, 0x20, 0xd9, 0x23, 0xee, 0xef
	};

    HRESULT hr           = S_OK;
    int numFrames        = 0;
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    bool bMVCEncoding    = m_stEncoderInput.profile == NV_ENC_H264_PROFILE_STEREO ? true : false;
    m_bAsyncModeEncoding = ((m_stEncoderInput.syncMode==0) ? true : false);
	string            s; // text-buffer
	ostringstream   oss; // text-buffer to generate encoder-settings

    m_uMaxHeight         = m_stEncoderInput.maxHeight;
    m_uMaxWidth          = m_stEncoderInput.maxWidth;
    m_dwFrameWidth       = m_stEncoderInput.width;
    m_dwFrameHeight      = m_stEncoderInput.height;

    memset(&m_stInitEncParams, 0, sizeof(NV_ENC_INITIALIZE_PARAMS));
    SET_VER(m_stInitEncParams, NV_ENC_INITIALIZE_PARAMS);
    m_stInitEncParams.encodeConfig = &m_stEncodeConfig;
    SET_VER(m_stEncodeConfig, NV_ENC_CONFIG);

	pvui = reinterpret_cast<NV_ENC_CONFIG_H264_VUI_PARAMETERS *>(p);

    m_stInitEncParams.darHeight           = m_stEncoderInput.darRatioY;
    m_stInitEncParams.darWidth            = m_stEncoderInput.darRatioX;
//    m_stInitEncParams.encodeHeight        = m_uMaxHeight;
//    m_stInitEncParams.encodeWidth         = m_uMaxWidth;
    m_stInitEncParams.encodeHeight        = m_dwFrameHeight;
    m_stInitEncParams.encodeWidth         = m_dwFrameWidth;
    
    m_uCurHeight                          = m_stInitEncParams.encodeHeight;
    m_uCurWidth                           = m_stInitEncParams.encodeWidth;
    
    m_stInitEncParams.maxEncodeHeight     = m_uMaxHeight;
    m_stInitEncParams.maxEncodeWidth      = m_uMaxWidth;

    m_stInitEncParams.frameRateNum        = m_stEncoderInput.frameRateNum;
    m_stInitEncParams.frameRateDen        = m_stEncoderInput.frameRateDen;
    //Fix me add theading model
    m_stInitEncParams.enableEncodeAsync   = m_bAsyncModeEncoding;
    m_stInitEncParams.enablePTD           = !m_stEncoderInput.disable_ptd;
    m_stInitEncParams.reportSliceOffsets  = m_stEncoderInput.report_slice_offsets;
    m_stInitEncParams.enableSubFrameWrite = m_stEncoderInput.enableSubFrameWrite;
    m_stInitEncParams.encodeGUID          = m_stEncodeGUID;
    m_stInitEncParams.presetGUID          = m_stPresetGUID;

	// user_SEI: (1) Create 16-byte UUID header (this is x264's uuid)
	for( unsigned i = 0; i < sizeof(x264_sei_uuid)/sizeof(x264_sei_uuid[0]); ++i ) 
		oss << static_cast<char>(x264_sei_uuid[i]);

	// user_SEI: (2) start putting NVENC's encoder-settings
    CUresult        cuResult = CUDA_SUCCESS;
    CUdevice        cuDevice = 0;
	char            gpu_name[100];
	checkCudaErrors(cuDeviceGet(&cuDevice, m_deviceID));
	checkCudaErrors(cuDeviceGetName(gpu_name, 100, cuDevice));

	// Get the Geforce driver-version using NVAPI -
	//   NVENC functionality is a hardware+firmware implementation, so it is important
	//   to report both the GPU-hardware and the Geforce driver revision.
	NvU32             NVidia_DriverVersion;
	NvAPI_ShortString szBuildBranchString;
	NvAPI_Status      nvs = NvAPI_SYS_GetDriverAndBranchVersion( &NVidia_DriverVersion, szBuildBranchString);

	//oss << "x264 - core 141 - H.264/MPEG-4 AVC codec - Copyleft 2003-2012 - " << __DATE__ "}, NVENC API " << std::dec << NVENCAPI_MAJOR_VERSION
	oss << "CNvEncoderH264[" << __DATE__  << ", NVENC API "
		<< std::dec << NVENCAPI_MAJOR_VERSION << "."
		<< std::dec << NVENCAPI_MINOR_VERSION << "]"
		<< gpu_name;
	if ( nvs == NVAPI_OK )
		oss << " (driver " << szBuildBranchString << "," << std::dec 
			<< static_cast<unsigned>(NVidia_DriverVersion)  << ")";
	else
		oss << " (driver ???)"; // unknown driver version
	oss	<< " - options: ";

	oss << " / PROFILE=" << std::dec << m_stEncoderInput.profile;
	// NVENC PRESET - print the index-value instead of the actual GUID (which isn't really informative)
	desc_nv_enc_preset_names.value2string(m_stPresetIdx, s);
	oss << ",PRESET=" << std::dec << m_stPresetIdx;
	oss << "(" << s << ")"; // show ascii-name of the preset

    if (m_stEncoderInput.disableCodecCfg == 0)
    {
        m_stInitEncParams.encodeConfig->profileGUID                  = m_stCodecProfileGUID;
        m_stInitEncParams.encodeConfig->rcParams.averageBitRate      = m_stEncoderInput.avgBitRate;
        m_stInitEncParams.encodeConfig->rcParams.maxBitRate          = m_stEncoderInput.peakBitRate;
        m_stInitEncParams.encodeConfig->rcParams.constQP.qpIntra     = m_stEncoderInput.qpI;
        m_stInitEncParams.encodeConfig->rcParams.constQP.qpInterP    = m_stEncoderInput.qpP;
        m_stInitEncParams.encodeConfig->rcParams.constQP.qpInterB    = m_stEncoderInput.qpB;
		m_stInitEncParams.encodeConfig->rcParams.enableMinQP       = m_stEncoderInput.min_qp_ena;
        m_stInitEncParams.encodeConfig->rcParams.minQP.qpIntra     = m_stEncoderInput.min_qpI;
        m_stInitEncParams.encodeConfig->rcParams.minQP.qpInterP    = m_stEncoderInput.min_qpP;
        m_stInitEncParams.encodeConfig->rcParams.minQP.qpInterB    = m_stEncoderInput.min_qpB;
		m_stInitEncParams.encodeConfig->rcParams.enableMaxQP       = m_stEncoderInput.max_qp_ena;
        m_stInitEncParams.encodeConfig->rcParams.maxQP.qpIntra     = m_stEncoderInput.max_qpI;
        m_stInitEncParams.encodeConfig->rcParams.maxQP.qpInterP    = m_stEncoderInput.max_qpP;
        m_stInitEncParams.encodeConfig->rcParams.maxQP.qpInterB    = m_stEncoderInput.max_qpB;
		m_stInitEncParams.encodeConfig->rcParams.enableInitialRCQP    = m_stEncoderInput.initial_qp_ena;
        m_stInitEncParams.encodeConfig->rcParams.initialRCQP.qpIntra  = m_stEncoderInput.initial_qpI;
        m_stInitEncParams.encodeConfig->rcParams.initialRCQP.qpInterP = m_stEncoderInput.initial_qpP;
        m_stInitEncParams.encodeConfig->rcParams.initialRCQP.qpInterB = m_stEncoderInput.initial_qpB;

        m_stInitEncParams.encodeConfig->rcParams.rateControlMode     = (NV_ENC_PARAMS_RC_MODE)m_stEncoderInput.rateControl;
		m_stInitEncParams.encodeConfig->rcParams.vbvBufferSize       =  m_stEncoderInput.vbvBufferSize;
		m_stInitEncParams.encodeConfig->rcParams.vbvInitialDelay     =  m_stEncoderInput.vbvInitialDelay;

	//////////////////////////////////////////////////
	//
	// user_SEI: (3) more NVENC's encoder-settings
	//

		oss << " / rateMode=";
		
		switch( m_stInitEncParams.encodeConfig->rcParams.rateControlMode ) {
			case NV_ENC_PARAMS_RC_CONSTQP:        /**< Constant QP mode */
				oss << "CONSTQP(I:P:B)="
					<< std::dec << m_stEncoderInput.qpI << ":"
					<< std::dec << m_stEncoderInput.qpP << ":"
					<< std::dec << m_stEncoderInput.qpB;
				break;

			case NV_ENC_PARAMS_RC_VBR:            /**< Variable bitrate mode */
				oss << "VBR(avg:max)="
					<< std::dec << m_stEncoderInput.avgBitRate << ":"
					<< std::dec << m_stEncoderInput.peakBitRate;
				break;

			case NV_ENC_PARAMS_RC_CBR:            /**< Constant bitrate mode */
				oss << "CBR(avg)="
					<< std::dec << m_stEncoderInput.avgBitRate;
				break;

			case NV_ENC_PARAMS_RC_VBR_MINQP:      /**< Variable bitrate mode with MinQP */
				// ASSUME min_qp_ena is set!
				oss << "VBR_MINQP(avg:max)="
					<< std::dec << m_stEncoderInput.avgBitRate << ":"
					<< std::dec << m_stEncoderInput.peakBitRate;
				break;

			case NV_ENC_PARAMS_RC_2_PASS_QUALITY: /**< Multi pass CBR encoding optimized for image quality and works only with low latency mode */
				oss << "2_PASS_QUALITY(avg)="
					<< std::dec << m_stEncoderInput.avgBitRate;
				break;

			case NV_ENC_PARAMS_RC_2_PASS_FRAMESIZE_CAP: /**< Multi pass CBR encoding optimized for maintaining frame size and works only with low latency mode */
				oss << "2_PASS_FRAMESIZE_CAP(avg)="
					<< std::dec << m_stEncoderInput.avgBitRate;
				break;

			case NV_ENC_PARAMS_RC_2_PASS_VBR: /**< Multi pass VBR encoding for higher quality */
				oss << "2_PASS_VBR(avg:max)="
					<< std::dec << m_stEncoderInput.avgBitRate << ":"
					<< std::dec << m_stEncoderInput.peakBitRate;
				break;
			default:
				break;
		}

		if ( m_stEncoderInput.initial_qp_ena ) {
			oss << ",IniQP(I:P:B)="
				<< std::dec << m_stEncoderInput.initial_qpI << ":"
				<< std::dec << m_stEncoderInput.initial_qpP << ":"
				<< std::dec << m_stEncoderInput.initial_qpB;
		}

		if (m_stEncoderInput.min_qp_ena && m_stEncoderInput.max_qp_ena) {
			oss << ",Min*MaxQP(I:P:B)=" 
				<< std::dec << m_stEncoderInput.min_qpI << ":"
				<< std::dec << m_stEncoderInput.min_qpP << ":"
				<< std::dec << m_stEncoderInput.min_qpB << "*"
				<< std::dec << m_stEncoderInput.max_qpI << ":"
				<< std::dec << m_stEncoderInput.max_qpP << ":"
				<< std::dec << m_stEncoderInput.max_qpB;
		}
		else if (m_stEncoderInput.min_qp_ena) {
			oss << ",MinQP(I:P:B)="
				<< std::dec << m_stEncoderInput.min_qpI << ":"
				<< std::dec << m_stEncoderInput.min_qpP << ":"
				<< std::dec << m_stEncoderInput.min_qpB;
		}
		else if (m_stEncoderInput.max_qp_ena) {
			oss << ",MaxQP(I:P:B)="
				<< std::dec << m_stEncoderInput.max_qpI << ":"
				<< std::dec << m_stEncoderInput.max_qpP << ":"
				<< std::dec << m_stEncoderInput.max_qpB;
		}

		// report adaptive-quantization for everything, 'cause I don't know its effect
		if (m_stInitEncParams.encodeConfig->rcParams.enableAQ )
			oss << ",AQ"; // adaptive quantization

#define ADD_ENCODECONFIG_RCPARAM_2_OSS2(var,name) \
	oss << " / " << name << "=" << std::dec << (unsigned) m_stInitEncParams.encodeConfig->rcParams. ## var
#define ADD_ENCODECONFIG_RCPARAM_2_OSS( var ) ADD_ENCODECONFIG_RCPARAM_2_OSS2(var,#var) 

		ADD_ENCODECONFIG_RCPARAM_2_OSS2(vbvBufferSize,"vbvBS");
		ADD_ENCODECONFIG_RCPARAM_2_OSS2(vbvInitialDelay,"vbvID");

	//
	// user_SEI: (3) more NVENC's encoder-settings
	//
	//////////////////////////////////////////////////

        m_stInitEncParams.encodeConfig->frameIntervalP       = m_stEncoderInput.numBFrames + 1;
        m_stInitEncParams.encodeConfig->gopLength            = (m_stEncoderInput.gopLength > 0) ?  m_stEncoderInput.gopLength : 30;
        m_stInitEncParams.encodeConfig->monoChromeEncoding   = m_stEncoderInput.monoChromeEncoding;
        m_stInitEncParams.encodeConfig->frameFieldMode       = m_stEncoderInput.FieldEncoding ;
        m_stInitEncParams.encodeConfig->mvPrecision          = m_stEncoderInput.mvPrecision;

#define ADD_ENCODECONFIG_2_OSS2( var, name ) \
	oss << " / " << name << "=" << std::dec << (unsigned) m_stInitEncParams.encodeConfig-> ## var
#define ADD_ENCODECONFIG_2_OSS(var) ADD_ENCODECONFIG_2_OSS2(var,#var)

#define ADD_ENCODECONFIG_2_OSS2_if_nz( var, name ) \
	if ( m_stInitEncParams.encodeConfig-> ## var ) \
		oss << " / " << name << "=" << std::dec << (unsigned) m_stInitEncParams.encodeConfig-> ## var

#define ADD_ENCODECONFIG_2_OSS_if_nz( var ) ADD_ENCODECONFIG_2_OSS2_if_nz(var,#var)

		ADD_ENCODECONFIG_2_OSS(frameIntervalP,"IntervalP");
		if ( m_stEncoderInput.numBFrames )
			oss << " (BFrames=" << std::dec << m_stEncoderInput.numBFrames << ")";
		ADD_ENCODECONFIG_2_OSS2(gopLength,"gop");
		ADD_ENCODECONFIG_2_OSS2_if_nz(monoChromeEncoding,"mono");
		ADD_ENCODECONFIG_2_OSS2(frameFieldMode,"frameMode");
		ADD_ENCODECONFIG_2_OSS2(mvPrecision,"mv");

        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.disableDeblockingFilterIDC = m_stEncoderInput.disableDeblock; // alawys enable deblk filter for h264
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.adaptiveTransformMode      = (m_stEncoderInput.profile >= NV_ENC_H264_PROFILE_HIGH) ? m_stEncoderInput.adaptive_transform_mode : NV_ENC_H264_ADAPTIVE_TRANSFORM_AUTOSELECT;
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.fmoMode                    = m_stEncoderInput.enableFMO;
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.bdirectMode                = m_stEncoderInput.numBFrames > 0 ? m_stEncoderInput.bdirectMode : NV_ENC_H264_BDIRECT_MODE_DISABLE;
//        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.bdirectMode                = m_stEncoderInput.numBFrames > 0 ? NV_ENC_H264_BDIRECT_MODE_TEMPORAL : NV_ENC_H264_BDIRECT_MODE_DISABLE;
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.outputAUD                  = m_stEncoderInput.aud_enable;
//      m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.entropyCodingMode        = (m_stEncoderInput.profile > NV_ENC_H264_PROFILE_BASELINE) ? m_stEncoderInput.vle_entropy_mode : NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC;
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.idrPeriod                = m_stInitEncParams.encodeConfig->gopLength ;
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.level                    = m_stEncoderInput.level;
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.numTemporalLayers        = m_stEncoderInput.numlayers;
/*
		if (m_stEncoderInput.svcTemporal)
        {
            m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.enableTemporalSVC = 1;
            m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.h264Extension.svcTemporalConfig.basePriorityID           = 0;
            m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.h264Extension.svcTemporalConfig.numTemporalLayers = m_stEncoderInput.numlayers;;
        }
*/

		// NVENC 4.0 API
		// -------------
		// From documentation, setting the chromaFromatIDC to 3 will select
		// 4:4:4 chroma-format!
		//
		// Don't need to specify 'separate color planes'
		switch (m_stEncoderInput.chromaFormatIDC) {
			case cudaVideoChromaFormat_444:
				m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.chromaFormatIDC = 3; // YUV444
				break;

			default: // cudaVideoChromaFormat_420:
				m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.chromaFormatIDC = 1; // YUV420
				break;
		}

#define ADD_ENCODECONFIGH264_2_OSS2(var,name) \
	oss << " / " << name << "=" << std::dec << (unsigned) m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config. ## var
#define ADD_ENCODECONFIGH264_2_OSS( var ) ADD_ENCODECONFIGH264_2_OSS2(var,#var)

#define ADD_ENCODECONFIGH264_2_OSS2_if_nz(var,name) \
	if ( m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config. ## var ) \
		oss << " / " << name << "=" << std::dec << (unsigned) m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config. ## var
#define ADD_ENCODECONFIGH264_2_OSS_if_nz(var,name) ADD_ENCODECONFIGH264_2_OSS2_if_nz(var,#var)

		desc_nv_enc_buffer_format_names.value2string(
			m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.chromaFormatIDC, s
		);
		ADD_ENCODECONFIGH264_2_OSS2(chromaFormatIDC,"chroma");
		//oss << "(" << s << ")"; // show ascii-name of the chromaFormatIDC
		ADD_ENCODECONFIGH264_2_OSS2_if_nz(separateColourPlaneFlag,"sepCPF");
		ADD_ENCODECONFIGH264_2_OSS2_if_nz(disableDeblockingFilterIDC,"disDF");
		ADD_ENCODECONFIGH264_2_OSS2(adaptiveTransformMode,"adaTM");
		ADD_ENCODECONFIGH264_2_OSS_if_nz(fmoMode);
		ADD_ENCODECONFIGH264_2_OSS(bdirectMode);
		ADD_ENCODECONFIGH264_2_OSS_if_nz(outputAUD);
		//ADD_ENCODECONFIGH264_2_OSS(entropyCodingMode);
		ADD_ENCODECONFIGH264_2_OSS(idrPeriod);
		ADD_ENCODECONFIGH264_2_OSS(level);
		//ADD_ENCODECONFIGH264_2_OSS(numTemporalLayers);

        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.outputBufferingPeriodSEI = m_stEncoderInput.output_sei_BufferPeriod;
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.outputPictureTimingSEI   = m_stEncoderInput.output_sei_PictureTime;
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.hierarchicalPFrames      = !! m_stEncoderInput.hierarchicalP;
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.hierarchicalBFrames      = !! m_stEncoderInput.hierarchicalB;
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.disableSPSPPS            = !! m_stEncoderInput.outBandSPSPPS;
//        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.outputFramePackingSEI    = m_stEncoderInput.stereo3dMode!= NV_ENC_STEREO_PACKING_MODE_NONE ? 1 : 0;
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.outputFramePackingSEI    = m_stEncoderInput.stereo3dEnable ? 1 : 0;
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.stereoMode               = (NV_ENC_STEREO_PACKING_MODE)m_stEncoderInput.stereo3dMode;
		m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.enableStereoMVC          = m_stEncoderInput.stereo3dEnable;
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.separateColourPlaneFlag  = m_stEncoderInput.separateColourPlaneFlag;// set to 1 to enable 4:4:4 mode
        m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.entropyCodingMode        = (m_stEncoderInput.profile > NV_ENC_H264_PROFILE_BASELINE) ? m_stEncoderInput.vle_entropy_mode : NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC;
        if (m_stEncoderInput.max_ref_frames>0) 
             m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.maxNumRefFrames     = m_stEncoderInput.max_ref_frames;
        if ( pvui != NULL )
            m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.h264VUIParameters = *pvui;

		ADD_ENCODECONFIGH264_2_OSS2_if_nz(outputBufferingPeriodSEI,"outBPSEI");
		ADD_ENCODECONFIGH264_2_OSS2_if_nz(outputPictureTimingSEI,"outPTSEI");
		ADD_ENCODECONFIGH264_2_OSS2_if_nz(hierarchicalPFrames,"hierP");
		ADD_ENCODECONFIGH264_2_OSS2_if_nz(hierarchicalBFrames,"hierB");
		ADD_ENCODECONFIGH264_2_OSS2_if_nz(disableSPSPPS,"disSPSPPS");
		ADD_ENCODECONFIGH264_2_OSS2_if_nz(outputFramePackingSEI,"outFPSEI");
//		ADD_ENCODECONFIGH264_2_OSS2_if_nz(enableStereoMVC,"enableStereo");
//		if ( m_stEncoderInput.stereo3dEnable )
//			ADD_ENCODECONFIGH264_2_OSS2(stereoMode,"stereoMode");
		ADD_ENCODECONFIGH264_2_OSS2(entropyCodingMode,"entCM");
		ADD_ENCODECONFIGH264_2_OSS2(maxNumRefFrames,"maxRef");
		if (m_stEncoderInput.enableLTR) {
			ADD_ENCODECONFIGH264_2_OSS2(ltrNumFrames, "ltrnf");
			ADD_ENCODECONFIGH264_2_OSS2_if_nz(ltrTrustMode, "ltrtm");
		}

		m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.sliceMode = m_stEncoderInput.sliceMode;
		m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.sliceModeData = m_stEncoderInput.sliceModeData;
		ADD_ENCODECONFIGH264_2_OSS2(sliceMode, "SM");
		ADD_ENCODECONFIGH264_2_OSS2(sliceModeData, "SMData");

		m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.enableLTR    = m_stEncoderInput.enableLTR;
		m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.ltrNumFrames = m_stEncoderInput.ltrNumFrames;
		m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.ltrTrustMode = m_stEncoderInput.ltrTrustMode;

		// NVENC API 3
		m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.enableVFR = m_stEncoderInput.enableVFR ? 1 : 0;

		ADD_ENCODECONFIGH264_2_OSS2_if_nz(enableVFR,"enaVFR");


		// NVENC API 4
		m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.qpPrimeYZeroTransformBypassFlag = m_stEncoderInput.qpPrimeYZeroTransformBypassFlag;
		ADD_ENCODECONFIGH264_2_OSS2_if_nz(qpPrimeYZeroTransformBypassFlag,"qpPrimeYZero");
    }

    // Initialize the Encoder
    nvStatus = m_pEncodeAPI->nvEncInitializeEncoder(m_hEncoder, &m_stInitEncParams);

	if (nvStatus == NV_ENC_SUCCESS)
    {
        if (m_stEncoderInput.outBandSPSPPS > 0)
        {
            printf("\n");
            SET_VER(m_spspps, NV_ENC_SEQUENCE_PARAM_PAYLOAD);
            m_spspps.spsppsBuffer = new unsigned char [1024];
            m_spspps.inBufferSize = 1024;
            m_spspps.outSPSPPSPayloadSize = new unsigned int[1];
            nvStatus = m_pEncodeAPI->nvEncGetSequenceParams(m_hEncoder, &m_spspps);
            assert(nvStatus == NV_ENC_SUCCESS);
            if (nvStatus == NV_ENC_SUCCESS)
            {
                (*m_fwrite_callback)(m_spspps.spsppsBuffer, 1, *m_spspps.outSPSPPSPayloadSize, m_fOutput, m_privateData);
                printf(">> outSPSPPS PayloadSize = %d, Payload=", *m_spspps.outSPSPPSPayloadSize);

                for (int i=0; i < sizeof(*m_spspps.outSPSPPSPayloadSize) ; i++) 
                    printf("%x", ((unsigned char *)m_spspps.spsppsBuffer)[i]);

                printf("\n");
            }
        }

        // Allocate IO buffers -
        //    Note, here the surface must be allocated to the source-video's *coded height* and *coded width*,
        // rather than the display height & width.  If we use the display-height & width, then the chroma portion
        // of the decoded Frames will most likely be offset improperly (resulting in in a color-misalignment.)
        // Example:
        //      Source video coded-width x height = 1920 x 1088 (Coded dimensions VC1/MPEG2/MPEG4/H264)
        //      Source video displayed dimensions = 1920 x 1080 
        //      ...AllocateIOBuffers( 1920, 1088, ...);
        //
		//   In interlaced encoding mode, the encoder receives input as a whole frames (i.e. a pair of fields),
		//                                so the full height is still used.

        unsigned int dwPicHeight = m_uMaxHeight;
        int numMBs = ((m_dwFrameWidth + 15)/16) * ((dwPicHeight + 15)/16);
        int NumIOBuffers = m_stEncoderInput.numBFrames + 4 + 1;
		/*
		if ( numMBs < 8160)   // less than 1920x1088
			NumIOBuffers = m_stEncoderInput.numBFrames + 4 + 1;
		else if ( numMBs < 16320 ) // between 1920x1088 and 2560x...
			NumIOBuffers = 16;
		else
			NumIOBuffers = 9;
		*/
        //AllocateIOBuffers(m_dwFrameWidth, dwPicHeight, NumIOBuffers);
        AllocateIOBuffers(m_uMaxWidth, dwPicHeight, NumIOBuffers);
        hr = S_OK;

		// Query and save the reported hardware capabilities for this NVENC-instance.
		QueryEncoderCaps( m_nv_enc_caps );
    }
    else
        hr = E_FAIL;

    // intialize output thread
    if (hr == S_OK && !m_pEncoderThread)
    {
        m_pEncoderThread = new CNvEncoderThread(reinterpret_cast<CNvEncoder*>(this), MAX_OUTPUT_QUEUE);
        if (!m_pEncoderThread)
        {
            hr = E_FAIL;
        }
        else
        {
            m_pEncoderThread->ThreadStart();
        }
    }
    
	///////
	//
	// transfer the encoder-settings to m_sei_user_payload
	//

	m_sei_user_payload_str = oss.str();
	//printf( "m_sei_user_payload(%0u) = '%s'\n", m_sei_user_payload_str.length(), m_sei_user_payload_str.c_str() );

	m_sei_user_payload.payloadType = 5;// Annex D : Type 5 = 'user data unregistered'
	m_sei_user_payload.payloadSize = m_sei_user_payload_str.length();// fill in later
	if ( m_sei_user_payload.payload != NULL )
		delete [] m_sei_user_payload.payload;

	m_sei_user_payload.payload = new uint8_t[ m_sei_user_payload.payloadSize ];
	memcpy( (char *)m_sei_user_payload.payload, m_sei_user_payload_str.c_str(), m_sei_user_payload.payloadSize );

    if (hr == S_OK)
        m_bEncoderInitialized = true;

    return hr;
}

HRESULT
CNvEncoderH264::ReconfigureEncoder(EncodeConfig EncoderReConfig)
{
    // Initialize the Encoder
    memcpy(&m_stEncoderInput ,&EncoderReConfig, sizeof(EncoderReConfig));
    m_stInitEncParams.encodeHeight        =  EncoderReConfig.height;
    m_stInitEncParams.encodeWidth         =  EncoderReConfig.width;
    m_stInitEncParams.darWidth            =  EncoderReConfig.width;
    m_stInitEncParams.darHeight           =  EncoderReConfig.height;

    m_stInitEncParams.frameRateNum        =  EncoderReConfig.frameRateNum;
    m_stInitEncParams.frameRateDen        =  EncoderReConfig.frameRateDen;
    //m_stInitEncParams.presetGUID          = m_stPresetGUID;
    m_stInitEncParams.encodeConfig->rcParams.maxBitRate         = EncoderReConfig.peakBitRate;
    m_stInitEncParams.encodeConfig->rcParams.averageBitRate     = EncoderReConfig.avgBitRate;
    m_stInitEncParams.encodeConfig->frameFieldMode              = EncoderReConfig.FieldEncoding ? NV_ENC_PARAMS_FRAME_FIELD_MODE_FIELD : NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME ;
    m_stInitEncParams.encodeConfig->rcParams.vbvBufferSize      = EncoderReConfig.vbvBufferSize;
    m_stInitEncParams.encodeConfig->rcParams.vbvInitialDelay    = EncoderReConfig.vbvInitialDelay;
    m_stInitEncParams.encodeConfig->encodeCodecConfig.h264Config.disableSPSPPS = 0;
    memcpy( &m_stReInitEncParams.reInitEncodeParams, &m_stInitEncParams, sizeof(m_stInitEncParams));
    SET_VER(m_stReInitEncParams, NV_ENC_RECONFIGURE_PARAMS);
    m_stReInitEncParams.resetEncoder    = true;
    NVENCSTATUS nvStatus = m_pEncodeAPI->nvEncReconfigureEncoder(m_hEncoder, &m_stReInitEncParams);
    return nvStatus;
}

//
// neither nvenc_export nor nvEncode2 uses the vanilla EncodeFrame() function
//
//HRESULT CNvEncoderH264::EncodeFrame(EncodeFrameConfig *pEncodeFrame, bool bFlush)

//
//  EncodeFramePPro() - called by the Premiere Pro Plugin 
//
//     we accept frames in either of the following formats:
//         (1) YV12(4:2:0) planar-format            - encoder chromaFormat must be 4:2:0
//         (2) YUY2(4:2:2) packed (16bpp per pixel) - encoder chromaFormat must be 4:2:0
//         (3) YUV444 packed (32bpp per pixel)      - encoder chromaFormat must be 4:4:4
HRESULT CNvEncoderH264::EncodeFramePPro(
	EncodeFrameConfig *pEncodeFrame,
	const bool bFlush
)
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    HRESULT hr = S_OK;
    NV_ENC_MAP_INPUT_RESOURCE mapRes = {0};

	if (bFlush)
	{
		FlushEncoder();
		return S_OK;
	}

	if (!pEncodeFrame)
	{
		return E_FAIL;
	}

	// Flags describing the chromaformat of the video-data received from PremierPro
	// (if necessary, the video-data will be converted into a NVENC-compatible bufferFormat)
	const bool input_yuv422 = pEncodeFrame->ppro_pixelformat_is_uyvy422 ||
		pEncodeFrame->ppro_pixelformat_is_yuyv422;
	const bool input_yuv420 = pEncodeFrame->ppro_pixelformat_is_yuv420;
	const bool input_yuv444 = pEncodeFrame->ppro_pixelformat_is_yuv444;

    EncodeInputSurfaceInfo  *pInput;
    EncodeOutputBuffer      *pOutputBitstream;

    if (!m_stInputSurfQueue.Remove(pInput, INFINITE))
    {
        assert(0);
    }

    if (!m_stOutputSurfQueue.Remove(pOutputBitstream, INFINITE))
    {
        assert(0);
    }

    // encode width and height
    unsigned int dwWidth =  m_uMaxWidth; //m_stEncoderInput.width;
    unsigned int dwHeight = m_uMaxHeight;//m_stEncoderInput.height;
    // Align 32 as driver does the same
    unsigned int dwSurfWidth  = (dwWidth + 0x1f) & ~0x1f;
    unsigned int dwSurfHeight = (dwHeight + 0x1f) & ~0x1f;
    unsigned char *pLuma    = pEncodeFrame->yuv[0];
    unsigned char *pChromaU = pEncodeFrame->yuv[1];
    unsigned char *pChromaV = pEncodeFrame->yuv[2];
    unsigned char *pInputSurface = NULL;
    unsigned char *pInputSurfaceCh = NULL;
    unsigned int lockedPitch = dwSurfWidth;
    
    pInputSurface = LockInputBuffer(pInput->hInputSurface, &lockedPitch);
    pInputSurfaceCh = pInputSurface + (dwSurfHeight*lockedPitch);

	// Test the stride-values and memory-pointers for 16-byte alignment.
	// If *everything* is 16-byte aligned, then we will use the faster SSE2-function
	//    ...otherwise must use the slower non-sse2 function
	bool is_xmm_aligned = true;

	// check both the src-stride and src-surfaces for *any* unaligned param
	unsigned src_plane_count = input_yuv420 ?
		3 : // YUV 4:2:0 planar (3 planes)
		1;  // YUV444 or YUV422 (packed pixel, 1 plane only)

	for( unsigned i = 0; i < src_plane_count; ++i ) {
		if ( reinterpret_cast<uint64_t>(pEncodeFrame->yuv[i]) & 0xF ) 
			is_xmm_aligned = false;
		else if ( pEncodeFrame->stride[i] & 0xF )
			is_xmm_aligned = false;
	}

	// Now check both the dest-stride and dest-surfaces for *any* unaligned param
	if ( reinterpret_cast<uint64_t>(pInputSurface) & 0xF )
		is_xmm_aligned = false;
	else if ( reinterpret_cast<uint64_t>(pInputSurfaceCh) & 0xF )
		is_xmm_aligned = false;
	else if ( lockedPitch & 0xF )
		is_xmm_aligned = false;

	// IsNV12Tiled16x16Format (bunch of sqaures)
	//convertYUVpitchtoNV12tiled16x16(pLuma, pChromaU, pChromaV,pInputSurface, pInputSurfaceCh, dwWidth, dwHeight, dwWidth, lockedPitch);
    //(IsNV12PLFormat(pInput->bufferFmt))  (Luma plane intact, chroma planes broken)
//	if ( IsYUV444Format(pInput->bufferFmt) ) {
	if ( m_stEncoderInput.chromaFormatIDC == cudaVideoChromaFormat_444 ) {
		// input = YUV 4:4:4
		//
		// Convert the source-video (YUVA_4444 32bpp packed-pixel) into 
		// planar format 4:4. (NVENC only accepts YUV444 3-plane format)
		//

		if ( is_xmm_aligned ) {
			m_Repackyuv._convert_YUV444toY444_ssse3( // Streaming SSE3 version of converter
				dwWidth, dwHeight, (pEncodeFrame->stride[0] >> 2) >> 2,
				reinterpret_cast<__m128i *>(pEncodeFrame->yuv[0]),
				lockedPitch >> 4,
				reinterpret_cast<__m128i *>(pInputSurface),    // output Y
				reinterpret_cast<__m128i *>(pInputSurfaceCh),  // output U
				reinterpret_cast<__m128i *>(pInputSurfaceCh + (dwSurfHeight*lockedPitch)) // output V
			);
		}
		else {
			// not XMM aligned- call the non-SSE function
			m_Repackyuv._convert_YUV444toY444(  // non-SSE version (slow)
				dwWidth, dwHeight, pEncodeFrame->stride[0] >> 2,
				reinterpret_cast<uint32_t *>(pEncodeFrame->yuv[0]),
				lockedPitch,
				pInputSurface,    // output Y
				pInputSurfaceCh,  // output U
				pInputSurfaceCh + (dwSurfHeight*lockedPitch) // output V
			);
		} // if ( is_xmm_aligned )
	} // if ( m_stEncoderInput.chromaFormatIDC == cudaVideoChromaFormat_444 ) )

	if (m_stEncoderInput.chromaFormatIDC == cudaVideoChromaFormat_420) {
		
		if ( input_yuv420) {
			// Note, PPro handed us YUV4:2:0 (YV12) data, and NVENC only accepts 
			// 4:2:0 pixel-data in the NV12_planar format (2 planes.)
			//
			// ... So we must convert the source-frame from YUV420 -> NV12

			/////////////////////////////
			//
			// Select which YUV420 -> NV12 conversion algorithm to use
			//

			if ( is_xmm_aligned ) {
				__m128i      *xmm_src_yuv[3];
				unsigned int xmm_src_stride[3];// stride in units of 128bits (i.e. 'stride==1' means 16 bytes)
				__m128i      *xmm_dst_luma;
				__m128i      *xmm_dst_chroma;
				unsigned int xmm_dst_stride;   // destination stride (units of 128 bits)

				for(unsigned i = 0; i < 3; ++i ) {
					xmm_src_yuv[i]    = reinterpret_cast<__m128i *>(pEncodeFrame->yuv[i]);
					xmm_src_stride[i] = pEncodeFrame->stride[i] >> 4;
				}
				xmm_dst_luma   = reinterpret_cast<__m128i *>(pInputSurface);
				xmm_dst_chroma = reinterpret_cast<__m128i *>(pInputSurfaceCh);
				xmm_dst_stride = lockedPitch >> 4;

				m_Repackyuv._convert_YUV420toNV12_sse2( // faster sse2 version (requires 16-byte data-alignment)
					dwWidth, dwHeight,
					xmm_src_yuv,
					xmm_src_stride,
					xmm_dst_luma, xmm_dst_chroma, 
					xmm_dst_stride
				);
			}
			else {
				// not XMM-aligned: call the plain (non-SSE2) function
				m_Repackyuv._convert_YUV420toNV12(  // plain (non-SSE2) version, slower
					dwWidth, dwHeight,
					pEncodeFrame->yuv,
					pEncodeFrame->stride,
					pInputSurface, reinterpret_cast<uint16_t*>(pInputSurfaceCh), 
					lockedPitch
				);
			}
		} ///////////////// if ( input_yuv420)
		else if (input_yuv422) {
			// Note, PPro handed us YUV4:2:2 (16bpp packed) data, and NVENC only accepts 
			// 4:2:0 pixel-data in the NV12_planar format (2 planes.)
			//
			// ... So we must convert the source-frame from YUV422 -> NV12

			/////////////////////////////
			//
			// Select which YUV420 -> NV12 conversion algorithm to use
			//
			if ( is_xmm_aligned )
				m_Repackyuv._convert_YUV422toNV12_ssse3(  // non-SSE version (slow)
					pEncodeFrame->ppro_pixelformat_is_uyvy422, // chroma-order: true=UYVY, false=YUYV
					dwWidth, dwHeight, pEncodeFrame->stride[0] >> 1,
					reinterpret_cast<__m128i *>(pEncodeFrame->yuv[0]),
					lockedPitch,
					reinterpret_cast<__m128i *>(pInputSurface),    // output Y
					reinterpret_cast<__m128i *>(pInputSurfaceCh)  // output UV
				);
			else
				m_Repackyuv._convert_YUV422toNV12(  // non-SSE version (slow)
					pEncodeFrame->ppro_pixelformat_is_uyvy422, // chroma-order: true=UYVY, false=YUYV
					dwWidth, dwHeight, pEncodeFrame->stride[0] >> 1,
					reinterpret_cast<uint32_t *>(pEncodeFrame->yuv[0]),
					lockedPitch,
					pInputSurface,    // output Y
					pInputSurfaceCh  // output UV
				);

		} ///////////////// if (input_yuv422)
		else {
			// TODO ERROR: if it wasn't YUV420, and not YUV422,
			//  then PremierePro gave us something we can't handle.
			// ABORT
		}

	} // if ( m_stEncoderInput.chromaFormatIDC == cudaVideoChromaFormat_420 )

    UnlockInputBuffer(pInput->hInputSurface);

    memset(&m_stEncodePicParams, 0, sizeof(m_stEncodePicParams));
    SET_VER(m_stEncodePicParams, NV_ENC_PIC_PARAMS);
    m_stEncodePicParams.inputBuffer = pInput->hInputSurface;
    m_stEncodePicParams.bufferFmt = pInput->bufferFmt;
    m_stEncodePicParams.inputWidth = pInput->dwWidth;
    m_stEncodePicParams.inputHeight = pInput->dwHeight;
    m_stEncodePicParams.outputBitstream = pOutputBitstream->hBitstreamBuffer;
    m_stEncodePicParams.completionEvent = m_bAsyncModeEncoding == true ? pOutputBitstream->hOutputEvent : NULL;
    m_stEncodePicParams.pictureStruct = pEncodeFrame->fieldPicflag ?
		(pEncodeFrame->topField ? NV_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM : NV_ENC_PIC_STRUCT_FIELD_BOTTOM_TOP) :
		NV_ENC_PIC_STRUCT_FRAME;
//    m_stEncodePicParams.codecPicParams.h264PicParams.h264ExtPicParams.mvcPicParams.viewID = pEncodeFrame->viewId;    
    m_stEncodePicParams.encodePicFlags = 0;
    m_stEncodePicParams.inputTimeStamp = 0;
    m_stEncodePicParams.inputDuration = 0;

	// embed encoder-settings (text-string) into the encoded videostream
	if (!m_stInitEncParams.enablePTD)
	{
        m_stEncodePicParams.codecPicParams.h264PicParams.refPicFlag = 1;
		//m_stEncodePicParams.codecPicParams.h264PicParams.frameNumSyntax = m_dwFrameNumInGOP;
		m_stEncodePicParams.codecPicParams.h264PicParams.displayPOCSyntax = 2*m_dwFrameNumInGOP;
	}

	// embed encoder-settings (text-string) into the encoded videostream
	if (m_sei_user_payload_str.length()) { // m_sei_user_payload.payloadSize ) {
		m_stEncodePicParams.codecPicParams.h264PicParams.seiPayloadArrayCnt = 1;
		m_stEncodePicParams.codecPicParams.h264PicParams.seiPayloadArray = &m_sei_user_payload;

		// Delete the payload.  This way, our user-sei is only embedded into the *first* frame
		// of the output-bitstream, and nothing subsequent.  While we really should mebed
		// it in every frame, that would bloat the output filesize, and MediaInfo only
		// needs the user-sei in the first-frame to display the info. 
		m_sei_user_payload_str.clear();
	}

	if (!m_stInitEncParams.enablePTD)
		m_stEncodePicParams.pictureType = ((m_dwFrameNumInGOP % m_stEncoderInput.gopLength) == 0) ? NV_ENC_PIC_TYPE_IDR : NV_ENC_PIC_TYPE_P;

	// Don't allow Dynamic Resolution Changing (not supported in PPro)
	assert (!pEncodeFrame->dynResChangeFlag);

    // Handling Dynamic Bitrate Change (don't need this for PPro)
	assert( pEncodeFrame->dynBitrateChangeFlag != DYN_DOWNSCALE);

    assert(pEncodeFrame->dynBitrateChangeFlag != DYN_UPSCALE);

    if ((m_bAsyncModeEncoding == false) && 
        (m_stInitEncParams.enablePTD == 1))
    {
        EncoderThreadData stThreadData;
        stThreadData.pOutputBfr = pOutputBitstream;
        stThreadData.pInputBfr = pInput;
        stThreadData.pOutputBfr->bDynResChangeFlag = pEncodeFrame->dynResChangeFlag == 1 ? 1 : 0;
        pOutputBitstream->bWaitOnEvent = false;
        m_pEncodeFrameQueue.Add(stThreadData);
    }

    nvStatus = m_pEncodeAPI->nvEncEncodePicture(m_hEncoder, &m_stEncodePicParams);
    
    m_dwFrameNumInGOP++;
    if ((m_bAsyncModeEncoding == false) && 
        (m_stInitEncParams.enablePTD == 1))
    {        
        if (nvStatus == NV_ENC_SUCCESS)
        {
            EncoderThreadData stThreadData;
            while (m_pEncodeFrameQueue.Remove(stThreadData, 0))
            {
                m_pEncoderThread->QueueSample(stThreadData);
            }
        }
        else
        {
            assert(nvStatus == NV_ENC_ERR_NEED_MORE_INPUT);
        }
    }
    else
    {
        if (nvStatus == NV_ENC_SUCCESS)
        {
            EncoderThreadData stThreadData;
            stThreadData.pOutputBfr = pOutputBitstream;
            stThreadData.pInputBfr = pInput;
            pOutputBitstream->bWaitOnEvent = true;
            stThreadData.pOutputBfr->bDynResChangeFlag = pEncodeFrame->dynResChangeFlag == 1 ? 1 : 0;
            // Queue o/p Sample
            if (!m_pEncoderThread->QueueSample(stThreadData))
            {
                assert(0);
            }
        }
        else
        {
            assert(0);
        }
    }
    return hr;
}

//
// nvEncode2 (the command-line transcoder) calls EncodeCudaMemFrame() function
//
HRESULT CNvEncoderH264::EncodeCudaMemFrame(
	EncodeFrameConfig *pEncodeFrame, CUdeviceptr oDecodedFrame[3], const unsigned int oFrame_pitch, bool bFlush )
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    HRESULT hr = S_OK;
    NV_ENC_MAP_INPUT_RESOURCE mapRes = {0};

    if (bFlush)
    {
        FlushEncoder();
        return S_OK;
    }

    if (!pEncodeFrame)
    {
        return E_FAIL;
    }

    EncodeInputSurfaceInfo  *pInput;
    EncodeOutputBuffer      *pOutputBitstream;

    if (!m_stInputSurfQueue.Remove(pInput, INFINITE))
    {
        assert(0);
    }


    if (!m_stOutputSurfQueue.Remove(pOutputBitstream, INFINITE))
    {
        assert(0);
    }

    unsigned int lockedPitch = 0;
    // encode width and height
    unsigned int dwWidth =  m_uMaxWidth; //m_stEncoderInput.width;
    unsigned int dwHeight = m_uMaxHeight;//m_stEncoderInput.height;
    // Align 32 as driver does the same
    unsigned int dwSurfWidth  = (dwWidth + 0x1f) & ~0x1f;
    unsigned int dwSurfHeight = (dwHeight + 0x1f) & ~0x1f;
    //unsigned char *pLuma    = pEncodeFrame->yuv[0];
    //unsigned char *pChromaU = pEncodeFrame->yuv[1];
    //unsigned char *pChromaV = pEncodeFrame->yuv[2];
    unsigned char *pInputSurface = NULL;
    unsigned char *pInputSurfaceCh = NULL;
	const    bool need_2d_memcpy = (oFrame_pitch % pInput->dwCuPitch) ? true : false;
    
    // CUDA or DX9 interop with NVENC
    if (m_stEncoderInput.useMappedResources)
    {
        // Here we copy from Host to Device Memory (CUDA)
        if (m_stEncoderInput.interfaceType == NV_ENC_CUDA)
        {
            CUresult result;
            cuCtxPushCurrent(m_cuContext); // Necessary to bind the 
            CUcontext cuContextCurr;
			// YUV444     : we don't actually support this correctly.  Copy only the luma (Y) plane
			// NV12  (420): the #bytes to 
			if ( need_2d_memcpy ) {
				// The source-framebuffer and destination-framebuffer have different pitches.
				// (This seems to only happen when decoding HEVC-video using the DXVA/hybrid decoder.)

				CUDA_MEMCPY2D cuda_memcpy2d;
				memset( (void *)&cuda_memcpy2d, 0, sizeof(cuda_memcpy2d) );// clear cuda_memcpy2d

				cuda_memcpy2d.dstDevice   = (CUdeviceptr) pInput->pExtAlloc;
				cuda_memcpy2d.dstMemoryType = CU_MEMORYTYPE_DEVICE;
				cuda_memcpy2d.dstPitch    = pInput->dwCuPitch;
				cuda_memcpy2d.dstXInBytes = 0; // pInput->dwWidth;
				cuda_memcpy2d.dstY = 0;
				if ( m_stEncoderInput.chromaFormatIDC == cudaVideoChromaFormat_444 )
					cuda_memcpy2d.Height = pInput->dwHeight;           // luma(Y) plane only
				else
					cuda_memcpy2d.Height = (pInput->dwHeight * 3) >> 1;// Y + UV plane
				//cuda_memcpy2d.srcArray 
				cuda_memcpy2d.srcDevice = oDecodedFrame[0];
				cuda_memcpy2d.srcHost = NULL;
				cuda_memcpy2d.srcMemoryType = CU_MEMORYTYPE_DEVICE;
				cuda_memcpy2d.srcPitch = oFrame_pitch;
				cuda_memcpy2d.srcXInBytes = 0; // pInput->dwWidth;
				cuda_memcpy2d.srcY = 0;
				cuda_memcpy2d.WidthInBytes = oFrame_pitch;

				printf("\nCNvEncoderH264::EncodeCudaMemFrame(): cuMemcpy2D(src_pitch=%0u -> dst_pitch=%0u)\n",
					oFrame_pitch, cuda_memcpy2d.dstPitch
				);
				result = cuMemcpy2D(&cuda_memcpy2d);
//				result = cuMemcpy2DUnaligned(&cuda_memcpy2d);
			}
			else {
				// source framebuffer and dest framebuffer have matching pitch,
				// we can use a simpler 1D-memcpy
				if (m_stEncoderInput.chromaFormatIDC == cudaVideoChromaFormat_444)
					result = cuMemcpyDtoD((CUdeviceptr)pInput->pExtAlloc, oDecodedFrame[0], pInput->dwCuPitch*pInput->dwHeight * 1);
				else
					result = cuMemcpyDtoD((CUdeviceptr)pInput->pExtAlloc, oDecodedFrame[0], (pInput->dwCuPitch*pInput->dwHeight*3) >> 1);
			}
            checkCudaErrors(result);
            cuCtxPopCurrent(&cuContextCurr);
        }
//#if defined(NV_WINDOWS)
//        // TODO: Grab a pointer GPU Device Memory (DX9) and then copy the result
//        if (m_stEncoderInput.interfaceType == NV_ENC_DX9)
//        {
//            IDirect3DSurface9 *pSurf = (IDirect3DSurface9 *)pInput->pExtAlloc;
//            pSurf->UnlockRect();
//        }
//#endif
        SET_VER(mapRes, NV_ENC_MAP_INPUT_RESOURCE);
        mapRes.registeredResource  = pInput->hRegisteredHandle;
        nvStatus = m_pEncodeAPI->nvEncMapInputResource(m_hEncoder, &mapRes);
        pInput->hInputSurface = mapRes.mappedResource;
    }
    else // here we just pass the frame in system memory to NVENC
    {
        printf("CNvEncoderH264::EncodeCudaMemFrame ERROR !useMappedResources\n");
        UnlockInputBuffer(pInput->hInputSurface);
    }

    memset(&m_stEncodePicParams, 0, sizeof(m_stEncodePicParams));
    SET_VER(m_stEncodePicParams, NV_ENC_PIC_PARAMS);
    m_stEncodePicParams.inputBuffer = pInput->hInputSurface;
    m_stEncodePicParams.bufferFmt = pInput->bufferFmt;
    m_stEncodePicParams.inputWidth = pInput->dwWidth;
    m_stEncodePicParams.inputHeight = pInput->dwHeight;
    m_stEncodePicParams.outputBitstream = pOutputBitstream->hBitstreamBuffer;
    m_stEncodePicParams.completionEvent = m_bAsyncModeEncoding == true ? pOutputBitstream->hOutputEvent : NULL;
    if ( m_stEncoderInput.FieldEncoding == NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME ) {
        // progressive-video encoding mode
        m_stEncodePicParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
	}
	else {
        // interlaced-video encoding mode
        // In interlaced-mode, NVENC requires interlaced-input, even if the frames are progressive
        // pEncodeFrame->fieldPicflag ?
        m_stEncodePicParams.pictureStruct = pEncodeFrame->topField ? 
            NV_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM : NV_ENC_PIC_STRUCT_FIELD_BOTTOM_TOP;
    }

//    m_stEncodePicParams.codecPicParams.h264PicParams.h264ExtPicParams.mvcPicParams.viewID = pEncodeFrame->viewId;    
    m_stEncodePicParams.encodePicFlags = 0;
    m_stEncodePicParams.inputTimeStamp = 0;
    m_stEncodePicParams.inputDuration = 0;

	// For H264-only: embed encoder-settings (text-string) into the encoded videostream
	if (m_sei_user_payload_str.length()) { // m_sei_user_payload.payloadSize ) {
		m_stEncodePicParams.codecPicParams.h264PicParams.seiPayloadArrayCnt = 1;
		m_stEncodePicParams.codecPicParams.h264PicParams.seiPayloadArray = &m_sei_user_payload;

		// Delete the payload.  This way, our user-sei is only embedded into the *first* frame
		// of the output-bitstream, and nothing subsequent.  While we really should mebed
		// it in every frame, that would bloat the output filesize, and MediaInfo only
		// needs the user-sei in the first-frame to display the info. 
		m_sei_user_payload_str.clear();
	} // sei

	if (!m_stInitEncParams.enablePTD)
	{
		m_stEncodePicParams.codecPicParams.h264PicParams.refPicFlag = 1;
		//m_stEncodePicParams.codecPicParams.h264PicParams.frameNumSyntax = m_dwFrameNumInGOP;
		m_stEncodePicParams.codecPicParams.h264PicParams.displayPOCSyntax = 2 * m_dwFrameNumInGOP;
	}

	if (!m_stInitEncParams.enablePTD)
		m_stEncodePicParams.pictureType = ((m_dwFrameNumInGOP % m_stEncoderInput.gopLength) == 0) ? NV_ENC_PIC_TYPE_IDR : NV_ENC_PIC_TYPE_P;

    // Handling Dynamic Resolution Changing    
    if (pEncodeFrame->dynResChangeFlag)
    {
		fprintf(stderr, "ERROR, dynResChangeFlag != 0: is not supported");
	}

    // Handling Dynamic Bitrate Change
    {
        if (pEncodeFrame->dynBitrateChangeFlag == DYN_DOWNSCALE)
        {
			fprintf(stderr, "ERROR, dynBitrateChangeFlag == DYN_UPSCALE: is not supported");
		}

        if (pEncodeFrame->dynBitrateChangeFlag == DYN_UPSCALE)
        {
			fprintf(stderr, "ERROR, dynBitrateChangeFlag == DYN_UPSCALE: is not supported");
		}
    }

    if ((m_bAsyncModeEncoding == false) && 
        (m_stInitEncParams.enablePTD == 1))
    {
        EncoderThreadData stThreadData;
        stThreadData.pOutputBfr = pOutputBitstream;
        stThreadData.pInputBfr = pInput;
        stThreadData.pOutputBfr->bDynResChangeFlag = pEncodeFrame->dynResChangeFlag == 1 ? 1 : 0;
        pOutputBitstream->bWaitOnEvent = false;
        m_pEncodeFrameQueue.Add(stThreadData);
    }

	nvStatus = m_pEncodeAPI->nvEncEncodePicture(m_hEncoder, &m_stEncodePicParams);
    
    m_dwFrameNumInGOP++;
    if ((m_bAsyncModeEncoding == false) && 
        (m_stInitEncParams.enablePTD == 1))
    {        
        if (nvStatus == NV_ENC_SUCCESS)
        {
            EncoderThreadData stThreadData;
            while (m_pEncodeFrameQueue.Remove(stThreadData, 0))
            {
                m_pEncoderThread->QueueSample(stThreadData);
            }
        }
        else
        {
			if (nvStatus != NV_ENC_ERR_NEED_MORE_INPUT) {
				checkNVENCErrors(nvStatus);
			}
            assert(nvStatus == NV_ENC_ERR_NEED_MORE_INPUT);
        }
    }
    else
    {
        if (nvStatus == NV_ENC_SUCCESS)
        {
            EncoderThreadData stThreadData;
            stThreadData.pOutputBfr = pOutputBitstream;
            stThreadData.pInputBfr = pInput;
            pOutputBitstream->bWaitOnEvent = true;
            stThreadData.pOutputBfr->bDynResChangeFlag = pEncodeFrame->dynResChangeFlag == 1 ? 1 : 0;
            // Queue o/p Sample
            if (!m_pEncoderThread->QueueSample(stThreadData))
            {
                assert(0);
            }
        }
        else
        {
			checkNVENCErrors(nvStatus);
            assert(0);
        }
    }
    return hr;
}

HRESULT CNvEncoderH264::DestroyEncoder()
{
    HRESULT hr = S_OK;
    // common
    hr = ReleaseEncoderResources();
    return hr;
}
