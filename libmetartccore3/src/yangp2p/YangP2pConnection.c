﻿//
// Copyright (c) 2019-2022 yanggaofeng
//
#include <yangp2p/YangP2pConnection.h>
#include <yangp2p/YangP2pSdp.h>
#include <yangrtc/YangRtcSession.h>
#include <yangrtc/YangRtcStun.h>
#include <yangrtc/YangUdpHandle.h>



#include <yangsdp/YangRtcSdp.h>
#include <yangutil/yangtype.h>
#include <yangutil/sys/YangLog.h>
#include <yangutil/sys/YangCString.h>
#include <yangsrs/YangSrsSdp.h>

int32_t yang_p2p_getRemoteSdp(YangRtcSession* session,char* p2p);


int32_t yang_p2p_startRtc(YangRtcSession* session,char* sdp){
	yang_trace("\n yang_p2p_startRtc...port==%d",session->context.streamConf->localPort);

	session->isServer=1;
	yang_p2p_getRemoteSdp(session,sdp);
	yang_stun_createRequestStunPacket(session,session->remoteIcePwd);

	yang_create_rtcudp(session->context.udp,session->context.streamConf->remoteIp,0, session->context.streamConf->localPort);
	yang_rtcsession_startudp(session);
	yang_start_rtcudp(session->context.udp);
	return Yang_Ok;
}
int32_t yang_p2p_connectRtcServer(YangRtcSession* session){
	int err=Yang_Ok;
	P2pSdpResponseType sdp;
	memset(&sdp,0,sizeof(P2pSdpResponseType));
    if ((err=yang_p2p_doHandleSignal(session,&sdp,session->context.streamConf->localPort,session->context.streamConf->streamOptType))  == Yang_Ok) {
		yang_p2p_startRtc(session,sdp.sdp);
	}
	yang_destroy_p2presponse(&sdp);
    return err;
}

YangRtcTrack* yang_p2p_find_track(YangRtcSession* session,uint32_t ssrc){
	if(session==NULL) return NULL;
	if(yang_has_ssrc(&session->context.audioTrack,ssrc)) return &session->context.audioTrack;
	for(int32_t i=0;i<session->context.videoTracks.vsize;i++){
		if(session->context.videoTracks.payload[i].ssrc==ssrc) return &session->context.videoTracks.payload[i];
	}
	return NULL;
}
int32_t yang_p2p_getRemoteSdp(YangRtcSession* session,char* sdpstr) {
	int32_t err = Yang_Ok;

	YangSdp sdp;
	memset(&sdp,0,sizeof(YangSdp));
	yang_create_rtcsdp(&sdp);
	if(session->remote_audio==NULL) session->remote_audio=(YangAudioParam*)calloc(sizeof(YangAudioParam),1);
	if(session->remote_video==NULL) session->remote_video=(YangVideoParam*)calloc(sizeof(YangVideoParam),1);

	yang_rtcsdp_parse(&sdp,sdpstr);

	int32_t k=0;
	for(int32_t i=0;i<sdp.media_descs.vsize;i++){
		YangMediaDesc* desc=&sdp.media_descs.payload[i];
		if(desc->sendonly){
			session->context.streamConf->streamOptType=Yang_Stream_Play;
		}else if(desc->recvonly){
			session->context.streamConf->streamOptType=Yang_Stream_Publish;
		}else if(desc->sendrecv){
			session->context.streamConf->streamOptType=Yang_Stream_Both;
		}
		if(strlen(session->remoteIcePwd)==0){
			memset(session->remoteIcePwd,0,sizeof(session->remoteIcePwd));
			strcpy(session->remoteIcePwd,desc->session_info.ice_pwd);
		}
		if(strlen(session->remote_ufrag)==0){
			memset(session->remote_ufrag,0,sizeof(session->remote_ufrag));
			strcpy(session->remote_ufrag,desc->session_info.ice_ufrag);
		}
		if(yang_strcmp(desc->type,"audio")==0){
			if(desc->ssrc_infos.vsize>0) session->context.audioTrack.ssrc=desc->ssrc_infos.payload[0].ssrc;

			for(k=0;k<desc->payload_types.vsize;k++){

				if(yang_strcmp(desc->payload_types.payload[k].encoding_name,"opus")==0){
					session->remote_audio->encode=Yang_AED_OPUS;
                    session->remote_audio->sample=desc->payload_types.payload[k].clock_rate;
					session->remote_audio->channel=atoi(desc->payload_types.payload[k].encoding_param);
                    session->remote_audio->audioClock=desc->payload_types.payload[k].clock_rate;
				}
			}

		}else if(yang_strcmp(desc->type,"video")==0){

			if(desc->ssrc_infos.vsize>0) {
				for(k=0;k<desc->ssrc_infos.vsize;k++){
					YangRtcTrack videoTrack;
					memset(&videoTrack,0,sizeof(YangRtcTrack));
					videoTrack.ssrc=desc->ssrc_infos.payload[k].ssrc;
					//msid=desc->ssrc_infos.payload[k].msid
					//id=desc->ssrc_infos.payload[k].msid_tracker
					yang_insert_YangRtcTrackVector(&session->context.videoTracks,&videoTrack);

				}
			}



			for(k=0;k<desc->payload_types.vsize;k++){

				if(yang_strcmp(desc->payload_types.payload[k].encoding_name,"H264")==0){
					session->remote_video->encode=Yang_VED_264;
					session->context.codec=Yang_VED_264;
				}else if(yang_strcmp(desc->payload_types.payload[k].encoding_name,"H265")==0){
					session->context.codec=Yang_VED_265;
					session->remote_video->encode=Yang_VED_265;
				}

				session->remote_video->videoClock=90000;
			}
			for (k = 0; k < desc->extmaps.vsize; k++) {
				if (strstr(desc->extmaps.payload[k].extmap,
						"http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01")) {
					session->context.twcc_id = desc->extmaps.payload[k].mapid;
				}
			}
		}
		// set track fec_ssrc and rtx_ssrc
		if (desc->ssrc_groups.vsize > 0) {
			for (k = 0; k < desc->ssrc_groups.vsize; k++) {
				YangSSRCGroup *ssrc_group = &desc->ssrc_groups.payload[k];
				YangRtcTrack *track_desc = yang_p2p_find_track(session,	ssrc_group->groups.payload[0]);
				if (track_desc) {
					if (strcmp(ssrc_group->semantic, "FID") == 0) {
						track_desc->rtx_ssrc = ssrc_group->groups.payload[1];
					} else if (strcmp(ssrc_group->semantic, "FEC") == 0) {
						track_desc->fec_ssrc = ssrc_group->groups.payload[1];
					}
				}
			}
		}

	}

	if(session->context.videoTracks.vsize){
		yang_trace("\nremote audiossrc==%d,remote videossrc==%d,audio sample==%d,audio channel==%d",session->context.audioTrack.ssrc,
			session->context.videoTracks.payload[0].ssrc,session->remote_audio->sample,session->remote_audio->channel);
	}

	if(session->context.avcontext&&session->context.avcontext->setPlayMediaConfig){
		session->context.avcontext->setPlayMediaConfig(session->remote_audio,session->remote_video,session->context.user);
	}

	yang_destroy_rtcsdp(&sdp);
	return err;
}



