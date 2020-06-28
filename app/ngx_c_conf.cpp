#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<vector>
#include "ngx_func.h"
#include "ngx_c_conf.h"

CConfig *CConfig::m_instance = NULL;

CConfig::CConfig()
{

}

CConfig::~CConfig()
{
    std::vector<LPCConfItem>::iterator pos;
    for(pos = m_ConfigItemList.begin(); pos != m_ConfigItemList.end(); pos++)
    {
        delete (*pos);
    } 
    m_ConfigItemList.clear();
    return;
}

bool CConfig::Load(const char *pconfName)
{
    FILE *fp;
    fp = fopen(pconfName,"r");
    if(fp == NULL) //打开失败
    {
        return false;
    }

    char Linebuf[501];

    while(!feof(fp)) //检查文件是否结束标志 0 1,循环读取每一行内容
    {
        if(fgets(Linebuf,500,fp) == NULL) 
        {
            continue;
        }
        if(Linebuf[0] == 0)
        {
            continue;
        }
        if(*Linebuf == ';' || *Linebuf == '#' || *Linebuf == ' ' || *Linebuf == '\n' || *Linebuf == '\t')
        {
            continue;
        }
lblprocstring:
        if(strlen(Linebuf) > 0)
        {
            if(Linebuf[strlen(Linebuf)-1] == 10 || Linebuf[strlen(Linebuf)-1] == 13 || Linebuf[strlen(Linebuf)-1] == 32)
            {
                Linebuf[strlen(Linebuf)-1] = '\0';
                goto lblprocstring;
            }
        }

        if(Linebuf[0] == 0)
        {
            continue;
        }
        if(Linebuf[0] == '[')
        {
            continue;
        }

        char *p_tmp =strchr(Linebuf,'=');
        if(p_tmp != NULL)
        {
            LPCConfItem p_confitem = new CConfItem;
            memset(p_confitem,0,sizeof(CConfItem));
            strncpy(p_confitem->ItemName,Linebuf,(int)(p_tmp-Linebuf));
            strcpy(p_confitem->ItemContent,p_tmp+1);
            Rtrim(p_confitem->ItemName);
            Ltrim(p_confitem->ItemName);
            Rtrim(p_confitem->ItemContent);
            Ltrim(p_confitem->ItemContent);
            m_ConfigItemList.push_back(p_confitem);
        }

    } //end while
    fclose(fp);
    return true;
}//end Load

const char *CConfig::Getstring(const char *p_itemname)
{
	std::vector<LPCConfItem>::iterator pos;	
	for(pos = m_ConfigItemList.begin(); pos != m_ConfigItemList.end(); ++pos)
	{	
		if(strcasecmp( (*pos)->ItemName,p_itemname) == 0)
			return (*pos)->ItemContent;
	}
	return NULL;
}

int CConfig::GetIntDefault(const char *p_itemname,const int def)
{
	std::vector<LPCConfItem>::iterator pos;	
	for(pos = m_ConfigItemList.begin(); pos !=m_ConfigItemList.end(); ++pos)
	{	
		if(strcasecmp( (*pos)->ItemName,p_itemname) == 0)
			return atoi((*pos)->ItemContent);
	}
	return def;
}
