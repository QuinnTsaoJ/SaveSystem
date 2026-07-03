// 存档系统模块入口
// SaveSystem Plugin

#pragma once

#include "Modules/ModuleManager.h"

class FSaveSystemModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
