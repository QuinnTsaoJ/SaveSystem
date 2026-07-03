# SaveSystem 轻量级通用存档插件设计文档（V1.0）

## 文档信息

- 项目名称：SaveSystem Plugin
- 项目类型：Unreal Engine 5 通用插件
- 开发语言：C++
- 最低支持版本：UE5.4+（当前工程为 UE 5.7）
- 目标平台：Windows、Android
- 文档状态：已通过 brainstorming 评审，待实现

---

## 一、设计目标与定位

开发一款**轻量级、零依赖、即插即用**的通用存档插件，为 UE 项目提供统一的数据持久化能力。遵循 UE 原生开发习惯，不改变用户工程架构，不要求继承特定基类，不依赖 Gameplay Framework 子系统以外的东西、GAS 或第三方插件。

### 1.1 插件负责

- 存档读写（基于 UE 原生 `USaveGame` + `UGameplayStatics::SaveGameToSlot`）
- 存档槽管理
- SaveGame 数据管理
- Actor 状态保存与恢复（地图原生 Actor + 运行时 Spawn Actor）
- Blueprint 接口

### 1.2 插件不负责（本期 V1.0，YAGNI）

- 云存档、网络同步、数据加密、数据压缩、世界分块保存、大型 MMO 数据管理
- 自动数据迁移（仅留钩子）
- 异步保存（本期同步达成性能目标，异步留后续扩展）

---

## 二、关键架构决策（brainstorming 已确认）

| # | 决策点 | 结论 | 理由 |
|---|--------|------|------|
| 1 | Serializer 路线 | **复用原生** UE `SaveGameToSlot` 反射，不写反射序列化器 | UE 原生已自动反射序列化 UPROPERTY(SaveGame) 字段（含嵌套 Struct/数组/Map/Set），自研与原生重复、违反"轻量零依赖" |
| 2 | Manager 形态 | **纯静态函数库** `USaveSystemBPLibrary` | 最零侵入、即插即用；自动存档时基由游戏层提供 |
| 3 | 数据承载 | **基类 + 客户端接口**：`USaveSystemSaveGame` 基类 + `ISaveSystemClient` | 与现有 QuestSaveData.h 完全解耦，领域数据保持强类型 |
| 4 | Actor 状态获取 | **Actor 自管**：SaveableComponent 回调让 Actor 自决存/取 | 面向对象、职责清晰，用户控制粒度，零侵入 |
| 5 | 地图 Actor 范围 | **仅 SaveableComponent 注册者**（含地图原生与运行时 Spawn） | 扫描快、可控，符合"用户自管状态"路线 |
| 6 | GUID 机制 | **方案 A**：地图 Actor 路径派生（免持久化），运行时 Actor `NewGuid` 随存档持久化 | 纯静态库约束下唯一自洽方案，地图 Actor GUID 免持久化、免冲突 |

---

## 三、总体架构

Manager + Component + Serializer 三层架构，但 Serializer 因"复用原生"决策而**取消独立模块**，逻辑内联进 BPLibrary 的 Save/Load 流程（避免空壳模块）。模块数从文档原 5 个降为 4 个实体类 + 1 接口，更贴合"轻量"。

### 3.1 模块映射

| 文档原模块 | 实现落点 | 说明 |
|---|---|---|
| SaveManager | `USaveSystemBPLibrary`（静态库） | 唯一入口，调度 Save/Load/Delete/枚举/自动存档 |
| SaveableComponent | `USaveableComponent` | 自动注册/注销/GUID/Save/Load 回调 |
| Serializer | **取消独立模块**，逻辑内联进 BPLibrary | 复用原生反射，无需独立序列化器 |
| SlotManager | `FSaveSlotMetadata` + BPLibrary 槽管理函数 | 槽元数据结构 + 增删查改 |
| RuntimeActorManager | 内联进 BPLibrary 的 Load 流程（Spawn+恢复） | 运行时 Actor 记录存在 SaveGame 基类字段里 |

### 3.2 目录结构

