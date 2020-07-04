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
//#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_crc32.h"
#include "ngx_c_slogic.h"  
#include "ngx_logiccomm.h" 
#include "ngx_c_lockmutex.h"   

typedef bool (CLogicSocket::*handler)(lpngx_connection_t pConn,              //���ӳ������ӵ�ָ��
	                                  LPSTRUC_MSG_HEADER pMsgHeader,         //��Ϣͷָ��
	                                  char *pPkgBody,                        //����ָ��
	                                  unsigned short iBodyLength);            //���峤��

//���������Ա����ָ�������
static const handler statusHandler[] = 
{
	//ǰ���Ԫ�ر���������չ
	&CLogicSocket::_HandlePing,                 //��0������������
	NULL,                                       //��1��
	NULL,                                       //��2��
	NULL,                                       //��3��
	NULL,                                       //��4��
	&CLogicSocket::_HandleRegister,             //��5����ʵ�־����ע�Ṧ��
	&CLogicSocket::_HandleLogin,                //��6����ʵ�־���ĵ�¼����
};   
#define AUTH_TOTAL_COMMANDS sizeof(statusHandler)/sizeof(handler)   

CLogicSocket::CLogicSocket()
{
	
}   
   
CLogicSocket::~CLogicSocket()
{
		
}  


//��ʼ��������fork()�ӽ���֮ǰʹ�á�
//�ɹ�����true��ʧ�ܷ���false  
bool CLogicSocket::Initialize()
{
	bool bParentInit = CSocket::Initialize();
	return bParentInit;
}  

//�����յ������ݰ�
//pMsgBuf����Ϣͷ + ��ͷ + ���� ���Խ��ͣ�
void CLogicSocket::threadRecvProcFunc(char *pMsgBuf)
{
	LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;                   //��Ϣͷ
	LPCOMM_PKG_HEADER  pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf+m_iLenMsgHeader);  //��ͷ
	void *pPkgBody ;
	unsigned short pkglen = ntohs(pPkgHeader->pkgLen);
	
	if(m_iLenPkgHeader == pkglen)
	{
		//û�а���ֻ�а�ͷ
		if(pPkgHeader->crc32 != 0)
		{
			return ;
		}
		pPkgBody = NULL;
	}
	else
	{
		//�а���
		pPkgHeader->crc32 = ntohl(pPkgHeader->crc32);                            //���4�ֽڵ����ݣ�������ת������
		pPkgBody = (void *)(pMsgBuf + m_iLenMsgHeader + m_iLenPkgHeader);        //ָ�����λ��
		
		//����crcֵ�жϰ���������
		int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char *)pPkgBody,pkglen - m_iLenPkgHeader);
		if(calccrc != pPkgHeader->crc32)
		{
			ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()��CRC���󣬶�������!");    //��ʽ�����п��Ըɵ������Ϣ
			return; //crc��ֱ�Ӷ���
		}	
	}
	
	//����ɹ���
	unsigned short imsgCode = ntohs(pPkgHeader->msgCode);                         //��Ϣ���룬ҵ������
	lpngx_connection_t pConn = pMsgHeader->pConn;                                 //��Ϣͷ�б�������ӳ�ָ��
	//(1)��������Ƿ����
	if(pConn->iCurrsequence != pMsgHeader->iCurrsequence)   //�����ӳ��������Ա�����tcp����ռ�ã�ԭ���Ŀͻ��˺ͱ������������Ӷ��ˣ����ְ�ֱ�Ӷ�������
    {
        return; //�����������ְ��ˡ��ͻ��˶Ͽ��ˡ�
    }
    
    //(2)�ж���Ϣ�����α��
    if(imsgCode >= AUTH_TOTAL_COMMANDS)                      //�޷��Ų�����С��0
    {
    	ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()��imsgCode=%d��Ϣ�벻��!",imsgCode);
        return; //�����������ְ�����������ߴ������
    }
    
    //(3)���Ҷ�Ӧ��ҵ������
    if(statusHandler[imsgCode] == NULL)
    {
    	ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()��imsgCode=%d��Ϣ���Ҳ�����Ӧ�Ĵ�����!",imsgCode); //�ݲ�֧�ֵ�ҵ��
        return;  //û����صĴ�����
    }
	//(4)������Ϣ���Ӧ�ĳ�Ա����������
    (this->*statusHandler[imsgCode])(pConn,pMsgHeader,(char *)pPkgBody,pkglen-m_iLenPkgHeader);
    return;	
}


//�������ҵ���߼�

//��������ʱ��⺯��
void CLogicSocket::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg,time_t cur_time)
{
	CMemory *pMemory = CMemory::GetInstance();
	
	if(tmpmsg->iCurrsequence == tmpmsg->pConn->iCurrsequence)                       //���Ӵ���
	{
		lpngx_connection_t pConn = tmpmsg->pConn;
		if(m_ifTimeOutKick == 1)
		{
			zdClosesocketProc(pConn);
		}
		else if((cur_time - pConn->lastPingTime) > m_iWaitTime*3 + 10)
		{
			//ngx_log_stderr(0,"ʱ�䵽�������������߳�ȥ!");   //�о�OK
			zdClosesocketProc(pConn); 
		}
		pMemory->FreeMemory(tmpmsg);
	}
	else
	{
		pMemory->FreeMemory(tmpmsg);
	}
}

