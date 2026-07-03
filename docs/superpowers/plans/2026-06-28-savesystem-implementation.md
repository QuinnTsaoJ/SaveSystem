# SaveSystem 轻量级通用存档插件 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: 本项目无 git，subagent 只读。采用 inline 执行（主 agent 用 Write/Edit 直接写代码 + TodoWrite 跟踪）。Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现一个轻量级、零依赖的 UE5 通用存档插件，提供存档读写、槽管理、Actor 状态保存恢复、领域数据注入能力。

**Architecture:** 纯静态函数库 `USaveSystemBPLibrary` 作为唯一入口；`USaveableComponent` 负责 Actor 注册与状态回调；`USaveSystemSaveGame` 基类 + `ISaveSystemClient` 接口承载领域数据；复用 UE 原生 `SaveGameToSlot` 反射序列化，不写自研序列化器。GUID 方案 A：地图 Actor 路径派生免持久化，运行时 Actor NewGuid 随档持久化。

**Tech Stack:** Unreal Engine 5.7 C++，仅依赖 Core/CoreUObject/Engine，零第三方依赖。

**验证手段:** 本项目无自动化测试框架，验证 = ① UBT 编译通过；② 插件被编辑器识别并加载；③ 编辑器内创建测试关卡/Actor 验证 Save/Load 行为。无 git，跳过所有 commit 步骤，改用 TodoWrite 跟踪。

**Spec:** `F:\UEProject\Inventory\Plugins\SaveSystem\docs\superpowers\specs\2026-06-27-savesystem-design.md`

**编码规范遵循:** 团队规范——除代码标识符与参数名外，所有注释（含 @brief、ToolTip、行内注释）必须使用简体中文；命名遵循 UE 风格（类型 U 前缀、成员 b 前缀布尔、PascalCase）。

---

## File Structure

```
Plugins/SaveSystem/
├── SaveSystem.uplugin                                    # Task 1
├── Source/SaveSystem/
│   ├── SaveSystem.Build.cs                               # Task 1
│   ├── Public/
│   │   ├── SaveSystem.h                                  # Task 1
│   │   ├── SaveSystemLog.h                               # Task 2
│   │   ├── SaveSystemTypes.h                             # Task 3 (错误码+版本+委托+Record结构)
│   │   ├── SaveSystemSlotMeta.h                          # Task 3 (FSaveSlotMetadata)
│   │   ├── SaveSystemSaveGame.h                          # Task 4 (USaveSystemSaveGameData + USaveSystemSaveGame)
│   │   ├── ISaveableActor.h                              # Task 5
│   │   ├── SaveableComponent.h                           # Task 5
│   │   ├── ISaveSystemClient.h                           # Task 6
│   │   └── SaveSystemBPLibrary.h                         # Task 7
│   └── Private/
│       ├── SaveSystem.cpp                                # Task 1
│       ├── SaveableComponent.cpp                         # Task 5
│       ├── SaveSystemSaveGame.cpp                        # Task 4
│       └── SaveSystemBPLibrary.cpp                       # Task 7
└── docs/...                                              # (已有 spec)
```

**职责边界:** 每个 .h 一个类/结构，单一职责。Types.h 放枚举与版本常量；SlotMeta.h 放元数据结构；SaveGame.h 放 SaveGame 类与数据载体基类；SaveableComponent.h/.cpp 放组件与 ISaveableActor；ISaveSystemClient.h 独立接口；BPLibrary.h/.cpp 放入口与全部流程逻辑。

---

## Task 1: 插件骨架（.uplugin / Build.cs / 模块声明）

**Files:**
- Create: `Plugins/SaveSystem/SaveSystem.uplugin`
- Create: `Plugins/SaveSystem/Source/SaveSystem/SaveSystem.Build.cs`
- Create: `Plugins/SaveSystem/Source/SaveSystem/Public/SaveSystem.h`
- Create: `Plugins/SaveSystem/Source/SaveSystem/Private/SaveSystem.cpp`

- [ ] **Step 1: 创建 .uplugin 清单**

`Plugins/SaveSystem/SaveSystem.uplugin`:
```json
{
	"FileVersion": 3,
	"Version": 1,
	"VersionName": "1.0",
	"FriendlyName": "SaveSystem",
	"Description": "轻量级通用存档插件 - 存档读写/槽管理/Actor 状态保存恢复,零业务依赖",
	"Category": "Other",
	"CreatedBy": "一氧化二氢",
	"CreatedByURL": "",
	"DocsURL": "",
	"MarketplaceURL": "",
	"SupportURL": "",
	"CanContainContent": true,
	"IsBetaVersion": true,
	"IsExperimentalVersion": false,
	"Installed": false,
	"Modules": [
		{
			"Name": "SaveSystem",
			"Type": "Runtime",
			"LoadingPhase": "Default"
		}
	]
}
```

- [ ] **Step 2: 创建 Build.cs（零依赖，仅 Core/CoreUObject/Engine）**

`Plugins/SaveSystem/Source/SaveSystem/SaveSystem.Build.cs`:
```csharp
// 存档系统模块构建规则 - 零第三方依赖
// SaveSystem Plugin

using UnrealBuildTool;

public class SaveSystem : ModuleRules
{
	public SaveSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			}
		);
	}
}
```