```
Plugins/SaveSystem/
├── SaveSystem.uplugin
├── Source/SaveSystem/
│   ├── SaveSystem.Build.cs
│   ├── Public/
│   │   ├── SaveSystem.h                      # 模块声明
│   │   ├── SaveSystemSaveGame.h              # USaveGame 基类(插槽元数据+Actor记录)
│   │   ├── SaveSystemSlotMeta.h              # FSaveSlotMetadata 插槽元数据结构
│   │   ├── SaveableComponent.h               # UActorComponent 注册+回调
│   │   ├── ISaveSystemClient.h               # 领域数据注入/取出接口
│   │   ├── ISaveableActor.h                  # Actor 状态回调接口(C++ 通道)
│   │   ├── SaveSystemBPLibrary.h             # 静态函数库(唯一入口)
│   │   └── SaveSystemTypes.h                 # 枚举/错误码等公共类型
│   └── Private/
│       ├── SaveSystem.cpp                    # 模块实现
│       ├── SaveSystemBPLibrary.cpp           # 静态函数库实现
│       ├── SaveableComponent.cpp
│       ├── SaveSystemSaveGame.cpp
│       └── SaveSystemLog.h                   # 日志宏
```

---

## 四、SaveGame 基类与数据承载

### 4.1 核心类 `USaveSystemSaveGame`

继承 `USaveGame`，承载三类数据：插槽元数据、运行时 Actor 记录、领域数据注入位。

```cpp
UCLASS(BlueprintType, Blueprintable)
class USaveSystemSaveGame : public USaveGame {
    GENERATED_BODY()

public:
    // 插槽元数据(每个存档文件固定一份)
    UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Slot")
    FSaveSlotMetadata SlotMeta;

    // 运行时 Actor 记录(Spawn 出来的对象,跨会话恢复用)
    UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|RuntimeActor")
    TArray<FRuntimeActorRecord> RuntimeActorRecords;

    // 地图原生 Actor 的状态记录(按路径 GUID 索引)
    UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|LevelActor")
    TArray<FLevelActorRecord> LevelActorRecords;

    // 领域数据注入区:每个实现 ISaveSystemClient 的对象以 ClientName 为键存入自定义子对象
    // 利用 UE 原生 SaveGame 反射 + Instanced 子对象机制,领域数据保持强类型
    UPROPERTY(SaveGame, BlueprintReadWrite, Category="SaveSystem|Client")
    TMap<FName, TObjectPtr<USaveSystemSaveGameData>> ClientData;
};
```

### 4.2 插槽元数据 `FSaveSlotMetadata`

```cpp
USTRUCT(BlueprintType)
struct FSaveSlotMetadata {
    GENERATED_BODY()

    FString SlotName;          // 存档槽名(主键)
    FDateTime CreateTime;      // 创建时间
    FDateTime LastSaveTime;    // 最后保存时间
    FString EngineVersion;     // 引擎版本(兼容性检查)
    FString PluginVersion;     // 插件版本
    float PlayTime = 0.f;      // 累计游戏时长(秒)
    FString LevelName;         // 关卡名(加载时校验)
    FText DisplayName;         // 显示名(供 UI)
    FString ThumbnailPath;     // 缩略图路径(可选,留空忽略)
};
```

### 4.3 领域数据载体 `USaveSystemSaveGameData`

```cpp
// 领域数据的抽象载体,领域系统继承它放自己的 SaveGame 字段
// 例:Quest 系统继承出 UQuestSaveGameData,内含 FQuestSaveData QuestData 字段
UCLASS(Abstract, BlueprintType, Blueprintable)
class USaveSystemSaveGameData : public UObject {
    GENERATED_BODY()
};
```

### 4.4 客户端接口 `ISaveSystemClient`

```cpp
UINTERFACE(BlueprintType)
class USaveSystemClient : public UInterface {
    GENERATED_BODY()
};

class ISaveSystemClient {
    GENERATED_BODY()
public:
    // 保存时:返回本客户端的领域数据子对象(插件写入 ClientData)
    virtual USaveSystemSaveGameData* GatherSaveData_Implementation() { return nullptr; }
    // 加载时:插件从 ClientData 取出本客户端的子对象,交给领域系统恢复
    virtual void ApplySaveData_Implementation(USaveSystemSaveGameData* Data) {}
    // 客户端唯一标识(用作 ClientData 的键)
    virtual FName GetClientName_Implementation() const { return NAME_None; }
};
```

