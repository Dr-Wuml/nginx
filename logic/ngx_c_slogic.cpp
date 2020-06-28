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

typedef bool (CLogicSocket::*handler)(lpngx_connection_t pConn,              //���ӳ������ӵ�ָ��
	                                  LPSTRUC_MSG_HEADER pMsgHeader,         //��Ϣͷָ��
	                                  char *pPkgBody,                        //����ָ��
	                                  unsigned short iBodyLength);            //���峤��

//���������Ա����ָ�������
static const handler statusHandler[] = 
{
	//ǰ���Ԫ�ر���������չ
	NULL,                                       //��0��
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
	void *pPkgBody = NULL;
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
			//ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()��CRC���󣬶�������!");    //��ʽ�����п��Ըɵ������Ϣ
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
bool CLogicSocket::_HandleRegister(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
    ngx_log_stderr(0,"ִ����CLogicSocket::_HandleRegister()!");
    return true;
}
bool CLogicSocket::_HandleLogin(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
    ngx_log_stderr(0,"ִ����CLogicSocket::_HandleLogIn()!");
    return true;
}