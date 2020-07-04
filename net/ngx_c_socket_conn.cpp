#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO��
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

ngx_connection_s::ngx_connection_s()         //���캯��
{
	iCurrsequence = 0;
	pthread_mutex_init(&logicPorcMutex,NULL);//��������ʼ��
}
ngx_connection_s::~ngx_connection_s()        //��������
{
	pthread_mutex_destroy(&logicPorcMutex) ;  //�������ͷ�
}

//���ӷ����ʼ��
void ngx_connection_s::GetOneToUse()
{
	++iCurrsequence;
	
	curStat  = _PKG_HD_INIT;                  //�հ���ʼ״̬
	precvbuf = dataHeadInfo;                 //����Ϣ���λ��
	irecvlen = sizeof(COMM_PKG_HEADER);      //ָ���������ݳ���
	
	precvMemPointer = NULL;                  //��δ�����ڴ�
	iThrowsendCount = 0;                     //ԭ�ӵ�
	psendMemPointer = NULL;                  //��������ͷָ���¼
	events          = 0;                     //��ʼ���¼�
	lastPingTime    = time(NULL);            //�ϴ�Ping��ʱ��
	
	FloodkickLastTime = 0;                   //Flood�����ϴ��յ�����ʱ��
	FloodAttackCount  = 0;                   //Flood�ڸ�ʱ�����յ����Ĵ���ͳ��
}

//���յ�������������һЩ����
void ngx_connection_s::PutOneToFree()
{
	++iCurrsequence;
	if(precvMemPointer != NULL)              //���ջ�����
	{
		CMemory::GetInstance()->FreeMemory(precvMemPointer);
		precvMemPointer = NULL;
	}
	if(psendMemPointer != NULL)              //���ͻ�����
	{
		CMemory::GetInstance()->FreeMemory(psendMemPointer);
		psendMemPointer = NULL;
	}
	iThrowsendCount = 0;
}

void CSocket::initconnection()
{
	lpngx_connection_t pConn;
	CMemory *pMemory = CMemory::GetInstance();
	
	int ilenconnpool = sizeof(ngx_connection_t);
	for(int i = 0;i < m_worker_connections; i++)                                 //��ʼ������m_worker_connections������
	{
		pConn = (lpngx_connection_t)pMemory->AllocMemory(ilenconnpool,true);
		pConn = new(pConn) ngx_connection_t();
		pConn->GetOneToUse();
		m_connectionList.push_back(pConn);                                       //ȫ������  ռ��+����
		m_freeconnectionList.push_back(pConn);                                   //��������
	}
	m_free_connection_n = m_total_connection_n = m_connectionList.size();        //��ʼ�׶������б�һ����
}

//���ջ������ӳأ��ͷ��ڴ�
void CSocket::clearconnection()
{
	lpngx_connection_t pConn;
	CMemory *pMemory = CMemory::GetInstance();
	while(!m_connectionList.empty())
	{
		pConn = m_connectionList.front();
		m_connectionList.pop_front();
		pConn->~ngx_connection_s();
		pMemory->FreeMemory(pConn);
	}
}

//��ȡһ����������ȡ��
lpngx_connection_t CSocket::ngx_get_connection(int isock)
{
	CLock lock(&m_connectionMutex);

	if(!m_freeconnectionList.empty())
	{
		lpngx_connection_t pConn = m_freeconnectionList.front();                       //���ص�һ����������
		m_freeconnectionList.pop_front();
		pConn->GetOneToUse();
		--m_free_connection_n;
		pConn->fd = isock;
		return pConn;
	}
    
    //û�����ӣ�����һ������
    CMemory *pMemory = CMemory::GetInstance();
    lpngx_connection_t pConn = (lpngx_connection_t)pMemory->AllocMemory(sizeof(ngx_connection_t),true);
    pConn = new(pConn)ngx_connection_t();
    pConn->GetOneToUse();
    m_connectionList.push_back(pConn);
    ++m_total_connection_n;
    pConn->fd = isock;
    return pConn;
}

//��������
void CSocket::ngx_free_connection(lpngx_connection_t pConn)
{
	CLock lock(&m_connectionMutex);
	
	pConn->PutOneToFree();
	m_freeconnectionList.push_back(pConn); // ���뵽��������
	++m_free_connection_n;
	return;
}