### 4.5 与 QuestSaveData.h 的边界（关键解耦点）

```
领域系统(如 Quest)侧:
  FQuestSaveData  ← 纯数据结构,不变,继续是 USTRUCT + SaveGame 标记
       │
       │ 封装进
       ▼
  UQuestSaveGameData : USaveSystemSaveGameData   ← 领域系统新增的薄封装
       │  UPROPERTY(SaveGame) FQuestSaveData QuestData;
       │
       │ 通过 ISaveSystemClient 注入
       ▼
插件侧:
  USaveSystemSaveGame::ClientData["Quest"] = UQuestSaveGameData 实例
```

- **领域数据强类型**：`UQuestSaveGameData` 继承基类后，`FQuestSaveData` 作为 `SaveGame` 字段被 UE 原生反射自动序列化，无需手写序列化，与现有 `QuestSaveData.h` 零改动。
- **解耦**：`QuestSaveData.h` 不依赖插件，插件不依赖 QuestSystem，二者通过 `ISaveSystemClient` + `USaveSystemSaveGameData` 这对抽象耦合点协作。

### 4.6 数据流

- **保存**：BPLibrary 遍历已注册 `ISaveSystemClient` → 调 `GatherSaveData()` 收集 → 存入 `ClientData[ClientName]` → 同时遍历 SaveableComponent 收集 Actor 记录 → 填 `RuntimeActorRecords`/`LevelActorRecords` → `UGameplayStatics::SaveGameToSlot`
- **加载**：`LoadGameFromSlot` → 反序列化出 `USaveSystemSaveGame` → 遍历已注册 `ISaveSystemClient` → 调 `ApplySaveData()` 注入领域数据 → 按 `RuntimeActorRecords` Spawn+恢复运行时 Actor → 按 `LevelActorRecords` 恢复地图 Actor

---

## 五、SaveableComponent 与 Actor 状态保存/恢复

### 5.1 `USaveableComponent`

继承 `UActorComponent`，挂任何 Actor，零侵入（无需改父类）。

**职责**：
1. 自动注册/注销到全局注册表（Component 生命周期驱动）
2. 提供 GUID（地图 Actor 路径派生 / 运行时 Actor NewGuid，按方案 A）
3. 提供保存/恢复回调入口

```cpp
UCLASS(ClassGroup=SaveSystem, meta=(BlueprintSpawnableComponent))
class USaveableComponent : public UActorComponent {
    GENERATED_BODY()

public:
    // === GUID ===
    // 地图 Actor:由 GetOwner()->GetPathName() 派生(OnRegister 时计算并缓存)
    // 运行时 Actor:首次需求时 NewGuid 生成,随存档记录持久化
    FGuid GetSaveGUID() const;

    // 运行时 Spawn 标记(运行时由 BPLibrary 在 Spawn 恢复时设置)
    bool IsRuntimeSpawned() const { return bRuntimeSpawned; }

protected:
    UPROPERTY()
    bool bRuntimeSpawned = false;

    // === Actor 状态回调(Actor 实现 ISaveableActor 或重写 BP 事件) ===
    FActorSaveState GatherActorState();
    void ApplyActorState(const FActorSaveState& State);
};
```

### 5.2 Actor 状态结构 `FActorSaveState`

Actor 自管，Actor 决定存什么：

```cpp
USTRUCT(BlueprintType)
struct FActorSaveState {
    GENERATED_BODY()

    // 通用基础(插件默认采集,所有 Actor 都有)
    FTransform Transform;          // 位置/旋转/缩放
    uint8 bHidden = 0;             // 可见性
    uint8 bActorIsPaused = 0;      // (预留)

    // 自定义状态:Actor 自己决定存什么,以字符串键值对存
    // 简单场景用 FString 足够;复杂可扩展为 bytes
    TMap<FString, FString> CustomFields;
};
```

