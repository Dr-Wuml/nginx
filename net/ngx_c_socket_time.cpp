//时间有关函数实现部分

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno
//#include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>


#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

//用户三次握手连接成功时，配置了踢人时钟，则本函数被调用
//设置踢出时钟(向multimap表中增加内容)
void CSocket::AddToTimerQueue(lpngx_connection_t pConn)
{
	CMemory *pMemory = CMemory::GetInstance();
	
	time_t futtime = time(NULL);
	futtime += m_iWaitTime;                       //20s之后的时间
	
	CLock lock(&m_timequeueMutex);             //设置互斥量
	LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)pMemory->AllocMemory(m_iLenMsgHeader,false);
	tmpMsgHeader->pConn = pConn;
	tmpMsgHeader->iCurrsequence = pConn->iCurrsequence;
	m_timerQueuemap.insert(std::make_pair(futtime,tmpMsgHeader));              //按键自动排序，小 —> 大
	m_cur_size_ ++;
	m_timer_value_ = GetEarliestTime();                                        //计时队列头部时间值保存到m_timer_value_里
	return;
}

//获取计时队列头部时间值
time_t CSocket::GetEarliestTime()
{
	std::multimap<time_t,LPSTRUC_MSG_HEADER>::iterator pos;
	pos = m_timerQueuemap.begin();
	return pos->first;
}


LPSTRUC_MSG_HEADER CSocket::RemoveFirstTimer()
{
	std::multimap<time_t,LPSTRUC_MSG_HEADER>::iterator pos;
	LPSTRUC_MSG_HEADER ptmp;
	if(m_cur_size_ <= 0)
	{
		return NULL;
	}
	pos = m_timerQueuemap.begin();
	ptmp = pos->second;
	m_timerQueuemap.erase(pos);
	--m_cur_size_;
	return ptmp;
}

LPSTRUC_MSG_HEADER CSocket::GetOverTimeTimer(time_t cur_time)
{
	CMemory *pMemory = CMemory::GetInstance();
	LPSTRUC_MSG_HEADER ptmp;
	if(m_cur_size_ == 0 || m_timerQueuemap.empty())
	{
		return NULL;
	}
	time_t earliesttime = GetEarliestTime();
	if(earliesttime <= cur_time)
	{
		ptmp = RemoveFirstTimer();    //把这个超时的节点从 m_timerQueuemap 删掉，并把这个节点的第二项返回来；
		if(m_ifTimeOutKick != 1)
		{
			time_t newinqueuetime = cur_time+m_iWaitTime;
			LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)pMemory->AllocMemory(sizeof(STRUC_MSG_HEADER),false);
			tmpMsgHeader->pConn = ptmp->pConn;
			tmpMsgHeader->iCurrsequence = ptmp->iCurrsequence;
			m_timerQueuemap.insert(std::make_pair(newinqueuetime,tmpMsgHeader));
			m_cur_size_ ++;
		}
		if(m_cur_size_ > 0)
		{
			m_timer_value_ = GetEarliestTime();
		}
		return ptmp;
	}
	return NULL;
	
}

void CSocket::DeleteFromTimerQueue(lpngx_connection_t pConn)
{
	std::multimap<time_t,LPSTRUC_MSG_HEADER>::iterator pos,posend;
	CMemory *pMemory =CMemory::GetInstance();
	CLock lock(&m_timequeueMutex);
lblMTQM:
	pos    = m_timerQueuemap.begin();
	posend = m_timerQueuemap.end();
	for(; pos != posend; ++pos)
	{
		if(pos->second->pConn == pConn)
		{
			pMemory->FreeMemory(pos->second);                   //释放内存
			m_timerQueuemap.erase(pos);
			--m_cur_size_;
			goto lblMTQM;
		}
	}
	if(m_cur_size_ > 0)
	{
		m_timer_value_ = GetEarliestTime();
	}
	return;
}

void CSocket::clearAllFromTimerQueue()
{
	std::multimap<time_t,LPSTRUC_MSG_HEADER>::iterator pos,posend;
	CMemory *pMemory = CMemory::GetInstance();
	pos    = m_timerQueuemap.begin();
	posend = m_timerQueuemap.end();
	for(; pos != posend; ++pos)
	{
		pMemory->FreeMemory(pos->second);
		--m_cur_size_;
	}
	m_timerQueuemap.clear();
}

void *CSocket::ServerTimerQueueMonitorThread(void *threadData)
{
	ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
	CSocket *pSocketObj = pThread->_pThis;
	
	time_t absolute_time,cur_time;
	int err;
	
	while(g_stopEvent == 0)
	{
		if(pSocketObj->m_cur_size_ > 0)
		{
			absolute_time = pSocketObj->m_timer_value_;
			cur_time = time(NULL);
			if(absolute_time < cur_time)
			{
				//时间到了，可以处理
				std::list<LPSTRUC_MSG_HEADER> m_lsIdleList;
			    LPSTRUC_MSG_HEADER result;
			    
			    err = pthread_mutex_lock(&pSocketObj->m_timequeueMutex);
			    if(err != 0)
			    {
			    	ngx_log_stderr(err,"CSocekt::ServerTimerQueueMonitorThread()pthread_mutex_lock()失败，返回的错误码为%d!",err);
			    }
			    while((result = pSocketObj->GetOverTimeTimer(cur_time)) != NULL)    //获取所有超时节点
			    {
			    	m_lsIdleList.push_back(result);
			    }
			    err = pthread_mutex_unlock(&pSocketObj->m_timequeueMutex);
			    if(err != 0)
			    {
			    	ngx_log_stderr(err,"CSocekt::ServerTimerQueueMonitorThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
			    }
			    LPSTRUC_MSG_HEADER tmpmsg;
			    while(!m_lsIdleList.empty())
			    {
			    	tmpmsg = m_lsIdleList.front();
			    	m_lsIdleList.pop_front();
			    	pSocketObj->procPingTimeOutChecking(tmpmsg,cur_time);
			    }  
			}
		}//end (pSocketObj->m_cur_size_ > 0)
		usleep(500 * 1000); //为简化问题，我们直接每次休息500毫秒
	}//end while(g_stopEvent == 0)
	return (void*)0;
}

//心跳包检测时间到，该去检测心跳包是否超时的事宜，本函数只是把内存释放，子类应该重新事先该函数以实现具体的判断动作
void CSocket::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg,time_t curtime)
{
	CMemory *pMemory = CMemory::GetInstance();
	pMemory->FreeMemory(tmpmsg);
}