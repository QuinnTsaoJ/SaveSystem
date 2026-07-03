// Actor 状态回调接口(C++ 与 Blueprint 统一通道)
// Actor 实现本接口即可提供自定义存档字段/恢复逻辑:
//   - C++ Actor:重写 GatherSaveFields_Implementation / ApplySaveFields_Implementation
//   - Blueprint Actor:实现本接口后,在事件图 Override GatherSaveFields/ApplySaveFields 事件
// SaveableComponent 通过 Execute_ 宏统一调用,无需关心实现侧是 C++ 还是 BP
// SaveSystem Plugin

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SaveSystemSaveGame.h"  // USaveSystemSaveGameData 完整定义(强类型载体返回值需要)
#include "ISaveableActor.generated.h"

UINTERFACE(BlueprintType)
class USaveableActor : public UInterface
{
	GENERATED_BODY()
};

class ISaveableActor
{
	GENERATED_BODY()

public:
	// === 简单标量通道(字符串键值对) ===
	// 适合血量/等级/开关等简单值,值需自行转为字符串

	// 保存时由插件调用:Actor 收集要持久化的自定义字段
	// 默认空实现:不提供自定义字段,插件仅保存 Transform
	UFUNCTION(BlueprintNativeEvent, Category="SaveSystem|Saveable")
	void GatherSaveFields(TMap<FString, FString>& OutFields);

	// 加载时由插件调用:Actor 从字段恢复状态
	UFUNCTION(BlueprintNativeEvent, Category="SaveSystem|Saveable")
	void ApplySaveFields(const TMap<FString, FString>& Fields);

	// === 复杂数据通道(强类型载体) ===
	// 适合背包/掉落表等复杂结构,无需转字符串,UE 原生反射自动序列化
	// 用法:Actor 创建 USaveSystemSaveGameData 子类实例,填入强类型字段后返回

	// 保存时由插件调用:Actor 返回承载复杂状态的数据载体子对象
	// 默认返回 nullptr:无强类型数据(仅用字符串字段或纯 Transform)
	UFUNCTION(BlueprintNativeEvent, Category="SaveSystem|Saveable")
	USaveSystemSaveGameData* GatherActorData();

	// 加载时由插件调用:Actor 从数据载体恢复复杂状态
	UFUNCTION(BlueprintNativeEvent, Category="SaveSystem|Saveable")
	void ApplyActorData(USaveSystemSaveGameData* Data);
};