### 5.3 Actor 提供状态的方式——双通道

**通道 1：C++ 接口 `ISaveableActor`**（Actor 实现）

```cpp
UINTERFACE(BlueprintType)
class USaveableActor : public UInterface { GENERATED_BODY() };

class ISaveableActor {
    GENERATED_BODY()
public:
    virtual void GatherSaveFields_Implementation(TMap<FString,FString>& OutFields) {}
    virtual void ApplySaveFields_Implementation(const TMap<FString,FString>& Fields) {}
};
```

**通道 2：Blueprint 事件**（SaveableComponent 上暴露 `BlueprintNativeEvent`，BP 用户在组件细节面板或 Actor 内重写）

C++ 默认实现：若 Owner 实现 `ISaveableActor` 则走接口，否则只存 Transform。

### 5.4 Actor 记录结构

```cpp
// 运行时 Actor 记录(需 Spawn 重建)
USTRUCT(BlueprintType)
struct FRuntimeActorRecord {
    GENERATED_BODY()
    FGuid GUID;                       // 方案 A:随存档持久化的 GUID
    TSoftClassPtr<AActor> ActorClass; // Spawn 用(软引用,跨关卡安全)
    FTransform Transform;             // Spawn 初始 Transform
    FActorSaveState State;            // 完整状态(含自定义字段)
    FName LevelName;                  // 所属关卡(多关卡校验)
};

// 地图 Actor 记录(已存在于世界中,只需恢复状态)
USTRUCT(BlueprintType)
struct FLevelActorRecord {
    GENERATED_BODY()
    FGuid GUID;                       // 方案 A:路径派生,加载时按此定位世界中 Actor
    FActorSaveState State;
};
```

### 5.5 Actor 部分流程

**保存**：
```
BPLibrary::Save()
  → 遍历全局注册表的所有 USaveableComponent
  → 对每个组件:
      guid = comp->GetSaveGUID()
      state = comp->GatherActorState()   // 走接口/BP 事件收集
      if (comp->IsRuntimeSpawned())      → 进 RuntimeActorRecords
      else                                → 进 LevelActorRecords
```

**加载**：
```
BPLibrary::Load()
  → 恢复运行时 Actor:
      for rec in RuntimeActorRecords:
        actor = World->SpawnActor(rec.ActorClass.Get(), rec.Transform)
        comp = actor->FindComponentByClass<USaveableComponent>()
        comp->bRuntimeSpawned = true
        comp->ApplyActorState(rec.State)
  → 恢复地图 Actor:
      for rec in LevelActorRecords:
        actor = 按 GUID(路径派生) 在当前世界查找同名 Actor
        if actor: actor上的comp->ApplyActorState(rec.State)
        else: 记警告(Actor 不存在,可能关卡变了)
```

### 5.6 关键设计点

1. **GUID 派生 vs 持久化分离**：地图 Actor GUID 免存储（路径算），运行时 Actor GUID 随记录存储——方案 A 的核心。
2. **双通道**（C++ 接口 + BP 事件）：覆盖两类用户，默认实现降级为"只存 Transform"。
3. **`CustomFields` 用 `TMap<FString,FString>`**：轻量通用，Actor 自管语义。后续需 bytes 可平滑升级。
4. **`TSoftClassPtr`**：运行时 Actor 的类用软引用，避免硬引用加载膨胀，跨关卡安全。

---

## 六、GUID 设计（方案 A）

所有参与存档的对象均拥有唯一 GUID。GUID 为唯一索引，插件所有恢复操作均基于 GUID 完成。

- **地图原生 Actor**：GUID 由其**放置路径**派生（`GetPathName()`，形如 `PersistentLevel.MyActor_3`，关卡内稳定且跨会话不变）。无需任何持久化存储——同一关卡同一 Actor 路径永远算出同一 GUID。零额外存储、零 GUID 冲突风险。
- **运行时 Spawn Actor**：GUID 在 Spawn 时生成（`FGuid::NewGuid()`），随存档数据持久化（记录在 RuntimeActorManager 的记录里）。下次加载时按记录里存的 GUID 重建。
- **恢复时**：地图 Actor 按路径 GUID 直接定位世界中已存在的同名 Actor；运行时 Actor 按记录里存的 GUID+Class+Transform Spawn。
- **GUID 不允许修改**：派生/生成后缓存，仅可读。

