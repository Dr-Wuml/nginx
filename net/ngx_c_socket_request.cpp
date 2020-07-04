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
#include "ngx_c_lockmutex.h"  //�Զ��ͷŻ�������һ����
//������ʱ��Ĵ���������������������ʱ�򣬱������ᱻngx_epoll_process_events()������  ,�ٷ������ƺ���Ϊngx_http_wait_request_handler();
void CSocket::ngx_read_request_handler(lpngx_connection_t pConn)
{ 
	bool isflood = false; 
    ssize_t reco = recvproc(pConn,pConn->precvbuf,pConn->irecvlen);
    if(reco <= 0 )
    {
    	return;
    }
    if(pConn->curStat == _PKG_HD_INIT)     
    {
    	if(reco == m_iLenPkgHeader)      //���յ�������ͷ
    	{
    		ngx_wait_request_handler_proc_p1(pConn,isflood);
    	}
    	else
    	{
    		pConn->curStat  = _PKG_HD_RECVING;
    		pConn->precvbuf = pConn->precvbuf + reco;
    		pConn->irecvlen = pConn->irecvlen - reco;
    	}
    }
    else if(pConn->curStat == _PKG_HD_RECVING)
    {
    	if(pConn->irecvlen == reco)           //�յ�������ͷ
    	{
    		ngx_wait_request_handler_proc_p1(pConn,isflood);
    	}
    	else
    	{
    		pConn->precvbuf = pConn->precvbuf + reco;
    		pConn->irecvlen = pConn->irecvlen - reco;
    	}
    }
    else if(pConn->curStat == _PKG_BD_INIT)
    {
    	if(reco = pConn->irecvlen)           //���յ���������
    	{
    		if(m_floodAkEnable == 1)
    		{
    			isflood = TestFlood(pConn);
    		}
    		ngx_wait_request_handler_proc_plast(pConn,isflood);
    	}
    	else
    	{
    		pConn->curStat  = _PKG_BD_RECVING;
    		pConn->precvbuf = pConn->precvbuf + reco;
    		pConn->irecvlen = pConn->irecvlen - reco;
    	}
    }
    else if(pConn->curStat == _PKG_BD_RECVING)
    {
    	if(pConn->irecvlen == reco)          //�յ�ʣ�����а���
    	{
    		if(m_floodAkEnable == 1)
    		{
    			isflood = TestFlood(pConn);
    		}
    		ngx_wait_request_handler_proc_plast(pConn,isflood);
    	}
    	else
    	{
    		pConn->precvbuf = pConn->precvbuf + reco;
    		pConn->irecvlen = pConn->irecvlen - reco;
    	}
    }
    
    if(isflood == true)
    {
    	 zdClosesocketProc(pConn);
    } 
    return;
}

//����������ʱ��д������
void CSocket::ngx_write_request_handler(lpngx_connection_t pConn)         
{
	CMemory *pMemory = CMemory::GetInstance();
	
	ssize_t sendsize = sendproc(pConn, pConn->psendbuf, pConn->isendlen);
	if(sendsize >0 && sendsize != pConn->isendlen)                       //����δ�������
	{
		pConn->psendbuf = pConn->psendbuf + sendsize;
		pConn->isendlen = pConn->isendlen - sendsize;
		return;
	}
	else if(sendsize == -1)
	{
		//�ⲻ̫���ܣ����Է�������ʱ֪ͨwrite�������ݣ�����ʱϵͳȴ֪ͨ�ҷ��ͻ���������
        ngx_log_stderr(errno,"CSocekt::ngx_write_request_handler()ʱif(sendsize == -1)��������ܹ��졣"); //��ӡ����־
        return;
	}
	
	if(sendsize > 0 && sendsize == pConn->isendlen)                                        //���ݷ�����ϣ��ɵ�������е�д�¼�
	{
		if(ngx_epoll_oper_event(pConn->fd, EPOLL_CTL_MOD, EPOLLOUT, 1, pConn) == -1)
		{
			ngx_log_stderr(errno,"CSocekt::ngx_write_request_handler()��ngx_epoll_oper_event()ʧ�ܡ�");
		}
		 ngx_log_stderr(0,"CSocekt::ngx_write_request_handler()�����ݷ�����ϣ��ܺá�");
	}
	
	if(sem_post(&m_semEventSendQueue) == -1)
	{
		 ngx_log_stderr(0,"CSocekt::ngx_write_request_handler()��sem_post(&m_semEventSendQueue)ʧ��.");
	}
	pMemory->FreeMemory(pConn->psendMemPointer);
	pConn->psendMemPointer = NULL;
	--pConn->iThrowsendCount;
	
	return ;
}
ssize_t CSocket::recvproc(lpngx_connection_t c,char *buff,ssize_t buflen)     //������Ϣ
{
	ssize_t n;
	n = recv(c->fd, buff, buflen, 0);
	if(n == 0)                                      //�ͻ��������ر� 
	{
        //ngx_log_stderr(0,"���ӱ��ͻ��������ر�[4·���ֹر�]��");
        //ngx_close_connection(c);
        zdClosesocketProc(c);
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
			//ngx_log_stderr(errno,"CSocket::recvproc()��errno == ECONNRESET�������ͻ��˷������رգ�");
		}
		else
		{
			ngx_log_stderr(errno,"CSocket::recvproc()�з�������");
		}
		//ngx_log_stderr(0,"���ӱ��ͻ��� �� �����رգ�");
		//ngx_close_connection(c);
		zdClosesocketProc(c);
		return -1;
	}
	return n;
}

