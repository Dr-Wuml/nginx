#ifndef __NGX_GLOBAL_H__
#define __NGX_GLOBAL_H__

#include "ngx_c_slogic.h"
#include "ngx_c_threadpool.h"
#include <signal.h>
//通用参数定义在这个位置
//配置结构定义
typedef struct _CConfItem{
    char ItemName[50];        //配置左部key
    char ItemContent[500];    //配置右部value
}CConfItem,*LPCConfItem;

//日志结构定义
typedef struct{
    int log_level; //日志级别
    int fd;        //文件描述符
}ngx_log_t;



//外部变量 —— 环境配置
extern size_t          g_argvneedmem;
extern size_t          g_envneedmem;
extern int             g_os_argc;
extern char            **g_os_argv;
extern char            *gp_envmem;
extern int             g_daemonized;
extern CLogicSocket    g_socket;
extern CThreadPool     g_threadpool;
//extern int g_environlen;

//外部变量 —— 日志

//外部变量 —— 信号
extern pid_t           ngx_pid;  
extern pid_t           ngx_parent;
extern ngx_log_t       ngx_log;
extern int             ngx_process;   
extern sig_atomic_t    ngx_reap; 


#endif 
