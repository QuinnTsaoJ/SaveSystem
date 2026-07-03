// 存档系统入口实现
// SaveSystem Plugin

#include "SaveSystemBPLibrary.h"
#include "SaveableComponent.h"
#include "ISaveSystemClient.h"
#include "SaveSystemSaveGame.h"
#include "SaveSystemLog.h"

#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

#define LOCTEXT_NAMESPACE "SaveSystem"

// === 全局静态状态 ===

// SaveableComponent 全局注册表(注册时 Add,注销时 RemoveSingle,O(1) 移除)
// 用 TWeakObjectPtr:对象被 GC 回收后弱引用自动失效,遍历时 IsValid() 安全返回 false
// 避免"注册后对象销毁但未反注册"导致的悬空指针崩溃
static TArray<TWeakObjectPtr<USaveableComponent>>& GetSaveableRegistry()
{
	static TArray<TWeakObjectPtr<USaveableComponent>> Registry;
	return Registry;
}

// 已注册的领域客户端列表(同上,用弱引用防止悬空)
static TArray<TWeakObjectPtr<UObject>>& GetClientRegistry()
{
	static TArray<TWeakObjectPtr<UObject>> Registry;
	return Registry;
}

// 清理注册表中已失效的弱引用条目(PIE 多轮启动后旧对象残留)
// 在每次遍历后调用,保持注册表干净,避免无限增长
static void CleanSaveableRegistry()
{
	GetSaveableRegistry().RemoveAll([](const TWeakObjectPtr<USaveableComponent>& Weak)
	{
		return !Weak.IsValid();
	});
}

static void CleanClientRegistry()
{
	GetClientRegistry().RemoveAll([](const TWeakObjectPtr<UObject>& Weak)
	{
		return !Weak.IsValid();
	});
}

// === 委托静态实例(生命周期与进程一致,存档操作频率低,无泄漏风险) ===

FOnSaveComplete& USaveSystemBPLibrary::OnSaveComplete()
{
	static FOnSaveComplete Delegate;
	return Delegate;
}
FOnLoadComplete& USaveSystemBPLibrary::OnLoadComplete()
{
	static FOnLoadComplete Delegate;
	return Delegate;
}
FOnVersionMismatch& USaveSystemBPLibrary::OnVersionMismatch()
{
	static FOnVersionMismatch Delegate;
	return Delegate;
}

// === SaveableComponent 注册/注销(组件生命周期回调) ===

void USaveSystemBPLibrary::RegisterSaveableComponent(USaveableComponent* Comp)
{
	if (Comp && !GetSaveableRegistry().Contains(Comp))
	{
		GetSaveableRegistry().Add(Comp);
	}
}
void USaveSystemBPLibrary::UnregisterSaveableComponent(USaveableComponent* Comp)
{
	GetSaveableRegistry().RemoveSingle(Comp);
}

// === 客户端注册 ===

void USaveSystemBPLibrary::RegisterSaveClient(UObject* Client)
{
	if (Client && Client->GetClass()->ImplementsInterface(USaveSystemClient::StaticClass())
		&& !GetClientRegistry().Contains(Client))
	{
		GetClientRegistry().Add(Client);
	}
}
void USaveSystemBPLibrary::UnregisterSaveClient(UObject* Client)
{
	GetClientRegistry().RemoveSingle(Client);
}

// === 错误码转文本 ===

FText USaveSystemBPLibrary::ErrorToText(ESaveSystemError Error)
{
	switch (Error)
	{
	case ESaveSystemError::None:                return LOCTEXT("Err_None", "成功");
	case ESaveSystemError::InvalidWorld:        return LOCTEXT("Err_InvalidWorld", "WorldContext 无效");
	case ESaveSystemError::InvalidSlotName:     return LOCTEXT("Err_InvalidSlotName", "槽名为空或非法");
	case ESaveSystemError::SlotNotFound:        return LOCTEXT("Err_SlotNotFound", "槽不存在");
	case ESaveSystemError::SlotAlreadyExists:   return LOCTEXT("Err_SlotAlreadyExists", "槽已存在且未允许覆盖");
	case ESaveSystemError::WriteFailed:         return LOCTEXT("Err_WriteFailed", "磁盘写入失败(权限/空间)");
	case ESaveSystemError::ReadFailed:          return LOCTEXT("Err_ReadFailed", "读取失败(文件损坏)");
	case ESaveSystemError::VersionMismatch:     return LOCTEXT("Err_VersionMismatch", "版本不兼容");
	case ESaveSystemError::LevelMismatch:       return LOCTEXT("Err_LevelMismatch", "关卡不匹配(警告级)");
	case ESaveSystemError::NoSaveableComponents:return LOCTEXT("Err_NoSaveable", "无可保存对象");
	default:                                    return LOCTEXT("Err_Unknown", "未知错误");
	}
}

