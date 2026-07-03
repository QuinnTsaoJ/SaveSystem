// 存档系统模块入口实现
// SaveSystem Plugin

#include "SaveSystem.h"
#include "SaveSystemLog.h"

#define LOCTEXT_NAMESPACE "FSaveSystemModule"

// 日志分类定义(对应 DECLARE_LOG_CATEGORY_EXTERN)
DEFINE_LOG_CATEGORY(LogSaveSystem);

void FSaveSystemModule::StartupModule()
{
	// 模块加载后执行(当前无额外初始化)
}

void FSaveSystemModule::ShutdownModule()
{
	// 模块卸载前执行(当前无额外清理)
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSaveSystemModule, SaveSystem)
