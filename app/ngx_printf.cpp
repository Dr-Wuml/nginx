#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdarg.h>
#include<stdint.h>  //类型相关头文件

#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h"

static u_char * ngx_sprintf_num(u_char * buf,u_char *last,uint64_t ui64,u_char zero,uintptr_t hexadecimal,uintptr_t width);

/*****************************************************************************************************
        对于 nginx 自定义的数据结构进行标准格式化输出,就像 printf,vprintf 一样，
        该函数只不过相当于针对ngx_vslprintf()函数包装了一下，所以，直接研究ngx_vslprintf()即可
******************************************************************************************************/
u_char *ngx_slprintf(u_char *buf,u_char *last,const char *fmt,...)
{
    va_list args;
    u_char *p;
    va_start(args,fmt);
    p = ngx_vslprintf(buf,last,fmt,args);
    va_end(args);
    return p;
}

/*****************************************************************************************************
          对于 nginx 自定义的数据结构进行标准格式化输出,就像 printf,vprintf 一样，
          该函数只不过相当于针对ngx_vslprintf()函数包装了一下，所以，直接研究ngx_vslprintf()即可
******************************************************************************************************/
u_char *ngx_snprintf(u_char *buf,size_t max,const char *fmt,...)
{
    va_list args;
    u_char *p;

    va_start(args,fmt);
    p = ngx_vslprintf(buf,buf+max,fmt,args);
    va_end(args);
    return p;
}
/*****************************************************************************************************
    对于 nginx 自定义的数据结构进行标准格式化输出,就像 printf,vprintf 
    例如，给进来一个 "abc = %d",13   ,最终buf里得到的应该是   abc=13 这种结果
    buf：往这里放数据
    last：放的数据不要超过这里
    fmt：以这个为首的一系列可变参数
    支持的格式： %d【%Xd/%xd】:数字,    %s:字符串      %f：浮点,  %P：pid_t
    对于：ngx_log_stderr(0, "invalid option: \"%s\",%d", "testinfo",123);
    fmt = "invalid option: \"%s\",%d"
    args = "testinfo",123
*********************************************************************************************************/
u_char *ngx_vslprintf(u_char *buf, u_char *last,const char *fmt,va_list args)
{
    u_char zero;

    uintptr_t width,sign,hex,frac_width,scale,n;
    
    int64_t  i64;   //保存%d对应的可变参
    uint64_t ui64;  //保存%ud对应的可变参
    u_char   *p;    //保存%s对应的可变参
    double   f;     //保存%f对应的可变参
    uint64_t frac;  //%f可变参，根据%.2f等，取得小数部分的2位后的内容；

    while(*fmt && buf < last) ////每次处理一个字符，处理的是  "invalid option: \"%s\",%d" 中的字符
    {
        if(*fmt == '%')
        {
            /* 初始化变量begin*/
            zero = (u_char)( (*++fmt == '0') ? '0':' '); //判断填充字符
            width = 0;
            sign = 1; //1表示有符号数字，0表示无符号数字
            hex = 0 ;
            frac_width = 0;
            i64 = 0;
            ui64 = 0;
            /*初始化变量end*/

            while(*fmt >= '0' && *fmt <= '9') //保存整数宽度
            {
                width = width * 10 + (*fmt++ - '0');
            }

            //特殊字符处理
            for(;;)
            {
                switch(*fmt)
                {
                case 'u':      //无符号数值
                    sign = 0;  //0表示这是一个无符号数值
                    fmt++;    //往后走一个字符
                    continue;
                case 'X':      //表示十六进制，并且十六进制中的A-F使用大写
                    hex = 2;   //大写十六进制数
                    fmt++;
                    sign = 0;
                    continue;
                case 'x':      //表示十六进制，并且十六进制中的A-F使用小写
                    hex = 1;   //小写十六进制数
                    fmt++;
                    sign = 0;
                    continue;
                case '.':      //表示小数，后面必须有一个数字才能成立
                    fmt++;
                    while(*fmt >= '0' && *fmt <= '9')
                    {
                        frac_width = frac_width * 10 + (*fmt++ - '0' );
                    }
                    break;
                default:
                    break;
                }//end switch
                break;
            }//end for(;;)

            switch(*fmt)
            {
            case '%' : //两个 %%表示输出一个%
                *buf++ = '%';
                fmt++;
                continue;
            case 'd':  //%d表示输出的为整数，如前面出现u则表示为无符号整数
                if(sign) //如果为有符号整数
                {
                    i64 = (int64_t) va_arg(args,int); //va_arg():遍历可变参数，var_arg的第二个参数表示遍历的这个可变的参数的类型
                }
                else //无符号整数
                {
                    ui64 = (uint64_t) va_arg(args,unsigned int); //va_arg():遍历可变参数，var_arg的第二个参数表示遍历的这个可变的参数的类型
                }
                break;
            case 'p':
                ui64 = (uintptr_t) va_arg(args,void *);
                hex = 2;
                sign = 0;
                zero = '0';
                width = 2 * sizeof(void *);
                break;
            case 's':  //%s表示字符型，显示字符串
                p = va_arg(args,u_char *);
                while(*p && buf < last )
                {
                    *buf++ = *p++;
                }
                fmt++;
                continue;
            case 'P':  //转换一个pid_t类型
                i64 = (int64_t) va_arg(args,pid_t);
                sign = 1;
                break;
            case 'f':  //一般用于显示double类型的数据，显示小数部分要有.f类型
                f = va_arg(args,double);
                if(f < 0)
                {
                    *buf++ = '-';
                    f = -f ;
                }
                ui64 = (int64_t) f ; //整数部分放入ui64中
                frac = 0;
                if(frac_width) //有小数部分
                {
                    scale = 1;
                    for(n=frac_width;n;n--)
                    {
                        scale *= 10;//过大可能会溢出范围
                    }
                    frac = (uint64_t) ( (f-(double) ui64) *scale +0.5 ); //取出来小数，保留位数，四舍五入的方法

                    if(frac == scale)
                    {
                        ui64++;
                        frac = 0;
                    }
                }//end if(frac_width)
                buf = ngx_sprintf_num(buf,last,ui64,zero,0,width); //把整数部分放入buf中
                if(frac_width)
                {
                    if(buf < last)
                    {
                        *buf ++ = '.';
                    }
                    buf = ngx_sprintf_num(buf,last,frac,'0',0,frac_width); //把小数部分放入buf中
                }
                fmt++;
                continue;
            default:
                *buf++ = *fmt++;
                continue;
            }//end switch(*fmt)

            if(sign) //显示的是有符号数
            {
                if(i64 < 0)
                {
                    *buf++ = '-';
                    ui64 =(uint64_t) -i64 ; 
                }
                else
                {
                    ui64 =(uint64_t) i64 ;
                }   
            }//end if(sign)
            buf = ngx_sprintf_num(buf,last,ui64,zero,hex,width);
            fmt++;
        }
        else
        {
            *buf++ = *fmt++;
        }//end if(*fmt == '%d')...
    }//end while(*fmt && buf < last)...
        return buf;
}