// === SaveGame ===

bool USaveSystemBPLibrary::SaveGame(UObject* WorldContextObject, const FString& SlotName,
	bool bOverride, FText& OutErrorMessage)
{
	// 前置检查:World
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World)
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::InvalidWorld);
		UE_LOG(LogSaveSystem, Error, TEXT("SaveGame 失败:WorldContext 无效"));
		OnSaveComplete().Broadcast(SlotName, false);
		return false;
	}

	// 前置检查:SlotName
	if (SlotName.IsEmpty())
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::InvalidSlotName);
		UE_LOG(LogSaveSystem, Error, TEXT("SaveGame 失败:槽名为空"));
		OnSaveComplete().Broadcast(SlotName, false);
		return false;
	}

	// 覆盖检查
	const bool bExists = UGameplayStatics::DoesSaveGameExist(SlotName, 0);
	if (bExists && !bOverride)
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::SlotAlreadyExists);
		UE_LOG(LogSaveSystem, Error, TEXT("SaveGame 失败:槽[%s]已存在且未允许覆盖"), *SlotName);
		OnSaveComplete().Broadcast(SlotName, false);
		return false;
	}

	// 取/建 SaveGame 对象(已存在则复用其元数据,保留 CreateTime)
	USaveSystemSaveGame* SaveGame = nullptr;
	FDateTime OriginalCreateTime = FDateTime::UtcNow();
	if (bExists)
	{
		USaveGame* LoadedRaw = UGameplayStatics::LoadGameFromSlot(SlotName, 0);
		SaveGame = Cast<USaveSystemSaveGame>(LoadedRaw);
		if (SaveGame)
		{
			OriginalCreateTime = SaveGame->SlotMeta.CreateTime;
		}
	}
	if (!SaveGame)
	{
		SaveGame = NewObject<USaveSystemSaveGame>();
		OriginalCreateTime = FDateTime::UtcNow();
	}

	// 清空记录数组(整体替换,复用底层分配)
	SaveGame->RuntimeActorRecords.Reset();
	SaveGame->LevelActorRecords.Reset();
	SaveGame->ClientData.Reset();

	// 更新元数据
	SaveGame->SlotMeta.SlotName = SlotName;
	SaveGame->SlotMeta.CreateTime = OriginalCreateTime;
	SaveGame->SlotMeta.LastSaveTime = FDateTime::UtcNow();
	SaveGame->SlotMeta.EngineVersion = FSaveSystemVersion::CurrentEngineVersion;
	SaveGame->SlotMeta.PluginVersion = FString::FromInt(FSaveSystemVersion::CurrentPluginVersion);
	SaveGame->SlotMeta.LevelName = World->GetName();
	// PlayTime 暂不累加(游戏层可自行维护并在此扩展)
	SaveGame->SlotMeta.PlayTime = 0.f;

	// 收集 Actor(遍历全局注册表,按 bRuntimeSpawned 分类)
	int32 CollectedCount = 0;
	for (TWeakObjectPtr<USaveableComponent> WeakComp : GetSaveableRegistry())
	{
		// 跳过已失效对象(对象销毁后弱引用自动失效),并顺手清理
		if (!WeakComp.IsValid())
		{
			continue;
		}
		USaveableComponent* Comp = WeakComp.Get();
		if (!Comp || !Comp->GetOwner())
		{
			continue;
		}
		// 跳过不属于当前 World 的组件(多关卡/多 PIE 场景)
		if (Comp->GetWorld() != World)
		{
			continue;
		}

		const FGuid Guid = Comp->GetSaveGUID();
		FActorSaveState State = Comp->GatherActorState();

		// 强类型数据载体序列化为 bytes(与 ClientData 同机制,避免 Instanced 跨会话失效)
		// GatherActorState 里已通过 ISaveableActor::Execute_GatherActorData 拿到子对象
		// 但 GatherActorState 返回的 FActorSaveState 里现在是 ActorDataBytes/ActorDataClassName
		// 实际序列化在 SaveableComponent::GatherActorState 里完成,这里无需额外处理

		if (Comp->IsRuntimeSpawned())
		{
			FRuntimeActorRecord& Rec = SaveGame->RuntimeActorRecords.AddDefaulted_GetRef();
			Rec.GUID = Guid;
			Rec.ActorClass = Comp->GetOwner()->GetClass();
			Rec.Transform = State.Transform;
			Rec.State = MoveTemp(State);
			Rec.LevelName = FName(World->GetName());
		}
		else
		{
			FLevelActorRecord& Rec = SaveGame->LevelActorRecords.AddDefaulted_GetRef();
			Rec.GUID = Guid;
			Rec.State = MoveTemp(State);
		}
		++CollectedCount;
	}

	// 清理失效条目(PIE 多轮启动后旧对象残留)
	CleanSaveableRegistry();

	// 无可保存对象记警告但不阻断(允许空存档)
	if (CollectedCount == 0)
	{
		UE_LOG(LogSaveSystem, Warning, TEXT("SaveGame:当前 World 无可保存对象,将写入空存档"));
	}

	// 收集领域数据(遍历已注册 ISaveSystemClient)
	for (TWeakObjectPtr<UObject> WeakClient : GetClientRegistry())
	{
		// 跳过已失效对象(对象销毁后弱引用自动失效)
		if (!WeakClient.IsValid())
		{
			continue;
		}
		UObject* Client = WeakClient.Get();
		const FName ClientName = ISaveSystemClient::Execute_GetClientName(Client);
		if (ClientName == NAME_None)
		{
			continue;
		}
		USaveSystemSaveGameData* Data = ISaveSystemClient::Execute_GatherSaveData(Client);
		if (Data)
		{
			// 把子对象序列化为 bytes 存入 ClientData
			FClientDataEntry& Entry = SaveGame->ClientData.Add(ClientName);
			Entry.ClassName = Data->GetClass()->GetPathName();

			// 用 FMemoryWriter + FObjectAndNameAsStringProxyArchive 序列化子对象
			// 该代理 Archive 能正确处理子对象引用与类路径,跨会话可靠
			Entry.SerializedData.Reset();
			FMemoryWriter Writer(Entry.SerializedData);
			FObjectAndNameAsStringProxyArchive Ar(Writer, /*bInLoadIfFindFails=*/ false);
			Ar.ArIsSaveGame = true;
			Data->Serialize(Ar);
		}
	}

	// 清理失效条目
	CleanClientRegistry();

	// 序列化到磁盘(复用 UE 原生)
	const bool bSaved = UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, 0);
	if (!bSaved)
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::WriteFailed);
		UE_LOG(LogSaveSystem, Error, TEXT("SaveGame 失败:磁盘写入失败,槽[%s]"), *SlotName);
		OnSaveComplete().Broadcast(SlotName, false);
		return false;
	}

	UE_LOG(LogSaveSystem, Log, TEXT("SaveGame 成功:槽[%s] 收集 Actor=%d"), *SlotName, CollectedCount);
	OnSaveComplete().Broadcast(SlotName, true);
	return true;
}