//������׶�1�����Ϣͷ�����ͷ
void CSocket::ngx_wait_request_handler_proc_p1(lpngx_connection_t pConn,bool &isflood)
{
	CMemory *pMemory = CMemory::GetInstance();
	
	LPCOMM_PKG_HEADER pPkgHeader;
	pPkgHeader = (LPCOMM_PKG_HEADER)pConn->dataHeadInfo;
	unsigned short e_pkgLen;
	e_pkgLen = ntohs(pPkgHeader->pkgLen);           //ntohs������ת������,htons������ת������
	if(e_pkgLen < m_iLenPkgHeader)                  //���ĳ���С�ڰ�ͷ����
	{
		pConn->curStat  = _PKG_HD_INIT;
		pConn->precvbuf = pConn->dataHeadInfo;
		pConn->irecvlen = m_iLenPkgHeader;
	}
	else if(e_pkgLen > (_PKG_MAX_LENGTH-1000))      //���ĳ��ȴ���ָ������
	{
		pConn->curStat  = _PKG_HD_INIT;
		pConn->precvbuf = pConn->dataHeadInfo;
		pConn->irecvlen = m_iLenPkgHeader;
	}
	else
	{
		//�Ϸ���ͷ
		char *pTmpBuffer  = (char *)pMemory->AllocMemory(m_iLenMsgHeader + e_pkgLen,false);  //��Ϣͷ+��Ϣ��
		pConn->precvMemPointer = pTmpBuffer;
		
		//����д��Ϣͷ
		LPSTRUC_MSG_HEADER ptmpMsgHeader = (LPSTRUC_MSG_HEADER)pTmpBuffer;
		ptmpMsgHeader->pConn = pConn;
		ptmpMsgHeader->iCurrsequence = pConn->iCurrsequence;                                     //�յ���ʱ�����ӳ���������ż�¼����Ϣͷ�������Ա������ã�
		
		//����д��ͷ����
		pTmpBuffer += m_iLenMsgHeader;
		memcpy(pTmpBuffer,pPkgHeader,m_iLenPkgHeader);
		if(e_pkgLen == m_iLenPkgHeader)
		{
			if(m_floodAkEnable == 1)
			{
				isflood = TestFlood(pConn);
			}
			ngx_wait_request_handler_proc_plast(pConn,isflood);
		}
		else
		{
			pConn->curStat  = _PKG_BD_INIT;
			pConn->precvbuf = pTmpBuffer + m_iLenPkgHeader;
			pConn->irecvlen = e_pkgLen - m_iLenPkgHeader;
		}	
	}
	return;
}

//������׶ζ� ��������֮��Ĵ���
void CSocket::ngx_wait_request_handler_proc_plast(lpngx_connection_t pConn,bool &isflood)
{
	//����Ϣ���������Ϣ�������
	//int irmqc = 0;
	//inMsgRecvQueue(c->precvMemPointer,irmqc);
	//g_threadpool.Call(irmqc);
	if(isflood == false)
	{
		g_threadpool.inMsgRecvQueueAndSignal(pConn->precvMemPointer);
	}
    else
    {
    	CMemory *pMemory = CMemory::GetInstance();
    	pMemory->FreeMemory(pConn->precvMemPointer);                     //�ͷ��ڴ�
    }
	//��������
	//pConn->ifnewrecvMem   = false;
	pConn->precvMemPointer = NULL;
	pConn->curStat        = _PKG_HD_INIT;    //��ԭ�հ�״̬������һ����
	pConn->precvbuf       = pConn->dataHeadInfo;  //�����հ�λ��
	pConn->irecvlen       = m_iLenPkgHeader; //�����հ���С
	return;
}

//��������ר�ú��������ر��η��͵��ֽ���
//���� > 0���ɹ�������һЩ�ֽ�
//=0�����ƶԷ�����
//-1��errno == EAGAIN ���������ͻ���������
//-2��errno != EAGAIN != EWOULDBLOCK != EINTR ��һ������Ϊ���ǶԶ˶Ͽ��Ĵ���
ssize_t CSocket::sendproc(lpngx_connection_t c,char *buff,ssize_t size)
{
	ssize_t n;
	for(;;)
	{
		n = send(c->fd, buff, size, 0);
		if(n > 0)
		{
			return n;
		}
		
		if(n == 0)         //���ӳ�ʱ�Ͽ����£�����recv�д�������
		{
			return 0;
		}
		
		if(errno == EAGAIN) //�ں˻���������
		{
			return -1;
		}
		
		if(errno == EINTR)
		{
			ngx_log_stderr(errno,"CSocekt::sendproc()��send()ʧ��."); 
		}
		else                //ͬ������recvģ�촦��
		{
			return -2;
		}
	}
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