//���������������
void CSocket::inRecyConnectQueue(lpngx_connection_t pConn)
{
	//ngx_log_stderr(0,"CSocket::inRecyConnectQueue()��ִ�У����ӷ�����ͷ��б�.");
	std::list<lpngx_connection_t>::iterator pos;
	bool iffind = false;
	CLock lock(&m_recyconnqueueMutex);              //���ӻ����б�Ļ��������߳�ServerRecyConnectionThread()ҲҪ�õ���������б�
	
	for(pos = m_recyconnectionList.begin(); pos != m_recyconnectionList.begin(); ++pos)
	{
		if((*pos) == pConn)
		{
			iffind = true;
			break;
		}
	}
	
	if(iffind == true)
	{
		return;
	}
	
	pConn->inRecyTime = time(NULL);                 //��¼����ʱ��
	++pConn->iCurrsequence;
	m_recyconnectionList.push_back(pConn);          //����������������ȴ�ServerRecyConnectionThread�̴߳���
	++m_total_recyconnection_n;                     //���ͷŶ��д�С+1
	--m_onlineUserCount;                            //�û���������
	return;
}


//������ʱ�����߳�
void* CSocket::ServerRecyConnectionThread(void *threadData)
{
	ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
	CSocket *pSocketObj = pThread->_pThis;
	
	time_t currtime;
	int err;
	std::list<lpngx_connection_t>::iterator pos,posend;
    lpngx_connection_t pConn;
    
    while(1)
    {
    	usleep(200 *1000);
    	if(pSocketObj->m_total_recyconnection_n > 0)
    	{
    		currtime = time(NULL);
    		err = pthread_mutex_lock(&pSocketObj->m_recyconnqueueMutex);
    		if(err != 0)
    		{
    			ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()��pthread_mutex_lock()ʧ�ܣ����صĴ�����Ϊ%d!",err);
    		}
lblRRTD:
	        pos    = pSocketObj->m_recyconnectionList.begin();
	        posend = pSocketObj->m_recyconnectionList.end();
	        for(; pos != posend; ++pos)
	        {
	        	pConn = (*pos);
	        	if( ((pConn->inRecyTime + pSocketObj->m_RecyConnectionWaitTime) > currtime) && (g_stopEvent == 0) )
	        	{
	        		continue;
	        	}
	        	
	        	if(pConn->iThrowsendCount > 0)
                {
                    //��ȷʵ��Ӧ�ã���ӡ����־�ɣ�
                    ngx_log_stderr(0,"CSocekt::ServerRecyConnectionThread()�е��ͷ�ʱ��ȴ����p_Conn.iThrowsendCount!=0��������÷���");
                    //��������ʱɶҲ���ң�·�̼��������ߣ�����ȥ�ͷŰɡ�
                }
	        	//Ԥ��
	        	--pSocketObj->m_total_recyconnection_n;                             //���ͷŶ��м�һ
	        	pSocketObj->m_recyconnectionList.erase(pos);                        //ɾ���ýڵ�
	        	pSocketObj->ngx_free_connection(pConn);                             //�黹������
	        	goto lblRRTD;
	        }//end for
	        err = pthread_mutex_unlock(&pSocketObj->m_recyconnqueueMutex);
	        if(err != 0)
	        {
	        	ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()pthread_mutex_unlock()ʧ�ܣ����صĴ�����Ϊ%d!",err);
	        }  
    	}//end if
    	if(g_stopEvent == 1)                                                        //�˳���������
    	{
    		if(pSocketObj->m_total_recyconnection_n > 0)
    		{
    			err = pthread_mutex_lock(&pSocketObj->m_recyconnqueueMutex);
    			if(err != 0)
    			{
    				ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()��pthread_mutex_lock2()ʧ�ܣ����صĴ�����Ϊ%d!",err);
    			}
    		lblRRTD2:
    			pos    = pSocketObj->m_recyconnectionList.begin();
	            posend = pSocketObj->m_recyconnectionList.end();
	            for(; pos != posend; pos++)
	            {
	            	pConn = (*pos);
	            	--pSocketObj->m_total_recyconnection_n;                             //���ͷŶ��м�һ
	        	    pSocketObj->m_recyconnectionList.erase(pos);                        //ɾ���ýڵ�
	        	    pSocketObj->ngx_free_connection(pConn);                             //�黹������
	        	    goto lblRRTD2;
	            }//end for
	            err = pthread_mutex_unlock(&pSocketObj->m_recyconnqueueMutex);
	            if(err != 0)
    			{
    				ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()��pthread_mutex_lock2()ʧ�ܣ����صĴ�����Ϊ%d!",err);
    			}
    		}//end if
    		break;
    	}//end if
    }//end while
    return (void*)0;
}

//��ngx_close_accepted_connection()���������������ļ�ngx_socket_accept.cxxǨ�Ƶ����ļ��У����������д���
void CSocket::ngx_close_connection(lpngx_connection_t pConn)
{
	ngx_free_connection(pConn);
	if(close(pConn->fd) == -1)
	{
		ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocket::ngx_close_connection()��close(%d)ʧ�ܣ�",pConn->fd);
	}
	pConn->fd = -1 ;
	return;
}