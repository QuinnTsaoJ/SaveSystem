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

	/** 保存到指定槽(覆盖式)。自动收集所有 SaveableComponent + ISaveSystemClient 数据。
	 *  @param WorldContextObject 世界上下文(用于定位当前 World)
	 *  @param SlotName 存档槽名
	 *  @param bOverride 槽已存在时是否覆盖
	 *  @param OutErrorMessage 失败时返回错误文本
	 *  @return 是否保存成功 */
	UFUNCTION(BlueprintCallable, Category="SaveSystem",
		meta=(WorldContext="WorldContextObject"))
	static bool SaveGame(UObject* WorldContextObject, const FString& SlotName,
		bool bOverride, FText& OutErrorMessage);

	/** 从指定槽加载。恢复 Actor + 注入领域数据 + 广播完成事件。
	 *  @param WorldContextObject 世界上下文
	 *  @param SlotName 存档槽名
	 *  @param OutErrorMessage 失败时返回错误文本
	 *  @return 是否加载成功 */
	UFUNCTION(BlueprintCallable, Category="SaveSystem",
		meta=(WorldContext="WorldContextObject"))
	static bool LoadGame(UObject* WorldContextObject, const FString& SlotName,
		FText& OutErrorMessage);

	/** 删除存档槽。
	 *  @param SlotName 存档槽名
	 *  @param OutErrorMessage 失败时返回错误文本
	 *  @return 是否删除成功 */
	UFUNCTION(BlueprintCallable, Category="SaveSystem")
	static bool DeleteSlot(const FString& SlotName, FText& OutErrorMessage);

	/** 创建空槽(只写元数据,用于"新建存档"场景)。
	 *  @param SlotName 存档槽名
	 *  @param DisplayName 显示名(供 UI)
	 *  @param OutErrorMessage 失败时返回错误文本
	 *  @return 是否创建成功 */
	UFUNCTION(BlueprintCallable, Category="SaveSystem")
	static bool CreateSlot(const FString& SlotName, const FText& DisplayName,
		FText& OutErrorMessage);

	/** 查询槽是否存在。 */
	UFUNCTION(BlueprintCallable, Category="SaveSystem")
	static bool DoesSlotExist(const FString& SlotName);

	/** 枚举所有槽的元数据(供存档列表 UI)。 */
	UFUNCTION(BlueprintCallable, Category="SaveSystem")
	static void GetSlotList(TArray<FSaveSlotMetadata>& OutSlots);

	// === 快速存档 ===

	/** 快捷保存(固定槽名 QuickSave,覆盖式)。 */
	UFUNCTION(BlueprintCallable, Category="SaveSystem",
		meta=(WorldContext="WorldContextObject"))
	static bool QuickSave(UObject* WorldContextObject, FText& OutErrorMessage);

	/** 快捷读取(固定槽名 QuickSave)。 */
	UFUNCTION(BlueprintCallable, Category="SaveSystem",
		meta=(WorldContext="WorldContextObject"))
	static bool QuickLoad(UObject* WorldContextObject, FText& OutErrorMessage);

	// === 自动存档(时基由游戏层提供) ===

	/** 手动触发一次自动存档(写入自动存档槽,按序号滚动,保留最近 3 个)。
	 *  游戏层应在定时器/事件中调用本函数实现自动存档调度。 */
	UFUNCTION(BlueprintCallable, Category="SaveSystem",
		meta=(WorldContext="WorldContextObject"))
	static bool AutoSave(UObject* WorldContextObject, FText& OutErrorMessage);

	// === 领域客户端注册 ===

	/** 注册领域客户端(供 GameMode/PlayerController 在初始化时登记领域系统)。 */
	UFUNCTION(BlueprintCallable, Category="SaveSystem")
	static void RegisterSaveClient(UObject* Client);

	/** 注销领域客户端。 */
	UFUNCTION(BlueprintCallable, Category="SaveSystem")
	static void UnregisterSaveClient(UObject* Client);

	// === 委托访问(BP 绑定用,静态实例承载) ===

	/** 保存完成事件。参数:SlotName 槽名,bSuccess 是否成功。保存流程结束(成功/失败)时广播。 */
	static FOnSaveComplete& OnSaveComplete();
	/** 加载完成事件。参数:SlotName 槽名,bSuccess 是否成功。加载流程结束(成功/失败)时广播。 */
	static FOnLoadComplete& OnLoadComplete();
	/** 版本不匹配事件。参数:SlotName 槽名,SavedVersion 存档版本,CurrentVersion 当前版本。
	 *  加载时检测到插件版本不匹配触发(不阻断加载),游戏层可监听做数据迁移。 */
	static FOnVersionMismatch& OnVersionMismatch();

	// === 内部接口(SaveableComponent 注册/注销用,非 Blueprint) ===
	static void RegisterSaveableComponent(USaveableComponent* Comp);
	static void UnregisterSaveableComponent(USaveableComponent* Comp);

private:
	// 把错误码转为本地化错误文本
	static FText ErrorToText(ESaveSystemError Error);
};
