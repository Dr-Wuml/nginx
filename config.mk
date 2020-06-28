
#������Ŀ����ĸ�Ŀ¼��ͨ��export��ĳ��ĳ����������Ϊȫ�ֱ���[�����ļ�������]�������ȡ��ǰ����ļ������ڵ�·��Ϊ��Ŀ¼
export BUILD_ROOT = $(shell pwd)

#����ͷ�ļ���·������
export INCLUDE_PATH = $(BUILD_ROOT)/_include

#��������Ҫ�����Ŀ¼
BUILD_DIR = $(BUILD_ROOT)/signal/ \
            $(BUILD_ROOT)/proc/ \
            $(BUILD_ROOT)/net/ \
            $(BUILD_ROOT)/misc/ \
            $(BUILD_ROOT)/logic/ \
            $(BUILD_ROOT)/app/

#����ʱ�Ƿ����ɵ�����Ϣ��GNU�������������ø���Ϣ
#���������������������valgrind
export DEBUG = true
