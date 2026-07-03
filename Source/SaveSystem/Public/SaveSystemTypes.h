// 存档系统公共类型:错误码/版本常量/委托/Actor 状态/记录结构
// SaveSystem Plugin

#pragma once

#include "CoreMinimal.h"
#include "SaveSystemTypes.generated.h"

// 前向声明:Actor 强类型数据载体基类(避免头文件循环依赖)
class USaveSystemSaveGameData;

// === 错误码 ===
UENUM(BlueprintType)
enum class ESaveSystemError : uint8
{
	None = 0,              // 成功
	InvalidWorld,          // WorldContext 无效
	InvalidSlotName,       // 槽名为空或非法
	SlotNotFound,          // 槽不存在(加载/删除时)
	SlotAlreadyExists,     // 槽已存在且未允许覆盖
	WriteFailed,           // 磁盘写入失败(权限/空间)
	ReadFailed,            // 读取失败(文件损坏)
	VersionMismatch,       // 版本不兼容
	LevelMismatch,         // 关卡不匹配(警告级,不阻断)
	NoSaveableComponents   // 无可保存对象(警告级,允许空存档)
};

// === 版本常量 ===
USTRUCT(BlueprintType)
struct FSaveSystemVersion
{
	GENERATED_BODY()

	// 当前插件版本(硬编码,随发版递增)
	static constexpr int32 CurrentPluginVersion = 1;
	// 当前引擎版本字符串(用于存档元数据比对)
	static constexpr const TCHAR* CurrentEngineVersion = TEXT("5.7");
};

// === Actor 状态(Actor 自管语义,插件默认采集 Transform,自定义字段由 Actor 提供) ===
USTRUCT(BlueprintType)
struct FActorSaveState
{
	GENERATED_BODY()

	// 位置/旋转/缩放(插件默认采集,所有 Actor 都有)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|ActorState")
	FTransform Transform;

	// 可见性(插件默认采集)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|ActorState")
	bool bHidden = false;

	// 自定义状态:Actor 自己决定存什么,以字符串键值对存(简单通用,复杂场景可扩展为 bytes)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|ActorState")
	TMap<FString, FString> CustomFields;

	// 强类型数据载体:Actor 的复杂数据(如背包 TArray<FItemStack>)走此字段,无需转字符串
	// 用 bytes 存储(类名+序列化数据),避免 Instanced 子对象跨会话失效
	// 留空=ClassName 为空,表示该 Actor 无强类型数据(仅用 CustomFields 或纯 Transform)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|ActorState")
	FString ActorDataClassName;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|ActorState")
	TArray<uint8> ActorDataBytes;
};

// === 运行时 Actor 记录(Spawn 出来的对象,需 Spawn 重建) ===
USTRUCT(BlueprintType)
struct FRuntimeActorRecord
{
	GENERATED_BODY()

	// 方案 A:随存档持久化的 GUID(运行时 Actor 用 NewGuid 生成)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|RuntimeActor")
	FGuid GUID;

	// Spawn 用类(软引用,避免硬引用加载膨胀,跨关卡安全)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|RuntimeActor")
	TSoftClassPtr<AActor> ActorClass;

	// Spawn 初始 Transform
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|RuntimeActor")
	FTransform Transform;

	// 完整状态(含自定义字段)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|RuntimeActor")
	FActorSaveState State;

	// 所属关卡名(多关卡校验用)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|RuntimeActor")
	FName LevelName;
};

// === 地图 Actor 记录(已存在于世界中,只需恢复状态) ===
USTRUCT(BlueprintType)
struct FLevelActorRecord
{
	GENERATED_BODY()

	// 方案 A:路径派生的 GUID,加载时按此定位世界中 Actor
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|LevelActor")
	FGuid GUID;

	// 完整状态
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|LevelActor")
	FActorSaveState State;
};

// === 领域数据条目:把子对象序列化为 bytes 存储,避免 Instanced 子对象跨会话失效 ===
USTRUCT(BlueprintType)
struct FClientDataEntry
{
	GENERATED_BODY()

	// 子对象类的完整路径(用于 Load 时找到正确的 BP 类并实例化)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Client")
	FString ClassName;

	// 子对象的二进制序列化数据
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Client")
	TArray<uint8> SerializedData;
};

// === 广播委托 ===
// 保存完成:槽名 + 是否成功
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSaveComplete, FString, SlotName, bool, bSuccess);
// 加载完成:槽名 + 是否成功
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoadComplete, FString, SlotName, bool, bSuccess);
// 版本不匹配:槽名 + 存档版本 + 当前版本(游戏层可监听做迁移)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnVersionMismatch, FString, SlotName, FString, SavedVersion, FString, CurrentVersion);
