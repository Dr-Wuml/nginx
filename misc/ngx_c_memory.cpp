//内存分配相关
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ngx_c_memory.h"

CMemory *CMemory::m_instance = NULL;

//分配内存
//memCount：分配的字节大小
//ifmemset：是否要把分配的内存初始化为0；	
void *CMemory::AllocMemory(int memCount,bool ifmemset)
{
	void *tmpData = (void *)new char[memCount];
	if(ifmemset)
	{
		memset(tmpData,0,memCount);
	}
	return tmpData;
}

//错误样例：
//delete [] point;  //这么删除编译会出现警告：warning: deleting ‘void*’ is undefined [-Wdelete-incomplete]
void CMemory::FreeMemory(void *point)
{
	delete [] ((char *)point);//new的时候是char *，这里弄回char *，以免出警告
}