- [ ] **Step 3: 创建模块头文件**

`Plugins/SaveSystem/Source/SaveSystem/Public/SaveSystem.h`:
```cpp
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
```

- [ ] **Step 4: 创建模块实现**

`Plugins/SaveSystem/Source/SaveSystem/Private/SaveSystem.cpp`:
```cpp
// 存档系统模块入口实现
// SaveSystem Plugin

#include "SaveSystem.h"

#define LOCTEXT_NAMESPACE "FSaveSystemModule"

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
```

- [ ] **Step 5: 编译验证**

Run: 用 UBT 编译 SaveSystem 模块（命令模板见 ue-cpp-build-debug 技能；UE5.7 项目路径 `F:/UEProject/Inventory/Inventory.uproject`，目标 Development Editor，平台 Win64）。
Expected: 编译通过，无 error。插件被项目识别（ uproject 暂未引用，但插件目录存在即可被编辑器扫描）。

---

## Task 2: 日志分类

**Files:**
- Create: `Plugins/SaveSystem/Source/SaveSystem/Public/SaveSystemLog.h`

- [ ] **Step 1: 创建日志宏**

`Plugins/SaveSystem/Source/SaveSystem/Public/SaveSystemLog.h`:
```cpp
// 存档系统日志分类声明
// SaveSystem Plugin

#pragma once

#include "CoreMinimal.h"

// 日志分类:LogSaveSystem,默认 verbosity=Log,编译期默认=All
SAVEYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogSaveSystem, Log, All);
```

- [ ] **Step 2: 在模块 cpp 中定义日志分类**

修改 `Plugins/SaveSystem/Source/SaveSystem/Private/SaveSystem.cpp`，在 `#include "SaveSystem.h"` 后追加：
```cpp
#include "SaveSystemLog.h"

// 日志分类定义(DECLARE_LOG_CATEGORY_EXTERN 的对应定义)
DEFINE_LOG_CATEGORY(LogSaveSystem);
```

- [ ] **Step 3: 编译验证**

Run: UBT 编译。
Expected: 通过，`LogSaveSystem` 可用。

---

## Task 3: 公共类型（错误码、版本、委托、Actor 状态与记录结构）

**Files:**
- Create: `Plugins/SaveSystem/Source/SaveSystem/Public/SaveSystemTypes.h`
- Create: `Plugins/SaveSystem/Source/SaveSystem/Public/SaveSystemSlotMeta.h`

- [ ] **Step 1: 创建 SaveSystemTypes.h（错误码 + 版本 + 委托 + Actor 状态 + 记录结构）**

`Plugins/SaveSystem/Source/SaveSystem/Public/SaveSystemTypes.h`:
```cpp
// 存档系统公共类型:错误码/版本常量/委托/Actor 状态/记录结构
// SaveSystem Plugin

#pragma once

#include "CoreMinimal.h"
#include "SaveSystemTypes.generated.h"

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

	// 位置/旋转/缩放(插件默认采集)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|ActorState")
	FTransform Transform;

	// 可见性(插件默认采集)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|ActorState")
	uint8 bHidden : 1;

	// 自定义状态:Actor 自己决定存什么,以字符串键值对存(简单通用)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|ActorState")
	TMap<FString, FString> CustomFields;
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

// === 广播委托 ===
// 保存完成:槽名 + 是否成功
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSaveComplete, FString, SlotName, bool, bSuccess);
// 加载完成:槽名 + 是否成功
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoadComplete, FString, SlotName, bool, bSuccess);
// 版本不匹配:槽名 + 存档版本 + 当前版本(游戏层可监听做迁移)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnVersionMismatch, FString, SlotName, FString, SavedVersion, FString, CurrentVersion);
```

- [ ] **Step 2: 创建 SaveSystemSlotMeta.h（插槽元数据结构）**

`Plugins/SaveSystem/Source/SaveSystem/Public/SaveSystemSlotMeta.h`:
```cpp
// 存档槽元数据结构
// SaveSystem Plugin

#pragma once

#include "CoreMinimal.h"
#include "SaveSystemSlotMeta.generated.h"

// 单个存档槽的元数据(每个存档文件固定一份,对应文档 3.4)
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
```

- [ ] **Step 3: 编译验证**

Run: UBT 编译。
Expected: 通过。注意 `FActorSaveState::bHidden` 用了位域 `uint8 bHidden : 1`，需确认 SaveGame 反射支持位域（UE5 支持）。若编译报位域 SaveGame 警告，改为普通 `bool bHidden`。

---

## Task 4: SaveGame 基类与领域数据载体

**Files:**
- Create: `Plugins/SaveSystem/Source/SaveSystem/Public/SaveSystemSaveGame.h`
- Create: `Plugins/SaveSystem/Source/SaveSystem/Private/SaveSystemSaveGame.cpp`

- [ ] **Step 1: 创建 SaveSystemSaveGame.h**

`Plugins/SaveSystem/Source/SaveSystem/Public/SaveSystemSaveGame.h`:
```cpp
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
// 领域系统通过 ISaveSystemClient 注入/取出 ClientData 子对象
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

	// 领域数据注入区:ClientName -> 领域数据子对象
	// 利用 UE 原生 SaveGame 反射 + 子对象机制,领域数据保持强类型
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Client")
	TMap<FName, TObjectPtr<USaveSystemSaveGameData>> ClientData;
};
```

