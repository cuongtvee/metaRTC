﻿//
// Copyright (c) 2019-2022 yanggaofeng
//
#include <yangutil/yang_unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <yangutil/sys/YangSysMessageHandle.h>
#include <yangutil/yangavinfotype.h>
#include <yangutil/sys/YangLog.h>
#include <yangutil/sys/YangThread.h>
void yang_post_message(int32_t st, int32_t uid, YangSysMessageI *mhandle,void* user) {
	if (YangSysMessageHandle::m_instance)
		YangSysMessageHandle::m_instance->putMessage(mhandle, st, uid, 0,user);
}
void yang_post_state_message(int32_t st, int32_t uid, int32_t handleState,YangSysMessageI *mhandle) {
	if (YangSysMessageHandle::m_instance)
		YangSysMessageHandle::m_instance->putMessage(mhandle, st, uid,
				handleState);
}
void yang_post_userMessage(int32_t st, int32_t uid,YangSysMessageI *mhandle,void* user){
    if (YangSysMessageHandle::m_instance)
        YangSysMessageHandle::m_instance->putMessage(mhandle, st, uid, 0);
}
YangSysMessageHandle::YangSysMessageHandle() {
	m_loop = 0;
	m_isStart = 0;

	yang_thread_mutex_init(&m_lock, NULL);
	yang_thread_cond_init(&m_cond_mess, NULL);
	m_waitState=0;
	m_receive=NULL;
	if (m_instance == NULL)
		m_instance = this;
}

YangSysMessageHandle::~YangSysMessageHandle() {

	if (m_isStart) {
		stop();
		while (m_isStart) {
			yang_usleep(1000);
		}
	}
	m_instance = NULL;
	yang_thread_mutex_destroy(&m_lock);
	yang_thread_cond_destroy(&m_cond_mess);

}
YangSysMessageHandle *YangSysMessageHandle::m_instance = NULL;
void YangSysMessageHandle::run() {
	m_isStart = 1;
    startLoop();
	//startLoop();
	m_isStart = 0;
}
void YangSysMessageHandle::stop() {
	stopLoop();

}
void YangSysMessageHandle::stopLoop() {
	m_loop = 0;
	//yang_thread_mutex_unlock(&m_lock);
	if(m_waitState){
		yang_thread_mutex_lock(&m_lock);
		yang_thread_cond_signal(&m_cond_mess);
		yang_thread_mutex_unlock(&m_lock);

	}
}

void YangSysMessageHandle::putMessage(YangSysMessageI *handle, int32_t pst,
        int32_t puid, int32_t handleState,void* user) {
	if(!m_loop) return;

	YangSysMessage* mes=new YangSysMessage();

	mes->uid = puid;
	mes->messageId = pst;
	mes->handleState = handleState;
	mes->handle = handle;
    mes->user=user;

	m_sysMessages.push_back(mes);
	mes=NULL;

	if(m_waitState){
		yang_thread_mutex_lock(&m_lock);

		yang_thread_cond_signal(&m_cond_mess);
		yang_thread_mutex_unlock(&m_lock);
	}
}

void YangSysMessageHandle::startLoop() {
   m_loop = 1;

   yang_thread_mutex_unlock(&m_lock);
    yang_thread_mutex_lock(&m_lock);

    while (m_loop) {

        m_waitState=1;
        yang_thread_cond_wait(&m_cond_mess, &m_lock);
        m_waitState=0;

        while (m_sysMessages.size()>0) {
            handleMessage(m_sysMessages.front());
            m_sysMessages.front()->handle = NULL;
            delete m_sysMessages.front();
            m_sysMessages.front() = NULL;
            m_sysMessages.erase(m_sysMessages.begin());
        }

    }
    yang_thread_mutex_unlock(&m_lock);
}
