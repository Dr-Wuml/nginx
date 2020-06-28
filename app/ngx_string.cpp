#include <string.h>
#include <stdio.h>

void Rtrim(char *str)
{
    size_t len = 0;
    if(str == NULL)
    {
        return;
    }

    len = strlen(str);
    while(len > 0 && str[len -1] == ' ')//左边部分有空格
    {
        str[--len] = 0;
    }
    return;
}
void Ltrim(char *str)
{
    size_t len = 0;
    len = strlen(str);
    char *p_tmp = str;
    if((*p_tmp) != ' ') //不是空格可以直接返回
    {
        return;
    }
    while((*p_tmp) != '\0')
    {
        if((*p_tmp) == ' ')
        {
            p_tmp++;
        }
        else
        { 
            break;
        }
    }
    if( (*p_tmp) == '\0') //全是空格
    {
        *str = '\0';
        return;
    }
    char *p_tmp1 = str;
    while((*p_tmp) != '\0')
    {
        (*p_tmp1) = (*p_tmp);
        p_tmp++;
        p_tmp1++;
    }
    (*p_tmp1) = '\0';
    return;
}