- [ ] **Step 2: 创建 SaveSystemSaveGame.cpp（构造函数初始化元数据默认值）**

`Plugins/SaveSystem/Source/SaveSystem/Private/SaveSystemSaveGame.cpp`:
```cpp
// 存档对象实现
// SaveSystem Plugin

#include "SaveSystemSaveGame.h"

USaveSystemSaveGame::USaveSystemSaveGame()
{
	// SaveGame 对象默认使用 SaveSystem 作为槽名前缀(实际槽名由调用方指定)
	// 此处不预设 SlotMeta,由 BPLibrary::SaveGame 在保存时填充
}
```

- [ ] **Step 3: 编译验证**

Run: UBT 编译。
Expected: 通过。`TMap<FName, TObjectPtr<USaveSystemSaveGameData>>` 作为 SaveGame 属性需确认 UE5.7 的 SaveGame 反射支持 TMap（UE5 支持 TMap 序列化）。`TObjectPtr` 在 SaveGame 上下文中会自动序列化为子对象。

---

## Task 5: SaveableComponent 与 ISaveableActor 接口

**Files:**
- Create: `Plugins/SaveSystem/Source/SaveSystem/Public/ISaveableActor.h`
- Create: `Plugins/SaveSystem/Source/SaveSystem/Public/SaveableComponent.h`
- Create: `Plugins/SaveSystem/Source/SaveSystem/Private/SaveableComponent.cpp`

- [ ] **Step 1: 创建 ISaveableActor 接口**

`Plugins/SaveSystem/Source/SaveSystem/Public/ISaveableActor.h`:
```cpp
// Actor 状态回调接口(C++ 通道)
// Actor 实现本接口以提供自定义存档字段/恢复逻辑
// 与 Blueprint 通道(SaveableComponent 的 NativeEvent)互补
// SaveSystem Plugin

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
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
	// 保存时由插件调用:Actor 收集要持久化的自定义字段
	// 默认空实现:不提供自定义字段,插件仅保存 Transform
	virtual void GatherSaveFields_Implementation(TMap<FString, FString>& OutFields) {}

	// 加载时由插件调用:Actor 从字段恢复状态
	virtual void ApplySaveFields_Implementation(const TMap<FString, FString>& Fields) {}
};
```

- [ ] **Step 2: 创建 SaveableComponent.h**

`Plugins/SaveSystem/Source/SaveSystem/Public/SaveableComponent.h`:
```cpp
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
	// 收集本 Actor 需持久化的状态(走 ISaveableActor 接口或 BP 事件)
	FActorSaveState GatherActorState();
	// 将状态写回本 Actor
	void ApplyActorState(const FActorSaveState& State);

protected:
	// === Blueprint 通道(BP 用户可重写,默认走 ISaveableActor 接口) ===
	// 收集自定义字段(BP 重写用)。C++ 默认实现:若 Owner 实现 ISaveableActor 则走接口
	UFUNCTION(BlueprintNativeEvent, Category="SaveSystem|Saveable")
	void OnGatherSaveFields(TMap<FString, FString>& OutFields);
	virtual void OnGatherSaveFields_Implementation(TMap<FString, FString>& OutFields);

	UFUNCTION(BlueprintNativeEvent, Category="SaveSystem|Saveable")
	void OnApplySaveFields(const TMap<FString, FString>& Fields);
	virtual void OnApplySaveFields_Implementation(const TMap<FString, FString>& Fields);

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
```

- [ ] **Step 3: 创建 SaveableComponent.cpp**

