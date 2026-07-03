// 领域数据注入/取出接口
// 领域系统(如 Quest)实现本接口,通过 USaveSystemBPLibrary::RegisterSaveClient 注册
// 保存时插件调 GatherSaveData 收集领域数据;加载时调 ApplySaveData 注入领域数据
// 与 USaveSystemSaveGameData 配合,保持领域数据强类型,与现有 QuestSaveData.h 解耦
// SaveSystem Plugin

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISaveSystemClient.generated.h"

class USaveSystemSaveGameData;

UINTERFACE(BlueprintType)
class USaveSystemClient : public UInterface
{
	GENERATED_BODY()
};

class ISaveSystemClient
{
	GENERATED_BODY()

public:
	// 保存时:返回本客户端的领域数据子对象(插件写入 ClientData[ClientName])
	// 默认返回 nullptr(不提供领域数据)
	UFUNCTION(BlueprintNativeEvent, Category="SaveSystem|Client")
	USaveSystemSaveGameData* GatherSaveData();

	// 加载时:插件从 ClientData 取出本客户端的子对象,交给领域系统恢复
	// 默认空实现
	UFUNCTION(BlueprintNativeEvent, Category="SaveSystem|Client")
	void ApplySaveData(USaveSystemSaveGameData* Data);

	// 客户端唯一标识(用作 ClientData 的键,如 "Quest")
	// 默认返回 NAME_None(调用方应在注册前确保 ClientName 有效)
	UFUNCTION(BlueprintNativeEvent, Category="SaveSystem|Client")
	FName GetClientName() const;
};
