//�ڴ�������
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ngx_c_memory.h"

CMemory *CMemory::m_instance = NULL;

//�����ڴ�
//memCount��������ֽڴ�С
//ifmemset���Ƿ�Ҫ�ѷ�����ڴ��ʼ��Ϊ0��	
void *CMemory::AllocMemory(int memCount,bool ifmemset)
{
	void *tmpData = (void *)new char[memCount];
	if(ifmemset)
	{
		memset(tmpData,0,memCount);
	}
	return tmpData;
}

//����������
//delete [] point;  //��ôɾ���������־��棺warning: deleting ��void*�� is undefined [-Wdelete-incomplete]
void CMemory::FreeMemory(void *point)
{
	delete [] ((char *)point);//new��ʱ����char *������Ū��char *�����������
}
