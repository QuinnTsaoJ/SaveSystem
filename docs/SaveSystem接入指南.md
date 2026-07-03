# SaveSystem 接入与使用指南

> **轻量零依赖** · **零侵入** · **即插即用**
>
> 插件只依赖 UE 核心模块（Core/CoreUObject/Engine），不依赖 GAS、GameplayTags 或任何第三方。复用 UE 原生 `USaveGame` 反射序列化，**不写自研序列化器**。任何 Actor 挂上 `USaveableComponent` 即参与存档，**无需继承特定基类**。

---

## 目录

- [前置条件](#前置条件)
- [架构速览](#架构速览)
- [第一步：让 Actor 参与存档](#第一步让-actor-参与存档)
- [第二步：自定义 Actor 存档内容（可选）](#第二步自定义-actor-存档内容可选)
  - [方式 A：C++ 实现 ISaveableActor 接口](#方式-ac实现-isaveableactor-接口)
  - [方式 B：Blueprint 实现接口](#方式-bblueprint-实现接口)
  - [方式 C：强类型数据载体](#方式-c强类型数据载体复杂数据无需转字符串)
- [第三步：存档与加载（基础用法）](#第三步存档与加载基础用法)
- [第四步：接入领域系统数据（可选，进阶）](#第四步接入领域系统数据可选进阶)
  - [典型场景：接入 QuestSystem 的任务数据](#典型场景接入-questsystem-的任务数据)
  - [典型场景：接入 InventorySystem 的背包数据](#典型场景接入-inventorysystem-的背包数据)
    - [方式一：C++ 实现](#方式一c实现)
    - [方式二：Blueprint 实现](#方式二blueprint-实现零-c-模块依赖)
- [第五步：快速存档与自动存档](#第五步快速存档与自动存档)
- [API 速查表](#api-速查表)
- [错误处理](#错误处理)
- [版本兼容](#版本兼容)
- [性能特性](#性能特性)
- [完整流程示例](#完整流程示例一个最简存档场景)
- [FAQ](#faq)
- [不在本期范围（V1.0）](#不在本期范围v10)

---

## 前置条件

1. 插件 **SaveSystem** 已在编辑器启用（插件管理器勾选，或 `Inventory.uproject` 的 Plugins 数组中 `"Enabled": true`）。
2. 你了解 UE 原生存档的基本概念（`USaveGame`、`UGameplayStatics::SaveGameToSlot`）。本插件在此基础上封装了槽管理、Actor 状态收集/恢复、领域数据注入。

> **核心概念**：插件只管**存档读写与对象状态搬运**，不关心你的业务数据是什么。
>
> - Actor 的状态由 Actor 自己决定存什么（实现 `ISaveableActor` 接口）
> - 领域系统（如任务/背包）的数据通过 `ISaveSystemClient` 接口注入
>
> 两者完全解耦。

---

## 架构速览

```
游戏层
  │
  ├── Actor 挂 USaveableComponent ──→ 自动注册,提供状态收集/恢复回调
  │
  ├── 领域系统实现 ISaveSystemClient ──→ 注入/取出领域数据
  │
  └── 调用 USaveSystemBPLibrary ──→ 唯一入口(Save/Load/槽管理/快存/自动存档)
                    │
                    ▼
              USaveSystemSaveGame(存档对象)
              ├── SlotMeta            插槽元数据(时间/版本/关卡)
              ├── LevelActorRecords   地图 Actor 状态记录
              ├── RuntimeActorRecords 运行时 Actor 记录(含 Class/Transform)
              └── ClientData          领域数据注入区(ClientName → 子对象)
                    │
                    ▼
        UGameplayStatics::SaveGameToSlot(复用 UE 原生反射序列化)
```

**GUID 机制（方案 A）**：

| Actor 类型 | GUID 来源 | 是否持久化 |
|---|---|---|
| 地图原生 Actor | 放置路径派生（`GetPathName()` 哈希） | 免持久化，同一路径永远算出同一 GUID |
| 运行时 Spawn Actor | `FGuid::NewGuid()` | 随存档记录持久化 |

加载时按 GUID 路由恢复。

---

## 第一步：让 Actor 参与存档

任何需要存档的 Actor，挂上 `USaveableComponent` 即可。**挂上就生效**，组件会在注册时自动加入全局注册表，无需手动调用。

**C++ 方式**：

```cpp
// MyCharacter.h
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SaveableComponent.h"  // 插件头文件
#include "MyCharacter.generated.h"

UCLASS()
class AMyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AMyCharacter();

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Save")
    USaveableComponent* SaveableComp;
};

// MyCharacter.cpp 构造函数
AMyCharacter::AMyCharacter()
{
    SaveableComp = CreateDefaultSubobject<USaveableComponent>(TEXT("SaveableComp"));
}
```

**蓝图方式**：Actor 蓝图的 Components 面板 → Add → 搜 `SaveableComponent`。

挂上后，该 Actor 的 **Transform 和可见性**会被插件默认采集与恢复。若只需存位置，到此为止即可。

---

## 第二步：自定义 Actor 存档内容（可选）

默认只存 Transform + 可见性。若要存 Actor 的自定义数据，让 Actor 实现 `ISaveableActor` 接口，有两条通道可选：

| 通道 | 方法 | 数据载体 | 适用场景 |
|---|---|---|---|
| **字符串通道** | `GatherSaveFields` / `ApplySaveFields` | `TMap<FString, FString>` | 血量/等级/开关等简单标量 |
| **强类型通道** | `GatherActorData` / `ApplyActorData` | `USaveSystemSaveGameData` 子对象 | 背包/掉落表等复杂结构，无需转字符串 |

C++ 与 Blueprint 走同一接口，组件内部统一调用。两条通道可并存（简单标量走字符串，复杂结构走强类型）。

> **关键**：`UPROPERTY(SaveGame)` 标记在 Actor 字段上**不会被插件自动采集**——插件只采集 Transform/可见性 + 你在回调里手动塞入的数据。这样设计是为了让 Actor **完全自控**存什么，避免插件反射扫描带来的不可预期。

### 方式 A：C++ 实现 ISaveableActor 接口

让你的 Actor 实现 `ISaveableActor`，在两个回调里收集/恢复自定义字段。字段用 `TMap<FString, FString>` 承载——简单通用，值需自行转为字符串。

```cpp
// MyCharacter.h
#include "ISaveableActor.h"  // 插件头文件

UCLASS()
class AMyCharacter : public ACharacter, public ISaveableActor
{
    GENERATED_BODY()

    // 你的业务字段
    UPROPERTY(SaveGame, BlueprintReadOnly, Category="Stats")
    int32 Health = 100;

    UPROPERTY(SaveGame, BlueprintReadOnly, Category="Stats")
    int32 Level = 1;

    UPROPERTY(SaveGame, BlueprintReadOnly, Category="Stats")
    bool bDoorOpen = false;

    // === ISaveableActor 实现 ===
    virtual void GatherSaveFields_Implementation(TMap<FString, FString>& OutFields) override;
    virtual void ApplySaveFields_Implementation(const TMap<FString, FString>& Fields) override;
};

// MyCharacter.cpp
void AMyCharacter::GatherSaveFields_Implementation(TMap<FString, FString>& OutFields)
{
    // 把要持久化的字段塞进 OutFields(键名自定义,恢复时按名取)
    OutFields.Add(TEXT("Health"), FString::FromInt(Health));
    OutFields.Add(TEXT("Level"), FString::FromInt(Level));
    OutFields.Add(TEXT("bDoorOpen"), bDoorOpen ? TEXT("1") : TEXT("0"));
}

void AMyCharacter::ApplySaveFields_Implementation(const TMap<FString, FString>& Fields)
{
    // 从 Fields 取值恢复(注意判空,旧存档可能缺字段)
    if (const FString* p = Fields.Find(TEXT("Health"))) Health = FCString::Atoi(**p);
    if (const FString* p = Fields.Find(TEXT("Level")))  Level = FCString::Atoi(**p);
    if (const FString* p = Fields.Find(TEXT("bDoorOpen"))) bDoorOpen = (*p == TEXT("1"));
}
```

### 方式 B：Blueprint 实现接口

BP 接入同样走 `ISaveableActor` 接口——C++ 与 BP 统一通道，组件内部用 `Execute_` 宏调用，无需关心实现侧语言。

**步骤**：

1. **Actor 蓝图实现接口**：打开 Actor 蓝图 → Class Settings（类默认值）→ Details 面板 → Interfaces → Add → 搜 `SaveableActor` → 添加。
2. **Override 事件**：在蓝图事件图空白处右键 → 搜 `GatherSaveFields` → 选 **Override GatherSaveFields**（同理 `ApplySaveFields`）。事件节点会出现。
3. **填写逻辑**：`GatherSaveFields` 事件有一个 `OutFields` 引用引脚（传入/传出 Map），往里 Add 键值对；`ApplySaveFields` 事件有一个 `Fields` 输入引脚，从中 Get/Find 取值恢复。

```
事件:GatherSaveFields (Override)
  OutFields(引脚) ──→ Make Map / Add 节点 ──→ 回写 OutFields
    键 "Health"   ← FString(Health变量)
    键 "bDoorOpen" ← Select(bDoorOpen, "1","0")

事件:ApplySaveFields (Override)
  Fields(引脚) ──→ Map Find 节点 ──→ 赋值给变量
    Find "Health"   → ToInt → Set Health
    Find "bDoorOpen" → == "1" → Set bDoorOpen
```

> **为何不能在 SaveableComponent 上重写事件？**
>
> `OnGatherSaveFields` 早期版本曾放在组件上作为 `BlueprintNativeEvent`，但 UE 的 `BlueprintNativeEvent` **只能在声明该函数的类的 BP 子类里被 Override**——不能从"挂载该组件的 Actor 蓝图"里 Override。当前版本已移除组件上的事件，统一走 `ISaveableActor` 接口。Actor 实现接口后，接口事件可在 Actor 蓝图事件图直接 Override，这才是正确的 BP 接入路径。

> **TMap 在 Blueprint 里的操作**：用 Make/Break Map 节点。`OutFields` 作为引用输出引脚，需通过 Add 节点向其写入后再传出（UE 会自动回写）。

### 方式 C：强类型数据载体（复杂数据无需转字符串）

当 Actor 身上有复杂数据（如宝箱的掉落表 `TArray<FItemStack>`、NPC 的对话历史），用字符串转换很痛苦。强类型通道让 Actor 直接返回一个 `USaveSystemSaveGameData` 子对象，内含强类型 `UPROPERTY(SaveGame)` 字段，UE 原生反射自动序列化。

**1. 定义 Actor 数据载体（继承插件基类）**

```cpp
// ChestSaveGameData.h
#include "SaveSystemSaveGame.h"  // 插件基类
#include "ChestSaveGameData.generated.h"

UCLASS()
class UChestSaveGameData : public USaveSystemSaveGameData
{
    GENERATED_BODY()
public:
    // 直接放强类型字段,数组/结构体/Map 都行,UE 自动序列化,不转字符串
    UPROPERTY(SaveGame, BlueprintReadWrite, Category="Chest")
    TArray<FItemStack> LootItems;

    UPROPERTY(SaveGame, BlueprintReadWrite, Category="Chest")
    bool bOpened = false;
};
```

**2. Actor 实现 ISaveableActor 的强类型方法**

```cpp
// MyChest.h
UCLASS()
class AMyChest : public AActor, public ISaveableActor
{
    GENERATED_BODY()

    UPROPERTY(SaveGame, BlueprintReadOnly, Category="Chest")
    TArray<FItemStack> LootItems;

    UPROPERTY(SaveGame, BlueprintReadOnly, Category="Chest")
    bool bOpened = false;

    // 强类型通道
    virtual USaveSystemSaveGameData* GatherActorData_Implementation() override;
    virtual void ApplyActorData_Implementation(USaveSystemSaveGameData* Data) override;
};

// MyChest.cpp
USaveSystemSaveGameData* AMyChest::GatherActorData_Implementation()
{
    UChestSaveGameData* Data = NewObject<UChestSaveGameData>();
    Data->LootItems = LootItems;  // 直接赋值强类型,不转字符串
    Data->bOpened = bOpened;
    return Data;
}

void AMyChest::ApplyActorData_Implementation(USaveSystemSaveGameData* Data)
{
    if (UChestSaveGameData* ChestData = Cast<UChestSaveGameData>(Data))
    {
        LootItems = ChestData->LootItems;  // 直接取强类型恢复
        bOpened = ChestData->bOpened;
    }
}
```

**3. Blueprint 接入强类型通道**

Actor 蓝图实现 `ISaveableActor` 接口后，Override `GatherActorData` / `ApplyActorData` 事件：

- `GatherActorData` 事件有一个返回值引脚（类型 `Save System Save Game Data`，需选你的子类）
- `ApplyActorData` 事件有一个 `Data` 输入引脚，用 Cast To 节点转成你的子类后读取字段

> **强类型 Actor 通道 vs 领域数据通道（ISaveSystemClient）**
>
> | | 强类型 Actor 通道 (`GatherActorData`) | 领域数据通道 (`ISaveSystemClient`) |
> |---|---|---|
> | 数据归属 | 随 Actor 走 | 随系统走 |
> | Actor 销毁时 | 数据即销毁 | 数据不受影响 |
> | 适用 | Actor 身上的复杂数据（宝箱掉落表） | 系统级数据（背包/任务/养成） |

---

## 第三步：存档与加载（基础用法）

在任意蓝图或 C++ 中调用 `USaveSystemBPLibrary` 的静态函数。所有函数返回 `bool Success` + 输出 `FText OutErrorMessage`。

### 蓝图

搜索节点 `Save Game`（SaveSystem 分类）：

```
节点:Save Game (SaveSystem)
  WorldContextObject  ← self / GetWorld
  SlotName            ← "MySlot1"
  bOverride           ← true(已存在则覆盖)
  → Return Value: bool
  → Out ErrorMessage: FText
```

加载用 `Load Game`，槽管理用 `Does Slot Exist` / `Get Slot List` / `Delete Slot` / `Create Slot`。

### C++

```cpp
#include "SaveSystemBPLibrary.h"

// 保存
FText Err;
bool bOk = USaveSystemBPLibrary::SaveGame(this, TEXT("MySlot1"), true, Err);
if (!bOk)
{
    UE_LOG(LogTemp, Warning, TEXT("保存失败:%s"), *Err.ToString());
}

// 加载
bool bLoaded = USaveSystemBPLibrary::LoadGame(this, TEXT("MySlot1"), Err);

// 槽管理
bool bExists = USaveSystemBPLibrary::DoesSlotExist(TEXT("MySlot1"));
TArray<FSaveSlotMetadata> Slots;
USaveSystemBPLibrary::GetSlotList(Slots);  // 供存档列表 UI
USaveSystemBPLibrary::DeleteSlot(TEXT("MySlot1"), Err);
```

### 运行时 Actor（Spawn 出来的对象）

运行时 Spawn 的带 `SaveableComponent` 的 Actor **会被自动保存与恢复**——插件记录其 Class、Transform、状态，加载时自动 Spawn 重建并恢复状态。你无需做任何额外工作。

> **关键区别**：地图里摆好的 Actor 只存状态（它本来就在世界里）；运行时 Spawn 的 Actor 连"重新生成"都由插件负责。判断依据是组件的 `bRuntimeSpawned` 标记——加载流程 Spawn 出的 Actor 会自动置为 true。

---

## 第四步：接入领域系统数据（可选，进阶）

如果你的领域系统（任务、背包、角色养成等）有需要持久化的数据，且这些数据不在某个 Actor 上，而是散落在子系统/组件里——实现 `ISaveSystemClient` 接口注入。

### 典型场景：接入 QuestSystem 的任务数据

以现有 QuestSystem 为例（它有 `FQuestSaveData` 结构和 `QuestComponent::SaveToData()/LoadFromData()`）。接入步骤：

**1. 创建领域数据载体（继承 `USaveSystemSaveGameData`）**

```cpp
// QuestSaveGameData.h
#include "SaveSystemSaveGame.h"  // 插件基类
#include "QuestSaveData.h"       // QuestSystem 的现有数据结构(零改动)
#include "QuestSaveGameData.generated.h"

// 任务系统的存档数据载体(薄封装,内含现有的 FQuestSaveData)
UCLASS()
class UQuestSaveGameData : public USaveSystemSaveGameData
{
    GENERATED_BODY()
public:
    // 复用现有数据结构,SaveGame 标记由 UE 原生反射自动序列化
    UPROPERTY(SaveGame, BlueprintReadWrite, Category="Quest")
    FQuestSaveData QuestData;
};
```

**2. 让承载 QuestComponent 的对象实现 ISaveSystemClient**

```cpp
// MyPlayerController.h
#include "ISaveSystemClient.h"  // 插件接口
#include "QuestSaveGameData.h"

UCLASS()
class AMyPlayerController : public APlayerController, public ISaveSystemClient
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="Quest")
    UQuestComponent* QuestComp;

    // === ISaveSystemClient 实现 ===
    virtual FName GetClientName_Implementation() const override { return TEXT("Quest"); }
    virtual USaveSystemSaveGameData* GatherSaveData_Implementation() override;
    virtual void ApplySaveData_Implementation(USaveSystemSaveGameData* Data) override;
};

// MyPlayerController.cpp
USaveSystemSaveGameData* AMyPlayerController::GatherSaveData_Implementation()
{
    // 保存时:把任务状态封装进载体返回,插件会写入 ClientData["Quest"]
    UQuestSaveGameData* Data = NewObject<UQuestSaveGameData>();
    Data->QuestData = QuestComp->SaveToData();  // 复用现有接口
    return Data;
}

void AMyPlayerController::ApplySaveData_Implementation(USaveSystemSaveGameData* Data)
{
    // 加载时:插件从 ClientData["Quest"] 取出载体交给你恢复
    if (UQuestSaveGameData* QuestData = Cast<UQuestSaveGameData>(Data))
    {
        QuestComp->LoadFromData(QuestData->QuestData);  // 复用现有接口
    }
}
```

**3. 注册客户端**

在游戏初始化时（如 `BeginPlay` 或 `GameMode::InitGame`）调用一次：

```cpp
USaveSystemBPLibrary::RegisterSaveClient(this);  // this = 实现 ISaveSystemClient 的对象
```

游戏结束或对象销毁时反注册：

```cpp
USaveSystemBPLibrary::UnregisterSaveClient(this);
```

> ⚠️ **必须反注册，否则存档时崩溃**
>
> 注册表内部用弱引用持有客户端对象，遍历时会跳过已失效的条目。但**如果你注册的对象在 `EndPlay` 时没有调用 `UnregisterSaveClient`，下次 Save/Load 遍历到该失效条目时会触发空指针崩溃**（异常码 `0xc0000005`，DEP violation）。
>
> 典型踩坑场景：在 PlayerController 的 `BeginPlay` 里 `RegisterSaveClient(this)`，PIE 结束后 Controller 被 GC 回收，但没人反注册——下次 PIE 再调 Save 就崩。
>
> **正确做法**：成对调用，`BeginPlay` 注册，`EndPlay` 反注册：
>
> ```cpp
> void AMyPlayerController::EndPlay(const EEndPlayReason::Type Reason)
> {
>     USaveSystemBPLibrary::UnregisterSaveClient(this);
>     Super::EndPlay(Reason);
> }
> ```

完成后，`SaveGame`/`LoadGame` 会自动调用 `GatherSaveData`/`ApplySaveData`，任务数据随之持久化与恢复。**多个领域系统可各自实现接口并注册**，以 `ClientName` 区分（如 `"Quest"`、`"Inventory"`、`"PlayerStats"`），互不干扰。

> **解耦保证**：插件不 include QuestSystem，`QuestSaveData.h` 零改动。两者仅通过 `ISaveSystemClient` + `USaveSystemSaveGameData` 这对抽象耦合点协作。已有领域数据结构原样复用，作为 `SaveGame` 字段被 UE 原生反射自动序列化。

### 典型场景：接入 InventorySystem 的背包数据

背包数据（物品列表/快捷栏/格子尺寸）是"系统级"数据——不属于某个 Actor，而属于玩家的背包子系统。本场景演示如何把已有背包插件接入存档系统，**全程强类型，零字符串转换，背包插件源码零改动**。

> **前提**：InventorySystem 插件已提供 `FInventorySaveData`（强类型 USTRUCT，含 `Items`/`QuickSlots`/`GridSize`/`MaxWeight`）和 `UInventoryComponent::SaveToData()`/`LoadFromData()`（均为 `BlueprintCallable`）。接入 SaveSystem 只需复用这两个接口，不改背包插件源码。
>
> 数据载体（承载背包数据的 `USaveSystemSaveGameData` 子类）有两种实现方式，任选其一：
>
> | 方式 | 载体类型 | 模块依赖 | 适用 |
> |---|---|---|---|
> | **方式一** | C++ 类 | 游戏模块引用两个插件（合理） | C++ 项目，追求重构友好 |
> | **方式二** | BP 资产 | 零 C++ 模块依赖 | 不想碰 C++，不想让游戏模块 include 插件头 |

#### 方式一：C++ 实现

**1. 创建背包数据载体（继承插件基类）**

在**游戏模块**（不是背包插件）里新增头文件。这样插件间零依赖，游戏模块承担组合职责。数据载体是一个薄封装，内含背包插件已有的 `FInventorySaveData` 强类型字段：

```cpp
// InventorySaveGameData.h
#include "SaveSystemSaveGame.h"   // SaveSystem 插件基类
#include "FInventorySaveData.h"   // 背包插件已有结构(零改动)
#include "InventorySaveGameData.generated.h"

// 背包系统的存档数据载体
UCLASS()
class UInventorySaveGameData : public USaveSystemSaveGameData
{
    GENERATED_BODY()
public:
    // 直接放强类型字段,UE 原生反射自动序列化,不转字符串
    UPROPERTY(SaveGame, BlueprintReadWrite, Category="Inventory")
    FInventorySaveData InventoryData;
};
```

**2. 让持有背包的对象实现 ISaveSystemClient**

通常是 PlayerController 或 Character。以 PlayerController 为例：

```cpp
// MyPlayerController.h
#include "GameFramework/PlayerController.h"
#include "ISaveSystemClient.h"         // SaveSystem 插件接口
#include "InventorySaveGameData.h"
#include "InventoryComponent.h"        // 背包插件组件
#include "MyPlayerController.generated.h"

UCLASS()
class AMyPlayerController : public APlayerController, public ISaveSystemClient
{
    GENERATED_BODY()

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, Category="Inventory")
    UInventoryComponent* InventoryComp;

    // === ISaveSystemClient 实现 ===
    virtual FName GetClientName_Implementation() const override { return TEXT("Inventory"); }
    virtual USaveSystemSaveGameData* GatherSaveData_Implementation() override;
    virtual void ApplySaveData_Implementation(USaveSystemSaveGameData* Data) override;
};

// MyPlayerController.cpp
void AMyPlayerController::BeginPlay()
{
    Super::BeginPlay();
    // 注册背包客户端(只注册一次)
    USaveSystemBPLibrary::RegisterSaveClient(this);
}

USaveSystemSaveGameData* AMyPlayerController::GatherSaveData_Implementation()
{
    // 保存时:复用背包已有的 SaveToData(),装入强类型载体
    UInventorySaveGameData* Data = NewObject<UInventorySaveGameData>();
    Data->InventoryData = InventoryComp->SaveToData();  // 强类型赋值,不转字符串
    return Data;
}

void AMyPlayerController::ApplySaveData_Implementation(USaveSystemSaveGameData* Data)
{
    // 加载时:Cast 取出强类型数据,复用背包已有的 LoadFromData()
    if (UInventorySaveGameData* InvData = Cast<UInventorySaveGameData>(Data))
    {
        InventoryComp->LoadFromData(InvData->InventoryData);  // 强类型恢复,不转字符串
    }
}
```

**3. 完成**

无需第三步。注册后，`SaveGame`/`LoadGame` 会自动调用 `GatherSaveData`/`ApplySaveData`，背包数据随存档持久化与恢复。

#### 方式二：Blueprint 实现（零 C++ 模块依赖）

不想新增 C++ 代码、不想让游戏模块 include 两个插件头文件？数据载体可以完全用蓝图实现——BP 类放在 Content 目录，不属任何 C++ 模块，插件间零依赖，不写一行 C++。

> **可行性依据**：`FInventorySaveData` 是 `BlueprintType` USTRUCT，`SaveToData()`/`LoadFromData()` 均为 `BlueprintCallable`，BP 可直接调用。

**1. 创建背包数据载体 BP 类**

1. Content Browser → 右键 → Blueprint Class → 搜父类 `SaveSystemSaveGameData`（插件基类）→ 命名 `BP_InventorySaveGameData`。
2. 打开 `BP_InventorySaveGameData` → Variables 面板 → Add → 命名 `InventoryData`。
3. 变量类型点选 → 搜 `Inventory Save Data`（`FInventorySaveData`，背包插件导出的 BP 可见结构）。
4. **关键**：变量 Details 面板确认 **SaveGame** 复选框勾上——这决定该字段是否被 UE 原生反射序列化。

```
BP_InventorySaveGameData (继承 SaveSystemSaveGameData)
  └── 变量 InventoryData : FInventorySaveData  [SaveGame ✓]
```

**2. PlayerController BP 实现 ISaveSystemClient 接口**

1. 打开 PlayerController 蓝图 → **Class Settings**（类默认值）→ Details → Interfaces → Add → 搜 `SaveSystemClient` → 添加。
2. 接口添加后，事件图可 Override 三个函数：`GetClientName`、`GatherSaveData`、`ApplySaveData`。

**3. Override GetClientName 事件**

返回固定字符串 `"Inventory"`（纯文本）：

```
事件:GetClientName (Override)
  → Return Node → "Inventory"
```

**4. Override GatherSaveData 事件（保存时收集背包数据）**

调用背包的 `SaveToData`，装入 BP 数据载体，返回给存档系统：

```
事件:GatherSaveData (Override)
  │
  ├─ self → Get InventoryComp → SaveToData() → 得到 FInventorySaveData
  │
  ├─ Construct Object from Class
  │     Class = BP_InventorySaveGameData
  │     Outer = self
  │     → 得到 BP_InventorySaveGameData 实例
  │
  ├─ Set InventoryData(把 SaveToData 结果塞进载体的 InventoryData 变量)
  │
  └─ Return Node → 返回载体对象(Cast 为 SaveSystemSaveGameData 自动完成)
```

**5. Override ApplySaveData 事件（加载时恢复背包数据）**

从存档系统收到的载体 Cast 取出背包数据，调 `LoadFromData` 恢复：

```
事件:ApplySaveData (Override)
  输入引脚 Data : SaveSystemSaveGameData
  │
  ├─ Cast To BP_InventorySaveGameData(Data)
  │     成功 → Get InventoryData → 得到 FInventorySaveData
  │           → self → Get InventoryComp → LoadFromData(FInventorySaveData)
  │     失败 → (什么都不做,或记一条警告日志)
```

**6. 注册客户端**

PlayerController BP 的 `Event BeginPlay` 里调一次：

```
Event BeginPlay
  → Register Save Client(Client = self)
```

> ⚠️ **必须配对反注册，否则存档时崩溃**
>
> 在 `Event EndPlay` 里务必调 `Unregister Save Client(Client = self)`。注册表用弱引用持有对象，但**不反注册会导致下次 Save/Load 遍历到失效条目时空指针崩溃**（`0xc0000005`）。详见第四步 QuestSystem 接入的注意事项。

```
Event EndPlay
  → Unregister Save Client(Client = self)
```

> **方式一 vs 方式二怎么选？**
>
> | 维度 | 方式一（C++） | 方式二（BP） |
> |---|---|---|
> | 模块依赖 | 游戏模块引用两插件 | 零 C++ 模块依赖 |
> | 字段定义 | C++ 显式声明，IDE 悬停可见 | BP 变量，编辑器内定义 |
> | 重构友好 | 高（改名安全） | 中（BP 改动需重编译） |
> | 代码量 | 新增 1 个 C++ 类 | 新增 1 个 BP 资产 + BP 逻辑 |
>
> 两种方式的运行时行为完全一致——最终都生成一个 `USaveSystemSaveGameData` 子对象，字段被 UE 原生反射序列化。

> **加载时数据怎么找到背包？**
>
> 不用你手动找。`LoadGame` 按 `ClientName` 自动路由分发——保存时以 `"Inventory"` 为键存入的数据，加载时自动分发给注册为 `"Inventory"` 的客户端。你拿到的就是原来的 `FInventorySaveData` 强类型数据。

> **多系统并存**：多个领域系统可各自实现接口并注册，以 `ClientName` 区分。例如同时注册 `"Inventory"`（背包）、`"Quest"`（任务）、`"PlayerStats"`（养成），互不干扰。

---

## 第五步：快速存档与自动存档

### 快速存档

固定槽名 `QuickSave`，覆盖式：

```cpp
USaveSystemBPLibrary::QuickSave(this, Err);  // 存
USaveSystemBPLibrary::QuickLoad(this, Err);  // 读
```

### 自动存档

`AutoSave` 内部维护滚动槽（槽名 `AutoSave_001` 起递增），**保留最近 3 个**，超出删最旧：

```cpp
USaveSystemBPLibrary::AutoSave(this, Err);
```

> **时基由你提供**：插件是纯静态函数库，无 Tick。自动存档的触发时机由游戏层决定——在 GameMode/PlayerController 的定时器或事件回调（战斗结束、关卡切换）里调用 `AutoSave` 即可。
>
> ```cpp
> // MyPlayerController.cpp
> FTimerHandle AutoSaveTimer;
> GetWorldTimerManager().SetTimer(AutoSaveTimer, [this]()
> {
>     FText Err;
>     USaveSystemBPLibrary::AutoSave(this, Err);
> }, 120.f, true);  // 每 120 秒自动存档一次
> ```

---

## API 速查表

### 存档槽管理

| 函数 | 说明 | 关键参数 |
|---|---|---|
| `SaveGame` | 保存到指定槽（收集 Actor + 领域数据） | SlotName, bOverride |
| `LoadGame` | 从指定槽加载（恢复 Actor + 注入领域数据） | SlotName |
| `CreateSlot` | 创建空槽（只写元数据） | SlotName, DisplayName |
| `DeleteSlot` | 删除存档槽 | SlotName |
| `DoesSlotExist` | 查询槽是否存在 | SlotName |
| `GetSlotList` | 枚举所有槽元数据（供存档列表 UI） | OutSlots |

### 快存与自动存档

| 函数 | 说明 |
|---|---|
| `QuickSave` / `QuickLoad` | 固定槽名 `QuickSave`，覆盖式 |
| `AutoSave` | 滚动槽 `AutoSave_001`...，保留最近 3 个 |

### 领域客户端

| 函数 | 说明 |
|---|---|
| `RegisterSaveClient` | 注册实现 `ISaveSystemClient` 的领域系统 |
| `UnregisterSaveClient` | 注销 |

### 事件委托（BP 可绑定）

| 委托 | 触发时机 | 参数 |
|---|---|---|
| `OnSaveComplete` | 保存流程结束（成功/失败） | SlotName, bSuccess |
| `OnLoadComplete` | 加载流程结束（成功/失败） | SlotName, bSuccess |
| `OnVersionMismatch` | 加载时检测到插件版本不匹配（不阻断） | SlotName, SavedVersion, CurrentVersion |

C++ 绑定：

```cpp
USaveSystemBPLibrary::OnSaveComplete().AddDynamic(this, &AMyActor::HandleSaveComplete);
```

---

## 错误处理

所有函数返回 `bool` 表示成功与否，失败时 `OutErrorMessage` 返回中文错误文本。插件**任何情况下不会导致游戏崩溃**——错误统一记日志（`LogSaveSystem` 分类）并返回错误码。

错误码枚举 `ESaveSystemError`：

| 错误码 | 含义 | 是否阻断 |
|---|---|---|
| `None` | 成功 | — |
| `InvalidWorld` | WorldContext 无效 | 阻断 |
| `InvalidSlotName` | 槽名为空或非法 | 阻断 |
| `SlotNotFound` | 槽不存在 | 阻断 |
| `SlotAlreadyExists` | 槽已存在且未允许覆盖 | 阻断 |
| `WriteFailed` | 磁盘写入失败（权限/空间） | 阻断 |
| `ReadFailed` | 读取失败（文件损坏） | 阻断 |
| `VersionMismatch` | 版本不兼容 | 不阻断（留迁移钩子） |
| `LevelMismatch` | 关卡不匹配（警告级） | 不阻断 |
| `NoSaveableComponents` | 无可保存对象（允许空存档） | 不阻断 |

**非阻断情况**会在 Output Log 输出 `Warning`，但不影响存档/加载完成。例如跨关卡加载时，存档里的地图 Actor 在当前关卡找不到，会跳过该条记录并告警，不崩溃。

---

## 版本兼容

存档元数据记录 `EngineVersion` 与 `PluginVersion`。加载时比对：

| 情况 | 处理 |
|---|---|
| 完全匹配 | 直接加载 |
| `PluginVersion` 不同 | 记 Warning，广播 `OnVersionMismatch`（游戏层可监听做数据迁移），**不阻断加载** |
| `EngineVersion` 大幅差异 | 记 Warning，仍尝试加载 |

> 本期 V1.0 **不内置自动迁移**。如需迁移，订阅 `OnVersionMismatch`，在回调里根据 `SavedVersion` 做字段补全/转换。

---

## 性能特性

| 特性 | 说明 |
|---|---|
| 无 Tick | SaveableComponent 不开 Tick；静态库无定时器 |
| 注册表 O(1) 注销 | 全局注册表用 `TArray` + `RemoveSingle` |
| 加载一次性哈希表 | 恢复地图 Actor 时先建 `TMap<GUID, Actor*>` 索引，O(1) 查找 |
| GUID 路径派生缓存 | 地图 Actor 的 GUID 在 `OnRegister` 时算一次缓存，不重复计算 |
| 同步 IO | 本期用 UE 原生同步 `SaveGameToSlot`/`LoadGameFromSlot`；1000 Actor 量级数据（<1MB）在 SSD 上远低于 20ms |

性能目标（1000 Saveable Actor）：扫描 ≤5ms，保存 ≤20ms，加载 ≤30ms。若实测超标，后续版本可切异步保存。

---

## 完整流程示例：一个最简存档场景

**目标**：主角位置 + 血量持久化，运行时 Spawn 的敌人也能存档恢复。

1. **主角蓝图**：挂 `SaveableComponent` → 实现 `ISaveableActor` 接口 → Override `GatherSaveFields` 塞入 `Health` → Override `ApplySaveFields` 恢复 `Health`。
2. **敌人蓝图**（运行时 Spawn）：挂 `SaveableComponent` 即可（位置自动存；血量同理实现接口 Override 事件）。
3. **存档点触发器**：Overlap 时调 `SaveGame(WorldContext, "Checkpoint1", true, Err)`。
4. **读档菜单**：`GetSlotList(Slots)` 填充 UI → 选中后 `LoadGame(WorldContext, Slots[i].SlotName, Err)`。

**运行验证**：

- 存档后移动主角 → 读档 → 主角回到存档位置 + 血量恢复
- 存档时存在的运行时敌人 → 读档后自动 Spawn 重建 + 状态恢复
- 退出 PIE 重进 → 调 `DoesSlotExist` 确认存档仍在

---

## FAQ

**Q1：为什么我的 `UPROPERTY(SaveGame)` 字段没被存下来？**

插件**不反射扫描** Actor 字段。`SaveGame` 标记在 Actor 字段上只是 UE 惯例提示，不会被插件自动采集——实际持久化由你决定：

- 简单标量：在 `GatherSaveFields` 手动塞入字符串键值对
- 复杂结构：在 `GatherActorData` 返回 `USaveSystemSaveGameData` 子对象，子对象里的 `UPROPERTY(SaveGame)` 字段才由 UE 原生反射自动序列化

这是为了让 Actor 完全自控存什么，避免插件扫描存到不该存的东西。

**Q2：背包/任务这类复杂数据要不要转字符串？**

**不要。** 两条强类型路径都不需要转字符串：

- 数据属于某个 Actor（如宝箱的掉落表）：走 `ISaveableActor::GatherActorData`，返回含 `UPROPERTY(SaveGame) TArray<FItemStack>` 的子对象，UE 自动序列化
- 数据属于系统（如玩家背包、任务进度）：走 `ISaveSystemClient::GatherSaveData`，返回含强类型字段的子对象

字符串通道（`GatherSaveFields`）只建议给血量/等级/开关这类简单标量用。

**Q3：地图 Actor 改了名字/删了，读档会怎样？**

地图 Actor 的 GUID 由放置路径派生。Actor 删除后，读档时按 GUID 找不到对应 Actor，会跳过该记录并记 Warning，**不崩溃**。改名若导致 `GetPathName()` 变化，同样会找不到。

**Q4：运行时 Spawn 的 Actor 读档后会被重复 Spawn 吗？**

不会。读档时先 Spawn 重建（此时世界里没有该 Actor），再恢复状态。原 Actor 在上次会话已随关卡卸载销毁，不存在重复。

**Q5：多个关卡怎么处理？**

存档元数据记录 `LevelName`。跨关卡读档会记 Warning（地图 Actor 可能找不到），但允许加载。运行时 Actor 不受影响（会重新 Spawn）。若需严格的关卡绑定，在调用 `LoadGame` 前自行校验 `GetSlotList` 返回的 `LevelName`。

**Q6：自动存档多久存一次？**

由你决定。插件不提供定时器，`AutoSave` 是"被调用即存"。在 GameMode/PlayerController 的定时器或事件里按需调用，间隔自定。

**Q7：能存玩家输入设置/键位映射吗？**

不在本插件职责内。这类配置建议用 UE 的 `UPlayerInput::SaveConfig` 或自定义 `USaveGame` 单独管理。本插件聚焦游戏状态（Actor 状态 + 领域数据）。

**Q8：存档文件在哪？**

UE 默认存到 `项目目录/Saved/SaveGames/<SlotName>.sav`。`GetSlotList` 扫描的就是这个目录。

**Q9：支持多平台吗？**

插件是 Runtime 模块，仅用 UE 核心 API，理论上支持所有 UE 支持的平台。当前项目目标平台为 Windows + Android。存档路径由 UE 的 `FPaths::ProjectSavedDir()` 决定，各平台自动适配。

**Q10：保存游戏时崩溃（0xc0000005），调用栈指向 `USaveSystemBPLibrary::SaveGame`？**

99% 是注册的 Save Client 或 SaveableComponent 对象已销毁但未反注册。注册表内部用弱引用持有对象，遍历时会跳过已失效条目——但如果你注册的对象在 `EndPlay` 时没有调用 `UnregisterSaveClient`/`UnregisterSaveableComponent`，残留条目会导致崩溃。

**修复**：在 `BeginPlay` 注册，在 `EndPlay` 反注册，成对调用：

```cpp
// C++
void AMyPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    USaveSystemBPLibrary::UnregisterSaveClient(this);
    Super::EndPlay(Reason);
}
```

```
// Blueprint
Event EndPlay → Unregister Save Client(Client = self)
```

**Q11：LoadGame 加载成功，但领域数据（背包/任务）没恢复？**

99% 是 **Load 时机太早**——Load 触发时 `ISaveSystemClient` 还没注册。典型场景：在 GameMode 的 `BeginPlay` 里检测到存档就调 `LoadGame`，但此时 PlayerController 还没执行 `BeginPlay`，`RegisterSaveClient` 还没调用，`GetClientRegistry()` 为空，`ApplySaveData` 没有客户端可分发。

**修复**：把 Load 挪到 `PlayerController` 的 `BeginPlay` 里，在 `RegisterSaveClient` **之后**调用：

```
PlayerController BP · Event BeginPlay:
  ├─ Register Save Client(self)              ← 先注册
  ├─ Does Slot Exist("MySlot")
  │     ├─ true  → LoadGame(self, "MySlot", Err)   ← 再加载
  │     └─ false → (新游戏,跳过)
```

不要在 GameMode 启动阶段调 Load——那时 PC 还没就绪。

**Q12：调试时看到注册表里有多个 PlayerController / Actor？**

PIE 多轮启动会累积残留。注册表是 `static` 变量，PIE 结束不自动清理。每跑一轮 PIE，上一轮的 SaveableComponent 变成失效弱引用（被 `IsValid()` 跳过），但不会自动移除。

**当前版本已自动清理**：Save/Load 每次遍历注册表后会 `RemoveAll` 失效条目，保持注册表干净。如果你看到日志里 Save 收集了正确的 Actor 数量（如 `收集 Actor=2`），说明清理已生效，残留不影响功能。

---

## 不在本期范围（V1.0）

以下功能 spec 已显式划为本期不做，留待后续版本：

- 云存档、网络同步、数据加密、数据压缩
- 世界分块保存、大型 MMO 数据管理
- 自动数据迁移（仅留 `OnVersionMismatch` 钩子）
- 异步保存（后台线程）
- 缩略图生成（仅留 `ThumbnailPath` 字段占位）
- 多关卡流式加载的 GUID 冲突处理

---

## 相关文件

- 设计文档：`docs/superpowers/specs/2026-06-27-savesystem-design.md`
- 实现计划：`docs/superpowers/plans/2026-06-28-savesystem-implementation.md`
- 插件源码：`Source/SaveSystem/`（Public/Private）

如需更多示例（如完整的测试 Actor、Quest 接入示例工程），联系插件作者：一氧化二氢
