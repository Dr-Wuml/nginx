#ifndef __NGX_C_CONF_H__
#define __NGX_C_CONF_H__

#include "ngx_global.h"  //通用参数放在

#include<vector>
//配置文件类
class CConfig 
{
private:
    CConfig();
public:
    ~CConfig();
private:
    static CConfig *m_instance;
public:
    static CConfig *GetInstance()//创建实例
    {
        if(NULL == m_instance)
        {
            //加锁位置Lock();
            if(NULL == m_instance)
            {
                m_instance =new CConfig();
                static CResysling Cr;
            }
            //解锁位置UnLock();
        }
        return m_instance;
    }
    class CResysling //资源回收类
    {
    public:
        ~CResysling() //析构
        {
            if(CConfig::m_instance)
            {
                delete CConfig::m_instance;
                CConfig::m_instance = NULL;
            }
        }
    };
public:
    bool Load(const char* pConfName);//加载配置文件
    const char *Getstring(const char *p_itemname);//加载配置value为字符型的
    int GetIntDefault(const char *p_itemname,const int def);//加载配置value为数值的
public:
    std::vector<LPCConfItem> m_ConfigItemList;//存储配置信息的列表
};

#endif