/******************************************************************************************************
        以一个指定的宽度把一个数字显示在buf对应的内存中, 如果实际显示的数字位数 比指定的宽度要小 ,
        比如指定显示10位，而你实际要显示的只有“1234567”，那结果可能是会显示“   1234567”
        当然如果你不指定宽度【参数width=0】，则按实际宽度显示
        你给进来一个%Xd之类的，还能以十六进制数字格式显示出来
        buf：往这里放数据
        last：放的数据不要超过这里
        ui64：显示的数字         
        zero:显示内容时，格式字符%后边接的是否是个'0',如果是zero = '0'，否则zero = ' ' 【一般显示的数字位数不足要求的，则用这个字符填充】
        ，比如要显示10位，而实际只有7位，则后边填充3个这个字符；
        hexadecimal：是否显示成十六进制数字 0：不
        width:显示内容时，格式化字符%后接的如果是个数字比如%16，那么width=16，所以这个是希望显示的宽度值
        【如果实际显示的内容不够，则后头用0填充】
******************************************************************************************************/
static u_char *ngx_sprintf_num(u_char *buf, u_char *last, uint64_t ui64,u_char zero, uintptr_t hexadecimal, uintptr_t width)
{
    u_char *p,temp[NGX_INT64_LEN+1];
    size_t len;
    uint32_t ui32;

    static u_char hex[] = "0123456789abcdef"; //十六进制小写
    static u_char Hex[] = "0123456789ABCDEF"; //十六进制大写

    p = temp + NGX_INT64_LEN; //NGX_INT64_LEN = 20 ,p是指向数组元素最后一个位置
    if(hexadecimal == 0) //正常十进制
    {
        if(ui64 <= (uint64_t) NGX_MAX_UINT32_VALUE)
        {
            ui32 = (uint32_t) ui64; //可以保存下
            do
            {
                *--p = (u_char) (ui32 % 10 + '0');
            }
            while (ui32 /= 10);
        }
        else
        {
            do
            {
                *--p = (u_char)(ui64 % 10 + '0');
            } 
            while (ui64 /= 10);           
        }
        
    }
    else if(hexadecimal == 1) //十六进制小写
    {
        do
        {
            *--p = hex[(uint32_t)(ui64 & 0xf)];
        } 
        while (ui64 >>= 4); 
    }
    else //十六进制大写
    {
        do
        {
            *--p = Hex[(uint32_t)(ui64 & 0xf)];
        } 
        while (ui64 >>= 4);
        
    }
    len = (temp + NGX_INT64_LEN) -p;

    while (len++ < width &&buf < last)
    {
        *buf++ = zero;
    }

    len = (temp + NGX_INT64_LEN) -p;
    if((buf + len) >= last)
    {
        len = last - buf;
    }
    return ngx_cpymem(buf,p,len);
       
}//end ngx_sprintf_num
