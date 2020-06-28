//信号相关函数处理
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdint.h>
#include<signal.h>
#include<errno.h>
#include <sys/wait.h>  //waitpid

#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h"

//信号处理结构
typedef struct
{
    int        signo;    //信号对应的编号
    const char *signame; //信号的中文名
    void (*handler)(int signo,siginfo_t *siginfo,void *ucontext); //函数指针，siginfo_t：系统定义的结构
}ngx_signal_t;

static void ngx_signal_handler(int signo,siginfo_t *siginfo,void *ucontext); //static静态函数，仅在当前文件中可看
static void ngx_process_get_status(void);                                      //获取子进程的结束状态，防止单独kill子进程时子进程变成僵尸进程


ngx_signal_t signals[] = {
    //signo     signame             handler
    {SIGHUP,    "SIGHUP",           ngx_signal_handler}, //终端断开标识
    {SIGINT,    "SIGINT",           ngx_signal_handler}, //标识2
    {SIGTERM,   "SIGTERM",          ngx_signal_handler}, //标识15
    {SIGCHLD,   "SIGCHLD",          ngx_signal_handler}, //子进程退出时，父进程会受到这个信号，标识17
    {SIGQUIT,   "SIGQUIT",          ngx_signal_handler}, //标识3
    {SIGIO,     "SIGIO",            ngx_signal_handler}, //标识一个异步I/O事件，通信异步I/O信号
    {SIGSYS,    "SIGSYS, SIG_IN",   NULL}, //忽略这个信号，SIGSYS：收到了一个无效系统调用，不忽略，进程会被操作系统杀死，--标识31，所以handler设置为NULL，忽略这个信号，请求操作系统不执行缺省信号处理动作
    {0,         NULL,               NULL}  //用0来作为特殊标记
};

int ngx_init_signals()
{
    ngx_signal_t      *sig; //指向自定义结构
    struct sigaction  sa; //sigaction：系统定义的信号结构，调用igaction()函数时要用到这个结构
    //将signo ==0作为一个标记，因为信号的编号都不为0；
    for(sig = signals; sig->signo != 0; sig++)
    {
        memset(&sa,0,sizeof(struct sigaction));
        if(sig->handler)//信号处理函数不为空，定义信号处理函数
        {
            sa.sa_sigaction = sig->handler;//sa_sigaction：信号处理程序(函数)，sa_sigaction是函数指针，是系统定义的结构sigaction中的一个成员（函数指针成员）；
            sa.sa_flags = SA_SIGINFO;
        }
        else
        {
            sa.sa_handler = SIG_IGN;//sa_handler:这个标记SIG_IGN给到sa_handler成员，表示忽略信号的处理程序
        }
        //处理某个信号时不希望收到另外的信号，可以用诸如sigaddset(&sa.sa_mask,SIGUSR2);针对信号为SIGUSR1时做处理
        //这里.sa_mask是个信号集（描述信号的集合）表示要阻塞的信号，sigemptyset()是把信号集中的所有信号清0，不阻塞任何信号；
        sigemptyset(&sa.sa_mask);
        //参数1：要操作的信号
        //参数2：信号处理函数以及执行信号处理函数时候要屏蔽的信号等内容
        //参数3：返回以往的对信号的处理方式【同sigprocmask()函数边的第三个参数】，跟参数2同一个类型，不需要这个东西直接设置为NULL；
        if(sigaction(sig->signo,&sa,NULL) == -1)
        {
            ngx_log_error_core(NGX_LOG_EMERG, errno, "sigaction(%s) failed", sig->signame);
            return -1;
        }
        else
        {
            //ngx_log_error_core(NGX_LOG_EMERG,errno, "sigaction(%s) successed !", sig->signame);
            //ngx_log_stderr(0,"sigaction(%s) successed !",sig->signame);
        }  
    }//end for
    return 0;//成功
}