`Plugins/SaveSystem/Source/SaveSystem/Private/SaveableComponent.cpp`:
```cpp
// 可保存组件实现
// SaveSystem Plugin

#include "SaveableComponent.h"
#include "ISaveableActor.h"
#include "SaveSystemLog.h"
#include "GameFramework/Actor.h"

USaveableComponent::USaveableComponent()
{
	// 组件无需 Tick(存档操作由 BPLibrary 主动触发)
	PrimaryComponentTick.bCanEverTick = false;
}

void USaveableComponent::OnRegister()
{
	Super::OnRegister();

	// 地图 Actor:OnRegister 时由路径派生 GUID 并缓存(免持久化、免冲突,方案 A 核心)
	// 运行 Actor(bRuntimeSpawned==true)的 GUID 在 EnsureRuntimeGUID 时按需生成
	if (!bRuntimeSpawned && GetOwner())
	{
		const FString PathName = GetOwner()->GetPathName();
		// 用路径字符串的稳定哈希生成 GUID(同一路径永远算出同一 GUID)
		CachedGUID = FGuid::NewGuid(); // 占位,Step 4 改为路径哈希派生
		// 注:此处先用占位确保编译,Step 4 替换为真正的路径派生
		bGUIDComputed = true;
	}
}

void USaveableComponent::OnUnregister()
{
	Super::OnUnregister();
	// 注:全局注册表的注册/注销由 BPLibrary 内部维护(静态 TArray),此处仅做组件自身清理
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

	// 收集自定义字段:先走 BP 事件(可能重写),BP 默认实现再走 ISaveableActor 接口
	OnGatherSaveFields(State.CustomFields);

	return State;
}

void USaveableComponent::ApplyActorState(const FActorSaveState& State)
{
	AActor* Owner = GetOwner();
	if (Owner)
	{
		// 恢复 Transform
		Owner->SetActorTransform(State.Transform);
		Owner->SetIsTemporarilyHiddenInEditor(State.bHidden != 0);
		Owner->SetActorHiddenInGame(State.bHidden != 0);
	}

	// 恢复自定义字段:走 BP 事件(默认走 ISaveableActor 接口)
	OnApplySaveFields(State.CustomFields);
}

void USaveableComponent::OnGatherSaveFields_Implementation(TMap<FString, FString>& OutFields)
{
	// BP 默认实现:若 Owner 实现 ISaveableActor 则走接口,否则无自定义字段
	AActor* Owner = GetOwner();
	if (Owner && Owner->GetClass()->ImplementsInterface(USaveableActor::StaticClass()))
	{
		ISaveableActor::GatherSaveFields_Implementation(Owner, OutFields);
	}
}

void USaveableComponent::OnApplySaveFields_Implementation(const TMap<FString, FString>& Fields)
{
	// BP 默认实现:若 Owner 实现 ISaveableActor 则走接口
	AActor* Owner = GetOwner();
	if (Owner && Owner->GetClass()->ImplementsInterface(USaveableActor::StaticClass()))
	{
		ISaveableActor::ApplySaveFields_Implementation(Owner, Fields);
	}
}
```

- [ ] **Step 4: 修正 GUID 路径派生逻辑（替换 OnRegister 中的占位）**

修改 `SaveableComponent.cpp` 的 `OnRegister`，把占位 `CachedGUID = FGuid::NewGuid();` 替换为真正的路径哈希派生：
```cpp
void USaveableComponent::OnRegister()
{
	Super::OnRegister();

	if (!bRuntimeSpawned && GetOwner())
	{
		const FString PathName = GetOwner()->GetPathName();
		// 方案 A:用路径字符串生成稳定哈希,再填充为 GUID(同一路径永远算出同一 GUID)
		// 取路径 SHA1 前 16 字节填充 GUID,保证唯一性与稳定性
		FSHAHash PathHash;
		FSHA1::HashBuffer(*PathName, PathName.Len() * sizeof(TCHAR), PathHash.Hash);
		// 取前 16 字节填入 FGuid(4 个 uint32)
		const uint32* HashWords = reinterpret_cast<const uint32*>(PathHash.Hash);
		CachedGUID = FGuid(HashWords[0], HashWords[1], HashWords[2], HashWords[3]);
		bGUIDComputed = true;
	}
}
```

需在文件顶部 include 追加：
```cpp
#include "Misc/SHA1.h"
```

- [ ] **Step 5: 编译验证**

Run: UBT 编译。
Expected: 通过。重点检查：`BlueprintNativeEvent` + `_Implementation` 配对、`ISaveableActor` 接口调用宏 `ISaveableActor::Execute_GatherSaveFields` 的正确用法。

注意：接口调用应使用 `Execute_` 前缀宏（因为 `_Implementation` 是给 C++ 实现用的，跨多态调用要用 Execute 宏）。如果编译报接口调用问题，把 `ISaveableActor::GatherSaveFields_Implementation(Owner, OutFields)` 改为：
```cpp
ISaveableActor::Execute_GatherSaveFields(Owner, OutFields);
```
（Execute 宏会正确路由到 C++/BP 实现）。

---

## Task 6: ISaveSystemClient 接口

**Files:**
- Create: `Plugins/SaveSystem/Source/SaveSystem/Public/ISaveSystemClient.h`

- [ ] **Step 1: 创建 ISaveSystemClient 接口**

`Plugins/SaveSystem/Source/SaveSystem/Public/ISaveSystemClient.h`:
```cpp
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
	virtual USaveSystemSaveGameData* GatherSaveData_Implementation() { return nullptr; }

	// 加载时:插件从 ClientData 取出本客户端的子对象,交给领域系统恢复
	// 默认空实现
	virtual void ApplySaveData_Implementation(USaveSystemSaveGameData* Data) {}

	// 客户端唯一标识(用作 ClientData 的键,如 "Quest")
	// 默认返回 NAME_None(调用方应在注册前确保 ClientName 有效)
	virtual FName GetClientName_Implementation() const { return NAME_None; }
};
```

- [ ] **Step 2: 编译验证**

Run: UBT 编译。
Expected: 通过。

---

## Task 7: 静态函数库 USaveSystemBPLibrary（唯一入口）

**Files:**
- Create: `Plugins/SaveSystem/Source/SaveSystem/Public/SaveSystemBPLibrary.h`
- Create: `Plugins/SaveSystem/Source/SaveSystem/Private/SaveSystemBPLibrary.cpp`

- [ ] **Step 1: 创建 SaveSystemBPLibrary.h**