### 6.1 路径派生的注意事项

- `GetPathName()` 在关卡流式加载场景需注意（但本插件不涉及世界分块，可接受）。
- `OnRegister` 时计算一次缓存进成员，`GetSaveGUID()` 直接返回缓存，不重复算字符串哈希。

---

## 七、静态函数库 API 与流程

### 7.1 `USaveSystemBPLibrary`

继承 `UBlueprintFunctionLibrary`，唯一入口，全部 `static`。所有函数返回 `bool Success` + `FText ErrorMessage`。

```cpp
UCLASS()
class USaveSystemBPLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:
    // === 存档槽管理 ===
    UFUNCTION(BlueprintCallable, Category="SaveSystem",
        meta=(WorldContext="WorldContextObject"))
    static bool SaveGame(UObject* WorldContextObject, const FString& SlotName,
                         bool bOverride, FText& OutErrorMessage);

    UFUNCTION(BlueprintCallable, Category="SaveSystem",
        meta=(WorldContext="WorldContextObject"))
    static bool LoadGame(UObject* WorldContextObject, const FString& SlotName,
                         FText& OutErrorMessage);

    UFUNCTION(BlueprintCallable, Category="SaveSystem")
    static bool DeleteSlot(const FString& SlotName, FText& OutErrorMessage);

    UFUNCTION(BlueprintCallable, Category="SaveSystem")
    static bool CreateSlot(const FString& SlotName, const FText& DisplayName,
                           FText& OutErrorMessage);

    UFUNCTION(BlueprintCallable, Category="SaveSystem")
    static bool DoesSlotExist(const FString& SlotName);

    UFUNCTION(BlueprintCallable, Category="SaveSystem")
    static void GetSlotList(TArray<FSaveSlotMetadata>& OutSlots);

    // === 快速存档 ===
    UFUNCTION(BlueprintCallable, Category="SaveSystem",
        meta=(WorldContext="WorldContextObject"))
    static bool QuickSave(UObject* WorldContextObject, FText& OutErrorMessage);

    UFUNCTION(BlueprintCallable, Category="SaveSystem",
        meta=(WorldContext="WorldContextObject"))
    static bool QuickLoad(UObject* WorldContextObject, FText& OutErrorMessage);

    // === 自动存档(时基由游戏层提供) ===
    UFUNCTION(BlueprintCallable, Category="SaveSystem",
        meta=(WorldContext="WorldContextObject"))
    static bool AutoSave(UObject* WorldContextObject, FText& OutErrorMessage);

    // === 领域客户端注册 ===
    UFUNCTION(BlueprintCallable, Category="SaveSystem")
    static void RegisterSaveClient(UObject* Client);

    UFUNCTION(BlueprintCallable, Category="SaveSystem")
    static void UnregisterSaveClient(UObject* Client);

    // === 委托(广播事件,BP 可绑定) ===
    static FOnSaveComplete& OnSaveComplete();
    static FOnLoadComplete& OnLoadComplete();
    static FOnVersionMismatch& OnVersionMismatch();
};
```

### 7.2 自动存档调度说明

纯静态库无 Tick、无自身定时器，**时基由游戏层提供**，插件提供"被调用即存"的 `AutoSave` 节点：

- 游戏层在 GameMode/PlayerController 的 Tick 或定时器里按间隔调用 `USaveSystemBPLibrary::AutoSave()`。
- `AutoSave` 内部：维护一个静态滚动序号（槽名形如 `AutoSave_001`），保留最近 N 个（N 可配，默认 3），超出则删最旧。
- **为何不放插件内定时器**：纯静态库要持有定时器必须依附某个 World，会破坏"零侵入"和"无状态"承诺；让游戏层控时基更灵活（可关联游戏事件如战斗结束、关卡切换）。