//ֻ���Ͱ�ͷ�ĺ���
void CLogicSocket::SendNoBodyPkgToClient(LPSTRUC_MSG_HEADER pMsgHeader,unsigned short iMsgCode)
{
	CMemory *pMemory = CMemory::GetInstance();
	
	char *pSendBuf = (char *) pMemory->AllocMemory(m_iLenMsgHeader+m_iLenPkgHeader, false);
	char *pTmpBuf  = pSendBuf;
	memcpy(pTmpBuf,pMsgHeader,m_iLenMsgHeader);
	pTmpBuf += m_iLenMsgHeader;
	
	LPCOMM_PKG_HEADER pPkgHeader = (LPCOMM_PKG_HEADER) pTmpBuf;  //�õ�Ҫ���͵İ�ͷ
	pPkgHeader->msgCode = htons(iMsgCode);
	pPkgHeader->pkgLen  = htons(m_iLenPkgHeader);
	pPkgHeader->crc32   = 0;
	msgSend(pSendBuf);
	return;
}

//������ʵ��
bool CLogicSocket::_HandlePing(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
	if(iBodyLength != 0)            //���������ֻ�а�ͷ���ް��壬�����а�����Ϊ�ǷǷ���
	{
		return false;
	}
	
	CLock(&pConn->logicPorcMutex);
	pConn->lastPingTime = time(NULL);
	SendNoBodyPkgToClient(pMsgHeader,_CMD_PING);
	ngx_log_stderr(0,"this is pkg is heartbeat.");
	return true;
}

//ע���
bool CLogicSocket::_HandleRegister(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
	if(pPkgBody == NULL)
	{
		return false;
	}
	
	int iRecvLen = sizeof(STRUCT_REGISTER);
	if(iRecvLen != iBodyLength)
	{
		return false;
	}
	
	CLock lock(&pConn->logicPorcMutex);
	
	LPSTRUCT_REGISTER pRecvInfo = (LPSTRUCT_REGISTER)pPkgBody;    //ȡ�ð�������
	pRecvInfo->iType    = ntohl(pRecvInfo->iType);
	pRecvInfo->username[sizeof(pRecvInfo->username) - 1] = 0;
	pRecvInfo->password[sizeof(pRecvInfo->password) - 1] = 0;
	
    //����Ϣ��Ӧ��
	LPCOMM_PKG_HEADER pPkgHeader;
	CMemory *pMemory  = CMemory::GetInstance();
	CCRC32  *pCrc32   = CCRC32::GetInstance();
	int     iSendLen  = sizeof(STRUCT_REGISTER);
	char    *pSendBuf = (char *)pMemory->AllocMemory(m_iLenMsgHeader+m_iLenPkgHeader+iSendLen,false);
	memcpy(pSendBuf,pMsgHeader,m_iLenMsgHeader);                  //������Ϣͷ
	pPkgHeader = (LPCOMM_PKG_HEADER)(pSendBuf+m_iLenMsgHeader);  //ָ���ͷ
	pPkgHeader->msgCode = _CMD_REGISTER;                          //��Ϣ����
	pPkgHeader->msgCode = htons(pPkgHeader->msgCode);             //ҵ�����,������ת������
	pPkgHeader->pkgLen  = htons(m_iLenPkgHeader + iSendLen);      //������ ��ͷ+����
	
	//�������
	LPSTRUCT_REGISTER pSendInfo = (LPSTRUCT_REGISTER)(pSendBuf + m_iLenMsgHeader + m_iLenPkgHeader);
	
	//�������CRC32ֵ
	pPkgHeader->crc32 = pCrc32->Get_CRC((unsigned char*)pSendInfo,iSendLen);
	pPkgHeader->crc32 = htonl(pPkgHeader->crc32);
	//�������ݰ�
    msgSend(pSendBuf);
    //���ʱ��OK��add_event
    /*if(ngx_epoll_oper_event(pConn->fd,                 //socket���
                            EPOLL_CTL_MOD,
                            EPOLLOUT,              //����д ,�����Ϊ1����ʾ�ͻ���Ӧ���������ҷ�����������Ϣ���ҷ�������Ҫ�����յ��ͻ��˵���Ϣ��
                            0,                                                      
                            pConn) == -1)
                      {
                           ngx_log_stderr(0,"111111111111!");
                      }
    

   /*
    sleep(100);  //��Ϣ��ô��ʱ��
    //������ӻ����ˣ���϶���iCurrsequence������
    if(pMsgHeader->iCurrsequence != pConn->iCurrsequence)
    {
        //Ӧ���ǲ��ȣ���Ϊ��������Ѿ���������
        ngx_log_stderr(0,"��������,%L--%L",pMsgHeader->iCurrsequence,pConn->iCurrsequence);
    }
    else
    {
        ngx_log_stderr(0,"�������Ŷ,%L--%L",pMsgHeader->iCurrsequence,pConn->iCurrsequence);
    }*/        
	
	
    ngx_log_stderr(0,"ִ����CLogicSocket::_HandleRegister()!");
    return true;
}

//��¼��
bool CLogicSocket::_HandleLogin(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
    ngx_log_stderr(0,"ִ����CLogicSocket::_HandleLogIn()!");
    return true;
}