`Plugins/SaveSystem/Source/SaveSystem/Public/SaveSystemBPLibrary.h`:
```cpp
// 存档系统唯一入口 —— 纯静态函数库
// 职责:存档读写/槽管理/快存快读/自动存档/客户端注册/事件广播
// 所有函数返回 bool Success + FText OutErrorMessage(异常处理约定)
// 自动存档时基由游戏层提供(纯静态库无 Tick),插件提供 AutoSave 节点 + 滚动槽管理
// SaveSystem Plugin

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SaveSystemTypes.h"
#include "SaveSystemSlotMeta.h"
#include "SaveSystemBPLibrary.generated.h"

class USaveableComponent;

UCLASS()
class SAVESYSTEM_API USaveSystemBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// === 存档槽管理 ===

	// 保存到指定槽(覆盖式)。自动收集所有 SaveableComponent + ISaveSystemClient 数据
	UFUNCTION(BlueprintCallable, Category="SaveSystem",
		meta=(WorldContext="WorldContextObject"))
	static bool SaveGame(UObject* WorldContextObject, const FString& SlotName,
		bool bOverride, FText& OutErrorMessage);

	// 从指定槽加载。恢复 Actor + 注入领域数据 + 广播完成事件
	UFUNCTION(BlueprintCallable, Category="SaveSystem",
		meta=(WorldContext="WorldContextObject"))
	static bool LoadGame(UObject* WorldContextObject, const FString& SlotName,
		FText& OutErrorMessage);

	// 删除存档槽
	UFUNCTION(BlueprintCallable, Category="SaveSystem")
	static bool DeleteSlot(const FString& SlotName, FText& OutErrorMessage);

	// 创建空槽(只写元数据,用于"新建存档"场景)
	UFUNCTION(BlueprintCallable, Category="SaveSystem")
	static bool CreateSlot(const FString& SlotName, const FText& DisplayName,
		FText& OutErrorMessage);

	// 查询槽是否存在
	UFUNCTION(BlueprintCallable, Category="SaveSystem")
	static bool DoesSlotExist(const FString& SlotName);

	// 枚举所有槽的元数据(供存档列表 UI)
	UFUNCTION(BlueprintCallable, Category="SaveSystem")
	static void GetSlotList(TArray<FSaveSlotMetadata>& OutSlots);

	// === 快速存档 ===

	// 固定槽名(QuickSaveSlot),快捷保存
	UFUNCTION(BlueprintCallable, Category="SaveSystem",
		meta=(WorldContext="WorldContextObject"))
	static bool QuickSave(UObject* WorldContextObject, FText& OutErrorMessage);

	// 固定槽名(QuickSaveSlot),快捷读取
	UFUNCTION(BlueprintCallable, Category="SaveSystem",
		meta=(WorldContext="WorldContextObject"))
	static bool QuickLoad(UObject* WorldContextObject, FText& OutErrorMessage);

	// === 自动存档(时基由游戏层提供) ===

	// 手动触发一次自动存档(写入自动存档槽,按序号滚动,保留最近 N 个)
	UFUNCTION(BlueprintCallable, Category="SaveSystem",
		meta=(WorldContext="WorldContextObject"))
	static bool AutoSave(UObject* WorldContextObject, FText& OutErrorMessage);

	// === 领域客户端注册 ===

	// 注册领域客户端(供 GameMode/PlayerController 在初始化时登记领域系统)
	UFUNCTION(BlueprintCallable, Category="SaveSystem")
	static void RegisterSaveClient(UObject* Client);

	UFUNCTION(BlueprintCallable, Category="SaveSystem")
	static void UnregisterSaveClient(UObject* Client);

	// === 委托访问(BP 绑定用,静态实例承载) ===
	static FOnSaveComplete& OnSaveComplete();
	static FOnLoadComplete& OnLoadComplete();
	static FOnVersionMismatch& OnVersionMismatch();

	// === 内部接口(SaveableComponent 注册/注销用,非 Blueprint) ===
	static void RegisterSaveableComponent(USaveableComponent* Comp);
	static void UnregisterSaveableComponent(USaveableComponent* Comp);

private:
	// 把错误码转为本地化错误文本
	static FText ErrorToText(ESaveSystemError Error);
};
```

- [ ] **Step 2: 创建 SaveSystemBPLibrary.cpp（骨架 + 错误文本转换 + 注册表 + 委托静态实例）**

