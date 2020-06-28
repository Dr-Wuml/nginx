#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>     //errno
#include <sys/stat.h>
#include <fcntl.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"

int ngx_daemon()
{

    //创建守护进程的步骤：
    // 1.fork()一个子进程出来
    switch(fork())
    {
    case -1:
        ngx_log_error_core(NGX_LOG_EMERG, errno, "ngx_daemon()中fork()失败！！");
        return -1;
    case 0 : //子进程
        break;
    case 1: //父进程
        return 1;
    }
    
    ngx_parent = ngx_pid;
    ngx_pid = getpid();   //获取当前子进程的pid
    
    //2. 脱离终端
    if(setsid() == -1 )
    {
        ngx_log_error_core(NGX_LOG_EMERG,errno,"ngx_daemon()中setsid()失败！！！");
        return -1;
    }

    //3. 使用umask(0); 取消限制文件权限
    umask(0);
 
    //4. 以读写方式打开黑洞设备
    int fd = open("/dev/null",O_RDWR);

    if(fd == -1)
    {
        ngx_log_error_core(NGX_LOG_EMERG,errno,"ngx_daemon()中打开黑洞设备/dev/null失败  !!!");
        return -1;
    }
    if(dup2(fd,STDIN_FILENO) == -1) //先关闭STDIN_FILENO[已经打开的描述符，操作之前，先close]
    {
        ngx_log_error_core(NGX_LOG_EMERG,errno,"ngx_daemon()中dup2(STDIN_FILENO)失败！！！");
        return -1;
    }
    if(dup2(fd,STDOUT_FILENO) == -1)
    {
        ngx_log_error_core(NGX_LOG_EMERG,errno,"ngx_daemon()中dup2(STDOUT_FILENO)失败！！！");
        return -1;
    }
    if(fd > STDERR_FILENO) //此时fd =3
    {
        if(close(fd) == -1) //释放资源这样这个文件描述符就可以被复用
        {
            ngx_log_error_core(NGX_LOG_EMERG,errno,"ngx_daemon()中close(fd)失败！！！");
            return -1;
        }
    }
    return 0;
}