// === LoadGame ===

bool USaveSystemBPLibrary::LoadGame(UObject* WorldContextObject, const FString& SlotName,
	FText& OutErrorMessage)
{
	// 前置检查:World
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World)
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::InvalidWorld);
		UE_LOG(LogSaveSystem, Error, TEXT("LoadGame 失败:WorldContext 无效"));
		OnLoadComplete().Broadcast(SlotName, false);
		return false;
	}

	// 前置检查:槽存在
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::SlotNotFound);
		UE_LOG(LogSaveSystem, Error, TEXT("LoadGame 失败:槽[%s]不存在"), *SlotName);
		OnLoadComplete().Broadcast(SlotName, false);
		return false;
	}

	// 反序列化
	USaveGame* LoadedRaw = UGameplayStatics::LoadGameFromSlot(SlotName, 0);
	if (!LoadedRaw)
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::ReadFailed);
		UE_LOG(LogSaveSystem, Error, TEXT("LoadGame 失败:文件损坏,槽[%s]"), *SlotName);
		OnLoadComplete().Broadcast(SlotName, false);
		return false;
	}
	USaveSystemSaveGame* SaveGame = Cast<USaveSystemSaveGame>(LoadedRaw);
	if (!SaveGame)
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::ReadFailed);
		UE_LOG(LogSaveSystem, Error, TEXT("LoadGame 失败:存档类型不匹配,槽[%s]"), *SlotName);
		OnLoadComplete().Broadcast(SlotName, false);
		return false;
	}

	// 版本兼容检查(不阻断,留迁移钩子)
	const FString CurrentPluginVer = FString::FromInt(FSaveSystemVersion::CurrentPluginVersion);
	if (SaveGame->SlotMeta.PluginVersion != CurrentPluginVer)
	{
		UE_LOG(LogSaveSystem, Warning, TEXT("LoadGame:插件版本不匹配 存档[%s] 当前[%s]"),
			*SaveGame->SlotMeta.PluginVersion, *CurrentPluginVer);
		OnVersionMismatch().Broadcast(SlotName, SaveGame->SlotMeta.PluginVersion, CurrentPluginVer);
	}

	// 关卡校验(不阻断,允许跨关卡)
	const FString CurrentLevel = World->GetName();
	if (!SaveGame->SlotMeta.LevelName.Equals(CurrentLevel, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogSaveSystem, Warning, TEXT("LoadGame:关卡不匹配 存档[%s] 当前[%s],Actor 可能找不到"),
			*SaveGame->SlotMeta.LevelName, *CurrentLevel);
	}

	// 恢复运行时 Actor(Spawn 重建)
	int32 RuntimeRestored = 0;
	for (const FRuntimeActorRecord& Rec : SaveGame->RuntimeActorRecords)
	{
		UClass* ActorClass = Rec.ActorClass.Get();
		if (!ActorClass)
		{
			// 软引用未加载,同步加载
			ActorClass = Rec.ActorClass.LoadSynchronous();
		}
		if (!ActorClass)
		{
			UE_LOG(LogSaveSystem, Warning, TEXT("LoadGame:运行时 Actor 类加载失败,GUID=%s"), *Rec.GUID.ToString());
			continue;
		}

		AActor* NewActor = World->SpawnActor<AActor>(ActorClass, Rec.Transform);
		if (!NewActor)
		{
			UE_LOG(LogSaveSystem, Warning, TEXT("LoadGame:Spawn 失败,GUID=%s"), *Rec.GUID.ToString());
			continue;
		}

		USaveableComponent* Comp = NewActor->FindComponentByClass<USaveableComponent>();
		if (Comp)
		{
			Comp->SetRuntimeSpawned(true);
			Comp->ApplyActorState(Rec.State);
			++RuntimeRestored;
		}
		else
		{
			UE_LOG(LogSaveSystem, Warning, TEXT("LoadGame:Spawn 的 Actor[%s] 无 SaveableComponent,跳过状态恢复"),
				*NewActor->GetName());
		}
	}

	// 恢复地图 Actor:先建一次性哈希表 TMap<FGuid,AActor*> 加速查找
	TMap<FGuid, AActor*> GuidToActorMap;
	for (TWeakObjectPtr<USaveableComponent> WeakComp : GetSaveableRegistry())
	{
		// 跳过已失效对象
		if (!WeakComp.IsValid())
		{
			continue;
		}
		USaveableComponent* Comp = WeakComp.Get();
		if (!Comp || !Comp->GetOwner() || Comp->GetWorld() != World)
		{
			continue;
		}
		if (!Comp->IsRuntimeSpawned())
		{
			GuidToActorMap.Add(Comp->GetSaveGUID(), Comp->GetOwner());
		}
	}

	// 清理失效条目
	CleanSaveableRegistry();

	int32 LevelRestored = 0;
	for (const FLevelActorRecord& Rec : SaveGame->LevelActorRecords)
	{
		if (AActor* const* Found = GuidToActorMap.Find(Rec.GUID))
		{
			AActor* Actor = *Found;
			if (USaveableComponent* Comp = Actor->FindComponentByClass<USaveableComponent>())
			{
				Comp->ApplyActorState(Rec.State);
				++LevelRestored;
			}
		}
		else
		{
			UE_LOG(LogSaveSystem, Warning, TEXT("LoadGame:地图 Actor 未找到 GUID=%s(可能关卡变了)"),
				*Rec.GUID.ToString());
		}
	}

	// 注入领域数据
		for (TWeakObjectPtr<UObject> WeakClient : GetClientRegistry())
	{
		// 跳过已失效对象
		if (!WeakClient.IsValid())
		{
			continue;
		}
		UObject* Client = WeakClient.Get();
		const FName ClientName = ISaveSystemClient::Execute_GetClientName(Client);
		if (ClientName == NAME_None)
		{
			continue;
		}
		// 从 bytes 反序列化重建领域数据子对象
		if (const FClientDataEntry* Entry = SaveGame->ClientData.Find(ClientName))
		{
			if (!Entry->ClassName.IsEmpty() && Entry->SerializedData.Num() > 0)
			{
				// 按类路径加载子对象类
				UClass* DataClass = LoadObject<UClass>(nullptr, *Entry->ClassName);
				if (DataClass && DataClass->IsChildOf(USaveSystemSaveGameData::StaticClass()))
				{
					// 创建子对象实例
					USaveSystemSaveGameData* Data = NewObject<USaveSystemSaveGameData>(GetTransientPackage(), DataClass);

					// 从 bytes 反序列化
					FMemoryReader Reader(Entry->SerializedData);
					FObjectAndNameAsStringProxyArchive Ar(Reader, /*bInLoadIfFindFails=*/ true);
					// 不设 ArIsSaveGame:与 Save 时保持一致,全量反序列化
					Data->Serialize(Ar);

					ISaveSystemClient::Execute_ApplySaveData(Client, Data);
				}
				else
				{
					UE_LOG(LogSaveSystem, Warning, TEXT("LoadGame:客户端[%s] 数据类[%s]加载失败或类型不匹配"),
						*ClientName.ToString(), *Entry->ClassName);
				}
			}
			else
			{
				UE_LOG(LogSaveSystem, Warning, TEXT("LoadGame:客户端[%s] 存档数据为空"), *ClientName.ToString());
			}
		}
		else
		{
			UE_LOG(LogSaveSystem, Warning, TEXT("LoadGame:客户端[%s]无对应存档数据"), *ClientName.ToString());
		}
	}

	// 清理失效条目
	CleanClientRegistry();

	UE_LOG(LogSaveSystem, Log, TEXT("LoadGame 成功:槽[%s] 运行时Actor=%d 地图Actor=%d"),
		*SlotName, RuntimeRestored, LevelRestored);
	OnLoadComplete().Broadcast(SlotName, true);
	return true;
}