### 7.3 Save 完整流程

```
SaveGame(WorldCtx, SlotName, bOverride)
 │
 ├─ 前置检查:World 有效? SlotName 非空? 槽存在且 !bOverride → 返回错误
 ├─ 取/建 USaveSystemSaveGame 对象(已存在则复用其元数据,保留 CreateTime)
 ├─ 更新元数据:LastSaveTime=now, EngineVersion, PluginVersion, LevelName, PlayTime(累加)
 │
 ├─ 收集运行时 Actor(遍历全局 SaveableComponent 注册表,bRuntimeSpawned==true)
 │    └─ GUID(NewGuid 持久化) + ActorClass(TSoftClassPtr) + Transform + State
 │       → RuntimeActorRecords(整体替换)
 ├─ 收集地图 Actor(遍历注册表,bRuntimeSpawned==false)
 │    └─ GUID(路径派生) + State → LevelActorRecords(整体替换)
 │
 ├─ 收集领域数据(遍历已注册 ISaveSystemClient)
 │    └─ client->GetClientName() 为键, client->GatherSaveData() 为值
 │       → ClientData(整体替换)
 │
 ├─ 序列化:UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, 0)
 │    └─ 失败 → 记日志 + 返回错误码(权限/磁盘)
 └─ 广播 OnSaveComplete(SlotName, bSuccess)
```

### 7.4 Load 完整流程

```
LoadGame(WorldCtx, SlotName)
 │
 ├─ 前置检查:World 有效? 槽存在? → 不存在返回错误
 ├─ UGameplayStatics::LoadGameFromSlot(SlotName, 0)
 │    └─ 失败/返回 null → 记日志 + 返回错误码(文件损坏)
 ├─ 版本兼容检查:EngineVersion/PluginVersion 与当前不符 → 记警告(不阻断,留迁移钩子)
 │
 ├─ 关卡校验:SlotMeta.LevelName 与当前关卡不符 → 记警告(允许跨关卡,Actor 可能找不到)
 │
 ├─ 恢复运行时 Actor:
 │    for rec in RuntimeActorRecords:
 │      actor = World->SpawnActor(rec.ActorClass.Get(), rec.Transform)
 │      comp = actor->FindComponentByClass<USaveableComponent>()
 │      if comp: comp->bRuntimeSpawned=true; comp->ApplyActorState(rec.State)
 │      else: 记警告(Spawn 的 Actor 无 SaveableComponent,跳过状态恢复)
 │
 ├─ 恢复地图 Actor(加载时建一次性哈希表 TMap<FGuid,AActor*> 加速查找):
 │    for rec in LevelActorRecords:
 │      actor = guidToActorMap[rec.GUID]
 │      if actor: actor上的comp->ApplyActorState(rec.State)
 │      else: 记警告(Actor 不存在)
 │
 ├─ 注入领域数据(遍历已注册 ISaveSystemClient)
 │    └─ data = SaveGame->ClientData[client->GetClientName()]
 │       client->ApplySaveData(data)
 │
 └─ 广播 OnLoadComplete(SlotName, bSuccess)
```

### 7.5 委托

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSaveComplete, FString, SlotName, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoadComplete, FString, SlotName, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnVersionMismatch, FString, SlotName, FString, SavedVersion, FString, CurrentVersion);

