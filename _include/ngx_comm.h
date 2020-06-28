#ifndef __NGX_COMM_H__
#define __NGX_COMM_H__

#define _PKG_MAX_LENGTH       30000  //每个包的最大长度
            

//通信收包状态定义
#define _PKG_HD_INIT         0      //初始状态，准备接收包头
#define _PKG_HD_RECVING      1      //接收包头中，包头不完整，继续接收
#define _PKG_BD_INIT         2      //包头接收完毕，开始接收包体
#define _PKG_BD_RECVING      3      //接收包体中，继续接收
//#define _PKG_RV_FINISHED     4       //完整包收完

#define _DATA_BUFSIZE_       20     //先收包头，定义一个固定大小的数组专门用来收包头，这个数字大小一定要 >sizeof(COMM_PKG_HEADER)


//定义结构
#pragma pack (1)        //对齐方式

typedef struct _COMM_PKG_HEADER
{
	unsigned short pkgLen;           //报文总长度
	unsigned short msgCode;          //消息类型代码
	int            crc32;            //CRC32效验，为了防止收发数据中出现收到内容和发送内容不一致的情况，引入这个字段做一个基本的校验用	
}COMM_PKG_HEADER,*LPCOMM_PKG_HEADER;


#pragma pack()          //取消对齐     


#endif