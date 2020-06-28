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
#include <pthread.h>   //���߳�

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_comm.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_lockmutex.h"  //�Զ��ͷŻ�������һ����
//������ʱ��Ĵ���������������������ʱ�򣬱������ᱻngx_epoll_process_events()������  ,�ٷ������ƺ���Ϊngx_http_wait_request_handler();
void CSocket::ngx_wait_request_handler(lpngx_connection_t c)
{  
    ssize_t reco = recvproc(c,c->precvbuf,c->irecvlen);
    if(reco <= 0 )
    {
    	return;
    }
    if(c->curStat == _PKG_HD_INIT)     
    {
    	if(reco == m_iLenPkgHeader)      //���յ�������ͷ
    	{
    		ngx_wait_request_handler_proc_p1(c);
    	}
    	else
    	{
    		c->curStat  = _PKG_HD_RECVING;
    		c->precvbuf = c->precvbuf + reco;
    		c->irecvlen = c->irecvlen - reco;
    	}
    }
    else if(c->curStat == _PKG_HD_RECVING)
    {
    	if(c->irecvlen == reco)           //�յ�������ͷ
    	{
    		ngx_wait_request_handler_proc_p1(c);
    	}
    	else
    	{
    		c->precvbuf = c->precvbuf + reco;
    		c->irecvlen = c->irecvlen - reco;
    	}
    }
    else if(c->curStat == _PKG_BD_INIT)
    {
    	if(reco = c->irecvlen)           //���յ���������
    	{
    		ngx_wait_request_handler_proc_plast(c);
    	}
    	else
    	{
    		c->curStat  = _PKG_BD_RECVING;
    		c->precvbuf = c->precvbuf + reco;
    		c->irecvlen = c->irecvlen - reco;
    	}
    }
    else if(c->curStat == _PKG_BD_RECVING)
    {
    	if(c->irecvlen == reco)          //�յ�ʣ�����а���
    	{
    		ngx_wait_request_handler_proc_plast(c);
    	}
    	else
    	{
    		c->precvbuf = c->precvbuf + reco;
    		c->irecvlen = c->irecvlen - reco;
    	}
    } 
    return;
}

ssize_t CSocket::recvproc(lpngx_connection_t c,char *buff,ssize_t buflen)     //������Ϣ
{
	ssize_t n;
	n = recv(c->fd, buff, buflen, 0);
	if(n == 0)                                      //�ͻ��������ر� 
	{
		ngx_close_connection(c);
		return -1;
	}
	//�ͻ������ӻ���
	if(n < 0)
	{
		if(errno == EAGAIN || errno == EWOULDBLOCK)
		{
			ngx_log_stderr(errno,"CSocket::recvproc()��errno == EAGAIN || errno == EWOULDBLOCK������");
			return -1;
		}
		if(errno == EINTR)
		{
			ngx_log_stderr(errno,"CSocket::recvproc()��errno == EINTR������");
			return -1;
		}
		if(errno == ECONNRESET)
		{
			ngx_log_stderr(errno,"CSocket::recvproc()��errno == ECONNRESET�������ͻ��˷������رգ�");
		}
		else
		{
			ngx_log_stderr(errno,"CSocket::recvproc()�з�������");
		}
		//ngx_log_stderr(0,"���ӱ��ͻ��� �� �����رգ�");
		ngx_close_connection(c);
		return -1;
	}
	return n;
}

//������׶�1�����Ϣͷ�����ͷ
void CSocket::ngx_wait_request_handler_proc_p1(lpngx_connection_t c)
{
	CMemory *pMemory = CMemory::GetInstance();
	
	LPCOMM_PKG_HEADER pPkgHeader;
	pPkgHeader = (LPCOMM_PKG_HEADER)c->dataHeadInfo;
	unsigned short e_pkgLen;
	e_pkgLen = ntohs(pPkgHeader->pkgLen);           //ntohs������ת������,htons������ת������
	if(e_pkgLen < m_iLenPkgHeader)                  //���ĳ���С�ڰ�ͷ����
	{
		c->curStat  = _PKG_HD_INIT;
		c->precvbuf = c->dataHeadInfo;
		c->irecvlen = m_iLenPkgHeader;
	}
	else if(e_pkgLen > (_PKG_MAX_LENGTH-1000))      //���ĳ��ȴ���ָ������
	{
		c->curStat  = _PKG_HD_INIT;
		c->precvbuf = c->dataHeadInfo;
		c->irecvlen = m_iLenPkgHeader;
	}
	else
	{
		//�Ϸ���ͷ
		char *pTmpBuffer  = (char *)pMemory->AllocMemory(m_iLenMsgHeader + e_pkgLen,false);  //��Ϣͷ+��Ϣ��
		c->ifnewrecvMem   = true;
		c->pnewMemPointer = pTmpBuffer;
		
		//����д��Ϣͷ
		LPSTRUC_MSG_HEADER ptmpMsgHeader = (LPSTRUC_MSG_HEADER)pTmpBuffer;
		ptmpMsgHeader->pConn = c;
		ptmpMsgHeader->iCurrsequence = c->iCurrsequence;                                     //�յ���ʱ�����ӳ���������ż�¼����Ϣͷ�������Ա������ã�
		
		//����д��ͷ����
		pTmpBuffer += m_iLenMsgHeader;
		memcpy(pTmpBuffer,pPkgHeader,m_iLenPkgHeader);
		if(e_pkgLen == m_iLenPkgHeader)
		{
			ngx_wait_request_handler_proc_plast(c);
		}
		else
		{
			c->curStat  = _PKG_BD_INIT;
			c->precvbuf = pTmpBuffer + m_iLenPkgHeader;
			c->irecvlen = e_pkgLen - m_iLenPkgHeader;
		}	
	}
	return;
}

