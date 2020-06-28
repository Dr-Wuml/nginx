#ifndef __NGX_MACRO_H__
#define __NGX_MACRO_H__

//宏定义位置

#define NGX_MAX_ERROR_STR 2048 //显示错误信息最大长度

//功能型函数，常用函数
#define ngx_cpymem(dst,src,n) ( ( (u_char *) memcpy(dst,src,n) ) + (n) ) //拷贝函数，并将返回值指向dst被赋值之后的位置
#define ngx_min(val1,val2) ( (val1 > val2)? (val2):(val1) )

//数字相关的定义
#define NGX_MAX_UINT32_VALUE (uint32_t) 0xffffffff   //最大的无符号32位的数，十进制是‭4294967295‬
#define NGX_INT64_LEN  (sizeof("-9223372036854775808") - 1)    //64位数最大长度

//日志等级相关,共九个等级0-8，级别依次减小
#define NGX_LOG_STDERR         0 //控制台错误，【stderr】最高级别日志,需要立刻提示，不往日志里写，直接在控制台提示
#define NGX_LOG_EMERG          1 //紧急【emerg】
#define NGX_LOG_ALERT          2 //警戒【alert】
#define NGX_LOG_CRIT           3 //严重【critical】较为严重的报错
#define NGX_LOG_ERR            4 //失败【ERROR】//常用级别日志
#define NGX_LOG_WARN           5 //警告【warn】//常用级别日志
#define NGX_LOG_NOTICE         6 //注意【notice】 
#define NGX_LOG_INFO           7 //信息【info】
#define NGX_LOG_DEBUG          8 //调试【debug】级别最低

#define NGX_LOG_PATH    "error.log" //日志存放目录和文件名 

//进程相关
#define NGX_PROCESS_MASTER     0  //master进程，管理进程
#define NGX_PROCESS_WORKER     1  //worker进程，工作进程

#endif  //!  __NGX_MACRO_H__