// === 槽管理 ===

bool USaveSystemBPLibrary::DeleteSlot(const FString& SlotName, FText& OutErrorMessage)
{
	if (SlotName.IsEmpty())
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::InvalidSlotName);
		return false;
	}
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::SlotNotFound);
		return false;
	}
	const bool bDeleted = UGameplayStatics::DeleteGameInSlot(SlotName, 0);
	if (!bDeleted)
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::WriteFailed);
		UE_LOG(LogSaveSystem, Error, TEXT("DeleteSlot 失败:槽[%s]"), *SlotName);
		return false;
	}
	UE_LOG(LogSaveSystem, Log, TEXT("DeleteSlot 成功:槽[%s]"), *SlotName);
	return true;
}

bool USaveSystemBPLibrary::CreateSlot(const FString& SlotName, const FText& DisplayName,
	FText& OutErrorMessage)
{
	if (SlotName.IsEmpty())
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::InvalidSlotName);
		return false;
	}
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::SlotAlreadyExists);
		return false;
	}

	// 创建仅含元数据的空存档
	USaveSystemSaveGame* SaveGame = NewObject<USaveSystemSaveGame>();
	SaveGame->SlotMeta.SlotName = SlotName;
	SaveGame->SlotMeta.CreateTime = FDateTime::UtcNow();
	SaveGame->SlotMeta.LastSaveTime = FDateTime::UtcNow();
	SaveGame->SlotMeta.EngineVersion = FSaveSystemVersion::CurrentEngineVersion;
	SaveGame->SlotMeta.PluginVersion = FString::FromInt(FSaveSystemVersion::CurrentPluginVersion);
	SaveGame->SlotMeta.DisplayName = DisplayName;

	const bool bSaved = UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, 0);
	if (!bSaved)
	{
		OutErrorMessage = ErrorToText(ESaveSystemError::WriteFailed);
		return false;
	}
	UE_LOG(LogSaveSystem, Log, TEXT("CreateSlot 成功:槽[%s]"), *SlotName);
	return true;
}

