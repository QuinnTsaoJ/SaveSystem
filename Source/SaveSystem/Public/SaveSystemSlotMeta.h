// 存档槽元数据结构
// SaveSystem Plugin

#pragma once

#include "CoreMinimal.h"
#include "SaveSystemSlotMeta.generated.h"

// 单个存档槽的元数据(每个存档文件固定一份,对应设计文档 3.4)
USTRUCT(BlueprintType)
struct FSaveSlotMetadata
{
	GENERATED_BODY()

	// 存档槽名(主键)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Slot")
	FString SlotName;

	// 创建时间
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Slot")
	FDateTime CreateTime = FDateTime(0);

	// 最后保存时间
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Slot")
	FDateTime LastSaveTime = FDateTime(0);

	// 引擎版本(兼容性检查)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Slot")
	FString EngineVersion;

	// 插件版本
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Slot")
	FString PluginVersion;

	// 累计游戏时长(秒)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Slot")
	float PlayTime = 0.f;

	// 关卡名(加载时校验)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Slot")
	FString LevelName;

	// 显示名(供 UI)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Slot")
	FText DisplayName;

	// 缩略图路径(可选,留空忽略)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Slot")
	FString ThumbnailPath;
};