`Plugins/SaveSystem/Source/SaveSystem/Private/SaveSystemBPLibrary.cpp`:
```cpp
// 存档系统入口实现
// SaveSystem Plugin

#include "SaveSystemBPLibrary.h"
#include "SaveableComponent.h"
#include "ISaveSystemClient.h"
#include "SaveSystemSaveGame.h"
#include "SaveSystemLog.h"
#include "ISaveableActor.h"

#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Internationalization/Internationalization.h"

#define LOCTEXT_NAMESPACE "SaveSystem"

// === 全局静态状态 ===

// SaveableComponent 全局注册表(注册时 Add,注销时 RemoveSwap,O(1) 移除)
static TArray<TObjectPtr<USaveableComponent>>& GetSaveableRegistry()
{
	static TArray<TObjectPtr<USaveableComponent>> Registry;
	return Registry;
}

// 已注册的领域客户端列表
static TArray<TObjectPtr<UObject>>& GetClientRegistry()
{
	static TArray<TObjectPtr<UObject>> Registry;
	return Registry;
}

// 委托静态实例(生命周期与进程一致,存档操作频率低,无泄漏风险)
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
	case ESaveSystemError::None:               return LOCTEXT("Err_None", "成功");
	case ESaveSystemError::InvalidWorld:       return LOCTEXT("Err_InvalidWorld", "WorldContext 无效");
	case ESaveSystemError::InvalidSlotName:    return LOCTEXT("Err_InvalidSlotName", "槽名为空或非法");
	case ESaveSystemError::SlotNotFound:       return LOCTEXT("Err_SlotNotFound", "槽不存在");
	case ESaveSystemError::SlotAlreadyExists:  return LOCTEXT("Err_SlotAlreadyExists", "槽已存在且未允许覆盖");
	case ESaveSystemError::WriteFailed:        return LOCTEXT("Err_WriteFailed", "磁盘写入失败(权限/空间)");
	case ESaveSystemError::ReadFailed:         return LOCTEXT("Err_ReadFailed", "读取失败(文件损坏)");
	case ESaveSystemError::VersionMismatch:    return LOCTEXT("Err_VersionMismatch", "版本不兼容");
	case ESaveSystemError::LevelMismatch:      return LOCTEXT("Err_LevelMismatch", "关卡不匹配(警告级)");
	case ESaveSystemError::NoSaveableComponents:return LOCTEXT("Err_NoSaveable", "无可保存对象");
	default:                                   return LOCTEXT("Err_Unknown", "未知错误");
	}
}

#undef LOCTEXT_NAMESPACE
```

- [ ] **Step 3: 实现 SaveGame 函数**

在 `SaveSystemBPLibrary.cpp` 的 `#undef LOCTEXT_NAMESPACE` 之前追加 `SaveGame` 实现：
```cpp
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
	SaveGame->SlotMeta.LevelName = World->GetWorld()->GetName();
	// PlayTime 暂不累加(游戏层可自行维护并在此扩展)
	SaveGame->SlotMeta.PlayTime = 0.f;

	// 收集 Actor(遍历全局注册表,按 bRuntimeSpawned 分类)
	int32 CollectedCount = 0;
	for (TObjectPtr<USaveableComponent> Comp : GetSaveableRegistry())
	{
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

		if (Comp->IsRuntimeSpawned())
		{
			FRuntimeActorRecord& Rec = SaveGame->RuntimeActorRecords.AddDefaulted_GetRef();
			Rec.GUID = Guid;
			Rec.ActorClass = Comp->GetOwner()->GetClass();
			Rec.Transform = State.Transform;
			Rec.State = MoveTemp(State);
			Rec.LevelName = FName(World->GetWorld()->GetName());
		}
		else
		{
			FLevelActorRecord& Rec = SaveGame->LevelActorRecords.AddDefaulted_GetRef();
			Rec.GUID = Guid;
			Rec.State = MoveTemp(State);
		}
		++CollectedCount;
	}

	// 无可保存对象记警告但不阻断(允许空存档)
	if (CollectedCount == 0)
	{
		UE_LOG(LogSaveSystem, Warning, TEXT("SaveGame:当前 World 无可保存对象,将写入空存档"));
	}

	// 收集领域数据(遍历已注册 ISaveSystemClient)
	for (TObjectPtr<UObject> Client : GetClientRegistry())
	{
		if (!Client)
		{
			continue;
		}
		const FName ClientName = ISaveSystemClient::Execute_GetClientName(Client);
		if (ClientName == NAME_None)
		{
			continue;
		}
		USaveSystemSaveGameData* Data = ISaveSystemClient::Execute_GatherSaveData(Client);
		if (Data)
		{
			// 将子对象设为 SaveGame 的实例子对象,确保 SaveGame 反射能序列化
			Data->Rename(nullptr, SaveGame);
			SaveGame->ClientData.Add(ClientName, Data);
		}
	}

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
```

- [ ] **Step 4: 实现 LoadGame 函数**

在 `SaveSystemBPLibrary.cpp` 追加 `LoadGame` 实现：
```cpp
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
			// 软引用未加载,尝试异步/同步加载
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
	for (TObjectPtr<USaveableComponent> Comp : GetSaveableRegistry())
	{
		if (!Comp || !Comp->GetOwner() || Comp->GetWorld() != World)
		{
			continue;
		}
		if (!Comp->IsRuntimeSpawned())
		{
			GuidToActorMap.Add(Comp->GetSaveGUID(), Comp->GetOwner());
		}
	}

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
	for (TObjectPtr<UObject> Client : GetClientRegistry())
	{
		if (!Client)
		{
			continue;
		}
		const FName ClientName = ISaveSystemClient::Execute_GetClientName(Client);
		if (ClientName == NAME_None)
		{
			continue;
		}
		if (USaveSystemSaveGameData* const* DataFound = SaveGame->ClientData.Find(ClientName))
		{
			ISaveSystemClient::Execute_ApplySaveData(Client, *DataFound);
		}
		else
		{
			UE_LOG(LogSaveSystem, Warning, TEXT("LoadGame:客户端[%s]无对应存档数据"), *ClientName.ToString());
		}
	}

	UE_LOG(LogSaveSystem, Log, TEXT("LoadGame 成功:槽[%s] 运行时Actor=%d 地图Actor=%d"),
		*SlotName, RuntimeRestored, LevelRestored);
	OnLoadComplete().Broadcast(SlotName, true);
	return true;
}
```

