// 存档对象基类与领域数据载体基类
// USaveSystemSaveGameData: 领域系统继承它放自己的 SaveGame 字段(强类型,原生反射序列化)
// USaveSystemSaveGame: 存档对象,承载插槽元数据 + Actor 记录 + 领域数据注入区
// SaveSystem Plugin

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "SaveSystemSlotMeta.h"
#include "SaveSystemTypes.h"
#include "SaveSystemSaveGame.generated.h"

// 领域数据的抽象载体
// 用法:领域系统(如 Quest)继承出 UQuestSaveGameData,内含 UPROPERTY(SaveGame) FQuestSaveData QuestData
// 通过 ISaveSystemClient 注入到 USaveSystemSaveGame::ClientData
UCLASS(Abstract, BlueprintType, Blueprintable)
class SAVESYSTEM_API USaveSystemSaveGameData : public UObject
{
	GENERATED_BODY()
};

// 存档对象(承载三类数据:插槽元数据/Actor记录/领域数据注入区)
// 领域系统通过 ISaveSystemClient 注入/取出 ClientData
// ClientData 用 bytes 存储,避免 BP 子对象跨会话 Instanced 序列化失效
UCLASS(BlueprintType, Blueprintable)
class SAVESYSTEM_API USaveSystemSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	USaveSystemSaveGame();

	// 插槽元数据(每个存档文件固定一份)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Slot")
	FSaveSlotMetadata SlotMeta;

	// 运行时 Actor 记录(Spawn 出来的对象,跨会话恢复用)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|RuntimeActor")
	TArray<FRuntimeActorRecord> RuntimeActorRecords;

	// 地图原生 Actor 的状态记录(按路径 GUID 索引)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|LevelActor")
	TArray<FLevelActorRecord> LevelActorRecords;

	// 领域数据注入区:ClientName -> 序列化后的领域数据(bytes)
	// 用 bytes 存储而非 TObjectPtr,因为 Instanced 对 BP 子对象跨会话序列化不可靠
	// Save 时 BPLibrary 用 FObjectWriter 把 USaveSystemSaveGameData 子对象序列化为 bytes
	// Load 时 BPLibrary 用 FObjectReader 从 bytes 重建子对象
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Client")
	TMap<FName, FClientDataEntry> ClientData;
};