bool USaveSystemBPLibrary::DoesSlotExist(const FString& SlotName)
{
	if (SlotName.IsEmpty())
	{
		return false;
	}
	return UGameplayStatics::DoesSaveGameExist(SlotName, 0);
}

void USaveSystemBPLibrary::GetSlotList(TArray<FSaveSlotMetadata>& OutSlots)
{
	OutSlots.Reset();
	// 扫描项目 Save 目录下所有 .sav 文件,逐个加载元数据
	const FString SaveDir = FPaths::ProjectSavedDir() / TEXT("SaveGames");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *SaveDir, TEXT(".sav"));
	for (const FString& File : Files)
	{
		const FString SlotName = FPaths::GetBaseFilename(File);
		USaveGame* Raw = UGameplayStatics::LoadGameFromSlot(SlotName, 0);
		if (USaveSystemSaveGame* SaveGame = Cast<USaveSystemSaveGame>(Raw))
		{
			OutSlots.Add(SaveGame->SlotMeta);
		}
	}
}

// === 快速存档 ===

// 固定快存槽名
static const FString QuickSaveSlotName = TEXT("QuickSave");

bool USaveSystemBPLibrary::QuickSave(UObject* WorldContextObject, FText& OutErrorMessage)
{
	// 快存固定覆盖,不报 SlotAlreadyExists
	return SaveGame(WorldContextObject, QuickSaveSlotName, true, OutErrorMessage);
}

