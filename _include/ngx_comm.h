#ifndef __NGX_COMM_H__
#define __NGX_COMM_H__

#define _PKG_MAX_LENGTH       30000  //ÿ��������󳤶�
            

//ͨ���հ�״̬����
#define _PKG_HD_INIT         0      //��ʼ״̬��׼�����հ�ͷ
#define _PKG_HD_RECVING      1      //���հ�ͷ�У���ͷ����������������
#define _PKG_BD_INIT         2      //��ͷ������ϣ���ʼ���հ���
#define _PKG_BD_RECVING      3      //���հ����У���������
//#define _PKG_RV_FINISHED     4       //����������

#define _DATA_BUFSIZE_       20     //���հ�ͷ������һ���̶���С������ר�������հ�ͷ��������ִ�Сһ��Ҫ >sizeof(COMM_PKG_HEADER)


//����ṹ
#pragma pack (1)        //���뷽ʽ

typedef struct _COMM_PKG_HEADER
{
	unsigned short pkgLen;           //�����ܳ���
	unsigned short msgCode;          //��Ϣ���ʹ���
	int            crc32;            //CRC32Ч�飬Ϊ�˷�ֹ�շ������г����յ����ݺͷ������ݲ�һ�µ��������������ֶ���һ��������У����	
}COMM_PKG_HEADER,*LPCOMM_PKG_HEADER;


#pragma pack()          //ȡ������     


#endif