// 暴露为静态属性访问(BP 绑定用)
static FOnSaveComplete& OnSaveComplete();
static FOnLoadComplete& OnLoadComplete();
static FOnVersionMismatch& OnVersionMismatch();
```

纯静态库的委托用静态实例承载，生命周期与进程一致——存档操作频率低，无需担心泄漏。

---

## 八、异常处理

### 8.1 统一原则

插件任何情况下不得导致游戏崩溃；所有错误记日志 + 返回错误码（`FText ErrorMessage` + `bool Success`）。

### 8.2 错误码枚举

```cpp
UENUM(BlueprintType)
enum class ESaveSystemError : uint8 {
    None = 0,              // 成功
    InvalidWorld,          // WorldContext 无效
    InvalidSlotName,       // 槽名为空或非法
    SlotNotFound,          // 槽不存在(加载/删除时)
    SlotAlreadyExists,     // 槽已存在且未允许覆盖
    WriteFailed,           // 磁盘写入失败(权限/空间)
    ReadFailed,            // 读取失败(文件损坏)
    VersionMismatch,       // 版本不兼容
    LevelMismatch,         // 关卡不匹配(警告级,不阻断)
    NoSaveableComponents,  // 无可保存对象(警告级,允许空存档)
};
```

### 8.3 各场景处理

| 场景 | 处理 | 是否阻断 |
|---|---|---|
| WorldContext 无效 | 返回 `InvalidWorld` | 阻断 |
| 槽名为空 | 返回 `InvalidSlotName` | 阻断 |
| 保存时槽已存在且 `bOverride=false` | 返回 `SlotAlreadyExists` | 阻断 |
| `SaveGameToSlot` 返回 false | 记 `UE_LOG(Error)`，返回 `WriteFailed` | 阻断 |
| `LoadGameFromSlot` 返回 null | 记 `UE_LOG(Error)`，返回 `ReadFailed` | 阻断 |
| 文件存在但反序列化后字段异常 | 记警告，尽力恢复，不崩溃 | 不阻断 |
| 版本不符 | 记警告，**不阻断**，留迁移钩子 | 不阻断 |
| 关卡不匹配 | 记警告，**不阻断**（允许跨关卡，Actor 可能找不到） | 不阻断 |
| Spawn 的运行时 Actor 无 SaveableComponent | 跳过状态恢复，记警告 | 不阻断 |
| 地图 Actor 按 GUID 找不到 | 跳过该记录，记警告 | 不阻断 |

### 8.4 日志分类

```cpp
SAVEYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogSaveSystem, Log, All);
// 用法:UE_LOG(LogSaveSystem, Warning, TEXT("..."));
```

---

## 九、版本兼容

```cpp
// SaveSystemTypes.h
USTRUCT(BlueprintType)
struct FSaveSystemVersion {
    GENERATED_BODY()
    static constexpr int32 CurrentPluginVersion = 1;
    static constexpr const TCHAR* CurrentEngineVersion = TEXT("5.7");
};
```

加载时比对：
- 完全匹配 → 直接加载
- `PluginVersion` 不同 → 记警告，留 `OnVersionMismatch` 委托给游戏层做迁移
- `EngineVersion` 大幅差异 → 记警告，仍尝试加载

本期 V1.0 **不实现自动迁移**，只暴露钩子 + 记警告——符合"轻量"和 YAGNI。

---

## 十、性能保障

文档目标：1000 Saveable Actor，扫描 ≤5ms，保存 ≤20ms，加载 ≤30ms，无 Tick/无持续分配/无循环查询。

### 10.1 保障措施

1. **全局注册表用 `TArray<TObjectPtr<USaveableComponent>>`**：注册时 Add，注销时 RemoveSwap，O(1) 移除。Save 时单次线性遍历，无嵌套查询。1000 个 = 1000 次迭代，远低于 5ms。
2. **地图 Actor GUID 路径派生用缓存**：`OnRegister` 时算一次缓存进成员，`GetSaveGUID()` 直接返回缓存。
3. **加载时地图 Actor 查找用一次性哈希表**：加载流程开头建 `TMap<FGuid, AActor*>`，后续 O(1) 查找，避免对每条记录都全世界扫描。
4. **SaveGame 对象复用**：同槽再次保存时复用已加载的 `USaveSystemSaveGame` 对象，避免重复构造。`ClientData`/`Records` 数组整体 `Reset()` 后重填，复用底层分配。
5. **无 Tick**：纯静态库 + 无 TickComponent 的 Component。
6. **无持续内存分配**：所有临时容器随调用栈创建销毁，无驻留缓冲。
7. **异步可选（本期不做）**：UE 原生 `SaveGameToSlot`/`LoadGameFromSlot` 是同步的。文档性能目标量级下同步足够。真正的异步保存作为后续扩展，本期 YAGNI。

### 10.2 性能风险点

`SaveGameToSlot` 的磁盘 IO 同步阻塞是主要耗时，但 1000 Actor 量级的数据量（估算 < 1MB）在 SSD 上远低于 20ms，可达成目标。若实测超标，后续切异步。

---

## 十一、模块依赖

```csharp
// SaveSystem.Build.cs
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine",
    // 无 GameplayAbilities/GAS,无 StructUtils,无第三方 —— 零依赖
});
```

仅依赖 UE 核心模块（Core/CoreUObject/Engine），不依赖 GameplayFramework 子系统以外的任何插件，与文档"不依赖 GAS 或其他第三方插件"一致。

---

## 十二、.uplugin 清单

```json
{
    "FileVersion": 3,
    "Version": 1,
    "VersionName": "1.0",
    "LiveCodingModule": true,
    "IsBetaVersion": true,
    "FriendlyName": "SaveSystem",
    "Description": "轻量级通用存档插件 - 存档读写/槽管理/Actor 状态保存恢复,零业务依赖",
    "Category": "Other",
    "CreatedBy": "一氧化二氢",
    "CanContainContent": true,
    "Modules": [
        { "Name": "SaveSystem", "Type": "Runtime", "LoadingPhase": "Default" }
    ]
}
```

---

## 十三、关键数据结构汇总

| 结构 | 类型 | 用途 |
|---|---|---|
| `FSaveSlotMetadata` | USTRUCT | 插槽元数据 |
| `USaveSystemSaveGameData` | UCLASS(Abstract) | 领域数据载体基类 |
| `USaveSystemSaveGame` | UCLASS(USaveGame 派生) | 存档对象（含元数据+Actor记录+领域数据） |
| `FActorSaveState` | USTRUCT | Actor 状态（Transform+自定义字段） |
| `FRuntimeActorRecord` | USTRUCT | 运行时 Actor 记录 |
| `FLevelActorRecord` | USTRUCT | 地图 Actor 记录 |
| `ESaveSystemError` | UENUM | 错误码 |
| `FSaveSystemVersion` | USTRUCT | 版本常量 |
| `USaveableComponent` | UCLASS(UActorComponent 派生) | 注册+GUID+回调 |
| `ISaveSystemClient` | UINTERFACE | 领域数据注入接口 |
| `ISaveableActor` | UINTERFACE | Actor 状态回调接口 |
| `USaveSystemBPLibrary` | UCLASS(UBlueprintFunctionLibrary 派生) | 唯一入口 |
| `FOnSaveComplete`/`FOnLoadComplete`/`FOnVersionMismatch` | 委托 | 广播事件 |

---

## 十四、不在本期范围

- 云存档、网络同步、数据加密、数据压缩、世界分块保存、大型 MMO 数据管理
- 自动数据迁移逻辑（仅留 `OnVersionMismatch` 钩子）
- 异步保存（后台线程）
- 缩略图生成（仅留 `ThumbnailPath` 字段占位）
- 多关卡流式加载的 GUID 冲突处理（本期单关卡场景，留待后续）

---

## 附录：与现有 QuestSaveData.h 的关系

现有 `Plugins/QuestSystem/.../QuestSaveData.h` 定义纯领域数据结构（`FQuestSaveData` 等，全部 `UPROPERTY(SaveGame)` 标记），通过 `QuestComponent::SaveToData()/LoadFromData()` 负责自身序列化，注释明确"游戏层负责持久化"。

本插件正是提供该"持久化"基础设施：

- `QuestSaveData.h` **零改动**，继续作为纯数据结构存在。
- Quest 系统接入本插件时，只需：①新增薄封装 `UQuestSaveGameData : USaveSystemSaveGameData`（内含 `UPROPERTY(SaveGame) FQuestSaveData QuestData;`）；②让承载 QuestComponent 的对象实现 `ISaveSystemClient`，在 `GatherSaveData` 里调 `QuestComponent::SaveToData()` 并装入 `UQuestSaveGameData`，在 `ApplySaveData` 里调 `QuestComponent::LoadFromData()`。
- 两插件通过抽象耦合点协作，无源码依赖。
