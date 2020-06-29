#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include "ngx_c_conf.h"
#include "ngx_func.h"
#include "ngx_macro.h"   //各种宏定义
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_crc32.h"       //和crc32校验算法有关 
#include "ngx_c_slogic.h"      //和socket通讯相关

static void freeresource(); //回收资源

//设置标题有关全局变量
size_t g_argvneedmem = 0;//保存下这些argv参数所需要的内存大小
size_t g_envneedmem = 0; //环境变量所占内存大小
int    g_os_argc;           //参数个数
char   **g_os_argv;        //原始命令行参数数组,在main中会被赋值
char   *gp_envmem = NULL;  //指向自己分配的env环境变量的内存，在ngx_init_setproctitle()函数中会被分配内存
int    g_daemonized=0;         //守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了


//socket相关
//CSocket g_socket;
CLogicSocket g_socket;
CThreadPool  g_threadpool;      //线程池全局对象

//和进程本身有关的全局量
pid_t ngx_pid;               //当前进程的pid
pid_t ngx_parent;            //父进程的pid
int   ngx_process;           //进程类型，比如master,worker进程等
int   g_stopEvent;           //程序退出标志

sig_atomic_t  ngx_reap;      //标记子进程状态变化
int main(int argc,char *const *argv)
{
    int exitcode = 0;   //退出代码，0表示正常
    int i;
    
    //(0)先初始化的变量
    g_stopEvent = 0;            //标记程序是否退出，0不退出 
    
    ngx_pid = getpid();//取得进程pid
    ngx_parent = getppid();//父进程pid;
    
    g_argvneedmem = 0;
    for(i=0 ;i<argc; i++)
    {
        g_argvneedmem += strlen(argv[i]) + 1;
    }
    for(i=0 ; environ[i]; i++)
    {
        g_envneedmem += strlen(environ[i]) + 1;
    }

    g_os_argc = argc;
    g_os_argv = (char **) argv;
    

    ngx_log.fd = -1;
    ngx_process = NGX_PROCESS_MASTER;
    ngx_reap = 0;   //标记子进程的变化
    //初始化配置文件
    CConfig *pConfig = CConfig::GetInstance();
    if(pConfig->Load("nginx.conf") == false)
    {
        ngx_log_init();
        ngx_log_stderr(0,"配置文件[%s]加载失败，退出程序!\n","nginx.conf");
        //exit(1);终止进程，在main中出现和return效果一样 ,exit(0)表示程序正常, exit(1)/exit(-1)表示程序异常退出，exit(2)表示表示系统找不到指定的文件
        exitcode = 2;
        goto lblexit;
    }
    
    CMemory::GetInstance();
    CCRC32::GetInstance();

    ngx_log_init(); //初始化日志文件
    
    //信号初始化
    if(ngx_init_signals() != 0)
    {
        exitcode = 1;
        goto lblexit;
    }

    //初始化socket
    if(g_socket.Initialize() == false)
    {
        exitcode = 1;
        goto lblexit;
    }
    
    ngx_init_setproctitle();//环境变量搬家
    
    //创建守护进程
    if(pConfig -> GetIntDefault("Daemon",0) == 1)
    {
        int cdaemonresult = ngx_daemon();
        if(cdaemonresult == -1)//fork()失败
        {
            exitcode = 1;
            goto lblexit;
        }
        if(cdaemonresult == 1) //原父进程
        {
            freeresource(); //守护进程创建成功正常退出，释放资源
            exitcode = 0;
            return exitcode;
        }
        g_daemonized = 1; //成功创建了守护进程，标记启用守护进程模式
    }



    ngx_master_process_cycle();

    /*测试代码
    for(;;)
    //for(int i = 0; i < 10;++i)
    {
        sleep(1); //休息1秒        
        printf("休息1秒\n");        

    }*/

lblexit:
    //(5)该释放的资源要释放掉
    ngx_log_stderr(0,"程序退出，再见！");
    freeresource();  //一系列的main返回前的释放动作函数
    //printf("程序退出，再见!\n");
    return exitcode;
}

void freeresource()
{
    //(1)对于因为设置可执行程序标题导致的环境变量分配的内存，我们应该释放
    if(gp_envmem)
    {
        delete []gp_envmem;
        gp_envmem = NULL;
    }

    //(2)关闭日志文件
    if(ngx_log.fd != STDERR_FILENO && ngx_log.fd != -1)  
    {        
        close(ngx_log.fd); //不用判断结果了
        ngx_log.fd = -1; //标记下，防止被再次close吧        
    }
}
