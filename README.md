# SaveSystem Plugin

> 轻量级、零依赖、即插即用的 Unreal Engine 5 通用存档插件

为 UE 项目提供统一的存档读写、槽管理、Actor 状态保存恢复、领域数据注入能力。遵循 UE 原生开发习惯，不改变用户工程架构，不要求继承特定基类。

---

## 特性

- **零依赖** — 仅依赖 UE 核心模块（Core / CoreUObject / Engine），不依赖 GAS、GameplayTags 或任何第三方插件
- **零侵入** — 任何 Actor 挂上 `USaveableComponent` 即参与存档，无需继承特定基类
- **复用原生** — 基于 UE 原生 `USaveGame` + `UGameplayStatics::SaveGameToSlot`，不自研序列化器
- **强类型数据** — 领域数据（背包/任务/养成）通过 `ISaveSystemClient` 接口注入，保持强类型，零字符串转换
- **多存档槽** — 支持任意数量的存档槽，含元数据（时间/版本/关卡/显示名）
- **运行时 Actor 恢复** — 运行时 Spawn 的 Actor 自动保存 Class + Transform + 状态，加载时自动重建
- **Blueprint 全支持** — 所有 API 均为 `BlueprintCallable`，BP 用户无需写 C++

---

## 支持版本

- UE 5.4+（当前工程基于 5.7）
- Windows / Android（理论支持所有 UE 支持的平台）

---

## 安装

将 `SaveSystem` 文件夹放入项目的 `Plugins/` 目录，或在编辑器插件管理器中启用。

`Inventory.uproject` 已包含插件引用：

```json
"Plugins": [
  { "Name": "SaveSystem", "Enabled": true }
]
```

---

## 快速开始

### 1. 让 Actor 参与存档

在 Actor 上挂 `USaveableComponent`：

- **C++**：`SaveableComp = CreateDefaultSubobject<USaveableComponent>(TEXT("SaveableComp"));`
- **蓝图**：Components 面板 → Add → 搜 `SaveableComponent`

挂上后，Actor 的 **Transform 和可见性**会被自动采集与恢复。

### 2. 存档 / 读档

**蓝图**：搜节点 `Save Game` / `Load Game`（SaveSystem 分类）

**C++**：

```cpp
#include "SaveSystemBPLibrary.h"

// 保存
FText Err;
USaveSystemBPLibrary::SaveGame(this, TEXT("MySlot"), true, Err);

// 加载
USaveSystemBPLibrary::LoadGame(this, TEXT("MySlot"), Err);

// 枚举存档列表
TArray<FSaveSlotMetadata> Slots;
USaveSystemBPLibrary::GetSlotList(Slots);
```

### 3. 存自定义数据

- **简单标量**（血量/等级/开关）：实现 `ISaveableActor` 接口，在 `GatherSaveFields` / `ApplySaveFields` 里用字符串键值对
- **Actor 复杂数据**（掉落表）：实现 `ISaveableActor` 的 `GatherActorData` / `ApplyActorData`，返回强类型 `USaveSystemSaveGameData` 子对象
- **系统级数据**（背包/任务）：实现 `ISaveSystemClient` 接口，注册后自动注入

详见 [接入与使用指南](docs/SaveSystem接入指南.md)。

---

## 架构

```
游戏层
  ├── Actor 挂 USaveableComponent ──→ 自动注册，提供状态收集/恢复回调
  ├── 领域系统实现 ISaveSystemClient ──→ 注入/取出领域数据
  └── 调用 USaveSystemBPLibrary ──→ 唯一入口（Save/Load/槽管理/快存/自动存档）
                    │
                    ▼
              USaveSystemSaveGame（存档对象）
              ├── SlotMeta            插槽元数据
              ├── LevelActorRecords   地图 Actor 状态记录
              ├── RuntimeActorRecords 运行时 Actor 记录
              └── ClientData          领域数据注入区（bytes 序列化）
```

### 核心类

| 类 | 职责 |
|---|---|
| `USaveSystemBPLibrary` | 纯静态函数库，唯一入口 |
| `USaveableComponent` | Actor 挂载的可保存组件，自动注册/GUID/状态回调 |
| `USaveSystemSaveGame` | 存档对象基类，承载元数据 + Actor 记录 + 领域数据 |
| `USaveSystemSaveGameData` | 领域数据载体抽象基类（用户继承放自己的字段） |
| `ISaveableActor` | Actor 状态回调接口（C++ 与 BP 统一通道） |
| `ISaveSystemClient` | 领域数据注入/取出接口 |