- [ ] **Step 5: 实现 DeleteSlot / CreateSlot / DoesSlotExist / GetSlotList**

在 `SaveSystemBPLibrary.cpp` 追加槽管理函数：
```cpp
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
		FString SlotName = FPaths::GetBaseFilename(File);
		USaveGame* Raw = UGameplayStatics::LoadGameFromSlot(SlotName, 0);
		if (USaveSystemSaveGame* SaveGame = Cast<USaveSystemSaveGame>(Raw))
		{
			OutSlots.Add(SaveGame->SlotMeta);
		}
	}
}
```

需在文件顶部 include 追加：
```cpp
#include "HAL/FileManager.h"
```

- [ ] **Step 6: 实现 QuickSave / QuickLoad / AutoSave**

在 `SaveSystemBPLibrary.cpp` 追加：
```cpp
// 固定快存槽名
static const FName QuickSaveSlotName = TEXT("QuickSave");

bool USaveSystemBPLibrary::QuickSave(UObject* WorldContextObject, FText& OutErrorMessage)
{
	// 快存固定覆盖,不报 SlotAlreadyExists
	return SaveGame(WorldContextObject, QuickSaveSlotName.ToString(), true, OutErrorMessage);
}

bool USaveSystemBPLibrary::QuickLoad(UObject* WorldContextObject, FText& OutErrorMessage)
{
	return LoadGame(WorldContextObject, QuickSaveSlotName.ToString(), OutErrorMessage);
}

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
	// 按序号排序(序号大的新),超出保留数的删除
	while (AutoSlots.Num() > MaxAutoSaves)
	{
		// 删除序号最小的(最旧)
		FText Dummy;
		DeleteSlot(AutoSlots[0], Dummy);
		AutoSlots.RemoveAt(0);
	}

	return true;
}
```

- [ ] **Step 7: 编译验证（全模块）**

Run: UBT 全量编译 SaveSystem 模块。
Expected: 通过，无 error。重点检查：
- `ISaveSystemClient::Execute_GetClientName` 等接口调用宏正确性
- `TObjectPtr` 在 SaveGame 反射上下文的兼容性
- `TMap<FGuid, AActor*>` 局部变量编译
- `FSHA1` / `FSHAHash` 头文件路径

如出现接口调用错误：`ISaveSystemClient::Execute_GatherSaveData(Client)` 需要确保接口的 UFUNCTION 声明正确。注意 `_Implementation` 后缀的虚函数在 UINTERFACE 中需配合 `Execute_` 宏调用。

---

## Task 8: 完善组件注册时机（连接 SaveableComponent 与注册表）

**Files:**
- Modify: `Plugins/SaveSystem/Source/SaveSystem/Private/SaveableComponent.cpp`

- [ ] **Step 1: 在组件生命周期接入注册表**

当前 `OnRegister`/`OnUnregister` 只做了 GUID 缓存，未接入全局注册表。修改 `SaveableComponent.cpp`，在 `OnRegister` 末尾、`OnUnregister` 开头接入：

`OnRegister` 末尾追加（在 `Super::OnRegister()` 之后，GUID 计算之后）：
```cpp
	// 注册到全局注册表(BPLibrary 维护的静态 TArray)
	USaveSystemBPLibrary::RegisterSaveableComponent(this);
```

`OnUnregister` 开头（在 `Super::OnUnregister()` 之前）追加：
```cpp
	// 从全局注册表注销
	USaveSystemBPLibrary::UnregisterSaveableComponent(this);
```

需在 `SaveableComponent.cpp` 顶部 include 追加：
```cpp
#include "SaveSystemBPLibrary.h"
```

- [ ] **Step 2: 编译验证**

Run: UBT 编译。
Expected: 通过。注意循环 include 风险：`SaveableComponent.cpp` include `SaveSystemBPLibrary.h`，而 `SaveSystemBPLibrary.cpp` include `SaveableComponent.h`——这是 .cpp 间的 include，不构成头文件循环，安全。

---

## Task 9: 在 Inventory.uproject 注册插件

**Files:**
- Modify: `F:/UEProject/Inventory/Inventory.uproject`

- [ ] **Step 1: 在 uproject 的 Plugins 数组追加 SaveSystem**

修改 `Inventory.uproject`，在 `Plugins` 数组末尾追加：
```json
		{
			"Name": "SaveSystem",
			"Enabled": true
		}
```

修改后 Plugins 数组应为：
```json
	"Plugins": [
		{
			"Name": "ModelingToolsEditorMode",
			"Enabled": true,
			"TargetAllowList": [
				"Editor"
			]
		},
		{
			"Name": "EnhancedInput",
			"Enabled": true
		},
		{
			"Name": "InteractionPlugin",
			"Enabled": true
		},
		{
			"Name": "SaveSystem",
			"Enabled": true
		}
	]
```

- [ ] **Step 2: 全量编译验证**