//信号处理函数
static void ngx_signal_handler(int signo,siginfo_t *siginfo,void *ucontext)
{
    //printf("信号来了\n");
    ngx_signal_t     *sig;
    char             *action; //一个字符串，用于记录一个动作写进日志

    for(sig = signals; sig->signo !=0; sig++)
    { 
        if(sig->signo == signo) //找到对应信号
        {
            break;
        }
    }

    action = (char *)""; //目前无动作
    if(ngx_process == NGX_PROCESS_MASTER) //master进程，管理进程，处理信号一般比较多
    {
        switch(signo)
        {
        case SIGCHLD:    //子进程退出信号
            ngx_reap = 1;//标记子进程状态变化，日后master主进程的for(;;)循环中可能会用到这个变量【比如重新产生一个子进程】
            break;

        default:
            break;
        }
    }
    else if (ngx_process == NGX_PROCESS_WORKER)
    {
        //子进程处理
    }
    else
    {
        //非worker进程、非master进程
    }

    if(siginfo && siginfo->si_pid)
    {
        ngx_log_error_core(NGX_LOG_NOTICE,0,"signal %d (%s) received from %P%s", signo, sig->signame, siginfo->si_pid, action);
    }
    else
    {
        ngx_log_error_core(NGX_LOG_NOTICE,0,"signal %d (%s) received %s",signo, sig->signame, action);//没有发送该信号的进程id，所以不显示发送该信号的进程id
    }

    //待扩展

    //子进程状态有变化，通常是意外退出
    if(signo == SIGCHLD)
    {
        ngx_process_get_status();
    }
    return;
}

static void ngx_process_get_status(void)
{
    pid_t pid;
    int   status;
    int   err;
    int   one = 0;//标记信号正常处理过一次

    for(;;)
    {
        //waitpid，有人也用wait,但老师要求大家掌握和使用waitpid即可；这个waitpid说白了获取子进程的终止状态，这样，子进程就不会成为僵尸进程了；
        //第一次waitpid返回一个> 0值，表示成功，后边显示 2019/01/14 21:43:38 [alert] 3375: pid = 3377 exited on signal 9【SIGKILL】
        //第二次再循环回来，再次调用waitpid会返回一个0，表示子进程还没结束，然后这里有return来退出

        //第一个参数为-1，表示等待任何子进程，
        //第二个参数：保存子进程的状态信息(大家如果想详细了解，可以百度一下)。
        //第三个参数：提供额外选项，WNOHANG表示不要阻塞，让这个waitpid()立即返回   
        pid = waitpid(-1, &status, WNOHANG);

        if(pid == 0) //子进程没结束，会立即返回这个数字，但这里应该不是这个数字【因为一般是子进程退出时会执行到这个函数】
        {
            return;
        } 
        if(pid == -1) //这表示这个waitpid调用有错误，有错误也理解返回出去，我们管不了这么多
        {
            err = errno;
            if(err == EINTR)
            {
                continue;
            }

            if(err == ECHILD && one)
            {
                return;
            }

            if(err == ECHILD)
            {
                ngx_log_error_core(NGX_LOG_INFO,err,"waitpid() failed!");
                return;
            }

            ngx_log_error_core(NGX_LOG_ALERT,err,"waitpid() failed!");
            return;
        }//end if(pid == -1) 

        //走到这里，表示  成功【返回进程id】 ，这里根据官方写法，打印一些日志来记录子进程的退出
        one = 1;  //标记waitpid()返回了正常的返回值
        if(WTERMSIG(status))  //获取使子进程终止的信号编号
        {
            ngx_log_error_core(NGX_LOG_ALERT,0,"pid = %P exited on signal %d!",pid,WTERMSIG(status)); //获取使子进程终止的信号编号
        }
        else
        {
            ngx_log_error_core(NGX_LOG_NOTICE,0,"pid = %P exited with code %d!",pid,WEXITSTATUS(status)); //WEXITSTATUS()获取子进程传递给exit或者_exit参数的低八位
        }
    }//end for
    return;
}