//������׶ζ� ��������֮��Ĵ���
void CSocket::ngx_wait_request_handler_proc_plast(lpngx_connection_t c)
{
	//����Ϣ���������Ϣ�������
	//int irmqc = 0;
	//inMsgRecvQueue(c->pnewMemPointer,irmqc);
	//g_threadpool.Call(irmqc);
	
	g_threadpool.inMsgRecvQueueAndSignal(c->pnewMemPointer);
	
	//��������
	c->ifnewrecvMem   = false;
	c->pnewMemPointer = NULL;
	c->curStat        = _PKG_HD_INIT;    //��ԭ�հ�״̬������һ����
	c->precvbuf       = c->dataHeadInfo;  //�����հ�λ��
	c->irecvlen       = m_iLenPkgHeader; //�����հ���С
	return;
}

//��Ϣ�����
/*void CSocket::inMsgRecvQueue(char *buf,int irmqc)
{
	CLock lock(&m_recvMessageQueueMutex);
	m_MsgRecvQueue.push_back(buf);
	 ++m_iRecvMsgQueueCount;                  //����Ϣ��������+1��������Ϊ�ñ���������һ�㣬�� m_MsgRecvQueue.size()��Ч
    irmqc = m_iRecvMsgQueueCount;            //������Ϣ���е�ǰ��Ϣ�������浽irmqc
	//tmpoutMsgRecvQueue();
	ngx_log_stderr(0,"success�յ���һ�����������ݰ���");
	return;
}

char *CSocket::outMsgRecvQueue() 
{
    CLock lock(&m_recvMessageQueueMutex);	//����
    if(m_MsgRecvQueue.empty())
    {
        return NULL; //Ҳ�������������Σ� ��Ϣ�����У������ɵ��ˣ��������ΪNULL�ģ�        
    }
    char *sTmpMsgBuf = m_MsgRecvQueue.front(); //���ص�һ��Ԫ�ص������Ԫ�ش������
    m_MsgRecvQueue.pop_front();                //�Ƴ���һ��Ԫ�ص�������	
    --m_iRecvMsgQueueCount;                    //����Ϣ��������-1
    return sTmpMsgBuf;                         
}*/

//��Ϣ�����߳���������ר�Ŵ�����ֽ��յ���TCP��Ϣ
//pMsgBuf�����͹�������Ϣ����������Ϣ�������Խ��͵ģ�ͨ����ͷ���Լ�����������
//         ��Ϣ�����ʽ����Ϣͷ+��ͷ+���塿 
void CSocket::threadRecvProcFunc(char *pMsgBuf)
{
    return;
}
/*void CSocket::tmpoutMsgRecvQueue()
{
	if(m_MsgRecvQueue.empty())                        //��Ϣ����Ϊ�գ��˳�
	{
		return ;
	}
	int size = m_MsgRecvQueue.size();                 //һǧ�����ڿɽ���
	if(size < 1000)
	{
		return;
	}
	CMemory *pMemory = CMemory::GetInstance();
	int cha = size - 500;                             //һ����ȥ��500��
	for(int i = 0;i < cha; i++)
	{
		char *sTmpMsgBuf = m_MsgRecvQueue.front();    //ȡ��һ��
		m_MsgRecvQueue.pop_front();                   //�Ƴ���һ��
		pMemory->FreeMemory(sTmpMsgBuf);              //���ͷŵ�����
	}
	return ;
}*/