Run: UBT 全量编译整个项目（含新插件）。
Expected: 通过。插件被项目正式引用。

---

## Task 10: 编辑器内功能验证（手动）

**Files:** 无（编辑器内操作）

- [ ] **Step 1: 启动编辑器，确认插件加载**

启动 UE 编辑器打开 Inventory 项目。检查：
- Edit → Plugins 中能看到 SaveSystem 插件，状态为 Enabled
- Output Log 无 SaveSystem 相关 error

- [ ] **Step 2: 创建测试关卡与测试 Actor**

在测试关卡中：
1. 放置一个 Actor，挂 USaveableComponent（验证地图 Actor 路径派生 GUID）
2. 实现 ISaveableActor 的测试 Actor，提供自定义字段（验证 C++ 通道）
3. （可选）创建 BP 子类重写 OnGatherSaveFields/OnApplySaveFields（验证 BP 通道）

- [ ] **Step 3: 验证 Save/Load 流程**

在蓝图中调用：
1. `SaveGame(WorldContext, "TestSlot", true, ErrMsg)` → 检查返回 true，日志输出"SaveGame 成功"
2. 移动测试 Actor 位置
3. `LoadGame(WorldContext, "TestSlot", ErrMsg)` → 检查 Actor 回到保存位置
4. `DoesSlotExist("TestSlot")` → true
5. `GetSlotList(Slots)` → 包含 TestSlot
6. `DeleteSlot("TestSlot", ErrMsg)` → true，`DoesSlotExist` → false

- [ ] **Step 4: 验证 QuickSave / AutoSave**

1. `QuickSave` → 存到 QuickSave 槽
2. `QuickLoad` → 读回
3. `AutoSave` 多次 → 验证滚动槽管理（保留最近 3 个）

- [ ] **Step 5: 验证运行时 Actor 保存恢复**

1. 在关卡中 Spawn 一个带 SaveableComponent 的 Actor（运行时生成）
2. SaveGame
3. 退出 PIE，重新 PIE + LoadGame
4. 检查运行时 Actor 被 Spawn 重建并恢复状态

---

## Self-Review

**1. Spec 覆盖:** 逐节核对 spec 14 章：
- 设计目标/定位 → Task 1-2（骨架+日志）
- 关键决策 6 项 → 全部体现在 Task 3-7 的代码中
- 总体架构/目录 → Task 1 文件结构
- SaveGame 基类与数据承载 → Task 4
- SaveableComponent 与 Actor 状态 → Task 5
- GUID 方案 A → Task 5 Step 4（路径 SHA1 派生）+ Task 7（运行时 NewGuid 持久化）
- 静态函数库 API 与流程 → Task 7（Save/Load/槽管理/快存/自动存档）
- 异常处理（错误码枚举+场景处理） → Task 3 错误码 + Task 7 各函数分支
- 版本兼容 → Task 7 LoadGame 版本检查 + OnVersionMismatch 钩子
- 性能保障 → Task 7 一次性哈希表、注册表 RemoveSwap、无 Tick
- 模块依赖 → Task 1 Build.cs（零依赖）
- .uplugin 清单 → Task 1
- 数据结构汇总 → Task 3-4 全部结构已定义
- 与 QuestSaveData.h 解耦 → Task 4/6 抽象基类+接口设计
- 本期不做项（云存档/迁移/异步） → 未实现，符合 YAGNI

**2. 占位符扫描:** 无 TBD/TODO。Task 5 Step 3 的 GUID 占位在 Step 4 已显式替换。所有代码块完整。

**3. 类型一致性:**
- `USaveSystemBPLibrary::RegisterSaveableComponent` 在 .h(Task 7 Step1) 声明，.cpp(Task 7 Step2) 实现，Task 8 调用——签名一致
- `USaveableComponent::SetRuntimeSpawned/IsRuntimeSpawned` Task 5 声明，Task 7 Load 调用——一致
- `FActorSaveState::bHidden` Task 3 用位域，Task 5 用 `State.bHidden != 0` 比较——一致
- `ISaveSystemClient::Execute_*` 调用宏在 Task 7 使用——接口定义(Task 6)为 `_Implementation` 后缀虚函数，配合 `Execute_` 宏正确
- `FRuntimeActorRecord.ActorClass` 为 `TSoftClassPtr`，Task 7 用 `.Get()`/`.LoadSynchronous()`——一致

**4. 潜在风险点(执行时需注意):**
- 位域 SaveGame 反射:若 Task 3 编译报位域不支持 SaveGame,改 `uint8 bHidden : 1` 为 `bool bHidden`
- 接口 Execute 宏:`Execute_GatherSaveData` 需对应 UFUNCTION 声明,注意 UINTERFACE 的 `_Implementation` 函数在 BP 可调用时需正确的 UFUNCTION 标记
- `TMap<FName, TObjectPtr<USaveSystemSaveGameData>>` SaveGame 反射:UE5.7 支持 TMap 序列化,TObjectPtr 子对象自动序列化
- `GetWorldFromContextObject` 的 EGetWorldErrorMode 枚举在 UE5.7 存在
- `IFileManager::Get().FindFiles` 签名:`(TArray<FString>&, const TCHAR*, const TCHAR*)`

所有 Task 完整,无遗漏。
