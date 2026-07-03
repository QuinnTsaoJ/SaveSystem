// 存档系统模块构建规则 - 零第三方依赖,仅依赖 UE 核心
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
