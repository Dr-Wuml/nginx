#ifndef __NGX_LOGICCOMM_H__
#define __NGX_LOGICCOMM_H__ 

#define _CMD_START	                    0  
#define _CMD_REGISTER 		            _CMD_START + 5   //注册
#define _CMD_LOGIN 		                _CMD_START + 6   //登录

#pragma pack (1)

//注册结构
typedef struct _STRUCT_REGISTER
{
	int         iType;             //类型
	char        username[56];      //用户名
	char        password[40];      //密码
}STRUCT_REGISTER,LPSTRUCT_REGISTER;

//登录结构
typedef struct _STRUCT_LOGIN
{
	char        username[56];      //用户名
	char        password[40];      //密码
}STRUCT_LOGIN,LPSTRUCT_LOGIN;

#pragma pack ()

#endif