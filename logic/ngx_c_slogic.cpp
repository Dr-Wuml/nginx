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
#include <pthread.h>   //多线程

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
//#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_crc32.h"
#include "ngx_c_slogic.h"  
#include "ngx_logiccomm.h"  

typedef bool (CLogicSocket::*handler)(lpngx_connection_t pConn,              //连接池中连接的指针
	                                  LPSTRUC_MSG_HEADER pMsgHeader,         //消息头指针
	                                  char *pPkgBody,                        //包体指针
	                                  unsigned short iBodyLength);            //包体长度

//用来保存成员函数指针的数组
static const handler statusHandler[] = 
{
	//前五个元素保留，待拓展
	NULL,                                       //【0】
	NULL,                                       //【1】
	NULL,                                       //【2】
	NULL,                                       //【3】
	NULL,                                       //【4】
	&CLogicSocket::_HandleRegister,             //【5】：实现具体的注册功能
	&CLogicSocket::_HandleLogin,                //【6】：实现具体的登录功能
};   
#define AUTH_TOTAL_COMMANDS sizeof(statusHandler)/sizeof(handler)   

CLogicSocket::CLogicSocket()
{
	
}   
   
CLogicSocket::~CLogicSocket()
{
		
}  


//初始化函数【fork()子进程之前使用】
//成功返回true，失败返回false  
bool CLogicSocket::Initialize()
{
	bool bParentInit = CSocket::Initialize();
	return bParentInit;
}  

//处理收到的数据包
//pMsgBuf：消息头 + 包头 + 包体 ：自解释；
void CLogicSocket::threadRecvProcFunc(char *pMsgBuf)
{
	LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;                   //消息头
	LPCOMM_PKG_HEADER  pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf+m_iLenMsgHeader);  //包头
	void *pPkgBody = NULL;
	unsigned short pkglen = ntohs(pPkgHeader->pkgLen);
	
	if(m_iLenPkgHeader == pkglen)
	{
		//没有包体只有包头
		if(pPkgHeader->crc32 != 0)
		{
			return ;
		}
		pPkgBody = NULL;
	}
	else
	{
		//有包体
		pPkgHeader->crc32 = ntohl(pPkgHeader->crc32);                            //针对4字节的数据，网络序转主机序
		pPkgBody = (void *)(pMsgBuf + m_iLenMsgHeader + m_iLenPkgHeader);        //指向包体位置
		
		//计算crc值判断包的完整性
		int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char *)pPkgBody,pkglen - m_iLenPkgHeader);
		if(calccrc != pPkgHeader->crc32)
		{
			//ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中CRC错误，丢弃数据!");    //正式代码中可以干掉这个信息
			return; //crc错，直接丢弃
		}	
	}
	
	//检验成功后
	unsigned short imsgCode = ntohs(pPkgHeader->msgCode);                         //消息代码，业务类型
	lpngx_connection_t pConn = pMsgHeader->pConn;                                 //消息头中保存的连接池指针
	//(1)检查连接是否存在
	if(pConn->iCurrsequence != pMsgHeader->iCurrsequence)   //该连接池中连接以被其他tcp连接占用，原来的客户端和本服务器的连接断了，这种包直接丢弃不理
    {
        return; //丢弃不理这种包了【客户端断开了】
    }
    
    //(2)判断消息码的真伪性
    if(imsgCode >= AUTH_TOTAL_COMMANDS)                      //无符号不可能小于0
    {
    	ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码不对!",imsgCode);
        return; //丢弃不理这种包【恶意包或者错误包】
    }
    
    //(3)查找对应的业务处理函数
    if(statusHandler[imsgCode] == NULL)
    {
    	ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码找不到对应的处理函数!",imsgCode); //暂不支持的业务
        return;  //没有相关的处理函数
    }
	//(4)调用消息码对应的成员函数来处理
    (this->*statusHandler[imsgCode])(pConn,pMsgHeader,(char *)pPkgBody,pkglen-m_iLenPkgHeader);
    return;	
}
//处理各种业务逻辑
bool CLogicSocket::_HandleRegister(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
    ngx_log_stderr(0,"执行了CLogicSocket::_HandleRegister()!");
    return true;
}
bool CLogicSocket::_HandleLogin(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
    ngx_log_stderr(0,"执行了CLogicSocket::_HandleLogIn()!");
    return true;
}