bool USaveSystemBPLibrary::QuickLoad(UObject* WorldContextObject, FText& OutErrorMessage)
{
	return LoadGame(WorldContextObject, QuickSaveSlotName, OutErrorMessage);
}

// === 自动存档 ===

bool USaveSystemBPLibrary::AutoSave(UObject* WorldContextObject, FText& OutErrorMessage)
{
	// 滚动自动存档:AutoSave_001 起递增,保留最近 3 个,超出删最旧
	static constexpr int32 MaxAutoSaves = 3;

	// 找下一个可用序号
	int32 NextIndex = 1;
	FString SlotName;
	do
	{
		SlotName = FString::Printf(TEXT("AutoSave_%03d"), NextIndex);
		++NextIndex;
	} while (UGameplayStatics::DoesSaveGameExist(SlotName, 0));

	// 保存到新槽
	const bool bOk = SaveGame(WorldContextObject, SlotName, true, OutErrorMessage);
	if (!bOk)
	{
		return false;
	}

	// 清理超出 MaxAutoSaves 的最旧自动存档
	TArray<FString> AutoSlots;
	for (int32 i = 1; i < 1000; ++i)
	{
		FString Candidate = FString::Printf(TEXT("AutoSave_%03d"), i);
		if (UGameplayStatics::DoesSaveGameExist(Candidate, 0))
		{
			AutoSlots.Add(Candidate);
		}
	}
	// AutoSlots 已按序号升序,序号小的旧,超出保留数的删除
	while (AutoSlots.Num() > MaxAutoSaves)
	{
		FText Dummy;
		DeleteSlot(AutoSlots[0], Dummy);
		AutoSlots.RemoveAt(0);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
