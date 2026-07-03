// 可保存组件 —— 挂任何 Actor 即参与存档(零侵入,无需改父类)
// 职责:自动注册/注销到全局注册表;提供 GUID(方案 A);提供 Save/Load 回调入口
// SaveSystem Plugin

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SaveSystemTypes.h"
#include "SaveableComponent.generated.h"

UCLASS(ClassGroup=SaveSystem, meta=(BlueprintSpawnableComponent))
class SAVESYSTEM_API USaveableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USaveableComponent();

	// === ActorComponent 生命周期 ===
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	// === GUID(方案 A) ===
	// 地图 Actor:由 GetOwner()->GetPathName() 派生(OnRegister 时计算并缓存)
	// 运行时 Actor:首次需求时 NewGuid 生成,随存档记录持久化
	FGuid GetSaveGUID() const;

	// 运行时 Spawn 标记(BPLibrary 在 Load 流程 Spawn 恢复时设为 true)
	bool IsRuntimeSpawned() const { return bRuntimeSpawned; }
	void SetRuntimeSpawned(bool bInRuntime) { bRuntimeSpawned = bInRuntime; }

	// === Actor 状态回调入口(BPLibrary 调用) ===
	// 收集本 Actor 需持久化的状态(走 ISaveableActor 接口)
	FActorSaveState GatherActorState();
	// 将状态写回本 Actor(走 ISaveableActor 接口)
	void ApplyActorState(const FActorSaveState& State);

private:
	// 运行时 Spawn 标记:默认 false(地图 Actor);加载流程 Spawn 出的 Actor 设为 true
	UPROPERTY(Transient)
	bool bRuntimeSpawned = false;

	// 缓存的 GUID(地图 Actor 在 OnRegister 时由路径派生并缓存;运行时 Actor 用 NewGuid)
	UPROPERTY(Transient)
	mutable FGuid CachedGUID;

	// GUID 是否已计算(区分"未计算"与"全零 GUID")
	UPROPERTY(Transient)
	mutable bool bGUIDComputed = false;

	// 为运行时 Actor 生成并缓存一个 NewGuid(仅运行时 Actor 调用)
	FGuid EnsureRuntimeGUID() const;
};