### GUID 机制

| Actor 类型 | GUID 来源 | 是否持久化 |
|---|---|---|
| 地图原生 Actor | 放置路径派生（`GetPathName()` 哈希） | 免持久化，同一路径永远算出同一 GUID |
| 运行时 Spawn Actor | `FGuid::NewGuid()` | 随存档记录持久化 |

---

## API 速查

### 存档槽管理

| 函数 | 说明 |
|---|---|
| `SaveGame(WorldCtx, SlotName, bOverride, Err)` | 保存到指定槽 |
| `LoadGame(WorldCtx, SlotName, Err)` | 从指定槽加载 |
| `DeleteSlot(SlotName, Err)` | 删除存档槽 |
| `CreateSlot(SlotName, DisplayName, Err)` | 创建空槽 |
| `DoesSlotExist(SlotName)` | 查询槽是否存在 |
| `GetSlotList(OutSlots)` | 枚举所有槽元数据 |

### 快存与自动存档

| 函数 | 说明 |
|---|---|
| `QuickSave(WorldCtx, Err)` | 固定槽名 `QuickSave`，覆盖式 |
| `QuickLoad(WorldCtx, Err)` | 从 `QuickSave` 槽读取 |
| `AutoSave(WorldCtx, Err)` | 滚动槽 `AutoSave_001`...，保留最近 3 个 |

### 事件委托

| 委托 | 触发时机 |
|---|---|
| `OnSaveComplete` | 保存流程结束（成功/失败） |
| `OnLoadComplete` | 加载流程结束（成功/失败） |
| `OnVersionMismatch` | 检测到插件版本不匹配（不阻断） |

---

## 存档文件位置

```
项目目录/Saved/SaveGames/<SlotName>.sav
```

由 UE 的 `FPaths::ProjectSavedDir()` 决定，各平台自动适配。

---

## 性能

| 指标 | 目标 |
|---|---|
| 无 Tick | SaveableComponent 不开 Tick；静态库无定时器 |
| 注册表 O(1) 注销 | `TWeakObjectPtr` + 自动清理失效条目 |
| 加载哈希表索引 | 恢复地图 Actor 时建 `TMap<GUID, Actor*>`，O(1) 查找 |
| 同步 IO | 1000 Actor 量级数据（<1MB）SSD 上 <20ms |

性能目标（1000 Saveable Actor）：扫描 ≤5ms，保存 ≤20ms，加载 ≤30ms。

---

## 常见问题

**Q：`UPROPERTY(SaveGame)` 标记的字段会被自动采集吗？**

不会。插件不反射扫描 Actor 字段，只采集 Transform/可见性 + 你在回调里手动塞入的数据。`SaveGame` 标记在子对象载体上才由 UE 原生反射自动序列化。

**Q：保存游戏时崩溃（0xc0000005）？**

注册的 SaveClient 没在 `EndPlay` 反注册，残留悬空指针。务必成对调用 `RegisterSaveClient` / `UnregisterSaveClient`。

**Q：LoadGame 成功但领域数据没恢复？**

Load 时机太早——`RegisterSaveClient` 还没调用就 Load 了。把 Load 放在 PlayerController 的 `BeginPlay` 里，在 `RegisterSaveClient` 之后调用。

更多问题详见 [接入与使用指南 FAQ](docs/SaveSystem接入指南.md#faq)。

---

## 不在本期范围（V1.0）

以下功能留待后续版本：

- 云存档、网络同步、数据加密、数据压缩
- 世界分块保存、大型 MMO 数据管理
- 自动数据迁移（仅留 `OnVersionMismatch` 钩子）
- 异步保存（后台线程）
- 缩略图生成

---

## 文档

- [接入与使用指南](docs/SaveSystem接入指南.md)
- [设计文档](docs/superpowers/specs/2026-06-27-savesystem-design.md)
- [实现计划](docs/superpowers/plans/2026-06-28-savesystem-implementation.md)

---

## 作者

一氧化二氢
