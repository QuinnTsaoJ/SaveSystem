// 可保存组件实现
// SaveSystem Plugin

#include "SaveableComponent.h"
#include "ISaveableActor.h"
#include "SaveSystemBPLibrary.h"
#include "SaveSystemSaveGame.h"
#include "SaveSystemLog.h"
#include "GameFramework/Actor.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

USaveableComponent::USaveableComponent()
{
	// 组件无需 Tick(存档操作由 BPLibrary 主动触发)
	PrimaryComponentTick.bCanEverTick = false;
}

void USaveableComponent::OnRegister()
{
	Super::OnRegister();

	// 自动检测:运行时 Spawn 的 Actor 在世界已开始游戏后才注册
	// 地图原生 Actor 在关卡加载阶段注册,此时 HasBegunPlay() == false
	// 此判定无需游戏侧手动标记,符合"零侵入"承诺
	if (!bRuntimeSpawned && GetOwner())
	{
		UWorld* World = GetWorld();
		if (World && World->HasBegunPlay())
		{
			// 世界已 BeginPlay,说明本组件是运行时 Spawn 的 Actor 挂载的
			bRuntimeSpawned = true;
		}
	}

	// GUID 派生:
	// - 地图 Actor(bRuntimeSpawned==false):由路径派生并缓存(免持久化、免冲突)
	// - 运行时 Actor(bRuntimeSpawned==true):延迟到 GetSaveGUID 时用 NewGuid 生成(随存档持久化)
	if (!bRuntimeSpawned && GetOwner())
	{
		const FString PathName = GetOwner()->GetPathName();
		const uint32 H0 = GetTypeHash(PathName);
		const uint32 H1 = GetTypeHash(PathName + TEXT("|1"));
		const uint32 H2 = GetTypeHash(PathName + TEXT("|2"));
		const uint32 H3 = GetTypeHash(PathName + TEXT("|3"));
		CachedGUID = FGuid(H0, H1, H2, H3);
		bGUIDComputed = true;
	}

	// 注册到全局注册表(BPLibrary 维护的静态 TArray)
	USaveSystemBPLibrary::RegisterSaveableComponent(this);
}

void USaveableComponent::OnUnregister()
{
	// 从全局注册表注销
	USaveSystemBPLibrary::UnregisterSaveableComponent(this);

	Super::OnUnregister();
}

FGuid USaveableComponent::GetSaveGUID() const
{
	if (!bGUIDComputed)
	{
		EnsureRuntimeGUID();
	}
	return CachedGUID;
}

FGuid USaveableComponent::EnsureRuntimeGUID() const
{
	if (!bGUIDComputed)
	{
		// 运行时 Actor:生成 NewGuid 并缓存(随存档记录持久化)
		CachedGUID = FGuid::NewGuid();
		bGUIDComputed = true;
	}
	return CachedGUID;
}

FActorSaveState USaveableComponent::GatherActorState()
{
	FActorSaveState State;

	AActor* Owner = GetOwner();
	if (Owner)
	{
		// 默认采集 Transform
		State.Transform = Owner->GetActorTransform();
		State.bHidden = Owner->IsHidden();
	}

	// 收集自定义字段:走 ISaveableActor 接口(Actor 实现接口即可,BP/C++ 统一)
	if (Owner && Owner->GetClass()->ImplementsInterface(USaveableActor::StaticClass()))
	{
		ISaveableActor::Execute_GatherSaveFields(Owner, State.CustomFields);

		// 收集强类型数据载体,序列化为 bytes(避免 Instanced 跨会话失效)
		USaveSystemSaveGameData* Data = ISaveableActor::Execute_GatherActorData(Owner);
		if (Data)
		{
			State.ActorDataClassName = Data->GetClass()->GetPathName();
			FMemoryWriter Writer(State.ActorDataBytes);
			FObjectAndNameAsStringProxyArchive Ar(Writer, /*bInLoadIfFindFails=*/ false);
			Ar.ArIsSaveGame = true;
			Data->Serialize(Ar);
		}
	}

	return State;
}

void USaveableComponent::ApplyActorState(const FActorSaveState& State)
{
	AActor* Owner = GetOwner();
	if (Owner)
	{
		// 恢复 Transform
		Owner->SetActorTransform(State.Transform);
		// 恢复可见性(运行时用 SetActorHiddenInGame)
		Owner->SetActorHiddenInGame(State.bHidden);
	}

	// 恢复自定义字段:走 ISaveableActor 接口
	if (Owner && Owner->GetClass()->ImplementsInterface(USaveableActor::StaticClass()))
	{
		ISaveableActor::Execute_ApplySaveFields(Owner, State.CustomFields);

		// 从 bytes 反序列化重建强类型数据载体(若有)
		if (!State.ActorDataClassName.IsEmpty() && State.ActorDataBytes.Num() > 0)
		{
			UClass* DataClass = LoadObject<UClass>(nullptr, *State.ActorDataClassName);
			if (DataClass && DataClass->IsChildOf(USaveSystemSaveGameData::StaticClass()))
			{
				USaveSystemSaveGameData* Data = NewObject<USaveSystemSaveGameData>(GetTransientPackage(), DataClass);
				FMemoryReader Reader(State.ActorDataBytes);
				FObjectAndNameAsStringProxyArchive Ar(Reader, /*bInLoadIfFindFails=*/ true);
				Ar.ArIsSaveGame = true;
				Data->Serialize(Ar);
				ISaveableActor::Execute_ApplyActorData(Owner, Data);
			}
		}
	}
}
