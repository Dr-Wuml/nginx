#ifndef __NGX_C_SLOGIC_H__
#define __NGX_C_SLOGIC_H__

#include <sys/socket.h>
#include "ngx_c_socket.h"


//处理逻辑和通讯的子类
class CLogicSocket : public CSocket
{
public:
	CLogicSocket();
	virtual ~CLogicSocket();
	virtual bool Initialize();
	
public:
	//业务逻辑部分
	bool _HandleRegister(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength);
	bool _HandleLogin(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength);
	
public:
	virtual void threadRecvProcFunc(char *pMsgBuf);
};

#endif